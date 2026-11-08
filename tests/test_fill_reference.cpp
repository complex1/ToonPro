#include "raster_tools.h"
#include "auto_actions.h"
#include "tool_catalog.h"
#include "tool_session.h"
#include "drawing_tool_controller.h"
#include "property_animation.h"
#include "reference3d_render.h"
#include "tool_catalog_legacy_fixture.h"
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QUuid>
#include <QtTest>
#include <algorithm>

using namespace toon;
namespace {
Project blank() {
    auto p=Project::create(ProjectKind::DrawingOnly);p.width=p.height=96;p.transparent=true;p.shots[0].sheet.overscan=0;return p;
}
void image(RasterLayer &layer,const std::function<void(QPainter&)> &paint) {
    QImage tile(tileSize,tileSize,QImage::Format_ARGB32_Premultiplied);tile.fill(Qt::transparent);
    {QPainter painter(&tile);paint(painter);}layer.tiles[{0,0}]=tile;
}
RasterLayer ink(const QString &name="Ink") {
    auto layer=RasterLayer::create(name);
    image(layer,[](QPainter &p){p.setPen(QPen(Qt::black,2));p.setBrush(Qt::NoBrush);p.drawRect(20,20,56,56);});return layer;
}
Project outlined() {auto p=blank();auto line=ink();line.fillReference=true;p.shots[0].layers.append(line);return p;}
ReferenceScene referenceQuad() {
    ReferenceAsset asset;asset.name="Fill reference quad";ReferenceMaterial material;material.baseColor=Qt::red;asset.materials.append(material);
    ReferencePrimitive primitive;primitive.materialIndex=0;primitive.indices={0,2,1,0,3,2};
    for(const auto &position:QVector<QVector3D>{{-.5f,.5f,0},{.5f,.5f,0},{.5f,-.5f,0},{-.5f,-.5f,0}}){ReferenceVertex vertex;vertex.position=position;vertex.normal={0,0,1};primitive.vertices.append(vertex);}
    ReferenceMesh mesh;mesh.primitives.append(primitive);asset.meshes.append(mesh);ReferenceNode node;node.meshIndex=0;asset.nodes.append(node);
    ReferenceInstance instance;instance.id="fill-reference-quad";instance.asset=finalizeReferenceAsset(std::move(asset));ReferenceScene scene;scene.instances={instance};scene.camera.target={0,0,0};scene.camera.yaw=0;scene.camera.pitch=0;scene.camera.distance=3;scene.camera.orthographic=true;scene.camera.orthoScale=2;scene.light.intensity=0;scene.light.ambientIntensity=1;return scene;
}
QColor pixel(const Document &doc,int x,int y,int index=0) {
    const auto tile=doc.activeShot().layers[index].tiles.value({x/tileSize,y/tileSize});return tile.isNull()?QColor(Qt::transparent):tile.pixelColor(x%tileSize,y%tileSize);
}
RasterFillSettings marked() {RasterFillSettings settings;settings.source=RasterSampleSource::MarkedReferences;settings.tolerance=0;return settings;}
QString id(){return QUuid::createUuid().toString(QUuid::WithoutBraces);}
QByteArray read(const QString &path){QFile f(path);return f.open(QIODevice::ReadOnly)?f.readAll():QByteArray{};}
bool write(const QString &path,const QByteArray &bytes){QFile f(path);return f.open(QIODevice::WriteOnly)&&f.write(bytes)==bytes.size();}
}

class FillReferenceTest final:public QObject {
    Q_OBJECT
private slots:
    void batchMarksAllowFrozenSourcesAndHaveAtomicUndo() {
        auto p=outlined();p.shots[0].layers[1].fillReference=false;p.shots[0].layers[1].locked=true;p.shots[0].layers[1].visible=false;
        Document doc;doc.load(p);const auto paint=doc.activeShot().layers[0].id,line=doc.activeShot().layers[1].id;
        QVERIFY(doc.setFillReferences({paint,line,line},true));QCOMPARE(doc.history()->count(),1);
        QVERIFY(doc.activeShot().layers[0].fillReference);QVERIFY(doc.activeShot().layers[1].fillReference);
        QVERIFY(doc.activeShot().layers[1].locked);QVERIFY(!doc.activeShot().layers[1].visible);
        QVERIFY(doc.setFillReferences({paint,line},true));QCOMPARE(doc.history()->count(),1);
        QVERIFY(!doc.setFillReferences({paint,"missing"},false));QVERIFY(doc.activeShot().layers[0].fillReference);
        doc.history()->undo();QVERIFY(!doc.activeShot().layers[0].fillReference);QVERIFY(!doc.activeShot().layers[1].fillReference);
        doc.history()->redo();QVERIFY(doc.activeShot().layers[1].fillReference);
        QVERIFY(doc.beginLayerPropertyEdit());QVERIFY(!doc.setFillReferences({line},false));doc.cancelLayerPropertyEdit();
    }
    void marksRoundTripWithFormat43AndKeepPixelsAndOutline() {
        auto p=outlined();auto &layer=p.shots[0].layers[1];layer.effects.outlineEnabled=true;layer.effects.outlineWidth=4.5;
        const auto source=layer.tiles;QTemporaryDir temp;QString error;const auto file=temp.filePath("reference.toon");
        QVERIFY2(saveProject(p,file,&error),qPrintable(error));QVERIFY(read(file).startsWith("TOONPRO43\n"));
        const auto restored=loadProject(file,&error);QVERIFY2(restored.has_value(),qPrintable(error));
        QVERIFY(restored->shots[0].layers[1].fillReference);QCOMPARE(restored->shots[0].layers[1].tiles,source);
        QCOMPARE(restored->shots[0].layers[1].effects,layer.effects);QCOMPARE(restored->toJson()["version"].toInt(),43);
        p.shots[0].layers[1].fillReference=false;QCOMPARE(p.toJson()["version"].toInt(),42);
        p.shots[0].layers[1].effects={};QVERIFY(p.toJson()["version"].toInt()<41);
    }
    void malformedOrOlderMarkMetadataIsRejectedAndOldDefaultsStayUnmarked() {
        auto p=outlined();auto manifest=p.toJson();QString error;manifest["version"]=42;
        QVERIFY(!Project::fromJson(manifest,&error));QVERIFY(error.contains("format 43"));
        manifest=p.toJson();auto shots=manifest["shots"].toArray();auto shot=shots[0].toObject();auto layers=shot["layers"].toArray();auto line=layers[1].toObject();line["fillReference"]="true";layers[1]=line;shot["layers"]=layers;shots[0]=shot;manifest["shots"]=shots;
        QVERIFY(!Project::fromJson(manifest,&error));
        p.shots[0].layers[1].fillReference=false;manifest=p.toJson();const auto restored=Project::fromJson(manifest,&error);QVERIFY(restored);QVERIFY(!restored->shots[0].layers[1].fillReference);
        QVERIFY(!manifest["shots"].toArray()[0].toObject()["layers"].toArray()[1].toObject().contains("fillReference"));
    }
    void missingAndEmptyReferencesCannotPaintOrReplaceSelection() {
        Document doc;doc.load(blank());const auto before=doc.project().toJson();QVERIFY(!RasterTools::referenceSourceReason(&doc).isEmpty());
        QVERIFY(!RasterTools::bucket(&doc,{40,40},Qt::red,marked()));QVERIFY(!RasterTools::selectColor(&doc,{40,40},marked(),true));
        QCOMPARE(doc.project().toJson(),before);QCOMPARE(doc.history()->count(),0);QVERIFY(!doc.rasterSelection().active);
        QVERIFY(doc.setFillReferences({doc.activeShot().layers[0].id},true));const int history=doc.history()->count();
        QVERIFY(!RasterTools::bucket(&doc,{40,40},Qt::red,marked()));QCOMPARE(doc.history()->count(),history);QVERIFY(doc.activeShot().layers[0].tiles.isEmpty());
        QPainterPath selected;selected.addRect(30,30,20,20);doc.setRasterSelection(selected);
        QVERIFY(!RasterTools::bucket(&doc,{40,40},Qt::red,marked()));QVERIFY(doc.activeShot().layers[0].tiles.isEmpty());QCOMPARE(doc.rasterSelection().path,selected);QCOMPARE(doc.history()->count(),history);
        QVERIFY(!RasterTools::selectColor(&doc,{40,40},marked(),true));QCOMPARE(doc.rasterSelection().path,selected);doc.clearRasterSelection();
        QVERIFY(doc.addAdjustmentLayer());QVERIFY(doc.setFillReferences({doc.activeShot().layers[0].id},false));QVERIFY(doc.setFillReferences({doc.activeShot().layers[doc.activeLayer()].id},true));QVERIFY(!RasterTools::referenceSourceReason(&doc).isEmpty());
    }
    void multipleHiddenFrozenMarksSupplyOnlyBoundariesToTheColorLayer() {
        auto p=blank();auto sides=RasterLayer::create("Sides"),caps=RasterLayer::create("Caps"),unrelated=RasterLayer::create("Do not sample");
        image(sides,[](QPainter &q){q.fillRect(20,20,2,58,Qt::black);q.fillRect(76,20,2,58,Qt::black);});
        image(caps,[](QPainter &q){q.fillRect(20,20,58,2,Qt::black);q.fillRect(20,76,58,2,Qt::black);});
        image(unrelated,[](QPainter &q){q.fillRect(48,0,2,96,Qt::black);});
        sides.fillReference=caps.fillReference=true;sides.visible=caps.visible=false;sides.locked=caps.locked=true;sides.opacity=caps.opacity=.15;
        p.shots[0].layers+={sides,caps,unrelated};Document doc;doc.load(p);
        QVERIFY(RasterTools::referenceSourceReason(&doc).isEmpty());QVERIFY(RasterTools::bucket(&doc,{30,40},Qt::red,marked()));
        QCOMPARE(pixel(doc,30,40),QColor(Qt::red));QCOMPARE(pixel(doc,60,40),QColor(Qt::red));QCOMPARE(pixel(doc,5,40).alpha(),0);
        QCOMPARE(doc.activeShot().layers[1].tiles,sides.tiles);QCOMPARE(doc.activeShot().layers[2].tiles,caps.tiles);QCOMPARE(doc.activeShot().layers[3].tiles,unrelated.tiles);
        QCOMPARE(doc.history()->count(),1);doc.history()->undo();QVERIFY(doc.activeShot().layers[0].tiles.isEmpty());doc.history()->redo();QCOMPARE(pixel(doc,60,40),QColor(Qt::red));
    }
    void aMarkedFolderIsSampledOnceAndKeepsItsChildVisibility() {
        auto p=blank();auto folder=RasterLayer::create("Reference folder"),child=RasterLayer::create("Half ink"),hidden=RasterLayer::create("Hidden child");
        folder.group=true;folder.fillReference=true;folder.visible=false;folder.opacity=.2;
        child.parentId=hidden.parentId=folder.id;child.fillReference=hidden.fillReference=true;hidden.visible=false;
        image(child,[](QPainter &q){q.fillRect(10,10,10,10,QColor(255,0,0,128));});image(hidden,[](QPainter &q){q.fillRect(30,30,10,10,Qt::green);});
        p.shots[0].layers+={folder,child,hidden};Document doc;doc.load(p);
        QCOMPARE(RasterTools::sample(&doc,{15,15},RasterSampleSource::MarkedReferences),QColor(255,0,0,128));
        QCOMPARE(RasterTools::sample(&doc,{35,35},RasterSampleSource::MarkedReferences).alpha(),0);
    }
    void referenceIsolationRetainsMasksWhileIgnoringRootClipping() {
        auto p=blank();auto source=RasterLayer::create("Masked reference");source.fillReference=true;source.clipped=true;source.blendMode=LayerBlendMode::Multiply;
        image(source,[](QPainter &q){q.fillRect(0,0,96,96,Qt::red);});LayerMask mask;QImage coverage(tileSize,tileSize,QImage::Format_Grayscale8);coverage.fill(0);
        for(int y=0;y<96;++y)std::fill_n(coverage.scanLine(y),48,uchar(255));mask.tiles[{0,0}]=coverage;source.mask=mask;p.shots[0].layers.append(source);
        Document doc;doc.load(p);QCOMPARE(RasterTools::sample(&doc,{20,40},RasterSampleSource::MarkedReferences),QColor(Qt::red));QCOMPARE(RasterTools::sample(&doc,{70,40},RasterSampleSource::MarkedReferences).alpha(),0);
        QVERIFY(RasterTools::bucket(&doc,{20,40},Qt::blue,marked()));QCOMPARE(pixel(doc,20,40),QColor(Qt::blue));QCOMPARE(pixel(doc,70,40).alpha(),0);QCOMPARE(doc.activeShot().layers[1].mask,source.mask);QCOMPARE(doc.activeShot().layers[1].tiles,source.tiles);
    }
    void hiddenVectorReferenceKeepsTheUnmarkedParentsWorldTransform() {
        auto p=blank();auto parent=RasterLayer::create("Moved parent"),vector=RasterLayer::create("Vector ink");parent.group=true;parent.visible=false;parent.opacity=.1;
        vector.kind=LayerKind::Vector;vector.parentId=parent.id;vector.fillReference=true;vector.visible=false;
        auto object=EditableObject::create();object.closed=true;object.stroke=QColor();object.fill=Qt::black;
        object.nodes={BezierNode::corner({20,0}),BezierNode::corner({22,0}),BezierNode::corner({22,96}),BezierNode::corner({20,96})};vector.objects={object};
        p.shots[0].layers+={parent,vector};LayerAnimation pose;pose.layerId=parent.id;pose.channels={PropertyChannel{KeyframeProperty::PositionX,{{0,20}}}};p.shots[0].layerAnimations={pose};
        QVERIFY2(p.validate().isEmpty(),qPrintable(p.validate()));Document doc;doc.load(p);doc.setKeyframeSourceEditing(false);
        QCOMPARE(RasterTools::sample(&doc,{40,40},RasterSampleSource::MarkedReferences),QColor(Qt::black));
        QCOMPARE(RasterTools::sample(&doc,{20,40},RasterSampleSource::MarkedReferences).alpha(),0);
        QVERIFY(doc.activeShot().layers[2].tiles.isEmpty());QCOMPARE(doc.activeShot().layers[2].objects,vector.objects);
    }
    void croppedSelectionsAndWandUseTheSharedMarkedSource() {
        Document doc;doc.load(outlined());QPainterPath region;region.addRect(30,30,12,12);doc.setRasterSelection(region);
        QVERIFY(RasterTools::bucket(&doc,{35,35},Qt::blue,marked()));QCOMPARE(pixel(doc,35,35),QColor(Qt::blue));QCOMPARE(pixel(doc,45,45).alpha(),0);
        doc.clearRasterSelection();QVERIFY(RasterTools::selectColor(&doc,{40,40},marked(),true));QVERIFY(doc.rasterSelection().path.contains({40,40}));QVERIFY(!doc.rasterSelection().path.contains({5,5}));
        QCOMPARE(doc.activeShot().layers[1].tiles,outlined().shots[0].layers[1].tiles);
    }
    void legacyExplicitReferenceKeepsItsMeaningAndValues() {
        QCOMPARE(int(RasterSampleSource::ActiveLayer),0);QCOMPARE(int(RasterSampleSource::AllLayers),1);QCOMPARE(int(RasterSampleSource::ReferenceLayer),2);QCOMPARE(int(RasterSampleSource::MarkedReferences),3);
        auto p=outlined();p.shots[0].layers[1].fillReference=false;p.shots[0].layers[1].visible=false;Document doc;doc.load(p);
        auto settings=marked();settings.source=RasterSampleSource::ReferenceLayer;settings.referenceLayerId=p.shots[0].layers[1].id;
        QVERIFY(RasterTools::bucket(&doc,{40,40},Qt::red,settings));QCOMPARE(pixel(doc,40,40),QColor(Qt::red));QCOMPARE(pixel(doc,5,5).alpha(),0);
    }
    void nativeReferenceImagesAreSampledByAllExplicitAndMarkedModes() {
        const auto scene=referenceQuad();QVERIFY(scene.instances[0].asset);QString error;const auto expected=renderReferenceLayer(scene,{96,96},&error);QVERIFY2(!expected.isNull(),qPrintable(error));QCOMPARE(expected.pixelColor(48,48),QColor(Qt::red));
        const auto original=serializeReferenceScene(scene);
        // The second pass forces the effects compositor through an empty
        // effect-bearing paint layer; both compositors need the frame size.
        for(bool effects:{false,true}) {
            auto p=blank();p.shots[0].layers[0].effects.glowEnabled=effects;auto reference=RasterLayer::create("Native reference");reference.kind=LayerKind::Reference3D;reference.reference3d=scene;reference.fillReference=true;p.shots[0].layers.append(reference);
            Document doc;doc.load(p);
            for(auto source:{RasterSampleSource::AllLayers,RasterSampleSource::ReferenceLayer,RasterSampleSource::MarkedReferences}) {
                QCOMPARE(RasterTools::sample(&doc,{48,48},source,reference.id),expected.pixelColor(48,48));QCOMPARE(RasterTools::sample(&doc,{2,2},source,reference.id),expected.pixelColor(2,2));
                auto settings=marked();settings.source=source;settings.referenceLayerId=reference.id;
                QVERIFY(RasterTools::bucket(&doc,{48,48},Qt::blue,settings));QCOMPARE(pixel(doc,48,48),QColor(Qt::blue));QCOMPARE(pixel(doc,2,2).alpha(),0);
                QCOMPARE(serializeReferenceScene(*doc.activeShot().layers[1].reference3d),original);QVERIFY(doc.activeShot().layers[1].tiles.isEmpty());doc.history()->undo();QVERIFY(doc.activeShot().layers[0].tiles.isEmpty());
            }
            p.shots[0].layers[1].visible=false;doc.load(p);QCOMPARE(RasterTools::sample(&doc,{48,48},RasterSampleSource::AllLayers).alpha(),0);QCOMPARE(RasterTools::sample(&doc,{48,48},RasterSampleSource::ReferenceLayer,reference.id),QColor(Qt::red));QCOMPARE(RasterTools::sample(&doc,{48,48},RasterSampleSource::MarkedReferences),QColor(Qt::red));
        }
    }
    void markedActionsUseTheReplayDocumentsReferencesAndPersistAsVersion3() {
        QTemporaryDir temp;Document doc;doc.load(outlined());AutoActions actions(&doc,temp.filePath("actions.json"));const auto action=actions.create("Marked fill");QVERIFY(actions.startRecording(action));
        QVERIFY(RasterTools::bucket(&doc,{40,40},Qt::red,marked()));QVERIFY(actions.recording());actions.stopRecording();QCOMPARE(actions.action(action)->steps.size(),1);
        const auto step=actions.action(action)->steps[0];QCOMPARE(step.parameters["source"].toInt(),3);QVERIFY(!step.parameters.contains("referenceOffset"));
        QCOMPARE(QJsonDocument::fromJson(read(actions.path())).object()["version"].toInt(),3);
        AutoActions reopened(&doc,actions.path());QVERIFY(reopened.load());doc.load(outlined());QVERIFY(reopened.play(action));QCOMPARE(pixel(doc,40,40),QColor(Qt::red));QCOMPARE(doc.history()->count(),1);
        doc.load(blank());QVERIFY(!reopened.play(action));QCOMPARE(doc.history()->count(),0);QVERIFY(doc.activeShot().layers[0].tiles.isEmpty());
        auto malformed=QJsonDocument::fromJson(read(actions.path())).object();malformed["version"]=2;QVERIFY(write(actions.path(),QJsonDocument(malformed).toJson()));AutoActions oldVersion(&doc,actions.path());QVERIFY(!oldVersion.load());
    }
    void activeReferenceMarkCanBeRecordedAndReplayOnLockedInk() {
        QTemporaryDir temp;Document doc;auto p=outlined();p.shots[0].layers[1].fillReference=false;p.shots[0].layers[1].locked=true;doc.load(p);doc.selectLayer(1);
        AutoActions actions(&doc,temp.filePath("marks.json"));const auto action=actions.create("Mark ink");QVERIFY(actions.startRecording(action));QVERIFY(doc.setFillReferences({doc.activeShot().layers[1].id},true));QVERIFY(actions.recording());actions.stopRecording();
        QCOMPARE(actions.action(action)->steps.size(),1);QVERIFY(actions.action(action)->steps[0].parameters["fillReference"].toBool());
        doc.load(p);doc.selectLayer(1);QVERIFY(actions.play(action));QVERIFY(doc.activeShot().layers[1].fillReference);QVERIFY(doc.activeShot().layers[1].locked);
    }
    void markedToolStateRoundTripsAndRejectsDisguisedCatalogs() {
        QTemporaryDir temp;BrushLibrary library(temp.filePath("brushes.json"));QVERIFY(library.load());ToolCatalog catalog(&library,temp.filePath("tools.json"));QVERIFY(catalog.load());
        const auto toolId=catalog.activeToolId();auto state=catalog.tool(toolId)->state;state.options["sampleSource"]=3;QVERIFY(catalog.updateToolState(toolId,state));QVERIFY(catalog.save());
        auto root=QJsonDocument::fromJson(read(catalog.path())).object();QCOMPARE(root["version"].toInt(),14);
        ToolCatalog reopened(&library,catalog.path());QVERIFY(reopened.load());QCOMPARE(reopened.tool(toolId)->state.options["sampleSource"].toInt(),3);
        Canvas canvas;canvas.setProject(blank());ToolSession session(&canvas,&reopened);QCOMPARE(canvas.drawingTools()->fillSettings.source,RasterSampleSource::MarkedReferences);
        state.options["sampleSource"]=4;QVERIFY(!reopened.updateToolState(toolId,state));
        root=test_fixture::toolCatalogBefore14(root);QVERIFY(write(catalog.path(),QJsonDocument(root).toJson()));ToolCatalog compatible13(&library,catalog.path());QString error;QVERIFY2(compatible13.load(&error),qPrintable(error));QCOMPARE(compatible13.tool(toolId)->state.options["sampleSource"].toInt(),3);
        root["version"]=12;QVERIFY(write(catalog.path(),QJsonDocument(root).toJson()));ToolCatalog oldVersion(&library,catalog.path());QVERIFY(!oldVersion.load(&error));QVERIFY(error.contains("version 13"));
    }
    void duplicateMergeAndFlattenRetainMarksDeliberately() {
        Document doc;doc.load(outlined());const auto line=doc.activeShot().layers[1].id;doc.duplicateLayers({line});QCOMPARE(doc.activeShot().layers.size(),3);QVERIFY(doc.activeShot().layers[2].fillReference);
        doc.history()->undo();doc.selectLayer(1);QVERIFY(doc.mergeDown());QCOMPARE(doc.activeShot().layers.size(),1);QVERIFY(doc.activeShot().layers[0].fillReference);
        doc.history()->undo();QVERIFY(doc.activeShot().layers[1].fillReference);QVERIFY(!doc.activeShot().layers[0].fillReference);QVERIFY(doc.flattenImage());QVERIFY(doc.activeShot().layers[0].fillReference);
    }
    void marksOnlySampleTheCurrentExposure() {
        auto p=blank();auto trackRoot=RasterLayer::create("Track"),aGroup=RasterLayer::create("A"),bGroup=RasterLayer::create("B");trackRoot.group=aGroup.group=bGroup.group=true;aGroup.parentId=bGroup.parentId=trackRoot.id;
        auto a=ink("A ink"),b=ink("B ink");a.parentId=aGroup.id;b.parentId=bGroup.id;a.fillReference=true;
        p.shots[0].durationFrames=4;p.shots[0].layers+={trackRoot,aGroup,a,bGroup,b};
        AnimationDrawing aDrawing{id(),aGroup.id,{{"ink",a.id}}},bDrawing{id(),bGroup.id,{{"ink",b.id}}};AnimationTrack track;track.id=id();track.rootGroupLayerId=trackRoot.id;track.drawingTemplate={{"ink","Ink"}};track.drawings={aDrawing,bDrawing};track.exposures={{id(),aDrawing.id,0,2},{id(),bDrawing.id,2,2}};p.shots[0].animationTracks={track};
        QVERIFY2(p.validate().isEmpty(),qPrintable(p.validate()));Document doc;doc.load(p);QVERIFY(RasterTools::referenceSourceReason(&doc).isEmpty());QCOMPARE(RasterTools::sample(&doc,{20,40},RasterSampleSource::MarkedReferences),QColor(Qt::black));
        QVERIFY(doc.setCurrentFrame(1));QVERIFY(RasterTools::referenceSourceReason(&doc).isEmpty());QVERIFY(doc.setCurrentFrame(2));QVERIFY(!RasterTools::referenceSourceReason(&doc).isEmpty());QVERIFY(!RasterTools::sample(&doc,{20,40},RasterSampleSource::MarkedReferences).isValid());
    }
};
QTEST_MAIN(FillReferenceTest)
#include "test_fill_reference.moc"
