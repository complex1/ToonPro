#include "document.h"
#include "sheet.h"
#include <QSignalSpy>
#include <QtTest>
#include <cmath>

using namespace toon;
namespace {
Project blank() { auto project = Project::create(); project.width = 640; project.height = 480; project.transparent = true; return project; }
QPainterPath rectangle(QRectF rectangle) { QPainterPath path; path.addRect(rectangle); return path; }
QImage image(QSize size, QColor color = Qt::transparent) {
    QImage result(size, QImage::Format_ARGB32_Premultiplied); result.fill(color); return result;
}
void storeImage(RasterLayer &layer, const QImage &source, QPoint origin) {
    const QRect area(origin, source.size());
    const int left = int(std::floor(area.left() / qreal(tileSize))), right = int(std::floor(area.right() / qreal(tileSize)));
    const int top = int(std::floor(area.top() / qreal(tileSize))), bottom = int(std::floor(area.bottom() / qreal(tileSize)));
    for (int y = top; y <= bottom; ++y) for (int x = left; x <= right; ++x) {
        auto tile = image({tileSize, tileSize}); QPainter painter(&tile);
        painter.drawImage(origin - QPoint(x * tileSize, y * tileSize), source); painter.end();
        layer.tiles.insert({x, y}, tile);
    }
}
QImage render(const Document &document, QRect area) {
    auto result = image(area.size()); QPainter painter(&result); painter.translate(-area.topLeft());
    paintLayers(painter, document.project().shots.first().layers, true); return result;
}
}
class SelectionActionsTest : public QObject {
    Q_OBJECT
private slots:
    void fillClearMatchIndependentPaintingAcrossHolesAndTileEdges() {
        const QRect area(-24, -24, 330, 330); auto expected = image(area.size(), QColor(20, 50, 100, 96));
        auto project = blank(); storeImage(project.shots[0].layers[0], expected, area.topLeft());
        Document document; document.load(project);
        auto path = rectangle({-10.25, -8.75, 285.5, 281.25}); path.addEllipse({-3, -2, 26, 26}); path.addRect({250, 250, 10, 10});
        document.setRasterSelection(path); const auto selection = document.rasterSelection().path;
        const QColor color(180, 80, 20, 120);
        { QPainter painter(&expected); painter.translate(-area.topLeft()); painter.setClipRect(document.drawingBounds()); painter.fillPath(selection, color); }
        QVERIFY(document.fillRasterSelection(color)); QCOMPARE(render(document, area), expected);
        QCOMPARE(document.history()->count(), 1); QCOMPARE(document.history()->undoText(), QString("Fill selection"));
        QCOMPARE(document.rasterSelection().path, selection);
        const auto filled = expected;
        { QPainter painter(&expected); painter.translate(-area.topLeft()); painter.setClipRect(document.drawingBounds()); painter.setCompositionMode(QPainter::CompositionMode_Clear); painter.fillPath(selection, Qt::white); }
        QVERIFY(document.clearSelectedPixels()); QCOMPARE(render(document, area), expected);
        QCOMPARE(document.history()->count(), 2); QCOMPARE(document.history()->undoText(), QString("Clear selected pixels"));
        QCOMPARE(document.rasterSelection().path, selection);
        const auto clearedTiles = document.project().shots[0].layers[0].tiles;
        QVERIFY(document.clearSelectedPixels()); QCOMPARE(document.history()->count(), 2); QCOMPARE(document.project().shots[0].layers[0].tiles, clearedTiles);
        document.history()->undo(); QCOMPARE(render(document, area), filled); QCOMPARE(document.rasterSelection().path, selection);
        document.history()->redo(); QCOMPARE(render(document, area), expected);
        const auto frame = renderMovieFrame(document.project());
        QCOMPARE(frame.pixelColor(100, 100), expected.pixelColor(124, 124));
    }
    void fillAlphaLockRetainsEveryDestinationAlpha_data() {
        QTest::addColumn<int>("foregroundAlpha");
        for (int alpha : {1, 37, 128, 254, 255}) QTest::newRow(qPrintable(QString::number(alpha))) << alpha;
    }
    void fillAlphaLockRetainsEveryDestinationAlpha() {
        QFETCH(int, foregroundAlpha);
        auto project = blank(); auto &layer = project.shots[0].layers[0]; layer.alphaLocked = true;
        auto tile = image({tileSize, tileSize});
        for (int x = 0; x < tileSize; ++x) for (int y = 0; y < 4; ++y) tile.setPixelColor(x, y, QColor(90, 120, 200, x));
        layer.tiles.insert({0, 0}, tile);
        Document document; document.load(project); document.setRasterSelection(rectangle({-5, 0, 270, 10}));
        QVERIFY(document.fillRasterSelection(QColor(240, 30, 70, foregroundAlpha)));
        const auto &tiles = document.project().shots[0].layers[0].tiles; QCOMPARE(tiles.size(), 1);
        for (int y = 0; y < tileSize; ++y) for (int x = 0; x < tileSize; ++x) {
            const auto actual = tiles.first().pixel(x, y), before = tile.pixel(x, y);
            QCOMPARE(qAlpha(actual), qAlpha(before));
            QVERIFY(qRed(actual) <= qAlpha(actual)); QVERIFY(qGreen(actual) <= qAlpha(actual)); QVERIFY(qBlue(actual) <= qAlpha(actual));
        }
        QCOMPARE(document.history()->count(), 1);
        const auto beforeClear = tiles; QSignalSpy warnings(&document, &Document::warning);
        QVERIFY(!document.clearSelectedPixels()); QCOMPARE(warnings.count(), 1); QCOMPARE(document.history()->count(), 1);
        QCOMPARE(document.project().shots[0].layers[0].tiles, beforeClear);
        document.history()->undo(); QCOMPARE(document.project().shots[0].layers[0].tiles, layer.tiles);
    }
    void emptyTransparentAndAlreadyFilledSelectionsDoNotDirty() {
        Document document; document.load(blank());
        QVERIFY(!document.fillRasterSelection(Qt::red)); QVERIFY(!document.clearSelectedPixels());
        document.setRasterSelection({}); QVERIFY(!document.fillRasterSelection(Qt::red)); QVERIFY(!document.clearSelectedPixels());
        QVERIFY(document.project().shots[0].layers[0].tiles.isEmpty()); QCOMPARE(document.history()->count(), 0);
        document.setRasterSelection(rectangle({10, 10, 30, 30}));
        QVERIFY(document.fillRasterSelection(Qt::transparent)); QVERIFY(document.clearSelectedPixels());
        QVERIFY(document.project().shots[0].layers[0].tiles.isEmpty()); QCOMPARE(document.history()->count(), 0);
        QVERIFY(document.fillRasterSelection(Qt::red)); const auto filled = document.project().shots[0].layers[0].tiles;
        QVERIFY(document.fillRasterSelection(Qt::red)); QCOMPARE(document.project().shots[0].layers[0].tiles, filled); QCOMPARE(document.history()->count(), 1);
        QVERIFY(document.clearSelectedPixels()); QVERIFY(document.project().shots[0].layers[0].tiles.isEmpty()); QCOMPARE(document.history()->count(), 2);
        QVERIFY(document.clearSelectedPixels()); QCOMPARE(document.history()->count(), 2);
        auto project = blank(); project.shots[0].layers[0].tiles.insert({0, 0}, image({tileSize, tileSize}));
        document.load(project); document.selectAllPixels();
        QVERIFY(document.clearSelectedPixels()); QCOMPARE(document.project().shots[0].layers[0].tiles, project.shots[0].layers[0].tiles);
        QVERIFY(document.history()->isClean()); QCOMPARE(document.history()->count(), 0);
    }
    void layerGroupsLocksAndActiveGesturesBlockEdits() {
        auto project = blank(); auto group = RasterLayer::create("Character"); group.group = true;
        project.shots[0].layers[0].parentId = group.id; project.shots[0].layers[0].tiles[{0, 0}] = image({tileSize, tileSize}, Qt::red);
        project.shots[0].layers.append(group);
        Document document;
        for (int mode = 0; mode < 4; ++mode) {
            auto blocked = project;
            if (mode == 0) blocked.shots[0].layers[1].visible = false;
            if (mode == 1) blocked.shots[0].layers[1].locked = true;
            if (mode == 2) blocked.shots[0].layers[0].locked = true;
            document.load(blocked); if (mode == 3) document.selectLayer(1);
            document.setRasterSelection(rectangle({20, 20, 30, 30}));
            QVERIFY(!document.fillRasterSelection(Qt::blue)); QVERIFY(!document.clearSelectedPixels());
            QCOMPARE(document.history()->count(), 0); QCOMPARE(document.project().shots[0].layers[0].tiles, blocked.shots[0].layers[0].tiles);
        }
        document.load(project); document.setRasterSelection(rectangle({20, 20, 30, 30}));
        QVERIFY(document.beginTransform()); QVERIFY(!document.fillRasterSelection(Qt::blue)); QVERIFY(!document.clearSelectedPixels()); document.cancelTransform();
        BrushSettings brush; QVERIFY(document.beginStroke({25, 25}, 1, brush, false));
        QVERIFY(!document.fillRasterSelection(Qt::blue)); QVERIFY(!document.clearSelectedPixels()); document.cancelStroke();
        QCOMPARE(document.history()->count(), 0); QCOMPARE(document.project().shots[0].layers[0].tiles, project.shots[0].layers[0].tiles);
    }
    void switchedLayerSelectionStaysWithinCurrentDrawingBounds() {
        auto project = blank(); project.shots[0].sheet.overscan = 0;
        auto notes = RasterLayer::create("Notes"); notes.annotation = true; project.shots[0].layers.append(notes);
        Document document; document.load(project); document.selectLayer(1);
        document.setRasterSelection(rectangle({-20, -20, 40, 40})); const auto selection = document.rasterSelection().path;
        document.selectLayer(0); QVERIFY(document.fillRasterSelection(Qt::red));
        QCOMPARE(document.rasterSelection().path, selection);
        const auto actual = render(document, {-25, -25, 50, 50});
        for (int y = 0; y < 50; ++y) for (int x = 0; x < 50; ++x)
            QCOMPARE(actual.pixelColor(x, y), x >= 25 && x < 45 && y >= 25 && y < 45 ? QColor(Qt::red) : QColor(Qt::transparent));
        QCOMPARE(document.project().shots[0].layers[0].tiles.size(), 1);
    }
    void tileBudgetFailureRollsBackAllPixelsAndHistory() {
        auto project = blank(); auto first = image({tileSize, tileSize}); first.setPixelColor(254, 40, Qt::blue);
        project.shots[0].layers[0].tiles.insert({0, 0}, first);
        auto other = RasterLayer::create("Other drawing"); const auto empty = image({tileSize, tileSize});
        for (int i = 0; i < maxProjectTiles - 1; ++i) other.tiles.insert({i % 32, i / 32}, empty);
        project.shots[0].layers.append(other);
        Document document; document.load(project); document.setRasterSelection(rectangle({254, 40, 10, 10}));
        const auto before = document.project().shots[0].layers[0].tiles;
        const auto selection = document.rasterSelection().path; QSignalSpy warnings(&document, &Document::warning);
        QVERIFY(!document.fillRasterSelection(Qt::red)); QCOMPARE(warnings.count(), 1);
        QCOMPARE(document.project().shots[0].layers[0].tiles, before); QCOMPARE(document.rasterSelection().path, selection);
        QCOMPARE(document.history()->count(), 0); QVERIFY(document.history()->isClean());
        document.setRasterSelection(rectangle({0, 0, 256, 256})); QVERIFY(document.clearSelectedPixels());
        QVERIFY(document.project().shots[0].layers[0].tiles.isEmpty()); QCOMPARE(document.history()->count(), 1);
        QVERIFY(document.fillRasterSelection(Qt::red)); QCOMPARE(document.project().shots[0].layers[0].tiles.size(), 1);
        document.history()->undo(); QVERIFY(document.project().shots[0].layers[0].tiles.isEmpty());
        document.history()->undo(); QCOMPARE(document.project().shots[0].layers[0].tiles, before);
    }
};
QTEST_MAIN(SelectionActionsTest)
#include "test_selection_actions.moc"
