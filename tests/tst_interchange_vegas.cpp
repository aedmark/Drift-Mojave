#include <QtTest>

#include <QDir>
#include <QFile>
#include <QFileInfo>

#include "core/interchange/EdlReader.h"
#include "core/interchange/OtioReader.h"
#include "core/interchange/ResolveReader.h"
#include "core/interchange/TimelineExporter.h"
#include "core/interchange/VegasReader.h"
#include "core/project/Project.h"
#include "core/project/Track.h"
#include "core/project/clip/Clip.h"

static QString findExtendedVeg(const QString &subPath)
{
    const QString extendedDir = qEnvironmentVariable("DRIFT_EXTENDED_VEGAS_TESTS_DIR");
    if (extendedDir.isEmpty())
        return QString();
    const QString candidate = QDir(extendedDir).filePath(subPath);
    if (QFile::exists(candidate))
        return candidate;
    const QString candidateOdds = QDir(extendedDir).filePath(QStringLiteral("Videos/Odds/") + subPath);
    if (QFile::exists(candidateOdds))
        return candidateOdds;
    return QString();
}

class TestInterchangeVegas : public QObject
{
    Q_OBJECT

private slots:
    void detectsVegasProject();
    void invalidDataFailsGracefully();
    void readsFixtureVegasProject();
    void readsRealVegasProject();
    void readsScene2VegasProjectWithMts();
    void readsEpisode4VegasProjectWithOffset96AndSubdirectories();
    void readsEpisode4WorkprintWithNestedTimelines();
    void readsTrackMetadataStateAndProperties();
    void readsTimelineMarkersAndRegions();
    void detectsCrossfadesAndTransitions();
    void readsTitlesAndTextGenerators();
    void readsVideoEffectsLevelsAndColor();
    void readsVignetteAndSoftlightEffects();
    void readsEventPanCropAndKeyframes();
    void readsProjectSummaryRiffInfoChunks();
    void readsRegionsEnvelopesBezierMasksAndTrackAudioFx();
    void exportsFcpxmlEdlOtioAndVegasTsv();
    void readsDeeperVegasEventTagsAndCompositeBlend();
    void readsImageMxfTrackFlagsAndLevelsToColorGrade();
    void readsPlaybackRateAndActiveTakeSelection();
    void parsesDirectShowTrackAndEventAudioFxGraphs();
    void parsesVideoTrackFxAndExplicitTransitionPlugins();
};

void TestInterchangeVegas::detectsVegasProject()
{
    const QString fixtureVeg = QStringLiteral(DRIFT_TEST_DATA_DIR "/sample.veg");
    if (QFile::exists(fixtureVeg)) {
        QVERIFY(drift::vegas::isVegasProject(fixtureVeg));
    }

    const QString realVeg = findExtendedVeg(QStringLiteral("Episode 3/Remaster/Scene 1/e3s1.veg"));
    if (!realVeg.isEmpty()) {
        QVERIFY(drift::vegas::isVegasProject(realVeg));
    }

    QVERIFY(!drift::vegas::isVegasProject(QStringLiteral("/nonexistent/file.txt")));
    QVERIFY(!drift::vegas::isVegasData(QByteArray("This is not a Wave64 RIFF file at all.")));
}

void TestInterchangeVegas::invalidDataFailsGracefully()
{
    QString err;
    const auto result = drift::vegas::readProjectData(QByteArray("random truncated junk"), QString(), &err);
    QVERIFY(!result.has_value());
    QVERIFY(!err.isEmpty());
}

void TestInterchangeVegas::readsFixtureVegasProject()
{
    const QString fixtureVeg = QStringLiteral(DRIFT_TEST_DATA_DIR "/sample.veg");
    if (!QFile::exists(fixtureVeg)) {
        QSKIP("Fixture sample.veg not present");
    }

    QString error;
    const auto optProj = drift::vegas::readProject(fixtureVeg, &error);
    QVERIFY2(optProj.has_value(), qPrintable(error));

    const drift::Project &proj = *optProj;
    QCOMPARE(proj.tracks().size(), 5);
    QCOMPARE(proj.assets().size(), 19);
    QCOMPARE(proj.width(), 1920);
    QCOMPARE(proj.height(), 1080);
    QCOMPARE(proj.fps(), 24);

    bool foundVideoTrack = false;
    bool foundAudioTrack = false;
    int totalClips = 0;

    for (const drift::Track &track : proj.tracks()) {
        if (track.type == drift::TrackType::Video)
            foundVideoTrack = true;
        if (track.type == drift::TrackType::Audio)
            foundAudioTrack = true;

        totalClips += track.clips.size();
        for (const drift::Clip &clip : track.clips) {
            QVERIFY(!clip.id.isEmpty());
            QVERIFY(clip.timelineDuration > 0);
            QVERIFY(clip.srcOut >= clip.srcIn);
        }
    }

    QVERIFY(foundVideoTrack);
    QVERIFY(foundAudioTrack);
    QVERIFY(totalClips > 0);
}

void TestInterchangeVegas::readsRealVegasProject()
{
    const QString realVeg = findExtendedVeg(QStringLiteral("Episode 3/Remaster/Scene 1/e3s1.veg"));
    if (realVeg.isEmpty()) {
        QSKIP("Extended test .veg file not present (set DRIFT_EXTENDED_VEGAS_TESTS_DIR to run)");
    }

    QString error;
    const auto optProj = drift::vegas::readProject(realVeg, &error);
    QVERIFY2(optProj.has_value(), qPrintable(error));

    const drift::Project &proj = *optProj;
    qDebug() << "Loaded VEGAS project:" << proj.name()
             << "Tracks:" << proj.tracks().size()
             << "Assets:" << proj.assets().size()
             << "Resolution:" << proj.width() << "x" << proj.height()
             << "FPS:" << proj.fps();

    QVERIFY(proj.tracks().size() >= 1);
    QVERIFY(proj.assets().size() >= 1);

    int totalClips = 0;
    bool foundVideoTrack = false;
    bool foundAudioTrack = false;

    for (const drift::Track &track : proj.tracks()) {
        if (track.type == drift::TrackType::Video)
            foundVideoTrack = true;
        if (track.type == drift::TrackType::Audio)
            foundAudioTrack = true;

        totalClips += track.clips.size();
        for (const drift::Clip &clip : track.clips) {
            QVERIFY(!clip.id.isEmpty());
            QVERIFY(clip.timelineDuration > 0);
            QVERIFY(clip.srcOut >= clip.srcIn);

            if (proj.assets().contains(clip.assetId)) {
                const auto &asset = proj.assets().value(clip.assetId);
                // If it was relinked to ProRes, verify editFriendly is true
                if (asset.path.endsWith(QLatin1String(".mov"), Qt::CaseInsensitive)) {
                    QVERIFY(asset.editFriendly);
                }
            }
        }
    }

    QVERIFY(foundVideoTrack);
    QVERIFY(foundAudioTrack);
    QVERIFY(totalClips > 0);

    int videoMovCount = 0;
    int videoMtsCount = 0;
    for (const auto &asset : proj.assets()) {
        if (asset.kind == drift::MediaKind::Video) {
            if (asset.path.endsWith(QLatin1String(".mov"), Qt::CaseInsensitive))
                ++videoMovCount;
            else if (asset.path.endsWith(QLatin1String(".mts"), Qt::CaseInsensitive))
                ++videoMtsCount;
        }
    }
    qDebug() << "e3s1 Video assets .mov:" << videoMovCount << ".mts:" << videoMtsCount;
    QCOMPARE(videoMovCount, 9);
    QCOMPARE(videoMtsCount, 0);
}

void TestInterchangeVegas::readsScene2VegasProjectWithMts()
{
    const QString scene2Veg = findExtendedVeg(QStringLiteral("Episode 3/Remaster/Scene 2/E3S2.veg"));
    if (scene2Veg.isEmpty()) {
        QSKIP("Extended test .veg file not present (set DRIFT_EXTENDED_VEGAS_TESTS_DIR to run)");
    }

    QString error;
    const auto optProj = drift::vegas::readProject(scene2Veg, &error);
    QVERIFY2(optProj.has_value(), qPrintable(error));

    const drift::Project &proj = *optProj;
    qDebug() << "Loaded Scene 2 VEGAS project:" << proj.name()
             << "Tracks:" << proj.tracks().size()
             << "Assets:" << proj.assets().size();

    for (const auto &asset : proj.assets()) {
        qDebug() << "Asset:" << asset.id << "path:" << asset.path << "exists:" << QFile::exists(asset.path)
                 << "kind:" << (asset.kind == drift::MediaKind::Video ? "video" : "audio")
                 << "editFriendly:" << asset.editFriendly;
    }

    // Verify native VEGAS event grouping (P6-04)
    int linkedCount = 0;
    QHash<QString, int> linkIdCounts;
    for (const drift::Track &track : proj.tracks()) {
        for (const drift::Clip &clip : track.clips) {
            if (!clip.linkId.isEmpty()) {
                ++linkedCount;
                ++linkIdCounts[clip.linkId];
            }
        }
    }
    qDebug() << "E3S2 linked clips:" << linkedCount << "unique link groups:" << linkIdCounts.size();
    QVERIFY(linkedCount >= 80);
    QVERIFY(linkIdCounts.size() >= 35);
    for (auto it = linkIdCounts.constBegin(); it != linkIdCounts.constEnd(); ++it) {
        QVERIFY2(it.value() >= 2, qPrintable(QStringLiteral("Link group %1 has fewer than 2 clips: %2")
                                                .arg(it.key()).arg(it.value())));
    }
}

void TestInterchangeVegas::readsEpisode4VegasProjectWithOffset96AndSubdirectories()
{
    const QString ep4Veg = findExtendedVeg(QStringLiteral("Episode 4/1.1.veg"));
    if (ep4Veg.isEmpty()) {
        QSKIP("Extended test .veg file not present (set DRIFT_EXTENDED_VEGAS_TESTS_DIR to run)");
    }

    QString error;
    const auto optProj = drift::vegas::readProject(ep4Veg, &error);
    QVERIFY2(optProj.has_value(), qPrintable(error));

    const drift::Project &proj = *optProj;
    qDebug() << "Loaded Episode 4 VEGAS project:" << proj.name()
             << "Tracks:" << proj.tracks().size()
             << "Assets:" << proj.assets().size();

    QVERIFY(proj.assets().size() >= 20);
    int unconvertedCount = 0;
    int resolvedCount = 0;

    int videoCount = 0;
    for (const auto &asset : proj.assets()) {
        qDebug() << "Ep4 Asset:" << asset.id << "path:" << asset.path << "exists:" << QFile::exists(asset.path)
                 << "kind:" << (asset.kind == drift::MediaKind::Video ? "video" : "audio")
                 << "editFriendly:" << asset.editFriendly;
        if (QFile::exists(asset.path)) {
            ++resolvedCount;
        }
        if (asset.kind == drift::MediaKind::Video) {
            ++videoCount;
            if (!asset.editFriendly) {
                ++unconvertedCount;
                QVERIFY(asset.path.endsWith(QLatin1String(".MTS"), Qt::CaseInsensitive));
            } else {
                QVERIFY(asset.path.endsWith(QLatin1String(".mov"), Qt::CaseInsensitive));
            }
        }
    }

    // All assets should be resolved on disk thanks to universal offset scan and subdirectory indexing
    QCOMPARE(resolvedCount, proj.assets().size());
    // Seven video clips total in Ep 4 (six camera clips + one plate effect)
    QCOMPARE(videoCount, 7);

    // Verify track metadata & naming (P6-05)
    QCOMPARE(proj.tracks().size(), 4);
    QCOMPARE(proj.tracks()[0].name, QStringLiteral("Track 1 (video)"));
    QCOMPARE(proj.tracks()[0].type, drift::TrackType::Video);
    QCOMPARE(proj.tracks()[0].muted, false);
    QCOMPARE(proj.tracks()[0].solo, false);

    QCOMPARE(proj.tracks()[1].name, QStringLiteral("Sin"));
    QCOMPARE(proj.tracks()[1].type, drift::TrackType::Audio);
    QCOMPARE(proj.tracks()[1].muted, false);
    QCOMPARE(proj.tracks()[1].solo, false);
    QCOMPARE(proj.tracks()[1].volume, 1.0);
    QCOMPARE(proj.tracks()[1].pan, 0.0);

    QCOMPARE(proj.tracks()[2].name, QStringLiteral("Andrew"));
    QCOMPARE(proj.tracks()[2].type, drift::TrackType::Audio);

    QCOMPARE(proj.tracks()[3].name, QStringLiteral("Boom"));
    QCOMPARE(proj.tracks()[3].type, drift::TrackType::Audio);
}

void TestInterchangeVegas::readsEpisode4WorkprintWithNestedTimelines()
{
    const QString sceneVeg = findExtendedVeg(QStringLiteral("Episode 4/10.1b.veg"));
    if (!sceneVeg.isEmpty()) {
        QString err;
        auto optScene = drift::vegas::readProject(sceneVeg, &err);
        QVERIFY2(optScene.has_value(), qPrintable(err));
        qDebug() << "10.1b.veg loaded! Tracks:" << optScene->tracks().size()
                 << "Assets:" << optScene->assets().size();
    }

    const QString workprintVeg = findExtendedVeg(QStringLiteral("Episode 4/Workprint.veg"));
    if (workprintVeg.isEmpty()) {
        QSKIP("Extended test .veg file not present (set DRIFT_EXTENDED_VEGAS_TESTS_DIR to run)");
    }

    QString error;
    const auto optProj = drift::vegas::readProject(workprintVeg, &error);
    QVERIFY2(optProj.has_value(), qPrintable(error));

    const drift::Project &proj = *optProj;
    qDebug() << "Workprint loaded:" << proj.name()
             << "Tracks:" << proj.tracks().size()
             << "Assets:" << proj.assets().size()
             << "Sequences:" << proj.sequenceIds();

    for (const auto &asset : proj.assets()) {
        qDebug() << "Workprint Asset:" << asset.id << "path:" << asset.path
                 << "kind:" << drift::mediaKindToString(asset.kind)
                 << "exists:" << QFile::exists(asset.path);
    }

    for (int t = 0; t < proj.tracks().size(); ++t) {
        const auto &track = proj.tracks()[t];
        qDebug() << "Track" << t << "type:" << drift::trackTypeToString(track.type)
                 << "clips:" << track.clips.size();
        for (const auto &clip : track.clips) {
            qDebug() << "  Clip:" << clip.id << "type:" << drift::clipTypeToString(clip.type)
                     << "assetId:" << clip.assetId << "sequenceId:" << clip.sequenceId
                     << "timelineStart:" << clip.timelineStart << "dur:" << clip.timelineDuration;
        }
    }

    // Verify native event grouping on master assembly
    int wpLinkedCount = 0;
    QHash<QString, int> wpLinkCounts;
    for (const drift::Track &track : proj.tracks()) {
        for (const drift::Clip &clip : track.clips) {
            if (!clip.linkId.isEmpty()) {
                ++wpLinkedCount;
                ++wpLinkCounts[clip.linkId];
            }
        }
    }
    qDebug() << "Workprint linked clips:" << wpLinkedCount << "link groups:" << wpLinkCounts.size();
    QVERIFY(wpLinkedCount >= 20);
    QVERIFY(wpLinkCounts.size() >= 10);
    for (auto it = wpLinkCounts.constBegin(); it != wpLinkCounts.constEnd(); ++it) {
        QVERIFY(it.value() >= 2);
    }
}

void TestInterchangeVegas::readsTrackMetadataStateAndProperties()
{
    // 1. Test mute states, volume levels, and names on 6.1a.veg
    const QString veg61a = findExtendedVeg(QStringLiteral("Episode 4/OLD/6.1a.veg"));
    if (!veg61a.isEmpty()) {
        QString err;
        const auto optProj = drift::vegas::readProject(veg61a, &err);
        QVERIFY2(optProj.has_value(), qPrintable(err));
        const drift::Project &proj = *optProj;
        QCOMPARE(proj.tracks().size(), 6);

        // Track 0 & 1: muted camera audio / reference tracks
        QVERIFY(proj.tracks()[0].muted);
        QVERIFY(!proj.tracks()[0].solo);
        QVERIFY(proj.tracks()[1].muted);
        QVERIFY(!proj.tracks()[1].solo);
        QVERIFY(!proj.tracks()[2].muted);

        // Track 3: Sin (unmuted, boosted volume ~1.603)
        QCOMPARE(proj.tracks()[3].name, QStringLiteral("Sin"));
        QVERIFY(!proj.tracks()[3].muted);
        QVERIFY(std::abs(proj.tracks()[3].volume - 1.6032) < 0.01);

        // Track 4: Diana (unmuted, boosted volume ~1.758)
        QCOMPARE(proj.tracks()[4].name, QStringLiteral("Diana"));
        QVERIFY(!proj.tracks()[4].muted);
        QVERIFY(std::abs(proj.tracks()[4].volume - 1.7579) < 0.01);

        // Track 5: Room Tone (muted, volume ~0.822)
        QCOMPARE(proj.tracks()[5].name, QStringLiteral("Room Tone"));
        QVERIFY(proj.tracks()[5].muted);
        QVERIFY(std::abs(proj.tracks()[5].volume - 0.8222) < 0.01);
    }

    // 2. Test solo states on BEACH PARTY B.veg
    const QString vegBeach = findExtendedVeg(QStringLiteral("Episode 4/BEACH PARTY B.veg"));
    if (!vegBeach.isEmpty()) {
        QString err;
        const auto optProj = drift::vegas::readProject(vegBeach, &err);
        QVERIFY2(optProj.has_value(), qPrintable(err));
        const drift::Project &proj = *optProj;
        QVERIFY(proj.tracks().size() >= 9);

        QVERIFY(!proj.tracks()[0].solo);
        QVERIFY(proj.tracks()[1].solo);
        QVERIFY(!proj.tracks()[2].solo);
        QVERIFY(proj.tracks()[6].solo);
        QVERIFY(proj.tracks()[7].solo);
        QVERIFY(proj.tracks()[8].solo);
    }

    // 3. Test stereo pan on 9.4c.veg
    const QString vegPan = findExtendedVeg(QStringLiteral("Episode 4/Intermediates/9.4c.veg"));
    if (!vegPan.isEmpty()) {
        QString err;
        const auto optProj = drift::vegas::readProject(vegPan, &err);
        QVERIFY2(optProj.has_value(), qPrintable(err));
        const drift::Project &proj = *optProj;
        QCOMPARE(proj.tracks().size(), 3);

        // Track 1: panned full Left (-1.0)
        QVERIFY(std::abs(proj.tracks()[1].pan - (-1.0)) < 0.01);
        // Track 2: panned full Right (+1.0)
        QVERIFY(std::abs(proj.tracks()[2].pan - 1.0) < 0.01);
    }
}

void TestInterchangeVegas::readsTimelineMarkersAndRegions()
{
    // 1. Test Previously On.veg for take region (Record Take 1)
    const QString vegPrev = findExtendedVeg(QStringLiteral("Episode 4/Previously On.veg"));
    if (!vegPrev.isEmpty()) {
        QString err;
        const auto optProj = drift::vegas::readProject(vegPrev, &err);
        QVERIFY2(optProj.has_value(), qPrintable(err));
        const drift::Project &proj = *optProj;

        bool foundIn = false;
        bool foundOut = false;
        for (const auto &bm : proj.bookmarks()) {
            if (bm.label == QStringLiteral("Record Take 1") && bm.timeUs == 0)
                foundIn = true;
            if (bm.label.contains(QStringLiteral("Record Take 1")) && bm.timeUs == 38671)
                foundOut = true;
        }
        QVERIFY(foundIn);
        QVERIFY(foundOut);
    }

    // 2. Test Acid's Story B.veg for marker (Marker 5) and source marker
    const QString vegAcid = findExtendedVeg(QStringLiteral("Episode 5 & 6/Episode 5/Acid's Story B.veg"));
    if (!vegAcid.isEmpty()) {
        QString err;
        const auto optProj = drift::vegas::readProject(vegAcid, &err);
        QVERIFY2(optProj.has_value(), qPrintable(err));
        const drift::Project &proj = *optProj;

        bool foundMarker5 = false;
        for (const auto &bm : proj.bookmarks()) {
            if (bm.label == QStringLiteral("Marker 5"))
                foundMarker5 = true;
        }
        QVERIFY(foundMarker5);

        // Verify SourceMarker was extracted on asset
        bool foundSourceMarker = false;
        for (const auto &asset : proj.assets()) {
            for (const auto &sm : asset.markers) {
                if (sm.name == QStringLiteral("Marker 5"))
                    foundSourceMarker = true;
            }
        }
        QVERIFY(foundSourceMarker);
    }

    // 3. Test synthetic Wave64 project with kTypeMarkerList (point markers and regions)
    {
        static const uint8_t kW64Riff[16] = {
            0x72, 0x69, 0x66, 0x66, 0x2e, 0x91, 0xcf, 0x11, 0xa5, 0xd6, 0x28, 0xdb, 0x04, 0xc1, 0x00, 0x00
        };
        static const uint8_t kW64Wave[16] = {
            0xef, 0x29, 0xc4, 0x46, 0x4a, 0x90, 0xd2, 0x11, 0x87, 0x22, 0x00, 0xc0, 0x4f, 0x8e, 0xdb, 0x8a
        };
        static const uint8_t kW64List[16] = {
            0x6c, 0x69, 0x73, 0x74, 0x2f, 0x91, 0xcf, 0x11, 0xa5, 0xd6, 0x28, 0xdb, 0x04, 0xc1, 0x00, 0x00
        };
        static const uint8_t kTypeTimeline[16] = {
            0x47, 0x07, 0x6c, 0x4d, 0x16, 0x23, 0xd2, 0x11, 0x86, 0xb0, 0x00, 0xc0, 0x4f, 0x8e, 0xdb, 0x8a
        };
        static const uint8_t kTypeTrack[16] = {
            0x48, 0x07, 0x6c, 0x4d, 0x16, 0x23, 0xd2, 0x11, 0x86, 0xb0, 0x00, 0xc0, 0x4f, 0x8e, 0xdb, 0x8a
        };
        static const uint8_t kTypeEventContainer[16] = {
            0x49, 0x07, 0x6c, 0x4d, 0x16, 0x23, 0xd2, 0x11, 0x86, 0xb0, 0x00, 0xc0, 0x4f, 0x8e, 0xdb, 0x8a
        };
        static const uint8_t kTypeEvent[16] = {
            0x4a, 0x07, 0x6c, 0x4d, 0x16, 0x23, 0xd2, 0x11, 0x86, 0xb0, 0x00, 0xc0, 0x4f, 0x8e, 0xdb, 0x8a
        };
        static const uint8_t kChunkEventTime[16] = {
            0x6a, 0x20, 0x8d, 0x16, 0x21, 0x23, 0xd2, 0x11, 0x86, 0xb0, 0x00, 0xc0, 0x4f, 0x8e, 0xdb, 0x8a
        };
        static const uint8_t kTypeMarkerList[16] = {
            0xb5, 0x00, 0x09, 0x1f, 0x84, 0xdc, 0x43, 0x47, 0x81, 0xcb, 0x08, 0x77, 0x44, 0x40, 0x45, 0x98
        };
        static const uint8_t kGuidMarker[16] = {
            0x56, 0x62, 0xf7, 0xab, 0x2d, 0x39, 0xd2, 0x11, 0x86, 0xc7, 0x00, 0xc0, 0x4f, 0x8e, 0xdb, 0x8a
        };

        auto writeU32 = [](QByteArray &b, quint32 v) {
            b.append(reinterpret_cast<const char *>(&v), 4);
        };
        auto writeU64 = [](QByteArray &b, quint64 v) {
            b.append(reinterpret_cast<const char *>(&v), 8);
        };
        auto writeGuid = [](QByteArray &b, const uint8_t *g) {
            b.append(reinterpret_cast<const char *>(g), 16);
        };
        auto makeChunk = [&](const uint8_t *guid, const QByteArray &payload) {
            QByteArray chunk;
            writeGuid(chunk, guid);
            writeU64(chunk, 24 + payload.size());
            chunk.append(payload);
            return chunk;
        };
        auto makeList = [&](const uint8_t *typeGuid, const QByteArray &subChunks) {
            QByteArray payload;
            writeGuid(payload, typeGuid);
            payload.append(subChunks);
            return makeChunk(kW64List, payload);
        };

        // Project settings chunk
        QByteArray c0Payload(48, 0);
        static const uint8_t kGuidSettings[16] = {
            0xb2, 0x8f, 0x2d, 0x5a, 0x23, 0x0f, 0x11, 0xd2, 0xaf, 0x86, 0x00, 0xc0, 0x4f, 0x8e, 0xdb, 0x8a
        };
        QByteArray c0 = makeChunk(kGuidSettings, c0Payload);

        // Track with 1 event
        QByteArray timePayload;
        timePayload.append(16, 0);
        writeU64(timePayload, 0);        // start 0
        writeU64(timePayload, 10000000); // dur 1 sec
        QByteArray timeChunk = makeChunk(kChunkEventTime, timePayload);
        QByteArray eventList = makeList(kTypeEvent, timeChunk);
        QByteArray eventContainer = makeList(kTypeEventContainer, eventList);
        QByteArray trackList = makeList(kTypeTrack, eventContainer);
        QByteArray timelineList = makeList(kTypeTimeline, trackList);

        // Marker chunk helper
        auto makeMarker = [&](quint32 id, qint64 posTicks, qint64 lenTicks, const QString &name) {
            QByteArray chunk;
            writeGuid(chunk, kGuidMarker);
            QByteArray nameBytes;
            if (!name.isEmpty()) {
                const auto *u16 = name.utf16();
                nameBytes.append(reinterpret_cast<const char *>(u16), (name.size() + 1) * sizeof(char16_t));
            }
            const quint32 hdrSize = 28;
            const quint32 dataSize = hdrSize + nameBytes.size();
            writeU32(chunk, dataSize);
            writeU32(chunk, hdrSize);
            writeU32(chunk, id);
            writeU64(chunk, posTicks);
            writeU64(chunk, lenTicks);
            writeU32(chunk, nameBytes.size());
            chunk.append(nameBytes);
            return chunk;
        };

        QByteArray markerData;
        markerData.append(makeMarker(1, 10000000, -1, QStringLiteral("Intro")));        // Point marker at 1.0s
        markerData.append(makeMarker(2, 20000000, -1, QString()));                      // Unnamed marker at 2.0s -> "Marker 2"
        markerData.append(makeMarker(3, 30000000, 15000000, QStringLiteral("Chorus"))); // Region at 3.0s, len 1.5s -> "Chorus" and "Chorus (Out)"

        QByteArray markerList = makeList(kTypeMarkerList, markerData);

        QByteArray body = c0 + timelineList + markerList;
        QByteArray synthData;
        writeGuid(synthData, kW64Riff);
        writeU64(synthData, 40 + body.size());
        writeGuid(synthData, kW64Wave);
        synthData.append(body);

        QString err;
        const auto optProj = drift::vegas::readProjectData(synthData, QString(), &err);
        QVERIFY2(optProj.has_value(), qPrintable(err));
        const drift::Project &proj = *optProj;

        QCOMPARE(proj.bookmarks().size(), 4);
        // 1. timeUs = 1,000,000 -> "Intro"
        QCOMPARE(proj.bookmarks()[0].timeUs, 1000000);
        QCOMPARE(proj.bookmarks()[0].label, QStringLiteral("Intro"));

        // 2. timeUs = 2,000,000 -> "Marker 2"
        QCOMPARE(proj.bookmarks()[1].timeUs, 2000000);
        QCOMPARE(proj.bookmarks()[1].label, QStringLiteral("Marker 2"));

        // 3. timeUs = 3,000,000 -> "Chorus"
        QCOMPARE(proj.bookmarks()[2].timeUs, 3000000);
        QCOMPARE(proj.bookmarks()[2].label, QStringLiteral("Chorus"));

        // 4. timeUs = 4,500,000 -> "Chorus (Out)"
        QCOMPARE(proj.bookmarks()[3].timeUs, 4500000);
        QCOMPARE(proj.bookmarks()[3].label, QStringLiteral("Chorus (Out)"));
    }
}

void TestInterchangeVegas::detectsCrossfadesAndTransitions()
{
    // 1. Test e3s5.veg for track transitions
    const QString vegE3S5 = findExtendedVeg(QStringLiteral("Episode 3/Remaster/Scene 5/e3s5.veg"));
    if (!vegE3S5.isEmpty()) {
        QString err;
        const auto optProj = drift::vegas::readProject(vegE3S5, &err);
        QVERIFY2(optProj.has_value(), qPrintable(err));
        const drift::Project &proj = *optProj;

        int totalTransitions = 0;
        for (const auto &track : proj.tracks()) {
            totalTransitions += track.transitions.size();
        }
        QCOMPARE(totalTransitions, 13);
        QCOMPARE(proj.tracks()[1].transitions.size(), 7);
        QCOMPARE(proj.tracks()[2].transitions.size(), 6);

        // Verify the 1.810s crossfade between clip 0 and clip 1 on track 1 (video)
        const auto &tr0 = proj.tracks()[1].transitions[0];
        QCOMPARE(tr0.kindId, QStringLiteral("crossfade"));
        QCOMPARE(tr0.durationUs, 1809956);
        QCOMPARE(tr0.fromClipId, proj.tracks()[1].clips[0].id);
        QCOMPARE(tr0.toClipId, proj.tracks()[1].clips[1].id);

        drift::TimeUs winStart = 0, winEnd = 0;
        QVERIFY(drift::transitionWindow(proj.tracks()[1], tr0, winStart, winEnd));
        QCOMPARE(winStart, 20212044);
        QCOMPARE(winEnd, 22022000);
        QVERIFY(drift::clipsPhysicallyOverlap(proj.tracks()[1].clips[0], proj.tracks()[1].clips[1]));
    }

    // 2. Test e3s8-9-10.veg for crossfades
    const QString vegE3S8 = findExtendedVeg(QStringLiteral("Episode 3/Remaster/Scene 8,9,10/e3s8-9-10.veg"));
    if (!vegE3S8.isEmpty()) {
        QString err;
        const auto optProj = drift::vegas::readProject(vegE3S8, &err);
        QVERIFY2(optProj.has_value(), qPrintable(err));
        const drift::Project &proj = *optProj;

        int totalTransitions = 0;
        for (const auto &track : proj.tracks()) {
            totalTransitions += track.transitions.size();
        }
        QCOMPARE(totalTransitions, 13);
        QCOMPARE(proj.tracks()[0].transitions.size(), 5);

        // Check first transition duration on Track 0
        const auto &tr0 = proj.tracks()[0].transitions[0];
        QCOMPARE(tr0.kindId, QStringLiteral("crossfade"));
        QCOMPARE(tr0.durationUs, 667332);
    }

    // 3. Test synthetic Wave64 project with overlapping clips
    {
        static const uint8_t kW64Riff[16] = {
            0x72, 0x69, 0x66, 0x66, 0x2e, 0x91, 0xcf, 0x11, 0xa5, 0xd6, 0x28, 0xdb, 0x04, 0xc1, 0x00, 0x00
        };
        static const uint8_t kW64Wave[16] = {
            0xef, 0x29, 0xc4, 0x46, 0x4a, 0x90, 0xd2, 0x11, 0x87, 0x22, 0x00, 0xc0, 0x4f, 0x8e, 0xdb, 0x8a
        };
        static const uint8_t kW64List[16] = {
            0x6c, 0x69, 0x73, 0x74, 0x2f, 0x91, 0xcf, 0x11, 0xa5, 0xd6, 0x28, 0xdb, 0x04, 0xc1, 0x00, 0x00
        };
        static const uint8_t kTypeTimeline[16] = {
            0x47, 0x07, 0x6c, 0x4d, 0x16, 0x23, 0xd2, 0x11, 0x86, 0xb0, 0x00, 0xc0, 0x4f, 0x8e, 0xdb, 0x8a
        };
        static const uint8_t kTypeTrack[16] = {
            0x48, 0x07, 0x6c, 0x4d, 0x16, 0x23, 0xd2, 0x11, 0x86, 0xb0, 0x00, 0xc0, 0x4f, 0x8e, 0xdb, 0x8a
        };
        static const uint8_t kTypeEventContainer[16] = {
            0x49, 0x07, 0x6c, 0x4d, 0x16, 0x23, 0xd2, 0x11, 0x86, 0xb0, 0x00, 0xc0, 0x4f, 0x8e, 0xdb, 0x8a
        };
        static const uint8_t kTypeEvent[16] = {
            0x4a, 0x07, 0x6c, 0x4d, 0x16, 0x23, 0xd2, 0x11, 0x86, 0xb0, 0x00, 0xc0, 0x4f, 0x8e, 0xdb, 0x8a
        };
        static const uint8_t kChunkEventTime[16] = {
            0x6a, 0x20, 0x8d, 0x16, 0x21, 0x23, 0xd2, 0x11, 0x86, 0xb0, 0x00, 0xc0, 0x4f, 0x8e, 0xdb, 0x8a
        };

        auto writeU64 = [](QByteArray &b, quint64 v) {
            b.append(reinterpret_cast<const char *>(&v), 8);
        };
        auto writeGuid = [](QByteArray &b, const uint8_t *g) {
            b.append(reinterpret_cast<const char *>(g), 16);
        };
        auto makeChunk = [&](const uint8_t *guid, const QByteArray &payload) {
            QByteArray chunk;
            writeGuid(chunk, guid);
            writeU64(chunk, 24 + payload.size());
            chunk.append(payload);
            return chunk;
        };
        auto makeList = [&](const uint8_t *typeGuid, const QByteArray &subChunks) {
            QByteArray payload;
            writeGuid(payload, typeGuid);
            payload.append(subChunks);
            return makeChunk(kW64List, payload);
        };

        QByteArray c0Payload(48, 0);
        static const uint8_t kGuidSettings[16] = {
            0xb2, 0x8f, 0x2d, 0x5a, 0x23, 0x0f, 0x11, 0xd2, 0xaf, 0x86, 0x00, 0xc0, 0x4f, 0x8e, 0xdb, 0x8a
        };
        QByteArray c0 = makeChunk(kGuidSettings, c0Payload);

        // Make event 1: start 0, dur 3s (30,000,000 ticks)
        QByteArray timePayload1;
        timePayload1.append(16, 0);
        writeU64(timePayload1, 0);
        writeU64(timePayload1, 30000000);
        QByteArray ev1 = makeList(kTypeEvent, makeChunk(kChunkEventTime, timePayload1));

        // Make event 2: start 2s (20,000,000 ticks), dur 3s (30,000,000 ticks) -> 1.0s overlap
        QByteArray timePayload2;
        timePayload2.append(16, 0);
        writeU64(timePayload2, 20000000);
        writeU64(timePayload2, 30000000);
        QByteArray ev2 = makeList(kTypeEvent, makeChunk(kChunkEventTime, timePayload2));

        QByteArray eventContainer = makeList(kTypeEventContainer, ev1 + ev2);
        QByteArray trackList = makeList(kTypeTrack, eventContainer);
        QByteArray timelineList = makeList(kTypeTimeline, trackList);

        QByteArray body = c0 + timelineList;
        QByteArray synthData;
        writeGuid(synthData, kW64Riff);
        writeU64(synthData, 40 + body.size());
        writeGuid(synthData, kW64Wave);
        synthData.append(body);

        QString err;
        const auto optProj = drift::vegas::readProjectData(synthData, QString(), &err);
        QVERIFY2(optProj.has_value(), qPrintable(err));
        const drift::Project &proj = *optProj;

        QCOMPARE(proj.tracks().size(), 1);
        QCOMPARE(proj.tracks()[0].clips.size(), 2);
        QCOMPARE(proj.tracks()[0].transitions.size(), 1);

        const auto &tr = proj.tracks()[0].transitions[0];
        QCOMPARE(tr.fromClipId, proj.tracks()[0].clips[0].id);
        QCOMPARE(tr.toClipId, proj.tracks()[0].clips[1].id);
        QCOMPARE(tr.kindId, QStringLiteral("crossfade"));
        QCOMPARE(tr.durationUs, 1000000);

        drift::TimeUs startUs = 0, endUs = 0;
        QVERIFY(drift::transitionWindow(proj.tracks()[0], tr, startUs, endUs));
        QCOMPARE(startUs, 2000000);
        QCOMPARE(endUs, 3000000);
    }
}

void TestInterchangeVegas::readsTitlesAndTextGenerators()
{
    // 1. Test Workprint.veg for Titles & Text generator clips
    const QString vegWorkprint = findExtendedVeg(QStringLiteral("Episode 4/Workprint.veg"));
    if (!vegWorkprint.isEmpty()) {
        QString err;
        const auto optProj = drift::vegas::readProject(vegWorkprint, &err);
        QVERIFY2(optProj.has_value(), qPrintable(err));
        const drift::Project &proj = *optProj;

        QList<drift::Clip> textClips;
        for (const auto &track : proj.tracks()) {
            for (const auto &clip : track.clips) {
                if (clip.type == drift::ClipType::Text)
                    textClips.append(clip);
            }
        }

        qDebug() << "Workprint Text Clips found:" << textClips.size();
        QVERIFY(textClips.size() >= 2);

        bool foundThreeMonths = false;
        bool foundWardEight = false;
        for (const auto &clip : textClips) {
            qDebug() << "  Text clip text:" << clip.textContent
                     << "font:" << clip.textStyle.fontFamily
                     << "pixelSize:" << clip.textStyle.pixelSize
                     << "align:" << drift::textAlignToString(clip.textStyle.align);

            QCOMPARE(clip.textStyle.fontFamily, QStringLiteral("Courier New"));
            QCOMPARE(clip.textStyle.align, drift::TextAlign::Center);
            QVERIFY(clip.textStyle.pixelSize > 0);

            if (clip.textContent.contains(QStringLiteral("THREE"))
                && clip.textContent.contains(QStringLiteral("MONTHS"))) {
                foundThreeMonths = true;
            }
            if (clip.textContent.contains(QStringLiteral("WARD"))
                && clip.textContent.contains(QStringLiteral("EIGHT"))) {
                foundWardEight = true;
            }
        }
        QVERIFY(foundThreeMonths);
        QVERIFY(foundWardEight);
    }

    // 2. Test Previously On.veg for "PREVIOUSLY ON" title card and solid color
    const QString vegPrev = findExtendedVeg(QStringLiteral("Episode 4/Previously On.veg"));
    if (!vegPrev.isEmpty()) {
        QString err;
        const auto optProj = drift::vegas::readProject(vegPrev, &err);
        QVERIFY2(optProj.has_value(), qPrintable(err));
        const drift::Project &proj = *optProj;

        bool foundPreviouslyOn = false;
        for (const auto &track : proj.tracks()) {
            for (const auto &clip : track.clips) {
                if (clip.type == drift::ClipType::Text && clip.textContent.contains(QStringLiteral("PREVIOUSLY ON"))) {
                    foundPreviouslyOn = true;
                    QCOMPARE(clip.textStyle.fontFamily, QStringLiteral("Franklin Gothic Medium"));
                    QVERIFY(clip.textStyle.pixelSize > 0);
                }
            }
        }
        QVERIFY(foundPreviouslyOn);
    }
}

void TestInterchangeVegas::readsVideoEffectsLevelsAndColor()
{
    // Test e3s3.veg for media-level Video FX (Levels, Color Corrector, Sharpen)
    const QString vegScene3 = findExtendedVeg(QStringLiteral("Episode 3/Remaster/Scene 3/e3s3.veg"));
    if (!vegScene3.isEmpty()) {
        QString err;
        const auto optProj = drift::vegas::readProject(vegScene3, &err);
        QVERIFY2(optProj.has_value(), qPrintable(err));
        const drift::Project &proj = *optProj;

        int totalAssetsWithEffects = 0;
        bool foundLevels = false;
        bool foundColorGrade = false;
        bool foundSharpen = false;

        const auto inspectFx = [&](const QList<drift::Effect> &effects) {
            for (const auto &fx : effects) {
                if (fx.catalogId == QStringLiteral("color.grade")
                    && fx.parameters.contains(QStringLiteral("curveMaster"))) {
                    foundLevels = true;
                    QVERIFY(!fx.parameters.value(QStringLiteral("curveMaster")).toString().isEmpty());
                } else if (fx.catalogId == QStringLiteral("color.grade")) {
                    foundColorGrade = true;
                    QVERIFY(fx.parameters.contains(QStringLiteral("saturation")));
                    QVERIFY(fx.parameters.value(QStringLiteral("saturation")).toDouble() > 1.0);
                } else if (fx.catalogId == QStringLiteral("stylize.sharpen")) {
                    foundSharpen = true;
                    QVERIFY(fx.parameters.contains(QStringLiteral("luma_amount")));
                    QVERIFY(fx.parameters.value(QStringLiteral("luma_amount")).toDouble() > 0.0);
                }
            }
        };

        for (const auto &asset : proj.assets()) {
            if (!asset.effects.isEmpty()) {
                ++totalAssetsWithEffects;
                inspectFx(asset.effects);
            }
        }

        for (const auto &track : proj.tracks()) {
            for (const auto &clip : track.clips) {
                if (!clip.effects.isEmpty()) {
                    inspectFx(clip.effects);
                }
            }
        }

        qDebug() << "e3s3.veg assets with effects:" << totalAssetsWithEffects
                 << "foundLevels:" << foundLevels
                 << "foundColorGrade:" << foundColorGrade
                 << "foundSharpen:" << foundSharpen;

        QVERIFY(totalAssetsWithEffects >= 5);
        QVERIFY(foundLevels);
        QVERIFY(foundColorGrade);
        QVERIFY(foundSharpen);
    }
}

void TestInterchangeVegas::readsVignetteAndSoftlightEffects()
{
    // 1. Test BEACH PARTY B.veg for Media Pool Sony Vignette
    const QString vegBeach = findExtendedVeg(QStringLiteral("Episode 4/BEACH PARTY B.veg"));
    if (!vegBeach.isEmpty()) {
        QString err;
        const auto optProj = drift::vegas::readProject(vegBeach, &err);
        QVERIFY2(optProj.has_value(), qPrintable(err));
        const drift::Project &proj = *optProj;

        bool foundVignette = false;
        // P11-04: Media Pool FX are preserved directly on MediaAsset::effects
        for (const auto &asset : proj.assets()) {
            for (const auto &fx : asset.effects) {
                if (fx.catalogId == QStringLiteral("stylize.vignette")) {
                    foundVignette = true;
                    QVERIFY(fx.parameters.contains(QStringLiteral("amount")));
                    QVERIFY(fx.parameters.value(QStringLiteral("amount")).toDouble() > 0.0);
                    QVERIFY(fx.parameters.contains(QStringLiteral("feather")));
                    QVERIFY(fx.parameters.value(QStringLiteral("feather")).toDouble() > 0.0);
                    QCOMPARE(fx.parameters.value(QStringLiteral("center.x")).toDouble(), 0.5);
                    QCOMPARE(fx.parameters.value(QStringLiteral("center.y")).toDouble(), 0.5);
                }
            }
        }
        for (const auto &track : proj.tracks()) {
            for (const auto &clip : track.clips) {
                for (const auto &fx : clip.effects) {
                    if (fx.catalogId == QStringLiteral("stylize.vignette")) {
                        foundVignette = true;
                        QVERIFY(fx.parameters.contains(QStringLiteral("amount")));
                        QVERIFY(fx.parameters.value(QStringLiteral("amount")).toDouble() > 0.0);
                        QVERIFY(fx.parameters.contains(QStringLiteral("feather")));
                        QVERIFY(fx.parameters.value(QStringLiteral("feather")).toDouble() > 0.0);
                        QVERIFY(fx.parameters.contains(QStringLiteral("center.x")));
                        QCOMPARE(fx.parameters.value(QStringLiteral("center.x")).toDouble(), 0.5);
                        QCOMPARE(fx.parameters.value(QStringLiteral("center.y")).toDouble(), 0.5);
                    }
                }
            }
        }
        qDebug() << "BEACH PARTY B.veg foundVignette:" << foundVignette;
        QVERIFY(foundVignette);
    }

    // 2. Test Credits Episode 3.veg for Event-level Softlight XML and Levels
    const QString vegCredits = findExtendedVeg(QStringLiteral("Episode 3/Remaster/Credits Episode 3.veg"));
    if (!vegCredits.isEmpty()) {
        QString err;
        const auto optProj = drift::vegas::readProject(vegCredits, &err);
        QVERIFY2(optProj.has_value(), qPrintable(err));
        const drift::Project &proj = *optProj;

        bool foundSoftlightGlow = false;
        bool foundSoftlightVignette = false;
        bool foundTakeLevels = false;

        for (const auto &track : proj.tracks()) {
            for (const auto &clip : track.clips) {
                for (const auto &fx : clip.effects) {
                    if (fx.catalogId == QStringLiteral("bloom_glow")) {
                        foundSoftlightGlow = true;
                        QVERIFY(fx.parameters.contains(QStringLiteral("intensity")));
                        QVERIFY(fx.parameters.value(QStringLiteral("intensity")).toDouble() > 0.0);
                        QVERIFY(fx.parameters.contains(QStringLiteral("blurRadius")));
                    } else if (fx.catalogId == QStringLiteral("stylize.vignette")) {
                        foundSoftlightVignette = true;
                        QVERIFY(fx.parameters.contains(QStringLiteral("amount")));
                        QVERIFY(fx.parameters.contains(QStringLiteral("feather")));
                    } else if (fx.catalogId == QStringLiteral("color.grade")
                               && fx.parameters.contains(QStringLiteral("curveMaster"))) {
                        foundTakeLevels = true;
                        QVERIFY(!fx.parameters.value(QStringLiteral("curveMaster")).toString().isEmpty());
                    }
                }
            }
        }

        qDebug() << "Credits Episode 3.veg foundSoftlightGlow:" << foundSoftlightGlow
                 << "foundSoftlightVignette:" << foundSoftlightVignette
                 << "foundTakeLevels:" << foundTakeLevels;

        QVERIFY(foundSoftlightGlow);
        QVERIFY(foundSoftlightVignette);
        QVERIFY(foundTakeLevels);
    }
}

void TestInterchangeVegas::readsEventPanCropAndKeyframes()
{
    // 1. Test BEACH PARTY B.veg for both static cropping and 2D pan/zoom keyframes
    const QString vegBeach = findExtendedVeg(QStringLiteral("Episode 4/BEACH PARTY B.veg"));
    if (!vegBeach.isEmpty()) {
        QString err;
        const auto optProj = drift::vegas::readProject(vegBeach, &err);
        QVERIFY2(optProj.has_value(), qPrintable(err));
        const drift::Project &proj = *optProj;

        bool foundStaticCrop = false;
        bool foundStraightenAngle = false;
        bool foundAnimatedMotion = false;
        bool foundZoomInFromFull = false;
        bool foundZoomShift = false;
        int staticCropCount = 0;
        int animatedClipCount = 0;

        for (const auto &track : proj.tracks()) {
            for (const auto &clip : track.clips) {
                if (clip.sourceFrame != QRectF(0, 0, 1, 1)) {
                    foundStaticCrop = true;
                    ++staticCropCount;
                    // Check crop rectangle sanity
                    QVERIFY(clip.sourceFrame.x() >= -0.05 && clip.sourceFrame.x() <= 1.0);
                    QVERIFY(clip.sourceFrame.y() >= -0.05 && clip.sourceFrame.y() <= 1.0);
                    QVERIFY(clip.sourceFrame.width() > 0.1 && clip.sourceFrame.width() <= 1.0);
                    QVERIFY(clip.sourceFrame.height() > 0.1 && clip.sourceFrame.height() <= 1.0);

                    if (clip.sourceFrameAngle != 0.0) {
                        foundStraightenAngle = true;
                        // Expect small angle correction, e.g. around -0.89 degrees
                        QVERIFY(std::abs(clip.sourceFrameAngle) > 0.1);
                    }
                }

                if (!clip.scaleX.isEmpty() && clip.scaleX.keyframes().size() > 1) {
                    foundAnimatedMotion = true;
                    ++animatedClipCount;
                    QVERIFY(!clip.scaleY.isEmpty());
                    QVERIFY(!clip.positionX.isEmpty());
                    QVERIFY(!clip.positionY.isEmpty());

                    // Test 2-keyframe zooms in BEACH PARTY B
                    if (clip.scaleX.keyframes().size() == 2) {
                        const auto keys = clip.scaleX.keyframes();
                        QVERIFY(keys.contains(0));
                        const auto itKey1 = std::next(keys.constBegin());
                        QVERIFY(itKey1 != keys.constEnd());
                        QVERIFY(itKey1.key() > 0);
                        if (qFuzzyCompare(keys.value(0).value, 1.0)) {
                            foundZoomInFromFull = true;
                            QVERIFY(itKey1.value().value > 1.1);
                        } else if (keys.value(0).value > 1.1) {
                            foundZoomShift = true;
                        }
                    }
                }
            }
        }

        qDebug() << "BEACH PARTY B.veg static crops:" << staticCropCount
                 << "animated clips:" << animatedClipCount
                 << "straighten angle found:" << foundStraightenAngle
                 << "zoom in from full:" << foundZoomInFromFull
                 << "zoom shift:" << foundZoomShift;

        QVERIFY(foundStaticCrop);
        QVERIFY(staticCropCount >= 10);
        QVERIFY(foundStraightenAngle);
        QVERIFY(foundAnimatedMotion);
        QVERIFY(animatedClipCount >= 3);
        QVERIFY(foundZoomInFromFull);
        QVERIFY(foundZoomShift);
    }

    // 2. Test 3.1b.veg for 4-keyframe camera zoom out
    const QString veg31b = findExtendedVeg(QStringLiteral("Episode 4/3.1b.veg"));
    if (!veg31b.isEmpty()) {
        QString err;
        const auto optProj = drift::vegas::readProject(veg31b, &err);
        QVERIFY2(optProj.has_value(), qPrintable(err));
        const drift::Project &proj = *optProj;

        bool found4KeyframeZoom = false;
        for (const auto &track : proj.tracks()) {
            for (const auto &clip : track.clips) {
                if (clip.scaleX.keyframes().size() == 4) {
                    found4KeyframeZoom = true;
                    const auto &keys = clip.scaleX.keyframes();
                    QVERIFY(keys.contains(0));
                    // Starts zoomed in (1.785)
                    QVERIFY(keys.value(0).value > 1.7);
                    // Ends at 1.0 (full frame)
                    const auto lastKey = std::prev(keys.constEnd());
                    QCOMPARE(lastKey.value().value, 1.0);
                    // Middle keyframe checks
                    QVERIFY(clip.positionX.keyframes().size() == 4);
                    QVERIFY(clip.positionY.keyframes().size() == 4);
                }
            }
        }
        qDebug() << "3.1b.veg found 4-keyframe zoom out:" << found4KeyframeZoom;
        QVERIFY(found4KeyframeZoom);
    }
}

void TestInterchangeVegas::readsProjectSummaryRiffInfoChunks()
{
    // 1. Synthetic Wave64 RIFF project with summary metadata chunk
    const uint8_t kGuidW64Riff[16] = {
        0x72, 0x69, 0x66, 0x66, 0x2e, 0x91, 0xcf, 0x11, 0xa5, 0xd6, 0x28, 0xdb, 0x04, 0xc1, 0x00, 0x00
    };
    const uint8_t kGuidW64List[16] = {
        0x6c, 0x69, 0x73, 0x74, 0x2f, 0x91, 0xcf, 0x11, 0xa5, 0xd6, 0x28, 0xdb, 0x04, 0xc1, 0x00, 0x00
    };
    const uint8_t kTypeSummaryList[16] = {
        0xbc, 0x94, 0x5f, 0x92, 0x5a, 0x52, 0xd2, 0x11, 0x86, 0xdc, 0x00, 0xc0, 0x4f, 0x8e, 0xdb, 0x8a
    };
    const uint8_t kGuidSummaryChunk[16] = {
        0x1d, 0x4f, 0x23, 0x71, 0x57, 0x52, 0xd2, 0x11, 0x86, 0xdc, 0x00, 0xc0, 0x4f, 0x8e, 0xdb, 0x8a
    };

    auto encodeUtf16 = [](const QString &s) -> QByteArray {
        QByteArray bytes;
        const qsizetype len = s.length();
        bytes.resize((len + 1) * sizeof(char16_t));
        auto *chars = reinterpret_cast<char16_t *>(bytes.data());
        for (int i = 0; i < len; ++i)
            chars[i] = s.at(i).unicode();
        chars[len] = 0;
        return bytes;
    };

    struct FieldDef {
        const char *tag;
        QString text;
    };
    const QList<FieldDef> fields = {
        {"INAM", QStringLiteral("Odds Episode 3 Remaster")},
        {"IART", QStringLiteral("Gordon K")},
        {"ICMT", QStringLiteral("Director notes: final cut with remaster audio")},
        {"ICOP", QStringLiteral("Copyright 2026 Odds Productions")},
        {"IENG", QStringLiteral("Sound Engineer: Vegas Pro 21")},
        {"ICRD", QStringLiteral("2026-11-19")},
    };

    QByteArray subPayload;
    // 4 bytes hdrSize (8), 4 bytes fieldCount (fields.size())
    const quint32 hdrSize = 8;
    const quint32 fieldCount = static_cast<quint32>(fields.size());
    subPayload.append(reinterpret_cast<const char *>(&hdrSize), 4);
    subPayload.append(reinterpret_cast<const char *>(&fieldCount), 4);

    for (const auto &f : fields) {
        const QByteArray strBytes = encodeUtf16(f.text);
        const quint32 strLen = static_cast<quint32>(strBytes.size());
        subPayload.append(f.tag, 4);
        subPayload.append(reinterpret_cast<const char *>(&strLen), 4);
        subPayload.append(strBytes);
    }

    // Pad subchunk to 8-byte boundary
    quint64 subchunkSize = 24 + subPayload.size();
    const int padSub = static_cast<int>((8 - (subchunkSize % 8)) % 8);
    subPayload.append(QByteArray(padSub, '\0'));
    subchunkSize += padSub;

    QByteArray subChunk;
    subChunk.append(reinterpret_cast<const char *>(kGuidSummaryChunk), 16);
    subChunk.append(reinterpret_cast<const char *>(&subchunkSize), 8);
    subChunk.append(subPayload);

    QByteArray listPayload;
    listPayload.append(reinterpret_cast<const char *>(kTypeSummaryList), 16);
    listPayload.append(subChunk);

    quint64 listSize = 24 + listPayload.size();
    const int padList = static_cast<int>((8 - (listSize % 8)) % 8);
    listPayload.append(QByteArray(padList, '\0'));
    listSize += padList;

    QByteArray listChunk;
    listChunk.append(reinterpret_cast<const char *>(kGuidW64List), 16);
    listChunk.append(reinterpret_cast<const char *>(&listSize), 8);
    listChunk.append(listPayload);

    // Chunk 0 (project settings)
    QByteArray c0Payload(50, '\0');
    // Set 48000 sample rate at offset 12
    const quint32 sr = 48000;
    std::memcpy(c0Payload.data() + 12, &sr, 4);
    // Set 24.0 fps at offset 16
    const double fps = 24.0;
    std::memcpy(c0Payload.data() + 16, &fps, 8);

    quint64 c0Size = 24 + c0Payload.size();
    const int padC0 = static_cast<int>((8 - (c0Size % 8)) % 8);
    c0Payload.append(QByteArray(padC0, '\0'));
    c0Size += padC0;

    QByteArray c0Chunk;
    c0Chunk.append(QByteArray(16, '\0')); // settings GUID
    c0Chunk.append(reinterpret_cast<const char *>(&c0Size), 8);
    c0Chunk.append(c0Payload);

    // Minimal Timeline with 1 Track so readProjectData accepts the project
    const uint8_t kTypeTimeline[16] = {
        0x47, 0x07, 0x6c, 0x4d, 0x16, 0x23, 0xd2, 0x11, 0x86, 0xb0, 0x00, 0xc0, 0x4f, 0x8e, 0xdb, 0x8a
    };
    const uint8_t kTypeTrack[16] = {
        0x48, 0x07, 0x6c, 0x4d, 0x16, 0x23, 0xd2, 0x11, 0x86, 0xb0, 0x00, 0xc0, 0x4f, 0x8e, 0xdb, 0x8a
    };
    QByteArray trackChunk;
    const quint64 trackSize = 40;
    trackChunk.append(reinterpret_cast<const char *>(kGuidW64List), 16);
    trackChunk.append(reinterpret_cast<const char *>(&trackSize), 8);
    trackChunk.append(reinterpret_cast<const char *>(kTypeTrack), 16);

    QByteArray timelinePayload;
    timelinePayload.append(reinterpret_cast<const char *>(kTypeTimeline), 16);
    timelinePayload.append(trackChunk);
    const quint64 timelineSize = 24 + timelinePayload.size();
    QByteArray timelineChunk;
    timelineChunk.append(reinterpret_cast<const char *>(kGuidW64List), 16);
    timelineChunk.append(reinterpret_cast<const char *>(&timelineSize), 8);
    timelineChunk.append(timelinePayload);

    quint64 totalSize = 40 + c0Chunk.size() + listChunk.size() + timelineChunk.size();
    QByteArray w64Data;
    w64Data.append(reinterpret_cast<const char *>(kGuidW64Riff), 16);
    w64Data.append(reinterpret_cast<const char *>(&totalSize), 8);
    w64Data.append(QByteArray(16, '\0')); // type GUID
    w64Data.append(c0Chunk);
    w64Data.append(timelineChunk);
    w64Data.append(listChunk);

    // Test parseProjectSummary on Wave64 data
    const drift::vegas::ProjectSummary summary = drift::vegas::parseProjectSummary(w64Data);
    QVERIFY(!summary.isEmpty());
    QCOMPARE(summary.title, QStringLiteral("Odds Episode 3 Remaster"));
    QCOMPARE(summary.author, QStringLiteral("Gordon K"));
    QCOMPARE(summary.comments, QStringLiteral("Director notes: final cut with remaster audio"));
    QCOMPARE(summary.copyright, QStringLiteral("Copyright 2026 Odds Productions"));
    QCOMPARE(summary.engineer, QStringLiteral("Sound Engineer: Vegas Pro 21"));
    QCOMPARE(summary.creationDate, QStringLiteral("2026-11-19"));

    // Test readProjectData imports summary into Project
    QString readErr;
    const auto optProj = drift::vegas::readProjectData(w64Data, QString(), &readErr);
    QVERIFY2(optProj.has_value(), qPrintable(readErr));
    const drift::Project &proj = *optProj;
    QCOMPARE(proj.name(), QStringLiteral("Odds Episode 3 Remaster"));
    QCOMPARE(proj.author(), QStringLiteral("Gordon K"));
    QCOMPARE(proj.notes(), proj.description());
    QVERIFY(proj.notes().contains(QStringLiteral("Director notes: final cut with remaster audio")));
    QVERIFY(proj.notes().contains(QStringLiteral("Copyright 2026 Odds Productions")));

    // 2. Standard 32-bit RIFF INFO chunk parsing
    QByteArray riffSubPayload;
    auto appendRiffTag = [&](const char *tag, const QString &text) {
        const QByteArray utf8 = text.toUtf8() + '\0';
        const quint32 len = static_cast<quint32>(utf8.size());
        riffSubPayload.append(tag, 4);
        riffSubPayload.append(reinterpret_cast<const char *>(&len), 4);
        riffSubPayload.append(utf8);
        if (len % 2 != 0)
            riffSubPayload.append('\0'); // 2-byte word align
    };
    appendRiffTag("INAM", QStringLiteral("Classic Vegas Cut"));
    appendRiffTag("IART", QStringLiteral("Antigravity Author"));
    appendRiffTag("ICMT", QStringLiteral("Archived standard RIFF notes"));
    appendRiffTag("ICOP", QStringLiteral("2026 All Rights Reserved"));

    QByteArray riffData;
    riffData.append("RIFF", 4);
    const quint32 riffTotalSize = 4 + 4 + 4 + static_cast<quint32>(riffSubPayload.size());
    riffData.append(reinterpret_cast<const char *>(&riffTotalSize), 4);
    riffData.append("WAVE", 4);

    riffData.append("LIST", 4);
    const quint32 listSubSize = 4 + static_cast<quint32>(riffSubPayload.size());
    riffData.append(reinterpret_cast<const char *>(&listSubSize), 4);
    riffData.append("INFO", 4);
    riffData.append(riffSubPayload);

    const drift::vegas::ProjectSummary riffSummary = drift::vegas::parseProjectSummary(riffData);
    QVERIFY(!riffSummary.isEmpty());
    QCOMPARE(riffSummary.title, QStringLiteral("Classic Vegas Cut"));
    QCOMPARE(riffSummary.author, QStringLiteral("Antigravity Author"));
    QCOMPARE(riffSummary.comments, QStringLiteral("Archived standard RIFF notes"));
    QCOMPARE(riffSummary.copyright, QStringLiteral("2026 All Rights Reserved"));

    // 3. Real sample.veg fixture: empty fields parse cleanly without overriding defaults
    const QString fixtureVeg = QStringLiteral(DRIFT_TEST_DATA_DIR "/sample.veg");
    if (QFile::exists(fixtureVeg)) {
        QFile file(fixtureVeg);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QByteArray sampleData = file.readAll();
        const drift::vegas::ProjectSummary sampleSummary = drift::vegas::parseProjectSummary(sampleData);
        // sample.veg had empty summary fields
        QVERIFY(sampleSummary.isEmpty());

        const auto sampleProj = drift::vegas::readProject(fixtureVeg);
        QVERIFY(sampleProj.has_value());
        QCOMPARE(sampleProj->name(), QStringLiteral("Untitled Project"));
    }
}

void TestInterchangeVegas::readsRegionsEnvelopesBezierMasksAndTrackAudioFx()
{
    auto appendU32 = [](QByteArray &b, quint32 v) {
        char buf[4] = {char(v & 0xFF), char((v >> 8) & 0xFF), char((v >> 16) & 0xFF), char((v >> 24) & 0xFF)};
        b.append(buf, 4);
    };
    auto appendU64 = [&](QByteArray &b, quint64 v) {
        appendU32(b, quint32(v & 0xFFFFFFFFu));
        appendU32(b, quint32(v >> 32));
    };
    auto appendDouble = [&](QByteArray &b, double d) {
        quint64 bits = 0;
        std::memcpy(&bits, &d, sizeof(bits));
        appendU64(b, bits);
    };
    auto appendUtf16 = [](QByteArray &b, const QString &s) {
        for (QChar ch : s) {
            const ushort u = ch.unicode();
            b.append(char(u & 0xFF));
            b.append(char((u >> 8) & 0xFF));
        }
        b.append('\0');
        b.append('\0');
    };
    auto pad8 = [](QByteArray &b) {
        while (b.size() % 8 != 0)
            b.append('\0');
    };
    auto makeChunk = [&](const uint8_t guid[16], const QByteArray &payload) {
        QByteArray out;
        out.append(reinterpret_cast<const char *>(guid), 16);
        appendU64(out, quint64(24 + payload.size()));
        out.append(payload);
        pad8(out);
        return out;
    };
    auto makeList = [&](const uint8_t listGuid[16], const uint8_t typeGuid[16], const QByteArray &body) {
        QByteArray out;
        out.append(reinterpret_cast<const char *>(listGuid), 16);
        appendU64(out, quint64(40 + body.size()));
        out.append(reinterpret_cast<const char *>(typeGuid), 16);
        out.append(body);
        pad8(out);
        return out;
    };

    static const uint8_t kW64Riff[16] = {0x72,0x69,0x66,0x66,0x2e,0x91,0xcf,0x11,0xa5,0xd6,0x28,0xdb,0x04,0xc1,0x00,0x00};
    static const uint8_t kW64List[16] = {0x6c,0x69,0x73,0x74,0x2f,0x91,0xcf,0x11,0xa5,0xd6,0x28,0xdb,0x04,0xc1,0x00,0x00};
    static const uint8_t kTimeline[16] = {0x47,0x07,0x6c,0x4d,0x16,0x23,0xd2,0x11,0x86,0xb0,0x00,0xc0,0x4f,0x8e,0xdb,0x8a};
    static const uint8_t kTrack[16] = {0x48,0x07,0x6c,0x4d,0x16,0x23,0xd2,0x11,0x86,0xb0,0x00,0xc0,0x4f,0x8e,0xdb,0x8a};
    static const uint8_t kEvCont[16] = {0x49,0x07,0x6c,0x4d,0x16,0x23,0xd2,0x11,0x86,0xb0,0x00,0xc0,0x4f,0x8e,0xdb,0x8a};
    static const uint8_t kEvent[16] = {0x4a,0x07,0x6c,0x4d,0x16,0x23,0xd2,0x11,0x86,0xb0,0x00,0xc0,0x4f,0x8e,0xdb,0x8a};
    static const uint8_t kEvTime[16] = {0x6a,0x20,0x8d,0x16,0x21,0x23,0xd2,0x11,0x86,0xb0,0x00,0xc0,0x4f,0x8e,0xdb,0x8a};
    static const uint8_t kPanCrop[16] = {0xff,0xac,0x4b,0x88,0xeb,0x74,0x6c,0x45,0x88,0xf3,0x73,0x9e,0x83,0x90,0xbd,0x07};
    static const uint8_t kEnvList[16] = {0x4b,0x07,0x6c,0x4d,0x16,0x23,0xd2,0x11,0x86,0xb0,0x00,0xc0,0x4f,0x8e,0xdb,0x8a};
    static const uint8_t kEnvChunk[16] = {0x4c,0x07,0x6c,0x4d,0x16,0x23,0xd2,0x11,0x86,0xb0,0x00,0xc0,0x4f,0x8e,0xdb,0x8a};
    static const uint8_t kAudioFxList[16] = {0x28,0x6c,0xd4,0x69,0x0b,0x7f,0xd2,0x11,0x87,0x17,0x00,0xc0,0x4f,0x8e,0xdb,0x8a};
    static const uint8_t kMarkerList[16] = {0xb5,0x00,0x09,0x1f,0x84,0xdc,0x43,0x47,0x81,0xcb,0x08,0x77,0x44,0x40,0x45,0x98};
    static const uint8_t kMarkerGuid[16] = {0x56,0x62,0xf7,0xab,0x2d,0x39,0xd2,0x11,0x86,0xc7,0x00,0xc0,0x4f,0x8e,0xdb,0x8a};
    static const uint8_t kTrackHdr[16] = {0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08,0x09,0x0a,0x0b,0x0c,0x0d,0x0e,0x0f,0x10};
    static const uint8_t kDummyForm[16] = {'V','E','G','S',0,0,0,0,0,0,0,0,0,0,0,0};

    auto makeTrackHdr = [&](quint32 trackTypeCode) {
        QByteArray p(64, '\0');
        p[32] = char(trackTypeCode & 0xFF);
        return makeChunk(kTrackHdr, p);
    };
    auto makeEnv = [&](quint32 envType, const QList<QPair<qint64, double>> &pts) {
        QByteArray p;
        appendU32(p, envType);
        appendU32(p, quint32(pts.size()));
        for (const auto &pt : pts) {
            appendU64(p, quint64(pt.first));
            appendDouble(p, pt.second);
            appendU32(p, 0);
        }
        return makeChunk(kEnvChunk, p);
    };

    QByteArray evTimePayload(32, '\0');
    {
        QByteArray t;
        appendU64(t, 0);          // header padding (0..7)
        appendU64(t, 0);          // header padding (8..15)
        appendU64(t, 0);          // startTicks at offset 16
        appendU64(t, 20000000);   // durTicks = 2.0s at offset 24
        evTimePayload = t;
    }

    QByteArray panPayload;
    {
        QByteArray epcs(52, '\0');
        quint32 w = 1920, h = 1080;
        std::memcpy(epcs.data() + 44, &w, 4);
        std::memcpy(epcs.data() + 48, &h, 4);
        panPayload.append("EPCS", 4);
        appendU32(panPayload, quint32(epcs.size()));
        panPayload.append(epcs);

        QByteArray maskBody;
        appendU32(maskBody, 1);    // flags: invert = true
        appendDouble(maskBody, 12.0);
        appendU32(maskBody, 4);    // 4 vertices
        appendDouble(maskBody, 192.0);  appendDouble(maskBody, 108.0);
        appendDouble(maskBody, 1728.0); appendDouble(maskBody, 108.0);
        appendDouble(maskBody, 1728.0); appendDouble(maskBody, 972.0);
        appendDouble(maskBody, 192.0);  appendDouble(maskBody, 972.0);
        panPayload.append("MASK", 4);
        appendU32(panPayload, quint32(maskBody.size()));
        panPayload.append(maskBody);
    }
    const QByteArray vEvent = makeList(kW64List, kEvent,
                                       makeChunk(kEvTime, evTimePayload) + makeChunk(kPanCrop, panPayload));
    const QByteArray vTrackBody =
        makeTrackHdr(2 /*Video*/)
        + makeList(kW64List, kEvCont, vEvent)
        + makeList(kW64List, kEnvList, makeEnv(2 /*Composite*/, {{0, 1.0}, {10000000, 0.5}}));

    QByteArray audioFxBody;
    appendUtf16(audioFxBody, QStringLiteral("Track EQ"));
    appendUtf16(audioFxBody, QStringLiteral("LowGain"));
    appendDouble(audioFxBody, 4.5);
    appendUtf16(audioFxBody, QStringLiteral("HighGain"));
    appendDouble(audioFxBody, -3.0);
    appendUtf16(audioFxBody, QStringLiteral("Track Compressor"));
    appendUtf16(audioFxBody, QStringLiteral("Threshold"));
    appendDouble(audioFxBody, -24.0);
    appendUtf16(audioFxBody, QStringLiteral("Ratio"));
    appendDouble(audioFxBody, 6.0);

    const QByteArray aTrackBody =
        makeTrackHdr(1 /*Audio*/)
        + makeList(kW64List, kEnvList,
                   makeEnv(0 /*Volume*/, {{0, 0.8}, {20000000, 1.5}})
                   + makeEnv(1 /*Pan*/, {{0, -0.5}, {20000000, 0.5}}))
        + makeList(kW64List, kAudioFxList, audioFxBody);

    QByteArray markerNameBytes;
    appendUtf16(markerNameBytes, QStringLiteral("Chorus"));
    QByteArray markerPayload;
    appendU32(markerPayload, 28);                         // hdrSize (Format B after uint64 chunkSize)
    appendU32(markerPayload, 1);                          // id
    appendU64(markerPayload, 5000000);                    // posTicks = 0.5s
    appendU64(markerPayload, 20000000);                   // lenTicks = 2.0s
    appendU32(markerPayload, quint32(markerNameBytes.size())); // cbName
    markerPayload.append(markerNameBytes);
    const QByteArray paddedMarker = makeChunk(kMarkerGuid, markerPayload);

    const QByteArray timelineBody =
        makeList(kW64List, kTrack, vTrackBody)
        + makeList(kW64List, kTrack, aTrackBody);
    const QByteArray fileBody =
        makeList(kW64List, kTimeline, timelineBody)
        + makeList(kW64List, kMarkerList, paddedMarker);
    const QByteArray vegBytes = makeList(kW64Riff, kDummyForm, fileBody);

    QString err;
    const auto loaded = drift::vegas::readProjectData(vegBytes, QString(), &err);
    QVERIFY2(loaded.has_value(), qPrintable(err));
    QCOMPARE(loaded->tracks().size(), 2);

    const drift::Track &vt = loaded->tracks().at(0);
    QCOMPARE(vt.type, drift::TrackType::Video);
    QVERIFY(!vt.compositeEnvelope.isEmpty());
    QVERIFY(qAbs(vt.compositeEnvelope.evaluateAt(drift::secondsToUs(1.0)) - 0.5) < 1e-4);
    QCOMPARE(vt.clips.size(), 1);
    QCOMPARE(vt.clips.at(0).mask.shape, drift::MaskShape::Freeform);
    QCOMPARE(vt.clips.at(0).mask.points.size(), 4);
    QVERIFY(vt.clips.at(0).mask.invert);
    QVERIFY(qAbs(vt.clips.at(0).mask.feather - 12.0) < 1e-4);
    QVERIFY(qAbs(vt.clips.at(0).mask.points.at(0).x() - 0.1) < 1e-4);

    const drift::Track &at = loaded->tracks().at(1);
    QCOMPARE(at.type, drift::TrackType::Audio);
    QVERIFY(!at.volumeEnvelope.isEmpty());
    QVERIFY(qAbs(at.volumeEnvelope.evaluateAt(drift::secondsToUs(2.0)) - 1.5) < 1e-4);
    QVERIFY(!at.panEnvelope.isEmpty());
    QVERIFY(qAbs(at.panEnvelope.evaluateAt(0) + 0.5) < 1e-4);
    QCOMPARE(at.audioEffects.size(), 2);
    QCOMPARE(at.audioEffects.at(0).catalogId, QStringLiteral("utility.eq"));
    QVERIFY(qAbs(at.audioEffects.at(0).parameters.value(QStringLiteral("low")).toDouble() - 4.5) < 1e-4);
    QCOMPARE(at.audioEffects.at(1).catalogId, QStringLiteral("utility.compressor"));
    QVERIFY(qAbs(at.audioEffects.at(1).parameters.value(QStringLiteral("threshold")).toDouble() + 24.0) < 1e-4);

    QCOMPARE(loaded->regions().size(), 1);
    QCOMPARE(loaded->regions().at(0).label, QStringLiteral("Chorus"));
    QCOMPARE(loaded->regions().at(0).startUs, drift::secondsToUs(0.5));
    QCOMPARE(loaded->regions().at(0).endUs, drift::secondsToUs(2.5));
}

void TestInterchangeVegas::exportsFcpxmlEdlOtioAndVegasTsv()
{
    drift::Project p;
    p.setName(QStringLiteral("Tier4 Export Sequence"));
    p.setFps(24);
    p.setResolution(1920, 1080);
    if (p.tracks().isEmpty())
        p.tracks().append(drift::Track{.id = QStringLiteral("v1"), .type = drift::TrackType::Video});
    p.tracks().append(drift::Track{.id = QStringLiteral("a1"), .type = drift::TrackType::Audio});

    drift::Clip vClip;
    vClip.id = QStringLiteral("v1");
    vClip.name = QStringLiteral("Shot A");
    vClip.path = QStringLiteral("/tmp/shot_a.mov");
    vClip.type = drift::ClipType::Video;
    vClip.timelineStart = 0;
    vClip.timelineDuration = drift::secondsToUs(3.0);
    vClip.srcIn = drift::secondsToUs(1.0);
    vClip.srcOut = drift::secondsToUs(4.0);
    p.tracks()[0].clips.append(vClip);

    drift::Clip aClip;
    aClip.id = QStringLiteral("a1");
    aClip.name = QStringLiteral("Dialogue");
    aClip.path = QStringLiteral("/tmp/dialogue.wav");
    aClip.type = drift::ClipType::Audio;
    aClip.timelineStart = drift::secondsToUs(0.5);
    aClip.timelineDuration = drift::secondsToUs(2.0);
    aClip.srcIn = 0;
    aClip.srcOut = drift::secondsToUs(2.0);
    p.tracks()[1].clips.append(aClip);

    drift::Bookmark bm;
    bm.timeUs = drift::secondsToUs(1.0);
    bm.label = QStringLiteral("Beat 1");
    p.bookmarks().append(bm);

    drift::Region rg;
    rg.id = QStringLiteral("r1");
    rg.startUs = drift::secondsToUs(0.5);
    rg.endUs = drift::secondsToUs(2.5);
    rg.label = QStringLiteral("Verse");
    p.regions().append(rg);

    QString err;
    // 1. FCPXML round-trip via ResolveReader.
    const QByteArray fcpxml = drift::interchange::exportToFcpxml(p, &err);
    QVERIFY2(!fcpxml.isEmpty(), qPrintable(err));
    QVERIFY(fcpxml.contains("<marker "));
    QVERIFY(fcpxml.contains("<chapter-marker "));
    const auto parsedFcpxml = drift::resolve::readFcpxmlData(fcpxml, QString(), &err);
    QVERIFY2(parsedFcpxml.has_value(), qPrintable(err));
    QVERIFY(!parsedFcpxml->tracks().isEmpty());
    QVERIFY(!parsedFcpxml->tracks().at(0).clips.isEmpty());

    // 2. CMX 3600 EDL round-trip via EdlReader.
    const QByteArray edl = drift::interchange::exportToEdl(p, &err);
    QVERIFY2(!edl.isEmpty(), qPrintable(err));
    const auto parsedEdl = drift::edl::readProjectData(edl, QString(), &err);
    QVERIFY2(parsedEdl.has_value(), qPrintable(err));
    QVERIFY(!parsedEdl->tracks().isEmpty());
    QVERIFY(!parsedEdl->tracks().at(0).clips.isEmpty());

    // 3. OpenTimelineIO JSON round-trip via OtioReader.
    const QByteArray otio = drift::interchange::exportToOtio(p, &err);
    QVERIFY2(!otio.isEmpty(), qPrintable(err));
    const auto parsedOtio = drift::otio::readProjectData(otio, QString(), &err);
    QVERIFY2(parsedOtio.has_value(), qPrintable(err));
    QCOMPARE(parsedOtio->tracks().size(), 2);
    QCOMPARE(parsedOtio->tracks().at(0).clips.size(), 1);
    QCOMPARE(parsedOtio->tracks().at(1).clips.size(), 1);

    // 4. VEGAS Markers/Regions TSV.
    const QByteArray tsv = drift::interchange::exportToVegasMarkersTsv(p, &err);
    QVERIFY2(!tsv.isEmpty(), qPrintable(err));
    const QString tsvStr = QString::fromUtf8(tsv);
    QVERIFY(tsvStr.contains(QStringLiteral("Beat 1")));
    QVERIFY(tsvStr.contains(QStringLiteral("Verse")));

    // 5. File writer helper.
    QTemporaryDir tmpDir;
    QVERIFY(tmpDir.isValid());
    const QString outOtio = tmpDir.filePath(QStringLiteral("timeline.otio"));
    QVERIFY(drift::interchange::exportProjectInterchange(p, outOtio, QString(), &err));
    QVERIFY(QFileInfo::exists(outOtio));
}

void TestInterchangeVegas::readsDeeperVegasEventTagsAndCompositeBlend()
{
    auto appendU32 = [](QByteArray &b, quint32 v) {
        char buf[4] = {char(v & 0xFF), char((v >> 8) & 0xFF), char((v >> 16) & 0xFF), char((v >> 24) & 0xFF)};
        b.append(buf, 4);
    };
    auto appendU64 = [](QByteArray &b, quint64 v) {
        for (int i = 0; i < 8; ++i)
            b.append(char((v >> (i * 8)) & 0xFF));
    };
    auto appendUtf16 = [&](QByteArray &b, const QString &s) {
        for (QChar ch : s) {
            const quint16 u = ch.unicode();
            b.append(char(u & 0xFF));
            b.append(char((u >> 8) & 0xFF));
        }
        b.append('\0');
        b.append('\0');
    };
    auto makeChunk = [&](const uint8_t guid[16], const QByteArray &payload) -> QByteArray {
        QByteArray out;
        out.append(reinterpret_cast<const char *>(guid), 16);
        appendU64(out, 24 + quint64(payload.size()));
        out.append(payload);
        while (out.size() % 8 != 0)
            out.append('\0');
        return out;
    };
    auto makeList = [&](const uint8_t outer[16], const uint8_t type[16], const QByteArray &body) -> QByteArray {
        QByteArray out;
        out.append(reinterpret_cast<const char *>(outer), 16);
        appendU64(out, 40 + quint64(body.size()));
        out.append(reinterpret_cast<const char *>(type), 16);
        out.append(body);
        while (out.size() % 8 != 0)
            out.append('\0');
        return out;
    };

    static const uint8_t kW64Riff[16] = {0x72,0x69,0x66,0x66,0x2e,0x91,0xcf,0x11,0xa5,0xd6,0x28,0xdb,0x04,0xc1,0x00,0x00};
    static const uint8_t kW64List[16] = {0x6c,0x69,0x73,0x74,0x2f,0x91,0xcf,0x11,0xa5,0xd6,0x28,0xdb,0x04,0xc1,0x00,0x00};
    static const uint8_t kTimeline[16] = {0x47,0x07,0x6c,0x4d,0x16,0x23,0xd2,0x11,0x86,0xb0,0x00,0xc0,0x4f,0x8e,0xdb,0x8a};
    static const uint8_t kTrack[16] = {0x48,0x07,0x6c,0x4d,0x16,0x23,0xd2,0x11,0x86,0xb0,0x00,0xc0,0x4f,0x8e,0xdb,0x8a};
    static const uint8_t kEvCont[16] = {0x49,0x07,0x6c,0x4d,0x16,0x23,0xd2,0x11,0x86,0xb0,0x00,0xc0,0x4f,0x8e,0xdb,0x8a};
    static const uint8_t kEvent[16] = {0x4a,0x07,0x6c,0x4d,0x16,0x23,0xd2,0x11,0x86,0xb0,0x00,0xc0,0x4f,0x8e,0xdb,0x8a};
    static const uint8_t kEvTime[16] = {0x6a,0x20,0x8d,0x16,0x21,0x23,0xd2,0x11,0x86,0xb0,0x00,0xc0,0x4f,0x8e,0xdb,0x8a};
    static const uint8_t kTrkHdr[16] = {0xe5,0x2a,0x6c,0x5a,0x11,0x23,0xd2,0x11,0x86,0xb0,0x00,0xc0,0x4f,0x8e,0xdb,0x8a};
    static const uint8_t kDummyForm[16] = {0x53,0x46,0x43,0x46,0,0,0,0,0,0,0,0,0,0,0,0};

    QByteArray trkHdr(64, '\0');
    trkHdr[32] = char(2); // Video (offset 32 inside track header payload)
    appendUtf16(trkHdr, QStringLiteral("CompositeMultiply"));

    QByteArray evTimePayload(72, '\0');
    auto putU64At = [&](int off, quint64 val) {
        for (int i = 0; i < 8; ++i)
            evTimePayload[off + i] = char((val >> (i * 8)) & 0xFF);
    };
    putU64At(16, 0);
    putU64At(24, 30000000); // 3.0s
    putU64At(48, 2);        // flags = muted (bit 1) + locked etc., and append UTF-16 tokens below
    appendUtf16(evTimePayload, QStringLiteral("EventMute EventLocked"));

    const QByteArray evChunk = makeList(kW64List, kEvent, makeChunk(kEvTime, evTimePayload));
    const QByteArray trackBody = makeChunk(kTrkHdr, trkHdr) + makeList(kW64List, kEvCont, evChunk);
    const QByteArray timelineBody = makeList(kW64List, kTrack, trackBody);
    const QByteArray vegBytes = makeList(kW64Riff, kDummyForm, makeList(kW64List, kTimeline, timelineBody));

    QString err;
    const auto loaded = drift::vegas::readProjectData(vegBytes, QString(), &err);
    QVERIFY2(loaded.has_value(), qPrintable(err));
    QCOMPARE(loaded->tracks().size(), 1);
    QCOMPARE(loaded->tracks().at(0).compositeBlendMode, drift::BlendMode::Multiply);
    QCOMPARE(loaded->tracks().at(0).clips.size(), 1);
    QVERIFY(!loaded->tracks().at(0).clips.at(0).enabled);
    QVERIFY(loaded->tracks().at(0).clips.at(0).locked);
}

void TestInterchangeVegas::readsImageMxfTrackFlagsAndLevelsToColorGrade()
{
    auto appendU32 = [](QByteArray &out, quint32 v) {
        for (int i = 0; i < 4; ++i)
            out.append(char((v >> (i * 8)) & 0xFF));
    };
    auto appendU64 = [](QByteArray &out, quint64 v) {
        for (int i = 0; i < 8; ++i)
            out.append(char((v >> (i * 8)) & 0xFF));
    };
    auto appendUtf16 = [](QByteArray &out, const QString &s) {
        const QByteArray u16(reinterpret_cast<const char *>(s.utf16()), s.size() * 2);
        out.append(u16);
        out.append('\0');
        out.append('\0');
    };
    auto makeChunk = [&](const uint8_t guid[16], const QByteArray &payload) -> QByteArray {
        QByteArray out;
        out.append(reinterpret_cast<const char *>(guid), 16);
        appendU64(out, 24 + quint64(payload.size()));
        out.append(payload);
        while (out.size() % 8 != 0)
            out.append('\0');
        return out;
    };
    auto makeList = [&](const uint8_t outer[16], const uint8_t type[16], const QByteArray &body) -> QByteArray {
        QByteArray out;
        out.append(reinterpret_cast<const char *>(outer), 16);
        appendU64(out, 40 + quint64(body.size()));
        out.append(reinterpret_cast<const char *>(type), 16);
        out.append(body);
        while (out.size() % 8 != 0)
            out.append('\0');
        return out;
    };

    static const uint8_t kW64Riff[16] = {0x72,0x69,0x66,0x66,0x2e,0x91,0xcf,0x11,0xa5,0xd6,0x28,0xdb,0x04,0xc1,0x00,0x00};
    static const uint8_t kW64List[16] = {0x6c,0x69,0x73,0x74,0x2f,0x91,0xcf,0x11,0xa5,0xd6,0x28,0xdb,0x04,0xc1,0x00,0x00};
    static const uint8_t kMediaPool[16] = {0x5b,0x2d,0x8f,0xb2,0x0f,0x23,0xd2,0x11,0x86,0xaf,0x00,0xc0,0x4f,0x8e,0xdb,0x8a};
    static const uint8_t kMediaEntry[16] = {0x5d,0x2d,0x8f,0xb2,0x0f,0x23,0xd2,0x11,0x86,0xaf,0x00,0xc0,0x4f,0x8e,0xdb,0x8a};
    static const uint8_t kTimeline[16] = {0x47,0x07,0x6c,0x4d,0x16,0x23,0xd2,0x11,0x86,0xb0,0x00,0xc0,0x4f,0x8e,0xdb,0x8a};
    static const uint8_t kTrack[16] = {0x48,0x07,0x6c,0x4d,0x16,0x23,0xd2,0x11,0x86,0xb0,0x00,0xc0,0x4f,0x8e,0xdb,0x8a};
    static const uint8_t kEvCont[16] = {0x49,0x07,0x6c,0x4d,0x16,0x23,0xd2,0x11,0x86,0xb0,0x00,0xc0,0x4f,0x8e,0xdb,0x8a};
    static const uint8_t kEvent[16] = {0x4a,0x07,0x6c,0x4d,0x16,0x23,0xd2,0x11,0x86,0xb0,0x00,0xc0,0x4f,0x8e,0xdb,0x8a};
    static const uint8_t kEvTime[16] = {0x6a,0x20,0x8d,0x16,0x21,0x23,0xd2,0x11,0x86,0xb0,0x00,0xc0,0x4f,0x8e,0xdb,0x8a};
    static const uint8_t kEvTake[16] = {0x6d,0x20,0x8d,0x16,0x21,0x23,0xd2,0x11,0x86,0xb0,0x00,0xc0,0x4f,0x8e,0xdb,0x8a};
    static const uint8_t kTrkHdr[16] = {0xe5,0x2a,0x6c,0x5a,0x11,0x23,0xd2,0x11,0x86,0xb0,0x00,0xc0,0x4f,0x8e,0xdb,0x8a};
    static const uint8_t kDummyForm[16] = {0x53,0x46,0x43,0x46,0,0,0,0,0,0,0,0,0,0,0,0};

    auto makeMediaPayload = [&](quint32 mid, const QString &path, bool withLevels) -> QByteArray {
        QByteArray p(64, '\0');
        for (int i = 0; i < 4; ++i)
            p[60 + i] = char((mid >> (i * 8)) & 0xFF);
        appendUtf16(p, path);
        if (withLevels) {
            appendUtf16(p, QStringLiteral("{Svfx:com.sonycreativesoftware:levels}"));
            appendUtf16(p, QStringLiteral("InputStart"));
            QByteArray dbl(8, '\0');
            auto putD = [&](double v) {
                std::memcpy(dbl.data(), &v, 8);
                p.append(dbl);
            };
            putD(0.1);
            appendUtf16(p, QStringLiteral("InputEnd"));
            putD(0.9);
            appendUtf16(p, QStringLiteral("Gamma"));
            putD(1.5);
            appendUtf16(p, QStringLiteral("OutputStart"));
            putD(0.05);
            appendUtf16(p, QStringLiteral("OutputEnd"));
            putD(0.95);
        }
        return p;
    };

    const QByteArray mpBody = makeChunk(kMediaEntry, makeMediaPayload(1, QStringLiteral("C:\\proj\\overlay.png"), true))
                            + makeChunk(kMediaEntry, makeMediaPayload(2, QStringLiteral("C:\\proj\\cam.mxf"), false));

    auto makeTrack = [&](quint32 flags, quint32 mid) -> QByteArray {
        QByteArray trkHdr(64, '\0');
        for (int i = 0; i < 4; ++i)
            trkHdr[4 + i] = char((flags >> (i * 8)) & 0xFF);
        trkHdr[32] = char(2); // Video

        QByteArray evTime(72, '\0');
        quint64 dur = 20000000;
        std::memcpy(evTime.data() + 24, &dur, 8);

        QByteArray evTake(40, '\0');
        std::memcpy(evTake.data() + 36, &mid, 4);

        const QByteArray evChunk = makeList(kW64List, kEvent,
                                            makeChunk(kEvTime, evTime) + makeChunk(kEvTake, evTake));
        return makeList(kW64List, kTrack,
                        makeChunk(kTrkHdr, trkHdr) + makeList(kW64List, kEvCont, evChunk));
    };

    const QByteArray vegBytes = makeList(
        kW64Riff, kDummyForm,
        makeList(kW64List, kMediaPool, mpBody)
            + makeList(kW64List, kTimeline, makeTrack(0x00000001u, 1) + makeTrack(0x00020002u, 2)));

    QString err;
    const auto loaded = drift::vegas::readProjectData(vegBytes, QString(), &err);
    QVERIFY2(loaded.has_value(), qPrintable(err));
    QCOMPARE(loaded->tracks().size(), 2);
    QVERIFY(loaded->tracks().at(0).solo);
    QVERIFY(!loaded->tracks().at(0).muted);
    QVERIFY(!loaded->tracks().at(0).hidden);
    QVERIFY(!loaded->tracks().at(1).solo);
    QVERIFY(loaded->tracks().at(1).muted);
    QVERIFY(loaded->tracks().at(1).hidden);

    QCOMPARE(loaded->tracks().at(0).clips.at(0).type, drift::ClipType::Image);
    QCOMPARE(loaded->tracks().at(1).clips.at(0).type, drift::ClipType::Video);

    bool foundPng = false, foundMxf = false, foundColorGradeCurve = false;
    for (const auto &asset : loaded->assets()) {
        if (asset.path.endsWith(QLatin1String("overlay.png"))) {
            foundPng = true;
            QCOMPARE(asset.kind, drift::MediaKind::Image);
            QVERIFY(asset.editFriendly);
            for (const auto &fx : asset.effects) {
                if (fx.catalogId == QStringLiteral("color.grade")
                    && fx.parameters.contains(QStringLiteral("curveMaster"))) {
                    foundColorGradeCurve = true;
                    QVERIFY(fx.parameters.value(QStringLiteral("curveMaster")).toString().contains(QStringLiteral("\"ease\":\"linear\"")));
                }
            }
        } else if (asset.path.endsWith(QLatin1String("cam.mxf"))) {
            foundMxf = true;
            QCOMPARE(asset.kind, drift::MediaKind::Video);
        }
    }
    QVERIFY(foundPng);
    QVERIFY(foundMxf);
    QVERIFY(foundColorGradeCurve);

    const QString realVeg = QStringLiteral("/mnt/Jellyfish/Videos/Odds/Episode 5 & 6/Bryan Enters Lair (Scene 1 & 8).veg");
    if (QFileInfo::exists(realVeg)) {
        QString realErr;
        const auto realProj = drift::vegas::readProject(realVeg, &realErr);
        QVERIFY2(realProj.has_value(), qPrintable(realErr));
        QVERIFY(realProj->tracks().size() >= 4);
        QVERIFY(!realProj->tracks().at(0).hidden);
        QVERIFY(!realProj->tracks().at(0).solo);
        QVERIFY(realProj->tracks().at(1).hidden);
        QVERIFY(realProj->tracks().at(2).hidden);
        QVERIFY(realProj->tracks().at(3).hidden);
        QCOMPARE(realProj->tracks().at(2).clips.at(0).type, drift::ClipType::Image);
        QCOMPARE(realProj->tracks().at(3).clips.at(0).type, drift::ClipType::Video);
    }
}

void TestInterchangeVegas::readsPlaybackRateAndActiveTakeSelection()
{
    auto guid = [](std::initializer_list<uint8_t> bytes) {
        QByteArray ba;
        for (uint8_t b : bytes)
            ba.append(char(b));
        return ba;
    };
    auto makeChunk = [](const QByteArray &id, const QByteArray &payload) {
        QByteArray out = id;
        const quint64 total = 24 + static_cast<quint64>(payload.size());
        for (int i = 0; i < 8; ++i)
            out.append(char((total >> (i * 8)) & 0xFF));
        out.append(payload);
        while (out.size() % 8 != 0)
            out.append('\0');
        return out;
    };
    auto makeList = [](const QByteArray &listGuid, const QByteArray &typeGuid, const QByteArray &children) {
        QByteArray out = listGuid;
        const quint64 total = 40 + static_cast<quint64>(children.size());
        for (int i = 0; i < 8; ++i)
            out.append(char((total >> (i * 8)) & 0xFF));
        out.append(typeGuid);
        out.append(children);
        while (out.size() % 8 != 0)
            out.append('\0');
        return out;
    };

    const QByteArray kW64Riff = guid({0x72,0x69,0x66,0x66,0x2e,0x91,0xcf,0x11,0xa5,0xd6,0x28,0xdb,0x04,0xc1,0x00,0x00});
    const QByteArray kW64List = guid({0x6c,0x69,0x73,0x74,0x2f,0x91,0xcf,0x11,0xa5,0xd6,0x28,0xdb,0x04,0xc1,0x00,0x00});
    const QByteArray kMediaPool = guid({0x5b,0x2d,0x8f,0xb2,0x0f,0x23,0xd2,0x11,0x86,0xaf,0x00,0xc0,0x4f,0x8e,0xdb,0x8a});
    const QByteArray kTimeline = guid({0x47,0x07,0x6c,0x4d,0x16,0x23,0xd2,0x11,0x86,0xb0,0x00,0xc0,0x4f,0x8e,0xdb,0x8a});
    const QByteArray kTrack = guid({0x48,0x07,0x6c,0x4d,0x16,0x23,0xd2,0x11,0x86,0xb0,0x00,0xc0,0x4f,0x8e,0xdb,0x8a});
    const QByteArray kEvCont = guid({0x49,0x07,0x6c,0x4d,0x16,0x23,0xd2,0x11,0x86,0xb0,0x00,0xc0,0x4f,0x8e,0xdb,0x8a});
    const QByteArray kEvent = guid({0x4a,0x07,0x6c,0x4d,0x16,0x23,0xd2,0x11,0x86,0xb0,0x00,0xc0,0x4f,0x8e,0xdb,0x8a});
    const QByteArray kEvTime = guid({0x6a,0x20,0x8d,0x16,0x21,0x23,0xd2,0x11,0x86,0xb0,0x00,0xc0,0x4f,0x8e,0xdb,0x8a});
    const QByteArray kEvTake = guid({0x6d,0x20,0x8d,0x16,0x21,0x23,0xd2,0x11,0x86,0xb0,0x00,0xc0,0x4f,0x8e,0xdb,0x8a});
    const QByteArray kMediaEntry = guid({0x5d,0x2d,0x8f,0xb2,0x0f,0x23,0xd2,0x11,0x86,0xaf,0x00,0xc0,0x4f,0x8e,0xdb,0x8a});
    const QByteArray kTrkHdr = guid({0xe5,0x2a,0x6c,0x5a,0x11,0x23,0xd2,0x11,0x86,0xb0,0x00,0xc0,0x4f,0x8e,0xdb,0x8a});
    const QByteArray kDummyForm = guid({0x53,0x46,0x43,0x46,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00});

    auto makeMediaPayload = [](quint32 id, const QString &path) {
        QByteArray p(64, '\0');
        for (int i = 0; i < 4; ++i)
            p[60 + i] = char((id >> (i * 8)) & 0xFF);
        const ushort *u16 = path.utf16();
        p.append(reinterpret_cast<const char *>(u16), (path.size() + 1) * 2);
        return p;
    };

    const QByteArray mpBody = makeChunk(kMediaEntry, makeMediaPayload(1, QStringLiteral("C:\\proj\\takeA.mov")))
                            + makeChunk(kMediaEntry, makeMediaPayload(2, QStringLiteral("C:\\proj\\takeB.mov")));

    // Track with Event 1 (playbackRate = 2.5) and Event 2 (2 takes, take 1 active via takeFlags 0x2c)
    QByteArray trkHdr(64, '\0');
    trkHdr[32] = char(2); // Video

    QByteArray ev1Time(72, '\0');
    quint64 dur1 = 20000000; // 2.0s timeline
    double rate1 = 2.5;
    std::memcpy(ev1Time.data() + 24, &dur1, 8);
    std::memcpy(ev1Time.data() + 40, &rate1, 8);
    QByteArray ev1Take(40, '\0');
    quint32 mid1 = 1;
    std::memcpy(ev1Take.data() + 36, &mid1, 4);
    const QByteArray ev1 = makeList(kW64List, kEvent, makeChunk(kEvTime, ev1Time) + makeChunk(kEvTake, ev1Take));

    QByteArray ev2Time(72, '\0');
    quint64 start2 = 30000000;
    quint64 dur2 = 40000000; // 4.0s timeline
    quint32 activeIdx2 = 1;
    double rate2 = 0.5;
    std::memcpy(ev2Time.data() + 16, &start2, 8);
    std::memcpy(ev2Time.data() + 24, &dur2, 8);
    std::memcpy(ev2Time.data() + 32, &activeIdx2, 4);
    std::memcpy(ev2Time.data() + 40, &rate2, 8);

    QByteArray ev2Take0(40, '\0');
    quint64 srcIn0 = 10000000; // 1.0s
    quint32 flags0 = 12;       // inactive
    quint32 mid0 = 1;
    std::memcpy(ev2Take0.data() + 16, &srcIn0, 8);
    std::memcpy(ev2Take0.data() + 32, &flags0, 4);
    std::memcpy(ev2Take0.data() + 36, &mid0, 4);

    QByteArray ev2Take1(40, '\0');
    quint64 srcIn1 = 50000000; // 5.0s
    quint32 flags1 = 44;       // 0x2c -> active (bit 0x20)
    quint32 mid2 = 2;
    std::memcpy(ev2Take1.data() + 16, &srcIn1, 8);
    std::memcpy(ev2Take1.data() + 32, &flags1, 4);
    std::memcpy(ev2Take1.data() + 36, &mid2, 4);

    const QByteArray ev2 = makeList(kW64List, kEvent,
                                    makeChunk(kEvTime, ev2Time) + makeChunk(kEvTake, ev2Take0) + makeChunk(kEvTake, ev2Take1));

    const QByteArray track = makeList(kW64List, kTrack,
                                      makeChunk(kTrkHdr, trkHdr) + makeList(kW64List, kEvCont, ev1 + ev2));
    const QByteArray vegBytes = makeList(kW64Riff, kDummyForm,
                                         makeList(kW64List, kMediaPool, mpBody) + makeList(kW64List, kTimeline, track));

    QString err;
    const auto loaded = drift::vegas::readProjectData(vegBytes, QString(), &err);
    QVERIFY2(loaded.has_value(), qPrintable(err));
    QCOMPARE(loaded->tracks().size(), 1);
    QCOMPARE(loaded->fpsNum(), 24000);
    QCOMPARE(loaded->fpsDen(), 1001);
    QCOMPARE(loaded->tracks().at(0).clips.size(), 2);

    const auto &c1 = loaded->tracks().at(0).clips.at(0);
    QCOMPARE(c1.timelineDuration, drift::TimeUs(2'000'000));
    QVERIFY(qAbs(c1.speed - 2.5) < 1e-6);
    QCOMPARE(c1.srcOut - c1.srcIn, drift::TimeUs(5'000'000));

    const auto &c2 = loaded->tracks().at(0).clips.at(1);
    QCOMPARE(c2.activeTakeIndex, 1);
    QCOMPARE(c2.srcIn, drift::TimeUs(5'000'000));
    QCOMPARE(c2.srcOut - c2.srcIn, drift::TimeUs(2'000'000));
    QVERIFY(qAbs(c2.speed - 0.5) < 1e-6);
    QVERIFY(c2.path.endsWith(QLatin1String("takeB.mov")));
    QCOMPARE(c2.takes.size(), 2);
    QCOMPARE(c2.takes.at(0).srcIn, drift::TimeUs(1'000'000));
    QCOMPARE(c2.takes.at(1).srcIn, drift::TimeUs(5'000'000));

    const QString e3s8Veg = findExtendedVeg(QStringLiteral("Episode 3/Remaster/Scene 8,9,10/e3s8-9-10.veg"));
    if (!e3s8Veg.isEmpty()) {
        QString rErr;
        const auto p = drift::vegas::readProject(e3s8Veg, &rErr);
        QVERIFY2(p.has_value(), qPrintable(rErr));
        bool found25x = false;
        for (const auto &t : p->tracks()) {
            for (const auto &c : t.clips) {
                if (qAbs(c.speed - 2.5) < 1e-4)
                    found25x = true;
            }
        }
        QVERIFY(found25x);
    }

    const QString e3s5Veg = findExtendedVeg(QStringLiteral("Episode 3/Remaster/Scene 5/e3s5.veg"));
    if (!e3s5Veg.isEmpty()) {
        QString rErr;
        const auto p = drift::vegas::readProject(e3s5Veg, &rErr);
        QVERIFY2(p.has_value(), qPrintable(rErr));
        bool foundActiveTake1 = false;
        for (const auto &t : p->tracks()) {
            for (const auto &c : t.clips) {
                if (c.takes.size() > 1 && c.activeTakeIndex == 1)
                    foundActiveTake1 = true;
            }
        }
        QVERIFY(foundActiveTake1);
    }
}

void TestInterchangeVegas::parsesDirectShowTrackAndEventAudioFxGraphs()
{
    auto appendU32 = [](QByteArray &out, quint32 v) {
        char b[4] = {char(v & 0xff), char((v >> 8) & 0xff), char((v >> 16) & 0xff), char((v >> 24) & 0xff)};
        out.append(b, 4);
    };
    auto appendI32 = [&](QByteArray &out, qint32 v) { appendU32(out, static_cast<quint32>(v)); };
    auto appendF32 = [&](QByteArray &out, float f) {
        quint32 u = 0;
        std::memcpy(&u, &f, 4);
        appendU32(out, u);
    };
    auto appendU64 = [](QByteArray &out, quint64 v) {
        char b[8];
        for (int i = 0; i < 8; ++i)
            b[i] = char((v >> (i * 8)) & 0xff);
        out.append(b, 8);
    };
    auto wrapW64Chunk = [&](const QByteArray &guid, const QByteArray &payload) {
        QByteArray out = guid;
        appendU64(out, static_cast<quint64>(24 + payload.size()));
        out.append(payload);
        while (out.size() % 8 != 0)
            out.append('\0');
        return out;
    };
    auto wrapW64List = [&](const QByteArray &typeGuid, const QByteArray &body) {
        static const QByteArray kList = QByteArray::fromHex("6c6973742f91cf11a5d628db04c10000");
        QByteArray out = kList;
        appendU64(out, static_cast<quint64>(40 + body.size()));
        out.append(typeGuid);
        out.append(body);
        while (out.size() % 8 != 0)
            out.append('\0');
        return out;
    };

    struct DsNode {
        QString name;
        QByteArray params;
    };
    auto buildDsGraphList = [&](const QByteArray &listGuid, const QList<DsNode> &nodes) {
        QByteArray graphBody;
        // "Empty Graph\0" in UTF-16LE (24 bytes) + 30 bytes WAVEFORMATEX/padding
        const QString eg = QStringLiteral("Empty Graph");
        graphBody.append(reinterpret_cast<const char *>(eg.utf16()), (eg.size() + 1) * 2);
        graphBody.append(QByteArray(30, '\0'));
        appendU32(graphBody, static_cast<quint32>(nodes.size()));
        appendU32(graphBody, 0x20);
        for (int i = 0; i < nodes.size(); ++i) {
            const DsNode &n = nodes.at(i);
            const QByteArray nameUtf16(reinterpret_cast<const char *>(n.name.utf16()), (n.name.size() + 1) * 2);
            graphBody.append(QByteArray(16, '\0')); // clsid1
            appendU32(graphBody, 0);                 // flags
            appendU32(graphBody, static_cast<quint32>(nameUtf16.size()));
            graphBody.append(nameUtf16);
            graphBody.append(QByteArray(16, '\0')); // clsid2
            const quint32 stateSize = static_cast<quint32>(16 + n.params.size());
            appendU32(graphBody, stateSize);
            appendU32(graphBody, stateSize);
            graphBody.append(QByteArray(16, '\0')); // state format GUID
            graphBody.append(n.params);
            if (i + 1 < nodes.size())
                appendU32(graphBody, 0xffffffffu);
        }
        const QByteArray innerChunk = wrapW64Chunk(listGuid, graphBody);
        return wrapW64List(listGuid, innerChunk);
    };

    const QByteArray kRiff = QByteArray::fromHex("726966662e91cf11a5d628db04c10000");
    const QByteArray kMediaPool = QByteArray::fromHex("5b2d8fb20f23d21186af00c04f8edb8a");
    const QByteArray kTimeline = QByteArray::fromHex("47076c4d1623d21186b000c04f8edb8a");
    const QByteArray kTrack = QByteArray::fromHex("48076c4d1623d21186b000c04f8edb8a");
    const QByteArray kEventContainer = QByteArray::fromHex("49076c4d1623d21186b000c04f8edb8a");
    const QByteArray kEvent = QByteArray::fromHex("4a076c4d1623d21186b000c04f8edb8a");
    const QByteArray kEventTime = QByteArray::fromHex("6a208d162123d21186b000c04f8edb8a");
    const QByteArray kEventTake = QByteArray::fromHex("6d208d162123d21186b000c04f8edb8a");
    const QByteArray kTrackProps28 = QByteArray::fromHex("286cd4690b7fd211871700c04f8edb8a");
    const QByteArray kF498 = QByteArray::fromHex("f49892f79556174b9ddb19a0912d39f3");
    const QByteArray kCe95 = QByteArray::fromHex("ce956ab64659ab48a4f4f88c95acdc1d");

    // 1. Media pool with 1 audio wav
    QByteArray mpPayload(96, '\0');
    const quint32 mid = 1;
    std::memcpy(mpPayload.data() + 60, &mid, 4);
    const QString wavPath = QStringLiteral("C:\\audio\\vox.wav");
    mpPayload.append(reinterpret_cast<const char *>(wavPath.utf16()), (wavPath.size() + 1) * 2);
    const QByteArray mpChunk = wrapW64Chunk(QByteArray::fromHex("5d2d8fb20f23d21186af00c04f8edb8a"), mpPayload);
    const QByteArray mpList = wrapW64List(kMediaPool, mpChunk);

    // Track 1: untouched default Sony Track Noise Gate (-800) + EQ (0,0,0,0) + Comp (ratio=1.0f, thresh=0.0f)
    QByteArray gateDefault;
    appendI32(gateDefault, 3);
    appendI32(gateDefault, 100);
    appendI32(gateDefault, -800);
    QByteArray eqDefault;
    appendI32(eqDefault, 0);
    appendI32(eqDefault, 0);
    appendI32(eqDefault, 0);
    appendI32(eqDefault, 0);
    QByteArray compDefault;
    appendF32(compDefault, 0.0f);
    appendF32(compDefault, 0.0f);
    appendF32(compDefault, 0.0f);
    appendF32(compDefault, 1.0f);
    appendF32(compDefault, 15.0f);
    appendF32(compDefault, 250.0f);

    QByteArray t1Props(40, '\0');
    const quint32 audioCode = 1;
    std::memcpy(t1Props.data() + 32, &audioCode, 4);
    const QByteArray t1List = wrapW64List(
        kTrack,
        wrapW64Chunk(kTrackProps28, t1Props)
            + buildDsGraphList(kF498, {
                                          {QStringLiteral("Sony Track Noise Gate"), gateDefault},
                                          {QStringLiteral("Sony Track EQ"), eqDefault},
                                          {QStringLiteral("Sony Track Compressor"), compDefault},
                                      }));

    // Track 2: modified EQ (-150, -63, -58, -150) + Reverb + 1 audio event with ce95 (Sony Volume 2.0f + EQ + ExpressFX Reverb)
    QByteArray eqModified;
    appendI32(eqModified, -150);
    appendI32(eqModified, -63);
    appendI32(eqModified, -58);
    appendI32(eqModified, -150);
    QByteArray revParams;
    appendI32(revParams, 0);
    appendI32(revParams, -418);
    appendI32(revParams, -300);

    QByteArray evTime(80, '\0');
    const quint64 startTicks = 0;
    const quint64 durTicks = 20'000'000ULL;
    std::memcpy(evTime.data() + 16, &startTicks, 8);
    std::memcpy(evTime.data() + 24, &durTicks, 8);

    QByteArray evTake(44, '\0');
    std::memcpy(evTake.data() + 36, &mid, 4);

    QByteArray volParams;
    appendF32(volParams, 2.0f);
    QByteArray evEqParams;
    appendI32(evEqParams, -150);
    appendI32(evEqParams, -49);
    appendI32(evEqParams, 52);
    appendI32(evEqParams, 72);

    const QByteArray evList = wrapW64List(
        kEvent,
        wrapW64Chunk(kEventTime, evTime)
            + wrapW64Chunk(kEventTake, evTake)
            + buildDsGraphList(kCe95, {
                                          {QStringLiteral("Sony Volume"), volParams},
                                          {QStringLiteral("Sony Track EQ"), evEqParams},
                                          {QStringLiteral("ExpressFX Reverb"), revParams},
                                      }));
    const QByteArray t2List = wrapW64List(
        kTrack,
        wrapW64Chunk(kTrackProps28, t1Props)
            + wrapW64List(kEventContainer, evList)
            + buildDsGraphList(kF498, {
                                          {QStringLiteral("Sony Track EQ"), eqModified},
                                          {QStringLiteral("Reverb"), revParams},
                                      }));

    const QByteArray tlList = wrapW64List(kTimeline, t1List + t2List);
    const QByteArray body = mpList + tlList;
    QByteArray veg = kRiff;
    appendU64(veg, static_cast<quint64>(40 + body.size()));
    veg.append(QByteArray(16, '\0'));
    veg.append(body);

    QString err;
    const auto loaded = drift::vegas::readProjectData(veg, QString(), &err);
    QVERIFY2(loaded.has_value(), qPrintable(err));
    QCOMPARE(loaded->tracks().size(), 2);

    // Track 1: untouched defaults should NOT pollute audioEffects.
    QVERIFY(loaded->tracks().at(0).audioEffects.isEmpty());

    // Track 2: EQ + Reverb parsed cleanly.
    const auto &t2Fx = loaded->tracks().at(1).audioEffects;
    QCOMPARE(t2Fx.size(), 2);
    QCOMPARE(t2Fx.at(0).catalogId, QStringLiteral("utility.eq"));
    QCOMPARE(t2Fx.at(0).parameters.value(QStringLiteral("low")).toDouble(), -12.0);
    QCOMPARE(t2Fx.at(0).parameters.value(QStringLiteral("high")).toDouble(), -12.0);
    QCOMPARE(t2Fx.at(1).catalogId, QStringLiteral("space.reverb"));

    // Event on Track 2: Sony Volume scaled clip.volume to 2.0, plus EQ + Reverb in clip.audioEffects.
    QCOMPARE(loaded->tracks().at(1).clips.size(), 1);
    const auto &clip = loaded->tracks().at(1).clips.at(0);
    QVERIFY(!clip.volume.isEmpty());
    QVERIFY(qAbs(clip.volume.evaluateAt(0) - 2.0) < 1e-3);
    QCOMPARE(clip.audioEffects.size(), 2);
    QCOMPARE(clip.audioEffects.at(0).catalogId, QStringLiteral("utility.eq"));
    QCOMPARE(clip.audioEffects.at(1).catalogId, QStringLiteral("space.reverb"));

    // Also validate against real Odds 4.1b.veg if mounted.
    const QString veg41b = findExtendedVeg(QStringLiteral("Episode 4/OLD/4.1b.veg"));
    if (!veg41b.isEmpty()) {
        QString rErr;
        const auto p = drift::vegas::readProject(veg41b, &rErr);
        QVERIFY2(p.has_value(), qPrintable(rErr));
        bool foundCleanAudioTrack = false;
        bool foundTrackAudioFx = false;
        bool foundClipAudioFx = false;
        for (const auto &t : p->tracks()) {
            if (t.type == drift::TrackType::Audio) {
                if (t.audioEffects.isEmpty())
                    foundCleanAudioTrack = true;
                else
                    foundTrackAudioFx = true;
                for (const auto &c : t.clips) {
                    if (!c.audioEffects.isEmpty())
                        foundClipAudioFx = true;
                }
            }
        }
        QVERIFY(foundCleanAudioTrack);
        QVERIFY(foundTrackAudioFx);
        QVERIFY(foundClipAudioFx);
    }
}

void TestInterchangeVegas::parsesVideoTrackFxAndExplicitTransitionPlugins()
{
    auto appendU32 = [](QByteArray &out, quint32 v) {
        char b[4] = {char(v & 0xff), char((v >> 8) & 0xff), char((v >> 16) & 0xff), char((v >> 24) & 0xff)};
        out.append(b, 4);
    };
    auto appendU64 = [](QByteArray &out, quint64 v) {
        char b[8];
        for (int i = 0; i < 8; ++i)
            b[i] = char((v >> (i * 8)) & 0xff);
        out.append(b, 8);
    };
    auto wrapW64Chunk = [&](const QByteArray &guid, const QByteArray &payload) {
        QByteArray out = guid;
        appendU64(out, static_cast<quint64>(24 + payload.size()));
        out.append(payload);
        while (out.size() % 8 != 0)
            out.append('\0');
        return out;
    };
    auto wrapW64List = [&](const QByteArray &typeGuid, const QByteArray &body) {
        static const QByteArray kList = QByteArray::fromHex("6c6973742f91cf11a5d628db04c10000");
        QByteArray out = kList;
        appendU64(out, static_cast<quint64>(40 + body.size()));
        out.append(typeGuid);
        out.append(body);
        while (out.size() % 8 != 0)
            out.append('\0');
        return out;
    };
    auto utf16 = [](const QString &s) {
        return QByteArray(reinterpret_cast<const char *>(s.utf16()), (s.size() + 1) * 2);
    };

    const QByteArray kRiff = QByteArray::fromHex("726966662e91cf11a5d628db04c10000");
    const QByteArray kMediaPool = QByteArray::fromHex("5b2d8fb20f23d21186af00c04f8edb8a");
    const QByteArray kTimeline = QByteArray::fromHex("47076c4d1623d21186b000c04f8edb8a");
    const QByteArray kTrack = QByteArray::fromHex("48076c4d1623d21186b000c04f8edb8a");
    const QByteArray kEventContainer = QByteArray::fromHex("49076c4d1623d21186b000c04f8edb8a");
    const QByteArray kEvent = QByteArray::fromHex("4a076c4d1623d21186b000c04f8edb8a");
    const QByteArray kEventTime = QByteArray::fromHex("6a208d162123d21186b000c04f8edb8a");
    const QByteArray kEventTake = QByteArray::fromHex("6d208d162123d21186b000c04f8edb8a");
    const QByteArray kTrackProps28 = QByteArray::fromHex("286cd4690b7fd211871700c04f8edb8a");
    const QByteArray kE5E4 = QByteArray::fromHex("e5e4f01a7f6ed211870500c04f8edb8a");
    const QByteArray kE6E4 = QByteArray::fromHex("e6e4f01a7f6ed211870500c04f8edb8a");
    const QByteArray kCe3b = QByteArray::fromHex("ce3b2bfc8579d211871100c04f8edb8a");
    const QByteArray kCf3b = QByteArray::fromHex("cf3b2bfc8579d211871100c04f8edb8a");

    QByteArray mpPayload(96, '\0');
    const quint32 mid = 1;
    std::memcpy(mpPayload.data() + 60, &mid, 4);
    mpPayload.append(utf16(QStringLiteral("C:\\video\\shot.mov")));
    const QByteArray mpList = wrapW64List(
        kMediaPool, wrapW64Chunk(QByteArray::fromHex("5d2d8fb20f23d21186af00c04f8edb8a"), mpPayload));

    auto makeEvent = [&](quint64 startTicks, quint64 durTicks, const QString &trPluginUtf16) {
        QByteArray evTime(80, '\0');
        std::memcpy(evTime.data() + 16, &startTicks, 8);
        std::memcpy(evTime.data() + 24, &durTicks, 8);
        QByteArray evTake(44, '\0');
        std::memcpy(evTake.data() + 36, &mid, 4);
        QByteArray inner = wrapW64Chunk(kEventTime, evTime) + wrapW64Chunk(kEventTake, evTake);
        if (!trPluginUtf16.isEmpty()) {
            QByteArray trBody(32, '\0');
            trBody.append(utf16(trPluginUtf16));
            inner += wrapW64List(kCe3b, wrapW64Chunk(kCf3b, trBody));
        }
        return wrapW64List(kEvent, inner);
    };

    // 3 overlapping events:
    // clip 1: [0, 3s]
    // clip 2: [2s, 5s] with :flash} -> dip_white
    // clip 3: [4s, 7s] with :linearwipe} Left-Right, Soft Edge -> wipe_right
    const QByteArray eventsBody =
        makeEvent(0, 30'000'000ULL, QString())
        + makeEvent(20'000'000ULL, 30'000'000ULL, QStringLiteral("{Svfx:com.sonycreativesoftware:flash}"))
        + makeEvent(40'000'000ULL, 30'000'000ULL,
                    QStringLiteral("{Svfx:com.sonycreativesoftware:linearwipe} Left-Right, Soft Edge"));

    QByteArray trackFxBody(16, '\0');
    trackFxBody.append(utf16(QStringLiteral("{Svfx:com.sonycreativesoftware:vignette}")));
    const QByteArray trackFxList = wrapW64List(kE5E4, wrapW64Chunk(kE6E4, trackFxBody));

    QByteArray vProps(40, '\0');
    const quint32 videoCode = 2;
    std::memcpy(vProps.data() + 32, &videoCode, 4);
    const QByteArray tList = wrapW64List(
        kTrack,
        wrapW64Chunk(kTrackProps28, vProps)
            + wrapW64List(kEventContainer, eventsBody)
            + trackFxList);

    const QByteArray body = mpList + wrapW64List(kTimeline, tList);
    QByteArray veg = kRiff;
    appendU64(veg, static_cast<quint64>(40 + body.size()));
    veg.append(QByteArray(16, '\0'));
    veg.append(body);

    QString err;
    const auto loaded = drift::vegas::readProjectData(veg, QString(), &err);
    QVERIFY2(loaded.has_value(), qPrintable(err));
    QCOMPARE(loaded->tracks().size(), 1);
    const auto &t = loaded->tracks().at(0);
    QCOMPARE(t.effects.size(), 1);
    QCOMPARE(t.effects.at(0).catalogId, QStringLiteral("stylize.vignette"));
    QCOMPARE(t.transitions.size(), 2);
    QCOMPARE(t.transitions.at(0).kindId, QStringLiteral("dip_white"));
    QCOMPARE(t.transitions.at(1).kindId, QStringLiteral("wipe_right"));

    // Extended real .veg verification if Odds folder is mounted.
    const QString creditsVeg = findExtendedVeg(QStringLiteral("Episode 4/OLD/Credits.veg"));
    if (!creditsVeg.isEmpty()) {
        QString rErr;
        const auto p = drift::vegas::readProject(creditsVeg, &rErr);
        QVERIFY2(p.has_value(), qPrintable(rErr));
        bool foundTrackVignette = false;
        for (const auto &tr : p->tracks()) {
            for (const auto &fx : tr.effects) {
                if (fx.catalogId == QLatin1String("stylize.vignette"))
                    foundTrackVignette = true;
            }
        }
        QVERIFY(foundTrackVignette);
    }

    const QString veg11a = findExtendedVeg(QStringLiteral("Episode 4/OLD/1.1a.veg"));
    if (!veg11a.isEmpty()) {
        QString rErr;
        const auto p = drift::vegas::readProject(veg11a, &rErr);
        QVERIFY2(p.has_value(), qPrintable(rErr));
        bool foundDipWhite = false;
        for (const auto &tr : p->tracks()) {
            for (const auto &tItem : tr.transitions) {
                if (tItem.kindId == QLatin1String("dip_white"))
                    foundDipWhite = true;
            }
        }
        QVERIFY(foundDipWhite);
    }

    const QString veg51 = findExtendedVeg(QStringLiteral("Episode 4/Backup/5.1.veg"));
    if (!veg51.isEmpty()) {
        QString rErr;
        const auto p = drift::vegas::readProject(veg51, &rErr);
        QVERIFY2(p.has_value(), qPrintable(rErr));
        bool foundWipeRight = false;
        for (const auto &tr : p->tracks()) {
            for (const auto &tItem : tr.transitions) {
                if (tItem.kindId == QLatin1String("wipe_right"))
                    foundWipeRight = true;
            }
        }
        QVERIFY(foundWipeRight);
    }
}

QTEST_MAIN(TestInterchangeVegas)
#include "tst_interchange_vegas.moc"
