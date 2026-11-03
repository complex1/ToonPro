#include "audio.h"
#include "document.h"
#include <QCryptographicHash>
#include <QDataStream>
#include <QFile>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QUuid>
#include <QtTest>
using namespace toon;
namespace {
QString id() { return QUuid::createUuid().toString(QUuid::WithoutBraces); }
Project fixture() {
    auto project = Project::create();
    const auto asset = makeAudioAsset("Dialogue", QByteArray(48000 * 4, '\x14'));
    project.shots[0].audioClips = {{id(), "First line", asset, 2, 17, 47000, .8, false},
                                  {id(), "Repeat", asset, 3, 100, 1000, 1.2, true}};
    auto second = Project::create().shots[0]; second.audioClips = {{id(), "Second shot", asset, 0, 0, 48000, 1, false}};
    project.shots.append(second); return project;
}
QByteArray bytes(const QString &path) { QFile file(path); if (!file.open(QIODevice::ReadOnly)) return {}; return file.readAll(); }
bool write(const QString &path, const QByteArray &data) { QFile file(path); return file.open(QIODevice::WriteOnly) && file.write(data) == data.size(); }
QByteArray container(const QJsonObject &manifest, const QList<QByteArray> &payloads = {}, int version = -1) {
    if (version < 0) version = manifest["version"].toInt();
    QByteArray data; QDataStream stream(&data, QIODevice::WriteOnly); stream.setByteOrder(QDataStream::BigEndian);
    const auto header = QByteArray("TOONPRO") + QByteArray::number(version) + '\n'; stream.writeRawData(header.constData(), header.size());
    const auto blob = [&](const QByteArray &value) { stream << quint32(value.size()); const auto hash = QCryptographicHash::hash(value, QCryptographicHash::Sha256);
        stream.writeRawData(hash.constData(), hash.size()); stream.writeRawData(value.constData(), value.size()); };
    blob(QJsonDocument(manifest).toJson(QJsonDocument::Compact)); for (const auto &payload : payloads) blob(payload);
    return data;
}
}
class AudioStorageTests : public QObject {
    Q_OBJECT
private slots:
    void roundTripDeduplicatesResourcesAcrossClipsAndShots() {
        const auto project = fixture(); QVERIFY2(project.validate().isEmpty(), qPrintable(project.validate()));
        QCOMPARE(project.toJson()["version"].toInt(), 22); QCOMPARE(project.toJson()["audioResources"].toArray().size(), 1);
        QTemporaryDir directory; const auto path = directory.filePath("audio.toon"); QString error;
        QVERIFY2(saveProject(project, path, &error), qPrintable(error));
        QCOMPARE(bytes(path).left(10), QByteArray("TOONPRO22\n"));
        const auto loaded = loadProject(path, &error); QVERIFY2(loaded, qPrintable(error));
        QCOMPARE(loaded->toJson(), project.toJson());
        QCOMPARE(loaded->shots[0].audioClips, project.shots[0].audioClips);
        const auto asset = loaded->shots[0].audioClips[0].asset;
        QCOMPARE(asset, loaded->shots[0].audioClips[1].asset); QCOMPARE(asset, loaded->shots[1].audioClips[0].asset);
        QCOMPARE(asset->pcm, project.shots[0].audioClips[0].asset->pcm);
        const auto metadataSize = QJsonDocument(project.toJson()).toJson(QJsonDocument::Compact).size();
        QCOMPARE(bytes(path).size(), 10 + 36 + metadataSize + 36 + asset->pcm.size());
        const auto metadataOnly = Project::fromJson(project.toJson(), &error); QVERIFY(metadataOnly);
        QVERIFY(!metadataOnly->validate().isEmpty()); QVERIFY(metadataOnly->shots[0].audioClips[0].asset->pcm.isEmpty());
        QCOMPARE(metadataOnly->shots[0].audioClips[0].asset->sampleFrameCount(), 48000);
    }
    void corruptedMissingForgedAndTrailingAudioDataAreRejected() {
        const auto project = fixture(); const auto asset = project.shots[0].audioClips[0].asset;
        const auto original = container(project.toJson(), {asset->pcm});
        QTemporaryDir directory; const auto path = directory.filePath("bad.toon"); QString error;
        auto bad = original; bad[bad.size() - 1] = char(bad.back() ^ 1);
        QVERIFY(write(path, bad)); QVERIFY(!loadProject(path, &error)); QVERIFY(error.contains("checksum"));
        QVERIFY(write(path, container(project.toJson()))); QVERIFY(!loadProject(path, &error));
        auto forged = asset->pcm; forged[0] ^= 1;
        QVERIFY(write(path, container(project.toJson(), {forged}))); QVERIFY(!loadProject(path, &error)); QVERIFY(error.contains("identifier"));
        QVERIFY(write(path, original + "junk")); QVERIFY(!loadProject(path, &error)); QVERIFY(error.contains("trailing"));
        QVERIFY(write(path, container(project.toJson(), {asset->pcm.left(asset->pcm.size() - 4)}))); QVERIFY(!loadProject(path, &error));
    }
    void strictMetadataChecksBudgetsReferencesAndSampleBoundsBeforeLoading() {
        const auto original = fixture().toJson(); QString error;
        for (int failure = 0; failure < 10; ++failure) {
            auto manifest = original; auto resources = manifest["audioResources"].toArray(); auto resource = resources[0].toObject();
            auto shots = manifest["shots"].toArray(); auto shot = shots[0].toObject(); auto clips = shot["audioClips"].toArray(); auto clip = clips[0].toObject();
            if (failure == 0) resource["sampleRate"] = 44100;
            if (failure == 1) resource["channels"] = 1;
            if (failure == 2) resource["sampleFrames"] = double(maxAudioAssetBytes / 4 + 1);
            if (failure == 3) resource["extra"] = true;
            if (failure == 4) clip["sourceOffsetSamples"] = -1;
            if (failure == 5) clip["sampleCount"] = 48001;
            if (failure == 6) clip["gain"] = 2.1;
            if (failure == 7) clip["assetId"] = QString(64, 'a');
            if (failure == 8) clip["extra"] = true;
            if (failure == 9) clip["sourceOffsetSamples"] = .5;
            resources[0] = resource; manifest["audioResources"] = resources;
            clips[0] = clip; shot["audioClips"] = clips; shots[0] = shot; manifest["shots"] = shots;
            QVERIFY2(!Project::fromJson(manifest, &error), qPrintable(QString("Failure case %1 unexpectedly accepted").arg(failure)));
        }
        auto manifest = original; auto resources = manifest["audioResources"].toArray(); resources.append(resources[0]); manifest["audioResources"] = resources;
        QVERIFY(!Project::fromJson(manifest, &error));
        manifest = original; resources = manifest["audioResources"].toArray(); auto unused = resources[0].toObject(); unused["id"] = QString(64, 'b'); resources.append(unused); manifest["audioResources"] = resources;
        QVERIFY(!Project::fromJson(manifest, &error)); QVERIFY(error.contains("unused"));
        resources = {};
        for (int i = 0; i < 3; ++i) { auto descriptor = unused; descriptor["id"] = QString(64, QChar(ushort('a' + i))); descriptor["sampleFrames"] = double(maxAudioAssetBytes / 4); resources.append(descriptor); }
        QMap<QString, AudioAssetPtr> parsed;
        QVERIFY(!audioResourcesFromJson(resources, &parsed, &error)); QVERIFY(error.contains("256")); QVERIFY(parsed.isEmpty());
    }
    void priorFormatsLoadWithoutAudioAndDisguisedAudioIsRejected() {
        QTemporaryDir directory; const auto path = directory.filePath("legacy.toon"); QString error;
        for (int version = 1; version <= 14; ++version) {
            auto manifest = Project::create().toJson(); manifest["version"] = version; manifest.remove("audioResources");
            QVERIFY(write(path, version == 1 ? QJsonDocument(manifest).toJson(QJsonDocument::Compact) : container(manifest, {}, version)));
            const auto loaded = loadProject(path, &error); QVERIFY2(loaded, qPrintable(QString("Version %1: %2").arg(version).arg(error)));
            QVERIFY(loaded->shots[0].audioClips.isEmpty()); QCOMPARE(loaded->toJson()["version"].toInt(), 19);
        }
        auto disguised = fixture().toJson(); disguised["version"] = 14;
        auto legacyShots = disguised["shots"].toArray();
        for (int i = 0; i < legacyShots.size(); ++i) { auto shot = legacyShots[i].toObject(); shot.remove("scene"); legacyShots[i] = shot; }
        disguised["shots"] = legacyShots;
        QVERIFY(!Project::fromJson(disguised, &error)); QVERIFY(error.contains("format 15"));
        auto future = Project::create().toJson(); future["version"] = latestProjectFormatVersion + 1;
        QVERIFY(write(path, container(future, {}, 20))); QVERIFY(!loadProject(path, &error));
    }
    void invalidSavePreservesPreviouslySavedAudio() {
        const auto project = fixture(); QTemporaryDir directory; const auto path = directory.filePath("protected.toon"); QString error;
        QVERIFY(saveProject(project, path, &error)); const auto before = bytes(path);
        auto invalid = project; invalid.shots[0].audioClips[0].sampleCount = 48000;
        QVERIFY(!saveProject(invalid, path, &error)); QCOMPARE(bytes(path), before);
        const auto loaded = loadProject(path, &error); QVERIFY(loaded); QCOMPARE(loaded->toJson(), project.toJson());
    }
};
QTEST_MAIN(AudioStorageTests)
#include "audio_storage_tests.moc"
