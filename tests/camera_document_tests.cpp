#include "document.h"
#include <QSignalSpy>
#include <QtTest>
#include <limits>
using namespace toon;
namespace {
Project fixture() {
    auto project = Project::create(); project.width = 160; project.height = 100; project.shots[0].durationFrames = 60;
    auto scene = RasterLayer::create("City"); scene.kind = LayerKind::Scene3D; scene.scene3d = Scene3D(); project.shots[0].layers.append(scene);
    return project;
}
TimelineKeyRef camera2D(int frame) { return {{}, KeyframeProperty::PositionX, {}, frame, TimelineKeyKind::Camera2D}; }
TimelineKeyRef camera3D(const QString &id, int frame) { return {id, KeyframeProperty::PositionX, {}, frame, TimelineKeyKind::Camera3D}; }
}
class CameraDocumentTests final : public QObject {
    Q_OBJECT
private slots:
    void staticCameraAndAnimationEditsUndoWithoutMovingEditor() {
        Document document; const auto project = fixture(); document.load(project); QVERIFY(document.setCurrentFrame(10));
        const Camera2D camera{{12, -8}, 1.5, 20}; QVERIFY(document.setShotCamera(camera));
        QCOMPARE(document.history()->count(), 1); QCOMPARE(document.currentFrame(), 10); QCOMPARE(document.activeLayer(), 0);
        document.history()->undo(); QCOMPARE(document.project().shots[0].camera2D, Camera2D());
        document.history()->redo(); QCOMPARE(document.project().shots[0].camera2D, camera);
        QVERIFY(document.enableShotCameraAnimation()); QCOMPARE(document.project().shots[0].camera2DKeys.size(), 2);
        QCOMPARE(document.project().shots[0].camera2DKeys[0].frame, 0); QCOMPARE(document.project().shots[0].camera2DKeys[1].frame, 10);
        QVERIFY(document.setCurrentFrame(20)); auto next = camera; next.position = {112, 32}; next.zoom = 2.5;
        QVERIFY(document.setShotCamera(next)); QVERIFY(document.setCurrentFrame(15));
        const auto evaluated = evaluateShotCamera(document.project().shots[0], 15);
        QCOMPARE(evaluated.position, QPointF(62, 12));
        QVERIFY(document.enableShotCameraAnimation(false)); QVERIFY(document.project().shots[0].camera2DKeys.isEmpty());
        QCOMPARE(document.project().shots[0].camera2D, evaluated);
        document.history()->undo(); QCOMPARE(document.project().shots[0].camera2DKeys.size(), 3);
        QCOMPARE(document.currentFrame(), 15); QCOMPARE(document.activeLayer(), 0);
    }
    void gestureCommitsOnceCancelsAndReturningToOriginCreatesNoKey() {
        Document document; document.load(fixture()); QVERIFY(document.enableShotCameraAnimation()); QVERIFY(document.setCurrentFrame(7));
        const auto original = document.project().toJson(); const auto pose = evaluateShotCamera(document.project().shots[0], 7);
        const int history = document.history()->count();
        QVERIFY(document.beginCameraEdit()); QVERIFY(document.editingGestureActive()); QVERIFY(!document.setCurrentFrame(8));
        auto moved = pose; moved.position = {50, 30};
        QVERIFY(document.previewShotCamera(moved)); QCOMPARE(document.history()->count(), history);
        QVERIFY(document.previewShotCamera(pose)); QCOMPARE(document.project().toJson(), original);
        document.commitCameraEdit(); QCOMPARE(document.history()->count(), history); QVERIFY(!document.cameraEditActive());
        QVERIFY(document.beginCameraEdit()); QVERIFY(document.previewShotCamera(moved)); document.cancelCameraEdit();
        QCOMPARE(document.project().toJson(), original); QCOMPARE(document.history()->count(), history);
        QVERIFY(document.beginCameraEdit()); QVERIFY(document.previewShotCamera(moved)); moved.zoom = 3;
        QVERIFY(document.previewShotCamera(moved)); document.commitCameraEdit();
        QCOMPARE(document.history()->count(), history + 1); QCOMPARE(evaluateShotCamera(document.project().shots[0], 7), moved);
        QCOMPARE(document.currentFrame(), 7); QCOMPARE(document.activeLayer(), 0);
        document.history()->undo(); QCOMPARE(document.project().toJson(), original);
    }
    void sceneFlagsLocksAndGestureHistory() {
        Document document; auto project = fixture(); const auto id = project.shots[0].layers.last().id; document.load(project);
        QVERIFY(document.enableSceneCameraAnimation(id)); QVERIFY(document.setCurrentFrame(10));
        const auto original = document.project().toJson(); const int history = document.history()->count();
        const auto start = evaluateSceneCamera(document.project().shots[0], id, 10); auto moved = start;
        moved.yaw = 150; moved.distance = 12;
        QVERIFY(document.beginCameraEdit(id)); QVERIFY(document.previewSceneCamera(moved));
        QVERIFY(document.previewSceneCamera(start)); document.commitCameraEdit();
        QCOMPARE(document.project().toJson(), original); QCOMPARE(document.history()->count(), history);
        QVERIFY(document.beginCameraEdit(id)); QVERIFY(document.previewSceneCamera(moved)); document.commitCameraEdit();
        QCOMPARE(document.history()->count(), history + 1); QCOMPARE(document.activeLayer(), 0);
        moved.orthographic = true; moved.locked = true; QVERIFY(document.setSceneCamera(id, moved));
        QVERIFY(!document.beginCameraEdit(id)); QVERIFY(!document.removeSceneCameraKeyframe(id, 10));
        auto invalid = moved; invalid.yaw += 10; QVERIFY(!document.setSceneCamera(id, invalid));
        moved.locked = false; QVERIFY(document.setSceneCamera(id, moved));
        QVERIFY(evaluateSceneCamera(document.project().shots[0], id, 0).orthographic);
        QCOMPARE(evaluateSceneCamera(document.project().shots[0], id, 0).yaw, start.yaw);
        QVERIFY(document.enableSceneCameraAnimation(id, false));
        QVERIFY(!sceneCameraAnimation(document.project().shots[0], id)); QCOMPARE(evaluateSceneCamera(document.project().shots[0], id, 0), moved);
        document.history()->undo(); QVERIFY(sceneCameraAnimation(document.project().shots[0], id));
    }
    void deletingLastKeyBakesDisplayedPoseAndUndoRestoresIt() {
        Document document; auto project = fixture(); auto &shot = project.shots[0]; const auto sceneId = shot.layers.last().id;
        shot.camera2DKeys = {{10, {{50, 30}, 2, 90}}};
        auto scenePose = sceneCameraPose(shot.layers.last().scene3d->camera); scenePose.yaw = 170;
        shot.sceneCameraAnimations = {{sceneId, {{10, scenePose}}}}; document.load(project); QVERIFY(document.setCurrentFrame(20));
        const auto pose2D = evaluateShotCamera(shot, 20); const auto pose3D = evaluateSceneCamera(shot, sceneId, 20);
        QVERIFY(document.removeTimelineKeys({camera2D(10), camera3D(sceneId, 10)}));
        QVERIFY(document.project().shots[0].camera2DKeys.isEmpty()); QVERIFY(document.project().shots[0].sceneCameraAnimations.isEmpty());
        QCOMPARE(evaluateShotCamera(document.project().shots[0], 20), pose2D); QCOMPARE(evaluateSceneCamera(document.project().shots[0], sceneId, 20), pose3D);
        document.history()->undo(); QCOMPARE(document.project().toJson(), project.toJson());
        QVERIFY(document.removeShotCameraKeyframe(10)); QCOMPARE(document.project().shots[0].camera2D, pose2D);
        QVERIFY(document.removeSceneCameraKeyframe(sceneId, 10)); QCOMPARE(evaluateSceneCamera(document.project().shots[0], sceneId, 20), pose3D);
    }
    void mixedTimelineMoveCopyPasteAreAtomicAndKeepEditorSelection() {
        Document document; const auto project = fixture(); const auto raster = project.shots[0].layers.first().id, scene = project.shots[0].layers.last().id;
        document.load(project); QVERIFY(document.setKeyframeValue(raster, KeyframeProperty::PositionX, 0));
        QVERIFY(document.enableShotCameraAnimation()); QVERIFY(document.enableSceneCameraAnimation(scene));
        QVERIFY(document.setCurrentFrame(10)); QVERIFY(document.setKeyframeValue(raster, KeyframeProperty::PositionX, 30));
        QVERIFY(document.setShotCamera({{40, 5}, 2, 30})); auto pose3D = evaluateSceneCamera(document.project().shots[0], scene, 10); pose3D.yaw = 80;
        QVERIFY(document.setSceneCamera(scene, pose3D));
        const QVector<TimelineKeyRef> refs{{raster, KeyframeProperty::PositionX, {}, 10}, camera2D(10), camera3D(scene, 10)};
        TimelineKeyClipboard clipboard; QVERIFY(document.copyTimelineKeys(refs, &clipboard)); QCOMPARE(clipboard.keys.size(), 3);
        const auto original = document.project().toJson(); const int history = document.history()->count();
        QVERIFY(document.moveTimelineKeys(refs, 3)); QCOMPARE(document.history()->count(), history + 1);
        QCOMPARE(document.project().shots[0].camera2DKeys.last().frame, 13); QCOMPARE(sceneCameraAnimation(document.project().shots[0], scene)->keys.last().frame, 13);
        QCOMPARE(document.currentFrame(), 10); QCOMPARE(document.activeLayer(), 0);
        document.history()->undo(); QCOMPARE(document.project().toJson(), original);
        QVERIFY(document.pasteTimelineKeys(clipboard, 20)); QCOMPARE(document.project().shots[0].camera2DKeys.last().frame, 20);
        const auto pasted = document.project().toJson(); QVERIFY(!document.pasteTimelineKeys(clipboard, 20)); QCOMPARE(document.project().toJson(), pasted);
        QVERIFY(!document.moveTimelineKeys(refs, -10)); QCOMPARE(document.project().toJson(), pasted);
        auto malformed = clipboard; malformed.keys[1].camera2D->pose.zoom = std::numeric_limits<qreal>::quiet_NaN();
        QVERIFY(!document.pasteTimelineKeys(malformed, 30)); QCOMPARE(document.project().toJson(), pasted);
        QCOMPARE(document.currentFrame(), 10); QCOMPARE(document.activeLayer(), 0);
    }
    void lockedSceneRejectsMixedEditButAllowsCopyAndDeletionPrunesKeys() {
        auto project = fixture(); auto &shot = project.shots[0]; const auto id = shot.layers.last().id;
        shot.camera2DKeys = {{0, {}}, {10, {{1, 2}, 2, 3}}};
        shot.sceneCameraAnimations = {{id, {{0, {}}, {10, {{0, 1, 0}, 60, 10, 4, 45, 3}}}}};
        shot.layers.last().scene3d->camera.locked = true; Document document; document.load(project);
        const QVector<TimelineKeyRef> refs{camera2D(10), camera3D(id, 10)}; TimelineKeyClipboard clipboard;
        QVERIFY(document.copyTimelineKeys(refs, &clipboard)); const auto original = document.project().toJson();
        QVERIFY(!document.moveTimelineKeys(refs, 1)); QVERIFY(!document.removeTimelineKeys(refs)); QVERIFY(!document.pasteTimelineKeys(clipboard, 20));
        QCOMPARE(document.project().toJson(), original);
        auto camera = evaluateSceneCamera(document.project().shots[0], id, 0); camera.locked = false; QVERIFY(document.setSceneCamera(id, camera));
        document.deleteLayers({id}); QVERIFY(document.project().shots[0].sceneCameraAnimations.isEmpty());
        document.history()->undo(); QVERIFY(sceneCameraAnimation(document.project().shots[0], id));
    }
    void invalidPreviewAndDurationChangeLeaveValidProject() {
        Document document; const auto project = fixture(); document.load(project); QVERIFY(document.enableShotCameraAnimation());
        QVERIFY(document.setCurrentFrame(30)); QVERIFY(document.setShotCamera({{10, 4}, 2, 8}));
        const auto original = document.project().toJson(); QVERIFY(document.beginCameraEdit());
        QVERIFY(!document.previewShotCamera({{}, 0, 0})); QCOMPARE(document.project().toJson(), original); document.cancelCameraEdit();
        auto shorter = document.project(); shorter.shots[0].durationFrames = 20; document.replaceProject(shorter, "Shorten shot");
        QCOMPARE(document.project().toJson(), original); QVERIFY(document.project().validate().isEmpty());
    }
};
QTEST_MAIN(CameraDocumentTests)
#include "camera_document_tests.moc"
