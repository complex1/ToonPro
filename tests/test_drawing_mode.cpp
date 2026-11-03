#include "canvas.h"
#include "drawing_input.h"
#include "main_window.h"
#include "value_slider.h"

#include <QAction>
#include <QDialog>
#include <QDir>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QMouseEvent>
#include <QPushButton>
#include <QScreen>
#include <QSettings>
#include <QSlider>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QToolBar>
#include <QVBoxLayout>
#include <QtTest>

using namespace toon;

namespace {
Project fixture()
{
    auto project = Project::create();
    project.width = 640;
    project.height = 480;
    project.transparent = true;
    return project;
}

bool prepare(MainWindow &window, const QTemporaryDir &directory)
{
    QString error;
    const auto path = directory.filePath("drawing.toonproj");
    if (!saveProject(fixture(), path, &error) || !window.openProject(path)) return false;
    // Match the host's available screen, including the small offscreen plugin
    // surface. restoreGeometry legitimately clamps oversized saved windows.
    const auto available = window.screen()->availableGeometry();
    window.resize(QSize(1280, 860).boundedTo(available.size() - QSize(40, 60)));
    window.move(available.topLeft() + QPoint(20, 40));
    window.show();
    window.activateWindow();
    window.canvas()->setFocus();
    QCoreApplication::processEvents();
    return true;
}

struct DockState {
    bool hidden = false;
    bool floating = false;
    Qt::DockWidgetArea area = Qt::NoDockWidgetArea;
    QStringList peers;
};

QMap<QString, DockState> dockStates(MainWindow &window)
{
    QMap<QString, DockState> states;
    for (auto *dock : window.findChildren<QDockWidget *>()) {
        QStringList peers;
        for (auto *peer : window.tabifiedDockWidgets(dock)) peers.append(peer->objectName());
        peers.sort();
        states.insert(dock->objectName(), {dock->isHidden(), dock->isFloating(), window.dockWidgetArea(dock), peers});
    }
    return states;
}

void compareDocks(MainWindow &window, const QMap<QString, DockState> &expected)
{
    const auto actual = dockStates(window);
    QCOMPARE(actual.keys(), expected.keys());
    for (auto it = expected.cbegin(); it != expected.cend(); ++it) {
        const auto found = actual.value(it.key());
        QVERIFY2(found.hidden == it->hidden, qPrintable(it.key() + " visibility changed"));
        QVERIFY2(found.floating == it->floating, qPrintable(it.key() + " floating state changed"));
        QVERIFY2(found.area == it->area, qPrintable(it.key() + " dock area changed"));
        QVERIFY2(found.peers == it->peers, qPrintable(it.key() + " tab group changed"));
    }
}

void customizeWorkspace(MainWindow &window)
{
    auto *references = window.findChild<QDockWidget *>("referencesPanel");
    auto *effects = window.findChild<QDockWidget *>("effectsPanel");
    auto *layers = window.findChild<QDockWidget *>("layersPanel");
    auto *colors = window.findChild<QDockWidget *>("colorsPanel");
    QVERIFY(references && effects && layers && colors);
    references->setFloating(true);
    references->resize(300, 360);
    references->move(window.pos() + QPoint(45, 75));
    references->show();
    window.tabifyDockWidget(layers, effects);
    layers->show(); effects->show(); layers->raise();
    colors->hide();
    auto *toolbox = window.findChild<QToolBar *>("toolboxPanel");
    QVERIFY(toolbox);
    window.addToolBar(Qt::RightToolBarArea, toolbox);
    toolbox->show();
    QCoreApplication::processEvents();
    window.activateWindow();
    window.canvas()->setFocus();
}

void sendMouse(Canvas *canvas, QEvent::Type type, QPointF artwork)
{
    const auto local = canvas->viewTransform().map(
        sheetGeometry(canvas->project(), canvas->shot()).output.topLeft() + artwork);
    QMouseEvent event(type, local, canvas->mapToGlobal(local),
        type == QEvent::MouseMove ? Qt::NoButton : Qt::LeftButton,
        type == QEvent::MouseButtonRelease ? Qt::NoButton : Qt::LeftButton,
        Qt::NoModifier);
    QApplication::sendEvent(canvas, &event);
}

void preview(MainWindow &window, const QString &name)
{
    const auto directory = qEnvironmentVariable("TOON_DRAWING_MODE_PREVIEW");
    if (directory.isEmpty()) return;
    QDir().mkpath(directory);
    QVERIFY(window.grab().save(QDir(directory).filePath(name + ".png")));
}
} // namespace

class DrawingModeTests final : public QObject {
    Q_OBJECT
private slots:
    void initTestCase()
    {
        configureDrawingInput();
        applyTheme();
    }

    void welcomeDoesNotEnterDrawingMode()
    {
        QTemporaryDir directory; QVERIFY(directory.isValid());
        MainWindow window(directory.filePath("workspace.ini"));
        window.show(); window.activateWindow();
        QVERIFY(!window.drawingMode());
        window.setDrawingMode(true);
        QVERIFY(!window.drawingMode());
        QVERIFY(!window.isFullScreen());
        auto *action = window.findChild<QAction *>("drawingModeAction");
        QVERIFY(action); QVERIFY(!action->isEnabled());
        QTest::keyClick(&window, Qt::Key_Tab);
        QVERIFY(!window.drawingMode());
        QVERIFY(window.close());
    }

    void tabHidesChromeAndRestoresTheArtistsLayout()
    {
        QTemporaryDir directory; QVERIFY(directory.isValid());
        MainWindow window(directory.filePath("workspace.ini"));
        QVERIFY(prepare(window, directory));
        customizeWorkspace(window);
        const auto before = dockStates(window);
        auto *quick = window.findChild<QToolBar *>("quickAccessToolbar");
        auto *project = window.findChild<QToolBar *>("projectToolbar");
        auto *toolbox = window.findChild<QToolBar *>("toolboxPanel");
        auto *toggle = window.findChild<QAction *>("drawingModeAction");
        QVERIFY(quick && project && toolbox && toggle);
        const auto state = window.windowState();
        const auto geometry = window.geometry();
        const auto toolboxArea = window.toolBarArea(toolbox);
        const bool projectHidden = project->isHidden();
        const bool menuHidden = window.menuBar()->isHidden();
        const bool statusHidden = window.statusBar()->isHidden();
        preview(window, "normal-workspace");

        QTest::keyClick(window.canvas(), Qt::Key_Tab);
        QTRY_VERIFY(window.drawingMode());
        QTRY_VERIFY(window.isFullScreen());
        for (auto *dock : window.findChildren<QDockWidget *>())
            QVERIFY2(dock->isHidden(), qPrintable(dock->objectName()));
        QVERIFY(project->isHidden()); QVERIFY(toolbox->isHidden());
        QVERIFY(window.menuBar()->isHidden()); QVERIFY(window.statusBar()->isHidden());
        QVERIFY(quick->isVisible()); QVERIFY(toggle->isEnabled()); QVERIFY(toggle->isChecked());
        QVERIFY(quick->widgetForAction(toggle));
        QVERIFY(quick->widgetForAction(toggle)->isVisible());
        QTRY_COMPARE(QApplication::focusWidget(), window.canvas());
        preview(window, "drawing-mode");

        QTest::keyClick(window.canvas(), Qt::Key_Tab);
        QTRY_VERIFY(!window.drawingMode());
        QTRY_VERIFY(!window.isFullScreen());
        compareDocks(window, before);
        QCOMPARE(window.windowState(), state);
        QTRY_COMPARE(window.geometry().size(), geometry.size());
        QCOMPARE(window.toolBarArea(toolbox), toolboxArea);
        QCOMPARE(project->isHidden(), projectHidden);
        QCOMPARE(window.menuBar()->isHidden(), menuHidden);
        QCOMPARE(window.statusBar()->isHidden(), statusHidden);
        QVERIFY(!toggle->isChecked());
        preview(window, "restored-workspace");
        QVERIFY(window.close());
    }

    void buttonCanExitAndPreExistingFullscreenIsPreserved()
    {
        QTemporaryDir directory; QVERIFY(directory.isValid());
        MainWindow window(directory.filePath("workspace.ini"));
        QVERIFY(prepare(window, directory));
        auto *project = window.findChild<QToolBar *>("projectToolbar");
        auto *quick = window.findChild<QToolBar *>("quickAccessToolbar");
        auto *toggle = window.findChild<QAction *>("drawingModeAction");
        QVERIFY(project && quick && toggle);
        // Drawing mode is independent of ordinary window fullscreen. It must
        // restore the artist's previous state, including deliberately hidden bars.
        project->hide(); window.statusBar()->hide();
        window.showFullScreen();
        QTRY_VERIFY(window.isFullScreen());
        const auto before = dockStates(window);
        window.setDrawingMode(true);
        QVERIFY(window.drawingMode());
        auto *button = quick->widgetForAction(toggle); QVERIFY(button && button->isVisible());
        QTest::mouseClick(button, Qt::LeftButton);
        QTRY_VERIFY(!window.drawingMode());
        QVERIFY(window.isFullScreen());
        QVERIFY(project->isHidden()); QVERIFY(window.statusBar()->isHidden());
        compareDocks(window, before);
        window.showNormal();
        QVERIFY(window.close());
    }

    void tabKeepsInputDialogAndPopupNavigation()
    {
        QTemporaryDir directory; QVERIFY(directory.isValid());
        MainWindow window(directory.filePath("workspace.ini"));
        QVERIFY(prepare(window, directory));
        auto *search = window.findChild<QLineEdit *>("toolPanelSearch");
        QVERIFY(search && search->isVisible());
        search->setFocus();
        QTRY_COMPARE(QApplication::focusWidget(), search);
        QTest::keyClick(search, Qt::Key_Tab);
        QVERIFY(!window.drawingMode());
        QVERIFY(QApplication::focusWidget() != search);

        QDialog dialog(&window);
        dialog.setModal(true);
        auto *layout = new QVBoxLayout(&dialog);
        auto *first = new QPushButton("First", &dialog);
        auto *second = new QPushButton("Second", &dialog);
        layout->addWidget(first); layout->addWidget(second);
        dialog.show(); dialog.activateWindow(); first->setFocus();
        QTRY_COMPARE(QApplication::activeModalWidget(), &dialog);
        QTRY_COMPARE(QApplication::focusWidget(), first);
        QTest::keyClick(first, Qt::Key_Tab);
        QVERIFY(!window.drawingMode());
        QCOMPARE(QApplication::focusWidget(), second);
        dialog.close();

        window.activateWindow(); window.canvas()->setFocus();
        QMenu menu(&window); menu.addAction("First"); menu.addAction("Second");
        menu.popup(window.canvas()->mapToGlobal(QPoint(30, 30)));
        QTRY_COMPARE(QApplication::activePopupWidget(), &menu);
        QTest::keyClick(&menu, Qt::Key_Tab);
        QVERIFY(!window.drawingMode());
        menu.close();
        window.activateWindow(); window.canvas()->setFocus();
        QTest::keyClick(window.canvas(), Qt::Key_Tab, Qt::ControlModifier);
        QVERIFY(!window.drawingMode());
        QTest::keyClick(window.canvas(), Qt::Key_Tab);
        QTRY_VERIFY(window.drawingMode());
        window.setDrawingMode(false);
        QVERIFY(window.close());
    }

    void tabNavigatesFloatingToolsAndCanvasModeKeepsInspectorsHidden()
    {
        QTemporaryDir directory; QVERIFY(directory.isValid());
        MainWindow window(directory.filePath("workspace.ini"));
        QVERIFY(prepare(window, directory));
        auto *tools = window.findChild<QDockWidget *>("toolsPanel");
        auto *properties = window.findChild<QDockWidget *>("propertiesPanel");
        auto *entries = window.findChild<QListWidget *>("toolEntries");
        QVERIFY(tools && properties && entries);
        tools->setFloating(true); tools->show(); tools->activateWindow();
        entries->setFocus();
        QTRY_COMPARE(QApplication::focusWidget(), entries);
        const auto before = dockStates(window);
        QTest::keyClick(entries, Qt::Key_Tab);
        QVERIFY(!window.drawingMode()); QVERIFY(!tools->isHidden());
        QVERIFY(QApplication::focusWidget() != entries);
        window.activateWindow(); window.canvas()->setFocus();
        QTest::keyClick(window.canvas(), Qt::Key_Tab);
        QTRY_VERIFY(window.drawingMode());
        QTRY_VERIFY(tools->isHidden());
        QTRY_COMPARE(QApplication::focusWidget(), window.canvas());
        window.canvas()->setTool(Canvas::Tool::Eraser);
        QCOMPARE(window.canvas()->tool(), Canvas::Tool::Eraser);
        QCoreApplication::processEvents();
        QVERIFY(properties->isHidden());
        window.canvas()->setTool(Canvas::Tool::Brush);
        QCOMPARE(window.canvas()->tool(), Canvas::Tool::Brush);
        QCoreApplication::processEvents();
        QVERIFY(window.drawingMode());
        for (auto *dock : window.findChildren<QDockWidget *>())
            QVERIFY2(dock->isHidden(), qPrintable(dock->objectName()));
        QTest::keyClick(window.canvas(), Qt::Key_Tab);
        QTRY_VERIFY(!window.drawingMode());
        compareDocks(window, before);
        QVERIFY(tools->isFloating()); QVERIFY(!tools->isHidden());
        QVERIFY(window.close());
    }

    void exactEntryAndLayerPreviewPreventLayoutChanges()
    {
        QTemporaryDir directory; MainWindow window(directory.filePath("workspace.ini"));
        QVERIFY(prepare(window, directory));
        auto *canvas = window.canvas(); auto *document = canvas->document();
        auto *size = window.findChild<ValueSlider *>("quickBrushSize"); QVERIFY(size);
        auto *number = size->findChild<QDoubleSpinBox *>();
        number->setFocus(); QTRY_VERIFY(number->hasFocus()); number->selectAll(); QTest::keyClicks(number, "73.5");
        const auto layout = window.saveState(3); const auto view = canvas->viewTransform();
        const int history = document->history()->count(); const auto original = size->value();
        window.setDrawingMode(true);
        QVERIFY(!window.drawingMode()); QCOMPARE(window.saveState(3), layout); QCOMPARE(canvas->viewTransform(), view);
        QVERIFY(number->hasFocus()); QCOMPARE(number->findChild<QLineEdit *>()->text(), QString("73.5"));
        QCOMPARE(size->value(), original); QCOMPARE(document->history()->count(), history);
        QTest::keyClick(number, Qt::Key_Escape); QCOMPARE(size->value(), original);
        auto *track = size->findChild<QSlider *>(); track->setFocus(); QTRY_VERIFY(track->hasFocus());
        QTest::keyClick(track, Qt::Key_Tab); QVERIFY(!window.drawingMode()); QVERIFY(!track->hasFocus());
        QVERIFY(document->beginLayerPropertyEdit()); document->previewLayerOpacity(.5);
        window.setDrawingMode(true); QVERIFY(!window.drawingMode()); QVERIFY(document->layerPropertyEditActive());
        QCOMPARE(document->history()->count(), history); document->cancelLayerPropertyEdit();
        canvas->setFocus(); QTest::keyClick(canvas, Qt::Key_Tab); QTRY_VERIFY(window.drawingMode());
        window.setDrawingMode(false); document->history()->setClean(); QVERIFY(window.close());
    }

    void penDownPreventsModeChangesAndDrawingUndoStillWorks()
    {
        QTemporaryDir directory; QVERIFY(directory.isValid());
        MainWindow window(directory.filePath("workspace.ini"));
        QVERIFY(prepare(window, directory));
        auto *canvas = window.canvas();
        auto *document = canvas->document();
        canvas->setTool(Canvas::Tool::Brush);
        BrushSettings brush; brush.color = Qt::black; brush.size = 12; brush.pressureSize = false;
        canvas->setBrushSettings(brush); canvas->setStabilization(0);
        window.setDrawingMode(true);
        QTRY_VERIFY(window.drawingMode());
        canvas->fitSheet(); canvas->setFocus();
        const auto empty = renderMovieFrame(canvas->project());
        sendMouse(canvas, QEvent::MouseButtonPress, {100, 100});
        QVERIFY(document->strokeActive());
        QTest::keyClick(canvas, Qt::Key_Tab);
        QVERIFY(window.drawingMode()); QVERIFY(document->strokeActive());
        window.setDrawingMode(false);
        QVERIFY(window.drawingMode()); QVERIFY(document->strokeActive());
        sendMouse(canvas, QEvent::MouseMove, {200, 150});
        sendMouse(canvas, QEvent::MouseButtonRelease, {300, 200});
        QVERIFY(!document->strokeActive());
        QCOMPARE(document->history()->count(), 1);
        const auto painted = renderMovieFrame(canvas->project());
        QVERIFY(painted != empty);
        QVERIFY(painted.pixelColor(150, 125).alpha() > 0);
        auto *undo = window.findChild<QAction *>("undoAction");
        auto *redo = window.findChild<QAction *>("redoAction");
        QVERIFY(undo && redo && undo->isEnabled());
        QTest::keySequence(canvas, QKeySequence::Undo);
        QTRY_COMPARE(renderMovieFrame(canvas->project()), empty);
        QVERIFY(redo->isEnabled());
        QTest::keySequence(canvas, QKeySequence::Redo);
        QTRY_COMPARE(renderMovieFrame(canvas->project()), painted);
        QVERIFY(window.drawingMode());
        document->history()->setClean();
        QTest::keyClick(canvas, Qt::Key_Tab);
        QTRY_VERIFY(!window.drawingMode());
        QVERIFY(window.close());
    }

    void closingInDrawingModePersistsNormalWorkspace()
    {
        QTemporaryDir directory; QVERIFY(directory.isValid());
        const auto settingsPath = directory.filePath("workspace.ini");
        QMap<QString, DockState> expected;
        QByteArray normalLayout;
        QSize normalSize;
        {
            MainWindow window(settingsPath);
            QVERIFY(prepare(window, directory));
            customizeWorkspace(window);
            expected = dockStates(window);
            normalLayout = window.saveState(3);
            normalSize = window.size();
            window.setDrawingMode(true);
            QVERIFY(window.drawingMode());
            QVERIFY(window.close());
        }
        QSettings settings(settingsPath, QSettings::IniFormat);
        QCOMPARE(settings.value("layoutV3").toByteArray(), normalLayout);
        {
            MainWindow reopened(settingsPath);
            QVERIFY(reopened.openProject(directory.filePath("drawing.toonproj")));
            reopened.show();
            QCoreApplication::processEvents();
            QVERIFY(!reopened.drawingMode()); QVERIFY(!reopened.isFullScreen());
            compareDocks(reopened, expected);
            QTRY_COMPARE(reopened.size(), normalSize);
            auto *toolbox = reopened.findChild<QToolBar *>("toolboxPanel");
            auto *project = reopened.findChild<QToolBar *>("projectToolbar");
            QVERIFY(toolbox && project);
            QCOMPARE(reopened.toolBarArea(toolbox), Qt::RightToolBarArea);
            QVERIFY(!toolbox->isHidden()); QVERIFY(!project->isHidden());
            QVERIFY(reopened.close());
        }
    }
};

QTEST_MAIN(DrawingModeTests)
#include "test_drawing_mode.moc"
