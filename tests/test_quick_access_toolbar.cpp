#include "quick_access_toolbar.h"
#include "tool_catalog.h"
#include "tool_session.h"
#include "ui_theme.h"
#include "ui_metrics.h"
#include "value_slider.h"
#include <QAction>
#include <QDialog>
#include <QDoubleSpinBox>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMainWindow>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QPushButton>
#include <QSettings>
#include <QStyle>
#include <QStyleHints>
#include <QTemporaryDir>
#include <QToolButton>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidgetAction>
#include <QtTest>

using namespace toon;
namespace {
struct Fixture {
    QTemporaryDir dir;
    BrushLibrary library{dir.filePath("brushes.json")};
    ToolCatalog catalog{&library, dir.filePath("tools.json")};
    QSettings settings{dir.filePath("preferences.ini"), QSettings::IniFormat};
    Canvas canvas;
    bool load() {
        if (!dir.isValid() || !library.load() || !catalog.load()) return false;
        canvas.resize(800, 600); canvas.setProject(Project::create()); return true;
    }
    QString tool(Canvas::Tool engine) const {
        for (const auto &entry : catalog.tools()) if (entry.engine == engine) return entry.id;
        return {};
    }
};
QByteArray preference(QJsonArray items, QJsonValue version = 1)
{
    return QJsonDocument(QJsonObject{{"version", version}, {"items", items}}).toJson(QJsonDocument::Compact);
}
QJsonObject colorItem(QString value = "#ff123456") { return {{"kind", "color"}, {"value", value}}; }
void clearPins(QuickAccessToolbar &toolbar)
{
    const auto items = toolbar.items();
    for (const auto &item : items) toolbar.removeItem(item.id());
}
} // namespace

class QuickAccessToolbarTests final : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() {
        // Reproduce macOS's text/list-only default. The workspace must make
        // its pen-friendly TabFocus buttons reachable without OS changes.
        QGuiApplication::styleHints()->setTabFocusBehavior(
            Qt::TabFocusBehavior(Qt::TabFocusTextControls | Qt::TabFocusListControls));
        applyTheme();
    }

    void activatesExactToolAndHonorsPendingTransformGuard()
    {
        Fixture f; QVERIFY(f.load()); ToolSession session(&f.canvas, &f.catalog);
        QuickAccessToolbar toolbar(&f.canvas, &f.catalog, &f.settings);
        const auto rectangle = f.tool(Canvas::Tool::Rectangle);
        QVERIFY(toolbar.addTool(rectangle));
        auto *action = toolbar.itemAction("tool:" + rectangle); QVERIFY(action);
        const auto old = f.catalog.activeToolId();
        f.catalog.setActivationGuard([] { return false; }); action->trigger();
        QCOMPARE(f.catalog.activeToolId(), old); QVERIFY(!action->isChecked());
        f.catalog.setActivationGuard([] { return true; }); action->trigger();
        QCOMPARE(f.catalog.activeToolId(), rectangle); QCOMPARE(f.canvas.tool(), Canvas::Tool::Rectangle);
        QVERIFY(action->isChecked());
        QVERIFY(f.catalog.renameTool(rectangle, "Background rectangle"));
        QCOMPARE(action->text(), QString("Background rectangle"));
        QCOMPARE(toolbar.widgetForAction(action)->accessibleName(), QString("Background rectangle"));
        QVERIFY(f.catalog.activateTool(f.tool(Canvas::Tool::Brush))); QVERIFY(!action->isChecked());
    }

    void colorsApplyAndTrackSharedForegroundWithoutHistoryEdits()
    {
        Fixture f; QVERIFY(f.load()); ToolSession session(&f.canvas, &f.catalog);
        QuickAccessToolbar toolbar(&f.canvas, &f.catalog, &f.settings);
        const QColor red("#ef315c"), green("#82c687");
        QVERIFY(toolbar.addColor(red)); QVERIFY(toolbar.addColor(green));
        const auto history = f.canvas.document()->history()->count();
        auto *redAction = toolbar.itemAction("color:" + red.name(QColor::HexArgb));
        auto *greenAction = toolbar.itemAction("color:" + green.name(QColor::HexArgb));
        redAction->trigger(); QCOMPARE(f.canvas.brush.color, red); QVERIFY(redAction->isChecked());
        f.canvas.setBrushColor(green); QVERIFY(greenAction->isChecked()); QVERIFY(!redAction->isChecked());
        QCOMPARE(f.canvas.document()->history()->count(), history);
        const auto bytes = f.settings.value("quickAccess/v1").toByteArray();
        f.canvas.setBrushSize(21); QCOMPARE(f.settings.value("quickAccess/v1").toByteArray(), bytes);
    }

    void rejectedPinsKeepTypedDraftAndAcceptedPinsReturnToCanvas()
    {
        QWidget host; // Outlives the fixture's stack-owned canvas after reparenting.
        Fixture f; QVERIFY(f.load()); ToolSession session(&f.canvas, &f.catalog);
        auto *layout = new QVBoxLayout(&host);
        auto *toolbar = new QuickAccessToolbar(&f.canvas, &f.catalog, &f.settings, &host);
        auto *size = new ValueSlider("Brush size", 1, 1000, f.canvas.brush.size, 1, "px", &host);
        layout->addWidget(toolbar); layout->addWidget(size); layout->addWidget(&f.canvas);
        connect(size, &ValueSlider::valueChanged, &f.canvas, &Canvas::setBrushSize);
        const auto rectangle = f.tool(Canvas::Tool::Rectangle); QVERIFY(toolbar->addTool(rectangle));
        auto *button = qobject_cast<QToolButton *>(toolbar->widgetForAction(toolbar->itemAction("tool:" + rectangle)));
        QVERIFY(button);
        auto *number = size->findChild<QDoubleSpinBox *>(); auto *editor = number->findChild<QLineEdit *>();
        host.show(); QVERIFY(QTest::qWaitForWindowExposed(&host)); host.activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(&host)); number->setFocus(); QTRY_VERIFY(number->hasFocus());
        number->selectAll(); QTest::keyClicks(number, "27.5");
        const auto before = f.canvas.brush; const auto oldTool = f.catalog.activeToolId();
        QSignalSpy committed(size, &ValueSlider::editingFinished);
        f.catalog.setActivationGuard([] { return false; });
        QTest::mouseClick(button, Qt::LeftButton);
        toolbar->itemAction("navigation:hand")->trigger();
        QCOMPARE(f.catalog.activeToolId(), oldTool); QVERIFY(number->hasFocus());
        QCOMPARE(editor->text(), QString("27.5")); QCOMPARE(committed.count(), 0);
        QVERIFY(f.canvas.brush == before); QVERIFY(!button->isChecked());
        f.catalog.setActivationGuard([] { return true; });
        QTest::mouseClick(button, Qt::LeftButton);
        QCOMPARE(f.catalog.activeToolId(), rectangle); QTRY_VERIFY(f.canvas.hasFocus());
        QCOMPARE(committed.count(), 1); QCOMPARE(f.canvas.document()->history()->count(), 0);
    }

    void keyboardCanReachAndUsePinsWithoutMouseFocus()
    {
        QWidget host;
        Fixture f; QVERIFY(f.load()); ToolSession session(&f.canvas, &f.catalog);
        auto *layout = new QVBoxLayout(&host);
        auto *toolbar = new QuickAccessToolbar(&f.canvas, &f.catalog, &f.settings, &host);
        auto *entry = new QLineEdit("Unfinished name", &host);
        layout->addWidget(entry); layout->addWidget(toolbar); layout->addWidget(&f.canvas);
        const auto rectangle = f.tool(Canvas::Tool::Rectangle); QVERIFY(toolbar->addTool(rectangle));
        auto *pin = toolbar->widgetForAction(toolbar->itemAction("tool:" + rectangle)); QVERIFY(pin);
        QWidget::setTabOrder(entry, pin);
        host.show(); QVERIFY(QTest::qWaitForWindowExposed(&host)); host.activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(&host)); entry->setFocus(); QTRY_VERIFY(entry->hasFocus());
        QVERIFY(pin->isVisible()); QVERIFY(toolbar->rect().contains(pin->geometry()));
        QTest::keyClick(entry, Qt::Key_Tab); QTRY_VERIFY(pin->hasFocus());
        QTest::keyClick(pin, Qt::Key_Backtab, Qt::ShiftModifier); QTRY_VERIFY(entry->hasFocus());
        QTest::keyClick(entry, Qt::Key_Tab); QTRY_VERIFY(pin->hasFocus());
        QTest::keyClick(pin, Qt::Key_Space);
        QCOMPARE(f.catalog.activeToolId(), rectangle); QTRY_VERIFY(f.canvas.hasFocus());
        QCOMPARE(entry->text(), QString("Unfinished name"));
    }

    void chooserRowsGrowWithTextAndPreserveFiltering()
    {
        Fixture f; QVERIFY(f.load());
        QuickAccessToolbar toolbar(&f.canvas, &f.catalog, &f.settings); toolbar.show();
        QVERIFY(QTest::qWaitForWindowExposed(&toolbar)); toolbar.activateWindow();
        bool opened = false, rowsFit = false, draftKept = false, selectionKept = false;
        const auto pins = toolbar.items();
        QTimer::singleShot(0, &toolbar, [&] {
            auto *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget());
            if (!menu) return;
            QAction *choose = nullptr;
            for (auto *action : menu->actions()) if (action->text() == "Add tool…") choose = action;
            if (!choose) { menu->close(); return; }
            QTimer::singleShot(0, &toolbar, [&] {
                auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
                if (!dialog) return;
                auto *list = dialog->findChild<QListWidget *>("quickAccessToolChoices");
                auto *search = dialog->findChild<QLineEdit *>("quickAccessToolSearch");
                if (list && search && list->count()) {
                    opened = true; list->setCurrentRow(0); auto *selected = list->currentItem();
                    const auto needle = selected->text().left(4); search->setText(needle);
                    auto font = dialog->font(); font.setPixelSize(28); dialog->setFont(font); QTest::qWait(10);
                    rowsFit = list->visualItemRect(selected).height() >= list->fontMetrics().height() + 2 * ui::metrics::controlGap;
                    draftKept = search->text() == needle;
                    selectionKept = list->currentItem() == selected && !selected->isHidden();
                }
                dialog->reject();
            });
            choose->trigger(); menu->close();
        });
        toolbar.findChild<QAction *>("quickAccessAdd")->trigger();
        QVERIFY(opened); QVERIFY(rowsFit); QVERIFY(draftKept); QVERIFY(selectionKept);
        QCOMPARE(toolbar.items(), pins);
    }

    void unplacedToolsCanBePinnedAndActivated()
    {
        Fixture f; QVERIFY(f.load()); ToolSession session(&f.canvas, &f.catalog);
        const auto ellipse = f.tool(Canvas::Tool::Ellipse);
        QStringList placements;
        for (const auto &entry : f.catalog.entries()) if (entry.tools.contains(ellipse)) placements.append(entry.id);
        for (const auto &placement : placements) QVERIFY(f.catalog.removeTool(placement, ellipse));
        QVERIFY(f.catalog.tool(ellipse));
        QuickAccessToolbar toolbar(&f.canvas, &f.catalog, &f.settings);
        QVERIFY(toolbar.addTool(ellipse)); toolbar.itemAction("tool:" + ellipse)->trigger();
        QCOMPARE(f.catalog.activeToolId(), ellipse); QCOMPARE(f.canvas.tool(), Canvas::Tool::Ellipse);
        QVERIFY(toolbar.itemAction("tool:" + ellipse)->isChecked());
    }

    void noProjectKeepsCustomizationButDisablesCanvasActions()
    {
        Fixture f; QVERIFY(f.library.load()); QVERIFY(f.catalog.load());
        QuickAccessToolbar toolbar(&f.canvas, &f.catalog, &f.settings);
        QVERIFY(!toolbar.drawingModeAction()->isEnabled());
        QVERIFY(toolbar.findChild<QAction *>("quickAccessAdd")->isEnabled());
        for (const auto &item : toolbar.items()) QVERIFY(!toolbar.itemAction(item.id())->isEnabled());
        f.canvas.setProject(Project::create());
        QVERIFY(toolbar.drawingModeAction()->isEnabled());
        for (const auto &item : toolbar.items()) QVERIFY(toolbar.itemAction(item.id())->isEnabled());
    }

    void navigationChangesTheViewAndRoutesHandThroughCatalog()
    {
        Fixture f; QVERIFY(f.load()); ToolSession session(&f.canvas, &f.catalog);
        QuickAccessToolbar toolbar(&f.canvas, &f.catalog, &f.settings);
        QVERIFY(toolbar.addNavigation("zoom-in")); QVERIFY(toolbar.addNavigation("zoom-out"));
        QVERIFY(toolbar.addNavigation("reset-rotation"));
        f.canvas.setZoom(.5);
        toolbar.itemAction("navigation:zoom-in")->trigger(); QCOMPARE(f.canvas.zoom(), .625);
        toolbar.itemAction("navigation:zoom-out")->trigger(); QCOMPARE(f.canvas.zoom(), .5);
        f.canvas.setRotation(30); toolbar.itemAction("navigation:reset-rotation")->trigger(); QCOMPARE(f.canvas.rotation(), 0.);
        toolbar.itemAction("navigation:hand")->trigger(); QCOMPARE(f.canvas.tool(), Canvas::Tool::Hand);
        QCOMPARE(f.catalog.activeToolId(), f.tool(Canvas::Tool::Hand));
        QVERIFY(toolbar.itemAction("navigation:hand")->isChecked());
        f.canvas.fitSheet(); const auto fitted = f.canvas.zoom();
        f.canvas.setZoom(2); toolbar.itemAction("navigation:fit-sheet")->trigger(); QCOMPARE(f.canvas.zoom(), fitted);
        QVERIFY(!toolbar.addNavigation("delete-all"));
    }

    void persistenceReorderingDeduplicationAndEmptyConfiguration()
    {
        Fixture f; QVERIFY(f.load());
        QuickAccessToolbar toolbar(&f.canvas, &f.catalog, &f.settings); clearPins(toolbar);
        const auto id = f.tool(Canvas::Tool::Line);
        QVERIFY(toolbar.addTool(id)); QVERIFY(!toolbar.addTool(id)); QVERIFY(!toolbar.addTool("missing"));
        QVERIFY(toolbar.addColor(QColor("#123456"))); QVERIFY(!toolbar.addColor(QColor("#FF123456")));
        QVERIFY(!toolbar.addColor(QColor()));
        QVERIFY(toolbar.addNavigation("zoom-in")); QVERIFY(!toolbar.addNavigation("zoom-in"));
        QVERIFY(toolbar.moveItem("navigation:zoom-in", 0));
        QCOMPARE(toolbar.items().first().id(), QString("navigation:zoom-in"));
        QVERIFY(!toolbar.moveItem("tool:" + id, -1)); QVERIFY(!toolbar.moveItem("missing", 0));
        QVERIFY(!toolbar.removeItem("missing"));
        f.settings.sync();
        QSettings reloaded(f.dir.filePath("preferences.ini"), QSettings::IniFormat);
        QuickAccessToolbar restored(&f.canvas, &f.catalog, &reloaded);
        QCOMPARE(restored.items(), toolbar.items());
        QVERIFY(restored.removeItem("tool:" + id)); QVERIFY(f.catalog.tool(id));
        clearPins(restored); reloaded.sync();
        QuickAccessToolbar empty(&f.canvas, &f.catalog, &reloaded); QVERIFY(empty.items().isEmpty());
        QCOMPARE(empty.actions().size(), 3); // Exit, customization, separator remain available.
    }

    void removedBrushCannotLeaveAStaleAction()
    {
        Fixture f; QVERIFY(f.load());
        QuickAccessToolbar toolbar(&f.canvas, &f.catalog, &f.settings);
        const auto brush = f.tool(Canvas::Tool::Brush);
        const auto copy = f.catalog.duplicateTool(brush, f.catalog.selectedEntryId(), "Disposable brush");
        QVERIFY(!copy.isEmpty()); QVERIFY(toolbar.addTool(copy));
        QVERIFY(f.catalog.deleteBrushes({copy}));
        QVERIFY(!toolbar.itemAction("tool:" + copy));
        QuickAccessToolbar restored(&f.canvas, &f.catalog, &f.settings);
        QVERIFY(!restored.itemAction("tool:" + copy));
        QVERIFY(restored.itemAction("tool:" + brush));
    }

    void boundedPreferencesRejectMalformedData_data()
    {
        QTest::addColumn<QByteArray>("bytes");
        QTest::newRow("malformed") << QByteArray("{");
        QTest::newRow("wrong-root") << QByteArray("[]");
        QTest::newRow("future-version") << preference({colorItem()}, 2);
        QTest::newRow("fractional-version") << preference({colorItem()}, 1.2);
        QTest::newRow("unknown-kind") << preference({QJsonObject{{"kind", "command"}, {"value", "erase"}}});
        QTest::newRow("invalid-navigation") << preference({QJsonObject{{"kind", "navigation"}, {"value", "erase"}}});
        QTest::newRow("invalid-color") << preference({colorItem("red")});
        QTest::newRow("wrong-field-type") << preference({QJsonObject{{"kind", "color"}, {"value", 32}}});
        QTest::newRow("unknown-field") << preference({QJsonObject{{"kind", "color"}, {"value", "#ff123456"}, {"extra", true}}});
        QTest::newRow("duplicate") << preference({colorItem(), colorItem()});
        QTest::newRow("not-partial") << preference({colorItem(), colorItem("invalid")});
        QTest::newRow("oversize-payload") << QByteArray(32769, ' ');
        QJsonArray excessive; for (int i = 0; i <= QuickAccessToolbar::maxItems; ++i) excessive.append(colorItem(QColor(i).name(QColor::HexArgb)));
        QTest::newRow("excessive-items") << preference(excessive);
    }
    void boundedPreferencesRejectMalformedData()
    {
        QFETCH(QByteArray, bytes);
        Fixture f; QVERIFY(f.load()); f.settings.setValue("quickAccess/v1", bytes);
        QuickAccessToolbar toolbar(&f.canvas, &f.catalog, &f.settings);
        QVERIFY(toolbar.items().isEmpty());
        QCOMPARE(f.settings.value("quickAccess/v1").toByteArray(), bytes); // No destructive rewrite on read.
        QVERIFY(toolbar.drawingModeAction()->isEnabled());
    }

    void missingToolsAreSkippedAndCapacityIsEnforced()
    {
        Fixture f; QVERIFY(f.load());
        f.settings.setValue("quickAccess/v1", preference({QJsonObject{{"kind", "tool"}, {"value", "missing-brush"}}, colorItem()}));
        QuickAccessToolbar toolbar(&f.canvas, &f.catalog, &f.settings);
        QCOMPARE(toolbar.items().size(), 1);
        while (toolbar.items().size() < QuickAccessToolbar::maxItems)
            QVERIFY(toolbar.addColor(QColor::fromRgb(toolbar.items().size() * 1397)));
        QVERIFY(!toolbar.addNavigation("zoom-in"));
        QCOMPARE(toolbar.items().size(), QuickAccessToolbar::maxItems);
        QuickAccessToolbar restored(&f.canvas, &f.catalog, &f.settings);
        QCOMPARE(restored.items(), toolbar.items());
    }

    void drawingModeActionFollowsConfirmedStateAndHasNoGlobalShortcut()
    {
        Fixture f; QVERIFY(f.load()); QuickAccessToolbar toolbar(&f.canvas, &f.catalog, &f.settings);
        QSignalSpy requests(&toolbar, &QuickAccessToolbar::drawingModeRequested);
        auto *action = toolbar.drawingModeAction(); QVERIFY(action->shortcut().isEmpty());
        action->trigger(); QCOMPARE(requests.count(), 1); QVERIFY(!toolbar.drawingMode()); QVERIFY(!action->isChecked());
        toolbar.setDrawingMode(true); QVERIFY(toolbar.drawingMode()); QVERIFY(action->isChecked());
        QCOMPARE(action->text(), QString("Return to workspace (Tab)"));
        action->trigger(); QCOMPARE(requests.count(), 2); QVERIFY(action->isChecked());
        toolbar.setDrawingMode(false); QVERIFY(!action->isChecked());
    }

    void kindBoundariesPreservePinOrderAndWorkspaceControls()
    {
        Fixture f; QVERIFY(f.load()); QuickAccessToolbar toolbar(&f.canvas, &f.catalog, &f.settings);
        clearPins(toolbar);
        auto *workspaceControl = new QWidgetAction(&toolbar);
        workspaceControl->setObjectName("workspaceContext");
        workspaceControl->setDefaultWidget(new QPushButton("Drawing tools", &toolbar));
        toolbar.insertAction(toolbar.findChild<QAction *>("quickAccessAdd"), workspaceControl);
        QVERIFY(toolbar.addColor(QColor("#ef315c")));
        QVERIFY(toolbar.addNavigation("zoom-in"));
        QVERIFY(toolbar.addNavigation("zoom-out"));
        const auto toolId = "tool:" + f.tool(Canvas::Tool::Line);
        QVERIFY(toolbar.addTool(f.tool(Canvas::Tool::Line)));
        const auto verify = [&] {
            QStringList actual;
            int separators = 0;
            for (auto *action : toolbar.actions()) {
                if (action->isSeparator()) ++separators;
                if (!action->data().toString().isEmpty()) actual.append(action->data().toString());
            }
            QStringList expected;
            int boundaries = 0;
            for (int i = 0; i < toolbar.items().size(); ++i) {
                expected.append(toolbar.items()[i].id());
                if (i && toolbar.items()[i - 1].kind != toolbar.items()[i].kind) ++boundaries;
            }
            QCOMPARE(actual, expected);
            QCOMPARE(separators, boundaries + 1);
            QVERIFY(toolbar.actions().contains(workspaceControl));
            QVERIFY(toolbar.widgetForAction(workspaceControl));
        };
        verify();
        QVERIFY(toolbar.moveItem(toolId, 1)); verify();
        QVERIFY(toolbar.removeItem("navigation:zoom-in")); verify();
        const auto saved = toolbar.items();
        QuickAccessToolbar restored(&f.canvas, &f.catalog, &f.settings);
        QCOMPARE(restored.items(), saved);
        clearPins(toolbar); verify();
    }

    void pinActivationDuringStrokePreservesInkStateAndPreferences()
    {
        Fixture f; QVERIFY(f.load()); ToolSession session(&f.canvas, &f.catalog);
        QuickAccessToolbar toolbar(&f.canvas, &f.catalog, &f.settings);
        QVERIFY(toolbar.addNavigation("zoom-in"));
        const QColor red("#ef315c"); QVERIFY(toolbar.addColor(red));
        const auto rectangle = f.tool(Canvas::Tool::Rectangle); QVERIFY(toolbar.addTool(rectangle));
        const auto initialTool = f.catalog.activeToolId();
        const auto brush = f.canvas.brush;
        const auto zoom = f.canvas.zoom();
        const auto stored = f.settings.value("quickAccess/v1");
        const auto history = f.canvas.document()->history()->count();
        QVERIFY(f.canvas.document()->beginStroke({100, 100}, 1, brush, false));
        toolbar.itemAction("tool:" + rectangle)->trigger();
        toolbar.itemAction("color:" + red.name(QColor::HexArgb))->trigger();
        toolbar.itemAction("navigation:zoom-in")->trigger();
        QCOMPARE(f.catalog.activeToolId(), initialTool); QVERIFY(f.canvas.brush == brush);
        QCOMPARE(f.canvas.zoom(), zoom); QVERIFY(f.canvas.document()->strokeActive());
        QCOMPARE(f.settings.value("quickAccess/v1"), stored);
        QCOMPARE(f.canvas.document()->history()->count(), history);
        QVERIFY(!toolbar.itemAction("tool:" + rectangle)->isChecked());
        QVERIFY(!toolbar.itemAction("color:" + red.name(QColor::HexArgb))->isChecked());
        f.canvas.document()->cancelStroke();
        toolbar.itemAction("color:" + red.name(QColor::HexArgb))->trigger();
        QCOMPARE(f.canvas.brush.color, red);
    }

    void contextualTextCanGrowWithoutClippingTheStrip()
    {
        Fixture f; QVERIFY(f.load()); QMainWindow window;
        auto *toolbar = new QuickAccessToolbar(&f.canvas, &f.catalog, &f.settings, &window);
        auto *context = new QWidgetAction(toolbar);
        auto *button = new QPushButton("Round Ink · 12", toolbar);
        button->setProperty("compactDrawingButton", true);
        button->setMinimumHeight(ui::metrics::actionTarget);
        context->setDefaultWidget(button);
        toolbar->insertAction(toolbar->findChild<QAction *>("quickAccessAdd"), context);
        window.setCentralWidget(new QWidget); window.addToolBar(toolbar); window.resize(700, 200); window.show();
        QCoreApplication::processEvents();
        const auto normalHeight = toolbar->height();
        QFont large = button->font(); large.setPixelSize(30); button->setFont(large);
        // The child invalidates the toolbar, whose layout request then reaches
        // QMainWindow on a subsequent event turn.
        QTRY_VERIFY(button->height() >= button->fontMetrics().height() + 4);
        QTRY_VERIFY(toolbar->height() > normalHeight);
        QVERIFY(toolbar->rect().contains(button->geometry()));
        const auto mode = toolbar->widgetForAction(toolbar->drawingModeAction());
        QCOMPARE(mode->size(), QSize(ui::metrics::actionTarget, ui::metrics::actionTarget));
    }

    void narrowToolbarKeepsExitAndExposesOverflowActions()
    {
        Fixture f; QVERIFY(f.load()); QMainWindow window;
        auto *toolbar = new QuickAccessToolbar(&f.canvas, &f.catalog, &f.settings, &window);
        for (int i = 0; i < 12; ++i) QVERIFY(toolbar->addColor(QColor::fromHsv(i * 30, 170, 220)));
        window.setCentralWidget(new QWidget); window.addToolBar(toolbar); window.resize(230, 200); window.show();
        QTest::qWait(30);
        auto *mode = qobject_cast<QToolButton *>(toolbar->widgetForAction(toolbar->drawingModeAction()));
        QVERIFY(mode && mode->isVisible()); QCOMPARE(mode->size(), QSize(ui::metrics::actionTarget, ui::metrics::actionTarget));
        QCOMPARE(mode->iconSize(), QSize(ui::metrics::actionIcon, ui::metrics::actionIcon));
        auto *activeTool = qobject_cast<QToolButton *>(toolbar->widgetForAction(
            toolbar->itemAction("tool:" + f.catalog.activeToolId())));
        QVERIFY(activeTool && activeTool->isChecked());
        QCOMPARE(activeTool->size(), QSize(ui::metrics::actionTarget, ui::metrics::actionTarget));
        QCOMPARE(activeTool->iconSize(), QSize(ui::metrics::primaryIcon, ui::metrics::primaryIcon));
        auto *add = toolbar->widgetForAction(toolbar->findChild<QAction *>("quickAccessAdd"));
        QVERIFY(add && add->isVisible()); QVERIFY(toolbar->rect().contains(add->geometry()));
        QVERIFY(!mode->accessibleName().isEmpty());
        QCOMPARE(mode->style()->styleHint(QStyle::SH_ToolTip_WakeUpDelay, nullptr, mode), ui::tooltipDelayMs);
        auto *overflow = toolbar->findChild<QToolButton *>("qt_toolbar_ext_button"); QVERIFY(overflow); QVERIFY(overflow->isVisible());
        QCOMPARE(overflow->accessibleName(), QString("More quick access items"));
        toolbar->setDrawingMode(true);
        QVERIFY(mode->isVisible()); QVERIFY(add->isVisible());
        QCOMPARE(mode->toolTip(), QString("Return to workspace (Tab on canvas)"));
        // Qt uses the same QActions in its overflow menu, so commands and colors
        // retain their exact behavior even when the icon cannot fit in the row.
        auto *last = toolbar->itemAction(toolbar->items().last().id()); QVERIFY(last);
        last->trigger(); QCOMPARE(f.canvas.brush.color, QColor(toolbar->items().last().value));
        QVERIFY(!toolbar->isMovable()); QVERIFY(!toolbar->isFloatable()); QCOMPARE(toolbar->allowedAreas(), Qt::ToolBarAreas(Qt::TopToolBarArea));
    }
};
QTEST_MAIN(QuickAccessToolbarTests)
#include "test_quick_access_toolbar.moc"
