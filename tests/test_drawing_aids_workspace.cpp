#include "canvas.h"
#include "document.h"
#include "light_table.h"
#include "cel_shading_panel.h"
#include "main_window.h"
#include "project_save.h"
#include "timeline_controller.h"
#include <QAction>
#include <QFile>
#include <QMessageBox>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolButton>
#include <QtTest>

using namespace toon;
namespace {
Project fixture() {
    auto project = Project::create(); project.width = 128; project.height = 128;
    return project;
}
LightTablePin pin() {
    LightTablePin result; result.id = "965ab912-fd80-47f0-bd69-4dd92bc6e8f1"; result.name = "Reference";
    result.image = QImage(32, 32, QImage::Format_ARGB32_Premultiplied); result.image.fill(Qt::red);
    result.bounds = QRectF(0, 0, 32, 32); return result;
}
}
class DrawingAidsWorkspaceTests : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { applyTheme(); }
    void metadataDirtyStateStaysIndependentOfArtworkUndo() {
        Document document; document.load(fixture());
        QSignalSpy changed(&document, &Document::drawingAidsChanged);
        auto aids = document.project().drawingAids; aids.lightTable.pins.append(pin());
        aids.lightTable.selectedPinId = aids.lightTable.pins[0].id;
        QVERIFY(document.setDrawingAids(aids)); QVERIFY(document.drawingAidsDirty());
        QCOMPARE(document.history()->count(), 0); QVERIFY(document.history()->isClean()); QCOMPARE(changed.size(), 1);
        QVERIFY(document.setDrawingAids(aids)); QCOMPARE(changed.size(), 1);
        document.markDrawingAidsSaved(); QVERIFY(!document.drawingAidsDirty());
        document.addLayer(); QVERIFY(!document.history()->isClean());
        aids.lightTable.pins[0].offset = {45, -12};
        QVERIFY(document.setDrawingAids(aids)); QVERIFY(document.drawingAidsDirty());
        document.history()->undo(); QVERIFY(document.history()->isClean()); QVERIFY(document.drawingAidsDirty());
        QCOMPARE(document.project().drawingAids.lightTable.pins[0].offset, QPointF(45, -12));
        document.history()->redo(); QCOMPARE(document.project().drawingAids.lightTable.pins[0].offset, QPointF(45, -12));
        auto invalid = aids; invalid.lightTable.pins[0].scale = -1; QString error;
        QVERIFY(!document.setDrawingAids(invalid, &error)); QVERIFY(!error.isEmpty());
        QCOMPARE(document.project().drawingAids.lightTable.pins[0].scale, qreal(1));
    }
    void loadingResumesShotAndFrameWithoutDirtyingProject() {
        auto project = fixture(); auto second = Project::create().shots[0]; project.shots.append(second);
        project.drawingAids.lightTable.pins.append(pin()); project.drawingAids.lightTable.selectedPinId = "965ab912-fd80-47f0-bd69-4dd92bc6e8f1";
        project.drawingAids.activeShotId = second.id; project.drawingAids.activeFrame = 17;
        Canvas canvas; canvas.setProject(project);
        QCOMPARE(canvas.document()->activeShotId(), second.id); QCOMPARE(canvas.document()->currentFrame(), 17);
        QVERIFY(!canvas.document()->drawingAidsDirty()); QVERIFY(canvas.document()->history()->isClean());
        canvas.setProject(fixture()); QVERIFY(canvas.project().drawingAids.lightTable.pins.isEmpty());
        QCOMPARE(canvas.document()->currentFrame(), 0);
    }
    void onlyPinChangesPromptOnCloseAndSaveWithNoArtworkEdits() {
        QTemporaryDir directory; QString error; const auto path = directory.filePath("pins.toonproj");
        QVERIFY2(saveProject(fixture(), path, &error), qPrintable(error));
        MainWindow window(directory.filePath("workspace.ini")); QVERIFY(window.openProject(path));
        auto *panel = window.findChild<LightTablePanel *>(); QVERIFY(panel);
        QVERIFY(panel->pinImage(pin().image, "Saved reference"));
        QVERIFY(panel->setPinTransform(0, {22, -8}, 1.25, 32));
        QVERIFY(window.isWindowModified()); QCOMPARE(window.canvas()->document()->history()->count(), 0);
        int questions = 0; QTimer answer; answer.setInterval(5);
        connect(&answer, &QTimer::timeout, [&] {
            if (auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) {
                ++questions; if (auto *button = box->button(QMessageBox::Cancel)) button->click();
            }
        });
        answer.start(); QVERIFY(!window.close()); answer.stop(); QCOMPARE(questions, 1);
        QVERIFY(window.isWindowModified()); QCOMPARE(panel->pins().size(), 1);
        auto *save = window.findChild<QAction *>("saveProjectAction"); QVERIFY(save); save->trigger();
        QVERIFY(!window.isWindowModified()); QVERIFY(!window.canvas()->document()->drawingAidsDirty());
        const auto saved = loadProject(path, &error); QVERIFY2(saved.has_value(), qPrintable(error));
        QCOMPARE(saved->shots[0].layers.size(), 1); QVERIFY(saved->shots[0].layers[0].tiles.isEmpty());
        QCOMPARE(saved->drawingAids.lightTable.pins.size(), 1);
        QCOMPARE(saved->drawingAids.lightTable.pins[0].offset, QPointF(22, -8));
        QVERIFY(window.openProject(path)); QCOMPARE(panel->pins().size(), 1);
        QCOMPARE(panel->pins()[0].rotation, qreal(32)); QVERIFY(!window.isWindowModified());
        QVERIFY(window.close());
    }
    void removingAllNewAidsClearsStaleRecoveryAndReturnsToCleanState() {
        QTemporaryDir directory; QString error; const auto path = directory.filePath("removed-aids.toonproj");
        QVERIFY(saveProject(fixture(), path, &error));
        MainWindow window(directory.filePath("workspace.ini")); QVERIFY(window.openProject(path));
        auto *light = window.findChild<LightTablePanel *>(); auto *shading = window.findChild<CelShadingPanel *>();
        auto *writer = window.findChild<ProjectSave *>("recoveryWriter");
        auto *timer = window.findChild<QTimer *>("recoveryTimer"); QVERIFY(light && shading && writer && timer);
        QSignalSpy finished(writer, &ProjectSave::finished);
        QVERIFY(light->pinImage(pin().image, "Temporary pin"));
        QVERIFY(QMetaObject::invokeMethod(timer, "timeout", Qt::DirectConnection));
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 10000); QVERIFY(QFile::exists(path + ".recovery"));
        light->removeSelected();
        QVERIFY(!window.isWindowModified()); QVERIFY(!QFile::exists(path + ".recovery"));
        QPainterPath boundary(QPointF(20, 20)); boundary.lineTo(90, 80);
        QVERIFY(shading->addBoundary(boundary, CelShadeKind::Shadow));
        QVERIFY(QMetaObject::invokeMethod(timer, "timeout", Qt::DirectConnection));
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 2, 10000); QVERIFY(QFile::exists(path + ".recovery"));
        shading->cancelEditing();
        QVERIFY(!window.isWindowModified()); QVERIFY(!QFile::exists(path + ".recovery"));
        QVERIFY(!window.canvas()->document()->drawingAidsDirty());
        QVERIFY(window.canvas()->project().drawingAids.activeShotId.isEmpty());
        QCOMPARE(window.canvas()->document()->history()->count(), 0);
    }
    void recoveryContainsCompletedPinsAndUnappliedGuides() {
        QTemporaryDir directory; QString error; const auto path = directory.filePath("recovery.toonproj");
        QVERIFY(saveProject(fixture(), path, &error));
        MainWindow window(directory.filePath("workspace.ini")); QVERIFY(window.openProject(path));
        auto *light = window.findChild<LightTablePanel *>(); auto *shading = window.findChild<CelShadingPanel *>();
        QVERIFY(light && shading); QVERIFY(light->pinImage(pin().image, "Recovery reference"));
        QPainterPath boundary(QPointF(20, 20)); boundary.cubicTo(40, 25, 60, 65, 95, 80);
        QVERIFY(shading->addBoundary(boundary, CelShadeKind::Highlight));
        auto *timeline = window.findChild<TimelineController *>(); QVERIFY(timeline);
        timeline->setFrame(5); QCOMPARE(window.canvas()->document()->currentFrame(), 5);
        auto *writer = window.findChild<ProjectSave *>("recoveryWriter");
        auto *timer = window.findChild<QTimer *>("recoveryTimer"); QVERIFY(writer && timer);
        QSignalSpy finished(writer, &ProjectSave::finished);
        QVERIFY(QMetaObject::invokeMethod(timer, "timeout", Qt::DirectConnection));
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 10000); QVERIFY(finished[0][0].toBool());
        const auto recovered = loadProject(path + ".recovery", &error); QVERIFY2(recovered.has_value(), qPrintable(error));
        QCOMPARE(recovered->drawingAids.lightTable.pins.size(), 1); QCOMPARE(recovered->drawingAids.shadingDrafts.size(), 1);
        QCOMPARE(recovered->drawingAids.shadingDrafts[0].boundaries[0].path, boundary);
        QCOMPARE(recovered->drawingAids.shadingDrafts[0].frame, 0);
        QCOMPARE(recovered->drawingAids.activeFrame, 5);
        QVERIFY(recovered->shots[0].layers[0].tiles.isEmpty());
        // Recovery is a snapshot, not an explicit save/clean operation.
        QVERIFY(window.isWindowModified()); QVERIFY(window.canvas()->document()->drawingAidsDirty());
    }
};
QTEST_MAIN(DrawingAidsWorkspaceTests)
#include "test_drawing_aids_workspace.moc"
