#include "raster_tools.h"
#include "advanced_transform.h"
#include <QtTest>
#include <QTemporaryDir>
#include <QElapsedTimer>
#include <cmath>
using namespace toon;
namespace {
Project blank(int size = 128) {
    auto project = Project::create(); project.width = project.height = size;
    project.shots[0].sheet.overscan = 0; project.transparent = true; return project;
}
QPainterPath rectangle(QRectF rect) { QPainterPath path; path.addRect(rect); return path; }
void putImage(RasterLayer &layer, QImage image) {
    for (int y = 0; y < image.height(); y += tileSize) for (int x = 0; x < image.width(); x += tileSize) {
        QImage tile(tileSize, tileSize, QImage::Format_ARGB32_Premultiplied); tile.fill(Qt::transparent);
        QPainter painter(&tile); painter.drawImage(-x, -y, image); painter.end(); layer.tiles[{x / tileSize, y / tileSize}] = tile;
    }
}
QImage image(int size, QColor color) { QImage result(size, size, QImage::Format_ARGB32_Premultiplied); result.fill(color); return result; }
QColor pixel(const Document &document, int x, int y) {
    const auto &layer = document.project().shots[0].layers[document.activeLayer()];
    const auto key = TileKey{int(std::floor(x / qreal(tileSize))), int(std::floor(y / qreal(tileSize)))};
    const auto tile = layer.tiles.constFind(key); return tile == layer.tiles.cend() ? QColor(Qt::transparent) : tile->pixelColor(x - key.first * tileSize, y - key.second * tileSize);
}
void outline(QImage &drawing, QRect box, int thickness = 2) {
    QPainter painter(&drawing);
    painter.fillRect(box.x(), box.y(), box.width(), thickness, Qt::black);
    painter.fillRect(box.x(), box.bottom() - thickness + 1, box.width(), thickness, Qt::black);
    painter.fillRect(box.x(), box.y(), thickness, box.height(), Qt::black);
    painter.fillRect(box.right() - thickness + 1, box.y(), thickness, box.height(), Qt::black);
}
}
class RasterToolsTest : public QObject {
    Q_OBJECT
private slots:
    void bucketClosesGapsAndCommitsOnce() {
        auto project = blank(); auto drawing = image(128, Qt::white);
        { QPainter painter(&drawing); painter.setPen(QPen(Qt::black, 2)); painter.setBrush(Qt::NoBrush); painter.drawRect(20, 20, 80, 80); painter.fillRect(58, 18, 3, 5, Qt::white); }
        putImage(project.shots[0].layers[0], drawing); Document document; document.load(project);
        RasterFillSettings settings; settings.source = RasterSampleSource::ActiveLayer; settings.tolerance = 0;
        QVERIFY(RasterTools::bucket(&document, {60, 60}, Qt::blue, settings)); QCOMPARE(pixel(document, 5, 5), QColor(Qt::blue));
        document.history()->undo(); QCOMPARE(pixel(document, 5, 5), QColor(Qt::white));
        settings.gapClosing = 3; QVERIFY(RasterTools::bucket(&document, {60, 60}, Qt::blue, settings));
        QCOMPARE(pixel(document, 60, 60), QColor(Qt::blue)); QCOMPARE(pixel(document, 5, 5), QColor(Qt::white));
        QCOMPARE(pixel(document, 20, 40), QColor(Qt::black)); QCOMPARE(document.history()->count(), 1);
    }
    void bucketToleranceAndSelectionAreRespected() {
        auto project = blank(); auto drawing = image(128, QColor(100, 100, 100));
        { QPainter painter(&drawing); painter.fillRect(64, 0, 64, 128, QColor(110, 110, 110)); }
        putImage(project.shots[0].layers[0], drawing); Document document; document.load(project);
        RasterFillSettings settings; settings.source = RasterSampleSource::ActiveLayer; settings.tolerance = 9;
        QVERIFY(RasterTools::bucket(&document, {20, 20}, Qt::red, settings)); QCOMPARE(pixel(document, 100, 20), QColor(110, 110, 110));
        document.history()->undo(); settings.tolerance = 10; document.setRasterSelection(rectangle({0, 0, 128, 50}));
        QVERIFY(RasterTools::bucket(&document, {20, 20}, Qt::red, settings)); QCOMPARE(pixel(document, 100, 20), QColor(Qt::red)); QCOMPARE(pixel(document, 20, 80), QColor(100, 100, 100));
    }
    void fractionalLastPixelClickFillsAndReferenceControlsBoundaries() {
        auto project = blank(); auto boundary = image(128, Qt::transparent);
        { QPainter painter(&boundary); painter.fillRect(63, 0, 2, 128, Qt::black); }
        auto reference = RasterLayer::create("Ink reference"); putImage(reference, boundary); reference.visible = false;
        project.shots[0].layers.append(reference); Document document; document.load(project);
        RasterFillSettings settings; settings.source = RasterSampleSource::ReferenceLayer; settings.referenceLayerId = reference.id; settings.tolerance = 0;
        QVERIFY(RasterTools::bucket(&document, {127.9, 127.9}, Qt::red, settings));
        QCOMPARE(pixel(document, 127, 127), QColor(Qt::red)); QCOMPARE(pixel(document, 20, 20).alpha(), 0);
    }
    void bucketReplacesAntialiasedBackgroundWithoutDamagingInk_data() {
        QTest::addColumn<bool>("transparent");
        QTest::addColumn<bool>("composited");
        for (bool transparent : {false, true}) for (bool composited : {false, true})
            QTest::newRow(qPrintable(QString("%1-%2").arg(transparent ? "transparent" : "opaque", composited ? "all-layers" : "active"))) << transparent << composited;
    }
    void bucketReplacesAntialiasedBackgroundWithoutDamagingInk() {
        QFETCH(bool, transparent); QFETCH(bool, composited);
        auto project = blank(); auto drawing = image(128, transparent ? QColor(Qt::transparent) : QColor(Qt::white));
        { QPainter painter(&drawing); painter.fillRect(40, 0, 1, 128, Qt::black);
          painter.fillRect(41, 0, 1, 128, QColor(0, 0, 0, 192)); painter.fillRect(42, 0, 1, 128, QColor(0, 0, 0, 64)); }
        putImage(project.shots[0].layers[0], drawing); Document document; document.load(project);
        RasterFillSettings settings; settings.source = composited ? RasterSampleSource::AllLayers : RasterSampleSource::ActiveLayer;
        QVERIFY(RasterTools::bucket(&document, {80, 60}, Qt::blue, settings));
        QCOMPARE(pixel(document, 80, 60), QColor(Qt::blue));
        QCOMPARE(pixel(document, 40, 60), QColor(Qt::black));
        QCOMPARE(pixel(document, 41, 60), QColor(0, 0, 63));
        QCOMPARE(pixel(document, 42, 60), QColor(0, 0, 191));
        QCOMPARE(pixel(document, 20, 60), transparent ? QColor(Qt::transparent) : QColor(Qt::white));
        QCOMPARE(document.history()->count(), 1); document.history()->undo();
        QCOMPARE(document.project().shots[0].layers[0].tiles, project.shots[0].layers[0].tiles);
        document.history()->redo(); QCOMPARE(pixel(document, 42, 60), QColor(0, 0, 191));
    }
    void bucketRecolorsAntialiasingWithPartiallyTransparentSeed() {
        auto project = blank(); auto drawing = image(128, QColor(255, 255, 255, 128));
        { QPainter painter(&drawing); painter.fillRect(40, 0, 1, 128, Qt::black);
          painter.fillRect(41, 0, 1, 128, QColor(0, 0, 0, 192)); painter.fillRect(42, 0, 1, 128, QColor(0, 0, 0, 64)); }
        putImage(project.shots[0].layers[0], drawing); Document document; document.load(project);
        RasterFillSettings settings; settings.source = RasterSampleSource::ActiveLayer;
        QVERIFY(RasterTools::bucket(&document, {80, 60}, Qt::blue, settings));
        for (const auto edge : {QPair<int, int>{41, 63}, QPair<int, int>{42, 191}}) {
            const auto color = pixel(document, edge.first, 60);
            QVERIFY(color.red() <= 1 && color.green() <= 1); QVERIFY(color.alpha() >= 254); QVERIFY(std::abs(color.blue() - edge.second) <= 1);
        }
        QCOMPARE(pixel(document, 20, 60), QColor(255, 255, 255, 128)); QCOMPARE(pixel(document, 40, 60), QColor(Qt::black));
    }
    void bucketFillsUnderSeparateReferenceAntialiasing_data() {
        QTest::addColumn<bool>("reference");
        QTest::newRow("all-layers") << false;
        QTest::newRow("reference-layer") << true;
    }
    void bucketFillsUnderSeparateReferenceAntialiasing() {
        QFETCH(bool, reference);
        auto project = blank(); auto drawing = image(128, Qt::transparent);
        { QPainter painter(&drawing); painter.fillRect(40, 0, 1, 128, Qt::black);
          painter.fillRect(41, 0, 1, 128, QColor(0, 0, 0, 192)); painter.fillRect(42, 0, 1, 128, QColor(0, 0, 0, 64)); }
        auto ink = RasterLayer::create("Ink"); putImage(ink, drawing); project.shots[0].layers.append(ink);
        Document document; document.load(project); RasterFillSettings settings;
        settings.source = reference ? RasterSampleSource::ReferenceLayer : RasterSampleSource::AllLayers; settings.referenceLayerId = ink.id;
        QVERIFY(RasterTools::bucket(&document, {80, 60}, Qt::blue, settings));
        QCOMPARE(pixel(document, 41, 60), QColor(Qt::blue)); QCOMPARE(pixel(document, 42, 60), QColor(Qt::blue));
        QCOMPARE(pixel(document, 40, 60).alpha(), 0); QCOMPARE(pixel(document, 20, 60).alpha(), 0);
        QCOMPARE(RasterTools::sample(&document, {41, 60}), QColor(0, 0, 63));
        QCOMPARE(RasterTools::sample(&document, {42, 60}), QColor(0, 0, 191));
        QCOMPARE(document.project().shots[0].layers[1].tiles, ink.tiles);
    }
    void bucketKeepsCurvedOutlineWhileRemovingWhiteFringe() {
        auto project = blank(); auto drawing = image(128, Qt::white);
        { QPainter painter(&drawing); painter.setRenderHint(QPainter::Antialiasing); painter.setPen(QPen(Qt::black, 2));
          painter.setBrush(Qt::NoBrush); painter.drawEllipse(QRectF(20.2, 20.2, 80.4, 80.4)); }
        putImage(project.shots[0].layers[0], drawing); Document document; document.load(project);
        QVERIFY(RasterTools::bucket(&document, {60, 60}, Qt::blue));
        int checkedEdges = 0;
        for (int y = 20; y < 102; ++y) for (int x = 20; x < 102; ++x) {
            const QColor old = drawing.pixelColor(x, y), after = pixel(document, x, y);
            if (old == QColor(Qt::black)) QCOMPARE(after, old);
            if (old.red() > 0 && old.red() < 255 && QLineF({60.4, 60.4}, {x + .5, y + .5}).length() < 39.5) {
                QVERIFY2(after.red() <= 2 && after.green() <= 2, qPrintable(QString("Unfilled fringe at %1,%2: %3").arg(x).arg(y).arg(after.name())));
                QVERIFY(std::abs(after.blue() - old.blue()) <= 2); ++checkedEdges;
            }
        }
        QVERIFY(checkedEdges > 40); QCOMPARE(pixel(document, 5, 5), QColor(Qt::white));
    }
    void bucketEdgeRecoveryObeysSelectionAndAlphaLock() {
        auto project = blank(); auto drawing = image(128, Qt::transparent);
        { QPainter painter(&drawing); painter.fillRect(40, 0, 1, 128, Qt::black); painter.fillRect(41, 0, 1, 128, QColor(0, 0, 0, 128)); }
        putImage(project.shots[0].layers[0], drawing); Document document; document.load(project);
        document.setRasterSelection(rectangle({41, 20, 70, 40}));
        QVERIFY(RasterTools::bucket(&document, {80, 40}, Qt::blue));
        QCOMPARE(pixel(document, 41, 40), QColor(0, 0, 127)); QCOMPARE(pixel(document, 41, 10), QColor(0, 0, 0, 128));
        QCOMPARE(pixel(document, 80, 10).alpha(), 0); QCOMPARE(pixel(document, 40, 40), QColor(Qt::black));
        document.history()->undo(); document.clearRasterSelection(); document.setLayerAlphaLocked(true);
        const auto before = document.project().shots[0].layers[0].tiles;
        QVERIFY(RasterTools::bucket(&document, {80, 40}, Qt::blue)); QCOMPARE(document.project().shots[0].layers[0].tiles, before);
    }
    void bucketExpansionGrowsUnderReferenceButCannotCrossItsBoundary() {
        auto project = blank(); auto drawing = image(128, Qt::transparent);
        { QPainter painter(&drawing); painter.fillRect(40, 0, 6, 128, Qt::black); }
        auto ink = RasterLayer::create("Ink"); putImage(ink, drawing); ink.visible = false;
        project.shots[0].layers.append(ink); Document document; document.load(project);
        RasterFillSettings settings; settings.source = RasterSampleSource::ReferenceLayer; settings.referenceLayerId = ink.id;
        settings.expansion = 3;
        QVERIFY(RasterTools::bucket(&document, {20, 64}, Qt::blue, settings));
        QCOMPARE(pixel(document, 42, 64), QColor(Qt::blue)); QCOMPARE(pixel(document, 43, 64).alpha(), 0);
        QCOMPARE(pixel(document, 46, 64).alpha(), 0); QCOMPARE(document.project().shots[0].layers[1].tiles, ink.tiles);
        QCOMPARE(document.history()->count(), 1); document.history()->undo();
        QVERIFY(document.project().shots[0].layers[0].tiles.isEmpty()); settings.expansion = 20;
        QVERIFY(RasterTools::bucket(&document, {20, 64}, Qt::red, settings));
        QCOMPARE(pixel(document, 45, 64), QColor(Qt::red)); QCOMPARE(pixel(document, 46, 64).alpha(), 0);
        document.history()->undo(); settings.expansion = -3;
        QVERIFY(RasterTools::bucket(&document, {20, 64}, Qt::green, settings));
        QCOMPARE(pixel(document, 3, 64), QColor(Qt::green)); QCOMPARE(pixel(document, 2, 64).alpha(), 0);
        QCOMPARE(pixel(document, 36, 64), QColor(Qt::green)); QCOMPARE(pixel(document, 37, 64).alpha(), 0);
        QCOMPARE(document.project().shots[0].layers[1].tiles, ink.tiles);
    }
    void bucketExpansionPreservesOwnInkAndExistingSelection() {
        auto project = blank(); auto drawing = image(128, Qt::white);
        { QPainter painter(&drawing); painter.fillRect(40, 0, 3, 128, Qt::black); }
        putImage(project.shots[0].layers[0], drawing); Document document; document.load(project);
        document.setRasterSelection(rectangle({0, 30, 90, 40}));
        RasterFillSettings settings; settings.expansion = 20; settings.source = RasterSampleSource::ActiveLayer;
        QVERIFY(RasterTools::bucket(&document, {20, 50}, Qt::blue, settings));
        QCOMPARE(pixel(document, 39, 50), QColor(Qt::blue)); QCOMPARE(pixel(document, 40, 50), QColor(Qt::black));
        QCOMPARE(pixel(document, 42, 50), QColor(Qt::black)); QCOMPARE(pixel(document, 43, 50), QColor(Qt::white));
        QCOMPARE(pixel(document, 20, 20), QColor(Qt::white));
        document.history()->undo(); QCOMPARE(document.project().shots[0].layers[0].tiles, project.shots[0].layers[0].tiles);
        settings.source = RasterSampleSource::AllLayers;
        QVERIFY(RasterTools::bucket(&document, {20, 50}, Qt::blue, settings));
        QCOMPARE(pixel(document, 40, 50), QColor(Qt::black)); QCOMPARE(pixel(document, 43, 50), QColor(Qt::white));
    }
    void encloseFillsMultipleClosedRegionsAndRetainsReference_data() {
        QTest::addColumn<bool>("white"); QTest::addColumn<bool>("marked");
        for (bool white : {false, true}) for (bool marked : {false, true})
            QTest::newRow(qPrintable(QString("%1-%2").arg(white ? "white" : "transparent", marked ? "marked" : "chosen"))) << white << marked;
    }
    void encloseFillsMultipleClosedRegionsAndRetainsReference() {
        QFETCH(bool, white); QFETCH(bool, marked);
        auto project = blank(); auto drawing = image(128, white ? QColor(Qt::white) : QColor(Qt::transparent));
        outline(drawing, {20, 20, 30, 80}); outline(drawing, {70, 20, 30, 80});
        auto ink = RasterLayer::create("Ink"); putImage(ink, drawing); ink.visible = false; ink.fillReference = marked;
        project.shots[0].layers.append(ink); Document document; document.load(project);
        RasterFillSettings settings; settings.source = marked ? RasterSampleSource::MarkedReferences : RasterSampleSource::ReferenceLayer; settings.referenceLayerId = ink.id;
        const auto gesture = rectangle({10, 10, 100, 100});
        QVERIFY(RasterTools::encloseFill(&document, gesture, Qt::blue, settings));
        QCOMPARE(pixel(document, 30, 50), QColor(Qt::blue)); QCOMPARE(pixel(document, 80, 50), QColor(Qt::blue));
        QCOMPARE(pixel(document, 60, 50).alpha(), 0); QCOMPARE(pixel(document, 20, 50).alpha(), 0);
        QCOMPARE(pixel(document, 5, 5).alpha(), 0); QCOMPARE(document.project().shots[0].layers[1].tiles, ink.tiles);
        QCOMPARE(document.history()->count(), 1); document.history()->undo();
        QVERIFY(document.project().shots[0].layers[0].tiles.isEmpty()); document.history()->redo();
        QCOMPARE(pixel(document, 30, 50), QColor(Qt::blue)); QCOMPARE(document.project().shots[0].layers[1].tiles, ink.tiles);
    }
    void encloseRejectsOpenExteriorAndPartiallyEnclosedRegions() {
        auto project = blank(); auto drawing = image(128, Qt::white);
        outline(drawing, {20, 20, 30, 80}); outline(drawing, {70, 20, 30, 80});
        { QPainter painter(&drawing); painter.fillRect(30, 20, 3, 2, Qt::white); }
        auto ink = RasterLayer::create("Ink"); putImage(ink, drawing); project.shots[0].layers.append(ink);
        Document document; document.load(project); RasterFillSettings settings; settings.source = RasterSampleSource::ReferenceLayer; settings.referenceLayerId = ink.id;
        QVERIFY(RasterTools::encloseFill(&document, rectangle({10, 10, 100, 100}), Qt::blue, settings));
        QCOMPARE(pixel(document, 30, 50).alpha(), 0); QCOMPARE(pixel(document, 80, 50), QColor(Qt::blue));
        QCOMPARE(pixel(document, 60, 50).alpha(), 0); document.history()->undo();
        QVERIFY(RasterTools::encloseFill(&document, rectangle({75, 40, 15, 20}), Qt::blue, settings));
        QVERIFY(document.project().shots[0].layers[0].tiles.isEmpty()); QCOMPARE(document.history()->index(), 0);
        settings.gapClosing = 2;
        QVERIFY(RasterTools::encloseFill(&document, rectangle({10, 10, 100, 100}), Qt::red, settings));
        QCOMPARE(pixel(document, 30, 50), QColor(Qt::red)); QCOMPARE(pixel(document, 80, 50), QColor(Qt::red));
        document.load(blank());
        QVERIFY(RasterTools::encloseFill(&document, rectangle({10, 10, 100, 100}), Qt::red));
        QCOMPARE(document.history()->count(), 0); QVERIFY(document.project().shots[0].layers[0].tiles.isEmpty());
    }
    void encloseExpansionShrinkSelectionAndOwnInk() {
        auto project = blank(); auto drawing = image(128, Qt::white); outline(drawing, {20, 20, 80, 80}, 4);
        auto ink = RasterLayer::create("Ink"); putImage(ink, drawing); project.shots[0].layers.append(ink);
        Document document; document.load(project); RasterFillSettings settings; settings.source = RasterSampleSource::ReferenceLayer; settings.referenceLayerId = ink.id;
        settings.expansion = 2;
        document.setRasterSelection(rectangle({0, 0, 128, 60}));
        QVERIFY(RasterTools::encloseFill(&document, rectangle({10, 10, 100, 100}), Qt::blue, settings));
        QCOMPARE(pixel(document, 22, 50), QColor(Qt::blue)); QCOMPARE(pixel(document, 20, 50).alpha(), 0);
        QCOMPARE(pixel(document, 19, 50).alpha(), 0); QCOMPARE(pixel(document, 50, 70).alpha(), 0);
        document.history()->undo(); document.clearRasterSelection(); settings.expansion = -2;
        QVERIFY(RasterTools::encloseFill(&document, rectangle({10, 10, 100, 100}), Qt::red, settings));
        QCOMPARE(pixel(document, 25, 50).alpha(), 0); QCOMPARE(pixel(document, 26, 50), QColor(Qt::red));
        document.load(project); document.selectLayer(1); settings.source = RasterSampleSource::ActiveLayer; settings.expansion = 20;
        QVERIFY(RasterTools::encloseFill(&document, rectangle({10, 10, 100, 100}), Qt::blue, settings));
        QCOMPARE(pixel(document, 20, 50), QColor(Qt::black)); QCOMPARE(pixel(document, 23, 50), QColor(Qt::black));
        QCOMPARE(pixel(document, 24, 50), QColor(Qt::blue)); QCOMPARE(pixel(document, 19, 50), QColor(Qt::white));
    }
    void encloseRecoversAntialiasingWithoutRepaintingLineCores_data() {
        QTest::addColumn<bool>("white"); QTest::newRow("transparent") << false; QTest::newRow("white") << true;
    }
    void encloseRecoversAntialiasingWithoutRepaintingLineCores() {
        QFETCH(bool, white); auto project = blank();
        auto drawing = image(128, white ? QColor(Qt::white) : QColor(Qt::transparent)); outline(drawing, {20, 20, 80, 80});
        { QPainter painter(&drawing); painter.fillRect(22, 22, 1, 76, QColor(0, 0, 0, 192)); painter.fillRect(23, 22, 1, 76, QColor(0, 0, 0, 64)); }
        putImage(project.shots[0].layers[0], drawing); Document document; document.load(project);
        RasterFillSettings settings; settings.source = RasterSampleSource::ActiveLayer;
        QVERIFY(RasterTools::encloseFill(&document, rectangle({10, 10, 100, 100}), Qt::blue, settings));
        QCOMPARE(pixel(document, 30, 50), QColor(Qt::blue)); QCOMPARE(pixel(document, 20, 50), QColor(Qt::black));
        QCOMPARE(pixel(document, 22, 50), QColor(0, 0, 63)); QCOMPARE(pixel(document, 23, 50), QColor(0, 0, 191));
        QCOMPARE(pixel(document, 10, 50), white ? QColor(Qt::white) : QColor(Qt::transparent));
        QCOMPARE(document.history()->count(), 1); document.history()->undo();
        QCOMPARE(document.activeShot().layers[0].tiles, project.shots[0].layers[0].tiles);
    }
    void encloseKeepsSolidGrayLineCoresDistinctFromAntialiasing() {
        auto project = blank(); auto drawing = image(128, Qt::white); outline(drawing, {20, 20, 80, 80});
        for (int y = 0; y < drawing.height(); ++y) for (int x = 0; x < drawing.width(); ++x)
            if (drawing.pixelColor(x, y) == QColor(Qt::black)) drawing.setPixelColor(x, y, QColor(80, 80, 80));
        putImage(project.shots[0].layers[0], drawing); Document document; document.load(project);
        RasterFillSettings settings; settings.source = RasterSampleSource::ActiveLayer; settings.expansion = 20;
        QVERIFY(RasterTools::encloseFill(&document, rectangle({10, 10, 100, 100}), Qt::blue, settings));
        QCOMPARE(pixel(document, 30, 50), QColor(Qt::blue)); QCOMPARE(pixel(document, 20, 50), QColor(80, 80, 80));
        QCOMPARE(pixel(document, 21, 50), QColor(80, 80, 80)); QCOMPARE(pixel(document, 19, 50), QColor(Qt::white));
    }
    void positiveExpansionProtectsOwnInkWhenCompositeOpacityChangesItsPixels() {
        for (bool white : {false, true}) {
            auto project = blank(); auto drawing = image(128, white ? QColor(Qt::white) : QColor(Qt::transparent));
            outline(drawing, {20, 20, 80, 80}); putImage(project.shots[0].layers[0], drawing);
            project.shots[0].layers[0].opacity = .5; Document document; document.load(project);
            RasterFillSettings settings; settings.source = RasterSampleSource::AllLayers; settings.expansion = 20;
            QVERIFY(RasterTools::encloseFill(&document, rectangle({10, 10, 100, 100}), Qt::blue, settings));
            QCOMPARE(pixel(document, 30, 50), QColor(Qt::blue)); QCOMPARE(pixel(document, 20, 50), QColor(Qt::black));
            QCOMPARE(pixel(document, 21, 50), QColor(Qt::black));
            QCOMPARE(pixel(document, 19, 50), white ? QColor(Qt::white) : QColor(Qt::transparent));
            if (white) {
                document.load(project); QVERIFY(RasterTools::bucket(&document, {50, 50}, Qt::blue, settings));
                QCOMPARE(pixel(document, 30, 50), QColor(Qt::blue)); QCOMPARE(pixel(document, 20, 50), QColor(Qt::black));
                QCOMPARE(pixel(document, 21, 50), QColor(Qt::black)); QCOMPARE(pixel(document, 19, 50), QColor(Qt::white));
            }
        }
    }
    void encloseRecoversSeparateReferenceEdgesOverWhitePaintTarget() {
        auto project = blank(); putImage(project.shots[0].layers[0], image(128, Qt::white));
        auto drawing = image(128, Qt::transparent); outline(drawing, {20, 20, 80, 80});
        { QPainter painter(&drawing); painter.fillRect(22, 22, 1, 76, QColor(0, 0, 0, 192)); painter.fillRect(23, 22, 1, 76, QColor(0, 0, 0, 64)); }
        auto ink = RasterLayer::create("Ink"); putImage(ink, drawing); project.shots[0].layers.append(ink);
        QVector<QMap<TileKey, QImage>> painted;
        for (const auto source : {RasterSampleSource::AllLayers, RasterSampleSource::ReferenceLayer}) {
            Document document; document.load(project); RasterFillSettings settings; settings.source = source; settings.referenceLayerId = ink.id;
            QVERIFY(RasterTools::encloseFill(&document, rectangle({10, 10, 100, 100}), Qt::blue, settings));
            QCOMPARE(pixel(document, 22, 50), QColor(Qt::blue)); QCOMPARE(pixel(document, 23, 50), QColor(Qt::blue));
            QCOMPARE(document.activeShot().layers[1].tiles, ink.tiles); painted.append(document.activeShot().layers[0].tiles);
        }
        QCOMPARE(painted[0], painted[1]);
    }
    void encloseAndExpansionRespectAlphaLockAndRejectInvalidRequests() {
        auto project = blank(); auto drawing = image(128, Qt::transparent); outline(drawing, {20, 20, 80, 80});
        auto ink = RasterLayer::create("Ink"); putImage(ink, drawing); project.shots[0].layers.append(ink);
        Document document; document.load(project); RasterFillSettings settings; settings.source = RasterSampleSource::ReferenceLayer; settings.referenceLayerId = ink.id;
        document.setLayerAlphaLocked(true); const auto before = document.project().shots[0].layers[0].tiles;
        QVERIFY(RasterTools::encloseFill(&document, rectangle({10, 10, 100, 100}), Qt::blue, settings));
        QCOMPARE(document.project().shots[0].layers[0].tiles, before);
        document.setLayerAlphaLocked(false); document.setLayersLocked({document.activeShot().layers[0].id}, true);
        QVERIFY(!RasterTools::encloseFill(&document, rectangle({10, 10, 100, 100}), Qt::blue, settings));
        document.setLayersLocked({document.activeShot().layers[0].id}, false); settings.expansion = 21;
        QVERIFY(!RasterTools::encloseFill(&document, rectangle({10, 10, 100, 100}), Qt::blue, settings));
        QVERIFY(!RasterTools::bucket(&document, {50, 50}, Qt::blue, settings));
        QCOMPARE(document.project().shots[0].layers[0].tiles, before);
        settings.expansion = 0; settings.referenceLayerId = "missing";
        QVERIFY(!RasterTools::encloseFill(&document, rectangle({10, 10, 100, 100}), Qt::blue, settings));
        document.load(blank(8192));
        QVERIFY(!RasterTools::encloseFill(&document, rectangle({0, 0, 8192, 8192}), Qt::blue));
        QCOMPARE(document.history()->count(), 0);
    }
    void fillsCannotOverwriteRetainedPuppetSourceOrVectorArtwork() {
        auto project = blank(); auto drawing = image(128, Qt::white); outline(drawing, {20, 20, 80, 80});
        putImage(project.shots[0].layers[0], drawing); Document document; document.load(project);
        AdvancedTransform transform; QVERIFY(transform.begin(&document)); QVERIFY(transform.beginPuppet(4)); QVERIFY(transform.apply());
        QVERIFY(document.activeShot().layers[0].puppetWarp); const auto retained = document.project().toJson();
        const int history = document.history()->count(); RasterFillSettings settings; settings.expansion = 4;
        QVERIFY(!RasterTools::bucket(&document, {50, 50}, Qt::red, settings));
        QVERIFY(!RasterTools::encloseFill(&document, rectangle({10, 10, 100, 100}), Qt::red, settings));
        QCOMPARE(document.project().toJson(), retained); QCOMPARE(document.history()->count(), history);
        project.shots[0].layers[0].kind = LayerKind::Vector; project.shots[0].layers[0].tiles.clear(); document.load(project);
        QVERIFY(!RasterTools::bucket(&document, {50, 50}, Qt::red, settings));
        QVERIFY(!RasterTools::encloseFill(&document, rectangle({10, 10, 100, 100}), Qt::red, settings));
        QCOMPARE(document.history()->count(), 0);
    }
    void retouchPressureAndInheritedLocksDoNotMutateArtwork() {
        auto project = blank(); putImage(project.shots[0].layers[0], image(128, QColor(80, 100, 120, 128)));
        Document document; document.load(project); RasterTools tools(&document); RetouchSettings settings; settings.mode = RetouchMode::Dodge;
        QVERIFY(tools.beginRetouch({64, 64}, 0, settings)); tools.continueRetouch({70, 64}, 0); tools.endRetouch(); QCOMPARE(document.history()->count(), 0);
        document.setLayerAlphaLocked(true); QVERIFY(tools.beginRetouch({64, 64}, 1, settings)); tools.continueRetouch({70, 64}, 1); tools.endRetouch(); QCOMPARE(pixel(document, 64, 64).alpha(), 128);
        document.addGroup(); const auto groupId = document.project().shots[0].layers[document.activeLayer()].id;
        document.setLayersLocked({groupId}, true); document.selectLayer(0);
        const auto before = document.project().shots[0].layers[0].tiles; QVERIFY(!tools.beginRetouch({64, 64}, 1, settings)); QCOMPARE(document.project().shots[0].layers[0].tiles, before);
    }
    void samplingUsesLayerReferenceOrVisibleComposite() {
        auto project = blank(); putImage(project.shots[0].layers[0], image(128, Qt::blue));
        auto top = RasterLayer::create("Top"); putImage(top, image(128, QColor(255, 0, 0, 128))); project.shots[0].layers.append(top);
        Document document; document.load(project);
        QCOMPARE(RasterTools::sample(&document, {10, 10}, RasterSampleSource::ActiveLayer), QColor(Qt::blue));
        const auto composite = RasterTools::sample(&document, {10, 10}); QVERIFY(composite.red() >= 127 && composite.blue() >= 126); QCOMPARE(composite.alpha(), 255);
        QCOMPARE(RasterTools::sample(&document, {10, 10}, RasterSampleSource::ReferenceLayer, top.id), QColor(255, 0, 0, 128));
        document.setLayersVisible({top.id}, false); QCOMPARE(RasterTools::sample(&document, {10, 10}), QColor(Qt::blue));
        QCOMPARE(RasterTools::sample(&document, {10, 10}, RasterSampleSource::ReferenceLayer, top.id), QColor(255, 0, 0, 128));
        QVERIFY(!RasterTools::sample(&document, {-1, 0}).isValid());
    }
    void colorSelectionDistinguishesContiguousAndGlobal() {
        auto project = blank(); auto drawing = image(128, Qt::white);
        { QPainter painter(&drawing); painter.fillRect(10, 10, 20, 20, Qt::red); painter.fillRect(80, 80, 20, 20, Qt::red); }
        putImage(project.shots[0].layers[0], drawing); Document document; document.load(project); RasterFillSettings settings; settings.tolerance = 0;
        QVERIFY(RasterTools::selectColor(&document, {15, 15}, settings, true)); QVERIFY(document.rasterSelection().path.contains({15, 15})); QVERIFY(!document.rasterSelection().path.contains({85, 85}));
        QVERIFY(RasterTools::selectColor(&document, {15, 15}, settings, false)); QVERIFY(document.rasterSelection().path.contains({85, 85})); QVERIFY(!document.rasterSelection().path.contains({60, 60}));
        QCOMPARE(document.history()->count(), 0);
    }
    void gradientStopsRadialShapeAndAlphaLock() {
        Document document; document.load(blank()); const QGradientStops stops{{0, Qt::red}, {.5, Qt::green}, {1, Qt::blue}};
        QVERIFY(RasterTools::gradient(&document, {0, 64}, {128, 64}, stops)); QVERIFY(pixel(document, 1, 64).red() > 240); QVERIFY(pixel(document, 64, 64).green() > 245); QVERIFY(pixel(document, 126, 64).blue() > 240);
        document.history()->undo(); QVERIFY(RasterTools::gradient(&document, {64, 64}, {120, 64}, stops, true)); QVERIFY(pixel(document, 64, 64).red() > 240); QVERIFY(pixel(document, 120, 64).blue() > 240);
        document.history()->undo(); QPainterPath circle; circle.addEllipse({20, 20, 80, 80});
        QVERIFY(RasterTools::drawPath(&document, circle, Qt::yellow, Qt::black, 4)); QCOMPARE(pixel(document, 60, 60), QColor(Qt::yellow)); QCOMPARE(pixel(document, 20, 60), QColor(Qt::black)); QCOMPARE(pixel(document, 5, 5).alpha(), 0);
        document.setLayerAlphaLocked(true); QVERIFY(RasterTools::gradient(&document, {0, 0}, {128, 0}, {{0, Qt::red}, {1, Qt::blue}})); QCOMPARE(pixel(document, 5, 5).alpha(), 0); QCOMPARE(pixel(document, 60, 60).alpha(), 255);
        document.setLayersLocked({document.project().shots[0].layers[0].id}, true); QVERIFY(!RasterTools::drawPath(&document, circle, Qt::red, {}, 0));
    }
    void featherMaskHasNoTileSeamsAndFillClearUseCoverage() {
        const auto path = rectangle({254, 10, 4, 40}); RasterSelectionCache cache(&path, 3);
        const auto left = cache.tile({0, 0}).mask, right = cache.tile({1, 0}).mask;
        QVERIFY(!left.isNull()); QVERIFY(!right.isNull());
        for (int x = 248; x < 265; ++x) {
            int count = 0; for (int dx = -3; dx <= 3; ++dx) count += x + dx >= 254 && x + dx < 258;
            const int expected = (count * 255 + 3) / 7;
            QCOMPARE(x < 256 ? int(left.constScanLine(25)[x]) : int(right.constScanLine(25)[x - 256]), expected);
        }
        Document document; document.load(blank(512)); document.setRasterSelection(path); QVERIFY(RasterTools::featherSelection(&document, 3));
        QVERIFY(document.fillRasterSelection(Qt::red)); QCOMPARE(pixel(document, 252, 25).alpha(), int(left.constScanLine(25)[252]));
        const int alpha = pixel(document, 254, 25).alpha(); QVERIFY(alpha > 0 && alpha < 255);
        QVERIFY(document.clearSelectedPixels()); QVERIFY(pixel(document, 254, 25).alpha() < alpha); QVERIFY(pixel(document, 254, 25).alpha() > 0);
    }
    void featherBrushCapAndTransformSplitAreSoft() {
        auto project = blank(); Document document; document.load(project);
        document.setRasterSelection(rectangle({40, 20, 40, 80})); QVERIFY(RasterTools::featherSelection(&document, 5));
        BrushSettings brush; brush.color = Qt::red; brush.size = 30; brush.hardness = 1; brush.opacity = 1; brush.strokeOpacity = 1; brush.pressureSize = false;
        QVERIFY(document.beginStroke({40, 60}, 1, brush, false)); for (int i = 0; i < 20; ++i) document.continueStroke({40, 60}, 1); document.endStroke();
        const auto coverage = rasterSelectionMask(document.rasterSelection().path, {0, 0}, 5).constScanLine(60)[40];
        QVERIFY(std::abs(pixel(document, 40, 60).alpha() - coverage) <= 1);
        auto layer = RasterLayer::create(); putImage(layer, image(128, Qt::red)); RasterLayer transformed; QString error; QTransform transform; transform.translate(0, 20);
        const auto path = rectangle({40, 20, 40, 40}); QVERIFY(transformRasterLayer(layer, &path, transform, {0, 0, 128, 160}, 4, &transformed, &error, 5));
        const int remaining = transformed.tiles[{0, 0}].pixelColor(40, 25).alpha(); QVERIFY(remaining > 0 && remaining < 255);
    }
    void savedSelectionRestoresFeatherAndSurvivesStorage() {
        Document document; document.load(blank()); const auto path = rectangle({10, 20, 50, 40}); document.setRasterSelection(path); QVERIFY(RasterTools::featherSelection(&document, 7));
        QVERIFY(RasterTools::saveSelection(&document, "Face")); const auto id = document.project().shots[0].savedSelections[0].id;
        document.clearRasterSelection(); QVERIFY(RasterTools::loadSelection(&document, id)); QCOMPARE(document.rasterSelection().path, path); QCOMPARE(document.rasterSelection().featherRadius, 7.0);
        QTemporaryDir directory; QString error; QVERIFY2(saveProject(document.project(), directory.filePath("selection.toon"), &error), qPrintable(error));
        const auto loaded = loadProject(directory.filePath("selection.toon"), &error); QVERIFY2(loaded.has_value(), qPrintable(error)); QCOMPARE(loaded->shots[0].savedSelections[0].path, path); QCOMPARE(loaded->shots[0].savedSelections[0].featherRadius, 7.0);
        QVERIFY(RasterTools::deleteSelection(&document, id)); QVERIFY(document.project().shots[0].savedSelections.isEmpty()); document.history()->undo(); QCOMPARE(document.project().shots[0].savedSelections.size(), 1);
    }
    void retouchHasOneUndoAndSupportsCancel_data() {
        QTest::addColumn<int>("mode");
        for (int mode = 0; mode <= int(RetouchMode::Burn); ++mode) QTest::newRow(qPrintable(QString::number(mode))) << mode;
    }
    void retouchHasOneUndoAndSupportsCancel() {
        QFETCH(int, mode); auto project = blank(); auto drawing = image(128, QColor(120, 100, 80));
        { QPainter painter(&drawing); painter.fillRect(64, 0, 64, 128, QColor(180, 150, 120)); }
        putImage(project.shots[0].layers[0], drawing); Document document; document.load(project); RasterTools tools(&document);
        RetouchSettings settings; settings.mode = RetouchMode(mode); settings.size = 40; settings.strength = 1; settings.color = Qt::red;
        document.setRasterSelection(rectangle({0, 0, 80, 128}));
        QVERIFY(tools.beginRetouch({55, 64}, 1, settings)); tools.continueRetouch({75, 64}, 1); tools.endRetouch();
        QCOMPARE(document.history()->count(), 1); QVERIFY(document.project().shots[0].layers[0].tiles != project.shots[0].layers[0].tiles); QCOMPARE(pixel(document, 90, 64), QColor(180, 150, 120));
        document.history()->undo(); QCOMPARE(document.project().shots[0].layers[0].tiles, project.shots[0].layers[0].tiles);
        QVERIFY(tools.beginRetouch({55, 64}, 1, settings)); tools.continueRetouch({75, 64}, 1); tools.cancelRetouch();
        QCOMPARE(document.project().shots[0].layers[0].tiles, project.shots[0].layers[0].tiles); QVERIFY(!document.strokeActive());
    }
    void retouchSpatialSamplingIsIndependentOfInputDensity() {
        auto project = blank(); auto drawing = image(128, QColor(50, 80, 120));
        { QPainter painter(&drawing); painter.fillRect(64, 0, 64, 128, QColor(160, 190, 210)); }
        putImage(project.shots[0].layers[0], drawing);
        for (const auto mode : {RetouchMode::Smudge, RetouchMode::Blur, RetouchMode::Dodge}) {
            Document coarse, dense; coarse.load(project); dense.load(project); RasterTools first(&coarse), second(&dense);
            RetouchSettings settings; settings.mode = mode; settings.size = 20; settings.strength = .65;
            QVERIFY(first.beginRetouch({40, 64}, 1, settings)); first.continueRetouch({80, 64}, 1); first.endRetouch();
            QVERIFY(second.beginRetouch({40, 64}, 1, settings));
            for (int i = 1; i <= 200; ++i) second.continueRetouch({40 + i * .2, 64}, 1);
            second.endRetouch(); QCOMPARE(coarse.project().shots[0].layers[0].tiles, dense.project().shots[0].layers[0].tiles);
        }
    }
    void largeFillAndSelectionRemainBounded_data() {
        QTest::addColumn<QSize>("canvasSize");
        QTest::newRow("2048-square") << QSize(2048, 2048);
        QTest::newRow("4K") << QSize(3840, 2160);
    }
    void largeFillAndSelectionRemainBounded() {
        QFETCH(QSize, canvasSize); auto project = blank(canvasSize.width()); project.height = canvasSize.height();
        Document document; document.load(project); QElapsedTimer timer; timer.start();
        const QPointF seed(canvasSize.width() / 2.0, canvasSize.height() / 2.0);
        QVERIFY(RasterTools::bucket(&document, seed, Qt::blue));
        QCOMPARE(document.project().shots[0].layers[0].tiles.size(), ((canvasSize.width() + tileSize - 1) / tileSize) * ((canvasSize.height() + tileSize - 1) / tileSize));
        const auto fillMs = timer.elapsed(); timer.restart(); QVERIFY(RasterTools::selectColor(&document, seed, {}, true));
        QVERIFY(document.rasterSelection().path.contains({canvasSize.width() - 1.0, canvasSize.height() - 1.0}));
        QVERIFY(document.rasterSelection().path.elementCount() < 20);
        qInfo("%d x %d fill: %lld ms; contiguous selection: %lld ms", canvasSize.width(), canvasSize.height(), fillMs, timer.elapsed());
    }
};
QTEST_MAIN(RasterToolsTest)
#include "test_raster_tools.moc"
