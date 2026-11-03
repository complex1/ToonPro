#include "canvas.h"
#include "camera_tool.h"
#include "timeline_controller.h"
#include "ui_theme.h"
#include <QMouseEvent>
#include <QPointingDevice>
#include <QTabletEvent>
#include <QtTest>
#include <cmath>
using namespace toon;
namespace {
Project fixture() {
    auto p = Project::create(); p.width = 160; p.height = 120; p.transparent = true;
    p.shots[0].layers.resize(1); p.shots[0].durationFrames = 30;
    return p;
}
void show(Canvas &canvas) {
    canvas.resize(900, 720); canvas.show(); canvas.setZoom(1.4); canvas.setRotation(23);
    canvas.centerOn(sheetGeometry(canvas.project(), canvas.shot()).output.center());
    QCoreApplication::processEvents();
}
QPointF outputPoint(Canvas &canvas, QPointF point) {
    return canvas.viewTransform().map(sheetGeometry(canvas.project(), canvas.shot()).output.topLeft() + point);
}
void mouse(Canvas &canvas, QEvent::Type type, QPointF position, Qt::MouseButtons buttons = Qt::LeftButton) {
    QMouseEvent event(type, position, position, canvas.mapToGlobal(position),
        type == QEvent::MouseMove ? Qt::NoButton : Qt::LeftButton,
        type == QEvent::MouseButtonRelease ? Qt::NoButton : buttons, Qt::NoModifier);
    QApplication::sendEvent(&canvas, &event);
}
void drag(Canvas &canvas, QPointF start, QPointF end) {
    mouse(canvas, QEvent::MouseButtonPress, start); mouse(canvas, QEvent::MouseMove, end); mouse(canvas, QEvent::MouseButtonRelease, end);
}
}
class CameraCanvasTests final : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { applyTheme(); }
    void inverseInputAndBrushFootprintFollowCamera() {
        auto p = fixture(); p.shots[0].camera2D = {{20, -10}, 2, 90};
        Canvas canvas; canvas.setProject(p); show(canvas);
        const auto center = outputPoint(canvas, {80, 60});
        QVERIFY(QLineF(canvas.artworkPoint(center), {100, 50}).length() < .00001);
        QVERIFY(QLineF(canvas.artworkPoint(outputPoint(canvas, {80, 40})), {110, 50}).length() < .00001);
        canvas.brush.size = 8; canvas.brush.strokeOpacity = 1; canvas.brush.opacity = 1; canvas.brush.pressureSize = false;
        canvas.brush.sizeDynamics.control = BrushControl::Off;
        mouse(canvas, QEvent::MouseMove, center, Qt::NoButton);
        QVERIFY(std::abs(canvas.brushCursorRect().width() - 8 * 2 * 1.4) < .01);
        drag(canvas, center, outputPoint(canvas, {80, 40}));
        const auto tiles = canvas.shot().layers[0].tiles; QVERIFY(tiles.contains({0, 0}));
        QVERIFY(qAlpha(tiles[{0, 0}].pixel(100, 50)) > 200);
        QVERIFY(qAlpha(tiles[{0, 0}].pixel(110, 50)) > 200);
        QCOMPARE(qAlpha(tiles[{0, 0}].pixel(80, 60)), 0);
        const auto movie = renderMovieFrame(canvas.project());
        QVERIFY(qAlpha(movie.pixel(80, 60)) > 200); QVERIFY(qAlpha(movie.pixel(80, 40)) > 200);
        const auto history = canvas.document()->history()->count(); const auto source = canvas.project().toJson();
        canvas.setCameraView(false);
        QVERIFY(QLineF(canvas.artworkPoint(center), {80, 60}).length() < .00001);
        QCOMPARE(canvas.project().toJson(), source); QCOMPARE(canvas.document()->history()->count(), history);
    }
    void cameraOnlyFramesRefreshCanvas() {
        auto p = fixture(); p.shots[0].camera2DKeys = {{0, {}}, {20, {{40, 0}, 2, 20}}};
        Canvas canvas; canvas.setProject(p); show(canvas);
        canvas.grab(); const auto count = canvas.artworkRenderCount();
        QVERIFY(canvas.document()->setCurrentFrame(10)); canvas.grab();
        QVERIFY(canvas.artworkRenderCount() > count);
        const auto expected = QPointF(100, 60);
        QVERIFY(QLineF(canvas.artworkPoint(outputPoint(canvas, {80, 60})), expected).length() < .00001);
    }
    void framingMoveZoomRotateAndUndo() {
        Canvas canvas; canvas.setProject(fixture()); TimelineController timeline(&canvas); show(canvas);
        QVERIFY(timeline.selectCamera()); QVERIFY(timeline.setCameraEnabled(true)); QVERIFY(timeline.setFrame(10));
        const auto zoom = canvas.zoom(), rotation = canvas.rotation(); const auto navCenter = canvas.center();
        timeline.editCameraFraming(); auto *tool = canvas.cameraTool(); QVERIFY(tool->enabled()); QVERIFY(!canvas.cameraView());
        const auto before = canvas.project().toJson(); const int count = canvas.document()->history()->count();
        auto box = tool->box(); const auto start = (box[0] + box[2]) / 2;
        drag(canvas, start, outputPoint(canvas, {104, 72}));
        QVERIFY(QLineF(evaluateShotCamera(canvas.shot(), 10).position, {24, 12}).length() < .00001);
        QCOMPARE(canvas.document()->history()->count(), count + 1);
        QCOMPARE(canvas.zoom(), zoom); QCOMPARE(canvas.rotation(), rotation); QCOMPARE(canvas.center(), navCenter);
        canvas.document()->history()->undo(); QCOMPARE(canvas.project().toJson(), before);
        box = tool->box(); const auto center = (box[0] + box[2]) / 2;
        drag(canvas, box[2], center + (box[2] - center) * .5);
        QVERIFY(std::abs(evaluateShotCamera(canvas.shot(), 10).zoom - 2) < .00001);
        canvas.document()->history()->undo(); QCOMPARE(canvas.project().toJson(), before);
        const auto handle = tool->rotationHandle(); QTransform turn; turn.rotate(30);
        drag(canvas, handle, center + turn.map(handle - center));
        QVERIFY(std::abs(evaluateShotCamera(canvas.shot(), 10).rotation - 30) < .00001);
        canvas.document()->history()->undo(); QCOMPARE(canvas.project().toJson(), before);
    }
    void cameraGestureCancellationReturnAndFrameTransition() {
        Canvas canvas; canvas.setProject(fixture()); TimelineController timeline(&canvas); show(canvas);
        QVERIFY(timeline.selectCamera()); QVERIFY(timeline.setCameraEnabled(true)); QVERIFY(timeline.setFrame(7)); timeline.editCameraFraming();
        auto *tool = canvas.cameraTool(); const auto before = canvas.project().toJson(); const int count = canvas.document()->history()->count();
        auto box = tool->box(); const auto start = (box[0] + box[2]) / 2, end = start + QPointF(40, 25);
        mouse(canvas, QEvent::MouseButtonPress, start); mouse(canvas, QEvent::MouseMove, end);
        QVERIFY(canvas.document()->cameraEditActive()); QTest::keyClick(&canvas, Qt::Key_Escape);
        QVERIFY(!tool->dragging()); QCOMPARE(canvas.project().toJson(), before);
        mouse(canvas, QEvent::MouseButtonPress, start); mouse(canvas, QEvent::MouseMove, end);
        mouse(canvas, QEvent::MouseMove, start); mouse(canvas, QEvent::MouseButtonRelease, start);
        QCOMPARE(canvas.project().toJson(), before); QCOMPARE(canvas.document()->history()->count(), count);
        mouse(canvas, QEvent::MouseButtonPress, start); mouse(canvas, QEvent::MouseMove, end);
        QVERIFY(timeline.setFrame(12)); QVERIFY(!tool->dragging()); QVERIFY(!canvas.document()->cameraEditActive());
        QCOMPARE(canvas.document()->history()->count(), count + 1);
        QCOMPARE(canvas.shot().camera2DKeys.last().frame, 7);
        const auto result = canvas.project().toJson(); mouse(canvas, QEvent::MouseButtonRelease, end); QCOMPARE(canvas.project().toJson(), result);
    }
    void fullRotationDoesNotCollapseToNoOp() {
        Canvas canvas; canvas.setProject(fixture()); TimelineController timeline(&canvas); show(canvas);
        QVERIFY(timeline.selectCamera()); timeline.editCameraFraming();
        const auto box = canvas.cameraTool()->box(); const auto center = (box[0] + box[2]) / 2;
        const auto start = canvas.cameraTool()->rotationHandle(); mouse(canvas, QEvent::MouseButtonPress, start);
        for (int angle : {90, 180, 270, 360}) {
            QTransform turn; turn.rotate(angle); mouse(canvas, QEvent::MouseMove, center + turn.map(start - center));
        }
        mouse(canvas, QEvent::MouseButtonRelease, start);
        QVERIFY(std::abs(canvas.shot().camera2D.rotation - 360) < .00001);
        QCOMPARE(canvas.document()->history()->count(), 1);
    }
    void tabletLossOfContactCommitsOnce() {
        Canvas canvas; canvas.setProject(fixture()); TimelineController timeline(&canvas); show(canvas);
        QVERIFY(timeline.selectCamera()); timeline.editCameraFraming();
        const auto box = canvas.cameraTool()->box(); const auto start = (box[0] + box[2]) / 2;
        QPointingDevice pen("Camera test pen", 88, QInputDevice::DeviceType::Stylus, QPointingDevice::PointerType::Pen,
            QInputDevice::Capability::Position | QInputDevice::Capability::Pressure, 1, 1);
        const auto send = [&](QEvent::Type type, QPointF point, Qt::MouseButtons buttons) {
            QTabletEvent event(type, &pen, point, canvas.mapToGlobal(point), buttons ? .7 : 0, 0, 0, 0, 0, 0,
                Qt::NoModifier, type == QEvent::TabletMove ? Qt::NoButton : Qt::LeftButton, buttons);
            QApplication::sendEvent(&canvas, &event);
        };
        send(QEvent::TabletPress, start, Qt::LeftButton); send(QEvent::TabletMove, start + QPointF(30, 12), Qt::LeftButton);
        send(QEvent::TabletMove, start + QPointF(90, 90), Qt::NoButton);
        QVERIFY(!canvas.cameraTool()->dragging()); QCOMPARE(canvas.document()->history()->count(), 1);
        const auto result = canvas.project().toJson(); send(QEvent::TabletRelease, start + QPointF(90, 90), Qt::NoButton);
        QCOMPARE(canvas.project().toJson(), result);
    }
};
QTEST_MAIN(CameraCanvasTests)
#include "camera_canvas_tests.moc"
