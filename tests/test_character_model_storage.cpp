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
QString id() { return QUuid::createUuid().toString(QUuid::WithoutBraces); }
QImage image(QSize size = {48, 32}, QColor color = Qt::red) {
    QImage result(size, QImage::Format_ARGB32_Premultiplied); result.fill(color); return result;
}
CharacterModel character() {
    return {id(), "Aiko", {{id(), "Front", ModelSheetCategory::Turnaround, image()},
        {id(), "Happy", ModelSheetCategory::Expression, image({32, 48}, Qt::blue)},
        {id(), "Winter coat", ModelSheetCategory::Costume, image({24, 64}, Qt::green)}}};
}
QByteArray png(const QImage &image) {
    QByteArray bytes; QBuffer buffer(&bytes); buffer.open(QIODevice::WriteOnly); image.save(&buffer, "PNG"); return bytes;
}
void blob(QDataStream &stream, const QByteArray &bytes) {
    stream << quint32(bytes.size());
    const auto hash = QCryptographicHash::hash(bytes, QCryptographicHash::Sha256);
    stream.writeRawData(hash.constData(), hash.size()); stream.writeRawData(bytes.constData(), bytes.size());
}
QByteArray container(const QJsonObject &metadata, const QList<QByteArray> &images = {}) {
    QByteArray bytes = "TOONPRO" + QByteArray::number(metadata["version"].toInt()) + "\n";
    QBuffer buffer(&bytes); buffer.open(QIODevice::WriteOnly | QIODevice::Append);
    QDataStream stream(&buffer); stream.setByteOrder(QDataStream::BigEndian);
    blob(stream, QJsonDocument(metadata).toJson(QJsonDocument::Compact));
    for (const auto &image : images) blob(stream, image);
    return bytes;
}
bool write(const QString &path, const QByteArray &bytes) {
    QFile file(path); return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}
QJsonObject withSheets(QJsonObject manifest, const QJsonArray &sheets) {
    auto characters = manifest["characters"].toArray(); auto character = characters[0].toObject();
    character["sheets"] = sheets; characters[0] = character; manifest["characters"] = characters; return manifest;
}
}
class CharacterModelStorageTest : public QObject {
    Q_OBJECT
private slots:
    void embeddedLibrarySurvivesSourceRemovalAndDoesNotRender() {
        QTemporaryDir temp; QString error;
        auto project = Project::create(); project.characters.append(character());
        const auto source = temp.filePath("Front.png");
        QVERIFY(project.characters[0].sheets[0].image.save(source));
        project.characters[0].sheets[0].image = QImage(source).convertToFormat(QImage::Format_ARGB32_Premultiplied);
        auto next = Project::create().shots[0]; next.number = "C002"; project.shots.append(next);
        // Exercise ordering alongside the older image sections.
        project.shots[0].layers[0].tiles[{0, 0}] = image({tileSize, tileSize}, Qt::yellow);
        project.shots[0].references.append({id(), "Landscape", image({16, 16})});
        LayerMask mask; QImage maskTile(tileSize, tileSize, QImage::Format_Grayscale8); maskTile.fill(255);
        mask.tiles[{0, 0}] = maskTile; project.shots[0].layers[0].mask = mask;
        const auto path = temp.filePath("movie.toonproj");
        QVERIFY2(saveProject(project, path, &error), qPrintable(error)); QVERIFY(QFile::remove(source));
        auto restored = loadProject(path, &error); QVERIFY2(restored, qPrintable(error));
        QCOMPARE(restored->toJson(), project.toJson()); QCOMPARE(restored->characters.size(), 1);
        for (int i = 0; i < 3; ++i) QCOMPARE(restored->characters[0].sheets[i].image, project.characters[0].sheets[i].image);
        QCOMPARE(restored->shots[0].layers[0].tiles, project.shots[0].layers[0].tiles);
        QCOMPARE(restored->shots[0].references[0].image, project.shots[0].references[0].image);
        QCOMPARE(restored->shots[0].layers[0].mask->tiles, project.shots[0].layers[0].mask->tiles);
        auto artworkOnly = project; artworkOnly.characters.clear();
        QCOMPARE(renderMovieFrame(*restored), renderMovieFrame(artworkOnly));
        QCOMPARE(renderSheet(*restored, restored->shots[0]), renderSheet(artworkOnly, artworkOnly.shots[0]));
        auto metadata = Project::fromJson(project.toJson(), &error); QVERIFY(metadata);
        QVERIFY(metadata->characters[0].sheets[0].image.isNull()); QVERIFY(!metadata->validate().isEmpty());
    }
    void malformedOrMissingPayloadCannotReplaceExistingSave() {
        QTemporaryDir temp; QString error; const auto path = temp.filePath("movie.toonproj");
        auto project = Project::create(); project.characters.append(character()); project.characters[0].sheets.resize(1);
        const auto original = container(project.toJson(), {png(project.characters[0].sheets[0].image)});
        QVERIFY(write(path, original)); QVERIFY2(loadProject(path, &error), qPrintable(error));
        auto damaged = original; damaged[damaged.size() - 9] ^= 1;
        QVERIFY(write(path, damaged)); QVERIFY(!loadProject(path, &error)); QVERIFY(error.contains("checksum"));
        QVERIFY(write(path, container(project.toJson()))); QVERIFY(!loadProject(path, &error));
        QVERIFY(write(path, original.left(original.size() - 3))); QVERIFY(!loadProject(path, &error));
        QVERIFY(write(path, container(project.toJson(), {png(image({49, 32}))})));
        QVERIFY(!loadProject(path, &error)); QVERIFY(error.contains("dimensions"));
        QImage highDepth(48, 32, QImage::Format_RGBA64); highDepth.fill(Qt::blue);
        QVERIFY(write(path, container(project.toJson(), {png(highDepth)})));
        QVERIFY(!loadProject(path, &error)); QVERIFY(error.contains("8-bit"));
        QVERIFY(write(path, original + "extra")); QVERIFY(!loadProject(path, &error)); QVERIFY(error.contains("trailing"));
        QVERIFY(write(path, original)); project.characters[0].sheets[0].image = {};
        QVERIFY(!saveProject(project, path, &error)); QVERIFY2(loadProject(path, &error), qPrintable(error));
    }
    void metadataValidationPrecedesImageDecode() {
        QString error; auto project = Project::create(); project.characters.append(character());
        const auto manifest = project.toJson(); auto sheets = manifest["characters"].toArray()[0].toObject()["sheets"].toArray();
        auto first = sheets[0].toObject(); first["category"] = "anything"; sheets[0] = first;
        QVERIFY(!Project::fromJson(withSheets(manifest, sheets), &error)); QVERIFY(error.contains("category"));
        first["category"] = "turnaround"; first["width"] = 4097; sheets[0] = first;
        QVERIFY(!Project::fromJson(withSheets(manifest, sheets), &error)); QVERIFY(error.contains("dimensions"));
        first["width"] = 4096.5; sheets[0] = first;
        QVERIFY(!Project::fromJson(withSheets(manifest, sheets), &error));
        first["width"] = 4096; first["height"] = 4096; sheets = {};
        for (int i = 0; i < 4; ++i) { first["id"] = id(); sheets.append(first); }
        QVERIFY(!Project::fromJson(withSheets(manifest, sheets), &error)); QVERIFY(error.contains("64 megapixel"));
        first["width"] = 1; first["height"] = 1; sheets = {};
        for (int i = 0; i <= maxProjectCharacterSheets; ++i) { first["id"] = id(); sheets.append(first); }
        QVERIFY(!Project::fromJson(withSheets(manifest, sheets), &error)); QVERIFY(error.contains("256"));
        auto changed = manifest; changed["characters"] = QJsonObject{};
        QVERIFY(!Project::fromJson(changed, &error));
        QJsonArray characters;
        for (int i = 0; i <= maxProjectCharacters; ++i)
            characters.append(QJsonObject{{"id", id()}, {"name", QString::number(i)}, {"sheets", QJsonArray{}}});
        changed["characters"] = characters; QVERIFY(!Project::fromJson(changed, &error)); QVERIFY(error.contains("128"));
    }
    void identifiersNamesAndCategoriesAreStrict() {
        auto project = Project::create(); project.characters.append(character()); QString error;
        const auto original = project;
        project.characters[0].id = QUuid(project.shots[0].id).toString().toUpper();
        QVERIFY(project.validate().contains("identifier")); QVERIFY(!Project::fromJson(project.toJson(), &error));
        project = original; project.characters[0].sheets[1].id = project.characters[0].sheets[0].id;
        QVERIFY(project.validate().contains("identifier")); QVERIFY(!Project::fromJson(project.toJson(), &error));
        project = original; project.characters[0].sheets[0].id = project.shots[0].layers[0].id;
        QVERIFY(!Project::fromJson(project.toJson(), &error));
        project = original; auto duplicate = character(); duplicate.name = "AIKO"; project.characters.append(duplicate);
        QVERIFY(project.validate().contains("unique"));
        project = original; project.characters[0].name = "Aiko\nHero"; QVERIFY(!project.validate().isEmpty());
        project = original; project.characters[0].sheets[0].name = QString(101, 'x'); QVERIFY(!project.validate().isEmpty());
        project = original; project.characters[0].sheets[0].category = ModelSheetCategory(91);
        QVERIFY(project.validate().contains("category"));
        project = original; project.characters[0].sheets[0].image = project.characters[0].sheets[0].image.convertToFormat(QImage::Format_RGB32);
        QVERIFY(!project.validate().isEmpty());
    }
    void previousProjectsOpenWithoutLibraryAndEmptyLibraryUsesOldFormat() {
        QTemporaryDir temp; const auto path = temp.filePath("old.toonproj"); QString error;
        auto project = Project::create(); const auto original = project.toJson();
        QCOMPARE(original["version"].toInt(), 19); QVERIFY(!original.contains("characters"));
        auto old = original; old["version"] = 26;
        auto oldShots = old["shots"].toArray(); auto oldShot = oldShots[0].toObject();
        oldShot["scene"] = "Scene 1"; oldShots[0] = oldShot; old["shots"] = oldShots;
        QVERIFY(write(path, container(old))); auto restored = loadProject(path, &error);
        QVERIFY2(restored, qPrintable(error)); QVERIFY(restored->characters.isEmpty());
        QCOMPARE(restored->toJson(), original);
        project.characters.append({id(), "Aiko", {}});
        QCOMPARE(project.toJson()["version"].toInt(), 27);
        QVERIFY(saveProject(project, path, &error)); restored = loadProject(path, &error);
        QVERIFY(restored); QCOMPARE(restored->characters.size(), 1); QVERIFY(restored->characters[0].sheets.isEmpty());
        auto invalid = project.toJson(); invalid["version"] = 26;
        QVERIFY(!Project::fromJson(invalid, &error)); QVERIFY(error.contains("27"));
    }
};
QTEST_MAIN(CharacterModelStorageTest)
#include "test_character_model_storage.moc"
