#include "drawing_template_library.h"
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QtTest>
using namespace toon;
namespace {
QByteArray bytes(const QString &path){QFile file(path);return file.open(QIODevice::ReadOnly)?file.readAll():QByteArray();}
bool write(const QString &path,const QByteArray &data){QFile file(path);return file.open(QIODevice::WriteOnly)&&file.write(data)==data.size();}
}
class DrawingTemplateLibraryTests : public QObject {
    Q_OBJECT
private slots:
    void explicitSaveRoundTripUpdateAndRemove() {
        QTemporaryDir dir; const auto path=dir.filePath("workspace/drawing-templates.json"); DrawingTemplateLibrary library(path); QString error;
        QVERIFY(library.load(&error)); QVERIFY(library.writable()); QVERIFY(library.entries().isEmpty()); QVERIFY(!QFile::exists(path));
        const QVector<DrawingTemplateSlot> layers{{"paint","Colors"},{"ink","Line Art",LayerKind::Vector}};
        QVERIFY2(library.saveTemplate("Character",layers,&error),qPrintable(error)); QCOMPARE(library.entries().size(),1);
        const auto id=library.entries()[0].id; DrawingTemplateLibrary reopened(path); QVERIFY(reopened.load(&error)); QCOMPARE(reopened.entries(),library.entries());
        auto changed=layers; changed[0].name="Flat colors"; QVERIFY(library.saveTemplate("character",changed,&error));
        QCOMPARE(library.entries().size(),1); QCOMPARE(library.entries()[0].id,id); QCOMPARE(library.entries()[0].layers,changed);
        const auto before=bytes(path); QVERIFY(!library.saveTemplate("Empty",{},&error)); QCOMPARE(bytes(path),before);
        auto invalid=layers; invalid[0].kind=LayerKind::Text; QVERIFY(!library.saveTemplate("Text",invalid,&error)); QCOMPARE(bytes(path),before);
        QVERIFY(library.removeTemplate(id,&error)); QVERIFY(library.entries().isEmpty()); QVERIFY(reopened.load(&error)); QVERIFY(reopened.entries().isEmpty());
    }
    void corruptFutureUnknownAndOversizedFilesArePreserved() {
        QTemporaryDir dir; const auto path=dir.filePath("drawing-templates.json");
        const QVector<QByteArray> bad{QByteArray("{broken"),QByteArray("{}"),
            QJsonDocument(QJsonObject{{"format","toon-pro-drawing-templates"},{"version",2},{"templates",QJsonArray()}}).toJson(),
            QJsonDocument(QJsonObject{{"format","toon-pro-drawing-templates"},{"version",1},{"templates",QJsonArray()},{"unknown",true}}).toJson(),
            QByteArray(1024*1024+1,' ')};
        for(const auto &data:bad){
            QVERIFY(write(path,data)); DrawingTemplateLibrary library(path); QString error;
            QVERIFY(!library.load(&error)); QVERIFY(!library.writable()); QVERIFY(!error.isEmpty());
            QVERIFY(!library.saveTemplate("Simple",drawingTemplateSlots(DrawingTemplate::Simple),&error)); QCOMPARE(bytes(path),data);
        }
    }
    void externalChangesAndFailedWritesDoNotChangeMemory() {
        QTemporaryDir dir; const auto path=dir.filePath("templates.json"); DrawingTemplateLibrary first(path),second(path); QString error;
        QVERIFY(first.load()); QVERIFY(second.load()); QVERIFY(first.saveTemplate("First",drawingTemplateSlots(DrawingTemplate::Simple)));
        const auto saved=bytes(path); QVERIFY(!second.saveTemplate("Second",drawingTemplateSlots(DrawingTemplate::Anime),&error));
        QCOMPARE(bytes(path),saved); QVERIFY(second.entries().isEmpty()); QVERIFY(!second.writable());
        QVERIFY(second.load()); QCOMPARE(second.entries(),first.entries());
        const auto before=second.entries(); QVERIFY(write(path,"changed externally")); QVERIFY(!second.removeTemplate(before[0].id,&error));
        QCOMPARE(second.entries(),before); QCOMPARE(bytes(path),QByteArray("changed externally"));
        const auto blocked=dir.filePath("block"); QVERIFY(write(blocked,"file")); DrawingTemplateLibrary impossible(blocked+"/templates.json");
        QVERIFY(impossible.load()); QVERIFY(!impossible.saveTemplate("Simple",drawingTemplateSlots(DrawingTemplate::Simple),&error)); QVERIFY(impossible.entries().isEmpty());
    }
    void boundsAndMalformedLayerTypes() {
        QTemporaryDir dir; const auto path=dir.filePath("templates.json"); DrawingTemplateLibrary library(path); QVERIFY(library.load());
        auto layers=drawingTemplateSlots(DrawingTemplate::Simple); layers.append(layers.first()); QVERIFY(!library.saveTemplate("Duplicate",layers));
        layers={{"empty"," "}}; QVERIFY(!library.saveTemplate("Bad",layers));
        for(int i=0;i<maxDrawingTemplates;++i)QVERIFY(library.saveTemplate(QString("Preset %1").arg(i),drawingTemplateSlots(DrawingTemplate::Simple)));
        const auto before=bytes(path); QVERIFY(!library.saveTemplate("Too many",drawingTemplateSlots(DrawingTemplate::Simple))); QCOMPARE(bytes(path),before);
        auto json=QJsonDocument::fromJson(before).object(); auto items=json["templates"].toArray(); auto entry=items[0].toObject(); auto recipes=entry["layers"].toArray();
        auto layer=recipes[0].toObject(); layer["kind"]="scene3d"; recipes[0]=layer; entry["layers"]=recipes; items[0]=entry; json["templates"]=items;
        QVERIFY(write(path,QJsonDocument(json).toJson())); DrawingTemplateLibrary corrupted(path); QVERIFY(!corrupted.load()); QVERIFY(!corrupted.writable());
    }
};
QTEST_APPLESS_MAIN(DrawingTemplateLibraryTests)
#include "drawing_template_library_tests.moc"
