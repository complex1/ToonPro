#include "reference3d_canvas.h"
#include "reference3d_builtin.h"
#include "reference3d_catalog.h"
#include "reference3d_layer_serialization.h"
#include "reference3d_render.h"
#include "document.h"
#include "property_animation.h"
#include "ui_theme.h"
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QSignalSpy>
#include <QTimer>
#include <QWidget>
#include <QtTest>

using namespace toon;
namespace {
constexpr QSize frame{400,400};
ReferenceScene figure(){ReferenceScene s;s.name="Drawing reference";ReferenceInstance i;i.id="drawing-model";i.name="Figure";i.asset=createReferenceStarter(ReferenceStarterKind::Figure);s.instances={i};s.camera.target={0,.9f,0};s.camera.yaw=0;s.camera.pitch=0;s.camera.distance=4;s.camera.orthographic=true;s.camera.orthoScale=2.5f;return s;}
Project drawing(){auto p=Project::create(ProjectKind::DrawingOnly);p.width=frame.width();p.height=frame.height();return p;}
QPointF projected(const ReferenceScene&s,QVector3D world,QTransform art={}){const auto p=referenceViewProjection(s.camera,frame)*QVector4D(world,1);return art.map(QPointF((p.x()/p.w()+1)*frame.width()*.5,(1-p.y()/p.w())*frame.height()*.5));}
void press(Reference3DCanvasController&c,QPointF p,Qt::MouseButton b=Qt::LeftButton,QTransform art={}){QMouseEvent e(QEvent::MouseButtonPress,p,p,b,b,Qt::NoModifier);QVERIFY(c.mousePress(&e,art,frame));}
void move(Reference3DCanvasController&c,QPointF p,QTransform art={}){QMouseEvent e(QEvent::MouseMove,p,p,Qt::NoButton,Qt::LeftButton,Qt::NoModifier);QVERIFY(c.mouseMove(&e,art,frame));}
void release(Reference3DCanvasController&c,QPointF p,QTransform art={}){QMouseEvent e(QEvent::MouseButtonRelease,p,p,Qt::LeftButton,Qt::NoButton,Qt::NoModifier);QVERIFY(c.mouseRelease(&e,art,frame));}
float displacement(const ReferenceEvaluation&a,const ReferenceEvaluation&b){float value=0;for(int p=0;p<a.primitives.size();++p)for(int v=0;v<a.primitives[p].vertices.size();++v)value=qMax(value,(a.primitives[p].vertices[v].position-b.primitives[p].vertices[v].position).length());return value;}
}
class Reference3DCanvasTests final:public QObject{
    Q_OBJECT
private slots:
    void initTestCase(){applyTheme();}
    void objectTranslationIsLiveOneUndoAndEscapeRestoresExactSource(){
        Document doc;doc.load(drawing());const auto id=doc.addReference3DLayer(figure());QVERIFY(!id.isEmpty());QWidget widget;Reference3DCanvasController controller(&doc,&widget);QVERIFY(controller.available());
        const auto before=*controller.scene();const auto at=projected(before,{0,1.05f,0});const int history=doc.history()->count();press(controller,at);QVERIFY(controller.interactionActive());
        for(int i=1;i<=20;++i)move(controller,at+QPointF(i,0));QCOMPARE(doc.history()->count(),history);QVERIFY(controller.scene()->instances[0].translation.x()>.1f);release(controller,at+QPointF(20,0));
        QVERIFY(!controller.interactionActive());QCOMPARE(doc.history()->count(),history+1);const auto after=*controller.scene();QCOMPARE(after.instances[0].asset.get(),before.instances[0].asset.get());
        doc.history()->undo();QVERIFY(referenceLayerScenesEqual(*controller.scene(),before));doc.history()->redo();QVERIFY(referenceLayerScenesEqual(*controller.scene(),after));
        const auto moved=projected(after,after.instances[0].translation+QVector3D(0,1.05f,0));press(controller,moved);move(controller,moved+QPointF(20,15));QKeyEvent escape(QEvent::KeyPress,Qt::Key_Escape,Qt::NoModifier);QVERIFY(controller.keyPress(&escape));QVERIFY(!controller.interactionActive());QCOMPARE(doc.history()->count(),history+1);QVERIFY(referenceLayerScenesEqual(*controller.scene(),after));
    }
    void jointDragChangesNativeSkinAndPreservesDrawingPixels(){
        auto project=drawing();QImage ink(tileSize,tileSize,QImage::Format_ARGB32_Premultiplied);ink.fill(QColor(10,20,30));project.shots[0].layers[0].tiles.insert({0,0},ink);
        Document doc;doc.load(project);QVERIFY(!doc.addReference3DLayer(figure()).isEmpty());QWidget widget;Reference3DCanvasController c(&doc,&widget);c.setMode(Reference3DCanvasMode::Pose);c.setAxis(2);
        const auto before=*c.scene();const auto evaluated=evaluateReferenceInstance(before.instances[0]);QVERIFY(evaluated);const int joint=before.instances[0].asset->humanoidBones.value("leftLowerArm",-1);QVERIFY(joint>=0);c.selectJoint(joint);
        const auto globals=evaluateReferenceNodeGlobals(before.instances[0]);QVERIFY(globals);const auto point=projected(before,(*globals)[joint].map(QVector3D()));const int history=doc.history()->count();press(c,point);QCOMPARE(c.selectedJoint(),joint);move(c,point+QPointF(45,-5));release(c,point+QPointF(45,-5));
        QCOMPARE(doc.history()->count(),history+1);const auto posed=evaluateReferenceInstance(c.scene()->instances[0]);QVERIFY(posed);QVERIFY(displacement(*evaluated,*posed)>.03f);QCOMPARE(doc.project().shots[0].layers[0].tiles.value({0,0}),ink);
        doc.history()->undo();QVERIFY(referenceLayerScenesEqual(*c.scene(),before));doc.history()->redo();QVERIFY(c.scene()->instances[0].poseRotations.contains(joint));
    }
    void rotationScaleAndCameraNavigationCommitIndependentGestures(){
        Document doc;doc.load(drawing());QVERIFY(!doc.addReference3DLayer(figure()).isEmpty());QWidget widget;Reference3DCanvasController c(&doc,&widget);const auto origin=*c.scene();const auto point=projected(origin,{0,1.05f,0});
        c.setMode(Reference3DCanvasMode::Rotate);c.setAxis(1);press(c,point);move(c,point+QPointF(30,0));release(c,point+QPointF(30,0));QVERIFY(c.scene()->instances[0].rotation!=origin.instances[0].rotation);
        c.setMode(Reference3DCanvasMode::Scale);c.setAxis(-1);const int history=doc.history()->count();press(c,point);move(c,point+QPointF(22,-5));release(c,point+QPointF(22,-5));QCOMPARE(doc.history()->count(),history+1);QVERIFY(c.scene()->instances[0].scale.x()>1.1f);QCOMPARE(c.scene()->instances[0].scale.x(),c.scene()->instances[0].scale.y());
        const auto beforeCamera=*c.scene();press(c,{30,30},Qt::RightButton);move(c,{65,50});release(c,{65,50});QVERIFY(c.scene()->camera.yaw!=beforeCamera.camera.yaw);QCOMPARE(c.scene()->instances[0].rotation,beforeCamera.instances[0].rotation);
    }
    void actualPosePresetsAndDrawingLayerSelectionAreIndependent(){
        Document doc;doc.load(drawing());QVERIFY(!doc.addReference3DLayer(figure()).isEmpty());QWidget widget;Reference3DCanvasController c(&doc,&widget);QString error;
        const auto before=*c.scene();QVERIFY2(c.applyPosePreset("body/relaxed-standing",&error),qPrintable(error));QVERIFY(!referenceLayerScenesEqual(*c.scene(),before));const auto posed=*c.scene();
        doc.addLayer();QVERIFY(!c.available());QVERIFY(!c.scene());QImage overlay(frame,QImage::Format_ARGB32_Premultiplied);overlay.fill(Qt::transparent);{QPainter p(&overlay);c.paintOverlay(p,{},frame);}QImage empty=overlay;empty.fill(Qt::transparent);QCOMPARE(overlay,empty);
        QMouseEvent mouse(QEvent::MouseButtonPress,QPointF(200,200),QPointF(200,200),Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);QVERIFY(!c.mousePress(&mouse,{},frame));
        doc.selectLayer(1);QVERIFY(c.available());QVERIFY(referenceLayerScenesEqual(*c.scene(),posed));
    }
    void lockedHiddenAndTransformedLayersRespectCanvasCoordinates(){
        Document doc;doc.load(drawing());const auto id=doc.addReference3DLayer(figure());QVERIFY(!id.isEmpty());QWidget widget;Reference3DCanvasController c(&doc,&widget);
        doc.setLayersLocked({id},true);QVERIFY(c.available());QVERIFY(!c.editable());const auto before=*c.scene();const auto point=projected(before,{0,1.05f,0});press(c,point);QVERIFY(!c.interactionActive());QVERIFY(referenceLayerScenesEqual(*c.scene(),before));doc.setLayersLocked({id},false);
        doc.setLayersVisible({id},false);QVERIFY(!c.available());doc.setLayersVisible({id},true);QVERIFY(c.available());
        auto project=doc.project();project.shots[0].layerAnimations.append({id,{},{{KeyframeProperty::PositionX,{{0,35}}},{KeyframeProperty::PositionY,{{0,22}}}}, {}});doc.load(project);doc.selectLayer(1);
        const QTransform art=QTransform().translate(40,30).scale(1.3,1.3);const auto translated=art.map(point+QPointF(35,22));press(c,translated,Qt::LeftButton,art);QVERIFY(c.interactionActive());move(c,translated+QPointF(26,0),art);release(c,translated+QPointF(26,0),art);QVERIFY(c.scene()->instances[0].translation.x()>.1f);
    }
    void asynchronousTransparentPreviewsSurviveDrawingAboveAndWindowDestruction(){
        Document doc;doc.load(drawing());const auto id=doc.addReference3DLayer(figure());QVERIFY(!id.isEmpty());QWidget widget;Reference3DCanvasController c(&doc,&widget);c.prepareLayerPreviews(frame);
        QTRY_VERIFY_WITH_TIMEOUT(c.previewReady(),15000);QVERIFY2(c.previewError().isEmpty(),qPrintable(c.previewError()));const auto rendered=c.preparedLayerPreviews().value(id);QVERIFY(!rendered.isNull());QCOMPARE(rendered.pixelColor(0,0).alpha(),0);
        doc.addLayer();c.prepareLayerPreviews(frame);QVERIFY(!c.available());QCOMPARE(c.preparedLayerPreviews().value(id).cacheKey(),rendered.cacheKey());QVERIFY(c.previewReady());
        auto *transient=new Reference3DCanvasController(&doc,&widget);transient->prepareLayerPreviews({1600,1200});delete transient;bool alive=false;QTimer::singleShot(30,&widget,[&]{alive=true;});QTRY_VERIFY(alive);
    }
    void sourceEditingUsesTheUnanimatedLayerCoordinates(){
        Document doc;doc.load(drawing());const auto id=doc.addReference3DLayer(figure());QVERIFY(!id.isEmpty());auto project=doc.project();
        project.shots[0].layerAnimations.append({id,{},{{KeyframeProperty::PositionX,{{0,130}}},{KeyframeProperty::PositionY,{{0,85}}}}, {}});doc.load(project);doc.selectLayer(1);doc.setKeyframeSourceEditing(true);QVERIFY(doc.keyframeSourceEditing());
        QWidget widget;Reference3DCanvasController c(&doc,&widget);const auto point=projected(*c.scene(),{0,1.05f,0});
        press(c,point);QVERIFY(c.interactionActive());move(c,point+QPointF(20,0));release(c,point+QPointF(20,0));QVERIFY(c.scene()->instances[0].translation.x()>.1f);
    }
    void viewVisibilityFilterSuppressesHandlesInputAndBackgroundWork(){
        Document doc;doc.load(drawing());const auto id=doc.addReference3DLayer(figure());QVERIFY(!id.isEmpty());QWidget widget;Reference3DCanvasController c(&doc,&widget);
        c.prepareLayerPreviews(frame);QTRY_VERIFY_WITH_TIMEOUT(c.previewReady(),15000);QVERIFY(!c.preparedLayerPreviews().value(id).isNull());
        const auto before=*c.scene();const auto point=projected(before,{0,1.05f,0});press(c,point);move(c,point+QPointF(20,0));QVERIFY(c.interactionActive());
        c.setLayerVisibilityFilter([](const QString&){return false;});QVERIFY(!c.available());QVERIFY(!c.interactionActive());QVERIFY(referenceLayerScenesEqual(*c.scene(),before));QTRY_VERIFY_WITH_TIMEOUT(c.previewReady(),15000);QVERIFY(!c.preparedLayerPreviews().contains(id));
        QMouseEvent mouse(QEvent::MouseButtonPress,point,point,Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);QVERIFY(!c.mousePress(&mouse,{},frame));
        c.setLayerVisibilityFilter({});QVERIFY(c.available());QTRY_VERIFY_WITH_TIMEOUT(c.previewReady(),15000);QVERIFY(!c.preparedLayerPreviews().value(id).isNull());
    }
};
QTEST_MAIN(Reference3DCanvasTests)
#include "test_reference3d_canvas.moc"
