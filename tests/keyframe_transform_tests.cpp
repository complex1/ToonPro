#include "canvas.h"
#include "document.h"
#include "keyframe_transform_tool.h"
#include "property_animation.h"
#include "sheet.h"
#include "symbols.h"
#include "timeline_controller.h"
#include "ui_theme.h"
#include <QMouseEvent>
#include <QPainter>
#include <QtTest>
#include <cmath>
using namespace toon;
namespace {
Project fixture(bool grouped=false){
    auto project=Project::create();project.width=256;project.height=192;project.transparent=true;project.shots[0].durationFrames=30;project.shots[0].sheet.overscan=0;
    auto &layers=project.shots[0].layers;layers.resize(1);layers[0].name="Artwork";
    QImage tile(tileSize,tileSize,QImage::Format_ARGB32_Premultiplied);tile.fill(Qt::transparent);QPainter painter(&tile);painter.fillRect(QRect(32,24,64,48),Qt::red);painter.end();layers[0].tiles.insert({0,0},tile);
    if(grouped){auto parent=RasterLayer::create("Animated parent");parent.group=true;layers[0].parentId=parent.id;layers.prepend(parent);}return project;
}
void show(Canvas &canvas){canvas.resize(900,720);canvas.show();canvas.fitSheet();QCoreApplication::processEvents();}
QTransform artworkView(const Canvas &canvas){auto transform=canvas.viewTransform();const auto origin=sheetGeometry(canvas.project(),canvas.shot()).output.topLeft();transform.translate(origin.x(),origin.y());return transform;}
void pointer(Canvas &canvas,QEvent::Type type,QPointF point,Qt::KeyboardModifiers modifiers=Qt::NoModifier){
    const auto button=type==QEvent::MouseMove?Qt::NoButton:Qt::LeftButton;
    const auto buttons=type==QEvent::MouseButtonRelease?Qt::NoButton:Qt::LeftButton;
    QMouseEvent event(type,point,canvas.mapToGlobal(point.toPoint()),button,buttons,modifiers);QApplication::sendEvent(&canvas,&event);
}
bool close(qreal a,qreal b,qreal epsilon=.001){return std::abs(a-b)<epsilon;}
}
class KeyframeTransformTests final:public QObject {
    Q_OBJECT
private slots:
    void initTestCase(){applyTheme();}
    void pivotUsesPaintedContentInsteadOfWholeTile(){
        for(bool grouped:{false,true}){const auto project=fixture(grouped);const auto &shot=project.shots[0];const auto id=shot.layers.first().id;
            QCOMPARE(defaultAnimationPivot(shot,id,QSizeF(project.width,project.height)),QPointF(64,48));
            Document document;document.load(project);QVERIFY(document.enableKeyframeChannel(id,KeyframeProperty::Rotation));QCOMPARE(layerAnimation(document.project().shots[0],id)->pivot,QPointF(64,48));}
    }
    void initialSymbolCanvasTransform_data(){
        QTest::addColumn<bool>("rotate");
        QTest::newRow("rotate")<<true;QTest::newRow("scale")<<false;
    }
    void initialSymbolCanvasTransform(){
        QFETCH(bool,rotate);
        Canvas canvas;canvas.setProject(fixture());TimelineController timeline(&canvas);auto *document=canvas.document();QString error;
        const auto definition=convertLayersToSymbol(document,{canvas.shot().layers[0].id},"Prop",&error);
        QVERIFY2(!definition.isEmpty(),qPrintable(error));const auto id=canvas.shot().layers[0].id;
        show(canvas);timeline.editAnimatedTransform();auto *tool=canvas.keyframeTransformTool();QVERIFY(tool->enabled());
        const QPointF center(64,48);QVERIFY(QLineF(canvas.artworkPoint(tool->pivotHandle()),center).length()<1e-8);
        const auto start=rotate?tool->rotationHandle():tool->box()[2];
        const auto arm=canvas.artworkPoint(start)-center;QTransform change;if(rotate)change.rotate(90);else change.scale(1.5,1.5);
        const auto end=artworkView(canvas).map(center+change.map(arm));const int count=document->history()->count();
        pointer(canvas,QEvent::MouseButtonPress,start);QVERIFY(tool->dragging());
        pointer(canvas,QEvent::MouseMove,end);pointer(canvas,QEvent::MouseButtonRelease,end);
        QVERIFY(!tool->dragging());QCOMPARE(document->history()->count(),count+1);
        const auto *animation=layerAnimation(canvas.shot(),id);QVERIFY(animation);QCOMPARE(animation->pivot,center);
        QVERIFY(close(evaluateProperty(canvas.shot(),id,rotate?KeyframeProperty::Rotation:KeyframeProperty::ScaleX,0),rotate?90.:1.5));
        QVERIFY(QLineF(evaluateLayerTransform(canvas.shot(),id,0).map(center),center).length()<1e-8);
        document->history()->undo();QVERIFY(!layerAnimation(canvas.shot(),id));
        QVERIFY(QLineF(canvas.artworkPoint(tool->pivotHandle()),center).length()<1e-8);
    }
    void canvasScaleHandles_data(){
        QTest::addColumn<bool>("parentAnimated");QTest::addColumn<bool>("proportional");
        QTest::newRow("layer")<<false<<false;QTest::newRow("layer-shift")<<false<<true;
        QTest::newRow("animated-parent")<<true<<false;QTest::newRow("animated-parent-shift")<<true<<true;
    }
    void canvasScaleHandles(){
        QFETCH(bool,parentAnimated);QFETCH(bool,proportional);
        Canvas canvas;canvas.setProject(fixture(parentAnimated));TimelineController timeline(&canvas);auto *document=canvas.document();const auto id=canvas.shot().layers.last().id;
        QString parent;if(parentAnimated){parent=canvas.shot().layers.first().id;QVERIFY(document->setCurrentFrame(20));QVERIFY(document->setKeyframeValue(parent,KeyframeProperty::PositionX,26));QVERIFY(document->setKeyframeValue(parent,KeyframeProperty::PositionY,18));QVERIFY(document->setKeyframeValue(parent,KeyframeProperty::ScaleX,1.8));QVERIFY(document->setKeyframeValue(parent,KeyframeProperty::ScaleY,.6));QVERIFY(document->setKeyframeValue(parent,KeyframeProperty::Rotation,52));}
        QVERIFY(timeline.selectPropertyLayer(id));QVERIFY(timeline.setFrame(0));QVERIFY(document->setKeyframeValue(id,KeyframeProperty::ScaleX,1.2));QVERIFY(document->setKeyframeValue(id,KeyframeProperty::ScaleY,.75));QVERIFY(document->setKeyframeValue(id,KeyframeProperty::Rotation,25));QVERIFY(document->setKeyframeValue(id,KeyframeProperty::PositionX,6));QVERIFY(document->setKeyframeValue(id,KeyframeProperty::PositionY,8));QVERIFY(timeline.setFrame(10));show(canvas);timeline.editAnimatedTransform();auto *tool=canvas.keyframeTransformTool();QVERIFY(tool->enabled());
        const auto source=canvas.shot().layers.last().tiles;const auto beforeParent=parentAnimated?*layerAnimation(canvas.shot(),parent):LayerAnimation{};const int history=document->history()->count();
        const auto pivot=layerAnimation(canvas.shot(),id)->pivot+QPointF(6,8);const QTransform parentTransform=parentAnimated?worldLayerTransform(canvas.shot(),parent,10):QTransform();
        const auto start=tool->box()[2];const auto startParent=parentTransform.inverted().map(canvas.artworkPoint(start));QTransform unrotate;unrotate.rotate(-25);const auto local=unrotate.map(startParent-pivot);QTransform rotate;rotate.rotate(25);
        const auto end=artworkView(canvas).map(parentTransform.map(pivot+rotate.map(QPointF(local.x()*1.5,local.y()*.6))));
        pointer(canvas,QEvent::MouseButtonPress,start);QVERIFY(tool->dragging());pointer(canvas,QEvent::MouseMove,end,proportional?Qt::ShiftModifier:Qt::NoModifier);
        QCOMPARE(document->history()->count(),history);QVERIFY(close(evaluateProperty(canvas.shot(),id,KeyframeProperty::ScaleX,10),1.8));
        QVERIFY(close(evaluateProperty(canvas.shot(),id,KeyframeProperty::ScaleY,10),proportional?1.125:.45));
        pointer(canvas,QEvent::MouseButtonRelease,end,proportional?Qt::ShiftModifier:Qt::NoModifier);QVERIFY(!tool->dragging());QCOMPARE(document->history()->count(),history+1);QCOMPARE(canvas.shot().layers.last().tiles,source);
        if(parentAnimated)QVERIFY(*layerAnimation(canvas.shot(),parent)==beforeParent);
        document->history()->undo();QVERIFY(close(evaluateProperty(canvas.shot(),id,KeyframeProperty::ScaleX,10),1.2));QVERIFY(close(evaluateProperty(canvas.shot(),id,KeyframeProperty::ScaleY,10),.75));
    }
    void rotationHandleUsesParentSpaceAndRemainsContinuous(){
        Canvas canvas;canvas.setProject(fixture(true));TimelineController timeline(&canvas);auto *document=canvas.document();const auto parent=canvas.shot().layers.first().id,id=canvas.shot().layers.last().id;
        QVERIFY(document->setCurrentFrame(20));QVERIFY(document->setKeyframeValue(parent,KeyframeProperty::ScaleX,2));QVERIFY(document->setKeyframeValue(parent,KeyframeProperty::ScaleY,.5));QVERIFY(document->setKeyframeValue(parent,KeyframeProperty::Rotation,70));
        QVERIFY(timeline.selectPropertyLayer(id));QVERIFY(timeline.setFrame(0));QVERIFY(document->setKeyframeValue(id,KeyframeProperty::Rotation,15));QVERIFY(timeline.setFrame(10));show(canvas);timeline.editAnimatedTransform();auto *tool=canvas.keyframeTransformTool();QVERIFY(tool->enabled());
        const auto center=layerAnimation(canvas.shot(),id)->pivot;const auto parentTransform=worldLayerTransform(canvas.shot(),parent,10);const auto start=tool->rotationHandle();const auto arm=parentTransform.inverted().map(canvas.artworkPoint(start))-center;
        const int history=document->history()->count();const auto source=canvas.shot().layers.last().tiles;pointer(canvas,QEvent::MouseButtonPress,start);QVERIFY(tool->dragging());QPointF end;
        for(qreal angle:{45.,90.,135.,180.,225.,270.,315.,359.}){QTransform turn;turn.rotate(angle);end=artworkView(canvas).map(parentTransform.map(center+turn.map(arm)));pointer(canvas,QEvent::MouseMove,end);
            QVERIFY2(close(evaluateProperty(canvas.shot(),id,KeyframeProperty::Rotation,10),15+angle),qPrintable(QString("Rotation expected %1, got %2").arg(15+angle).arg(evaluateProperty(canvas.shot(),id,KeyframeProperty::Rotation,10))));QCOMPARE(document->history()->count(),history);}
        pointer(canvas,QEvent::MouseButtonRelease,end);QCOMPARE(document->history()->count(),history+1);QCOMPARE(canvas.shot().layers.last().tiles,source);document->history()->undo();QCOMPARE(evaluateProperty(canvas.shot(),id,KeyframeProperty::Rotation,10),15.);
    }
    void returningToStartCancelsNoChangeTransform_data(){QTest::addColumn<int>("mode");QTest::newRow("move")<<0;QTest::newRow("scale")<<1;QTest::newRow("rotate")<<2;}
    void returningToStartCancelsNoChangeTransform(){
        QFETCH(int,mode);Canvas canvas;canvas.setProject(fixture());TimelineController timeline(&canvas);QVERIFY(timeline.setFrame(10));show(canvas);timeline.editAnimatedTransform();auto *tool=canvas.keyframeTransformTool();const auto box=tool->box();
        const auto start=mode==0?(box[0]+box[2])/2:mode==1?box[2]:tool->rotationHandle();const auto before=canvas.project().toJson();const int count=canvas.document()->history()->count();
        pointer(canvas,QEvent::MouseButtonPress,start);QVERIFY(tool->dragging());pointer(canvas,QEvent::MouseMove,start+QPointF(45,25));QVERIFY(canvas.project().toJson()!=before);
        pointer(canvas,QEvent::MouseMove,start);pointer(canvas,QEvent::MouseButtonRelease,start);QVERIFY(!tool->dragging());QCOMPARE(canvas.project().toJson(),before);QCOMPARE(canvas.document()->history()->count(),count);
    }
};
QTEST_MAIN(KeyframeTransformTests)
#include "keyframe_transform_tests.moc"
