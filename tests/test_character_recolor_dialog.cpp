#include "character_recolor_dialog.h"
#include "document.h"
#include "main_window.h"
#include "reference_panel.h"
#include <QComboBox>
#include <QLabel>
#include <QListWidget>
#include <QPainter>
#include <QPushButton>
#include <QSignalSpy>
#include <QTabBar>
#include <QToolButton>
#include <QtTest>

using namespace toon;
namespace {
const QColor dayColor("#dca091"), nightColor("#5467a4");
struct Fixture { Project project; QString character, role, night; };
Fixture fixture() {
    auto project = Project::create(); project.width = 128; project.height = 128;
    QImage pixels(tileSize, tileSize, QImage::Format_ARGB32_Premultiplied); pixels.fill(Qt::transparent);
    { QPainter painter(&pixels); painter.fillRect(QRect(10, 10, 80, 80), dayColor); painter.fillRect(QRect(100, 10, 12, 12), Qt::black); }
    project.shots[0].layers[0].tiles[{0, 0}] = pixels; project.shots[0].layers[0].name = "Skin paint";
    auto second = Project::create().shots[0]; second.number = "C002"; second.layers[0].name = "Skin close-up";
    second.layers[0].tiles[{0, 0}] = pixels; project.shots.append(second);
    Document document; document.load(project); Fixture result;
    result.character = document.addCharacter("Hana");
    result.role = document.addCharacterColor(result.character, "Skin base", CharacterColorCategory::Skin, dayColor);
    for (const auto &variant : document.project().characters[0].colorVariants) if (variant.name == "Night") result.night = variant.id;
    document.updateCharacterColor(result.character, result.role, "Skin base", CharacterColorCategory::Skin, nightColor, nullptr, result.night);
    result.project = document.project(); return result;
}
QListWidget *drawings(CharacterRecolorDialog &dialog) { return dialog.findChild<QListWidget *>("characterRecolorDrawings"); }
QPushButton *apply(CharacterRecolorDialog &dialog) { return dialog.findChild<QPushButton *>("characterRecolorApply"); }
bool contained(QWidget *child, QWidget *parent) {
    return child && child->isVisibleTo(parent) && parent->rect().contains(QRect(child->mapTo(parent, QPoint()), child->size()));
}
}

class CharacterRecolorDialogTests : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { applyTheme(); }
    void previewIsNonDestructiveAndCancelPreservesArtwork() {
        const auto source = fixture(); Document document; document.load(source.project);
        CharacterRecolorDialog dialog(&document, source.character, source.night);
        QCOMPARE(dialog.findChild<QComboBox *>("characterRecolorSourcePalette")->currentData().toString(), QString());
        QCOMPARE(dialog.findChild<QComboBox *>("characterRecolorTargetPalette")->currentData().toString(), source.night);
        QCOMPARE(drawings(dialog)->count(), 1); QCOMPARE(drawings(dialog)->item(0)->checkState(), Qt::Checked);
        QVERIFY(!apply(dialog)->isEnabled());
        QSignalSpy completed(&dialog, &CharacterRecolorDialog::previewFinished);
        dialog.preview(); QVERIFY(dialog.busy()); QTRY_COMPARE_WITH_TIMEOUT(completed.size(), 1, 10000);
        QVERIFY2(dialog.previewResult().error.isEmpty(), qPrintable(dialog.previewResult().error));
        QVERIFY(dialog.previewResult().changedPixels > 0); QVERIFY(apply(dialog)->isEnabled());
        QCOMPARE(document.project().shots[0].layers[0].tiles, source.project.shots[0].layers[0].tiles);
        QCOMPARE(document.history()->count(), 0);
        dialog.reject(); QCOMPARE(dialog.result(), int(QDialog::Rejected));
        QCOMPARE(document.project().shots[0].layers[0].tiles, source.project.shots[0].layers[0].tiles);
        QCOMPARE(document.history()->count(), 0);
    }
    void selectedDrawingsAcrossShotsApplyAsOneUndo() {
        const auto source = fixture(); Document document; document.load(source.project);
        CharacterRecolorDialog dialog(&document, source.character, source.night);
        dialog.findChild<QComboBox *>("characterRecolorScope")->setCurrentIndex(1);
        QCOMPARE(drawings(dialog)->count(), 2); QCOMPARE(drawings(dialog)->item(0)->checkState(), Qt::Checked);
        QCOMPARE(drawings(dialog)->item(1)->checkState(), Qt::Unchecked);
        drawings(dialog)->item(1)->setCheckState(Qt::Checked);
        QSignalSpy completed(&dialog, &CharacterRecolorDialog::previewFinished);
        dialog.preview(); QTRY_COMPARE_WITH_TIMEOUT(completed.size(), 1, 10000);
        QCOMPARE(dialog.previewResult().drawings.size(), 2);
        QVERIFY(dialog.applyPreview()); QCOMPARE(dialog.result(), int(QDialog::Accepted));
        QCOMPARE(document.history()->count(), 1);
        for (const auto &shot : document.project().shots) {
            QCOMPARE(shot.layers[0].tiles.value({0, 0}).pixelColor(20, 20), nightColor);
            QCOMPARE(shot.layers[0].tiles.value({0, 0}).pixelColor(105, 15), QColor(Qt::black));
        }
        document.history()->undo();
        for (int i = 0; i < source.project.shots.size(); ++i) QCOMPARE(document.project().shots[i].layers[0].tiles, source.project.shots[i].layers[0].tiles);
        document.history()->redo(); QCOMPARE(document.project().shots[1].layers[0].tiles.value({0, 0}).pixelColor(20, 20), nightColor);
    }
    void selectionAndPaletteChangesRequireANewPreview() {
        const auto source = fixture(); Document document; document.load(source.project);
        CharacterRecolorDialog dialog(&document, source.character, source.night);
        QSignalSpy completed(&dialog, &CharacterRecolorDialog::previewFinished);
        dialog.preview(); QTRY_COMPARE_WITH_TIMEOUT(completed.size(), 1, 10000); QVERIFY(apply(dialog)->isEnabled());
        drawings(dialog)->item(0)->setCheckState(Qt::Unchecked);
        QVERIFY(dialog.previewResult().drawings.isEmpty()); QVERIFY(!apply(dialog)->isEnabled());
        QVERIFY(!dialog.findChild<QPushButton *>("characterRecolorPreview")->isEnabled());
        drawings(dialog)->item(0)->setCheckState(Qt::Checked);
        dialog.findChild<QComboBox *>("characterRecolorTargetPalette")->setCurrentIndex(0);
        dialog.preview(); QTRY_COMPARE_WITH_TIMEOUT(completed.size(), 2, 10000);
        QCOMPARE(dialog.previewResult().changedPixels, 0); QVERIFY(!apply(dialog)->isEnabled());
        QVERIFY(dialog.findChild<QLabel *>("characterRecolorStatus")->text().contains("already"));
        QVERIFY(document.renameCharacter(source.character, "Hana new"));
        QCOMPARE(dialog.findChild<QComboBox *>("characterRecolorTargetPalette")->currentData().toString(), QString());
        QVERIFY(dialog.previewResult().drawings.isEmpty());
    }
    void changingArtworkOrPalettesCancelsOutstandingWorkAndStalePreview() {
        const auto source = fixture(); Document document; document.load(source.project);
        CharacterRecolorDialog dialog(&document, source.character, source.night);
        QSignalSpy completed(&dialog, &CharacterRecolorDialog::previewFinished);
        dialog.preview(); QVERIFY(document.renameCharacter(source.character, "Hana revised"));
        QTRY_COMPARE_WITH_TIMEOUT(completed.size(), 1, 10000);
        QVERIFY(dialog.previewResult().drawings.isEmpty()); QVERIFY(!apply(dialog)->isEnabled());
        dialog.preview(); QTRY_COMPARE_WITH_TIMEOUT(completed.size(), 2, 10000); QVERIFY(apply(dialog)->isEnabled());
        QVERIFY(document.updateCharacterColor(source.character, source.role, "Skin base", CharacterColorCategory::Skin,
            Qt::blue, nullptr, source.night));
        QVERIFY(dialog.previewResult().drawings.isEmpty()); QVERIFY(!dialog.applyPreview());
        QCOMPARE(document.project().shots[0].layers[0].tiles, source.project.shots[0].layers[0].tiles);
    }
    void lockedDrawingsAreUnavailableAndNarrowingScopeDropsHiddenSelections() {
        auto source = fixture(); source.project.shots[1].layers[0].locked = true;
        Document document; document.load(source.project); CharacterRecolorDialog dialog(&document, source.character, source.night);
        dialog.findChild<QComboBox *>("characterRecolorScope")->setCurrentIndex(1);
        auto *list = drawings(dialog); QCOMPARE(list->count(), 2);
        QVERIFY(!list->item(1)->flags().testFlag(Qt::ItemIsEnabled)); QVERIFY(!list->item(1)->toolTip().isEmpty());
        dialog.findChild<QToolButton *>("characterRecolorSelectAll")->click();
        QCOMPARE(list->item(0)->checkState(), Qt::Checked); QCOMPARE(list->item(1)->checkState(), Qt::Unchecked);
        dialog.findChild<QComboBox *>("characterRecolorScope")->setCurrentIndex(0); QCOMPARE(list->count(), 1);
        QSignalSpy completed(&dialog, &CharacterRecolorDialog::previewFinished);
        dialog.preview(); QTRY_COMPARE_WITH_TIMEOUT(completed.size(), 1, 10000); QCOMPARE(dialog.previewResult().drawings.size(), 1);
    }
    void ambiguousMappingShowsReasonWithoutChangingAnything() {
        const auto source = fixture(); Document document; document.load(source.project);
        const auto role = document.addCharacterColor(source.character, "Costume", CharacterColorCategory::Costume, dayColor);
        QVERIFY(document.updateCharacterColor(source.character, role, "Costume", CharacterColorCategory::Costume,
            Qt::green, nullptr, source.night));
        const auto history = document.history()->count(); CharacterRecolorDialog dialog(&document, source.character, source.night);
        QSignalSpy completed(&dialog, &CharacterRecolorDialog::previewFinished); dialog.preview();
        QTRY_COMPARE_WITH_TIMEOUT(completed.size(), 1, 10000);
        QVERIFY(!dialog.previewResult().error.isEmpty()); QVERIFY(!apply(dialog)->isEnabled());
        QCOMPARE(dialog.findChild<QLabel *>("characterRecolorStatus")->text(), dialog.previewResult().error);
        QCOMPARE(document.history()->count(), history);
        QCOMPARE(document.project().shots[0].layers[0].tiles, source.project.shots[0].layers[0].tiles);
    }
    void comparePreservesZoomPanAndShowsActualChangedColor() {
        const auto source = fixture(); Document document; document.load(source.project);
        CharacterRecolorDialog dialog(&document, source.character, source.night); dialog.show();
        QSignalSpy completed(&dialog, &CharacterRecolorDialog::previewFinished); dialog.preview();
        QTRY_COMPARE_WITH_TIMEOUT(completed.size(), 1, 10000);
        auto *view = dialog.findChild<ReferenceView *>("characterRecolorView"); QVERIFY(view);
        auto *compare = dialog.findChild<QTabBar *>("characterRecolorCompare"); QVERIFY(compare);
        view->setZoom(1.25); const auto transform = view->imageTransform();
        compare->setCurrentIndex(0); QCOMPARE(view->imageTransform(), transform);
        compare->setCurrentIndex(1); QCOMPARE(view->imageTransform(), transform);
        QVERIFY(!view->image().isNull());
        compare->setCurrentIndex(0); const auto original = view->grab().toImage();
        compare->setCurrentIndex(1); const auto recolored = view->grab().toImage(); QVERIFY(original != recolored);
        QCOMPARE(document.history()->count(), 0);
    }
    void compactLayoutAndLargerFontsKeepControlsReachable() {
        const auto source = fixture(); Document document; document.load(source.project);
        CharacterRecolorDialog dialog(&document, source.character, source.night); dialog.resize(540, 680); dialog.show();
        QCoreApplication::processEvents();
        const auto verify = [&] {
            for (const auto *id : {"characterRecolorCharacter", "characterRecolorSourcePalette", "characterRecolorTargetPalette", "characterRecolorScope"})
                QVERIFY2(contained(dialog.findChild<QComboBox *>(id), &dialog), id);
            QVERIFY(contained(drawings(dialog), &dialog)); QVERIFY(contained(apply(dialog), &dialog));
            QVERIFY(contained(dialog.findChild<QTabBar *>("characterRecolorCompare"), &dialog));
            for (auto *button : dialog.findChildren<QToolButton *>()) if (button->objectName().startsWith("characterRecolor")) {
                QVERIFY(contained(button, &dialog)); QVERIFY(!button->accessibleName().isEmpty()); QVERIFY(!button->toolTip().isEmpty());
            }
        };
        verify(); auto font = dialog.font(); font.setPointSizeF(font.pointSizeF() + 4); dialog.setFont(font);
        QCoreApplication::processEvents(); verify();
        QSignalSpy completed(&dialog, &CharacterRecolorDialog::previewFinished); dialog.preview();
        QTRY_COMPARE_WITH_TIMEOUT(completed.size(), 1, 10000); verify();
    }
};
QTEST_MAIN(CharacterRecolorDialogTests)
#include "test_character_recolor_dialog.moc"
