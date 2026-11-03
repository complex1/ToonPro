#include "canvas.h"
#include "camera_animation.h"
#include "keyframe_transform_tool.h"
#include "main_window.h"
#include "motion_controls.h"
#include "timeline_controller.h"
#include "ui_theme.h"
#include "animation_test_fixture.h"
#include <QAction>
#include <QDir>
#include <QFocusEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPointingDevice>
#include <QSignalSpy>
#include <QTabletEvent>
#include <QTemporaryDir>
#include <QtTest>
#include <cmath>
using namespace toon;
namespace {
using P = KeyframeProperty;
using Mode = KeyframeTransformTool::Mode;
using Handle = KeyframeTransformTool::MotionControlHandle;
Project fixture(bool grouped = false) {
    auto project = Project::create(); project.width = 320; project.height = 240; project.transparent = true;
    auto &shot = project.shots.first(); shot.durationFrames = 31; shot.sheet.overscan = 0; shot.layers.resize(1);
    QImage tile(tileSize, tileSize, QImage::Format_ARGB32_Premultiplied); tile.fill(Qt::transparent);
    { QPainter painter(&tile); painter.fillRect(QRect(32, 24, 64, 48), Qt::red); }
    shot.layers.first().tiles[{0, 0}] = tile;
    if (grouped) {
        auto parent = RasterLayer::create("Moving parent"); parent.group = true;
        shot.layers.first().parentId = parent.id; shot.layers.prepend(parent);
        shot.layerAnimations.append({parent.id, {64, 48}, {
            {P::PositionX, {{0, 0}, {20, 15}}}, {P::PositionY, {{0, 0}, {20, 12}}},
            {P::ScaleX, {{0, 1}, {20, 1.4}}}, {P::ScaleY, {{0, 1}, {20, .7}}},
            {P::Rotation, {{0, 0}, {20, 35}}}
        }, {}});
    }
    shot.layerAnimations.append({shot.layers.last().id, {64, 48}, {
        {P::PositionX, {{0, 0}, {20, 100}, {30, 150}}},
        {P::PositionY, {{0, 0}, {20, 20}, {30, 65}}}
    }, {}});
    auto &keys = shot.layerAnimations.last().channels.first().keys;
    keys[0].spatial = SpatialTangents{{}, {30, -35}, true};
    keys[1].spatial = SpatialTangents{{-30, -35}, {20, 35}, true};
    keys[2].spatial = SpatialTangents{{-20, -10}, {}, false};
    return project;
}
QTransform artworkView(const Canvas &canvas) {
    auto view = canvas.viewTransform();
    const auto origin = sheetGeometry(canvas.project(), canvas.shot()).output.topLeft();
    view.translate(origin.x(), origin.y());
    if (canvas.cameraView()) return shotCameraTransform(canvas.shot(), canvas.document()->currentFrame(),
        QSizeF(canvas.project().width, canvas.project().height)) * view;
    return view;
}
void show(Canvas &canvas) {
    canvas.resize(1000, 760); canvas.show(); canvas.fitSheet(); canvas.setZoom(1.6); canvas.setRotation(19);
    QCoreApplication::processEvents();
}
void mouse(Canvas &canvas, QEvent::Type type, QPointF point, Qt::MouseButtons buttons = Qt::LeftButton,
           Qt::MouseEventSource source = Qt::MouseEventNotSynthesized, Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
    QMouseEvent event(type, point, point, canvas.mapToGlobal(point),
        type == QEvent::MouseMove ? Qt::NoButton : Qt::LeftButton,
        type == QEvent::MouseButtonRelease ? Qt::NoButton : buttons, modifiers, source);
    QApplication::sendEvent(&canvas, &event);
}
void drag(Canvas &canvas, QPointF start, QPointF end) {
    mouse(canvas, QEvent::MouseButtonPress, start); mouse(canvas, QEvent::MouseMove, end);
    mouse(canvas, QEvent::MouseButtonRelease, end);
}
void enable(Canvas &canvas) {
    canvas.document()->selectLayer(canvas.shot().layers.size() - 1);
    canvas.keyframeTransformTool()->setMode(Mode::MotionPath);
    canvas.keyframeTransformTool()->setEnabled(true);
    QVERIFY(canvas.keyframeTransformTool()->enabled());
}
void selectMarker(Canvas &canvas, int frame) {
    const auto id = canvas.shot().layers.last().id;
    const auto point = artworkView(canvas).map(animationPivotWorld(canvas.shot(), id, frame));
    drag(canvas, point, point);
    QCOMPARE(canvas.keyframeTransformTool()->selectedMotionFrame(), frame);
}
Handle handle(Canvas &canvas, int frame, bool incoming) {
    for (const auto &control : canvas.keyframeTransformTool()->motionControlHandles())
        if (control.frame == frame && control.incoming == incoming) return control;
    QTest::qFail("Expected a visible cubic motion handle", __FILE__, __LINE__);
    return {};
}
bool near(QPointF a, QPointF b) { return QLineF(a, b).length() < .0001; }
}
class AdvancedMotionCanvasTests final : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { applyTheme(); }
    void earlierTangentUsesParentPoseAndCurrentCamera_data() {
        QTest::addColumn<bool>("grouped"); QTest::addColumn<bool>("camera");
        QTest::newRow("layer") << false << false;
        QTest::newRow("animated-parent") << true << false;
        QTest::newRow("camera") << false << true;
        QTest::newRow("parent-and-camera") << true << true;
    }
    void earlierTangentUsesParentPoseAndCurrentCamera() {
        QFETCH(bool, grouped); QFETCH(bool, camera);
        auto project = fixture(grouped);
        if (camera) project.shots.first().camera2D = {{8, -6}, 1.2, 17};
        Canvas canvas; canvas.setProject(project); auto *document = canvas.document();
        QVERIFY(document->setCurrentFrame(10)); show(canvas); enable(canvas); selectMarker(canvas, 20);
        const auto id = canvas.shot().layers.last().id;
        const auto parent = grouped ? worldLayerTransform(canvas.shot(), canvas.shot().layers.first().id, 20) : QTransform();
        const auto before = canvas.project().toJson(); const auto source = canvas.shot().layers.last().tiles;
        const auto original = propertyChannel(canvas.shot(), id, P::PositionX)->keys;
        const auto control = handle(canvas, 20, true); const QPointF delta(12, -9);
        const auto endpoint = animationPivotWorld(canvas.shot(), id, 20);
        const auto end = artworkView(canvas).map(parent.map(parent.inverted().map(canvas.artworkPoint(control.position)) + delta));
        const int count = document->history()->count();
        mouse(canvas, QEvent::MouseButtonPress, control.position); QVERIFY(canvas.keyframeTransformTool()->dragging());
        mouse(canvas, QEvent::MouseMove, end);
        QCOMPARE(document->currentFrame(), 10); QCOMPARE(document->history()->count(), count);
        QVERIFY(near(propertyChannel(canvas.shot(), id, P::PositionX)->keys[1].spatial->incoming, original[1].spatial->incoming + delta));
        QVERIFY(near(animationPivotWorld(canvas.shot(), id, 20), endpoint));
        QVERIFY(near(handle(canvas, 20, true).position, end));
        mouse(canvas, QEvent::MouseButtonRelease, end);
        QCOMPARE(document->history()->count(), count + 1); QCOMPARE(canvas.shot().layers.last().tiles, source);
        const auto &result = propertyChannel(canvas.shot(), id, P::PositionX)->keys;
        QCOMPARE(result.size(), original.size());
        for (int i = 0; i < original.size(); ++i) {
            QCOMPARE(result[i].frame, original[i].frame); QCOMPARE(result[i].value, original[i].value);
            QCOMPARE(result[i].interpolation, original[i].interpolation); QCOMPARE(result[i].easing, original[i].easing);
            QCOMPARE(result[i].spatial->outgoing, original[i].spatial->outgoing);
        }
        document->history()->undo(); QCOMPARE(canvas.project().toJson(), before);
    }
    void collapsedTangentsCanBeExtendedWithoutLosingEndpointDragging_data() {
        QTest::addColumn<bool>("incoming"); QTest::newRow("outgoing") << false; QTest::newRow("incoming") << true;
    }
    void collapsedTangentsCanBeExtendedWithoutLosingEndpointDragging() {
        QFETCH(bool, incoming); auto project = fixture();
        auto &keys = project.shots.first().layerAnimations.last().channels.first().keys;
        const int index = incoming ? 1 : 0, frame = keys[index].frame;
        if(incoming) keys[index].spatial->incoming = {}; else keys[index].spatial->outgoing = {};
        Canvas canvas; canvas.setProject(project); QVERIFY(canvas.document()->setCurrentFrame(10)); show(canvas); enable(canvas); selectMarker(canvas, frame);
        auto *document = canvas.document(); const auto id = canvas.shot().layers.last().id;
        const auto before = canvas.project().toJson(); const auto start = handle(canvas, frame, incoming).anchor;
        const QPointF delta(14, -21); const auto end = artworkView(canvas).map(canvas.artworkPoint(start) + delta);
        const auto modifiers = incoming ? Qt::AltModifier | Qt::ShiftModifier : Qt::AltModifier;
        mouse(canvas, QEvent::MouseButtonPress, start, Qt::LeftButton, Qt::MouseEventNotSynthesized, modifiers);
        QVERIFY(canvas.keyframeTransformTool()->dragging()); mouse(canvas, QEvent::MouseMove, end); mouse(canvas, QEvent::MouseButtonRelease, end);
        const auto &spatial = *propertyChannel(canvas.shot(), id, P::PositionX)->keys[index].spatial;
        QVERIFY(near(incoming ? spatial.incoming : spatial.outgoing, delta));
        QCOMPARE(propertyChannel(canvas.shot(), id, P::PositionX)->keys[index].value, keys[index].value);
        QCOMPARE(document->history()->count(), 1); document->history()->undo(); QCOMPARE(canvas.project().toJson(), before);
        drag(canvas, start, end);
        QVERIFY(std::abs(propertyChannel(canvas.shot(), id, P::PositionX)->keys[index].value - keys[index].value - delta.x()) < .0001);
        const auto &after = *propertyChannel(canvas.shot(), id, P::PositionX)->keys[index].spatial;
        QVERIFY(near(incoming ? after.incoming : after.outgoing, {}));
    }
    void outgoingHandleEscapeAndReturnToStartAreLossless() {
        Canvas canvas; canvas.setProject(fixture()); QVERIFY(canvas.document()->setCurrentFrame(10));
        show(canvas); enable(canvas); selectMarker(canvas, 0); auto *tool = canvas.keyframeTransformTool();
        const auto before = canvas.project().toJson(); const int count = canvas.document()->history()->count();
        const auto start = handle(canvas, 0, false).position;
        mouse(canvas, QEvent::MouseButtonPress, start); mouse(canvas, QEvent::MouseMove, start + QPointF(35, 25));
        QVERIFY(tool->dragging()); QVERIFY(canvas.project().toJson() != before);
        QTest::keyClick(&canvas, Qt::Key_Escape); QVERIFY(!tool->dragging()); QCOMPARE(canvas.project().toJson(), before);
        QCOMPARE(canvas.document()->history()->count(), count);
        mouse(canvas, QEvent::MouseButtonPress, start); mouse(canvas, QEvent::MouseMove, start + QPointF(35, 25));
        mouse(canvas, QEvent::MouseMove, start); mouse(canvas, QEvent::MouseButtonRelease, start);
        QCOMPARE(canvas.project().toJson(), before); QCOMPARE(canvas.document()->history()->count(), count);
        QVERIFY(!canvas.document()->keyframeEditActive());
    }
    void tangentHitTargetsStayConstantAcrossZoom_data() {
        QTest::addColumn<qreal>("zoom"); QTest::newRow("zoomed-out") << qreal(.8); QTest::newRow("zoomed-in") << qreal(3.1);
    }
    void tangentHitTargetsStayConstantAcrossZoom() {
        QFETCH(qreal, zoom); Canvas canvas; canvas.setProject(fixture()); QVERIFY(canvas.document()->setCurrentFrame(10));
        show(canvas); canvas.setZoom(zoom); enable(canvas); selectMarker(canvas, 0);
        const auto control = handle(canvas, 0, false);
        QLineF direction(control.anchor, control.position); direction.setLength(direction.length() + 10);
        const auto pressPoint = direction.p2();
        mouse(canvas, QEvent::MouseButtonPress, pressPoint); QVERIFY(canvas.keyframeTransformTool()->dragging());
        mouse(canvas, QEvent::MouseMove, pressPoint + QPointF(15, -10));
        QVERIFY(canvas.document()->keyframeEditActive());
        mouse(canvas, QEvent::MouseButtonRelease, pressPoint + QPointF(15, -10));
        QCOMPARE(canvas.document()->history()->count(), 1);
    }
    void lostContactAndTabletSynthesisCommitOnlyOnce() {
        Canvas canvas; canvas.setProject(fixture()); QVERIFY(canvas.document()->setCurrentFrame(10));
        show(canvas); enable(canvas); selectMarker(canvas, 20); auto *tool = canvas.keyframeTransformTool(); auto *document = canvas.document();
        auto start = handle(canvas, 20, true).position;
        mouse(canvas, QEvent::MouseButtonPress, start); mouse(canvas, QEvent::MouseMove, start + QPointF(20, 15));
        const auto painted = canvas.project().toJson();
        mouse(canvas, QEvent::MouseMove, start + QPointF(100, 100), Qt::NoButton);
        QVERIFY(!tool->dragging()); QCOMPARE(document->history()->count(), 1);
        mouse(canvas, QEvent::MouseButtonRelease, start + QPointF(100, 100)); QCOMPARE(canvas.project().toJson(), painted);
        QPointingDevice pen("Motion tangent pen", 93, QInputDevice::DeviceType::Stylus, QPointingDevice::PointerType::Pen,
            QInputDevice::Capability::Position | QInputDevice::Capability::Pressure, 1, 1);
        const auto tablet = [&](QEvent::Type type, QPointF point, Qt::MouseButtons buttons) {
            QTabletEvent event(type, &pen, point, canvas.mapToGlobal(point), buttons ? .7 : 0, 0, 0, 0, 0, 0,
                Qt::NoModifier, type == QEvent::TabletMove ? Qt::NoButton : Qt::LeftButton, buttons);
            QApplication::sendEvent(&canvas, &event); QVERIFY(event.isAccepted());
        };
        start = handle(canvas, 20, false).position;
        tablet(QEvent::TabletPress, start, Qt::LeftButton); QVERIFY(tool->dragging());
        mouse(canvas, QEvent::MouseButtonRelease, start + QPointF(100, 100), Qt::NoButton, Qt::MouseEventSynthesizedByQt);
        QVERIFY(tool->dragging()); tablet(QEvent::TabletMove, start + QPointF(-20, 12), Qt::LeftButton);
        const auto final = canvas.project().toJson(); tablet(QEvent::TabletMove, start + QPointF(100, 100), Qt::NoButton);
        QVERIFY(!tool->dragging()); QCOMPARE(document->history()->count(), 2);
        tablet(QEvent::TabletRelease, start + QPointF(100, 100), Qt::NoButton); QCOMPARE(canvas.project().toJson(), final);
    }
    void navigationResolvesTangentAndOverlaysStayOutOfExports() {
        Canvas canvas; canvas.setProject(fixture()); TimelineController timeline(&canvas); QVERIFY(timeline.setFrame(10));
        show(canvas); enable(canvas); selectMarker(canvas, 20);
        const auto original = renderMovieFrame(canvas.project(), 10); const auto source = canvas.project().toJson();
        const auto controls = canvas.keyframeTransformTool()->motionControlHandles(); QCOMPARE(controls.size(), 2);
        QCOMPARE(renderMovieFrame(canvas.project(), 10), original); QCOMPARE(canvas.project().toJson(), source);
        const auto start = handle(canvas, 20, true).position;
        mouse(canvas, QEvent::MouseButtonPress, start); mouse(canvas, QEvent::MouseMove, start + QPointF(30, -15));
        QVERIFY(timeline.setFrame(12)); QVERIFY(!canvas.keyframeTransformTool()->dragging());
        QVERIFY(!canvas.document()->keyframeEditActive()); QCOMPARE(canvas.document()->history()->count(), 1);
        canvas.document()->setKeyframeSourceEditing(true); QVERIFY(canvas.keyframeTransformTool()->motionControlHandles().isEmpty());
        canvas.document()->setKeyframeSourceEditing(false); canvas.setAnimationPlayback(true);
        QVERIFY(canvas.keyframeTransformTool()->motionControlHandles().isEmpty()); canvas.setAnimationPlayback(false);
        canvas.keyframeTransformTool()->setMode(Mode::Pivot); QVERIFY(canvas.keyframeTransformTool()->motionControlHandles().isEmpty());
    }
    void offTimeUnexposedTangentKeepsDrawingAndPlayhead() {
        auto project = animation_fixture::project(); auto &shot = project.shots.first();
        const auto id = shot.animationTracks.first().drawings.first().groupLayerId;
        shot.layerAnimations = {{id, {64, 64}, {{P::PositionX, {{0, 0}, {5, 50}}}, {P::PositionY, {{0, 0}, {5, 20}}}}, {}}};
        auto &keys = shot.layerAnimations.first().channels.first().keys;
        keys[0].spatial = SpatialTangents{{}, {18, -25}, true}; keys[1].spatial = SpatialTangents{{-18, -25}, {}, false};
        Canvas canvas; canvas.setProject(project); auto *document = canvas.document(); QVERIFY(document->setCurrentFrame(1));
        const int selected = animation_fixture::layerIndex(canvas.shot(), id); document->selectLayer(selected); show(canvas);
        auto *tool = canvas.keyframeTransformTool(); tool->setMode(Mode::MotionPath); tool->setEnabled(true); QVERIFY(tool->enabled());
        const auto point = artworkView(canvas).map(animationPivotWorld(canvas.shot(), id, 5)); drag(canvas, point, point);
        QCOMPARE(tool->selectedMotionFrame(), 5); const auto control = handle(canvas, 5, true);
        const auto tracks = canvas.shot().animationTracks; const auto otherDrawing = renderMovieFrame(canvas.project(), 5);
        drag(canvas, control.position, control.position + QPointF(20, -10));
        QCOMPARE(document->currentFrame(), 1); QCOMPARE(document->activeLayer(), selected); QCOMPARE(canvas.shot().animationTracks, tracks);
        QCOMPARE(document->history()->count(), 1); QCOMPARE(renderMovieFrame(canvas.project(), 5), otherDrawing);
    }
    void nativePendingTangentUndoAndSaveResolveOnce() {
        QTemporaryDir directory; QVERIFY(directory.isValid()); QString error; const auto path = directory.filePath("motion.toonproj");
        QVERIFY2(saveProject(fixture(), path, &error), qPrintable(error));
        MainWindow window(directory.filePath("workspace.ini")); QVERIFY(window.openProject(path)); window.resize(1320, 900); window.show();
        auto *canvas = window.canvas(); auto *timeline = window.findChild<TimelineController *>(); QVERIFY(timeline);
        QVERIFY(timeline->setFrame(10)); canvas->fitSheet(); enable(*canvas); selectMarker(*canvas, 20);
        const auto original = canvas->project().toJson(); auto *tool = canvas->keyframeTransformTool();
        auto *undo = window.findChild<QAction *>("undoAction"); auto *save = window.findChild<QAction *>("saveProjectAction"); QVERIFY(undo && save);
        auto start = handle(*canvas, 20, true).position; auto end = start + QPointF(30, -18);
        mouse(*canvas, QEvent::MouseButtonPress, start); mouse(*canvas, QEvent::MouseMove, end); QVERIFY(tool->dragging());
        QVERIFY(undo->isEnabled()); undo->trigger(); QVERIFY(!tool->dragging()); QCOMPARE(canvas->project().toJson(), original);
        mouse(*canvas, QEvent::MouseButtonRelease, end); QCOMPARE(timeline->document()->history()->count(), 0);
        mouse(*canvas, QEvent::MouseButtonPress, start); mouse(*canvas, QEvent::MouseMove, end); QVERIFY(tool->dragging());
        save->trigger(); QVERIFY(!tool->dragging()); QCOMPARE(timeline->document()->history()->count(), 1); QVERIFY(timeline->document()->history()->isClean());
        const auto saved = loadProject(path, &error); QVERIFY2(saved.has_value(), qPrintable(error));
        QCOMPARE(saved->toJson(), canvas->project().toJson()); QCOMPARE(timeline->document()->currentFrame(), 10);
        mouse(*canvas, QEvent::MouseButtonRelease, end); QCOMPARE(timeline->document()->history()->count(), 1); QVERIFY(window.close());
    }
    void orientedTransformScalesInItsVisibleAxesAndRotationStaysAnOffset() {
        auto project = fixture(true); auto &animation = project.shots.first().layerAnimations.last(); animation.orientAlongPath = true;
        animation.channels.append({P::Rotation, {{0, 20}}}); animation.channels.append({P::ScaleX, {{0, 1.3}}});
        animation.channels.append({P::ScaleY, {{0, .8}}});
        project.shots.first().camera2D = {{8, -6}, 1.2, 17};
        Canvas canvas; canvas.setProject(project); auto *document = canvas.document(); QVERIFY(document->setCurrentFrame(10));
        show(canvas); document->selectLayer(canvas.shot().layers.size() - 1); auto *tool = canvas.keyframeTransformTool();
        tool->setMode(Mode::Transform); tool->setEnabled(true); QVERIFY(tool->enabled());
        const auto id = canvas.shot().layers.last().id; const auto world = worldLayerTransform(canvas.shot(), id, 10);
        const auto before = canvas.project().toJson(); const QPointF pivot(64, 48), corner(96, 72);
        const auto target = pivot + QPointF((corner.x() - pivot.x()) * 1.2, (corner.y() - pivot.y()) * .7);
        drag(canvas, tool->box()[2], artworkView(canvas).map(world.map(target)));
        QVERIFY(std::abs(evaluateProperty(canvas.shot(), id, P::ScaleX, 10) - 1.56) < .0001);
        QVERIFY(std::abs(evaluateProperty(canvas.shot(), id, P::ScaleY, 10) - .56) < .0001);
        QCOMPARE(evaluateProperty(canvas.shot(), id, P::Rotation, 10), 20.);
        document->history()->undo(); QCOMPARE(canvas.project().toJson(), before);
        const auto parent = worldLayerTransform(canvas.shot(), canvas.shot().layers.first().id, 10);
        const auto from = tool->rotationHandle();
        const auto center = pivot + QPointF(evaluateProperty(canvas.shot(), id, P::PositionX, 10), evaluateProperty(canvas.shot(), id, P::PositionY, 10));
        const auto local = parent.inverted().map(canvas.artworkPoint(from)); QTransform rotation; rotation.rotate(30);
        drag(canvas, from, artworkView(canvas).map(parent.map(center + rotation.map(local - center))));
        QVERIFY(std::abs(evaluateProperty(canvas.shot(), id, P::Rotation, 10) - 50) < .0001);
        QVERIFY(layerAnimation(canvas.shot(), id)->orientAlongPath);
    }
    void renderPreview() {
        const auto output = qEnvironmentVariable("TOON_UI_PREVIEW"); if (output.isEmpty()) QSKIP("Opt-in motion curve preview.");
        Canvas canvas; canvas.setProject(fixture()); QVERIFY(canvas.document()->setCurrentFrame(10)); show(canvas);
        canvas.setRotation(0); canvas.setZoom(2.2); enable(canvas); selectMarker(canvas, 20); QCoreApplication::processEvents();
        QDir().mkpath(output); QVERIFY(canvas.grab().save(output + "/advanced-motion-canvas.png"));
    }
};
QTEST_MAIN(AdvancedMotionCanvasTests)
#include "advanced_motion_canvas_tests.moc"
