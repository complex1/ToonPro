#include "document.h"
#include "sheet.h"
#include <QJsonArray>
#include <QUuid>
#include <QtTest>

using namespace toon;
namespace {
QString id() { return QUuid::createUuid().toString(QUuid::WithoutBraces); }
QImage image() {
    QImage result(32, 48, QImage::Format_ARGB32_Premultiplied); result.fill(Qt::cyan); return result;
}
}

class CharacterColorDocumentTest : public QObject {
    Q_OBJECT
private slots:
    void editsAreSharedAcrossShotsAndUndoRestoresEveryField() {
        Document document; document.load(Project::create()); QString error;
        const auto character = document.addCharacter("Hana");
        const auto other = document.addCharacter("Ren");
        const auto firstShot = document.activeShotId();
        const auto secondShot = document.createShot("Close-up"); QVERIFY(!secondShot.isEmpty());
        const auto shotsBefore = document.project().toJson().value("shots");
        const auto history = document.history()->count();
        const auto color = document.addCharacterColor(character, " Skin base ", CharacterColorCategory::Skin, QColor("#edb995"), &error);
        QVERIFY2(!color.isEmpty(), qPrintable(error)); QCOMPARE(document.history()->count(), history + 1);
        QVERIFY(!QUuid(color).isNull()); QCOMPARE(document.project().characters[0].colors[0].name, QString("Skin base"));
        QVERIFY(document.project().characters[1].colors.isEmpty());
        QVERIFY(document.selectShot(firstShot)); QCOMPARE(document.project().characters[0].colors[0].id, color);
        QVERIFY(document.selectShot(secondShot)); QCOMPARE(document.history()->count(), history + 1);
        QVERIFY(document.updateCharacterColor(character, color, " Hair shadow ", CharacterColorCategory::Shadow, QColor(32, 64, 96, 128), &error));
        QCOMPARE(document.history()->count(), history + 2);
        const auto updated = document.project().characters[0].colors[0];
        QCOMPARE(updated.name, QString("Hair shadow")); QCOMPARE(updated.category, CharacterColorCategory::Shadow);
        QCOMPARE(updated.color.rgba(), qRgba(32, 64, 96, 128));
        document.history()->undo(); QCOMPARE(document.project().characters[0].colors[0].name, QString("Skin base"));
        QCOMPARE(document.project().characters[0].colors[0].category, CharacterColorCategory::Skin);
        document.history()->redo(); QCOMPARE(document.project().characters[0].colors[0], updated);
        QVERIFY(document.removeCharacterColor(character, color, &error)); QVERIFY(document.project().characters[0].colors.isEmpty());
        document.history()->undo(); QCOMPARE(document.project().characters[0].colors[0], updated);
        QCOMPARE(document.project().toJson().value("shots"), shotsBefore);
        QVERIFY(!document.removeCharacterColor(other, color, &error));
    }
    void normalizationNoOpsAndInvalidInputDoNotCreateStrayHistory() {
        Document document; QString error;
        QVERIFY(document.addCharacterColor("missing", "Base", CharacterColorCategory::Skin, Qt::red, &error).isEmpty());
        QVERIFY(!error.isEmpty()); document.load(Project::create());
        const auto character = document.addCharacter("Hana");
        const auto source = QColor::fromRgbF(.12345, .54321, .67891, .33333);
        const auto color = document.addCharacterColor(character, "Base", CharacterColorCategory::Skin, source, &error);
        QVERIFY(!color.isEmpty()); QCOMPARE(document.project().characters[0].colors[0].color, QColor::fromRgba(source.rgba()));
        QCOMPARE(document.project().characters[0].colors[0].color.spec(), QColor::Rgb);
        const auto before = document.project().toJson(); const auto history = document.history()->count();
        QVERIFY(document.updateCharacterColor(character, color, " Base ", CharacterColorCategory::Skin, source, &error));
        QVERIFY(error.isEmpty()); QCOMPARE(document.history()->count(), history);
        QVERIFY(document.addCharacterColor("missing", "Base", CharacterColorCategory::Skin, Qt::red, &error).isEmpty());
        QVERIFY(document.addCharacterColor(character, "", CharacterColorCategory::Skin, Qt::red, &error).isEmpty());
        QVERIFY(document.addCharacterColor(character, "Base\nSkin", CharacterColorCategory::Skin, Qt::red, &error).isEmpty());
        QVERIFY(document.addCharacterColor(character, QString(101, 'x'), CharacterColorCategory::Skin, Qt::red, &error).isEmpty());
        QVERIFY(document.addCharacterColor(character, "Bad", CharacterColorCategory(99), Qt::red, &error).isEmpty());
        QVERIFY(document.addCharacterColor(character, "Bad", CharacterColorCategory::Skin, QColor(), &error).isEmpty());
        QVERIFY(!document.updateCharacterColor(character, color, "", CharacterColorCategory::Skin, Qt::red, &error));
        QVERIFY(!document.updateCharacterColor(character, color, "Bad", CharacterColorCategory(-1), Qt::red, &error));
        QVERIFY(!document.updateCharacterColor(character, color, "Bad", CharacterColorCategory::Skin, QColor(), &error));
        QVERIFY(!document.updateCharacterColor(character, "missing", "Base", CharacterColorCategory::Skin, Qt::red, &error));
        QVERIFY(!document.removeCharacterColor(character, "missing", &error));
        QCOMPARE(document.history()->count(), history); QCOMPARE(document.project().toJson(), before);
        const auto hsv = QColor::fromHsvF(.45, .6, .8, .5);
        QVERIFY(document.updateCharacterColor(character, color, "Base", CharacterColorCategory::Skin, hsv, &error));
        QCOMPARE(document.project().characters[0].colors[0].color, QColor::fromRgba(hsv.rgba()));
    }
    void deletingACharacterRestoresItsColorsAndSheetsTogether() {
        Document document; document.load(Project::create());
        const auto character = document.addCharacter("Hana"); const auto pixels = image();
        QVERIFY(document.addCharacterSheets(character, {{{}, "Front", ModelSheetCategory::Turnaround, pixels}}));
        const auto color = document.addCharacterColor(character, "Skin base", CharacterColorCategory::Skin, Qt::red);
        QVERIFY(!color.isEmpty()); const auto before = document.project().toJson();
        const auto frame = renderMovieFrame(document.project());
        QVERIFY(document.removeCharacter(character)); QVERIFY(document.project().characters.isEmpty());
        document.history()->undo(); QCOMPARE(document.project().toJson(), before);
        QCOMPARE(document.project().characters[0].sheets[0].image, pixels);
        QCOMPARE(document.project().characters[0].colors[0].id, color);
        QCOMPARE(renderMovieFrame(document.project()), frame);
        document.history()->redo(); QVERIFY(document.project().characters.isEmpty());
    }
    void activeGesturesCannotBeLostByColorEdits() {
        Document document; document.load(Project::create()); QString error;
        const auto character = document.addCharacter("Hana");
        const auto color = document.addCharacterColor(character, "Base", CharacterColorCategory::Skin, Qt::red);
        const auto attempt = [&] {
            const auto history = document.history()->count();
            QVERIFY(document.addCharacterColor(character, "New", CharacterColorCategory::Hair, Qt::blue, &error).isEmpty());
            QVERIFY(!document.updateCharacterColor(character, color, "Changed", CharacterColorCategory::Hair, Qt::blue, &error));
            QVERIFY(!document.removeCharacterColor(character, color, &error));
            QVERIFY(error.contains("Finish")); QCOMPARE(document.history()->count(), history);
        };
        BrushSettings brush; QVERIFY(document.beginStroke({100, 100}, 1, brush, false)); attempt(); QVERIFY(document.strokeActive());
        document.continueStroke({150, 150}, 1); document.endStroke();
        QVERIFY(document.beginTransform()); attempt(); QVERIFY(document.transformActive()); document.cancelTransform();
        QVERIFY(document.beginLayerPropertyEdit()); attempt(); QVERIFY(document.layerPropertyEditActive()); document.cancelLayerPropertyEdit();
        QCOMPARE(document.project().characters[0].colors[0].name, QString("Base"));
        QCOMPARE(document.project().characters[0].colors[0].color, QColor(Qt::red));
    }
    void budgetsAndChangedMetadataAreAccountedInHistory() {
        Document document; auto project = Project::create();
        CharacterModel character{id(), "Hana", {}};
        for (int index = 0; index < maxCharacterColors; ++index)
            character.colors.append({id(), QString("Color %1").arg(index), CharacterColorCategory::Skin, Qt::red});
        project.characters.append(character); document.load(project); QString error;
        QVERIFY(document.addCharacterColor(character.id, "Overflow", CharacterColorCategory::Skin, Qt::blue, &error).isEmpty());
        QVERIFY(error.contains("256")); QCOMPARE(document.history()->count(), 0);
        const auto color = character.colors[0].id; const auto before = document.project();
        QVERIFY(document.updateCharacterColor(character.id, color, "Face", CharacterColorCategory::Shadow, Qt::blue));
        const auto after = document.project(); QVERIFY(characterModelHistoryCost(before, after) > 0);
        QCOMPARE(characterModelHistoryCost(after, after), 0);
        QVERIFY(document.renameCharacter(character.id, "Renamed"));
        QCOMPARE(characterModelHistoryCost(after, document.project()), 0);
        QVERIFY(document.removeCharacter(character.id)); QVERIFY(characterModelHistoryCost(after, document.project()) > 0);
        document.history()->undo(); QCOMPARE(document.project().characters[0].colors.size(), maxCharacterColors);

        project.characters.clear();
        for (int index = 0; index < 32; ++index) {
            CharacterModel entry{id(), QString("Character %1").arg(index), {}};
            for (auto swatch : character.colors) { swatch.id = id(); entry.colors.append(swatch); }
            project.characters.append(entry);
        }
        project.characters.append({id(), "Empty", {}}); document.load(project);
        QVERIFY(document.addCharacterColor(project.characters.last().id, "Overflow", CharacterColorCategory::Skin, Qt::blue, &error).isEmpty());
        QVERIFY(error.contains("8192")); QCOMPARE(document.history()->count(), 0);
    }
};
QTEST_MAIN(CharacterColorDocumentTest)
#include "test_character_color_document.moc"
