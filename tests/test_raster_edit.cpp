#include "raster_edit.h"
#include "brush_engine.h"
#include <QtTest>
#include <QElapsedTimer>
#include <limits>
#include <cmath>

using namespace toon;
namespace {
int coordinate(int x) { return int(std::floor(x / qreal(tileSize))); }
void pixel(RasterLayer &layer, int x, int y, QRgb color) {
    const TileKey key{coordinate(x), coordinate(y)};
    if (!layer.tiles.contains(key)) {
        QImage image(tileSize, tileSize, QImage::Format_ARGB32_Premultiplied); image.fill(Qt::transparent); layer.tiles.insert(key, image);
    }
    layer.tiles[key].setPixel(x - key.first * tileSize, y - key.second * tileSize, color);
}
QRgb pixel(const RasterLayer &layer, int x, int y) {
    const TileKey key{coordinate(x), coordinate(y)}; const auto found = layer.tiles.constFind(key);
    return found == layer.tiles.cend() ? 0 : found->pixel(x - key.first * tileSize, y - key.second * tileSize);
}
QPainterPath rectangle(QRectF rect) { QPainterPath path; path.addRect(rect); return path; }
RasterLayer patch(QPoint origin) {
    auto layer = RasterLayer::create("Patch");
    for (int y = 0; y < 32; ++y) for (int x = 0; x < 32; ++x) {
        const int alpha = (x * 7 + y * 3) % 254 + 1;
        pixel(layer, origin.x() + x, origin.y() + y, qPremultiply(qRgba((x * 19) % 256, (y * 11) % 256, 170, alpha)));
    }
    return layer;
}
BrushDab stamp(QColor color, qreal opacity = 1) {
    BrushDab dab; dab.color = color; dab.opacity = opacity;
    dab.mask = QImage(32, 32, QImage::Format_Grayscale8); dab.mask.fill(255); return dab;
}
QImage referenceSelectionMask(const QPainterPath &path, TileKey key) {
    QImage image(tileSize, tileSize, QImage::Format_ARGB32_Premultiplied); image.fill(Qt::black);
    QPainter painter(&image); painter.translate(-key.first * tileSize, -key.second * tileSize);
    painter.setPen(Qt::NoPen); painter.setBrush(Qt::white); painter.drawPath(path); painter.end();
    return image.convertToFormat(QImage::Format_Grayscale8);
}
const QRectF drawing(-1024, -1024, 4096, 4096);
}
class RasterEditTest : public QObject {
    Q_OBJECT
private slots:
    void selectionCacheMatchesHardCoverageForCurvesHolesAndBoundaries() {
        QVector<QPainterPath> paths;
        paths << rectangle({-256, -256, 768, 768}) << rectangle({-3.25, -.5, 261.5, 257.5})
              << rectangle({.499, .499, .002, .002});
        QPainterPath ellipse; ellipse.addEllipse({-200.25, -180.5, 640.5, 720.25}); paths << ellipse;
        auto hole = rectangle({-512, -512, 1024, 1024}); hole.addEllipse({-30, -10, 350, 480}); paths << hole;
        QPainterPath curve; curve.moveTo(-260, 260); curve.cubicTo(120, -420, 110, 530, 490, -80); curve.lineTo(360, 300); curve.closeSubpath(); paths << curve;
        for (const auto &path : paths) {
            RasterSelectionCache cache(&path);
            for (const TileKey key : {TileKey{-1, -1}, TileKey{-1, 0}, TileKey{0, 0}, TileKey{1, 1}}) {
                const auto tile = cache.tile(key);
                QVERIFY(tile.coverage != RasterSelectionTile::Coverage::Invalid);
                auto image = tile.mask;
                if (image.isNull()) {
                    image = QImage(tileSize, tileSize, QImage::Format_Grayscale8);
                    image.fill(tile.coverage == RasterSelectionTile::Coverage::Full ? 255 : 0);
                }
                QCOMPARE(image, referenceSelectionMask(path, key));
                QCOMPARE(rasterSelectionMask(path, key), image);
            }
        }
    }
    void selectionCacheReusesBoundariesAndBoundsMemory() {
        QPainterPath path; path.addEllipse({-1200, -1200, 2400, 2400}); RasterSelectionCache cache(&path);
        const auto full = cache.tile({0, 0}), empty = cache.tile({10, 10});
        QCOMPARE(full.coverage, RasterSelectionTile::Coverage::Full); QVERIFY(full.mask.isNull());
        QCOMPARE(empty.coverage, RasterSelectionTile::Coverage::Empty); QVERIFY(empty.mask.isNull());
        const auto boundary = cache.tile({-5, 0}); QCOMPARE(boundary.coverage, RasterSelectionTile::Coverage::Partial);
        for (int i = 0; i < 1000; ++i) QCOMPARE(cache.tile({-5, 0}).mask.cacheKey(), boundary.mask.cacheKey());
        cache.reset(&path); QCOMPARE(cache.tile({-5, 0}).mask.cacheKey(), boundary.mask.cacheKey());
        path.translate(15, 0); // Cache owns a snapshot until explicitly reset.
        QCOMPARE(cache.tile({-5, 0}).mask.cacheKey(), boundary.mask.cacheKey());
        cache.reset(&path); QVERIFY(cache.tile({-5, 0}).mask != boundary.mask);
        QPainterPath emptyPath; cache.reset(&emptyPath); QCOMPARE(cache.tile({0, 0}).coverage, RasterSelectionTile::Coverage::Empty);
        cache.reset(); QCOMPARE(cache.tile({0, 0}).coverage, RasterSelectionTile::Coverage::Full);
        QPainterPath striped;
        for (int i = 0; i < 140; ++i) striped.addRect(i * 256, 0, 256, 128);
        cache.reset(&striped);
        for (int i = 0; i < 140; ++i) {
            QCOMPARE(cache.tile({i, 0}).coverage, RasterSelectionTile::Coverage::Partial);
            QVERIFY(cache.cachedBytes() <= RasterSelectionCache::maxCachedBytes);
        }
    }
    void cachedSelectionBrushMatchesUncachedWithOpacityAndEraser() {
        QPainterPath selection; selection.addEllipse({230, 80, 52, 42}); selection.addRect({250, 95, 8, 10});
        RasterSelectionCache cache(&selection);
        auto original = RasterLayer::create(), cached = original, uncached = original;
        RasterLayer cachedAccumulation, uncachedAccumulation; auto dab = stamp(QColor(40, 100, 180), .45); dab.flow = .2;
        for (int i = 0; i < 40; ++i) {
            const QPointF point(244 + i * .6, 100 + (i % 3) * .25);
            QVERIFY(applyBrushDab(cached, cachedAccumulation, original, dab, point, {}, BrushBlend::Normal, false, drawing, 8, 1, &selection, &cache));
            QVERIFY(applyBrushDab(uncached, uncachedAccumulation, original, dab, point, {}, BrushBlend::Normal, false, drawing, 8, 1, &selection));
        }
        QCOMPARE(cached.tiles, uncached.tiles);
        original = cached; cachedAccumulation.tiles.clear(); uncachedAccumulation.tiles.clear(); dab.opacity = .7;
        QVERIFY(applyBrushDab(cached, cachedAccumulation, original, dab, {256, 100}, {}, BrushBlend::Normal, true, drawing, 8, 1, &selection, &cache));
        QVERIFY(applyBrushDab(uncached, uncachedAccumulation, original, dab, {256, 100}, {}, BrushBlend::Normal, true, drawing, 8, 1, &selection));
        QCOMPARE(cached.tiles, uncached.tiles);
    }
    void selectionMaskWorkMeasurement() {
        const auto path = rectangle({-100, -100, 4096, 4096});
        RasterSelectionCache cache(&path); QCOMPARE(cache.tile({1, 1}).coverage, RasterSelectionTile::Coverage::Full);
        constexpr int repetitions = 1000; QElapsedTimer timer; quint64 checksum = 0;
        timer.start();
        for (int i = 0; i < repetitions; ++i) checksum += referenceSelectionMask(path, {1, 1}).constScanLine(10)[10];
        const qint64 oldNanoseconds = timer.nsecsElapsed(); timer.restart();
        for (int i = 0; i < repetitions; ++i) checksum += cache.tile({1, 1}).coverage == RasterSelectionTile::Coverage::Full ? 255 : 0;
        const qint64 cachedNanoseconds = timer.nsecsElapsed(); QCOMPARE(checksum, quint64(repetitions * 510));
        qInfo("Selection coverage (%d repeated dabs): previous mask work %.3f ms; cached full tiles %.3f ms", repetitions,
              oldNanoseconds / 1000000.0, cachedNanoseconds / 1000000.0);
    }
    void hardSelectionUsesSameCoverageAtNegativeTileEdges() {
        auto path = rectangle({-3.25, -1.25, 6.5, 4.5});
        QImage dense(520, 520, QImage::Format_ARGB32_Premultiplied); dense.fill(Qt::black);
        { QPainter painter(&dense); painter.translate(260, 260); painter.setPen(Qt::NoPen); painter.setBrush(Qt::white); painter.drawPath(path); }
        for (const TileKey key : {TileKey{-1, -1}, TileKey{0, -1}, TileKey{-1, 0}, TileKey{0, 0}}) {
            const auto mask = rasterSelectionMask(path, key); QCOMPARE(mask.format(), QImage::Format_Grayscale8);
            for (int y = 0; y < 256; ++y) for (int x = 0; x < 256; ++x) {
                const int value = mask.constScanLine(y)[x]; QVERIFY(value == 0 || value == 255);
                QCOMPARE(value, qRed(dense.pixel(key.first * 256 + x + 260, key.second * 256 + y + 260)));
            }
        }
        const auto empty = rasterSelectionMask({}, {0, 0}); QCOMPARE(empty.constScanLine(20)[20], uchar(0));
    }
    void selectedBoundsUsePixelsNotSelectionRectangle() {
        auto layer = RasterLayer::create(); pixel(layer, -1, 255, qRgba(10, 20, 30, 80)); pixel(layer, 2, 257, qRgba(100, 20, 30, 120));
        QCOMPARE(selectedPixelBounds(layer), QRectF(-1, 255, 4, 3));
        auto selection = rectangle({0, 0, 100, 1000}); QCOMPARE(selectedPixelBounds(layer, &selection), QRectF(2, 257, 1, 1));
        QPainterPath empty; QVERIFY(selectedPixelBounds(layer, &empty).isEmpty());
        layer.group = true; QVERIFY(selectedPixelBounds(layer).isEmpty());
    }
    void translationCutsSelectedPixelsAndPreservesUnselectedPixels() {
        auto layer = RasterLayer::create("Original");
        const QRgb translucent = qPremultiply(qRgba(40, 80, 160, 128)), red = qRgb(255, 0, 0), untouched = qPremultiply(qRgba(20, 80, 10, 101));
        pixel(layer, -1, 255, translucent); pixel(layer, 2, 258, red); pixel(layer, 11, 260, red); pixel(layer, 200, 200, untouched);
        auto selection = rectangle({-2, 254, 6, 6}); RasterLayer output; QString error;
        QTransform transform; transform.translate(12, 5);
        QVERIFY2(transformRasterLayer(layer, &selection, transform, drawing, 10, &output, &error), qPrintable(error));
        QCOMPARE(pixel(output, -1, 255), QRgb(0)); QCOMPARE(pixel(output, 2, 258), QRgb(0));
        QCOMPARE(pixel(output, 14, 263), red); QCOMPARE(pixel(output, 200, 200), untouched);
        const auto merged = QColor::fromRgba(qUnpremultiply(pixel(output, 11, 260)));
        QCOMPARE(merged.alpha(), 255); QVERIFY(qAbs(merged.red() - 147) <= 1); QVERIFY(qAbs(merged.blue() - 80) <= 1);
        QCOMPARE(pixel(layer, -1, 255), translucent); QCOMPARE(output.id, layer.id);
    }
    void wholeLayerIntegerTranslationIsPixelExact() {
        auto layer = patch({-16, 244}); auto output = RasterLayer::create(); QString error;
        QTransform transform; transform.translate(300, -260);
        QVERIFY2(transformRasterLayer(layer, nullptr, transform, drawing, 10, &output, &error), qPrintable(error));
        for (int y = -2; y < 34; ++y) for (int x = -2; x < 34; ++x)
            QCOMPARE(pixel(output, 284 + x, -16 + y), pixel(layer, -16 + x, 244 + y));
        QCOMPARE(selectedPixelBounds(output), QRectF(284, -16, 32, 32));
    }
    void integerTranslationMatchesSourceOverRounding() {
        auto source = RasterLayer::create();
        for (int y = 0; y < 32; ++y) for (int x = 0; x < 32; ++x) {
            pixel(source, x, y, qPremultiply(qRgba(x * 7, y * 7, 180, (x * 5 + y * 7) % 255 + 1)));
            pixel(source, 300 + x, y, qPremultiply(qRgba(140, x * 7, y * 7, (x * 3 + y * 11) % 255 + 1)));
        }
        auto selection = rectangle({0, 0, 32, 32}); QTransform transform; transform.translate(300, 0);
        RasterLayer output; QString error;
        QVERIFY2(transformRasterLayer(source, &selection, transform, drawing, 8, &output, &error), qPrintable(error));
        for (int y = 0; y < 32; ++y) for (int x = 0; x < 32; ++x) {
            const auto s = pixel(source, x, y), d = pixel(source, 300 + x, y);
            const auto channel = [s](int source, int destination) { return source + (destination * (255 - qAlpha(s)) + 127) / 255; };
            const auto expected = qRgba(channel(qRed(s), qRed(d)), channel(qGreen(s), qGreen(d)),
                                       channel(qBlue(s), qBlue(d)), channel(qAlpha(s), qAlpha(d)));
            QCOMPARE(pixel(output, 300 + x, y), expected); QCOMPARE(pixel(output, x, y), QRgb(0));
        }
    }
    void sparseRotatedRowsInsideBoundsAreAccepted() {
        auto source = RasterLayer::create(); pixel(source, 0, 20, qRgb(200, 100, 30)); pixel(source, 20, 0, qRgb(200, 100, 30));
        TransformParameters parameters; parameters.rotation = 45;
        const auto transform = rasterTransform(selectedPixelBounds(source), parameters);
        const QRectF extent(-6, 8, 33, 6);
        QVERIFY(!extent.contains(transform.mapRect(selectedPixelBounds(source))));
        RasterLayer output; QString error;
        QVERIFY2(transformRasterLayer(source, nullptr, transform, extent, 8, &output, &error), qPrintable(error));
        QVERIFY(!selectedPixelBounds(output).isEmpty()); QVERIFY(extent.contains(selectedPixelBounds(output)));
    }
    void fullHdIntegerMoveWorkMeasurement() {
        auto source = RasterLayer::create();
        for (int ty = 0; ty < 5; ++ty) for (int tx = 0; tx < 8; ++tx) {
            QImage image(tileSize, tileSize, QImage::Format_ARGB32_Premultiplied); image.fill(Qt::transparent);
            QPainter painter(&image); painter.translate(-tx * tileSize, -ty * tileSize);
            painter.fillRect(QRect(0, 0, 1920, 1080), QColor(80, 140, 200, 180)); painter.end();
            source.tiles.insert({tx, ty}, image);
        }
        QTransform transform; transform.translate(13, 17); RasterLayer output; QString error;
        const auto selection = rectangle({0, 0, 1920, 1080}); QElapsedTimer timer; timer.start();
        QVERIFY2(transformRasterLayer(source, &selection, transform, {-256, -256, 2560, 1800}, 100, &output, &error), qPrintable(error));
        const qint64 elapsed = timer.nsecsElapsed(); QCOMPARE(selectedPixelBounds(output), QRectF(13, 17, 1920, 1080));
        QCOMPARE(pixel(output, 13, 17), pixel(source, 0, 0)); QCOMPARE(pixel(output, 1932, 1096), pixel(source, 1919, 1079));
        qInfo("1920x1080 integer selection move Apply: %.3f ms", elapsed / 1000000.0);
    }
    void emptySelectionAndIdentityRetainExactTiles() {
        auto layer = patch({10, 10}); pixel(layer, 800, 800, 0); // retained empty tile is intentional
        auto empty = QPainterPath(); auto output = RasterLayer::create(); QString error;
        QTransform move; move.translate(400, 400);
        QVERIFY(transformRasterLayer(layer, &empty, move, drawing, 10, &output, &error)); QCOMPARE(output.tiles, layer.tiles);
        QVERIFY(transformRasterLayer(layer, nullptr, {}, drawing, 10, &output, &error)); QCOMPARE(output.tiles, layer.tiles);
        auto outside = rectangle({-300, -300, 50, 50});
        QVERIFY(transformRasterLayer(layer, &outside, move, drawing, 10, &output, &error)); QCOMPARE(output.tiles, layer.tiles);
    }
    void rotationAndScaleSampleAcrossPositiveAndNegativeTiles() {
        const auto crossing = patch({240, -16}), single = patch({40, 40});
        TransformParameters parameters; parameters.scaleX = 1.5; parameters.scaleY = .8; parameters.rotation = 31;
        auto first = RasterLayer::create(), second = RasterLayer::create(); QString error;
        auto transform = rasterTransform(selectedPixelBounds(crossing), parameters);
        QVERIFY2(transformRasterLayer(crossing, nullptr, transform, drawing, 16, &first, &error), qPrintable(error));
        parameters.translation = {200, -56}; transform = rasterTransform(selectedPixelBounds(single), parameters);
        QVERIFY2(transformRasterLayer(single, nullptr, transform, drawing, 16, &second, &error), qPrintable(error));
        for (int y = -60; y < 60; ++y) for (int x = 200; x < 310; ++x) {
            const auto a = pixel(first, x, y), b = pixel(second, x, y);
            QVERIFY(qAbs(qRed(a) - qRed(b)) <= 1); QVERIFY(qAbs(qGreen(a) - qGreen(b)) <= 1);
            QVERIFY(qAbs(qBlue(a) - qBlue(b)) <= 1); QVERIFY(qAbs(qAlpha(a) - qAlpha(b)) <= 1);
            QVERIFY(qRed(a) <= qAlpha(a)); QVERIFY(qGreen(a) <= qAlpha(a)); QVERIFY(qBlue(a) <= qAlpha(a));
        }
    }
    void denseAndSparseTransformSamplingArePixelIdentical() {
        auto nearby = patch({-16, -16}), widelySeparated = nearby;
        // This expands the source bounding rectangle beyond the 64 MiB dense
        // scratch allowance without changing any samples near the first mark.
        pixel(widelySeparated, 6000, 6000, qPremultiply(qRgba(40, 90, 180, 170)));
        QTransform transform; transform.rotate(31); transform.scale(1.25, .8); transform.translate(.25, -.5);
        RasterLayer dense, sparse; QString error;
        const QRectF extent(-16000, -16000, 32000, 32000);
        QVERIFY2(transformRasterLayer(nearby, nullptr, transform, extent, 32, &dense, &error), qPrintable(error));
        QVERIFY2(transformRasterLayer(widelySeparated, nullptr, transform, extent, 32, &sparse, &error), qPrintable(error));
        for (int y = -48; y <= 48; ++y) for (int x = -48; x <= 48; ++x)
            QCOMPARE(pixel(dense, x, y), pixel(sparse, x, y));
    }
    void ninetyDegreeRotationIsExactAtTileBoundary() {
        auto source = RasterLayer::create(); pixel(source, 255, -1, qPremultiply(qRgba(20, 90, 120, 130))); pixel(source, 256, 0, qRgb(90, 80, 70));
        TransformParameters parameters; parameters.rotation = 90;
        auto output = RasterLayer::create(); QString error;
        QVERIFY2(transformRasterLayer(source, nullptr, rasterTransform(selectedPixelBounds(source), parameters), drawing, 10, &output, &error), qPrintable(error));
        QCOMPARE(pixel(output, 256, -1), pixel(source, 255, -1)); QCOMPARE(pixel(output, 255, 0), pixel(source, 256, 0));
        QCOMPARE(pixel(output, 255, -1), QRgb(0)); QCOMPARE(pixel(output, 256, 0), QRgb(0));
    }
    void integerMoveCropsOnlySelectedPixelsAtCommit() {
        auto source=RasterLayer::create();
        for(int x=240;x<256;++x)pixel(source,x,10,qRgb(30+x-240,80,150));
        const auto untouched=qRgb(250,50,20);pixel(source,100,100,untouched);
        const auto selected=rectangle({240,10,16,1});QTransform move=QTransform::fromTranslate(8,0);
        RasterLayer output;QString error;
        QVERIFY2(transformRasterLayer(source,&selected,move,{0,0,256,256},10,&output,&error),qPrintable(error));
        QCOMPARE(pixel(output,100,100),untouched);
        for(int x=240;x<272;++x)QCOMPARE(pixel(output,x,10),x>=248&&x<256?pixel(source,x-8,10):QRgb(0));
        QCOMPARE(pixel(source,255,10),qRgb(45,80,150));
        QVERIFY2(transformRasterLayer(source,&selected,QTransform::fromTranslate(500,0),{0,0,256,256},10,&output,&error),qPrintable(error));
        QCOMPARE(selectedPixelBounds(output),QRectF(100,100,1,1));
        QVERIFY2(transformRasterLayer(source,nullptr,QTransform::fromTranslate(-500,0),{0,0,256,256},0,&output,&error),qPrintable(error));
        QVERIFY(output.tiles.isEmpty());
    }
    void affineCropMatchesUnclippedSamplingAtEveryEdge() {
        const auto source=patch({0,0});QString error;
        QTransform transform;transform.translate(10.25,5.5);transform.rotate(27);transform.scale(1.7,.8);
        RasterLayer full,cropped;
        QVERIFY2(transformRasterLayer(source,nullptr,transform,drawing,16,&full,&error),qPrintable(error));
        const QRectF extent(5,7,20,21);
        QVERIFY2(transformRasterLayer(source,nullptr,transform,extent,16,&cropped,&error),qPrintable(error));
        for(int y=-20;y<80;++y)for(int x=-30;x<80;++x)
            QCOMPARE(pixel(cropped,x,y),extent.contains(QRectF(x,y,1,1))?pixel(full,x,y):QRgb(0));
        QVERIFY(extent.contains(selectedPixelBounds(cropped)));
        QTransform tiny;tiny.translate(500,500);tiny.scale(.001,.001);
        QVERIFY2(transformRasterLayer(source,nullptr,tiny,{0,0,256,256},0,&cropped,&error),qPrintable(error));
        QVERIFY(cropped.tiles.isEmpty());
    }
    void boundsBudgetAndInvalidTransformsFailAtomically() {
        auto source = RasterLayer::create("Source"); pixel(source, 255, 10, qRgb(30, 80, 150)); pixel(source, 10, 10, qRgb(255, 0, 0));
        auto output = patch({20, 20}); const auto before = output; QString error;
        auto selection = rectangle({254, 9, 3, 3}); QTransform move; move.translate(1, 0);
        QVERIFY(!transformRasterLayer(source, &selection, move, drawing, 1, &output, &error)); QVERIFY(error.contains("limit")); QCOMPARE(output.tiles, before.tiles);
        QTransform singular; singular.scale(0, 1);
        QVERIFY(!transformRasterLayer(source, nullptr, singular, drawing, 10, &output, &error)); QCOMPARE(output.tiles, before.tiles);
        QTransform invalid; invalid.translate(std::numeric_limits<qreal>::quiet_NaN(), 0);
        QVERIFY(!transformRasterLayer(source, nullptr, invalid, drawing, 10, &output, &error)); QCOMPARE(output.tiles, before.tiles);
        QTransform huge; huge.scale(100000, 100000);
        QVERIFY(!transformRasterLayer(source, nullptr, huge, drawing, 10, &output, &error)); QCOMPARE(output.tiles, before.tiles);
        auto grouped = source; grouped.group = true;
        QVERIFY(!transformRasterLayer(grouped, nullptr, move, drawing, 10, &output, &error)); QCOMPARE(output.tiles, before.tiles);
        QCOMPARE(output.id, before.id);
    }
    void brushAndEraserRespectHardSelectionAndOpacity() {
        auto selection = rectangle({254, 95, 4, 10}); auto original = RasterLayer::create(), output = original; RasterLayer accumulation;
        auto dab = stamp(QColor(30, 90, 170), .4); dab.flow = .3;
        for (int i = 0; i < 60; ++i)
            QVERIFY(applyBrushDab(output, accumulation, original, dab, {256, 100}, {}, BrushBlend::Normal, false, drawing, 8, 1, &selection));
        QCOMPARE(output.tiles.size(), 2);
        for (int y = 85; y < 115; ++y) for (int x = 240; x < 272; ++x) {
            const int alpha = qAlpha(pixel(output, x, y));
            if (x >= 254 && x < 258 && y >= 95 && y < 105) QVERIFY(qAbs(alpha - 102) <= 1);
            else QCOMPARE(alpha, 0);
        }
        original = output; accumulation.tiles.clear(); auto eraseSelection = rectangle({256, 95, 2, 10}); dab.opacity = 1; dab.flow = 1;
        QVERIFY(applyBrushDab(output, accumulation, original, dab, {256, 100}, {}, BrushBlend::Normal, true, drawing, 8, 1, &eraseSelection));
        QCOMPARE(pixel(output, 255, 100), pixel(original, 255, 100)); QCOMPARE(qAlpha(pixel(output, 256, 100)), 0);
    }
    void selectionOutsideDabAllocatesNoRasterOrAccumulationTiles() {
        auto output = RasterLayer::create(), original = output; RasterLayer accumulation; auto dab = stamp(Qt::red);
        auto selection = rectangle({700, 700, 20, 20});
        QVERIFY(applyBrushDab(output, accumulation, original, dab, {256, 100}, {}, BrushBlend::Normal, false, drawing, 0, 1, &selection));
        QVERIFY(output.tiles.isEmpty()); QVERIFY(accumulation.tiles.isEmpty());
        QPainterPath empty;
        QVERIFY(applyBrushDab(output, accumulation, original, dab, {256, 100}, {}, BrushBlend::Normal, false, drawing, 0, 1, &empty));
        QVERIFY(output.tiles.isEmpty()); QVERIFY(accumulation.tiles.isEmpty());
        // A path hole overlaps the dab's bounds but must still produce no allocation.
        auto hole = rectangle({200, 50, 100, 100}); hole.addRect({235, 80, 42, 40});
        QVERIFY(applyBrushDab(output, accumulation, original, dab, {256, 100}, {}, BrushBlend::Normal, false, drawing, 0, 1, &hole));
        QVERIFY(output.tiles.isEmpty()); QVERIFY(accumulation.tiles.isEmpty());
    }
};
QTEST_GUILESS_MAIN(RasterEditTest)
#include "test_raster_edit.moc"
