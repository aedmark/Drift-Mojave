#include <QtTest>

#include "core/effects/BlendMode.h"
#include "core/effects/Effect.h"
#include "core/project/Project.h"
#include "engine/color/CubeLut.h"
#include "engine/compositor/FrameCompositor.h"
#include "engine/compositor/GpuCompositor.h"
#include "engine/gpu/GlRuntime.h"
#include "engine/packages/effects/EffectCatalog.h"
#include "engine/packages/transitions/TransitionCatalog.h"

#include <QDir>
#include <QImage>
#include <QScopeGuard>
#include <QStandardPaths>
#include <QTemporaryDir>

#include <algorithm>
#include <cmath>
#include <vector>

using namespace drift;

namespace {

constexpr double kMinPsnrDb = 45.0;
constexpr int kMaxChannelAbsDiff = 12;
constexpr int kMaxP99ChannelDiff = 2;

QImage makeSolidImage(int w, int h, const QColor &color)
{
    QImage img(w, h, QImage::Format_RGBA8888);
    img.fill(color);
    return img;
}

QImage makeGradientImage(int w, int h)
{
    QImage img(w, h, QImage::Format_RGBA8888);
    for (int y = 0; y < h; ++y) {
        const int g = (y * 255) / qMax(1, h - 1);
        for (int x = 0; x < w; ++x) {
            const int r = (x * 255) / qMax(1, w - 1);
            const int b = 255 - ((x + y) * 255) / qMax(1, w + h - 2);
            img.setPixelColor(x, y, QColor(r, g, b, 255));
        }
    }
    return img;
}

QImage makeBarsImage(int w, int h)
{
    static const QColor kBars[7] = {
        QColor(191, 191, 191), QColor(191, 191, 0), QColor(0, 191, 191),
        QColor(0, 191, 0),     QColor(191, 0, 191), QColor(191, 0, 0),
        QColor(0, 0, 191),
    };
    QImage img(w, h, QImage::Format_RGBA8888);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const int bar = qBound(0, (x * 7) / qMax(1, w), 6);
            img.setPixelColor(x, y, kBars[bar]);
        }
    }
    return img;
}

QImage makeCheckerImage(int w, int h, int cell = 16)
{
    QImage img(w, h, QImage::Format_RGBA8888);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const bool light = (((x / cell) + (y / cell)) & 1) == 0;
            img.setPixelColor(x, y, light ? QColor(220, 220, 230, 200) : QColor(30, 35, 45, 200));
        }
    }
    return img;
}

struct ImageDiffMetrics
{
    double psnrDb = 0.0;
    double meanAbs = 0.0;
    int maxAbs = 0;
    int p99Abs = 0;
    QImage diff8x;
};

ImageDiffMetrics compareImages(const QImage &actualIn, const QImage &expectedIn)
{
    const QImage a = actualIn.convertToFormat(QImage::Format_RGBA8888);
    const QImage b = expectedIn.convertToFormat(QImage::Format_RGBA8888);
    ImageDiffMetrics m;
    if (a.size() != b.size() || a.isNull())
        return m;

    const int w = a.width();
    const int h = a.height();
    m.diff8x = QImage(w, h, QImage::Format_RGBA8888);

    std::vector<int> diffs;
    diffs.reserve(size_t(w) * size_t(h) * 4);
    double sse = 0.0;
    double absSum = 0.0;

    for (int y = 0; y < h; ++y) {
        const uchar *pa = a.constScanLine(y);
        const uchar *pb = b.constScanLine(y);
        uchar *pd = m.diff8x.scanLine(y);
        for (int x = 0; x < w; ++x) {
            for (int c = 0; c < 4; ++c) {
                const int d = std::abs(int(pa[x * 4 + c]) - int(pb[x * 4 + c]));
                diffs.push_back(d);
                m.maxAbs = std::max(m.maxAbs, d);
                sse += double(d) * double(d);
                absSum += double(d);
                pd[x * 4 + c] = c == 3 ? 255 : static_cast<uchar>(qMin(255, d * 8));
            }
        }
    }

    const double n = double(diffs.size());
    m.meanAbs = absSum / std::max(1.0, n);
    const double mse = sse / std::max(1.0, n);
    m.psnrDb = mse <= 1e-12 ? 99.0 : (10.0 * std::log10((255.0 * 255.0) / mse));
    std::sort(diffs.begin(), diffs.end());
    if (!diffs.empty()) {
        const size_t idx = std::min(diffs.size() - 1, (diffs.size() * 99) / 100);
        m.p99Abs = diffs[idx];
    }
    return m;
}

Clip makeImageClip(const QString &id, const QString &path, TimeUs startUs, TimeUs durUs)
{
    Clip c;
    c.id = id;
    c.type = ClipType::Image;
    c.path = path;
    c.timelineStart = startUs;
    c.timelineDuration = durUs;
    c.srcIn = 0;
    c.srcOut = durUs;
    return c;
}

} // namespace

class GoldenRenderTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void transformAndOpacityMatchesGolden();
    void blendModesMatchGolden();
    void colorGradeVignetteSharpenMatchesGolden();
    void crossDissolveMidpointMatchesGoldenAndAnalytic();
    void rgba8FallbackMatchesFloatFbo();
    void cubeLutTetrahedralMatchesAnalytic();

private:
    bool ensureGpuOrSkip();
    void checkAgainstGolden(const QString &name, const QImage &actual);

    QTemporaryDir m_mediaDir;
    QString m_solidRedPath;
    QString m_solidBluePath;
    QString m_gradientPath;
    QString m_barsPath;
    QString m_checkerPath;
};

void GoldenRenderTest::initTestCase()
{
    QStandardPaths::setTestModeEnabled(true);
    reloadEffectCatalog({QString::fromUtf8(DRIFT_TEST_EFFECTS_DIR)});
    reloadTransitionCatalog({QString::fromUtf8(DRIFT_TEST_TRANSITIONS_DIR)});

    QVERIFY(m_mediaDir.isValid());
    m_solidRedPath = m_mediaDir.filePath(QStringLiteral("red.png"));
    m_solidBluePath = m_mediaDir.filePath(QStringLiteral("blue.png"));
    m_gradientPath = m_mediaDir.filePath(QStringLiteral("gradient.png"));
    m_barsPath = m_mediaDir.filePath(QStringLiteral("bars.png"));
    m_checkerPath = m_mediaDir.filePath(QStringLiteral("checker.png"));

    QVERIFY(makeSolidImage(320, 180, QColor(224, 32, 32)).save(m_solidRedPath));
    QVERIFY(makeSolidImage(320, 180, QColor(32, 64, 224)).save(m_solidBluePath));
    QVERIFY(makeGradientImage(320, 180).save(m_gradientPath));
    QVERIFY(makeBarsImage(320, 180).save(m_barsPath));
    QVERIFY(makeCheckerImage(320, 180).save(m_checkerPath));
}

bool GoldenRenderTest::ensureGpuOrSkip()
{
    if (GpuCompositor::isAvailable())
        return true;
    if (qEnvironmentVariableIsSet("DRIFT_REQUIRE_GPU")) {
        QTest::qFail("DRIFT_REQUIRE_GPU=1 is set, but OpenGL offscreen compositor is unavailable",
                     __FILE__, __LINE__);
        return false;
    }
    return false;
}

void GoldenRenderTest::checkAgainstGolden(const QString &name, const QImage &actual)
{
    QVERIFY(!actual.isNull());
    const QDir goldenDir(QString::fromUtf8(DRIFT_TEST_GOLDEN_DIR));
    const QString goldenPath = goldenDir.filePath(name + QStringLiteral(".png"));

    if (qEnvironmentVariableIsSet("DRIFT_BLESS_GOLDENS")) {
        QVERIFY(QDir().mkpath(goldenDir.absolutePath()));
        QVERIFY2(actual.save(goldenPath), qPrintable(goldenPath));
        return;
    }

    QVERIFY2(QFileInfo::exists(goldenPath),
             qPrintable(QStringLiteral("Missing golden image: %1 (run with DRIFT_BLESS_GOLDENS=1)")
                            .arg(goldenPath)));

    const QImage expected(goldenPath);
    QVERIFY(!expected.isNull());
    QCOMPARE(actual.size(), expected.size());

    const ImageDiffMetrics m = compareImages(actual, expected);
    if (m.psnrDb < kMinPsnrDb || m.maxAbs > kMaxChannelAbsDiff || m.p99Abs > kMaxP99ChannelDiff) {
        const QString failDirPath = QString::fromUtf8(DRIFT_TEST_GOLDEN_FAIL_DIR);
        QDir().mkpath(failDirPath);
        actual.save(QDir(failDirPath).filePath(name + QStringLiteral("_actual.png")));
        m.diff8x.save(QDir(failDirPath).filePath(name + QStringLiteral("_diff8x.png")));
    }

    QVERIFY2(m.psnrDb >= kMinPsnrDb,
             qPrintable(QStringLiteral("%1 PSNR %2 dB < %3")
                            .arg(name)
                            .arg(m.psnrDb, 0, 'f', 2)
                            .arg(kMinPsnrDb, 0, 'f', 2)));
    QVERIFY2(m.p99Abs <= kMaxP99ChannelDiff,
             qPrintable(QStringLiteral("%1 p99 diff %2 > %3")
                            .arg(name)
                            .arg(m.p99Abs)
                            .arg(kMaxP99ChannelDiff)));
    QVERIFY2(m.maxAbs <= kMaxChannelAbsDiff,
             qPrintable(QStringLiteral("%1 max diff %2 > %3")
                            .arg(name)
                            .arg(m.maxAbs)
                            .arg(kMaxChannelAbsDiff)));
}

void GoldenRenderTest::transformAndOpacityMatchesGolden()
{
    if (!ensureGpuOrSkip()) {
        if (!qEnvironmentVariableIsSet("DRIFT_REQUIRE_GPU"))
            QSKIP("OpenGL offscreen context unavailable");
        return;
    }

    Project project;
    project.setResolution(320, 180);
    project.setFps(30);
    project.setBackground({BackgroundKind::Color, QColor(20, 24, 30), 0.0});
    project.tracks().clear();
    project.tracks().append(Track{.type = TrackType::Video});
    project.tracks().append(Track{.type = TrackType::Video});

    project.tracks()[0].clips.append(
        makeImageClip(QStringLiteral("bg"), m_gradientPath, 0, secondsToUs(2.0)));

    Clip fg = makeImageClip(QStringLiteral("fg"), m_barsPath, 0, secondsToUs(2.0));
    fg.scaleX.setKeyframe(0, 0.6);
    fg.scaleY.setKeyframe(0, 0.6);
    fg.rotation.setKeyframe(0, 15.0);
    fg.positionX.setKeyframe(0, 24.0);
    fg.positionY.setKeyframe(0, -10.0);
    fg.opacity.setKeyframe(0, 0.75);
    project.tracks()[1].clips.append(fg);

    FrameCompositor comp;
    comp.setProject(&project);
    const QImage frame = comp.compositeAt(secondsToUs(0.5));
    checkAgainstGolden(QStringLiteral("transform_opacity"), frame);
}

void GoldenRenderTest::blendModesMatchGolden()
{
    if (!ensureGpuOrSkip()) {
        if (!qEnvironmentVariableIsSet("DRIFT_REQUIRE_GPU"))
            QSKIP("OpenGL offscreen context unavailable");
        return;
    }

    Project project;
    project.setResolution(320, 180);
    project.setFps(30);
    project.tracks().clear();
    for (int i = 0; i < 4; ++i)
        project.tracks().append(Track{.type = TrackType::Video});

    project.tracks()[0].clips.append(
        makeImageClip(QStringLiteral("base"), m_barsPath, 0, secondsToUs(2.0)));

    Clip mult = makeImageClip(QStringLiteral("mult"), m_gradientPath, 0, secondsToUs(2.0));
    mult.blendMode = BlendMode::Multiply;
    project.tracks()[1].clips.append(mult);

    Clip scr = makeImageClip(QStringLiteral("scr"), m_checkerPath, 0, secondsToUs(2.0));
    scr.blendMode = BlendMode::Screen;
    scr.opacity.setKeyframe(0, 0.55);
    project.tracks()[2].clips.append(scr);

    Clip ovl = makeImageClip(QStringLiteral("ovl"), m_solidRedPath, 0, secondsToUs(2.0));
    ovl.blendMode = BlendMode::Overlay;
    ovl.scaleX.setKeyframe(0, 0.5);
    ovl.scaleY.setKeyframe(0, 0.5);
    project.tracks()[3].clips.append(ovl);

    FrameCompositor comp;
    comp.setProject(&project);
    const QImage frame = comp.compositeAt(secondsToUs(0.5));
    checkAgainstGolden(QStringLiteral("blend_modes"), frame);
}

void GoldenRenderTest::colorGradeVignetteSharpenMatchesGolden()
{
    if (!ensureGpuOrSkip()) {
        if (!qEnvironmentVariableIsSet("DRIFT_REQUIRE_GPU"))
            QSKIP("OpenGL offscreen context unavailable");
        return;
    }

    Project project;
    project.setResolution(320, 180);
    project.setFps(30);
    project.tracks().clear();
    project.tracks().append(Track{.type = TrackType::Video});

    Clip clip = makeImageClip(QStringLiteral("graded"), m_barsPath, 0, secondsToUs(2.0));

    Effect grade;
    grade.catalogId = QStringLiteral("color.grade");
    grade.parameters.insert(QStringLiteral("saturation"), 1.25);
    grade.parameters.insert(QStringLiteral("contrast"), 1.15);
    clip.effects.append(grade);

    Effect vig;
    vig.catalogId = QStringLiteral("stylize.vignette");
    vig.parameters.insert(QStringLiteral("amount"), 0.55);
    vig.parameters.insert(QStringLiteral("feather"), 0.35);
    clip.effects.append(vig);

    Effect sharp;
    sharp.catalogId = QStringLiteral("stylize.sharpen");
    sharp.parameters.insert(QStringLiteral("amount"), 0.4);
    clip.effects.append(sharp);

    project.tracks()[0].clips.append(clip);

    FrameCompositor comp;
    comp.setProject(&project);
    const QImage frame = comp.compositeAt(secondsToUs(0.5));
    checkAgainstGolden(QStringLiteral("color_vignette_sharpen"), frame);
}

void GoldenRenderTest::crossDissolveMidpointMatchesGoldenAndAnalytic()
{
    if (!ensureGpuOrSkip()) {
        if (!qEnvironmentVariableIsSet("DRIFT_REQUIRE_GPU"))
            QSKIP("OpenGL offscreen context unavailable");
        return;
    }

    Project project;
    project.setResolution(320, 180);
    project.setFps(30);
    project.tracks().clear();
    project.tracks().append(Track{.type = TrackType::Video});

    const Clip left = makeImageClip(QStringLiteral("left"), m_solidRedPath, 0, secondsToUs(2.0));
    const Clip right = makeImageClip(QStringLiteral("right"), m_solidBluePath, secondsToUs(1.0), secondsToUs(2.0));
    project.tracks()[0].clips.append(left);
    project.tracks()[0].clips.append(right);

    Transition tr;
    tr.id = QStringLiteral("tr-1");
    tr.kindId = QStringLiteral("crossfade");
    tr.fromClipId = left.id;
    tr.toClipId = right.id;
    tr.durationUs = secondsToUs(1.0);
    project.tracks()[0].transitions.append(tr);

    FrameCompositor comp;
    comp.setProject(&project);
    // Midpoint at 1.5s across [1.0s, 2.0s] overlap between (224,32,32) and (32,64,224).
    const QImage mid = comp.compositeAt(secondsToUs(1.5)).convertToFormat(QImage::Format_RGBA8888);
    const QColor c = mid.pixelColor(160, 90);
    QVERIFY2(qAbs(c.red() - 128) <= 8, qPrintable(QStringLiteral("red=%1").arg(c.red())));
    QVERIFY2(qAbs(c.blue() - 128) <= 8, qPrintable(QStringLiteral("blue=%1").arg(c.blue())));

    checkAgainstGolden(QStringLiteral("cross_dissolve_midpoint"), mid);
}

void GoldenRenderTest::rgba8FallbackMatchesFloatFbo()
{
    if (!ensureGpuOrSkip()) {
        if (!qEnvironmentVariableIsSet("DRIFT_REQUIRE_GPU"))
            QSKIP("OpenGL offscreen context unavailable");
        return;
    }

    Project project;
    project.setResolution(320, 180);
    project.setFps(30);
    project.tracks().clear();
    project.tracks().append(Track{.type = TrackType::Video});

    Clip clip = makeImageClip(QStringLiteral("graded"), m_gradientPath, 0, secondsToUs(2.0));
    Effect grade;
    grade.catalogId = QStringLiteral("color.grade");
    grade.parameters.insert(QStringLiteral("saturation"), 1.2);
    grade.parameters.insert(QStringLiteral("contrast"), 1.1);
    clip.effects.append(grade);
    project.tracks()[0].clips.append(clip);

    FrameCompositor comp;
    comp.setProject(&project);
    const QImage floatImg = comp.compositeAt(secondsToUs(0.5));
    QVERIFY(!floatImg.isNull());

    drift::gl::runtime().exec([] { drift::gl::runtime().setFloatTargetsDisabledForTest(true); });
    const auto restore = qScopeGuard([] {
        drift::gl::runtime().exec([] { drift::gl::runtime().setFloatTargetsDisabledForTest(false); });
    });
    const QImage rgba8Img = comp.compositeAt(secondsToUs(0.5));
    QVERIFY(!rgba8Img.isNull());

    const ImageDiffMetrics m = compareImages(rgba8Img, floatImg);
    QVERIFY2(m.p99Abs <= 4, qPrintable(QStringLiteral("p99=%1").arg(m.p99Abs)));
    QVERIFY2(m.meanAbs <= 2.0, qPrintable(QStringLiteral("mean=%1").arg(m.meanAbs, 0, 'f', 3)));
}

void GoldenRenderTest::cubeLutTetrahedralMatchesAnalytic()
{
    // Build a deterministic 17^3 3D LUT applying (r, g, b) -> (0.8*r + 0.1, 0.5*g + 0.25, 0.9*b)
    CubeLut lut = identityLut(17);
    for (size_t i = 0; i + 2 < lut.rgb.size(); i += 3) {
        lut.rgb[i + 0] = 0.8f * lut.rgb[i + 0] + 0.1f;
        lut.rgb[i + 1] = 0.5f * lut.rgb[i + 1] + 0.25f;
        lut.rgb[i + 2] = 0.9f * lut.rgb[i + 2];
    }
    QVERIFY(lut.isValid());

    const QRgb inPx = qRgba(100, 160, 200, 255);
    const QRgb outPx = applyLut(lut, inPx, 1.0f);

    const int expectedR = qRound((0.8 * (100.0 / 255.0) + 0.1) * 255.0);
    const int expectedG = qRound((0.5 * (160.0 / 255.0) + 0.25) * 255.0);
    const int expectedB = qRound((0.9 * (200.0 / 255.0)) * 255.0);

    QVERIFY(qAbs(qRed(outPx) - expectedR) <= 1);
    QVERIFY(qAbs(qGreen(outPx) - expectedG) <= 1);
    QVERIFY(qAbs(qBlue(outPx) - expectedB) <= 1);
}

QTEST_MAIN(GoldenRenderTest)
#include "tst_golden_render.moc"
