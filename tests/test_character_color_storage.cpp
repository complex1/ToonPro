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
Project fixture() {
    auto project = Project::create();
    project.width = 320; project.height = 240;
    project.characters.append({id(), "Hana", {}, {
        {id(), "Skin base", CharacterColorCategory::Skin, QColor("#edb995")},
        {id(), "Hair ink", CharacterColorCategory::Hair, QColor("#352b40")},
        {id(), "Winter coat", CharacterColorCategory::Costume, QColor("#45859b")},
        {id(), "Soft shadow", CharacterColorCategory::Shadow, QColor(64, 39, 98, 96)}}});
    return project;
}
QImage image(QSize size, QColor color) {
    QImage result(size, QImage::Format_ARGB32_Premultiplied); result.fill(color); return result;
}
QJsonObject withColors(QJsonObject manifest, QJsonValue colors) {
    auto characters = manifest["characters"].toArray(); auto character = characters[0].toObject();
    character["colors"] = colors; characters[0] = character; manifest["characters"] = characters;
    return manifest;
}
QByteArray container(const QJsonObject &manifest) {
    QByteArray bytes = "TOONPRO" + QByteArray::number(manifest["version"].toInt()) + "\n";
    QBuffer buffer(&bytes); buffer.open(QIODevice::WriteOnly | QIODevice::Append);
    QDataStream stream(&buffer); stream.setByteOrder(QDataStream::BigEndian);
    const auto json = QJsonDocument(manifest).toJson(QJsonDocument::Compact);
    const auto digest = QCryptographicHash::hash(json, QCryptographicHash::Sha256);
    stream << quint32(json.size()); stream.writeRawData(digest.constData(), digest.size());
    stream.writeRawData(json.constData(), json.size()); return bytes;
}
bool write(const QString &path, const QByteArray &bytes) {
    QFile file(path); return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}
}

class CharacterColorStorageTest : public QObject {
    Q_OBJECT
private slots:
    void colorsRoundTripAcrossShotsAlongsideImagesAndArtwork() {
        auto project = fixture(); QString error; QTemporaryDir temp;
        const auto front = image({48, 64}, Qt::cyan);
        project.characters[0].sheets.append({id(), "Front", ModelSheetCategory::Turnaround, front});
        project.characters.append({id(), "Ren", {}}); // Empty color models are valid beside populated ones.
        auto second = Project::create().shots[0]; second.number = "C002"; project.shots.append(second);
        project.shots[0].layers[0].tiles[{0, 0}] = image({tileSize, tileSize}, QColor("#d2b0a0"));
        project.shots[1].references.append({id(), "Environment", image({32, 24}, Qt::green)});
        const auto metadata = project.toJson(); QCOMPARE(metadata["version"].toInt(), 28);
        const auto swatches = metadata["characters"].toArray()[0].toObject()["colors"].toArray();
        QCOMPARE(swatches[0].toObject()["color"].toString(), QString("#ffedb995"));
        QCOMPARE(swatches[3].toObject()["color"].toString(), QString("#60402762"));
        QVERIFY(metadata["characters"].toArray()[1].toObject()["colors"].toArray().isEmpty());
        const auto path = temp.filePath("movie.toonproj");
        QVERIFY2(saveProject(project, path, &error), qPrintable(error));
        QFile saved(path); QVERIFY(saved.open(QIODevice::ReadOnly)); QCOMPARE(saved.read(10), QByteArray("TOONPRO28\n")); saved.close();
        const auto loaded = loadProject(path, &error); QVERIFY2(loaded, qPrintable(error));
        QCOMPARE(loaded->toJson(), metadata); QCOMPARE(loaded->characters[0].colors, project.characters[0].colors);
        QCOMPARE(loaded->characters[0].sheets[0].image, front);
        QCOMPARE(loaded->shots[0].layers[0].tiles, project.shots[0].layers[0].tiles);
        QCOMPARE(loaded->shots[1].references[0].image, project.shots[1].references[0].image);
        auto withoutColors = project; withoutColors.characters[0].colors.clear();
        QCOMPARE(renderMovieFrame(*loaded), renderMovieFrame(withoutColors));
        QCOMPARE(renderSheet(*loaded, loaded->shots[0]), renderSheet(withoutColors, withoutColors.shots[0]));
    }
    void format27AndLegacyProjectsMigrateWithoutColorData() {
        auto project = fixture(); project.characters[0].colors.clear(); QString error; QTemporaryDir temp;
        const auto path = temp.filePath("older.toonproj"); const auto manifest = project.toJson();
        QCOMPARE(manifest["version"].toInt(), 27);
        QVERIFY(!manifest["characters"].toArray()[0].toObject().contains("colors"));
        QVERIFY(write(path, container(manifest))); auto loaded = loadProject(path, &error);
        QVERIFY2(loaded, qPrintable(error)); QVERIFY(loaded->characters[0].colors.isEmpty()); QCOMPARE(loaded->toJson(), manifest);
        project.characters.clear(); QCOMPARE(project.toJson()["version"].toInt(), 19);
        QVERIFY(saveProject(project, path, &error)); loaded = loadProject(path, &error);
        QVERIFY(loaded); QVERIFY(loaded->characters.isEmpty());
        auto downgraded = fixture().toJson(); downgraded["version"] = 27;
        QVERIFY(!Project::fromJson(downgraded, &error)); QVERIFY(error.contains("28"));
        downgraded = withColors(manifest, QJsonArray{});
        QVERIFY(!Project::fromJson(downgraded, &error)); QVERIFY(error.contains("28"));
        downgraded = fixture().toJson(); downgraded["version"] = 29;
        QVERIFY(!Project::fromJson(downgraded, &error));
    }
    void malformedColorMetadataIsRejectedWithoutFallback() {
        const auto original = fixture().toJson(); QString error;
        auto colors = original["characters"].toArray()[0].toObject()["colors"].toArray();
        const auto originalColor = colors[0].toObject();
        for (const auto &bad : QStringList{"red", "#edb995", "#abcd", "#ggff0000", "#ff0000000", " #ffedb995", "#ffedb995\n"}) {
            auto color = originalColor; color["color"] = bad; colors[0] = color;
            QVERIFY2(!Project::fromJson(withColors(original, colors), &error), qPrintable(bad));
            QVERIFY(error.contains("#AARRGGBB"));
        }
        for (const auto &bad : QList<QJsonValue>{QJsonValue(12), QJsonValue(QJsonValue::Null), QJsonValue(QJsonArray{})}) {
            auto color = originalColor; color["color"] = bad; colors[0] = color;
            QVERIFY(!Project::fromJson(withColors(original, colors), &error));
        }
        auto color = originalColor; color["category"] = "unknown"; colors[0] = color;
        QVERIFY(!Project::fromJson(withColors(original, colors), &error)); QVERIFY(error.contains("category"));
        color = originalColor; color["name"] = "Skin\nBase"; colors[0] = color;
        QVERIFY(!Project::fromJson(withColors(original, colors), &error));
        color = originalColor; color["name"] = 17; colors[0] = color;
        QVERIFY(!Project::fromJson(withColors(original, colors), &error));
        colors[0] = "invalid"; QVERIFY(!Project::fromJson(withColors(original, colors), &error));
        QVERIFY(!Project::fromJson(withColors(original, QJsonValue::Null), &error));
        auto missing = original; auto entries = missing["characters"].toArray(); auto character = entries[0].toObject();
        character.remove("colors"); entries[0] = character; missing["characters"] = entries;
        QVERIFY(!Project::fromJson(missing, &error));
        color = originalColor; color["color"] = "#00AABBCC"; colors = {color};
        auto parsed = Project::fromJson(withColors(original, colors), &error); QVERIFY(parsed);
        QCOMPARE(parsed->characters[0].colors[0].color.rgba(), QRgb(0x00aabbcc));
    }
    void identifiersCategoriesNamesAndBudgetsAreChecked() {
        auto project = fixture(); const auto original = project; QString error;
        project.characters[0].colors[0].id = QUuid(project.shots[0].id).toString().toUpper();
        QVERIFY(project.validate().contains("identifier")); QVERIFY(!Project::fromJson(project.toJson(), &error));
        project = original; project.characters[0].colors[1].id = project.characters[0].colors[0].id;
        QVERIFY(!project.validate().isEmpty());
        project = original; project.characters[0].sheets.append({project.characters[0].colors[0].id, "Front", ModelSheetCategory::Turnaround, image({1, 1}, Qt::red)});
        QVERIFY(!project.validate().isEmpty());
        project = original; project.characters[0].colors[0].name = QString(101, 'x'); QVERIFY(!project.validate().isEmpty());
        project = original; project.characters[0].colors[0].category = CharacterColorCategory(42); QVERIFY(project.validate().contains("category"));
        project = original; project.characters[0].colors[0].color = QColor(); QVERIFY(project.validate().contains("RGBA"));
        project = original; project.characters[0].colors.clear();
        for (int i = 0; i <= maxCharacterColors; ++i)
            project.characters[0].colors.append({id(), QString("Color %1").arg(i), CharacterColorCategory::Skin, Qt::red});
        QVERIFY(project.validate().contains("256")); QVERIFY(!Project::fromJson(project.toJson(), &error)); QVERIFY(error.contains("256"));
        project.characters[0].colors.removeLast(); QVERIFY(project.validate().isEmpty());
        const auto templateColors = project.characters[0].colors; project.characters.clear();
        for (int i = 0; i < 32; ++i) {
            CharacterModel character{id(), QString("Character %1").arg(i), {}};
            for (auto color : templateColors) { color.id = id(); character.colors.append(color); }
            project.characters.append(character);
        }
        QVERIFY(project.validate().isEmpty()); QVERIFY(Project::fromJson(project.toJson(), &error));
        project.characters.append({id(), "Overflow", {}, {{id(), "Base", CharacterColorCategory::Skin, Qt::red}}});
        QVERIFY(project.validate().contains("8192")); QVERIFY(!Project::fromJson(project.toJson(), &error)); QVERIFY(error.contains("8192"));
    }
    void invalidSaveCannotReplaceThePreviousProject() {
        QTemporaryDir temp; QString error; const auto path = temp.filePath("movie.toonproj");
        auto project = fixture(); QVERIFY(saveProject(project, path, &error)); const auto original = project.toJson();
        project.characters[0].colors[0].color = QColor();
        QVERIFY(!saveProject(project, path, &error)); auto loaded = loadProject(path, &error); QVERIFY(loaded);
        QCOMPARE(loaded->toJson(), original);
        auto bytes = container(original); bytes[bytes.size() - 8] ^= 1;
        QVERIFY(write(path, bytes)); QVERIFY(!loadProject(path, &error)); QVERIFY(error.contains("checksum"));
        QVERIFY(write(path, container(original) + "trailing")); QVERIFY(!loadProject(path, &error));
    }
};
QTEST_MAIN(CharacterColorStorageTest)
#include "test_character_color_storage.moc"
