#include "canvas.h"
#include "drawing_tool_controller.h"
#include "object_tools.h"
#include "property_animation.h"
#include "vector_edit_panel.h"
#include "value_slider.h"
#include "ui_theme.h"
#include <QLabel>
#include <QSignalSpy>
#include <QSlider>
#include <QTemporaryDir>
#include <QToolButton>
#include <QtTest>
using namespace toon;
namespace {
Project project(bool grouped=false) {
    auto p=Project::create();p.width=256;p.height=192;p.transparent=true;
    auto &shot=p.shots.first();shot.durationFrames=20;shot.sheet.overscan=0;
    auto layer=RasterLayer::create("Vector drawing");layer.kind=LayerKind::Vector;
    if(grouped){auto group=RasterLayer::create("Character");group.group=true;layer.parentId=group.id;shot.layers={group,layer};}
    else shot.layers={layer};
    return p;
}
const RasterLayer &active(const Document &doc){return doc.project().shots.first().layers[doc.activeLayer()];}
EditableObject evaluated(const Document &doc,const QString &id) {
    const auto &shot=doc.project().shots.first();const auto &layer=active(doc);
    return evaluateAnimatedObject(shot,layer.id,*ObjectTools::object(layer,id),doc.currentFrame());
}
QString addPath(Document &doc) {
    return ObjectTools::addPath(&doc,{BezierNode::corner({40,80}),BezierNode::corner({120,80}),BezierNode::corner({200,80})},false,Qt::blue,10);
}
QPointF viewPoint(const Canvas &canvas,QPointF point) {
    const auto &shot=canvas.shot();const auto *doc=canvas.document();
    if(!doc->keyframeSourceEditing())point=worldLayerTransform(shot,active(*doc).id,doc->currentFrame()).map(point);
    return canvas.viewTransform().map(sheetGeometry(canvas.project(),shot).output.topLeft()+point);
}
void clickPath(Canvas &canvas,QPointF point) {
    auto *tools=canvas.drawingTools();const auto view=viewPoint(canvas,point);
    tools->press(view,Qt::NoModifier);tools->release(view,Qt::NoModifier);
}
}
class KeyframeMorphTests:public QObject {
    Q_OBJECT
private slots:
    void initTestCase(){applyTheme();}
    void poseGestureKeepsSourceAndCommitsOnce() {
        Document doc;doc.load(project());const auto id=addPath(doc);QVERIFY(!id.isEmpty());
        const auto layerId=active(doc).id;const auto source=active(doc).objects;const auto tiles=active(doc).tiles;
        QVERIFY(doc.enablePathAnimation(layerId,id));QVERIFY(doc.setCurrentFrame(10));
        const int history=doc.history()->count();ObjectTools tools(&doc);QVERIFY(tools.beginEdit(id));
        auto pose=evaluated(doc,id);pose.nodes[1].anchor.ry()+=20;pose.nodes[1].inControl=pose.nodes[1].outControl=pose.nodes[1].anchor;
        QVERIFY(tools.previewEdit(pose));pose.nodes[1].anchor.ry()+=20;pose.nodes[1].inControl=pose.nodes[1].outControl=pose.nodes[1].anchor;
        pose.strokeWidth=20;pose.stroke=Qt::red;pose.opacity=.5;pose.widthFactors={.5,2,1};
        QVERIFY(tools.previewEdit(pose));QCOMPARE(doc.history()->count(),history);
        QCOMPARE(active(doc).objects,source);QCOMPARE(active(doc).tiles,tiles);QCOMPARE(evaluated(doc,id),pose);
        tools.finishEdit();QCOMPARE(doc.history()->count(),history+1);
        QCOMPARE(pathAnimation(doc.project().shots.first(),layerId,id)->keys.size(),2);
        QVERIFY(doc.setCurrentFrame(5));const auto middle=evaluated(doc,id);
        QCOMPARE(middle.nodes[1].anchor,QPointF(120,100));QCOMPARE(middle.strokeWidth,15.);QCOMPARE(middle.opacity,.75);
        QCOMPARE(middle.widthFactors[1],1.5);
        doc.history()->undo();QCOMPARE(evaluated(doc,id),source.first());QCOMPARE(active(doc).objects,source);
        doc.history()->redo();QCOMPARE(evaluated(doc,id),middle);
    }
    void cancelledAndUnchangedGesturesDoNotAddKeys() {
        Document doc;doc.load(project());const auto id=addPath(doc);const auto layerId=active(doc).id;
        QVERIFY(doc.enablePathAnimation(layerId,id));QVERIFY(doc.setCurrentFrame(8));
        const auto before=doc.project().toJson();const int history=doc.history()->count();ObjectTools tools(&doc);
        QVERIFY(tools.beginEdit(id));QVERIFY(tools.previewEdit(evaluated(doc,id)));tools.finishEdit();
        QCOMPARE(doc.project().toJson(),before);QCOMPARE(doc.history()->count(),history);
        QVERIFY(tools.beginEdit(id));auto pose=evaluated(doc,id);pose.transform.translate(25,5);
        QVERIFY(tools.previewEdit(pose));tools.cancelEdit();QCOMPARE(doc.project().toJson(),before);
        QVERIFY(tools.beginEdit(id));QVERIFY(tools.previewEdit(pose));QVERIFY(tools.previewEdit(active(doc).objects.first()));tools.finishEdit();
        QCOMPARE(doc.project().toJson(),before);QCOMPARE(doc.history()->count(),history);
    }
    void topologyAndDestructiveEditsAreRejectedAtomically() {
        Document doc;doc.load(project());const auto id=addPath(doc);const auto layerId=active(doc).id;
        QVERIFY(doc.enablePathAnimation(layerId,id));QVERIFY(doc.setCurrentFrame(6));
        const auto before=doc.project().toJson();QSignalSpy warnings(&doc,&Document::warning);
        auto pose=evaluated(doc,id);pose.nodes.insert(1,BezierNode::corner({80,80}));
        QVERIFY(!ObjectTools::updateObject(&doc,pose));QVERIFY(!warnings.isEmpty());
        pose=evaluated(doc,id);pose.closed=true;QVERIFY(!ObjectTools::updateObject(&doc,pose));
        QVERIFY(!ObjectTools::simplify(&doc,2,id));QVERIFY(!ObjectTools::removeObject(&doc,id));
        QVERIFY(!ObjectTools::eraseAt(&doc,{120,80},20));QVERIFY(!ObjectTools::rasterize(&doc));
        QCOMPARE(doc.project().toJson(),before);
        doc.setKeyframeSourceEditing(true);pose=active(doc).objects.first();pose.stroke=Qt::green;
        QVERIFY(!ObjectTools::updateObject(&doc,pose));QVERIFY(warnings.last().first().toString().contains("Source"));
        QCOMPARE(doc.project().toJson(),before);
        doc.setKeyframeSourceEditing(false);QVERIFY(doc.enablePathAnimation(layerId,id,false));
        QVERIFY(ObjectTools::simplify(&doc,2,id));QCOMPARE(active(doc).objects.first().nodes.size(),2);
    }
    void sampledBrushMorphRetainsResourcesAndReplaysAfterSave() {
        Document doc;doc.load(project());BrushSettings brush;brush.size=16;brush.spacing=.2;brush.pressureSize=false;
        brush.tipMask=QImage(9,7,QImage::Format_Grayscale8);brush.tipMask.fill(180);
        brush.texture.enabled=true;brush.texture.pattern=QImage(5,5,QImage::Format_Grayscale8);brush.texture.pattern.fill(120);
        brush.scatter=.2;brush.color=Qt::blue;doc.setBrushRandomSeed(31);
        QVERIFY(doc.beginStroke({40,80},1,brush,false));doc.continueStroke({120,80},1);doc.continueStroke({200,80},1);doc.endStroke();
        const auto source=active(doc).objects.first();QVERIFY(source.brushStroke.has_value());const auto layerId=active(doc).id;
        QVERIFY(doc.enablePathAnimation(layerId,source.id));QVERIFY(doc.setCurrentFrame(10));
        auto pose=evaluated(doc,source.id);pose.stroke=Qt::red;pose.strokeWidth=22;
        for(auto &node:pose.nodes){node.anchor.ry()+=40;node.inControl.ry()+=40;node.outControl.ry()+=40;}
        QVERIFY(ObjectTools::updateObject(&doc,pose));QCOMPARE(active(doc).objects.first(),source);
        QTemporaryDir temporary;QString error;const auto file=temporary.filePath("morph.toonproj");
        QVERIFY2(saveProject(doc.project(),file,&error),qPrintable(error));const auto loaded=loadProject(file,&error);QVERIFY2(loaded.has_value(),qPrintable(error));
        QCOMPARE(loaded->shots.first().layers.first().objects.first().brushStroke,source.brushStroke);
        QCOMPARE(loaded->shots.first().layerAnimations,doc.project().shots.first().layerAnimations);
        const auto first=renderMovieFrame(*loaded,0),middle=renderMovieFrame(*loaded,5),last=renderMovieFrame(*loaded,10);
        QVERIFY(!first.isNull());QVERIFY(first!=middle);QVERIFY(middle!=last);QCOMPARE(middle,renderMovieFrame(doc.project(),5));
        QCOMPARE(renderMovieFrame(*loaded,10),last);
        const auto before=doc.project().toJson();auto oversized=pose;oversized.strokeWidth=2048;oversized.transform.scale(100,100);
        QSignalSpy warnings(&doc,&Document::warning);QVERIFY(!ObjectTools::updateObject(&doc,oversized));
        QVERIFY(!warnings.isEmpty());QCOMPARE(doc.project().toJson(),before);
    }
    void eraserUsesVisiblePoseWithoutErasingItsSourcePosition() {
        Document doc;doc.load(project());const auto id=addPath(doc);const auto layerId=active(doc).id;
        QVERIFY(doc.enablePathAnimation(layerId,id));QVERIFY(doc.setCurrentFrame(10));
        auto pose=evaluated(doc,id);pose.transform.translate(0,70);QVERIFY(ObjectTools::updateObject(&doc,pose));
        const auto before=doc.project().toJson();QSignalSpy warnings(&doc,&Document::warning);
        QVERIFY(!ObjectTools::eraseAt(&doc,{120,80},10));QVERIFY(warnings.isEmpty());QCOMPARE(doc.project().toJson(),before);
        QVERIFY(!ObjectTools::eraseAt(&doc,{120,150},10));QVERIFY(!warnings.isEmpty());QCOMPARE(doc.project().toJson(),before);
    }
    void canvasNodeEditUsesEvaluatedPoseAndParentTransforms() {
        Canvas canvas;canvas.resize(800,650);canvas.setProject(project(true));canvas.show();
        auto *doc=canvas.document();doc->selectLayer(1);const auto id=addPath(*doc);const auto layerId=active(*doc).id;
        const auto parent=active(*doc).parentId;
        QVERIFY(doc->enableKeyframeChannel(parent,KeyframeProperty::PositionX));QVERIFY(doc->setKeyframeValue(parent,KeyframeProperty::PositionX,20));
        QVERIFY(doc->enableKeyframeChannel(layerId,KeyframeProperty::PositionY));QVERIFY(doc->setKeyframeValue(layerId,KeyframeProperty::PositionY,12));
        QVERIFY(doc->enableKeyframeChannel(layerId,KeyframeProperty::Rotation));QVERIFY(doc->setKeyframeValue(layerId,KeyframeProperty::Rotation,12));
        QVERIFY(doc->enablePathAnimation(layerId,id));QVERIFY(doc->setCurrentFrame(10));
        canvas.setTool(Canvas::Tool::VectorNode);clickPath(canvas,{80,80});auto *tools=canvas.drawingTools();QCOMPARE(tools->selectedObjectId(),id);
        tools->setPathEditMode(DrawingToolController::PathEditMode::Nodes);
        const auto from=viewPoint(canvas,{120,80}),to=viewPoint(canvas,{120,110});const int history=doc->history()->count();
        tools->press(from,Qt::NoModifier);tools->move(to,Qt::NoModifier);tools->release(to,Qt::NoModifier);
        QCOMPARE(doc->history()->count(),history+1);QVERIFY(QLineF(tools->selectedObject()->nodes[1].anchor,QPointF(120,110)).length()<1e-8);
        QCOMPARE(active(*doc).objects.first().nodes[1].anchor,QPointF(120,80));
        QVERIFY(doc->setCurrentFrame(5));QVERIFY(QLineF(tools->selectedObject()->nodes[1].anchor,QPointF(120,95)).length()<1e-8);
        QSignalSpy changes(tools,&DrawingToolController::changed);QVERIFY(doc->setCurrentFrame(0));QVERIFY(!changes.isEmpty());
        QCOMPARE(tools->selectedObject()->nodes[1].anchor,QPointF(120,80));
    }
    void widthBrushAndPropertySlidersCreatePoseKeysWithoutResampling() {
        Canvas canvas;canvas.resize(800,650);canvas.setProject(project());canvas.show();auto *doc=canvas.document();
        const auto id=addPath(*doc);const auto layerId=active(*doc).id;const auto source=active(*doc).objects.first();
        QVERIFY(doc->enablePathAnimation(layerId,id));QVERIFY(doc->setCurrentFrame(8));
        canvas.setTool(Canvas::Tool::VectorWidth);auto *tools=canvas.drawingTools();tools->widthBrushRadius=25;tools->widthBrushStrength=.4;
        clickPath(canvas,{120,80});auto pose=evaluated(*doc,id);QCOMPARE(pose.nodes.size(),source.nodes.size());
        QVERIFY(pose.widthFactors[1]>1.2);QCOMPARE(active(*doc).objects.first(),source);
        canvas.setTool(Canvas::Tool::VectorNode);clickPath(canvas,{80,80});QCOMPARE(tools->selectedObjectId(),id);
        VectorEditPanel panel(&canvas);panel.resize(240,600);panel.show();
        auto *width=panel.findChild<ValueSlider*>("pathStrokeWidth");auto *slider=width->findChild<QSlider*>();
        const int history=doc->history()->count();slider->setSliderDown(true);slider->setValue(180);slider->setValue(220);
        QCOMPARE(doc->history()->count(),history);slider->setSliderDown(false);QCOMPARE(doc->history()->count(),history+1);
        QCOMPARE(tools->selectedObject()->strokeWidth,22.);QCOMPARE(active(*doc).objects.first(),source);
        QVERIFY(panel.findChild<QLabel*>("pathSelectionStatus")->text().contains("Animated path"));
        QVERIFY(!panel.findChild<QToolButton*>("pathSimplifySelected")->isEnabled());
        QVERIFY(doc->setCurrentFrame(0));QCOMPARE(tools->strokeWidth,10.);QCOMPARE(width->value(),10.);
    }
};
QTEST_MAIN(KeyframeMorphTests)
#include "test_keyframe_morph.moc"
