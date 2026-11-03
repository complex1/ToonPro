#include "document.h"
#include "sheet.h"
#include <QJsonArray>
#include <QUuid>
#include <QtTest>

using namespace toon;
namespace {
QString id() { return QUuid::createUuid().toString(QUuid::WithoutBraces); }
}
class CharacterVariantsDocumentTest : public QObject {
    Q_OBJECT
private slots:
    void newCharactersHaveFourPalettesWithIndependentColorValues() {
        Document document; document.load(Project::create()); QString error;
        const auto character = document.addCharacter("Hana", &error); QVERIFY2(!character.isEmpty(), qPrintable(error));
        const auto variants = document.project().characters[0].colorVariants;
        QCOMPARE(variants.size(), 3); QCOMPARE(variants[0].name, QString("Night"));
        QCOMPARE(variants[1].name, QString("Sunset")); QCOMPARE(variants[2].name, QString("Interior"));
        const auto base = QColor("#edc2a4"); const auto night = QColor(82, 94, 148, 153);
        const auto role = document.addCharacterColor(character, "Base", CharacterColorCategory::Skin, base, &error);
        QVERIFY(!role.isEmpty()); const auto history = document.history()->count();
        for (const auto &variant : document.project().characters[0].colorVariants) QCOMPARE(variant.colors.value(role), base);
        QVERIFY(document.updateCharacterColor(character, role, "Face", CharacterColorCategory::Shadow, night, &error, variants[0].id));
        QCOMPARE(document.history()->count(), history + 1);
        const auto &entry = document.project().characters[0];
        QCOMPARE(entry.colors[0].color, base); QCOMPARE(entry.colors[0].name, QString("Face")); QCOMPARE(entry.colors[0].category, CharacterColorCategory::Shadow);
        QCOMPARE(entry.colorVariants[0].colors.value(role), night); QCOMPARE(entry.colorVariants[1].colors.value(role), base);
        QVERIFY(document.updateCharacterColor(character, role, "Face", CharacterColorCategory::Shadow, Qt::yellow));
        QCOMPARE(document.project().characters[0].colorVariants[0].colors.value(role), night);
        document.history()->undo(); document.history()->undo();
        QCOMPARE(document.project().characters[0].colors[0].name, QString("Base"));
        QCOMPARE(document.project().characters[0].colorVariants[0].colors.value(role), base);
        document.history()->redo(); QCOMPARE(document.project().characters[0].colorVariants[0].colors.value(role), night);
    }
    void duplicateRenameDeleteAndShotReusePreserveArtwork() {
        Document document; document.load(Project::create()); QString error;
        const auto character = document.addCharacter("Hana");
        const auto role = document.addCharacterColor(character, "Skin", CharacterColorCategory::Skin, Qt::red);
        const auto night = document.project().characters[0].colorVariants[0].id;
        QVERIFY(document.updateCharacterColor(character, role, "Skin", CharacterColorCategory::Skin, Qt::blue, &error, night));
        const auto firstShot = document.activeShotId(); QVERIFY(!document.createShot("Close-up").isEmpty());
        const auto shots = document.project().toJson().value("shots"); const auto pixels = renderMovieFrame(document.project());
        const auto history = document.history()->count();
        const auto duplicate = document.addCharacterColorVariant(character, "Rainy night", night, &error); QVERIFY2(!duplicate.isEmpty(), qPrintable(error));
        QCOMPARE(document.history()->count(), history + 1); QCOMPARE(document.project().characters[0].colorVariants.last().colors.value(role), QColor(Qt::blue));
        QVERIFY(document.updateCharacterColor(character, role, "Skin", CharacterColorCategory::Skin, Qt::cyan, &error, duplicate));
        QCOMPARE(document.project().characters[0].colorVariants[0].colors.value(role), QColor(Qt::blue));
        QVERIFY(document.renameCharacterColorVariant(character, duplicate, "Moonlight", &error));
        QVERIFY(document.selectShot(firstShot)); QCOMPARE(document.project().characters[0].colorVariants.last().name, QString("Moonlight"));
        QVERIFY(document.removeCharacterColorVariant(character, duplicate, &error)); QCOMPARE(document.project().characters[0].colorVariants.size(), 3);
        document.history()->undo(); QCOMPARE(document.project().characters[0].colorVariants.last().id, duplicate);
        QCOMPARE(document.project().characters[0].colorVariants.last().colors.value(role), QColor(Qt::cyan));
        QCOMPARE(document.project().toJson()["shots"], shots); QCOMPARE(renderMovieFrame(document.project()), pixels);
        QVERIFY(document.removeCharacterColor(character, role)); QVERIFY(document.project().characters[0].colors.isEmpty());
        for (const auto &variant : document.project().characters[0].colorVariants) QVERIFY(variant.colors.isEmpty());
        document.history()->undo(); QCOMPARE(document.project().characters[0].colorVariants.last().colors.value(role), QColor(Qt::cyan));
    }
    void legacyCharactersGainMissingStandardsInOneUndoWithoutResettingEdits() {
        auto project = Project::create(); CharacterModel entry{id(), "Hana", {}, {{id(), "Base", CharacterColorCategory::Skin, Qt::red}}};
        project.characters.append(entry); Document document; document.load(project); QString error;
        const auto night = document.addCharacterColorVariant(entry.id, "NIGHT", {}, &error); QVERIFY(!night.isEmpty());
        QVERIFY(document.updateCharacterColor(entry.id, entry.colors[0].id, "Base", CharacterColorCategory::Skin, Qt::blue, &error, night));
        const auto before = document.project().toJson(); const auto count = document.history()->count();
        QVERIFY(document.addStandardCharacterColorVariants(entry.id, &error)); QCOMPARE(document.history()->count(), count + 1);
        QCOMPARE(document.project().characters[0].colorVariants.size(), 3); QCOMPARE(document.project().characters[0].colorVariants[0].colors.value(entry.colors[0].id), QColor(Qt::blue));
        QVERIFY(document.addStandardCharacterColorVariants(entry.id, &error)); QCOMPARE(document.history()->count(), count + 1);
        document.history()->undo(); QCOMPARE(document.project().toJson(), before);
        document.history()->redo(); QCOMPARE(document.project().characters[0].colorVariants.size(), 3);
    }
    void invalidVariantsAndNoOpsDoNotCreateHistory() {
        Document document; document.load(Project::create()); QString error;
        const auto character = document.addCharacter("Hana");
        const auto role = document.addCharacterColor(character, "Skin", CharacterColorCategory::Skin, Qt::red);
        const auto variant = document.project().characters[0].colorVariants[0].id;
        const auto before = document.project().toJson(); const auto count = document.history()->count();
        for (const auto &name : QStringList{"", "Day", "dAY", "Night", "NIGHT", "Bad\nName", QString(101, 'x')})
            QVERIFY(document.addCharacterColorVariant(character, name, {}, &error).isEmpty());
        QVERIFY(document.addCharacterColorVariant("missing", "New", {}, &error).isEmpty());
        QVERIFY(document.addCharacterColorVariant(character, "New", "missing", &error).isEmpty());
        QVERIFY(!document.renameCharacterColorVariant(character, {}, "Base", &error));
        QVERIFY(!document.removeCharacterColorVariant(character, {}, &error));
        QVERIFY(!document.renameCharacterColorVariant(character, variant, "Sunset", &error));
        QVERIFY(!document.updateCharacterColor(character, role, "Skin", CharacterColorCategory::Skin, Qt::blue, &error, "missing"));
        QVERIFY(!document.updateCharacterColor(character, role, "Skin", CharacterColorCategory::Skin, QColor(), &error, variant));
        QVERIFY(document.renameCharacterColorVariant(character, variant, " Night ", &error));
        QVERIFY(document.updateCharacterColor(character, role, "Skin", CharacterColorCategory::Skin, Qt::red, &error, variant));
        QVERIFY(document.addStandardCharacterColorVariants(character, &error));
        QCOMPARE(document.history()->count(), count); QCOMPARE(document.project().toJson(), before);
    }
    void activeArtworkGesturesBlockVariantOperations() {
        Document document; document.load(Project::create()); QString error;
        const auto character = document.addCharacter("Hana"); const auto variant = document.project().characters[0].colorVariants[0].id;
        const auto role = document.addCharacterColor(character, "Base", CharacterColorCategory::Skin, Qt::red);
        const auto attempt = [&] {
            const auto count = document.history()->count(); const auto colors = document.project().characters[0].colorVariants;
            QVERIFY(document.addCharacterColorVariant(character, "Extra", {}, &error).isEmpty());
            QVERIFY(!document.renameCharacterColorVariant(character, variant, "Changed", &error));
            QVERIFY(!document.removeCharacterColorVariant(character, variant, &error));
            QVERIFY(!document.addStandardCharacterColorVariants(character, &error));
            QVERIFY(!document.updateCharacterColor(character, role, "Changed", CharacterColorCategory::Skin, Qt::blue, &error, variant));
            QVERIFY(error.contains("Finish")); QCOMPARE(document.history()->count(), count); QCOMPARE(document.project().characters[0].colorVariants, colors);
        };
        QVERIFY(document.beginStroke({30, 30}, 1, BrushSettings{}, false)); attempt(); document.endStroke();
        QVERIFY(document.beginTransform()); attempt(); document.cancelTransform();
        QVERIFY(document.beginLayerPropertyEdit()); attempt(); document.cancelLayerPropertyEdit();
    }
    void budgetsAndHistoryAccountForIndependentVariantMaps() {
        Document document; document.load(Project::create()); QString error;
        const auto character = document.addCharacter("Hana"); const auto role = document.addCharacterColor(character, "Skin", CharacterColorCategory::Skin, Qt::red);
        const auto variant = document.project().characters[0].colorVariants[0].id;
        const auto before = document.project();
        QVERIFY(document.updateCharacterColor(character, role, "Skin", CharacterColorCategory::Skin, Qt::blue, &error, variant));
        const auto after = document.project(); QVERIFY(characterModelHistoryCost(before, after) > 0); QCOMPARE(characterModelHistoryCost(after, after), 0);
        while (document.project().characters[0].colorVariants.size() < maxCharacterColorVariants) {
            const auto name = QString("Custom %1").arg(document.project().characters[0].colorVariants.size());
            QVERIFY(!document.addCharacterColorVariant(character, name, {}, &error).isEmpty());
        }
        const auto count = document.history()->count();
        QVERIFY(document.addCharacterColorVariant(character, "Overflow", {}, &error).isEmpty()); QVERIFY(error.contains("16"));
        QCOMPARE(document.history()->count(), count);
        QVERIFY(document.removeCharacter(character)); document.history()->undo();
        QCOMPARE(document.project().characters[0].colorVariants.size(), maxCharacterColorVariants);
    }
};
QTEST_MAIN(CharacterVariantsDocumentTest)
#include "test_character_variants_document.moc"
