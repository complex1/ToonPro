#include "brush_engine.h"
#include "canvas.h"
#include "drawing_tool_controller.h"
#include "object_tools.h"
#include "sheet.h"
#include "vector_edit_geometry.h"
#include <QApplication>
#include <QMouseEvent>
#include <QSignalSpy>
#include <QFocusEvent>
#include <QTemporaryDir>
#include <QDir>
#include <QPointingDevice>
#include <QTabletEvent>
#include <QtTest>
#include <algorithm>
#include <cmath>

using namespace toon;
namespace {
EditableObject source(SymmetryMode mode) {
    auto object=EditableObject::create();object.strokeWidth=10;
    for(int i=0;i<=30;++i)object.nodes.append(BezierNode::corner({80.+i*5,100.+(i%2)*.3}));
    object.brushStroke=VectorBrushStroke();object.brushStroke->canvasSize={640,480};
    auto &symmetry=object.brushStroke->settings.symmetry;
    symmetry.mode=mode;symmetry.count=4;symmetry.angle=11;symmetry.radius=100;symmetry.distance=70;
    object.transform.translate(15,20);object.transform.rotate(7);object.transform.scale(1.1,.85);
    return object;
}
QPointF copyPoint(const EditableObject &object,QPointF local,int copy=1) {
    const auto &stroke=*object.brushStroke;
    return object.transform.map(brushSymmetryInstances(stroke.settings.symmetry,local,stroke.canvasSize)[copy].point);
}
QPainterPath circle(QPointF point,qreal radius){QPainterPath path;path.addEllipse(point,radius,radius);return path;}
void modes() {
    QTest::addColumn<int>("mode");
    for(const auto &[name,mode]:{
            std::pair{"vertical",SymmetryMode::Vertical},std::pair{"horizontal",SymmetryMode::Horizontal},
            std::pair{"both-axes",SymmetryMode::DualAxis},std::pair{"diagonal",SymmetryMode::Diagonal},
            std::pair{"radial",SymmetryMode::Radial},std::pair{"parallel",SymmetryMode::ParallelLines},
            std::pair{"wave",SymmetryMode::Wave},std::pair{"circle",SymmetryMode::Circle},
            std::pair{"spiral",SymmetryMode::Spiral}})QTest::newRow(name)<<int(mode);
}
const RasterLayer &active(Canvas &canvas){return canvas.shot().layers[canvas.document()->activeLayer()];}
void setup(Canvas &canvas,SymmetryMode mode) {
    auto project=Project::create();project.width=640;project.height=480;project.transparent=true;
    project.shots[0].sheet.overscan=0;project.shots[0].layers[0].kind=LayerKind::Vector;
    canvas.resize(1000,800);canvas.setProject(project);canvas.show();canvas.setZoom(1);QApplication::processEvents();
    BrushSettings brush;brush.size=8;brush.spacing=.15;brush.pressureSize=false;brush.symmetry.mode=mode;brush.symmetry.count=4;
    auto *document=canvas.document();
    QVERIFY(document->beginStroke({80,100},1,brush,false));
    for(int i=1;i<=30;++i)document->continueStroke({80.+i*5,100.+(i%2)*.3},1);
    document->endStroke();QCOMPARE(active(canvas).objects.size(),1);
    auto object=active(canvas).objects.first();object.transform.translate(15,10);object.transform.rotate(5);object.transform.scale(.95,.9);
    QVERIFY(ObjectTools::updateObject(document,object));
}
void mouse(Canvas &canvas,QEvent::Type type,QPointF artwork) {
    const auto view=canvas.artworkViewTransform().map(artwork);
    QMouseEvent event(type,view,view,canvas.mapToGlobal(view),type==QEvent::MouseMove?Qt::NoButton:Qt::LeftButton,
        type==QEvent::MouseButtonRelease?Qt::NoButton:Qt::LeftButton,Qt::NoModifier);
    QApplication::sendEvent(&canvas,&event);
}
void click(Canvas &canvas,QPointF point){mouse(canvas,QEvent::MouseButtonPress,point);mouse(canvas,QEvent::MouseButtonRelease,point);}
}

class VectorCorrectionTest:public QObject {
    Q_OBJECT
private slots:
    void nudgeLocallySubdividesCurvesAndKeepsIdentityAndWidth(){
        auto object=EditableObject::create();object.nodes={{{0,100},{0,100},{100,75}},{{600,100},{500,125},{600,100}}};object.widthFactors={.5,1.5};object.strokeWidth=7;
        QString error;auto result=nudgedVectorPath(object,{300,100},{0,-30},35,.8,{},&error);QVERIFY2(result,qPrintable(error));
        QCOMPARE(result->id,object.id);QCOMPARE(result->transform,object.transform);QCOMPARE(result->strokeWidth,object.strokeWidth);QVERIFY(result->nodes.size()>object.nodes.size());
        QCOMPARE(result->nodes.first().anchor,object.nodes.first().anchor);QCOMPARE(result->nodes.last().anchor,object.nodes.last().anchor);QCOMPARE(result->widthFactors.first(),.5);QCOMPARE(result->widthFactors.last(),1.5);
        const auto path=editableObjectPath(*result);QVERIFY(path.boundingRect().top()<80);QCOMPARE(nudgedVectorPath(object,{300,100},{},35,.8).value(),object);
        QCOMPARE(nudgedVectorPath(object,{900,900},{20,20},35,.8).value(),object);QVERIFY(!nudgedVectorPath(object,{300,100},{0,20},0,.8));
    }
    void nudgeSymmetryCopyRetainsRecordedBrush_data(){modes();}
    void nudgeSymmetryCopyRetainsRecordedBrush(){
        QFETCH(int,mode);Canvas canvas;setup(canvas,SymmetryMode(mode));const auto object=active(canvas).objects.first();
        const auto local=object.nodes[object.nodes.size()/2].anchor;const auto center=copyPoint(object,local);QString error;
        const QPointF push(0,SymmetryMode(mode)==SymmetryMode::Wave?8:-8);
        const auto result=nudgedVectorPath(object,center,push,22,.75,{},&error);QVERIFY2(result,qPrintable(error));QVERIFY(result->nodes!=object.nodes);QCOMPARE(result->brushStroke,object.brushStroke);QCOMPARE(result->id,object.id);
        qreal closest=100000;for(const auto&node:result->nodes)closest=qMin(closest,QLineF(copyPoint(*result,node.anchor),center+push*.75).length());QVERIFY2(closest<.05,qPrintable(QString::number(closest)));
    }
    void nudgeRefusesFoldedWaveCopyAtomically(){
        Canvas canvas;setup(canvas,SymmetryMode::Wave);const auto object=active(canvas).objects.first();const auto center=copyPoint(object,object.nodes[object.nodes.size()/2].anchor);QString error;
        QVERIFY(!nudgedVectorPath(object,center,{0,-8},22,.75,{},&error));QVERIFY(error.contains("cannot follow"));
        const auto before=canvas.project().toJson();const auto history=canvas.document()->history()->count();QSignalSpy warnings(canvas.document(),&Document::warning);canvas.setTool(Canvas::Tool::VectorNudge);canvas.drawingTools()->nudgeRadius=22;canvas.drawingTools()->nudgeStrength=.75;
        mouse(canvas,QEvent::MouseButtonPress,center);mouse(canvas,QEvent::MouseMove,center+QPointF(0,-8));mouse(canvas,QEvent::MouseButtonRelease,center+QPointF(0,-8));QCOMPARE(canvas.project().toJson(),before);QCOMPARE(canvas.document()->history()->count(),history);QVERIFY(!canvas.document()->externalEditActive());QVERIFY(!canvas.drawingTools()->active());QVERIFY(!warnings.isEmpty());
    }
    void nudgeGestureIsIndependentOfMoveCountAndViewTransform(){
        Canvas canvas;setup(canvas,SymmetryMode::Off);const auto originalProject=canvas.project();const auto original=active(canvas).objects.first();const auto center=original.transform.map(QPointF(155,100));
        QVector<EditableObject> sparse;
        for(int pass=0;pass<2;++pass){
            canvas.setProject(originalProject);canvas.setTool(Canvas::Tool::VectorNudge);canvas.drawingTools()->nudgeRadius=35;canvas.drawingTools()->nudgeStrength=.8;
            canvas.setZoom(pass?.7:1);canvas.setRotation(pass?27:0);canvas.setCanvasMirrored(pass);auto*doc=canvas.document();const auto before=canvas.project().toJson();
            mouse(canvas,QEvent::MouseButtonPress,center);for(int i=1;i<=(pass?24:1);++i)mouse(canvas,QEvent::MouseMove,center+QPointF(0,-20)*qreal(i)/(pass?24:1));mouse(canvas,QEvent::MouseButtonRelease,center+QPointF(0,-20));
            QCOMPARE(doc->history()->count(),1);QCOMPARE(active(canvas).objects.first().brushStroke,original.brushStroke);QCOMPARE(active(canvas).objects.first().id,original.id);
            if(!pass)sparse=active(canvas).objects;else {const auto dense=active(canvas).objects;QCOMPARE(dense.size(),sparse.size());for(int i=0;i<dense.size();++i){QCOMPARE(dense[i].nodes.size(),sparse[i].nodes.size());for(int j=0;j<dense[i].nodes.size();++j){QVERIFY(QLineF(dense[i].nodes[j].anchor,sparse[i].nodes[j].anchor).length()<1e-7);QVERIFY(QLineF(dense[i].nodes[j].inControl,sparse[i].nodes[j].inControl).length()<1e-7);QVERIFY(QLineF(dense[i].nodes[j].outControl,sparse[i].nodes[j].outControl).length()<1e-7);}}}
            const auto after=canvas.project().toJson();doc->history()->undo();QCOMPARE(canvas.project().toJson(),before);doc->history()->redo();QCOMPARE(canvas.project().toJson(),after);
            QTemporaryDir dir;QString error;const auto path=dir.filePath("nudged.toon");QVERIFY2(saveProject(canvas.project(),path,&error),qPrintable(error));const auto reopened=loadProject(path,&error);QVERIFY2(reopened,qPrintable(error));QCOMPARE(reopened->toJson(),canvas.project().toJson());
            doc->history()->undo();mouse(canvas,QEvent::MouseButtonPress,center);mouse(canvas,QEvent::MouseMove,center+QPointF(15,0));QTest::keyClick(&canvas,Qt::Key_Escape);QCOMPARE(canvas.project().toJson(),before);QVERIFY(!doc->externalEditActive());
            mouse(canvas,QEvent::MouseButtonPress,center);mouse(canvas,QEvent::MouseMove,center+QPointF(15,0));QFocusEvent focus(QEvent::FocusOut);QApplication::sendEvent(&canvas,&focus);QCOMPARE(canvas.project().toJson(),before);QVERIFY(!doc->externalEditActive());
            doc->setLayerPositionLocked(true);QVERIFY(canvas.toolBlockReason().contains("Unlock"));
        }
    }
    void nudgeUsesArtworkPixelsWithLayerAnimationAndCamera(){
        Canvas canvas;setup(canvas,SymmetryMode::Off);auto p=canvas.project();const auto id=p.shots[0].layers[0].id;
        LayerAnimation layer;layer.layerId=id;layer.channels={{KeyframeProperty::ScaleX,{{0,1.7}}},{KeyframeProperty::ScaleY,{{0,.65}}},{KeyframeProperty::Rotation,{{0,16}}}};p.shots[0].layerAnimations={layer};p.shots[0].camera2D={{0,0},1.2,23};
        canvas.setProject(p);canvas.setZoom(.8);canvas.setRotation(-13);canvas.setCanvasMirrored(true);canvas.setTool(Canvas::Tool::VectorNudge);auto*c=canvas.drawingTools();c->nudgeRadius=28;c->nudgeStrength=.7;
        const auto source=active(canvas).objects.first();const auto world=worldLayerTransform(canvas.shot(),id,0);const auto center=world.map(source.transform.map(QPointF(155,100)));QString error;const auto expected=nudgedVectorPath(source,center,{0,-15},28,.7,world,&error);QVERIFY2(expected,qPrintable(error));
        QVERIFY(canvas.toolBlockReason().isEmpty());mouse(canvas,QEvent::MouseButtonPress,center);mouse(canvas,QEvent::MouseMove,center+QPointF(0,-15));mouse(canvas,QEvent::MouseButtonRelease,center+QPointF(0,-15));
        const auto actual=active(canvas).objects.first();QCOMPARE(actual.nodes.size(),expected->nodes.size());for(int i=0;i<actual.nodes.size();++i)QVERIFY(QLineF(actual.nodes[i].anchor,expected->nodes[i].anchor).length()<1e-6);QCOMPARE(canvas.shot().layerAnimations,p.shots[0].layerAnimations);
        auto animated=p;PathAnimation path;path.objectId=source.id;path.keys={{0,pathPose(source)}};animated.shots[0].layerAnimations[0].paths={path};canvas.setProject(animated);canvas.setTool(Canvas::Tool::VectorNudge);QVERIFY(canvas.toolBlockReason().contains("Disable path animation"));
        canvas.document()->setKeyframeSourceEditing(true);QVERIFY(canvas.toolBlockReason().contains("Disable path animation"));
        auto collapsed=p;collapsed.shots[0].layerAnimations[0].channels[0].keys[0].value=0;canvas.setProject(collapsed);canvas.setTool(Canvas::Tool::VectorNudge);canvas.document()->setKeyframeSourceEditing(false);QVERIFY(canvas.toolBlockReason().contains("nonzero layer scale"));
        canvas.document()->setKeyframeSourceEditing(true);QVERIFY(canvas.toolBlockReason().isEmpty());const auto sourcePoint=source.transform.map(QPointF(155,100));mouse(canvas,QEvent::MouseButtonPress,sourcePoint);mouse(canvas,QEvent::MouseMove,sourcePoint+QPointF(0,-10));mouse(canvas,QEvent::MouseButtonRelease,sourcePoint+QPointF(0,-10));QCOMPARE(canvas.document()->history()->count(),1);QVERIFY(active(canvas).objects.first().nodes!=source.nodes);QCOMPARE(canvas.shot().layerAnimations,collapsed.shots[0].layerAnimations);
    }
    void nudgePenReleaseKeepsPressureAndLostContactCancels(){
        Canvas canvas;setup(canvas,SymmetryMode::Off);const auto project=canvas.project();const auto object=active(canvas).objects.first();const auto center=object.transform.map(QPointF(155,100));
        canvas.setTool(Canvas::Tool::VectorNudge);canvas.drawingTools()->nudgeRadius=30;canvas.drawingTools()->nudgeStrength=.8;const int history=canvas.document()->history()->count();
        QPointingDevice device("Nudge test pen",289,QInputDevice::DeviceType::Stylus,QPointingDevice::PointerType::Pen,QInputDevice::Capability::Position|QInputDevice::Capability::Pressure,1,3);
        const auto send=[&](QEvent::Type type,QPointF point,qreal pressure,Qt::MouseButtons buttons){const auto view=canvas.artworkViewTransform().map(point);QTabletEvent event(type,&device,view,canvas.mapToGlobal(view),pressure,0,0,0,0,0,Qt::NoModifier,type==QEvent::TabletMove?Qt::NoButton:Qt::LeftButton,buttons);QApplication::sendEvent(&canvas,&event);};
        send(QEvent::TabletPress,center,.5,Qt::LeftButton);send(QEvent::TabletMove,center+QPointF(0,-20),.5,Qt::LeftButton);QVERIFY(canvas.document()->externalEditActive());const auto preview=active(canvas).objects;
        send(QEvent::TabletRelease,center+QPointF(0,-20),0,Qt::NoButton);QCOMPARE(active(canvas).objects,preview);QCOMPARE(canvas.document()->history()->count(),history+1);QVERIFY(!canvas.document()->externalEditActive());
        canvas.setProject(project);canvas.setTool(Canvas::Tool::VectorNudge);send(QEvent::TabletPress,center,.5,Qt::LeftButton);send(QEvent::TabletMove,center+QPointF(0,-20),.5,Qt::LeftButton);send(QEvent::TabletMove,center+QPointF(30,0),0,Qt::NoButton);send(QEvent::TabletRelease,center+QPointF(30,0),0,Qt::NoButton);
        QCOMPARE(canvas.project().toJson(),project.toJson());QCOMPARE(canvas.document()->history()->count(),0);QVERIFY(!canvas.document()->externalEditActive());
    }
    void nudgeTileRefusalRollsBackEarlierValidPreview(){
        auto p=Project::create();p.width=640;p.height=480;p.transparent=true;p.shots[0].sheet.overscan=0;p.shots[0].layers[0].kind=LayerKind::Vector;
        Document source;source.load(p);QVERIFY(!ObjectTools::addPath(&source,{BezierNode::corner({200,100}),BezierNode::corner({250,100})},false,Qt::black,4).isEmpty());p=source.project();QCOMPARE(p.shots[0].layers[0].tiles.size(),1);
        auto budget=RasterLayer::create("Budget fixture");budget.visible=false;QImage shared(tileSize,tileSize,QImage::Format_ARGB32_Premultiplied);shared.fill(Qt::black);for(int i=0;i<maxProjectTiles-1;++i)budget.tiles.insert({50+i%32,50+i/32},shared);p.shots[0].layers.append(budget);const auto validation=p.validate();QVERIFY2(validation.isEmpty(),qPrintable(validation));
        Canvas canvas;canvas.resize(1000,800);canvas.setProject(p);canvas.show();canvas.setZoom(1);canvas.setTool(Canvas::Tool::VectorNudge);canvas.drawingTools()->nudgeRadius=30;canvas.drawingTools()->nudgeStrength=.8;const auto before=canvas.project().toJson();QSignalSpy warnings(canvas.document(),&Document::warning);
        mouse(canvas,QEvent::MouseButtonPress,{230,100});mouse(canvas,QEvent::MouseMove,{230,92});QVERIFY(canvas.document()->externalEditActive());QVERIFY(canvas.project().toJson()!=before);
        mouse(canvas,QEvent::MouseMove,{280,92});QVERIFY(!canvas.document()->externalEditActive());QVERIFY(!canvas.drawingTools()->active());QCOMPARE(canvas.project().toJson(),before);mouse(canvas,QEvent::MouseButtonRelease,{280,92});QCOMPARE(canvas.document()->history()->count(),0);QVERIFY(!warnings.isEmpty());QVERIFY(warnings.last().first().toString().contains("tile"));
    }
    void renderNudgePreview(){
        if(qEnvironmentVariableIsEmpty("TOON_UI_PREVIEW"))QSKIP("Preview output is opt-in");Canvas canvas;setup(canvas,SymmetryMode::Vertical);canvas.setTool(Canvas::Tool::VectorNudge);canvas.drawingTools()->nudgeRadius=35;canvas.drawingTools()->nudgeStrength=.8;const auto source=active(canvas).objects.first();const auto center=copyPoint(source,{155,100},0);
        mouse(canvas,QEvent::MouseButtonPress,center);mouse(canvas,QEvent::MouseMove,center+QPointF(0,-24));QApplication::processEvents();const auto output=qEnvironmentVariable("TOON_UI_PREVIEW");QVERIFY(QDir().mkpath(output));QVERIFY(canvas.grab().save(output+"/vector-nudge-canvas.png"));QTest::keyClick(&canvas,Qt::Key_Escape);
    }
    void widthOnSymmetryCopy_data(){modes();}
    void widthOnSymmetryCopy() {
        QFETCH(int,mode);auto object=source(SymmetryMode(mode));const int middle=15;
        const auto point=copyPoint(object,object.nodes[middle].anchor);
        const auto region=circle(point,12);QVERIFY(vectorCorrectionIntersects(object,region));
        QVERIFY(brushVectorWidths(object,point,point,12,.4,true));
        QVERIFY(qAbs(object.widthFactors[middle]-std::exp(.4))<1e-9);
        QCOMPARE(object.widthFactors.first(),1.);QCOMPARE(object.widthFactors.last(),1.);
        QVERIFY(brushVectorWidths(object,point,point,12,.4,false));
        QVERIFY(qAbs(object.widthFactors[middle]-1.)<1e-9);
    }
    void simplifyOnSymmetryCopy_data(){modes();}
    void simplifyOnSymmetryCopy() {
        QFETCH(int,mode);const auto object=source(SymmetryMode(mode));
        const auto region=circle(copyPoint(object,object.nodes[15].anchor),25);
        const auto result=simplifiedVectorPath(object,2,region);
        QVERIFY(result.nodes.size()<object.nodes.size());QVERIFY(result.nodes.size()>10);
        QCOMPARE(result.nodes.first(),object.nodes.first());QCOMPARE(result.nodes.last(),object.nodes.last());
        QCOMPARE(result.brushStroke,object.brushStroke);QCOMPARE(result.transform,object.transform);
        for(const auto &node:object.nodes) {
            const auto copies=brushSymmetryInstances(object.brushStroke->settings.symmetry,node.anchor,object.brushStroke->canvasSize);
            const bool affected=std::any_of(copies.cbegin(),copies.cend(),[&](const auto &copy){return region.contains(object.transform.map(copy.point));});
            if(!affected)QVERIFY(result.nodes.contains(node));
        }
    }
    void overlappingCopiesApplyWidthOnce() {
        for(const auto mode:{SymmetryMode::Vertical,SymmetryMode::DualAxis,SymmetryMode::Radial}) {
            auto object=source(mode);object.brushStroke->settings.symmetry.angle=0;
            object.nodes={BezierNode::corner({320,230}),BezierNode::corner({320,240}),BezierNode::corner({320,250})};
            const auto point=object.transform.map(QPointF(320,240));
            QVERIFY(brushVectorWidths(object,point,point,30,.4,true));
            QVERIFY(qAbs(object.widthFactors[1]-std::exp(.4))<1e-9);
        }
    }
    void singleDabCopyCanBeCorrected() {
        auto object=source(SymmetryMode::Vertical);
        object.nodes={BezierNode::corner({80,100}),BezierNode::corner({80,100})};
        const auto point=copyPoint(object,{80,100});
        QVERIFY(vectorCorrectionIntersects(object,circle(point,12)));
        QVERIFY(brushVectorWidths(object,point,point,12,.4,true));
        QVERIFY(qAbs(object.widthFactors.first()-std::exp(.4))<1e-9);
        QCOMPARE(object.widthFactors.first(),object.widthFactors.last());
    }
    void resamplingUsesLongestTransformedCopy() {
        auto object=source(SymmetryMode::Radial);object.nodes={BezierNode::corner({100,100}),BezierNode::corner({200,100})};
        object.transform=QTransform::fromScale(.1,3);object.brushStroke->settings.symmetry.angle=0;
        resampleVectorWidthNodes(object,10);QVERIFY(object.nodes.size()>=31);
        const auto point=copyPoint(object,{150,100});
        QVERIFY(brushVectorWidths(object,point,point,12,.4,true));
    }
    void simplifyToleranceIncludesNonuniformRadialCopy() {
        auto object=source(SymmetryMode::Radial);object.brushStroke->settings.symmetry.angle=0;
        object.nodes={BezierNode::corner({100,100}),BezierNode::corner({100.8,150}),BezierNode::corner({100,200})};
        object.transform=QTransform::fromScale(.1,10);
        // The source has only .08 pixels of deviation, but its quarter-turn
        // copy has 8. Simplifying the shared path must preserve the latter.
        QCOMPARE(simplifiedVectorPath(object,2).nodes.size(),3);
        object.brushStroke.reset();QCOMPARE(simplifiedVectorPath(object,2).nodes.size(),2);
    }
    void canvasGesture_data() {
        QTest::addColumn<int>("mode");QTest::addColumn<bool>("simplify");
        for(const auto &[name,mode]:{std::pair{"vertical",SymmetryMode::Vertical},std::pair{"horizontal",SymmetryMode::Horizontal},
            std::pair{"both",SymmetryMode::DualAxis},std::pair{"radial",SymmetryMode::Radial}})
            for(bool simplify:{false,true})QTest::newRow(qPrintable(QString(name)+(simplify?"-simplify":"-width")))<<int(mode)<<simplify;
    }
    void canvasGesture() {
        QFETCH(int,mode);QFETCH(bool,simplify);Canvas canvas;setup(canvas,SymmetryMode(mode));auto *document=canvas.document();
        const auto before=canvas.project().toJson();const auto original=active(canvas).objects.first();
        const auto point=copyPoint(original,{155,100});auto *tools=canvas.drawingTools();
        canvas.setTool(simplify?Canvas::Tool::VectorSimplify:Canvas::Tool::VectorWidth);
        tools->widthBrushRadius=30;tools->widthBrushStrength=.4;tools->simplifyTolerance=2;
        QSignalSpy warnings(document,&Document::warning);const int history=document->history()->count();
        click(canvas,{20,240});QCOMPARE(canvas.project().toJson(),before);QCOMPARE(document->history()->count(),history);
        click(canvas,point);QVERIFY(warnings.isEmpty());QCOMPARE(document->history()->count(),history+1);
        const auto changed=active(canvas).objects.first();QCOMPARE(changed.id,original.id);QCOMPARE(changed.brushStroke,original.brushStroke);
        if(simplify)QVERIFY(changed.nodes.size()<original.nodes.size());
        else {QVERIFY(!changed.widthFactors.isEmpty());QVERIFY(*std::max_element(changed.widthFactors.cbegin(),changed.widthFactors.cend())>1.2);}
        QVERIFY(!active(canvas).tiles.isEmpty());const auto after=canvas.project().toJson();
        document->history()->undo();QCOMPARE(canvas.project().toJson(),before);
        document->history()->redo();QCOMPARE(canvas.project().toJson(),after);
        document->history()->undo();mouse(canvas,QEvent::MouseButtonPress,point);mouse(canvas,QEvent::MouseMove,point+QPointF(10,0));
        QVERIFY(document->externalEditActive());QTest::keyClick(&canvas,Qt::Key_Escape);
        QCOMPARE(canvas.project().toJson(),before);QVERIFY(!document->externalEditActive());
        document->setLayersLocked({active(canvas).id},true);const auto locked=canvas.project().toJson();click(canvas,point);QCOMPARE(canvas.project().toJson(),locked);
    }
};
QTEST_MAIN(VectorCorrectionTest)
#include "test_vector_correction.moc"
