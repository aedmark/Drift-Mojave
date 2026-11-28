#include <QtTest>

#include "core/audio/MusicRemix.h"
#include "core/edit/TimelineOps.h"
#include "core/project/Project.h"
#include "engine/audio/AudioMixer.h"
#include "engine/audio/analysis/AudioRemix.h"

#include <cmath>
#include <random>
#include <vector>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

using namespace drift;

class AudioRemixTest : public QObject
{
    Q_OBJECT

private slots:
    void rejectsTooShortSilentAndAperiodicAudio();
    void shortensAndLengthensToTargetWithinOneBeat();
    void isDeterministicAcrossRuns();
    void projectJsonRoundTripAndSplitPreservesRemix();
    void mixerCrossfadeIsClickFreeAndBlockSizeIndependent();
};

namespace {

// Synthesizes a 4-bar chord loop + kick transient every beat at `bpm`.
std::vector<float> makeMusicalTrack(double durationSec, double bpm, int sampleRate)
{
    const int n = static_cast<int>(llround(durationSec * sampleRate));
    std::vector<float> pcm(static_cast<size_t>(n), 0.0f);
    const double beatSec = 60.0 / bpm;
    // Chord roots across a 4-bar phrase (4 beats per chord): C3, G2, A2, F2.
    static constexpr double kRoots[] = {130.81, 98.00, 110.00, 87.31};
    for (int i = 0; i < n; ++i) {
        const double t = static_cast<double>(i) / sampleRate;
        const int beatIndex = static_cast<int>(std::floor(t / beatSec));
        const double phaseInBeat = t - beatIndex * beatSec;
        const int bar = (beatIndex / 4) % 4;
        const double f0 = kRoots[bar];

        // Percussive kick + crisp hi-hat burst near beat onset.
        const double kickEnv = std::exp(-phaseInBeat * 28.0);
        const double kick = 0.45 * kickEnv * std::sin(2.0 * M_PI * 72.0 * phaseInBeat);
        const bool downbeat = (beatIndex % 4) == 0;
        const double accent = downbeat ? 0.25 * std::exp(-phaseInBeat * 40.0)
                                       * std::sin(2.0 * M_PI * 1200.0 * phaseInBeat)
                                       : 0.0;

        // Warm triad harmonic texture giving distinct chroma per bar.
        const double harm = 0.14 * std::sin(2.0 * M_PI * f0 * t)
                          + 0.10 * std::sin(2.0 * M_PI * (f0 * 1.25) * t)
                          + 0.08 * std::sin(2.0 * M_PI * (f0 * 1.50) * t);

        pcm[static_cast<size_t>(i)] = static_cast<float>(kick + accent + harm);
    }
    return pcm;
}

} // namespace

void AudioRemixTest::rejectsTooShortSilentAndAperiodicAudio()
{
    constexpr int rate = 22050;
    AudioRemixOptions opts;
    opts.targetDurationUs = secondsToUs(10.0);

    // 1. Shorter than 4 seconds must fail cleanly.
    const std::vector<float> shortTrack = makeMusicalTrack(2.5, 120.0, rate);
    const AudioRemixResult shortRes = AudioRemix::planFromMonoPcm(
        shortTrack.data(), static_cast<int>(shortTrack.size()), rate, 0, secondsToUs(2.5), opts);
    QVERIFY(!shortRes.ok);
    QVERIFY(!shortRes.error.isEmpty());

    // 2. Pure silence must fail cleanly.
    std::vector<float> silent(static_cast<size_t>(10 * rate), 0.0f);
    const AudioRemixResult silentRes = AudioRemix::planFromMonoPcm(
        silent.data(), static_cast<int>(silent.size()), rate, 0, secondsToUs(10.0), opts);
    QVERIFY(!silentRes.ok);
    QVERIFY(!silentRes.error.isEmpty());

    // 3. Steady unmodulated sine wave has no rhythmic beat and must fail cleanly.
    std::vector<float> flatTone(static_cast<size_t>(10 * rate), 0.0f);
    for (int i = 0; i < static_cast<int>(flatTone.size()); ++i)
        flatTone[static_cast<size_t>(i)] = 0.4f * float(std::sin(2.0 * M_PI * 440.0 * i / rate));
    const AudioRemixResult flatRes = AudioRemix::planFromMonoPcm(
        flatTone.data(), static_cast<int>(flatTone.size()), rate, 0, secondsToUs(10.0), opts);
    QVERIFY(!flatRes.ok);
    QVERIFY(!flatRes.error.isEmpty());
}

void AudioRemixTest::shortensAndLengthensToTargetWithinOneBeat()
{
    constexpr int rate = 22050;
    constexpr double kBpm = 120.0;
    constexpr double kSrcSec = 16.0;
    const std::vector<float> pcm = makeMusicalTrack(kSrcSec, kBpm, rate);
    const TimeUs srcEnd = secondsToUs(kSrcSec);

    // 1 beat at 120 BPM = 0.5 s (+ tiny float/crossfade tolerance).
    const TimeUs maxBeatTol = secondsToUs(0.55);

    // Shortening: 16s -> 9s
    {
        AudioRemixOptions opts;
        opts.targetDurationUs = secondsToUs(9.0);
        opts.segments = 50;
        opts.variations = 50;
        const AudioRemixResult res = AudioRemix::planFromMonoPcm(
            pcm.data(), static_cast<int>(pcm.size()), rate, 0, srcEnd, opts);
        QVERIFY2(res.ok, qPrintable(res.error));
        QVERIFY(res.plan.active());
        QVERIFY2(qAbs(res.plan.bpm - kBpm) <= 6.0, qPrintable(QString::number(res.plan.bpm)));
        QVERIFY(res.plan.plan.size() >= 2);
        QCOMPARE(res.plan.plan.first().srcStartUs, TimeUs{0});
        QVERIFY2(llabs(res.actualDurationUs - opts.targetDurationUs) <= maxBeatTol,
                 qPrintable(QStringLiteral("short actual=%1 target=%2")
                                .arg(usToSeconds(res.actualDurationUs))
                                .arg(usToSeconds(opts.targetDurationUs))));
    }

    // Lengthening: 16s -> 26s
    {
        AudioRemixOptions opts;
        opts.targetDurationUs = secondsToUs(26.0);
        opts.segments = 60;
        opts.variations = 55;
        const AudioRemixResult res = AudioRemix::planFromMonoPcm(
            pcm.data(), static_cast<int>(pcm.size()), rate, 0, srcEnd, opts);
        QVERIFY2(res.ok, qPrintable(res.error));
        QVERIFY(res.plan.active());
        QVERIFY(res.plan.plan.size() >= 2);
        QCOMPARE(res.plan.plan.first().srcStartUs, TimeUs{0});
        QCOMPARE(res.plan.plan.last().srcEndUs, srcEnd);
        QVERIFY2(llabs(res.actualDurationUs - opts.targetDurationUs) <= maxBeatTol,
                 qPrintable(QStringLiteral("long actual=%1 target=%2")
                                .arg(usToSeconds(res.actualDurationUs))
                                .arg(usToSeconds(opts.targetDurationUs))));
    }
}

void AudioRemixTest::isDeterministicAcrossRuns()
{
    constexpr int rate = 22050;
    const std::vector<float> pcm = makeMusicalTrack(14.0, 110.0, rate);
    AudioRemixOptions opts;
    opts.targetDurationUs = secondsToUs(8.5);
    opts.segments = 45;
    opts.variations = 40;

    const AudioRemixResult a = AudioRemix::planFromMonoPcm(
        pcm.data(), static_cast<int>(pcm.size()), rate, secondsToUs(0.5), secondsToUs(14.0), opts);
    const AudioRemixResult b = AudioRemix::planFromMonoPcm(
        pcm.data(), static_cast<int>(pcm.size()), rate, secondsToUs(0.5), secondsToUs(14.0), opts);
    QVERIFY2(a.ok, qPrintable(a.error));
    QCOMPARE(a.plan, b.plan);
    QCOMPARE(a.actualDurationUs, b.actualDurationUs);
    QCOMPARE(a.plan.plan.first().srcStartUs, secondsToUs(0.5));
}

void AudioRemixTest::projectJsonRoundTripAndSplitPreservesRemix()
{
    Project project;
    Track audio;
    audio.id = QStringLiteral("audio_track_1");
    audio.name = QStringLiteral("Audio 1");
    audio.type = TrackType::Audio;
    Clip clip;
    clip.id = QStringLiteral("remix_clip_1");
    clip.type = ClipType::Audio;
    clip.timelineStart = 0;
    clip.srcIn = 0;
    clip.srcOut = secondsToUs(16.0);
    clip.remix.enabled = true;
    clip.remix.targetDurationUs = secondsToUs(9.0);
    clip.remix.originalDurationUs = secondsToUs(16.0);
    clip.remix.originalSrcOutUs = secondsToUs(16.0);
    clip.remix.bpm = 120.0;
    clip.remix.segments = 40;
    clip.remix.variations = 60;
    clip.remix.plan = {
        MusicRemixSegment{0, secondsToUs(4.5), 20000},
        MusicRemixSegment{secondsToUs(11.48), secondsToUs(16.0), 0},
    };
    clip.timelineDuration = clip.remix.totalDurationUs();
    const TimeUs origDur = clip.timelineDuration;
    audio.clips.append(clip);
    project.tracks().append(audio);

    const QJsonObject json = project.toJson();
    const Project loaded = Project::fromJson(json);
    QCOMPARE(loaded.tracks().size(), 2);
    QVERIFY(!loaded.tracks().at(1).clips.isEmpty());
    const Clip &loadedClip = loaded.tracks().at(1).clips.at(0);
    QCOMPARE(loadedClip.remix, clip.remix);
    QCOMPARE(loadedClip.timelineDuration, origDur);

    Clip head = clip;
    Clip tail;
    const TimeUs splitAt = secondsToUs(5.0);
    QVERIFY(splitClipAtOffsetMin(head, tail, splitAt, minClipDurationUs(30.0)));
    QVERIFY(head.hasRemix());
    QVERIFY(tail.hasRemix());
    QCOMPARE(head.timelineDuration + tail.timelineDuration, origDur);
    QCOMPARE(head.remix.totalDurationUs(), head.timelineDuration);
    QCOMPARE(tail.remix.totalDurationUs(), tail.timelineDuration);
}

void AudioRemixTest::mixerCrossfadeIsClickFreeAndBlockSizeIndependent()
{
    constexpr int rate = 48000;
    Clip clip;
    clip.id = QStringLiteral("mix_test");
    clip.type = ClipType::Audio;
    clip.timelineStart = 0;
    clip.srcIn = 0;
    clip.srcOut = secondsToUs(10.0);
    clip.remix.enabled = true;
    clip.remix.plan = {
        MusicRemixSegment{0, secondsToUs(1.020), 20000},
        MusicRemixSegment{secondsToUs(5.000), secondsToUs(6.000), 0},
    };
    clip.timelineDuration = clip.remix.totalDurationUs();

    // Deterministic continuous 440 Hz stereo tone with phase jump deliberately avoided inside
    // each segment: even if phase is shifted 180° at t=5s (worst case for a naive splice),
    // the 20 ms equal-power crossfade blends smoothly without a step spike.
    const AudioMixer::SourceReader source = [rate](TimeUs startUs, int frames, float *dst) {
        const double t0 = usToSeconds(startUs);
        for (int i = 0; i < frames; ++i) {
            const double t = t0 + static_cast<double>(i) / rate;
            const double phaseFlip = (t >= 4.0) ? -1.0 : 1.0;
            const float s = static_cast<float>(0.5 * phaseFlip * std::sin(2.0 * M_PI * 220.0 * t));
            dst[i * 2] = s;
            dst[i * 2 + 1] = s;
        }
        return frames;
    };

    const int totalFrames = static_cast<int>((clip.timelineDuration * rate) / kUsPerSecond);
    const QVector<float> whole = AudioMixer::readClipAudio(
        clip, 1, 0, totalFrames, rate, nullptr, source);

    QVector<float> chunked;
    chunked.reserve(totalFrames * 2);
    // 240 frames at 48 kHz is an exact 5000 us window so cross-block stitching avoids us-truncation jitter.
    for (int pos = 0; pos < totalFrames; pos += 240) {
        const int take = qMin(240, totalFrames - pos);
        const TimeUs at = (static_cast<TimeUs>(pos) * kUsPerSecond) / rate;
        const QVector<float> part = AudioMixer::readClipAudio(
            clip, 1, at, take, rate, nullptr, source);
        chunked += part;
    }
    QCOMPARE(chunked.size(), whole.size());

    float maxDiff = 0.0f;
    for (int i = 0; i < whole.size(); ++i)
        maxDiff = qMax(maxDiff, std::fabs(whole[i] - chunked[i]));
    QVERIFY2(maxDiff < 1e-5f, qPrintable(QString::number(maxDiff)));

    // Verify no sample-to-sample step glitch across the crossfade zone at t=1.000..1.020s.
    const int jointStartFrame = static_cast<int>(0.99 * rate);
    const int jointEndFrame = static_cast<int>(1.03 * rate);
    float worstDelta = 0.0f;
    for (int f = jointStartFrame + 1; f < qMin(jointEndFrame, totalFrames); ++f)
        worstDelta = qMax(worstDelta, std::fabs(whole[f * 2] - whole[(f - 1) * 2]));
    QVERIFY2(worstDelta < 0.05f, qPrintable(QString::number(worstDelta)));
}

QTEST_MAIN(AudioRemixTest)
#include "tst_audioremix.moc"
