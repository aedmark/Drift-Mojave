#include "app/platform/FileDialogs.h"
#include "engine/platform/AndroidUri.h"
#include "app/editor/appcontroller/AppControllerInternal.h"
#include "app/editor/sessions/CurveEditorController.h"
#include "app/jobs/render/BakeController.h"
#include "app/jobs/vision/FaceTrackingController.h"
#include "app/jobs/vision/SegmentationController.h"
#include "app/bin/MediaBinController.h"
#include "app/jobs/vision/StabilizeController.h"
#include "app/editor/sessions/SpeedCurveController.h"
#include "app/editor/ProjectFileController.h"
#include "app/mcp/McpController.h"
#include "app/mcp/protocol/McpJson.h"
#include "ui/preview/PreviewController.h"

#include "core/interchange/TimelineExporter.h"
#include "core/interchange/VegasReader.h"
#include "engine/audio/AudioMixer.h"
#include "engine/decode/readers/ClipReaderPool.h"
#include "engine/project/ProjectBundle.h"
#include "engine/diagnostics/DebugReport.h"
#include "engine/project/ProjectDependencies.h"
#include "engine/encode/export/Exporter.h"
#include "engine/packages/assets/EmojiCatalog.h"
// Engine-internal by its own header comment, and included here only for releaseCaches(): the GPU
// caches have no other owner outside src/engine to ask. A one-line forwarder on GpuCompositor
// would restore the boundary.
#include "engine/gpu/GlRuntime.h"
#include "playback/stats/PlaybackDiagnostics.h"
#include "engine/packages/assets/StickerCatalog.h"
#ifdef DRIFT_WITH_SKIA
#include "engine/render2d/text/SkiaTextPainter.h"
#endif

#include <QClipboard>
#include <QDesktopServices>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>

#include <QFileInfo>
#include <QMimeDatabase>
#include <QUrl>
#include <QtConcurrent/QtConcurrentRun>

#include <QCryptographicHash>
#include <QTimer>
#include "app/editor/PreferencesController.h"
#include <QSettings>
#include <QStandardPaths>
#include <QDir>
#include <QImage>
#include <QLinearGradient>
#include <QPainter>
#include <QRadialGradient>
#include <QSaveFile>
#include <QtEndian>
using namespace drift::appdetail;
using drift::mcpdetail::findClipById;
using drift::mcpdetail::runBeatAnalysis;
using namespace drift::appcontroller_detail;


QByteArray AppController::serializeProjectJson() const
{
    QJsonObject root = m_project.toJson();
    const QJsonObject session = sessionJson();
    for (auto it = session.constBegin(); it != session.constEnd(); ++it)
        root.insert(it.key(), it.value());
    if (persistsUndoHistory()) {
        if (const QJsonObject history = historyJson(drift::captureHistory(m_undoStack)); !history.isEmpty())
            root.insert(QStringLiteral("undoHistory"), history);
    }
    return QJsonDocument(root).toJson(QJsonDocument::Indented);
}

// What a saved project records beside the project itself: view and editing state, read on the GUI
// thread so the project half can be serialized anywhere.
QJsonObject AppController::sessionJson() const
{
    QJsonObject root;
    root.insert(QStringLiteral("playheadUs"), static_cast<double>(m_playheadUs));
    root.insert(QStringLiteral("snapEnabled"), m_snapEnabled);
    root.insert(QStringLiteral("rippleEnabled"), m_rippleEnabled);
    root.insert(QStringLiteral("rippleMode"), drift::rippleModeToString(m_rippleMode));
    root.insert(QStringLiteral("allowClipOverlap"), m_allowClipOverlap);
    root.insert(QStringLiteral("mediaViewMode"), m_mediaViewMode);
    // Still written so a project saved here opens with the right view in builds that
    // predate the tree mode.
    root.insert(QStringLiteral("mediaGridMode"), mediaGridMode());
    root.insert(QStringLiteral("loopWorkArea"), m_loopWorkAreaEnabled);
    if (hasProjectHistorySettings()) {
        QJsonObject history;
        if (m_projectHistorySteps != kProjectHistoryUnset)
            history.insert(QStringLiteral("steps"), m_projectHistorySteps);
        if (m_projectHistoryPersist >= 0)
            history.insert(QStringLiteral("persist"), m_projectHistoryPersist == 1);
        root.insert(QStringLiteral("undoHistorySettings"), history);
    }
    QJsonArray guideSets;
    for (const QString &id : m_activeGuideSets) {
        const drift::GuideSet *set = findGuideSet(id);
        if (set && !set->builtIn)
            guideSets.append(drift::guideSetToJson(*set));
    }
    root.insert(QStringLiteral("guides"), QJsonObject{
        {QStringLiteral("enabled"), m_preview->guidesEnabled()},
        {QStringLiteral("active"), QJsonArray::fromStringList(m_activeGuideSets)},
        {QStringLiteral("sets"), guideSets},
    });
    return root;
}

void AppController::resetSessionState()
{
    // A replace whose probe the bin is about to drop would leave its row busy for good.
    if (m_bin)
        m_bin->dropPendingReplace();
    ++m_documentGeneration;
    m_projectHistorySteps = kProjectHistoryUnset;
    m_projectHistoryPersist = -1;
    // These end up editing the project, so they have to run while it is still the one they were
    // opened against.
    m_speedCurve->end();
    m_curves->endFade();
    m_segmentation->endSession();
    endMulticamSession();

    m_clipboard.clear();
    // Keyed by timeline position rather than by source, so entries from the old project would be
    // served for the same span in the new one.
    m_subtitleWaveformCache.clear();
    m_subtitleWaveformPending.clear();
    // Beats are timeline-absolute for the same reason.
    clearBeatAnalysis();
    // Also drops the failed-source blacklist, so media that was missing gets another chance.
    m_filmstripTiles.clear();
    // Blocks are keyed by source path, so they would not go stale across documents — this is
    // about memory. Nothing else evicts them beyond the size budget, so a session that opens
    // several projects in turn would otherwise hold the peaks of every source in all of them.
    m_waveformBlocks.clear();

    m_sequencePlayheads.clear();
    m_pendingEffectTemplate.reset();
    m_previewDragActive = false;
    m_previewDragAuto = false;
    m_previewDragDirty = false;
    m_previewAutoCommit->stop();
    if (m_transitionPreview.active) {
        m_transitionPreview = {};
        m_playback.clearPreviewRange();
    }
    m_keyframeGraphHiddenProperties.clear();
    m_preview->setCanvasCropMode(false);
    m_preview->setGuideEditSetId(QString());
    setSubtitleEditing(false);
    setSelectedSubtitleCue(-1);
    m_expandedEditActive = false;
    m_expandedEditTrack = -1;
    m_expandedEditLeftClip = -1;
    emit expandedEditChanged();

    emit projectReset();
}

bool AppController::applyProjectJson(const QByteArray &data, QString *error)
{
    std::optional<ParsedProject> parsed = parseProjectJson(data, error);
    if (!parsed)
        return false;
    applyParsedProject(std::move(*parsed));
    return true;
}

std::optional<AppController::ParsedProject> AppController::parseProjectJson(const QByteArray &data,
                                                                             QString *error)
{
    const QJsonDocument document = QJsonDocument::fromJson(data);
    if (!document.isObject()) {
        if (error)
            *error = QStringLiteral("Invalid project file");
        return std::nullopt;
    }
    return parseProjectJson(document.object(), error);
}

std::optional<AppController::ParsedProject> AppController::parseProjectJson(const QJsonObject &root,
                                                                             QString *error)
{
    // Parse into a local and check before touching anything: a project we end up rejecting must
    // leave the open one exactly as it was, session state included.
    QString parseError;
    drift::Project project = drift::Project::fromJson(root, &parseError);
    if (!parseError.isEmpty()) {
        if (error)
            *error = parseError;
        return std::nullopt;
    }
    ParsedProject parsed{std::move(project), root, std::nullopt};
    // A history that fails to decode is dropped; the project itself still opens.
    if (root.contains(QStringLiteral("undoHistory")))
        parsed.history = drift::historyFromJson(root.value(QStringLiteral("undoHistory")).toObject());
    return parsed;
}

void AppController::applyParsedProject(ParsedProject parsed)
{
    const QJsonObject &root = parsed.root;

    resetSessionState();
    m_project = std::move(parsed.project);

    // Stickers moved out of the QRC and into an addon, so projects saved before that store paths
    // like ":/qt/qml/Drift/resources/stickers/grinning.png" that no longer resolve. Repoint them
    // at the installed pack; a sticker with no installed pack keeps its old path and simply fails
    // to load, which is the same outcome as a missing media file.
    //
    // Media that travelled inside the bundle now lives in this machine's extraction directory, so
    // the saved absolute paths have to be repointed before anything probes them. Same class of
    // fix-up as the two migrations around it.
    //
    // Emoji clips point at a raster in this machine's app data cache, which a project opened
    // elsewhere will not have. The glyph sequence is what was saved, so re-derive the path — the
    // render is cached, and without the font addon it comes back empty and the clip fails to load
    // like any other missing file.
    //
    // Undo history states are full projects and get the same treatment, once per distinct state.
    const QHash<QString, QString> pathRemap = m_projectFile->pendingPathRemap();
    m_projectFile->pendingPathRemap().clear();
    const auto migrate = [&pathRemap](drift::Project &project) {
        for (drift::MediaAsset &asset : project.assets()) {
            const QString migrated = resolveLegacyStickerPath(asset.path);
            if (!migrated.isEmpty())
                asset.path = migrated;
        }
        ProjectFileController::remapPaths(project, pathRemap);
        project.forEachTrackList([&](QList<drift::Track> &tracks) {
            for (drift::Track &track : tracks) {
                for (drift::Clip &clip : track.clips) {
                    if (clip.emoji.isEmpty())
                        continue;
                    const QString path = emojiImagePath(clip.emoji);
                    if (path.isEmpty())
                        continue;
                    clip.path = path;
                    clip.thumbnailPath = path;
                    clip.filmstripPath = path;
                }
            }
        });
    };
    if (!pathRemap.isEmpty())
        m_filmstripTiles.clear();
    migrate(m_project);
    if (parsed.history) {
        for (drift::Project &state : parsed.history->states)
            migrate(state);
    }

    m_stabilize->migrateLegacyClips();

    if (m_assetLibrary)
        m_assetLibrary->setProject(&m_project, true);
    m_bin->folderModel()->setProject(&m_project);
    m_bin->setCurrentFolderId(QString());

    m_projectFile->rehydrateMissingSources();

    m_projectFile->reportMissingCatalogEntries();

    setPlaying(false);
    m_snapEnabled = root.value(QStringLiteral("snapEnabled")).toBool(true);
    m_rippleEnabled = root.value(QStringLiteral("rippleEnabled")).toBool(false);
    m_rippleMode = drift::rippleModeFromString(root.value(QStringLiteral("rippleMode")).toString());
    m_allowClipOverlap = root.value(QStringLiteral("allowClipOverlap")).toBool(false);

    if (root.contains(QStringLiteral("mediaViewMode"))) {
        setMediaViewMode(root.value(QStringLiteral("mediaViewMode")).toString());
    } else if (root.contains(QStringLiteral("mediaGridMode"))) {
        setMediaGridMode(root.value(QStringLiteral("mediaGridMode")).toBool(true));
    }

    if (root.contains(QStringLiteral("loopWorkArea"))) {
        setLoopWorkAreaEnabled(root.value(QStringLiteral("loopWorkArea")).toBool(false));
    } else {
        m_playback.setLoopWorkArea(m_loopWorkAreaEnabled);
    }

    // Projects from before guides were saved keep whatever the session had.
    if (root.contains(QStringLiteral("guides"))) {
        const QJsonObject guides = root.value(QStringLiteral("guides")).toObject();
        m_preview->restoreGuidesEnabled(guides.value(QStringLiteral("enabled")).toBool(false));
        m_projectGuideSets.clear();
        for (const QJsonValue &value : guides.value(QStringLiteral("sets")).toArray()) {
            drift::GuideSet set = drift::guideSetFromJson(value.toObject());
            if (!set.id.isEmpty())
                m_projectGuideSets.append(set);
        }
        m_activeGuideSets.clear();
        for (const QJsonValue &value : guides.value(QStringLiteral("active")).toArray()) {
            const QString id = value.toString();
            if (findGuideSet(id) && !m_activeGuideSets.contains(id))
                m_activeGuideSets.append(id);
        }
    }

    if (root.contains(QStringLiteral("playheadUs"))) {
        setPlayheadUs(static_cast<drift::TimeUs>(root.value(QStringLiteral("playheadUs")).toDouble()));
    } else {
        setPlayheadSeconds(root.value(QStringLiteral("playheadSeconds")).toDouble());
    }

    restoreFilmstripsAfterLoad();
    // Face Swap landmarks are derived from the photo and deliberately not bundled, so a project
    // opened anywhere but where it was made has none. Re-derive them in the background; the
    // effect renders pass-through until each lands.
    m_faces->ingestSwapSourcesInProject();
    m_playback.setProject(&m_project);
    m_undoStack.clear();
    // With history kept per session (or off), a history saved earlier is not brought back: it
    // would only be dropped on the next save, after costing the memory this setting avoids. A
    // recovery file's history is the crashed session's own, so "session" restores it.
    {
        // Before the history itself: the project's own setting decides whether it is restored.
        const QJsonObject history = root.value(QStringLiteral("undoHistorySettings")).toObject();
        m_projectHistorySteps = history.contains(QStringLiteral("steps"))
                                    ? qMax(-1, history.value(QStringLiteral("steps")).toInt())
                                    : kProjectHistoryUnset;
        m_projectHistoryPersist = history.contains(QStringLiteral("persist"))
                                      ? (history.value(QStringLiteral("persist")).toBool() ? 1 : 0)
                                      : -1;
    }
    const bool restoreHistory =
        parsed.fromRecovery ? undoHistoryCap() != 0 : persistsUndoHistory();
    if (parsed.history && restoreHistory) {
        const drift::Project loaded = m_project;
        QList<drift::HistoryStatePtr> states;
        states.reserve(parsed.history->states.size());
        for (drift::Project &state : parsed.history->states)
            states.append(std::make_shared<const drift::HistoryState>(std::move(state)));
        {
            const QSignalBlocker blocker(&m_undoStack);
            for (const drift::HistoryStep &step : std::as_const(parsed.history->steps)) {
                auto *command = new drift::ProjectSnapshotCommand(
                    &m_project, states.at(step.before), states.at(step.after), step.text);
                command->adoptApplied();
                m_undoStack.push(command);
            }
            m_undoStack.setIndex(parsed.history->index);
        }
        m_project = loaded;
        // Signals were blocked while it was rebuilt, so the limit is applied here.
        scheduleUndoHistoryTrim();
    }
    emit undoHistoryChanged();
    emit undoStackChanged();
    clearSelection();
    m_projectFile->setDirty(false);
    m_projectFile->sweepBrushMattes();
    emit snapEnabledChanged();
    emit rippleEnabledChanged();
    emit rippleModeChanged();
    emit allowClipOverlapChanged();
    notifyTracksChanged();
    emit sequenceTabsChanged();
    emit bookmarksChanged();
    emit workAreaChanged();
    emit m_projectFile->projectNameChanged();
    emit m_projectFile->projectMetadataChanged();
    emit m_projectFile->backgroundChanged();
    emit guidesChanged();
}


void AppController::releaseTransientCaches()
{
    ClipReaderPool::instance().releaseAll();
    FrameCompositor::clearStillImageCache();
    drift::text::clearLayoutCache();
#ifdef DRIFT_WITH_SKIA
    drift::skia::clearTextGeometryCache();
#endif
    // Uploaded textures and the FBO pool, without tearing the context down. Runs on the GL thread
    // and blocks, which is what makes it safe from here; it returns without creating a context if
    // GL was never brought up at all.
    drift::gl::runtime().releaseCaches();
}


QVariantList AppController::exportPresets() const
{
    QVariantList out;
    for (const ExportScalePreset &preset : Exporter::scalePresets()) {
        out.append(QVariantMap{
            {QStringLiteral("id"), preset.id},
            {QStringLiteral("label"), preset.label},
        });
    }
    return out;
}

QVariantList AppController::exportScaleOptions() const
{
    return Exporter::scaleOptions(m_project.width(), m_project.height());
}

QVariantList AppController::exportFrameRateOptions() const
{
    return Exporter::frameRateOptions(m_project.fps());
}

QVariantList AppController::exportVideoCodecs() const
{
    return Exporter::videoCodecs();
}

QVariantList AppController::exportAudioCodecs() const
{
    return Exporter::audioCodecs();
}

bool AppController::exportGifAvailable() const
{
    return Exporter::gifAvailable();
}

QVariantMap AppController::estimateExportSize(const QVariantMap &settings) const
{
    const ExportSettings s = Exporter::settingsFromMap(settings);
    const QSize size = Exporter::scaledSize(m_project.width(), m_project.height(), s.targetHeight);
    const double fps = s.fpsNum > 0 && s.fpsDen > 0 ? static_cast<double>(s.fpsNum) / s.fpsDen : m_project.fps();
    const double seconds = static_cast<double>(renderedDurationUs(m_project, s)) / 1e6;
    const ExportSizeEstimate e = Exporter::estimateSize(s, size.width(), size.height(), fps, seconds);
    return QVariantMap{{QStringLiteral("bytes"), e.bytes}, {QStringLiteral("approximate"), e.approximate}};
}

QVariantMap AppController::exportDefaultSettings() const
{
    const ExportSettings s = Exporter::defaultSettings();
    return QVariantMap{
        {QStringLiteral("targetHeight"), s.targetHeight},
        {QStringLiteral("fpsNum"), s.fpsNum},
        {QStringLiteral("fpsDen"), s.fpsDen},
        {QStringLiteral("videoCodecId"), s.videoCodecId},
        {QStringLiteral("rateControl"), s.rateControl},
        {QStringLiteral("crf"), s.crf},
        {QStringLiteral("videoBitrateKbps"), s.videoBitrateKbps},
        {QStringLiteral("videoPreset"), s.videoPreset},
        {QStringLiteral("audioCodecId"), s.audioCodecId},
        {QStringLiteral("audioBitrateKbps"), s.audioBitrateKbps},
        {QStringLiteral("audioOnly"), s.audioOnly},
        {QStringLiteral("gifExport"), s.gifExport},
    };
}

QVariantMap AppController::lastExportSettings() const
{
    QSettings settings;
    settings.beginGroup(QStringLiteral("export"));
    if (!settings.contains(QStringLiteral("videoCodecId"))
        && !settings.contains(QStringLiteral("audioCodecId"))
        && !settings.contains(QStringLiteral("scaleId"))) {
        return {};
    }

    // A relaunch re-parses the INI and hands every value back as a QString, so the
    // types have to be restored here. QML sees these directly, and in JavaScript the
    // string "false" is truthy — an untyped audioOnly would put the export dialog in
    // audio mode on every launch after the first.
    QVariantMap out;
    const auto takeString = [&](const QString &key) {
        if (settings.contains(key))
            out.insert(key, settings.value(key).toString());
    };
    const auto takeInt = [&](const QString &key) {
        if (settings.contains(key))
            out.insert(key, settings.value(key).toInt());
    };
    const auto takeBool = [&](const QString &key) {
        if (settings.contains(key))
            out.insert(key, settings.value(key).toBool());
    };
    takeString(QStringLiteral("scaleId"));
    takeInt(QStringLiteral("targetHeight"));
    takeInt(QStringLiteral("fpsNum"));
    takeInt(QStringLiteral("fpsDen"));
    takeString(QStringLiteral("videoCodecId"));
    takeString(QStringLiteral("rateControl"));
    takeInt(QStringLiteral("crf"));
    takeInt(QStringLiteral("videoBitrateKbps"));
    takeString(QStringLiteral("videoPreset"));
    takeString(QStringLiteral("audioCodecId"));
    takeInt(QStringLiteral("audioBitrateKbps"));
    takeBool(QStringLiteral("audioOnly"));
    takeBool(QStringLiteral("gifExport"));
    takeBool(QStringLiteral("exportWorkAreaOnly"));
    return out;
}

QString AppController::lastExportFolder() const
{
    const QString folder = QSettings().value(QStringLiteral("export/lastFolder")).toString();
    if (folder.isEmpty() || !QDir(folder).exists())
        return {};
    return folder;
}

void AppController::rememberExportChoice(const QString &outputPath, const QVariantMap &settings)
{
    QSettings store;
    if (!outputPath.isEmpty()) {
        const QFileInfo info(outputPath);
        const QString folder = info.absolutePath();
        if (!folder.isEmpty() && QDir(folder).exists())
            store.setValue(QStringLiteral("export/lastFolder"), folder);
    }

    store.beginGroup(QStringLiteral("export"));
    const auto put = [&](const QString &key) {
        if (settings.contains(key))
            store.setValue(key, settings.value(key));
    };
    put(QStringLiteral("scaleId"));
    put(QStringLiteral("targetHeight"));
    put(QStringLiteral("fpsNum"));
    put(QStringLiteral("fpsDen"));
    put(QStringLiteral("videoCodecId"));
    put(QStringLiteral("rateControl"));
    put(QStringLiteral("crf"));
    put(QStringLiteral("videoBitrateKbps"));
    put(QStringLiteral("videoPreset"));
    put(QStringLiteral("audioCodecId"));
    put(QStringLiteral("audioBitrateKbps"));
    put(QStringLiteral("audioOnly"));
    put(QStringLiteral("gifExport"));
    put(QStringLiteral("exportWorkAreaOnly"));
    store.endGroup();
}

QString AppController::exportPreferredAudioOnlyContainer(const QString &audioCodecId) const
{
    return Exporter::preferredAudioOnlyContainer(audioCodecId);
}

QString AppController::exportPreferredContainer(const QString &videoCodecId,
                                                const QString &audioCodecId) const
{
    return Exporter::preferredContainer(videoCodecId, audioCodecId);
}

QStringList AppController::exportSaveFilters(const QString &container, bool audioOnly) const
{
    return Exporter::saveFilters(container, audioOnly);
}

QString AppController::exportDefaultSuffix(const QString &container, bool audioOnly) const
{
    return Exporter::defaultSuffix(container, audioOnly);
}

double AppController::exportProgress() const
{
    return m_exportProgress;
}

QByteArray AppController::exportSnapshotJson() const
{
    return QJsonDocument(m_project.exportView({}).toJson()).toJson(QJsonDocument::Compact);
}

void AppController::cancelExport()
{
    if (m_exportInProgress)
        m_exportCancel.storeRelaxed(1);
}

void AppController::exportProject(const QUrl &outputUrl)
{
    exportWithSettings(outputUrl, exportDefaultSettings());
}

void AppController::exportWithPreset(const QUrl &outputUrl, const QString &presetId, const QString &bakes)
{
    QVariantMap map = exportDefaultSettings();
    if (const ExportScalePreset *preset = Exporter::scalePresetById(presetId)) {
        map.insert(QStringLiteral("scaleId"), preset->id);
        map.insert(QStringLiteral("targetHeight"), preset->targetHeight);
        map.insert(QStringLiteral("videoBitrateKbps"), preset->videoBitrateKbps);
    }
    if (!bakes.isEmpty())
        map.insert(QStringLiteral("bakes"), bakes);
    exportWithSettings(outputUrl, map);
}

QList<QPair<drift::TimeUs, drift::TimeUs>> AppController::exportReviewSpans() const
{
    // A bake there has a review track burnt in.
    QList<QPair<drift::TimeUs, drift::TimeUs>> reviewSpans;
    if (m_project.hasExportExcludedTracks()) {
        for (const drift::Track &track : m_project.rootTracks()) {
            for (const drift::Clip &clip : track.clips) {
                bool review = track.excludeFromExport;
                if (!review && clip.type == drift::ClipType::Composite) {
                    for (const drift::Track &inner : m_project.sequenceTracks(clip.sequenceId))
                        review = review || inner.excludeFromExport;
                }
                if (!review)
                    continue;
                // A transition draws the clip up to its duration past its own edges.
                drift::TimeUs pad = 0;
                for (const drift::Transition &transition : track.transitions) {
                    if (transition.fromClipId == clip.id || transition.toClipId == clip.id)
                        pad = qMax(pad, transition.durationUs);
                }
                reviewSpans.append({clip.timelineStart - pad, clip.timelineEnd() + pad});
            }
        }
    }
    return reviewSpans;
}

void AppController::exportWithSettings(const QUrl &outputUrl, const QVariantMap &settings,
                                       bool rememberChoice)
{
    ExportSettings exportSettings = Exporter::settingsFromMap(settings);
    // "bakes": "all" (default) | "fresh" (skip stale bakes) | "none"; "useBakes": false is "none".
    const QString bakeMode = settings.value(QStringLiteral("useBakes"), true).toBool()
        ? settings.value(QStringLiteral("bakes"), QStringLiteral("all")).toString()
        : QStringLiteral("none");
    exportSettings.useBakes = bakeMode != QLatin1String("none");
    if (exportSettings.useBakes) {
        exportSettings.bakes = m_bakes->exportPool(bakeMode != QLatin1String("fresh"), exportReviewSpans());
    }

    // The muxer is chosen from the output file's extension, and a created document's URI has
    // none — so the staging file the encoder writes is given the one the settings imply.
    const QString container =
        exportSettings.gifExport
            ? QStringLiteral("gif")
            : exportSettings.audioOnly
                  ? Exporter::preferredAudioOnlyContainer(exportSettings.audioCodecId)
                  : Exporter::preferredContainer(exportSettings.videoCodecId, exportSettings.audioCodecId);
    const QString outputPath =
        writeTargetPath(outputUrl, Exporter::defaultSuffix(container, exportSettings.audioOnly));
    if (outputPath.isEmpty()) {
        const QString problem = exportTargetProblem(outputUrl, outputPath);
        setLastMessage(problem.isEmpty() ? tr("That save location isn’t valid") : problem,
                       QStringLiteral("error"));
        emit exportFinished(false);
        return;
    }

    if (m_exportInProgress) {
        setLastMessage(tr("Export already in progress"), QStringLiteral("warning"));
        return;
    }

    // The chosen location, not the staging file: remembering the latter would point the next
    // export dialog at this app's cache.
    if (rememberChoice) {
#ifdef Q_OS_ANDROID
        rememberExportChoice(AndroidUri::filePath(outputUrl), settings);
#else
        rememberExportChoice(outputPath, settings);
#endif
    }

    // Stop playback so the decode pool isn't driven from two threads at once.
    setPlaying(false);

    m_exportCancel.storeRelaxed(0);
    m_exportProgress = 0.0;
    emit exportProgressChanged();
    m_exportInProgress = true;
    m_exportByAgent = m_exportRequestedByAgent;
    m_exportAudioOnly = exportSettings.audioOnly && !exportSettings.gifExport;
    emit exportInProgressChanged();
    setLastMessage(tr("Exporting…"));

    // Snapshot the project so edits during export can't race the encoder. Always the main
    // timeline, even while a composite's tab is open.
    // Review tracks (excludeFromExport) are hidden and muted in what gets rendered.
    const drift::Project snapshot = m_project.exportView({});

    const bool disposable = writeTargetIsDisposable(outputUrl);

    m_exportFuture = QtConcurrent::run([this, snapshot, exportSettings, outputPath, outputUrl, disposable]() {
        // Spans the encode *and* the copy into the user's document. Exporter::run takes its own
        // hold, but that one ends when run() returns — and the commit below is the phase that
        // would otherwise lose a multi-minute render to a screen that idled out mid-copy. The
        // holds are refcounted, so the inner one nests without restarting the service.
        Exporter::BackgroundHold hold(QStringLiteral("Exporting"), /*cancellable=*/true);
        QString error;
        const auto report = [this](double fraction) {
            QMetaObject::invokeMethod(
                this,
                [this, fraction]() {
                    m_exportProgress = fraction;
                    emit exportProgressChanged();
                },
                Qt::QueuedConnection);
            // Either route into the same stop: the in-app Cancel button sets m_exportCancel, the
            // notification's action sets the service flag.
            return m_exportCancel.loadRelaxed() == 0 && !Exporter::BackgroundHold::cancelRequested();
        };
        // On the worker: probing a disconnected network share can block for many seconds.
        error = exportTargetProblem(outputUrl, outputPath);
        bool ok = error.isEmpty() && Exporter::run(snapshot, exportSettings, outputPath, &error, report);
        // Before the commit: on Android the staged file is gone once it has been streamed out.
        const qint64 sizeBytes = ok ? QFileInfo(outputPath).size() : 0;
        // Android: the encoder needs a real file, and the document the user picked is only
        // reachable through the resolver, so the finished encode is streamed into it — the bar
        // runs a second time for that, and cancel still lands.
        if (ok) {
            ok = commitWriteTarget(
                outputPath, outputUrl,
                [&report](qint64 done, qint64 total) {
                    const double fraction = total > 0 ? double(done) / double(total) : 0.0;
                    Exporter::BackgroundHold::setPercent(static_cast<int>(fraction * 100.0));
                    return report(fraction);
                },
                &error);
        }
        // An empty document is one this export created through the save picker: without disposing
        // of it a cancel at 5% leaves a 0-byte file the user's gallery shows as a video. A document
        // that already had bytes is one the user chose to replace, and a cancel never reached it.
        if (!ok)
            discardWriteTarget(outputPath, outputUrl, disposable);

        QString thumbnail;
        if (ok) {
            const QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
            const QString hash =
                QString::fromLatin1(QCryptographicHash::hash(outputUrl.toString().toUtf8(),
                                                             QCryptographicHash::Sha1).toHex().left(16));
            const QString thumbPath = QDir(base).filePath(QStringLiteral("thumbs/export-%1.jpg").arg(hash));
            FrameCompositor compositor;
            compositor.setProject(&snapshot);
            QImage frame = compositor.compositeAt(0);
            if (!frame.isNull()) {
                if (frame.width() > 320)
                    frame = frame.scaledToWidth(320, Qt::SmoothTransformation);
                QDir().mkpath(QFileInfo(thumbPath).absolutePath());
                if (frame.convertToFormat(QImage::Format_RGB32).save(thumbPath, "JPEG", 85))
                    thumbnail = thumbPath;
            }
        }
        // What was rendered, by the rule the export itself used: hidden tracks (review tracks among
        // them) and audio the mixer drops don't stretch it, and an explicit range is honoured.
        const double durationSeconds = drift::usToSeconds(renderedDurationUs(snapshot, exportSettings));

        QMetaObject::invokeMethod(
            this,
            [this, ok, error, outputUrl, sizeBytes, thumbnail, durationSeconds]() {
                m_exportInProgress = false;
                m_exportProgress = ok ? 1.0 : 0.0;
                if (ok)
                    recordExport(outputUrl, durationSeconds, sizeBytes, thumbnail);
#ifdef Q_OS_ANDROID
                m_lastExportUrl = ok ? outputUrl : QUrl();
                m_lastExportName = ok ? exportDisplayName(outputUrl) : QString();
                emit canShareExportChanged();
#else
                Q_UNUSED(outputUrl);
#endif
                emit exportProgressChanged();
                emit exportInProgressChanged();
                const bool cancelled = error == QLatin1String("Export cancelled")
                                       || error == QLatin1String("Cancelled");
                const QString severity = ok          ? QStringLiteral("success")
                                         : cancelled ? QStringLiteral("info")
                                                     : QStringLiteral("error");
                setLastMessage(ok ? tr("Export complete")
                                  : error.isEmpty() ? tr("Export failed") : error,
                               severity);
                emit exportFinished(ok);
            },
            Qt::QueuedConnection);
    });
}

namespace {
QString sanitizeFilenameSegment(const QString &input, const QString &fallback)
{
    QString out = input.trimmed();
    static const QString kIllegal = QStringLiteral("/\\:*?\"<>|");
    for (QChar &ch : out) {
        if (kIllegal.contains(ch) || ch.unicode() < 32)
            ch = QLatin1Char('_');
    }
    while (out.endsWith(QLatin1Char('.')) || out.endsWith(QLatin1Char(' ')))
        out.chop(1);
    return out.isEmpty() ? fallback : out;
}

QString formatBatchRegionFilenameHelper(const QString &templateStr, const QString &projectName,
                                        const QString &regionLabel, int oneBasedIndex,
                                        const QString &ext)
{
    QString tmpl = templateStr.trimmed();
    if (tmpl.isEmpty())
        tmpl = QStringLiteral("{Project}_{Region}_{Index}.{ext}");

    const QString safeProject = sanitizeFilenameSegment(projectName, QStringLiteral("Project"));
    const QString safeRegion = sanitizeFilenameSegment(regionLabel, QStringLiteral("Region_%1").arg(oneBasedIndex));
    const QString paddedIndex = QStringLiteral("%1").arg(qMax(1, oneBasedIndex), 2, 10, QLatin1Char('0'));
    QString safeExt = ext.trimmed();
    if (safeExt.startsWith(QLatin1Char('.')))
        safeExt.remove(0, 1);
    if (safeExt.isEmpty())
        safeExt = QStringLiteral("mp4");

    tmpl.replace(QStringLiteral("{Project}"), safeProject);
    tmpl.replace(QStringLiteral("{Region}"), safeRegion);
    tmpl.replace(QStringLiteral("{Index}"), paddedIndex);
    tmpl.replace(QStringLiteral("{ext}"), safeExt);
    QString result = sanitizeFilenameSegment(tmpl, QStringLiteral("%1_%2.%3").arg(safeProject, paddedIndex, safeExt));
    if (!result.endsWith(QLatin1Char('.') + safeExt, Qt::CaseInsensitive))
        result += QLatin1Char('.') + safeExt;
    return result;
}
} // namespace

QString AppController::previewBatchRegionFilename(const QString &templateStr,
                                                  const QString &regionLabel, int oneBasedIndex,
                                                  const QString &ext) const
{
    return formatBatchRegionFilenameHelper(templateStr, m_project.name(), regionLabel, oneBasedIndex, ext);
}

bool AppController::batchExportRegions(const QUrl &outputDirUrl, const QString &filenameTemplate,
                                       const QVariantList &regionIds, const QVariantMap &settings)
{
    if (m_exportInProgress) {
        setLastMessage(tr("Export already in progress"), QStringLiteral("warning"));
        return false;
    }

    QString dirPath = outputDirUrl.isLocalFile() ? outputDirUrl.toLocalFile() : outputDirUrl.toString();
    dirPath = dirPath.trimmed();
    if (dirPath.isEmpty()) {
        setLastMessage(tr("Choose an output folder for batch render"), QStringLiteral("warning"));
        return false;
    }

    QSet<QString> wantedIds;
    for (const QVariant &v : regionIds) {
        const QString id = v.toString();
        if (!id.isEmpty())
            wantedIds.insert(id);
    }

    QList<drift::Region> targetRegions;
    for (const drift::Region &r : m_project.regions()) {
        if (r.endUs <= r.startUs)
            continue;
        if (wantedIds.isEmpty() || wantedIds.contains(r.id))
            targetRegions.append(r);
    }
    if (targetRegions.isEmpty()) {
        setLastMessage(tr("No regions selected for batch render"), QStringLiteral("warning"));
        return false;
    }
    std::stable_sort(targetRegions.begin(), targetRegions.end(),
                     [](const drift::Region &a, const drift::Region &b) {
                         return a.startUs < b.startUs;
                     });

    if (!QDir().mkpath(dirPath)) {
        setLastMessage(tr("Could not create output folder"), QStringLiteral("error"));
        return false;
    }

    ExportSettings baseSettings = Exporter::settingsFromMap(settings);
    const QString bakeMode = settings.value(QStringLiteral("useBakes"), true).toBool()
        ? settings.value(QStringLiteral("bakes"), QStringLiteral("all")).toString()
        : QStringLiteral("none");
    baseSettings.useBakes = bakeMode != QLatin1String("none");
    if (baseSettings.useBakes && m_bakes)
        baseSettings.bakes = m_bakes->exportPool(bakeMode != QLatin1String("fresh"), {});

    const QString container =
        baseSettings.gifExport
            ? QStringLiteral("gif")
            : baseSettings.audioOnly
                  ? Exporter::preferredAudioOnlyContainer(baseSettings.audioCodecId)
                  : Exporter::preferredContainer(baseSettings.videoCodecId, baseSettings.audioCodecId);
    const QString suffix = Exporter::defaultSuffix(container, baseSettings.audioOnly);

    setPlaying(false);
    m_exportCancel.storeRelaxed(0);
    m_exportProgress = 0.0;
    m_exportInProgress = true;
    m_exportByAgent = m_exportRequestedByAgent;
    m_exportAudioOnly = baseSettings.audioOnly && !baseSettings.gifExport;
    m_batchExportStatus = QVariantMap{
        {QStringLiteral("active"), true},
        {QStringLiteral("currentIndex"), 0},
        {QStringLiteral("totalCount"), int(targetRegions.size())},
        {QStringLiteral("currentRegionName"), targetRegions.first().label},
        {QStringLiteral("succeededCount"), 0},
        {QStringLiteral("failedCount"), 0},
        {QStringLiteral("outputPaths"), QStringList{}},
    };
    emit batchExportStatusChanged();
    emit exportProgressChanged();
    emit exportInProgressChanged();
    setLastMessage(tr("Batch rendering %1 regions…").arg(targetRegions.size()));

    const drift::Project snapshot = m_project.exportView({});
    const QString projectName = m_project.name();

    m_exportFuture = QtConcurrent::run(
        [this, snapshot, baseSettings, targetRegions, dirPath, filenameTemplate, projectName, suffix]() {
            Exporter::BackgroundHold hold(QStringLiteral("Batch rendering regions"), /*cancellable=*/true);
            const int total = int(targetRegions.size());
            int succeeded = 0;
            int failed = 0;
            QStringList outputs;
            QSet<QString> usedPaths;
            QString lastError;
            bool cancelled = false;

            for (int i = 0; i < total; ++i) {
                if (m_exportCancel.loadRelaxed() != 0 || Exporter::BackgroundHold::cancelRequested()) {
                    cancelled = true;
                    break;
                }
                const drift::Region &region = targetRegions.at(i);
                const QString baseName = formatBatchRegionFilenameHelper(
                    filenameTemplate, projectName, region.label, i + 1, suffix);
                QString outPath = QDir(dirPath).filePath(baseName);
                int dedup = 2;
                while (usedPaths.contains(outPath)) {
                    QFileInfo fi(baseName);
                    outPath = QDir(dirPath).filePath(
                        QStringLiteral("%1_%2.%3").arg(fi.completeBaseName()).arg(dedup++).arg(fi.suffix()));
                }
                usedPaths.insert(outPath);

                QMetaObject::invokeMethod(
                    this,
                    [this, i, total, label = region.label, succeeded, failed, outputs]() {
                        m_batchExportStatus = QVariantMap{
                            {QStringLiteral("active"), true},
                            {QStringLiteral("currentIndex"), i},
                            {QStringLiteral("totalCount"), total},
                            {QStringLiteral("currentRegionName"), label},
                            {QStringLiteral("succeededCount"), succeeded},
                            {QStringLiteral("failedCount"), failed},
                            {QStringLiteral("outputPaths"), outputs},
                        };
                        emit batchExportStatusChanged();
                    },
                    Qt::QueuedConnection);

                ExportSettings regionSettings = baseSettings;
                regionSettings.startUs = region.startUs;
                regionSettings.endUs = region.endUs;

                QString error;
                const auto report = [this, i, total](double fraction) {
                    const double overall = (double(i) + qBound(0.0, fraction, 1.0)) / double(qMax(1, total));
                    QMetaObject::invokeMethod(
                        this,
                        [this, overall]() {
                            m_exportProgress = overall;
                            emit exportProgressChanged();
                        },
                        Qt::QueuedConnection);
                    return m_exportCancel.loadRelaxed() == 0 && !Exporter::BackgroundHold::cancelRequested();
                };

                const bool ok = Exporter::run(snapshot, regionSettings, outPath, &error, report);
                if (ok) {
                    ++succeeded;
                    outputs.append(outPath);
                } else {
                    if (error == QLatin1String("Export cancelled") || error == QLatin1String("Cancelled")
                        || m_exportCancel.loadRelaxed() != 0) {
                        cancelled = true;
                        break;
                    }
                    ++failed;
                    lastError = error;
                }
            }

            const bool allOk = !cancelled && failed == 0 && succeeded == total;
            QMetaObject::invokeMethod(
                this,
                [this, allOk, cancelled, succeeded, failed, total, outputs, lastError]() {
                    m_exportInProgress = false;
                    m_exportProgress = allOk ? 1.0 : 0.0;
                    m_batchExportStatus = QVariantMap{
                        {QStringLiteral("active"), false},
                        {QStringLiteral("currentIndex"), total},
                        {QStringLiteral("totalCount"), total},
                        {QStringLiteral("currentRegionName"), QString()},
                        {QStringLiteral("succeededCount"), succeeded},
                        {QStringLiteral("failedCount"), failed},
                        {QStringLiteral("outputPaths"), outputs},
                    };
                    for (const QString &path : outputs) {
                        const qint64 sz = QFileInfo(path).size();
                        recordExport(QUrl::fromLocalFile(path), 0.0, sz, {});
                    }
                    emit batchExportStatusChanged();
                    emit exportProgressChanged();
                    emit exportInProgressChanged();
                    if (cancelled) {
                        setLastMessage(tr("Batch render cancelled (%1/%2 completed)").arg(succeeded).arg(total),
                                       QStringLiteral("info"));
                    } else if (allOk) {
                        setLastMessage(tr("Batch render complete (%1 regions)").arg(succeeded),
                                       QStringLiteral("success"));
                    } else {
                        setLastMessage(lastError.isEmpty()
                                           ? tr("Batch render finished with %1 failures").arg(failed)
                                           : lastError,
                                       QStringLiteral("error"));
                    }
                    emit batchExportFinished(succeeded, total, outputs);
                    emit exportFinished(allOk);
                },
                Qt::QueuedConnection);
        });
    return true;
}

QVariantList AppController::renderQueue() const
{
    QVariantList out;
    out.reserve(m_renderQueue.size());
    for (const RenderQueueJob &job : m_renderQueue) {
        out.append(QVariantMap{
            {QStringLiteral("id"), job.id},
            {QStringLiteral("label"), job.label},
            {QStringLiteral("outputPath"), job.outputPath},
            {QStringLiteral("sequenceId"), job.sequenceId},
            {QStringLiteral("status"), job.status},
            {QStringLiteral("error"), job.error},
        });
    }
    return out;
}

QString AppController::enqueueRenderJob(const QString &label, const QUrl &outputUrl,
                                        const QVariantMap &settings, const QString &sequenceId)
{
    QString outPath = outputUrl.isLocalFile() ? outputUrl.toLocalFile() : outputUrl.toString();
    outPath = outPath.trimmed();
    if (outPath.isEmpty())
        return {};

    RenderQueueJob job;
    job.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    job.label = label.trimmed().isEmpty() ? QFileInfo(outPath).fileName() : label.trimmed();
    job.outputPath = outPath;
    job.settings = settings;
    job.sequenceId = sequenceId;
    job.status = QStringLiteral("queued");
    m_renderQueue.append(job);
    emit renderQueueChanged();
    saveRenderQueue();
    setLastMessage(tr("Queued “%1” for batch render").arg(job.label), QStringLiteral("success"));
    return job.id;
}

bool AppController::removeRenderJob(const QString &jobId)
{
    for (int i = 0; i < m_renderQueue.size(); ++i) {
        if (m_renderQueue.at(i).id == jobId) {
            if (m_renderQueue.at(i).status == QLatin1String("running"))
                return false;
            m_renderQueue.removeAt(i);
            emit renderQueueChanged();
            saveRenderQueue();
            return true;
        }
    }
    return false;
}

void AppController::clearRenderQueue()
{
    bool changed = false;
    for (int i = m_renderQueue.size() - 1; i >= 0; --i) {
        if (m_renderQueue.at(i).status != QLatin1String("running")) {
            m_renderQueue.removeAt(i);
            changed = true;
        }
    }
    if (changed) {
        emit renderQueueChanged();
        saveRenderQueue();
    }
}

bool AppController::startRenderQueue()
{
    if (m_exportInProgress || m_renderQueueRunning) {
        setLastMessage(tr("Export already in progress"), QStringLiteral("warning"));
        return false;
    }

    QList<int> queuedIndices;
    for (int i = 0; i < m_renderQueue.size(); ++i) {
        if (m_renderQueue.at(i).status == QLatin1String("queued"))
            queuedIndices.append(i);
    }
    if (queuedIndices.isEmpty()) {
        setLastMessage(tr("No queued jobs to render"), QStringLiteral("warning"));
        return false;
    }

    struct PreparedJob
    {
        int queueIndex = -1;
        QString id;
        QString outputPath;
        drift::Project snapshot;
        ExportSettings settings;
    };

    QList<PreparedJob> prepared;
    prepared.reserve(queuedIndices.size());
    for (int idx : queuedIndices) {
        const RenderQueueJob &job = m_renderQueue.at(idx);
        PreparedJob p;
        p.queueIndex = idx;
        p.id = job.id;
        p.outputPath = job.outputPath;
        p.snapshot = m_project.exportView(job.sequenceId);
        p.settings = Exporter::settingsFromMap(job.settings);
        const QString bakeMode = job.settings.value(QStringLiteral("useBakes"), true).toBool()
            ? job.settings.value(QStringLiteral("bakes"), QStringLiteral("all")).toString()
            : QStringLiteral("none");
        p.settings.useBakes = bakeMode != QLatin1String("none");
        if (p.settings.useBakes && m_bakes)
            p.settings.bakes = m_bakes->exportPool(bakeMode != QLatin1String("fresh"), {});
        prepared.append(std::move(p));
    }

    setPlaying(false);
    m_exportCancel.storeRelaxed(0);
    m_exportProgress = 0.0;
    m_exportInProgress = true;
    m_renderQueueRunning = true;
    emit renderQueueChanged();
    emit exportProgressChanged();
    emit exportInProgressChanged();
    setLastMessage(tr("Starting render queue (%1 job(s))…").arg(prepared.size()));

    m_exportFuture = QtConcurrent::run([this, prepared = std::move(prepared)]() {
        Exporter::BackgroundHold hold(QStringLiteral("Running render queue"), /*cancellable=*/true);
        const int total = int(prepared.size());
        int succeeded = 0;
        int failed = 0;
        bool cancelled = false;

        for (int i = 0; i < total; ++i) {
            if (m_exportCancel.loadRelaxed() != 0 || Exporter::BackgroundHold::cancelRequested()) {
                cancelled = true;
                break;
            }
            const PreparedJob &pj = prepared.at(i);
            QMetaObject::invokeMethod(
                this,
                [this, id = pj.id]() {
                    for (RenderQueueJob &j : m_renderQueue) {
                        if (j.id == id) {
                            j.status = QStringLiteral("running");
                            break;
                        }
                    }
                    emit renderQueueChanged();
                },
                Qt::QueuedConnection);

            QString error;
            const auto report = [this, i, total](double fraction) {
                const double overall = (double(i) + qBound(0.0, fraction, 1.0)) / double(qMax(1, total));
                QMetaObject::invokeMethod(
                    this,
                    [this, overall]() {
                        m_exportProgress = overall;
                        emit exportProgressChanged();
                    },
                    Qt::QueuedConnection);
                return m_exportCancel.loadRelaxed() == 0 && !Exporter::BackgroundHold::cancelRequested();
            };

            const bool ok = Exporter::run(pj.snapshot, pj.settings, pj.outputPath, &error, report);
            const bool wasCancelled = !ok && (error == QLatin1String("Export cancelled")
                                              || error == QLatin1String("Cancelled")
                                              || m_exportCancel.loadRelaxed() != 0);
            if (ok)
                ++succeeded;
            else if (!wasCancelled)
                ++failed;

            QMetaObject::invokeMethod(
                this,
                [this, id = pj.id, ok, wasCancelled, error]() {
                    for (RenderQueueJob &j : m_renderQueue) {
                        if (j.id == id) {
                            j.status = ok ? QStringLiteral("done")
                                          : (wasCancelled ? QStringLiteral("cancelled")
                                                          : QStringLiteral("failed"));
                            j.error = error;
                            break;
                        }
                    }
                    emit renderQueueChanged();
                },
                Qt::QueuedConnection);

            if (wasCancelled) {
                cancelled = true;
                break;
            }
        }

        const bool allOk = !cancelled && failed == 0 && succeeded == total;
        QMetaObject::invokeMethod(
            this,
            [this, allOk, cancelled, succeeded, failed, total]() {
                m_exportInProgress = false;
                m_renderQueueRunning = false;
                m_exportProgress = allOk ? 1.0 : 0.0;
                emit renderQueueChanged();
                saveRenderQueue();
                emit exportProgressChanged();
                emit exportInProgressChanged();
                if (cancelled) {
                    setLastMessage(tr("Render queue cancelled (%1/%2 completed)").arg(succeeded).arg(total),
                                   QStringLiteral("info"));
                } else if (allOk) {
                    setLastMessage(tr("Render queue complete (%1 job(s))").arg(succeeded),
                                   QStringLiteral("success"));
                } else {
                    setLastMessage(tr("Render queue finished with %1 failure(s)").arg(failed),
                                   QStringLiteral("error"));
                }
                emit exportFinished(allOk);
            },
            Qt::QueuedConnection);
    });
    return true;
}

void AppController::saveRenderQueue() const
{
    QJsonArray arr;
    for (const RenderQueueJob &job : m_renderQueue) {
        QJsonObject obj;
        obj.insert(QStringLiteral("id"), job.id);
        obj.insert(QStringLiteral("label"), job.label);
        obj.insert(QStringLiteral("outputPath"), job.outputPath);
        obj.insert(QStringLiteral("sequenceId"), job.sequenceId);
        obj.insert(QStringLiteral("status"),
                   job.status == QLatin1String("running") ? QStringLiteral("queued") : job.status);
        obj.insert(QStringLiteral("error"), job.error);
        obj.insert(QStringLiteral("settings"), QJsonObject::fromVariantMap(job.settings));
        arr.append(obj);
    }
    QSettings().setValue(QStringLiteral("renderQueue/jobs"),
                         QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact)));
}

void AppController::loadRenderQueue()
{
    const QString raw = QSettings().value(QStringLiteral("renderQueue/jobs")).toString();
    m_renderQueue.clear();
    if (!raw.isEmpty()) {
        const QJsonDocument doc = QJsonDocument::fromJson(raw.toUtf8());
        if (doc.isArray()) {
            const QJsonArray arr = doc.array();
            for (const QJsonValue &val : arr) {
                if (!val.isObject())
                    continue;
                const QJsonObject obj = val.toObject();
                RenderQueueJob job;
                job.id = obj.value(QStringLiteral("id")).toString();
                job.label = obj.value(QStringLiteral("label")).toString();
                job.outputPath = obj.value(QStringLiteral("outputPath")).toString();
                job.sequenceId = obj.value(QStringLiteral("sequenceId")).toString();
                job.status = obj.value(QStringLiteral("status")).toString(QStringLiteral("queued"));
                if (job.status == QLatin1String("running"))
                    job.status = QStringLiteral("queued");
                job.error = obj.value(QStringLiteral("error")).toString();
                job.settings = obj.value(QStringLiteral("settings")).toObject().toVariantMap();
                if (!job.id.isEmpty() && !job.outputPath.isEmpty())
                    m_renderQueue.append(job);
            }
        }
    }
    emit renderQueueChanged();
}

QVariantList AppController::deliveryPresets() const
{
    return QVariantList{
        QVariantMap{
            {QStringLiteral("id"), QStringLiteral("youtube_4k")},
            {QStringLiteral("name"), tr("YouTube 4K (H.264 / -14 LUFS)")},
            {QStringLiteral("description"), tr("2160p H.264 CRF 17 with -14 LUFS loudness & chapters")},
            {QStringLiteral("videoCodecId"), QStringLiteral("h264")},
            {QStringLiteral("audioCodecId"), QStringLiteral("aac")},
            {QStringLiteral("targetHeight"), 2160},
            {QStringLiteral("crf"), 17},
            {QStringLiteral("rateControl"), QStringLiteral("crf")},
            {QStringLiteral("audioBitrateKbps"), 256},
            {QStringLiteral("audioOnly"), false},
            {QStringLiteral("normalizeLoudness"), true},
            {QStringLiteral("targetLufs"), -14.0},
            {QStringLiteral("embedChapters"), true},
        },
        QVariantMap{
            {QStringLiteral("id"), QStringLiteral("youtube_1080p")},
            {QStringLiteral("name"), tr("YouTube 1080p (H.264 / -14 LUFS)")},
            {QStringLiteral("description"), tr("1080p H.264 CRF 18 with -14 LUFS loudness & chapters")},
            {QStringLiteral("videoCodecId"), QStringLiteral("h264")},
            {QStringLiteral("audioCodecId"), QStringLiteral("aac")},
            {QStringLiteral("targetHeight"), 1080},
            {QStringLiteral("crf"), 18},
            {QStringLiteral("rateControl"), QStringLiteral("crf")},
            {QStringLiteral("audioBitrateKbps"), 192},
            {QStringLiteral("audioOnly"), false},
            {QStringLiteral("normalizeLoudness"), true},
            {QStringLiteral("targetLufs"), -14.0},
            {QStringLiteral("embedChapters"), true},
        },
        QVariantMap{
            {QStringLiteral("id"), QStringLiteral("prores_422_hq")},
            {QStringLiteral("name"), tr("Apple ProRes 422 HQ Master")},
            {QStringLiteral("description"), tr("Source resolution ProRes 422 HQ intra-frame master")},
            {QStringLiteral("videoCodecId"), QStringLiteral("prores_hq")},
            {QStringLiteral("audioCodecId"), QStringLiteral("pcm_s24le")},
            {QStringLiteral("targetHeight"), 0},
            {QStringLiteral("crf"), 18},
            {QStringLiteral("rateControl"), QStringLiteral("crf")},
            {QStringLiteral("audioBitrateKbps"), 320},
            {QStringLiteral("audioOnly"), false},
            {QStringLiteral("normalizeLoudness"), false},
            {QStringLiteral("targetLufs"), -23.0},
            {QStringLiteral("embedChapters"), true},
        },
        QVariantMap{
            {QStringLiteral("id"), QStringLiteral("archival_ffv1")},
            {QStringLiteral("name"), tr("Archival Lossless (FFV1)")},
            {QStringLiteral("description"), tr("Bit-exact lossless FFV1 MKV with embedded chapters")},
            {QStringLiteral("videoCodecId"), QStringLiteral("ffv1")},
            {QStringLiteral("audioCodecId"), QStringLiteral("flac")},
            {QStringLiteral("targetHeight"), 0},
            {QStringLiteral("crf"), 0},
            {QStringLiteral("rateControl"), QStringLiteral("crf")},
            {QStringLiteral("audioBitrateKbps"), 320},
            {QStringLiteral("audioOnly"), false},
            {QStringLiteral("normalizeLoudness"), false},
            {QStringLiteral("targetLufs"), -23.0},
            {QStringLiteral("embedChapters"), true},
        },
        QVariantMap{
            {QStringLiteral("id"), QStringLiteral("podcast_wav")},
            {QStringLiteral("name"), tr("Podcast Master (-16 LUFS WAV)")},
            {QStringLiteral("description"), tr("Audio-only PCM WAV normalized to -16 LUFS")},
            {QStringLiteral("videoCodecId"), QStringLiteral("h264")},
            {QStringLiteral("audioCodecId"), QStringLiteral("pcm_s16le")},
            {QStringLiteral("targetHeight"), 0},
            {QStringLiteral("crf"), 18},
            {QStringLiteral("rateControl"), QStringLiteral("crf")},
            {QStringLiteral("audioBitrateKbps"), 320},
            {QStringLiteral("audioOnly"), true},
            {QStringLiteral("normalizeLoudness"), true},
            {QStringLiteral("targetLufs"), -16.0},
            {QStringLiteral("embedChapters"), true},
        },
    };
}

QString AppController::formatYouTubeChapters() const
{
    struct Entry
    {
        drift::TimeUs timeUs = 0;
        QString label;
    };
    QList<Entry> entries;

    if (!m_project.regions().isEmpty()) {
        QList<drift::Region> regs = m_project.regions();
        std::sort(regs.begin(), regs.end(), [](const drift::Region &a, const drift::Region &b) {
            return a.startUs < b.startUs;
        });
        for (int i = 0; i < regs.size(); ++i) {
            const drift::Region &r = regs.at(i);
            entries.append({std::max<drift::TimeUs>(0, r.startUs),
                            r.label.trimmed().isEmpty() ? tr("Chapter %1").arg(i + 1)
                                                        : r.label.trimmed()});
        }
    } else if (!m_project.bookmarks().isEmpty()) {
        QList<drift::Bookmark> bms = m_project.bookmarks();
        std::sort(bms.begin(), bms.end(), [](const drift::Bookmark &a, const drift::Bookmark &b) {
            return a.timeUs < b.timeUs;
        });
        for (int i = 0; i < bms.size(); ++i) {
            const drift::Bookmark &bm = bms.at(i);
            entries.append({std::max<drift::TimeUs>(0, bm.timeUs),
                            bm.label.trimmed().isEmpty() ? tr("Chapter %1").arg(i + 1)
                                                         : bm.label.trimmed()});
        }
    }

    if (entries.isEmpty())
        return {};

    if (entries.first().timeUs >= drift::kUsPerSecond)
        entries.prepend({0, tr("Intro")});

    const bool useHours = entries.last().timeUs >= 3600 * drift::kUsPerSecond;
    QStringList lines;
    lines.reserve(entries.size());
    for (const Entry &e : entries) {
        const qint64 totalSec = e.timeUs / drift::kUsPerSecond;
        const qint64 h = totalSec / 3600;
        const qint64 m = (totalSec % 3600) / 60;
        const qint64 s = totalSec % 60;
        const QString stamp = useHours
            ? QStringLiteral("%1:%2:%3")
                  .arg(h)
                  .arg(m, 2, 10, QLatin1Char('0'))
                  .arg(s, 2, 10, QLatin1Char('0'))
            : QStringLiteral("%1:%2")
                  .arg(m, 2, 10, QLatin1Char('0'))
                  .arg(s, 2, 10, QLatin1Char('0'));
        lines.append(stamp + QLatin1Char(' ') + e.label);
    }
    return lines.join(QLatin1Char('\n'));
}

bool AppController::copyYouTubeChaptersToClipboard()
{
    const QString text = formatYouTubeChapters();
    if (text.isEmpty()) {
        setLastMessage(tr("No markers or regions to copy as chapters"), QStringLiteral("warning"));
        return false;
    }
    if (QClipboard *cb = QGuiApplication::clipboard()) {
        cb->setText(text);
        setLastMessage(tr("Copied YouTube chapters to clipboard"), QStringLiteral("success"));
        return true;
    }
    return false;
}

void AppController::recordExport(const QUrl &url, double durationSeconds, qint64 sizeBytes,
                                 const QString &thumbnail)
{
    const QString uri = url.toString();
    const QString name = exportDisplayName(url);
    QSettings settings;
    QVariantList history = settings.value(QStringLiteral("exports/history")).toList();
    for (qsizetype i = history.size() - 1; i >= 0; --i) {
        if (history.at(i).toMap().value(QStringLiteral("uri")).toString() == uri)
            history.removeAt(i);
    }
    history.prepend(QVariantMap{
        {QStringLiteral("uri"), uri},
        {QStringLiteral("name"), name},
        {QStringLiteral("created"), QDateTime::currentDateTime().toString(Qt::ISODate)},
        {QStringLiteral("durationSeconds"), durationSeconds},
        {QStringLiteral("sizeBytes"), sizeBytes},
        {QStringLiteral("mime"),
         QMimeDatabase().mimeTypeForFile(name, QMimeDatabase::MatchExtension).name()},
        {QStringLiteral("thumbnail"), thumbnail.isEmpty() ? QString() : QUrl::fromLocalFile(thumbnail).toString()},
    });
    while (history.size() > 100)
        history.removeLast();
    settings.setValue(QStringLiteral("exports/history"), history);
    emit exportHistoryChanged();
}

QVariantList AppController::exportHistory() const
{
    const QVariantList history = QSettings().value(QStringLiteral("exports/history")).toList();
    QVariantList out;
    for (const QVariant &entry : history) {
        QVariantMap map = entry.toMap();
        const QUrl url(map.value(QStringLiteral("uri")).toString());
        bool exists;
#ifdef Q_OS_ANDROID
        if (AndroidUri::isContentUri(url))
            exists = AndroidUri::openForRead(url) != nullptr;
        else
#endif
            exists = QFileInfo::exists(url.toLocalFile());
        if (!exists)
            continue;
        const QString thumb = QUrl(map.value(QStringLiteral("thumbnail")).toString()).toLocalFile();
        if (!thumb.isEmpty() && !QFileInfo::exists(thumb))
            map.insert(QStringLiteral("thumbnail"), QString());
        out.append(map);
    }
    return out;
}

void AppController::removeExport(const QString &uri)
{
    QSettings settings;
    QVariantList history = settings.value(QStringLiteral("exports/history")).toList();
    bool removed = false;
    for (qsizetype i = history.size() - 1; i >= 0; --i) {
        const QVariantMap map = history.at(i).toMap();
        if (map.value(QStringLiteral("uri")).toString() != uri)
            continue;
        const QString thumb = QUrl(map.value(QStringLiteral("thumbnail")).toString()).toLocalFile();
        if (!thumb.isEmpty())
            QFile::remove(thumb);
        history.removeAt(i);
        removed = true;
    }
    if (!removed)
        return;
    settings.setValue(QStringLiteral("exports/history"), history);
    emit exportHistoryChanged();
}

void AppController::openExportedFile(const QString &uri, bool share)
{
    const QUrl source(uri);
    if (source.isEmpty())
        return;
#ifdef Q_OS_ANDROID
    if (m_sharingExport)
        return;
    m_sharingExport = true;
    emit canShareExportChanged();
    setLastMessage(share ? tr("Getting your video ready to share…") : tr("Opening your video…"));

    const QString name = exportDisplayName(source);
    (void)QtConcurrent::run([this, source, name, share]() {
        Exporter::BackgroundHold hold(QStringLiteral("Preparing to share"));
        QString error;
        const QUrl published = Exporter::publishToGallery(source, name, &error);
        QMetaObject::invokeMethod(
            this,
            [this, published, error, share]() {
                m_sharingExport = false;
                emit canShareExportChanged();
                if (published.isEmpty()) {
                    setLastMessage(error, QStringLiteral("error"));
                    return;
                }
                const bool opened = share ? FileDialogs().shareFile(published) : FileDialogs().viewFile(published);
                if (!opened)
                    setLastMessage(share ? tr("Nothing on this device can share that file")
                                         : tr("Nothing on this device can play that file"),
                                   QStringLiteral("error"));
            },
            Qt::QueuedConnection);
    });
#else
    if (!share)
        QDesktopServices::openUrl(source);
#endif
}

void AppController::shareFile(const QString &uri)
{
    openExportedFile(uri, true);
}

void AppController::viewFile(const QString &uri)
{
    openExportedFile(uri, false);
}

bool AppController::canShareExport() const
{
#ifdef Q_OS_ANDROID
    return !m_lastExportUrl.isEmpty() && !m_sharingExport;
#else
    return false;
#endif
}

void AppController::saveToGallery(const QString &filePath, const QString &displayName)
{
#ifdef Q_OS_ANDROID
    if (filePath.isEmpty())
        return;

    const QString name = displayName.isEmpty() ? QFileInfo(filePath).fileName() : displayName;
    const QUrl source = QUrl::fromLocalFile(filePath);
    const QString mime = QMimeDatabase().mimeTypeForFile(name, QMimeDatabase::MatchExtension).name();
    const QString location = mime.startsWith(QLatin1String("audio/")) ? QStringLiteral("Music/Drift")
                             : mime.startsWith(QLatin1String("image/"))
                                 ? QStringLiteral("Pictures/Drift")
                                 : QStringLiteral("Movies/Drift");

    (void)QtConcurrent::run([this, source, name, location]() {
        Exporter::BackgroundHold hold(QStringLiteral("Saving to gallery"));
        QString error;
        const QUrl published = Exporter::publishToGallery(source, name, &error);
        const bool ok = !published.isEmpty();
        QMetaObject::invokeMethod(
            this,
            [this, name, ok, location, error]() {
                emit savedToGallery(name, ok, ok ? location : QString(), error);
            },
            Qt::QueuedConnection);
    });
#else
    Q_UNUSED(filePath);
    Q_UNUSED(displayName);
#endif
}

void AppController::playLastExport()
{
#ifdef Q_OS_ANDROID
    if (m_lastExportUrl.isEmpty() || m_sharingExport)
        return;

    m_sharingExport = true;
    emit canShareExportChanged();
    setLastMessage(tr("Opening your video…"));

    const QUrl source = m_lastExportUrl;
    const QString name = m_lastExportName;
    (void)QtConcurrent::run([this, source, name]() {
        Exporter::BackgroundHold hold(QStringLiteral("Preparing to play"));
        QString error;
        const QUrl published = Exporter::publishToGallery(source, name, &error);
        QMetaObject::invokeMethod(
            this,
            [this, published, error]() {
                m_sharingExport = false;
                emit canShareExportChanged();
                if (published.isEmpty()) {
                    setLastMessage(error, QStringLiteral("error"));
                    return;
                }
                if (!FileDialogs().viewFile(published))
                    setLastMessage(tr("Nothing on this device can play that file"),
                                   QStringLiteral("error"));
            },
            Qt::QueuedConnection);
    });
#endif
}

void AppController::shareLastExport()
{
#ifdef Q_OS_ANDROID
    if (m_lastExportUrl.isEmpty() || m_sharingExport)
        return;

    // publishToGallery streams every byte of the export into a MediaStore row, reading it back out
    // of the SAF document the user picked — which can be a cloud provider, so the copy is bounded
    // by the network. Inline, as this used to be, that is an ANR on anything longer than a clip.
    // Only the copy moves: shareFile starts an Activity and has to stay on the GUI thread.
    m_sharingExport = true;
    emit canShareExportChanged();
    setLastMessage(tr("Getting your video ready to share…"));

    const QUrl source = m_lastExportUrl;
    const QString name = m_lastExportName;
    (void)QtConcurrent::run([this, source, name]() {
        Exporter::BackgroundHold hold(QStringLiteral("Preparing to share"));
        QString error;
        const QUrl published = Exporter::publishToGallery(source, name, &error);
        QMetaObject::invokeMethod(
            this,
            [this, published, error]() {
                m_sharingExport = false;
                emit canShareExportChanged();
                if (published.isEmpty()) {
                    setLastMessage(error, QStringLiteral("error"));
                    return;
                }
                if (!FileDialogs().shareFile(published))
                    setLastMessage(tr("Nothing on this device can share that file"),
                                   QStringLiteral("error"));
            },
            Qt::QueuedConnection);
    });
#endif
}
namespace {


void copyToClipboard(const QString &text)
{
    if (text.isEmpty())
        return;
    if (QClipboard *clip = QGuiApplication::clipboard())
        clip->setText(text);
}

} // namespace

QVariantMap AppController::debugInfo() const
{
    return DebugReport::collect();
}

QVariantMap AppController::playbackDiagnostics() const
{
    return PlaybackDiagnostics::collect(*m_playback.stats(), &m_project,
                                        m_playback.displayRefreshRate());
}

void AppController::startPlaybackBenchmark()
{
    if (m_benchmarkRunning.exchange(true))
        return;
    emit playbackBenchmarkRunningChanged();

    // The sweep opens its own readers against the same decoders and GL runtime the preview is
    // using. Leaving playback running has the two fight over hardware decoder sessions, which
    // measures neither of them, and the counters the report prints would describe the fight.
    m_playback.pause();

    // The reference clip is what makes two bug reports comparable; the timeline's own first
    // video clip is what reproduces the reporter's actual codec and frame rate. Measure both,
    // because either one alone leaves a question the other answers.
    QString timelineClip;
    for (const drift::Track &track : m_project.tracks()) {
        for (const drift::Clip &clip : track.clips) {
            if (clip.type == drift::ClipType::Video && !clip.path.isEmpty()) {
                timelineClip = clip.path;
                break;
            }
        }
        if (!timelineClip.isEmpty())
            break;
    }
    const QSize canvas(m_project.width(), m_project.height());

    // Off the GUI thread: the sweep decodes for a couple of seconds. Everything it touches —
    // the reader pool and the GL runtime — is already called from the compositor's own worker
    // threads, so this adds no new threading assumption.
    QThreadPool::globalInstance()->start([this, timelineClip, canvas] {
        QVariantMap out;
        out.insert(QStringLiteral("reference"), PlaybackDiagnostics::benchmarkClip({}, canvas));
        if (!timelineClip.isEmpty()) {
            out.insert(QStringLiteral("timeline"),
                       PlaybackDiagnostics::benchmarkClip(timelineClip, canvas));
        }
        QMetaObject::invokeMethod(
            this,
            [this, out] {
                m_benchmarkRunning.store(false);
                emit playbackBenchmarkRunningChanged();
                emit playbackBenchmarkFinished(out);
            },
            Qt::QueuedConnection);
    });
}

void AppController::copyDiagnosticsReport(const QVariantMap &benchmarkInfo)
{
    QVariantMap playbackInfo = playbackDiagnostics();
    // The benchmark's sections ride along; its "rows" key, if it ever grows one, must not
    // displace the ones just collected.
    for (auto it = benchmarkInfo.cbegin(); it != benchmarkInfo.cend(); ++it) {
        if (it.key() != QStringLiteral("rows"))
            playbackInfo.insert(it.key(), it.value());
    }

    QString report = DebugReport::formatPlainText(DebugReport::collect());
    report += QLatin1Char('\n');
    report += PlaybackDiagnostics::formatPlainText(playbackInfo);
    copyToClipboard(report);
}

QString AppController::debugInfoText() const
{
    return DebugReport::formatPlainText(debugInfo());
}

void AppController::copyDebugInfo()
{
    copyToClipboard(debugInfoText());
}

void AppController::copyText(const QString &text) const
{
    copyToClipboard(text);
}
namespace {


// Every file a project state points at. Thread-safe: it only reads the project it is given.
QStringList stateMediaPaths(const drift::Project &project)
{
    QStringList list;
    for (const drift::MediaAsset &asset : project.assets())
        list.append(asset.path);
    for (const drift::bundle::MediaEntry &entry : drift::bundle::collectMedia(project, false))
        list.append(entry.originalPath);
    list.removeDuplicates();
    return list;
}

const drift::ProjectSnapshotCommand *historyCommand(const QUndoStack &stack, int commandIndex)
{
    return dynamic_cast<const drift::ProjectSnapshotCommand *>(stack.command(commandIndex));
}

} // namespace

QString AppController::historyHashAt(int stackIndex) const
{
    if (stackIndex < 0 || stackIndex > m_undoStack.count())
        return {};
    if (stackIndex == 0) {
        if (m_undoStack.count() == 0)
            return m_project.contentHash();
        const auto *cmd = historyCommand(m_undoStack, 0);
        return cmd ? cmd->beforeHash() : m_project.contentHash();
    }
    const auto *cmd = historyCommand(m_undoStack, stackIndex - 1);
    return cmd ? cmd->afterHash() : QString();
}

std::optional<drift::Project> AppController::historyProjectAt(int stackIndex) const
{
    if (stackIndex < 0 || stackIndex > m_undoStack.count())
        return std::nullopt;
    // The recorded states, as historyHashAt hashes them; "head" is the live project.
    if (m_undoStack.count() == 0)
        return m_project;
    const auto *cmd = historyCommand(m_undoStack, stackIndex == 0 ? 0 : stackIndex - 1);
    if (!cmd)
        return std::nullopt;
    return stackIndex == 0 ? cmd->before() : cmd->after();
}

int AppController::historyIndexForHash(const QString &prefix) const
{
    const QString needle = prefix.trimmed().toLower();
    if (needle.size() < 8)
        return -1;
    int exact = -1;
    int found = -1;
    int matches = 0;
    for (int i = 0; i <= m_undoStack.count(); ++i) {
        const QString hash = historyHashAt(i).toLower();
        if (hash == needle)
            exact = i;
        if (hash.startsWith(needle)) {
            ++matches;
            found = i;
        }
    }
    if (exact >= 0)
        return exact;
    return matches == 1 ? found : -1;
}

QSet<QString> AppController::historyMediaPaths() const
{
    QSet<QString> paths;
    const drift::HistoryCapture capture = drift::captureHistory(m_undoStack);
    const auto add = [&paths](const drift::HistoryStatePtr &state) {
        const QStringList list = state->mediaPaths(stateMediaPaths);
        for (const QString &path : list)
            paths.insert(path);
    };
    for (const drift::HistoryCapture::Step &step : capture.steps) {
        add(step.before);
        add(step.after);
    }
    return paths;
}

QString AppController::effectiveUndoHistoryMode() const
{
    const QString global = m_preferences ? m_preferences->undoHistoryMode() : QStringLiteral("session");
    if (m_projectHistorySteps == 0
        || (m_projectHistorySteps == kProjectHistoryUnset && global == QLatin1String("off")))
        return QStringLiteral("off");
    // A project that turns history on while the app has it off keeps it for the session unless
    // it asked to save it.
    const bool persist = m_projectHistoryPersist >= 0 ? m_projectHistoryPersist == 1
                                                      : global == QLatin1String("project");
    return persist ? QStringLiteral("project") : QStringLiteral("session");
}

QSet<QString> AppController::historyOnlyMediaPaths() const
{
    QSet<QString> paths = historyMediaPaths();
    for (const QString &path : stateMediaPaths(m_project))
        paths.remove(path);
    return paths;
}

bool AppController::persistsUndoHistory() const
{
    return effectiveUndoHistoryMode() == QLatin1String("project");
}

int AppController::undoHistoryCap() const
{
    if (effectiveUndoHistoryMode() == QLatin1String("off"))
        return 0;
    if (m_projectHistorySteps != kProjectHistoryUnset)
        return m_projectHistorySteps; // -1 (no limit) or N; 0 is "off", handled above
    const int limit = m_preferences ? m_preferences->undoHistoryLimit() : 0;
    return limit > 0 ? limit : -1;
}

static bool undoHistoryOverCap(const QUndoStack &stack, int cap)
{
    if (cap < 0)
        return false;
    return cap == 0 ? stack.count() > 0 : stack.index() > cap;
}

void AppController::scheduleUndoHistoryTrim()
{
    if (m_undoTrimQueued || !undoHistoryOverCap(m_undoStack, undoHistoryCap()))
        return;
    m_undoTrimQueued = true;
    QTimer::singleShot(0, this, [this] {
        m_undoTrimQueued = false;
        trimUndoHistory();
    });
}

void AppController::flushUndoHistoryTrim()
{
    if (!m_undoTrimQueued)
        return;
    m_undoTrimQueued = false; // the queued call then finds nothing left to do
    trimUndoHistory();
}

void AppController::trimUndoHistory()
{
    const int cap = undoHistoryCap();
    if (!undoHistoryOverCap(m_undoStack, cap))
        return;
    // QUndoStack can only drop its oldest commands through setUndoLimit, which it refuses on a
    // non-empty stack, so the kept steps are rebuilt the way a loaded history is. The states are
    // shared, so this copies pointers, not projects; the dropped ones are freed with the last
    // command holding them, and the media only they referenced stops being kept for them.
    if (cap == 0) {
        // History off: nothing is kept, redo included. clear() deletes the commands without
        // running them, and the project stays as it is.
        {
            const QSignalBlocker blocker(&m_undoStack);
            m_undoStack.clear();
        }
        emit undoHistoryChanged();
        emit undoStackChanged();
        return;
    }
    const drift::HistoryCapture capture = drift::captureHistory(m_undoStack);
    // The oldest applied steps beyond the cap go. Undone ones stay: dropping one would leave
    // redo out of step, and the next edit drops them anyway.
    const int drop = capture.index - cap;
    if (drop <= 0)
        return;
    rebuildUndoStack(capture, drop, int(capture.steps.size()), capture.index - drop);
}

void AppController::rebuildUndoStack(const drift::HistoryCapture &capture, int first, int last,
                                     int index)
{
    const drift::Project current = m_project;
    {
        const QSignalBlocker blocker(&m_undoStack);
        m_undoStack.clear();
        for (int i = first; i < last; ++i) {
            const drift::HistoryCapture::Step &step = capture.steps.at(i);
            auto *command = new drift::ProjectSnapshotCommand(&m_project, step.before, step.after, step.text);
            command->adoptApplied();
            m_undoStack.push(command);
        }
        m_undoStack.setIndex(index);
    }
    m_project = current;
    emit undoHistoryChanged();
    emit undoStackChanged();
}

void AppController::foldEditIntoHistory()
{
    flushUndoHistoryTrim();
    drift::HistoryCapture capture = drift::captureHistory(m_undoStack);
    if (capture.steps.isEmpty())
        return;
    if (capture.index > 0)
        capture.steps[capture.index - 1].after = std::make_shared<const drift::HistoryState>(m_project);
    rebuildUndoStack(capture, 0, capture.index, capture.index);
}

int AppController::clearUndoHistory(int keepLast)
{
    flushUndoHistoryTrim();
    const drift::HistoryCapture capture = drift::captureHistory(m_undoStack);
    const int keep = qBound(0, keepLast, capture.index);
    const int removed = int(capture.steps.size()) - keep;
    if (removed <= 0)
        return 0;
    rebuildUndoStack(capture, capture.index - keep, capture.index, keep);
    return removed;
}

QJsonObject AppController::historyJson(const drift::HistoryCapture &capture,
                                       QList<drift::HistoryStatePtr> *states)
{
    drift::HistoryDocument document = drift::historyToJson(capture);
    if (document.json.isEmpty())
        return {};
    QSet<QString> media;
    for (const drift::HistoryStatePtr &state : std::as_const(document.states)) {
        const QStringList list = state->mediaPaths(stateMediaPaths);
        for (const QString &path : list)
            media.insert(path);
    }
    document.json.insert(QStringLiteral("media"), QJsonArray::fromStringList(media.values()));
    if (states)
        *states = std::move(document.states);
    return document.json;
}

QString AppController::historySnapshotDir()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
           + QStringLiteral("/history");
}

void AppController::pruneHistorySnapshots()
{
    constexpr int kMax = 32;
    QDir dir(historySnapshotDir());
    if (!dir.exists())
        return;
    QFileInfoList files = dir.entryInfoList({QStringLiteral("*.json")}, QDir::Files, QDir::Time);
    for (int i = kMax; i < files.size(); ++i)
        QFile::remove(files.at(i).absoluteFilePath());
}

QByteArray AppController::historyJsonAt(int stackIndex) const
{
    if (stackIndex < 0 || stackIndex > m_undoStack.count())
        return {};
    if (stackIndex == 0) {
        if (m_undoStack.count() == 0)
            return m_project.toCompactJson();
        const auto *cmd = historyCommand(m_undoStack, 0);
        return cmd ? cmd->before().toCompactJson() : m_project.toCompactJson();
    }
    const auto *cmd = historyCommand(m_undoStack, stackIndex - 1);
    return cmd ? cmd->after().toCompactJson() : m_project.toCompactJson();
}

// --- Tier 4B: Media Usage Audit, Smart Bin Filters & VEGAS Media Generators ---

namespace {

bool clipUsesAsset(const drift::Clip &clip, const QString &assetId, const drift::MediaAsset *asset,
                   bool assetIsSubclip)
{
    if (!clip.assetId.isEmpty())
        return clip.assetId == assetId;
    if (asset && !assetIsSubclip && !asset->path.isEmpty())
        return clip.path == asset->path;
    return false;
}

QString sanitizeStemFilename(QString name, const QString &fallback)
{
    name = name.trimmed().replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9._-]+")),
                                  QStringLiteral("_"));
    while (name.startsWith(QLatin1Char('_')))
        name.remove(0, 1);
    while (name.endsWith(QLatin1Char('_')))
        name.chop(1);
    return name.isEmpty() ? fallback : name;
}

bool writeWavFromProject(const drift::Project &project, const QString &outPath, QString *errorOut,
                         bool normalizeLoudness = false, double targetLufs = -14.0)
{
    constexpr int kSampleRate = 48000;
    constexpr int kChannels = 2;
    constexpr int kBitsPerSample = 16;
    constexpr int kBlockFrames = 4096;

    QFile file(outPath);
    if (!file.open(QIODevice::WriteOnly)) {
        if (errorOut)
            *errorOut = file.errorString();
        return false;
    }

    // Reserve standard 44-byte RIFF/WAVE header.
    QByteArray header(44, '\0');
    file.write(header);

    AudioMixer mixer;
    mixer.setProject(&project);
    mixer.setMasterClipEnabled(false);

    const drift::TimeUs durationUs = qMax<drift::TimeUs>(0, project.durationUs());
    if (normalizeLoudness && durationUs > 0) {
        mixer.setMasterVolume(Exporter::computeLoudnessGain(
            project, 0, durationUs, kSampleRate, targetLufs));
    }
    const qint64 totalFrames = (durationUs * kSampleRate + 500000) / 1000000;
    QVector<float> floatBuf(kBlockFrames * kChannels);
    QVector<qint16> pcmBuf(kBlockFrames * kChannels);

    quint64 totalDataBytes = 0;
    qint64 framePos = 0;
    while (framePos < totalFrames) {
        const int frames = int(qMin<qint64>(kBlockFrames, totalFrames - framePos));
        const drift::TimeUs startUs = (framePos * 1000000) / kSampleRate;
        mixer.mix(startUs, frames, kSampleRate, floatBuf.data());
        for (int i = 0; i < frames * kChannels; ++i) {
            const float clamped = qBound(-1.0f, floatBuf.at(i), 1.0f);
            pcmBuf[i] = static_cast<qint16>(qRound(clamped * 32767.0f));
        }
        const qint64 bytes = qint64(frames) * kChannels * sizeof(qint16);
        if (file.write(reinterpret_cast<const char *>(pcmBuf.constData()), bytes) != bytes) {
            if (errorOut)
                *errorOut = file.errorString();
            return false;
        }
        totalDataBytes += quint64(bytes);
        framePos += frames;
    }

    auto putLe32 = [](uchar *dst, quint32 v) { qToLittleEndian(v, dst); };
    auto putLe16 = [](uchar *dst, quint16 v) { qToLittleEndian(v, dst); };
    uchar *h = reinterpret_cast<uchar *>(header.data());
    std::memcpy(h + 0, "RIFF", 4);
    putLe32(h + 4, quint32(36 + totalDataBytes));
    std::memcpy(h + 8, "WAVE", 4);
    std::memcpy(h + 12, "fmt ", 4);
    putLe32(h + 16, 16);
    putLe16(h + 20, 1); // PCM
    putLe16(h + 22, kChannels);
    putLe32(h + 24, kSampleRate);
    putLe32(h + 28, kSampleRate * kChannels * (kBitsPerSample / 8));
    putLe16(h + 32, kChannels * (kBitsPerSample / 8));
    putLe16(h + 34, kBitsPerSample);
    std::memcpy(h + 36, "data", 4);
    putLe32(h + 40, quint32(totalDataBytes));

    if (!file.seek(0) || file.write(header) != header.size()) {
        if (errorOut)
            *errorOut = file.errorString();
        return false;
    }
    file.close();
    return true;
}

QImage renderGeneratorImage(const QString &generatorId, int w, int h, int fps, const QVariantMap &params)
{
    QImage img(qMax(16, w), qMax(16, h), QImage::Format_RGBA8888);
    img.fill(Qt::black);
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing, true);

    if (generatorId == QLatin1String("solidColor")) {
        const QColor c(params.value(QStringLiteral("color"), QStringLiteral("#1f2937")).toString());
        img.fill(c.isValid() ? c : QColor(QStringLiteral("#1f2937")));
    } else if (generatorId == QLatin1String("colorGradient")) {
        const QColor c1(params.value(QStringLiteral("colorStart"), QStringLiteral("#0f172a")).toString());
        const QColor c2(params.value(QStringLiteral("colorEnd"), QStringLiteral("#2563eb")).toString());
        const QString style = params.value(QStringLiteral("style"), QStringLiteral("linear")).toString();
        if (style == QLatin1String("radial")) {
            QRadialGradient g(w * 0.5, h * 0.5, qMax(w, h) * 0.6);
            g.setColorAt(0.0, c2.isValid() ? c2 : Qt::blue);
            g.setColorAt(1.0, c1.isValid() ? c1 : Qt::black);
            p.fillRect(0, 0, w, h, g);
        } else {
            QLinearGradient g(0, 0, w, h);
            g.setColorAt(0.0, c1.isValid() ? c1 : Qt::black);
            g.setColorAt(1.0, c2.isValid() ? c2 : Qt::blue);
            p.fillRect(0, 0, w, h, g);
        }
    } else if (generatorId == QLatin1String("creditRoll")) {
        const QColor bg(params.value(QStringLiteral("background"), QStringLiteral("#090b10")).toString());
        const QColor fg(params.value(QStringLiteral("foreground"), QStringLiteral("#f8fafc")).toString());
        img.fill(bg.isValid() ? bg : Qt::black);
        p.setPen(fg.isValid() ? fg : Qt::white);
        const QString title = params.value(QStringLiteral("title"), QStringLiteral("CREDITS")).toString();
        const QString body = params.value(QStringLiteral("body"),
                                          QStringLiteral("Directed by\nProducer\nEdited in Drift")).toString();
        QFont titleFont = p.font();
        titleFont.setPixelSize(qMax(24, h / 14));
        titleFont.setBold(true);
        p.setFont(titleFont);
        p.drawText(QRect(0, h / 8, w, h / 6), Qt::AlignHCenter | Qt::AlignTop, title);

        QFont bodyFont = p.font();
        bodyFont.setPixelSize(qMax(18, h / 22));
        bodyFont.setBold(false);
        p.setFont(bodyFont);
        p.drawText(QRect(w / 10, h / 3, w * 8 / 10, h / 2), Qt::AlignHCenter | Qt::AlignTop, body);
    } else if (generatorId == QLatin1String("timecodeBurnIn")) {
        img.fill(Qt::transparent);
        const QRect box(w / 2 - w / 5, h - h / 5, (w * 2) / 5, h / 9);
        p.setBrush(QColor(0, 0, 0, 190));
        p.setPen(QColor(255, 255, 255, 110));
        p.drawRoundedRect(box, 10, 10);
        p.setPen(Qt::white);
        QFont tcFont(QStringLiteral("monospace"));
        tcFont.setPixelSize(qMax(18, box.height() / 2));
        tcFont.setBold(true);
        p.setFont(tcFont);
        const QString label = QStringLiteral("00:00:00:00  %1fps").arg(qMax(1, fps));
        p.drawText(box, Qt::AlignCenter, label);
    } else if (generatorId == QLatin1String("checkerboard")) {
        const QColor c1(params.value(QStringLiteral("colorA"), QStringLiteral("#1e293b")).toString());
        const QColor c2(params.value(QStringLiteral("colorB"), QStringLiteral("#e2e8f0")).toString());
        const QColor ca = c1.isValid() ? c1 : QColor(QStringLiteral("#1e293b"));
        const QColor cb = c2.isValid() ? c2 : QColor(QStringLiteral("#e2e8f0"));
        const int cell = qMax(8, params.value(QStringLiteral("cellSize"), qMax(24, qMin(w, h) / 16)).toInt());
        for (int y = 0; y < h; y += cell) {
            for (int x = 0; x < w; x += cell) {
                const bool odd = ((x / cell) + (y / cell)) % 2 != 0;
                p.fillRect(x, y, qMin(cell, w - x), qMin(cell, h - y), odd ? cb : ca);
            }
        }
    } else if (generatorId == QLatin1String("testGrid")) {
        img.fill(QColor(QStringLiteral("#0f172a")));
        const int step = qMax(16, qMin(w, h) / 12);
        p.setPen(QPen(QColor(148, 163, 184, 90), 1));
        for (int x = 0; x < w; x += step)
            p.drawLine(x, 0, x, h);
        for (int y = 0; y < h; y += step)
            p.drawLine(0, y, w, y);
        // Title-safe (90%) and action-safe (93%) overlays
        p.setPen(QPen(QColor(QStringLiteral("#38bdf8")), 2, Qt::DashLine));
        p.drawRect(QRectF(w * 0.035, h * 0.035, w * 0.93, h * 0.93));
        p.setPen(QPen(QColor(QStringLiteral("#f59e0b")), 2, Qt::SolidLine));
        p.drawRect(QRectF(w * 0.05, h * 0.05, w * 0.90, h * 0.90));
        // Center crosshair + circle
        p.setPen(QPen(QColor(248, 250, 252, 200), 2));
        p.drawLine(w / 2 - step, h / 2, w / 2 + step, h / 2);
        p.drawLine(w / 2, h / 2 - step, w / 2, h / 2 + step);
        p.drawEllipse(QPointF(w * 0.5, h * 0.5), qMin(w, h) * 0.25, qMin(w, h) * 0.25);
    } else if (generatorId == QLatin1String("whiteNoise")) {
        p.end();
        quint32 state = quint32(params.value(QStringLiteral("seed"), 1337).toUInt());
        if (state == 0)
            state = 1337u;
        for (int y = 0; y < h; ++y) {
            uchar *scan = img.scanLine(y);
            for (int x = 0; x < w; ++x) {
                state = state * 1664525u + 1013904223u;
                const uchar v = uchar((state >> 24) & 0xFFu);
                scan[x * 4 + 0] = v;
                scan[x * 4 + 1] = v;
                scan[x * 4 + 2] = v;
                scan[x * 4 + 3] = 255;
            }
        }
        return img;
    } else {
        // Default: SMPTE RP 219-style 75% color bars.
        const QColor topBars[7] = {
            QColor(191, 191, 191), QColor(191, 191, 0), QColor(0, 191, 191), QColor(0, 191, 0),
            QColor(191, 0, 191),   QColor(191, 0, 0),   QColor(0, 0, 191),
        };
        const QColor midBars[7] = {
            QColor(0, 0, 191),   QColor(19, 19, 19), QColor(191, 0, 191), QColor(19, 19, 19),
            QColor(0, 191, 191), QColor(19, 19, 19), QColor(191, 191, 191),
        };
        const int topH = (h * 67) / 100;
        const int midH = (h * 8) / 100;
        const int botY = topH + midH;
        const int botH = h - botY;
        for (int i = 0; i < 7; ++i) {
            const int x0 = (i * w) / 7;
            const int x1 = ((i + 1) * w) / 7;
            p.fillRect(x0, 0, x1 - x0, topH, topBars[i]);
            p.fillRect(x0, topH, x1 - x0, midH, midBars[i]);
        }
        p.fillRect(0, botY, (w * 5) / 28, botH, QColor(0, 33, 76));
        p.fillRect((w * 5) / 28, botY, (w * 5) / 28, botH, QColor(255, 255, 255));
        p.fillRect((w * 10) / 28, botY, (w * 5) / 28, botH, QColor(50, 0, 106));
        p.fillRect((w * 15) / 28, botY, (w * 13) / 28 + 1, botH, QColor(19, 19, 19));
    }

    p.end();
    return img;
}

QString historyShortHash(const QString &hash)
{
    return hash.left(12);
}

} // namespace

bool AppController::isSubclipAsset(const QString &assetId) const
{
    const drift::MediaAsset *asset = m_project.asset(assetId);
    if (!asset)
        return false;
    return asset->trimInUs > 0 || asset->trimOutUs >= 0;
}

int AppController::assetUseCount(const QString &assetId) const
{
    const drift::MediaAsset *asset = m_project.asset(assetId);
    if (!asset)
        return 0;
    const bool subclip = isSubclipAsset(assetId);
    int count = 0;
    for (const drift::Track &track : m_project.tracks()) {
        for (const drift::Clip &clip : track.clips) {
            if (clipUsesAsset(clip, assetId, asset, subclip))
                ++count;
        }
    }
    return count;
}

int AppController::selectClipsUsingAsset(const QString &assetId)
{
    const drift::MediaAsset *asset = m_project.asset(assetId);
    if (!asset)
        return 0;
    const bool subclip = isSubclipAsset(assetId);
    QList<QPair<int, int>> matches;
    for (int t = 0; t < m_project.tracks().size(); ++t) {
        const drift::Track &track = m_project.tracks().at(t);
        for (int c = 0; c < track.clips.size(); ++c) {
            if (clipUsesAsset(track.clips.at(c), assetId, asset, subclip))
                matches.append({t, c});
        }
    }
    if (matches.isEmpty()) {
        clearSelection();
        return 0;
    }
    m_selection = matches;
    m_selectedTrack = matches.first().first;
    m_selectedClip = matches.first().second;
    emit selectionChanged();
    return int(matches.size());
}

QVariantList AppController::filterMediaAssets(const QString &filterMode) const
{
    QVariantList result;
    if (!m_assetLibrary)
        return result;
    const QString mode = filterMode.trimmed().toLower();
    const int n = m_assetLibrary->rowCount();
    for (int i = 0; i < n; ++i) {
        const QString id = m_assetLibrary->assetIdAt(i);
        const drift::MediaAsset *asset = m_project.asset(id);
        if (!asset)
            continue;
        const int uses = assetUseCount(id);
        const bool subclip = isSubclipAsset(id);
        const bool missing = m_assetLibrary->isMissing(id);
        const bool editFriendly = m_assetLibrary->isEditFriendly(id);
        const bool unconverted = asset->kind == drift::MediaKind::Video && !editFriendly;

        bool match = true;
        if (mode == QLatin1String("unused"))
            match = (uses == 0);
        else if (mode == QLatin1String("unconverted"))
            match = unconverted;
        else if (mode == QLatin1String("missing"))
            match = missing;
        else if (mode == QLatin1String("subclips"))
            match = subclip;
        if (!match)
            continue;

        QVariantMap row = m_assetLibrary->assetAt(i);
        row.insert(QStringLiteral("assetIndex"), i);
        row.insert(QStringLiteral("useCount"), uses);
        row.insert(QStringLiteral("isSubclip"), subclip);
        row.insert(QStringLiteral("missing"), missing);
        row.insert(QStringLiteral("editFriendly"), editFriendly);
        result.append(row);
    }
    return result;
}

QVariantList AppController::mediaGeneratorCatalog() const
{
    return {
        QVariantMap{{QStringLiteral("id"), QStringLiteral("smpteBars")},
                    {QStringLiteral("name"), tr("SMPTE Color Bars")},
                    {QStringLiteral("description"), tr("Broadcast 75% SMPTE RP 219 color bars test pattern")}},
        QVariantMap{{QStringLiteral("id"), QStringLiteral("solidColor")},
                    {QStringLiteral("name"), tr("Solid Color")},
                    {QStringLiteral("description"), tr("Full-frame solid matte color generator")}},
        QVariantMap{{QStringLiteral("id"), QStringLiteral("colorGradient")},
                    {QStringLiteral("name"), tr("Color Gradient")},
                    {QStringLiteral("description"), tr("Linear or radial two-stop color ramp generator")}},
        QVariantMap{{QStringLiteral("id"), QStringLiteral("creditRoll")},
                    {QStringLiteral("name"), tr("Credit Roll")},
                    {QStringLiteral("description"), tr("End-credits title card generator")}},
        QVariantMap{{QStringLiteral("id"), QStringLiteral("timecodeBurnIn")},
                    {QStringLiteral("name"), tr("Timecode Burn-In")},
                    {QStringLiteral("description"), tr("Burnt-in SMPTE timecode overlay graphic")}},
        QVariantMap{{QStringLiteral("id"), QStringLiteral("checkerboard")},
                    {QStringLiteral("name"), tr("Checkerboard Pattern")},
                    {QStringLiteral("description"), tr("Crisp alternating checkerboard matte generator")}},
        QVariantMap{{QStringLiteral("id"), QStringLiteral("testGrid")},
                    {QStringLiteral("name"), tr("Calibration Grid & Safe Zones")},
                    {QStringLiteral("description"), tr("Alignment grid with title-safe and action-safe guides")}},
        QVariantMap{{QStringLiteral("id"), QStringLiteral("whiteNoise")},
                    {QStringLiteral("name"), tr("Film Grain / Noise Texture")},
                    {QStringLiteral("description"), tr("Monochrome film grain and static noise plate")}},
    };
}

int AppController::removeUnusedMedia()
{
    if (!m_assetLibrary)
        return 0;

    QStringList unusedIds;
    const int n = m_assetLibrary->rowCount();
    for (int i = 0; i < n; ++i) {
        const QString id = m_assetLibrary->assetIdAt(i);
        if (!id.isEmpty() && assetUseCount(id) == 0)
            unusedIds.append(id);
    }
    if (unusedIds.isEmpty()) {
        setLastMessage(tr("No unused media in project"), QStringLiteral("info"));
        return 0;
    }

    const drift::Project before = m_project;
    int removed = 0;
    for (const QString &id : unusedIds) {
        const int idx = m_assetLibrary->indexOfId(id);
        if (idx < 0)
            continue;
        const QString removedPath = m_assetLibrary->assetAt(idx).value(QStringLiteral("path")).toString();
        if (m_assetLibrary->removeAssetAt(idx)) {
            ++removed;
            if (!removedPath.isEmpty())
                m_waveformBlocks.forgetSource(removedPath);
        }
    }
    if (removed == 0)
        return 0;

    pruneOrphanSequences();
    pushProjectEdit(before, tr("Remove unused media"));
    finishEdit(tr("Removed %n unused media asset(s)", nullptr, removed));
    return removed;
}

QVariantMap AppController::consolidateProjectMedia(const QUrl &targetDirUrl, bool onlyUsedAssets)
{
    QString dirPath = targetDirUrl.isLocalFile() ? targetDirUrl.toLocalFile() : targetDirUrl.toString();
    dirPath = dirPath.trimmed();
    QVariantMap result{{QStringLiteral("ok"), false},
                       {QStringLiteral("copiedCount"), 0},
                       {QStringLiteral("skippedCount"), 0},
                       {QStringLiteral("outputDir"), dirPath},
                       {QStringLiteral("files"), QStringList{}},
                       {QStringLiteral("error"), QString{}}};
    if (dirPath.isEmpty()) {
        result.insert(QStringLiteral("error"), tr("Target folder path is empty"));
        return result;
    }
    if (!QDir().mkpath(dirPath)) {
        result.insert(QStringLiteral("error"), tr("Could not create target folder"));
        return result;
    }

    QHash<QString, QString> remap;
    QStringList copiedFiles;
    QSet<QString> usedDestNames;
    int skipped = 0;

    for (const QString &id : m_project.assetOrder()) {
        const drift::MediaAsset *asset = m_project.asset(id);
        if (!asset || asset->path.isEmpty())
            continue;
        if (onlyUsedAssets && assetUseCount(id) == 0)
            continue;
        if (remap.contains(asset->path))
            continue;

        const QFileInfo srcInfo(asset->path);
        if (!srcInfo.exists() || !srcInfo.isFile()) {
            ++skipped;
            continue;
        }

        const QString base = srcInfo.completeBaseName();
        const QString ext = srcInfo.suffix();
        QString fileName = srcInfo.fileName();
        int counter = 2;
        while (usedDestNames.contains(fileName.toLower())
               || (QFileInfo::exists(QDir(dirPath).filePath(fileName))
                   && QDir(dirPath).filePath(fileName) != asset->path)) {
            fileName = ext.isEmpty()
                           ? QStringLiteral("%1_%2").arg(base).arg(counter++)
                           : QStringLiteral("%1_%2.%3").arg(base).arg(counter++).arg(ext);
        }
        usedDestNames.insert(fileName.toLower());

        const QString destPath = QDir(dirPath).filePath(fileName);
        if (QFileInfo(asset->path).canonicalFilePath() == QFileInfo(destPath).canonicalFilePath()) {
            copiedFiles.append(destPath);
            continue;
        }
        if (QFile::exists(destPath))
            QFile::remove(destPath);
        if (!QFile::copy(asset->path, destPath)) {
            ++skipped;
            continue;
        }
        remap.insert(asset->path, destPath);
        copiedFiles.append(destPath);
    }

    if (!remap.isEmpty() && m_bin) {
        if (!m_bin->relinkPaths(remap, tr("Consolidate project media"))) {
            result.insert(QStringLiteral("error"), tr("Could not relink Consolidated media (track locked?)"));
            return result;
        }
    }

    result.insert(QStringLiteral("ok"), true);
    result.insert(QStringLiteral("copiedCount"), int(remap.size()));
    result.insert(QStringLiteral("skippedCount"), skipped);
    result.insert(QStringLiteral("files"), copiedFiles);
    setLastMessage(tr("Consolidated %n media file(s)", nullptr, int(remap.size())), QStringLiteral("success"));
    return result;
}

QString AppController::insertMediaGenerator(const QString &generatorId, double atSeconds,
                                            double durationSeconds, const QVariantMap &params)
{
    if (!m_assetLibrary)
        return {};
    const QString genId = generatorId.trimmed().isEmpty() ? QStringLiteral("smpteBars") : generatorId.trimmed();
    const int w = qMax(16, m_project.width());
    const int h = qMax(16, m_project.height());
    const QImage image = renderGeneratorImage(genId, w, h, m_project.fps(), params);
    const QString mediaDir = projectMediaDir(m_project.id());
    if (!QDir().mkpath(mediaDir))
        return {};

    const QString fileId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const QString outPath = QDir(mediaDir).filePath(QStringLiteral("gen_%1_%2.png").arg(genId, fileId));
    if (!image.save(outPath, "PNG"))
        return {};

    QString label = tr("Generator (%1)").arg(genId);
    for (const QVariant &entryVar : mediaGeneratorCatalog()) {
        const QVariantMap entry = entryVar.toMap();
        if (entry.value(QStringLiteral("id")).toString() == genId) {
            label = entry.value(QStringLiteral("name")).toString();
            break;
        }
    }

    const drift::Project before = m_project;

    drift::MediaAsset asset;
    asset.name = label;
    asset.path = outPath;
    asset.kind = drift::MediaKind::Image;
    asset.width = w;
    asset.height = h;
    asset.thumbnailPath = outPath;
    asset.filmstripPath = outPath;
    asset.hasAudio = false;
    asset.hasAudioKnown = true;
    const QString assetId = m_assetLibrary->addGeneratedAsset(asset);

    const int trackIndex = drift::ensureTrackForClipType(m_project, drift::ClipType::Image, true);
    drift::Track &track = m_project.tracks()[trackIndex];
    const drift::TimeUs desiredUs = atSeconds >= 0.0 ? drift::secondsToUs(atSeconds) : m_playheadUs;
    const drift::TimeUs durUs = drift::secondsToUs(qMax(0.1, durationSeconds));
    const drift::TimeUs startUs = drift::resolveClipStart(m_project, track, -1, desiredUs, durUs,
                                                          m_snapEnabled, m_playheadUs);

    drift::Clip clip;
    clip.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    clip.assetId = assetId;
    clip.type = drift::ClipType::Image;
    clip.name = label;
    clip.path = outPath;
    clip.thumbnailPath = outPath;
    clip.filmstripPath = outPath;
    clip.timelineStart = startUs;
    clip.timelineDuration = durUs;
    clip.srcIn = 0;
    clip.srcOut = durUs;
    fitClipLayoutToCanvas(clip, w, h, w, h);

    track.clips.append(clip);
    const int clipIdx = int(track.clips.size()) - 1;
    pushProjectEdit(before, tr("Media generator inserted"));
    finishEdit(tr("Media generator inserted"));
    selectClip(trackIndex, clipIdx);
    return clip.id;
}

// --- Tier 4C: Audio Stem Export & Smart Render Analysis ----------------------

QVariantMap AppController::exportAudioStems(const QUrl &outputDirUrl, bool includeTracks,
                                            bool includeBuses, bool includeMaster,
                                            bool normalizeLoudness, double targetLufs)
{
    QString dirPath = outputDirUrl.isLocalFile() ? outputDirUrl.toLocalFile() : outputDirUrl.toString();
    dirPath = dirPath.trimmed();
    QVariantMap result{{QStringLiteral("ok"), false},
                       {QStringLiteral("outputDir"), dirPath},
                       {QStringLiteral("files"), QStringList{}},
                       {QStringLiteral("count"), 0},
                       {QStringLiteral("error"), QString{}}};
    if (dirPath.isEmpty()) {
        result.insert(QStringLiteral("error"), tr("Output directory is empty"));
        return result;
    }
    if (!QDir().mkpath(dirPath)) {
        result.insert(QStringLiteral("error"), tr("Could not create output directory"));
        return result;
    }

    const drift::Project baseProject = m_project.exportView({});
    QStringList files;
    QSet<QString> usedNames;
    auto uniqueStemPath = [&](const QString &rawName, const QString &fallback) -> QString {
        QString stem = sanitizeStemFilename(rawName, fallback);
        QString candidate = stem;
        int n = 2;
        while (usedNames.contains(candidate.toLower())) {
            candidate = QStringLiteral("%1_%2").arg(stem).arg(n++);
        }
        usedNames.insert(candidate.toLower());
        return QDir(dirPath).filePath(candidate + QStringLiteral(".wav"));
    };

    if (includeTracks) {
        const auto &tracks = baseProject.tracks();
        for (int i = 0; i < tracks.size(); ++i) {
            const drift::Track &t = tracks.at(i);
            bool hasAudioCapableClip = (t.type == drift::TrackType::Audio);
            if (!hasAudioCapableClip && t.type == drift::TrackType::Video) {
                for (const drift::Clip &c : t.clips) {
                    if (c.type == drift::ClipType::Video || c.type == drift::ClipType::Audio) {
                        hasAudioCapableClip = true;
                        break;
                    }
                }
            }
            if (!hasAudioCapableClip)
                continue;

            drift::Project iso = baseProject;
            for (drift::AudioBus &b : iso.audioBuses())
                b.solo = false;
            for (int j = 0; j < iso.tracks().size(); ++j) {
                drift::Track &tr = iso.tracks()[j];
                tr.solo = false;
                tr.muted = (j != i);
            }
            QString err;
            const QString fallback = QStringLiteral("track_%1").arg(i + 1);
            const QString outPath = uniqueStemPath(t.name.trimmed().isEmpty() ? fallback : t.name, fallback);
            if (!writeWavFromProject(iso, outPath, &err, normalizeLoudness, targetLufs)) {
                result.insert(QStringLiteral("error"), err);
                result.insert(QStringLiteral("files"), files);
                result.insert(QStringLiteral("count"), int(files.size()));
                return result;
            }
            files.append(outPath);
        }
    }

    if (includeBuses) {
        for (const drift::AudioBus &target : baseProject.audioBuses()) {
            // Collect this bus and any upstream sub-bus that routes to it.
            QSet<QString> activeBuses{target.id};
            bool added = true;
            while (added) {
                added = false;
                for (const drift::AudioBus &b : baseProject.audioBuses()) {
                    if (!activeBuses.contains(b.id) && activeBuses.contains(b.outputBusId)) {
                        activeBuses.insert(b.id);
                        added = true;
                    }
                }
            }

            drift::Project iso = baseProject;
            for (drift::AudioBus &b : iso.audioBuses()) {
                b.solo = false;
                b.muted = !activeBuses.contains(b.id);
                if (b.id == target.id)
                    b.outputBusId.clear();
            }
            for (drift::Track &tr : iso.tracks()) {
                tr.solo = false;
                bool feeds = activeBuses.contains(tr.outputBusId);
                for (const drift::AudioSend &send : tr.audioSends) {
                    if (send.enabled && send.level > 0.0 && activeBuses.contains(send.busId))
                        feeds = true;
                }
                if (!feeds)
                    tr.muted = true;
            }
            QString err;
            const QString outPath = uniqueStemPath(
                QStringLiteral("bus_%1").arg(target.name.isEmpty() ? target.id : target.name),
                QStringLiteral("bus_%1").arg(target.id));
            if (!writeWavFromProject(iso, outPath, &err, normalizeLoudness, targetLufs)) {
                result.insert(QStringLiteral("error"), err);
                result.insert(QStringLiteral("files"), files);
                result.insert(QStringLiteral("count"), int(files.size()));
                return result;
            }
            files.append(outPath);
        }
    }

    if (includeMaster) {
        QString err;
        const QString outPath = uniqueStemPath(QStringLiteral("master"), QStringLiteral("master"));
        if (!writeWavFromProject(baseProject, outPath, &err, normalizeLoudness, targetLufs)) {
            result.insert(QStringLiteral("error"), err);
            result.insert(QStringLiteral("files"), files);
            result.insert(QStringLiteral("count"), int(files.size()));
            return result;
        }
        files.append(outPath);
    }

    result.insert(QStringLiteral("ok"), true);
    result.insert(QStringLiteral("files"), files);
    result.insert(QStringLiteral("count"), int(files.size()));
    return result;
}

QVariantList AppController::analyzeSmartRenderSegments(const QVariantMap &settings) const
{
    const ExportSettings parsed = Exporter::settingsFromMap(settings);
    const QList<Exporter::SmartRenderSegment> segments =
        Exporter::findSmartRenderSegments(m_project, parsed);
    QVariantList out;
    for (const Exporter::SmartRenderSegment &seg : segments) {
        out.append(QVariantMap{
            {QStringLiteral("clipId"), seg.clipId},
            {QStringLiteral("sourcePath"), seg.sourcePath},
            {QStringLiteral("timelineStartUs"), qint64(seg.timelineStartUs)},
            {QStringLiteral("timelineDurationUs"), qint64(seg.timelineDurationUs)},
            {QStringLiteral("srcInUs"), qint64(seg.srcInUs)},
            {QStringLiteral("srcOutUs"), qint64(seg.srcOutUs)},
        });
    }
    return out;
}

// --- Tier 4D: Bidirectional Interchange Export -----------------------------

bool AppController::exportTimelineInterchange(const QUrl &url, const QString &format)
{
    const QString path = (url.isLocalFile() ? url.toLocalFile() : url.toString()).trimmed();
    if (path.isEmpty()) {
        setLastMessage(tr("Choose an export file path"), QStringLiteral("warning"));
        return false;
    }
    QString err;
    if (!drift::interchange::exportProjectInterchange(m_project, path, format, &err)) {
        setLastMessage(err.isEmpty() ? tr("Timeline export failed") : err, QStringLiteral("error"));
        return false;
    }
    setLastMessage(tr("Timeline exported to %1").arg(QFileInfo(path).fileName()));
    return true;
}

// --- Tier 5C: Nested Project Import (.drift / .veg / .json) ------------------

QString AppController::importNestedProject(const QUrl &url, double atSeconds)
{
    const QString path = (url.isLocalFile() ? url.toLocalFile() : url.toString()).trimmed();
    if (path.isEmpty() || !QFileInfo::exists(path)) {
        setLastMessage(tr("Nested project file not found"), QStringLiteral("warning"));
        return {};
    }

    QString err;
    std::optional<drift::Project> loaded;
    QHash<QString, QString> pathRemap;

    if (path.endsWith(QLatin1String(".veg"), Qt::CaseInsensitive)
        || drift::vegas::isVegasProject(path)) {
        loaded = drift::vegas::readProject(path, &err);
    } else if (const auto bundle = drift::bundle::readManifest(path, &err); bundle.has_value()) {
        const QString extractDir = QDir(projectMediaDir(m_project.id()))
                                       .filePath(QStringLiteral("nested_%1").arg(
                                           bundle->projectId.isEmpty()
                                               ? QUuid::createUuid().toString(QUuid::WithoutBraces)
                                               : bundle->projectId));
        QDir().mkpath(extractDir);
        drift::bundle::extract(path, extractDir, {}, &pathRemap, &err);
        QString parseErr;
        drift::Project parsed = drift::Project::fromJson(bundle->document, &parseErr);
        if (!parseErr.isEmpty())
            err = parseErr;
        else
            loaded = std::move(parsed);
    } else {
        err.clear();
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly)) {
            setLastMessage(f.errorString(), QStringLiteral("error"));
            return {};
        }
        const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
        if (!doc.isObject()) {
            setLastMessage(tr("Unrecognized nested project format"), QStringLiteral("error"));
            return {};
        }
        drift::Project parsed = drift::Project::fromJson(doc.object(), &err);
        if (err.isEmpty())
            loaded = std::move(parsed);
    }

    if (!loaded.has_value()) {
        setLastMessage(err.isEmpty() ? tr("Failed to import nested project") : err,
                       QStringLiteral("error"));
        return {};
    }

    const drift::Project &child = *loaded;
    const drift::TimeUs childDuration = child.durationUs();
    if (child.tracks().isEmpty() || childDuration <= 0) {
        setLastMessage(tr("Nested project has an empty timeline"), QStringLiteral("warning"));
        return {};
    }

    const drift::Project before = m_project;

    QHash<QString, QString> assetIdMap;
    for (const drift::MediaAsset &srcAsset : child.assets()) {
        drift::MediaAsset copy = srcAsset;
        if (pathRemap.contains(copy.path))
            copy.path = pathRemap.value(copy.path);
        if (copy.id.isEmpty() || m_project.assets().contains(copy.id))
            copy.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        assetIdMap.insert(srcAsset.id, copy.id);
        m_project.addAsset(copy);
    }

    QHash<QString, QString> seqIdMap;
    auto remapTrackList = [&](drift::TrackList list) {
        for (drift::Track &tr : list) {
            for (drift::Clip &c : tr.clips) {
                c.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
                if (!c.assetId.isEmpty() && assetIdMap.contains(c.assetId))
                    c.assetId = assetIdMap.value(c.assetId);
                if (pathRemap.contains(c.path))
                    c.path = pathRemap.value(c.path);
                if (!c.sequenceId.isEmpty() && seqIdMap.contains(c.sequenceId))
                    c.sequenceId = seqIdMap.value(c.sequenceId);
            }
        }
        return list;
    };

    for (const QString &subId : child.sequenceIds()) {
        drift::TrackList subTracks = child.sequenceTracks(subId);
        subTracks = remapTrackList(std::move(subTracks));
        const QString newSubId = m_project.addSequence(std::move(subTracks));
        seqIdMap.insert(subId, newSubId);
    }

    drift::TrackList mainTracks = remapTrackList(child.tracks());
    const QString rootSeqId = m_project.addSequence(std::move(mainTracks));

    const QString baseLabel = !child.name().trimmed().isEmpty()
                                  ? child.name().trimmed()
                                  : QFileInfo(path).completeBaseName();

    drift::MediaAsset compAsset;
    compAsset.kind = drift::MediaKind::Composite;
    compAsset.name = baseLabel;
    compAsset.sequenceId = rootSeqId;
    compAsset.durationUs = childDuration;
    compAsset.width = child.width() > 0 ? child.width() : m_project.width();
    compAsset.height = child.height() > 0 ? child.height() : m_project.height();
    compAsset.fps = child.fps() > 0 ? child.fps() : m_project.fps();
    compAsset.folderId = m_bin ? m_bin->currentFolderId() : QString{};
    const QString compAssetId = m_project.addAsset(compAsset);
    if (m_assetLibrary)
        m_assetLibrary->syncToProject();

    const int trackIndex = drift::ensureTrackForClipType(m_project, drift::ClipType::Composite, true);
    drift::Track &targetTrack = m_project.tracks()[trackIndex];
    const drift::TimeUs desiredUs = atSeconds >= 0.0 ? drift::secondsToUs(atSeconds) : m_playheadUs;
    const drift::TimeUs startUs = drift::resolveClipStart(
        m_project, targetTrack, -1, desiredUs, childDuration, m_snapEnabled, m_playheadUs);

    drift::Clip compClip;
    compClip.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    compClip.assetId = compAssetId;
    compClip.sequenceId = rootSeqId;
    compClip.type = drift::ClipType::Composite;
    compClip.name = baseLabel;
    compClip.timelineStart = startUs;
    compClip.timelineDuration = childDuration;
    compClip.srcIn = 0;
    compClip.srcOut = childDuration;

    targetTrack.clips.append(compClip);
    const int clipIdx = int(targetTrack.clips.size()) - 1;
    pushProjectEdit(before, tr("Nested project imported"));
    finishEdit(tr("Nested project \"%1\" added to timeline").arg(baseLabel));
    selectClip(trackIndex, clipIdx);
    return compClip.id;
}

QJsonObject AppController::jumpToHistory(int index, const QString &hash)
{
    flushUndoHistoryTrim(); // history indices as they will stay
    using namespace drift::mcp;
    int target = index;
    if (!hash.trimmed().isEmpty()) {
        target = historyIndexForHash(hash);
        if (target < 0)
            return err("not_found", QStringLiteral("No history entry matches that hash"));
    } else if (index < 0) {
        return err("bad_args", QStringLiteral("index or hash required"));
    }
    if (target < 0 || target > m_undoStack.count())
        return err("bad_args", QStringLiteral("index out of range"));
    // Same as AppController::undo()/redo(): the selection is carried across by clip id, or the
    // jump leaves it on whatever clip now sits at the old indices.
    captureSelectionClipIds();
    const int from = m_undoStack.index();
    m_undoStack.setIndex(target);
    m_undoSelectionCaptured = false;
    if (mcp()) {
        mcp()->setPendingChangeLabel(QStringLiteral("Jump to version %1").arg(target));
        mcp()->bumpEditRevision(target < from ? McpController::ChangeKind::Undo : McpController::ChangeKind::Redo);
    }
    // Redoing forward can take the applied steps past the limit again; settle it before replying.
    flushUndoHistoryTrim();
    const QString at = historyHashAt(m_undoStack.index());
    return ok({{QStringLiteral("index"), m_undoStack.index()},
               {QStringLiteral("hash"), at},
               {QStringLiteral("short"), historyShortHash(at)}});
}

QJsonObject AppController::historySettings() const
{
    return {{QStringLiteral("mode"), effectiveUndoHistoryMode()},
            // Steps that can be undone at most: 0 with history off, -1 with no limit.
            {QStringLiteral("steps"), undoHistoryCap()},
            {QStringLiteral("source"), hasProjectHistorySettings() ? QStringLiteral("project")
                                                                          : QStringLiteral("global")}};
}

QJsonObject AppController::setHistoryLimit(bool reset, std::optional<int> steps, std::optional<bool> persist)
{
    using namespace drift::mcp;
    if (!reset && !steps && !persist)
        return err("bad_args", QStringLiteral("steps, persist or reset required"));
    if (steps && *steps < -1)
        return err("bad_args", QStringLiteral("steps must be -1 (no limit), 0 (no history) or more"));
    const int stepsBefore = m_projectHistorySteps;
    const int persistBefore = m_projectHistoryPersist;
    if (reset) {
        m_projectHistorySteps = kProjectHistoryUnset;
        m_projectHistoryPersist = -1;
    }
    if (steps)
        m_projectHistorySteps = *steps;
    if (persist)
        m_projectHistoryPersist = *persist ? 1 : 0;
    // Saved with the project, so changing it changes the project.
    if (m_projectHistorySteps != stepsBefore || m_projectHistoryPersist != persistBefore)
        m_projectFile->setDirty(true);
    scheduleUndoHistoryTrim();
    flushUndoHistoryTrim();
    QJsonObject reply = historySettings();
    reply.insert(QStringLiteral("steps_kept"), m_undoStack.count());
    return ok(reply);
}

QJsonObject AppController::historyStats(bool size)
{
    using namespace drift::mcp;
    flushUndoHistoryTrim();
    const drift::HistoryCapture capture = drift::captureHistory(m_undoStack);
    // Project snapshots held in memory (a step's before and after, shared where a step reuses
    // one). Each is a full project copy, though copies share data that did not change. Counted by
    // identity, so this serialises nothing.
    // Walked through the live commands, not the capture: a macro's inner steps hold snapshots
    // of their own, which the capture flattens away.
    QSet<const drift::HistoryState *> states;
    const std::function<void(const QUndoCommand *)> collect = [&](const QUndoCommand *command) {
        if (const auto *snapshot = dynamic_cast<const drift::ProjectSnapshotCommand *>(command)) {
            states.insert(snapshot->beforeState().get());
            states.insert(snapshot->afterState().get());
        }
        for (int c = 0; c < command->childCount(); ++c)
            collect(command->child(c));
    };
    for (int i = 0; i < m_undoStack.count(); ++i)
        collect(m_undoStack.command(i));
    QStringList media = historyOnlyMediaPaths().values();
    media.sort();
    const int mediaCount = media.size();
    if (media.size() > 20)
        media = media.mid(0, 20);
    QJsonObject reply{
        {QStringLiteral("steps"), m_undoStack.count()},
        {QStringLiteral("undoable"), m_undoStack.index()},
        {QStringLiteral("redoable"), m_undoStack.count() - m_undoStack.index()},
        {QStringLiteral("snapshots"), int(states.size())},
        {QStringLiteral("saved_with_project"), persistsUndoHistory()},
        {QStringLiteral("history_only_media"), mediaCount},
        {QStringLiteral("history_only_media_paths"), QJsonArray::fromStringList(media)},
        {QStringLiteral("settings"), historySettings()},
    };
    if (size) {
        // Serialises every state on this thread, so only on request: the history as uncompressed
        // JSON, which is what a save adds before compression. Memory use is higher; it is not
        // measured here.
        const drift::HistoryDocument document = drift::historyToJson(capture);
        const qint64 bytes =
            document.json.isEmpty() ? 0 : QJsonDocument(document.json).toJson(QJsonDocument::Compact).size();
        reply.insert(QStringLiteral("json_bytes"), double(bytes));
    }
    return ok(reply);
}

QJsonObject AppController::takeSnapshot(const QString &label)
{
    flushUndoHistoryTrim(); // history indices as they will stay
    using namespace drift::mcp;
    const int current = m_undoStack.index();
    const QByteArray json = historyJsonAt(current);
    const QString hash = historyHashAt(current);
    if (json.isEmpty() || hash.isEmpty())
        return err("bad_args", QStringLiteral("Nothing to snapshot"));

    const QString dir = historySnapshotDir();
    if (!QDir().mkpath(dir))
        return err("bad_args", QStringLiteral("Could not create history folder"));
    const QString path = dir + QLatin1Char('/') + hash + QStringLiteral(".json");
    bool existed = QFile::exists(path);
    if (!existed) {
        QSaveFile file(path);
        if (!file.open(QIODevice::WriteOnly))
            return err("bad_args", QStringLiteral("Could not write snapshot"));
        file.write(json);
        if (!file.commit())
            return err("bad_args", QStringLiteral("Could not write snapshot"));
        pruneHistorySnapshots();
    }
    QJsonObject extra{{QStringLiteral("hash"), hash},
                      {QStringLiteral("short"), historyShortHash(hash)},
                      {QStringLiteral("path"), path},
                      {QStringLiteral("index"), current},
                      {QStringLiteral("existed"), existed},
                      {QStringLiteral("bytes"), json.size()}};
    if (!label.trimmed().isEmpty())
        extra.insert(QStringLiteral("label"), label.trimmed());
    else if (current > 0)
        extra.insert(QStringLiteral("label"), m_undoStack.text(current - 1));
    else
        extra.insert(QStringLiteral("label"), QStringLiteral("Origin"));
    return ok(extra);
}

QJsonObject AppController::listSnapshots() const
{
    using namespace drift::mcp;
    QJsonArray snapshots;
    QDir dir(historySnapshotDir());
    const QFileInfoList files =
        dir.entryInfoList({QStringLiteral("*.json")}, QDir::Files, QDir::Time);
    for (const QFileInfo &info : files) {
        const QString hash = info.completeBaseName();
        snapshots.append(QJsonObject{
            {QStringLiteral("hash"), hash},
            {QStringLiteral("short"), historyShortHash(hash)},
            {QStringLiteral("path"), info.absoluteFilePath()},
            {QStringLiteral("bytes"), info.size()},
            {QStringLiteral("savedAt"), info.lastModified().toUTC().toString(Qt::ISODate)},
        });
    }
    return ok({{QStringLiteral("snapshots"), snapshots}, {QStringLiteral("n"), snapshots.size()}});
}

QJsonObject AppController::restoreSnapshot(const QString &hash)
{
    using namespace drift::mcp;
    const QString needle = hash.trimmed();
    if (needle.size() < 8)
        return err("bad_args", QStringLiteral("hash required"));

    flushUndoHistoryTrim(); // resolve against the stack as it will stay
    const int onStack = historyIndexForHash(needle);
    if (onStack >= 0)
        return jumpToHistory(onStack, {});

    QDir dir(historySnapshotDir());
    QString path;
    int matches = 0;
    const QFileInfoList files =
        dir.entryInfoList({QStringLiteral("*.json")}, QDir::Files, QDir::Name);
    for (const QFileInfo &info : files) {
        const QString name = info.completeBaseName();
        if (name.startsWith(needle, Qt::CaseInsensitive) || needle.startsWith(name, Qt::CaseInsensitive)) {
            ++matches;
            path = info.absoluteFilePath();
            if (name.compare(needle, Qt::CaseInsensitive) == 0) {
                path = info.absoluteFilePath();
                matches = 1;
                break;
            }
        }
    }
    if (matches != 1 || path.isEmpty())
        return err("not_found", QStringLiteral("No snapshot matches that hash"));

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return err("bad_args", QStringLiteral("Could not read snapshot"));
    const QByteArray json = file.readAll();
    const QString fileHash = QString::fromLatin1(
        QCryptographicHash::hash(json, QCryptographicHash::Sha256).toHex());
    // Snapshots are hashed without transcripts; keep the session's so restoring one never loses them.
    const auto transcripts = m_project.transcripts();
    QString error;
    if (!applyProjectJson(json, &error))
        return err("bad_args", error.isEmpty() ? QStringLiteral("Snapshot refused") : error);
    for (auto it = transcripts.cbegin(); it != transcripts.cend(); ++it) {
        if (!m_project.transcript(it.key()))
            m_project.setTranscript(it.key(), it.value());
    }
    if (mcp()) {
        mcp()->setPendingChangeLabel(QStringLiteral("Restore snapshot"));
        mcp()->bumpEditRevision();
    }
    return ok({{QStringLiteral("index"), 0},
               {QStringLiteral("hash"), fileHash},
               {QStringLiteral("short"), historyShortHash(fileHash)},
               {QStringLiteral("reset"), true}});
}

QJsonObject AppController::setProjectHistorySteps(int steps)
{
    return setHistoryLimit(false, steps, std::nullopt);
}

QJsonObject AppController::setProjectHistoryPersist(bool persist)
{
    return setHistoryLimit(false, std::nullopt, persist);
}

QJsonObject AppController::resetProjectHistory()
{
    return setHistoryLimit(true, std::nullopt, std::nullopt);
}
