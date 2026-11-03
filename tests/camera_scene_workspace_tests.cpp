#include "canvas.h"
#include "camera_animation.h"
#include "scene3d_controller.h"
#include "scene3d_panel.h"
#include "scene3d_cache.h"
#include "value_slider.h"
#include <QLineEdit>
#include <QtTest>
using namespace toon;
namespace {
Project fixture() {
    auto p=Project::create();p.width=128;p.height=128;p.shots[0].durationFrames=11;
    auto scene=RasterLayer::create("3D city");scene.kind=LayerKind::Scene3D;scene.scene3d=Scene3D{};
    auto a=sceneCameraPose(scene.scene3d->camera);auto b=a;b.yaw=130;b.target={2,.8f,0};
    p.shots[0].layers.prepend(scene);
    p.shots[0].sceneCameraAnimations={{scene.id,{{0,a,KeyframeInterpolation::Linear},{10,b,KeyframeInterpolation::Linear}}}};
    return p;
}
}
class CameraSceneWorkspaceTests final:public QObject {
    Q_OBJECT
private slots:
    void evaluatedSceneFollowsFrameWithoutChangingSavedCamera() {
        Canvas canvas;const auto p=fixture();canvas.setProject(p);auto *controller=canvas.scene3D();
        const auto original=p.toJson();QVERIFY(controller->scene());
        for(int frame:{0,5,10,0}) {
            canvas.document()->setCurrentFrame(frame);
            QCOMPARE(controller->scene()->camera,evaluateSceneCamera(canvas.shot(),controller->layerId(),frame));
            QCOMPARE(canvas.project().toJson(),original);
        }
        QCOMPARE(canvas.document()->history()->count(),0);
    }
    void cameraGestureWritesCurrentKeyAndUndoKeepsBasePose() {
        Canvas canvas;canvas.setProject(fixture());auto *controller=canvas.scene3D();auto *doc=canvas.document();
        doc->setCurrentFrame(5);const auto before=canvas.project().toJson();const auto camera=controller->scene()->camera;
        const auto base=canvas.shot().layers[0].scene3d->camera;const auto history=doc->history()->count();
        QVERIFY(controller->beginCameraEdit());auto changed=camera;changed.yaw+=20;controller->updateCamera(changed);
        changed.yaw+=10;controller->updateCamera(changed);QCOMPARE(doc->history()->count(),history);
        QCOMPARE(canvas.shot().layers[0].scene3d->camera,base);QCOMPARE(controller->scene()->camera,changed);
        controller->endEdit();QCOMPARE(doc->history()->count(),history+1);
        const auto *animation=sceneCameraAnimation(canvas.shot(),controller->layerId());QVERIFY(animation);QCOMPARE(animation->keys.size(),3);
        QCOMPARE(animation->keys[1].frame,5);QCOMPARE(animation->keys[1].pose.yaw,qreal(changed.yaw));
        doc->history()->undo();QCOMPARE(canvas.project().toJson(),before);QCOMPARE(controller->scene()->camera,camera);
        doc->history()->redo();QCOMPARE(controller->scene()->camera,changed);
        QVERIFY(controller->beginCameraEdit());changed.distance*=2;controller->updateCamera(changed);controller->cancelEdit();
        QVERIFY(!doc->cameraEditActive());QCOMPARE(controller->scene()->camera.distance,camera.distance);
    }
    void frameChangeNeverPublishesOldCameraImageAtNewKey() {
        auto p=fixture();const auto &shot=p.shots[0];auto first=*shot.layers[0].scene3d;
        first.camera=evaluateSceneCamera(shot,shot.layers[0].id,0);
        QImage old(128,128,QImage::Format_ARGB32_Premultiplied);old.fill(Qt::red);
        cacheScene3DPreview(scene3DCacheKey(first,{128,128}),old);
        Canvas canvas;canvas.setProject(p);auto *controller=canvas.scene3D();
        QCOMPARE(scene3DPreview(*controller->scene(),{128,128}),old);
        canvas.document()->setCurrentFrame(10);const auto next=controller->previewDisplayKey();
        QVERIFY(next!=scene3DCacheKey(first,{128,128}));QVERIFY(cachedScene3DPreview(next).isNull());
        canvas.document()->setCurrentFrame(0);QCOMPARE(scene3DPreview(*controller->scene(),{128,128}),old);
    }
    void existingCameraPanelShowsEvaluatedPoseAndResolvesTypedDraft() {
        Canvas canvas;canvas.setProject(fixture());auto *controller=canvas.scene3D();Scene3DPanel panel(controller);
        canvas.document()->setCurrentFrame(5);auto *yaw=panel.findChild<ValueSlider*>("scene3DYaw");QVERIFY(yaw);
        QCOMPARE(yaw->value(),80.);const int count=canvas.document()->history()->count();
        auto *entry=yaw->findChild<QLineEdit*>();QVERIFY(entry);entry->setFocus();entry->selectAll();QTest::keyClicks(entry,"95");
        controller->endEdit();QCOMPARE(controller->scene()->camera.yaw,95.f);
        QCOMPARE(canvas.document()->history()->count(),count+1);QVERIFY(!controller->editing());
    }
};
QTEST_MAIN(CameraSceneWorkspaceTests)
#include "camera_scene_workspace_tests.moc"
