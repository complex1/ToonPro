#include "canvas.h"
#include "scene3d_controller.h"
#include "scene3d_cache.h"
#include "scene3d_renderer.h"
#include "scene_model_io.h"
#include "skin3d_view.h"
#include "sheet.h"
#include "ui_theme.h"
#include <QMouseEvent>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QDir>
#include <QUuid>
#include <QtTest>
using namespace toon;
namespace {
Project fixture(bool bound=true) {
    auto project=Project::create();project.width=480;project.height=320;
    auto model=builtInModels3D().first();Scene3D scene;scene.models.insert(model->id,model);
    ModelInstance3D object;object.id="skin-object";object.name="Cube";object.modelId=model->id;
    object.rig.joints={{"base","Base",{},{},{0,-.5f,0}},{"tip","Tip","base",{},{0,.5f,0}}};
    if(bound)object.skin=autoBindSkin3D(*model,object.rig);
    scene.objects.append(object);scene.camera.yaw=0;scene.camera.pitch=0;
    scene.camera=fitCamera3D(scene.camera,sceneBounds(scene),1.5f);
    auto layer=RasterLayer::create("3D reference");layer.kind=LayerKind::Scene3D;layer.scene3d=scene;
    project.shots[0].layers.prepend(layer);return project;
}
void choose(Canvas &canvas) {
    auto *c=canvas.scene3D();c->selectObject("skin-object");c->selectRigTool();c->selectJoint("tip");
    c->setRigEditMode(Scene3DController::RigEditMode::Weights);
}
QTransform artworkView(const Canvas &canvas) {
    auto transform=canvas.viewTransform();const auto origin=sheetGeometry(canvas.project(),canvas.shot()).output.topLeft();
    transform.translate(origin.x(),origin.y());return transform;
}
QPointF screenPoint(const Canvas &canvas,int mesh,int vertex) {
    const auto *c=canvas.scene3D();const auto &scene=*c->scene();const auto *object=c->selectedObject();
    const auto model=scene.models.value(object->modelId);const auto &m=model->meshes[mesh];
    const auto matrix=cameraProjectionMatrix(scene.camera,1.5f)*cameraViewMatrix(scene.camera)*instanceMatrix(*object)*m.transform;
    auto clip=matrix*QVector4D(m.positions[vertex],1);clip/=clip.w();
    return artworkView(canvas).map(QPointF((clip.x()+1)*240.,(1-clip.y())*160.));
}
void mouse(Canvas &canvas,QEvent::Type type,QPointF point,Qt::MouseButton button,Qt::MouseButtons buttons) {
    QMouseEvent event(type,point,canvas.mapToGlobal(point),button,buttons,{});QCoreApplication::sendEvent(&canvas,&event);
}
QPointF paintablePoint(Canvas &canvas) {
    SkinPaintProjection projection;QString error;
    if(!projection.build(*canvas.scene3D()->scene(),"skin-object",{480,320},artworkView(canvas),&error))return {};
    const auto hits=projection.verticesInBrush(QPointF(canvas.width()/2.,canvas.height()/2.),2000);
    for(const auto &hit:hits) {
        const auto weight=skinJointWeight(canvas.scene3D()->selectedSkin()->meshes[hit.mesh][hit.vertex],1);
        if(weight<.95f)return screenPoint(canvas,hit.mesh,hit.vertex);
    }
    return {};
}
}
class SkinWorkspaceTests:public QObject {
    Q_OBJECT
private slots:
    void initTestCase(){applyTheme();}
    void bindingUndoUnbindAndInstanceIsolation(){
        Canvas canvas;canvas.setProject(fixture(false));choose(canvas);auto *c=canvas.scene3D();
        c->bindSkin();QVERIFY(c->skinBindingBusy());QTRY_VERIFY_WITH_TIMEOUT(!c->skinBindingBusy(),15000);
        QVERIFY(c->selectedSkin());QVERIFY(!c->canEditRig());QCOMPARE(canvas.document()->history()->count(),1);
        const auto skin=*c->selectedSkin();canvas.document()->history()->undo();QVERIFY(!c->selectedSkin());
        canvas.document()->history()->redo();QCOMPARE(*c->selectedSkin(),skin);
        const auto originalPreview=c->previewDisplayKey();c->setSkinPreviewRotation({0,0,20});
        c->duplicateSelected();QVERIFY(c->selectedSkin());QVERIFY(c->skinPreviewRotation().isNull());
        QVERIFY(c->previewDisplayKey()!=originalPreview);c->unbindSkin();QVERIFY(!c->selectedSkin());
        c->selectObject("skin-object");QCOMPARE(*c->selectedSkin(),skin);
        c->unbindSkin();QVERIFY(c->canEditRig());canvas.document()->history()->undo();QCOMPARE(*c->selectedSkin(),skin);
        const auto rig=*c->selectedRig();auto joint=*rigJoint(rig,"tip");joint.position.setX(2);c->selectJoint("tip");c->updateJoint(joint);
        QCOMPARE(*c->selectedRig(),rig);
    }
    void bindingCannotApplyToAnotherProject(){
        Canvas canvas;canvas.setProject(fixture(false));choose(canvas);auto *c=canvas.scene3D();c->bindSkin();
        canvas.setProject(fixture(false));QCoreApplication::processEvents();QTest::qWait(50);
        c->selectObject("skin-object");QVERIFY(!c->selectedSkin());QCOMPARE(canvas.document()->history()->count(),0);
    }
    void duplicationRespectsSkinBudgetAcrossShots(){
        constexpr int count=750000;
        Model3D source;source.name="Binding budget fixture";
        ModelMesh3D mesh;mesh.positions=QVector<QVector3D>(count,{0,0,0});
        mesh.positions[1]={1,0,0};mesh.positions[2]={0,1,0};
        mesh.normals=QVector<QVector3D>(count,{0,0,1});mesh.indices={0,1,2};source.meshes.append(mesh);
        const auto model=finalizeModel3D(std::move(source));QVERIFY(model);
        auto project=fixture(false);auto &scene=*project.shots.first().layers.first().scene3d;
        scene.models.clear();scene.models.insert(model->id,model);
        auto &object=scene.objects.first();object.modelId=model->id;object.visible=false;
        Skin3D skin;skin.jointIds={"base","tip"};SkinVertex3D vertex;vertex.weights[0]=1;
        skin.meshes.append(QVector<SkinVertex3D>(count,vertex));object.skin=skin;
        auto second=project.shots.first();second.id=QUuid::createUuid().toString(QUuid::WithoutBraces);
        for(auto &layer:second.layers)layer.id=QUuid::createUuid().toString(QUuid::WithoutBraces);
        project.shots.append(second);QVERIFY2(project.validate().isEmpty(),qPrintable(project.validate()));
        Canvas canvas;canvas.setProject(project);auto *c=canvas.scene3D();c->selectObject("skin-object");
        QSignalSpy warnings(c,&Scene3DController::warning);c->duplicateSelected();
        QCOMPARE(c->scene()->objects.size(),1);QCOMPARE(canvas.document()->history()->count(),0);
        QVERIFY(!c->editing());QVERIFY(!canvas.document()->externalEditActive());
        QVERIFY(!warnings.isEmpty());QVERIFY(warnings.last().first().toString().contains("48 MiB"));
    }
    void paintingIsOneUndoAndEscapeLeavesNoChanges(){
        Canvas canvas;canvas.resize(1000,800);canvas.setProject(fixture());canvas.show();canvas.fitSheet();choose(canvas);
        auto *c=canvas.scene3D();QTRY_VERIFY_WITH_TIMEOUT(!c->weightProjectionBusy(),15000);
        const auto at=paintablePoint(canvas);QVERIFY(!at.isNull());c->setWeightBrushRadius(45);c->setWeightBrushStrength(.6);
        const auto original=*c->selectedSkin();const int history=canvas.document()->history()->count();const auto originalPreview=c->previewDisplayKey();
        mouse(canvas,QEvent::MouseMove,at,Qt::NoButton,{});QVERIFY(!canvas.brushCursorPath().isEmpty());QCOMPARE(canvas.cursor().shape(),Qt::BlankCursor);
        mouse(canvas,QEvent::MouseButtonPress,at,Qt::LeftButton,Qt::LeftButton);QVERIFY(c->editing());
        mouse(canvas,QEvent::MouseMove,at+QPointF(4,0),Qt::NoButton,Qt::LeftButton);
        QCOMPARE(*c->selectedSkin(),original);QCOMPARE(canvas.document()->history()->count(),history);
        mouse(canvas,QEvent::MouseButtonRelease,at+QPointF(4,0),Qt::LeftButton,{});
        QVERIFY(!c->editing());QVERIFY(*c->selectedSkin()!=original);QCOMPARE(canvas.document()->history()->count(),history+1);
        QVERIFY(c->previewDisplayKey()!=originalPreview);
        const auto changed=*c->selectedSkin();QVERIFY(validateSkin3D(changed,*c->selectedRig()).isEmpty());
        canvas.document()->history()->undo();QCOMPARE(*c->selectedSkin(),original);
        canvas.document()->history()->redo();QCOMPARE(*c->selectedSkin(),changed);
        mouse(canvas,QEvent::MouseButtonPress,at,Qt::LeftButton,Qt::LeftButton);QVERIFY(c->editing());
        QTest::keyClick(&canvas,Qt::Key_Escape);QVERIFY(!c->editing());QCOMPARE(*c->selectedSkin(),changed);
        mouse(canvas,QEvent::MouseButtonRelease,at,Qt::LeftButton,{});QCOMPARE(canvas.document()->history()->count(),history+1);
        QTemporaryDir directory;QString error;const auto path=directory.filePath("skin.toonproj");
        QVERIFY2(saveProject(canvas.project(),path,&error),qPrintable(error));const auto reopened=loadProject(path,&error);QVERIFY2(reopened,qPrintable(error));
        QCOMPARE(*reopened->shots.first().layers.first().scene3d->objects.first().skin,changed);
    }
    void soleInfluenceSubtractionExplainsNoChangeOnce(){
        auto project=fixture(false);auto &scene=*project.shots.first().layers.first().scene3d;
        auto &object=scene.objects.first();object.rig.joints.removeLast();
        object.skin=autoBindSkin3D(*scene.models.value(object.modelId),object.rig);
        Canvas canvas;canvas.resize(1000,800);canvas.setProject(project);canvas.show();canvas.fitSheet();
        auto *c=canvas.scene3D();c->selectObject("skin-object");c->selectRigTool();c->selectJoint("base");
        c->setRigEditMode(Scene3DController::RigEditMode::Weights);c->setWeightPaintMode(Scene3DController::WeightPaintMode::Subtract);
        QTRY_VERIFY_WITH_TIMEOUT(!c->weightProjectionBusy(),15000);
        SkinPaintProjection projection;QVERIFY(projection.build(*c->scene(),"skin-object",{480,320},artworkView(canvas)));
        const auto hits=projection.verticesInBrush(QPointF(canvas.width()/2.,canvas.height()/2.),2000);QVERIFY(!hits.isEmpty());
        const auto at=screenPoint(canvas,hits.first().mesh,hits.first().vertex);const auto original=*c->selectedSkin();
        QSignalSpy warnings(c,&Scene3DController::warning);
        mouse(canvas,QEvent::MouseButtonPress,at,Qt::LeftButton,Qt::LeftButton);
        mouse(canvas,QEvent::MouseMove,at+QPointF(2,0),Qt::NoButton,Qt::LeftButton);
        QCOMPARE(warnings.size(),0);mouse(canvas,QEvent::MouseButtonRelease,at,Qt::LeftButton,{});
        QCOMPARE(warnings.size(),1);QVERIFY(warnings.first().first().toString().contains("another joint"));
        QCOMPARE(*c->selectedSkin(),original);QCOMPARE(canvas.document()->history()->count(),0);
    }
    void transientPreviewDoesNotPolluteNormalImagesOrExports(){
#ifndef TOON_HAS_FILAMENT
        QSKIP("Requires supported GPU renderer");
#else
        Canvas canvas;canvas.resize(1000,800);canvas.setProject(fixture());canvas.show();canvas.fitSheet();auto *c=canvas.scene3D();
        QTRY_VERIFY_WITH_TIMEOUT(!c->rendering(),15000);const auto ordinary=scene3DPreview(*c->scene(),{480,320},true);QVERIFY(!ordinary.isNull());
        const auto movie=renderMovieFrame(canvas.project());const auto sheet=renderSheet(canvas.project(),canvas.shot());
        const auto before=canvas.project().toJson();choose(canvas);QTRY_VERIFY_WITH_TIMEOUT(!c->rendering(),15000);
        QImage heat;{Scene3DPreviewScope view(c->previewBaseKey(),c->previewDisplayKey());heat=scene3DPreview(*c->scene(),{480,320});}
        QVERIFY(heat!=ordinary);QCOMPARE(scene3DPreview(*c->scene(),{480,320}),ordinary);
        c->setSkinPreviewRotation({0,0,50});QVERIFY(!c->canPaintWeights());QVERIFY(canvas.toolBlockReason().contains("Reset"));
        QTRY_VERIFY_WITH_TIMEOUT(!c->rendering(),15000);QImage bent;
        {Scene3DPreviewScope view(c->previewBaseKey(),c->previewDisplayKey());bent=scene3DPreview(*c->scene(),{480,320});}
        QVERIFY(bent!=heat);QCOMPARE(canvas.project().toJson(),before);QCOMPARE(renderMovieFrame(canvas.project()),movie);
        QCOMPARE(renderSheet(canvas.project(),canvas.shot()),sheet);
        canvas.setTool(Canvas::Tool::SceneObject);QTRY_VERIFY_WITH_TIMEOUT(!c->rendering(),15000);
        {Scene3DPreviewScope view(c->previewBaseKey(),c->previewDisplayKey());QCOMPARE(scene3DPreview(*c->scene(),{480,320}),ordinary);}
        QCOMPARE(canvas.document()->history()->count(),0);
#endif
    }
    void preview(){
        const auto output=qEnvironmentVariable("TOON_UI_PREVIEW");if(output.isEmpty())QSKIP("Opt-in preview");
        Canvas canvas;canvas.resize(1100,820);canvas.setProject(fixture());canvas.show();canvas.fitSheet();choose(canvas);
        auto *c=canvas.scene3D();QTRY_VERIFY_WITH_TIMEOUT(!c->rendering(),15000);QDir().mkpath(output);
        QVERIFY(canvas.grab().save(output+"/skin-canvas-weights.png"));c->setSkinPreviewRotation({0,0,50});
        QTRY_VERIFY_WITH_TIMEOUT(!c->rendering(),15000);QVERIFY(canvas.grab().save(output+"/skin-canvas-bend.png"));
    }
};
QTEST_MAIN(SkinWorkspaceTests)
#include "test_skin_workspace.moc"
