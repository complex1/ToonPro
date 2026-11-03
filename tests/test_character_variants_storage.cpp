#include "project.h"
#include "sheet.h"
#include <QFile>
#include <QJsonArray>
#include <QTemporaryDir>
#include <QUuid>
#include <QtTest>

using namespace toon;
namespace {
QString id() { return QUuid::createUuid().toString(QUuid::WithoutBraces); }
QImage image(QSize size = {24, 32}) {
    QImage result(size, QImage::Format_ARGB32_Premultiplied); result.fill(QColor(40, 80, 120, 128)); return result;
}
Project fixture() {
    auto project = Project::create(); project.width = 128; project.height = 128;
    CharacterModel character{id(), "Hana", {}, {{id(), "Skin base", CharacterColorCategory::Skin, QColor("#edc2a4")},
        {id(), "Hair", CharacterColorCategory::Hair, QColor("#30384a")}}};
    character.colorVariants = standardCharacterColorVariants(character.colors);
    character.colorVariants[0].colors[character.colors[0].id] = QColor(82, 94, 148, 153);
    character.colorVariants[1].colors[character.colors[1].id] = QColor("#a35837");
    project.characters.append(character); return project;
}
QJsonObject withVariants(QJsonObject manifest, const QJsonValue &variants) {
    auto characters = manifest["characters"].toArray(); auto character = characters[0].toObject();
    character["colorVariants"] = variants; characters[0] = character; manifest["characters"] = characters; return manifest;
}
}

class CharacterVariantsStorageTest : public QObject {
    Q_OBJECT
private slots:
    void roundTripKeepsIndependentColorsAndSharedRoles() {
        const auto project = fixture(); QString error; QTemporaryDir directory;
        QVERIFY2(project.validate().isEmpty(), qPrintable(project.validate()));
        QCOMPARE(project.toJson()["version"].toInt(), 30); QVERIFY(project.toJson()["drawingAids"].isObject());
        const auto path = directory.filePath("lighting.toonproj"); QVERIFY2(saveProject(project, path, &error), qPrintable(error));
        QFile file(path); QVERIFY(file.open(QIODevice::ReadOnly)); QCOMPARE(file.read(10), QByteArray("TOONPRO30\n")); file.close();
        const auto loaded = loadProject(path, &error); QVERIFY2(loaded.has_value(), qPrintable(error));
        QCOMPARE(loaded->toJson(), project.toJson()); QCOMPARE(loaded->characters[0].colorVariants, project.characters[0].colorVariants);
        QCOMPARE(characterVariantColors(loaded->characters[0]), loaded->characters[0].colors);
        const auto night = characterVariantColors(loaded->characters[0], loaded->characters[0].colorVariants[0].id);
        QCOMPARE(night[0].color, QColor(82, 94, 148, 153)); QCOMPARE(night[0].id, loaded->characters[0].colors[0].id);
        QCOMPARE(night[0].name, QString("Skin base")); QVERIFY(characterVariantColors(loaded->characters[0], id()).isEmpty());
        auto without = project; without.characters.clear();
        QCOMPARE(renderMovieFrame(*loaded), renderMovieFrame(without));
        QCOMPARE(renderSheet(*loaded, loaded->shots[0]), renderSheet(without, without.shots[0]));
    }
    void variantsCoexistWithEmbeddedSheetsPinsDraftsAndArtwork() {
        auto project = fixture(); QString error; QTemporaryDir directory;
        project.characters[0].sheets.append({id(), "Front", ModelSheetCategory::Turnaround, image()});
        project.characters.append({id(), "Ren", {}}); // Required empty arrays in mixed format-30 libraries.
        project.shots[0].layers[0].tiles[{0, 0}] = image({tileSize, tileSize});
        project.shots[0].references.append({id(), "Room", image()});
        auto shot = Project::create().shots[0]; shot.number = "C002"; project.shots.append(shot);
        LightTablePin pin; pin.id = id(); pin.name = "Pose"; pin.image = image(); pin.bounds = QRectF(0, 0, 24, 32);
        project.drawingAids.lightTable.pins.append(pin); project.drawingAids.lightTable.selectedPinId = pin.id;
        CelShadingDraft draft; draft.shotId = shot.id; draft.outlineLayerId = shot.layers[0].id; draft.frame = 4;
        QPainterPath path(QPointF(10, 10)); path.cubicTo(15, 20, 35, 10, 45, 40);
        draft.boundaries.append({path, CelShadeKind::Shadow, Qt::cyan, 2}); draft.operations.append(false);
        project.drawingAids.shadingDrafts.append(draft);
        const auto output = directory.filePath("mixed.toonproj"); QVERIFY2(saveProject(project, output, &error), qPrintable(error));
        const auto loaded = loadProject(output, &error); QVERIFY2(loaded.has_value(), qPrintable(error));
        QCOMPARE(loaded->toJson(), project.toJson()); QVERIFY(drawingAidsEqual(loaded->drawingAids, project.drawingAids));
        QCOMPARE(loaded->characters[0].sheets[0].image, project.characters[0].sheets[0].image);
        QCOMPARE(loaded->shots[0].layers[0].tiles, project.shots[0].layers[0].tiles);
        QCOMPARE(loaded->shots[0].references[0].image, project.shots[0].references[0].image);
    }
    void legacyProjectsRemainDayOnlyAndKeepTheirFormats() {
        auto project = fixture(); project.characters[0].colorVariants.clear(); QString error;
        QCOMPARE(project.toJson()["version"].toInt(), 28);
        auto loaded = Project::fromJson(project.toJson(), &error); QVERIFY2(loaded.has_value(), qPrintable(error));
        QVERIFY(loaded->characters[0].colorVariants.isEmpty()); QCOMPARE(loaded->toJson(), project.toJson());
        project.characters[0].colors.clear(); QCOMPARE(project.toJson()["version"].toInt(), 27);
        project.characters.clear(); QCOMPARE(project.toJson()["version"].toInt(), 19);
        project.drawingAids.lightTable.visible = false; QCOMPARE(project.toJson()["version"].toInt(), 29);
        auto downgraded = fixture().toJson(); downgraded["version"] = 29;
        QVERIFY(!Project::fromJson(downgraded, &error)); QVERIFY(error.contains("30"));
        auto future = fixture().toJson(); future["version"] = latestProjectFormatVersion + 1;
        QVERIFY(!Project::fromJson(future, &error));
    }
    void malformedNamesIdsMapsAndColorsAreRejected() {
        const auto project = fixture(); const auto manifest = project.toJson(); QString error;
        const auto original = manifest["characters"].toArray()[0].toObject()["colorVariants"].toArray();
        for (const auto &name : QStringList{"", "Day", "dAY", " Night", "Night\nBlue", QString(101, 'x')}) {
            auto variants = original; auto first = variants[0].toObject(); first["name"] = name; variants[0] = first;
            QVERIFY2(!Project::fromJson(withVariants(manifest, variants), &error), qPrintable(name));
        }
        auto variants = original; auto first = variants[0].toObject(); first["name"] = "sUNSET"; variants[0] = first;
        QVERIFY(!Project::fromJson(withVariants(manifest, variants), &error));
        for (const auto &badId : QStringList{"not-an-id", project.shots[0].id, project.characters[0].colors[0].id, project.characters[0].colorVariants[1].id}) {
            variants = original; first = variants[0].toObject(); first["id"] = badId; variants[0] = first;
            QVERIFY(!Project::fromJson(withVariants(manifest, variants), &error));
        }
        const auto colors = original[0].toObject()["colors"].toObject(); const auto role = project.characters[0].colors[0].id;
        for (const auto &value : QList<QJsonValue>{QJsonValue("red"), QJsonValue("#abcdef"), QJsonValue("#gg112233"), QJsonValue(12), QJsonValue(QJsonValue::Null)}) {
            auto bad = colors; bad[role] = value; variants = original; first = variants[0].toObject(); first["colors"] = bad; variants[0] = first;
            QVERIFY(!Project::fromJson(withVariants(manifest, variants), &error));
        }
        auto bad = colors; bad.remove(role); variants = original; first = variants[0].toObject(); first["colors"] = bad; variants[0] = first;
        QVERIFY(!Project::fromJson(withVariants(manifest, variants), &error));
        bad[id()] = "#ff112233"; first["colors"] = bad; variants[0] = first;
        QVERIFY(!Project::fromJson(withVariants(manifest, variants), &error));
        variants = original; first = variants[0].toObject(); first["unknown"] = true; variants[0] = first;
        QVERIFY(!Project::fromJson(withVariants(manifest, variants), &error));
        QVERIFY(!Project::fromJson(withVariants(manifest, QJsonObject{}), &error));
        auto missing = manifest; auto characters = missing["characters"].toArray(); auto character = characters[0].toObject();
        character.remove("colorVariants"); characters[0] = character; missing["characters"] = characters;
        QVERIFY(!Project::fromJson(missing, &error));
    }
    void budgetsAndFailedSavesProtectThePreviousFile() {
        auto project = fixture(); const auto original = project; QString error; QTemporaryDir directory;
        const auto path = directory.filePath("safe.toonproj"); QVERIFY(saveProject(project, path, &error));
        while (project.characters[0].colorVariants.size() <= maxCharacterColorVariants) {
            auto variant = project.characters[0].colorVariants[0]; variant.id = id(); variant.name = "Extra " + variant.id;
            project.characters[0].colorVariants.append(variant);
        }
        QVERIFY(project.validate().contains("16")); QVERIFY(!Project::fromJson(project.toJson(), &error));
        QVERIFY(!saveProject(project, path, &error)); const auto loaded = loadProject(path, &error); QVERIFY(loaded);
        QCOMPARE(loaded->toJson(), original.toJson());
        project = Project::create();
        for (int c = 0; c < 16; ++c) {
            CharacterModel character{id(), QString("Character %1").arg(c), {}};
            for (int n = 0; n < maxCharacterColors; ++n) character.colors.append({id(), QString("Color %1").arg(n), CharacterColorCategory::Skin, Qt::red});
            for (int v = 0; v < maxCharacterColorVariants; ++v) {
                CharacterColorVariant variant{id(), QString("Variant %1").arg(v), {}};
                for (const auto &swatch : character.colors) variant.colors[swatch.id] = Qt::blue;
                character.colorVariants.append(variant);
            }
            project.characters.append(character);
        }
        QVERIFY(project.validate().contains("65536")); QVERIFY(!Project::fromJson(project.toJson(), &error)); QVERIFY(error.contains("65536"));
    }
};
QTEST_MAIN(CharacterVariantsStorageTest)
#include "test_character_variants_storage.moc"
