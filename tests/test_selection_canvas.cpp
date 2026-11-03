#include "canvas.h"
#include <QApplication>
#include <QDir>
#include <QFocusEvent>
#include <QLineF>
#include <QMouseEvent>
#include <QPointingDevice>
#include <QTabletEvent>
#include <QtTest>
#include <cmath>

using namespace toon;

namespace {
Project artwork(bool filled = false) {
    auto project = Project::create();
    project.width = 640; project.height = 480; project.transparent = true;
    auto &sheet = project.shots[0].sheet;
    sheet.left = sheet.right = sheet.top = sheet.bottom = 20; sheet.header = 80; sheet.overscan = 20;
    if (filled) {
        QImage tile(tileSize, tileSize, QImage::Format_ARGB32_Premultiplied); tile.fill(Qt::transparent);
        QPainter painter(&tile); painter.fillRect(QRect(160, 140, 80, 60), Qt::red); painter.end();
        project.shots[0].layers[0].tiles[{0, 0}] = tile;
    }
    return project;
}
void prepare(Canvas &canvas, bool filled = false) {
    canvas.resize(1000, 800); canvas.setProject(artwork(filled)); canvas.show();
    canvas.setZoom(1.2); canvas.setRotation(31);
    QApplication::processEvents();
}
QPointF screen(const Canvas &canvas, QPointF artwork) {
    return canvas.viewTransform().map(sheetGeometry(canvas.project(), canvas.shot()).output.topLeft() + artwork);
}
void mouseAt(Canvas &canvas, QEvent::Type type, QPointF position, Qt::KeyboardModifiers modifiers = Qt::NoModifier,
             Qt::MouseButton button = Qt::LeftButton, Qt::MouseEventSource source = Qt::MouseEventNotSynthesized) {
    QMouseEvent event(type, position, position, canvas.mapToGlobal(position),
        type == QEvent::MouseMove ? Qt::NoButton : button,
        type == QEvent::MouseButtonRelease ? Qt::NoButton : button, modifiers, source);
    QApplication::sendEvent(&canvas, &event);
}
void drag(Canvas &canvas, QPointF from, QPointF to, Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
    mouseAt(canvas, QEvent::MouseButtonPress, screen(canvas, from), modifiers);
    mouseAt(canvas, QEvent::MouseMove, screen(canvas, to), modifiers);
    mouseAt(canvas, QEvent::MouseButtonRelease, screen(canvas, to), modifiers);
}
void selectRect(Canvas &canvas, QRectF rect) {
    canvas.setTool(Canvas::Tool::RectangleSelect); drag(canvas, rect.topLeft(), rect.bottomRight());
}
bool close(qreal actual, qreal expected) { return std::abs(actual - expected) < .000001; }
}

class SelectionCanvasTest : public QObject {
    Q_OBJECT
private slots:
    void combineModesAndModifiersAreCapturedAtGestureStart() {
        Canvas canvas; prepare(canvas); canvas.setTool(Canvas::Tool::RectangleSelect);
        auto *doc = canvas.document();
        QPainterPath original; original.addRect(100, 100, 120, 120);
        doc->setRasterSelection(original);
        canvas.setSelectionCombine(SelectionCombine::Subtract);
        // Shift overrides the persistent subtract choice for this gesture. Its
        // release mid-drag must not unexpectedly change Add back to Subtract.
        mouseAt(canvas, QEvent::MouseButtonPress, screen(canvas, {180, 140}), Qt::ShiftModifier);
        QVERIFY(canvas.selectionGestureActive());
        canvas.setSelectionCombine(SelectionCombine::Intersect);
        mouseAt(canvas, QEvent::MouseMove, screen(canvas, {280, 250}));
        mouseAt(canvas, QEvent::MouseButtonRelease, screen(canvas, {280, 250}));
        QVERIFY(!canvas.selectionGestureActive());
        QVERIFY(doc->rasterSelection().path.contains({120, 120}));
        QVERIFY(doc->rasterSelection().path.contains({260, 230}));
        QCOMPARE(canvas.selectionCombine(), SelectionCombine::Intersect);
        doc->setRasterSelection(original);
        drag(canvas, {170, 90}, {240, 230}, Qt::AltModifier);
        QVERIFY(doc->rasterSelection().path.contains({120, 120}));
        QVERIFY(!doc->rasterSelection().path.contains({190, 140}));
        doc->setRasterSelection(original);
        drag(canvas, {170, 90}, {240, 230}, Qt::ShiftModifier | Qt::AltModifier);
        QVERIFY(!doc->rasterSelection().path.contains({120, 120}));
        QVERIFY(doc->rasterSelection().path.contains({190, 140}));
        doc->clearRasterSelection(); canvas.setSelectionCombine(SelectionCombine::Subtract);
        drag(canvas, {100, 100}, {200, 200});
        QVERIFY(doc->rasterSelection().path.contains({140, 140})); // New selection without an existing one.
        const auto before = doc->rasterSelection().path;
        drag(canvas, {150, 150}, {150.1, 150.1}, Qt::AltModifier);
        QCOMPARE(doc->rasterSelection().path, before);
        QVERIFY(doc->history()->isClean());
    }
    void tabletCombinationAlsoKeepsPressModifier() {
        Canvas canvas; prepare(canvas); canvas.setTool(Canvas::Tool::RectangleSelect);
        QPainterPath original; original.addRect(100, 100, 120, 120);
        canvas.document()->setRasterSelection(original);
        QPointingDevice pen("Combination pen", 21, QInputDevice::DeviceType::Stylus, QPointingDevice::PointerType::Pen,
            QInputDevice::Capability::Position | QInputDevice::Capability::Pressure, 1, 1);
        auto send = [&](QEvent::Type type, QPointF artwork, Qt::KeyboardModifiers modifiers) {
            const auto position = screen(canvas, artwork);
            QTabletEvent event(type, &pen, position, canvas.mapToGlobal(position), .5, 0, 0, 0, 0, 0,
                modifiers, type == QEvent::TabletMove ? Qt::NoButton : Qt::LeftButton,
                type == QEvent::TabletRelease ? Qt::NoButton : Qt::LeftButton);
            QApplication::sendEvent(&canvas, &event);
        };
        send(QEvent::TabletPress, {170, 90}, Qt::AltModifier);
        send(QEvent::TabletMove, {240, 230}, Qt::ShiftModifier);
        send(QEvent::TabletRelease, {240, 230}, Qt::NoModifier);
        QVERIFY(canvas.document()->rasterSelection().path.contains({120, 120}));
        QVERIFY(!canvas.document()->rasterSelection().path.contains({190, 140}));
        QVERIFY(!canvas.selectionGestureActive());
    }
    void rotatedRectangleIsTransientAndIgnoresClicks() {
        Canvas canvas; prepare(canvas);
        canvas.setTool(Canvas::Tool::RectangleSelect);
        const auto frame = renderMovieFrame(canvas.project());
        const auto sheet = renderSheet(canvas.project(), canvas.shot());
        const auto clean = canvas.grab().toImage();
        drag(canvas, {180, 150}, {300, 240});
        const auto selection = canvas.document()->rasterSelection();
        QVERIFY(selection.active); QVERIFY(selection.path.contains({200, 200}));
        QVERIFY(!selection.path.contains({175, 200}));
        const auto bounds = selection.path.boundingRect();
        QVERIFY(close(bounds.left(), 180)); QVERIFY(close(bounds.top(), 150));
        QVERIFY(close(bounds.width(), 120)); QVERIFY(close(bounds.height(), 90));
        QVERIFY(canvas.brushCursorPath().isEmpty());
        QVERIFY(canvas.grab().toImage() != clean);
        QCOMPARE(renderMovieFrame(canvas.project()), frame); QCOMPARE(renderSheet(canvas.project(), canvas.shot()), sheet);
        QVERIFY(canvas.document()->history()->isClean());
        drag(canvas, {100, 100}, {100.3, 100.3});
        QCOMPARE(canvas.document()->rasterSelection().path, selection.path);
        mouseAt(canvas, QEvent::MouseButtonPress, screen(canvas, {100, 100}));
        mouseAt(canvas, QEvent::MouseMove, screen(canvas, {350, 300}));
        QTest::keyClick(&canvas, Qt::Key_Escape);
        mouseAt(canvas, QEvent::MouseButtonRelease, screen(canvas, {350, 300}));
        QCOMPARE(canvas.document()->rasterSelection().path, selection.path);
    }
    void tabletLassoClipsBrushAndSuppressesSynthesizedMouse() {
        Canvas canvas; prepare(canvas); canvas.setTool(Canvas::Tool::LassoSelect);
        QPointingDevice pen("Selection pen", 20, QInputDevice::DeviceType::Stylus, QPointingDevice::PointerType::Pen,
            QInputDevice::Capability::Position | QInputDevice::Capability::Pressure, 1, 1);
        auto send = [&](QEvent::Type type, QPointF artwork) {
            const auto position = screen(canvas, artwork);
            QTabletEvent event(type, &pen, position, canvas.mapToGlobal(position), .5, 0, 0, 0, 0, 0,
                Qt::NoModifier, type == QEvent::TabletMove ? Qt::NoButton : Qt::LeftButton,
                type == QEvent::TabletRelease ? Qt::NoButton : Qt::LeftButton);
            QApplication::sendEvent(&canvas, &event); QVERIFY(event.isAccepted());
        };
        send(QEvent::TabletPress, {140, 100}); send(QEvent::TabletMove, {320, 100});
        send(QEvent::TabletMove, {220, 260}); send(QEvent::TabletRelease, {140, 100});
        const auto selection = canvas.document()->rasterSelection();
        QVERIFY(selection.active); QVERIFY(selection.path.contains({220, 150}));
        QVERIFY(!selection.path.contains({150, 240}));
        mouseAt(canvas, QEvent::MouseButtonPress, screen(canvas, {50, 50}), Qt::NoModifier, Qt::LeftButton,
                Qt::MouseEventSynthesizedByQt);
        mouseAt(canvas, QEvent::MouseButtonRelease, screen(canvas, {500, 400}), Qt::NoModifier, Qt::LeftButton,
                Qt::MouseEventSynthesizedByQt);
        QCOMPARE(canvas.document()->rasterSelection().path, selection.path);
        canvas.setTool(Canvas::Tool::Brush); canvas.setBrushColor(Qt::blue); canvas.setBrushSize(40);
        drag(canvas, {100, 150}, {350, 150});
        const auto frame = renderMovieFrame(canvas.project());
        QCOMPARE(frame.pixelColor(220, 150), QColor(Qt::blue));
        QCOMPARE(frame.pixelColor(150, 150).alpha(), 0); QCOMPARE(frame.pixelColor(320, 150).alpha(), 0);
        QCOMPARE(canvas.document()->history()->count(), 1);
        canvas.document()->history()->undo(); QCOMPARE(renderMovieFrame(canvas.project()).pixelColor(220, 150).alpha(), 0);
        canvas.setTool(Canvas::Tool::LassoSelect);
        send(QEvent::TabletPress, {80, 80}); send(QEvent::TabletRelease, {80, 80});
        QCOMPARE(canvas.document()->rasterSelection().path, selection.path);
    }
    void movePreviewApplyCancelAndUndo() {
        Canvas canvas; prepare(canvas, true);
        selectRect(canvas, {150, 130, 100, 80});
        const auto original = renderMovieFrame(canvas.project());
        const auto originalSheet = renderSheet(canvas.project(), canvas.shot());
        canvas.setTool(Canvas::Tool::Transform); QVERIFY(canvas.document()->transformActive());
        drag(canvas, {200, 170}, {270, 140});
        QVERIFY(canvas.document()->transformActive()); // Release ends the drag, not the edit.
        QVERIFY(QLineF(canvas.document()->transformParameters().translation, QPointF(70, -30)).length() < .000001);
        QCOMPARE(renderMovieFrame(canvas.project()), original);
        QCOMPARE(renderSheet(canvas.project(), canvas.shot()), originalSheet);
        auto preview = renderMovieFrame(canvas.document()->displayProject());
        QCOMPARE(preview.pixelColor(270, 140), QColor(Qt::red)); QCOMPARE(preview.pixelColor(170, 170).alpha(), 0);
        QVERIFY(canvas.brushCursorPath().isEmpty());
        QDir().mkpath("test-artifacts"); QVERIFY(canvas.grab().save("test-artifacts/selection-transform.png"));
        QTest::keyClick(&canvas, Qt::Key_Return);
        QVERIFY(!canvas.document()->transformActive()); QCOMPARE(renderMovieFrame(canvas.project()), preview);
        QCOMPARE(canvas.document()->history()->count(), 1);
        canvas.document()->history()->undo(); QCOMPARE(renderMovieFrame(canvas.project()), original);
        canvas.document()->history()->redo(); QCOMPARE(renderMovieFrame(canvas.project()), preview);
        canvas.setTool(Canvas::Tool::Transform); QVERIFY(canvas.document()->transformActive());
        drag(canvas, {270, 140}, {290, 180}); QTest::keyClick(&canvas, Qt::Key_Escape);
        QVERIFY(!canvas.document()->transformActive()); QCOMPARE(renderMovieFrame(canvas.project()), preview);
        canvas.setTool(Canvas::Tool::Transform); drag(canvas, {270, 140}, {310, 140});
        QTest::keyClick(&canvas, Qt::Key_Escape); // Explicit discard; tool-switch confirmation has separate coverage.
        canvas.setTool(Canvas::Tool::Brush);
        QVERIFY(!canvas.document()->transformActive()); QCOMPARE(renderMovieFrame(canvas.project()), preview);
        QCOMPARE(canvas.document()->history()->count(), 1);
    }
    void scaleAndRotateHandlesUseArtworkCoordinates() {
        Canvas canvas; prepare(canvas, true); canvas.setTool(Canvas::Tool::Transform);
        auto *doc = canvas.document();
        QCOMPARE(doc->transformBounds(), QRectF(160, 140, 80, 60));
        drag(canvas, {242, 200}, {242, 200});
        QVERIFY(close(doc->transformParameters().scaleX, 1)); QVERIFY(close(doc->transformParameters().scaleY, 1));
        drag(canvas, {240, 200}, {260, 215});
        QVERIFY(close(doc->transformParameters().scaleX, 1.5)); QVERIFY(close(doc->transformParameters().scaleY, 1.5));
        doc->cancelTransform(); canvas.setTool(Canvas::Tool::Transform);
        drag(canvas, {240, 200}, {260, 185}, Qt::ShiftModifier);
        QVERIFY(close(doc->transformParameters().scaleX, 1.5)); QVERIFY(close(doc->transformParameters().scaleY, .5));
        doc->cancelTransform(); canvas.setTool(Canvas::Tool::Transform);
        const QPointF center(200, 170);
        const qreal distance = 30 + 28 / canvas.zoom();
        drag(canvas, center + QPointF(0, -distance), center + QPointF(distance, 0));
        QVERIFY(close(doc->transformParameters().rotation, 90));
        QVERIFY(close(doc->transformParameters().scaleX, 1));
        QTest::keyClick(&canvas, Qt::Key_Escape);
        QVERIFY(doc->history()->isClean());
    }
    void pendingPreviewSurvivesFocusAndTemporaryPan() {
        Canvas canvas; prepare(canvas, true);
        Navigator navigator(&canvas); navigator.resize(260, 220); navigator.show();
        const auto originalMiniature = navigator.grab().toImage();
        canvas.setTool(Canvas::Tool::Transform); drag(canvas, {200, 170}, {320, 170});
        const auto preview = renderMovieFrame(canvas.document()->displayProject());
        QVERIFY(navigator.grab().toImage() != originalMiniature);
        QFocusEvent focus(QEvent::FocusOut); QApplication::sendEvent(&canvas, &focus);
        QVERIFY(canvas.document()->transformActive());
        auto parameters = canvas.document()->transformParameters(); parameters.rotation = 15;
        QVERIFY(canvas.document()->previewTransform(parameters));
        const auto rotated = renderMovieFrame(canvas.document()->displayProject()); QVERIFY(rotated != preview);
        const auto oldCenter = canvas.center();
        QTest::keyPress(&canvas, Qt::Key_Space);
        const QPointF press(500, 400), release(550, 430);
        mouseAt(canvas, QEvent::MouseButtonPress, press); mouseAt(canvas, QEvent::MouseMove, release);
        mouseAt(canvas, QEvent::MouseButtonRelease, release); QTest::keyRelease(&canvas, Qt::Key_Space);
        QVERIFY(canvas.center() != oldCenter); QVERIFY(canvas.document()->transformActive());
        QCOMPARE(renderMovieFrame(canvas.document()->displayProject()), rotated);
        QCOMPARE(canvas.document()->transformParameters().translation, parameters.translation);
        mouseAt(canvas, QEvent::MouseButtonPress, press, Qt::NoModifier, Qt::MiddleButton);
        mouseAt(canvas, QEvent::MouseMove, release, Qt::NoModifier, Qt::MiddleButton);
        mouseAt(canvas, QEvent::MouseButtonRelease, release, Qt::NoModifier, Qt::MiddleButton);
        QVERIFY(canvas.document()->transformActive()); QCOMPARE(renderMovieFrame(canvas.document()->displayProject()), rotated);
        QVERIFY(canvas.document()->history()->isClean());
    }
    void emptyAndLockedLayersDoNotStartTransforms() {
        Canvas canvas; prepare(canvas);
        canvas.setTool(Canvas::Tool::Transform); QVERIFY(!canvas.document()->transformActive());
        drag(canvas, {200, 170}, {250, 200}); QVERIFY(canvas.document()->history()->isClean());
        auto project = artwork(true); project.shots[0].layers[0].locked = true; canvas.setProject(project);
        canvas.setTool(Canvas::Tool::Transform); QVERIFY(!canvas.document()->transformActive());
        QCOMPARE(renderMovieFrame(canvas.project()), renderMovieFrame(project));
    }
};

QTEST_MAIN(SelectionCanvasTest)
#include "test_selection_canvas.moc"
