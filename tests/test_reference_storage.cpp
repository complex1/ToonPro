#include "project.h"
#include "sheet.h"
#include <QBuffer>
#include <QCryptographicHash>
#include <QDataStream>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QUuid>
#include <QtTest>

using namespace toon;

namespace {
ReferenceImage reference(QSize size = {48, 32}, QColor color = Qt::red) {
    ReferenceImage result{QUuid::createUuid().toString(QUuid::WithoutBraces), "Character study",
        QImage(size, QImage::Format_ARGB32_Premultiplied)};
    result.image.fill(color);
    return result;
}
QByteArray png(const QImage &image) {
    QByteArray data; QBuffer buffer(&data); buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    return data;
}
void appendBlob(QDataStream &stream, const QByteArray &data) {
    stream << quint32(data.size());
    const auto hash = QCryptographicHash::hash(data, QCryptographicHash::Sha256);
    stream.writeRawData(hash.constData(), hash.size());
    stream.writeRawData(data.constData(), data.size());
}
QByteArray container(int version, QJsonObject manifest, const QList<QByteArray> &images) {
    manifest["version"] = version;
    QByteArray data = version == 2 ? "TOONPRO2\n" : "TOONPRO3\n";
    QBuffer buffer(&data); buffer.open(QIODevice::WriteOnly | QIODevice::Append);
    QDataStream stream(&buffer); stream.setByteOrder(QDataStream::BigEndian);
    appendBlob(stream, QJsonDocument(manifest).toJson(QJsonDocument::Compact));
    for (const auto &image : images) appendBlob(stream, image);
    return data;
}
bool writeFile(const QString &path, const QByteArray &data) {
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(data) == data.size();
}
QJsonObject withReferenceMetadata(QJsonObject manifest, const QJsonArray &references) {
    auto shots = manifest["shots"].toArray();
    auto shot = shots[0].toObject(); shot["references"] = references;
    shots[0] = shot; manifest["shots"] = shots;
    return manifest;
}
}

class ReferenceStorageTest : public QObject {
    Q_OBJECT
private slots:
    void embeddedImagesSurviveSourceRemoval() {
        QTemporaryDir temp; QString error;
        auto project = Project::create();
        auto first = reference();
        const auto sourcePath = temp.filePath("source.png");
        QVERIFY(first.image.save(sourcePath));
        first.image = QImage(sourcePath).convertToFormat(QImage::Format_ARGB32_Premultiplied);
        project.shots.first().references.append(first);
        auto secondShot = Project::create().shots.first();
        secondShot.number = "C002";
        secondShot.references.append(reference({64, 80}, Qt::blue));
        project.shots.append(secondShot);
        QImage tile(tileSize, tileSize, QImage::Format_ARGB32_Premultiplied); tile.fill(Qt::green);
        project.shots[1].layers[0].tiles[{0, 0}] = tile;
        const auto path = temp.filePath("movie.toonproj");
        QVERIFY2(saveProject(project, path, &error), qPrintable(error));
        QVERIFY(QFile::remove(sourcePath));
        auto loaded = loadProject(path, &error);
        QVERIFY2(loaded.has_value(), qPrintable(error));
        QCOMPARE(loaded->toJson(), project.toJson());
        QCOMPARE(loaded->shots[0].references[0].image, project.shots[0].references[0].image);
        QCOMPARE(loaded->shots[1].references[0].image, project.shots[1].references[0].image);
        QCOMPARE(loaded->shots[1].layers[0].tiles, project.shots[1].layers[0].tiles);
        auto withoutReferences = project;
        for (auto &shot : withoutReferences.shots) shot.references.clear();
        QCOMPARE(renderMovieFrame(*loaded), renderMovieFrame(withoutReferences));
        QCOMPARE(renderSheet(*loaded, loaded->shots[0]), renderSheet(withoutReferences, withoutReferences.shots[0]));
        const auto metadata = Project::fromJson(project.toJson(), &error);
        QVERIFY(metadata); QVERIFY(metadata->shots[0].references[0].image.isNull());
        QVERIFY(!metadata->validate().isEmpty());
    }
    void damagedPayloadsAndDimensionsAreRejected() {
        QTemporaryDir temp; const auto path = temp.filePath("movie.toonproj"); QString error;
        auto project = Project::create(); project.shots[0].references.append(reference());
        const auto original = container(3, project.toJson(), {png(project.shots[0].references[0].image)});
        QVERIFY(writeFile(path, original)); QVERIFY2(loadProject(path, &error), qPrintable(error));
        auto damaged = original; damaged[damaged.size() - 10] ^= 1;
        QVERIFY(writeFile(path, damaged)); QVERIFY(!loadProject(path, &error)); QVERIFY(error.contains("checksum"));
        QVERIFY(writeFile(path, original.left(original.size() - 4))); QVERIFY(!loadProject(path, &error));
        QVERIFY(writeFile(path, container(3, project.toJson(), {}))); QVERIFY(!loadProject(path, &error));
        // A valid checksum must not allow image dimensions to exceed the declared budget.
        QVERIFY(writeFile(path, container(3, project.toJson(), {png(reference({49, 32}).image)})));
        QVERIFY(!loadProject(path, &error)); QVERIFY(error.contains("dimensions"));
        QVERIFY(writeFile(path, original + "extra")); QVERIFY(!loadProject(path, &error));
        QVERIFY(error.contains("trailing"));
    }
    void metadataBudgetsAndIdentifiersAreCheckedBeforeDecode() {
        auto project = Project::create(); project.shots[0].references.append(reference());
        QString error;
        auto metadata = project.toJson();
        auto refs = metadata["shots"].toArray()[0].toObject()["references"].toArray();
        auto ref = refs[0].toObject(); ref["width"] = 4097; refs[0] = ref;
        QVERIFY(!Project::fromJson(withReferenceMetadata(metadata, refs), &error));
        QVERIFY(error.contains("dimensions"));
        ref["width"] = 4096; ref["height"] = 4096; refs[0] = ref;
        QVERIFY(!Project::fromJson(withReferenceMetadata(metadata, refs), &error));
        QVERIFY(error.contains("16 megapixel"));
        refs = {};
        for (int i = 0; i <= maxProjectReferences; ++i) {
            ref["id"] = QUuid::createUuid().toString(QUuid::WithoutBraces);
            ref["width"] = 1; ref["height"] = 1; refs.append(ref);
        }
        QVERIFY(!Project::fromJson(withReferenceMetadata(metadata, refs), &error));
        QVERIFY(error.contains("16 reference"));
        project.shots[0].references[0].id = project.shots[0].layers[0].id;
        QVERIFY(project.validate().contains("identifier"));
        QVERIFY(!Project::fromJson(project.toJson(), &error));
        project.shots[0].references[0].id = QUuid(project.shots[0].id).toString().toUpper();
        QVERIFY(project.validate().contains("identifier"));
        project.shots[0].references[0] = reference(); project.shots[0].references[0].name = "   ";
        QVERIFY(project.validate().contains("names"));
        QVERIFY(!Project::fromJson(project.toJson(), &error));
    }
    void saveValidationPreservesPriorFile() {
        QTemporaryDir temp; const auto path = temp.filePath("movie.toonproj"); QString error;
        auto project = Project::create();
        QVERIFY(saveProject(project, path, &error));
        const auto original = project.toJson();
        auto large = reference({2000, 2000});
        for (int i = 0; i < 4; ++i) {
            large.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
            project.shots[0].references.append(large);
        }
        QVERIFY(project.validate().isEmpty());
        project.shots[0].references.append(reference({1, 1}));
        QVERIFY(!saveProject(project, path, &error)); QVERIFY(error.contains("16 megapixel"));
        auto recovered = loadProject(path, &error); QVERIFY(recovered); QCOMPARE(recovered->toJson(), original);
        project.shots[0].references = {reference()};
        project.shots[0].references[0].image = {};
        QVERIFY(!saveProject(project, path, &error));
        project.shots[0].references[0] = reference({4097, 1});
        QVERIFY(!saveProject(project, path, &error));
        project.shots[0].references[0] = reference();
        project.shots[0].references[0].image = project.shots[0].references[0].image.convertToFormat(QImage::Format_RGB32);
        QVERIFY(!saveProject(project, path, &error));
    }
    void versionTwoArtworkMigratesWithoutLoss() {
        QTemporaryDir temp; const auto path = temp.filePath("old.toonproj"); QString error;
        auto project = Project::create();
        QImage tile(tileSize, tileSize, QImage::Format_ARGB32_Premultiplied); tile.fill(Qt::blue);
        project.shots[0].layers[0].tiles[{-1, 0}] = tile;
        auto manifest = project.toJson(); manifest["version"] = 2;
        auto shots = manifest["shots"].toArray(); auto shot = shots[0].toObject();
        shot.remove("references"); shots[0] = shot; manifest["shots"] = shots;
        QVERIFY(writeFile(path, container(2, manifest, {png(tile)})));
        auto migrated = loadProject(path, &error); QVERIFY2(migrated.has_value(), qPrintable(error));
        QVERIFY(migrated->shots[0].references.isEmpty());
        QCOMPARE(migrated->shots[0].layers[0].tiles, project.shots[0].layers[0].tiles);
        QVERIFY(saveProject(*migrated, path, &error));
        QFile file(path); QVERIFY(file.open(QIODevice::ReadOnly)); QCOMPARE(file.read(10), QByteArray("TOONPRO19\n"));
        file.close();
        const auto reloaded = loadProject(path, &error); QVERIFY(reloaded);
        QCOMPARE(reloaded->shots[0].layers[0].tiles, project.shots[0].layers[0].tiles);
    }
};

QTEST_MAIN(ReferenceStorageTest)
#include "test_reference_storage.moc"
