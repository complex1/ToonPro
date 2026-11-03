#include "character_color_panel.h"
#include "document.h"
#include "main_window.h"
#include <QAction>
#include <QComboBox>
#include <QDialog>
#include <QInputDialog>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QSignalSpy>
#include <QStyle>
#include <QTabBar>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolButton>
#include <QtTest>
#include <functional>

using namespace toon;
namespace {
Project fixture() {
    auto project = Project::create(); project.width = 128; project.height = 128;
    QImage pixels(tileSize, tileSize, QImage::Format_ARGB32_Premultiplied); pixels.fill(QColor("#e2b3a1"));
    project.shots[0].layers[0].tiles[{0, 0}] = pixels;
    auto second = Project::create().shots[0]; second.number = "C002"; project.shots.append(second); return project;
}
QString variantId(const Document &document, const QString &characterId, const QString &name) {
    for (const auto &character : document.project().characters) if (character.id == characterId)
        for (const auto &variant : character.colorVariants) if (variant.name == name) return variant.id;
    return {};
}
QColor roleColor(const Document &document, const QString &characterId, const QString &roleId, const QString &variantId = {}) {
    for (const auto &character : document.project().characters) if (character.id == characterId)
        for (const auto &color : characterVariantColors(character, variantId)) if (color.id == roleId) return color.color;
    return {};
}
bool dialogAction(const std::function<void()> &open, const std::function<bool(QDialog *)> &interact) {
    QTimer timer, timeout; timer.setInterval(5); timeout.setSingleShot(true); bool handled = false;
    QObject::connect(&timer, &QTimer::timeout, [&] {
        auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget()); if (!dialog || !dialog->isVisible()) return;
        timer.stop(); handled = interact(dialog); if (!handled) dialog->reject();
    });
    QObject::connect(&timeout, &QTimer::timeout, [&] {
        timer.stop(); if (auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget())) dialog->reject();
    });
    timer.start(); timeout.start(2500); open(); timer.stop(); timeout.stop(); return handled;
}
bool enterName(QDialog *dialog, const QString &name) {
    auto *input = qobject_cast<QInputDialog *>(dialog); if (!input) return false;
    input->setTextValue(name); input->accept(); return true;
}
bool contained(QWidget *child, QWidget *parent) {
    return child && child->isVisibleTo(parent) && parent->rect().contains(QRect(child->mapTo(parent, QPoint()), child->size()));
}
}

class CharacterVariantsPanelTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { applyTheme(); }
    void lightingNavigationIsViewOnlyAndRememberedAcrossShotsAndCharacters() {
        Document document; document.load(fixture()); const auto hana = document.addCharacter("Hana"), ren = document.addCharacter("Ren");
        const auto role = document.addCharacterColor(hana, "Skin", CharacterColorCategory::Skin, Qt::red);
        const auto night = variantId(document, hana, "Night"), sunset = variantId(document, ren, "Sunset");
        QVERIFY(!night.isEmpty() && !sunset.isEmpty());
        const QColor moonlit(63, 74, 139, 128);
        QVERIFY(document.updateCharacterColor(hana, role, "Skin", CharacterColorCategory::Skin, moonlit, nullptr, night));
        CharacterColorPanel panel(&document); panel.resize(280, 380); panel.show();
        auto *tabs = panel.findChild<QTabBar *>("characterColorVariantTabs"); QVERIFY(tabs); QCOMPARE(tabs->count(), 4);
        QCOMPARE(tabs->tabText(0), QString("Day"));
        QSignalSpy chosen(&panel, &CharacterColorPanel::paletteSelected), picked(&panel, &CharacterColorPanel::colorSelected);
        const auto before = document.project().toJson(); const auto history = document.history()->count();
        panel.selectVariant(night); QCOMPARE(panel.selectedVariantId(), night); QCOMPARE(chosen.size(), 1); QCOMPARE(picked.size(), 0);
        panel.selectVariant(night); QCOMPARE(chosen.size(), 1);
        auto *list = panel.findChild<QListWidget *>("characterColorList"); QVERIFY(list); QCOMPARE(list->count(), 1);
        QCOMPARE(list->item(0)->data(Qt::UserRole + 1).value<QColor>(), moonlit);
        QCoreApplication::processEvents(); QTest::mouseClick(list->viewport(), Qt::LeftButton, Qt::NoModifier, list->visualItemRect(list->item(0)).center());
        QCOMPARE(picked.size(), 1); QCOMPARE(picked[0][0].value<QColor>(), moonlit);
        QVERIFY(document.selectShot(document.project().shots[1].id)); QCOMPARE(panel.selectedVariantId(), night); QCOMPARE(chosen.size(), 1);
        panel.setCurrentColor(Qt::green); QCOMPARE(chosen.size(), 1);
        panel.selectCharacter(ren); panel.selectVariant(sunset); panel.selectCharacter(hana); QCOMPARE(panel.selectedVariantId(), night);
        QCOMPARE(document.history()->count(), history); QCOMPARE(document.project().toJson(), before);
    }
    void foregroundReplacementEditsOnlySelectedLightingPaletteAndSupportsUndo() {
        Document document; document.load(fixture()); const auto character = document.addCharacter("Hana");
        const auto role = document.addCharacterColor(character, "Skin", CharacterColorCategory::Skin, Qt::red);
        const auto night = variantId(document, character, "Night"), sunset = variantId(document, character, "Sunset");
        CharacterColorPanel panel(&document); panel.selectVariant(night); panel.setCurrentColor(QColor(80, 90, 125, 120));
        auto *replace = panel.findChild<QToolButton *>("updateCharacterColor"); QVERIFY(replace);
        const auto pixels = document.activeShot().layers[0].tiles; const auto history = document.history()->count();
        QSignalSpy picked(&panel, &CharacterColorPanel::colorSelected); replace->click();
        QCOMPARE(roleColor(document, character, role, night), QColor(80, 90, 125, 120));
        QCOMPARE(roleColor(document, character, role), QColor(Qt::red)); QCOMPARE(roleColor(document, character, role, sunset), QColor(Qt::red));
        QCOMPARE(document.history()->count(), history + 1); QCOMPARE(picked.size(), 0); QCOMPARE(document.activeShot().layers[0].tiles, pixels);
        document.history()->undo(); QCOMPARE(roleColor(document, character, role, night), QColor(Qt::red)); QCOMPARE(panel.selectedVariantId(), night);
        document.history()->redo(); QCOMPARE(roleColor(document, character, role, night), QColor(80, 90, 125, 120));
        panel.selectVariant({}); panel.setCurrentColor(Qt::yellow); replace->click();
        QCOMPARE(roleColor(document, character, role), QColor(Qt::yellow)); QCOMPARE(roleColor(document, character, role, night), QColor(80, 90, 125, 120));
    }
    void paletteActionsDuplicateRenameRemoveAndUndoWithoutChangingRoles() {
        Document document; document.load(fixture()); const auto character = document.addCharacter("Hana");
        const auto role = document.addCharacterColor(character, "Hair", CharacterColorCategory::Hair, Qt::blue);
        const auto night = variantId(document, character, "Night");
        QVERIFY(document.updateCharacterColor(character, role, "Hair", CharacterColorCategory::Hair, QColor("#56517e"), nullptr, night));
        CharacterColorPanel panel(&document); panel.selectCategory(CharacterColorCategory::Hair); panel.selectVariant(night);
        auto *add = panel.findChild<QToolButton *>("addCharacterColorVariant");
        auto *rename = panel.findChild<QAction *>("renameCharacterColorVariant"); auto *remove = panel.findChild<QAction *>("removeCharacterColorVariant");
        QVERIFY(add && rename && remove); const auto history = document.history()->count();
        QVERIFY(dialogAction([&] { add->click(); }, [](QDialog *dialog) { return enterName(dialog, "Moonrise"); }));
        const auto copy = variantId(document, character, "Moonrise"); QVERIFY(!copy.isEmpty()); QCOMPARE(panel.selectedVariantId(), copy);
        QCOMPARE(roleColor(document, character, role, copy), QColor("#56517e"));
        QVERIFY(dialogAction([&] { rename->trigger(); }, [](QDialog *dialog) { return enterName(dialog, "Blue moon"); }));
        QCOMPARE(variantId(document, character, "Blue moon"), copy); QCOMPARE(panel.selectedVariantId(), copy);
        remove->trigger(); QVERIFY(panel.selectedVariantId().isEmpty()); QVERIFY(!remove->isEnabled()); QVERIFY(!rename->isEnabled());
        QCOMPARE(document.history()->count(), history + 3);
        document.history()->undo(); QCOMPARE(panel.selectedVariantId(), copy); QCOMPARE(roleColor(document, character, role, copy), QColor("#56517e"));
        QCOMPARE(document.project().characters[0].colors.size(), 1);
    }
    void olderCharacterGetsStandardPalettesOnlyThroughExplicitAction() {
        Document seed; seed.load(fixture()); const auto character = seed.addCharacter("Hana");
        const auto role = seed.addCharacterColor(character, "Skin", CharacterColorCategory::Skin, QColor("#c4a191"));
        auto legacy = seed.project(); legacy.characters[0].colorVariants.clear(); Document document; document.load(legacy);
        CharacterColorPanel panel(&document); auto *tabs = panel.findChild<QTabBar *>("characterColorVariantTabs");
        auto *standard = panel.findChild<QAction *>("addStandardCharacterColorVariants"); QVERIFY(tabs && standard);
        QCOMPARE(tabs->count(), 1); QCOMPARE(document.history()->count(), 0); QVERIFY(standard->isEnabled());
        standard->trigger(); QCOMPARE(tabs->count(), 4); QCOMPARE(document.history()->count(), 1); QVERIFY(!standard->isEnabled());
        for (const auto &name : {"Night", "Sunset", "Interior"}) {
            const auto variant = variantId(document, character, name); QVERIFY(!variant.isEmpty());
            QCOMPARE(roleColor(document, character, role, variant), QColor("#c4a191"));
        }
        document.history()->undo(); QCOMPARE(tabs->count(), 1); QVERIFY(standard->isEnabled());
    }
    void nameDraftCommitsBeforeVariantSwitchAndInvalidDraftBlocksNavigation() {
        Document document; document.load(fixture()); const auto character = document.addCharacter("Hana");
        const auto other = document.addCharacter("Ren");
        const auto role = document.addCharacterColor(character, "Skin", CharacterColorCategory::Skin, Qt::red);
        const auto night = variantId(document, character, "Night");
        QVERIFY(document.updateCharacterColor(character, role, "Skin", CharacterColorCategory::Skin, Qt::blue, nullptr, night));
        CharacterColorPanel panel(&document); panel.selectCharacter(character); panel.selectVariant(night);
        auto *name = panel.findChild<QLineEdit *>("characterColorName"); auto *tabs = panel.findChild<QTabBar *>("characterColorVariantTabs"); QVERIFY(name && tabs);
        name->setText("Warm skin"); name->setModified(true); panel.selectVariant({});
        QCOMPARE(document.project().characters[0].colors[0].name, QString("Warm skin"));
        QCOMPARE(roleColor(document, character, role, night), QColor(Qt::blue)); QCOMPARE(roleColor(document, character, role), QColor(Qt::red));
        const auto history = document.history()->count(); name->setText(" "); name->setModified(true);
        const auto rejectError = [](QDialog *dialog) { if (!qobject_cast<QMessageBox *>(dialog)) return false; dialog->accept(); return true; };
        QVERIFY(dialogAction([&] { panel.selectVariant(night); }, rejectError));
        QVERIFY(panel.selectedVariantId().isEmpty()); QCOMPARE(tabs->currentIndex(), 0); QCOMPARE(name->text(), QString(" ")); QVERIFY(name->isModified());
        QVERIFY(dialogAction([&] { panel.selectCategory(CharacterColorCategory::Hair); }, rejectError));
        QCOMPARE(panel.selectedCategory(), CharacterColorCategory::Skin); QVERIFY(name->isModified());
        QVERIFY(dialogAction([&] { panel.selectCharacter(other); }, rejectError));
        QCOMPARE(panel.selectedCharacterId(), character); QCOMPARE(document.history()->count(), history);
        QTest::keyClick(name, Qt::Key_Escape); QVERIFY(!name->isModified()); panel.selectVariant(night); QCOMPARE(panel.selectedVariantId(), night);
    }
    void saveReopenKeepsEditedPalettesAndLoadingAnotherProjectClearsSelection() {
        Document document; document.load(fixture()); const auto character = document.addCharacter("Hana");
        const auto role = document.addCharacterColor(character, "Skin", CharacterColorCategory::Skin, Qt::red);
        const auto interior = variantId(document, character, "Interior");
        CharacterColorPanel panel(&document); panel.selectVariant(interior); panel.setCurrentColor(QColor("#b09a75"));
        panel.findChild<QToolButton *>("updateCharacterColor")->click();
        auto *name = panel.findChild<QLineEdit *>("characterColorName"); name->setText("Face base"); name->setModified(true); QVERIFY(panel.finishEditing());
        QTemporaryDir directory; QString error; const auto path = directory.filePath("palettes.toonproj");
        const auto pixels = document.activeShot().layers[0].tiles;
        QVERIFY2(saveProject(document.project(), path, &error), qPrintable(error)); const auto saved = loadProject(path, &error); QVERIFY2(saved.has_value(), qPrintable(error));
        document.load(Project::create()); QVERIFY(panel.selectedCharacterId().isEmpty()); QVERIFY(panel.selectedVariantId().isEmpty());
        document.load(*saved); panel.selectVariant(interior); QCOMPARE(panel.selectedVariantId(), interior);
        auto *list = panel.findChild<QListWidget *>("characterColorList"); QCOMPARE(list->item(0)->text(), QString("Face base"));
        QCOMPARE(roleColor(document, character, role, interior), QColor("#b09a75")); QCOMPARE(roleColor(document, character, role), QColor(Qt::red));
        QCOMPARE(document.activeShot().layers[0].tiles, pixels); QCOMPARE(document.history()->count(), 0);
    }
    void narrowTabsAndActionsStayReachableWithLargeText() {
        Document document; document.load(fixture()); const auto character = document.addCharacter("Hana");
        const auto role = document.addCharacterColor(character, "Base", CharacterColorCategory::Skin, Qt::red); QVERIFY(!role.isEmpty());
        const auto custom = document.addCharacterColorVariant(character, QString(90, 'N')); QVERIFY(!custom.isEmpty());
        CharacterColorPanel panel(&document); panel.resize(220, 300); panel.show(); panel.selectVariant(custom); QCoreApplication::processEvents();
        auto *tabs = panel.findChild<QTabBar *>("characterColorVariantTabs"); QVERIFY(tabs); QCOMPARE(panel.width(), 220); QVERIFY(contained(tabs, &panel));
        for (int i = 0; i < tabs->count(); ++i) { QVERIFY(!tabs->tabText(i).isEmpty()); QVERIFY(!tabs->tabToolTip(i).isEmpty()); QVERIFY(!tabs->accessibleTabName(i).isEmpty()); }
        const auto checkReadableWidths = [&] {
            for (int i = 0; i < 4; ++i)
                QVERIFY2(tabs->tabRect(i).width() >= tabs->fontMetrics().horizontalAdvance(tabs->tabText(i)) + 8,
                    qPrintable(tabs->tabText(i)));
            const int arrows = 2 * tabs->style()->pixelMetric(QStyle::PM_TabBarScrollButtonWidth, nullptr, tabs);
            QVERIFY(tabs->tabRect(4).width() <= qMin(160, qMax(64, tabs->width() - arrows)));
            QVERIFY(tabs->tabRect(tabs->currentIndex()).intersects(tabs->rect()));
        };
        checkReadableWidths();
        for (const auto *id : {"addCharacterColorVariant", "characterColorVariantOptions"}) {
            auto *button = panel.findChild<QToolButton *>(id); QVERIFY(contained(button, &panel)); QVERIFY(!button->icon().isNull()); QVERIFY(!button->accessibleName().isEmpty()); QVERIFY(!button->toolTip().isEmpty());
        }
        auto font = panel.font(); font.setPointSizeF(font.pointSizeF() + 4); panel.setFont(font); panel.resize(220, 360); QCoreApplication::processEvents();
        QCOMPARE(panel.width(), 220); QVERIFY(contained(tabs, &panel)); QVERIFY(contained(panel.findChild<QListWidget *>("characterColorList"), &panel));
        QVERIFY(contained(panel.findChild<QLineEdit *>("characterColorName"), &panel)); QCOMPARE(panel.selectedVariantId(), custom);
        checkReadableWidths();
    }
};
QTEST_MAIN(CharacterVariantsPanelTest)
#include "test_character_variants_panel.moc"
