#include "canvas.h"
#include "scene3d_controller.h"
#include "scene3d_cache.h"
#include "scene_model_io.h"
#include "sheet.h"
#include "ui_theme.h"
#include <QDir>
#include <QMouseEvent>
#include <QSignalSpy>
#include <QTabletEvent>
#include <QTemporaryDir>
#include <QtTest>
using namespace toon;
namespace {
Project fixture(){
    auto project=Project::create();project.width=480;project.height=320;
    Model3D source;source.name="Pose fixture";ModelMesh3D mesh;
    mesh.positions={{-.5f,0,0},{.5f,0,0},{-.5f,1,0},{.5f,1,0},{-.5f,2,0},{.5f,2,0}};
    mesh.normals=QVector<QVector3D>(6,{0,0,1});mesh.indices={0,1,2,1,3,2,2,3,4,3,5,4};mesh.doubleSided=true;source.meshes.append(mesh);
    const auto model=finalizeModel3D(std::move(source));Scene3D scene;scene.models.insert(model->id,model);
    ModelInstance3D object;object.id="posed";object.name="Character";object.modelId=model->id;
    object.rig.joints={{"root","Root",{},{},{0,0,0}},{"middle","Middle","root",{},{0,1,0}},{"end","End","middle",{},{0,2,0}}};
    object.skin=autoBindSkin3D(*model,object.rig);scene.objects.append(object);scene.camera.yaw=0;scene.camera.pitch=0;
    scene.camera=fitCamera3D(scene.camera,sceneBounds(scene),1.5f);
    auto layer=RasterLayer::create("3D reference");layer.kind=LayerKind::Scene3D;layer.scene3d=scene;project.shots[0].layers.prepend(layer);return project;
}
Project branchedFixture(){
    auto project=fixture();auto &scene=*project.shots[0].layers[0].scene3d;auto &object=scene.objects[0];
    object.rig.joints={{"root","Root",{},{},{0,0,0}},
        {"start","Start","root",{},{0,1,0}},{"middle","Middle","start",{},{1,1,0}},{"end","End","middle",{},{2,1,0}},
        {"other-start","Other start","root",{},{0,-1,0}},{"other-middle","Other middle","other-start",{},{-1,-1,0}},
        {"other-end","Other end","other-middle",{},{-2,-1,0}}};
    object.skin=autoBindSkin3D(*scene.models.value(object.modelId),object.rig);return project;
}
void choose(Canvas &canvas,const QString &joint="middle"){
    auto *c=canvas.scene3D();c->selectObject("posed");c->selectRigTool();c->setRigEditMode(Scene3DController::RigEditMode::Pose);c->selectJoint(joint);
}
QPointF screen(const Canvas &canvas,QVector3D local){
    const auto *c=canvas.scene3D();const auto &scene=*c->scene();
    const auto matrix=cameraProjectionMatrix(scene.camera,1.5f)*cameraViewMatrix(scene.camera)*instanceMatrix(*c->selectedObject());
    auto clip=matrix*QVector4D(local,1);clip/=clip.w();
    auto view=canvas.viewTransform();const auto origin=sheetGeometry(canvas.project(),canvas.shot()).output.topLeft();view.translate(origin.x(),origin.y());
    return view.map(QPointF((clip.x()+1)*240.,(1-clip.y())*160.));
}
QVector3D posedPoint(const Scene3DController *c,const QString &joint){const auto p=evaluatePose3D(*c->selectedRig(),*c->selectedPose());return p->positions[p->jointIds.indexOf(joint)];}
void mouse(Canvas &canvas,QEvent::Type type,QPointF point,Qt::MouseButton button,Qt::MouseButtons buttons){
    QMouseEvent event(type,point,canvas.mapToGlobal(point),button,buttons,{});QCoreApplication::sendEvent(&canvas,&event);
}
}
class PoseWorkspaceTests:public QObject {
    Q_OBJECT
private slots:
    void initTestCase(){applyTheme();}
    void poseIsSavedIndependentAndUndoable(){
        Canvas canvas;canvas.setProject(fixture());choose(canvas);auto *c=canvas.scene3D();const auto rig=*c->selectedRig();const auto skin=*c->selectedSkin();
        c->updatePoseRotation({0,0,40});QCOMPARE(c->poseRotation(),QVector3D(0,0,40));QCOMPARE(canvas.document()->history()->count(),1);
        const auto pose=*c->selectedPose();canvas.document()->history()->undo();QVERIFY(pose3DIsIdentity(*c->selectedPose()));
        canvas.document()->history()->redo();QCOMPARE(*c->selectedPose(),pose);QCOMPARE(*c->selectedRig(),rig);QCOMPARE(*c->selectedSkin(),skin);
        c->duplicateSelected();QCOMPARE(*c->selectedPose(),pose);c->resetAllPose();QVERIFY(pose3DIsIdentity(*c->selectedPose()));
        c->selectObject("posed");QCOMPARE(*c->selectedPose(),pose);
        QTemporaryDir dir;QString error;const auto path=dir.filePath("pose.toonproj");QVERIFY2(saveProject(canvas.project(),path,&error),qPrintable(error));
        const auto loaded=loadProject(path,&error);QVERIFY2(loaded.has_value(),qPrintable(error));QCOMPARE(loaded->shots[0].layers[0].scene3d->objects[0].pose,pose);
        c->unbindSkin();QVERIFY(!c->selectedSkin());QCOMPARE(*c->selectedPose(),Pose3D{});canvas.document()->history()->undo();QCOMPARE(*c->selectedPose(),pose);
    }
    void rotationRingOneUndoAndEscape_data(){QTest::addColumn<bool>("orthographic");QTest::newRow("perspective")<<false;QTest::newRow("orthographic")<<true;}
    void rotationRingOneUndoAndEscape(){
        QFETCH(bool,orthographic);auto project=fixture();auto &scene=*project.shots[0].layers[0].scene3d;scene.camera.orthographic=orthographic;
        scene.objects[0].rotation={5,20,15};scene.objects[0].scale={1.1f,.9f,1};
        Canvas canvas;canvas.resize(1100,820);canvas.setProject(project);canvas.show();canvas.fitSheet();canvas.setRotation(20);choose(canvas);
        auto *c=canvas.scene3D();c->setPoseRotationAxis(2);const auto pivot=screen(canvas,posedPoint(c,"middle"));
        const auto start=pivot+QPointF(42,0),finish=pivot+QPointF(0,42);
        mouse(canvas,QEvent::MouseButtonPress,start,Qt::LeftButton,Qt::LeftButton);QVERIFY(c->editing());
        mouse(canvas,QEvent::MouseMove,finish,Qt::NoButton,Qt::LeftButton);QVERIFY(std::abs(c->poseRotation().z()-90)<.01f);
        mouse(canvas,QEvent::MouseButtonRelease,finish,Qt::LeftButton,{});QCOMPARE(canvas.document()->history()->count(),1);QVERIFY(!c->editing());
        canvas.document()->history()->undo();QVERIFY(c->poseRotation().isNull());canvas.document()->history()->redo();
        const auto before=*c->selectedPose();mouse(canvas,QEvent::MouseButtonPress,start,Qt::LeftButton,Qt::LeftButton);
        mouse(canvas,QEvent::MouseMove,pivot+QPointF(30,30),Qt::NoButton,Qt::LeftButton);QTest::keyClick(&canvas,Qt::Key_Escape);
        QCOMPARE(*c->selectedPose(),before);QVERIFY(!c->editing());
    }
    void tabletPoseRingLostContactCommitsOnceAndEscapeCancelsNextGesture(){
        Canvas canvas;canvas.resize(1100,820);canvas.setProject(fixture());canvas.show();canvas.fitSheet();choose(canvas);
        auto *c=canvas.scene3D();c->setPoseRotationAxis(2);
        const auto original=*c->selectedPose();const auto rig=*c->selectedRig();const auto skin=*c->selectedSkin();
        const int history=canvas.document()->history()->count();const auto pivot=screen(canvas,posedPoint(c,"middle"));
        const auto start=pivot+QPointF(42,0),finish=pivot+QPointF(0,42);
        QPointingDevice device("Pose test pen",64,QInputDevice::DeviceType::Stylus,QPointingDevice::PointerType::Pen,
            QInputDevice::Capability::Position|QInputDevice::Capability::Pressure,1,1);
        const auto pen=[&](QEvent::Type type,QPointF position,Qt::MouseButtons buttons){
            QTabletEvent event(type,&device,position,canvas.mapToGlobal(position),buttons?.7:0,
                0,0,0,0,0,Qt::NoModifier,type==QEvent::TabletMove?Qt::NoButton:Qt::LeftButton,buttons);
            QCoreApplication::sendEvent(&canvas,&event);
        };
        pen(QEvent::TabletPress,start,Qt::LeftButton);QVERIFY(c->editing());
        pen(QEvent::TabletMove,finish,Qt::LeftButton);QVERIFY(std::abs(c->poseRotation().z()-90)<.01f);
        const auto moved=*c->selectedPose();QCOMPARE(canvas.document()->history()->count(),history);
        // A driver can deliver hovering without a release. End at the last
        // contact sample; the distant hover must not continue rotating.
        pen(QEvent::TabletMove,pivot+QPointF(-80,-70),{});
        QVERIFY(!c->editing());QVERIFY(!canvas.document()->externalEditActive());QCOMPARE(*c->selectedPose(),moved);
        QCOMPARE(canvas.document()->history()->count(),history+1);
        pen(QEvent::TabletRelease,pivot+QPointF(-80,-70),{});QCOMPARE(canvas.document()->history()->count(),history+1);
        canvas.document()->history()->undo();QCOMPARE(*c->selectedPose(),original);
        canvas.document()->history()->redo();QCOMPARE(*c->selectedPose(),moved);
        pen(QEvent::TabletPress,start,Qt::LeftButton);pen(QEvent::TabletMove,pivot+QPointF(30,30),Qt::LeftButton);
        QVERIFY(c->editing());QVERIFY(*c->selectedPose()!=moved);QTest::keyClick(&canvas,Qt::Key_Escape);
        QVERIFY(!c->editing());QVERIFY(!canvas.document()->externalEditActive());QCOMPARE(*c->selectedPose(),moved);
        pen(QEvent::TabletMove,pivot+QPointF(-60,10),{});pen(QEvent::TabletRelease,pivot+QPointF(-60,10),{});
        QCOMPARE(*c->selectedPose(),moved);QCOMPARE(canvas.document()->history()->count(),history+1);
        // Lost contact/cancellation must release tablet ownership for mouse input.
        mouse(canvas,QEvent::MouseButtonPress,start,Qt::LeftButton,Qt::LeftButton);QVERIFY(c->editing());
        mouse(canvas,QEvent::MouseMove,pivot+QPointF(0,-42),Qt::NoButton,Qt::LeftButton);
        mouse(canvas,QEvent::MouseButtonRelease,pivot+QPointF(0,-42),Qt::LeftButton,{});
        QVERIFY(!c->editing());QCOMPARE(canvas.document()->history()->count(),history+2);
        QCOMPARE(*c->selectedRig(),rig);QCOMPARE(*c->selectedSkin(),skin);
    }
    void ikCanvasDragPinsAndBodyTranslation(){
        Canvas canvas;canvas.resize(1100,820);canvas.setProject(fixture());canvas.show();canvas.fitSheet();choose(canvas,"end");auto *c=canvas.scene3D();
        c->setPoseMode(Scene3DController::PoseMode::IK);const auto start=screen(canvas,posedPoint(c,"end"));const auto end=start+QPointF(35,30);
        mouse(canvas,QEvent::MouseButtonPress,start,Qt::LeftButton,Qt::LeftButton);QVERIFY(c->editing());
        mouse(canvas,QEvent::MouseMove,end,Qt::NoButton,Qt::LeftButton);mouse(canvas,QEvent::MouseButtonRelease,end,Qt::LeftButton,{});
        QVERIFY(c->selectedIKTarget());QCOMPARE(canvas.document()->history()->count(),1);QVERIFY((screen(canvas,posedPoint(c,"end"))-end).manhattanLength()<1);
        c->setIKPinned(true);const auto fixed=posedPoint(c,"end");c->selectJoint("root");c->updatePoseRootTranslation({.1f,.1f,0});
        QVERIFY((posedPoint(c,"end")-fixed).length()<.001f);QVERIFY(!c->canRotateSelectedPoseJoint());
        c->selectJoint("end");c->setIKPinned(false);const auto released=posedPoint(c,"end");c->updatePoseRootTranslation({.2f,.1f,0});
        QVERIFY((posedPoint(c,"end")-released-QVector3D(.1f,0,0)).length()<.001f);
        const auto lengths=evaluatePose3D(*c->selectedRig(),*c->selectedPose());QVERIFY(std::abs((lengths->positions[1]-lengths->positions[0]).length()-1)<.001f);
        c->updateIKTarget({50,0,0});QVERIFY(c->poseStatus().contains("reach"));QVERIFY(validatePose3D(*c->selectedPose(),*c->selectedRig()).isEmpty());
    }
    void limitsLocksAndToolSwitch(){
        Canvas canvas;canvas.resize(1000,800);canvas.setProject(fixture());canvas.show();canvas.fitSheet();choose(canvas);auto *c=canvas.scene3D();
        auto limit=c->selectedPoseLimit();limit.enabled=true;limit.minimum={-20,-20,-20};limit.maximum={20,20,20};c->updatePoseLimit(limit);
        c->updatePoseRotation({0,0,90});QCOMPARE(c->poseRotation().z(),20.f);
        auto object=*c->selectedObject();object.locked=true;c->updateObject(object);const auto before=*c->selectedPose();
        c->updatePoseRotation({0,0,0});c->resetAllPose();QCOMPARE(*c->selectedPose(),before);QVERIFY(canvas.toolBlockReason().contains("unlock"));
        object.locked=false;c->updateObject(object);const auto point=screen(canvas,posedPoint(c,"middle"));
        mouse(canvas,QEvent::MouseButtonPress,point+QPointF(42,0),Qt::LeftButton,Qt::LeftButton);
        mouse(canvas,QEvent::MouseMove,point+QPointF(36,-21),Qt::NoButton,Qt::LeftButton);QVERIFY(c->editing());
        canvas.setTool(Canvas::Tool::Hand);QVERIFY(!c->editing());QVERIFY(!canvas.document()->externalEditActive());QVERIFY(c->poseRotation().z()<20);
        choose(canvas);c->setRigVisible(false);QVERIFY(canvas.toolBlockReason().contains("Show the skeleton"));
    }
    void rootResetCarriesUnpinnedControlsAndRetainsOtherPins(){
        Canvas canvas;canvas.setProject(branchedFixture());choose(canvas,"end");auto *c=canvas.scene3D();
        c->setPoseMode(Scene3DController::PoseMode::IK);c->updateIKTarget({1.3f,1.6f,0});c->updateIKPole({0,3,0});
        const auto original=*c->selectedIKTarget();
        c->selectJoint("other-end");c->updateIKTarget({-1.4f,-.4f,0});c->setIKPinned(true);
        const auto fixed=*c->selectedIKTarget();const auto fixedEnd=posedPoint(c,"other-end");
        c->selectJoint("root");c->updatePoseRootTranslation({.2f,.1f,0});
        c->selectJoint("end");QVERIFY((c->selectedIKTarget()->target-original.target-QVector3D(.2f,.1f,0)).length()<.001f);
        c->selectJoint("root");const auto before=*c->selectedPose();const int history=canvas.document()->history()->count();
        c->resetSelectedPose();QVERIFY(c->poseRootTranslation().isNull());QCOMPARE(canvas.document()->history()->count(),history+1);
        c->selectJoint("end");QVERIFY(c->selectedIKTarget());QVERIFY((c->selectedIKTarget()->target-original.target).length()<.001f);
        QVERIFY((c->selectedIKTarget()->pole-original.pole).length()<.001f);QVERIFY((posedPoint(c,"end")-original.target).length()<.001f);
        c->selectJoint("other-end");QCOMPARE(*c->selectedIKTarget(),fixed);QVERIFY((posedPoint(c,"other-end")-fixedEnd).length()<.001f);
        canvas.document()->history()->undo();QCOMPARE(*c->selectedPose(),before);
        canvas.document()->history()->redo();QVERIFY(c->poseRootTranslation().isNull());
    }
    void ancestorFkLimitsAndResetCarryUnpinnedTargetAndPole(){
        Canvas canvas;canvas.setProject(branchedFixture());choose(canvas,"end");auto *c=canvas.scene3D();
        c->updateIKTarget({1.3f,1.6f,0});c->updateIKPole({0,3,0});const auto original=*c->selectedIKTarget();
        c->selectJoint("root");c->updatePoseRotation({0,0,40});
        QMatrix4x4 rotation;rotation.rotate(40,0,0,1);
        c->selectJoint("end");QVERIFY(c->selectedIKTarget());
        QVERIFY((c->selectedIKTarget()->target-rotation.map(original.target)).length()<.001f);
        QVERIFY((c->selectedIKTarget()->pole-rotation.map(original.pole)).length()<.001f);
        QVERIFY((posedPoint(c,"end")-c->selectedIKTarget()->target).length()<.001f);
        c->selectJoint("root");auto limit=c->selectedPoseLimit();limit.enabled=true;limit.minimum={-10,-10,-10};limit.maximum={10,10,10};c->updatePoseLimit(limit);
        QCOMPARE(c->poseRotation().z(),10.f);rotation.setToIdentity();rotation.rotate(10,0,0,1);
        c->selectJoint("end");QVERIFY((c->selectedIKTarget()->target-rotation.map(original.target)).length()<.001f);
        QVERIFY((c->selectedIKTarget()->pole-rotation.map(original.pole)).length()<.001f);
        c->selectJoint("root");c->resetSelectedPose();c->selectJoint("end");
        QVERIFY(c->selectedIKTarget());QVERIFY((c->selectedIKTarget()->target-original.target).length()<.001f);
        QVERIFY((c->selectedIKTarget()->pole-original.pole).length()<.001f);
        // Direct FK of a controlled joint still hands that chain over to FK.
        c->selectJoint("start");c->updatePoseRotation({0,0,20});c->selectJoint("end");QVERIFY(!c->selectedIKTarget());
    }
    void resettingPinnedEndpointBakesTheDisplayedChainBeforeRelease(){
        Canvas canvas;canvas.setProject(fixture());choose(canvas,"end");auto *c=canvas.scene3D();
        c->updateIKTarget({.7f,1.4f,0});c->setIKPinned(true);c->updatePoseRootTranslation({.2f,.1f,0});
        const auto before=*c->selectedPose();const auto displayed=evaluatePose3D(*c->selectedRig(),before);QVERIFY(displayed);
        const int history=canvas.document()->history()->count();c->resetSelectedPose();
        QVERIFY(!c->selectedIKTarget());QVERIFY(c->poseRotation().isNull());QCOMPARE(canvas.document()->history()->count(),history+1);
        const auto after=evaluatePose3D(*c->selectedRig(),*c->selectedPose());QVERIFY(after);
        for(int i=0;i<displayed->positions.size();++i)QVERIFY((after->positions[i]-displayed->positions[i]).length()<.001f);
        canvas.document()->history()->undo();QCOMPARE(*c->selectedPose(),before);
        canvas.document()->history()->redo();QVERIFY(!c->selectedIKTarget());
        QVERIFY((posedPoint(c,"end")-displayed->positions.last()).length()<.001f);
    }
    void ancestorIkCarriesIndependentControlsAndRetiresOverlappingOnes(){
        auto project=fixture();auto &scene=*project.shots[0].layers[0].scene3d;auto &object=scene.objects[0];
        object.rig.joints.append({"d","D","end",{},{0,3,0}});object.rig.joints.append({"e","E","d",{},{0,4,0}});
        object.rig.joints.append({"f","F","e",{},{0,5,0}});object.skin=autoBindSkin3D(*scene.models.value(object.modelId),object.rig);
        Canvas canvas;canvas.setProject(project);choose(canvas,"f");auto *c=canvas.scene3D();
        c->updateIKTarget({.7f,4.5f,0});c->updateIKPole({2,3,0});const auto original=*c->selectedIKTarget();
        const auto before=*c->selectedPose();const auto oldFrame=evaluatePose3D(*c->selectedRig(),before);QVERIFY(oldFrame);
        c->selectJoint("end");const int history=canvas.document()->history()->count();c->updateIKTarget({.5f,1.5f,0});
        QCOMPARE(canvas.document()->history()->count(),history+1);QCOMPARE(c->selectedIKTarget()->target,QVector3D(.5f,1.5f,0));
        const auto changed=evaluatePose3D(*c->selectedRig(),*c->selectedPose());QVERIFY(changed);
        const int root=oldFrame->jointIds.indexOf("d");const auto delta=(changed->rotations[root]*oldFrame->rotations[root].conjugated()).normalized();
        const auto carry=[&](QVector3D point){return changed->positions[root]+delta.rotatedVector(point-oldFrame->positions[root]);};
        c->selectJoint("f");QVERIFY(c->selectedIKTarget());
        QVERIFY((c->selectedIKTarget()->target-carry(original.target)).length()<.001f);
        QVERIFY((c->selectedIKTarget()->pole-carry(original.pole)).length()<.001f);
        QVERIFY((posedPoint(c,"f")-c->selectedIKTarget()->target).length()<.001f);
        canvas.document()->history()->undo();QCOMPARE(*c->selectedPose(),before);canvas.document()->history()->redo();
        // Changing the ancestor's bend plane also carries the independent child.
        c->selectJoint("end");c->setIKPinned(true);c->updateIKPole({-2,1,0});c->setIKPinned(false);
        c->selectJoint("f");QVERIFY(c->selectedIKTarget());QVERIFY((posedPoint(c,"f")-c->selectedIKTarget()->target).length()<.001f);
        // D's two-bone control overlaps End's. The newly edited chain owns
        // those joints, while the independent F control stays enabled.
        c->selectJoint("d");c->enableSelectedIK(true);QVERIFY(c->selectedIKTarget());
        c->selectJoint("end");QVERIFY(!c->selectedIKTarget());c->updateIKTarget({.2f,1.4f,0});QVERIFY(c->selectedIKTarget());
        c->selectJoint("d");QVERIFY(!c->selectedIKTarget());c->selectJoint("f");QVERIFY(c->selectedIKTarget());
        QVERIFY((posedPoint(c,"f")-c->selectedIKTarget()->target).length()<.001f);
    }
    void objectPickingUsesPosedDepthAndBounds(){
        auto project=fixture();auto &scene=*project.shots[0].layers[0].scene3d;
        scene.camera.orthographic=true;scene.camera.orthoScale=8;
        auto behind=scene.objects[0];behind.id="behind";behind.name="Behind";scene.objects.append(behind);
        scene.objects[0].position={0,0,-2};scene.objects[0].pose.rootTranslation={0,0,3};
        Canvas canvas;canvas.resize(1000,800);canvas.setProject(project);canvas.show();canvas.fitSheet();choose(canvas);
        auto *c=canvas.scene3D();c->selectTool(false);c->selectObject("behind");
        const auto center=screen(canvas,{0,1,0});
        mouse(canvas,QEvent::MouseButtonPress,center,Qt::LeftButton,Qt::LeftButton);
        QCOMPARE(c->selectedObjectId(),QString("posed"));mouse(canvas,QEvent::MouseButtonRelease,center,Qt::LeftButton,{});
        QCOMPARE(canvas.document()->history()->count(),0);
        c->updatePoseRootTranslation({3,0,3});c->selectObject("behind");
        const auto translated=screen(canvas,{3,1,0});const int history=canvas.document()->history()->count();
        mouse(canvas,QEvent::MouseButtonPress,translated,Qt::LeftButton,Qt::LeftButton);
        QCOMPARE(c->selectedObjectId(),QString("posed"));mouse(canvas,QEvent::MouseButtonRelease,translated,Qt::LeftButton,{});
        QCOMPARE(canvas.document()->history()->count(),history);
    }
    void poseRestWeightsDisplayAndExportIsolation(){
#ifndef TOON_HAS_FILAMENT
        QSKIP("Requires GPU renderer");
#else
        Canvas canvas;canvas.resize(1000,800);canvas.setProject(fixture());canvas.show();canvas.fitSheet();choose(canvas);auto *c=canvas.scene3D();
        QTRY_VERIFY_WITH_TIMEOUT(!c->rendering(),15000);const auto rest=scene3DPreview(*c->scene(),{480,320},true);QVERIFY(!rest.isNull());
        const auto movie=renderMovieFrame(canvas.project());c->updatePoseRotation({0,0,70});QTRY_VERIFY_WITH_TIMEOUT(!c->rendering(),15000);
        const auto posed=scene3DPreview(*c->scene(),{480,320},true);QVERIFY(posed!=rest);const auto saved=*c->selectedPose();const int history=canvas.document()->history()->count();
        c->setRigEditMode(Scene3DController::RigEditMode::Rest);QTRY_VERIFY_WITH_TIMEOUT(!c->rendering(),15000);
        {Scene3DPreviewScope scoped(c->previewBaseKey(),c->previewDisplayKey());QCOMPARE(scene3DPreview(*c->scene(),{480,320}),rest);}
        QCOMPARE(scene3DPreview(*c->scene(),{480,320},true),posed);c->setRigEditMode(Scene3DController::RigEditMode::Weights);
        QTRY_VERIFY_WITH_TIMEOUT(!c->rendering(),15000);c->setRigEditMode(Scene3DController::RigEditMode::Pose);QTRY_VERIFY_WITH_TIMEOUT(!c->rendering(),15000);
        QCOMPARE(*c->selectedPose(),saved);QCOMPARE(canvas.document()->history()->count(),history);QCOMPARE(renderMovieFrame(canvas.project()),movie);
        QCOMPARE(scene3DPreview(*c->scene(),{480,320},true),posed);
#endif
    }
    void preview(){
        const auto out=qEnvironmentVariable("TOON_UI_PREVIEW");if(out.isEmpty())QSKIP("Opt-in preview");QDir().mkpath(out);
        Canvas canvas;canvas.resize(1100,820);canvas.setProject(fixture());canvas.show();canvas.fitSheet();choose(canvas);auto *c=canvas.scene3D();
        c->updatePoseRotation({0,0,50});QTRY_VERIFY_WITH_TIMEOUT(!c->rendering(),15000);QVERIFY(canvas.grab().save(out+"/pose-canvas-fk.png"));
        c->selectJoint("end");c->setPoseMode(Scene3DController::PoseMode::IK);c->enableSelectedIK(true);c->updateIKTarget({.6f,1.4f,0});c->setIKPinned(true);
        QTRY_VERIFY_WITH_TIMEOUT(!c->rendering(),15000);QVERIFY(canvas.grab().save(out+"/pose-canvas-ik.png"));
    }
};
QTEST_MAIN(PoseWorkspaceTests)
#include "test_pose_workspace.moc"
