#include "brush_preset_panel.h"
#include "brush_settings_panel.h"
#include "canvas.h"
#include "camera_tool.h"
#include "drawing_input.h"
#include "keyframe_transform_tool.h"
#include "main_window.h"
#include "mask_tool.h"
#include "tool_session.h"
#include "ui_metrics.h"
#include "ui_theme.h"
#include "value_slider.h"

#include <QAction>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QPushButton>
#include <QScreen>
#include <QScopeGuard>
#include <QScrollBar>
#include <QSettings>
#include <QSlider>
#include <QStyle>
#include <QTemporaryDir>
#include <QTabBar>
#include <QTimer>
#include <QToolButton>
#include <QtTest>
#include <algorithm>
#include <cmath>
#include <limits>

using namespace toon;

namespace {
Project projectFixture()
{
    auto project = Project::create();
    project.width = 640;
    project.height = 480;
    project.transparent = true;
    return project;
}

struct Fixture {
    QTemporaryDir directory;
    QSettings settings{directory.filePath("workspace.ini"), QSettings::IniFormat};
    Canvas canvas;
    Fixture() { canvas.resize(800, 600); canvas.setProject(projectFixture()); }
};

QByteArray preference(const QJsonArray &sizes, const QJsonValue &version = 1)
{
    return QJsonDocument(QJsonObject{{"version", version}, {"sizes", sizes}}).toJson(QJsonDocument::Compact);
}

QListWidgetItem *sizeItem(BrushPresetPanel &panel, double value)
{
    auto *list = panel.findChild<QListWidget *>("brushPresetSizes");
    if (!list) return nullptr;
    for (int row = 0; row < list->count(); ++row)
        if (list->item(row)->data(Qt::UserRole).toDouble() == value) return list->item(row);
    return nullptr;
}

void clearPresets(BrushPresetPanel &panel)
{
    const auto previous = panel.sizes();
    for (double value : previous) QVERIFY(panel.removeSize(value));
}

QString toolId(const ToolCatalog &catalog, Canvas::Tool engine)
{
    for (const auto &tool : catalog.tools()) if (tool.engine == engine) return tool.id;
    return {};
}

void preview(QWidget &widget, const QString &name)
{
    const auto directory = qEnvironmentVariable("TOON_BRUSH_PRESET_PREVIEW");
    if (directory.isEmpty()) return;
    QDir().mkpath(directory);
    QVERIFY(widget.grab().save(QDir(directory).filePath(name + ".png")));
}

bool prepare(MainWindow &window, const QTemporaryDir &directory)
{
    QString error;
    const auto path = directory.filePath("drawing.toonproj");
    if (!saveProject(projectFixture(), path, &error) || !window.openProject(path)) return false;
    const auto available = window.screen()->availableGeometry();
    window.resize(QSize(1280, 860).boundedTo(available.size() - QSize(40, 60)));
    window.move(available.topLeft() + QPoint(20, 40));
    window.show();
    window.activateWindow();
    window.canvas()->setFocus();
    QCoreApplication::processEvents();
    return true;
}
} // namespace

class BrushPresetPanelTests final : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { configureDrawingInput(); applyTheme(); }

    void activationChangesOnlySizeAndUpdatesQuickPropertiesAndToolSession()
    {
        Fixture f; QVERIFY(f.directory.isValid());
        BrushLibrary library(f.directory.filePath("brushes.json")); QVERIFY(library.load());
        ToolCatalog catalog(&library, f.directory.filePath("tools.json")); QVERIFY(catalog.load());
        ToolSession session(&f.canvas, &catalog);
        BrushSettingsPanel properties(&f.canvas, &library);
        BrushPresetPanel panel(&f.canvas, &f.settings);
        const auto brushId = toolId(catalog, Canvas::Tool::Brush);
        const auto eraserId = toolId(catalog, Canvas::Tool::Eraser);
        QVERIFY(catalog.activateTool(brushId));
        auto expected = f.canvas.brush;
        expected.color = QColor("#c64e29"); expected.opacity = .37; expected.spacing = .19;
        expected.angle = 27; expected.sizeDynamics.jitter = .42;
        f.canvas.setBrushSettings(expected); f.canvas.setStabilization(24);
        const auto history = f.canvas.document()->history()->count();
        const auto organizationHistory = catalog.history()->count();
        QVERIFY(panel.addSize(37.5));
        QVERIFY(panel.activateSize(37.5));
        expected.size = 37.5;
        QVERIFY(f.canvas.brush == expected);
        QCOMPARE(f.canvas.stabilization(), 24.);
        QCOMPARE(f.canvas.tool(), Canvas::Tool::Brush);
        QCOMPARE(catalog.activeToolId(), brushId);
        QCOMPARE(catalog.tool(brushId)->state.brush.size, 37.5);
        auto *quickSize = properties.findChild<ValueSlider *>("quickBrushSize"); QVERIFY(quickSize);
        QCOMPARE(quickSize->value(), 37.5);
        QVERIFY(sizeItem(panel, 37.5)->isSelected());
        QVERIFY(catalog.activateTool(eraserId));
        QVERIFY(panel.activateSize(8));
        QCOMPARE(f.canvas.tool(), Canvas::Tool::Eraser);
        QCOMPARE(catalog.tool(eraserId)->state.brush.size, 8.);
        QVERIFY(catalog.activateTool(brushId));
        QCOMPARE(f.canvas.brush.size, 37.5);
        QVERIFY(sizeItem(panel, 37.5)->isSelected());
        QCOMPARE(f.canvas.document()->history()->count(), history);
        QCOMPARE(catalog.history()->count(), organizationHistory);
        session.flush();
        ToolCatalog reopened(&library, catalog.path()); QVERIFY(reopened.load());
        QCOMPARE(reopened.tool(brushId)->state.brush.size, 37.5);
        QCOMPARE(reopened.tool(eraserId)->state.brush.size, 8.);
    }

    void clickAndExternalSizeChangesKeepExactActiveHighlight()
    {
        Fixture f; BrushPresetPanel panel(&f.canvas, &f.settings);
        panel.resize(260, 320); panel.show(); panel.activateWindow();
        auto *list = panel.findChild<QListWidget *>("brushPresetSizes"); QVERIFY(list);
        QTRY_VERIFY(list->isVisible());
        auto *item = sizeItem(panel, 8); QVERIFY(item);
        list->scrollToItem(item); QCoreApplication::processEvents();
        QTest::mouseClick(list->viewport(), Qt::LeftButton, Qt::NoModifier, list->visualItemRect(item).center());
        QCOMPARE(f.canvas.brush.size, 8.); QVERIFY(item->isSelected());
        f.canvas.setBrushSize(12); QVERIFY(sizeItem(panel, 12)->isSelected()); QVERIFY(!item->isSelected());
        f.canvas.setBrushSize(13.7); QVERIFY(list->selectedItems().isEmpty());
        QVERIFY(panel.addSize(13.7)); QVERIFY(sizeItem(panel, 13.7)->isSelected());
        const auto saved = f.settings.value("brushPresets/v1").toByteArray();
        f.canvas.setBrushSize(8); f.canvas.setBrushColor(Qt::red);
        QCOMPARE(f.settings.value("brushPresets/v1").toByteArray(), saved);
    }

    void nonBrushToolsAndMissingProjectsCannotActivateHiddenBrushSettings()
    {
        Fixture f; BrushPresetPanel panel(&f.canvas, &f.settings);
        auto *list = panel.findChild<QListWidget *>("brushPresetSizes"); QVERIFY(list);
        f.canvas.setTool(Canvas::Tool::Rectangle);
        const auto brush = f.canvas.brush;
        QVERIFY(!panel.activateSize(8)); QVERIFY(!list->isEnabled()); QVERIFY(list->selectedItems().isEmpty());
        QVERIFY(f.canvas.brush == brush); QCOMPARE(f.canvas.tool(), Canvas::Tool::Rectangle);
        f.canvas.setTool(Canvas::Tool::Retouch);
        QVERIFY(!panel.activateSize(8)); QVERIFY(f.canvas.brush == brush);
        f.canvas.setTool(Canvas::Tool::Brush); QVERIFY(list->isEnabled());
        f.canvas.setToolAvailable(false); QVERIFY(!panel.activateSize(8));
        f.canvas.setToolAvailable(true); QVERIFY(panel.activateSize(8));
        Canvas welcome; BrushPresetPanel empty(&welcome, &f.settings);
        QVERIFY(!empty.activateSize(8));
        QVERIFY(!empty.findChild<QListWidget *>("brushPresetSizes")->isEnabled());
        QVERIFY(empty.findChild<QToolButton *>("brushPresetAdd")->isEnabled());
        QVERIFY(empty.addSize(22.5));
    }

    void preferencesNormalizeDeduplicatePersistAndKeepExplicitEmpty()
    {
        Fixture f; BrushPresetPanel panel(&f.canvas, &f.settings); clearPresets(panel);
        QVERIFY(panel.addSize(37.54)); QCOMPARE(panel.sizes(), QVector<double>({37.5}));
        QVERIFY(!panel.addSize(37.51));
        QVERIFY(panel.addSize(4)); QVERIFY(panel.addSize(100));
        QCOMPARE(panel.sizes(), QVector<double>({4, 37.5, 100}));
        QVERIFY(!panel.replaceSize(4, 100)); QVERIFY(!panel.replaceSize(90, 6));
        QVERIFY(panel.replaceSize(4, 7.24));
        QCOMPARE(panel.sizes(), QVector<double>({7.2, 37.5, 100}));
        QVERIFY(!panel.removeSize(4)); QVERIFY(panel.removeSize(37.5));
        f.settings.sync();
        QSettings restoredSettings(f.directory.filePath("workspace.ini"), QSettings::IniFormat);
        BrushPresetPanel restored(&f.canvas, &restoredSettings);
        QCOMPARE(restored.sizes(), panel.sizes());
        clearPresets(restored); restoredSettings.sync();
        BrushPresetPanel empty(&f.canvas, &restoredSettings); QVERIFY(empty.sizes().isEmpty());
        empty.resetSizes(); QVERIFY(!empty.sizes().isEmpty());
        BrushPresetPanel defaults(&f.canvas, &restoredSettings); QCOMPARE(defaults.sizes(), empty.sizes());
    }

    void overlayToolsAndPlaybackCannotChangeDormantBrushSize()
    {
        Fixture f; BrushPresetPanel panel(&f.canvas, &f.settings);
        auto *list = panel.findChild<QListWidget *>("brushPresetSizes"); QVERIFY(list);
        const auto original = f.canvas.brush;
        QVERIFY(f.canvas.document()->addLayerMask());
        f.canvas.maskTool()->setEnabled(true);
        QVERIFY(f.canvas.maskTool()->enabled()); QVERIFY(!panel.activateSize(8)); QVERIFY(!list->isEnabled());
        f.canvas.maskTool()->setEnabled(false);
        f.canvas.cameraTool()->setEnabled(true);
        QVERIFY(f.canvas.cameraTool()->enabled()); QVERIFY(!panel.activateSize(8)); QVERIFY(!list->isEnabled());
        f.canvas.cameraTool()->setEnabled(false);
        f.canvas.keyframeTransformTool()->setEnabled(true);
        QVERIFY(f.canvas.keyframeTransformTool()->enabled()); QVERIFY(!panel.activateSize(8)); QVERIFY(!list->isEnabled());
        f.canvas.keyframeTransformTool()->setEnabled(false);
        f.canvas.setAnimationPlayback(true);
        QVERIFY(!panel.activateSize(8)); QVERIFY(!list->isEnabled());
        f.canvas.setAnimationPlayback(false);
        QVERIFY(f.canvas.brush == original); QVERIFY(list->isEnabled());
        QVERIFY(panel.activateSize(8)); QCOMPARE(f.canvas.brush.size, 8.);
    }

    void invalidValuesAndCapacityNeverChangeStoredPreferences()
    {
        Fixture f; BrushPresetPanel panel(&f.canvas, &f.settings); clearPresets(panel);
        QVERIFY(panel.addSize(1)); QVERIFY(panel.addSize(256));
        const auto before = f.settings.value("brushPresets/v1").toByteArray();
        for (double invalid : {0., .9, 1024.1, 10000., std::numeric_limits<double>::quiet_NaN(),
                              std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity()}) {
            QVERIFY(!panel.addSize(invalid)); QVERIFY(!panel.replaceSize(1, invalid));
            QVERIFY(!panel.activateSize(invalid));
        }
        QCOMPARE(f.settings.value("brushPresets/v1").toByteArray(), before);
        for (int value = 2; panel.sizes().size() < BrushPresetPanel::maxPresets; ++value)
            QVERIFY(panel.addSize(value));
        QCOMPARE(panel.sizes().size(), BrushPresetPanel::maxPresets);
        QVERIFY(!panel.addSize(255));
        QVERIFY(panel.replaceSize(1, 255));
        BrushPresetPanel reloaded(&f.canvas, &f.settings); QCOMPARE(reloaded.sizes(), panel.sizes());
    }

    void malformedOrFuturePreferencesArePreserved_data()
    {
        QTest::addColumn<QByteArray>("bytes");
        QTest::newRow("malformed") << QByteArray("{");
        QTest::newRow("wrong-root") << QByteArray("[]");
        QTest::newRow("future-version") << preference({4, 8}, 2);
        QTest::newRow("fractional-version") << preference({4, 8}, 1.2);
        QTest::newRow("string-value") << preference({4, "8"});
        QTest::newRow("null-value") << preference({4, QJsonValue(QJsonValue::Null)});
        QTest::newRow("out-of-range") << preference({4, 1025});
        QTest::newRow("extra-precision") << preference({4, 8.125});
        QTest::newRow("unknown-field") << QByteArray("{\"version\":1,\"sizes\":[8],\"extra\":true}");
        QJsonArray excessive; for (int i = 0; i <= BrushPresetPanel::maxPresets; ++i) excessive.append(i + 1);
        QTest::newRow("excessive-items") << preference(excessive);
        QTest::newRow("oversized") << QByteArray(32769, ' ');
    }

    void malformedOrFuturePreferencesArePreserved()
    {
        QFETCH(QByteArray, bytes);
        Fixture f; BrushPresetPanel defaults(&f.canvas, &f.settings);
        f.settings.setValue("brushPresets/v1", bytes);
        BrushPresetPanel panel(&f.canvas, &f.settings);
        QCOMPARE(panel.sizes(), defaults.sizes());
        QCOMPARE(f.settings.value("brushPresets/v1").toByteArray(), bytes);
        QVERIFY(panel.activateSize(8));
        QCOMPARE(f.settings.value("brushPresets/v1").toByteArray(), bytes);
    }

    void validUnsortedPreferencesNormalizeWithoutRewriting()
    {
        Fixture f; const auto raw = preference({90, 4.5, 90, 1, 8});
        f.settings.setValue("brushPresets/v1", raw);
        BrushPresetPanel panel(&f.canvas, &f.settings);
        QCOMPARE(panel.sizes(), QVector<double>({1, 4.5, 8, 90}));
        QCOMPARE(f.settings.value("brushPresets/v1").toByteArray(), raw);
    }

    void addDialogProvidesSliderAndCompactNumberWithCancelAndAccept()
    {
        Fixture f; BrushPresetPanel panel(&f.canvas, &f.settings);
        panel.resize(260, 320); panel.show();
        auto *add = panel.findChild<QToolButton *>("brushPresetAdd"); QVERIFY(add);
        const auto before = panel.sizes();
        bool inspected = false;
        QTimer::singleShot(0, &panel, [&] {
            auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget()); QVERIFY(dialog);
            const auto cleanup = qScopeGuard([dialog] { if (dialog->isVisible()) dialog->reject(); });
            QCOMPARE(dialog->objectName(), QString("brushPresetSizeDialog"));
            auto *value = dialog->findChild<ValueSlider *>("brushPresetSizeValue"); QVERIFY(value);
            auto *number = value->findChild<QDoubleSpinBox *>(); QVERIFY(number);
            QVERIFY(value->findChild<QSlider *>()); QCOMPARE(number->width(), 48); QVERIFY(number->suffix().isEmpty());
            value->setValue(33.5); inspected = true; dialog->reject();
        });
        QTest::mouseClick(add, Qt::LeftButton); QVERIFY(inspected); QCOMPARE(panel.sizes(), before);
        QTimer::singleShot(0, &panel, [&] {
            auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget()); QVERIFY(dialog);
            const auto cleanup = qScopeGuard([dialog] { if (dialog->isVisible()) dialog->reject(); });
            QCOMPARE(dialog->objectName(), QString("brushPresetSizeDialog"));
            auto *value = dialog->findChild<ValueSlider *>("brushPresetSizeValue"); QVERIFY(value);
            value->setValue(33.5);
            auto *buttons = dialog->findChild<QDialogButtonBox *>(); QVERIFY(buttons);
            auto *accept = buttons->button(QDialogButtonBox::Ok); QVERIFY(accept);
            QTest::mouseClick(accept, Qt::LeftButton);
        });
        QTest::mouseClick(add, Qt::LeftButton);
        QVERIFY(panel.sizes().contains(33.5));
        BrushPresetPanel reopened(&f.canvas, &f.settings); QVERIFY(reopened.sizes().contains(33.5));
    }

    void activeStrokeRejectsPresetWithoutInterruptingDrawingOrChangingHistory()
    {
        Fixture f; BrushPresetPanel panel(&f.canvas, &f.settings);
        f.canvas.setBrushSize(12);
        const auto before = f.canvas.brush;
        const auto history = f.canvas.document()->history()->count();
        const auto stored = f.settings.value("brushPresets/v1");
        QVERIFY(f.canvas.document()->beginStroke({100, 100}, 1, f.canvas.brush, false));
        QVERIFY(!panel.activateSize(8));
        QVERIFY(f.canvas.document()->strokeActive()); QVERIFY(f.canvas.brush == before);
        QCOMPARE(f.canvas.document()->history()->count(), history);
        QCOMPARE(f.settings.value("brushPresets/v1"), stored);
        f.canvas.document()->cancelStroke();
        QVERIFY(panel.activateSize(8)); QCOMPARE(f.canvas.brush.size, 8.);
        QCOMPARE(f.canvas.document()->history()->count(), history);
    }

    void responsiveGridRemainsAccessibleAndUsesDelayedHelp()
    {
        Fixture f; BrushPresetPanel panel(&f.canvas, &f.settings);
        auto *list = panel.findChild<QListWidget *>("brushPresetSizes"); QVERIFY(list);
        QCOMPARE(list->viewMode(), QListView::IconMode); QVERIFY(!list->accessibleName().isEmpty());
        QCOMPARE(list->style()->styleHint(QStyle::SH_ToolTip_WakeUpDelay, nullptr, list), ui::tooltipDelayMs);
        for (int row = 0; row < list->count(); ++row) {
            auto *item = list->item(row);
            QVERIFY(!item->toolTip().isEmpty()); QVERIFY(!item->data(Qt::AccessibleTextRole).toString().isEmpty());
            QVERIFY(!item->icon().isNull()); QVERIFY(!item->text().contains("px"));
        }
        for (auto *button : panel.findChildren<QToolButton *>()) {
            QVERIFY(!button->accessibleName().isEmpty()); QVERIFY(!button->toolTip().isEmpty());
            QVERIFY(button->width() >= 28 && button->height() >= 28);
        }
        panel.resize(180, 280); panel.show();
        QTest::qWait(30);
        QCOMPARE(list->gridSize().height(), qMax(ui::metrics::presetTileHeight,
            ui::metrics::presetCueSize + list->fontMetrics().height() + 6));
        QVERIFY(list->gridSize().height() < 50);
        for (auto *button : panel.findChildren<QToolButton *>())
            QCOMPARE(button->iconSize(), QSize(ui::metrics::actionIcon, ui::metrics::actionIcon));
        QCOMPARE(list->horizontalScrollBar()->maximum(), 0);
        const int narrowTop = list->visualItemRect(list->item(5)).top();
        preview(panel, "brush-preset-narrow");
        panel.resize(440, 280); QTest::qWait(30);
        QCOMPARE(list->horizontalScrollBar()->maximum(), 0);
        QVERIFY(list->visualItemRect(list->item(5)).top() < narrowTop);
        preview(panel, "brush-preset-wide");

        panel.resize(220, 100); QCoreApplication::processEvents();
        QVERIFY(list->verticalScrollBar()->maximum() > 0);
        panel.resize(220, 200); QCoreApplication::processEvents();
        QTRY_COMPARE(list->verticalScrollBar()->maximum(), 0);
        QCOMPARE(list->visualItemRect(list->item(4)).top(), list->visualItemRect(list->item(0)).top());
        QVERIFY(list->visualItemRect(list->item(5)).top() > list->visualItemRect(list->item(0)).top());
        preview(panel, "brush-preset-complete-defaults");
    }

    void exactLabelsAndKeyboardSelectionSurviveFontAndColumnChanges()
    {
        Fixture f; BrushPresetPanel panel(&f.canvas, &f.settings);
        QVERIFY(panel.addSize(1023.9));
        panel.resize(220, 180); panel.show(); panel.activateWindow();
        auto *list = panel.findChild<QListWidget *>("brushPresetSizes"); QVERIFY(list);
        list->setFocus(); QTRY_VERIFY(list->hasFocus());
        const auto history = f.canvas.document()->history()->count();
        const auto saved = f.settings.value("brushPresets/v1");
        QTest::keyClick(list, Qt::Key_End);
        QCOMPARE(f.canvas.brush.size, 1023.9);
        auto *active = sizeItem(panel, 1023.9); QVERIFY(active && active->isSelected());
        QCOMPARE(active->text(), QString("1023.9"));
        const auto iconKey = active->icon().cacheKey();
        const auto normalHeight = list->gridSize().height();
        preview(panel, "brush-preset-compact-exact-size");

        QFont larger = panel.font(); larger.setPixelSize(25); panel.setFont(larger);
        QCoreApplication::processEvents();
        QVERIFY(list->gridSize().height() > normalHeight);
        QVERIFY(list->gridSize().height() >= list->fontMetrics().height() + ui::metrics::presetCueSize + 6);
        for (const int width : {180, 440, 220}) {
            panel.resize(width, 240); QCoreApplication::processEvents();
            QVERIFY(list->hasFocus());
            QCOMPARE(list->currentItem(), active); QVERIFY(active->isSelected());
            QCOMPARE(f.canvas.brush.size, 1023.9);
            QCOMPARE(active->icon().cacheKey(), iconKey);
            QCOMPARE(list->horizontalScrollBar()->maximum(), 0);
            for (int row = 0; row < list->count(); ++row) {
                const auto rect = list->visualItemRect(list->item(row));
                QVERIFY(rect.width() >= list->fontMetrics().horizontalAdvance(list->item(row)->text()) + 8);
                QVERIFY(rect.width() >= ui::metrics::actionTarget);
            }
        }
        list->scrollToItem(active); QCoreApplication::processEvents();
        preview(panel, "brush-preset-larger-font");
        QCOMPARE(f.settings.value("brushPresets/v1"), saved);
        QCOMPARE(f.canvas.document()->history()->count(), history);
        QTest::keyClick(list, Qt::Key_Home); QCOMPARE(f.canvas.brush.size, 1.);
        QTest::keyClick(list, Qt::Key_Right); QCOMPARE(f.canvas.brush.size, 2.);
        QVERIFY(list->hasFocus()); QVERIFY(sizeItem(panel, 2)->isSelected());
    }

    void dockIsAvailableFromPanelsAndRestoresAcrossDrawingModeAndReopen()
    {
        QTemporaryDir directory; QVERIFY(directory.isValid());
        const auto settingsPath = directory.filePath("workspace.ini");
        {
            MainWindow window(settingsPath); QVERIFY(prepare(window, directory));
            auto *dock = window.findChild<QDockWidget *>("brushPresetPanel"); QVERIFY(dock);
            QCOMPARE(dock->windowTitle(), QString("Sizes"));
            QCOMPARE(dock->accessibleName(), QString("Brush Preset"));
            QCOMPARE(dock->toggleViewAction()->text(), QString("Brush Preset"));
            QVERIFY(dock->findChild<BrushPresetPanel *>());
            auto *tools = window.findChild<QDockWidget *>("toolsPanel"); QVERIFY(tools);
            QCOMPARE(window.dockWidgetArea(dock), window.dockWidgetArea(tools));
            QVERIFY(!dock->isHidden());
            QAction *toggle = nullptr;
            for (auto *menu : window.findChildren<QMenu *>()) {
                if (menu->title().remove('&') != "Panels") continue;
                for (auto *action : menu->actions())
                    if (action->text().remove('&') == "Brush Preset") toggle = action;
            }
            QVERIFY(toggle); QVERIFY(toggle->isVisible()); QVERIFY(toggle->isEnabled());
            toggle->trigger(); QVERIFY(dock->isHidden()); toggle->trigger(); dock->raise();
            QTRY_VERIFY(dock->isVisible());
            preview(window, "brush-preset-workspace");
            auto *content = dock->widget();
            window.applyDrawingWorkspace(true); QCoreApplication::processEvents();
            QVERIFY(dock->isHidden()); toggle->trigger();
            auto *drawer = window.findChild<QWidget *>("drawingControlsDrawer"); QVERIFY(drawer);
            QTRY_VERIFY(drawer->isVisible()); QVERIFY(dock->isHidden()); QVERIFY(drawer->isAncestorOf(content));
            auto *tabs = window.findChild<QTabBar *>("drawingDrawerTabs"); QVERIFY(tabs);
            QCOMPARE(tabs->currentIndex(), 1);
            window.applyDrawingWorkspace(false); QCoreApplication::processEvents();
            QCOMPARE(dock->widget(), content); QVERIFY(dock->isVisible()); QVERIFY(drawer->isHidden());
            dock->setFloating(true); dock->resize(260, 360); dock->show();
            window.activateWindow(); window.canvas()->setFocus();
            QCoreApplication::processEvents();
            window.setDrawingMode(true); QVERIFY(window.drawingMode()); QVERIFY(dock->isHidden());
            window.setDrawingMode(false); QVERIFY(!window.drawingMode());
            QVERIFY(dock->isFloating()); QVERIFY(!dock->isHidden());
            QVERIFY(window.close());
        }
        MainWindow reopened(settingsPath);
        QVERIFY(reopened.openProject(directory.filePath("drawing.toonproj"))); reopened.show();
        QCoreApplication::processEvents();
        auto *dock = reopened.findChild<QDockWidget *>("brushPresetPanel"); QVERIFY(dock);
        QVERIFY(dock->isFloating()); QVERIFY(!dock->isHidden());
        QVERIFY(reopened.close());
    }
};

QTEST_MAIN(BrushPresetPanelTests)
#include "test_brush_preset_panel.moc"
