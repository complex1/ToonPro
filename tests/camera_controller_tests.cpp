#include "canvas.h"
#include "camera_tool.h"
#include "timeline_controller.h"
#include "scene3d_controller.h"
#include <QSignalSpy>
#include <QtTest>
using namespace toon;
class CameraControllerTests final : public QObject {
    Q_OBJECT
private slots:
    void cameraSelectionBatchKeysAndPlayback() {
        auto project = Project::create(); project.width = 160; project.height = 120;
        Canvas canvas; canvas.setProject(project); TimelineController timeline(&canvas);
        const auto active = canvas.document()->activeLayer();
        QVERIFY(timeline.selectCamera()); QCOMPARE(canvas.document()->activeLayer(), active);
        QVERIFY(timeline.setCameraKey()); QVERIFY(timeline.setFrame(10));
        QVERIFY(timeline.beginCameraEdit()); QVERIFY(timeline.previewShotCamera({{30, 20}, 2, 15})); timeline.finishCameraEdit();
        QCOMPARE(canvas.shot().camera2DKeys.size(), 2);
        TimelineKeyRef key; key.kind = TimelineKeyKind::Camera2D; key.frame = 10;
        timeline.setSelectedKeys({key}); QCOMPARE(timeline.selectedKeys().size(), 1);
        QVERIFY(timeline.copySelectedKeys()); QVERIFY(timeline.canPasteKeys());
        QVERIFY(timeline.setFrame(20)); QVERIFY(timeline.pasteKeys());
        QCOMPARE(canvas.shot().camera2DKeys.size(), 3); QCOMPARE(timeline.selectedKeys().first().frame, 20);
        QVERIFY(timeline.moveSelectedKeys(3)); QCOMPARE(canvas.shot().camera2DKeys.last().frame, 23);
        QVERIFY(timeline.deleteSelectedKeys()); QCOMPARE(canvas.shot().camera2DKeys.size(), 2);
        QCOMPARE(canvas.document()->activeLayer(), active); QCOMPARE(timeline.currentFrame(), 20);
        timeline.editCameraFraming(); QVERIFY(canvas.cameraTool()->enabled()); QVERIFY(!canvas.cameraView());
        timeline.setPlaying(true); QVERIFY(timeline.playing()); QVERIFY(canvas.cameraView()); QVERIFY(!canvas.cameraTool()->enabled());
        timeline.setPlaying(false);
    }
    void draftsResolveOrCancelBeforeTimeAndToolsChange() {
        Canvas canvas; canvas.setProject(Project::create()); TimelineController timeline(&canvas);
        QVERIFY(timeline.selectCamera()); QVERIFY(timeline.setCameraEnabled(true)); QVERIFY(timeline.setFrame(10));
        const auto original = canvas.project().toJson(); const auto count = canvas.document()->history()->count();
        QVERIFY(timeline.beginCameraEdit()); QVERIFY(timeline.previewShotCamera({{13, 4}, 1.25, -12}));
        timeline.cancelEditing(); QCOMPARE(canvas.project().toJson(), original); QCOMPARE(canvas.document()->history()->count(), count);
        QVERIFY(timeline.beginCameraEdit()); QVERIFY(timeline.previewShotCamera({{18, -4}, 1.5, 20}));
        QVERIFY(timeline.setFrame(12)); QVERIFY(!canvas.document()->cameraEditActive()); QCOMPARE(canvas.document()->history()->count(), count + 1);
        QCOMPARE(canvas.shot().camera2DKeys.last().frame, 10);
        QVERIFY(timeline.beginCameraEdit()); QVERIFY(timeline.previewShotCamera({{21, 5}, 1.5, 20}));
        canvas.setTool(Canvas::Tool::Eraser); QVERIFY(!canvas.document()->cameraEditActive());
        QCOMPARE(canvas.shot().camera2DKeys.last().frame, 12);
    }
    void sourceNavigationDoesNotChangeCameraAndReloadResetsTransientMode() {
        auto project = Project::create(); project.shots[0].camera2D = {{90, -20}, 1.3, 17};
        Canvas canvas; canvas.setProject(project); TimelineController timeline(&canvas);
        QVERIFY(timeline.selectCamera()); const auto original = canvas.project().toJson();
        canvas.setZoom(.7); canvas.setRotation(30); canvas.centerOn({200, 300}); timeline.setSourceEditing(true);
        QCOMPARE(canvas.project().toJson(), original); QVERIFY(canvas.cameraView());
        timeline.setCameraView(false); QCOMPARE(canvas.project().toJson(), original);
        canvas.setProject(Project::create()); QVERIFY(!timeline.cameraSelected()); QVERIFY(canvas.cameraView());
        QVERIFY(!canvas.cameraTool()->enabled()); QVERIFY(!timeline.canPasteKeys());
    }
    void sceneSelectionKeepsArtworkUntilExplicitCameraTool() {
        Canvas canvas; canvas.setProject(Project::create()); auto *scene = canvas.scene3D();
        QVERIFY(scene->addModel(builtInModels3D().first()));
        const auto sceneId = scene->layerId(); QVERIFY(!sceneId.isEmpty());
        canvas.document()->selectLayer(0); const auto active = canvas.document()->activeLayer(); TimelineController timeline(&canvas);
        QVERIFY(timeline.selectCamera(sceneId)); QCOMPARE(canvas.document()->activeLayer(), active);
        QVERIFY(timeline.setCameraEnabled(true)); QVERIFY(timeline.setFrame(10)); QVERIFY(timeline.beginCameraEdit());
        auto camera = evaluateSceneCamera(canvas.shot(), sceneId, 10); camera.yaw = 70;
        QVERIFY(timeline.previewSceneCamera(camera)); timeline.finishCameraEdit();
        QCOMPARE(evaluateSceneCamera(canvas.shot(), sceneId, 10).yaw, 70.f);
        timeline.editCameraFraming(); QCOMPARE(canvas.tool(), Canvas::Tool::SceneCamera);
        QCOMPARE(timeline.selectedPropertyLayerId(), sceneId); QVERIFY(timeline.cameraSelected());
    }
    void returningToArtworkStopsCameraFraming() {
        Canvas canvas; canvas.setProject(Project::create()); TimelineController timeline(&canvas);
        QVERIFY(timeline.selectCamera()); timeline.editCameraFraming();
        QVERIFY(canvas.cameraTool()->enabled());
        timeline.setSourceEditing(true); QVERIFY(!canvas.cameraTool()->enabled());
        QVERIFY(timeline.sourceEditing());
        timeline.editCameraFraming(); QVERIFY(canvas.cameraTool()->enabled());
        QVERIFY(timeline.addTrack(true, DrawingTemplate::Simple, "Character"));
        QVERIFY(!timeline.cameraSelected()); QVERIFY(!canvas.cameraTool()->enabled());
        QVERIFY(!canvas.document()->activeAnimationTrackId().isEmpty());
        QCOMPARE(timeline.selectedKeys().size(), 0);
    }
};
QTEST_MAIN(CameraControllerTests)
#include "camera_controller_tests.moc"
