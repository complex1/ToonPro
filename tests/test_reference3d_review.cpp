#include "reference3d_review.h"
#include "reference3d.h"
#include "reference3d_builtin.h"
#include "reference3d_catalog.h"
#include "reference3d_library.h"
#include "reference3d_pose_library.h"
#include "reference3d_pets.h"
#include "scene3d.h"
#include "ui_theme.h"
#include "value_slider.h"
#include <QDoubleSpinBox>
#include <QDockWidget>
#include <QCheckBox>
#include <QDataStream>
#include <QEventLoop>
#include <QDir>
#include <QFile>
#include <QLineEdit>
#include <QProgressDialog>
#include <QPushButton>
#include <QPointer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QListWidget>
#include <QSet>
#include <QSlider>
#include <QStandardPaths>
#include <QTabWidget>
#include <QTabBar>
#include <QTemporaryDir>
#include <QToolButton>
#include <QTimer>
#include <QUndoStack>
#include <QtTest>
#include <limits>

using namespace toon;
namespace {
ValueSlider *field(QWidget &window,const char *name){return window.findChild<ValueSlider*>(name);}
QToolButton *button(QWidget &window,const char *name){return window.findChild<QToolButton*>(name);}
QVector<QVector3D> vertices(const ReferenceInstance &instance){
    QVector<QVector3D> result;const auto evaluated=evaluateReferenceInstance(instance);
    if(evaluated)for(const auto &primitive:evaluated->primitives)for(const auto &v:primitive.vertices)result.append(v.position);
    return result;
}
bool hasModelPixels(const QImage &image){
    if(image.isNull())return false;QSet<QRgb> colors;
    for(int y=0;y<image.height();y+=3)for(int x=0;x<image.width();x+=3){colors.insert(image.pixel(x,y));if(colors.size()>12)return true;}
    return false;
}
bool exactPreviewReady(const Reference3DReviewWindow &window){
    const auto stats=window.previewStats();
    return window.viewportReady()&&stats.finalQuality&&stats.publishedGeneration==stats.requestedGeneration;
}
QPoint viewportPoint(const Reference3DReviewWindow &window,QVector3D world){
    const auto &source=window.scene().camera;Camera3D camera;camera.target=source.target;camera.yaw=source.yaw;camera.pitch=source.pitch;
    camera.distance=source.distance;camera.fieldOfView=source.fieldOfView;camera.orthographic=source.orthographic;camera.orthoScale=source.orthoScale;
    const auto size=window.viewportWidget()->size();
    const auto clip=cameraProjectionMatrix(camera,float(size.width())/qMax(1,size.height()))*cameraViewMatrix(camera)*QVector4D(world,1);
    const auto normalized=clip.toVector3DAffine();
    return {qRound((normalized.x()+1)*.5f*size.width()),qRound((1-normalized.y())*.5f*size.height())};
}
QColor pixelAtWorld(const Reference3DReviewWindow &window,QVector3D world){
    const auto point=viewportPoint(window,world);const auto image=window.viewportImage();const auto size=window.viewportWidget()->size();
    if(image.isNull()||!size.isValid())return {};
    return image.pixelColor(qBound(0,point.x()*image.width()/size.width(),image.width()-1),qBound(0,point.y()*image.height()/size.height(),image.height()-1));
}
ReferenceScene texturedQuad(ReferenceAlphaMode mode,int leftAlpha,int baseAlpha,float vertexAlpha,QString *error){
    ReferenceAsset source;source.name="RGBA channel fixture";ReferenceMaterial material;material.name="Tinted texture";material.baseColor={128,192,255,baseAlpha};
    material.alphaMode=mode;material.alphaCutoff=.75f;material.doubleSided=true;material.baseColorTexture=QImage(2,1,QImage::Format_RGBA8888);
    material.baseColorTexture.setPixelColor(0,0,QColor(200,40,20,leftAlpha));material.baseColorTexture.setPixelColor(1,0,QColor(20,60,220,255));source.materials.append(material);
    ReferencePrimitive primitive;primitive.materialIndex=0;primitive.indices={0,2,1,0,3,2};
    const QVector<QVector3D> positions{{-.75f,.75f,0},{.75f,.75f,0},{.75f,-.75f,0},{-.75f,-.75f,0}};
    const QVector<QVector2D> uv{{0,0},{1,0},{1,1},{0,1}};
    for(int i=0;i<4;++i){ReferenceVertex vertex;vertex.position=positions[i];vertex.normal={0,0,1};vertex.uv=uv[i];vertex.color={.5f,.75f,1,vertexAlpha};primitive.vertices.append(vertex);}
    ReferenceMesh mesh;mesh.name="Quad";mesh.primitives.append(primitive);source.meshes.append(mesh);ReferenceNode node;node.name="Surface";node.meshIndex=0;source.nodes.append(node);
    ReferenceInstance instance;instance.id="rgba-quad";instance.name="RGBA quad";instance.asset=finalizeReferenceAsset(std::move(source),error);
    ReferenceScene scene;scene.instances.append(instance);scene.camera.target={0,0,0};scene.camera.yaw=0;scene.camera.pitch=0;scene.camera.distance=3;scene.camera.orthographic=true;scene.camera.orthoScale=2;
    scene.light.intensity=0;scene.light.ambientIntensity=1;return scene;
}
}
class Reference3DReviewTests final:public QObject{
    Q_OBJECT
private slots:
    void initTestCase(){QStandardPaths::setTestModeEnabled(true);applyTheme();}
    void startersPoseAndUndoKeepCanonicalRig(){
        QTemporaryDir directory;Reference3DReviewWindow window(nullptr,directory.path());QString error;
        const auto asset=createReferenceStarter(ReferenceStarterKind::Figure,&error);QVERIFY2(asset,qPrintable(error));
        QVERIFY2(window.addAsset(*asset,&error),qPrintable(error));QCOMPARE(window.scene().instances.size(),1);
        auto *history=window.undoStack();QCOMPARE(history->count(),1);
        const int arm=asset->humanoidBones.value("leftUpperArm",-1);QVERIFY(arm>=0);
        window.selectJoint(arm);const auto original=vertices(window.scene().instances.first());
        QVERIFY(window.setJointRotation({0,0,45}));QCOMPARE(history->count(),2);
        QVERIFY(vertices(window.scene().instances.first())!=original);
        QCOMPARE(window.scene().instances.first().asset->id,asset->id);
        QCOMPARE(window.scene().instances.first().asset->skins.size(),asset->skins.size());
        history->undo();QCOMPARE(vertices(window.scene().instances.first()),original);history->redo();
        QVERIFY(!window.scene().instances.first().poseRotations.isEmpty());
        button(window,"referenceResetPose")->click();QVERIFY(window.scene().instances.first().poseRotations.isEmpty());history->undo();
        QVERIFY(!window.scene().instances.first().poseRotations.isEmpty());
        button(window,"referenceLocked")->click();const int count=history->count();
        QVERIFY(!window.setJointRotation({0,0,70}));QCOMPARE(history->count(),count);
        QVERIFY(!field(window,"referenceJointRotationZ")->isEnabled());
        QVERIFY(window.findChild<QWidget*>("reference3DViewport"));
        QVERIFY(!window.findChild<QWidget*>("rig3DContent"));
    }
    void shapeAndMorphGesturesPreviewCommitAndCancel(){
        QTemporaryDir directory;Reference3DReviewWindow window(nullptr,directory.path());QString error;
        const auto asset=createReferenceStarter(ReferenceStarterKind::Figure,&error);QVERIFY(asset);QVERIFY(window.addAsset(*asset,&error));
        auto *height=field(window,"referenceShape_height");QVERIFY(height);
        auto *track=height->findChild<QSlider*>();auto *history=window.undoStack();const int initial=history->index();
        const auto original=vertices(window.scene().instances.first());
        track->setSliderDown(true);track->setValue(185);track->setValue(195);
        QCOMPARE(history->index(),initial);QVERIFY(vertices(window.scene().instances.first())!=original);
        track->setSliderDown(false);QCOMPARE(history->index(),initial+1);
        history->undo();QCOMPARE(vertices(window.scene().instances.first()),original);history->redo();
        height=field(window,"referenceShape_height");QVERIFY(height);const auto committed=vertices(window.scene().instances.first());track=height->findChild<QSlider*>();
        track->setSliderDown(true);track->setValue(155);height->cancelEditing();
        QCOMPARE(vertices(window.scene().instances.first()),committed);QCOMPARE(history->index(),initial+1);
        const auto *instance=&window.scene().instances.first();const auto &mesh=instance->asset->meshes.first();QVERIFY(!mesh.primitives.first().morphTargets.isEmpty());
        QVERIFY(window.setMorphWeight(0,0,.75));QVERIFY(vertices(window.scene().instances.first())!=committed);
        history->undo();QCOMPARE(vertices(window.scene().instances.first()),committed);
        QVERIFY(!window.setMorphWeight(0,100,.5));QVERIFY(!window.setMorphWeight(0,0,std::numeric_limits<float>::infinity()));
    }
    void materialsRoundTripAndApplyWithoutReplacingPlacement(){
        QTemporaryDir directory;QString id,poseId,error;QVector<QVector3D> expected;
        {
            Reference3DReviewWindow window(nullptr,directory.path());const auto asset=createReferenceStarter(ReferenceStarterKind::Figure,&error);QVERIFY(asset);QVERIFY(window.addAsset(*asset,&error));
            window.selectJoint(asset->humanoidBones.value("leftUpperArm"));QVERIFY(window.setJointRotation({0,0,55}));expected=vertices(window.scene().instances.first());
            id=window.saveMaterial("My character",ReferenceLibraryKind::Character,&error);QVERIFY2(!id.isEmpty(),qPrintable(error));
            poseId=window.saveMaterial("Raised arm",ReferenceLibraryKind::Pose,&error);QVERIFY2(!poseId.isEmpty(),qPrintable(error));
            QCOMPARE(window.materialLibrary()->entries().size(),2);
        }
        Reference3DReviewWindow reopened(nullptr,directory.path());QCOMPARE(reopened.materialLibrary()->entries().size(),2);
        QVERIFY2(reopened.applyMaterial(id,&error),qPrintable(error));QCOMPARE(reopened.scene().instances.size(),1);QCOMPARE(vertices(reopened.scene().instances.first()),expected);
        auto *position=field(reopened,"referencePositionX")->findChild<QDoubleSpinBox*>();position->setValue(2.25);
        const auto placement=reopened.scene().instances.first().translation;
        button(reopened,"referenceResetPose")->click();QVERIFY(reopened.scene().instances.first().poseRotations.isEmpty());
        QVERIFY2(reopened.applyMaterial(poseId,&error),qPrintable(error));QVERIFY(!reopened.scene().instances.first().poseRotations.isEmpty());
        QCOMPARE(reopened.scene().instances.first().translation,placement);
        QVERIFY(reopened.materialLibrary()->setFavorite(id,true,&error));
        auto *favorites=button(reopened,"referenceMaterialFavorites");favorites->click();
        QCOMPARE(reopened.findChild<QListWidget*>("referenceMaterials")->count(),1);
        ReferenceMaterialLibrary external(directory.path());QVERIFY(external.load(&error));
        QVERIFY(!external.addScene(reopened.scene(),"Other window",ReferenceLibraryKind::Scene,&error).isEmpty());
        QVERIFY(reopened.saveMaterial("Stale writer",ReferenceLibraryKind::Scene,&error).isEmpty());QVERIFY(!error.isEmpty());
        button(reopened,"referenceReloadMaterials")->click();QCOMPARE(reopened.materialLibrary()->entries().size(),3);
        QVERIFY2(!reopened.saveMaterial("After reload",ReferenceLibraryKind::Scene,&error).isEmpty(),qPrintable(error));
    }
    void limbIKAndExpressionMixingRemainUndoable(){
        QTemporaryDir directory;Reference3DReviewWindow window(nullptr,directory.path());QString error;
        const auto asset=createReferenceStarter(ReferenceStarterKind::Figure,&error);QVERIFY(asset);QVERIFY(window.addAsset(*asset,&error));
        const int hand=asset->humanoidBones.value("leftHand",-1),elbow=asset->humanoidBones.value("leftLowerArm",-1);QVERIFY(hand>=0&&elbow>=0);
        window.selectJoint(hand);window.findChild<QTabBar*>("referencePoseModes")->setCurrentIndex(1);
        const auto evaluated=evaluateReferenceInstance(window.scene().instances.first());QVERIFY(evaluated);
        const auto upper=evaluated->nodeGlobals[asset->humanoidBones.value("leftUpperArm")].map(QVector3D());
        const auto end=evaluated->nodeGlobals[hand].map(QVector3D());
        const auto target=upper+(end-upper)*.7f+QVector3D(0,0,(end-upper).length()*.1f);
        const auto pole=evaluated->nodeGlobals[elbow].map(QVector3D())+QVector3D(0,0,.3f);
        const int initial=window.undoStack()->index();QVERIFY2(window.setIKTarget(target,pole,&error),qPrintable(error));
        QCOMPARE(window.undoStack()->index(),initial+1);const auto posed=evaluateReferenceInstance(window.scene().instances.first());QVERIFY(posed);
        QVERIFY((posed->nodeGlobals[hand].map(QVector3D())-target).length()<.001f);
        window.undoStack()->undo();QCOMPARE(window.scene().instances.first().poseRotations.size(),0);
        auto *smile=field(window,"referenceExpression_Smile");auto *blink=field(window,"referenceExpression_Blink");QVERIFY(smile&&blink);
        smile->findChild<QDoubleSpinBox*>()->setValue(.6);blink->findChild<QDoubleSpinBox*>()->setValue(.4);
        QCOMPARE(window.scene().instances.first().expressionWeights.value("Smile"),.6f);QCOMPARE(window.scene().instances.first().expressionWeights.value("Blink"),.4f);
        QCOMPARE(field(window,"referenceExpression_Smile")->value(),.6);QCOMPARE(field(window,"referenceExpression_Blink")->value(),.4);
        window.undoStack()->undo();QCOMPARE(window.scene().instances.first().expressionWeights.value("Smile"),.6f);QCOMPARE(window.scene().instances.first().expressionWeights.value("Blink"),0.f);
        QCOMPARE(field(window,"referenceExpression_Blink")->value(),0.);
    }
    void sceneMaterialUndoRestoresCompositionCamera(){
        QTemporaryDir directory;Reference3DReviewWindow window(nullptr,directory.path());QString error;
        const auto asset=createReferenceStarter(ReferenceStarterKind::Head,&error);QVERIFY(asset);QVERIFY(window.addAsset(*asset,&error));
        const auto before=window.scene();auto composition=before;composition.camera.yaw=120;composition.camera.pitch=-12;composition.camera.distance*=2;composition.camera.target={1,2,3};composition.instances[0].translation={2,0,0};
        const auto id=window.materialLibrary()->addScene(composition,"Different composition",ReferenceLibraryKind::Scene,&error);QVERIFY2(!id.isEmpty(),qPrintable(error));
        QVERIFY2(window.applyMaterial(id,&error),qPrintable(error));QCOMPARE(window.scene().camera.yaw,120.f);QCOMPARE(window.scene().instances.first().translation,QVector3D(2,0,0));
        window.undoStack()->undo();QCOMPARE(window.scene().camera.yaw,before.camera.yaw);QCOMPARE(window.scene().camera.pitch,before.camera.pitch);QCOMPARE(window.scene().camera.target,before.camera.target);QCOMPARE(window.scene().camera.distance,before.camera.distance);QCOMPARE(window.scene().instances.first().translation,before.instances.first().translation);
        window.undoStack()->redo();QCOMPARE(window.scene().camera.yaw,composition.camera.yaw);QCOMPARE(window.scene().camera.target,composition.camera.target);QCOMPARE(window.scene().instances.first().translation,QVector3D(2,0,0));
    }
    void saveCommitsExactEntryAndFailedOpenPreservesScene(){
        QTemporaryDir directory;Reference3DReviewWindow window(nullptr,directory.path()+"/library");QString error;
        const auto asset=createReferenceStarter(ReferenceStarterKind::Head,&error);QVERIFY(asset);QVERIFY(window.addAsset(*asset,&error));
        auto *number=field(window,"referencePositionX")->findChild<QDoubleSpinBox*>();auto *text=number->findChild<QLineEdit*>();
        text->setFocus();text->selectAll();QTest::keyClicks(text,"1.375");
        auto *name=window.findChild<QLineEdit*>("referenceObjectName");name->setFocus();name->selectAll();QTest::keyClicks(name,"Custom head");
        const auto path=directory.filePath("scene.toon3d");QVERIFY2(window.saveScene(path,&error),qPrintable(error));
        QCOMPARE(window.scene().instances.first().translation.x(),1.375f);QCOMPARE(window.scene().instances.first().name,QString("Custom head"));QVERIFY(!window.isWindowModified());
        Reference3DReviewWindow restored(nullptr,directory.path()+"/other");QVERIFY2(restored.openScene(path,&error),qPrintable(error));
        QCOMPARE(serializeReferenceScene(restored.scene()),serializeReferenceScene(window.scene()));
        const auto before=serializeReferenceScene(restored.scene());QFile corrupt(directory.filePath("broken.toon3d"));QVERIFY(corrupt.open(QIODevice::WriteOnly));corrupt.write("invalid");corrupt.close();
        QVERIFY(!restored.openScene(corrupt.fileName(),&error));QVERIFY(!error.isEmpty());QCOMPARE(serializeReferenceScene(restored.scene()),before);
    }
    void viewportDrawsDeformationAndCameraNavigationIsOutsideUndo(){
        QTemporaryDir directory;Reference3DReviewWindow window(nullptr,directory.path());window.resize(1100,700);window.show();QString error;
        const auto asset=createReferenceStarter(ReferenceStarterKind::Figure,&error);QVERIFY(asset);QVERIFY(window.addAsset(*asset,&error));
        QTRY_VERIFY_WITH_TIMEOUT(hasModelPixels(window.viewportImage()),15000);
        const auto original=window.viewportImage();window.selectJoint(asset->humanoidBones.value("leftUpperArm"));QVERIFY(window.setJointRotation({0,0,70}));
        QTRY_VERIFY_WITH_TIMEOUT(window.viewportImage()!=original,15000);
        auto *viewport=window.viewportWidget();const auto camera=window.scene().camera;const int index=window.undoStack()->index();
        const QPoint center=viewport->rect().center();QTest::mousePress(viewport,Qt::RightButton,Qt::NoModifier,center);QTest::mouseMove(viewport,center+QPoint(60,20));QTest::mouseRelease(viewport,Qt::RightButton,Qt::NoModifier,center+QPoint(60,20));
        QVERIFY(window.scene().camera.yaw!=camera.yaw);QCOMPARE(window.undoStack()->index(),index);
        const auto navigated=window.scene().camera;window.undoStack()->undo();QCOMPARE(window.scene().camera.yaw,navigated.yaw);
        for(auto *control:window.findChildren<QToolButton*>())if(!control->icon().isNull())QVERIFY2(!control->accessibleName().isEmpty(),qPrintable(control->objectName()));
        const auto capture=qEnvironmentVariable("TOON_REFERENCE3D_CAPTURE");if(!capture.isEmpty())QVERIFY(window.grab().save(capture));
    }
    void continuousTransformsPublishDuringDragAndReusePoseGeometry(){
        QTemporaryDir directory;Reference3DReviewWindow window(nullptr,directory.path());window.resize(1180,760);window.show();QString error;
        const auto asset=createReferenceStarter(ReferenceStarterKind::Figure,&error);QVERIFY2(asset,qPrintable(error));QVERIFY2(window.addAsset(*asset,&error),qPrintable(error));
        QTRY_VERIFY_WITH_TIMEOUT(exactPreviewReady(window)&&!window.viewportImage().isNull(),15000);
        for(const auto &control:QStringList{"referencePositionX","referenceRotationZ","referenceScaleX"}){
            auto *value=window.findChild<ValueSlider*>(control);QVERIFY(value);auto *track=value->findChild<QSlider*>();QVERIFY(track);
            const auto before=window.previewStats();const auto original=window.scene().instances.first();const auto originalImage=window.viewportImage();
            const double start=value->value(),span=control.contains("Position")?.35:control.contains("Rotation")?45.:.25;
            const int history=window.undoStack()->index();int updates=0,updatesWhileWorkerRunning=0;bool timedOut=false,dragging=true;
            QVector<quint64> publishedDuringDrag;QEventLoop loop;QTimer driver,safety;driver.setInterval(12);driver.setTimerType(Qt::PreciseTimer);safety.setSingleShot(true);
            const auto publication=connect(&window,&Reference3DReviewWindow::previewFramePublished,&loop,[&](quint64 generation,bool){if(dragging)publishedDuringDrag.append(generation);});
            connect(&driver,&QTimer::timeout,&loop,[&]{
                const auto running=window.previewStats();if(running.startedFrames>running.completedFrames)++updatesWhileWorkerRunning;
                value->setValue(start+span*double(updates+1)/60.);if(++updates==60)loop.quit();
            });
            connect(&safety,&QTimer::timeout,&loop,[&]{timedOut=true;loop.quit();});
            track->setSliderDown(true);driver.start();safety.start(15000);loop.exec();driver.stop();safety.stop();dragging=false;
            const auto atRelease=window.previewStats();track->setSliderDown(false);disconnect(publication);
            QVERIFY2(!timedOut,qPrintable(control));QCOMPARE(updates,60);
            QVERIFY2(publishedDuringDrag.size()>=2,qPrintable(control+": sustained input starved viewport publication until release"));
            for(int i=1;i<publishedDuringDrag.size();++i)QVERIFY(publishedDuringDrag[i]>=publishedDuringDrag[i-1]);
            QVERIFY(atRelease.startedFrames>before.startedFrames);QCOMPARE(atRelease.geometryEvaluations,before.geometryEvaluations);
            qInfo().noquote()<<control<<"updates overlapping rendering:"<<updatesWhileWorkerRunning<<"published during drag:"<<publishedDuringDrag.size();
            QTRY_VERIFY_WITH_TIMEOUT(exactPreviewReady(window),15000);QVERIFY(window.viewportError().isEmpty());
            const auto after=window.previewStats();QCOMPARE(after.geometryEvaluations,before.geometryEvaluations);QVERIFY(after.cacheHits>before.cacheHits);
            QCOMPARE(window.undoStack()->index(),history+1);QVERIFY(window.viewportImage()!=originalImage);
            if(control.contains("Position"))QVERIFY(std::abs(window.scene().instances.first().translation.x()-float(start+span))<.0001f);
            else if(control.contains("Scale"))QVERIFY(std::abs(window.scene().instances.first().scale.x()-float(start+span))<.0001f);
            else QVERIFY(std::abs(QQuaternion::dotProduct(window.scene().instances.first().rotation,QQuaternion::fromEulerAngles(0,0,float(start+span))))>.9999f);
            window.undoStack()->undo();QCOMPARE(window.undoStack()->index(),history);
            QCOMPARE(window.scene().instances.first().translation,original.translation);QCOMPARE(window.scene().instances.first().rotation,original.rotation);QCOMPARE(window.scene().instances.first().scale,original.scale);
            QTRY_VERIFY_WITH_TIMEOUT(exactPreviewReady(window),15000);QCOMPARE(window.viewportImage(),originalImage);
            // Picking must use the exact committed/undone frame, not a previous
            // drag generation's object-ID surface.
            window.selectObject(-1);QTRY_VERIFY_WITH_TIMEOUT(exactPreviewReady(window),15000);
            const auto evaluated=evaluateReferenceInstance(window.scene().instances.first());QVERIFY(evaluated);
            auto *viewport=window.viewportWidget();const auto point=viewportPoint(window,evaluated->bounds.center());QVERIFY(viewport->rect().contains(point));
            QTest::mouseClick(viewport,Qt::LeftButton,Qt::NoModifier,point);
            auto *objects=window.findChild<QListWidget*>("referenceSceneObjects");QVERIFY(objects);QCOMPARE(objects->currentRow(),0);
            QCOMPARE(window.undoStack()->index(),history);QTRY_VERIFY_WITH_TIMEOUT(exactPreviewReady(window),15000);
        }
    }
    void poseCacheInvalidatesForJointMorphAndExpressionChanges(){
        QTemporaryDir directory;Reference3DReviewWindow window(nullptr,directory.path());window.resize(1180,760);window.show();QString error;
        const auto asset=createReferenceStarter(ReferenceStarterKind::Figure,&error);QVERIFY(asset);QVERIFY(window.addAsset(*asset,&error));
        QTRY_VERIFY_WITH_TIMEOUT(exactPreviewReady(window),15000);auto previous=window.previewStats();
        window.selectJoint(asset->humanoidBones.value("leftUpperArm",-1));QVERIFY(window.setJointRotation({0,0,55}));
        QTRY_VERIFY_WITH_TIMEOUT(exactPreviewReady(window),15000);auto current=window.previewStats();QVERIFY(current.geometryEvaluations>previous.geometryEvaluations);previous=current;
        QVERIFY(window.setMorphWeight(0,0,.8f));QTRY_VERIFY_WITH_TIMEOUT(exactPreviewReady(window),15000);
        current=window.previewStats();QVERIFY(current.geometryEvaluations>previous.geometryEvaluations);previous=current;
        auto *smile=field(window,"referenceExpression_Smile");QVERIFY(smile);smile->setValue(.6);
        QTRY_VERIFY_WITH_TIMEOUT(exactPreviewReady(window),15000);current=window.previewStats();QVERIFY(current.geometryEvaluations>previous.geometryEvaluations);previous=current;
        // A camera change can reuse the same posed geometry even though its
        // projected pixels and picking surface have to be regenerated.
        auto *viewport=window.viewportWidget();const QPoint center=viewport->rect().center();const auto camera=window.scene().camera;
        QTest::mousePress(viewport,Qt::RightButton,Qt::NoModifier,center);QTest::mouseMove(viewport,center+QPoint(45,15));QTest::mouseRelease(viewport,Qt::RightButton,Qt::NoModifier,center+QPoint(45,15));
        QVERIFY(window.scene().camera.yaw!=camera.yaw);QTRY_VERIFY_WITH_TIMEOUT(exactPreviewReady(window),15000);
        current=window.previewStats();QCOMPARE(current.geometryEvaluations,previous.geometryEvaluations);QVERIFY(current.cacheHits>previous.cacheHits);
    }
    void openingAnotherSceneCannotPickAnObjectFromTheOldFrame(){
        QTemporaryDir directory;Reference3DReviewWindow window(nullptr,directory.path()+"/materials");window.resize(1180,760);window.show();QString error;
        const auto figure=createReferenceStarter(ReferenceStarterKind::Figure,&error);QVERIFY(figure);QVERIFY(window.addAsset(*figure,&error));
        QTRY_VERIFY_WITH_TIMEOUT(exactPreviewReady(window),15000);
        const auto evaluated=evaluateReferenceInstance(window.scene().instances.first());QVERIFY(evaluated);
        auto *viewport=window.viewportWidget();const QPoint oldModel=viewportPoint(window,evaluated->bounds.center());QVERIFY(viewport->rect().contains(oldModel));
        ReferenceScene replacement;replacement.name="Different character off camera";replacement.camera=window.scene().camera;
        ReferenceInstance head;head.id="different-stable-object-id";head.name="Off-camera head";head.asset=createReferenceStarter(ReferenceStarterKind::Head,&error);head.translation={100,0,0};replacement.instances.append(head);
        const auto path=directory.filePath("replacement.toon3d");QVERIFY2(saveReferenceScene(replacement,path,&error),qPrintable(error));
        QVERIFY2(window.openScene(path,&error),qPrintable(error));window.selectObject(-1);
        auto *objects=window.findChild<QListWidget*>("referenceSceneObjects");QVERIFY(objects);QCOMPARE(objects->currentRow(),-1);
        // No event-loop wait: if the preceding frame is retained while the new
        // scene renders, its numeric object slot must not select this new ID.
        QTest::mousePress(viewport,Qt::LeftButton,Qt::NoModifier,oldModel,0);QTest::mouseRelease(viewport,Qt::LeftButton,Qt::NoModifier,oldModel,0);
        QCOMPARE(objects->currentRow(),-1);QCOMPARE(window.scene().instances.first().id,head.id);
        QTRY_VERIFY_WITH_TIMEOUT(exactPreviewReady(window),15000);
        QTest::mouseClick(viewport,Qt::LeftButton,Qt::NoModifier,oldModel,0);QCOMPARE(objects->currentRow(),-1);
    }
    void texturedPreviewPreservesRGBAChannelsTintMaskAndBlend(){
        QTemporaryDir directory;Reference3DReviewWindow window(nullptr,directory.path());window.resize(1180,760);window.show();QString error;
        auto opaque=texturedQuad(ReferenceAlphaMode::Opaque,255,255,1,&error);QVERIFY2(opaque.instances.first().asset,qPrintable(error));
        auto background=opaque;background.instances.clear();QVERIFY2(window.setScene(background,&error),qPrintable(error));
        QTRY_VERIFY_WITH_TIMEOUT(exactPreviewReady(window),15000);
        const QVector3D left(-.4f,.2f,0),right(.4f,.2f,0);const auto leftBackground=pixelAtWorld(window,left),rightBackground=pixelAtWorld(window,right);
        const auto expected=[](const QColor &texture,const QColor &background,float baseAlpha,float vertexAlpha,bool blend){
            const double alpha=blend?texture.alphaF()*baseAlpha*vertexAlpha:1.;
            return QColor(qRound(texture.red()*(128./255.)*.5*alpha+background.red()*(1-alpha)),
                qRound(texture.green()*(192./255.)*.75*alpha+background.green()*(1-alpha)),qRound(texture.blue()*alpha+background.blue()*(1-alpha)),255);
        };
        const auto near=[](const QColor &actual,const QColor &wanted){return actual.isValid()&&std::abs(actual.red()-wanted.red())<=1&&std::abs(actual.green()-wanted.green())<=1&&std::abs(actual.blue()-wanted.blue())<=1&&actual.alpha()==wanted.alpha();};
        const QColor red(200,40,20,255),blue(20,60,220,255);QVERIFY2(window.setScene(opaque,&error),qPrintable(error));
        QTRY_VERIFY_WITH_TIMEOUT(exactPreviewReady(window),15000);
        const auto wantedRed=expected(red,leftBackground,1,1,false),wantedBlue=expected(blue,rightBackground,1,1,false);
        QVERIFY2(near(pixelAtWorld(window,left),wantedRed),qPrintable("Red channel sample: "+pixelAtWorld(window,left).name()+" expected "+wantedRed.name()));
        QVERIFY2(near(pixelAtWorld(window,right),wantedBlue),qPrintable("Blue channel sample: "+pixelAtWorld(window,right).name()+" expected "+wantedBlue.name()));
        const auto geometry=window.previewStats().geometryEvaluations;field(window,"referencePositionX")->setValue(.15);
        QTRY_VERIFY_WITH_TIMEOUT(exactPreviewReady(window),15000);QCOMPARE(window.previewStats().geometryEvaluations,geometry);
        QVERIFY(near(pixelAtWorld(window,left+QVector3D(.15f,0,0)),wantedRed));QVERIFY(near(pixelAtWorld(window,right+QVector3D(.15f,0,0)),wantedBlue));
        const auto mask=texturedQuad(ReferenceAlphaMode::Mask,128,255,1,&error);QVERIFY2(window.setScene(mask,&error),qPrintable(error));
        QTRY_VERIFY_WITH_TIMEOUT(exactPreviewReady(window),15000);QVERIFY(near(pixelAtWorld(window,left),leftBackground));QVERIFY(near(pixelAtWorld(window,right),wantedBlue));
        const auto blend=texturedQuad(ReferenceAlphaMode::Blend,128,192,.5f,&error);QVERIFY2(window.setScene(blend,&error),qPrintable(error));
        QTRY_VERIFY_WITH_TIMEOUT(exactPreviewReady(window),15000);
        const auto blendRed=expected(QColor(200,40,20,128),leftBackground,192.f/255,.5f,true),blendBlue=expected(blue,rightBackground,192.f/255,.5f,true);
        QVERIFY2(near(pixelAtWorld(window,left),blendRed),qPrintable("Blended RGBA sample: "+pixelAtWorld(window,left).name()+" expected "+blendRed.name()));
        QVERIFY2(near(pixelAtWorld(window,right),blendBlue),qPrintable("Blended blue sample: "+pixelAtWorld(window,right).name()+" expected "+blendBlue.name()));
        for(const auto format:{QImage::Format_ARGB32,QImage::Format_ARGB32_Premultiplied,QImage::Format_RGBA8888_Premultiplied}){
            auto converted=blend;auto asset=*blend.instances.first().asset;asset.id.clear();
            asset.materials[0].baseColorTexture=asset.materials[0].baseColorTexture.convertToFormat(format);
            converted.instances[0].asset=finalizeReferenceAsset(std::move(asset),&error);QVERIFY2(converted.instances.first().asset,qPrintable(error));
            QVERIFY2(window.setScene(converted,&error),qPrintable(error));QTRY_VERIFY_WITH_TIMEOUT(exactPreviewReady(window),15000);
            QVERIFY2(near(pixelAtWorld(window,left),blendRed),qPrintable(QString("Texture format %1 red: %2 expected %3").arg(int(format)).arg(pixelAtWorld(window,left).name(),blendRed.name())));
            QVERIFY2(near(pixelAtWorld(window,right),blendBlue),qPrintable(QString("Texture format %1 blue: %2 expected %3").arg(int(format)).arg(pixelAtWorld(window,right).name(),blendBlue.name())));
        }
    }
    void importedLinearFactorsAndSrgbTexturesUseTheCorrectDisplayTransfer(){
        QTemporaryDir directory;Reference3DReviewWindow window(nullptr,directory.path());window.resize(1180,760);window.show();QString error;
        auto scene=texturedQuad(ReferenceAlphaMode::Opaque,255,255,1,&error);auto asset=*scene.instances[0].asset;
        asset.metadata["sourceGltf"]=QJsonObject{};asset.materials[0].baseColor=QColor::fromRgbF(.173,.070,.044);
        asset.materials[0].baseColorTexture={};for(auto &v:asset.meshes[0].primitives[0].vertices)v.color={1,1,1,1};
        scene.instances[0].asset=finalizeReferenceAsset(asset,&error);QVERIFY2(scene.instances[0].asset,qPrintable(error));
        QVERIFY(window.setScene(scene,&error));QTRY_VERIFY_WITH_TIMEOUT(exactPreviewReady(window),15000);
        auto color=pixelAtWorld(window,{-.4f,.2f,0});
        // Standard linear -> sRGB values, not linear values written as bytes.
        QVERIFY(std::abs(color.red()-115)<=1);QVERIFY(std::abs(color.green()-75)<=1);QVERIFY(std::abs(color.blue()-59)<=1);
        asset.materials[0].baseColor=QColor::fromRgbF(.5,.5,.5);asset.materials[0].baseColorTexture=QImage(1,1,QImage::Format_RGBA8888);asset.materials[0].baseColorTexture.fill(QColor(128,128,128));
        for(auto &v:asset.meshes[0].primitives[0].vertices)v.color={.5f,.5f,.5f,1};scene.light.ambientIntensity=.5;
        scene.instances[0].asset=finalizeReferenceAsset(asset,&error);QVERIFY2(scene.instances[0].asset,qPrintable(error));QVERIFY(window.setScene(scene,&error));QTRY_VERIFY_WITH_TIMEOUT(exactPreviewReady(window),15000);
        color=pixelAtWorld(window,{-.4f,.2f,0});QVERIFY(std::abs(color.red()-46)<=1);QVERIFY(std::abs(color.green()-46)<=1);QVERIFY(std::abs(color.blue()-46)<=1);
        // The source and its canonical bytes retain their original linear data.
        QCOMPARE(scene.instances[0].asset->materials[0].baseColor.redF(),asset.materials[0].baseColor.redF());
        QVERIFY(!serializeReferenceScene(scene,&error).isEmpty());
    }
    void coplanarOpaqueOrderStaysStableWhenBlendTrianglesArePresent(){
        QTemporaryDir directory;Reference3DReviewWindow window(nullptr,directory.path());window.resize(1180,760);window.show();QString error;
        ReferenceAsset source;source.name="Coplanar source-order fixture";
        ReferenceMaterial transparent;transparent.name="Blend first in source";transparent.baseColor={0,255,0,128};transparent.alphaMode=ReferenceAlphaMode::Blend;transparent.doubleSided=true;
        ReferenceMaterial red;red.name="First opaque surface";red.baseColor={230,30,15};red.doubleSided=true;
        ReferenceMaterial blue;blue.name="Second coplanar surface";blue.baseColor={10,50,230};blue.doubleSided=true;source.materials={transparent,red,blue};
        ReferencePrimitive blend;blend.materialIndex=0;blend.indices={0,1,2};
        for(const auto &point:QVector<QVector3D>{{-.7f,-.55f,.05f},{-.25f,-.55f,.05f},{-.5f,-.1f,.05f}}){ReferenceVertex vertex;vertex.position=point;vertex.normal={0,0,1};blend.vertices.append(vertex);}
        const auto opaque=[](int material){ReferencePrimitive primitive;primitive.materialIndex=material;primitive.indices={0,2,1,0,3,2};
            for(const auto &point:QVector<QVector3D>{{-.75f,.75f,0},{.75f,.75f,0},{.75f,-.75f,0},{-.75f,-.75f,0}}){ReferenceVertex vertex;vertex.position=point;vertex.normal={0,0,1};primitive.vertices.append(vertex);}return primitive;};
        // An unstable in-place partition moves the last blue opaque primitive
        // ahead of red to fill this leading transparent slot. Equal-depth
        // opaque pixels must instead keep their original first-writer order.
        ReferenceMesh mesh;mesh.name="Interleaved material classes";mesh.primitives={blend,opaque(1),opaque(2)};source.meshes.append(mesh);
        ReferenceNode node;node.name="Surface";node.meshIndex=0;source.nodes.append(node);
        ReferenceInstance instance;instance.id="coplanar";instance.name="Coplanar";instance.asset=finalizeReferenceAsset(std::move(source),&error);QVERIFY2(instance.asset,qPrintable(error));
        ReferenceScene scene;scene.instances.append(instance);scene.camera.target={0,0,0};scene.camera.yaw=0;scene.camera.pitch=0;scene.camera.distance=3;scene.camera.orthographic=true;scene.camera.orthoScale=2;
        scene.light.intensity=0;scene.light.ambientIntensity=1;QVERIFY2(window.setScene(scene,&error),qPrintable(error));QTRY_VERIFY_WITH_TIMEOUT(exactPreviewReady(window),15000);
        const auto first=pixelAtWorld(window,{.35f,.25f,0});
        QVERIFY2(std::abs(first.red()-230)<=1&&std::abs(first.green()-30)<=1&&std::abs(first.blue()-15)<=1,qPrintable("Coplanar first-writer color changed: "+first.name()));
        const auto blended=pixelAtWorld(window,{-.48f,-.4f,.05f});QVERIFY(blended.green()>first.green()+60);
        const auto geometry=window.previewStats().geometryEvaluations;field(window,"referencePositionX")->setValue(.1);QTRY_VERIFY_WITH_TIMEOUT(exactPreviewReady(window),15000);
        QCOMPARE(window.previewStats().geometryEvaluations,geometry);QCOMPARE(pixelAtWorld(window,{.45f,.25f,0}),first);
    }
    void repeatedInstancesFailPreviewBudgetWithoutDiscardingSources(){
        ReferenceAsset source;source.name="Detailed reference";ReferencePrimitive primitive;
        for(int triangle=0;triangle<6000;++triangle){const float x=(triangle%80)*.02f,y=(triangle/80)*.02f;
            for(const auto &position:QVector<QVector3D>{{x,y,0},{x+.01f,y,0},{x,y+.01f,0}}){ReferenceVertex vertex;vertex.position=position;vertex.normal={0,0,1};primitive.indices.append(primitive.vertices.size());primitive.vertices.append(vertex);}
        }
        ReferenceMesh mesh;mesh.name="Surface";mesh.primitives.append(primitive);source.meshes.append(mesh);ReferenceNode node;node.name="Surface";node.meshIndex=0;source.nodes.append(node);
        QString error;const auto asset=finalizeReferenceAsset(source,&error);QVERIFY2(asset,qPrintable(error));
        ReferenceScene scene;for(int i=0;i<128;++i){ReferenceInstance instance;instance.id=QString("copy-%1").arg(i);instance.name="Detailed reference";instance.asset=asset;scene.instances.append(instance);}
        QVERIFY(!reference3DPreviewBudgetError(scene).isEmpty());
        auto single=scene;single.instances.resize(1);QVERIFY(reference3DPreviewBudgetError(single).isEmpty());
        QTemporaryDir directory;Reference3DReviewWindow window(nullptr,directory.path());QVERIFY2(window.setScene(scene,&error),qPrintable(error));
        QTRY_VERIFY_WITH_TIMEOUT(window.viewportReady(),15000);QVERIFY(!window.viewportError().isEmpty());QVERIFY(window.viewportImage().isNull());
        QCOMPARE(window.scene().instances.size(),128);QCOMPARE(window.scene().instances.first().asset->id,asset->id);QCOMPARE(window.scene().instances.first().asset->meshes.first().primitives.first().vertices.size(),18000);
        button(window,"referenceRemove")->click();QCOMPARE(window.scene().instances.size(),127);
        QVERIFY(window.setScene(single,&error));QTRY_VERIFY_WITH_TIMEOUT(window.viewportReady(),15000);QVERIFY(window.viewportError().isEmpty());QVERIFY(!window.viewportImage().isNull());
    }
    void asynchronousImportPublishesAfterProgressCloses(){
        QTemporaryDir directory;QByteArray data;QDataStream stream(&data,QIODevice::WriteOnly);stream.setByteOrder(QDataStream::LittleEndian);stream.setFloatingPointPrecision(QDataStream::SinglePrecision);
        for(float value:{0.f,0.f,0.f,1.f,0.f,0.f,0.f,1.f,0.f})stream<<value;
        QJsonObject gltf{{"asset",QJsonObject{{"version","2.0"}}},{"scene",0},{"scenes",QJsonArray{QJsonObject{{"nodes",QJsonArray{0}}}}},{"nodes",QJsonArray{QJsonObject{{"mesh",0}}}},
            {"buffers",QJsonArray{QJsonObject{{"byteLength",data.size()},{"uri","data:application/octet-stream;base64,"+QString::fromLatin1(data.toBase64())}}}},
            {"bufferViews",QJsonArray{QJsonObject{{"buffer",0},{"byteLength",data.size()}}}},
            {"accessors",QJsonArray{QJsonObject{{"bufferView",0},{"componentType",5126},{"count",3},{"type","VEC3"},{"min",QJsonArray{0,0,0}},{"max",QJsonArray{1,1,0}}}}},
            {"meshes",QJsonArray{QJsonObject{{"primitives",QJsonArray{QJsonObject{{"attributes",QJsonObject{{"POSITION",0}}}}}}}}}};
        const auto path=directory.filePath("triangle.gltf");QFile file(path);QVERIFY(file.open(QIODevice::WriteOnly));file.write(QJsonDocument(gltf).toJson());file.close();
        Reference3DReviewWindow window(nullptr,directory.path()+"/materials");window.importFileAsync(path);QVERIFY(window.importInProgress());
        QTRY_VERIFY_WITH_TIMEOUT(!window.importInProgress(),15000);QCOMPARE(window.scene().instances.size(),1);QVERIFY(window.scene().instances.first().asset);
        QCOMPARE(window.undoStack()->count(),1);QVERIFY(!window.scene().instances.first().asset->meshes.isEmpty());
    }
    void modelAndPoseBrowsersAreSearchableAndBrowsingIsNeutral(){
        QTemporaryDir directory;Reference3DReviewWindow window(nullptr,directory.path());auto *models=window.findChild<QListWidget*>("referenceStarters");auto *kinds=window.findChild<QTabBar*>("referenceModelKinds");auto *search=window.findChild<QLineEdit*>("referenceModelSearch");QVERIFY(models&&kinds&&search);
        QCOMPARE(models->count(),4);kinds->setCurrentIndex(1);QCOMPARE(models->count(),9);search->setText("round");QCOMPARE(models->count(),1);QCOMPARE(models->item(0)->data(Qt::UserRole).toString(),QString("head/round"));
        kinds->setCurrentIndex(2);search->clear();QCOMPARE(models->count(),7);QCOMPARE(window.scene().instances.size(),0);QCOMPARE(window.undoStack()->count(),0);
        auto *poses=window.findChild<QListWidget*>("referencePosePresets");auto *poseKinds=window.findChild<QTabBar*>("referencePoseKinds");auto *poseSearch=window.findChild<QLineEdit*>("referencePoseSearch");QVERIFY(poses&&poseKinds&&poseSearch);QCOMPARE(poses->count(),20);poseKinds->setCurrentIndex(1);QCOMPARE(poses->count(),11);poseSearch->setText("fist");QCOMPARE(poses->count(),1);QCOMPARE(poses->item(0)->data(Qt::UserRole).toString(),QString("hands/fist"));QCOMPARE(window.undoStack()->count(),0);
        QString error;QVERIFY2(window.placeStarter("head/round",&error),qPrintable(error));QCOMPARE(window.scene().instances.size(),1);QCOMPARE(window.undoStack()->count(),1);
        QTRY_VERIFY_WITH_TIMEOUT(exactPreviewReady(window),15000);const auto before=serializeReferenceScene(window.scene());window.requestPresetThumbnail("head/round");window.requestPresetThumbnail("head/angular");
        QTRY_VERIFY_WITH_TIMEOUT(!window.presetThumbnail("head/round").isNull()&&!window.presetThumbnail("head/angular").isNull(),15000);
        QCOMPARE(window.presetThumbnail("head/round").size(),QSize(176,176));QVERIFY(window.presetThumbnail("head/round")!=window.presetThumbnail("head/angular"));QCOMPARE(serializeReferenceScene(window.scene()),before);QCOMPARE(window.undoStack()->count(),1);
    }
    void animeAndPetCategoriesAreDirectSearchableAndNeutral(){
        QTemporaryDir directory;Reference3DReviewWindow window(nullptr,directory.path());auto *models=window.findChild<QListWidget*>("referenceStarters");auto *kinds=window.findChild<QTabBar*>("referenceModelKinds");auto *search=window.findChild<QLineEdit*>("referenceModelSearch");QCOMPARE(kinds->count(),5);kinds->setCurrentIndex(3);QCOMPARE(models->count(),2);QSet<QString> anime;for(int row=0;row<models->count();++row)anime.insert(models->item(row)->data(Qt::UserRole).toString());QCOMPARE(anime,QSet<QString>({"anime/seed-san","anime/alicia-solid"}));search->setText("Alicia");QCOMPARE(models->count(),1);
        search->clear();kinds->setCurrentIndex(4);QCOMPARE(models->count(),3);search->setText("cat");QCOMPARE(models->count(),1);QCOMPARE(models->item(0)->data(Qt::UserRole).toString(),QString("pet/cat"));QVERIFY(window.scene().instances.isEmpty());QCOMPARE(window.undoStack()->count(),0);
        window.show();kinds->setFixedWidth(210);QCoreApplication::processEvents();for(int tab=0;tab<kinds->count();++tab){QVERIFY(kinds->isTabEnabled(tab));QVERIFY(kinds->tabRect(tab).width()>=28);QVERIFY2(kinds->rect().contains(kinds->tabRect(tab).center()),qPrintable(QString("Tab %1 (%2) rectangle %3,%4 %5x%6 exceeds bar %7x%8").arg(tab).arg(kinds->tabText(tab)).arg(kinds->tabRect(tab).x()).arg(kinds->tabRect(tab).y()).arg(kinds->tabRect(tab).width()).arg(kinds->tabRect(tab).height()).arg(kinds->width()).arg(kinds->height())));QVERIFY(!kinds->tabIcon(tab).isNull());QVERIFY(!kinds->tabToolTip(tab).isEmpty());QVERIFY(!kinds->accessibleTabName(tab).isEmpty());}QVERIFY(window.scene().instances.isEmpty());QCOMPARE(window.undoStack()->count(),0);
    }
    void animeCatalogPlacementUsesActualRigAndExpressionsWithOneUndo(){
        QTemporaryDir directory;Reference3DReviewWindow window(nullptr,directory.path());auto *kinds=window.findChild<QTabBar*>("referenceModelKinds");kinds->setCurrentIndex(3);window.findChild<QLineEdit*>("referenceModelSearch")->setText("Seed");button(window,"referencePlaceStarter")->click();QVERIFY(window.catalogPlacementInProgress());QVERIFY(window.scene().instances.isEmpty());
        QTRY_VERIFY_WITH_TIMEOUT(!window.catalogPlacementInProgress(),30000);QCOMPARE(window.scene().instances.size(),1);QCOMPARE(window.undoStack()->count(),1);const auto placed=window.scene().instances.first();QVERIFY(placed.asset);QVERIFY(!placed.asset->skins.isEmpty());QVERIFY(placed.asset->humanoidBones.contains("leftUpperArm"));QVERIFY(!placed.asset->expressions.isEmpty());QVERIFY(referenceBodyShapeParameters(placed).isEmpty());QVERIFY(!field(window,"referenceShape_height"));QVERIFY(!window.findChild<QCheckBox*>("referenceHeadMixLimit"));
        for(const auto &expression:placed.asset->expressions)QVERIFY2(field(window,qPrintable("referenceExpression_"+expression.name)),qPrintable(expression.name));window.undoStack()->undo();QVERIFY(window.scene().instances.isEmpty());window.undoStack()->redo();QCOMPARE(window.scene().instances.first().asset,placed.asset);
        window.requestPresetThumbnail("body/reach-up",true);window.requestPresetThumbnail("body/reach-up",true,true);QTRY_VERIFY_WITH_TIMEOUT(!window.presetThumbnail("body/reach-up",true).isNull()&&!window.presetThumbnail("body/reach-up",true,true).isNull(),30000);QVERIFY(window.presetThumbnail("body/reach-up",true)!=window.presetThumbnail("body/reach-up",true,true));
        const auto restVertices=vertices(window.scene().instances.first());window.selectJoint(placed.asset->humanoidBones.value("leftUpperArm"));QVERIFY(window.setJointRotation({0,0,30}));QVERIFY(vertices(window.scene().instances.first())!=restVertices);QTRY_VERIFY_WITH_TIMEOUT(exactPreviewReady(window),30000);QVERIFY(hasModelPixels(window.viewportImage()));
        QString error;const auto sceneFile=directory.filePath("anime.toon3d");QVERIFY(window.saveScene(sceneFile,&error));kinds->setCurrentIndex(4);QVERIFY(window.openScene(sceneFile,&error));QCOMPARE(kinds->currentIndex(),3);QCOMPARE(window.findChild<QListWidget*>("referenceSceneObjects")->currentRow(),0);kinds->setCurrentIndex(1);window.selectJoint(placed.asset->humanoidBones.value("head"));QVERIFY(window.setJointRotation({0,10,0}));QCOMPARE(kinds->currentIndex(),1);
    }
    void switchingBetweenAnimeAndPetsRebuildsMorphBindingsBeforeValueRefresh(){
        QTemporaryDir directory;Reference3DReviewWindow window(nullptr,directory.path());QString error;
        const auto morphFields=[&]{QVector<ValueSlider*> result;for(auto *field:window.findChildren<ValueSlider*>())if(field->objectName().startsWith("referenceMorph_"))result.append(field);return result;};
        QVERIFY2(window.placeCatalogModel("anime/seed-san",&error),qPrintable(error));
        const auto seed=window.scene().instances.first();QVERIFY(!morphFields().isEmpty());
        QPointer<ValueSlider> previousMorph=morphFields().first();
        // No event-loop wait: edit() refreshes values before committing the
        // placement, while the preceding model's controls still exist.
        QVERIFY2(window.placeCatalogModel("pet/dog",&error),qPrintable(error));
        QCOMPARE(window.scene().instances.size(),2);QVERIFY(morphFields().isEmpty());
        const auto animal=window.scene().instances.last();const int placedIndex=window.undoStack()->index();
        if(previousMorph)previousMorph->setValue(.5);
        QCOMPARE(window.undoStack()->index(),placedIndex);QVERIFY(window.scene().instances.last().morphOverrides.isEmpty());
        window.undoStack()->undo();QCOMPARE(window.scene().instances.size(),1);window.selectObject(0);QVERIFY(!morphFields().isEmpty());
        window.undoStack()->redo();QCOMPARE(window.scene().instances.size(),2);window.selectObject(1);QVERIFY(morphFields().isEmpty());
        window.selectObject(0);QVERIFY(!morphFields().isEmpty());
        button(window,"referenceRemove")->click();QCOMPARE(window.scene().instances.size(),1);QCOMPARE(window.scene().instances.first().asset,animal.asset);QVERIFY(morphFields().isEmpty());
        window.undoStack()->undo();QCOMPARE(window.scene().instances.size(),2);QVERIFY(!morphFields().isEmpty());
        window.undoStack()->redo();QCOMPARE(window.scene().instances.size(),1);QVERIFY(morphFields().isEmpty());
        ReferenceScene replacement;auto replaced=seed;replaced.asset=animal.asset;replacement.instances={replaced};
        QVERIFY2(window.setScene(replacement,&error),qPrintable(error));QCOMPARE(window.scene().instances.first().id,seed.id);QVERIFY(morphFields().isEmpty());
        replacement.instances[0]=seed;QVERIFY2(window.setScene(replacement,&error),qPrintable(error));QVERIFY(!morphFields().isEmpty());
        QTRY_VERIFY_WITH_TIMEOUT(exactPreviewReady(window),30000);QVERIFY(window.viewportError().isEmpty());
    }
    void catalogPlacementCancelReplacementAndDestructionDoNotPublish(){
        QTemporaryDir directory;Reference3DReviewWindow window(nullptr,directory.path());window.placeCatalogModelAsync("pet/dog");QVERIFY(window.catalogPlacementInProgress());auto *progress=window.findChild<QProgressDialog*>("referenceCatalogProgress");QVERIFY(progress);auto *cancel=progress->findChild<QPushButton*>();QVERIFY(cancel);cancel->click();QVERIFY(!window.catalogPlacementInProgress());QTRY_VERIFY_WITH_TIMEOUT(!window.presetThumbnail("pet/dog").isNull(),15000);QVERIFY(window.scene().instances.isEmpty());QCOMPARE(window.undoStack()->count(),0);
        window.placeCatalogModelAsync("pet/cat");QVERIFY(window.catalogPlacementInProgress());QString error;QVERIFY(window.setScene(ReferenceScene{},&error));QVERIFY(!window.catalogPlacementInProgress());QTRY_VERIFY_WITH_TIMEOUT(!window.presetThumbnail("pet/cat").isNull(),15000);QVERIFY(window.scene().instances.isEmpty());QCOMPARE(window.undoStack()->count(),0);
        QPointer<Reference3DReviewWindow> transient=new Reference3DReviewWindow(nullptr,directory.path()+"/transient");transient->placeCatalogModelAsync("anime/alicia-solid");QVERIFY(transient->catalogPlacementInProgress());QCoreApplication::processEvents();delete transient.data();QVERIFY(transient.isNull());bool loopContinues=false;QTimer::singleShot(100,&window,[&]{loopContinues=true;});QTRY_VERIFY_WITH_TIMEOUT(loopContinues,5000);
    }
    void petPoseBrowserUsesNativeSpeciesAndRetainsPlacement(){
        QTemporaryDir directory;Reference3DReviewWindow window(nullptr,directory.path());QString error;QVERIFY2(window.placeCatalogModel("pet/dog",&error),qPrintable(error));auto source=window.scene();source.instances[0].translation={1,.1f,-.3f};source.instances[0].rotation=QQuaternion::fromEulerAngles(0,25,0);QVERIFY(window.setScene(source,&error));const auto original=window.scene().instances.first();QVERIFY(referenceBodyShapeParameters(original).isEmpty());const auto catalog=referencePetPosePresets(original);QVERIFY(catalog.size()>1);auto *kinds=window.findChild<QTabBar*>("referencePoseKinds");kinds->setCurrentIndex(2);auto *poses=window.findChild<QListWidget*>("referencePosePresets");QCOMPARE(poses->count(),catalog.size());QVERIFY(button(window,"referenceMirrorPose")->isHidden());
        const QString pose="pet/dog/walking-left";QVERIFY2(window.applyPosePreset(pose,&error),qPrintable(error));const auto posed=window.scene().instances.first();QCOMPARE(window.undoStack()->count(),1);QCOMPARE(posed.asset,original.asset);QCOMPARE(posed.translation,original.translation);QCOMPARE(posed.rotation,original.rotation);QCOMPARE(posed.scale,original.scale);QCOMPARE(posed.morphOverrides,original.morphOverrides);QVERIFY(vertices(posed)!=vertices(original));window.undoStack()->undo();QCOMPARE(vertices(window.scene().instances.first()),vertices(original));window.undoStack()->redo();QCOMPARE(vertices(window.scene().instances.first()),vertices(posed));
        window.requestPresetThumbnail(pose,true);QTRY_VERIFY_WITH_TIMEOUT(!window.presetThumbnail(pose,true).isNull(),15000);QVERIFY2(window.placeCatalogModel("pet/cat",&error),qPrintable(error));const auto catCatalog=referencePetPosePresets(window.scene().instances.last());QCOMPARE(poses->count(),catCatalog.size());for(int row=0;row<poses->count();++row)QVERIFY(poses->item(row)->data(Qt::UserRole).toString().startsWith("pet/cat/"));QTRY_VERIFY_WITH_TIMEOUT(exactPreviewReady(window),15000);QVERIFY(hasModelPixels(window.viewportImage()));
    }
    void posePresetActionsPreserveShapePlacementAndOneUndo(){
        QTemporaryDir directory;Reference3DReviewWindow window(nullptr,directory.path());QString error;QVERIFY2(window.placeStarter("figure/athletic",&error),qPrintable(error));auto scene=window.scene();scene.instances[0].translation={1.25f,.1f,-.2f};scene.instances[0].rotation=QQuaternion::fromEulerAngles(0,25,0);scene.instances[0].expressionWeights["Smile"]=.3f;QVERIFY(window.setScene(scene,&error));const auto original=window.scene().instances.first();
        QVERIFY2(window.applyPosePreset("body/reach-up",&error),qPrintable(error));const auto reached=window.scene().instances.first();QCOMPARE(window.undoStack()->count(),1);QCOMPARE(reached.translation,original.translation);QCOMPARE(reached.rotation,original.rotation);QCOMPARE(reached.asset,original.asset);QCOMPARE(reached.expressionWeights,original.expressionWeights);QVERIFY(vertices(reached)!=vertices(original));window.undoStack()->undo();QCOMPARE(vertices(window.scene().instances.first()),vertices(original));window.undoStack()->redo();QCOMPARE(vertices(window.scene().instances.first()),vertices(reached));
        auto *category=window.findChild<QTabBar*>("referencePoseCategories");int seated=-1;for(int i=0;i<category->count();++i)if(category->tabText(i)=="Seated")seated=i;QVERIFY(seated>=0);category->setCurrentIndex(seated);window.findChild<QLineEdit*>("referencePoseSearch")->setText("chair");QCOMPARE(window.findChild<QListWidget*>("referencePosePresets")->count(),1);const int before=window.undoStack()->index();button(window,"referenceApplyPose")->click();QCOMPARE(window.undoStack()->index(),before+1);QCOMPARE(window.scene().instances.first().translation,original.translation);
        auto *poseKinds=window.findChild<QTabBar*>("referencePoseKinds");poseKinds->setCurrentIndex(1);window.findChild<QLineEdit*>("referencePoseSearch")->setText("fist");auto *side=window.findChild<QTabBar*>("referencePoseHandTarget");side->setCurrentIndex(1);const auto beforeHand=window.scene().instances.first();button(window,"referenceApplyPose")->click();const auto afterHand=window.scene().instances.first();QCOMPARE(afterHand.translation,beforeHand.translation);
        bool changedLeft=false;for(auto it=beforeHand.asset->humanoidBones.begin();it!=beforeHand.asset->humanoidBones.end();++it){if(it.key().startsWith("right"))QCOMPARE(afterHand.poseRotations.value(it.value()),beforeHand.poseRotations.value(it.value()));if(it.key().startsWith("left")&&afterHand.poseRotations.value(it.value())!=beforeHand.poseRotations.value(it.value()))changedLeft=true;}QVERIFY(changedLeft);
        window.requestPresetThumbnail("body/reach-up",true);window.requestPresetThumbnail("body/seated-chair",true);QTRY_VERIFY_WITH_TIMEOUT(!window.presetThumbnail("body/reach-up",true).isNull()&&!window.presetThumbnail("body/seated-chair",true).isNull(),15000);QVERIFY(window.presetThumbnail("body/reach-up",true)!=window.presetThumbnail("body/seated-chair",true));
    }
    void shapeMixersAndBuildPadChangeGeometryWithoutReplacingPose(){
        QTemporaryDir directory;Reference3DReviewWindow window(nullptr,directory.path());QString error;QVERIFY2(window.placeStarter("figure/balanced",&error),qPrintable(error));const int arm=window.scene().instances.first().asset->humanoidBones.value("leftUpperArm");window.selectJoint(arm);QVERIFY(window.setJointRotation({0,0,35}));const auto pose=window.scene().instances.first().poseRotations;const auto placement=window.scene().instances.first().translation;
        ValueSlider *mix=nullptr;for(auto *candidate:window.findChildren<ValueSlider*>())if(candidate->property("referenceMixFamily").toInt()==int(ReferenceStarterKind::Head)&&candidate->property("referenceMixId").toString().contains("angular")){mix=candidate;break;}QVERIFY(mix);const auto original=vertices(window.scene().instances.first());auto *track=mix->findChild<QSlider*>();const int before=window.undoStack()->index();track->setSliderDown(true);track->setValue(450);track->setValue(650);QCOMPARE(window.undoStack()->index(),before);track->setSliderDown(false);QCOMPARE(window.undoStack()->index(),before+1);QVERIFY(vertices(window.scene().instances.first())!=original);QCOMPARE(window.scene().instances.first().poseRotations,pose);QCOMPARE(window.scene().instances.first().translation,placement);window.undoStack()->undo();QCOMPARE(vertices(window.scene().instances.first()),original);
        auto *pad=window.findChild<QWidget*>("referenceBodyBuildPad");QVERIFY(pad);auto *properties=window.findChild<QTabWidget*>("referencePropertyTabs");properties->setCurrentIndex(2);window.show();QTest::qWait(30);const int padBefore=window.undoStack()->index();QTest::mousePress(pad,Qt::LeftButton,Qt::NoModifier,pad->rect().center());QTest::mouseMove(pad,QPoint(pad->width()-20,24));QTest::mouseRelease(pad,Qt::LeftButton,Qt::NoModifier,QPoint(pad->width()-20,24));QCOMPARE(window.undoStack()->index(),padBefore+1);QVERIFY(vertices(window.scene().instances.first())!=original);window.undoStack()->undo();QCOMPARE(vertices(window.scene().instances.first()),original);
        const auto beforeCancel=serializeReferenceScene(window.scene());const int cancelIndex=window.undoStack()->index();QTest::mousePress(pad,Qt::LeftButton,Qt::NoModifier,QPoint(40,pad->height()-30));QTest::mouseMove(pad,QPoint(pad->width()-15,20));QTest::keyClick(pad,Qt::Key_Escape);QTest::mouseRelease(pad,Qt::LeftButton,Qt::NoModifier,QPoint(pad->width()-15,20));QCOMPARE(window.undoStack()->index(),cancelIndex);QCOMPARE(serializeReferenceScene(window.scene()),beforeCancel);
    }
    void handControlsPoseSelectedFingersWithUndoAndRetainDraftTarget(){
        QTemporaryDir directory;Reference3DReviewWindow window(nullptr,directory.path());QString error;QVERIFY2(window.placeStarter("figure/balanced",&error),qPrintable(error));
        const auto bones=window.scene().instances.first().asset->humanoidBones;window.selectJoint(bones.value("leftUpperArm"));QVERIFY(window.setJointRotation({10,0,20}));window.selectJoint(bones.value("leftHand"));QVERIFY(window.setJointRotation({0,15,5}));const auto original=window.scene().instances.first();
        window.findChild<QTabWidget*>("referencePropertyTabs")->setCurrentIndex(1);window.findChild<QTabBar*>("referencePoseModes")->setCurrentIndex(2);auto *sides=window.findChild<QTabBar*>("referenceHandControlSides");auto *fingers=window.findChild<QTabBar*>("referenceHandControlFingers");QVERIFY(sides&&fingers);sides->setCurrentIndex(1);window.show();QTest::qWait(20);
        auto *curl=field(window,"referenceHandCurl"),*spread=field(window,"referenceHandSplay");QVERIFY(curl&&spread&&curl->isEnabled());auto *track=curl->findChild<QSlider*>();const int before=window.undoStack()->index();track->setSliderDown(true);track->setValue(420);track->setValue(720);QCOMPARE(window.undoStack()->index(),before);track->setSliderDown(false);QCOMPARE(window.undoStack()->index(),before+1);
        const auto curled=window.scene().instances.first();QVERIFY(vertices(curled)!=vertices(original));QCOMPARE(curled.translation,original.translation);QCOMPARE(curled.asset,original.asset);QCOMPARE(curled.poseRotations.value(bones.value("leftHand")),original.poseRotations.value(bones.value("leftHand")));QCOMPARE(curled.poseRotations.value(bones.value("leftUpperArm")),original.poseRotations.value(bones.value("leftUpperArm")));
        for(auto it=bones.begin();it!=bones.end();++it)if(it.key().startsWith("right"))QCOMPARE(curled.poseRotations.value(it.value()),original.poseRotations.value(it.value()));
        for(const auto &control:referenceHandControls(curled,ReferenceHandTarget::Left,&error))QVERIFY2(std::abs(control.curl-.72f)<.02f,qPrintable(control.id));window.undoStack()->undo();QCOMPARE(window.scene().instances.first().poseRotations,original.poseRotations);window.undoStack()->redo();QCOMPARE(window.scene().instances.first().poseRotations,curled.poseRotations);
        fingers->setCurrentIndex(2);const auto beforeIndex=window.scene().instances.first();spread->findChild<QDoubleSpinBox*>()->setValue(42);const auto indexSpread=window.scene().instances.first();bool changedIndex=false;for(auto it=bones.begin();it!=bones.end();++it){if(it.key().startsWith("leftIndex"))changedIndex|=indexSpread.poseRotations.value(it.value())!=beforeIndex.poseRotations.value(it.value());else QCOMPARE(indexSpread.poseRotations.value(it.value()),beforeIndex.poseRotations.value(it.value()));}QVERIFY(changedIndex);
        auto *number=curl->findChild<QDoubleSpinBox*>();number->setFocus();number->selectAll();QTest::keyClicks(number,"31");QVERIFY(curl->editingActive());const int beforeSwitch=window.undoStack()->index();fingers->setCurrentIndex(3);QCOMPARE(window.undoStack()->index(),beforeSwitch+1);QVERIFY(!curl->editingActive());
        const auto controls=referenceHandControls(window.scene().instances.first(),ReferenceHandTarget::Left,&error);bool checkedIndex=false,checkedMiddle=false;for(const auto &control:controls){if(control.id=="index"){QVERIFY(std::abs(control.curl-.31f)<.02f);QVERIFY(std::abs(control.splay-.42f)<.02f);checkedIndex=true;}if(control.id=="middle"){QVERIFY(std::abs(control.curl-.72f)<.02f);checkedMiddle=true;}}QVERIFY(checkedIndex&&checkedMiddle);QVERIFY(std::abs(curl->value()-72)<2);
        const auto beforeCancel=serializeReferenceScene(window.scene());const int cancelIndex=window.undoStack()->index();auto *spreadTrack=spread->findChild<QSlider*>();spreadTrack->setSliderDown(true);spreadTrack->setValue(-600);QTest::keyClick(spreadTrack,Qt::Key_Escape);QCOMPARE(window.undoStack()->index(),cancelIndex);QCOMPARE(serializeReferenceScene(window.scene()),beforeCancel);
        auto mixed=window.scene();QVERIFY(setReferenceHandControls(mixed.instances[0],{{"index",{.2f,.7f}}},ReferenceHandTarget::Left,&error));QVERIFY(setReferenceHandControls(mixed.instances[0],{{"index",{.8f,-.2f}}},ReferenceHandTarget::Right,&error));QVERIFY(window.setScene(mixed,&error));sides->setCurrentIndex(0);fingers->setCurrentIndex(2);curl->findChild<QDoubleSpinBox*>()->setValue(50);QCOMPARE(window.undoStack()->count(),0);curl->findChild<QDoubleSpinBox*>()->setValue(60);QCOMPARE(window.undoStack()->count(),1);
        for(const auto target:{ReferenceHandTarget::Left,ReferenceHandTarget::Right}){bool checked=false;for(const auto &control:referenceHandControls(window.scene().instances.first(),target,&error))if(control.id=="index"){QVERIFY(std::abs(control.curl-.6f)<.02f);QVERIFY(std::abs(control.splay-(target==ReferenceHandTarget::Left?.7f:-.2f))<.02f);checked=true;}QVERIFY(checked);}window.undoStack()->undo();QCOMPARE(window.scene().instances.first().poseRotations,mixed.instances.first().poseRotations);
    }
    void handSideTabsMirrorThePosedStudyAndFollowUndo(){
        QTemporaryDir directory;Reference3DReviewWindow window(nullptr,directory.path());QString error;QVERIFY2(window.placeStarter("hand/balanced",&error),qPrintable(error));QVERIFY2(window.applyPosePreset("hands/pinch",&error),qPrintable(error));auto source=window.scene();source.instances[0].translation={1,.2f,-.4f};source.instances[0].scale={1.1f,.9f,1.05f};QVERIFY(window.setScene(source,&error));const auto original=window.scene().instances.first();const auto left=vertices(original);
        auto *sides=window.findChild<QTabBar*>("referenceHandedness");QVERIFY(sides);QCOMPARE(sides->currentIndex(),0);QVERIFY(!field(window,"referenceShape_handedness"));sides->setCurrentIndex(1);QCOMPARE(window.undoStack()->count(),1);const auto right=window.scene().instances.first();const auto reflected=vertices(right);QCOMPARE(right.translation,original.translation);QCOMPARE(right.rotation,original.rotation);QCOMPARE(right.scale,original.scale);QVERIFY(right.asset->humanoidBones.contains("rightHand"));QVERIFY(!right.asset->humanoidBones.contains("leftHand"));QVERIFY(!right.poseRotations.isEmpty());QCOMPARE(reflected.size(),left.size());
        for(int i=0;i<left.size();++i){const auto expected=QVector3D(2*original.translation.x()-left[i].x(),left[i].y(),left[i].z());QVERIFY2((reflected[i]-expected).length()<.0005f,qPrintable(QString("Pose reflection differs at vertex %1").arg(i)));}
        window.undoStack()->undo();QCOMPARE(sides->currentIndex(),0);QCOMPARE(window.scene().instances.first().asset,original.asset);QCOMPARE(window.scene().instances.first().poseRotations,original.poseRotations);QCOMPARE(vertices(window.scene().instances.first()),left);window.undoStack()->redo();QCOMPARE(sides->currentIndex(),1);QCOMPARE(vertices(window.scene().instances.first()),reflected);
    }
    void captureReviewWorkflows(){
        const auto captureDirectory=qEnvironmentVariable("TOON_REFERENCE3D_CAPTURE_DIR");
        if(captureDirectory.isEmpty())QSKIP("Set TOON_REFERENCE3D_CAPTURE_DIR for actual widget review captures");
        QVERIFY(QDir().mkpath(captureDirectory));
        const QVector<ReferenceStarterKind>kinds{ReferenceStarterKind::Figure,ReferenceStarterKind::Head,ReferenceStarterKind::Hand};
        for(int index=0;index<kinds.size();++index){
            QTemporaryDir directory;Reference3DReviewWindow window(nullptr,directory.path());window.resize(1180,760);window.show();QString error;
            const auto asset=createReferenceStarter(kinds[index],&error);QVERIFY2(asset,qPrintable(error));QVERIFY2(window.addAsset(*asset,&error),qPrintable(error));
            auto *tabs=window.findChild<QTabWidget*>("referencePropertyTabs");QVERIFY(tabs);
            if(index==0){window.selectJoint(asset->humanoidBones.value("leftUpperArm"));QVERIFY(window.setJointRotation({15,-15,65}));window.selectJoint(asset->humanoidBones.value("rightLowerArm"));QVERIFY(window.setJointRotation({0,-30,-45}));tabs->setCurrentIndex(1);}
            else if(index==1){field(window,"referenceShape_chinWidth")->findChild<QDoubleSpinBox*>()->setValue(.65);field(window,"referenceShape_chinLength")->findChild<QDoubleSpinBox*>()->setValue(1.18);field(window,"referenceExpression_Smile")->findChild<QDoubleSpinBox*>()->setValue(.85);field(window,"referenceExpression_Blink")->findChild<QDoubleSpinBox*>()->setValue(.25);window.selectJoint(asset->humanoidBones.value("head"));QVERIFY(window.setJointRotation({-12,18,0}));tabs->setCurrentIndex(2);auto *groups=window.findChild<QTabBar*>("referenceShapeGroups");for(int i=0;i<groups->count();++i)if(groups->tabText(i)=="Chin")groups->setCurrentIndex(i);}
            else{for(const auto &name:QStringList{"leftIndexProximal","leftMiddleProximal","leftRingProximal","leftLittleProximal"}){window.selectJoint(asset->humanoidBones.value(name,-1));QVERIFY(window.setJointRotation({55,0,0}));}tabs->setCurrentIndex(1);}
            QTRY_VERIFY_WITH_TIMEOUT(window.viewportReady()&&hasModelPixels(window.viewportImage()),15000);
            const QString base=QStringList{"figure-posed","head-shaped-expression","hand-posed"}[index];
            QVERIFY(window.grab().save(QDir(captureDirectory).filePath(base+".png")));
            QVERIFY(window.viewportImage().save(QDir(captureDirectory).filePath(base+"-canvas.png")));
            if(index==1){window.resize(720,650);QCoreApplication::processEvents();button(window,"referenceFit")->click();QTRY_VERIFY_WITH_TIMEOUT(window.viewportReady(),15000);QVERIFY(window.grab().save(QDir(captureDirectory).filePath("head-narrow.png")));}
        }
    }
    void captureModelPoseAndShapeWorkflows(){
        const auto output=qEnvironmentVariable("TOON_REFERENCE3D_CAPTURE_DIR");if(output.isEmpty())QSKIP("Set TOON_REFERENCE3D_CAPTURE_DIR for model, pose and shape review captures");QVERIFY(QDir().mkpath(output));QString error;
        QTemporaryDir directory;Reference3DReviewWindow window(nullptr,directory.path());window.resize(1240,840);window.show();QVERIFY2(window.placeStarter("figure/balanced",&error),qPrintable(error));auto *browser=window.findChild<QTabWidget*>("referenceLibraryTabs");auto *properties=window.findChild<QTabWidget*>("referencePropertyTabs");QVERIFY(browser&&properties);
        const QStringList bodyModels{"figure/balanced","figure/slender","figure/athletic","figure/rounded"};for(const auto &id:bodyModels)window.requestPresetThumbnail(id);for(const auto &id:bodyModels)QTRY_VERIFY_WITH_TIMEOUT(!window.presetThumbnail(id).isNull(),20000);QTRY_VERIFY_WITH_TIMEOUT(exactPreviewReady(window),15000);QVERIFY(window.grab().save(QDir(output).filePath("model-browser.png")));
        browser->setCurrentIndex(1);const QStringList bodyPoses{"body/relaxed-standing","body/contrapposto","body/arms-crossed","body/hands-on-hips"};for(const auto &id:bodyPoses)window.requestPresetThumbnail(id,true);for(const auto &id:bodyPoses)QTRY_VERIFY_WITH_TIMEOUT(!window.presetThumbnail(id,true).isNull(),20000);QVERIFY2(window.applyPosePreset("body/reach-up",&error),qPrintable(error));QTRY_VERIFY_WITH_TIMEOUT(exactPreviewReady(window),15000);QVERIFY(window.grab().save(QDir(output).filePath("pose-browser.png")));
        properties->setCurrentIndex(2);auto *groups=window.findChild<QTabBar*>("referenceShapeGroups");for(int i=0;i<groups->count();++i)if(groups->tabText(i)=="Body")groups->setCurrentIndex(i);field(window,"referenceShape_build")->findChild<QDoubleSpinBox*>()->setValue(1.18);field(window,"referenceShape_definition")->findChild<QDoubleSpinBox*>()->setValue(.75);QTRY_VERIFY_WITH_TIMEOUT(exactPreviewReady(window),15000);QVERIFY(window.grab().save(QDir(output).filePath("body-build-controls.png")));
        QVERIFY(window.setScene(ReferenceScene{},&error));QVERIFY2(window.placeStarter("head/balanced",&error),qPrintable(error));properties->setCurrentIndex(2);groups=window.findChild<QTabBar*>("referenceShapeGroups");for(int i=0;i<groups->count();++i)if(groups->tabText(i)=="Head mix")groups->setCurrentIndex(i);for(auto *candidate:window.findChildren<ValueSlider*>())if(candidate->property("referenceMixFamily").toInt()==int(ReferenceStarterKind::Head)&&candidate->property("referenceMixId").toString().contains("angular"))candidate->findChild<QDoubleSpinBox*>()->setValue(55);browser->setCurrentIndex(0);window.findChild<QTabBar*>("referenceModelKinds")->setCurrentIndex(1);QTRY_VERIFY_WITH_TIMEOUT(exactPreviewReady(window),15000);QVERIFY(window.grab().save(QDir(output).filePath("head-family-mixer.png")));
        QVERIFY(window.setScene(ReferenceScene{},&error));QVERIFY2(window.placeStarter("hand/balanced",&error),qPrintable(error));QVERIFY2(window.applyPosePreset("hands/pinch",&error),qPrintable(error));properties->setCurrentIndex(1);window.findChild<QTabBar*>("referencePoseModes")->setCurrentIndex(2);window.findChild<QTabBar*>("referenceHandControlSides")->setCurrentIndex(1);window.findChild<QTabBar*>("referenceHandControlFingers")->setCurrentIndex(2);field(window,"referenceHandSplay")->findChild<QDoubleSpinBox*>()->setValue(32);browser->setCurrentIndex(1);window.findChild<QTabBar*>("referencePoseKinds")->setCurrentIndex(1);window.findChild<QLineEdit*>("referencePoseSearch")->setText("pinch");window.requestPresetThumbnail("hands/pinch",true);QTRY_VERIFY_WITH_TIMEOUT(!window.presetThumbnail("hands/pinch",true).isNull(),20000);button(window,"referenceFit")->click();QTRY_VERIFY_WITH_TIMEOUT(exactPreviewReady(window),15000);QVERIFY(window.grab().save(QDir(output).filePath("hand-finger-controls.png")));
    }
    void captureAnimeAndPetCatalogWorkflows(){
        const auto output=qEnvironmentVariable("TOON_REFERENCE3D_CAPTURE_DIR");if(output.isEmpty())QSKIP("Set TOON_REFERENCE3D_CAPTURE_DIR for actual anime and pet catalog captures");QVERIFY(QDir().mkpath(output));QString error;
        for(const auto &id:QStringList{"anime/seed-san","anime/alicia-solid"}){
            QTemporaryDir directory;Reference3DReviewWindow window(nullptr,directory.path());window.resize(1240,840);window.show();auto *browser=window.findChild<QTabWidget*>("referenceLibraryTabs");auto *properties=window.findChild<QTabWidget*>("referencePropertyTabs");window.findChild<QTabBar*>("referenceModelKinds")->setCurrentIndex(3);QVERIFY2(window.placeCatalogModel(id,&error),qPrintable(error));QVERIFY2(window.applyPosePreset("body/contrapposto",&error),qPrintable(error));button(window,"referenceFit")->click();
            for(const auto &model:QStringList{"anime/seed-san","anime/alicia-solid"})window.requestPresetThumbnail(model);for(const auto &model:QStringList{"anime/seed-san","anime/alicia-solid"})QTRY_VERIFY_WITH_TIMEOUT(!window.presetThumbnail(model).isNull(),30000);QTRY_VERIFY_WITH_TIMEOUT(exactPreviewReady(window),30000);QVERIFY(window.grab().save(QDir(output).filePath(QString(id).replace('/','-')+"-model-browser.png")));
            if(id!="anime/seed-san")continue;
            browser->setCurrentIndex(1);properties->setCurrentIndex(1);const QStringList previews{"body/relaxed-standing","body/contrapposto","body/arms-crossed","body/hands-on-hips"};for(const auto &pose:previews)window.requestPresetThumbnail(pose,true,true);for(const auto &pose:previews)QTRY_VERIFY_WITH_TIMEOUT(!window.presetThumbnail(pose,true,true).isNull(),30000);QTRY_VERIFY_WITH_TIMEOUT(exactPreviewReady(window),30000);QVERIFY(window.grab().save(QDir(output).filePath("anime-pose-browser.png")));
            const auto asset=window.scene().instances.first().asset;QString expression;for(const auto &candidate:asset->expressions)if(!candidate.bindings.isEmpty()&&(candidate.name.contains("happy",Qt::CaseInsensitive)||candidate.name.contains("joy",Qt::CaseInsensitive)||candidate.name.contains("smile",Qt::CaseInsensitive))){expression=candidate.name;break;}if(expression.isEmpty())for(const auto &candidate:asset->expressions)if(!candidate.bindings.isEmpty()){expression=candidate.name;break;}QVERIFY(!expression.isEmpty());field(window,qPrintable("referenceExpression_"+expression))->findChild<QDoubleSpinBox*>()->setValue(.8);
            auto portrait=window.scene();const auto globals=evaluateReferenceNodeGlobals(portrait.instances.first());QVERIFY(globals);const int head=asset->humanoidBones.value("head",-1);QVERIFY(head>=0);portrait.camera.target=(*globals)[head].map(QVector3D(0,.07f,0));portrait.camera.yaw=0;portrait.camera.pitch=0;portrait.camera.distance=.65f;QVERIFY(window.setScene(portrait,&error));browser->setCurrentIndex(0);properties->setCurrentIndex(3);QTRY_VERIFY_WITH_TIMEOUT(exactPreviewReady(window),30000);QVERIFY(window.grab().save(QDir(output).filePath("anime-expressions.png")));
            window.resize(760,650);QCoreApplication::processEvents();QTRY_VERIFY_WITH_TIMEOUT(exactPreviewReady(window),30000);QVERIFY(window.grab().save(QDir(output).filePath("anime-narrow.png")));
        }
        for(const auto &kind:QStringList{"dog","cat","bird"}){
            QTemporaryDir directory;Reference3DReviewWindow window(nullptr,directory.path());window.resize(1240,840);window.show();auto *browser=window.findChild<QTabWidget*>("referenceLibraryTabs");auto *properties=window.findChild<QTabWidget*>("referencePropertyTabs");window.findChild<QTabBar*>("referenceModelKinds")->setCurrentIndex(4);QVERIFY2(window.placeCatalogModel("pet/"+kind,&error),qPrintable(error));
            if(kind=="dog"){for(const auto &model:QStringList{"pet/dog","pet/cat","pet/bird"})window.requestPresetThumbnail(model);for(const auto &model:QStringList{"pet/dog","pet/cat","pet/bird"})QTRY_VERIFY_WITH_TIMEOUT(!window.presetThumbnail(model).isNull(),20000);QTRY_VERIFY_WITH_TIMEOUT(exactPreviewReady(window),15000);QVERIFY(window.grab().save(QDir(output).filePath("pets-model-browser.png")));}
            browser->setCurrentIndex(1);properties->setCurrentIndex(1);window.findChild<QTabBar*>("referencePoseKinds")->setCurrentIndex(2);const auto pose="pet/"+kind+(kind=="bird"?"/flying-up":"/walking-left");auto *gallery=window.findChild<QListWidget*>("referencePosePresets");for(int row=0;row<gallery->count();++row)if(gallery->item(row)->data(Qt::UserRole).toString()==pose)gallery->setCurrentRow(row);QVERIFY2(window.applyPosePreset(pose,&error),qPrintable(error));for(const auto &entry:referencePetPosePresets(window.scene().instances.first()))window.requestPresetThumbnail(entry.id,true);for(const auto &entry:referencePetPosePresets(window.scene().instances.first()))QTRY_VERIFY_WITH_TIMEOUT(!window.presetThumbnail(entry.id,true).isNull(),20000);button(window,"referenceFit")->click();QTRY_VERIFY_WITH_TIMEOUT(exactPreviewReady(window),15000);QVERIFY(window.grab().save(QDir(output).filePath("pet-"+kind+"-poses.png")));
        }
    }
};
QTEST_MAIN(Reference3DReviewTests)
#include "test_reference3d_review.moc"
