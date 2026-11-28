#include "engine/audio/analysis/AudioRemix.h"

#include "engine/audio/analysis/AudioOnsets.h"
#include "engine/audio/analysis/Stft.h"
#include "engine/decode/readers/ClipReaderPool.h"

#include <QFileInfo>
#include <QHash>
#include <QMutex>
#include <QtMath>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <memory>
#include <vector>

namespace drift {
namespace {

constexpr int kFftSize = 2048;
constexpr int kHopSize = 512;
constexpr int kChromaBins = 12;
constexpr int kTimbreBins = 8;
constexpr int kFeatureDims = kChromaBins + kTimbreBins;
constexpr int kContextBeats = 4;
constexpr double kMinRmsEnergy = 1e-4;
constexpr int kMaxCachedTracks = 16;

using FeatureVec = std::array<float, kFeatureDims>;

struct CachedTrackFeatures
{
    double bpm = 0.0;
    int beatsPerBar = 4;
    TimeUs srcDurationUs = 0;
    QList<TimeUs> beatOffsetsUs; // N + 1 boundaries within [0, srcDurationUs]
    std::vector<FeatureVec> beatFeatures; // size N
};

struct CandidateJump
{
    int fromBeat = 0; // cut occurs at boundary `fromBeat + 1` (end of beat `fromBeat`)
    int toBeat = 0;   // next segment begins at `beatOffsetsUs[toBeat]`
    double similarity = 0.0;
};

struct BeamState
{
    int segStartBeat = 0;     // 0 means source head (includes pre-beat pickup from srcInUs)
    TimeUs curDurUs = 0;
    double simSum = 0.0;
    int cutCount = 0;
    QList<MusicRemixSegment> segs;
};

TimeUs secondsToUsClamped(double seconds, TimeUs maxUs)
{
    const TimeUs t = static_cast<TimeUs>(llround(seconds * static_cast<double>(kUsPerSecond)));
    return qBound<TimeUs>(0, t, maxUs);
}

void l2NormalizeSlice(float *data, int count)
{
    double sq = 0.0;
    for (int i = 0; i < count; ++i)
        sq += static_cast<double>(data[i]) * data[i];
    if (sq <= 1e-12)
        return;
    const float inv = static_cast<float>(1.0 / std::sqrt(sq));
    for (int i = 0; i < count; ++i)
        data[i] *= inv;
}

std::vector<FeatureVec> extractBeatFeatures(const float *mono, int frameCount, int sampleRate,
                                            const QList<double> &beatsSec)
{
    const int beatCount = static_cast<int>(beatsSec.size()) - 1;
    if (beatCount <= 0 || !mono || frameCount < kFftSize || sampleRate <= 0)
        return {};

    std::vector<float> pcm(mono, mono + frameCount);
    Stft stft(kFftSize, kHopSize);
    if (!stft.ok())
        return {};

    std::vector<float> spectra;
    stft.analyze(pcm, &spectra);
    const int stftFrames = stft.frameCount(frameCount);
    const int bins = stft.bins();
    if (stftFrames <= 0 || static_cast<int>(spectra.size()) < stftFrames * bins * 2)
        return {};

    // Precompute bin -> chroma & log-band lookup tables for fast aggregation.
    std::vector<int> binChroma(static_cast<size_t>(bins), -1);
    std::vector<int> binTimbre(static_cast<size_t>(bins), -1);
    const double minFreq = 65.0;
    const double maxFreq = qMin(8000.0, 0.45 * static_cast<double>(sampleRate));
    const double logMin = std::log2(minFreq);
    const double logSpan = qMax(1.0, std::log2(maxFreq) - logMin);

    for (int k = 1; k < bins; ++k) {
        const double freq = static_cast<double>(k) * sampleRate / kFftSize;
        if (freq < minFreq || freq > maxFreq)
            continue;
        if (freq <= 2400.0) {
            const double midi = 69.0 + 12.0 * std::log2(freq / 440.0);
            int pc = static_cast<int>(llround(midi)) % kChromaBins;
            if (pc < 0)
                pc += kChromaBins;
            binChroma[static_cast<size_t>(k)] = pc;
        }
        const double normLog = (std::log2(freq) - logMin) / logSpan;
        binTimbre[static_cast<size_t>(k)] =
            qBound(0, static_cast<int>(normLog * kTimbreBins), kTimbreBins - 1);
    }

    std::vector<FeatureVec> features(static_cast<size_t>(beatCount));
    const double frameRate = static_cast<double>(sampleRate) / kHopSize;
    for (int b = 0; b < beatCount; ++b) {
        FeatureVec &vec = features[static_cast<size_t>(b)];
        vec.fill(0.0f);

        const int f0 = qBound(0, static_cast<int>(std::floor(beatsSec.at(b) * frameRate)), stftFrames - 1);
        const int f1 = qBound(f0 + 1, static_cast<int>(std::ceil(beatsSec.at(b + 1) * frameRate)), stftFrames);
        int count = 0;
        for (int f = f0; f < f1; ++f) {
            const float *frameSpec = spectra.data() + static_cast<size_t>(f) * bins * 2;
            for (int k = 1; k < bins; ++k) {
                const float re = frameSpec[k * 2];
                const float im = frameSpec[k * 2 + 1];
                const float mag = std::sqrt(re * re + im * im);
                if (const int c = binChroma[static_cast<size_t>(k)]; c >= 0)
                    vec[static_cast<size_t>(c)] += mag;
                if (const int t = binTimbre[static_cast<size_t>(k)]; t >= 0)
                    vec[static_cast<size_t>(kChromaBins + t)] += std::log1p(mag);
            }
            ++count;
        }
        if (count > 0) {
            l2NormalizeSlice(vec.data(), kChromaBins);
            l2NormalizeSlice(vec.data() + kChromaBins, kTimbreBins);
        }
    }
    return features;
}

double cosineBeatSimilarity(const FeatureVec &a, const FeatureVec &b)
{
    double cDot = 0.0;
    for (int k = 0; k < kChromaBins; ++k)
        cDot += static_cast<double>(a[static_cast<size_t>(k)]) * b[static_cast<size_t>(k)];
    double tDot = 0.0;
    for (int k = 0; k < kTimbreBins; ++k)
        tDot += static_cast<double>(a[static_cast<size_t>(kChromaBins + k)])
              * b[static_cast<size_t>(kChromaBins + k)];
    // Harmonic chroma carries 65%, spectral timbre carries 35%.
    return 0.65 * cDot + 0.35 * tDot;
}

// Evaluates how cleanly exiting after beat `i` (cut at beat boundary `i + 1`) joins into entering
// at beat `j` (cut at beat boundary `j`). Uses a 4-beat trailing/leading musical context window
// plus continuity between the outgoing continuation `i + 1` and target `j`.
double contextTransitionSimilarity(const std::vector<FeatureVec> &feats, int beatsPerBar, int i, int j)
{
    const int n = static_cast<int>(feats.size());
    if (i < 0 || i + 1 >= n || j <= 0 || j >= n)
        return -1.0;

    static constexpr double kWeights[kContextBeats] = {0.42, 0.28, 0.18, 0.12};
    double weightedSum = 0.0;
    double weightTotal = 0.0;
    for (int m = 0; m < kContextBeats; ++m) {
        const int aIdx = i - m;
        const int bIdx = (j - 1) - m;
        if (aIdx >= 0 && bIdx >= 0) {
            weightedSum += kWeights[m] * cosineBeatSimilarity(feats[static_cast<size_t>(aIdx)],
                                                              feats[static_cast<size_t>(bIdx)]);
            weightTotal += kWeights[m];
        }
    }
    // Also check forward-edge continuity: the new beat `j` should resemble `i + 1`.
    weightedSum += 0.35 * cosineBeatSimilarity(feats[static_cast<size_t>(i + 1)],
                                               feats[static_cast<size_t>(j)]);
    weightTotal += 0.35;

    double sim = weightTotal > 0.0 ? (weightedSum / weightTotal) : 0.0;
    if (beatsPerBar > 1 && ((i + 1) % beatsPerBar) == (j % beatsPerBar))
        sim += 0.15;
    return qBound(-1.0, sim, 1.0);
}

std::shared_ptr<const CachedTrackFeatures> buildFeaturesFromPcm(const float *mono, int frameCount,
                                                                int sampleRate, TimeUs srcDurationUs,
                                                                QString *errorOut)
{
    if (srcDurationUs < static_cast<TimeUs>(AudioOnsets::kMinAnalysisSec * kUsPerSecond)) {
        if (errorOut)
            *errorOut = QStringLiteral("Clip source must be at least 4.0 seconds long to remix.");
        return {};
    }
    if (!mono || frameCount < kFftSize || sampleRate <= 0) {
        if (errorOut)
            *errorOut = QStringLiteral("Clip has too little audio data to analyze.");
        return {};
    }

    double sqSum = 0.0;
    for (int i = 0; i < frameCount; ++i)
        sqSum += static_cast<double>(mono[i]) * mono[i];
    const double rms = std::sqrt(sqSum / frameCount);
    if (rms < kMinRmsEnergy) {
        if (errorOut)
            *errorOut = QStringLiteral("No rhythmic signal detected (clip is silent).");
        return {};
    }

    const AudioBeatAnalysis beatAnalysis = AudioOnsets::analyze(mono, frameCount, sampleRate, 0.0);
    if (beatAnalysis.bpm <= 0.0 || beatAnalysis.beats.size() < 6) {
        if (errorOut)
            *errorOut = QStringLiteral("No clear rhythmic beat detected for auto-remix.");
        return {};
    }

    auto out = std::make_shared<CachedTrackFeatures>();
    out->bpm = beatAnalysis.bpm;
    out->beatsPerBar = qMax(1, beatAnalysis.beatsPerBar);
    out->srcDurationUs = srcDurationUs;

    QList<double> cleanedBeats;
    cleanedBeats.reserve(beatAnalysis.beats.size());
    TimeUs lastUs = -1;
    for (double b : beatAnalysis.beats) {
        const TimeUs us = secondsToUsClamped(b, srcDurationUs);
        if (us > lastUs && us < srcDurationUs) {
            cleanedBeats.append(b);
            out->beatOffsetsUs.append(us);
            lastUs = us;
        }
    }
    if (out->beatOffsetsUs.size() < 6) {
        if (errorOut)
            *errorOut = QStringLiteral("Not enough beat boundaries inside clip range.");
        return {};
    }

    out->beatFeatures = extractBeatFeatures(mono, frameCount, sampleRate, cleanedBeats);
    if (out->beatFeatures.size() + 1 != static_cast<size_t>(out->beatOffsetsUs.size())) {
        if (errorOut)
            *errorOut = QStringLiteral("Failed to compute spectral beat features.");
        return {};
    }
    return out;
}

AudioRemixResult planFromFeatures(const CachedTrackFeatures &f, TimeUs srcInUs, TimeUs srcOutUs,
                                  const AudioRemixOptions &options)
{
    AudioRemixResult res;
    const TimeUs srcDurationUs = f.srcDurationUs;
    const TimeUs targetUs = options.targetDurationUs;
    if (targetUs <= 0) {
        res.error = QStringLiteral("Target duration must be greater than zero.");
        return res;
    }

    const int segCtrl = qBound(0, options.segments, 100);
    const int varCtrl = qBound(0, options.variations, 100);
    const int nBeats = static_cast<int>(f.beatFeatures.size()); // beatOffsetsUs has nBeats + 1 entries
    const TimeUs avgBeatUs = qMax<TimeUs>(
        150000, static_cast<TimeUs>(llround((60.0 / qMax(40.0, f.bpm)) * kUsPerSecond)));
    const TimeUs crossfadeUs = AudioRemix::kDefaultCrossfadeUs;

    res.plan.enabled = true;
    res.plan.targetDurationUs = targetUs;
    res.plan.originalDurationUs = srcDurationUs;
    res.plan.originalSrcOutUs = srcOutUs;
    res.plan.segments = segCtrl;
    res.plan.variations = varCtrl;
    res.plan.bpm = f.bpm;

    // If the target duration is already within half a beat of the original clip, preserve intact.
    if (llabs(targetUs - srcDurationUs) <= avgBeatUs / 2) {
        res.plan.plan = {MusicRemixSegment{srcInUs, srcOutUs, 0}};
        res.ok = true;
        res.actualDurationUs = srcDurationUs;
        res.avgSimilarity = 1.0;
        return res;
    }

    const auto segStartSrcUs = [&](int beatIdx) -> TimeUs {
        return beatIdx <= 0 ? srcInUs : srcInUs + f.beatOffsetsUs.at(beatIdx);
    };
    const auto segEndSrcUs = [&](int beatIdx) -> TimeUs {
        return beatIdx >= nBeats ? srcOutUs : srcInUs + f.beatOffsetsUs.at(beatIdx + 1);
    };

    // Scale section minimum length & cut count ceiling from the Segments slider (0..100).
    const double segFrac = segCtrl / 100.0;
    const int minSectionBeats = qMax(2, static_cast<int>(llround((1.0 - segFrac) * 6.0 + 2.0)));
    const int preferredCuts = qMax(1, static_cast<int>(llround(1.0 + segFrac * 5.0)));
    const int maxCuts = qBound(2, static_cast<int>(llround(2.0 + segFrac * 10.0)), 14);
    const double minSim = 0.65 - (varCtrl / 100.0) * 1.15; // 0.65 (strict) down to -0.50 (relaxed)
    const bool shortening = (targetUs < srcDurationUs);

    // Precompute strongest transition jumps out of each beat boundary `i` -> `j`.
    std::vector<std::vector<CandidateJump>> outgoing(static_cast<size_t>(nBeats));
    for (int i = 1; i + 1 < nBeats; ++i) {
        std::vector<CandidateJump> cands;
        for (int j = 1; j < nBeats; ++j) {
            if (j == i + 1)
                continue; // not a jump
            // Avoid tiny 1-2 beat stutter loops on backwards jumps and tiny skips on forwards jumps.
            if (j <= i && (i + 1 - j) < minSectionBeats)
                continue;
            if (shortening && j < i)
                continue; // shortening prefers forward progression without jarring back-loops
            const double sim = contextTransitionSimilarity(f.beatFeatures, f.beatsPerBar, i, j);
            if (sim >= minSim || cands.size() < 4)
                cands.push_back(CandidateJump{i, j, sim});
        }
        std::sort(cands.begin(), cands.end(), [](const CandidateJump &a, const CandidateJump &b) {
            return a.similarity > b.similarity;
        });
        constexpr int kKeepPerBeat = 10;
        if (static_cast<int>(cands.size()) > kKeepPerBeat)
            cands.resize(kKeepPerBeat);
        outgoing[static_cast<size_t>(i)] = std::move(cands);
    }

    // Also keep unconstrained best jumps when target is strict so we never fail on weird tempos.
    if (!shortening) {
        // Ensure we have backward jumps to extend even if `minSim` filtered some out.
        for (int i = minSectionBeats; i + 1 < nBeats; ++i) {
            if (!outgoing[static_cast<size_t>(i)].empty())
                continue;
            CandidateJump best{i, qMax(1, i - minSectionBeats), -1.0};
            for (int j = 1; j <= i - minSectionBeats; ++j) {
                const double sim = contextTransitionSimilarity(f.beatFeatures, f.beatsPerBar, i, j);
                if (sim > best.similarity)
                    best = CandidateJump{i, j, sim};
            }
            outgoing[static_cast<size_t>(i)].push_back(best);
        }
    }

    // Track the best complete plan ending smoothly at the clip's true outro (`srcOutUs`).
    bool hasBest = false;
    double bestScore = std::numeric_limits<double>::lowest();
    QList<MusicRemixSegment> bestPlan;
    TimeUs bestActualUs = srcDurationUs;
    double bestSim = 0.0;

    const auto scoreAndConsider = [&](const QList<MusicRemixSegment> &segs, TimeUs totalUs,
                                      double simSum, int cutCount, bool trueOutro) {
        if (segs.isEmpty())
            return;
        const TimeUs errUs = llabs(totalUs - targetUs);
        const double avgSim = cutCount > 0 ? (simSum / cutCount) : 0.0;
        // Heavy penalty outside 1 beat tolerance; within 1 beat, optimize musical smoothness,
        // bar alignment / true outro preservation, and cut-count alignment to `segments`.
        const double beatsOff = static_cast<double>(errUs) / static_cast<double>(avgBeatUs);
        double score = -beatsOff * (beatsOff <= 1.05 ? 4.0 : 40.0);
        score += avgSim * 6.0;
        score += trueOutro ? 2.5 : -4.0;
        if (cutCount > 0)
            score -= 0.15 * std::abs(cutCount - preferredCuts);
        if (!hasBest || score > bestScore) {
            hasBest = true;
            bestScore = score;
            bestPlan = segs;
            bestActualUs = totalUs;
            bestSim = avgSim;
        }
    };

    const auto effectiveXfFor = [&](const MusicRemixSegment &prev, const MusicRemixSegment &next) -> TimeUs {
        return qBound<TimeUs>(0, crossfadeUs,
                              qMin(prev.rawDurationUs() / 3, next.rawDurationUs() / 3));
    };

    // Beam search over sequence of jump segments.
    std::vector<BeamState> beam;
    beam.push_back(BeamState{0, 0, 0.0, 0, {}});
    constexpr int kBeamWidth = 64;

    for (int depth = 0; depth < maxCuts; ++depth) {
        std::vector<BeamState> nextBeam;

        for (const BeamState &st : beam) {
            // Option 1: Close out by playing directly from `st.segStartBeat` to the true end `srcOutUs`.
            {
                const TimeUs sStart = segStartSrcUs(st.segStartBeat);
                if (srcOutUs > sStart + avgBeatUs) {
                    MusicRemixSegment tail{sStart, srcOutUs, 0};
                    QList<MusicRemixSegment> candSegs = st.segs;
                    TimeUs xf = 0;
                    if (!candSegs.isEmpty()) {
                        xf = effectiveXfFor(candSegs.back(), tail);
                        candSegs.back().crossfadeUs = xf;
                    }
                    candSegs.append(tail);
                    const TimeUs tot = st.curDurUs + tail.rawDurationUs() - xf;
                    scoreAndConsider(candSegs, tot, st.simSum, st.cutCount, true);
                }
            }

            // Option 1b: Direct single-jump landing into the true outro (`srcOutUs`) targeted by binary search
            // so even strict targets on long songs always find beat-matched outro candidates.
            for (int i = qMax(st.segStartBeat + 1, (depth == 0 ? 1 : st.segStartBeat + minSectionBeats));
                 i + 1 < nBeats; ++i) {
                const TimeUs sStart = segStartSrcUs(st.segStartBeat);
                const TimeUs sEnd = segEndSrcUs(i);
                const TimeUs segLen = sEnd - sStart;
                if (segLen <= crossfadeUs * 3)
                    continue;
                TimeUs xfPrev = 0;
                MusicRemixSegment midSeg{sStart, sEnd, crossfadeUs};
                if (!st.segs.isEmpty())
                    xfPrev = effectiveXfFor(st.segs.back(), midSeg);
                const TimeUs afterMidDur = st.curDurUs + segLen - xfPrev;
                if (afterMidDur >= targetUs)
                    continue;

                const TimeUs wantedTail = targetUs - afterMidDur + crossfadeUs;
                const TimeUs wantedOutroStartRel = srcDurationUs - wantedTail;
                auto it = std::lower_bound(f.beatOffsetsUs.cbegin(), f.beatOffsetsUs.cend(), wantedOutroStartRel);
                const int baseIdx = static_cast<int>(it - f.beatOffsetsUs.cbegin());
                for (int dIdx = -1; dIdx <= 1; ++dIdx) {
                    const int j = baseIdx + dIdx;
                    if (j <= 0 || j >= nBeats || j == i + 1)
                        continue;
                    if (shortening && j < i)
                        continue;
                    if (j <= i && (i + 1 - j) < minSectionBeats)
                        continue;
                    const TimeUs outroStartSrc = segStartSrcUs(j);
                    if (srcOutUs <= outroStartSrc + crossfadeUs * 3)
                        continue;
                    MusicRemixSegment tail{outroStartSrc, srcOutUs, 0};
                    const TimeUs xfTail = effectiveXfFor(midSeg, tail);
                    const TimeUs tot = afterMidDur + tail.rawDurationUs() - xfTail;
                    if (llabs(tot - targetUs) > avgBeatUs * 2)
                        continue;
                    const double sim = contextTransitionSimilarity(f.beatFeatures, f.beatsPerBar, i, j);
                    QList<MusicRemixSegment> candSegs = st.segs;
                    if (!candSegs.isEmpty())
                        candSegs.back().crossfadeUs = xfPrev;
                    midSeg.crossfadeUs = xfTail;
                    candSegs.append(midSeg);
                    candSegs.append(tail);
                    scoreAndConsider(candSegs, tot, st.simSum + sim, st.cutCount + 1, true);
                }
            }

            // Option 2: Close early at a beat boundary if target is tiny and true outro overshoots.
            const TimeUs minPlayLen = qMax<TimeUs>(avgBeatUs, avgBeatUs * 2);
            const TimeUs targetRemaining = targetUs - st.curDurUs;
            if (st.curDurUs + minPlayLen <= targetUs + avgBeatUs && targetRemaining > 0) {
                for (int endBeat = st.segStartBeat + 1; endBeat < nBeats; ++endBeat) {
                    const TimeUs sStart = segStartSrcUs(st.segStartBeat);
                    const TimeUs sEnd = segEndSrcUs(endBeat);
                    if (sEnd <= sStart + minPlayLen / 2)
                        continue;
                    MusicRemixSegment tail{sStart, sEnd, 0};
                    QList<MusicRemixSegment> candSegs = st.segs;
                    TimeUs xf = 0;
                    if (!candSegs.isEmpty()) {
                        xf = effectiveXfFor(candSegs.back(), tail);
                        candSegs.back().crossfadeUs = xf;
                    }
                    const TimeUs tot = st.curDurUs + tail.rawDurationUs() - xf;
                    if (llabs(tot - targetUs) <= avgBeatUs) {
                        candSegs.append(tail);
                        scoreAndConsider(candSegs, tot, st.simSum, st.cutCount, false);
                    }
                }
            }

            // Option 3: Expand with a beat-matched jump `(i -> j)`.
            const int minEndBeat = (depth == 0)
                                       ? qMax(1, qMin(minSectionBeats, nBeats - 3))
                                       : (st.segStartBeat + minSectionBeats);
            for (int i = minEndBeat; i + 1 < nBeats; ++i) {
                const TimeUs sStart = segStartSrcUs(st.segStartBeat);
                const TimeUs sEnd = segEndSrcUs(i);
                const TimeUs segLen = sEnd - sStart;
                if (segLen <= crossfadeUs * 3)
                    continue;

                MusicRemixSegment seg{sStart, sEnd, crossfadeUs};
                TimeUs xfPrev = 0;
                if (!st.segs.isEmpty())
                    xfPrev = effectiveXfFor(st.segs.back(), seg);
                const TimeUs newDur = st.curDurUs + segLen - xfPrev;
                // Keep headroom for an outro segment at least 1 beat long.
                if (newDur > targetUs - avgBeatUs / 2)
                    continue;

                for (const CandidateJump &jump : outgoing[static_cast<size_t>(i)]) {
                    // For the final jump, ensure `jump.toBeat` allows reaching the end or near target.
                    BeamState nxt;
                    nxt.segStartBeat = jump.toBeat;
                    nxt.curDurUs = newDur;
                    nxt.simSum = st.simSum + jump.similarity;
                    nxt.cutCount = st.cutCount + 1;
                    nxt.segs = st.segs;
                    if (!nxt.segs.isEmpty())
                        nxt.segs.back().crossfadeUs = xfPrev;
                    nxt.segs.append(seg);
                    nextBeam.push_back(std::move(nxt));
                }
            }
        }

        if (nextBeam.empty())
            break;

        // Deduplicate by (segStartBeat, quantized curDurUs) and keep top beam states.
        std::sort(nextBeam.begin(), nextBeam.end(),
                  [&](const BeamState &a, const BeamState &b) {
                      const double avgA = a.cutCount > 0 ? a.simSum / a.cutCount : 0.0;
                      const double avgB = b.cutCount > 0 ? b.simSum / b.cutCount : 0.0;
                      const TimeUs remA = targetUs - a.curDurUs;
                      const TimeUs remB = targetUs - b.curDurUs;
                      const TimeUs typicalTail = qMax<TimeUs>(avgBeatUs * 2, srcDurationUs / 3);
                      const double fitA = -std::abs(static_cast<double>(remA)) / static_cast<double>(avgBeatUs) * 0.05;
                      const double fitB = -std::abs(static_cast<double>(remB)) / static_cast<double>(avgBeatUs) * 0.05;
                      (void)typicalTail;
                      return (avgA + fitA) > (avgB + fitB);
                  });

        QHash<quint64, bool> seen;
        beam.clear();
        for (BeamState &st : nextBeam) {
            const qint64 bucketUs = st.curDurUs / qMax<TimeUs>(1, avgBeatUs / 2);
            const quint64 key = (static_cast<quint64>(st.segStartBeat) << 32)
                              ^ static_cast<quint64>(bucketUs);
            if (seen.contains(key))
                continue;
            seen.insert(key, true);
            beam.push_back(std::move(st));
            if (static_cast<int>(beam.size()) >= kBeamWidth)
                break;
        }
    }

    // Check any final-depth states closing directly into outro.
    for (const BeamState &st : beam) {
        const TimeUs sStart = segStartSrcUs(st.segStartBeat);
        if (srcOutUs > sStart + avgBeatUs) {
            MusicRemixSegment tail{sStart, srcOutUs, 0};
            QList<MusicRemixSegment> candSegs = st.segs;
            TimeUs xf = 0;
            if (!candSegs.isEmpty()) {
                xf = effectiveXfFor(candSegs.back(), tail);
                candSegs.back().crossfadeUs = xf;
            }
            candSegs.append(tail);
            const TimeUs tot = st.curDurUs + tail.rawDurationUs() - xf;
            scoreAndConsider(candSegs, tot, st.simSum, st.cutCount, true);
        }
    }

    if (!hasBest || bestPlan.isEmpty()) {
        res.error = QStringLiteral("Could not find a beat-matched cut plan for the requested duration.");
        return res;
    }

    bestPlan.last().crossfadeUs = 0;
    res.plan.plan = std::move(bestPlan);
    res.plan.targetDurationUs = targetUs;
    res.actualDurationUs = res.plan.totalDurationUs();
    res.avgSimilarity = bestSim;
    res.ok = !res.plan.plan.isEmpty();
    return res;
}

struct MediaCacheKey
{
    QString path;
    int streamIndex = 0;
    TimeUs srcInUs = 0;
    TimeUs srcOutUs = 0;

    bool operator==(const MediaCacheKey &other) const
    {
        return path == other.path && streamIndex == other.streamIndex
            && srcInUs == other.srcInUs && srcOutUs == other.srcOutUs;
    }
};

size_t qHash(const MediaCacheKey &k, size_t seed = 0)
{
    return qHashMulti(seed, k.path, k.streamIndex, k.srcInUs, k.srcOutUs);
}

QMutex &cacheMutex()
{
    static QMutex m;
    return m;
}

QHash<MediaCacheKey, std::shared_ptr<const CachedTrackFeatures>> &featureCache()
{
    static QHash<MediaCacheKey, std::shared_ptr<const CachedTrackFeatures>> cache;
    return cache;
}

} // namespace

AudioRemixResult AudioRemix::planFromMonoPcm(const float *mono, int frameCount, int sampleRate,
                                            TimeUs srcInUs, TimeUs srcOutUs,
                                            const AudioRemixOptions &options)
{
    AudioRemixResult res;
    if (srcOutUs <= srcInUs) {
        res.error = QStringLiteral("Invalid source range.");
        return res;
    }
    QString err;
    const auto features = buildFeaturesFromPcm(mono, frameCount, sampleRate, srcOutUs - srcInUs, &err);
    if (!features) {
        res.error = err;
        return res;
    }
    return planFromFeatures(*features, srcInUs, srcOutUs, options);
}

AudioRemixResult AudioRemix::planFromMedia(const QString &path, int streamIndex,
                                           TimeUs srcInUs, TimeUs srcOutUs,
                                           const AudioRemixOptions &options)
{
    AudioRemixResult res;
    if (path.isEmpty() || srcOutUs <= srcInUs) {
        res.error = QStringLiteral("Clip has no audio media to remix.");
        return res;
    }

    const QFileInfo fi(path);
    const QString canonical = fi.exists() ? fi.canonicalFilePath() : path;
    const MediaCacheKey key{canonical, streamIndex, srcInUs, srcOutUs};

    std::shared_ptr<const CachedTrackFeatures> cached;
    {
        QMutexLocker lock(&cacheMutex());
        cached = featureCache().value(key);
    }

    if (!cached) {
        const TimeUs spanUs = srcOutUs - srcInUs;
        if (spanUs < static_cast<TimeUs>(AudioOnsets::kMinAnalysisSec * kUsPerSecond)) {
            res.error = QStringLiteral("Clip source must be at least 4.0 seconds long to remix.");
            return res;
        }
        const int totalFrames = static_cast<int>((spanUs * kAnalysisSampleRate) / kUsPerSecond);
        if (totalFrames < kFftSize) {
            res.error = QStringLiteral("Clip has too little audio data to analyze.");
            return res;
        }

        std::vector<float> mono(static_cast<size_t>(totalFrames), 0.0f);
        std::vector<float> stereoChunk;
        constexpr int kChunkFrames = 32768;
        const quint64 streamId = ClipReaderPool::streamIdForClip(QStringLiteral("remix:") + canonical)
                               ^ 0x52454D4958ULL;
        int done = 0;
        while (done < totalFrames) {
            const int take = qMin(kChunkFrames, totalFrames - done);
            stereoChunk.resize(static_cast<size_t>(take) * 2);
            const TimeUs at = srcInUs + (static_cast<TimeUs>(done) * kUsPerSecond) / kAnalysisSampleRate;
            const int got = ClipReaderPool::instance().readAudioInterleaved(
                path, streamId, at, take, kAnalysisSampleRate, stereoChunk.data(), streamIndex);
            for (int i = 0; i < got; ++i)
                mono[static_cast<size_t>(done + i)] = 0.5f * (stereoChunk[i * 2] + stereoChunk[i * 2 + 1]);
            if (got <= 0)
                break;
            done += take;
        }

        QString err;
        cached = buildFeaturesFromPcm(mono.data(), done, kAnalysisSampleRate, spanUs, &err);
        if (!cached) {
            res.error = err;
            return res;
        }

        QMutexLocker lock(&cacheMutex());
        auto &cache = featureCache();
        if (cache.size() >= kMaxCachedTracks)
            cache.clear();
        cache.insert(key, cached);
    }

    return planFromFeatures(*cached, srcInUs, srcOutUs, options);
}

void AudioRemix::clearCache()
{
    QMutexLocker lock(&cacheMutex());
    featureCache().clear();
}

} // namespace drift
