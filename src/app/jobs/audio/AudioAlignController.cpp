#include "app/jobs/audio/AudioAlignController.h"

#include "app/editor/AppController.h"
#include "core/edit/CutOps.h"
#include "core/edit/TimelineOps.h"
#include "engine/audio/AudioMixer.h"
#include "engine/audio/analysis/AudioAlign.h"

#include <QMetaObject>
#include <QSet>
#include <QUuid>
#include <QtConcurrent>

#include <algorithm>
#include <cmath>
#include <functional>
#include <vector>

namespace {

constexpr int kAnalysisRate = 8000;
constexpr double kMaxSyncSeconds = 120.0;
constexpr double kMaxSyncLagSeconds = 60.0;

bool carriesAudio(const drift::Clip &clip)
{
    return clip.type == drift::ClipType::Audio
           || (clip.type == drift::ClipType::Video && !clip.suppressEmbeddedAudio);
}

// The clip whose sound `clip` plays: itself, or for a video whose audio was separated, its audio
// partner.
const drift::Clip *audibleClip(const drift::Project &project, const drift::Clip &clip)
{
    if (carriesAudio(clip))
        return &clip;
    for (const drift::ClipRef &ref : drift::linkedPartners(project, clip)) {
        if (project.tracks().at(ref.trackIndex).type != drift::TrackType::Audio)
            continue;
        const drift::Clip &partner = project.tracks().at(ref.trackIndex).clips.at(ref.clipIndex);
        if (carriesAudio(partner))
            return &partner;
    }
    return nullptr;
}

bool findClip(const drift::Project &project, const QString &id, int *trackOut, int *clipOut)
{
    for (int t = 0; t < project.tracks().size(); ++t) {
        const QList<drift::Clip> &clips = project.tracks().at(t).clips;
        for (int c = 0; c < clips.size(); ++c) {
            if (clips.at(c).id == id) {
                *trackOut = t;
                *clipOut = c;
                return true;
            }
        }
    }
    return false;
}

// Mono mix of `snap` from startUs for `frames` frames at the analysis rate. `progress` takes
// 0-1 and returns false to stop, which leaves the result short.
std::vector<float> mixMono(const drift::Project &snap, drift::TimeUs startUs, qint64 frames,
                           const std::function<bool(double)> &progress)
{
    std::vector<float> mono;
    if (frames <= 0)
        return mono;
    mono.resize(size_t(frames));
    AudioMixer mixer;
    mixer.setProject(&snap);
    constexpr int kBlock = 4096;
    std::vector<float> stereo(kBlock * 2);
    for (qint64 off = 0; off < frames; off += kBlock) {
        if (progress && !progress(double(off) / double(frames))) {
            mono.clear();
            return mono;
        }
        const int n = int(std::min<qint64>(kBlock, frames - off));
        std::fill(stereo.begin(), stereo.begin() + n * 2, 0.0f);
        mixer.mix(startUs + off * drift::kUsPerSecond / kAnalysisRate, n, kAnalysisRate,
                  stereo.data());
        for (int i = 0; i < n; ++i)
            mono[size_t(off + i)] = 0.5f * (stereo[i * 2] + stereo[i * 2 + 1]);
    }
    return mono;
}

bool retimed(const drift::Clip &clip)
{
    return std::abs(clip.effectiveSpeed() - 1.0) > 1e-6 || clip.reverse || clip.hasSpeedCurve();
}

} // namespace

AudioAlignController::AudioAlignController(AppController *app, QObject *parent)
    : QObject(parent)
    , m_app(app)
{
}

void AudioAlignController::cancel()
{
    if (m_running)
        m_cancel.storeRelaxed(1);
}

bool AudioAlignController::begin(const QString &message)
{
    if (m_running) {
        m_app->setLastMessage(tr("Audio analysis already in progress"), QStringLiteral("warning"));
        return false;
    }
    // Playback would drive the decode pool from a second thread while the mixer walks it.
    m_app->setPlaying(false);
    m_cancel.storeRelaxed(0);
    m_progress = 0.0;
    emit progressChanged();
    m_status = message;
    emit statusChanged();
    m_running = true;
    emit runningChanged();
    m_app->setLastMessage(message);
    return true;
}

void AudioAlignController::end()
{
    m_running = false;
    emit runningChanged();
    m_progress = 1.0;
    emit progressChanged();
}

void AudioAlignController::reportProgress(double fraction, const QString &status)
{
    QMetaObject::invokeMethod(
        this,
        [this, fraction, status]() {
            if (!m_running)
                return;
            m_progress = fraction;
            emit progressChanged();
            if (!status.isEmpty() && status != m_status) {
                m_status = status;
                emit statusChanged();
            }
        },
        Qt::QueuedConnection);
}

bool AudioAlignController::canRemoveSilence(int trackIndex, int clipIndex) const
{
    if (!m_app->isValidClipIndex(trackIndex, clipIndex))
        return false;
    return audibleClip(m_app->m_project, m_app->m_project.tracks().at(trackIndex).clips.at(clipIndex))
           != nullptr;
}

void AudioAlignController::removeSilence(int trackIndex, int clipIndex)
{
    removeSilenceWithOptions(trackIndex, clipIndex, -34.0, 0.5, 0.08);
}

void AudioAlignController::removeSilenceWithOptions(int trackIndex, int clipIndex,
                                                    double thresholdDb,
                                                    double minSilenceSec,
                                                    double paddingSec)
{
    if (!m_app->isValidClipIndex(trackIndex, clipIndex))
        return;
    const drift::Clip &target = m_app->m_project.tracks().at(trackIndex).clips.at(clipIndex);
    const drift::Clip *audible = audibleClip(m_app->m_project, target);
    if (!audible) {
        m_app->setLastMessage(tr("Select a clip with audio"), QStringLiteral("warning"));
        return;
    }
    if (audible->timelineDuration <= 0)
        return;
    const QString targetId = target.id;
    const drift::TimeUs startUs = audible->timelineStart;
    const drift::TimeUs durationUs = audible->timelineDuration;
    drift::Project snap = m_app->m_project.detachedCopy();
    drift::muteAllButClip(snap, audible->id);

    drift::SilenceParams params;
    params.threshold = float(std::clamp(std::pow(10.0, thresholdDb / 20.0), 0.0005, 0.5));
    params.minSilence = std::clamp(minSilenceSec, 0.05, 30.0);
    params.padding = std::clamp(paddingSec, 0.0, 2.0);

    if (!begin(tr("Finding silence…")))
        return;

    (void)QtConcurrent::run([this, snap, targetId, startUs, durationUs, params]() {
        const qint64 frames = durationUs * kAnalysisRate / drift::kUsPerSecond;
        const std::vector<float> mono = mixMono(snap, startUs, frames, [this](double f) {
            reportProgress(0.95 * f, {});
            return m_cancel.loadRelaxed() == 0;
        });
        const bool cancelled = m_cancel.loadRelaxed() != 0 || mono.empty();
        QList<QPair<double, double>> silences;
        if (!cancelled) {
            for (const drift::SilenceSpan &s : drift::findSilence(mono.data(), qint64(mono.size()),
                                                                  kAnalysisRate, params))
                silences.append({s.start, s.end});
        }
        QMetaObject::invokeMethod(
            this,
            [this, cancelled, targetId, startUs, durationUs, silences]() {
                end();
                if (cancelled) {
                    m_app->setLastMessage(tr("Silence removal cancelled"));
                    return;
                }
                applySilence(targetId, startUs, durationUs, silences);
            },
            Qt::QueuedConnection);
    });
}

void AudioAlignController::applySilence(const QString &clipId, qint64 startUs, qint64 durationUs,
                                        const QList<QPair<double, double>> &silences)
{
    int track = -1, index = -1;
    if (!findClip(m_app->m_project, clipId, &track, &index)) {
        m_app->setLastMessage(tr("The clip changed while it was analysed"), QStringLiteral("warning"));
        return;
    }
    const drift::Clip clip = m_app->m_project.tracks().at(track).clips.at(index);
    if (clip.timelineStart != startUs || clip.timelineDuration != durationUs) {
        m_app->setLastMessage(tr("The clip changed while it was analysed"), QStringLiteral("warning"));
        return;
    }

    QList<drift::TimeRangeUs> removed;
    for (const QPair<double, double> &s : silences)
        removed.append({startUs + drift::secondsToUs(s.first), startUs + drift::secondsToUs(s.second)});
    constexpr drift::TimeUs kDeclickUs = 30'000;
    const QList<drift::TimeRangeUs> kept = drift::keptTimelineIntervals(
        clip, removed, std::max<drift::TimeUs>(drift::kCutMinEdgeUs, 2 * kDeclickUs));
    if (kept.size() == 1 && kept.first().startUs == clip.timelineStart
        && kept.first().endUs == clip.timelineEnd()) {
        m_app->setLastMessage(tr("No silence found"));
        return;
    }
    drift::TimeUs keptTotal = 0;
    for (const drift::TimeRangeUs &k : kept)
        keptTotal += k.length();

    const drift::Project before = m_app->m_project;
    QString error;
    if (!m_app->replaceClipGroupWithSegments(
            track, index,
            [&kept](const drift::Clip &member) {
                return drift::packedSegments(member, kept, kDeclickUs);
            },
            true, nullptr, &error)) {
        m_app->m_project = before;
        m_app->setLastMessage(tr("Could not remove the silence"), QStringLiteral("error"));
        return;
    }
    m_app->pushProjectEdit(before, tr("Remove silence"));
    m_app->finishEdit(tr("Remove silence"));
    m_app->setLastMessage(
        tr("Removed %1 s of silence")
            .arg(drift::usToSeconds(clip.timelineDuration - keptTotal), 0, 'f', 1));
}

// A selected item for syncing: every selected clip sharing a link (a video and its separated
// audio) is one item, represented by its first audio-carrying member.
struct AudioAlignController::SyncUnit
{
    QString key;
    QString repId;
    drift::TimeUs startUs = 0;
    drift::TimeUs durationUs = 0;
};

struct AudioAlignController::SyncMatch
{
    SyncUnit unit;
    drift::AudioOffsetResult result;
};

bool AudioAlignController::canSyncByAudio() const
{
    QSet<QString> keys;
    for (const QPair<int, int> &pair : std::as_const(m_app->m_selection)) {
        if (!m_app->isValidClipIndex(pair.first, pair.second))
            continue;
        const drift::Clip &clip = m_app->m_project.tracks().at(pair.first).clips.at(pair.second);
        if (carriesAudio(clip))
            keys.insert(clip.linkId.isEmpty() ? clip.id : clip.linkId);
    }
    return keys.size() >= 2;
}

void AudioAlignController::syncByAudio(bool groupSynced, bool muteScratchAudio)
{
    QList<SyncUnit> units;
    for (const QPair<int, int> &pair : std::as_const(m_app->m_selection)) {
        if (!m_app->isValidClipIndex(pair.first, pair.second))
            continue;
        const drift::Clip &clip = m_app->m_project.tracks().at(pair.first).clips.at(pair.second);
        if (!carriesAudio(clip))
            continue;
        const QString key = clip.linkId.isEmpty() ? clip.id : clip.linkId;
        const bool known = std::any_of(units.cbegin(), units.cend(),
                                       [&key](const SyncUnit &u) { return u.key == key; });
        if (!known)
            units.append({key, clip.id, clip.timelineStart, clip.timelineDuration});
    }
    if (units.size() < 2) {
        m_app->setLastMessage(tr("Select at least two clips with audio to sync"),
                              QStringLiteral("warning"));
        return;
    }

    // All items and everything linked to them: speed or direction changes break the
    // fixed-offset assumption.
    for (const SyncUnit &unit : std::as_const(units)) {
        int t = -1, c = -1;
        if (!findClip(m_app->m_project, unit.repId, &t, &c))
            return;
        const drift::Clip &rep = m_app->m_project.tracks().at(t).clips.at(c);
        bool bad = retimed(rep);
        for (const drift::ClipRef &ref : drift::linkedPartners(m_app->m_project, rep))
            bad = bad || retimed(m_app->m_project.tracks().at(ref.trackIndex).clips.at(ref.clipIndex));
        if (bad) {
            m_app->setLastMessage(
                tr("Clips with changed speed or reversed playback can't be synced. Reset their "
                   "speed and direction first."),
                QStringLiteral("warning"));
            return;
        }
    }

    const SyncUnit anchor = units.at(0);
    QList<SyncUnit> others = units.mid(1);
    drift::Project anchorSnap = m_app->m_project.detachedCopy();
    drift::muteAllButClip(anchorSnap, anchor.repId);

    QList<drift::Project> otherSnaps;
    otherSnaps.reserve(others.size());
    for (const SyncUnit &other : std::as_const(others)) {
        drift::Project snap = m_app->m_project.detachedCopy();
        drift::muteAllButClip(snap, other.repId);
        otherSnaps.append(std::move(snap));
    }

    if (!begin(tr("Matching audio…")))
        return;

    (void)QtConcurrent::run([this, anchor, others, anchorSnap, otherSnaps, groupSynced, muteScratchAudio]() {
        const auto framesFor = [](drift::TimeUs durationUs) {
            return qint64(std::min(drift::usToSeconds(durationUs), kMaxSyncSeconds) * kAnalysisRate);
        };
        const auto alive = [this]() { return m_cancel.loadRelaxed() == 0; };
        const int totalSteps = 1 + others.size();
        const double stepWeight = 0.85 / std::max(1, totalSteps);

        const std::vector<float> a = mixMono(anchorSnap, anchor.startUs, framesFor(anchor.durationUs),
                                             [this, &alive, stepWeight](double f) {
                                                 reportProgress(stepWeight * f, {});
                                                 return alive();
                                             });
        QList<SyncMatch> matches;
        matches.reserve(others.size());
        bool cancelled = !alive() || a.empty();

        drift::AudioOffsetParams params;
        params.maxLagSeconds = kMaxSyncLagSeconds;

        for (int i = 0; i < others.size() && !cancelled; ++i) {
            const SyncUnit &other = others.at(i);
            const double base = stepWeight * (1 + i);
            const std::vector<float> b = mixMono(otherSnaps.at(i), other.startUs,
                                                 framesFor(other.durationUs),
                                                 [this, &alive, base, stepWeight](double f) {
                                                     reportProgress(base + stepWeight * f, {});
                                                     return alive();
                                                 });
            if (!alive() || b.empty()) {
                cancelled = true;
                break;
            }
            reportProgress(0.85 + 0.15 * (double(i) / std::max(1, int(others.size()))),
                           tr("Correlating…"));
            drift::AudioOffsetResult result = drift::findAudioOffset(a, b, kAnalysisRate, params, alive);
            if (!alive()) {
                cancelled = true;
                break;
            }
            matches.append({other, result});
        }

        QMetaObject::invokeMethod(
            this,
            [this, anchor, matches, cancelled, groupSynced, muteScratchAudio]() {
                end();
                if (m_cancel.loadRelaxed() != 0 || cancelled) {
                    m_app->setLastMessage(tr("Audio sync cancelled"));
                    return;
                }
                applyMultiSync(anchor, matches, groupSynced, muteScratchAudio);
            },
            Qt::QueuedConnection);
    });
}

void AudioAlignController::applyMultiSync(const SyncUnit &anchor, const QList<SyncMatch> &matches,
                                          bool groupSynced, bool muteScratchAudio)
{
    int at = -1, ac = -1;
    if (!findClip(m_app->m_project, anchor.repId, &at, &ac)
        || m_app->m_project.tracks().at(at).clips.at(ac).timelineStart != anchor.startUs) {
        m_app->setLastMessage(tr("The clips changed while they were analysed"),
                              QStringLiteral("warning"));
        return;
    }

    using Status = drift::AudioOffsetResult::Status;
    QList<SyncMatch> okMatches;
    int unmatched = 0;
    for (const SyncMatch &m : matches) {
        if (m.result.status == Status::Ok)
            okMatches.append(m);
        else
            ++unmatched;
    }

    if (okMatches.isEmpty()) {
        if (matches.size() == 1) {
            switch (matches.first().result.status) {
            case Status::Silent:
                m_app->setLastMessage(tr("One of the clips is silent, so there is nothing to match"),
                                      QStringLiteral("warning"));
                return;
            case Status::TooShort:
                m_app->setLastMessage(tr("The clips are too short to match by audio"),
                                      QStringLiteral("warning"));
                return;
            case Status::Ambiguous:
            case Status::Ok:
                m_app->setLastMessage(
                    tr("No clear match between the clips' audio. Nothing was moved."),
                    QStringLiteral("warning"));
                return;
            }
        }
        m_app->setLastMessage(tr("No clear audio match found for any selected clip"),
                              QStringLiteral("warning"));
        return;
    }

    struct UnitGroup
    {
        drift::TimeUs delta = 0;
        QList<QPair<int, int>> refs;
    };

    auto collectGroupRefs = [&](int t, int c) {
        QList<QPair<int, int>> refs{{t, c}};
        for (const drift::ClipRef &ref :
             drift::linkedPartners(m_app->m_project, m_app->m_project.tracks().at(t).clips.at(c))) {
            refs.append({ref.trackIndex, ref.clipIndex});
        }
        return refs;
    };

    QList<UnitGroup> groups;
    groups.reserve(1 + okMatches.size());
    groups.append({0, collectGroupRefs(at, ac)});

    for (const SyncMatch &m : std::as_const(okMatches)) {
        int ot = -1, oc = -1;
        if (!findClip(m_app->m_project, m.unit.repId, &ot, &oc)
            || m_app->m_project.tracks().at(ot).clips.at(oc).timelineStart != m.unit.startUs) {
            m_app->setLastMessage(tr("The clips changed while they were analysed"),
                                  QStringLiteral("warning"));
            return;
        }
        const drift::TimeUs delta = anchor.startUs + drift::secondsToUs(m.result.lagSeconds) - m.unit.startUs;
        groups.append({delta, collectGroupRefs(ot, oc)});
    }

    QSet<QString> allIds;
    drift::TimeUs minMovedStart = std::numeric_limits<drift::TimeUs>::max();
    for (const UnitGroup &g : std::as_const(groups)) {
        for (const QPair<int, int> &p : std::as_const(g.refs)) {
            const drift::Clip &clip = m_app->m_project.tracks().at(p.first).clips.at(p.second);
            allIds.insert(clip.id);
            minMovedStart = std::min(minMovedStart, clip.timelineStart + g.delta);
        }
    }

    const drift::TimeUs globalShift = (minMovedStart < 0) ? -minMovedStart : 0;
    bool anyMoved = false;
    for (UnitGroup &g : groups) {
        g.delta += globalShift;
        if (g.delta != 0)
            anyMoved = true;
    }

    if (!m_app->allowClipOverlap()) {
        for (const UnitGroup &g : std::as_const(groups)) {
            for (const QPair<int, int> &p : std::as_const(g.refs)) {
                const drift::Track &track = m_app->m_project.tracks().at(p.first);
                const drift::Clip &clip = track.clips.at(p.second);
                const drift::TimeUs moved = clip.timelineStart + g.delta;
                if (drift::clampClipStartNoOverlap(track, allIds, moved, clip.timelineDuration) != moved) {
                    m_app->setLastMessage(tr("There is no room to move the clip there"),
                                          QStringLiteral("warning"));
                    return;
                }
            }
        }
    }

    bool hasExternalAudio = false;
    if (muteScratchAudio) {
        for (const UnitGroup &g : std::as_const(groups)) {
            for (const QPair<int, int> &p : std::as_const(g.refs)) {
                if (m_app->m_project.tracks().at(p.first).clips.at(p.second).type == drift::ClipType::Audio) {
                    hasExternalAudio = true;
                    break;
                }
            }
            if (hasExternalAudio)
                break;
        }
    }

    if (!anyMoved && !groupSynced && !(muteScratchAudio && hasExternalAudio)) {
        m_app->setLastMessage(tr("The clips are already in sync"));
        return;
    }

    const drift::Project before = m_app->m_project;
    const QString sharedLinkId = groupSynced ? QUuid::createUuid().toString(QUuid::WithoutBraces) : QString();
    for (const UnitGroup &g : std::as_const(groups)) {
        for (const QPair<int, int> &p : std::as_const(g.refs)) {
            drift::Clip &clip = m_app->m_project.tracks()[p.first].clips[p.second];
            clip.timelineStart += g.delta;
            if (groupSynced)
                clip.linkId = sharedLinkId;
            if (muteScratchAudio && hasExternalAudio && clip.type == drift::ClipType::Video)
                clip.suppressEmbeddedAudio = true;
        }
    }

    m_app->pushProjectEdit(before, tr("Sync by audio"));
    m_app->finishEdit(tr("Sync by audio"));

    if (matches.size() == 1 && !groupSynced && !(muteScratchAudio && hasExternalAudio)) {
        m_app->setLastMessage(
            tr("Moved the second clip %1 s to match the first")
                .arg(drift::usToSeconds(groups.at(1).delta), 0, 'f', 3));
    } else if (unmatched > 0) {
        m_app->setLastMessage(
            tr("Synced %1 clip(s) by audio (%2 could not be matched)")
                .arg(okMatches.size())
                .arg(unmatched),
            QStringLiteral("warning"));
    } else {
        m_app->setLastMessage(
            tr("Synced %1 clip(s) by audio").arg(okMatches.size()));
    }
}

void AudioAlignController::autoDuckTrack(int targetTrackIndex, int driverTrackIndex,
                                         double duckDb, double thresholdRms,
                                         double attackSec, double releaseSec)
{
    const auto &tracks = m_app->m_project.tracks();
    if (targetTrackIndex < 0 || targetTrackIndex >= tracks.size()
        || driverTrackIndex < 0 || driverTrackIndex >= tracks.size()
        || targetTrackIndex == driverTrackIndex) {
        m_app->setLastMessage(tr("Select a different target and driver track for auto-ducking"),
                              QStringLiteral("warning"));
        return;
    }

    const drift::Track &targetTrack = tracks.at(targetTrackIndex);
    const drift::Track &driverTrack = tracks.at(driverTrackIndex);
    if (driverTrack.clips.isEmpty()) {
        m_app->setLastMessage(tr("The driver track has no clips to analyze"),
                              QStringLiteral("warning"));
        return;
    }

    drift::TimeUs driverEndUs = 0;
    for (const drift::Clip &clip : driverTrack.clips)
        driverEndUs = std::max(driverEndUs, clip.timelineEnd());
    driverEndUs = std::min<drift::TimeUs>(driverEndUs, drift::secondsToUs(3600.0));
    if (driverEndUs <= 0)
        return;

    const QString targetTrackId = targetTrack.id;
    const QString driverName = driverTrack.name;
    const double effThresh = qBound(0.001, thresholdRms, 0.5);
    const double effAttack = qBound(0.01, attackSec, 2.0);
    const double effRelease = qBound(0.02, releaseSec, 5.0);

    drift::Project driverSnap = m_app->m_project.detachedCopy();
    for (int i = 0; i < driverSnap.tracks().size(); ++i) {
        drift::Track &t = driverSnap.tracks()[i];
        t.muted = (i != driverTrackIndex);
        t.solo = false;
    }

    if (!begin(tr("Analyzing dialogue for auto-ducking…")))
        return;

    (void)QtConcurrent::run([this, driverSnap, driverEndUs, targetTrackId, driverName,
                             duckDb, effThresh, effAttack, effRelease]() {
        const qint64 totalFrames = qMax<qint64>(1, (driverEndUs * kAnalysisRate) / drift::kUsPerSecond);
        const auto alive = [this]() { return m_cancel.loadRelaxed() == 0; };
        const std::vector<float> mono = mixMono(driverSnap, 0, totalFrames, [this, &alive](double f) {
            reportProgress(0.85 * f, {});
            return alive();
        });
        const bool cancelled = !alive() || mono.empty();

        QList<QPair<double, double>> spans;
        if (!cancelled) {
            reportProgress(0.9, tr("Detecting speech regions…"));
            const int hop = kAnalysisRate / 20; // 50 ms windows
            const double winSec = double(hop) / double(kAnalysisRate);
            QList<QPair<double, double>> rawSpans;
            bool inSpan = false;
            double spanStart = 0.0;

            for (size_t off = 0; off + size_t(hop) <= mono.size(); off += size_t(hop)) {
                double sumSq = 0.0;
                for (int i = 0; i < hop; ++i) {
                    const double v = mono[off + size_t(i)];
                    sumSq += v * v;
                }
                const double rms = std::sqrt(sumSq / double(hop));
                const double tSec = double(off) / double(kAnalysisRate);
                if (rms >= effThresh) {
                    if (!inSpan) {
                        inSpan = true;
                        spanStart = tSec;
                    }
                } else if (inSpan) {
                    inSpan = false;
                    rawSpans.append({spanStart, tSec});
                }
            }
            if (inSpan)
                rawSpans.append({spanStart, double(mono.size()) / double(kAnalysisRate)});

            for (const auto &s : rawSpans) {
                if (s.second - s.first < winSec * 0.5)
                    continue;
                if (!spans.isEmpty() && s.first - spans.last().second < effRelease) {
                    spans.last().second = s.second;
                } else {
                    spans.append(s);
                }
            }
        }

        QMetaObject::invokeMethod(
            this,
            [this, cancelled, targetTrackId, driverName, spans, duckDb, effAttack, effRelease]() {
                end();
                if (m_cancel.loadRelaxed() != 0 || cancelled) {
                    m_app->setLastMessage(tr("Auto-ducking cancelled"));
                    return;
                }
                applyAutoDuck(targetTrackId, driverName, spans, duckDb, effAttack, effRelease);
            },
            Qt::QueuedConnection);
    });
}

void AudioAlignController::applyAutoDuck(const QString &targetTrackId, const QString &driverName,
                                         const QList<QPair<double, double>> &speechSpans,
                                         double duckDb, double attackSec, double releaseSec)
{
    int targetIdx = -1;
    for (int i = 0; i < m_app->m_project.tracks().size(); ++i) {
        if (m_app->m_project.tracks().at(i).id == targetTrackId) {
            targetIdx = i;
            break;
        }
    }
    if (targetIdx < 0)
        return;

    if (speechSpans.isEmpty()) {
        m_app->setLastMessage(tr("No active audio detected on “%1” to duck against").arg(driverName),
                              QStringLiteral("warning"));
        return;
    }

    const double duckLinear = std::pow(10.0, qBound(-48.0, duckDb, -0.5) / 20.0);
    const drift::Project before = m_app->m_project;

    drift::Track &targetTrack = m_app->m_project.tracks()[targetIdx];
    targetTrack.volumeEnvelope = drift::KeyframeTrack<double>();
    targetTrack.showVolumeEnvelope = true;

    double prevEnd = -1.0;
    if (speechSpans.first().first > attackSec)
        targetTrack.volumeEnvelope.setKeyframe(0, 1.0);

    for (const auto &span : speechSpans) {
        const double s = span.first;
        const double e = span.second;
        const double downStart = std::max(0.0, s - attackSec);
        if (downStart > prevEnd + 0.001)
            targetTrack.volumeEnvelope.setKeyframe(drift::secondsToUs(downStart), 1.0);
        targetTrack.volumeEnvelope.setKeyframe(drift::secondsToUs(s), duckLinear);
        targetTrack.volumeEnvelope.setKeyframe(drift::secondsToUs(e), duckLinear);
        const double upEnd = e + releaseSec;
        targetTrack.volumeEnvelope.setKeyframe(drift::secondsToUs(upEnd), 1.0);
        prevEnd = upEnd;
    }

    m_app->pushProjectEdit(before, tr("Auto-duck audio track"));
    m_app->finishEdit(tr("Auto-duck audio track"));
    m_app->setLastMessage(
        tr("Auto-ducked “%1” under “%2” (%n region(s))", "", speechSpans.size())
            .arg(targetTrack.name, driverName),
        QStringLiteral("success"));
}

#include "moc_AudioAlignController.cpp"
