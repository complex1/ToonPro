#include "feature_flags.h"
#include "canvas.h"
#include "main_window.h"
#include "ui_metrics.h"
#include "project_dialog.h"
#include "value_slider.h"
#include "tool_catalog.h"
#include "tool_navigation_panel.h"
#include "tool_management_panel.h"
#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QPushButton>
#include <QScrollArea>
#include <QScreen>
#include <QSettings>
#include <QSignalSpy>
#include <QSlider>
#include <QTemporaryDir>
#include <QToolBar>
#include <QtTest>

using namespace toon;

class FoundationTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { applyTheme(); }
    void projectRoundTrip() {
        auto p = Project::create();
        p.movie = QString::fromUtf8("星の映画 / The night train");
        p.fps = 23.976;
        p.transparent = true;
        p.shots.first().sheet.left = 71;
        p.shots.first().sheet.safeArea = true;
        auto second = Project::create().shots.first();
        second.number = "C002";
        p.shots.append(second);
        QTemporaryDir temp;
        QString error;
        QVERIFY2(saveProject(p, temp.filePath("movie.toonproj"), &error), qPrintable(error));
        auto loaded = loadProject(temp.filePath("movie.toonproj"), &error);
        QVERIFY2(loaded.has_value(), qPrintable(error));
        QCOMPARE(loaded->toJson(), p.toJson());
    }
    void rejectMalformedProjects() {
        const auto original = Project::create().toJson();
        QString error;
        auto o = original;
        o["version"] = 99;
        QVERIFY(!Project::fromJson(o, &error));
        QVERIFY(error.contains("version"));
        o = original;
        auto output = o["output"].toObject();
        output["width"] = 1920.5;
        o["output"] = output;
        QVERIFY(!Project::fromJson(o, &error));
        o = original;
        auto shots = o["shots"].toArray();
        shots.append(shots.first());
        o["shots"] = shots;
        QVERIFY(!Project::fromJson(o, &error));
        o = original;
        o["shots"] = QJsonArray();
        QVERIFY(!Project::fromJson(o, &error));
        auto p = Project::create();
        p.shots.first().sheet.left = -1;
        QVERIFY(!p.validate().isEmpty());
        p = Project::create();
        p.width = 8192; p.height = 8192;
        QVERIFY(p.validate().contains("megapixel"));
    }
    void invalidSavePreservesExistingFile() {
        QTemporaryDir temp;
        const auto path = temp.filePath("movie.toonproj");
        auto p = Project::create();
        QString error;
        QVERIFY(saveProject(p, path, &error));
        p.movie.clear();
        QVERIFY(!saveProject(p, path, &error));
        auto recovered = loadProject(path, &error);
        QVERIFY(recovered);
        QCOMPARE(recovered->movie, QString("Untitled movie"));
        QVERIFY(!saveProject(*recovered, temp.filePath("absent/movie.toonproj"), &error));
    }
    void corruptAndOversizedFiles() {
        QTemporaryDir temp;
        const auto path = temp.filePath("bad.toonproj");
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("{bad json"); file.close();
        QString error;
        QVERIFY(!loadProject(path, &error));
        QVERIFY(file.open(QIODevice::WriteOnly));
        QVERIFY(file.resize(4 * 1024 * 1024 + 1)); file.close();
        QVERIFY(!loadProject(path, &error));
        QVERIFY(error.contains("4 MB"));
    }
    void sheetGeometryAndExports() {
        auto p = Project::create();
        auto g = sheetGeometry(p, p.shots.first());
        QCOMPARE(g.output.size(), QSizeF(1920, 1080));
        QVERIFY(g.drawing.contains(g.output));
        QVERIFY(g.paper.contains(g.drawing));
        QVERIFY(g.header.bottom() <= g.drawing.top());
        const auto before = g.paper.size();
        p.shots.first().sheet.left += 50;
        g = sheetGeometry(p, p.shots.first());
        QCOMPARE(g.paper.width(), before.width() + 50);
        QCOMPARE(g.output.size(), QSizeF(1920, 1080));
        const auto sheet = renderSheet(p, p.shots.first());
        const auto frame = renderMovieFrame(p);
        QCOMPARE(frame.size(), QSize(1920, 1080));
        QVERIFY(sheet.width() > frame.width());
        QCOMPARE(frame.pixelColor(0, 0), QColor(Qt::white));
        QCOMPARE(frame.pixelColor(frame.width() - 1, frame.height() - 1), QColor(Qt::white));
        p.transparent = true;
        const auto transparent = renderMovieFrame(p);
        QCOMPARE(transparent.pixelColor(0, 0).alpha(), 0);
        QCOMPARE(transparent.pixelColor(960, 540).alpha(), 0);
        QTemporaryDir temp;
        QString error;
        QVERIFY(savePng(sheet, temp.filePath("sheet.png"), &error));
        QCOMPARE(QImage(temp.filePath("sheet.png")).size(), sheet.size());
        QVERIFY(savePng(transparent, temp.filePath("frame.png"), &error));
        QCOMPARE(QImage(temp.filePath("frame.png")).pixelColor(0, 0).alpha(), 0);
    }
    void navigationNeverChangesDocument() {
        auto p = Project::create();
        const auto expected = p.toJson();
        Canvas canvas;
        canvas.resize(900, 700);
        canvas.setProject(p);
        const auto originalSheet = renderSheet(canvas.project(), canvas.shot());
        canvas.setZoom(1.75);
        canvas.setRotation(37);
        canvas.centerOn({840, 530});
        const QPointF point(203, 432);
        QVERIFY(QLineF(point, canvas.viewTransform().inverted().map(canvas.viewTransform().map(point))).length() < .001);
        QCOMPARE(canvas.project().toJson(), expected);
        QCOMPARE(renderSheet(canvas.project(), canvas.shot()), originalSheet);
        canvas.fitSheet();
        const auto paperOnScreen = canvas.viewTransform().mapRect(canvas.paperRect());
        QVERIFY(QRectF(canvas.rect()).contains(paperOnScreen));
    }
    void navigatorPansRotatedCanvas() {
        Canvas canvas;
        canvas.resize(900, 600);
        canvas.setProject(Project::create());
        canvas.setZoom(1.5);
        canvas.setRotation(45);
        Navigator navigator(&canvas);
        navigator.resize(300, 240);
        navigator.show();
        const auto centerBefore = canvas.center();
        const auto start = navigator.miniatureTransform().map(centerBefore).toPoint();
        const auto end = start + QPoint(22, 10);
        const auto expectedDelta = navigator.miniatureTransform().inverted().map(QPointF(end))
            - navigator.miniatureTransform().inverted().map(QPointF(start));
        QTest::mousePress(&navigator, Qt::LeftButton, Qt::NoModifier, start);
        QTest::mouseMove(&navigator, end);
        QTest::mouseRelease(&navigator, Qt::LeftButton, Qt::NoModifier, end);
        QVERIFY(QLineF(canvas.center(), centerBefore + expectedDelta).length() < .01);
        QCOMPARE(canvas.rotation(), 45.0);
    }
    void projectDialogValidationAndPreview() {
        ProjectDialog dialog(Project::create(), true);
        auto *title = dialog.findChild<QLineEdit *>("movieTitle");
        auto *submit = dialog.findChild<QPushButton *>("submitProject");
        QVERIFY(title && submit);
        title->setText("   ");
        QVERIFY(!submit->isEnabled());
        title->setText("The last summer");
        QVERIFY(submit->isEnabled());
        dialog.findChild<ValueSlider *>("frameWidth")->setValue(1280);
        QCOMPARE(dialog.resultProject().width, 1280);
        QCOMPARE(dialog.findChild<Canvas *>()->project().width, 1280);
        QCOMPARE(dialog.resultProject().shots.size(), 1);
    }
    void projectNumbersPairSliderAndExactEntry() {
        ProjectDialog dialog(Project::create(), true);
        QTemporaryDir temp;
        dialog.findChild<QLineEdit *>("projectPath")->setText(temp.filePath("numeric-entry.toonproj"));
        QSignalSpy accepted(&dialog, &QDialog::accepted);
        dialog.show();
        QTest::qWait(20);
        for (const auto *id : {"frameWidth", "frameHeight", "frameRate", "durationFrames", "overscan",
                               "leftMargin", "rightMargin", "topMargin", "bottomMargin", "headerHeight"}) {
            auto *control = dialog.findChild<ValueSlider *>(id); QVERIFY2(control, id);
            QVERIFY(control->findChild<QSlider *>("valueSliderTrack"));
            auto *field = control->findChild<QDoubleSpinBox *>("valueSliderNumber"); QVERIFY(field);
            QVERIFY(field->width() <= 80);
        }
        auto *width = dialog.findChild<ValueSlider *>("frameWidth");
        auto *height = dialog.findChild<ValueSlider *>("frameHeight");
        auto *track = width->findChild<QSlider *>("valueSliderTrack");
        dialog.findChild<QCheckBox *>("projectAspectLock")->setChecked(true);
        track->setSliderDown(true); track->setValue(1280);
        QCOMPARE(dialog.resultProject().width, 1280);
        QCOMPARE(dialog.resultProject().height, 720);
        QCOMPARE(dialog.findChild<Canvas *>()->project().width, 1280);
        track->setSliderDown(false);
        auto *fps = dialog.findChild<ValueSlider *>("frameRate");
        auto *field = fps->findChild<QDoubleSpinBox *>("valueSliderNumber");
        dialog.findChild<QScrollArea *>()->ensureWidgetVisible(field);
        field->setFocus(); QTRY_VERIFY(field->hasFocus());
        field->selectAll(); QTest::keyClicks(field, "23.976");
        QTest::keyClick(field, Qt::Key_Return);
        QCOMPARE(dialog.resultProject().fps, 23.976);
        QCOMPARE(fps->findChild<QSlider *>("valueSliderTrack")->value(), 23976);
        QCOMPARE(accepted.count(), 0); QVERIFY(dialog.isVisible());
        QTest::keyClick(field, Qt::Key_Enter); // An unchanged value must not trigger Create either.
        QCOMPARE(accepted.count(), 0); QVERIFY(dialog.isVisible());
        field->selectAll(); QTest::keyClicks(field, "29.97");
        QTest::keyClick(field, Qt::Key_Tab);
        QCOMPARE(dialog.resultProject().fps, 29.97);
        QCOMPARE(fps->findChild<QSlider *>("valueSliderTrack")->value(), 29970);
        QCOMPARE(accepted.count(), 0); QVERIFY(dialog.isVisible());
        auto *preset = dialog.findChild<QComboBox *>("framePreset");
        preset->setCurrentIndex(4); QVERIFY(QMetaObject::invokeMethod(preset, "activated", Q_ARG(int, 4)));
        QCOMPARE(width->value(), 1080.0); QCOMPARE(height->value(), 1920.0);
        width->setValue(540);
        QCOMPARE(dialog.resultProject().height, 960);
        dialog.findChild<QCheckBox *>("projectAspectLock")->setChecked(false);
        QCOMPARE(width->findChild<QSlider *>()->maximum(), 4096);
        QCOMPARE(width->findChild<QDoubleSpinBox *>()->maximum(), 8192.0);
        width->setValue(8192); height->setValue(8192);
        QVERIFY(!dialog.findChild<QPushButton *>("submitProject")->isEnabled());
    }
    void workspaceRoundTripAndVisualArtifacts() {
        QTemporaryDir temp;
        QString error;
        auto p = Project::create();
        p.movie = "The last summer";
        p.shots.first().name = "Station at dusk";
        const auto projectPath = temp.filePath("movie.toonproj");
        QVERIFY(saveProject(p, projectPath, &error));
        const auto settingsPath = temp.filePath("workspace.ini");
        QDir().mkpath("test-artifacts");
        {
            MainWindow window(settingsPath);
            window.show();
            QTest::qWait(80);
            QVERIFY(window.grab().save("test-artifacts/welcome.png"));
            QVERIFY(window.openProject(projectPath));
            QVERIFY(window.height() <= 900);
            const auto panels = window.findChildren<QDockWidget *>();
            QStringList actualPanels;
            for (const auto *panel : panels) actualPanels.append(panel->objectName());
            QStringList expectedPanels{
                "controlPanel", "toolsPanel", "brushPresetPanel", "propertiesPanel", "layersPanel",
                "colorsPanel", "referencesPanel", "effectsPanel", "celPaintCheckPanel", "celShadingPanel",
                "animationPanel", "framePropertiesPanel", "inbetweenPanel", "symbolsPanel", "historyPanel", "autoActionsPanel",
                "modelLibraryPanel", "shotsPanel", "toolManagementPanel", "brushManagementPanel"};
            if (!features::legacy3DEditor) expectedPanels.removeAll("modelLibraryPanel");
            actualPanels.sort(); expectedPanels.sort();
            QCOMPARE(actualPanels, expectedPanels);
            QVERIFY(window.findChild<QDockWidget *>("shotsPanel")->isHidden());
            if (features::legacy3DEditor) QVERIFY(window.findChild<QDockWidget *>("modelLibraryPanel")->isHidden());
            else QVERIFY(!window.findChild<QDockWidget *>("modelLibraryPanel"));
            QVERIFY(window.findChild<QDockWidget *>("toolManagementPanel")->isHidden());
            QVERIFY(window.findChild<QDockWidget *>("brushManagementPanel")->isHidden());
            QVERIFY(window.findChild<QDockWidget *>("historyPanel")->isHidden());
            QVERIFY(window.findChild<QDockWidget *>("autoActionsPanel")->isHidden());
            for (const auto *id : {"toolsPanel", "brushPresetPanel", "propertiesPanel", "layersPanel"})
                QVERIFY(!window.findChild<QDockWidget *>(id)->isHidden());
            auto *column = window.findChild<QToolBar *>("toolboxPanel");
            QVERIFY(column && column->isVisible());
            QCOMPARE(column->width(), ui::metrics::railWidth);
            QCOMPARE(window.toolBarArea(column), Qt::LeftToolBarArea);
            QCOMPARE(column->allowedAreas(), Qt::LeftToolBarArea | Qt::RightToolBarArea);
            QTest::qWait(20);
            QVERIFY(window.grab().save("test-artifacts/workspace-icon-column.png"));
            auto *content = column->findChild<QWidget *>("toolboxContent");
            QVERIFY(content);
            auto *tools = window.findChild<QDockWidget *>("toolsPanel");
            auto *animation = window.findChild<QDockWidget *>("animationPanel");
            animation->show();
            window.resize(1440, 960);
            QTest::qWait(40);
            QCOMPARE(column->width(), ui::metrics::railWidth);
            QVERIFY(column->y() <= tools->y());
            QVERIFY(column->y() <= window.canvas()->mapTo(&window, QPoint()).y());
            QVERIFY(column->geometry().bottom() >= window.canvas()->mapTo(&window, window.canvas()->rect().bottomLeft()).y());
            QVERIFY(column->geometry().bottom() >= animation->geometry().bottom());
            QVERIFY(content->height() >= column->height() - 32);
            const int originalToolsWidth = tools->width();
            window.resizeDocks({tools}, {originalToolsWidth + 80}, Qt::Horizontal);
            QTRY_VERIFY(tools->width() >= originalToolsWidth + 40);
            QCOMPARE(column->width(), ui::metrics::railWidth);
            bool listedInPanels = false;
            for (auto *menu : window.menuBar()->findChildren<QMenu *>())
                if (menu->title().remove('&') == "Panels")
                    listedInPanels = menu->actions().contains(column->toggleViewAction());
            QVERIFY(listedInPanels);
            column->toggleViewAction()->trigger();
            QVERIFY(column->isHidden());
            column->toggleViewAction()->trigger();
            QVERIFY(column->isVisible());
            window.addToolBar(Qt::RightToolBarArea, column);
            QTest::qWait(20);
            QCOMPARE(window.toolBarArea(column), Qt::RightToolBarArea);
            QCOMPARE(column->width(), ui::metrics::railWidth);
            QVERIFY(column->geometry().bottom() >= animation->geometry().bottom());
            animation->hide();
            QVERIFY(!window.findChild<QDockWidget *>("brushesPanel"));
            auto *navigator = window.findChild<QDockWidget *>("controlPanel");
            navigator->show();
            QTest::qWait(80);
            window.canvas()->fitSheet();
            QVERIFY(window.grab().save("test-artifacts/workspace.png"));
            auto *layers = window.findChild<QDockWidget *>("layersPanel");
            layers->setFloating(true);
            layers->show();
            window.close();
        }
        {
            MainWindow restored(settingsPath);
            restored.show();
            QVERIFY(restored.openProject(projectPath));
            QTest::qWait(40);
            QVERIFY(restored.findChild<QDockWidget *>("controlPanel")->isVisible());
            QVERIFY(restored.findChild<QDockWidget *>("layersPanel")->isFloating());
            auto *column = restored.findChild<QToolBar *>("toolboxPanel");
            QVERIFY(column && column->isVisible());
            QCOMPARE(restored.toolBarArea(column), Qt::RightToolBarArea);
            QCOMPARE(column->width(), ui::metrics::railWidth);
            QCOMPARE(restored.canvas()->project().toJson(), p.toJson());
            auto *reset = restored.findChild<QAction *>("resetWorkspaceAction");
            QVERIFY(reset);
            reset->trigger();
            QTest::qWait(40);
            QCOMPARE(restored.toolBarArea(column), Qt::LeftToolBarArea);
            QVERIFY(column->isVisible());
            QCOMPARE(column->width(), ui::metrics::railWidth);
            QVERIFY(!restored.findChild<QDockWidget *>("layersPanel")->isFloating());
            QVERIFY(restored.findChild<QDockWidget *>("toolsPanel")->isVisible());
            restored.close();
        }
        QSettings saved(settingsPath, QSettings::IniFormat);
        QVERIFY(saved.contains("layoutV3"));
        ProjectDialog dialog(p, true);
        dialog.show();
        QTest::qWait(80);
        QVERIFY(dialog.grab().save("test-artifacts/project-setup.png"));
    }
    void toolManagementAndViewPreferencesSurviveReopen() {
        QTemporaryDir temp;
        const auto settingsPath = temp.filePath("workspace.ini");
        const auto projectPath = temp.filePath("editor.toonproj");
        QString error; QVERIFY(saveProject(Project::create(), projectPath, &error));
        QString active;
        {
            MainWindow window(settingsPath); window.show();
            QVERIFY(window.openProject(projectPath));
            auto *tools = window.findChild<ToolNavigationPanel *>("toolListContent");
            auto *dock = window.findChild<QDockWidget *>("toolManagementPanel");
            auto *manager = qobject_cast<ToolManagementPanel *>(dock->widget());
            QVERIFY(tools); QVERIFY(manager); QVERIFY(dock->isHidden());
            QVERIFY(dock->isFloating());
            bool listed = false;
            for (auto *menu : window.menuBar()->findChildren<QMenu *>())
                if (menu->title().remove('&') == "Panels") listed = menu->actions().contains(dock->toggleViewAction());
            QVERIFY(listed);
            auto *catalog = window.toolCatalog(); active = catalog->activeToolId();
            QVERIFY(catalog->setToolTags({active}, {"Inking"}));
            tools->setIconsOnly(true); tools->setTagFilter("Inking");
            tools->manageToolsRequested(catalog->selectedEntryId(), active);
            QTRY_VERIFY(dock->isVisible());
            QCOMPARE(catalog->activeToolId(), active);
            QVERIFY(catalog->setToolColor({active}, QColor("#c86484")));
            auto *list = manager->findChild<QListWidget *>("managedTools"); QVERIFY(list);
            list->setFocus();
            QTRY_VERIFY(manager->isAncestorOf(QApplication::focusWidget()));
            auto *undo = window.findChild<QAction *>("undoAction"); QVERIFY(undo);
            QTRY_VERIFY(undo->isEnabled()); QTest::keySequence(list, undo->shortcut());
            QVERIFY(!catalog->tool(active)->color.isValid());
            QCOMPARE(catalog->tool(active)->tags, QStringList{"Inking"});
            QCOMPARE(window.canvas()->document()->history()->count(), 0);
            QVERIFY(window.close());
        }
        {
            MainWindow restored(settingsPath); restored.show();
            QVERIFY(restored.openProject(projectPath));
            auto *tools = restored.findChild<ToolNavigationPanel *>("toolListContent");
            QVERIFY(tools && tools->iconsOnly());
            QCOMPARE(tools->tagFilter(), QString("Inking"));
            QCOMPARE(restored.toolCatalog()->activeToolId(), active);
            QCOMPARE(restored.toolCatalog()->tool(active)->tags, QStringList{"Inking"});
            QVERIFY(restored.findChild<QDockWidget *>("toolManagementPanel")->isVisible());
            QVERIFY(restored.close());
        }
    }
    void previousDockLayoutMigratesToColumn_data() {
        QTest::addColumn<bool>("toolboxHidden");
        QTest::newRow("visible-toolbox") << false;
        QTest::newRow("hidden-toolbox") << true;
    }
    void previousDockLayoutMigratesToColumn() {
        QFETCH(bool, toolboxHidden);
        QTemporaryDir temp;
        const auto settingsPath = temp.filePath("workspace.ini");
        const auto projectPath = temp.filePath("movie.toonproj");
        QString error;
        QVERIFY(saveProject(Project::create(), projectPath, &error));
        QByteArray legacyState;
        {
            // Reproduce the old workspace using only its public Qt dock state format.
            QMainWindow legacy;
            legacy.resize(1320, 860);
            legacy.setCentralWidget(new QWidget);
            QDockWidget *oldToolbox = nullptr;
            for (const auto *id : {"toolboxPanel", "toolsPanel", "propertiesPanel", "layersPanel",
                                   "colorsPanel", "controlPanel", "referencesPanel", "effectsPanel", "animationPanel"}) {
                auto *dock = new QDockWidget(id, &legacy);
                dock->setObjectName(id);
                dock->setWidget(new QLabel(id));
                const bool left = QString(id) == "propertiesPanel" || QString(id) == "referencesPanel" || QString(id) == "effectsPanel";
                legacy.addDockWidget(QString(id) == "animationPanel" ? Qt::BottomDockWidgetArea
                    : left ? Qt::LeftDockWidgetArea : Qt::RightDockWidgetArea, dock);
                if (QString(id) == "toolboxPanel") oldToolbox = dock;
                else if (QString(id) != "toolsPanel" && QString(id) != "propertiesPanel" && QString(id) != "layersPanel") dock->hide();
            }
            legacy.show();
            oldToolbox->setVisible(!toolboxHidden);
            QTest::qWait(20);
            legacyState = legacy.saveState(2);
            QSettings settings(settingsPath, QSettings::IniFormat);
            settings.setValue("geometry", legacy.saveGeometry());
            settings.setValue("layoutV2", legacyState);
            settings.sync();
        }
        {
            MainWindow restored(settingsPath);
            restored.show();
            QVERIFY(restored.openProject(projectPath));
            QTest::qWait(40);
            auto *column = restored.findChild<QToolBar *>("toolboxPanel");
            QVERIFY(column);
            QCOMPARE(column->isHidden(), toolboxHidden);
            QCOMPARE(restored.toolBarArea(column), Qt::RightToolBarArea);
            QCOMPARE(restored.dockWidgetArea(restored.findChild<QDockWidget *>("toolsPanel")), Qt::RightDockWidgetArea);
            QCOMPARE(restored.dockWidgetArea(restored.findChild<QDockWidget *>("propertiesPanel")), Qt::LeftDockWidgetArea);
            QCOMPARE(restored.dockWidgetArea(restored.findChild<QDockWidget *>("layersPanel")), Qt::RightDockWidgetArea);
            QVERIFY(restored.findChild<QDockWidget *>("toolsPanel")->isVisible());
            restored.close();
        }
        QSettings saved(settingsPath, QSettings::IniFormat);
        QCOMPARE(saved.value("layoutV2").toByteArray(), legacyState);
        QVERIFY(saved.contains("layoutV3"));
    }
    void tabbedLegacyWorkspaceSurvivesStartupAndReopen() {
        // Real V2 layout from the startup crash: split side docks, a hidden tab
        // group and a fullscreen window. Only geometry/layout bytes are retained.
        const auto fixturePath = QFINDTESTDATA("fixtures/workspace-v2-tabbed.ini");
        QVERIFY(!fixturePath.isEmpty());
        QSettings fixture(fixturePath, QSettings::IniFormat);
        QCOMPARE(fixture.allKeys(), QStringList({"geometry", "layoutV2"}));
        const auto legacyState = fixture.value("layoutV2").toByteArray();
        QVERIFY(!legacyState.isEmpty());
        const auto displayStateMask = Qt::WindowMaximized | Qt::WindowFullScreen;
        auto geometry = fixture.value("geometry").toByteArray();
        Qt::WindowStates expectedDisplayState;
        {
            QMainWindow geometryProbe;
            if (!geometryProbe.restoreGeometry(geometry)) {
                // Qt rejects saved geometry from a much larger screen. Keep the
                // fixture's fullscreen mode on headless/smaller test displays.
                geometryProbe.setGeometry(geometryProbe.screen()->availableGeometry().adjusted(40, 40, -40, -40));
                geometryProbe.setWindowState(Qt::WindowFullScreen);
                geometry = geometryProbe.saveGeometry();
            }
            expectedDisplayState = geometryProbe.windowState() & displayStateMask;
            geometryProbe.setWindowState(Qt::WindowNoState);
        }
        QVERIFY(expectedDisplayState.testFlag(Qt::WindowFullScreen));
        QTemporaryDir temp;
        const auto settingsPath = temp.filePath("workspace.ini");
        QVERIFY(QFile::copy(fixturePath, settingsPath));
        if (geometry != fixture.value("geometry").toByteArray()) {
            QSettings settings(settingsPath, QSettings::IniFormat);
            settings.setValue("geometry", geometry);
            settings.sync();
        }
        const auto projectPath = temp.filePath("movie.toonproj");
        QString error;
        QVERIFY2(saveProject(Project::create(), projectPath, &error), qPrintable(error));

        // First launch migrates V2; the second exercises the saved V3 state.
        for (int launch = 0; launch < 2; ++launch) {
            auto requestedDisplayState = expectedDisplayState;
            if (launch > 0) {
                QSettings saved(settingsPath, QSettings::IniFormat);
                QMainWindow geometryProbe;
                QVERIFY(geometryProbe.restoreGeometry(saved.value("geometry").toByteArray()));
                requestedDisplayState = geometryProbe.windowState() & displayStateMask;
                geometryProbe.setWindowState(Qt::WindowNoState);
            }
            MainWindow window(settingsPath);
            // Verify restoration before Cocoa asynchronously applies or adjusts
            // the requested mode to accommodate visible panel minimum sizes.
            QCOMPARE(window.windowState() & displayStateMask, requestedDisplayState);
            window.show();
            QTest::qWait(QGuiApplication::platformName() == "cocoa" ? 600 : 80);
            QVERIFY(window.isVisible());
            if (QGuiApplication::platformName() == "offscreen") {
                // The virtual screen is smaller than this production layout's
                // panel minimums. Startup above still exercises fullscreen;
                // give spatial assertions room once restoration has completed.
                window.showNormal();
                window.resize(1800, 1400);
                QTest::qWait(40);
            }
            QVERIFY(window.openProject(projectPath));
            QTest::qWait(40);
            QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);

            auto *column = window.findChild<QToolBar *>("toolboxPanel");
            QVERIFY(column && column->isVisible());
            QCOMPARE(window.toolBarArea(column), Qt::LeftToolBarArea);
            QCOMPARE(column->width(), ui::metrics::railWidth);
            auto *tools = window.findChild<QDockWidget *>("toolsPanel");
            auto *properties = window.findChild<QDockWidget *>("propertiesPanel");
            auto *colors = window.findChild<QDockWidget *>("colorsPanel");
            auto *layers = window.findChild<QDockWidget *>("layersPanel");
            QVERIFY(tools && properties && colors && layers);
            QVERIFY(tools->isVisible());
            QCOMPARE(window.dockWidgetArea(tools), Qt::LeftDockWidgetArea);
            for (auto *panel : {properties, colors, layers}) {
                QVERIFY(panel->isVisible());
                QVERIFY(!panel->isFloating());
                QCOMPARE(window.dockWidgetArea(panel), Qt::RightDockWidgetArea);
            }
            // Properties and Color remain separate vertical panels above Layers.
            QVERIFY(properties->geometry().bottom() < colors->geometry().top());
            QVERIFY(colors->geometry().bottom() < layers->geometry().top());
            QVERIFY(!window.tabifiedDockWidgets(layers).contains(properties));
            QVERIFY(!window.tabifiedDockWidgets(layers).contains(colors));
            for (const auto *id : {"controlPanel", "referencesPanel", "effectsPanel"}) {
                auto *panel = window.findChild<QDockWidget *>(id);
                QVERIFY2(panel, id);
                QVERIFY2(panel->isHidden(), id);
                // Qt omits hidden docks from tabifiedDockWidgets(). Showing it
                // must recover its saved tab membership, then restore hiding.
                panel->show();
                QTest::qWait(20);
                QVERIFY2(window.tabifiedDockWidgets(layers).contains(panel), id);
                panel->hide();
            }
            QVERIFY(window.findChild<QDockWidget *>("animationPanel")->isHidden());
            QVERIFY(window.close());
            QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
            QSettings saved(settingsPath, QSettings::IniFormat);
            QCOMPARE(saved.value("layoutV2").toByteArray(), legacyState);
            QVERIFY(!saved.value("layoutV3").toByteArray().isEmpty());
            if (QGuiApplication::platformName() == "cocoa") QTest::qWait(500);
        }
    }
};

QTEST_MAIN(FoundationTest)
#include "test_foundation.moc"
