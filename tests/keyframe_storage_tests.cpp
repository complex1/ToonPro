#include "document.h"
#include "object_tools.h"
#include <QCryptographicHash>
#include <QDataStream>
#include <QFile>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QtTest>
using namespace toon;
class KeyframeStorageTests:public QObject{
Q_OBJECT
private slots:
void fullRoundTripAndProtectedSave(){QTemporaryDir dir;Document d;auto p=Project::create();p.width=p.height=128;d.load(p);const auto rasterId=p.shots[0].layers[0].id;QVERIFY(d.setCurrentFrame(12));QVERIFY(d.setKeyframeValue(rasterId,KeyframeProperty::PositionX,70));QVERIFY(ObjectTools::addLayer(&d,LayerKind::Vector));const auto objectId=ObjectTools::addPath(&d,{BezierNode::corner({10,10}),BezierNode::corner({30,50})},false,Qt::blue,4);const auto layer=d.project().shots[0].layers[d.activeLayer()];QVERIFY(d.enablePathAnimation(layer.id,objectId));auto pose=layer.objects[0];pose.nodes[1].anchor={60,70};QVERIFY(d.setPathKeyframe(layer.id,objectId,pose));QString error;const auto path=dir.filePath("keys.toon");QVERIFY2(saveProject(d.project(),path,&error),qPrintable(error));QFile file(path);QVERIFY(file.open(QIODevice::ReadOnly));QCOMPARE(file.read(10),QByteArray("TOONPRO19\n"));file.close();auto loaded=loadProject(path,&error);QVERIFY2(loaded.has_value(),qPrintable(error));QCOMPARE(loaded->toJson(),d.project().toJson());QCOMPARE(loaded->shots[0].layerAnimations,d.project().shots[0].layerAnimations);QCOMPARE(loaded->shots[0].layers[1].objects[0],layer.objects[0]);auto invalid=*loaded;invalid.shots[0].durationFrames=4;QVERIFY(!saveProject(invalid,path,&error));loaded=loadProject(path,&error);QVERIFY(loaded.has_value());QCOMPARE(loaded->toJson(),d.project().toJson());}
void legacy13MigrationAndDisguisedMetadata(){QTemporaryDir dir;auto p=Project::create();auto json=p.toJson();json["version"]=13;const auto path=dir.filePath("legacy.toon");QFile file(path);QVERIFY(file.open(QIODevice::WriteOnly));file.write("TOONPRO13\n");QDataStream stream(&file);stream.setByteOrder(QDataStream::BigEndian);const auto bytes=QJsonDocument(json).toJson(QJsonDocument::Compact),hash=QCryptographicHash::hash(bytes,QCryptographicHash::Sha256);stream<<quint32(bytes.size());stream.writeRawData(hash.constData(),hash.size());stream.writeRawData(bytes.constData(),bytes.size());file.close();QString error;auto loaded=loadProject(path,&error);QVERIFY2(loaded.has_value(),qPrintable(error));QVERIFY(loaded->shots[0].layerAnimations.isEmpty());QCOMPARE(loaded->toJson()["version"].toInt(),19);p.shots[0].layerAnimations={{p.shots[0].layers[0].id,{},{{KeyframeProperty::Opacity,{{0,1}}}}, {}}};json=p.toJson();json["version"]=13;QVERIFY(!Project::fromJson(json,&error));QVERIFY(error.contains("format 14"));json=p.toJson();json["version"]=latestProjectFormatVersion + 1;QVERIFY(!Project::fromJson(json,&error));}
};
QTEST_MAIN(KeyframeStorageTests)
#include "keyframe_storage_tests.moc"
