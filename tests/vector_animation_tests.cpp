#include "document.h"
#include "canvas.h"
#include "layer_panel.h"
#include "drawing_tool_controller.h"
#include "object_tools.h"
#include "animation_render.h"
#include "sheet.h"
#include "animation_test_fixture.h"
#include <QCryptographicHash>
#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QToolButton>
#include <QtTest>
using namespace toon;
namespace {
const RasterLayer &active(const Document &document){return document.project().shots[0].layers[document.activeLayer()];}
Project blank(){auto p=Project::create();p.width=p.height=128;p.transparent=true;p.shots[0].sheet.overscan=0;return p;}
void stroke(Document &document){
    BrushSettings brush; brush.size=15; brush.pressureSize=false; brush.spacing=.2; brush.color=Qt::red;
    brush.tipMask=QImage(9,7,QImage::Format_Grayscale8); brush.tipMask.fill(255);
    QVERIFY(document.beginStroke({25,40},1,brush,false)); document.continueStroke({75,40},1); document.endStroke();
}
bool writeContainer(const QString &path,QJsonObject metadata,int version){
    metadata["version"]=version; QFile file(path); if(!file.open(QIODevice::WriteOnly))return false;
    if(version==1)return file.write(QJsonDocument(metadata).toJson())>0;
    file.write(QByteArray("TOONPRO")+QByteArray::number(version)+'\n'); QDataStream stream(&file); stream.setByteOrder(QDataStream::BigEndian);
    const auto data=QJsonDocument(metadata).toJson(QJsonDocument::Compact),hash=QCryptographicHash::hash(data,QCryptographicHash::Sha256);
    stream<<quint32(data.size()); stream.writeRawData(hash.constData(),hash.size()); stream.writeRawData(data.constData(),data.size()); return stream.status()==QDataStream::Ok;
}
}
class VectorAnimationTests : public QObject {
    Q_OBJECT
private slots:
    void vectorBrushesDuplicateAndIndependentBlankDrawings() {
        Document document; document.load(blank()); const QVector<DrawingTemplateSlot> recipe{{"ink","Ink",LayerKind::Vector},{"paint","Color",LayerKind::Raster}};
        QVERIFY(document.addAnimationTrack("Character",recipe)); QCOMPARE(active(document).kind,LayerKind::Vector); stroke(document);
        const auto source=active(document); QCOMPARE(source.objects.size(),1); QVERIFY(source.objects[0].brushStroke); QVERIFY(!source.tiles.isEmpty());
        QVERIFY(document.setAnimationExposureTiming(0,3)); QVERIFY(document.createAnimationDrawing(true)); QCOMPARE(document.currentFrame(),3);
        const auto copy=active(document); QCOMPARE(copy.kind,LayerKind::Vector); QCOMPARE(copy.objects.size(),1); QVERIFY(copy.objects[0].id!=source.objects[0].id);
        QCOMPARE(copy.objects[0].brushStroke,source.objects[0].brushStroke); QCOMPARE(copy.tiles,source.tiles);
        auto edited=copy.objects[0]; edited.stroke=Qt::blue; edited.strokeWidth=22; QVERIFY(ObjectTools::updateObject(&document,edited));
        const int originalIndex=animation_fixture::layerIndex(document.project().shots[0],source.id);
        QCOMPARE(document.project().shots[0].layers[originalIndex].objects[0].stroke,QColor(Qt::red));
        document.history()->undo(); QCOMPARE(active(document).tiles,source.tiles); document.history()->redo(); QCOMPARE(active(document).objects[0].stroke,QColor(Qt::blue));
        QVERIFY(document.createAnimationDrawing()); QCOMPARE(document.currentFrame(),4); QCOMPARE(active(document).kind,LayerKind::Vector); QVERIFY(active(document).objects.isEmpty());
        QVERIFY(document.setCurrentFrame(1)); QCOMPARE(active(document).id,source.id); QCOMPARE(active(document).kind,LayerKind::Vector);
        QVERIFY2(document.project().validate().isEmpty(),qPrintable(document.project().validate()));
        document.history()->undo(); QCOMPARE(document.project().shots[0].animationTracks[0].drawings.size(),2);
        document.history()->redo(); QCOMPARE(document.project().shots[0].animationTracks[0].drawings.size(),3);
    }
    void objectLayerToolsRespectCelOwnershipAndKinds() {
        Document document; document.load(blank()); QVERIFY(document.addAnimationTrack("Character",DrawingTemplate::Simple));
        QVERIFY(ObjectTools::addLayer(&document,LayerKind::Vector));
        const auto id=ObjectTools::addPath(&document,{BezierNode::corner({20,20}),BezierNode::corner({70,70})},false,Qt::green,5);
        QVERIFY(!id.isEmpty()); QVERIFY(!animationOwner(document.project().shots[0],active(document).id).drawingId.isEmpty());
        const auto before=document.project().toJson(); QVERIFY(!ObjectTools::addLayer(&document,LayerKind::Text)); QVERIFY(!ObjectTools::addLayer(&document,LayerKind::Scene3D)); QCOMPARE(document.project().toJson(),before);
        auto invalid=document.project(); invalid.shots[0].layers[document.activeLayer()].annotation=true; QVERIFY(!invalid.validate().isEmpty());
        QVERIFY(!document.addAnimationTrack("Bad",QVector<DrawingTemplateSlot>{{"text","Text",LayerKind::Text}})); QCOMPARE(document.project().toJson(),before);
        Document conversion; auto p=blank(); p.shots[0].layers[0].kind=LayerKind::Vector; conversion.load(p); stroke(conversion);
        const auto original=active(conversion); QVERIFY(conversion.convertLayerToAnimation("Motion")); QCOMPARE(conversion.project().shots[0].animationTracks[0].drawingTemplate[0].kind,LayerKind::Vector);
        QCOMPARE(active(conversion).objects,original.objects); QVERIFY(conversion.createAnimationDrawing()==false); // Initial exposure fills the shot.
        QVERIFY(conversion.setAnimationExposureTiming(0,1)); QVERIFY(conversion.createAnimationDrawing()); QCOMPARE(active(conversion).kind,LayerKind::Vector);
    }
    void canvasAndLayerPanelAllowVectorCelsButKeepEmptyFramesBlocked() {
        Canvas canvas; canvas.resize(600,480); canvas.setProject(blank()); canvas.show();
        QVERIFY(QTest::qWaitForWindowExposed(&canvas)); auto *document=canvas.document();
        QVERIFY(document->addAnimationTrack("Character",DrawingTemplate::Simple)); LayerPanel panel(document);
        auto *add=panel.findChild<QToolButton*>("addVectorLayer"); QVERIFY(add); QVERIFY(add->isEnabled());
        const auto rasterId=active(*document).id; const auto rootId=canvas.shot().animationTracks[0].rootGroupLayerId;
        canvas.setTool(Canvas::Tool::VectorPen); QVERIFY2(canvas.toolBlockReason().isEmpty(),qPrintable(canvas.toolBlockReason()));
        canvas.setTool(Canvas::Tool::Text); QVERIFY(canvas.toolBlockReason().contains("static layer"));
        add->click(); QCOMPARE(active(*document).kind,LayerKind::Vector); stroke(*document);
        for(auto tool:{Canvas::Tool::Brush,Canvas::Tool::VectorPen,Canvas::Tool::VectorNode,Canvas::Tool::VectorSimplify,Canvas::Tool::VectorWidth,Canvas::Tool::VectorEraser}){
            canvas.setTool(tool); QVERIFY2(canvas.toolBlockReason().isEmpty(),qPrintable(canvas.toolBlockReason()));
        }
        // Path Edit uses the same editable source objects on an exposed cel.
        canvas.setTool(Canvas::Tool::VectorNode); const auto objectId=active(*document).objects.first().id;
        QVERIFY(canvas.drawingTools()->selectAnimatedPath(objectId)); QVERIFY(canvas.drawingTools()->selectedObject().has_value());
        canvas.drawingTools()->cancel();
        QVERIFY(document->setCurrentFrame(1)); QVERIFY(!add->isEnabled()); QCOMPARE(active(*document).id,rootId);
        canvas.setTool(Canvas::Tool::VectorPen); QVERIFY(canvas.toolBlockReason().contains("empty"));
        QVERIFY(document->setCurrentFrame(0)); document->selectLayer(animation_fixture::layerIndex(canvas.shot(),rasterId));
        QVERIFY(add->isEnabled()); canvas.setTool(Canvas::Tool::VectorPen); QVERIFY(canvas.toolBlockReason().isEmpty());
        document->setLayersLocked({rootId},true); QVERIFY(!add->isEnabled()); QVERIFY(!canvas.toolBlockReason().isEmpty());
    }
    void reopenOnionAndPngSequencePreserveEditableStrokes() {
        QTemporaryDir dir; Document document; document.load(blank()); QVERIFY(document.addAnimationTrack("Ink",QVector<DrawingTemplateSlot>{{"ink","Ink",LayerKind::Vector}})); stroke(document);
        const auto original=active(document); const auto first=renderMovieFrame(document.project(),0); QVERIFY(first.pixelColor(40,40).alpha()>0);
        QVERIFY(document.createAnimationDrawing()); const auto next=renderMovieFrame(document.project(),1); QCOMPARE(next.pixelColor(40,40).alpha(),0);
        const auto track=document.project().shots[0].animationTracks[0]; const auto ghost=onionSkinLayers(document.project().shots[0],1,track.id,-1,document.drawingBounds());
        bool found=false; for(const auto &layer:ghost)if(layer.id==original.id){found=true;QCOMPARE(layer.kind,LayerKind::Vector);QCOMPARE(layer.objects,original.objects);QCOMPARE(layer.tiles,original.tiles);} QVERIFY(found);
        QString error; const auto path=dir.filePath("vector-animation.toon"); QVERIFY2(saveProject(document.project(),path,&error),qPrintable(error));
        auto restored=loadProject(path,&error); QVERIFY2(restored,qPrintable(error)); QCOMPARE(restored->toJson(),document.project().toJson()); QCOMPARE(renderMovieFrame(*restored,0),first); QCOMPARE(renderMovieFrame(*restored,1),next);
        const auto restoredIndex=animation_fixture::layerIndex(restored->shots[0],original.id); QCOMPARE(restored->shots[0].layers[restoredIndex].objects,original.objects);
        const auto output=dir.filePath("sequence"); QVERIFY2(exportPngSequence(*restored,0,0,1,output,{},&error),qPrintable(error));
        const auto files=QDir(output).entryList({"*.png"},QDir::Files,QDir::Name); QCOMPARE(files.size(),2);
        QCOMPARE(QImage(QDir(output).filePath(files[0])).convertToFormat(first.format()),first); QCOMPARE(QImage(QDir(output).filePath(files[1])).convertToFormat(next.format()),next);
    }
    void legacyMigrationTypedMetadataAndStrictKinds() {
        QTemporaryDir dir; const auto path=dir.filePath("project.toon"); QString error;
        for(int version=1;version<=18;++version){
            auto metadata=blank().toJson(); if(version<15)metadata.remove("audioResources");
            QVERIFY(writeContainer(path,metadata,version)); auto restored=loadProject(path,&error); QVERIFY2(restored,qPrintable(QString("Version %1: %2").arg(version).arg(error)));
            QCOMPARE(restored->toJson()["version"].toInt(),19);
        }
        Document document;document.load(blank());QVERIFY(document.addAnimationTrack("Ink",QVector<DrawingTemplateSlot>{{"ink","Ink",LayerKind::Vector}}));
        auto json=document.project().toJson();json["version"]=18;QVERIFY(!Project::fromJson(json,&error));QVERIFY(error.contains("format 19"));
        auto shots=json["shots"].toArray();auto shot=shots[0].toObject();auto tracks=shot["animationTracks"].toArray();auto track=tracks[0].toObject();auto recipes=track["drawingTemplate"].toArray();auto slot=recipes[0].toObject();slot.remove("kind");recipes[0]=slot;track["drawingTemplate"]=recipes;tracks[0]=track;shot["animationTracks"]=tracks;shots[0]=shot;json["shots"]=shots;
        QVERIFY(!Project::fromJson(json,&error));QVERIFY(error.contains("Vector animation"));
        auto tracksJson=animationTracksToJson(document.project().shots[0].animationTracks); auto object=tracksJson[0].toObject();recipes=object["drawingTemplate"].toArray();slot=recipes[0].toObject();slot["kind"]="text";recipes[0]=slot;object["drawingTemplate"]=recipes;tracksJson[0]=object;
        QVector<AnimationTrack> parsed;QVERIFY(!animationTracksFromJson(tracksJson,&parsed,&error));
        auto future=blank().toJson(); future["version"]=latestProjectFormatVersion + 1; QVERIFY(!Project::fromJson(future,&error));
    }
};
QTEST_MAIN(VectorAnimationTests)
#include "vector_animation_tests.moc"
