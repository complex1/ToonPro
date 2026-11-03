#include "project.h"
#include "vector_brush.h"
#include "vector_brush_storage.h"
#include "sheet.h"
#include <QBuffer>
#include <QCryptographicHash>
#include <QDataStream>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QtTest>
#include <QUuid>

using namespace toon;
namespace {
QImage tip() {
    QImage image(17,19,QImage::Format_Grayscale8); image.fill(0);
    for(int y=2;y<17;++y) for(int x=3;x<14;++x) image.scanLine(y)[x]=uchar(40+(x*y)%215);
    return image;
}
EditableObject stroke() {
    auto object=EditableObject::create();
    object.nodes={BezierNode::corner({30,40}),BezierNode::corner({170,110})};
    object.stroke=QColor("#9060c0"); object.strokeWidth=19;
    object.opacity=.8; object.widthFactors={.65,1.3};
    VectorBrushStroke capture;
    capture.settings.size=19; capture.settings.color=object.stroke;
    capture.settings.backgroundColor=QColor("#336699"); capture.settings.tipMask=tip();
    capture.settings.texture.enabled=true; capture.settings.texture.pattern=tip();
    capture.settings.texture.depth=.6; capture.settings.texture.name="Saved texture";
    capture.settings.dual.enabled=true; capture.settings.dual.tipMask=tip().flipped(Qt::Vertical);
    capture.settings.dual.size=13; capture.settings.scatter=.1;
    capture.settings.angleDynamics.jitter=.3; capture.settings.colorDynamics.enabled=true;
    capture.settings.colorDynamics.hue=.1; capture.settings.opacity=.6;
    capture.settings.strokeOpacity=.7; capture.settings.noise=true;
    capture.seed=0xffffffff; capture.canvasSize={256,256};
    capture.sourceGeometry=editableGeometrySignature(object);
    capture.selection.addRect(20,20,190,150); capture.selection.setFillRule(Qt::WindingFill);
    capture.selectionActive=true; capture.selectionFeather=4;
    for(int i=0;i<24;++i) {
        const qreal t=i/23.; VectorBrushSample sample;
        sample.dabIndex=i; sample.point={30+140*t,40+70*t};
        sample.input={.2+.8*t,-25+30*t,8,350+20*t,.7};
        sample.direction=26.565051177; sample.initialDirection=sample.direction; sample.position=t;
        capture.samples.append(sample);
    }
    object.brushStroke=capture; return object;
}
Project project(bool rich=true) {
    auto p=Project::create(); p.width=p.height=256; p.transparent=true;
    auto &layer=p.shots[0].layers[0]; layer.kind=LayerKind::Vector;
    auto object=stroke();
    if(!rich) { object.brushStroke.reset(); object.opacity=1; object.widthFactors.clear(); }
    layer.objects.append(object); return p;
}
QJsonObject objectJson(const QJsonObject &manifest) {
    return manifest["shots"].toArray()[0].toObject()["layers"].toArray()[0].toObject()["objects"].toArray()[0].toObject();
}
QJsonObject replaceObject(QJsonObject manifest,const QJsonObject &object) {
    auto shots=manifest["shots"].toArray(); auto shot=shots[0].toObject(); auto layers=shot["layers"].toArray();
    auto layer=layers[0].toObject(); auto objects=layer["objects"].toArray(); objects[0]=object;
    layer["objects"]=objects; layers[0]=layer; shot["layers"]=layers; shots[0]=shot; manifest["shots"]=shots; return manifest;
}
bool writeContainer(const QString &path,QJsonObject manifest,const QVector<QImage> &resources={}) {
    QFile file(path); if(!file.open(QIODevice::WriteOnly)) return false;
    file.write(QByteArray("TOONPRO")+QByteArray::number(manifest["version"].toInt())+"\n");
    QDataStream stream(&file); stream.setByteOrder(QDataStream::BigEndian);
    auto blob=[&](const QByteArray &data) {
        stream<<quint32(data.size()); const auto hash=QCryptographicHash::hash(data,QCryptographicHash::Sha256);
        stream.writeRawData(hash.constData(),hash.size()); stream.writeRawData(data.constData(),data.size());
    };
    blob(QJsonDocument(manifest).toJson(QJsonDocument::Compact));
    for(const auto &image:resources) { QByteArray png; QBuffer buffer(&png); buffer.open(QIODevice::WriteOnly); if(!image.save(&buffer,"PNG")) return false; blob(png); }
    return stream.status()==QDataStream::Ok;
}
}

class VectorBrushStorageTest:public QObject {
    Q_OBJECT
private slots:
    void richBrushRoundTripsAndRebuildsWithoutLibrary() {
        auto p=project(); auto &layer=p.shots[0].layers[0]; QString error;
        QVERIFY2(rebuildObjectLayer(layer,{-120,-120,496,496},maxProjectTiles,&error),qPrintable(error));
        QVERIFY(!layer.tiles.isEmpty()); QVERIFY2(p.validate().isEmpty(),qPrintable(p.validate()));
        const auto manifest=p.toJson(); QCOMPARE(manifest["version"].toInt(),19);
        const auto metadata=Project::fromJson(manifest,&error); QVERIFY2(metadata.has_value(),qPrintable(error));
        QVERIFY(metadata->validate().contains("not been loaded"));
        QCOMPARE(manifest["brushResources"].toArray().size(),2); // Equal independent images deduplicate.
        QVERIFY(manifest["shots"].toArray()[0].toObject()["layers"].toArray()[0].toObject()["tiles"].toArray().isEmpty());
        QTemporaryDir directory; const auto path=directory.filePath("brush.toonproj");
        QVERIFY2(saveProject(p,path,&error),qPrintable(error));
        QFile file(path); QVERIFY(file.open(QIODevice::ReadOnly)); QCOMPARE(file.read(10),QByteArray("TOONPRO19\n")); file.close();
        const auto loaded=loadProject(path,&error); QVERIFY2(loaded.has_value(),qPrintable(error));
        QCOMPARE(loaded->toJson(),manifest); QCOMPARE(loaded->shots[0].layers[0].tiles,layer.tiles);
        const auto &restored=loaded->shots[0].layers[0].objects[0];
        QCOMPARE(restored.brushStroke->settings,layer.objects[0].brushStroke->settings);
        QCOMPARE(restored.brushStroke->sourceGeometry,layer.objects[0].brushStroke->sourceGeometry);
        QCOMPARE(restored.brushStroke->selection,layer.objects[0].brushStroke->selection);
        QCOMPARE(restored.brushStroke->samples[15].input,layer.objects[0].brushStroke->samples[15].input);
        QCOMPARE(restored.brushStroke->samples[15].dabIndex,quint64(15));
        QCOMPARE(renderMovieFrame(*loaded),renderMovieFrame(p));
        auto edited=*loaded; auto &object=edited.shots[0].layers[0].objects[0];
        object.stroke=Qt::blue; object.strokeWidth=25; object.widthFactors={1.8,.4}; object.transform.translate(5,3);
        QVERIFY(rebuildObjectLayer(edited.shots[0].layers[0],{-120,-120,496,496},maxProjectTiles,&error));
        QVERIFY(saveProject(edited,path,&error)); const auto reloaded=loadProject(path,&error); QVERIFY2(reloaded.has_value(),qPrintable(error));
        QCOMPARE(renderMovieFrame(*reloaded),renderMovieFrame(edited));
    }
    void emptyInactiveSelectionStaysEmptyAndReplaysIdentically() {
        auto p=project(); auto &layer=p.shots[0].layers[0]; auto &capture=*layer.objects[0].brushStroke;
        capture.selection=QPainterPath(); capture.selectionActive=false; capture.selectionFeather=0;
        QCOMPARE(capture.selection.elementCount(),0);
        const auto serialized=vectorBrushStrokeToJson(capture); QVERIFY(serialized["selection"].toArray().isEmpty());
        VectorBrushStroke restored; QString error;
        QVERIFY2(vectorBrushStrokeFromJson(serialized,&restored,&error),qPrintable(error));
        QCOMPARE(restored.selection.elementCount(),0); QVERIFY(!restored.selectionActive);
        QCOMPARE(vectorBrushStrokeToJson(restored)["selection"],serialized["selection"]);
        QVERIFY2(rebuildObjectLayer(layer,{-120,-120,496,496},maxProjectTiles,&error),qPrintable(error));
        const auto frame=renderMovieFrame(p); QVERIFY(!layer.tiles.isEmpty());
        QTemporaryDir directory; const auto path=directory.filePath("empty-selection.toon");
        QVERIFY2(saveProject(p,path,&error),qPrintable(error)); const auto loaded=loadProject(path,&error);
        QVERIFY2(loaded,qPrintable(error)); QCOMPARE(loaded->toJson(),p.toJson());
        QCOMPARE(loaded->shots[0].layers[0].objects[0].brushStroke->selection.elementCount(),0);
        QCOMPARE(loaded->shots[0].layers[0].tiles,layer.tiles); QCOMPARE(renderMovieFrame(*loaded),frame);
    }
    void oldVersionSevenObjectsMigrateWithoutBrushData() {
        auto p=project(false); auto manifest=p.toJson(); manifest["version"]=7; manifest.remove("brushResources");
        auto object=objectJson(manifest); object.remove("opacity"); object.remove("widthFactors"); manifest=replaceObject(manifest,object);
        QTemporaryDir directory; const auto path=directory.filePath("old.toonproj"); QString error;
        QVERIFY(writeContainer(path,manifest)); const auto loaded=loadProject(path,&error); QVERIFY2(loaded.has_value(),qPrintable(error));
        const auto &restored=loaded->shots[0].layers[0].objects[0]; QVERIFY(!restored.brushStroke);
        QCOMPARE(restored.opacity,1.); QVERIFY(restored.widthFactors.isEmpty()); QVERIFY(!loaded->shots[0].layers[0].tiles.isEmpty());
        QCOMPARE(loaded->toJson()["version"].toInt(),19);
        manifest=project().toJson(); manifest["version"]=7; QVERIFY(!Project::fromJson(manifest,&error));
    }
    void malformedSamplesAreRejectedBeforeRendering() {
        const auto valid=vectorBrushStrokeToJson(*stroke().brushStroke);
        QVector<QJsonObject> invalid;
        auto changed=valid; changed["seed"]=-1; invalid.append(changed);
        changed=valid; changed["sourceGeometry"]="invalid"; invalid.append(changed);
        changed=valid; changed["selectionActive"]=1; invalid.append(changed);
        changed=valid; changed["unknown"]=true; invalid.append(changed);
        changed=valid; changed["canvasSize"]=QJsonArray{999999,256}; invalid.append(changed);
        changed=valid; changed["selection"]=QJsonArray{QJsonArray{2,1,1}}; invalid.append(changed);
        changed=valid; changed["samples"]=QJsonArray{}; invalid.append(changed);
        for(int field:{0,2,3,7,9,10}) {
            changed=valid; auto samples=changed["samples"].toArray(); auto sample=samples[5].toArray();
            sample[field]=999999; samples[5]=sample; changed["samples"]=samples; invalid.append(changed);
        }
        changed=valid; auto samples=changed["samples"].toArray(); auto sample=samples[5].toArray();
        sample[10]=0; samples[5]=sample; changed["samples"]=samples; invalid.append(changed);
        for(const auto &json:invalid) { VectorBrushStroke result; QString error; QVERIFY(!vectorBrushStrokeFromJson(json,&result,&error)); QVERIFY(!error.isEmpty()); }
        auto p=project(); p.shots[0].layers[0].objects[0].brushStroke->samples[0].input.pressure=qQNaN(); QVERIFY(!p.validate().isEmpty());
    }
    void resourceDescriptorsAndReferencesAreStrictlyBounded() {
        const auto valid=project().toJson(); QString error;
        auto json=valid; json.remove("brushResources"); QVERIFY(!Project::fromJson(json,&error));
        json=valid; auto resources=json["brushResources"].toArray(); auto descriptor=resources[0].toObject();
        descriptor["width"]=2049; resources[0]=descriptor; json["brushResources"]=resources; QVERIFY(!Project::fromJson(json,&error));
        json=valid; resources=json["brushResources"].toArray(); resources.append(resources[0]); json["brushResources"]=resources; QVERIFY(!Project::fromJson(json,&error));
        json=valid; auto object=objectJson(json); auto brush=object["brushStroke"].toObject(); brush["tipResource"]=QString(64,'a'); object["brushStroke"]=brush;
        QVERIFY(!Project::fromJson(replaceObject(json,object),&error));
        json=valid; resources=json["brushResources"].toArray(); descriptor=resources[0].toObject(); descriptor["id"]=QString(64,'a'); resources.append(descriptor);
        json["brushResources"]=resources; QVERIFY(!Project::fromJson(json,&error));
        json=valid; resources={};
        for(int i=0;i<4;++i) resources.append(QJsonObject{{"id",QString(64,QChar('a'+i))},{"width",2048},{"height",2048}});
        json["brushResources"]=resources; QVERIFY(!Project::fromJson(json,&error)); QVERIFY(error.contains("16 megapixel"));
        auto p=project(); p.shots[0].layers[0].objects[0].brushStroke->settings.tipMask=QImage(2049,1,QImage::Format_Grayscale8);
        QVERIFY(!p.validate().isEmpty());
        p=project(); p.shots[0].layers[0].objects[0].brushStroke->settings.tipMask=QImage(20,20,QImage::Format_ARGB32);
        QVERIFY(!p.validate().isEmpty());
    }
    void missingCorruptOrMismatchedImagesAreRejected() {
        QTemporaryDir directory; const auto path=directory.filePath("broken.toonproj"); QString error;
        auto p=project(); const auto manifest=p.toJson();
        QVERIFY(writeContainer(path,manifest)); QVERIFY(!loadProject(path,&error)); QVERIFY(error.contains("missing"));
        QVector<QImage> images;
        const auto &brush=p.shots[0].layers[0].objects[0].brushStroke->settings;
        QMap<QString,QImage> resources{{vectorBrushResourceId(brush.tipMask),brush.tipMask},{vectorBrushResourceId(brush.dual.tipMask),brush.dual.tipMask}};
        for(const auto &image:resources) images.append(image);
        auto wrong=images; wrong[0]=QImage(1,1,QImage::Format_Grayscale8); wrong[0].fill(255);
        QVERIFY(writeContainer(path,manifest,wrong)); QVERIFY(!loadProject(path,&error)); QVERIFY(error.contains("dimensions"));
        wrong=images; wrong[0]=wrong[0].copy(); wrong[0].scanLine(0)[0]^=1;
        QVERIFY(writeContainer(path,manifest,wrong)); QVERIFY(!loadProject(path,&error)); QVERIFY(error.contains("identifier"));
        QVERIFY(saveProject(p,path,&error)); QFile file(path); QVERIFY(file.open(QIODevice::ReadWrite)); QVERIFY(file.seek(file.size()-3));
        const auto byte=file.read(1); QVERIFY(file.seek(file.size()-3)); file.write(QByteArray(1,byte[0]^1)); file.close();
        QVERIFY(!loadProject(path,&error)); QVERIFY(error.contains("checksum"));
    }
    void historyCountsDetachedSamplesAndUniqueImages() {
        auto before=project(false), after=before;
        QCOMPARE(editableObjectHistoryCost(before,after),qint64(0));
        after.shots[0].layers[0].objects[0].brushStroke=stroke().brushStroke;
        const auto cost=editableObjectHistoryCost(before,after);
        QVERIFY(cost>=24*qint64(sizeof(VectorBrushSample))+2*tip().sizeInBytes());
        before=after; QCOMPARE(editableObjectHistoryCost(before,after),qint64(0));
        after.shots[0].layers[0].objects[0].brushStroke->samples[0].input.pressure=.3;
        QCOMPARE(editableObjectHistoryCost(before,after),48*qint64(sizeof(VectorBrushSample)));
        before=after; after.shots[0].layers[0].objects[0].nodes[0].anchor.setX(31);
        QCOMPARE(editableObjectHistoryCost(before,after),4*qint64(sizeof(BezierNode)));
    }
};
QTEST_MAIN(VectorBrushStorageTest)
#include "test_vector_brush_storage.moc"
