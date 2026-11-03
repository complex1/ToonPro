#include "canvas.h"
#include "drawing_input.h"
#include "main_window.h"

#include <QAction>
#include <QDialog>
#include <QDir>
#include <QDockWidget>
#include <QPainter>
#include <QScreen>
#include <QSettings>
#include <QTemporaryDir>
#include <QTabBar>
#include <QToolBar>
#include <QToolButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QDoubleSpinBox>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include "brush_preset_panel.h"
#include "brush_settings_panel.h"
#include "value_slider.h"
#include <QWidgetAction>
#include <QtTest>

using namespace toon;

namespace {
Project fixture()
{
    auto project = Project::create();
    project.width = 640; project.height = 480; project.transparent = true;
    QImage tile(tileSize, tileSize, QImage::Format_ARGB32_Premultiplied);
    tile.fill(Qt::transparent);
    QPainter painter(&tile); painter.fillRect(QRect(60, 60, 80, 60), QColor("#ba655c")); painter.end();
    project.shots[0].layers[0].tiles[{0, 0}] = tile;
    return project;
}

bool prepare(MainWindow &window, const QTemporaryDir &directory)
{
    const auto path = directory.filePath("drawing.toonproj");
    QString error;
    if (!saveProject(fixture(), path, &error) || !window.openProject(path)) return false;
    const auto available = window.screen()->availableGeometry();
    window.resize(QSize(1280, 860).boundedTo(available.size() - QSize(40, 60)));
    window.move(available.topLeft() + QPoint(20, 40));
    window.show();
    if (!QTest::qWaitForWindowExposed(&window)) return false;
    window.raise(); window.activateWindow();
    if (!QTest::qWaitForWindowActive(&window)) return false;
    window.canvas()->setFocus();
    QCoreApplication::processEvents();
    return true;
}

QDockWidget *dock(MainWindow &window, const char *name)
{
    return window.findChild<QDockWidget *>(name);
}

void verifyDrawingControls(MainWindow &window, bool shown)
{
    if (window.compactDrawingWorkspace()) {
        auto *drawer = window.findChild<QWidget *>("drawingControlsDrawer"); QVERIFY(drawer);
        QCOMPARE(drawer->isHidden(), !shown);
        for (const auto *name : {"toolsPanel", "brushPresetPanel", "propertiesPanel"})
            QVERIFY(dock(window, name)->isHidden());
        return;
    }
    for (const auto *name : {"toolsPanel", "brushPresetPanel", "propertiesPanel"}) {
        auto *panel = dock(window, name); QVERIFY(panel);
        QCOMPARE(panel->isHidden(), !shown);
    }
}

void capture(MainWindow &window, const QString &name)
{
    const auto path = qEnvironmentVariable("TOON_DRAWING_WORKSPACE_PREVIEW");
    if (path.isEmpty()) return;
    QDir().mkpath(path);
    QVERIFY(window.grab().save(QDir(path).filePath(name + ".png")));
}
}

class DrawingWorkspaceTests final : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { configureDrawingInput(); applyTheme(); }

    void playbackPreviewPreferenceSurvivesRestartWithoutEditingArtwork()
    {
        QTemporaryDir directory; QVERIFY(directory.isValid());
        const auto path = directory.filePath("workspace.ini");
        {
            QSettings settings(path, QSettings::IniFormat);
            settings.setValue("view/playbackPreviewDivisor", 3); // Unsupported persisted value.
            settings.sync(); QCOMPARE(settings.status(), QSettings::NoError);
        }
        QJsonObject originalProject;
        QImage originalArtwork;
        {
            MainWindow window(path);
            QCOMPARE(window.canvas()->playbackPreviewDivisor(), 1);
            QVERIFY(!window.canvas()->hasProject());
            QVERIFY(prepare(window, directory));
            auto *canvas = window.canvas(); auto *history = canvas->document()->history();
            originalProject = canvas->project().toJson(); originalArtwork = renderMovieFrame(canvas->project());
            const int count = history->count(), index = history->index();
            QVERIFY(history->isClean());
            auto *quarter = window.findChild<QAction *>("timelinePreviewQuarter"); QVERIFY(quarter);
            quarter->trigger();
            QCOMPARE(canvas->playbackPreviewDivisor(), 4); QVERIFY(quarter->isChecked());
            QCOMPARE(canvas->project().toJson(), originalProject);
            QCOMPARE(renderMovieFrame(canvas->project()), originalArtwork);
            QCOMPARE(history->count(), count); QCOMPARE(history->index(), index); QVERIFY(history->isClean());
            QVERIFY(window.close());
        }
        {
            QSettings settings(path, QSettings::IniFormat);
            QCOMPARE(settings.value("view/playbackPreviewDivisor").toInt(), 4);
        }
        MainWindow reopened(path);
        QCOMPARE(reopened.canvas()->playbackPreviewDivisor(), 4);
        QVERIFY(!reopened.canvas()->hasProject());
        QVERIFY(reopened.openProject(directory.filePath("drawing.toonproj")));
        QCOMPARE(reopened.canvas()->project().toJson(), originalProject);
        QCOMPARE(renderMovieFrame(reopened.canvas()->project()), originalArtwork);
        QCOMPARE(reopened.canvas()->document()->history()->count(), 0);
        QVERIFY(reopened.canvas()->document()->history()->isClean());
        auto *quarter = reopened.findChild<QAction *>("timelinePreviewQuarter"); QVERIFY(quarter); QVERIFY(quarter->isChecked());
        QVERIFY(reopened.close());
    }

    void freshDrawingWorkspaceKeepsDailyControlsAdjacent()
    {
        QTemporaryDir directory; MainWindow window(directory.filePath("workspace.ini"));
        QVERIFY(prepare(window, directory));
        QVERIFY(!window.compactDrawingWorkspace());
        verifyDrawingControls(window, true);
        auto *tools = dock(window, "toolsPanel"), *presets = dock(window, "brushPresetPanel");
        auto *properties = dock(window, "propertiesPanel"), *layers = dock(window, "layersPanel");
        auto *colors = dock(window, "colorsPanel"), *references = dock(window, "referencesPanel");
        auto *navigator = dock(window, "controlPanel");
        QVERIFY(tools && presets && properties && layers && colors && references && navigator);
        for (auto *panel : {tools, presets, properties}) {
            QCOMPARE(window.dockWidgetArea(panel), Qt::LeftDockWidgetArea);
            QVERIFY(!panel->isFloating());
            QVERIFY(window.tabifiedDockWidgets(panel).isEmpty());
        }
        QVERIFY(tools->geometry().bottom() <= presets->geometry().top());
        QVERIFY(presets->geometry().bottom() <= properties->geometry().top());
        // Qt excludes explicitly hidden peers from this query.
        references->show(); navigator->show(); colors->show(); colors->raise(); layers->show();
        QCoreApplication::processEvents();
        for (auto *panel : {colors, references, navigator, layers})
            QCOMPARE(window.dockWidgetArea(panel), Qt::RightDockWidgetArea);
        QVERIFY(window.tabifiedDockWidgets(colors).contains(references));
        QVERIFY(window.tabifiedDockWidgets(colors).contains(navigator));
        QVERIFY(!window.tabifiedDockWidgets(colors).contains(layers));
        QVERIFY(colors->geometry().bottom() <= layers->geometry().top());
        QVERIFY(dock(window, "animationPanel")->isHidden());
        QVERIFY(window.findChild<QAction *>("drawingWorkspacePresetAction"));
        QVERIFY(window.findChild<QAction *>("compactDrawingWorkspaceAction"));
        capture(window, "drawing-workspace");
        QVERIFY(window.close());
    }

    void compactChoiceKeepsCanvasSpaceUntilExplicitlyOpened()
    {
        QTemporaryDir directory; MainWindow window(directory.filePath("workspace.ini"));
        QVERIFY(prepare(window, directory));
        const int fullCanvasWidth = window.canvas()->width();
        auto *compact = window.findChild<QAction *>("compactDrawingWorkspaceAction"); QVERIFY(compact);
        compact->trigger(); QCoreApplication::processEvents();
        QVERIFY(window.compactDrawingWorkspace()); verifyDrawingControls(window, false);
        QVERIFY(window.canvas()->width() > fullCanvasWidth);
        auto *controls = window.findChild<QToolButton *>("compactDrawingControls");
        auto *color = window.findChild<QToolButton *>("compactDrawingColor");
        auto *context = window.findChild<QWidgetAction *>("compactDrawingContext");
        QVERIFY(controls && color && context); QVERIFY(context->isVisible());
        QVERIFY(controls->isVisible()); QVERIFY(controls->isCheckable()); QVERIFY(!controls->isChecked());
        QVERIFY(!controls->accessibleName().isEmpty()); QVERIFY(!controls->toolTip().isEmpty());
        QVERIFY(!color->accessibleName().isEmpty()); QVERIFY(!color->toolTip().isEmpty());
        QVERIFY(controls->height() >= 28); QVERIFY(color->height() >= 28);
        // Window width and tool changes never decide to reopen the drawer.
        window.resize(900, 680); QCoreApplication::processEvents();
        QVERIFY(window.compactDrawingWorkspace()); verifyDrawingControls(window, false);
        window.resize(1440, 980); QCoreApplication::processEvents();
        QVERIFY(window.compactDrawingWorkspace()); verifyDrawingControls(window, false);
        window.canvas()->setTool(Canvas::Tool::Eraser); QCoreApplication::processEvents();
        verifyDrawingControls(window, false);
        window.canvas()->setTool(Canvas::Tool::Brush); QCoreApplication::processEvents();
        verifyDrawingControls(window, false);
        capture(window, "compact-drawing-closed");
        window.applyDrawingWorkspace(false); QCoreApplication::processEvents();
        QVERIFY(!window.compactDrawingWorkspace()); verifyDrawingControls(window, true);
        QVERIFY(window.close());
    }

    void togglingControlsPreservesViewArtworkAndToolSettings()
    {
        QTemporaryDir directory; MainWindow window(directory.filePath("workspace.ini"));
        QVERIFY(prepare(window, directory)); window.applyDrawingWorkspace(true);
        auto *canvas = window.canvas(); canvas->setZoom(.83); canvas->setRotation(17);
        canvas->setBrushSize(37.5); canvas->setBrushColor(QColor("#7652a3"));
        QTest::qWait(30); // Let the explicit workspace preset finish its deferred dock layout.
        const auto pixels = renderMovieFrame(canvas->project());
        const auto project = canvas->project().toJson(); const auto brush = canvas->brush;
        const auto history = canvas->document()->history()->count();
        const auto geometry = canvas->geometry(); const auto view = canvas->viewTransform();
        auto *button = window.findChild<QToolButton *>("compactDrawingControls"); QVERIFY(button);
        QTest::mouseClick(button, Qt::LeftButton); QCoreApplication::processEvents();
        verifyDrawingControls(window, true); QVERIFY(button->isChecked());
        QCOMPARE(canvas->zoom(), .83); QCOMPARE(canvas->rotation(), 17.);
        QCOMPARE(canvas->geometry(), geometry); QCOMPARE(canvas->viewTransform(), view);
        capture(window, "compact-drawing-open");
        window.toggleDrawingControls(); QCoreApplication::processEvents();
        verifyDrawingControls(window, false); QVERIFY(!button->isChecked());
        QCOMPARE(canvas->zoom(), .83); QCOMPARE(canvas->rotation(), 17.);
        QCOMPARE(canvas->geometry(), geometry); QCOMPARE(canvas->viewTransform(), view);
        QCOMPARE(canvas->brush, brush); QCOMPARE(canvas->project().toJson(), project);
        QCOMPARE(renderMovieFrame(canvas->project()), pixels);
        QCOMPARE(canvas->document()->history()->count(), history);
        QTRY_COMPARE(QApplication::focusWidget(), canvas);
        QVERIFY(window.close());
    }

    void controlsDoNotRefitTheCanvasOrHideUpperPanelTabs()
    {
        QTemporaryDir directory; MainWindow window(directory.filePath("workspace.ini"));
        QVERIFY(prepare(window, directory)); window.applyDrawingWorkspace(true);
        QCoreApplication::processEvents(); window.canvas()->fitSheet();
        const auto zoom = window.canvas()->zoom();
        window.toggleDrawingControls(); QCoreApplication::processEvents();
        QCOMPARE(window.canvas()->zoom(), zoom);
        window.toggleDrawingControls(); QCoreApplication::processEvents();
        QCOMPARE(window.canvas()->zoom(), zoom);
        auto *colors = dock(window, "colorsPanel");
        QCOMPARE(dock(window, "referencesPanel")->windowTitle(), QString("Refs"));
        QCOMPARE(dock(window, "referencesPanel")->accessibleName(), QString("References"));
        QVERIFY(window.tabifiedDockWidgets(colors).contains(dock(window, "controlPanel")));
        bool foundTabs = false;
        for (auto *tabs : window.findChildren<QTabBar *>(QString(), Qt::FindDirectChildrenOnly)) {
            if (!tabs->isVisible() || tabs->count() != 3) continue;
            QStringList names;
            for (int i = 0; i < tabs->count(); ++i) names.append(tabs->accessibleTabName(i));
            if (!names.contains("Colors") || !names.contains("References") || !names.contains("Navigator")) continue;
            foundTabs = true;
            QTRY_VERIFY(!tabs->usesScrollButtons());
            for (int i = 0; i < tabs->count(); ++i) {
                QVERIFY(!tabs->tabText(i).isEmpty()); QVERIFY(!tabs->tabIcon(i).isNull());
                QVERIFY(!tabs->tabToolTip(i).isEmpty());
                QVERIFY(tabs->rect().contains(tabs->tabRect(i)));
                QVERIFY(tabs->tabRect(i).width() >= 28);
            }
        }
        QVERIFY(foundTabs);
        QVERIFY(window.close());
    }

    void overlayReusesPanelsAndPreservesDraftsAndCanvasGeometry()
    {
        QTemporaryDir directory; MainWindow window(directory.filePath("workspace.ini"));
        QVERIFY(prepare(window, directory));
        auto *toolsContent = dock(window, "toolsPanel")->widget();
        auto *sizesContent = dock(window, "brushPresetPanel")->widget();
        auto *propertiesContent = dock(window, "propertiesPanel")->widget();
        window.applyDrawingWorkspace(true); QCoreApplication::processEvents();
        auto *canvas = window.canvas(); canvas->fitSheet();
        const auto geometry = canvas->geometry(); const auto view = canvas->viewTransform();
        const auto history = canvas->document()->history()->count();
        window.toggleDrawingControls(); QCoreApplication::processEvents();
        auto *drawer = window.findChild<QWidget *>("drawingControlsDrawer"); QVERIFY(drawer);
        auto *tabs = window.findChild<QTabBar *>("drawingDrawerTabs"); QVERIFY(tabs);
        auto *close = window.findChild<QToolButton *>("closeDrawingDrawer"); QVERIFY(close);
        QVERIFY(drawer->isAncestorOf(toolsContent)); QVERIFY(drawer->isAncestorOf(sizesContent));
        QVERIFY(drawer->isAncestorOf(propertiesContent)); QVERIFY(!canvas->isAncestorOf(drawer));
        QCOMPARE(canvas->geometry(), geometry); QCOMPARE(canvas->viewTransform(), view);
        tabs->setCurrentIndex(1);
        auto *presets = qobject_cast<BrushPresetPanel *>(sizesContent); QVERIFY(presets);
        QVERIFY(presets->activateSize(64)); QCOMPARE(canvas->brush.size, 64.);
        tabs->setCurrentIndex(2); QCoreApplication::processEvents();
        auto *size = propertiesContent->findChild<ValueSlider *>("quickBrushSize"); QVERIFY(size);
        auto *number = size->findChild<QDoubleSpinBox *>(); auto *editor = number->findChild<QLineEdit *>();
        window.activateWindow(); QVERIFY(QTest::qWaitForWindowActive(&window)); number->setFocus(); QTRY_VERIFY(number->hasFocus());
        number->selectAll(); QTest::keyClicks(number, "77.5");
        QTest::mouseClick(close, Qt::LeftButton);
        QVERIFY(!drawer->isHidden()); QCOMPARE(editor->text(), QString("77.5")); QVERIFY(number->hasFocus());
        QCOMPARE(canvas->brush.size, 64.); QCOMPARE(canvas->geometry(), geometry); QCOMPARE(canvas->viewTransform(), view);
        QTest::keyClick(number, Qt::Key_Escape); QCOMPARE(canvas->brush.size, 64.);
        QTest::mouseClick(close, Qt::LeftButton); QVERIFY(drawer->isHidden());
        QCOMPARE(canvas->geometry(), geometry); QCOMPARE(canvas->viewTransform(), view);
        window.toggleDrawingControls(); QCOMPARE(tabs->currentIndex(), 2);
        QCOMPARE(size->value(), 64.); QCOMPARE(canvas->document()->history()->count(), history);
        window.applyDrawingWorkspace(false); QCoreApplication::processEvents();
        QCOMPARE(dock(window, "toolsPanel")->widget(), toolsContent);
        QCOMPARE(dock(window, "brushPresetPanel")->widget(), sizesContent);
        QCOMPARE(dock(window, "propertiesPanel")->widget(), propertiesContent);
        verifyDrawingControls(window, true); QVERIFY(drawer->isHidden());
        QVERIFY(window.close());
    }

    void overlayFitsNarrowWindowsLargerTextAndDoesNotDismissPopups()
    {
        QTemporaryDir directory; MainWindow window(directory.filePath("workspace.ini"));
        QVERIFY(prepare(window, directory)); window.applyDrawingWorkspace(true);
        window.resize(900, 620); QCoreApplication::processEvents(); window.toggleDrawingControls();
        auto *drawer = window.findChild<QWidget *>("drawingControlsDrawer"); QVERIFY(drawer);
        auto *tabs = window.findChild<QTabBar *>("drawingDrawerTabs"); QVERIFY(tabs);
        auto *close = window.findChild<QToolButton *>("closeDrawingDrawer"); QVERIFY(close);
        auto font = window.font(); font.setPixelSize(24); window.setFont(font);
        for (const auto extent : {QSize(900, 620), QSize(760, 520), QSize(1100, 780)}) {
            window.resize(extent); QTest::qWait(20);
            const QRect canvasRect(window.canvas()->mapTo(&window, QPoint()), window.canvas()->size());
            QVERIFY(canvasRect.contains(drawer->geometry()));
            QVERIFY(drawer->rect().contains(QRect(close->mapTo(drawer, QPoint()), close->size())));
            for (int index = 0; index < tabs->count(); ++index) {
                const auto tabRect = tabs->tabRect(index);
                QVERIFY2(tabs->rect().contains(tabRect), qPrintable(QString("window %1x%2; drawer %3x%4; bar %5x%6; tab %7 at %8,%9 %10x%11; font %12px")
                    .arg(window.width()).arg(window.height()).arg(drawer->width()).arg(drawer->height())
                    .arg(tabs->width()).arg(tabs->height()).arg(index).arg(tabRect.x()).arg(tabRect.y())
                    .arg(tabRect.width()).arg(tabRect.height()).arg(tabs->fontMetrics().height())));
                QVERIFY(tabRect.width() >= 28);
                QVERIFY(!tabs->accessibleTabName(index).isEmpty()); QVERIFY(!tabs->tabToolTip(index).isEmpty());
                tabs->setCurrentIndex(index); QCoreApplication::processEvents();
                for (auto *scroll : drawer->findChildren<QScrollArea *>())
                    if (scroll->isVisibleTo(drawer)) QCOMPARE(scroll->horizontalScrollBar()->maximum(), 0);
            }
            const auto geometry = window.canvas()->geometry(); const auto view = window.canvas()->viewTransform();
            window.toggleDrawingControls(); window.toggleDrawingControls(); QCoreApplication::processEvents();
            QCOMPARE(window.canvas()->geometry(), geometry); QCOMPARE(window.canvas()->viewTransform(), view);
        }
        QMenu popup(drawer); popup.addAction("Example"); popup.popup(drawer->mapToGlobal(QPoint(20, 50)));
        QTRY_VERIFY(QApplication::activePopupWidget()); window.toggleDrawingControls();
        QVERIFY(!drawer->isHidden()); QVERIFY(popup.isVisible()); popup.close();
        QDialog dialog(drawer); dialog.setWindowModality(Qt::WindowModal); dialog.open();
        QTRY_VERIFY(QApplication::activeModalWidget()); window.toggleDrawingControls(); QVERIFY(!drawer->isHidden()); dialog.reject();
        QTest::qWait(10); tabs->setFocus(); QTest::keyClick(tabs, Qt::Key_Escape);
        QVERIFY(drawer->isHidden()); QVERIFY(window.close());
    }

    void compactStateRestoresAfterFullscreenAndReopen()
    {
        QTemporaryDir directory; const auto path = directory.filePath("workspace.ini");
        {
            MainWindow window(path); QVERIFY(prepare(window, directory));
            window.applyDrawingWorkspace(true); QCoreApplication::processEvents();
            window.setDrawingMode(true); QVERIFY(window.drawingMode());
            auto *context = window.findChild<QWidgetAction *>("compactDrawingContext"); QVERIFY(context);
            QVERIFY(!context->isVisible());
            window.setDrawingMode(false); QVERIFY(!window.drawingMode());
            QVERIFY(window.compactDrawingWorkspace()); verifyDrawingControls(window, false);
            QVERIFY(context->isVisible());
            window.toggleDrawingControls(); verifyDrawingControls(window, true);
            window.findChild<QTabBar *>("drawingDrawerTabs")->setCurrentIndex(1);
            window.setDrawingMode(true); QVERIFY(window.drawingMode());
            // Closing fullscreen saves the normal compact layout and open drawer page.
            QVERIFY(window.close());
        }
        QSettings settings(path, QSettings::IniFormat);
        QVERIFY(settings.value("workspace/compactDrawing").toBool());
        QVERIFY(!settings.value("layoutV3").toByteArray().isEmpty());
        MainWindow reopened(path);
        QVERIFY(reopened.openProject(directory.filePath("drawing.toonproj"))); reopened.show();
        QCoreApplication::processEvents();
        QVERIFY(reopened.compactDrawingWorkspace()); QVERIFY(!reopened.drawingMode());
        verifyDrawingControls(reopened, true);
        QCOMPARE(reopened.findChild<QTabBar *>("drawingDrawerTabs")->currentIndex(), 1);
        reopened.toggleDrawingControls(); verifyDrawingControls(reopened, false);
        QVERIFY(reopened.close());
    }

    void inlineRenameAndAdvancedBrushDialogKeepTheirEscapeBehavior()
    {
        QTemporaryDir directory; MainWindow window(directory.filePath("workspace.ini"));
        QVERIFY(prepare(window, directory)); window.applyDrawingWorkspace(true);
        window.toggleDrawingControls();
        auto *drawer = window.findChild<QWidget *>("drawingControlsDrawer"); QVERIFY(drawer);
        auto *tabs = window.findChild<QTabBar *>("drawingDrawerTabs"); QVERIFY(tabs);
        auto *list = drawer->findChild<QListWidget *>("toolEntries"); QVERIFY(list); QVERIFY(list->count() > 0);
        list->setCurrentRow(0); list->setFocus();
        const auto originalName = list->item(0)->text();
        list->editItem(list->currentItem());
        auto *rename = list->findChild<QLineEdit *>(); QVERIFY(rename);
        rename->selectAll(); QTest::keyClicks(rename, "Cancelled brush name");
        QTest::keyClick(rename, Qt::Key_Escape); QCoreApplication::processEvents();
        QVERIFY(!drawer->isHidden()); QCOMPARE(list->item(0)->text(), originalName);
        tabs->setCurrentIndex(2); QCoreApplication::processEvents();
        auto *advanced = drawer->findChild<QToolButton *>("advancedBrushSettings"); QVERIFY(advanced);
        QTest::mouseClick(advanced, Qt::LeftButton);
        auto *dialog = drawer->findChild<QDialog *>("advancedBrushDialog"); QVERIFY(dialog);
        QTRY_VERIFY(dialog->isVisible()); QCOMPARE(QApplication::activeModalWidget(), dialog);
        QTest::keyClick(dialog, Qt::Key_Escape); QTRY_VERIFY(!dialog->isVisible());
        QVERIFY(!drawer->isHidden());
        tabs->setFocus(); QTest::keyClick(tabs, Qt::Key_Escape); QVERIFY(drawer->isHidden());
        QVERIFY(window.close());
    }

    void legacyCompactDocksMigrateAndPanelsMenuOpensOverlay()
    {
        QTemporaryDir directory; const auto path = directory.filePath("workspace.ini");
        {
            MainWindow legacy(path); QVERIFY(prepare(legacy, directory));
            QVERIFY(legacy.close());
        }
        {
            QSettings settings(path, QSettings::IniFormat);
            settings.setValue("workspace/compactDrawing", true);
            settings.remove("workspace/drawingDrawerOpen"); settings.remove("workspace/drawingDrawerPage");
        }
        MainWindow window(path); QVERIFY(window.openProject(directory.filePath("drawing.toonproj")));
        window.show(); QCoreApplication::processEvents();
        QVERIFY(window.compactDrawingWorkspace()); verifyDrawingControls(window, true);
        window.toggleDrawingControls(); verifyDrawingControls(window, false);
        auto *canvas = window.canvas(); QTest::qWait(30); canvas->fitSheet();
        const auto geometry = canvas->geometry(); const auto view = canvas->viewTransform();
        auto *showProperties = window.findChild<QAction *>("drawingPanelAction_propertiesPanel"); QVERIFY(showProperties);
        bool inPanelsMenu = false;
        for (auto *menu : window.findChildren<QMenu *>())
            if (menu->actions().contains(showProperties)) inPanelsMenu = true;
        QVERIFY(inPanelsMenu); showProperties->trigger();
        QTRY_VERIFY(!window.findChild<QWidget *>("drawingControlsDrawer")->isHidden());
        QCOMPARE(window.findChild<QTabBar *>("drawingDrawerTabs")->currentIndex(), 2);
        verifyDrawingControls(window, true);
        QCOMPARE(canvas->geometry(), geometry); QCOMPARE(canvas->viewTransform(), view);
        QVERIFY(window.close());
    }

    void activeEditsRejectLayoutChangesWithoutResolvingArtwork_data()
    {
        QTest::addColumn<int>("kind");
        QTest::newRow("brush-contact") << 0;
        QTest::newRow("pending-transform") << 1;
        QTest::newRow("pending-stamp") << 2;
    }

    void activeEditsRejectLayoutChangesWithoutResolvingArtwork()
    {
        QFETCH(int, kind);
        QTemporaryDir directory; MainWindow window(directory.filePath("workspace.ini"));
        QVERIFY(prepare(window, directory)); window.applyDrawingWorkspace(true);
        auto *document = window.canvas()->document();
        if (kind == 0) {
            QVERIFY(document->beginStroke({220, 160}, 1., window.canvas()->brush, false));
        } else if (kind == 1) {
            QVERIFY(document->beginTransform());
            TransformParameters parameters; parameters.translation = {35, 18};
            QVERIFY(document->previewTransform(parameters));
        } else {
            auto brush = window.canvas()->brush;
            brush.tipMask = QImage(16, 20, QImage::Format_Grayscale8); brush.tipMask.fill(220);
            brush.size = 40;
            QVERIFY(document->beginStampPlacement({240, 160}, BrushInput{}, brush));
            QVERIFY(document->stampPlacementActive());
        }
        const auto history = document->history()->count();
        const auto matrix = document->transformMatrix();
        window.toggleDrawingControls();
        window.applyDrawingWorkspace(false);
        window.applyDrawingWorkspace(true);
        QVERIFY(window.compactDrawingWorkspace()); verifyDrawingControls(window, false);
        QCOMPARE(document->history()->count(), history);
        if (kind == 0) { QVERIFY(document->strokeActive()); document->endStroke(); }
        else {
            QVERIFY(document->transformActive()); QCOMPARE(document->transformMatrix(), matrix);
            if (kind == 2) QVERIFY(document->stampPlacementActive());
            document->cancelTransform();
        }
        document->history()->setClean();
        window.toggleDrawingControls(); verifyDrawingControls(window, true);
        QVERIFY(window.close());
    }

    void existingCustomLayoutIsNotReplacedOnLaunch()
    {
        QTemporaryDir directory; const auto path = directory.filePath("workspace.ini");
        {
            MainWindow window(path); QVERIFY(prepare(window, directory));
            auto *tools = dock(window, "toolsPanel");
            auto *properties = dock(window, "propertiesPanel");
            window.addDockWidget(Qt::RightDockWidgetArea, properties);
            window.tabifyDockWidget(dock(window, "layersPanel"), properties);
            properties->show(); dock(window, "layersPanel")->show(); properties->raise();
            tools->setFloating(true); tools->resize(280, 420); tools->show();
            dock(window, "colorsPanel")->hide();
            QCoreApplication::processEvents(); QVERIFY(window.close());
        }
        MainWindow reopened(path);
        QVERIFY(reopened.openProject(directory.filePath("drawing.toonproj"))); reopened.show();
        QCoreApplication::processEvents();
        QVERIFY(!reopened.compactDrawingWorkspace());
        QVERIFY(dock(reopened, "toolsPanel")->isFloating());
        QVERIFY(!dock(reopened, "toolsPanel")->isHidden());
        QVERIFY(dock(reopened, "colorsPanel")->isHidden());
        QCOMPARE(reopened.dockWidgetArea(dock(reopened, "propertiesPanel")), Qt::RightDockWidgetArea);
        QVERIFY(reopened.tabifiedDockWidgets(dock(reopened, "layersPanel")).contains(dock(reopened, "propertiesPanel")));
        QVERIFY(reopened.close());
    }

    void staleCompactPreferenceCannotOverrideInvalidLayout()
    {
        QTemporaryDir directory; const auto path = directory.filePath("workspace.ini");
        { QSettings settings(path, QSettings::IniFormat);
          settings.setValue("workspace/compactDrawing", true);
          settings.setValue("layoutV3", QByteArray("invalid state")); settings.sync(); }
        MainWindow window(path); QVERIFY(prepare(window, directory));
        QVERIFY(!window.compactDrawingWorkspace()); verifyDrawingControls(window, true);
        QVERIFY(window.close());
    }
};

QTEST_MAIN(DrawingWorkspaceTests)
#include "test_drawing_workspace.moc"
