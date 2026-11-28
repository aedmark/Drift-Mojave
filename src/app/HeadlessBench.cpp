#include "app/HeadlessBench.h"

#include "core/effects/BlendMode.h"
#include "core/effects/Effect.h"
#include "core/project/Project.h"
#include "engine/audio/AudioMixer.h"
#include "engine/compositor/FrameCompositor.h"
#include "engine/compositor/GpuCompositor.h"
#include "engine/decode/readers/ClipReader.h"
#include "engine/packages/effects/EffectCatalog.h"
#include "engine/packages/transitions/TransitionCatalog.h"
#include "playback/stats/PlaybackDiagnostics.h"

#include <QColor>
#include <QElapsedTimer>
#include <QFile>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QTemporaryDir>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

namespace drift {

namespace {

double percentileMs(std::vector<double> values, double p)
{
    if (values.empty())
        return 0.0;
    std::sort(values.begin(), values.end());
    const size_t idx = std::min(values.size() - 1, static_cast<size_t>(std::floor(p * double(values.size() - 1))));
    return values[idx];
}

} // namespace

int runHeadlessBench(const QString &jsonOutPath)
{
    reloadEffectCatalog({});
    reloadTransitionCatalog({});

    QJsonObject report;
    report.insert(QStringLiteral("gpu_available"), GpuCompositor::isAvailable());

    // 1. Decode benchmark using bundled 1080p60 clip when present
    const QVariantMap decodeBench = PlaybackDiagnostics::benchmarkClip(QString(), QSize(1920, 1080));
    QJsonObject decodeJson = QJsonObject::fromVariantMap(decodeBench);
    report.insert(QStringLiteral("decode_1080p"), decodeJson);

    // 2. 3-layer 1080p GPU compositor stack benchmark (blend modes + grade + vignette)
    QJsonObject compJson;
    if (GpuCompositor::isAvailable()) {
        QTemporaryDir dir;
        if (dir.isValid()) {
            const QString bgPath = dir.filePath(QStringLiteral("bg.png"));
            const QString midPath = dir.filePath(QStringLiteral("mid.png"));
            const QString topPath = dir.filePath(QStringLiteral("top.png"));

            QImage bg(1920, 1080, QImage::Format_RGBA8888);
            bg.fill(QColor(30, 60, 110));
            QImage mid(1920, 1080, QImage::Format_RGBA8888);
            mid.fill(QColor(180, 120, 40, 200));
            QImage top(1920, 1080, QImage::Format_RGBA8888);
            top.fill(QColor(220, 220, 240, 140));
            bg.save(bgPath);
            mid.save(midPath);
            top.save(topPath);

            Project proj;
            proj.setResolution(1920, 1080);
            proj.setFps(30);
            proj.tracks().clear();
            for (int i = 0; i < 3; ++i)
                proj.tracks().append(Track{.type = TrackType::Video});

            auto imgClip = [](const QString &id, const QString &path, BlendMode mode) {
                Clip c;
                c.id = id;
                c.type = ClipType::Image;
                c.path = path;
                c.timelineStart = 0;
                c.timelineDuration = secondsToUs(2.0);
                c.srcOut = secondsToUs(2.0);
                c.blendMode = mode;
                return c;
            };

            Clip base = imgClip(QStringLiteral("b0"), bgPath, BlendMode::Normal);
            Effect grade;
            grade.catalogId = QStringLiteral("color.grade");
            grade.parameters.insert(QStringLiteral("saturation"), 1.2);
            base.effects.append(grade);

            Clip layer1 = imgClip(QStringLiteral("b1"), midPath, BlendMode::Screen);
            layer1.scaleX.setKeyframe(0, 0.8);
            layer1.scaleY.setKeyframe(0, 0.8);
            layer1.rotation.setKeyframe(0, 8.0);

            Clip layer2 = imgClip(QStringLiteral("b2"), topPath, BlendMode::Overlay);
            Effect vig;
            vig.catalogId = QStringLiteral("stylize.vignette");
            vig.parameters.insert(QStringLiteral("amount"), 0.5);
            layer2.effects.append(vig);

            proj.tracks()[0].clips.append(base);
            proj.tracks()[1].clips.append(layer1);
            proj.tracks()[2].clips.append(layer2);

            FrameCompositor comp;
            comp.setProject(&proj);
            FrameCompositor::RenderOptions opts;

            std::vector<double> timesMs;
            constexpr int kWarmup = 5;
            constexpr int kFrames = 20;
            timesMs.reserve(kFrames);
            for (int i = 0; i < kWarmup + kFrames; ++i) {
                QElapsedTimer t;
                t.start();
                const GpuFrameTexture tex = comp.compositeToTextureAt(TimeUs(i) * 33'333, opts);
                if (!tex.isValid())
                    break;
                if (i >= kWarmup)
                    timesMs.push_back(double(t.nsecsElapsed()) / 1'000'000.0);
            }
            const double p50 = percentileMs(timesMs, 0.50);
            const double p95 = percentileMs(timesMs, 0.95);
            compJson.insert(QStringLiteral("frames"), int(timesMs.size()));
            compJson.insert(QStringLiteral("p50_ms"), p50);
            compJson.insert(QStringLiteral("p95_ms"), p95);
            compJson.insert(QStringLiteral("fps"), p50 > 0.0 ? (1000.0 / p50) : 0.0);
        }
    }
    report.insert(QStringLiteral("composite_1080p_stack"), compJson);

    // 3. 24-track audio mixer DSP benchmark (3s @ 48 kHz)
    QJsonObject audioJson;
    {
        Project aproj;
        aproj.setFps(30);
        aproj.tracks().clear();
        for (int i = 0; i < 24; ++i) {
            Track tr;
            tr.id = QStringLiteral("a-%1").arg(i);
            tr.type = TrackType::Audio;
            tr.volume = 0.8;
            for (const char *fxId : {"utility.eq", "utility.compressor", "utility.gain"}) {
                Effect fx;
                fx.catalogId = QString::fromLatin1(fxId);
                tr.audioEffects.append(fx);
            }
            aproj.tracks().append(tr);
        }

        AudioMixer mixer;
        mixer.setProject(&aproj);
        constexpr int kSampleRate = 48000;
        constexpr int kBlock = 1024;
        constexpr double kAudioSeconds = 3.0;
        const int totalFrames = int(kSampleRate * kAudioSeconds);
        std::vector<float> buf(size_t(kBlock) * 2, 0.0f);

        QElapsedTimer t;
        t.start();
        for (int pos = 0; pos < totalFrames; pos += kBlock) {
            const int n = std::min(kBlock, totalFrames - pos);
            const TimeUs atUs = (TimeUs(pos) * kUsPerSecond) / kSampleRate;
            mixer.mix(atUs, n, kSampleRate, buf.data());
        }
        const double mixMs = double(t.nsecsElapsed()) / 1'000'000.0;
        const double realtimeFactor = mixMs > 0.0 ? (kAudioSeconds * 1000.0) / mixMs : 0.0;
        audioJson.insert(QStringLiteral("tracks"), 24);
        audioJson.insert(QStringLiteral("audio_seconds"), kAudioSeconds);
        audioJson.insert(QStringLiteral("mix_ms"), mixMs);
        audioJson.insert(QStringLiteral("realtime_factor"), realtimeFactor);
    }
    report.insert(QStringLiteral("audio_mix_24_tracks"), audioJson);

    const ClipReader::DecodeStats d = ClipReader::decodeStats();
    report.insert(QStringLiteral("decode_stats"), QJsonObject{
        {QStringLiteral("frames_decoded"), double(d.framesDecoded)},
        {QStringLiteral("frames_skipped_catch_up"), double(d.framesSkippedCatchUp)},
        {QStringLiteral("seeks"), double(d.seeks)},
        {QStringLiteral("cache_requests"), double(d.cacheRequests)},
        {QStringLiteral("cache_hits"), double(d.cacheHits)},
    });

    std::printf("Drift Mojave Headless Benchmark\n");
    std::printf("  GPU available       : %s\n", GpuCompositor::isAvailable() ? "yes" : "no");
    if (decodeBench.contains(QStringLiteral("decodePerFrameMs"))) {
        std::printf("  Decode 1080p60      : %.2f ms/f (readback %.2f ms/f, %s)\n",
                    decodeBench.value(QStringLiteral("decodePerFrameMs")).toDouble(),
                    decodeBench.value(QStringLiteral("readbackPerFrameMs")).toDouble(),
                    qPrintable(decodeBench.value(QStringLiteral("decoder")).toString()));
    }
    if (compJson.contains(QStringLiteral("p50_ms"))) {
        std::printf("  Composite 3x1080p   : p50=%.2f ms  p95=%.2f ms  (%.1f fps)\n",
                    compJson.value(QStringLiteral("p50_ms")).toDouble(),
                    compJson.value(QStringLiteral("p95_ms")).toDouble(),
                    compJson.value(QStringLiteral("fps")).toDouble());
    }
    std::printf("  Audio 24x3fx (3.0s) : %.2f ms (%.1fx realtime)\n",
                audioJson.value(QStringLiteral("mix_ms")).toDouble(),
                audioJson.value(QStringLiteral("realtime_factor")).toDouble());

    if (!jsonOutPath.isEmpty()) {
        QSaveFile out(jsonOutPath);
        if (!out.open(QIODevice::WriteOnly)) {
            std::fprintf(stderr, "Failed to open benchmark output: %s\n", qPrintable(jsonOutPath));
            return 1;
        }
        out.write(QJsonDocument(report).toJson(QJsonDocument::Indented));
        if (!out.commit()) {
            std::fprintf(stderr, "Failed to commit benchmark output: %s\n", qPrintable(jsonOutPath));
            return 1;
        }
    }
    return 0;
}

} // namespace drift
