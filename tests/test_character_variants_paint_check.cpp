#include "canvas.h"
#include "cel_paint_check_panel.h"
#include "document.h"
#include "main_window.h"
#include "value_slider.h"
#include <QComboBox>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QSignalSpy>
#include <QTimer>
#include <QtTest>

using namespace toon;
namespace {
const QColor dayColor(220, 45, 35);
const QColor nightColor(40, 80, 180, 96);
struct Fixture { Project project; QString character, role, night; };
Fixture fixture() {
    auto project = Project::create(); project.width = 64; project.height = 64;
    QImage pixels(tileSize, tileSize, QImage::Format_ARGB32_Premultiplied); pixels.fill(Qt::transparent);
    { QPainter painter(&pixels); painter.fillRect(QRect(5, 5, 20, 20), dayColor);
      painter.fillRect(QRect(32, 5, 20, 20), QColor(nightColor.red(), nightColor.green(), nightColor.blue())); }
    project.shots[0].layers[0].tiles[{0, 0}] = pixels;
    Document document; document.load(project);
    Fixture result; result.character = document.addCharacter("Hana");
    result.role = document.addCharacterColor(result.character, "Skin base", CharacterColorCategory::Skin, dayColor);
    for (const auto &variant : document.project().characters[0].colorVariants)
        if (variant.name == "Night") result.night = variant.id;
    document.updateCharacterColor(result.character, result.role, "Skin base", CharacterColorCategory::Skin, nightColor, nullptr, result.night);
    result.project = document.project(); return result;
}
QVector<int> paletteIssues(const CelPaintCheckPanel &panel) {
    QVector<int> indices;
    for (int index = 0; index < panel.inspection().issues.size(); ++index)
        if (panel.inspection().issues[index].kind == CelPaintIssueKind::OutsidePalette) indices.append(index);
    return indices;
}
}
class CharacterVariantsPaintCheckTests : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { applyTheme(); }
    void eachLightingPaletteChecksOnlyItsOwnColorsAndKeepsRgbAlphaRules() {
        const auto source = fixture(); QVERIFY(!source.night.isEmpty());
        Canvas canvas; canvas.setProject(source.project); CelPaintCheckPanel panel(&canvas);
        auto *combo = panel.findChild<QComboBox *>("celPaintCheckPalette"); QVERIFY(combo);
        QCOMPARE(combo->count(), 5); QCOMPARE(combo->itemText(1), QString("Hana · Day"));
        QCOMPARE(combo->itemText(2), QString("Hana · Night"));
        panel.findChild<ValueSlider *>("celPaintCheckTolerance")->setValue(0);
        QVERIFY(panel.selectPalette(source.character));
        QSignalSpy scanned(&panel, &CelPaintCheckPanel::inspectionFinished);
        panel.startCheck(); QTRY_COMPARE_WITH_TIMEOUT(scanned.count(), 1, 10000);
        QCOMPARE(panel.inspection().settings.approvedColors, QVector<QColor>{dayColor});
        QCOMPARE(paletteIssues(panel).size(), 1);
        QCOMPARE(panel.inspection().issues[paletteIssues(panel).first()].bounds, QRect(32, 5, 20, 20));
        QVERIFY(panel.selectPalette(source.character, source.night)); QVERIFY(panel.inspection().issues.isEmpty());
        panel.startCheck(); QTRY_COMPARE_WITH_TIMEOUT(scanned.count(), 2, 10000);
        QCOMPARE(panel.inspection().settings.approvedColors, QVector<QColor>{nightColor});
        QCOMPARE(paletteIssues(panel).size(), 1);
        QCOMPARE(panel.inspection().issues[paletteIssues(panel).first()].bounds, QRect(5, 5, 20, 20));
        // The approved swatch keeps alpha, but palette inspection still checks RGB.
        auto *problems = panel.findChild<QListWidget *>("celPaintCheckProblems");
        problems->item(paletteIssues(panel).first())->setCheckState(Qt::Checked);
        QSignalSpy prepared(&panel, &CelPaintCheckPanel::previewFinished);
        panel.previewRepairs(); QTRY_COMPARE_WITH_TIMEOUT(prepared.count(), 1, 10000);
        QCOMPARE(panel.repairs().drawings.size(), 1);
        QCOMPARE(panel.repairs().drawings[0].preview.pixelColor(10, 10), QColor(40, 80, 180));
        QCOMPARE(canvas.project().shots[0].layers[0].tiles, source.project.shots[0].layers[0].tiles);
        QCOMPARE(canvas.document()->history()->count(), 0);
    }
    void stableSelectionSurvivesShotChangesRenameRemovalAndUndo() {
        const auto source = fixture(); Canvas canvas; canvas.setProject(source.project); CelPaintCheckPanel panel(&canvas);
        auto *document = canvas.document(); const auto firstShot = document->activeShotId();
        QVERIFY(panel.selectPalette(source.character, source.night));
        const auto second = document->createShot("Close-up"); QVERIFY(!second.isEmpty());
        QCOMPARE(panel.selectedCharacterId(), source.character); QCOMPARE(panel.selectedVariantId(), source.night);
        QVERIFY(document->selectShot(firstShot)); QCOMPARE(panel.selectedVariantId(), source.night);
        QVERIFY(document->renameCharacterColorVariant(source.character, source.night, "Moonlight"));
        auto *combo = panel.findChild<QComboBox *>("celPaintCheckPalette");
        QCOMPARE(combo->currentText(), QString("Hana · Moonlight"));
        QVERIFY(document->removeCharacterColorVariant(source.character, source.night));
        QCOMPARE(panel.selectedCharacterId(), source.character); QVERIFY(panel.selectedVariantId().isEmpty());
        QCOMPARE(combo->currentText(), QString("Hana · Day"));
        document->history()->undo(); QVERIFY(panel.selectedVariantId().isEmpty());
        QVERIFY(panel.selectPalette(source.character, source.night));
        const auto history = document->history()->count();
        QVERIFY(!panel.selectPalette(source.character, "missing")); QCOMPARE(panel.selectedVariantId(), source.night);
        QCOMPARE(document->history()->count(), history);
        QVERIFY(document->removeCharacter(source.character)); QVERIFY(panel.selectedCharacterId().isEmpty());
        QCOMPARE(combo->currentText(), QString("No palette check"));
    }
    void changingPaletteResolvesPreparedRepairsAndCancelKeepsSelection() {
        const auto source = fixture(); Canvas canvas; canvas.setProject(source.project); CelPaintCheckPanel panel(&canvas);
        QVERIFY(panel.selectPalette(source.character, source.night));
        QSignalSpy scanned(&panel, &CelPaintCheckPanel::inspectionFinished);
        panel.startCheck(); QTRY_COMPARE_WITH_TIMEOUT(scanned.count(), 1, 10000);
        auto *problems = panel.findChild<QListWidget *>("celPaintCheckProblems");
        QVERIFY(!paletteIssues(panel).isEmpty()); problems->item(paletteIssues(panel).first())->setCheckState(Qt::Checked);
        QSignalSpy prepared(&panel, &CelPaintCheckPanel::previewFinished);
        panel.previewRepairs(); QTRY_COMPARE_WITH_TIMEOUT(prepared.count(), 1, 10000); QVERIFY(panel.pending());
        auto *combo = panel.findChild<QComboBox *>("celPaintCheckPalette");
        bool discard = false; int questions = 0; QTimer answer; answer.setInterval(5);
        connect(&answer, &QTimer::timeout, [&] {
            if (auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) {
                ++questions;
                if (!discard) { if (auto *button = box->button(QMessageBox::Cancel)) button->click(); }
                else for (auto *button : box->buttons()) if (box->buttonRole(button) == QMessageBox::DestructiveRole) { button->click(); break; }
            }
        });
        answer.start(); combo->setCurrentIndex(combo->findData(source.character)); answer.stop();
        QCOMPARE(questions, 1); QCOMPARE(panel.selectedVariantId(), source.night);
        QCOMPARE(combo->currentData(Qt::UserRole + 1).toString(), source.night); QVERIFY(panel.pending());
        discard = true; answer.start(); QVERIFY(panel.selectPalette(source.character)); answer.stop();
        QCOMPARE(questions, 2); QVERIFY(panel.selectedVariantId().isEmpty()); QVERIFY(!panel.pending());
        QVERIFY(panel.inspection().issues.isEmpty()); QCOMPARE(canvas.document()->history()->count(), 0);
        QCOMPARE(canvas.project().shots[0].layers[0].tiles, source.project.shots[0].layers[0].tiles);
    }
    void editingVariantInvalidatesBothRunningChecksAndPreparedRepairs() {
        const auto source = fixture(); Canvas canvas; canvas.setProject(source.project); CelPaintCheckPanel panel(&canvas);
        QVERIFY(panel.selectPalette(source.character, source.night));
        QSignalSpy scanned(&panel, &CelPaintCheckPanel::inspectionFinished);
        panel.startCheck(); QVERIFY(panel.busy());
        QVERIFY(canvas.document()->updateCharacterColor(source.character, source.role, "Skin base", CharacterColorCategory::Skin, Qt::green, nullptr, source.night));
        QTRY_VERIFY_WITH_TIMEOUT(!panel.busy(), 10000); QCOMPARE(scanned.count(), 0);
        QVERIFY(panel.inspection().issues.isEmpty()); QVERIFY(!panel.applyRepairs());
        panel.startCheck(); QTRY_COMPARE_WITH_TIMEOUT(scanned.count(), 1, 10000);
        QCOMPARE(panel.inspection().settings.approvedColors, QVector<QColor>{QColor(Qt::green)});
        auto *problems = panel.findChild<QListWidget *>("celPaintCheckProblems");
        QVERIFY(!paletteIssues(panel).isEmpty()); problems->item(paletteIssues(panel).first())->setCheckState(Qt::Checked);
        QSignalSpy prepared(&panel, &CelPaintCheckPanel::previewFinished);
        panel.previewRepairs(); QTRY_COMPARE_WITH_TIMEOUT(prepared.count(), 1, 10000); QVERIFY(panel.pending());
        QVERIFY(canvas.document()->updateCharacterColor(source.character, source.role, "Skin base", CharacterColorCategory::Skin, Qt::yellow, nullptr, source.night));
        QVERIFY(!panel.pending()); QVERIFY(panel.repairs().drawings.isEmpty()); QVERIFY(!panel.applyRepairs());
        QCOMPARE(canvas.project().shots[0].layers[0].tiles, source.project.shots[0].layers[0].tiles);
    }
    void activeStrokeBlocksPaletteSwitchAndEmptyPaletteCannotSilentlySkipValidation() {
        const auto source = fixture(); Canvas canvas; canvas.setProject(source.project); CelPaintCheckPanel panel(&canvas);
        QVERIFY(panel.selectPalette(source.character, source.night));
        QVERIFY(canvas.document()->beginStroke({30, 40}, 1, BrushSettings{}, false));
        QVERIFY(!panel.selectPalette(source.character)); QCOMPARE(panel.selectedVariantId(), source.night);
        QVERIFY(canvas.document()->strokeActive()); canvas.document()->cancelStroke();
        QVERIFY(canvas.document()->removeCharacterColor(source.character, source.role));
        QCOMPARE(panel.selectedVariantId(), source.night);
        QSignalSpy scanned(&panel, &CelPaintCheckPanel::inspectionFinished); panel.startCheck();
        QVERIFY(!panel.busy()); QCOMPARE(scanned.count(), 0); QVERIFY(panel.inspection().settings.approvedColors.isEmpty());
        QVERIFY(panel.selectPalette({})); panel.startCheck(); QTRY_COMPARE_WITH_TIMEOUT(scanned.count(), 1, 10000);
        QVERIFY(panel.inspection().settings.approvedColors.isEmpty()); QVERIFY(paletteIssues(panel).isEmpty());
    }
};
QTEST_MAIN(CharacterVariantsPaintCheckTests)
#include "test_character_variants_paint_check.moc"
