#include "canvas.h"
#include "main_window.h"
#include "object_tools.h"
#include "raster_tools.h"
#include "ui_theme.h"

#include <QAction>
#include <QFile>
#include <QMenu>
#include <QSettings>
#include <QSignalSpy>
#include <QStyle>
#include <QTemporaryDir>
#include <QToolBar>
#include <QToolButton>
#include <QtTest>
#include <cmath>

using namespace toon;

namespace {
QImage blankImage(QSize logicalSize, qreal ratio = 1)
{
    QImage image(logicalSize * ratio, QImage::Format_ARGB32_Premultiplied);
    image.setDevicePixelRatio(ratio);
    image.fill(Qt::transparent);
    return image;
}

Project pixelProject()
{
    auto project = Project::create();
    project.width = project.height = 256;
    project.transparent = true;
    auto &layer = project.shots.first().layers.first();
    QImage tile(tileSize, tileSize, QImage::Format_ARGB32_Premultiplied);
    for (int y = 0; y < tile.height(); ++y)
        for (int x = 0; x < tile.width(); ++x)
            tile.setPixelColor(x, y, (x / 5 + y / 5) % 2 ? Qt::black : Qt::white);
    layer.tiles.insert({0, 0}, tile);
    return project;
}

QImage paintView(const QVector<RasterLayer> &layers, bool smooth, const QTransform &view,
                 qreal ratio, const RasterLayerPreview *preview = nullptr)
{
    auto image = blankImage({520, 420}, ratio);
    QPainter painter(&image);
    painter.setTransform(view);
    paintLayers(painter, layers, true, preview, smooth);
    return image;
}

// The independent reference has no tile boundaries: first compose at native
// resolution, then ask Qt to sample that one continuous image exactly once.
QImage continuousReference(const QVector<RasterLayer> &layers, const QTransform &view, qreal ratio)
{
    const QRect bounds(-2 * tileSize, -tileSize, 4 * tileSize, 3 * tileSize);
    auto source = blankImage(bounds.size());
    {
        QPainter painter(&source);
        painter.translate(-bounds.topLeft());
        paintLayers(painter, layers, true);
    }
    auto image = blankImage({520, 420}, ratio);
    QPainter painter(&image);
    painter.setTransform(view);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    painter.setRenderHint(QPainter::Antialiasing, false);
    painter.drawImage(bounds.topLeft(), source);
    return image;
}

QVector<RasterLayer> tiledArtwork(bool translucent, bool vector)
{
    auto base = RasterLayer::create("Continuous pattern");
    for (int ty = -1; ty <= 1; ++ty) for (int tx = -2; tx <= 1; ++tx) {
        QImage tile(tileSize, tileSize, QImage::Format_ARGB32_Premultiplied);
        for (int y = 0; y < tileSize; ++y) for (int x = 0; x < tileSize; ++x) {
            const int ax = tx * tileSize + x + 2 * tileSize;
            const int ay = ty * tileSize + y + tileSize;
            const QColor color((ax / 7) % 2 ? 210 : 40, (ay / 11) % 2 ? 170 : 20,
                               (ax + ay) % 251, translucent ? 145 + (ax % 5) * 20 : 255);
            tile.setPixelColor(x, y, color);
        }
        base.tiles.insert({tx, ty}, tile);
    }
    QVector<RasterLayer> layers{base};
    if (translucent) {
        auto overlay = RasterLayer::create("Blend overlay");
        overlay.opacity = .61;
        overlay.blendMode = LayerBlendMode::Multiply;
        for (int ty = -1; ty <= 1; ++ty) for (int tx = -2; tx <= 1; ++tx) {
            QImage tile(tileSize, tileSize, QImage::Format_ARGB32_Premultiplied);
            tile.fill(QColor(80, 190, 240, 175));
            overlay.tiles.insert({tx, ty}, tile);
        }
        layers.append(overlay);
    }
    if (vector) {
        auto layer = RasterLayer::create("Vector cache");
        layer.kind = LayerKind::Vector;
        auto object = EditableObject::create();
        object.nodes = {BezierNode::corner({-420, -180}), BezierNode::corner({400, 370})};
        object.stroke = QColor(245, 60, 80, 200);
        object.strokeWidth = 31;
        layer.objects.append(object);
        if (!rebuildObjectLayer(layer, {-512, -256, 1024, 768}, 64)) return {};
        layers.append(layer);
    }
    return layers;
}

struct Difference {
    int maximum = 0;
    QPoint maximumAt;
    double average = 0;
    int count = 0;
};

Difference imageDifference(const QImage &a, const QImage &b, QRect sample = {})
{
    if (sample.isEmpty()) sample = a.rect();
    sample &= a.rect() & b.rect();
    Difference difference;
    qint64 total = 0;
    for (int y = sample.top(); y <= sample.bottom(); ++y) {
        for (int x = sample.left(); x <= sample.right(); ++x) {
            const QRgb left = a.pixel(x, y), right = b.pixel(x, y);
            for (const int shift : {0, 8, 16, 24}) {
                const int delta = qAbs(int((left >> shift) & 255) - int((right >> shift) & 255));
                if (delta > difference.maximum) {
                    difference.maximum = delta;
                    difference.maximumAt = {x, y};
                }
                total += delta;
                ++difference.count;
            }
        }
    }
    difference.average = double(total) / qMax(1, difference.count);
    return difference;
}

void samplingDiagnostics(const QVector<RasterLayer> &layers, const QImage &actual,
                         const QImage &expected, const QTransform &view, qreal ratio, QRect sample = {})
{
    const auto directory = qEnvironmentVariable("TOON_UI_PREVIEW");
    if (directory.isEmpty()) return;
    const auto delta = imageDifference(actual, expected, sample);
    if (delta.maximum <= 17) return;
    if (sample.isEmpty()) sample = actual.rect();
    const auto inverse = view.inverted();
    int nearSeam = 0, awayFromSeam = 0;
    auto heatmap = blankImage(actual.size());
    for (int y = sample.top(); y <= sample.bottom(); ++y) for (int x = sample.left(); x <= sample.right(); ++x) {
        const auto a = actual.pixel(x, y), b = expected.pixel(x, y);
        int error = 0;
        for (const int shift : {0, 8, 16, 24}) error = qMax(error, qAbs(int((a >> shift) & 255) - int((b >> shift) & 255)));
        heatmap.setPixelColor(x, y, QColor(qMin(255, error * 16), 0, 0, 255));
        if (error <= 17) continue;
        const auto point = inverse.map(QPointF((x + .5) / ratio, (y + .5) / ratio));
        const auto distance = [](qreal position) { return qAbs(position - qRound(position / tileSize) * tileSize); };
        if (qMin(distance(point.x()), distance(point.y())) < 2) ++nearSeam;
        else ++awayFromSeam;
    }
    QDir().mkpath(directory);
    const auto name = QString::fromLatin1(QTest::currentTestFunction()) + "-" + QString::fromLatin1(QTest::currentDataTag());
    actual.save(directory + "/" + name + "-actual.png");
    expected.save(directory + "/" + name + "-expected.png");
    heatmap.save(directory + "/" + name + "-difference.png");
    const auto p = delta.maximumAt;
    const auto artwork = inverse.map(QPointF((p.x() + .5) / ratio, (p.y() + .5) / ratio));
    const QRect bounds(-512, -256, 1024, 768);
    auto source = blankImage(bounds.size());
    {
        QPainter painter(&source); painter.translate(-bounds.topLeft());
        paintLayers(painter, layers, true);
    }
    const auto sourcePoint = artwork - bounds.topLeft() - QPointF(.5, .5);
    const int left = std::floor(sourcePoint.x()), top = std::floor(sourcePoint.y());
    const qreal fx = sourcePoint.x() - left, fy = sourcePoint.y() - top;
    QList<qreal> ideal;
    if (source.rect().contains(QRect(left, top, 2, 2))) {
        for (const int shift : {16, 8, 0, 24}) {
            qreal value = 0;
            for (int y = 0; y < 2; ++y) for (int x = 0; x < 2; ++x)
                value += ((source.pixel(left + x, top + y) >> shift) & 255)
                    * (x ? fx : 1 - fx) * (y ? fy : 1 - fy);
            ideal.append(value);
        }
    }
    qInfo() << "Sampling diagnostics" << name << "mean" << delta.average << "max" << delta.maximum
            << "at" << p << "artwork" << artwork << "nearSeam" << nearSeam << "away" << awayFromSeam
            << "actualRGBA" << actual.pixelColor(p) << "referenceRGBA" << expected.pixelColor(p)
            << "idealPremultipliedRGBA" << ideal;
}

QByteArray readFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    return file.readAll();
}
} // namespace

class CanvasSmoothingTest final : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { applyTheme(); }

    void canvasRepaintsWithSmoothOrPixelSampling()
    {
        Canvas canvas;
        canvas.resize(600, 480);
        canvas.setProject(pixelProject());
        canvas.setTool(Canvas::Tool::Hand);
        canvas.setZoom(6.3);
        canvas.centerOn(sheetGeometry(canvas.project(), canvas.shot()).output.center());
        canvas.show();
        QCoreApplication::processEvents();
        QVERIFY(canvas.canvasSmoothing());
        QSignalSpy changed(&canvas, &Canvas::canvasSmoothingChanged);
        const auto smooth = canvas.grab().toImage();
        const auto renders = canvas.artworkRenderCount();
        canvas.setCanvasSmoothing(false);
        const auto sharp = canvas.grab().toImage();
        QCOMPARE(changed.count(), 1);
        QVERIFY(canvas.artworkRenderCount() > renders);
        const qreal ratio = canvas.devicePixelRatioF();
        const QRect sample(150 * ratio, 120 * ratio, 300 * ratio, 240 * ratio);
        int smoothGrays = 0, sharpGrays = 0;
        for (int y = sample.top(); y <= sample.bottom(); ++y) {
            for (int x = sample.left(); x <= sample.right(); ++x) {
                const int soft = qRed(smooth.pixel(x, y)), crisp = qRed(sharp.pixel(x, y));
                smoothGrays += soft > 5 && soft < 250;
                sharpGrays += crisp > 5 && crisp < 250;
            }
        }
        QVERIFY(smoothGrays > sample.width() * sample.height() / 10);
        QCOMPARE(sharpGrays, 0);
        const auto sharpRenders = canvas.artworkRenderCount();
        canvas.setCanvasSmoothing(false);
        canvas.grab();
        QCOMPARE(changed.count(), 1);
        QCOMPARE(canvas.artworkRenderCount(), sharpRenders);
        canvas.setCanvasSmoothing(true);
        QCOMPARE(canvas.grab().toImage(), smooth);
    }

    void smoothTilesMatchContinuousImage_data()
    {
        QTest::addColumn<qreal>("ratio");
        QTest::addColumn<bool>("translucent");
        QTest::addColumn<bool>("vector");
        QTest::newRow("opaque-1x") << qreal(1) << false << false;
        QTest::newRow("alpha-and-blend-retina") << qreal(2) << true << false;
        QTest::newRow("vector-cache-retina") << qreal(2) << true << true;
    }

    void liveStrokeRedrawsChangedTiles_data()
    {
        QTest::addColumn<qreal>("rotation");
        QTest::addColumn<bool>("smooth");
        QTest::addColumn<bool>("erase");
        QTest::addColumn<bool>("vector");
        QTest::addColumn<bool>("selectedGroup");
        QTest::newRow("raster-smooth") << qreal(0) << true << false << false << false;
        QTest::newRow("raster-pixel") << qreal(0) << false << false << false << false;
        QTest::newRow("rotated-smooth") << qreal(31) << true << false << false << false;
        QTest::newRow("rotated-pixel") << qreal(31) << false << false << false << false;
        QTest::newRow("rotated-eraser") << qreal(31) << true << true << false << false;
        QTest::newRow("rotated-vector") << qreal(31) << true << false << true << false;
        QTest::newRow("feathered-group") << qreal(31) << true << false << false << true;
    }

    void liveStrokeRedrawsChangedTiles()
    {
        QFETCH(qreal, rotation); QFETCH(bool, smooth); QFETCH(bool, erase); QFETCH(bool, vector);
        QFETCH(bool, selectedGroup);
        auto project = Project::create();
        auto base = RasterLayer::create("Background");
        for (int y = -1; y < 4; ++y) for (int x = -1; x < 6; ++x) {
            QImage tile(tileSize, tileSize, QImage::Format_ARGB32_Premultiplied);
            tile.fill(QColor(80 + x * 12, 130 + y * 10, 160)); base.tiles.insert({x, y}, tile);
        }
        auto drawing = RasterLayer::create("Drawing"); drawing.kind = vector ? LayerKind::Vector : LayerKind::Raster;
        drawing.opacity = .73; drawing.blendMode = LayerBlendMode::Multiply;
        if (erase) {
            for (int y = 0; y < 3; ++y) for (int x = 0; x < 4; ++x) {
                QImage tile(tileSize, tileSize, QImage::Format_ARGB32_Premultiplied);
                tile.fill(QColor(220, 50, 90, 170)); drawing.tiles.insert({x, y}, tile);
            }
        }
        auto clipped = RasterLayer::create("Clipped color"); clipped.clipped = true;
        clipped.opacity = .41; clipped.blendMode = LayerBlendMode::Screen; clipped.tiles = base.tiles;
        project.shots.first().layers = {base, drawing, clipped};
        if (selectedGroup) {
            auto group = RasterLayer::create("Paint group"); group.group = true; group.opacity = .67;
            project.shots.first().layers[1].parentId = group.id;
            project.shots.first().layers[2].parentId = group.id;
            project.shots.first().layers.append(group);
        }
        Canvas canvas; canvas.resize(1000, 800); canvas.setProject(project); canvas.setTool(Canvas::Tool::Hand);
        canvas.document()->selectLayer(1); canvas.setZoom(.73); canvas.setRotation(rotation);
        canvas.centerOn(sheetGeometry(project, project.shots.first()).output.topLeft() + QPointF(440, 300));
        canvas.setCanvasSmoothing(smooth); canvas.show(); canvas.grab();
        if (selectedGroup) {
            QPainterPath selected; selected.addRect({245, 245, 310, 135});
            canvas.document()->setRasterSelection(selected);
            QVERIFY(RasterTools::featherSelection(canvas.document(), 14));
            canvas.setSelectionAnimated(false);
        }
        auto brush = canvas.brush; brush.size = 18; brush.pressureSize = false; brush.spacing = .2;
        brush.scatter = 3; brush.scatterBothAxes = true; brush.opacity = .26;
        brush.texture.enabled = true; brush.texture.scale = 4; brush.texture.pattern = QImage(8, 8, QImage::Format_Grayscale8);
        for (int y = 0; y < 8; ++y) for (int x = 0; x < 8; ++x)
            brush.texture.pattern.scanLine(y)[x] = (x * 71 + y * 37) % 256;
        QVERIFY(canvas.document()->beginStroke({250, 250}, 1, brush, erase)); canvas.grab();
        const quint64 fullPixels = quint64(qCeil(canvas.width() * canvas.devicePixelRatioF()))
            * qCeil(canvas.height() * canvas.devicePixelRatioF());
        for (const QPointF point : {QPointF(270, 270), QPointF(530, 330), QPointF(550, 360)}) {
            const auto painted = canvas.artworkPaintedPixelCount();
            canvas.document()->continueStroke(point, 1);
            const auto partial = canvas.grab().toImage();
            const auto updated = canvas.artworkPaintedPixelCount() - painted;
            QVERIFY(updated > 0);
            if (!smooth) QCOMPARE(updated, fullPixels);
            else QVERIFY(updated < fullPixels);
            canvas.setCanvasSmoothing(!smooth); canvas.setCanvasSmoothing(smooth);
            const auto full = canvas.grab().toImage();
            const auto difference = imageDifference(partial, full);
            // Qt's clipped bilinear span sampler can round a color channel by
            // one unit; a Qt-only drawImage probe reproduces this after 100
            // repaints. Nearest-neighbor views use the exact full
            // redraw fallback because a source-pixel crossing is not bounded.
            const int tolerance = smooth ? 1 : 0;
            if (difference.maximum > tolerance) {
                const QString stem = QString("test-artifacts/partial-stroke-") + QTest::currentDataTag();
                QDir().mkpath("test-artifacts"); partial.save(stem + "-partial.png"); full.save(stem + "-full.png");
                qInfo() << "Partial redraw difference" << difference.average << difference.maximumAt
                    << partial.pixelColor(difference.maximumAt) << full.pixelColor(difference.maximumAt)
                    << "artwork" << canvas.artworkPoint(difference.maximumAt / canvas.devicePixelRatioF());
            }
            QVERIFY2(difference.maximum <= tolerance, qPrintable(QString("Partial redraw differs by %1 at %2,%3")
                .arg(difference.maximum).arg(difference.maximumAt.x()).arg(difference.maximumAt.y())));
            QVERIFY(difference.average < .0001);
        }
        // Repeated dirty updates must not accumulate rounding or paint twice.
        for (int i = 0; i < 40; ++i) {
            canvas.document()->continueStroke({530 + 12 * std::sin(i), 350 + 12 * std::cos(i)}, 1);
            canvas.grab();
        }
        const auto repeated = canvas.grab().toImage();
        canvas.setCanvasSmoothing(!smooth); canvas.setCanvasSmoothing(smooth);
        const auto repeatDifference = imageDifference(repeated, canvas.grab().toImage());
        QVERIFY(repeatDifference.maximum <= (smooth ? 1 : 0));
        QVERIFY(repeatDifference.average < .0001);
        canvas.document()->endStroke();
        const auto finished = canvas.grab().toImage();
        canvas.document()->history()->undo(); canvas.grab();
        canvas.document()->history()->redo(); QCOMPARE(canvas.grab().toImage(), finished);
        QVERIFY(canvas.document()->beginStroke({250, 250}, 1, brush, erase));
        canvas.document()->continueStroke({550, 400}, 1); canvas.grab();
        canvas.document()->cancelStroke(); QCOMPARE(canvas.grab().toImage(), finished);
    }

    void smoothTilesMatchContinuousImage()
    {
        QFETCH(qreal, ratio);
        QFETCH(bool, translucent);
        QFETCH(bool, vector);
        auto layers = tiledArtwork(translucent, vector);
        QVERIFY(!layers.isEmpty());
        QTransform view;
        view.translate(262.37, 211.61);
        view.rotate(17);
        view.scale(.83, .83);
        view.translate(-12.4, -112.8);
        auto verify = [&] {
            const auto actual = paintView(layers, true, view, ratio);
            const auto expected = continuousReference(layers, view, ratio);
            // Stay away from the source image's outer boundary; this area crosses
            // positive/negative tile edges and the four-tile junction at (0, 0).
            const QRect sample(30 * ratio, 30 * ratio, 460 * ratio, 360 * ratio);
            const auto delta = imageDifference(actual, expected, sample);
            samplingDiagnostics(layers, actual, expected, view, ratio, sample);
            QVERIFY2(delta.average < .35, qPrintable(QString("Average channel error %1; maximum %2")
                .arg(delta.average).arg(delta.maximum)));
            // Qt's affine sampler changes fractional weight rounding with the
            // image origin. At abrupt 0/250 jumps we measured isolated deltas
            // of 14–16 away from tile seams; mean error remains below .05/255.
            // Constant-color coverage is checked exactly in the test below.
            QVERIFY2(delta.maximum <= 17, qPrintable(QString("Sampling error %1").arg(delta.maximum)));
        };
        verify();
        // A neighbouring tile changing must invalidate the interpolation gutter.
        auto &tile = layers.first().tiles[{0, 0}];
        QPainter painter(&tile);
        painter.fillRect(QRect(0, 0, 3, tileSize), QColor(255, 0, 255, 210));
        painter.end();
        verify();
    }

    void sharedTileEdgesHaveExactContinuousCoverage()
    {
        QTransform view;
        view.translate(262.37, 211.61);
        view.rotate(17);
        view.scale(.83, .83);
        view.translate(-12.4, -112.8);
        for (const qreal ratio : {1., 2.}) for (const int alpha : {128, 255}) {
            auto layer = RasterLayer::create("Solid coverage");
            const QColor color(40, 150, 230, alpha);
            for (int ty = -1; ty <= 1; ++ty) for (int tx = -2; tx <= 1; ++tx) {
                QImage tile(tileSize, tileSize, QImage::Format_ARGB32_Premultiplied);
                tile.fill(color);
                layer.tiles.insert({tx, ty}, tile);
            }
            const auto actual = paintView({layer}, true, view, ratio);
            const auto expected = qPremultiply(color.rgba());
            const QRect sample(30 * ratio, 30 * ratio, 460 * ratio, 360 * ratio);
            for (int y = sample.top(); y <= sample.bottom(); ++y) {
                for (int x = sample.left(); x <= sample.right(); ++x) {
                    if (actual.pixel(x, y) != expected)
                        QFAIL(qPrintable(QString("Coverage seam at %1,%2 DPR %3 alpha %4")
                            .arg(x).arg(y).arg(ratio).arg(alpha)));
                }
            }
        }
    }

    void smoothGradientHasNoTileBoundaryDiscontinuity()
    {
        auto layer = RasterLayer::create("Continuous gradient");
        for (int ty = -1; ty <= 1; ++ty) for (int tx = -2; tx <= 1; ++tx) {
            QImage tile(tileSize, tileSize, QImage::Format_ARGB32_Premultiplied);
            for (int y = 0; y < tileSize; ++y) for (int x = 0; x < tileSize; ++x) {
                const int ax = tx * tileSize + x + 2 * tileSize;
                const int ay = ty * tileSize + y + tileSize;
                tile.setPixelColor(x, y, QColor(30 + ax / 5, 40 + ay / 4, 100, 180));
            }
            layer.tiles.insert({tx, ty}, tile);
        }
        QTransform view;
        view.translate(262.37, 211.61);
        view.rotate(17);
        view.scale(.83, .83);
        view.translate(-12.4, -112.8);
        for (const qreal ratio : {1., 2.}) {
            const auto actual = paintView({layer}, true, view, ratio);
            const auto expected = continuousReference({layer}, view, ratio);
            const auto delta = imageDifference(actual, expected, QRect(30 * ratio, 30 * ratio, 460 * ratio, 360 * ratio));
            QVERIFY2(delta.maximum <= 2, qPrintable(QString("Gradient seam error %1").arg(delta.maximum)));
            QVERIFY(delta.average < .05);
        }
    }

    void transformPreviewUsesSameSamplingChoice()
    {
        auto layers = tiledArtwork(false, false);
        RasterLayerPreview preview;
        preview.layerId = layers.first().id;
        preview.moving = layers.first();
        preview.transform.translate(9.7, -14.1);
        preview.transform.rotate(-7);
        QTransform view;
        view.translate(260.4, 210.2);
        view.rotate(13);
        view.scale(1.13, 1.13);
        view.translate(-50, -115);
        const auto smooth = paintView(layers, true, view, 2, &preview);
        const auto sharp = paintView(layers, false, view, 2, &preview);
        const auto expected = continuousReference(layers, preview.transform * view, 2);
        const QRect interior(100, 100, 840, 640);
        QVERIFY(imageDifference(smooth, sharp, interior).average > 1);
        const auto delta = imageDifference(smooth, expected, interior);
        samplingDiagnostics(layers, smooth, expected, preview.transform * view, 2, interior);
        QVERIFY2(delta.average < .35, qPrintable(QString("Preview average error %1; maximum %2")
            .arg(delta.average).arg(delta.maximum)));
        QVERIFY(delta.maximum <= 17);
    }

    void isolatedTileEdgesFadeIntoTransparentNeighbours()
    {
        auto layer = RasterLayer::create("Transparent boundary");
        QImage tile(tileSize, tileSize, QImage::Format_ARGB32_Premultiplied);
        tile.fill(QColor(40, 150, 230, 160));
        layer.tiles.insert({0, 0}, tile);
        QTransform view;
        view.translate(180.3, 140.8);
        view.rotate(13);
        view.scale(2.9, 2.9);
        const auto actual = paintView({layer}, true, view, 2);
        const auto expected = continuousReference({layer}, view, 2);
        const auto delta = imageDifference(actual, expected);
        samplingDiagnostics({layer}, actual, expected, view, 2);
        QVERIFY2(delta.average < .05, qPrintable(QString("Transparent edge average error %1; maximum %2")
            .arg(delta.average).arg(delta.maximum)));
        QVERIFY(delta.maximum <= 17);
    }

    void startingTransformPreservesFeatheredLayerComposite()
    {
        auto project = pixelProject();
        auto &layers = project.shots.first().layers;
        auto &base = layers.first();
        base.opacity = .75;
        base.tiles[{0, 0}].fill(QColor(40, 150, 230, 170));
        auto overlay = RasterLayer::create("Clipped shadow");
        overlay.clipped = true;
        overlay.opacity = .6;
        overlay.blendMode = LayerBlendMode::Multiply;
        QImage tile(tileSize, tileSize, QImage::Format_ARGB32_Premultiplied);
        tile.fill(QColor(230, 100, 40, 150));
        overlay.tiles.insert({0, 0}, tile);
        layers.append(overlay);
        Document document;
        document.load(project);
        document.selectLayer(0);
        QPainterPath selection;
        selection.addEllipse(QRectF(50, 60, 140, 110));
        document.setRasterSelection(selection);
        QVERIFY(RasterTools::featherSelection(&document, 9));
        QTransform view;
        view.translate(180.3, 140.8);
        view.rotate(13);
        view.scale(1.7, 1.7);
        for (const bool smooth : {false, true}) {
            const auto before = paintView(document.project().shots.first().layers, smooth, view, 2);
            QVERIFY(document.beginTransform());
            const auto during = paintView(document.project().shots.first().layers, smooth, view, 2, document.transformPreview());
            QCOMPARE(during, before);
            document.cancelTransform();
        }
    }

    void customIdentityPreviewStillDisplaysItsOwnPixels()
    {
        auto layer = RasterLayer::create("Original blue");
        QImage tile(tileSize, tileSize, QImage::Format_ARGB32_Premultiplied);
        tile.fill(Qt::blue);
        layer.tiles.insert({0, 0}, tile);
        RasterLayerPreview preview;
        preview.layerId = layer.id;
        preview.moving = layer;
        preview.moving.tiles[{0, 0}].fill(Qt::red);
        QTransform view;
        view.translate(150, 100);
        const auto original = paintView({layer}, true, view, 1);
        const auto replaced = paintView({layer}, true, view, 1, &preview);
        QCOMPARE(original.pixelColor(200, 150), QColor(Qt::blue));
        QCOMPARE(replaced.pixelColor(200, 150), QColor(Qt::red));
    }

    void togglingDoesNotEditPaintedPixelsHistoryOrExports()
    {
        Canvas canvas;
        canvas.setProject(pixelProject());
        auto *document = canvas.document();
        auto brush = canvas.brush;
        brush.size = 13; brush.color = Qt::red;
        QVERIFY(document->beginStroke({40, 40}, 1, brush, false));
        document->continueStroke({100, 75}, .35);
        document->endStroke();
        QVERIFY(ObjectTools::addLayer(document, LayerKind::Vector));
        QVERIFY(!ObjectTools::addPath(document, {BezierNode::corner({10, 30}),
            BezierNode::corner({180, 145})}, false, Qt::blue, 9).isEmpty());
        document->history()->setClean();
        const auto metadata = canvas.project().toJson();
        const auto frame = renderMovieFrame(canvas.project());
        const auto sheet = renderSheet(canvas.project(), canvas.shot());
        const auto historyIndex = document->history()->index();
        const auto historyCount = document->history()->count();
        QTemporaryDir temp;
        QString error;
        const auto path = temp.filePath("drawing.toonproj");
        QVERIFY2(saveProject(canvas.project(), path, &error), qPrintable(error));
        const auto stored = readFile(path);
        QVERIFY(!stored.isEmpty());
        QSignalSpy documentChanged(document, &Document::changed);
        for (const bool smooth : {false, true, false}) {
            canvas.setCanvasSmoothing(smooth);
            QCOMPARE(canvas.project().toJson(), metadata);
            QCOMPARE(renderMovieFrame(canvas.project()), frame);
            QCOMPARE(renderSheet(canvas.project(), canvas.shot()), sheet);
            QCOMPARE(document->history()->index(), historyIndex);
            QCOMPARE(document->history()->count(), historyCount);
            QVERIFY(document->history()->isClean());
            QVERIFY2(saveProject(canvas.project(), path, &error), qPrintable(error));
            QCOMPARE(readFile(path), stored);
        }
        QCOMPARE(documentChanged.count(), 0);
    }

    void togglingKeepsActiveStrokeAndTransformInProgress()
    {
        Canvas canvas;
        canvas.setProject(pixelProject());
        auto *document = canvas.document();
        Document control;
        control.load(canvas.project());
        auto brush = canvas.brush;
        brush.size = 21; brush.color = QColor(220, 80, 30);
        for (auto *target : {document, &control}) {
            QVERIFY(target->beginStroke({30, 30}, .3, brush, false));
            target->continueStroke({60, 50}, .8);
        }
        const int beforeStroke = document->history()->index();
        canvas.setCanvasSmoothing(false);
        QVERIFY(document->strokeActive());
        QCOMPARE(document->history()->index(), beforeStroke);
        for (auto *target : {document, &control}) target->continueStroke({110, 80}, .5);
        canvas.setCanvasSmoothing(true);
        QVERIFY(document->strokeActive());
        QCOMPARE(document->history()->index(), beforeStroke);
        for (auto *target : {document, &control}) target->endStroke();
        QCOMPARE(renderMovieFrame(canvas.project()), renderMovieFrame(control.project()));
        QCOMPARE(document->history()->index(), beforeStroke + 1);

        document->selectAllPixels();
        QVERIFY(document->beginTransform());
        TransformParameters parameters;
        parameters.translation = {16.3, -8.4};
        parameters.rotation = 13;
        parameters.scaleX = .7; parameters.scaleY = 1.1;
        QVERIFY(document->previewTransform(parameters));
        const auto transform = document->transformMatrix();
        const auto frame = renderMovieFrame(canvas.project());
        const int beforeTransform = document->history()->index();
        for (const bool smooth : {false, true, false}) {
            canvas.setCanvasSmoothing(smooth);
            QVERIFY(document->transformActive());
            QCOMPARE(document->transformMatrix(), transform);
            QCOMPARE(document->history()->index(), beforeTransform);
            QCOMPARE(renderMovieFrame(canvas.project()), frame);
        }
        document->cancelTransform();
        QVERIFY(!document->transformActive());
        QCOMPARE(document->history()->index(), beforeTransform);
    }

    void headerActionAndWorkspacePreferenceStaySynchronized()
    {
        QTemporaryDir temp;
        const auto settingsPath = temp.filePath("workspace.ini");
        const auto projectPath = temp.filePath("drawing.toonproj");
        QString error;
        QVERIFY(saveProject(pixelProject(), projectPath, &error));
        {
            MainWindow window(settingsPath);
            QVERIFY(window.openProject(projectPath));
            window.show();
            QCoreApplication::processEvents();
            auto *action = window.findChild<QAction *>("canvasSmoothingAction");
            auto *toolbar = window.findChild<QToolBar *>("projectToolbar");
            QVERIFY(action && toolbar);
            QVERIFY(action->isCheckable());
            QVERIFY(action->isChecked());
            QVERIFY(toolbar->actions().contains(action));
            bool inMenu = false;
            for (auto *menu : window.findChildren<QMenu *>()) inMenu |= menu->actions().contains(action);
            QVERIFY(inMenu);
            auto *button = qobject_cast<QToolButton *>(toolbar->widgetForAction(action));
            QVERIFY(button);
            button->ensurePolished();
            QVERIFY(!button->accessibleName().isEmpty());
            QVERIFY(!button->toolTip().isEmpty());
            QCOMPARE(button->style()->styleHint(QStyle::SH_ToolTip_WakeUpDelay, nullptr, button), 600);
            QTest::mouseClick(button, Qt::LeftButton);
            QVERIFY(!action->isChecked());
            QVERIFY(!window.canvas()->canvasSmoothing());
            window.canvas()->setTool(Canvas::Tool::Eraser);
            window.canvas()->document()->selectLayer(0);
            QVERIFY(!window.canvas()->canvasSmoothing());
            QVERIFY(window.openProject(projectPath));
            QVERIFY(!window.canvas()->canvasSmoothing());
            window.canvas()->setCanvasSmoothing(true);
            QVERIFY(action->isChecked());
            window.canvas()->setCanvasSmoothing(false);
            QVERIFY(!action->isChecked());
            QVERIFY(window.close());
        }
        QSettings settings(settingsPath, QSettings::IniFormat);
        QCOMPARE(settings.value("view/canvasSmoothing").toBool(), false);
        {
            MainWindow reopened(settingsPath);
            QVERIFY(!reopened.canvas()->canvasSmoothing());
            auto *action = reopened.findChild<QAction *>("canvasSmoothingAction");
            QVERIFY(action && !action->isChecked());
            QVERIFY(reopened.openProject(projectPath));
            QVERIFY(!reopened.canvas()->canvasSmoothing());
            action->trigger();
            QVERIFY(reopened.canvas()->canvasSmoothing());
            QVERIFY(reopened.close());
        }
        settings.sync();
        QCOMPARE(settings.value("view/canvasSmoothing").toBool(), true);
    }

    void optionalWorkspaceVisuals()
    {
        const auto directory = qEnvironmentVariable("TOON_UI_PREVIEW");
        if (directory.isEmpty()) return;
        QVERIFY(QDir().mkpath(directory));
        QTemporaryDir temp;
        const auto path = temp.filePath("pixel-pattern.toonproj");
        auto project = pixelProject();
        project.movie = "Canvas smoothing";
        project.shots.first().name = "Display sampling comparison";
        // Sharp pixel-art curves make the effect apparent alongside the grid.
        auto &tile = project.shots.first().layers.first().tiles[{0, 0}];
        QPainter painter(&tile);
        painter.setPen(QPen(QColor(30, 145, 240), 5));
        painter.setBrush(QColor(240, 120, 40));
        painter.drawEllipse(QRect(70, 70, 105, 105));
        painter.end();
        QString error;
        QVERIFY(saveProject(project, path, &error));
        MainWindow window(temp.filePath("workspace.ini"));
        QVERIFY(window.openProject(path));
        window.show();
        QCoreApplication::processEvents();
        window.canvas()->setTool(Canvas::Tool::Hand);
        window.canvas()->setZoom(3.3);
        window.canvas()->setRotation(11);
        window.canvas()->centerOn(sheetGeometry(project, project.shots.first()).output.center());
        QVERIFY(window.grab().save(directory + "/canvas-smoothing-on.png"));
        window.canvas()->setCanvasSmoothing(false);
        QVERIFY(window.grab().save(directory + "/canvas-smoothing-off.png"));
        QVERIFY(window.close());
    }
};

QTEST_MAIN(CanvasSmoothingTest)
#include "test_canvas_smoothing.moc"
