#include "raster_tools.h"
#include "selection_geometry.h"
#include <QtTest>
#include <QElapsedTimer>
#include <QPainter>
#include <cmath>

using namespace toon;
namespace {
QImage binaryMask(int width, int height) {
    QImage mask(width, height, QImage::Format_Grayscale8); mask.fill(0); return mask;
}
QImage rasterize(const QPainterPath &path, QSize size, QPoint origin = {}) {
    auto mask = binaryMask(size.width(), size.height());
    QPainter painter(&mask); painter.translate(-origin);
    painter.setPen(Qt::NoPen); painter.setBrush(Qt::white); painter.drawPath(path);
    return mask;
}
Project projectFromMask(const QImage &mask) {
    auto project = Project::create(); project.width = mask.width(); project.height = mask.height();
    project.shots[0].sheet.overscan = 0; project.transparent = true;
    for (int ty = 0; ty * tileSize < mask.height(); ++ty)
        for (int tx = 0; tx * tileSize < mask.width(); ++tx) {
            QImage tile(tileSize, tileSize, QImage::Format_ARGB32_Premultiplied); tile.fill(Qt::transparent);
            for (int y = 0; y < tileSize && ty * tileSize + y < mask.height(); ++y) {
                const auto source = mask.constScanLine(ty * tileSize + y);
                auto row = reinterpret_cast<QRgb *>(tile.scanLine(y));
                for (int x = 0; x < tileSize && tx * tileSize + x < mask.width(); ++x)
                    row[x] = source[tx * tileSize + x] ? qRgb(255, 0, 0) : qRgb(255, 255, 255);
            }
            project.shots[0].layers[0].tiles.insert({tx, ty}, tile);
        }
    return project;
}
qint64 pixelPerimeter(const QImage &mask) {
    qint64 perimeter = 0;
    for (int y = 0; y < mask.height(); ++y) for (int x = 0; x < mask.width(); ++x) {
        if (!mask.constScanLine(y)[x]) continue;
        perimeter += x == 0 || !mask.constScanLine(y)[x - 1];
        perimeter += x + 1 == mask.width() || !mask.constScanLine(y)[x + 1];
        perimeter += y == 0 || !mask.constScanLine(y - 1)[x];
        perimeter += y + 1 == mask.height() || !mask.constScanLine(y + 1)[x];
    }
    return perimeter;
}
}
class ColorRangeTest : public QObject {
    Q_OBJECT
private slots:
    void contoursPreservePixelsAndOnlyDrawRealBoundaries_data() {
        QTest::addColumn<QImage>("mask");
        auto holes = binaryMask(128, 96);
        { QPainter painter(&holes); painter.fillRect(0, 0, 120, 90, Qt::white);
            painter.fillRect(12, 12, 96, 66, Qt::black); painter.fillRect(28, 24, 64, 42, Qt::white);
            painter.fillRect(40, 36, 40, 18, Qt::black); }
        QTest::newRow("nested-holes-touching-edges") << holes;
        auto diagonals = binaryMask(64, 64);
        for (int y = 0; y < 64; ++y) for (int x = 0; x < 64; ++x) diagonals.scanLine(y)[x] = (x + y) % 2 ? 255 : 0;
        QTest::newRow("diagonal-contacts") << diagonals;
        auto random = binaryMask(129, 97); quint32 seed = 0x12345678;
        for (int y = 0; y < random.height(); ++y) for (int x = 0; x < random.width(); ++x) {
            seed = seed * 1664525 + 1013904223; random.scanLine(y)[x] = seed >> 30 ? 255 : 0;
        }
        QTest::newRow("irregular-texture") << random;
        auto stripes = binaryMask(256, 128);
        for (int y = 0; y < stripes.height(); ++y) for (int x = 0; x < stripes.width(); ++x)
            stripes.scanLine(y)[x] = x % 4 < 2 ? 255 : 0;
        QTest::newRow("long-parallel-strips") << stripes;
    }
    void contoursPreservePixelsAndOnlyDrawRealBoundaries() {
        QFETCH(QImage, mask); const QPoint origin(-19, -31); bool complex = false;
        const auto path = selectionPathFromMask(mask, origin, &complex);
        QVERIFY(!complex); QCOMPARE(rasterize(path, mask.size(), origin), mask);
        QCOMPARE(qRound64(path.length()), pixelPerimeter(mask));
    }
    void emptyAndSolidMasksStaySimple() {
        bool complex = false; auto mask = binaryMask(1920, 1080);
        QVERIFY(selectionPathFromMask(mask, {}, &complex).isEmpty()); QVERIFY(!complex);
        mask.fill(255); const auto path = selectionPathFromMask(mask, {}, &complex);
        QVERIFY(!complex); QCOMPARE(path.elementCount(), 5); QCOMPARE(path.boundingRect(), QRectF(0, 0, 1920, 1080));
    }
    void fragmentedColorRangeCanDeselectAndDrawAgain() {
        auto mask = binaryMask(1024, 1024);
        for (int y = 0; y < mask.height(); y += 8) for (int x = 0; x < mask.width(); x += 8) {
            mask.scanLine(y)[x] = mask.scanLine(y)[x + 1] = 255;
            mask.scanLine(y + 1)[x] = mask.scanLine(y + 1)[x + 1] = 255;
        }
        auto project = projectFromMask(mask); Document document; document.load(project);
        RasterFillSettings settings; settings.source = RasterSampleSource::ActiveLayer; settings.tolerance = 0;
        QElapsedTimer timer; timer.start();
        QVERIFY(RasterTools::selectColor(&document, {.5, .5}, settings, false));
        const auto duration = timer.elapsed();
        QVERIFY(document.rasterSelection().active);
        QCOMPARE(rasterize(document.rasterSelection().path, mask.size()), mask);
        QCOMPARE(document.history()->count(), 0);
        QCOMPARE(document.project().shots[0].layers[0].tiles, project.shots[0].layers[0].tiles);
        document.clearRasterSelection(); QVERIFY(!document.rasterSelection().active);
        BrushSettings brush; brush.color = Qt::blue; brush.size = 8; brush.pressureSize = false;
        QVERIFY(document.beginStroke({20, 20}, 1, brush, false)); document.endStroke();
        QCOMPARE(document.history()->count(), 1);
        qInfo("Color Range: 16,384 separate islands in %lld ms", duration);
        QVERIFY2(duration < 3000, "Color Range must not stall on fragmented texture");
    }
    void booleanOperationsMatchHardPixelCoverage() {
        auto first = binaryMask(96, 72), second = binaryMask(96, 72); quint32 seed = 0xabc123;
        for (int y = 0; y < first.height(); ++y) for (int x = 0; x < first.width(); ++x) {
            seed = seed * 1664525 + 1013904223; first.scanLine(y)[x] = seed & 0x40000000 ? 255 : 0;
            second.scanLine(y)[x] = seed & 0x20000000 ? 255 : 0;
        }
        bool complex = false; const QPoint origin(-7, -11);
        const auto left = selectionPathFromMask(first, origin, &complex);
        const auto right = selectionPathFromMask(second, origin, &complex);
        const QRectF bounds(origin, first.size());
        for (const auto operation : {SelectionPathOperation::Union, SelectionPathOperation::Difference, SelectionPathOperation::Intersection}) {
            QPainterPath result; QString error;
            QVERIFY2(selectionBooleanPath(left, right, operation, bounds, &result, &error), qPrintable(error));
            auto expected = first.copy();
            for (int y = 0; y < first.height(); ++y) for (int x = 0; x < first.width(); ++x) {
                const bool a = first.constScanLine(y)[x], b = second.constScanLine(y)[x];
                expected.scanLine(y)[x] = (operation == SelectionPathOperation::Union ? a || b
                    : operation == SelectionPathOperation::Difference ? a && !b : a && b) ? 255 : 0;
            }
            QCOMPARE(rasterize(result, first.size(), origin), expected);
        }
    }
    void resizeMatchesRoundPixelCellDistance_data() {
        QTest::addColumn<qreal>("radius");
        for (const qreal radius : {1.0, 2.0, 3.5, 5.0, -1.0, -2.0, -3.5, -5.0})
            QTest::newRow(qPrintable(QString::number(radius))) << radius;
    }
    void resizeMatchesRoundPixelCellDistance() {
        QFETCH(qreal, radius); auto mask = binaryMask(24, 24);
        { QPainter painter(&mask); painter.fillRect(0, 0, 18, 17, Qt::white); painter.fillRect(4, 4, 6, 5, Qt::black);
            painter.fillRect(21, 21, 1, 1, Qt::white); }
        bool complex = false; const QPoint origin(-7, -11);
        const auto path = selectionPathFromMask(mask, origin, &complex);
        QPainterPath result; QString error;
        QVERIFY2(selectionResizePath(path, radius, QRectF(origin, mask.size()), &result, &error), qPrintable(error));
        auto expected = binaryMask(mask.width(), mask.height()); const bool growing = radius > 0;
        const int halo = int(std::ceil(std::abs(radius))) + 1;
        for (int y = 0; y < mask.height(); ++y) for (int x = 0; x < mask.width(); ++x) {
            bool nearby = false;
            for (int sy = y - halo; sy <= y + halo && !nearby; ++sy)
                for (int sx = x - halo; sx <= x + halo; ++sx) {
                    const bool selected = sx >= 0 && sy >= 0 && sx < mask.width() && sy < mask.height() && mask.constScanLine(sy)[sx];
                    if (selected != growing) continue;
                    const qreal dx = qMax(0.0, std::abs(x - sx) - .5), dy = qMax(0.0, std::abs(y - sy) - .5);
                    if (dx * dx + dy * dy <= radius * radius) { nearby = true; break; }
                }
            expected.scanLine(y)[x] = (growing ? nearby : !nearby) ? 255 : 0;
        }
        QCOMPARE(rasterize(result, mask.size(), origin), expected);
    }
    void geometryWorkingLimitIsAtomic() {
        QPainterPath huge; huge.addRect(0, 0, 8192, 8192);
        QPainterPath result; result.addRect(1, 2, 3, 4); const auto previous = result; QString error;
        QVERIFY(!selectionBooleanPath(huge, huge, SelectionPathOperation::Union, huge.boundingRect(), &result, &error));
        QVERIFY(!error.isEmpty()); QCOMPARE(result, previous); error.clear();
        QVERIFY(!selectionResizePath(huge, -10, huge.boundingRect(), &result, &error));
        QVERIFY(!error.isEmpty()); QCOMPARE(result, previous);
    }
    void excessDetailLeavesPreviousSelectionUsable() {
        auto mask = binaryMask(512, 512);
        for (int y = 0; y < mask.height(); ++y) for (int x = 0; x < mask.width(); ++x)
            mask.scanLine(y)[x] = (x + y) % 2 ? 255 : 0;
        Document document; document.load(projectFromMask(mask));
        QPainterPath previous; previous.addRect(10, 10, 30, 20); document.setRasterSelection(previous);
        QSignalSpy warnings(&document, &Document::warning);
        RasterFillSettings settings; settings.source = RasterSampleSource::ActiveLayer; settings.tolerance = 0;
        QElapsedTimer timer; timer.start();
        QVERIFY(!RasterTools::selectColor(&document, {1.5, .5}, settings, false));
        QCOMPARE(warnings.count(), 1); QCOMPARE(document.rasterSelection().path, previous);
        QVERIFY(timer.elapsed() < 3000);
        document.clearRasterSelection(); QVERIFY(!document.rasterSelection().active); QVERIFY(!document.editingGestureActive());
        document.selectAllPixels(); QVERIFY(document.rasterSelection().path.contains({400, 400}));
    }
};
QTEST_MAIN(ColorRangeTest)
#include "test_color_range.moc"
