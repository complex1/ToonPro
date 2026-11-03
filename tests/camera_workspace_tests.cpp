#include "canvas.h"
#include "camera_tool.h"
#include "main_window.h"
#include "timeline_controller.h"
#include "value_slider.h"
#include <QAction>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QMouseEvent>
#include <QTabBar>
#include <QTemporaryDir>
#include <QtTest>
using namespace toon;
namespace {
Project fixture() {
    auto project = Project::create(); project.width = 192; project.height = 128;
    project.shots[0].camera2DKeys = {{0, {}}, {20, {{20, 10}, 1.5, 15}}}; return project;
}
void mouse(Canvas *canvas, QEvent::Type type, QPointF point) {
    QMouseEvent event(type, point, point, canvas->mapToGlobal(point), type == QEvent::MouseMove ? Qt::NoButton : Qt::LeftButton,
        type == QEvent::MouseButtonRelease ? Qt::NoButton : Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(canvas, &event);
}
}
class CameraWorkspaceTests final : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { applyTheme(); }
    void pendingCameraEntryUndoEscapeSaveAndReload() {
        QTemporaryDir dir; QString error; const auto path = dir.filePath("camera.toonproj");
        QVERIFY2(saveProject(fixture(), path, &error), qPrintable(error));
        MainWindow window(dir.filePath("settings.ini")); QVERIFY(window.openProject(path)); window.resize(1280, 900); window.show();
        auto *controller = window.findChild<TimelineController *>(); QVERIFY(controller);
        auto *dock = window.findChild<QDockWidget *>("framePropertiesPanel"); QVERIFY(dock); dock->show(); dock->raise();
        QVERIFY(controller->setFrame(6)); QVERIFY(controller->selectCamera()); QCoreApplication::processEvents();
        auto *tabs = dock->findChild<QTabBar *>("framePropertiesTabs"); QVERIFY(tabs); QCOMPARE(tabs->currentIndex(), 3);
        auto *field = dock->findChild<ValueSlider *>("cameraX"); QVERIFY(field); QVERIFY(field->isEnabled());
        auto *spin = field->findChild<QDoubleSpinBox *>(); QVERIFY(spin);
        auto *undo = window.findChild<QAction *>("undoAction"), *save = window.findChild<QAction *>("saveProjectAction"); QVERIFY(undo && save);
        const auto original = window.canvas()->project().toJson(); auto *history = controller->document()->history();
        const auto enter = [&](const QString &value) { spin->setFocus(); spin->selectAll(); QTest::keyClicks(spin, value); };
        enter("96"); QVERIFY(controller->hasPendingEdits()); QVERIFY(undo->isEnabled()); undo->trigger();
        QVERIFY(!controller->hasPendingEdits()); QVERIFY(!controller->document()->cameraEditActive());
        QCOMPARE(window.canvas()->project().toJson(), original); QCOMPARE(history->count(), 0);
        enter("104"); QTest::keyClick(spin, Qt::Key_Escape); QCOMPARE(window.canvas()->project().toJson(), original);
        enter("96"); QVERIFY(controller->hasPendingEdits()); QVERIFY(save->isEnabled()); save->trigger();
        QVERIFY(!controller->hasPendingEdits()); QVERIFY(!controller->document()->cameraEditActive());
        QCOMPARE(history->count(), 1); QVERIFY(history->isClean()); QCOMPARE(controller->currentFrame(), 6);
        QCOMPARE(evaluateShotCamera(window.canvas()->shot(), 6).position.x(), 96.);
        const auto saved = loadProject(path, &error); QVERIFY2(saved.has_value(), qPrintable(error));
        QCOMPARE(saved->toJson(), window.canvas()->project().toJson());
        window.canvas()->setFocus(); QCoreApplication::processEvents(); QCOMPARE(history->count(), 1); QVERIFY(window.close());
    }
    void unfinishedCanvasFramingUndoAndSaveResolveOnce() {
        QTemporaryDir dir; QString error; const auto path = dir.filePath("framing.toonproj");
        QVERIFY(saveProject(fixture(), path, &error)); MainWindow window(dir.filePath("settings.ini")); QVERIFY(window.openProject(path));
        window.resize(1280, 900); window.show(); QCoreApplication::processEvents(); auto *canvas = window.canvas();
        auto *controller = window.findChild<TimelineController *>(); QVERIFY(controller); QVERIFY(controller->setFrame(6)); QVERIFY(controller->selectCamera());
        controller->editCameraFraming(); canvas->fitSheet(); auto *tool = canvas->cameraTool(); QVERIFY(tool->enabled());
        const auto original = canvas->project().toJson(); const auto frame = tool->box(); const auto start = (frame[0] + frame[2]) / 2, end = start + QPointF(35, 23);
        auto *undo = window.findChild<QAction *>("undoAction"), *save = window.findChild<QAction *>("saveProjectAction"); QVERIFY(undo && save);
        mouse(canvas, QEvent::MouseButtonPress, start); mouse(canvas, QEvent::MouseMove, end); QVERIFY(tool->dragging()); QVERIFY(undo->isEnabled()); undo->trigger();
        QVERIFY(!tool->dragging()); mouse(canvas, QEvent::MouseButtonRelease, end); QCOMPARE(canvas->project().toJson(), original);
        mouse(canvas, QEvent::MouseButtonPress, start); mouse(canvas, QEvent::MouseMove, end); QVERIFY(tool->dragging()); QVERIFY(save->isEnabled()); save->trigger();
        QVERIFY(!tool->dragging()); QCOMPARE(canvas->document()->history()->count(), 1); QVERIFY(canvas->document()->history()->isClean());
        const auto saved = loadProject(path, &error); QVERIFY2(saved.has_value(), qPrintable(error)); QCOMPARE(saved->toJson(), canvas->project().toJson());
        mouse(canvas, QEvent::MouseButtonRelease, end); QCOMPARE(canvas->document()->history()->count(), 1); QVERIFY(window.close());
    }
};
QTEST_MAIN(CameraWorkspaceTests)
#include "camera_workspace_tests.moc"
