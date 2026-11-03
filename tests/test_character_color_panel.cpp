#include "character_color_panel.h"
#include "character_model_panel.h"
#include "canvas.h"
#include "color_panel.h"
#include "document.h"
#include "main_window.h"
#include "reference_panel.h"
#include "value_slider.h"
#include <QAction>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDockWidget>
#include <QInputDialog>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalSpy>
#include <QTabBar>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolButton>
#include <QtTest>
#include <functional>

using namespace toon;

namespace {
Project twoShots() {
    auto project = Project::create(); project.width = 320; project.height = 240;
    QImage tile(tileSize, tileSize, QImage::Format_ARGB32_Premultiplied); tile.fill(QColor("#c18269"));
    project.shots[0].layers[0].tiles[{0, 0}] = tile;
    auto second = Project::create().shots.first(); second.number = "C002"; second.name = "Reverse angle";
    project.shots.append(second); return project;
}
const CharacterColorSwatch *swatch(const Document &document, const QString &characterId, const QString &id) {
    for (const auto &character : document.project().characters) if (character.id == characterId)
        for (const auto &color : character.colors) if (color.id == id) return &color;
    return nullptr;
}
QListWidgetItem *itemFor(QListWidget *list, const QString &id) {
    for (int i = 0; i < list->count(); ++i)
        if (list->item(i)->data(Qt::UserRole).toString() == id) return list->item(i);
    return nullptr;
}
bool contained(QWidget *widget, QWidget *parent) {
    return widget && widget->isVisibleTo(parent)
        && parent->rect().contains(QRect(widget->mapTo(parent, QPoint()), widget->size()));
}
// Exercise real modal controls while guaranteeing that a missing/changed dialog
// cannot leave the test suite blocked in a nested event loop.
bool useDialog(const std::function<void()> &open, const std::function<bool(QDialog *)> &interact) {
    QTimer timer, timeout; timer.setInterval(5); timeout.setSingleShot(true);
    bool handled = false;
    QObject::connect(&timer, &QTimer::timeout, [&] {
        auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
        if (!dialog || !dialog->isVisible()) return;
        timer.stop(); handled = interact(dialog);
        if (!handled) dialog->reject();
    });
    QObject::connect(&timeout, &QTimer::timeout, [&] {
        timer.stop();
        if (auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget())) dialog->reject();
        if (auto *popup = QApplication::activePopupWidget()) popup->close();
    });
    timer.start(); timeout.start(2500); open(); timer.stop(); timeout.stop(); return handled;
}
}

class CharacterColorPanelTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { applyTheme(); }

    void selectingNamedColorsIsViewOnlyAndPreservesRgba() {
        Document document; document.load(twoShots());
        const auto hana = document.addCharacter("Hana"), ren = document.addCharacter("Ren");
        const QColor skin("#ffefbd9b"), shadow("#80442211"), hair("#ff293542");
        const auto skinId = document.addCharacterColor(hana, "Skin base", CharacterColorCategory::Skin, skin);
        const auto shadowId = document.addCharacterColor(hana, "Face shadow", CharacterColorCategory::Shadow, shadow);
        QVERIFY(!skinId.isEmpty() && !shadowId.isEmpty());
        QVERIFY(!document.addCharacterColor(ren, "Hair base", CharacterColorCategory::Hair, hair).isEmpty());
        CharacterColorPanel panel(&document); panel.resize(260, 340); panel.show();
        panel.selectCharacter(hana); panel.selectCategory(CharacterColorCategory::Shadow);
        auto *list = panel.findChild<QListWidget *>("characterColorList"); QVERIFY(list);
        QCOMPARE(list->count(), 1); QCOMPARE(list->item(0)->text(), QString("Face shadow"));
        QSignalSpy picked(&panel, &CharacterColorPanel::colorSelected);
        const auto history = document.history()->count(); const auto before = document.project().toJson();
        const auto pixels = document.activeShot().layers[0].tiles;
        QCoreApplication::processEvents();
        QTest::mouseClick(list->viewport(), Qt::LeftButton, Qt::NoModifier, list->visualItemRect(list->item(0)).center());
        QCOMPARE(picked.size(), 1); QCOMPARE(picked.last()[0].value<QColor>(), shadow);
        QCOMPARE(document.history()->count(), history); QCOMPARE(document.project().toJson(), before);
        QCOMPARE(document.activeShot().layers[0].tiles, pixels);
        QVERIFY(document.selectShot(document.project().shots[1].id));
        QCOMPARE(panel.selectedCharacterId(), hana); QCOMPARE(panel.selectedCategory(), CharacterColorCategory::Shadow);
        QCOMPARE(list->currentItem()->data(Qt::UserRole).toString(), shadowId);
        panel.selectCharacter(ren); panel.selectCategory(CharacterColorCategory::Hair);
        QCOMPARE(list->count(), 1); QCOMPARE(list->item(0)->data(Qt::UserRole + 1).value<QColor>(), hair);
        panel.selectCharacter(hana); panel.selectCategory(CharacterColorCategory::Skin);
        QCOMPARE(list->currentItem()->data(Qt::UserRole).toString(), skinId);
        // Selection chrome must not tint a production color reference.
        const auto icon = list->currentItem()->icon();
        for (qreal ratio : {1., 2.}) for (auto mode : {QIcon::Normal, QIcon::Active, QIcon::Selected, QIcon::Disabled})
            for (auto state : {QIcon::Off, QIcon::On}) {
                const auto chip = icon.pixmap(QSize(24, 24), ratio, mode, state).toImage();
                QCOMPARE(chip.pixelColor(qRound(12 * ratio), qRound(12 * ratio)), skin);
            }
        QCOMPARE(picked.size(), 1); // Browsing and refreshes never silently change the painting color.
        QCOMPARE(document.history()->count(), history); QCOMPARE(document.project().toJson(), before);
    }

    void draftNameSurvivesForegroundArtworkAndShotChanges() {
        Document document; document.load(twoShots()); const auto character = document.addCharacter("Hana");
        const auto id = document.addCharacterColor(character, "Base", CharacterColorCategory::Skin, Qt::red); QVERIFY(!id.isEmpty());
        CharacterColorPanel panel(&document); panel.resize(260, 340); panel.show(); panel.activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(&panel));
        auto *name = panel.findChild<QLineEdit *>("characterColorName");
        auto *list = panel.findChild<QListWidget *>("characterColorList"); QVERIFY(name && list);
        name->setFocus(); name->selectAll(); QTest::keyClicks(name, "Warm skin base");
        panel.setCurrentColor(QColor("#ff3267a1")); document.addLayer();
        QVERIFY(document.selectShot(document.project().shots[1].id));
        QCOMPARE(name->text(), QString("Warm skin base")); QVERIFY(name->hasFocus());
        QCOMPARE(list->currentItem()->data(Qt::UserRole).toString(), id);
        QCOMPARE(swatch(document, character, id)->name, QString("Base"));
        const auto history = document.history()->count(); QTest::keyClick(name, Qt::Key_Return);
        QCOMPARE(swatch(document, character, id)->name, QString("Warm skin base"));
        QCOMPARE(document.history()->count(), history + 1);
        name->selectAll(); QTest::keyClicks(name, "Cancel this name"); QTest::keyClick(name, Qt::Key_Escape);
        QCOMPARE(name->text(), QString("Warm skin base")); QCOMPARE(document.history()->count(), history + 1);
        QCOMPARE(swatch(document, character, id)->color, QColor(Qt::red));
    }

    void addReplaceAndRemoveUseCurrentColorAndDocumentUndo() {
        Document document; document.load(twoShots()); const auto character = document.addCharacter("Hana");
        CharacterColorPanel panel(&document); panel.selectCategory(CharacterColorCategory::Costume);
        panel.resize(260, 340); panel.show(); panel.activateWindow(); QVERIFY(QTest::qWaitForWindowActive(&panel));
        auto *add = panel.findChild<QToolButton *>("addCharacterColor");
        auto *replace = panel.findChild<QToolButton *>("updateCharacterColor");
        auto *remove = panel.findChild<QToolButton *>("removeCharacterColor");
        auto *list = panel.findChild<QListWidget *>("characterColorList"); QVERIFY(add && replace && remove && list);
        panel.setCurrentColor(QColor("#80336699")); const auto history = document.history()->count();
        QVERIFY(useDialog([&] { add->click(); }, [](QDialog *dialog) {
            auto *name = qobject_cast<QInputDialog *>(dialog); if (!name) return false;
            name->setTextValue("Winter coat"); name->accept(); return true;
        }));
        QCOMPARE(document.history()->count(), history + 1); QCOMPARE(list->count(), 1);
        const auto id = list->currentItem()->data(Qt::UserRole).toString();
        QVERIFY(swatch(document, character, id));
        QCOMPARE(swatch(document, character, id)->name, QString("Winter coat"));
        QCOMPARE(swatch(document, character, id)->category, CharacterColorCategory::Costume);
        QCOMPARE(swatch(document, character, id)->color, QColor("#80336699"));
        panel.setCurrentColor(QColor("#ffb37a65")); replace->click();
        QCOMPARE(document.history()->count(), history + 2);
        QCOMPARE(swatch(document, character, id)->color, QColor("#ffb37a65"));
        document.history()->undo(); QCOMPARE(swatch(document, character, id)->color, QColor("#80336699"));
        document.history()->redo(); QCOMPARE(swatch(document, character, id)->color, QColor("#ffb37a65"));
        remove->click(); QVERIFY(document.project().characters[0].colors.isEmpty()); QCOMPARE(list->count(), 0);
        document.history()->undo(); QCOMPARE(list->count(), 1); QVERIFY(swatch(document, character, id));
        const auto beforeCancel = document.project().toJson();
        QVERIFY(useDialog([&] { add->click(); }, [](QDialog *dialog) { dialog->reject(); return true; }));
        QCOMPARE(document.project().toJson(), beforeCancel);
        QCOMPARE(document.project().shots[0].layers[0].tiles, twoShots().shots[0].layers[0].tiles);
    }

    void editColorDialogCommitsOnceAndCancelKeepsModelAndBrush() {
        Document document; document.load(twoShots()); const auto character = document.addCharacter("Hana");
        const QColor original("#80442211"), changed("#ccae8064");
        const auto id = document.addCharacterColor(character, "Skin shadow", CharacterColorCategory::Shadow, original);
        QVERIFY(!id.isEmpty()); CharacterColorPanel panel(&document); panel.selectCategory(CharacterColorCategory::Shadow);
        panel.resize(260, 340); panel.show(); panel.activateWindow(); QVERIFY(QTest::qWaitForWindowActive(&panel));
        auto *edit = panel.findChild<QToolButton *>("editCharacterColor"); QVERIFY(edit);
        panel.setCurrentColor(Qt::cyan); QSignalSpy picked(&panel, &CharacterColorPanel::colorSelected);
        const auto history = document.history()->count(); bool privatePreview = false;
        bool invalidHexStayedOpen = false;
        QVERIFY(useDialog([&] { edit->click(); }, [&](QDialog *dialog) {
            auto *hex = dialog->findChild<QLineEdit *>("characterColorHex");
            auto *buttons = dialog->findChild<QDialogButtonBox *>("characterColorEditorButtons");
            if (!hex || !buttons) return false;
            hex->setText("#xyz"); hex->setModified(true);
            buttons->button(QDialogButtonBox::Ok)->click();
            invalidHexStayedOpen = dialog->isVisible()
                && document.history()->count() == history && swatch(document, character, id)->color == original;
            buttons->button(QDialogButtonBox::Cancel)->click(); return true;
        }));
        QVERIFY(invalidHexStayedOpen);
        const auto interact = [&](QDialog *dialog, bool accept) {
            if (dialog->objectName() != "characterColorEditor") return false;
            auto *hex = dialog->findChild<QLineEdit *>("characterColorHex");
            auto *picker = dialog->findChild<ColorPicker *>("characterColorPicker");
            auto *opacity = dialog->findChild<ValueSlider *>("characterColorOpacity");
            auto *buttons = dialog->findChild<QDialogButtonBox *>("characterColorEditorButtons");
            if (!hex || !picker || !opacity || !buttons) return false;
            hex->setText(changed.name(QColor::HexArgb)); hex->setModified(true);
            QMetaObject::invokeMethod(hex, "editingFinished", Qt::DirectConnection);
            privatePreview = document.history()->count() == history && swatch(document, character, id)->color == original;
            buttons->button(accept ? QDialogButtonBox::Ok : QDialogButtonBox::Cancel)->click(); return true;
        };
        QVERIFY(useDialog([&] { edit->click(); }, [&](QDialog *dialog) { return interact(dialog, false); }));
        QVERIFY(privatePreview); QCOMPARE(document.history()->count(), history);
        QCOMPARE(swatch(document, character, id)->color, original); QCOMPARE(picked.size(), 0);
        QVERIFY(useDialog([&] { edit->click(); }, [&](QDialog *dialog) { return interact(dialog, true); }));
        QVERIFY(privatePreview); QCOMPARE(document.history()->count(), history + 1);
        QCOMPARE(swatch(document, character, id)->color, changed); QCOMPARE(picked.size(), 0);
        QCOMPARE(panel.currentColor(), QColor(Qt::cyan));
        document.history()->undo(); QCOMPARE(swatch(document, character, id)->color, original);
    }

    void pendingNameStaysWithOriginalSwatchWhenSelectionChanges() {
        Document document; document.load(twoShots()); const auto character = document.addCharacter("Hana");
        const auto base = document.addCharacterColor(character, "Base", CharacterColorCategory::Skin, Qt::red);
        const auto light = document.addCharacterColor(character, "Light", CharacterColorCategory::Skin, Qt::yellow);
        const auto shadow = document.addCharacterColor(character, "Shadow", CharacterColorCategory::Shadow, Qt::blue);
        QVERIFY(!base.isEmpty() && !light.isEmpty() && !shadow.isEmpty());
        CharacterColorPanel panel(&document); panel.resize(260, 340); panel.show(); panel.activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(&panel));
        auto *name = panel.findChild<QLineEdit *>("characterColorName");
        auto *list = panel.findChild<QListWidget *>("characterColorList"); QVERIFY(name && list);
        const auto history = document.history()->count();
        name->setFocus(); name->selectAll(); QTest::keyClicks(name, "Warm base");
        list->setCurrentItem(itemFor(list, light));
        QCOMPARE(swatch(document, character, base)->name, QString("Warm base"));
        QCOMPARE(swatch(document, character, light)->name, QString("Light"));
        QCOMPARE(list->currentItem()->data(Qt::UserRole).toString(), light); QCOMPARE(name->text(), QString("Light"));
        name->setFocus(); name->selectAll(); QTest::keyClicks(name, "Soft highlight");
        panel.selectCategory(CharacterColorCategory::Shadow);
        QCOMPARE(swatch(document, character, light)->name, QString("Soft highlight"));
        QCOMPARE(swatch(document, character, shadow)->name, QString("Shadow"));
        QCOMPARE(list->currentItem()->data(Qt::UserRole).toString(), shadow); QCOMPARE(name->text(), QString("Shadow"));
        panel.selectCategory(CharacterColorCategory::Skin);
        QCOMPARE(list->currentItem()->data(Qt::UserRole).toString(), light); QCOMPARE(name->text(), QString("Soft highlight"));
        QCOMPARE(document.history()->count(), history + 2);
    }

    void movingCategoryThroughContextMenuIsUndoable() {
        Document document; document.load(twoShots()); const auto character = document.addCharacter("Hana");
        const auto id = document.addCharacterColor(character, "Face shade", CharacterColorCategory::Skin, Qt::red); QVERIFY(!id.isEmpty());
        CharacterColorPanel panel(&document); panel.resize(260, 340); panel.show();
        auto *list = panel.findChild<QListWidget *>("characterColorList"); QVERIFY(list); QCoreApplication::processEvents();
        QTimer choose, timeout; choose.setInterval(5); timeout.setSingleShot(true); bool moved = false;
        connect(&choose, &QTimer::timeout, [&] {
            auto *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget()); if (!menu) return;
            choose.stop();
            for (auto *action : menu->actions()) if (action->menu()) {
                for (auto *destination : action->menu()->actions()) if (destination->text() == "Shadow" && destination->isEnabled()) {
                    destination->trigger(); moved = true; break;
                }
            }
            menu->close();
        });
        connect(&timeout, &QTimer::timeout, [&] { choose.stop(); if (auto *popup = QApplication::activePopupWidget()) popup->close(); });
        const auto history = document.history()->count(); choose.start(); timeout.start(2500);
        QVERIFY(QMetaObject::invokeMethod(list, "customContextMenuRequested", Qt::DirectConnection,
            Q_ARG(QPoint, list->visualItemRect(list->item(0)).center())));
        choose.stop(); timeout.stop(); QVERIFY(moved);
        QCOMPARE(swatch(document, character, id)->category, CharacterColorCategory::Shadow);
        QCOMPARE(document.history()->count(), history + 1);
        panel.selectCategory(CharacterColorCategory::Shadow); QVERIFY(itemFor(list, id));
        document.history()->undo(); QCOMPARE(swatch(document, character, id)->category, CharacterColorCategory::Skin);
        panel.selectCategory(CharacterColorCategory::Skin); QVERIFY(itemFor(list, id));
    }

    void compactCategoriesAndNamedRowsRemainAccessible() {
        Document document; document.load(twoShots()); const auto character = document.addCharacter(QString(90, 'H'));
        for (int i = 0; i < 20; ++i)
            QVERIFY(!document.addCharacterColor(character, QString("Long descriptive skin swatch %1").arg(i),
                CharacterColorCategory::Skin, QColor::fromHsv(i * 15, 120, 210)).isEmpty());
        CharacterColorPanel panel(&document); panel.resize(220, 280); panel.show(); QCoreApplication::processEvents();
        QCOMPARE(panel.size(), QSize(220, 280));
        auto *tabs = panel.findChild<QTabBar *>("characterColorTabs");
        auto *list = panel.findChild<QListWidget *>("characterColorList");
        auto *selector = panel.findChild<QComboBox *>("colorModelCharacterSelector");
        auto *name = panel.findChild<QLineEdit *>("characterColorName"); QVERIFY(tabs && list && selector && name);
        QCOMPARE(tabs->count(), 4); QCOMPARE(list->count(), 20);
        for (auto *widget : QList<QWidget *>{tabs, list, selector, name}) QVERIFY(contained(widget, &panel));
        for (const auto *id : {"addColorModelCharacter", "renameColorModelCharacter", "removeColorModelCharacter",
                "addCharacterColor", "updateCharacterColor", "editCharacterColor", "removeCharacterColor"}) {
            auto *button = panel.findChild<QToolButton *>(id); QVERIFY2(button, id); QVERIFY2(contained(button, &panel), id);
            QVERIFY2(!button->accessibleName().isEmpty(), id); QVERIFY2(!button->toolTip().isEmpty(), id); QVERIFY2(!button->icon().isNull(), id);
        }
        for (int i = 0; i < tabs->count(); ++i) {
            QVERIFY(!tabs->tabIcon(i).isNull()); QVERIFY(!tabs->tabToolTip(i).isEmpty()); QVERIFY(!tabs->accessibleTabName(i).isEmpty());
        }
        QVERIFY(!list->item(0)->data(Qt::AccessibleTextRole).toString().isEmpty());
        const auto retina = list->item(0)->icon().pixmap(QSize(24, 24), 2.0); QVERIFY(!retina.isNull()); QCOMPARE(retina.devicePixelRatio(), 2.0);
        panel.resize(640, 340); QCoreApplication::processEvents();
        for (int i = 0; i < tabs->count(); ++i) QVERIFY(!tabs->tabText(i).isEmpty());
        auto large = panel.font(); large.setPointSizeF(large.pointSizeF() + 4); panel.setFont(large);
        panel.resize(220, 340); QCoreApplication::processEvents(); QCOMPARE(panel.width(), 220);
        for (int i = 0; i < tabs->count(); ++i) QVERIFY(tabs->tabText(i).isEmpty());
        QVERIFY(contained(list, &panel)); QVERIFY(contained(name, &panel));
    }

    void loadingAnotherProjectClearsStaleCharacterColors() {
        Document document; document.load(twoShots()); const auto character = document.addCharacter("Hana");
        const auto id = document.addCharacterColor(character, "Base", CharacterColorCategory::Skin, Qt::red); QVERIFY(!id.isEmpty());
        const auto saved = document.project(); CharacterColorPanel panel(&document);
        auto *list = panel.findChild<QListWidget *>("characterColorList");
        auto *add = panel.findChild<QToolButton *>("addCharacterColor");
        auto *remove = panel.findChild<QToolButton *>("removeCharacterColor"); QVERIFY(list && add && remove);
        QCOMPARE(list->count(), 1); document.load(Project::create());
        QVERIFY(panel.selectedCharacterId().isEmpty()); QCOMPARE(list->count(), 0); QVERIFY(!add->isEnabled()); QVERIFY(!remove->isEnabled());
        document.load(saved); QCOMPARE(panel.selectedCharacterId(), character); QCOMPARE(list->count(), 1);
        QCOMPARE(list->currentItem()->data(Qt::UserRole).toString(), id); QCOMPARE(document.history()->count(), 0);
    }

    void saveAndCloseResolveTheNameDraftBeforeSnapshotOrDirtyCheck() {
        QTemporaryDir dir; QVERIFY(dir.isValid()); const auto path = dir.filePath("character-colors.toonproj");
        Document seed; seed.load(twoShots()); const auto character = seed.addCharacter("Hana");
        const auto id = seed.addCharacterColor(character, "Skin base", CharacterColorCategory::Skin, QColor("#efbd9b"));
        QVERIFY(!id.isEmpty()); QString error; QVERIFY2(saveProject(seed.project(), path, &error), qPrintable(error));
        MainWindow window(dir.filePath("workspace.ini")); QVERIFY(window.openProject(path));
        window.resize(1200, 800); window.show();
        auto *document = window.canvas()->document(); auto *history = document->history();
        auto *references = window.findChild<ReferencePanel *>(); QVERIFY(references);
        auto *panel = references->findChild<CharacterColorPanel *>(); QVERIFY(panel);
        auto *tabs = references->findChild<QTabBar *>("referenceTabs"); QVERIFY(tabs); tabs->setCurrentIndex(2);
        panel->selectCharacter(character); panel->selectCategory(CharacterColorCategory::Skin);
        auto *dock = window.findChild<QDockWidget *>("referencesPanel"); QVERIFY(dock);
        dock->setFloating(true); dock->move(40, 40); dock->resize(260, 340); dock->show(); dock->raise(); dock->activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(dock));
        auto *name = panel->findChild<QLineEdit *>("characterColorName");
        auto *save = window.findChild<QAction *>("saveProjectAction"); QVERIFY(name && save); QVERIFY(save->isEnabled());
        name->setFocus(); QTRY_VERIFY(name->hasFocus()); name->selectAll(); QTest::keyClicks(name, "Warm skin base");
        QCOMPARE(swatch(*document, character, id)->name, QString("Skin base"));
        QCOMPARE(history->count(), 0); QVERIFY(name->isModified());
        // Trigger the actual command without a preliminary focus-out. The
        // progress dialog must not be the event which commits this draft.
        save->trigger();
        QCOMPARE(swatch(*document, character, id)->name, QString("Warm skin base"));
        QCOMPARE(name->text(), QString("Warm skin base")); QVERIFY(!name->isModified());
        QCOMPARE(history->count(), 1); QVERIFY(history->isClean());
        auto saved = loadProject(path, &error); QVERIFY2(saved.has_value(), qPrintable(error));
        QCOMPARE(saved->characters[0].colors[0].name, QString("Warm skin base"));
        QCOMPARE(saved->toJson(), document->project().toJson());
        window.canvas()->setFocus(); QCoreApplication::processEvents(); QCOMPARE(history->count(), 1);

        dock->activateWindow(); QVERIFY(QTest::qWaitForWindowActive(dock));
        name->setFocus(); QTRY_VERIFY(name->hasFocus()); name->selectAll(); QTest::keyClicks(name, "Skin for close-up");
        QVERIFY(history->isClean()); QVERIFY(name->isModified());
        bool closed = true;
        QVERIFY(useDialog([&] { closed = window.close(); }, [](QDialog *dialog) {
            auto *question = qobject_cast<QMessageBox *>(dialog);
            if (!question || !question->button(QMessageBox::Cancel)) return false;
            question->button(QMessageBox::Cancel)->click(); return true;
        }));
        QVERIFY(!closed); QVERIFY(window.isVisible());
        QCOMPARE(swatch(*document, character, id)->name, QString("Skin for close-up"));
        QCOMPARE(history->count(), 2); QVERIFY(!history->isClean());
        saved = loadProject(path, &error); QVERIFY(saved);
        QCOMPARE(saved->characters[0].colors[0].name, QString("Warm skin base"));
        save->trigger(); QCOMPARE(history->count(), 2); QVERIFY(history->isClean());
        saved = loadProject(path, &error); QVERIFY(saved);
        QCOMPARE(saved->characters[0].colors[0].name, QString("Skin for close-up"));
        QVERIFY(window.close());
    }

    void referencesSharesCharactersAndRoutesSwatchesToDrawingForeground() {
        QTemporaryDir dir; QVERIFY(dir.isValid()); MainWindow window(dir.filePath("workspace.ini"));
        auto *canvas = window.canvas(); canvas->setProject(twoShots()); auto *document = canvas->document();
        const auto hana = document->addCharacter("Hana"), ren = document->addCharacter("Ren");
        const QColor color("#a0437695");
        QVERIFY(!document->addCharacterColor(ren, "Coat", CharacterColorCategory::Costume, color).isEmpty());
        auto *references = window.findChild<ReferencePanel *>(); QVERIFY(references);
        auto *models = references->findChild<CharacterModelPanel *>();
        auto *colors = references->findChild<CharacterColorPanel *>();
        auto *tabs = references->findChild<QTabBar *>("referenceTabs"); QVERIFY(models && colors && tabs);
        QCOMPARE(tabs->count(), 4); models->selectCharacter(ren); QCOMPARE(colors->selectedCharacterId(), ren);
        colors->selectCharacter(hana); QCOMPARE(models->selectedCharacterId(), hana);
        colors->selectCharacter(ren); colors->selectCategory(CharacterColorCategory::Costume); tabs->setCurrentIndex(2);
        auto *dock = window.findChild<QDockWidget *>("referencesPanel"); QVERIFY(dock);
        window.resize(1200, 800); window.show(); dock->setFloating(true); dock->move(40, 40); dock->resize(260, 340); dock->show();
        QCoreApplication::processEvents();
        auto *list = colors->findChild<QListWidget *>("characterColorList"); QVERIFY(list); QCOMPARE(list->count(), 1);
        const auto history = document->history()->count(); const auto before = document->project().toJson();
        QTest::mouseClick(list->viewport(), Qt::LeftButton, Qt::NoModifier, list->visualItemRect(list->item(0)).center());
        QCOMPARE(canvas->brush.color, color); QCOMPARE(colors->currentColor(), color);
        QCOMPARE(document->history()->count(), history); QCOMPARE(document->project().toJson(), before);
        canvas->setBrushColor(Qt::yellow); QCOMPARE(colors->currentColor(), QColor(Qt::yellow));
        QVERIFY(document->selectShot(document->project().shots[1].id));
        QCOMPARE(colors->selectedCharacterId(), ren); QCOMPARE(models->selectedCharacterId(), ren);
        QCOMPARE(colors->selectedCategory(), CharacterColorCategory::Costume);
        document->history()->setClean(); window.hide();
    }
};

QTEST_MAIN(CharacterColorPanelTest)
#include "test_character_color_panel.moc"
