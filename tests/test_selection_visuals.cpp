#include "canvas.h"
#include "selection_context_bar.h"
#include "selection_geometry.h"
#include <QDir>
#include <QElapsedTimer>
#include <QLinearGradient>
#include <QSignalSpy>
#include <QTimer>
#include <QtTest>

using namespace toon;

namespace {
Project scene(bool illustration = false) {
    auto project = Project::create(); project.width = 640; project.height = 480;
    auto &shot = project.shots[0]; shot.sheet.left = shot.sheet.right = shot.sheet.top = shot.sheet.bottom = 20;
    shot.sheet.overscan = 20; shot.sheet.header = 80;
    project.movie = "Selection study"; shot.name = "Evening arrival";
    if (illustration) {
        QImage picture(640, 480, QImage::Format_ARGB32_Premultiplied);
        QPainter painter(&picture); painter.setRenderHint(QPainter::Antialiasing);
        QLinearGradient sky(0, 0, 0, 480); sky.setColorAt(0, QColor("#28394c"));
        sky.setColorAt(.65, QColor("#a79892")); sky.setColorAt(1, QColor("#ead9ad"));
        painter.fillRect(picture.rect(), sky);
        painter.setPen(Qt::NoPen); painter.setBrush(QColor("#efd5ac")); painter.drawEllipse(QPointF(462, 134), 60, 60);
        for (int i = 0; i < 9; ++i) {
            const QRect block(i * 81 - 20, 250 - (i % 4) * 26, 68, 230);
            painter.fillRect(block, QColor("#3c515f"));
            for (int y = block.top() + 12; y < 465; y += 23) for (int x = block.left() + 9; x < block.right() - 5; x += 18)
                painter.fillRect(QRect(x, y, 7, 11), (x + y) % 3 ? QColor("#cfb58c") : QColor("#52717e"));
        }
        painter.setPen(QPen(QColor("#25313b"), 3)); painter.setBrush(QColor("#dcad91"));
        painter.drawEllipse(QRectF(257, 144, 112, 142));
        painter.setBrush(QColor("#243c4a"));
        QPainterPath hair; hair.moveTo(253, 232); hair.cubicTo(220, 114, 342, 101, 377, 171);
        hair.lineTo(374, 245); hair.lineTo(355, 190); hair.lineTo(320, 157); hair.lineTo(292, 210);
        hair.lineTo(291, 172); hair.closeSubpath(); painter.drawPath(hair);
        painter.setBrush(QColor("#b86159"));
        QPainterPath coat; coat.moveTo(282, 274); coat.lineTo(348, 274); coat.lineTo(403, 318);
        coat.lineTo(430, 480); coat.lineTo(205, 480); coat.lineTo(231, 319); coat.closeSubpath(); painter.drawPath(coat);
        painter.setBrush(QColor("#ead4ba")); painter.drawPolygon(QPolygonF{{280, 277}, {348, 277}, {316, 324}});
        painter.setPen(QPen(QColor("#24313c"), 3)); painter.drawLine(284, 224, 299, 225); painter.drawLine(331, 224, 347, 222);
        painter.setPen(QPen(QColor("#a46b61"), 2)); painter.drawLine(310, 253, 327, 252);
        painter.end();
        for (int y = 0; y < 2; ++y) for (int x = 0; x < 3; ++x) {
            QImage tile(tileSize, tileSize, QImage::Format_ARGB32_Premultiplied); tile.fill(Qt::transparent);
            QPainter tilePainter(&tile); tilePainter.drawImage(QPoint(-x * tileSize, -y * tileSize), picture); tilePainter.end();
            shot.layers[0].tiles[{x, y}] = tile;
        }
    }
    return project;
}
void prepare(Canvas &canvas, bool illustration = false) {
    canvas.resize(1000, 800); canvas.setProject(scene(illustration));
    canvas.setTool(Canvas::Tool::RectangleSelect); canvas.setSelectionBarVisible(false);
    canvas.show(); canvas.setZoom(1); QApplication::processEvents();
}
QPointF screen(const Canvas &canvas, QPointF artwork) {
    return canvas.viewTransform().map(sheetGeometry(canvas.project(), canvas.shot()).output.topLeft() + artwork);
}
QColor colorAt(const QImage &image, QPointF point) {
    const auto pixel = (point * image.devicePixelRatio()).toPoint(); return image.pixelColor(pixel);
}
QPainterPath ring() {
    QPainterPath path; path.setFillRule(Qt::OddEvenFill);
    path.addRect(120, 90, 320, 260); path.addRect(210, 160, 70, 70); return path;
}
QTimer *ants(Canvas &canvas) { return canvas.findChild<QTimer *>("selectionAntsTimer"); }
}

class SelectionVisualsTest : public QObject {
    Q_OBJECT
private slots:
    void focusMaskPreservesInteriorAndHeaderIncludingRotatedHoles() {
        Canvas canvas; prepare(canvas);
        for (const auto view : {QPointF(1, 0), QPointF(1.15, 31), QPointF(.7, -18)}) {
            canvas.document()->clearRasterSelection(); canvas.setZoom(view.x()); canvas.setRotation(view.y());
            const auto clean = canvas.grab().toImage();
            canvas.document()->setRasterSelection(ring()); const auto focused = canvas.grab().toImage();
            const auto inside = screen(canvas, {350, 130}), outside = screen(canvas, {70, 160}), hole = screen(canvas, {245, 195});
            QCOMPARE(colorAt(focused, inside), colorAt(clean, inside));
            QVERIFY(colorAt(focused, outside).lightness() < colorAt(clean, outside).lightness());
            QVERIFY(colorAt(focused, hole).lightness() < colorAt(clean, hole).lightness());
            const auto header = canvas.viewTransform().map(QPointF(canvas.paperRect().center().x(), 40));
            QVERIFY(canvas.rect().contains(header.toPoint()));
            QCOMPARE(colorAt(focused, header), colorAt(clean, header));
            QCOMPARE(colorAt(focused, {8, 8}), colorAt(clean, {8, 8}));
            canvas.setSelectionFocusOverlay(false); const auto noFocus = canvas.grab().toImage();
            QCOMPARE(colorAt(noFocus, hole), colorAt(clean, hole));
            QCOMPARE(colorAt(noFocus, outside), colorAt(clean, outside));
            canvas.setSelectionFocusOverlay(true);
        }
    }
    void animatedAntsNeverRecomposeArtworkOrEmitDocumentChanges() {
        Canvas canvas; prepare(canvas, true); canvas.document()->setRasterSelection(ring());
        auto *timer = ants(canvas); QVERIFY(timer); QVERIFY(timer->isActive());
        const auto frame = renderMovieFrame(canvas.project()); const auto sheet = renderSheet(canvas.project(), canvas.shot());
        const auto first = canvas.grab().toImage(); const auto renders = canvas.artworkRenderCount();
        QSignalSpy documentChanges(&canvas, &Canvas::documentChanged);
        QSignalSpy selectionChanges(canvas.document(), &Document::rasterSelectionChanged);
        bool changedBorder = false;
        for (int i = 0; i < 12; ++i) {
            QVERIFY(QMetaObject::invokeMethod(timer, "timeout", Qt::DirectConnection));
            const auto image = canvas.grab().toImage(); changedBorder |= image != first;
            QCOMPARE(colorAt(image, screen(canvas, {350, 130})), colorAt(first, screen(canvas, {350, 130})));
            QCOMPARE(canvas.artworkRenderCount(), renders);
        }
        QVERIFY(changedBorder); QCOMPARE(documentChanges.count(), 0); QCOMPARE(selectionChanges.count(), 0);
        QVERIFY(canvas.document()->history()->isClean()); QCOMPARE(canvas.document()->history()->count(), 0);
        QCOMPARE(renderMovieFrame(canvas.project()), frame); QCOMPARE(renderSheet(canvas.project(), canvas.shot()), sheet);
        canvas.hide(); QVERIFY(!timer->isActive()); canvas.show(); QVERIFY(timer->isActive());
        canvas.document()->clearRasterSelection(); QVERIFY(!timer->isActive());
        canvas.document()->setRasterSelection(QPainterPath()); QVERIFY(!timer->isActive()); // Active empty mask has no boundary.
    }
    void screenHatchingSpacingDoesNotScaleWithArtwork() {
        Canvas canvas; prepare(canvas); QPainterPath selection; selection.addRect(300, 100, 200, 250);
        canvas.document()->setRasterSelection(selection);
        for (qreal zoom : {.7, 1.4}) {
            canvas.setZoom(zoom); const auto image = canvas.grab().toImage();
            const QPoint start = screen(canvas, {50, 180}).toPoint();
            // This white outside strip is away from every paper/selection edge.
            for (int offset = 0; offset < 36; ++offset)
                QCOMPARE(colorAt(image, start + QPoint(offset, 0)), colorAt(image, start + QPoint(offset + 9, 0)));
            int low = 255, high = 0;
            for (int offset = 0; offset < 9; ++offset) {
                const int lightness = colorAt(image, start + QPoint(offset, 0)).lightness();
                low = qMin(low, lightness); high = qMax(high, lightness);
            }
            QVERIFY(high > low);
        }
    }
    void fragmentedSelectionStaysAnimatedAndCanBeDismissed() {
        Canvas canvas; prepare(canvas); canvas.setTool(Canvas::Tool::ColorRange);
        const auto clean = canvas.grab().toImage();
        QPainterPath islands; islands.setFillRule(Qt::OddEvenFill);
        for (int y = 20; y < 460; y += 7) for (int x = 20; x < 620; x += 7)
            islands.addRect(x, y, 3, 3);
        QVERIFY(islands.elementCount() > 20000);
        QElapsedTimer elapsed; elapsed.start();
        canvas.document()->setRasterSelection(islands);
        const auto first = canvas.grab().toImage();
        QDir().mkpath("test-artifacts"); QVERIFY(first.save("test-artifacts/fragmented-selection.png"));
        const auto renders = canvas.artworkRenderCount();
        QVERIFY(ants(canvas)->isActive());
        bool changed = false;
        for (int i = 0; i < 8; ++i) {
            QVERIFY(QMetaObject::invokeMethod(ants(canvas), "timeout", Qt::DirectConnection));
            const auto next = canvas.grab().toImage(); changed |= next != first;
            QCOMPARE(canvas.artworkRenderCount(), renders);
        }
        QVERIFY(changed);
        // Interior pixels stay clear, while gaps between islands are shaded.
        QCOMPARE(colorAt(first, screen(canvas, {21, 21})), colorAt(clean, screen(canvas, {21, 21})));
        QVERIFY(colorAt(first, screen(canvas, {25, 25})).lightness() < colorAt(clean, screen(canvas, {25, 25})).lightness());
        const auto original = canvas.document()->rasterSelection().path;
        for (auto tool : {Canvas::Tool::RectangleSelect, Canvas::Tool::EllipseSelect}) {
            canvas.setTool(tool); canvas.setSelectionCombine(SelectionCombine::Add);
            QTest::mousePress(&canvas, Qt::LeftButton, Qt::NoModifier, screen(canvas, {100, 100}).toPoint());
            QTest::mouseMove(&canvas, screen(canvas, {300, 260}).toPoint());
            QVERIFY(canvas.selectionGestureActive());
            canvas.grab();
            QTest::keyClick(&canvas, Qt::Key_Escape);
            QTest::mouseRelease(&canvas, Qt::LeftButton, Qt::NoModifier, screen(canvas, {300, 260}).toPoint());
            QVERIFY(!canvas.selectionGestureActive());
            QCOMPARE(canvas.document()->rasterSelection().path, original);
        }
        canvas.setTool(Canvas::Tool::ColorRange);
        QTest::keyClick(&canvas, Qt::Key_Escape);
        QVERIFY(!canvas.document()->rasterSelection().active);
        QVERIFY(!ants(canvas)->isActive());
        QVERIFY(canvas.toolBlockReason().isEmpty());
        qInfo() << "Fragmented selection display, eight animation frames and Escape:" << elapsed.elapsed() << "ms";
        QVERIFY2(elapsed.elapsed() < 5000, "Fragmented selections must not stall the canvas event loop.");
        canvas.setTool(Canvas::Tool::Brush);
        const auto point = screen(canvas, {100, 100}).toPoint();
        QTest::mouseClick(&canvas, Qt::LeftButton, Qt::NoModifier, point);
        QVERIFY(!canvas.document()->project().shots.first().layers.first().tiles.isEmpty());
    }
    void denseColorRangeBoundaryRemainsResponsiveAtRetinaScale() {
        Canvas canvas; prepare(canvas);
        auto project = scene(); project.height = 600; canvas.setProject(project); canvas.setZoom(1);
        canvas.setTool(Canvas::Tool::ColorRange);
        QImage mask(512, 512, QImage::Format_Grayscale8); quint32 seed = 0x12345678;
        for (int y = 0; y < mask.height(); ++y) for (int x = 0; x < mask.width(); ++x) {
            seed = seed * 1664525 + 1013904223; mask.scanLine(y)[x] = seed >> 30 ? 255 : 0;
        }
        bool tooComplex = false; const auto path = selectionPathFromMask(mask, {40, 30}, &tooComplex);
        QVERIFY(!tooComplex); QVERIFY(path.elementCount() > 150000);
        QElapsedTimer elapsed; elapsed.start();
        canvas.document()->setRasterSelection(path); canvas.grab();
        canvas.setRotation(23); canvas.grab();
        const auto renders = canvas.artworkRenderCount();
        QVERIFY(QMetaObject::invokeMethod(ants(canvas), "timeout", Qt::DirectConnection)); canvas.grab();
        QCOMPARE(canvas.artworkRenderCount(), renders);
        QTest::keyClick(&canvas, Qt::Key_Escape); QVERIFY(!canvas.document()->rasterSelection().active);
        qInfo() << "Dense color-range boundary, rotated view, animation and Escape:" << elapsed.elapsed() << "ms";
        QVERIFY2(elapsed.elapsed() < 1500, "Retina antialiasing must not rebuild one enormous stroke mesh.");
    }
    void cacheInvalidatesForPixelsViewAndDirectSymmetrySettings() {
        Canvas canvas; prepare(canvas, true); canvas.grab();
        auto renders = canvas.artworkRenderCount();
        QPainterPath selection; selection.addRect(120, 90, 320, 260);
        canvas.document()->setRasterSelection(selection); canvas.grab(); QCOMPARE(canvas.artworkRenderCount(), renders);
        canvas.setZoom(.9); canvas.grab(); QVERIFY(canvas.artworkRenderCount() > renders); renders = canvas.artworkRenderCount();
        canvas.setRotation(13); canvas.grab(); QVERIFY(canvas.artworkRenderCount() > renders); renders = canvas.artworkRenderCount();
        canvas.centerOn(canvas.center() + QPointF(15, 9)); canvas.grab(); QVERIFY(canvas.artworkRenderCount() > renders); renders = canvas.artworkRenderCount();
        canvas.resize(1050, 780); canvas.grab(); QVERIFY(canvas.artworkRenderCount() > renders); renders = canvas.artworkRenderCount();
        QEvent ratioChange(QEvent::DevicePixelRatioChange); QApplication::sendEvent(&canvas, &ratioChange); canvas.grab();
        QVERIFY(canvas.artworkRenderCount() > renders); renders = canvas.artworkRenderCount();
        canvas.brush.symmetry.mode = SymmetryMode::Vertical; canvas.grab();
        QVERIFY(canvas.artworkRenderCount() > renders); renders = canvas.artworkRenderCount();
        QVERIFY(canvas.document()->fillRasterSelection(Qt::blue)); canvas.grab();
        QVERIFY(canvas.artworkRenderCount() > renders); renders = canvas.artworkRenderCount();
        QVERIFY(canvas.document()->beginTransform()); TransformParameters transform; transform.translation = {15, 10};
        QVERIFY(canvas.document()->previewTransform(transform)); canvas.grab();
        QVERIFY(canvas.artworkRenderCount() > renders); renders = canvas.artworkRenderCount();
        canvas.document()->cancelTransform(); canvas.grab(); QVERIFY(canvas.artworkRenderCount() > renders);
    }
    void floatingBarAndOverlaysAreExcludedFromExports() {
        Canvas canvas; prepare(canvas, true); canvas.setSelectionBarVisible(true);
        const auto frame = renderMovieFrame(canvas.project()), sheet = renderSheet(canvas.project(), canvas.shot());
        QPainterPath selection; selection.addRect(225, 115, 180, 335); canvas.document()->setRasterSelection(selection);
        auto *bar = canvas.findChild<SelectionContextBar *>(); QVERIFY(bar); QVERIFY(bar->isVisible());
        QCOMPARE(renderMovieFrame(canvas.project()), frame); QCOMPARE(renderSheet(canvas.project(), canvas.shot()), sheet);
        QDir().mkpath("test-artifacts"); QVERIFY(canvas.grab().save("test-artifacts/selection-focus.png"));
        QPainterPath lasso; lasso.setFillRule(Qt::OddEvenFill);
        lasso.moveTo(225, 290); lasso.lineTo(227, 128); lasso.lineTo(303, 96); lasso.lineTo(380, 122);
        lasso.lineTo(425, 284); lasso.lineTo(450, 444); lasso.lineTo(198, 444); lasso.closeSubpath();
        lasso.addEllipse(QRectF(310, 345, 56, 50));
        canvas.document()->setRasterSelection(lasso);
        canvas.setRotation(-12); QVERIFY(canvas.grab().save("test-artifacts/selection-focus-rotated.png"));
    }
};
QTEST_MAIN(SelectionVisualsTest)
#include "test_selection_visuals.moc"
