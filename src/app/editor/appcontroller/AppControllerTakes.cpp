#include "app/editor/appcontroller/AppControllerInternal.h"
#include "app/editor/AppController.h"
#include "app/jobs/audio/AudioAlignController.h"
#include "app/bin/AssetLibrary.h"
#include "core/edit/TimelineOps.h"

#include <QFileInfo>
#include <QTimer>
#include <QUuid>
#include <algorithm>

using namespace drift::appcontroller_detail;

namespace {

void syncClipToActiveTake(drift::Clip &clip)
{
    if (clip.takes.isEmpty() || clip.activeTakeIndex < 0 || clip.activeTakeIndex >= clip.takes.size())
        return;
    drift::ClipTake &cur = clip.takes[clip.activeTakeIndex];
    cur.assetId = clip.assetId;
    cur.path = clip.path;
    cur.name = clip.name;
    cur.srcIn = clip.srcIn;
    cur.srcOut = clip.srcOut;
    cur.audioStreamIndex = clip.audioStreamIndex;
    cur.thumbnailPath = clip.thumbnailPath;
    cur.filmstripPath = clip.filmstripPath;
    cur.effects = clip.effects;
}

void loadActiveTakeIntoClip(drift::Clip &clip, const drift::Project &project)
{
    if (clip.takes.isEmpty() || clip.activeTakeIndex < 0 || clip.activeTakeIndex >= clip.takes.size())
        return;
    const drift::ClipTake &next = clip.takes.at(clip.activeTakeIndex);
    clip.assetId = next.assetId;
    clip.path = next.path;
    clip.name = next.name;
    clip.srcIn = next.srcIn;
    const drift::TimeUs curSpan = clip.sourceSpanUs();
    if (next.srcOut > next.srcIn) {
        clip.srcOut = next.srcOut;
    } else {
        clip.srcOut = clip.srcIn + curSpan;
    }
    clip.audioStreamIndex = next.audioStreamIndex;
    clip.thumbnailPath = next.thumbnailPath;
    clip.filmstripPath = next.filmstripPath;
    if (!next.effects.isEmpty()) {
        clip.effects = next.effects;
    }
    if (const drift::MediaAsset *a = project.asset(clip.assetId)) {
        if (clip.thumbnailPath.isEmpty())
            clip.thumbnailPath = a->thumbnailPath;
        if (clip.filmstripPath.isEmpty())
            clip.filmstripPath = a->filmstripPath;
        if (a->durationUs > 0 && !clip.loops()) {
            if (clip.srcIn >= a->durationUs)
                clip.srcIn = 0;
            clip.srcOut = qMin(clip.srcIn + curSpan, a->durationUs);
            clip.syncSrcOutFromSpeed(a->durationUs);
        }
    }
}

} // namespace

void AppController::cycleTake(int direction)
{
    QList<QPair<int, int>> pairs = m_selection;
    if (pairs.isEmpty() && isValidClipIndex(m_selectedTrack, m_selectedClip))
        pairs = {qMakePair(m_selectedTrack, m_selectedClip)};
    if (pairs.isEmpty())
        return;
    expandSelectionWithLinkedPartners(m_project, pairs);

    bool hasMultiTakes = false;
    for (const auto &p : std::as_const(pairs)) {
        if (isValidClipIndex(p.first, p.second)) {
            if (m_project.tracks().at(p.first).clips.at(p.second).takes.size() > 1) {
                hasMultiTakes = true;
                break;
            }
        }
    }
    if (!hasMultiTakes)
        return;

    const drift::Project before = m_project.detachedCopy();
    QSet<QString> handledIds;

    for (const auto &p : std::as_const(pairs)) {
        if (!isValidClipIndex(p.first, p.second))
            continue;
        drift::Clip &clip = m_project.tracks()[p.first].clips[p.second];
        if (handledIds.contains(clip.id))
            continue;
        handledIds.insert(clip.id);

        if (clip.takes.size() > 1) {
            syncClipToActiveTake(clip);
            int nextIdx = 0;
            if (direction > 0)
                nextIdx = (clip.activeTakeIndex + 1) % clip.takes.size();
            else
                nextIdx = (clip.activeTakeIndex - 1 + clip.takes.size()) % clip.takes.size();
            clip.activeTakeIndex = nextIdx;
            loadActiveTakeIntoClip(clip, m_project);
        }
    }

    const QString label = direction > 0 ? tr("Next Take") : tr("Previous Take");
    pushProjectEdit(before, label);
    syncClipModels();
    finishEdit(label);
}

void AppController::nextTake()
{
    cycleTake(1);
}

void AppController::previousTake()
{
    cycleTake(-1);
}

void AppController::setActiveTake(int trackIndex, int clipIndex, int takeIndex)
{
    if (!isValidClipIndex(trackIndex, clipIndex))
        return;
    drift::Clip &clip = m_project.tracks()[trackIndex].clips[clipIndex];
    if (takeIndex < 0 || takeIndex >= clip.takes.size() || takeIndex == clip.activeTakeIndex)
        return;

    const drift::Project before = m_project.detachedCopy();
    syncClipToActiveTake(clip);
    clip.activeTakeIndex = takeIndex;
    loadActiveTakeIntoClip(clip, m_project);

    // Synchronize companion clips (e.g. linked audio track)
    if (!clip.linkId.isEmpty()) {
        for (int ti = 0; ti < m_project.tracks().size(); ++ti) {
            for (int ci = 0; ci < m_project.tracks()[ti].clips.size(); ++ci) {
                if (ti == trackIndex && ci == clipIndex)
                    continue;
                drift::Clip &partner = m_project.tracks()[ti].clips[ci];
                if (partner.linkId == clip.linkId && partner.takes.size() == clip.takes.size()
                    && partner.activeTakeIndex != takeIndex) {
                    syncClipToActiveTake(partner);
                    partner.activeTakeIndex = takeIndex;
                    loadActiveTakeIntoClip(partner, m_project);
                }
            }
        }
    }

    const QString label = tr("Set Active Take");
    pushProjectEdit(before, label);
    syncClipModels();
    finishEdit(label);
}

bool AppController::addTakeFromAsset(int trackIndex, int clipIndex, int assetIndex)
{
    if (!m_assetLibrary || !isValidClipIndex(trackIndex, clipIndex))
        return false;
    const QString newAssetId = m_assetLibrary->assetIdAt(assetIndex);
    const drift::MediaAsset *asset = m_project.asset(newAssetId);
    if (!asset)
        return false;

    const drift::Project before = m_project.detachedCopy();
    drift::Clip &clip = m_project.tracks()[trackIndex].clips[clipIndex];

    if (clip.takes.isEmpty()) {
        drift::ClipTake t0;
        t0.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        t0.name = clip.name.isEmpty() ? QFileInfo(clip.path).fileName() : clip.name;
        t0.assetId = clip.assetId;
        t0.path = clip.path;
        t0.srcIn = clip.srcIn;
        t0.srcOut = clip.srcOut;
        t0.audioStreamIndex = clip.audioStreamIndex;
        t0.thumbnailPath = clip.thumbnailPath;
        t0.filmstripPath = clip.filmstripPath;
        t0.effects = clip.effects;
        clip.takes.append(t0);
        clip.activeTakeIndex = 0;
    } else {
        syncClipToActiveTake(clip);
    }

    drift::ClipTake tNew;
    tNew.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    tNew.name = asset->name.isEmpty() ? QFileInfo(asset->path).fileName() : asset->name;
    tNew.assetId = asset->id;
    tNew.path = asset->path;
    tNew.srcIn = 0;
    tNew.srcOut = clip.sourceSpanUs();
    tNew.audioStreamIndex = 0;
    tNew.thumbnailPath = asset->thumbnailPath;
    tNew.filmstripPath = asset->filmstripPath;
    tNew.effects = clip.effects;
    clip.takes.append(tNew);

    clip.activeTakeIndex = clip.takes.size() - 1;
    loadActiveTakeIntoClip(clip, m_project);

    // Linked partner
    if (!clip.linkId.isEmpty()) {
        for (int ti = 0; ti < m_project.tracks().size(); ++ti) {
            for (int ci = 0; ci < m_project.tracks()[ti].clips.size(); ++ci) {
                if (ti == trackIndex && ci == clipIndex)
                    continue;
                drift::Clip &partner = m_project.tracks()[ti].clips[ci];
                if (partner.linkId == clip.linkId) {
                    if (partner.takes.isEmpty()) {
                        drift::ClipTake pt0;
                        pt0.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
                        pt0.name = partner.name.isEmpty() ? QFileInfo(partner.path).fileName() : partner.name;
                        pt0.assetId = partner.assetId;
                        pt0.path = partner.path;
                        pt0.srcIn = partner.srcIn;
                        pt0.srcOut = partner.srcOut;
                        pt0.audioStreamIndex = partner.audioStreamIndex;
                        pt0.thumbnailPath = partner.thumbnailPath;
                        pt0.filmstripPath = partner.filmstripPath;
                        pt0.effects = partner.effects;
                        partner.takes.append(pt0);
                        partner.activeTakeIndex = 0;
                    } else {
                        syncClipToActiveTake(partner);
                    }
                    drift::ClipTake ptNew;
                    ptNew.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
                    ptNew.name = tNew.name;
                    ptNew.assetId = asset->id;
                    ptNew.path = asset->path;
                    ptNew.srcIn = 0;
                    ptNew.srcOut = partner.sourceSpanUs();
                    ptNew.audioStreamIndex = 0;
                    ptNew.thumbnailPath = asset->thumbnailPath;
                    ptNew.filmstripPath = asset->filmstripPath;
                    ptNew.effects = partner.effects;
                    partner.takes.append(ptNew);
                    partner.activeTakeIndex = partner.takes.size() - 1;
                    loadActiveTakeIntoClip(partner, m_project);
                }
            }
        }
    }

    const QString label = tr("Add Take");
    pushProjectEdit(before, label);
    syncClipModels();
    finishEdit(label);
    return true;
}

bool AppController::deleteActiveTake(int trackIndex, int clipIndex)
{
    if (!isValidClipIndex(trackIndex, clipIndex))
        return false;
    drift::Clip &clip = m_project.tracks()[trackIndex].clips[clipIndex];
    if (clip.takes.size() <= 1)
        return false;

    const drift::Project before = m_project.detachedCopy();
    const int removedIdx = clip.activeTakeIndex;
    clip.takes.removeAt(removedIdx);
    clip.activeTakeIndex = qBound(0, removedIdx, clip.takes.size() - 1);
    loadActiveTakeIntoClip(clip, m_project);

    // Linked partner
    if (!clip.linkId.isEmpty()) {
        for (int ti = 0; ti < m_project.tracks().size(); ++ti) {
            for (int ci = 0; ci < m_project.tracks()[ti].clips.size(); ++ci) {
                if (ti == trackIndex && ci == clipIndex)
                    continue;
                drift::Clip &partner = m_project.tracks()[ti].clips[ci];
                if (partner.linkId == clip.linkId && partner.takes.size() > removedIdx) {
                    partner.takes.removeAt(removedIdx);
                    partner.activeTakeIndex = qBound(0, partner.activeTakeIndex, partner.takes.size() - 1);
                    loadActiveTakeIntoClip(partner, m_project);
                }
            }
        }
    }

    const QString label = tr("Delete Active Take");
    pushProjectEdit(before, label);
    syncClipModels();
    finishEdit(label);
    return true;
}

QVariantList AppController::takesForClip(int trackIndex, int clipIndex) const
{
    QVariantList result;
    if (!isValidClipIndex(trackIndex, clipIndex))
        return result;
    const drift::Clip &clip = m_project.tracks().at(trackIndex).clips.at(clipIndex);
    for (int i = 0; i < clip.takes.size(); ++i) {
        const drift::ClipTake &t = clip.takes.at(i);
        result.append(QVariantMap{
            {QStringLiteral("index"), i},
            {QStringLiteral("id"), t.id},
            {QStringLiteral("name"), t.name},
            {QStringLiteral("assetId"), t.assetId},
            {QStringLiteral("path"), t.path},
            {QStringLiteral("isActive"), i == clip.activeTakeIndex},
        });
    }
    return result;
}

// --- Inline Multicamera Mode (Tier 2C) ---------------------------------------

bool AppController::findActiveInlineMulticamClip(int *outTrackIndex, int *outClipIndex) const
{
    if (outTrackIndex)
        *outTrackIndex = -1;
    if (outClipIndex)
        *outClipIndex = -1;

    const auto isMultiTakeVideo = [](const drift::Track &track, const drift::Clip &clip) {
        return track.type == drift::TrackType::Video && !track.hidden
               && clip.type == drift::ClipType::Video && clip.takes.size() >= 2;
    };

    const QList<drift::Track> &tracks = m_project.tracks();
    if (isValidClipIndex(m_selectedTrack, m_selectedClip)) {
        const drift::Track &selTrack = tracks.at(m_selectedTrack);
        const drift::Clip &selClip = selTrack.clips.at(m_selectedClip);
        if (isMultiTakeVideo(selTrack, selClip) && selClip.containsTime(m_playheadUs)) {
            if (outTrackIndex)
                *outTrackIndex = m_selectedTrack;
            if (outClipIndex)
                *outClipIndex = m_selectedClip;
            return true;
        }
    }

    for (int ti = 0; ti < tracks.size(); ++ti) {
        const drift::Track &track = tracks.at(ti);
        if (track.type != drift::TrackType::Video || track.hidden)
            continue;
        for (int ci = 0; ci < track.clips.size(); ++ci) {
            const drift::Clip &clip = track.clips.at(ci);
            if (clip.type == drift::ClipType::Video && clip.takes.size() >= 2
                && clip.containsTime(m_playheadUs)) {
                if (outTrackIndex)
                    *outTrackIndex = ti;
                if (outClipIndex)
                    *outClipIndex = ci;
                return true;
            }
        }
    }

    if (isValidClipIndex(m_selectedTrack, m_selectedClip)) {
        const drift::Track &selTrack = tracks.at(m_selectedTrack);
        const drift::Clip &selClip = selTrack.clips.at(m_selectedClip);
        if (isMultiTakeVideo(selTrack, selClip)) {
            if (outTrackIndex)
                *outTrackIndex = m_selectedTrack;
            if (outClipIndex)
                *outClipIndex = m_selectedClip;
            return true;
        }
    }
    return false;
}

QVariantList AppController::inlineMulticamTakes() const
{
    int ti = -1;
    int ci = -1;
    if (!findActiveInlineMulticamClip(&ti, &ci))
        return {};
    const drift::Clip &clip = m_project.tracks().at(ti).clips.at(ci);
    QVariantList out;
    out.reserve(clip.takes.size());
    for (int i = 0; i < clip.takes.size(); ++i) {
        const drift::ClipTake &take = clip.takes.at(i);
        out.append(QVariantMap{
            {QStringLiteral("index"), i},
            {QStringLiteral("name"), take.name.isEmpty() ? tr("Angle %1").arg(i + 1) : take.name},
            {QStringLiteral("active"), i == clip.activeTakeIndex},
            {QStringLiteral("assetId"), take.assetId},
        });
    }
    return out;
}

bool AppController::createMulticamTrackFromSelection()
{
    QList<QPair<int, int>> pairs = m_selection;
    if (isValidClipIndex(m_selectedTrack, m_selectedClip)
        && !pairs.contains(qMakePair(m_selectedTrack, m_selectedClip))) {
        pairs.prepend(qMakePair(m_selectedTrack, m_selectedClip));
    }

    const QList<drift::Track> &tracks = m_project.tracks();
    QList<QPair<int, int>> videoClips;
    for (const auto &pair : std::as_const(pairs)) {
        if (!isValidClipIndex(pair.first, pair.second))
            continue;
        if (tracks.at(pair.first).type != drift::TrackType::Video)
            continue;
        if (tracks.at(pair.first).clips.at(pair.second).type != drift::ClipType::Video)
            continue;
        if (!videoClips.contains(pair))
            videoClips.append(pair);
    }
    if (videoClips.size() < 2)
        return false;

    std::stable_sort(videoClips.begin(), videoClips.end(),
                     [&tracks](const QPair<int, int> &a, const QPair<int, int> &b) {
                         if (a.first != b.first)
                             return a.first < b.first;
                         return tracks.at(a.first).clips.at(a.second).timelineStart
                                < tracks.at(b.first).clips.at(b.second).timelineStart;
                     });

    const int targetTrack = videoClips.first().first;
    drift::TimeUs minStart = tracks.at(videoClips.first().first).clips.at(videoClips.first().second).timelineStart;
    drift::TimeUs maxEnd = tracks.at(videoClips.first().first).clips.at(videoClips.first().second).timelineEnd();
    for (const auto &pair : std::as_const(videoClips)) {
        const drift::Clip &c = tracks.at(pair.first).clips.at(pair.second);
        minStart = qMin(minStart, c.timelineStart);
        maxEnd = qMax(maxEnd, c.timelineEnd());
    }
    if (maxEnd <= minStart)
        return false;

    drift::Clip combined = tracks.at(videoClips.first().first).clips.at(videoClips.first().second);
    combined.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    combined.linkId.clear();
    combined.timelineStart = minStart;
    combined.timelineDuration = maxEnd - minStart;
    combined.takes.clear();

    for (const auto &pair : std::as_const(videoClips)) {
        const drift::Clip &src = tracks.at(pair.first).clips.at(pair.second);
        if (!src.takes.isEmpty()) {
            for (const drift::ClipTake &existing : src.takes)
                combined.takes.append(existing);
        } else {
            drift::ClipTake t;
            t.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
            t.name = src.name.isEmpty() ? QFileInfo(src.path).fileName() : src.name;
            t.assetId = src.assetId;
            t.path = src.path;
            t.srcIn = src.srcIn;
            t.srcOut = src.srcOut;
            t.audioStreamIndex = src.audioStreamIndex;
            t.thumbnailPath = src.thumbnailPath;
            t.filmstripPath = src.filmstripPath;
            t.effects = src.effects;
            combined.takes.append(t);
        }
    }
    if (combined.takes.size() < 2)
        return false;
    combined.activeTakeIndex = 0;

    const drift::Project before = m_project.detachedCopy();
    loadActiveTakeIntoClip(combined, m_project);

    // Remove source clips in descending (trackIndex, clipIndex) order so indices stay valid.
    QList<QPair<int, int>> toRemove = videoClips;
    std::sort(toRemove.begin(), toRemove.end(),
              [](const QPair<int, int> &a, const QPair<int, int> &b) {
                  if (a.first != b.first)
                      return a.first > b.first;
                  return a.second > b.second;
              });
    for (const auto &pair : std::as_const(toRemove))
        m_project.tracks()[pair.first].clips.removeAt(pair.second);

    QList<drift::Clip> &targetClips = m_project.tracks()[targetTrack].clips;
    int insertIdx = targetClips.size();
    for (int i = 0; i < targetClips.size(); ++i) {
        if (targetClips.at(i).timelineStart >= combined.timelineStart) {
            insertIdx = i;
            break;
        }
    }
    targetClips.insert(insertIdx, combined);

    pushProjectEdit(before, tr("Create multicamera track"));
    syncClipModels();
    finishEdit(tr("Multicamera track created"));
    selectClip(targetTrack, insertIdx);
    emit inlineMulticamChanged();
    if (m_inlineMulticamEnabled)
        refreshInlineMulticamFrames();
    return true;
}

void AppController::setInlineMulticamEnabled(bool enabled)
{
    if (m_inlineMulticamEnabled == enabled)
        return;
    m_inlineMulticamEnabled = enabled;
    if (enabled) {
        if (!m_multicamTimer) {
            m_multicamTimer = new QTimer(this);
            m_multicamTimer->setInterval(66);
            connect(m_multicamTimer, &QTimer::timeout, this, [this] { refreshMulticamTiles(); });
        }
        if (m_playing)
            m_multicamTimer->start();
        refreshInlineMulticamFrames();
    } else if (!m_multicamActive && m_multicamTimer) {
        m_multicamTimer->stop();
    }
    emit inlineMulticamChanged();
}

void AppController::toggleInlineMulticam()
{
    setInlineMulticamEnabled(!m_inlineMulticamEnabled);
}

bool AppController::punchMulticamTake(int takeIndex)
{
    int ti = -1;
    int ci = -1;
    if (!findActiveInlineMulticamClip(&ti, &ci))
        return false;

    const drift::Clip &check = m_project.tracks().at(ti).clips.at(ci);
    if (takeIndex < 0 || takeIndex >= check.takes.size())
        return false;

    const drift::TimeUs offset = m_playheadUs - check.timelineStart;
    const drift::TimeUs minEdge = drift::minClipDurationUs(m_project.fps());

    const drift::Project before = m_project.detachedCopy();
    drift::Clip &clip = m_project.tracks()[ti].clips[ci];
    syncClipToActiveTake(clip);

    int selectIdx = ci;
    if (offset >= minEdge && clip.timelineDuration - offset >= minEdge) {
        drift::Clip tail = clip;
        if (drift::splitClipAtOffset(clip, tail, offset, m_project.fps())) {
            tail.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
            tail.activeTakeIndex = takeIndex;
            loadActiveTakeIntoClip(tail, m_project);
            m_project.tracks()[ti].clips.insert(ci + 1, tail);
            selectIdx = ci + 1;
        } else {
            if (clip.activeTakeIndex == takeIndex)
                return false;
            clip.activeTakeIndex = takeIndex;
            loadActiveTakeIntoClip(clip, m_project);
        }
    } else {
        if (clip.activeTakeIndex == takeIndex)
            return false;
        clip.activeTakeIndex = takeIndex;
        loadActiveTakeIntoClip(clip, m_project);
    }

    pushProjectEdit(before, tr("Cut multicam angle"));
    syncClipModels();
    finishEdit(tr("Multicam angle cut"));
    selectClip(ti, selectIdx);
    emit inlineMulticamChanged();
    if (m_inlineMulticamEnabled)
        refreshInlineMulticamFrames();
    return true;
}

bool AppController::syncAndStartInlineMulticam(bool muteScratchAudio)
{
    if (m_audioAlign && m_audioAlign->canSyncByAudio()) {
        auto conn = std::make_shared<QMetaObject::Connection>();
        *conn = connect(m_audioAlign, &AudioAlignController::runningChanged, this, [this, conn]() {
            if (m_audioAlign && !m_audioAlign->running()) {
                QObject::disconnect(*conn);
                if (createMulticamTrackFromSelection())
                    setInlineMulticamEnabled(true);
            }
        });
        m_audioAlign->syncByAudio(/*groupSynced=*/false, muteScratchAudio);
        return true;
    }

    if (createMulticamTrackFromSelection()) {
        setInlineMulticamEnabled(true);
        return true;
    }
    return false;
}

int AppController::multicamCutCount() const
{
    int ti = -1;
    int ci = -1;
    if (findActiveInlineMulticamClip(&ti, &ci) && ti >= 0 && ti < m_project.tracks().size())
        return m_project.tracks().at(ti).clips.size();
    for (const drift::Track &track : m_project.tracks()) {
        if (track.type != drift::TrackType::Video)
            continue;
        for (const drift::Clip &c : track.clips) {
            if (c.takes.size() > 1)
                return track.clips.size();
        }
    }
    return 0;
}

bool AppController::jumpToAdjacentMulticamCut(int direction)
{
    if (direction == 0)
        return false;

    int targetTrack = -1;
    int ci = -1;
    if (!findActiveInlineMulticamClip(&targetTrack, &ci)) {
        for (int ti = 0; ti < m_project.tracks().size(); ++ti) {
            const drift::Track &track = m_project.tracks().at(ti);
            if (track.type != drift::TrackType::Video)
                continue;
            for (const drift::Clip &c : track.clips) {
                if (c.takes.size() > 1) {
                    targetTrack = ti;
                    break;
                }
            }
            if (targetTrack >= 0)
                break;
        }
    }
    if (targetTrack < 0 || targetTrack >= m_project.tracks().size())
        return false;

    QList<drift::TimeUs> starts;
    for (const drift::Clip &c : m_project.tracks().at(targetTrack).clips) {
        if (!starts.contains(c.timelineStart))
            starts.append(c.timelineStart);
    }
    if (starts.isEmpty())
        return false;
    std::sort(starts.begin(), starts.end());

    constexpr drift::TimeUs kTolUs = 10'000;
    drift::TimeUs targetUs = -1;
    if (direction > 0) {
        for (drift::TimeUs s : std::as_const(starts)) {
            if (s > m_playheadUs + kTolUs) {
                targetUs = s;
                break;
            }
        }
    } else {
        for (int i = starts.size() - 1; i >= 0; --i) {
            if (starts.at(i) < m_playheadUs - kTolUs) {
                targetUs = starts.at(i);
                break;
            }
        }
    }

    if (targetUs < 0)
        return false;
    setPlayheadSeconds(static_cast<double>(targetUs) / static_cast<double>(drift::kUsPerSecond));
    return true;
}
