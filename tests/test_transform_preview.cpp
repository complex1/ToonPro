#include "raster.h"
#include "raster_edit.h"
#include "sheet.h"
#include "canvas.h"
#include <QDir>
#include <QElapsedTimer>
#include <QtTest>

using namespace toon;

namespace {
QImage tile(QColor color = Qt::transparent) {
    QImage image(tileSize, tileSize, QImage::Format_ARGB32_Premultiplied); image.fill(color); return image;
}
RasterLayer rectangle(QString name, QRect rect, QColor color) {
    auto layer = RasterLayer::create(name); auto image = tile();
    QPainter painter(&image); painter.fillRect(rect, color); painter.end(); layer.tiles[{0, 0}] = image;
    return layer;
}
QImage render(const QVector<RasterLayer> &layers, bool annotations, const RasterLayerPreview *preview = nullptr,
              QSize size = {320, 280}, QTransform view = {}, qreal dpr = 1, QRectF clip = {}) {
    QImage image(QSize(qRound(size.width() * dpr), qRound(size.height() * dpr)), QImage::Format_ARGB32_Premultiplied);
    image.setDevicePixelRatio(dpr); image.fill(Qt::transparent);
    QPainter painter(&image); painter.setTransform(view);
    if (!clip.isEmpty()) painter.setClipRect(clip);
    paintLayers(painter, layers, annotations, preview); return image;
}
QVector<RasterLayer> character() {
    auto group = RasterLayer::create("Character"); group.group = true; group.opacity = .6;
    auto base = rectangle("Base", {30, 30, 40, 40}, Qt::red); base.parentId = group.id;
    auto second = rectangle("Stationary", {110, 30, 40, 40}, Qt::blue);
    { QPainter painter(&base.tiles[{0, 0}]); painter.drawImage(QPoint(), second.tiles[{0, 0}]); }
    auto shade = rectangle("Shadow", {0, 0, 240, 240}, QColor(0, 160, 70, 180));
    shade.parentId = group.id; shade.clipped = true; shade.opacity = .7;
    return {base, shade, group};
}
RasterLayerPreview movingPatch(const RasterLayer &layer, const QRect &selection, QPointF translation) {
    RasterLayerPreview preview;
    preview.layerId = layer.id; preview.stationary = layer; preview.moving = layer;
    preview.moving.tiles.clear();
    auto moving = tile();
    { QPainter painter(&moving); painter.setClipRect(selection); painter.drawImage(QPoint(), layer.tiles[{0, 0}]); }
    preview.moving.tiles[{0, 0}] = moving;
    { QPainter painter(&preview.stationary.tiles[{0, 0}]); painter.setCompositionMode(QPainter::CompositionMode_Clear); painter.fillRect(selection, Qt::transparent); }
    preview.transform.translate(translation.x(), translation.y());
    return preview;
}
}

class TransformPreviewTest : public QObject {
    Q_OBJECT
private slots:
    void sheetPreviewShowsOffPaperPixelsUntilApply_data() {
        QTest::addColumn<qreal>("ratio");
        QTest::newRow("standard") << qreal(1); QTest::newRow("retina") << qreal(2);
    }
    void sheetPreviewShowsOffPaperPixelsUntilApply() {
        QFETCH(qreal, ratio);
        auto project = Project::create(); project.width = project.height = 128;
        auto &shot = project.shots[0]; shot.sheet.overscan = 0;
        shot.sheet.left = shot.sheet.right = shot.sheet.top = shot.sheet.bottom = 10;
        shot.layers = {rectangle("Moving", {20, 20, 30, 30}, Qt::red)};
        Document document; document.load(project); QVERIFY(document.beginTransform());
        TransformParameters movement; movement.translation = {145, 0};
        QVERIFY(document.previewTransform(movement));
        const QPointF offset(30, 25); const auto geometry = sheetGeometry(project, shot);
        const QPointF sample = offset + geometry.output.topLeft() + QPointF(175, 35);
        QVERIFY(!geometry.paper.translated(offset).contains(sample));
        const auto renderPreview = [&](const RasterLayerPreview *preview) {
            QImage image(QSize(500 * ratio, 600 * ratio), QImage::Format_ARGB32_Premultiplied);
            image.setDevicePixelRatio(ratio); image.fill(QColor("#202020"));
            QPainter painter(&image); painter.translate(offset);
            paintSheet(painter, document.project(), document.project().shots[0], false, preview, true);
            return image;
        };
        const auto pixel = (sample * ratio).toPoint();
        QCOMPARE(renderPreview(document.transformPreview()).pixelColor(pixel), QColor(Qt::red));
        QCOMPARE(renderPreview(nullptr).pixelColor(pixel), QColor("#202020"));
        QCOMPARE(renderMovieFrame(document.project()).pixelColor(25, 25), QColor(Qt::red));
        QVERIFY(document.applyTransform());
        QCOMPARE(renderPreview(nullptr).pixelColor(pixel), QColor("#202020"));
        QVERIFY(document.project().shots[0].layers[0].tiles.isEmpty());
        document.history()->undo(); QCOMPARE(renderMovieFrame(document.project()).pixelColor(25, 25), QColor(Qt::red));
    }
    void transformedBaseAndClippedOverlayKeepGroupCompositing_data() {
        QTest::addColumn<int>("index"); QTest::newRow("base") << 0; QTest::newRow("clipped overlay") << 1;
    }
    void transformedBaseAndClippedOverlayKeepGroupCompositing() {
        QFETCH(int, index);
        const auto layers = character();
        const QRect selection = index == 0 ? QRect(25, 25, 50, 50) : QRect(10, 10, 100, 100);
        const auto preview = movingPatch(layers[index], selection, {40, 60});
        auto expected = layers; QString error; QPainterPath path; path.addRect(selection);
        QVERIFY2(transformRasterLayer(layers[index], &path, preview.transform, {0, 0, 320, 280}, 10,
                                    &expected[index], &error), qPrintable(error));
        QCOMPARE(render(layers, true, &preview), render(expected, true));
        // Rendering never mutates the authoritative raster tiles.
        QCOMPARE(render(layers, true), render(characterReplacement(layers), true));
    }
    void notesAndParentVisibilityStillGatePreview() {
        auto layers = character(); const auto preview = movingPatch(layers[0], {25, 25, 50, 50}, {40, 60});
        layers[2].annotation = true;
        const auto movie = render(layers, false, &preview), sheet = render(layers, true, &preview);
        QCOMPARE(movie.pixelColor(90, 110).alpha(), 0); QVERIFY(sheet.pixelColor(90, 110).alpha() > 0);
        layers[2].visible = false; QCOMPARE(render(layers, true, &preview), movie);
        layers[2].visible = true; layers[0].visible = false;
        QCOMPARE(render(layers, true, &preview), movie); // A hidden clipping base hides its clipped run.
    }
    void retinaRotationAndOffsetClipMatchCommittedArtwork() {
        auto layer = rectangle("Patch", {30, 30, 90, 70}, QColor(40, 100, 180, 180));
        auto preview = movingPatch(layer, {20, 20, 110, 90}, {15, 20});
        auto expected = layer; QString error;
        QVERIFY(transformRasterLayer(layer, nullptr, preview.transform, {0, 0, 400, 400}, 10, &expected, &error));
        QTransform view; view.translate(93.5, 37.5); view.rotate(31); view.scale(.8, .8);
        const QRectF clip(42, 38, 110, 90);
        const auto actual = render({layer}, true, &preview, {340, 300}, view, 2, clip);
        const auto committed = render({expected}, true, nullptr, {340, 300}, view, 2, clip);
        QCOMPARE(actual.devicePixelRatio(), 2);
        QImage direct(actual.size(), QImage::Format_ARGB32_Premultiplied); direct.setDevicePixelRatio(2); direct.fill(Qt::transparent);
        { QPainter painter(&direct); painter.setTransform(view); painter.setClipRect(clip);
          painter.setTransform(preview.transform, true); painter.drawImage(QPoint(), layer.tiles[{0, 0}]); }
        QCOMPARE(actual, direct);
        QCOMPARE(actual, committed);
    }
    void movingTileEdgesHaveNoRotatedSeams() {
        auto layer = RasterLayer::create("Dense block");
        for (int y = 0; y < 2; ++y) for (int x = 0; x < 2; ++x) layer.tiles[{x, y}] = tile(Qt::blue);
        RasterLayerPreview preview{layer.id, RasterLayer::create(), layer, {}};
        preview.transform.translate(256.25, 255.75); preview.transform.rotate(31); preview.transform.translate(-256, -256);
        QTransform view; view.translate(90, 70); view.scale(.7, .7);
        const auto image = render({layer}, true, &preview, {650, 650}, view, 2);
        const auto mapping = preview.transform * view;
        for (int along = 12; along < 500; ++along) for (int across = -2; across <= 2; ++across) {
            const QPoint horizontal = (mapping.map(QPointF(along, 256 + across)) * 2).toPoint();
            const QPoint vertical = (mapping.map(QPointF(256 + across, along)) * 2).toPoint();
            QCOMPARE(image.pixelColor(horizontal), QColor(Qt::blue)); QCOMPARE(image.pixelColor(vertical), QColor(Qt::blue));
        }
    }
    void normalCompositeCacheTracksPixelsAndLayerFlags() {
        auto layers = character();
        const auto first = render(layers, true); QCOMPARE(render(layers, true), first);
        layers[1].tiles[{0, 0}].fill(Qt::yellow);
        const auto recolored = render(layers, true); QVERIFY(recolored != first);
        layers[2].opacity = .25;
        const auto faded = render(layers, true); QVERIFY(faded.pixelColor(45, 45).alpha() < recolored.pixelColor(45, 45).alpha());
        layers[1].clipped = false;
        const auto unclipped = render(layers, true); QVERIFY(unclipped.pixelColor(180, 180).alpha() > 0);
        layers[2].visible = false;
        QCOMPARE(render(layers, true).pixelColor(45, 45).alpha(), 0);
        layers[2].visible = true; layers[2].annotation = true;
        QCOMPARE(render(layers, false).pixelColor(45, 45).alpha(), 0);
        QCOMPARE(render(layers, true), unclipped);
    }
    void fourKPreviewRenderingIsBoundedByViewport() {
        auto base = RasterLayer::create("4K base");
        for (int y = 0; y < 16; ++y) for (int x = 0; x < 16; ++x)
            base.tiles[{x, y}] = tile(QColor(40 + x * 8, 70 + y * 8, 150 + (x + y) % 6 * 12));
        auto group = RasterLayer::create("Character"); group.group = true; group.opacity = .8; base.parentId = group.id;
        auto overlay = base; overlay.id = RasterLayer::create().id; overlay.clipped = true; overlay.opacity = .2;
        RasterLayerPreview preview{base.id, RasterLayer::create(), base, {}};
        QTransform view; view.translate(150, 50); view.scale(.16, .16);
        const QVector<RasterLayer> layers{base, overlay, group};
        const auto sourceKey = base.tiles.first().cacheKey();
        QElapsedTimer timer; timer.start();
        QImage result;
        constexpr int frames = 12;
        for (int frame = 0; frame < frames; ++frame) {
            preview.transform = {}; preview.transform.translate(2048 + frame * 3, 2048);
            preview.transform.rotate(frame); preview.transform.translate(-2048, -2048);
            result = render(layers, true, &preview, {1100, 800}, view);
        }
        const qreal milliseconds = timer.nsecsElapsed() / 1000000.0;
        qInfo("4K layered transform preview: %.2f ms/frame at 1100 x 800", milliseconds / frames);
        QVERIFY(!result.isNull()); QVERIFY(result.pixelColor(500, 400).alpha() > 0);
        QCOMPARE(base.tiles.first().cacheKey(), sourceKey);
        auto project = Project::create(); project.width = project.height = 4096;
        project.shots[0].sheet.overscan = 512; project.shots[0].layers = layers;
        Canvas canvas; canvas.resize(1100, 800); canvas.setProject(project); canvas.show();
        canvas.setTool(Canvas::Tool::Transform); QVERIFY(canvas.document()->transformActive());
        TransformParameters parameters; parameters.rotation = 11; parameters.translation = {33, 0};
        QVERIFY(canvas.document()->previewTransform(parameters));
        QDir().mkpath("test-artifacts"); QVERIFY(canvas.grab().save("test-artifacts/transform-performance.png"));
    }
private:
    static QVector<RasterLayer> characterReplacement(const QVector<RasterLayer> &layers) {
        // Reconstruct the authoritative base for an independent mutation check.
        auto expected = layers;
        const auto rebuilt = character();
        for (int i = 0; i < 2; ++i) expected[i].tiles = rebuilt[i].tiles;
        return expected;
    }
};

QTEST_MAIN(TransformPreviewTest)
#include "test_transform_preview.moc"
