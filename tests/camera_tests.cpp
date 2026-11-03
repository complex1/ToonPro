#include "camera_animation.h"
#include "project.h"
#include <QJsonArray>
#include <QLineF>
#include <QtTest>
#include <limits>
using namespace toon;
namespace {
Project fixture() {
    auto project = Project::create(); project.width = 200; project.height = 100; project.shots[0].durationFrames = 30;
    auto scene = RasterLayer::create("City"); scene.kind = LayerKind::Scene3D; scene.scene3d = Scene3D();
    project.shots[0].layers.append(scene); return project;
}
}
class CameraTests final : public QObject {
    Q_OBJECT
private slots:
    void identityAndInverseFraming() {
        auto project = fixture(); auto &shot = project.shots[0];
        QVERIFY(shotCameraTransform(shot, 0, QSizeF(200, 100)).isIdentity());
        shot.camera2D = {{20, -10}, 2, 90};
        const auto transform = shotCameraTransform(shot, 0, QSizeF(200, 100));
        QVERIFY(QLineF(transform.map(QPointF(120, 40)), QPointF(100, 50)).length() < .00001);
        QVERIFY(QLineF(transform.map(QPointF(130, 40)), QPointF(100, 30)).length() < .00001);
        const QPointF point(-40, 120);
        QVERIFY(QLineF(transform.inverted().map(transform.map(point)), point).length() < .00001);
    }
    void posesUseSameClockAndExistingEasing() {
        auto project = fixture(); auto &shot = project.shots[0];
        shot.camera2D = {{1000, 1000}, 1, 0};
        shot.camera2DKeys = {{2, {{0, 0}, 1, 0}}, {12, {{100, -50}, 3, 720}}};
        QCOMPARE(evaluateShotCamera(shot, 0), shot.camera2DKeys.first().pose);
        QCOMPARE(evaluateShotCamera(shot, 29), shot.camera2DKeys.last().pose);
        QCOMPARE(evaluateShotCamera(shot, 7), (Camera2D{{50, -25}, 2, 360}));
        for (const auto easing : {KeyframeInterpolation::Hold, KeyframeInterpolation::Linear,
            KeyframeInterpolation::EaseIn, KeyframeInterpolation::EaseOut, KeyframeInterpolation::EaseInOut}) {
            shot.camera2DKeys[0].interpolation = easing;
            QCOMPARE(evaluateShotCamera(shot, 7).position.x(), 100 * keyframeProgress(.5, easing));
            QCOMPARE(evaluateShotCamera(shot, 12), shot.camera2DKeys.last().pose);
        }
    }
    void sceneCameraEvaluationPreservesProjectionAndLock() {
        auto project = fixture(); auto &shot = project.shots[0]; auto &layer = shot.layers.last();
        layer.scene3d->camera.orthographic = true; layer.scene3d->camera.locked = true;
        auto first = sceneCameraPose(layer.scene3d->camera), last = first;
        last.target += QVector3D(10, 4, -2); last.yaw += 720; last.distance = 15; last.orthoScale = 9;
        shot.sceneCameraAnimations = {{layer.id, {{0, first}, {20, last}}}};
        const auto before = project.toJson(); const auto evaluated = evaluateSceneCamera(shot, layer.id, 10);
        QVERIFY(evaluated.orthographic); QVERIFY(evaluated.locked);
        QCOMPARE(evaluated.target, first.target + QVector3D(5, 2, -1));
        QCOMPARE(evaluated.yaw, 390.f); QCOMPARE(evaluated.distance, 10.f); QCOMPARE(evaluated.orthoScale, 6.f);
        QCOMPARE(project.toJson(), before); QVERIFY(validateCameraAnimation(shot).isEmpty());
    }
    void boundsAndKeyValidationFailClosed() {
        auto project = fixture(); auto &shot = project.shots[0];
        QVERIFY(validateCameraAnimation(shot).isEmpty());
        for (const qreal zoom : {0., -.1, 100.01, std::numeric_limits<qreal>::quiet_NaN()}) {
            shot.camera2D.zoom = zoom; QVERIFY(!validateCameraAnimation(shot).isEmpty());
        }
        shot.camera2D = {}; shot.camera2DKeys = {{0, {}}, {29, {{3, 4}, .01, 36000}}};
        QVERIFY(validateCameraAnimation(shot).isEmpty());
        shot.camera2DKeys[1].frame = 0; QVERIFY(!validateCameraAnimation(shot).isEmpty());
        shot.camera2DKeys[1].frame = 30; QVERIFY(!validateCameraAnimation(shot).isEmpty());
        shot.camera2DKeys[1].frame = 29; shot.camera2DKeys[0].interpolation = KeyframeInterpolation(99);
        QVERIFY(!validateCameraAnimation(shot).isEmpty()); shot.camera2DKeys.clear();
        const auto id = shot.layers.last().id; auto pose = sceneCameraPose(shot.layers.last().scene3d->camera);
        shot.sceneCameraAnimations = {{id, {{0, pose}}}, {id, {{1, pose}}}};
        QVERIFY(!validateCameraAnimation(shot).isEmpty()); shot.sceneCameraAnimations.removeLast();
        shot.sceneCameraAnimations[0].layerId = shot.layers.first().id; QVERIFY(!validateCameraAnimation(shot).isEmpty());
        Camera3D camera; camera.pitch = 89.9f; QVERIFY(validSceneCameraPose(sceneCameraPose(camera)));
        pose.pitch = 90; QVERIFY(!validSceneCameraPose(pose));
        pose = {}; pose.distance = 0; QVERIFY(!validSceneCameraPose(pose));
        pose = {}; pose.fieldOfView = 151; QVERIFY(!validSceneCameraPose(pose));
    }
    void combinedCameraKeyBudgetIsEnforced() {
        auto project = fixture(); auto &shot = project.shots[0]; shot.durationFrames = maxCameraKeyframes + 1;
        for (int i = 0; i < maxCameraKeyframes; ++i) shot.camera2DKeys.append(Camera2DKeyframe{i, {}});
        QVERIFY(validateCameraAnimation(shot).isEmpty());
        shot.sceneCameraAnimations = {{shot.layers.last().id, {{0, {}}}}};
        QVERIFY(!validateCameraAnimation(shot).isEmpty());
    }
    void metadataRoundTripAndParserAtomicity() {
        auto project = fixture(); auto &shot = project.shots[0]; shot.camera2D = {{30, -10}, 2, 12};
        shot.camera2DKeys = {{0, {}}, {12, {{20, 4}, 1.25, 360}, KeyframeInterpolation::EaseOut}};
        shot.sceneCameraAnimations = {{shot.layers.last().id, {{0, {}}, {17, {{1, 2, 3}, 70, 20, 11, 60, 5}, KeyframeInterpolation::Hold}}}};
        Shot copy; QString error;
        QVERIFY(shotCameraFromJson(shotCameraToJson(shot), &copy, &error));
        QVERIFY(sceneCameraAnimationsFromJson(sceneCameraAnimationsToJson(shot.sceneCameraAnimations), &copy.sceneCameraAnimations, &error));
        QCOMPARE(copy.camera2D, shot.camera2D); QCOMPARE(copy.camera2DKeys, shot.camera2DKeys); QCOMPARE(copy.sceneCameraAnimations, shot.sceneCameraAnimations);
        const auto before = shotCameraToJson(copy); auto malformed = before; malformed["unknown"] = true;
        QVERIFY(!shotCameraFromJson(malformed, &copy, &error)); QCOMPARE(shotCameraToJson(copy), before);
        malformed = before; auto keys = malformed["keys"].toArray(); auto key = keys[0].toObject(); key["frame"] = .5; keys[0] = key; malformed["keys"] = keys;
        QVERIFY(!shotCameraFromJson(malformed, &copy, &error)); QCOMPARE(shotCameraToJson(copy), before);
        const auto sceneBefore = copy.sceneCameraAnimations;
        auto scenes = sceneCameraAnimationsToJson(sceneBefore); auto entry = scenes[0].toObject(); entry["keys"] = QJsonArray(); scenes[0] = entry;
        QVERIFY(!sceneCameraAnimationsFromJson(scenes, &copy.sceneCameraAnimations, &error)); QCOMPARE(copy.sceneCameraAnimations, sceneBefore);
    }
};
QTEST_MAIN(CameraTests)
#include "camera_tests.moc"
