#include "document.h"
#include "object_tools.h"
#include "sheet.h"
#include "vector_brush.h"
#include <QFile>
#include <QJsonArray>
#include <QTemporaryDir>
#include <QUuid>
#include <QtTest>
#include <algorithm>
using namespace toon;
namespace {
Project project() { auto p = Project::create(); p.width = 512; p.height = 384; p.transparent = true; return p; }
QVector<BezierNode> curve() {
    return {{{40,150},{40,150},{100,20}},{{240,150},{180,20},{240,150}}};
}
const RasterLayer &active(const Document &doc) { return doc.project().shots.first().layers[doc.activeLayer()]; }
QString addCurve(Document &doc) { return ObjectTools::addPath(&doc,curve(),false,QColor("#4080c0"),12); }
QColor pixel(const RasterLayer &layer, int x, int y) {
    const int tx = int(std::floor(qreal(x)/tileSize)), ty = int(std::floor(qreal(y)/tileSize));
    const auto image = layer.tiles.value({tx,ty}); return image.isNull() ? QColor(Qt::transparent) : image.pixelColor(x-tx*tileSize,y-ty*tileSize);
}
}
class ObjectsTest : public QObject {
    Q_OBJECT
private slots:
    void vectorCreationRenderingAndUndo() {
        Document doc; doc.load(project()); QVERIFY(ObjectTools::addLayer(&doc,LayerKind::Vector));
        const auto id = addCurve(doc); QVERIFY(!id.isEmpty()); QCOMPARE(active(doc).objects.size(),1);
        QVERIFY(pixel(active(doc),140,52).alpha()>0); QCOMPARE(pixel(active(doc),140,150).alpha(),0);
        QVERIFY(!layerCanPaint(doc.project().shots[0].layers,doc.activeLayer()));
        QVERIFY(layerCanEdit(doc.project().shots[0].layers,doc.activeLayer()));
        doc.history()->undo(); QVERIFY(active(doc).objects.isEmpty()); QVERIFY(active(doc).tiles.isEmpty());
        doc.history()->redo(); QCOMPARE(active(doc).objects.first().id,id);
        QCOMPARE(renderMovieFrame(doc.project()).pixelColor(140,52),pixel(active(doc),140,52));
    }
    void editableFillAndStroke() {
        Document doc; doc.load(project()); QVERIFY(ObjectTools::addLayer(&doc,LayerKind::Vector));
        const auto id = ObjectTools::addPath(&doc,{BezierNode::corner({30,30}),BezierNode::corner({100,30}),BezierNode::corner({100,100}),BezierNode::corner({30,100})},true,Qt::black,4,Qt::red);
        QVERIFY(!id.isEmpty()); QCOMPARE(pixel(active(doc),60,60),QColor(Qt::red));
        auto object = *ObjectTools::object(active(doc),id); object.fill = Qt::green; object.strokeWidth = 10;
        QVERIFY(ObjectTools::updateObject(&doc,object)); QCOMPARE(pixel(active(doc),60,60),QColor(Qt::green));
        QCOMPARE(ObjectTools::hitTest(active(doc),{60,60}),id);
    }
    void objectEditPreviewCommitsOnceAndCancels() {
        Document doc; doc.load(project()); ObjectTools::addLayer(&doc,LayerKind::Vector); const auto id = addCurve(doc);
        const int count = doc.history()->count(); ObjectTools edit(&doc);
        QVERIFY(edit.beginEdit(id)); QVERIFY(doc.externalEditActive());
        auto object = *ObjectTools::object(active(doc),id);
        for (int step = 1; step <= 8; ++step) { object.transform = QTransform::fromTranslate(step*10,20); QVERIFY(edit.previewEdit(object)); }
        QCOMPARE(doc.history()->count(),count); QCOMPARE(active(doc).objects.first().transform.dx(),80.0);
        edit.finishEdit(); QCOMPARE(doc.history()->count(),count+1); QVERIFY(!doc.externalEditActive());
        doc.history()->undo(); QVERIFY(active(doc).objects.first().transform.isIdentity());
        QVERIFY(edit.beginEdit(id)); object.transform = QTransform::fromTranslate(100,100); QVERIFY(edit.previewEdit(object));
        edit.cancelEdit(); QVERIFY(active(doc).objects.first().transform.isIdentity());
        QCOMPARE(doc.history()->count(),count+1);
    }
    void previewDoesNotRestoreAfterNewDocumentLoad() {
        Document doc; doc.load(project()); ObjectTools::addLayer(&doc,LayerKind::Vector); const auto id = addCurve(doc);
        ObjectTools edit(&doc); QVERIFY(edit.beginEdit(id)); auto next = project(); next.movie = "New document";
        doc.load(next); edit.cancelEdit(); QCOMPARE(doc.project().movie,QString("New document"));
    }
    void invalidEditAndPositionLockAreAtomic() {
        Document doc; doc.load(project()); ObjectTools::addLayer(&doc,LayerKind::Vector); const auto id = addCurve(doc);
        const auto before = doc.project().toJson(); const auto pixels = active(doc).tiles;
        auto object = *ObjectTools::object(active(doc),id); object.nodes.first().anchor.setX(qQNaN());
        QVERIFY(!ObjectTools::updateObject(&doc,object)); QCOMPARE(doc.project().toJson(),before); QCOMPARE(active(doc).tiles,pixels);
        doc.setLayerPositionLocked(true); object = *ObjectTools::object(active(doc),id); object.transform.translate(10,10);
        QVERIFY(!ObjectTools::updateObject(&doc,object));
        object.transform = {}; object.stroke = Qt::red; QVERIFY(ObjectTools::updateObject(&doc,object));
    }
    void unavailableObjectEditsExplainTheirCause_data() {
        QTest::addColumn<int>("restriction");
        QTest::addColumn<QString>("messagePart");
        QTest::newRow("locked-layer") << 0 << QString("unlock");
        QTest::newRow("hidden-layer") << 1 << QString("Show");
        QTest::newRow("locked-folder") << 2 << QString("folders");
        QTest::newRow("selected-folder") << 3 << QString("inside the folder");
    }
    void unavailableObjectEditsExplainTheirCause() {
        QFETCH(int,restriction); QFETCH(QString,messagePart);
        Document doc; doc.load(project()); QVERIFY(ObjectTools::addLayer(&doc,LayerKind::Vector));
        const auto id=addCurve(doc); const auto layerId=active(doc).id;
        if(restriction==0)doc.setLayersLocked({layerId},true);
        else if(restriction==1)doc.setLayersVisible({layerId},false);
        else {
            doc.groupLayers({layerId});
            if(restriction==2) {
                doc.setLayersLocked({active(doc).id},true);
                for(int i=0;i<doc.project().shots.first().layers.size();++i)
                    if(doc.project().shots.first().layers[i].id==layerId){doc.selectLayer(i);break;}
            }
        }
        QSignalSpy warnings(&doc,&Document::warning); const auto before=doc.project().toJson();
        const int history=doc.history()->count(); ObjectTools edit(&doc);
        QVERIFY(!edit.beginEdit(id)); QCOMPARE(warnings.size(),1);
        QVERIFY(warnings.first().first().toString().contains(messagePart));
        QCOMPARE(doc.project().toJson(),before); QCOMPARE(doc.history()->count(),history);
    }
    void positionLockPreviewWarnsOnceAndStillAllowsStyleEdits() {
        Document doc; doc.load(project()); QVERIFY(ObjectTools::addLayer(&doc,LayerKind::Vector));
        const auto id=addCurve(doc); doc.setLayerPositionLocked(true);
        QSignalSpy warnings(&doc,&Document::warning); ObjectTools edit(&doc);
        QVERIFY(edit.beginLayerEdit("Brush path width")); const auto before=doc.project().toJson();
        auto objects=active(doc).objects;
        objects.first().nodes.insert(1,BezierNode::corner({140,52}));
        for(int move=0;move<8;++move)QVERIFY(!edit.previewObjects(objects));
        QCOMPARE(warnings.size(),1); QVERIFY(warnings.first().first().toString().contains("position"));
        QCOMPARE(doc.project().toJson(),before);
        objects=active(doc).objects; objects.first().strokeWidth=18;
        QVERIFY(edit.previewObjects(objects)); QCOMPARE(warnings.size(),1);
        edit.finishEdit(); QCOMPARE(active(doc).objects.first().strokeWidth,18.0);
        QVERIFY(edit.beginEdit(id)); auto moved=active(doc).objects.first(); moved.transform.translate(10,10);
        QVERIFY(!edit.previewEdit(moved)); QCOMPARE(warnings.size(),2);
        edit.cancelEdit(); QCOMPARE(active(doc).objects.first().strokeWidth,18.0);
        QVERIFY(active(doc).objects.first().transform.isIdentity());
    }
    void failedObjectActionsWarnButOrdinaryNoOpsDoNot() {
        Document doc; doc.load(project()); QSignalSpy warnings(&doc,&Document::warning);
        QVERIFY(!ObjectTools::simplify(&doc,2)); QCOMPARE(warnings.size(),1);
        QVERIFY(warnings.takeFirst().first().toString().contains("vector layer"));
        QVERIFY(!ObjectTools::eraseAt(&doc,{100,100},5)); QCOMPARE(warnings.size(),1);
        QVERIFY(warnings.takeFirst().first().toString().contains("vector layer"));
        QVERIFY(ObjectTools::addLayer(&doc,LayerKind::Vector)); const auto id=addCurve(doc);
        ObjectTools edit(&doc); QVERIFY(edit.beginEdit(id));
        QVERIFY(!ObjectTools::simplify(&doc,2)); QCOMPARE(warnings.size(),1);
        QVERIFY(warnings.takeFirst().first().toString().contains("Finish the current edit")); edit.cancelEdit();
        QVERIFY(!ObjectTools::eraseAt(&doc,{480,350},3)); QVERIFY(warnings.isEmpty());
        QVERIFY(!ObjectTools::simplify(&doc,2)); QVERIFY(warnings.isEmpty());
        doc.setLayerPositionLocked(true);
        QVERIFY(!ObjectTools::simplify(&doc,2)); QCOMPARE(warnings.size(),1);
        QVERIFY(warnings.first().first().toString().contains("position"));
    }
    void pathAndSegmentErasersRespectUndo() {
        Document doc; doc.load(project()); ObjectTools::addLayer(&doc,LayerKind::Vector);
        const auto id = ObjectTools::addPath(&doc,{BezierNode::corner({20,100}),BezierNode::corner({100,100}),BezierNode::corner({200,100}),BezierNode::corner({300,100})},false,Qt::black,4);
        QVERIFY(!id.isEmpty()); QVERIFY(ObjectTools::eraseAt(&doc,{150,100},5,true));
        QCOMPARE(active(doc).objects.size(),2); QCOMPARE(pixel(active(doc),150,100).alpha(),0); QVERIFY(pixel(active(doc),50,100).alpha()>0);
        doc.history()->undo(); QCOMPARE(active(doc).objects.size(),1); QCOMPARE(active(doc).objects.first().id,id);
        QPainterPath sweep; sweep.moveTo(150,60); sweep.lineTo(150,140);
        QVERIFY(ObjectTools::erasePath(&doc,sweep,3)); QVERIFY(active(doc).objects.isEmpty());
        doc.history()->undo(); QCOMPARE(active(doc).objects.first().id,id);
    }
    void eraserAndHitTestingRespectTransformedStrokeWidth() {
        Document doc; doc.load(project()); ObjectTools::addLayer(&doc,LayerKind::Vector);
        const auto id=ObjectTools::addPath(&doc,{BezierNode::corner({20,20}),BezierNode::corner({100,20})},false,Qt::black,10);
        auto object=*ObjectTools::object(active(doc),id); object.transform.scale(2,5);
        QVERIFY(ObjectTools::updateObject(&doc,object));
        QCOMPARE(ObjectTools::hitTest(active(doc),{100,120},1),id);
        QVERIFY(ObjectTools::eraseAt(&doc,{100,120},2)); QVERIFY(active(doc).objects.isEmpty());
    }
    void closedPathSegmentErasingOpensRemainingContour() {
        Document doc; doc.load(project()); ObjectTools::addLayer(&doc,LayerKind::Vector);
        ObjectTools::addPath(&doc,{BezierNode::corner({30,30}),BezierNode::corner({100,30}),BezierNode::corner({100,100}),BezierNode::corner({30,100})},true,Qt::black,4,Qt::red);
        QVERIFY(ObjectTools::eraseAt(&doc,{60,30},4,true)); QCOMPARE(active(doc).objects.size(),1);
        const auto &object = active(doc).objects.first(); QVERIFY(!object.closed); QVERIFY(!object.fill.isValid()); QCOMPARE(object.nodes.size(),4);
        QCOMPARE(pixel(active(doc),60,30).alpha(),0); QVERIFY(pixel(active(doc),60,100).alpha()>0);
    }
    void intersectionEraserTrimsAtCrossingsInsteadOfExistingNodesAndUndoesOnce() {
        Document doc;doc.load(project());QVERIFY(ObjectTools::addLayer(&doc,LayerKind::Vector));
        const auto ink=ObjectTools::addPath(&doc,{BezierNode::corner({20,120}),BezierNode::corner({60,120}),
            BezierNode::corner({110,120}),BezierNode::corner({180,120}),BezierNode::corner({260,120}),
            BezierNode::corner({340,120})},false,QColor("#204080"),6);
        const auto crossing=ObjectTools::addPath(&doc,{BezierNode::corner({220,60}),BezierNode::corner({220,180})},false,Qt::black,4);
        const auto before=doc.project().toJson();const auto barrier=*ObjectTools::object(active(doc),crossing);
        const int history=doc.history()->count();
        QVERIFY(ObjectTools::eraseAt(&doc,{320,120},3,VectorEraseMode::Intersection));
        QCOMPARE(doc.history()->count(),history+1);QCOMPARE(active(doc).objects.size(),2);
        const auto &fragment=active(doc).objects.first();QVERIFY(fragment.id!=ink);
        QCOMPARE(fragment.nodes.size(),5);QVERIFY(QLineF(fragment.nodes.last().anchor,{220,120}).length()<.002);
        QCOMPARE(fragment.nodes[2].anchor,QPointF(110,120));QCOMPARE(active(doc).objects.last(),barrier);
        QCOMPARE(pixel(active(doc),320,120).alpha(),0);QVERIFY(pixel(active(doc),200,120).alpha()>0);
        const auto after=doc.project().toJson();doc.history()->undo();QCOMPARE(doc.project().toJson(),before);
        doc.history()->redo();QCOMPARE(doc.project().toJson(),after);
    }
    void intersectionEraserRetainsBothSidesOfAnInteriorInterval() {
        Document doc;doc.load(project());QVERIFY(ObjectTools::addLayer(&doc,LayerKind::Vector));
        QVERIFY(!ObjectTools::addPath(&doc,{BezierNode::corner({20,150}),BezierNode::corner({380,150})},false,Qt::blue,6).isEmpty());
        for(qreal x:{120.,280.})QVERIFY(!ObjectTools::addPath(&doc,
            {BezierNode::corner({x,60}),BezierNode::corner({x,220})},false,Qt::black,4).isEmpty());
        QVERIFY(ObjectTools::eraseAt(&doc,{200,150},3,VectorEraseMode::Intersection));
        QCOMPARE(active(doc).objects.size(),4);
        const auto &left=active(doc).objects[0],&right=active(doc).objects[1];
        QVERIFY(QLineF(left.nodes.last().anchor,{120,150}).length()<.002);
        QVERIFY(QLineF(right.nodes.first().anchor,{280,150}).length()<.002);
        QCOMPARE(pixel(active(doc),200,150).alpha(),0);
        QVERIFY(pixel(active(doc),60,150).alpha()>0);QVERIFY(pixel(active(doc),340,150).alpha()>0);
    }
    void intersectionEraserSplitsTransformedCubicWithoutFlatteningItsShapeOrStyle() {
        Document doc;doc.load(project());QVERIFY(ObjectTools::addLayer(&doc,LayerKind::Vector));
        const auto id=ObjectTools::addPath(&doc,{{{40,170},{40,170},{40,10}},{{280,170},{280,10},{280,170}}},
            false,QColor("#6940a0"),8);
        auto original=*ObjectTools::object(active(doc),id);original.opacity=.65;original.widthFactors={.5,1.5};
        original.transform.translate(10,20);original.transform.scale(1.2,.8);QVERIFY(ObjectTools::updateObject(&doc,original));
        const qreal x=original.transform.map(QPointF(160,50)).x();
        const auto barrierId=ObjectTools::addPath(&doc,{BezierNode::corner({x,10}),BezierNode::corner({x,220})},false,Qt::black,3);
        const auto barrier=*ObjectTools::object(active(doc),barrierId);
        const qreal t=.85,u=1-t;
        const auto point=QPointF(40,170)*(u*u*u)+QPointF(40,10)*(3*u*u*t)
            +QPointF(280,10)*(3*u*t*t)+QPointF(280,170)*(t*t*t);
        QVERIFY(ObjectTools::eraseAt(&doc,original.transform.map(point),3,VectorEraseMode::Intersection));
        QCOMPARE(active(doc).objects.size(),2);const auto &fragment=active(doc).objects.first();
        QCOMPARE(fragment.transform,original.transform);QCOMPARE(fragment.stroke,original.stroke);
        QCOMPARE(fragment.opacity,original.opacity);QCOMPARE(fragment.strokeWidth,original.strokeWidth);
        QCOMPARE(fragment.nodes.size(),2);
        QVERIFY(QLineF(fragment.nodes[0].anchor,{40,170}).length()<.002);
        QVERIFY(QLineF(fragment.nodes[0].outControl,{40,90}).length()<.002);
        QVERIFY(QLineF(fragment.nodes[1].inControl,{100,50}).length()<.002);
        QVERIFY(QLineF(fragment.nodes[1].anchor,{160,50}).length()<.002);
        QCOMPARE(fragment.widthFactors.first(),.5);QVERIFY(qAbs(fragment.widthFactors.last()-1)<.002);
        QCOMPARE(active(doc).objects.last(),barrier);QVERIFY(validateEditableObject(fragment).isEmpty());
    }
    void intersectionEraserHandlesClosedWrapAndNoCrossingFallback() {
        Document doc;doc.load(project());QVERIFY(ObjectTools::addLayer(&doc,LayerKind::Vector));
        const QVector<BezierNode> square{BezierNode::corner({30,30}),BezierNode::corner({250,30}),
            BezierNode::corner({250,250}),BezierNode::corner({30,250})};
        const auto id=ObjectTools::addPath(&doc,square,true,Qt::black,5,Qt::red);
        const auto barrierId=ObjectTools::addPath(&doc,{BezierNode::corner({140,5}),BezierNode::corner({140,280})},false,Qt::blue,4);
        QVERIFY(ObjectTools::eraseAt(&doc,{30,140},3,VectorEraseMode::Intersection));
        QCOMPARE(active(doc).objects.size(),2);const auto &fragment=active(doc).objects.first();
        QVERIFY(!fragment.closed);QVERIFY(!fragment.fill.isValid());QCOMPARE(fragment.nodes.size(),4);
        QVERIFY(QLineF(fragment.nodes.first().anchor,{140,30}).length()<.002);
        QVERIFY(QLineF(fragment.nodes.last().anchor,{140,250}).length()<.002);
        QCOMPARE(pixel(active(doc),30,140).alpha(),0);QVERIFY(pixel(active(doc),250,140).alpha()>0);
        doc.history()->undo();QVERIFY(ObjectTools::removeObject(&doc,barrierId));
        QVERIFY(ObjectTools::eraseAt(&doc,{30,140},3,VectorEraseMode::Intersection));QVERIFY(active(doc).objects.isEmpty());
        doc.history()->undo();QCOMPARE(active(doc).objects.first().id,id);QVERIFY(active(doc).objects.first().closed);
    }
    void intersectionEraserRetainsRecordedPressureAndBrushSource() {
        Document doc;doc.load(project());QVERIFY(ObjectTools::addLayer(&doc,LayerKind::Vector));
        BrushSettings brush;brush.size=8;brush.spacing=.1;brush.pressureSize=true;
        QVERIFY(doc.beginStroke({20,180},.2,brush,false));
        for(int i=1;i<=36;++i)doc.continueStroke({20.+10*i,180},.2+.8*i/36);
        doc.endStroke();QCOMPARE(active(doc).objects.size(),1);const auto original=active(doc).objects.first();
        QVERIFY(original.brushStroke.has_value());QVERIFY(original.brushStroke->samples.size()>20);
        const auto barrier=ObjectTools::addPath(&doc,{BezierNode::corner({220,100}),BezierNode::corner({220,260})},false,Qt::black,3);
        QVERIFY(!barrier.isEmpty());QVERIFY(ObjectTools::eraseAt(&doc,{320,180},3,VectorEraseMode::Intersection));
        QCOMPARE(active(doc).objects.size(),2);const auto &fragment=active(doc).objects.first();
        QVERIFY(fragment.brushStroke.has_value());QVERIFY(QLineF(fragment.nodes.last().anchor,{220,180}).length()<.002);
        QCOMPARE(fragment.brushStroke->settings,original.brushStroke->settings);
        QCOMPARE(fragment.brushStroke->seed,original.brushStroke->seed);
        QCOMPARE(fragment.brushStroke->canvasSize,original.brushStroke->canvasSize);
        QVERIFY(fragment.brushStroke->samples.size()<original.brushStroke->samples.size());
        for(const auto &sample:fragment.brushStroke->samples) {
            const auto found=std::find_if(original.brushStroke->samples.cbegin(),original.brushStroke->samples.cend(),
                [&](const VectorBrushSample &value){return value.dabIndex==sample.dabIndex;});
            QVERIFY(found!=original.brushStroke->samples.cend());QCOMPARE(sample.input,found->input);
            QCOMPARE(sample.point,found->point);QVERIFY(sample.position>=0&&sample.position<=1);
        }
        QCOMPARE(pixel(active(doc),320,180).alpha(),0);QVERIFY(pixel(active(doc),100,180).alpha()>0);
        QVERIFY(validateEditableObject(fragment).isEmpty());
    }
    void intersectionEraserDoesNotUseTangenciesOrCoincidentSpansAsCuts() {
        Document doc;doc.load(project());QVERIFY(ObjectTools::addLayer(&doc,LayerKind::Vector));
        const auto id=ObjectTools::addPath(&doc,{BezierNode::corner({20,100}),BezierNode::corner({380,100})},false,Qt::blue,4);
        QVERIFY(!ObjectTools::addPath(&doc,{BezierNode::corner({80,100}),BezierNode::corner({180,100})},false,Qt::black,2).isEmpty());
        // The arch touches y=100 at its horizontal tangent, without crossing.
        QVERIFY(!ObjectTools::addPath(&doc,{{{100,160},{100,160},{140,80}},{{220,160},{180,80},{220,160}}},false,Qt::black,2).isEmpty());
        QVERIFY(ObjectTools::eraseAt(&doc,{350,100},2,VectorEraseMode::Intersection));
        QCOMPARE(active(doc).objects.size(),2);QVERIFY(!ObjectTools::object(active(doc),id));
    }
    void intersectionEraserRecognizesNonAdjacentSelfCrossings() {
        Document doc;doc.load(project());QVERIFY(ObjectTools::addLayer(&doc,LayerKind::Vector));
        QVERIFY(!ObjectTools::addPath(&doc,{BezierNode::corner({20,50}),BezierNode::corner({300,250}),
            BezierNode::corner({20,250}),BezierNode::corner({300,50})},false,Qt::black,4).isEmpty());
        QVERIFY(ObjectTools::eraseAt(&doc,{265,75},2,VectorEraseMode::Intersection));
        QCOMPARE(active(doc).objects.size(),2);
        for(const auto &fragment:active(doc).objects) {
            QVERIFY(QLineF(fragment.nodes.last().anchor,{160,150}).length()<.002);
            QVERIFY(validateEditableObject(fragment).isEmpty());
        }
        QCOMPARE(pixel(active(doc),265,75).alpha(),0);QVERIFY(pixel(active(doc),60,250).alpha()>0);
    }
    void intersectionEraserRespectsLocksAndAnimatedPathProtection() {
        Document doc;doc.load(project());QVERIFY(ObjectTools::addLayer(&doc,LayerKind::Vector));
        const auto id=ObjectTools::addPath(&doc,{BezierNode::corner({20,150}),BezierNode::corner({380,150})},false,Qt::black,5);
        QVERIFY(!ObjectTools::addPath(&doc,{BezierNode::corner({220,60}),BezierNode::corner({220,240})},false,Qt::blue,4).isEmpty());
        const auto layerId=active(doc).id;doc.setLayersLocked({layerId},true);
        QSignalSpy warnings(&doc,&Document::warning);const auto locked=doc.project().toJson();const int history=doc.history()->count();
        QVERIFY(!ObjectTools::eraseAt(&doc,{320,150},3,VectorEraseMode::Intersection));
        QCOMPARE(doc.project().toJson(),locked);QCOMPARE(doc.history()->count(),history);QCOMPARE(warnings.size(),1);
        doc.setLayersLocked({layerId},false);QVERIFY(doc.enablePathAnimation(layerId,id));
        const auto animated=doc.project().toJson();const int keyedHistory=doc.history()->count();warnings.clear();
        QVERIFY(!ObjectTools::eraseAt(&doc,{320,150},3,VectorEraseMode::Intersection));
        QCOMPARE(doc.project().toJson(),animated);QCOMPARE(doc.history()->count(),keyedHistory);
        QCOMPARE(warnings.size(),1);QVERIFY(warnings.first().first().toString().contains("path animation"));
    }
    void textIsEditableAndWraps() {
        Document doc; doc.load(project()); ObjectTools::addLayer(&doc,LayerKind::Text);
        auto text = EditableObject::create(ObjectKind::Text); text.text = "A city\nat dawn"; text.position={40,30}; text.fontSize=36;
        const auto id = ObjectTools::addText(&doc,text); QVERIFY(!id.isEmpty()); QVERIFY(!active(doc).tiles.isEmpty());
        const auto initial = editableObjectBounds(active(doc).objects.first()); QVERIFY(initial.height()>50);
        text = *ObjectTools::object(active(doc),id); text.text="A long line that wraps within a narrow text frame"; text.textWidth=120; text.bold=true; text.italic=true; text.alignment=Qt::AlignHCenter;
        QVERIFY(ObjectTools::updateObject(&doc,text)); const auto wrapped=editableObjectBounds(active(doc).objects.first());
        QVERIFY(wrapped.height()>initial.height()); QVERIFY(wrapped.width()<130);
        doc.history()->undo(); QCOMPARE(active(doc).objects.first().text,QString("A city\nat dawn"));
    }
    void vectorTransformsPreserveSourceAndRasterizeUndo() {
        Document doc; doc.load(project()); ObjectTools::addLayer(&doc,LayerKind::Vector); const auto id=addCurve(doc);
        auto layer=active(doc); const auto nodes=layer.objects.first().nodes;
        QTransform matrix; matrix.translate(300,0); matrix.scale(-1,1);
        QString error; QVERIFY2(transformEditableLayer(layer,matrix,doc.drawingBounds(),1024,&error),qPrintable(error));
        QCOMPARE(layer.objects.first().nodes[0].anchor,nodes[0].anchor); QCOMPARE(layer.objects.first().transform,matrix);
        QCOMPARE(layer.objects.first().id,id); QVERIFY(pixel(layer,160,52).alpha()>0);
        const auto pixels=active(doc).tiles; QVERIFY(ObjectTools::rasterize(&doc)); QCOMPARE(active(doc).kind,LayerKind::Raster);
        QVERIFY(active(doc).objects.isEmpty()); QCOMPARE(active(doc).tiles,pixels);
        doc.history()->undo(); QCOMPARE(active(doc).kind,LayerKind::Vector); QCOMPARE(active(doc).objects.first().id,id);
    }
    void objectStorageHasNoEmbeddedCacheAndRebuildsExactly() {
        Document doc; doc.load(project()); ObjectTools::addLayer(&doc,LayerKind::Vector); addCurve(doc);
        ObjectTools::addLayer(&doc,LayerKind::Text); auto text=EditableObject::create(ObjectKind::Text); text.text="Film title"; text.position={20,200}; ObjectTools::addText(&doc,text);
        auto p=doc.project(); NamedSelection saved; saved.id=QUuid::createUuid().toString(QUuid::WithoutBraces); saved.name="Character";
        saved.path.addEllipse(QRectF(20,30,130,200)); saved.featherRadius=12; p.shots[0].savedSelections.append(saved);
        const auto manifest=p.toJson(); QCOMPARE(manifest["version"].toInt(),19);
        const auto layers=manifest["shots"].toArray()[0].toObject()["layers"].toArray();
        QVERIFY(layers[1].toObject()["tiles"].toArray().isEmpty()); QVERIFY(!p.shots[0].layers[1].tiles.isEmpty());
        QTemporaryDir tmp; QString error; const auto file=tmp.filePath("objects.toonproj");
        QVERIFY2(saveProject(p,file,&error),qPrintable(error)); const auto loaded=loadProject(file,&error); QVERIFY2(loaded.has_value(),qPrintable(error));
        QCOMPARE(loaded->toJson(),p.toJson()); QCOMPARE(loaded->shots[0].layers[1].tiles,p.shots[0].layers[1].tiles);
        QCOMPARE(loaded->shots[0].layers[2].tiles,p.shots[0].layers[2].tiles); QCOMPARE(loaded->shots[0].savedSelections[0].path,saved.path);
    }
    void objectCachesFollowProjectResizeAndNotesAncestry() {
        Document doc; auto initial=project(); initial.shots[0].sheet.overscan=0; doc.load(initial);
        ObjectTools::addLayer(&doc,LayerKind::Vector);
        ObjectTools::addPath(&doc,{BezierNode::corner({450,100}),BezierNode::corner({600,100})},false,Qt::red,10);
        QCOMPARE(pixel(active(doc),550,100).alpha(),0);
        auto larger=doc.project();larger.width=640;doc.replaceProject(larger,"Resize artwork");
        QVERIFY(pixel(active(doc),550,100).alpha()>0);const auto resized=renderMovieFrame(doc.project());
        QTemporaryDir tmp;QString error;const auto file=tmp.filePath("resized.toonproj");QVERIFY(saveProject(doc.project(),file,&error));auto loaded=loadProject(file,&error);QVERIFY2(loaded.has_value(),qPrintable(error));QCOMPARE(renderMovieFrame(*loaded),resized);
        doc.history()->undo();QCOMPARE(pixel(active(doc),550,100).alpha(),0);doc.history()->redo();QVERIFY(pixel(active(doc),550,100).alpha()>0);
        const auto id=ObjectTools::addPath(&doc,{BezierNode::corner({-80,150}),BezierNode::corner({30,150})},false,Qt::blue,10);QVERIFY(!id.isEmpty());QCOMPARE(pixel(active(doc),-50,150).alpha(),0);
        const auto layerId=active(doc).id;doc.groupLayers({layerId});const auto group=active(doc);
        doc.changeLayer(group.name,group.visible,group.locked,true,group.opacity);
        const auto &inside=doc.project().shots[0].layers[1];QCOMPARE(inside.id,layerId);QVERIFY(pixel(inside,-50,150).alpha()>0);
        const auto sheet=renderSheet(doc.project(),doc.project().shots[0]);QVERIFY(saveProject(doc.project(),file,&error));loaded=loadProject(file,&error);QVERIFY2(loaded.has_value(),qPrintable(error));QCOMPARE(renderSheet(*loaded,loaded->shots[0]),sheet);
        doc.history()->undo();QCOMPARE(pixel(doc.project().shots[0].layers[1],-50,150).alpha(),0);
    }
    void malformedObjectAndSelectionMetadataRejects() {
        auto p=project(); auto layer=RasterLayer::create("Vector"); layer.kind=LayerKind::Vector;
        auto object=EditableObject::create(); object.nodes=curve(); layer.objects.append(object); p.shots[0].layers.append(layer);
        auto json=p.toJson(); auto shots=json["shots"].toArray(); auto shot=shots[0].toObject(); auto layers=shot["layers"].toArray();
        auto changed=layers[1].toObject(); changed["kind"]="raster"; layers[1]=changed; shot["layers"]=layers; shots[0]=shot; json["shots"]=shots;
        QString error; QVERIFY(!Project::fromJson(json,&error));
        p.shots[0].layers[1].objects[0].transform=QTransform(1,0,0,0,0,0); QVERIFY(!p.validate().isEmpty());
        p=project(); NamedSelection saved; saved.id=QUuid::createUuid().toString(QUuid::WithoutBraces); saved.name="Selection"; saved.path.addRect(0,0,100,100); p.shots[0].savedSelections.append(saved);
        json=p.toJson(); shots=json["shots"].toArray(); shot=shots[0].toObject(); auto selections=shot["savedSelections"].toArray(); auto selection=selections[0].toObject();
        selection["path"]=QJsonArray{QJsonArray{0,0,0},QJsonArray{2,100,100}}; selections[0]=selection; shot["savedSelections"]=selections; shots[0]=shot; json["shots"]=shots;
        QVERIFY(!Project::fromJson(json,&error));
    }
    void oldVersionMigratesToRasterWithoutObjects() {
        auto json=project().toJson(); json["version"]=6; QString error; const auto loaded=Project::fromJson(json,&error);
        QVERIFY2(loaded.has_value(),qPrintable(error)); QCOMPARE(loaded->shots[0].layers.first().kind,LayerKind::Raster);
        QVERIFY(loaded->shots[0].layers.first().objects.isEmpty()); QVERIFY(loaded->shots[0].savedSelections.isEmpty());
    }
    void layerDuplicationUsesFreshObjectIds() {
        Document doc; doc.load(project()); ObjectTools::addLayer(&doc,LayerKind::Vector); const auto id=addCurve(doc);
        doc.duplicateLayers({active(doc).id}); QCOMPARE(doc.project().shots[0].layers.size(),3);
        QVERIFY(active(doc).objects.first().id!=id); QVERIFY(doc.project().validate().isEmpty());
    }
};
QTEST_MAIN(ObjectsTest)
#include "test_objects.moc"
