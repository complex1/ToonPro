#include "canvas.h"
#include "icons.h"
#include "main_window.h"
#include "tool_catalog.h"
#include "tool_navigation_panel.h"
#include "ui_theme.h"
#include "ui_metrics.h"
#include "property_row.h"
#include "value_slider.h"

#include <QAbstractButton>
#include <QAction>
#include <QActionGroup>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QLineEdit>
#include <QSlider>
#include <QHelpEvent>
#include <QListWidget>
#include <QMouseEvent>
#include <QPointingDevice>
#include <QPushButton>
#include <QSettings>
#include <QStyle>
#include <QTemporaryDir>
#include <QTabBar>
#include <QTabletEvent>
#include <QToolBar>
#include <QToolButton>
#include <QToolTip>
#include <QtTest>

using namespace toon;

class UiThemeTest final : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { applyTheme(); }

    void sharedPaletteAndConsistentTooltipDelay()
    {
        QCOMPARE(qApp->palette().color(QPalette::Window), QColor(ui::panel));
        QCOMPARE(qApp->palette().color(QPalette::Base), QColor(ui::base));
        QCOMPARE(qApp->palette().color(QPalette::WindowText), QColor(ui::text));
        QCOMPARE(qApp->palette().color(QPalette::Accent), QColor(ui::accent));
        QCOMPARE(qApp->palette().color(QPalette::Disabled, QPalette::ButtonText), QColor(ui::disabled));
        QToolButton button;
        button.ensurePolished();
        QCOMPARE(button.style()->styleHint(QStyle::SH_ToolTip_WakeUpDelay, nullptr, &button), 600);
        QCOMPARE(button.style()->styleHint(QStyle::SH_ToolTip_FallAsleepDelay, nullptr, &button), 0);
    }

    void lateCreatedIconsKeepHelpAndAccessibility()
    {
        QToolButton button;
        button.setIcon(appIcon("brush"));
        button.setToolTip("Paint with the active brush (B)");
        button.ensurePolished();
        QCOMPARE(button.toolTip(), QString("Paint with the active brush (B)"));
        QCOMPARE(button.accessibleName(), button.toolTip());
        QPushButton labeled("Save brush");
        labeled.setIcon(appIcon("save"));
        labeled.ensurePolished();
        QCOMPARE(labeled.toolTip(), QString("Save brush"));
        QCOMPARE(labeled.accessibleName(), QString("Save brush"));

        QDockWidget dock("Refs");
        dock.setProperty("panelLabel", "References");
        dock.ensurePolished();
        for (const auto *name : {"qt_dockwidget_closebutton", "qt_dockwidget_floatbutton"}) {
            auto *control = dock.findChild<QAbstractButton *>(name);
            QVERIFY(control);
            control->setAccessibleName({});
            control->setToolTip({});
            control->ensurePolished();
            QVERIFY2(control->toolTip().contains("References"), name);
            QCOMPARE(control->accessibleName(), control->toolTip());
        }
    }

    void compactTabRolesKeepKeyboardHelpAndLargerFonts()
    {
        QTabBar ordinary, compact;
        compact.setProperty("compactDrawingTabs", true);
        compact.setIconSize({ui::metrics::tabIcon, ui::metrics::tabIcon});
        compact.setExpanding(false); compact.setUsesScrollButtons(true);
        compact.setFocusPolicy(Qt::StrongFocus);
        for (const auto *label : {"Brush", "Shapes", "Selection", "Guides", "Colors"}) {
            ordinary.addTab(label); compact.addTab(label);
        }
        ordinary.ensurePolished(); compact.ensurePolished();
        QVERIFY(compact.sizeHint().height() < ordinary.sizeHint().height());
        compact.resize(160, compact.sizeHint().height()); compact.show();
        compact.activateWindow(); compact.setFocus();
        QTRY_VERIFY(compact.hasFocus());
        for (int i = 1; i < compact.count(); ++i) {
            QTest::keyClick(&compact, Qt::Key_Right);
            QCOMPARE(compact.currentIndex(), i);
            QVERIFY(!compact.accessibleTabName(i).isEmpty());
            QVERIFY(!compact.tabToolTip(i).isEmpty());
        }
        QFont larger = compact.font(); larger.setPointSizeF(larger.pointSizeF() + 6);
        compact.setFont(larger); compact.resize(160, compact.sizeHint().height());
        QCoreApplication::processEvents();
        QVERIFY(compact.tabRect(compact.currentIndex()).height() >= compact.fontMetrics().height() + 4);
        QTest::keyClick(&compact, Qt::Key_Left); QCOMPARE(compact.currentIndex(), 3);

        QTabBar symbols;
        symbols.addTab(appIcon("brush"), QString());
        symbols.setTabToolTip(0, "Brush settings\nChange the active brush");
        symbols.ensurePolished();
        QCOMPARE(symbols.accessibleTabName(0), QString("Brush settings"));
        QCOMPARE(symbols.tabToolTip(0), QString("Brush settings\nChange the active brush"));
    }

    void shellUsesDiscoverableIconActions()
    {
        QTemporaryDir temp;
        MainWindow window(temp.filePath("workspace.ini"));
        window.canvas()->setProject(Project::create());
        window.show();
        QCoreApplication::processEvents();
        auto *toolbar = window.findChild<QToolBar *>("projectToolbar");
        QVERIFY(toolbar);
        QCOMPARE(toolbar->toolButtonStyle(), Qt::ToolButtonIconOnly);
        for (auto *action : toolbar->actions()) {
            if (action->isSeparator()) continue;
            QVERIFY2(!action->icon().isNull(), qPrintable(action->text()));
            QVERIFY2(!action->toolTip().isEmpty(), qPrintable(action->text()));
            if (!action->shortcut().isEmpty())
                QVERIFY(action->toolTip().contains(action->shortcut().toString(QKeySequence::NativeText)));
            auto *button = qobject_cast<QToolButton *>(toolbar->widgetForAction(action));
            QVERIFY(button);
            QVERIFY(!button->accessibleName().isEmpty());
        }
        for (const auto *name : {"navigatorFit", "navigatorResetRotation"}) {
            auto *button = window.findChild<QToolButton *>(name);
            QVERIFY(button); QVERIFY(button->text().isEmpty());
            QVERIFY(!button->icon().isNull()); QVERIFY(!button->toolTip().isEmpty());
            QVERIFY(!button->accessibleName().isEmpty());
        }
        for (auto *dock : window.findChildren<QDockWidget *>()) {
            for (auto *button : dock->findChildren<QAbstractButton *>(QString(), Qt::FindDirectChildrenOnly)) {
                if (button->objectName().startsWith("qt_dockwidget_")) {
                    button->ensurePolished();
                    const auto identity = dock->objectName() + "/" + button->objectName();
                    QVERIFY2(!button->toolTip().isEmpty(), qPrintable(identity));
                    QVERIFY2(!button->accessibleName().isEmpty(), qPrintable(identity));
                }
            }
        }
        for (auto *button : window.findChildren<QAbstractButton *>()) {
            button->ensurePolished();
            if (button->icon().isNull()) continue;
            QVERIFY2(!button->toolTip().isEmpty(), qPrintable(button->objectName()));
            QVERIFY2(!button->accessibleName().isEmpty(), qPrintable(button->objectName()));
        }
        auto *tabs = window.findChild<QTabBar *>("brushSettingsCategory");
        QVERIFY(tabs);
        for (int index = 0; index < tabs->count(); ++index) {
            QVERIFY(!tabs->tabIcon(index).isNull());
            QVERIFY(!tabs->tabToolTip(index).isEmpty());
            QVERIFY(!tabs->accessibleTabName(index).isEmpty());
        }
    }

    void tooltipDoesNotRemainDuringInteraction()
    {
        QToolButton button;
        button.resize(40, 40); button.setToolTip("Brush (B)"); button.show();
        QToolTip::showText(button.mapToGlobal(QPoint(0, 40)), button.toolTip(), &button);
        QVERIFY(QToolTip::isVisible());
        QTest::mousePress(&button, Qt::LeftButton);
        QTRY_VERIFY_WITH_TIMEOUT(!QToolTip::isVisible(), 600);
        QHelpEvent hover(QEvent::ToolTip, button.rect().center(), button.mapToGlobal(button.rect().center()));
        QApplication::sendEvent(&button, &hover);
        QVERIFY(!QToolTip::isVisible());
        QTest::mouseRelease(&button, Qt::LeftButton);
    }

    void tooltipRecoversAfterLostTabletRelease_data()
    {
        QTest::addColumn<bool>("leaveProximity");
        QTest::newRow("hover-without-buttons") << false;
        QTest::newRow("pen-leaves-range") << true;
    }

    void tooltipRecoversAfterLostTabletRelease()
    {
        QFETCH(bool, leaveProximity);
        // Observe whether the application filter permits help without relying
        // on native tooltip window timers or moving the artist's real pointer.
        class HelpTarget final : public QWidget {
        public:
            int helpEvents = 0;
            bool event(QEvent *event) override {
                if (event->type() == QEvent::ToolTip) {
                    ++helpEvents; event->accept(); return true;
                }
                if (event->type() == QEvent::TabletPress || event->type() == QEvent::TabletMove ||
                    event->type() == QEvent::TabletRelease || event->type() == QEvent::MouseMove) {
                    event->accept(); return true;
                }
                return QWidget::event(event);
            }
        } target;
        target.setAttribute(Qt::WA_TabletTracking); target.setMouseTracking(true);
        target.setToolTip("Brush pressure");
        QPointingDevice pen("Tooltip pen", 71, QInputDevice::DeviceType::Stylus,
            QPointingDevice::PointerType::Pen,
            QInputDevice::Capability::Position | QInputDevice::Capability::Pressure, 1, 1);
        const QPointF point(12, 12);
        const auto tablet = [&](QEvent::Type type, Qt::MouseButtons buttons) {
            QTabletEvent event(type, &pen, point, target.mapToGlobal(point),
                buttons == Qt::NoButton ? 0 : .5, 0, 0, 0, 0, 0, Qt::NoModifier,
                type == QEvent::TabletPress || type == QEvent::TabletRelease ? Qt::LeftButton : Qt::NoButton,
                buttons);
            QApplication::sendEvent(type == QEvent::TabletLeaveProximity
                ? static_cast<QObject *>(qApp) : &target, &event);
        };
        const auto help = [&] {
            QHelpEvent event(QEvent::ToolTip, point.toPoint(), target.mapToGlobal(point.toPoint()));
            QApplication::sendEvent(&target, &event);
        };
        const auto synthesizedHover = [&] {
            QMouseEvent event(QEvent::MouseMove, point, point, target.mapToGlobal(point),
                Qt::NoButton, Qt::NoButton, Qt::NoModifier, Qt::MouseEventSynthesizedByQt);
            QApplication::sendEvent(&target, &event);
        };
        help(); QCOMPARE(target.helpEvents, 1);
        tablet(QEvent::TabletPress, Qt::LeftButton);
        synthesizedHover(); help(); QCOMPARE(target.helpEvents, 1);
        tablet(QEvent::TabletMove, Qt::LeftButton);
        help(); QCOMPARE(target.helpEvents, 1);

        tablet(leaveProximity ? QEvent::TabletLeaveProximity : QEvent::TabletMove, Qt::NoButton);
        synthesizedHover(); help(); QCOMPARE(target.helpEvents, 2);

        // Recovery must not disable suppression for the next ordinary stroke.
        tablet(QEvent::TabletPress, Qt::LeftButton);
        help(); QCOMPARE(target.helpEvents, 2);
        tablet(QEvent::TabletRelease, Qt::NoButton);
        help(); QCOMPARE(target.helpEvents, 3);
    }

    void toolPanelsAndCanvasShortcutsStaySynchronized()
    {
        QTemporaryDir temp;
        MainWindow window(temp.filePath("workspace.ini"));
        QVERIFY(!window.findChild<QActionGroup *>("toolbarDrawingTools"));
        QVERIFY(!window.findChild<QAction *>("toolbarBrushAction"));
        QString error;
        const auto projectPath = temp.filePath("drawing.toonproj");
        QVERIFY2(saveProject(Project::create(), projectPath, &error), qPrintable(error));
        QVERIFY(window.openProject(projectPath)); window.show(); window.activateWindow();
        QCoreApplication::processEvents();
        auto *toolbar = window.findChild<QToolBar *>("projectToolbar");
        auto *groups = window.findChild<QListWidget *>("toolboxEntries");
        auto *tools = window.findChild<QListWidget *>("toolEntries");
        auto *catalog = window.toolCatalog(); QVERIFY(toolbar); QVERIFY(groups); QVERIFY(tools); QVERIFY(catalog);
        const auto brush = catalog->activeToolId(); QCOMPARE(catalog->tool(brush)->engine, Canvas::Tool::Brush);
        QString eraser;
        for (const auto &tool : catalog->tools()) if (tool.engine == Canvas::Tool::Eraser) { eraser = tool.id; break; }
        QVERIFY(!eraser.isEmpty());
        const auto standalone = catalog->addStandalone(eraser); QVERIFY(!standalone.isEmpty());
        QCOMPARE(window.canvas()->tool(), Canvas::Tool::Eraser); QCOMPARE(tools->count(), 1);
        QCOMPARE(tools->currentItem()->data(Qt::UserRole).toString(), eraser);
        QCOMPARE(groups->currentItem()->data(Qt::UserRole).toString(), standalone);
        window.canvas()->setFocus(); QTRY_VERIFY(window.canvas()->hasFocus());
        QTest::keyClick(window.canvas(), Qt::Key_L);
        QCOMPARE(window.canvas()->tool(), Canvas::Tool::LassoSelect);
        QCOMPARE(catalog->tool(catalog->activeToolId())->engine, Canvas::Tool::LassoSelect);
        QCOMPARE(tools->currentItem()->data(Qt::UserRole).toString(), catalog->activeToolId());
        QCOMPARE(groups->currentItem()->data(Qt::UserRole).toString(), catalog->selectedEntryId());
        for (int i = 0; i < groups->count(); ++i) {
            if (groups->item(i)->data(Qt::UserRole).toString() != standalone) continue;
            groups->scrollToItem(groups->item(i));
            QTest::mouseClick(groups->viewport(), Qt::LeftButton, Qt::NoModifier, groups->visualItemRect(groups->item(i)).center());
            break;
        }
        QCOMPARE(window.canvas()->tool(), Canvas::Tool::Eraser); QCOMPARE(tools->count(), 1);
        window.canvas()->setFocus(); QTRY_VERIFY(window.canvas()->hasFocus());
        QTest::keyClick(window.canvas(), Qt::Key_B);
        QCOMPARE(window.canvas()->tool(), Canvas::Tool::Brush);
        QCOMPARE(catalog->tool(catalog->activeToolId())->engine, Canvas::Tool::Brush);
    }

    void globalUndoRoutesByToolPanelOrCanvasFocus()
    {
        QTemporaryDir temp; MainWindow window(temp.filePath("workspace.ini"));
        QString error; const auto path = temp.filePath("drawing.toonproj");
        QVERIFY(saveProject(Project::create(), path, &error)); QVERIFY(window.openProject(path));
        window.show(); window.activateWindow(); QCoreApplication::processEvents();
        auto *canvas = window.canvas(); auto *document = canvas->document(); auto *catalog = window.toolCatalog();
        QVERIFY(document->beginStroke({300, 300}, 1, canvas->brush, false)); document->endStroke();
        QCOMPARE(document->history()->index(), 1);
        const auto group = catalog->addGroup("Undo this group", "folder-open"); QVERIFY(!group.isEmpty());
        auto *list = window.findChild<QListWidget *>("toolboxEntries"); list->setFocus(); QTRY_VERIFY(list->hasFocus());
        auto *undo = window.findChild<QAction *>("undoAction"); auto *redo = window.findChild<QAction *>("redoAction");
        QVERIFY(undo && redo); QVERIFY(undo->isEnabled()); undo->trigger();
        QVERIFY(!catalog->entry(group)); QCOMPARE(document->history()->index(), 1); QVERIFY(redo->isEnabled());
        redo->trigger(); QVERIFY(catalog->entry(group)); QCOMPARE(document->history()->index(), 1);
        const int organizationIndex = catalog->history()->index();
        canvas->setFocus(); QTRY_VERIFY(canvas->hasFocus()); QVERIFY(undo->isEnabled()); undo->trigger();
        QCOMPARE(document->history()->index(), 0); QCOMPARE(catalog->history()->index(), organizationIndex);
        QVERIFY(catalog->entry(group)); document->history()->setClean();
    }

    void dockTabsReceiveHelpWithoutReplacingExplicitTabDescriptions()
    {
        QTemporaryDir temp;
        MainWindow window(temp.filePath("workspace.ini"));
        window.findChild<QDockWidget *>("toolsPanel")->show();
        window.findChild<QDockWidget *>("propertiesPanel")->show();
        window.tabifyDockWidget(window.findChild<QDockWidget *>("toolsPanel"), window.findChild<QDockWidget *>("propertiesPanel"));
        window.show(); QCoreApplication::processEvents();
        bool foundDockTabs = false;
        for (auto *tabs : window.findChildren<QTabBar *>()) {
            if (tabs->objectName() == "brushSettingsCategory") continue;
            tabs->ensurePolished();
            QHelpEvent help(QEvent::ToolTip, QPoint(), tabs->mapToGlobal(QPoint()));
            QApplication::sendEvent(tabs, &help);
            for (int index = 0; index < tabs->count(); ++index) {
                if (tabs->tabText(index).isEmpty()) continue;
                foundDockTabs = true;
                QVERIFY(!tabs->tabToolTip(index).isEmpty());
                QVERIFY(!tabs->accessibleTabName(index).isEmpty());
            }
        }
        QVERIFY(foundDockTabs);
        QTabBar explicitHelp;
        explicitHelp.addTab(appIcon("brush"), "Brush");
        explicitHelp.setTabToolTip(0, "Custom brush help (B)");
        explicitHelp.setAccessibleTabName(0, "Custom accessible brush label");
        explicitHelp.ensurePolished();
        QCOMPARE(explicitHelp.tabToolTip(0), QString("Custom brush help (B)"));
        QCOMPARE(explicitHelp.accessibleTabName(0), QString("Custom accessible brush label"));
        QToolTip::hideText();
    }

    void activeBrushCategoryUsesWorkspaceSettings()
    {
        QTemporaryDir temp;
        const auto path = temp.filePath("workspace.ini");
        {
            MainWindow window(path);
            auto *tabs = window.findChild<QTabBar *>("brushSettingsCategory");
            QVERIFY(tabs); tabs->setCurrentIndex(3);
            window.show(); QVERIFY(window.close());
        }
        {
            MainWindow restored(path);
            QCOMPARE(restored.findChild<QTabBar *>("brushSettingsCategory")->currentIndex(), 3);
        }
        {
            QSettings settings(path, QSettings::IniFormat);
            settings.setValue("brush/category", 999); settings.sync();
        }
        MainWindow safeFallback(path);
        QCOMPARE(safeFallback.findChild<QTabBar *>("brushSettingsCategory")->currentIndex(), 0);
    }

    void dockLabelsStayShortWhenTheActiveToolHasALongName()
    {
        QTemporaryDir temp;
        MainWindow window(temp.filePath("workspace.ini"));
        window.canvas()->setProject(Project::create());
        auto *properties = window.findChild<QDockWidget *>("propertiesPanel");
        auto *colors = window.findChild<QDockWidget *>("colorsPanel");
        auto *frames = window.findChild<QDockWidget *>("framePropertiesPanel");
        // Properties is now a standalone left dock by default. Verify the same
        // short-label/help behavior in an explicitly customized tab group.
        window.tabifyDockWidget(properties, colors);
        properties->show(); colors->show(); properties->raise();
        window.show(); QTest::qWait(20);
        QCOMPARE(properties->windowTitle(), QString("Properties"));
        QCOMPARE(properties->toggleViewAction()->text(), QString("Tool Properties"));
        QCOMPARE(frames->windowTitle(), QString("Frame"));
        QCOMPARE(frames->toggleViewAction()->text(), QString("Frame Properties"));
        const auto name = QString("Ink for final character clean-up and detailed backgrounds");
        QVERIFY(window.toolCatalog()->renameTool(window.toolCatalog()->activeToolId(), name));
        QCOMPARE(properties->windowTitle(), QString("Properties"));
        QVERIFY(properties->toolTip().contains(name));
        bool found = false;
        for (auto *tabs : window.findChildren<QTabBar *>(QString(), Qt::FindDirectChildrenOnly)) {
            for (int i = 0; i < tabs->count(); ++i) {
                if (tabs->tabText(i) != "Properties") continue;
                found = true;
                QTRY_VERIFY(tabs->property("compactDockTabs").toBool());
                QCOMPARE(tabs->iconSize(), QSize(16,16));
                QVERIFY2(tabs->tabRect(i).height() <= 32,
                    qPrintable(QString("tab height %1, hint %2, font %3").arg(tabs->tabRect(i).height())
                        .arg(tabs->sizeHint().height()).arg(tabs->fontMetrics().height())));
                QVERIFY(tabs->tabToolTip(i).contains(name));
                QCOMPARE(tabs->accessibleTabName(i), QString("Tool Properties"));
                QCOMPARE(tabs->style()->styleHint(QStyle::SH_ToolTip_WakeUpDelay, nullptr, tabs), 600);
                for (int j = 0; j < tabs->count(); ++j) if (tabs->tabText(j) == "Colors") {
                    QTest::mouseClick(tabs, Qt::LeftButton, Qt::NoModifier, tabs->tabRect(j).center());
                    QTRY_VERIFY(colors->isVisible());
                    QCOMPARE(tabs->currentIndex(), j);
                }
            }
        }
        QVERIFY(found);
        // Undocking and re-docking retains Qt's normal navigation and metadata.
        colors->setFloating(true); colors->show(); QTest::qWait(10);
        colors->setFloating(false); window.tabifyDockWidget(properties, colors);
        properties->raise(); QTest::qWait(10);
        QCOMPARE(properties->windowTitle(), QString("Properties"));
        QCOMPARE(properties->toggleViewAction()->text(), QString("Tool Properties"));
    }

    void compactNavigatorRetainsPreciseViewEdits()
    {
        QTemporaryDir temp; MainWindow window(temp.filePath("workspace.ini"));
        window.canvas()->setProject(Project::create());
        auto *dock = window.findChild<QDockWidget *>("controlPanel");
        QCOMPARE(dock->windowTitle(), QString("Navigator"));
        dock->setFloating(true); dock->resize(240, 340); dock->show();
        window.show(); dock->activateWindow(); QTest::qWait(20);
        auto *zoom = dock->findChild<ValueSlider *>("navigatorZoom");
        auto *rotation = dock->findChild<ValueSlider *>("navigatorRotation");
        QVERIFY(qobject_cast<PropertyRow *>(zoom->parentWidget()));
        QVERIFY(qobject_cast<PropertyRow *>(rotation->parentWidget()));
        for (auto *control : {zoom, rotation}) {
            QVERIFY(control->findChild<QSlider *>()->width() >= 74);
            QCOMPARE(control->findChild<QDoubleSpinBox *>()->width(), 48);
        }
        auto *number = zoom->findChild<QDoubleSpinBox *>();
        number->setFocus(); number->selectAll(); QTest::keyClicks(number, "125");
        dock->resize(220, 340); QTest::qWait(10);
        QCOMPARE(number->findChild<QLineEdit *>()->text(), QString("125"));
        QTest::keyClick(number, Qt::Key_Return);
        QCOMPARE(window.canvas()->zoom(), 1.25);
        rotation->setValue(45); QCOMPARE(window.canvas()->rotation(), 45.);
        QTest::mouseClick(dock->findChild<QToolButton *>("navigatorResetRotation"), Qt::LeftButton);
        QCOMPARE(window.canvas()->rotation(), 0.);
        QCOMPARE(window.canvas()->document()->history()->count(), 0);
    }

    void iconsAreReusableAndRespectDisabledPalette()
    {
        auto icon = appIcon("brush");
        QCOMPARE(icon.cacheKey(), appIcon("brush").cacheKey());
        const auto normal = icon.pixmap(QSize(24, 24), QIcon::Normal).toImage();
        const auto disabled = icon.pixmap(QSize(24, 24), QIcon::Disabled).toImage();
        QVERIFY(!normal.isNull()); QVERIFY(normal != disabled);
        bool hasStroke = false;
        for (int y = 0; y < normal.height(); ++y) for (int x = 0; x < normal.width(); ++x) {
            if (normal.pixelColor(x, y).alpha() < 250) continue;
            hasStroke = true;
            const auto pixel = normal.pixelColor(x, y), expected = QColor(ui::text);
            // Premultiplied edge coverage can round an unpremultiplied channel
            // by one level; fully covered strokes still use the theme color.
            QVERIFY(qAbs(pixel.red() - expected.red()) <= 1);
            QVERIFY(qAbs(pixel.green() - expected.green()) <= 1);
            QVERIFY(qAbs(pixel.blue() - expected.blue()) <= 1);
        }
        QVERIFY(hasStroke);
    }

    void iconsKeepProportionsInNonSquareSlots_data()
    {
        QTest::addColumn<QSize>("slot");
        QTest::addColumn<qreal>("scale");
        QTest::addColumn<int>("mode");
        for (const QSize slot : {QSize(46, 28), QSize(28, 46)}) {
            for (qreal scale : {1.0, 2.0}) {
                for (const auto mode : {QIcon::Normal, QIcon::Disabled, QIcon::Selected}) {
                    const auto name = QString("%1x%2-%3x-mode%4")
                        .arg(slot.width()).arg(slot.height()).arg(scale).arg(mode).toLatin1();
                    QTest::newRow(name.constData()) << slot << scale << int(mode);
                }
            }
        }
    }

    void iconsKeepProportionsInNonSquareSlots()
    {
        QFETCH(QSize, slot);
        QFETCH(qreal, scale);
        QFETCH(int, mode);
        const auto icon = appIcon("circle");
        const auto pixmap = icon.pixmap(slot, scale, QIcon::Mode(mode));
        QCOMPARE(pixmap.devicePixelRatio(), scale);
        QCOMPARE(pixmap.size(), slot * scale);
        const auto image = pixmap.toImage();
        QRect strokeBounds;
        for (int y = 0; y < image.height(); ++y) for (int x = 0; x < image.width(); ++x) {
            if (image.pixelColor(x, y).alpha() > 32) strokeBounds |= QRect(x, y, 1, 1);
        }
        QVERIFY(!strokeBounds.isEmpty());
        QCOMPARE(strokeBounds.width(), strokeBounds.height());
        QCOMPARE(strokeBounds.center(), image.rect().center());

        // The centered glyph must retain the same geometry and stroke weight
        // as a normal square control, with only transparent padding added.
        const int side = qMin(slot.width(), slot.height());
        const auto square = icon.pixmap(QSize(side, side), scale, QIcon::Mode(mode)).toImage();
        const QRect glyphRect((image.width() - square.width()) / 2,
                              (image.height() - square.height()) / 2,
                              square.width(), square.height());
        QCOMPARE(image.copy(glyphRect), square);
    }
};

QTEST_MAIN(UiThemeTest)
#include "test_ui_theme.moc"
