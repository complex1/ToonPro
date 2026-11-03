#include "canvas.h"
#include "clipboard_tools.h"
#include "main_window.h"
#include "ui_theme.h"
#include <QApplication>
#include <QDir>
#include <QEnterEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QSignalSpy>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QTimer>
#include <QtTest>

using namespace toon;

namespace {
bool openDrawing(MainWindow &window, const QTemporaryDir &directory) {
    auto project = Project::create();
    project.width = 640; project.height = 480;
    auto vector = RasterLayer::create("Vector ink"); vector.kind = LayerKind::Vector;
    project.shots[0].layers.append(vector);
    const auto path = directory.filePath("drawing.toonproj");
    if (!saveProject(project, path, nullptr) || !window.openProject(path)) return false;
    window.resize(1100, 780); window.show();
    QApplication::processEvents();
    window.canvas()->document()->selectLayer(1);
    window.canvas()->setTool(Canvas::Tool::Bucket);
    return true;
}
void hover(Canvas *canvas) {
    const QPointF position = canvas->rect().center();
    QEnterEvent enter(position, position, canvas->mapToGlobal(position));
    QApplication::sendEvent(canvas, &enter);
    QMouseEvent move(QEvent::MouseMove, position, position, canvas->mapToGlobal(position),
                     Qt::NoButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(canvas, &move);
}
void leave(Canvas *canvas) {
    QEvent leaveEvent(QEvent::Leave);
    QApplication::sendEvent(canvas, &leaveEvent);
}
QLabel *message(MainWindow &window) { return window.findChild<QLabel *>("canvasStatusMessage"); }
QWidget *feedback(MainWindow &window) { return window.findChild<QWidget *>("canvasStatusFeedback"); }
}

class CanvasFeedbackTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() {
        QCoreApplication::setApplicationVersion("test-current-version");
        applyTheme();
    }
    void hoverExplainsRestrictionAndClearsOnLeaveOrValidLayer() {
        QTemporaryDir directory; MainWindow window(directory.filePath("workspace.ini"));
        QVERIFY(openDrawing(window, directory));
        auto *canvas = window.canvas(); hover(canvas);
        const auto reason = canvas->toolBlockReason(); QVERIFY(!reason.isEmpty());
        QCOMPARE(message(window)->text(), reason);
        QCOMPARE(message(window)->accessibleName(), reason);
        QVERIFY(window.findChild<QLabel *>("canvasStatusIcon")->isVisible());
        const auto snapshot = canvas->project().toJson();
        canvas->setZoom(.75);
        QCOMPARE(message(window)->text(), reason); // Zoom metadata must not erase the hint.
        leave(canvas);
        QVERIFY(message(window)->text().contains("Zoom 75%"));
        QVERIFY(window.findChild<QLabel *>("canvasStatusIcon")->isHidden());
        hover(canvas); QCOMPARE(message(window)->text(), reason);
        canvas->document()->selectLayer(0);
        QVERIFY(canvas->toolBlockReason().isEmpty());
        QVERIFY(message(window)->text() != reason);
        QCOMPARE(canvas->project().toJson(), snapshot);
        QCOMPARE(canvas->document()->history()->count(), 0);
    }
    void attemptedActionHighlightsThenRestoresPersistentHint() {
        QTemporaryDir directory; MainWindow window(directory.filePath("workspace.ini"));
        QVERIFY(openDrawing(window, directory));
        auto *canvas = window.canvas(); hover(canvas);
        const auto reason = canvas->toolBlockReason();
        QSignalSpy blocked(canvas, &Canvas::actionBlocked);
        const auto snapshot = canvas->project().toJson();
        QTest::mouseClick(canvas, Qt::LeftButton, Qt::NoModifier, canvas->rect().center());
        QCOMPARE(blocked.count(), 1);
        QCOMPARE(message(window)->text(), reason);
        QVERIFY(feedback(window)->property("highlighted").toBool());
        window.findChild<QTimer *>("canvasFeedbackHighlightTimer")->setInterval(10);
        QTRY_VERIFY(!feedback(window)->property("highlighted").toBool());
        window.findChild<QTimer *>("canvasFeedbackMessageTimer")->setInterval(10);
        QTRY_VERIFY(!window.findChild<QTimer *>("canvasFeedbackMessageTimer")->isActive());
        QCOMPARE(message(window)->text(), reason);
        QCOMPARE(canvas->project().toJson(), snapshot);
        QCOMPARE(canvas->document()->history()->count(), 0);
        canvas->setTool(Canvas::Tool::Brush); // Existing raster brushes are valid on vector layers.
        QVERIFY(canvas->toolBlockReason().isEmpty());
        QVERIFY(message(window)->text() != reason);
    }
    void temporaryStatusMessagesRestoreTheHoverHint() {
        QTemporaryDir directory; MainWindow window(directory.filePath("workspace.ini"));
        QVERIFY(openDrawing(window, directory));
        hover(window.canvas()); const auto reason = window.canvas()->toolBlockReason();
        window.statusBar()->showMessage("Project saved", 20);
        QVERIFY(!feedback(window)->isVisible());
        QTRY_VERIFY(window.statusBar()->currentMessage().isEmpty());
        QVERIFY(feedback(window)->isVisible());
        QCOMPARE(message(window)->text(), reason);
    }
    void documentFailureStaysReadableAcrossHoverAndRestoresCurrentHint() {
        QTemporaryDir directory; MainWindow window(directory.filePath("workspace.ini"));
        QVERIFY(openDrawing(window, directory));
        auto *canvas = window.canvas(); hover(canvas);
        const auto reason = canvas->toolBlockReason();
        QSignalSpy warnings(canvas->document(), &Document::warning);
        QString error;
        QVERIFY(!ClipboardTools::copy(canvas->document(), false, &error));
        QCOMPARE(warnings.count(), 1); QVERIFY(!error.isEmpty());
        QCOMPARE(message(window)->text(), error);
        QCOMPARE(message(window)->accessibleName(), error);
        QVERIFY(feedback(window)->property("highlighted").toBool());
        QVERIFY(!QApplication::activeModalWidget());
        leave(canvas); hover(canvas); canvas->setZoom(.9);
        QCOMPARE(message(window)->text(), error);
        window.findChild<QTimer *>("canvasFeedbackMessageTimer")->setInterval(10);
        QTRY_COMPARE(message(window)->text(), reason);
        leave(canvas); QVERIFY(message(window)->text().contains("Zoom 90%"));
        QVERIFY(canvas->document()->history()->isClean());
    }
    void applicationVersionReflectsTheRunningBuild() {
        QTemporaryDir directory; MainWindow window(directory.filePath("workspace.ini"));
        QCOMPARE(window.findChild<QLabel *>("applicationVersionStatus")->text(),
                 QString("test-current-version · Drawing preview"));
    }
    void openingAnotherProjectClearsThePreviousRuntimeFailure() {
        QTemporaryDir directory; MainWindow window(directory.filePath("workspace.ini"));
        QVERIFY(openDrawing(window, directory));
        hover(window.canvas());
        QString error;
        QVERIFY(!ClipboardTools::copy(window.canvas()->document(), false, &error));
        QCOMPARE(message(window)->text(), error);
        QVERIFY(window.findChild<QTimer *>("canvasFeedbackMessageTimer")->isActive());
        auto next = Project::create(); next.movie = "Next drawing";
        const auto nextPath = directory.filePath("next.toonproj");
        QVERIFY(saveProject(next, nextPath, nullptr));
        QVERIFY(window.openProject(nextPath));
        QVERIFY(message(window)->text() != error);
        QVERIFY(message(window)->text().contains("Next drawing"));
        QVERIFY(!window.findChild<QTimer *>("canvasFeedbackMessageTimer")->isActive());
        QVERIFY(!window.findChild<QTimer *>("canvasFeedbackHighlightTimer")->isActive());
        QVERIFY(!feedback(window)->property("highlighted").toBool());
        QVERIFY(!QApplication::activeModalWidget());
        QVERIFY(window.canvas()->document()->history()->isClean());
    }
    void renderPreview() {
        const auto path = qEnvironmentVariable("TOON_UI_PREVIEW");
        if (path.isEmpty()) QSKIP("Preview output is opt-in");
        QTemporaryDir directory; MainWindow window(directory.filePath("workspace.ini"));
        QVERIFY(openDrawing(window, directory));
        hover(window.canvas());
        QTest::mouseClick(window.canvas(), Qt::LeftButton, Qt::NoModifier, window.canvas()->rect().center());
        QApplication::processEvents();
        QDir().mkpath(path);
        QVERIFY(window.grab().save(path + "/canvas-unavailable.png"));
        QVERIFY(window.statusBar()->grab().save(path + "/canvas-status.png"));
        QString error;
        QVERIFY(!ClipboardTools::copy(window.canvas()->document(), false, &error));
        QApplication::processEvents();
        QVERIFY(window.statusBar()->grab().save(path + "/canvas-warning.png"));
        QVERIFY(message(window)->width() > 300);
    }
};
QTEST_MAIN(CanvasFeedbackTest)
#include "test_canvas_feedback.moc"
