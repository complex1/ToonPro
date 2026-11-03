#include "brush_engine.h"
#include "canvas.h"
#include "drawing_tool_controller.h"
#include "object_tools.h"
#include "sheet.h"
#include "vector_edit_geometry.h"
#include <QApplication>
#include <QMouseEvent>
#include <QSignalSpy>
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
    const auto view=canvas.viewTransform().map(artwork+sheetGeometry(canvas.project(),canvas.shot()).output.topLeft());
    QMouseEvent event(type,view,view,canvas.mapToGlobal(view),type==QEvent::MouseMove?Qt::NoButton:Qt::LeftButton,
        type==QEvent::MouseButtonRelease?Qt::NoButton:Qt::LeftButton,Qt::NoModifier);
    QApplication::sendEvent(&canvas,&event);
}
void click(Canvas &canvas,QPointF point){mouse(canvas,QEvent::MouseButtonPress,point);mouse(canvas,QEvent::MouseButtonRelease,point);}
}

class VectorCorrectionTest:public QObject {
    Q_OBJECT
private slots:
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
