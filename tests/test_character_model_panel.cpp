#include "character_model_panel.h"
#include "document.h"
#include "main_window.h"
#include "reference_panel.h"
#include "value_slider.h"
#include <QAction>
#include <QComboBox>
#include <QContextMenuEvent>
#include <QDoubleSpinBox>
#include <QFile>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QPainter>
#include <QSignalSpy>
#include <QSlider>
#include <QTabBar>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolButton>
#include <QtTest>

using namespace toon;

namespace {
QImage modelImage(QColor color, QSize size = {160, 200}) {
    QImage image(size, QImage::Format_ARGB32_Premultiplied);
    image.fill(color);
    return image;
}
Project twoShots() {
    auto project = Project::create();
    project.width = 320; project.height = 240;
    auto second = Project::create().shots.first();
    second.number = "C002"; second.name = "Reverse angle";
    project.shots.append(second);
    return project;
}
CharacterModelSheet sheet(const QString &name, ModelSheetCategory category, const QImage &image) {
    return {{}, name, category, image};
}
bool contained(QWidget *widget, QWidget *parent) {
    return widget && widget->isVisibleTo(parent)
        && parent->rect().contains(QRect(widget->mapTo(parent, QPoint()), widget->size()));
}
}

class CharacterModelPanelTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { applyTheme(); }

    void importsCategoryInOneUndoAndRejectsPartialBatch() {
        QTemporaryDir dir; QVERIFY(dir.isValid());
        const auto front = dir.filePath("Front.png"), side = dir.filePath("Side.png");
        const auto red = modelImage(Qt::red), blue = modelImage(Qt::blue);
        QVERIFY(red.save(front)); QVERIFY(blue.save(side));
        Document document; document.load(twoShots());
        const auto character = document.addCharacter("Hana"); QVERIFY(!character.isEmpty());
        CharacterModelPanel panel(&document); panel.selectCharacter(character);
        panel.selectCategory(ModelSheetCategory::Expression);
        auto *list = panel.findChild<QListWidget *>("characterSheetList");
        auto *view = panel.findChild<ReferenceView *>("characterModelView");
        QVERIFY(list && view);
        const auto history = document.history()->count(); QString error;
        QVERIFY2(panel.importImages({front, side}, &error), qPrintable(error));
        QCOMPARE(document.history()->count(), history + 1);
        const auto &sheets = document.project().characters[0].sheets;
        QCOMPARE(sheets.size(), 2); QCOMPARE(list->count(), 2);
        QCOMPARE(sheets[0].name, QString("Front")); QCOMPARE(sheets[1].name, QString("Side"));
        QCOMPARE(sheets[0].category, ModelSheetCategory::Expression);
        QCOMPARE(sheets[1].category, ModelSheetCategory::Expression);
        QCOMPARE(sheets[0].image, red); QCOMPARE(sheets[1].image, blue);
        QVERIFY(document.activeShot().references.isEmpty());
        document.history()->undo();
        QVERIFY(document.project().characters[0].sheets.isEmpty());
        QCOMPARE(list->count(), 0); QVERIFY(view->image().isNull());
        document.history()->redo(); QCOMPARE(list->count(), 2);
        const auto before = document.project().toJson();
        const auto afterImportHistory = document.history()->count();
        QVERIFY(!panel.importImages({front, dir.filePath("missing.png")}, &error));
        QVERIFY(!error.isEmpty()); QCOMPARE(document.project().toJson(), before);
        QCOMPARE(document.history()->count(), afterImportHistory);
        panel.selectCategory(ModelSheetCategory::Turnaround); QCOMPARE(list->count(), 0);
        panel.selectCategory(ModelSheetCategory::Expression); QCOMPARE(list->count(), 2);
        QCOMPARE(document.history()->count(), afterImportHistory);
    }

    void characterAndCategoryNavigationReuseLibraryAcrossShots() {
        Document document; document.load(twoShots());
        const auto hana = document.addCharacter("Hana"), ren = document.addCharacter("Ren");
        QVERIFY(!hana.isEmpty() && !ren.isEmpty());
        const auto front = modelImage(Qt::red), happy = modelImage(Qt::green);
        const auto uniform = modelImage(Qt::blue), other = modelImage(Qt::yellow);
        QVERIFY(document.addCharacterSheets(hana, {
            sheet("Front", ModelSheetCategory::Turnaround, front),
            sheet("Happy", ModelSheetCategory::Expression, happy),
            sheet("Uniform", ModelSheetCategory::Costume, uniform)}));
        QVERIFY(document.addCharacterSheets(ren, {sheet("Front", ModelSheetCategory::Turnaround, other)}));
        CharacterModelPanel panel(&document); panel.resize(260, 400); panel.show();
        panel.selectCharacter(hana); panel.selectCategory(ModelSheetCategory::Turnaround);
        auto *view = panel.findChild<ReferenceView *>("characterModelView");
        auto *list = panel.findChild<QListWidget *>("characterSheetList");
        auto *selector = panel.findChild<QComboBox *>("characterSelector");
        QVERIFY(view && list && selector); QCOMPARE(selector->count(), 2);
        QCOMPARE(view->image(), front);
        const auto history = document.history()->count();
        const auto metadata = document.project().toJson();
        panel.selectCategory(ModelSheetCategory::Expression); QCOMPARE(view->image(), happy);
        panel.selectCategory(ModelSheetCategory::Costume); QCOMPARE(view->image(), uniform);
        view->setZoom(1.75); const auto transform = view->imageTransform();
        QVERIFY(document.selectShot(document.project().shots[1].id));
        QCOMPARE(panel.selectedCharacterId(), hana);
        QCOMPARE(panel.selectedCategory(), ModelSheetCategory::Costume);
        QCOMPARE(view->image(), uniform); QCOMPARE(view->imageTransform(), transform);
        QCOMPARE(list->count(), 1); QVERIFY(document.activeShot().references.isEmpty());
        panel.selectCharacter(ren); panel.selectCategory(ModelSheetCategory::Turnaround);
        QCOMPARE(view->image(), other);
        panel.selectCharacter(hana); panel.selectCategory(ModelSheetCategory::Turnaround);
        QCOMPARE(view->image(), front);
        QCOMPARE(document.history()->count(), history);
        QCOMPARE(document.project().toJson(), metadata);
    }

    void zoomDraftAndPanSurviveArtworkRefreshAndShotSwitch() {
        Document document; document.load(twoShots());
        const auto character = document.addCharacter("Hana");
        QVERIFY(document.addCharacterSheets(character, {
            sheet("Front", ModelSheetCategory::Turnaround, modelImage(Qt::cyan))}));
        CharacterModelPanel panel(&document); panel.selectCharacter(character);
        panel.resize(320, 440); panel.show(); panel.activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(&panel));
        auto *view = panel.findChild<ReferenceView *>("characterModelView");
        auto *zoom = panel.findChild<ValueSlider *>("characterModelZoom");
        QVERIFY(view && zoom);
        auto *number = zoom->findChild<QDoubleSpinBox *>(); QVERIFY(number);
        auto *edit = number->findChild<QLineEdit *>(); QVERIFY(edit);
        number->setValue(125); QCOMPARE(view->zoom(), 1.25);
        const auto center = view->rect().center(); const QPoint delta(16, 12);
        const auto before = view->imageTransform().map(QPointF());
        QTest::mousePress(view, Qt::LeftButton, Qt::NoModifier, center);
        QTest::mouseMove(view, center + delta);
        QTest::mouseRelease(view, Qt::LeftButton, Qt::NoModifier, center + delta);
        QVERIFY(QLineF(view->imageTransform().map(QPointF()), before + delta).length() < .01);
        const auto panned = view->imageTransform();
        edit->setFocus(); QTRY_VERIFY(edit->hasFocus());
        edit->selectAll(); QTest::keyClicks(edit, "175");
        document.addLayer();
        QVERIFY(document.selectShot(document.project().shots[1].id));
        QCOMPARE(edit->text(), QString("175")); QVERIFY(edit->hasFocus());
        QCOMPARE(view->imageTransform(), panned);
        const auto history = document.history()->count();
        QTest::keyClick(edit, Qt::Key_Return);
        QCOMPARE(view->zoom(), 1.75); QCOMPARE(zoom->value(), 175.0);
        edit->selectAll(); QTest::keyClicks(edit, "250"); QTest::keyClick(edit, Qt::Key_Escape);
        QCOMPARE(view->zoom(), 1.75); QCOMPARE(zoom->value(), 175.0);
        auto *fit = panel.findChild<QToolButton *>("fitCharacterSheet"); QVERIFY(fit); fit->click();
        QVERIFY(QRectF(view->rect()).contains(view->imageTransform().mapRect(QRectF(view->image().rect()))));
        QCOMPARE(document.history()->count(), history);
    }

    void renameMoveAndDeleteStayUndoableWithoutChangingArtwork() {
        Document document; document.load(twoShots());
        const auto character = document.addCharacter("Hana");
        const auto pixels = modelImage(Qt::magenta);
        QVERIFY(document.addCharacterSheets(character, {
            sheet("Front", ModelSheetCategory::Turnaround, pixels)}));
        const auto sheetId = document.project().characters[0].sheets[0].id;
        CharacterModelPanel panel(&document); panel.selectCharacter(character);
        panel.resize(260, 400); panel.show(); panel.activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(&panel));
        auto *name = panel.findChild<QLineEdit *>("characterSheetName");
        auto *list = panel.findChild<QListWidget *>("characterSheetList");
        auto *view = panel.findChild<ReferenceView *>("characterModelView");
        auto *removeSheet = panel.findChild<QToolButton *>("removeCharacterSheet");
        auto *removeCharacter = panel.findChild<QToolButton *>("removeCharacter");
        QVERIFY(name && list && view && removeSheet && removeCharacter);
        const auto history = document.history()->count();
        name->setFocus(); name->selectAll(); QTest::keyClicks(name, "Three-quarter view");
        QTest::keyClick(name, Qt::Key_Return);
        QCOMPARE(document.project().characters[0].sheets[0].name, QString("Three-quarter view"));
        QCOMPARE(document.history()->count(), history + 1); QCOMPARE(view->image(), pixels);
        name->selectAll(); QTest::keyClicks(name, "Unfinished name"); QTest::keyClick(name, Qt::Key_Escape);
        QCOMPARE(name->text(), QString("Three-quarter view"));
        QCOMPARE(document.history()->count(), history + 1);
        QVERIFY(document.setCharacterSheetCategory(character, sheetId, ModelSheetCategory::Expression));
        QCOMPARE(list->count(), 0);
        panel.selectCategory(ModelSheetCategory::Expression); QCOMPARE(list->count(), 1); QCOMPARE(view->image(), pixels);
        const auto beforeRemove = document.history()->count();
        removeSheet->click(); QCOMPARE(document.history()->count(), beforeRemove + 1);
        QVERIFY(document.project().characters[0].sheets.isEmpty()); QVERIFY(view->image().isNull());
        document.history()->undo(); QCOMPARE(list->count(), 1); QCOMPARE(view->image(), pixels);
        removeCharacter->click(); QVERIFY(document.project().characters.isEmpty());
        document.history()->undo(); QCOMPARE(document.project().characters.size(), 1);
        panel.selectCharacter(character); panel.selectCategory(ModelSheetCategory::Expression);
        QCOMPARE(view->image(), pixels);
        for (const auto &shot : document.project().shots) {
            QVERIFY(shot.references.isEmpty()); QVERIFY(shot.layers[0].tiles.isEmpty());
        }
    }

    void narrowPanelKeepsNavigationAndAccessibleActionsAvailable() {
        Document document; document.load(twoShots());
        const auto character = document.addCharacter("A character with a deliberately long production name");
        QVector<CharacterModelSheet> sheets;
        for (int i = 0; i < 6; ++i)
            sheets.append(sheet(QString("Turnaround view %1 with a descriptive production name").arg(i),
                                ModelSheetCategory::Turnaround, modelImage(QColor::fromHsv(i * 50, 140, 200))));
        QVERIFY(document.addCharacterSheets(character, sheets));
        CharacterModelPanel panel(&document); panel.selectCharacter(character);
        panel.resize(220, 400); panel.show(); QCoreApplication::processEvents();
        QCOMPARE(panel.width(), 220);
        auto *tabs = panel.findChild<QTabBar *>("characterModelTabs");
        auto *list = panel.findChild<QListWidget *>("characterSheetList");
        auto *view = panel.findChild<ReferenceView *>("characterModelView");
        auto *zoom = panel.findChild<ValueSlider *>("characterModelZoom");
        auto *selector = panel.findChild<QComboBox *>("characterSelector");
        QVERIFY(tabs && list && view && zoom && selector);
        for (auto *widget : QList<QWidget *>{tabs, list, view, zoom, selector}) QVERIFY(contained(widget, &panel));
        QVERIFY(view->height() >= 80); QCOMPARE(tabs->count(), 3);
        for (int i = 0; i < tabs->count(); ++i) QVERIFY(!tabs->tabToolTip(i).isEmpty());
        // Keep all category identities available without partial labels at pen-panel widths.
        panel.resize(640, 400); QCoreApplication::processEvents();
        for (int i = 0; i < tabs->count(); ++i) QVERIFY(!tabs->tabText(i).isEmpty());
        panel.resize(220, 400); QCoreApplication::processEvents();
        for (int i = 0; i < tabs->count(); ++i) {
            QVERIFY(tabs->tabText(i).isEmpty()); QVERIFY(!tabs->tabIcon(i).isNull());
            QVERIFY(!tabs->accessibleTabName(i).isEmpty());
        }
        for (const auto *id : {"addCharacter", "renameCharacter", "removeCharacter", "importCharacterSheets",
                               "removeCharacterSheet", "fitCharacterSheet"}) {
            auto *button = panel.findChild<QToolButton *>(id); QVERIFY2(button, id);
            QVERIFY2(contained(button, &panel), id);
            QVERIFY2(!button->accessibleName().isEmpty(), id); QVERIFY2(!button->toolTip().isEmpty(), id);
        }
        QVERIFY(zoom->findChild<QSlider *>()->width() >= 74);
        QCOMPARE(list->count(), 6);
        QVERIFY(!list->item(0)->data(Qt::AccessibleTextRole).toString().isEmpty());
        const auto retina = list->item(0)->icon().pixmap(QSize(40, 40), 2.0);
        QCOMPARE(retina.devicePixelRatio(), 2.0); QVERIFY(!retina.isNull());
        const auto history = document.history()->count();
        list->setCurrentRow(5);
        for (int i = 0; i < 3; ++i) tabs->setCurrentIndex(i);
        QCOMPARE(document.history()->count(), history);
    }

    void referencesTabsKeepImagesIndependentAndRouteColorSampling() {
        Document document; document.load(twoShots());
        const auto reference = modelImage(Qt::red), model = modelImage(Qt::green);
        QVERIFY(document.addReference("Lighting study", reference));
        const auto character = document.addCharacter("Hana");
        QVERIFY(document.addCharacterSheets(character, {sheet("Front", ModelSheetCategory::Turnaround, model)}));
        ReferencePanel panel(&document); panel.resize(280, 460); panel.show();
        auto *tabs = panel.findChild<QTabBar *>("referenceTabs");
        auto *characters = panel.findChild<CharacterModelPanel *>();
        auto *imageView = panel.findChild<ReferenceView *>("referenceView");
        auto *modelView = panel.findChild<ReferenceView *>("characterModelView");
        QVERIFY(tabs && characters && imageView && modelView); QCOMPARE(tabs->count(), 3);
        QCOMPARE(tabs->currentIndex(), 0); QCOMPARE(imageView->image(), reference);
        characters->selectCharacter(character); characters->selectCategory(ModelSheetCategory::Turnaround);
        QSignalSpy colors(&panel, &ReferencePanel::colorSampled);
        panel.setSampling(true); QVERIFY(imageView->sampling()); QVERIFY(modelView->sampling());
        QCoreApplication::processEvents();
        QTest::mouseClick(imageView, Qt::LeftButton, Qt::NoModifier, imageView->rect().center());
        QCOMPARE(colors.size(), 1); QCOMPARE(colors.last()[0].value<QColor>(), QColor(Qt::red));
        tabs->setCurrentIndex(1); QCoreApplication::processEvents();
        QVERIFY(modelView->isVisible()); QCOMPARE(modelView->image(), model);
        QTest::mouseClick(modelView, Qt::LeftButton, Qt::NoModifier, modelView->rect().center());
        QCOMPARE(colors.size(), 2); QCOMPARE(colors.last()[0].value<QColor>(), QColor(Qt::green));
        panel.setSampling(false); QVERIFY(!imageView->sampling()); QVERIFY(!modelView->sampling());
        tabs->setCurrentIndex(0); QCOMPARE(imageView->image(), reference);
        QCOMPARE(document.history()->count(), 3);
    }

    void projectReplacementClearsThePreviousCharacterView() {
        Document document; document.load(twoShots());
        const auto character = document.addCharacter("Hana");
        QVERIFY(document.addCharacterSheets(character, {
            sheet("Front", ModelSheetCategory::Turnaround, modelImage(Qt::cyan))}));
        const auto original = document.project();
        CharacterModelPanel panel(&document); panel.selectCharacter(character);
        auto *view = panel.findChild<ReferenceView *>("characterModelView");
        auto *selector = panel.findChild<QComboBox *>("characterSelector");
        auto *importButton = panel.findChild<QToolButton *>("importCharacterSheets");
        auto *remove = panel.findChild<QToolButton *>("removeCharacterSheet");
        QVERIFY(view && selector && importButton && remove); QVERIFY(!view->image().isNull());
        document.load(Project::create());
        QCOMPARE(selector->count(), 0); QVERIFY(view->image().isNull());
        QVERIFY(panel.selectedCharacterId().isEmpty()); QVERIFY(!importButton->isEnabled()); QVERIFY(!remove->isEnabled());
        document.load(original);
        QCOMPARE(selector->count(), 1); QCOMPARE(view->image(), original.characters[0].sheets[0].image);
        QCOMPARE(document.history()->count(), 0);
    }

    void singleSheetViewerMenuMovesCategoryAndSupportsUndo() {
        Document document; document.load(twoShots());
        const auto character = document.addCharacter("Hana");
        const auto image = modelImage(Qt::cyan);
        QVERIFY(document.addCharacterSheets(character, {sheet("Front", ModelSheetCategory::Turnaround, image)}));
        CharacterModelPanel panel(&document); panel.selectCharacter(character);
        panel.resize(260, 400); panel.show();
        auto *view = panel.findChild<ReferenceView *>("characterModelView");
        auto *list = panel.findChild<QListWidget *>("characterSheetList");
        QVERIFY(view && list); QVERIFY(list->isHidden()); QCOMPARE(view->image(), image);
        QCOMPARE(view->contextMenuPolicy(), Qt::CustomContextMenu);
        bool movedThroughMenu = false;
        QTimer::singleShot(0, &panel, [&] {
            // A single sheet hides the thumbnail strip, so organization must
            // remain available from the image itself.
            const auto menus = panel.findChildren<QMenu *>(QString(), Qt::FindDirectChildrenOnly);
            if (menus.isEmpty()) return;
            auto *menu = menus.first();
            for (auto *action : menu->actions()) {
                if (action->text() != "Move to" || !action->menu()) continue;
                for (auto *destination : action->menu()->actions()) {
                    if (destination->text() != "Expressions" || !destination->isEnabled()) continue;
                    destination->trigger(); movedThroughMenu = true; break;
                }
            }
            menu->close();
        });
        const auto history = document.history()->count();
        const auto point = view->rect().center();
        QContextMenuEvent context(QContextMenuEvent::Mouse, point, view->mapToGlobal(point));
        QApplication::sendEvent(view, &context);
        QVERIFY(movedThroughMenu); QCOMPARE(document.history()->count(), history + 1);
        QCOMPARE(document.project().characters[0].sheets[0].category, ModelSheetCategory::Expression);
        QCOMPARE(list->count(), 0); QVERIFY(view->image().isNull());
        panel.selectCategory(ModelSheetCategory::Expression);
        QCOMPARE(list->count(), 1); QCOMPARE(view->image(), image); QVERIFY(list->isHidden());
        document.history()->undo();
        QCOMPARE(list->count(), 0);
        panel.selectCategory(ModelSheetCategory::Turnaround);
        QCOMPARE(view->image(), image); QCOMPARE(list->count(), 1);
    }

    void pendingSheetNameFollowsItsOriginalTargetWhenSelectionChanges() {
        Document document; document.load(twoShots());
        const auto character = document.addCharacter("Hana");
        const auto front = modelImage(Qt::red), side = modelImage(Qt::blue), happy = modelImage(Qt::green);
        QVERIFY(document.addCharacterSheets(character, {
            sheet("Front", ModelSheetCategory::Turnaround, front),
            sheet("Side", ModelSheetCategory::Turnaround, side),
            sheet("Happy", ModelSheetCategory::Expression, happy)}));
        CharacterModelPanel panel(&document); panel.selectCharacter(character);
        panel.resize(260, 400); panel.show(); panel.activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(&panel));
        auto *name = panel.findChild<QLineEdit *>("characterSheetName");
        auto *list = panel.findChild<QListWidget *>("characterSheetList");
        auto *view = panel.findChild<ReferenceView *>("characterModelView");
        QVERIFY(name && list && view); QCOMPARE(list->currentRow(), 0);
        const auto history = document.history()->count();
        name->setFocus(); name->selectAll(); QTest::keyClicks(name, "Front corrected");
        // Changing selection without a separate focus-out first reproduces
        // the synchronous document-refresh path used by keyboard/navigation.
        list->setCurrentRow(1);
        QCOMPARE(document.project().characters[0].sheets[0].name, QString("Front corrected"));
        QCOMPARE(document.project().characters[0].sheets[1].name, QString("Side"));
        QCOMPARE(list->currentRow(), 1); QCOMPARE(view->image(), side); QCOMPARE(name->text(), QString("Side"));
        QCOMPARE(list->currentItem()->data(Qt::UserRole).toString(), document.project().characters[0].sheets[1].id);
        QCOMPARE(document.history()->count(), history + 1);
        name->setFocus(); name->selectAll(); QTest::keyClicks(name, "Side corrected");
        panel.selectCategory(ModelSheetCategory::Expression);
        QCOMPARE(document.project().characters[0].sheets[1].name, QString("Side corrected"));
        QCOMPARE(document.project().characters[0].sheets[2].name, QString("Happy"));
        QCOMPARE(list->currentRow(), 0); QCOMPARE(view->image(), happy); QCOMPARE(name->text(), QString("Happy"));
        QCOMPARE(document.history()->count(), history + 2);
        panel.selectCategory(ModelSheetCategory::Turnaround);
        QCOMPARE(list->currentRow(), 1); QCOMPARE(view->image(), side); QCOMPARE(name->text(), QString("Side corrected"));
        QCOMPARE(document.history()->count(), history + 2);
    }
};

QTEST_MAIN(CharacterModelPanelTest)
#include "test_character_model_panel.moc"
