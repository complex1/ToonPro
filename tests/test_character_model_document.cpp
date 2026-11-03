#include "document.h"
#include <QUuid>
#include <QtTest>

using namespace toon;
namespace {
QImage image(QSize size = {80, 120}, QColor color = Qt::red) {
    QImage result(size, QImage::Format_ARGB32_Premultiplied); result.fill(color); return result;
}
QVector<CharacterModelSheet> sheets() {
    return {{"untrusted-source-id", "Front", ModelSheetCategory::Turnaround, image()},
        {"untrusted-source-id", "Smile", ModelSheetCategory::Expression, image({40, 60}, Qt::blue)},
        {{}, "School uniform", ModelSheetCategory::Costume, image({60, 100}, Qt::green)}};
}
}
class CharacterModelDocumentTest : public QObject {
    Q_OBJECT
private slots:
    void libraryIsSharedAcrossShotsAndEveryMutationUndoes() {
        Document document; document.load(Project::create()); QString error;
        const auto firstShot = document.activeShotId();
        const auto character = document.addCharacter("  Aiko  ", &error); QVERIFY2(!character.isEmpty(), qPrintable(error));
        QCOMPARE(document.project().characters[0].name, QString("Aiko"));
        QVERIFY(document.addCharacterSheets(character, sheets(), &error));
        QCOMPARE(document.history()->count(), 2); QCOMPARE(document.project().characters[0].sheets.size(), 3);
        const auto original = document.project().characters[0];
        for (const auto &sheet : original.sheets) { QVERIFY(!QUuid(sheet.id).isNull()); QVERIFY(sheet.id != "untrusted-source-id"); }
        QVERIFY(original.sheets[0].id != original.sheets[1].id);
        const auto secondShot = document.createShot("City"); QVERIFY(!secondShot.isEmpty());
        QCOMPARE(document.project().characters[0].sheets[0].image.cacheKey(), original.sheets[0].image.cacheKey());
        const auto history = document.history()->count();
        QVERIFY(document.selectShot(firstShot)); QVERIFY(document.selectShot(secondShot));
        QCOMPARE(document.history()->count(), history); QCOMPARE(document.project().characters.size(), 1);
        QVERIFY(document.renameCharacter(character, "Aiko Sato", &error));
        const auto sheetId = original.sheets[0].id;
        QVERIFY(document.renameCharacterSheet(character, sheetId, "Three-quarter", &error));
        QVERIFY(document.setCharacterSheetCategory(character, sheetId, ModelSheetCategory::Costume, &error));
        QCOMPARE(document.activeShotId(), secondShot);
        document.history()->undo(); QCOMPARE(document.project().characters[0].sheets[0].category, ModelSheetCategory::Turnaround);
        document.history()->undo(); QCOMPARE(document.project().characters[0].sheets[0].name, QString("Front"));
        document.history()->undo(); QCOMPARE(document.project().characters[0].name, QString("Aiko"));
        document.history()->redo(); QCOMPARE(document.project().characters[0].name, QString("Aiko Sato"));
        QVERIFY(document.removeCharacterSheet(character, sheetId, &error)); QCOMPARE(document.project().characters[0].sheets.size(), 2);
        document.history()->undo(); QCOMPARE(document.project().characters[0].sheets[0].id, sheetId);
        QCOMPARE(document.project().characters[0].sheets[0].image, original.sheets[0].image);
        QVERIFY(document.removeCharacter(character, &error)); QVERIFY(document.project().characters.isEmpty());
        document.history()->undo(); QCOMPARE(document.project().characters[0].id, character);
        QCOMPARE(document.project().characters[0].sheets.size(), 3);
        document.history()->redo(); QVERIFY(document.project().characters.isEmpty());
    }
    void batchImportIsAtomicAndRejectsInvalidInput() {
        Document document; document.load(Project::create()); QString error;
        const auto character = document.addCharacter("Aiko"); const auto before = document.project().toJson();
        const auto history = document.history()->count();
        auto batch = sheets(); batch[2].category = ModelSheetCategory(999);
        QVERIFY(!document.addCharacterSheets(character, batch, &error)); QVERIFY(error.contains("category"));
        QCOMPARE(document.project().toJson(), before); QCOMPARE(document.history()->count(), history);
        batch = sheets(); batch[1].image = {};
        QVERIFY(!document.addCharacterSheets(character, batch, &error)); QCOMPARE(document.project().toJson(), before);
        QVERIFY(!document.addCharacterSheets(character, {}, &error));
        QVERIFY(!document.addCharacterSheets("missing", sheets(), &error));
        batch = sheets(); batch[0].image = batch[0].image.convertToFormat(QImage::Format_RGB32);
        QVERIFY(document.addCharacterSheets(character, batch, &error)); QCOMPARE(document.history()->count(), history + 1);
        QCOMPARE(document.project().characters[0].sheets[0].image.format(), QImage::Format_ARGB32_Premultiplied);
        document.history()->undo(); QCOMPARE(document.project().toJson(), before);
        document.history()->redo(); QCOMPARE(document.project().characters[0].sheets.size(), 3);
    }
    void invalidNamesMissingTargetsAndNoChangesDoNotAddHistory() {
        Document document; QString error;
        QVERIFY(document.addCharacter("Aiko", &error).isEmpty()); QVERIFY(!error.isEmpty());
        document.load(Project::create()); const auto character = document.addCharacter("Aiko");
        QVERIFY(document.addCharacterSheets(character, sheets()));
        const auto sheet = document.project().characters[0].sheets[0].id;
        const auto before = document.project().toJson(); const auto history = document.history()->count();
        QVERIFY(document.addCharacter("aIkO", &error).isEmpty()); QVERIFY(error.contains("unique"));
        QVERIFY(document.addCharacter("\n  ", &error).isEmpty());
        QVERIFY(document.addCharacter(QString(101, 'x'), &error).isEmpty());
        QVERIFY(!document.renameCharacter(character, "Aiko\nHero", &error));
        QVERIFY(!document.renameCharacterSheet(character, sheet, "", &error));
        QVERIFY(!document.renameCharacter("missing", "Renamed", &error));
        QVERIFY(!document.removeCharacter("missing", &error));
        QVERIFY(!document.removeCharacterSheet(character, "missing", &error));
        QVERIFY(!document.setCharacterSheetCategory(character, sheet, ModelSheetCategory(-1), &error));
        QVERIFY(document.renameCharacter(character, " Aiko ", &error)); QVERIFY(error.isEmpty());
        QVERIFY(document.renameCharacterSheet(character, sheet, " Front ", &error));
        QVERIFY(document.setCharacterSheetCategory(character, sheet, ModelSheetCategory::Turnaround, &error));
        QCOMPARE(document.history()->count(), history); QCOMPARE(document.project().toJson(), before);
    }
    void activeStrokeAndTransformCannotBeDiscardedByLibraryEdits() {
        Document document; document.load(Project::create()); QString error;
        const auto character = document.addCharacter("Aiko"); QVERIFY(document.addCharacterSheets(character, sheets()));
        const auto sheet = document.project().characters[0].sheets[0].id;
        const auto attemptAll = [&] {
            QVERIFY(document.addCharacter("New", &error).isEmpty());
            QVERIFY(!document.renameCharacter(character, "Renamed", &error));
            QVERIFY(!document.removeCharacter(character, &error));
            QVERIFY(!document.addCharacterSheets(character, sheets(), &error));
            QVERIFY(!document.renameCharacterSheet(character, sheet, "Side", &error));
            QVERIFY(!document.setCharacterSheetCategory(character, sheet, ModelSheetCategory::Costume, &error));
            QVERIFY(!document.removeCharacterSheet(character, sheet, &error));
            QVERIFY(error.contains("Finish"));
        };
        BrushSettings brush; QVERIFY(document.beginStroke({100, 100}, 1, brush, false));
        const auto history = document.history()->count(); attemptAll(); QVERIFY(document.strokeActive());
        QCOMPARE(document.history()->count(), history);
        document.continueStroke({150, 150}, 1); document.endStroke();
        QVERIFY(document.beginTransform()); attemptAll(); QVERIFY(document.transformActive()); document.cancelTransform();
        QVERIFY(document.beginLayerPropertyEdit()); attemptAll(); QVERIFY(document.layerPropertyEditActive()); document.cancelLayerPropertyEdit();
        QCOMPARE(document.project().characters[0].name, QString("Aiko"));
    }
    void separateLibraryBudgetsAndHistoryAccountForImages() {
        Document document; auto project = Project::create();
        for (int i = 0; i < maxProjectReferences; ++i)
            project.shots[0].references.append({QUuid::createUuid().toString(QUuid::WithoutBraces), QString::number(i), image({1, 1})});
        document.load(project); const auto character = document.addCharacter("Aiko");
        const auto before = document.project(); const auto batch = sheets();
        QVERIFY(document.addCharacterSheets(character, batch));
        const auto after = document.project();
        const auto cost = batch[0].image.sizeInBytes() + batch[1].image.sizeInBytes() + batch[2].image.sizeInBytes();
        QCOMPARE(characterModelHistoryCost(before, after), cost);
        QVERIFY(document.renameCharacter(character, "Hero"));
        QCOMPARE(characterModelHistoryCost(after, document.project()), 0);
        auto shared = after; shared.characters[0].sheets.append(shared.characters[0].sheets[0]);
        QCOMPARE(characterModelHistoryCost(after, shared), 0); // One retained buffer, even when referenced twice.
        QVector<CharacterModelSheet> tooMany(maxProjectCharacterSheets, batch[0]); QString error;
        const auto history = document.history()->count();
        QVERIFY(!document.addCharacterSheets(character, tooMany, &error)); QVERIFY(error.contains("256"));
        QCOMPARE(document.history()->count(), history);
        auto large = batch[0]; large.image = image({4000, 4000});
        QVERIFY(!document.addCharacterSheets(character, {large, large, large, large}, &error));
        QVERIFY(error.contains("64 megapixel")); QCOMPARE(document.history()->count(), history);
    }
};
QTEST_MAIN(CharacterModelDocumentTest)
#include "test_character_model_document.moc"
