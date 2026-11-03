#include "animation_test_fixture.h"
#include <QBuffer>
#include <QCryptographicHash>
#include <QDataStream>
#include <QFile>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QtTest>
using namespace toon;
namespace {
bool writeLegacy12(const QString &path,QJsonObject manifest) {
    manifest["version"]=12;
    QFile file(path); if(!file.open(QIODevice::WriteOnly)) return false;
    file.write("TOONPRO12\n"); QDataStream stream(&file); stream.setByteOrder(QDataStream::BigEndian);
    const auto data=QJsonDocument(manifest).toJson(QJsonDocument::Compact);
    const auto hash=QCryptographicHash::hash(data,QCryptographicHash::Sha256);
    stream<<quint32(data.size()); stream.writeRawData(hash.constData(),hash.size()); stream.writeRawData(data.constData(),data.size());
    return stream.status()==QDataStream::Ok;
}
}
class AnimationStorageTests: public QObject {
    Q_OBJECT
private slots:
    void fullProjectRoundTripAndRecovery() {
        QTemporaryDir directory; QVERIFY(directory.isValid());
        auto project=animation_fixture::project(); auto &track=project.shots[0].animationTracks[0];
        track.exposures.append({animation_fixture::id(),track.drawings[0].id,10,5});
        const auto path=directory.filePath("animation.toon"); QString error;
        QVERIFY2(saveProject(project,path,&error),qPrintable(error));
        QFile file(path); QVERIFY(file.open(QIODevice::ReadOnly)); QCOMPARE(file.read(10),QByteArray("TOONPRO19\n")); file.close();
        auto restored=loadProject(path,&error); QVERIFY2(restored.has_value(),qPrintable(error)); QCOMPARE(restored->toJson(),project.toJson());
        QCOMPARE(restored->shots[0].layers[3].tiles,project.shots[0].layers[3].tiles);
        QCOMPARE(drawingAtFrame(restored->shots[0].animationTracks[0],12)->id,track.drawings[0].id);
        const auto recovery=directory.filePath("animation.toon.recovery"); QVERIFY(saveProject(project,recovery,&error));
        auto invalid=project; invalid.shots[0].animationTracks[0].exposures[0].durationFrames=200;
        QVERIFY(!saveProject(invalid,recovery,&error)); restored=loadProject(recovery,&error); QVERIFY(restored.has_value()); QCOMPARE(restored->toJson(),project.toJson());
    }
    void legacyStaticAndDisguisedMetadata() {
        QTemporaryDir directory; const auto path=directory.filePath("legacy.toon"); QString error;
        auto project=Project::create(); QVERIFY(writeLegacy12(path,project.toJson())); auto restored=loadProject(path,&error);
        QVERIFY2(restored.has_value(),qPrintable(error)); QVERIFY(restored->shots[0].animationTracks.isEmpty()); QCOMPARE(restored->shots[0].layers[0].id,project.shots[0].layers[0].id);
        auto animated=animation_fixture::project().toJson(); animated["version"]=12;
        QVERIFY(!Project::fromJson(animated,&error)); QVERIFY(error.contains("format 13"));
        QVERIFY(writeLegacy12(path,animated)); QVERIFY(!loadProject(path,&error)); QVERIFY(error.contains("format 13"));
        animated=animation_fixture::project().toJson(); animated["version"]=latestProjectFormatVersion + 1; QVERIFY(!Project::fromJson(animated,&error));
    }
    void corruptedReferencesAndStrictSchemaRejected() {
        const auto original=animation_fixture::project().toJson(); QString error;
        auto manifest=original; auto shots=manifest["shots"].toArray(); auto shot=shots[0].toObject(); auto tracks=shot["animationTracks"].toArray(); auto track=tracks[0].toObject();
        auto exposures=track["exposures"].toArray(); auto exposure=exposures[0].toObject(); exposure["drawingId"]=animation_fixture::id(); exposures[0]=exposure; track["exposures"]=exposures; tracks[0]=track; shot["animationTracks"]=tracks; shots[0]=shot; manifest["shots"]=shots;
        QVERIFY(!Project::fromJson(manifest,&error)); QVERIFY(error.contains("missing drawing"));
        manifest=original; shots=manifest["shots"].toArray(); shot=shots[0].toObject(); shot["animationTracks"]="bad"; shots[0]=shot; manifest["shots"]=shots; QVERIFY(!Project::fromJson(manifest,&error));
        manifest=original; shots=manifest["shots"].toArray(); shot=shots[0].toObject(); tracks=shot["animationTracks"].toArray(); track=tracks[0].toObject(); track["propertyKeys"]=QJsonArray{}; tracks[0]=track; shot["animationTracks"]=tracks; shots[0]=shot; manifest["shots"]=shots; QVERIFY(!Project::fromJson(manifest,&error));
    }
};
QTEST_MAIN(AnimationStorageTests)
#include "animation_storage_tests.moc"
