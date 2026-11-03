#include "canvas.h"
#include "cel_paint_check_panel.h"
#include "cel_shading_panel.h"
#include "light_table.h"
#include "main_window.h"
#include "reference_panel.h"
#include "timeline_controller.h"
#include "icons.h"
#include <QAction>
#include <QDockWidget>
#include <QMessageBox>
#include <QPainter>
#include <QTabBar>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolButton>
#include <QtTest>

using namespace toon;

class CelWorkflowsTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { applyTheme(); }

    void panelsAndReferenceTabAreReachableWithoutChangingArtwork() {
        QTemporaryDir directory; MainWindow window(directory.filePath("workspace.ini"));
        auto project = Project::create(); project.width = 320; project.height = 240;
        window.canvas()->setProject(project);
        const auto before = window.canvas()->project().toJson();
        for (const auto *id : {"celPaintCheckPanel", "celShadingPanel"}) {
            auto *dock = window.findChild<QDockWidget *>(id); QVERIFY(dock);
            QVERIFY(!dock->windowIcon().isNull()); QVERIFY(!dock->accessibleName().isEmpty());
            QVERIFY(!dock->toggleViewAction()->text().isEmpty());
        }
        auto *references = window.findChild<ReferencePanel *>(); QVERIFY(references);
        auto *tabs = references->findChild<QTabBar *>("referenceTabs"); QVERIFY(tabs);
        QCOMPARE(tabs->count(), 4); QVERIFY(references->lightTable());
        QCOMPARE(tabs->accessibleTabName(3), QString("Light Table and Shift-and-Trace"));
        for (int index = 0; index < tabs->count(); ++index) {
            tabs->setCurrentIndex(index); QVERIFY(!tabs->tabToolTip(index).isEmpty());
        }
        QCOMPARE(window.canvas()->project().toJson(), before);
        QCOMPARE(window.canvas()->document()->history()->count(), 0);
        for (const auto *name : {"toon-paint-check", "toon-cel-shading", "toon-light-table"})
            QVERIFY(!appIcon(name).isNull());
    }

    void cancelledPreviewGuardKeepsFrameAndPlaybackUnchanged() {
        Canvas canvas; auto project = Project::create(); project.width = 320; project.height = 240;
        canvas.setProject(project); TimelineController timeline(&canvas);
        const auto before = canvas.project().toJson();
        int calls = 0;
        timeline.setTransitionGuard([&] { ++calls; return false; });
        QVERIFY(!timeline.setFrame(5)); QCOMPARE(canvas.document()->currentFrame(), 0);
        timeline.setPlaying(true); QVERIFY(!timeline.playing());
        QVERIFY(!timeline.prepareTimelineEdit()); QCOMPARE(calls, 3);
        QCOMPARE(canvas.project().toJson(), before);
        QCOMPARE(canvas.document()->history()->count(), 0);
        timeline.setTransitionGuard([] { return true; });
        QVERIFY(timeline.setFrame(5)); QCOMPARE(canvas.document()->currentFrame(), 5);
    }

    void referenceAndBoundaryEditingAreMutuallyExclusive() {
        QTemporaryDir directory; MainWindow window(directory.filePath("workspace.ini"));
        auto project = Project::create(); project.width = 160; project.height = 160;
        window.canvas()->setProject(project); window.resize(1100, 780); window.show();
        auto *shading = window.findChild<CelShadingPanel *>();
        auto *light = window.findChild<LightTablePanel *>(); QVERIFY(shading && light);
        auto *refs = window.findChild<QDockWidget *>("referencesPanel");
        auto *shadeDock = window.findChild<QDockWidget *>("celShadingPanel");
        refs->setFloating(true); refs->show(); refs->findChild<QTabBar *>("referenceTabs")->setCurrentIndex(3);
        shadeDock->setFloating(true); shadeDock->show();
        QImage reference(40, 40, QImage::Format_ARGB32_Premultiplied); reference.fill(Qt::red);
        QVERIFY(light->pinImage(reference, "Pose"));
        shading->setActive(true); QVERIFY(shading->active());
        light->setEditing(true); QVERIFY(light->editing()); QVERIFY(!shading->active());
        QCOMPARE(window.canvas()->cursor().shape(), Qt::OpenHandCursor);
        shading->setActive(true); QVERIFY(shading->active()); QVERIFY(!light->editing());
        QCOMPARE(window.canvas()->cursor().shape(), Qt::CrossCursor);
        shadeDock->hide(); QVERIFY(!shading->active());
        QCOMPARE(window.canvas()->document()->history()->count(), 0);
        window.hide();
    }

    void shadingDraftSurvivesSaveAndFrameChange() {
        QTemporaryDir directory;
        auto project = Project::create(); project.width = 160; project.height = 160;
        QImage ink(tileSize, tileSize, QImage::Format_ARGB32_Premultiplied); ink.fill(Qt::transparent);
        { QPainter painter(&ink); painter.setPen(QPen(Qt::black, 2)); painter.drawRect(QRect(20, 20, 120, 120)); }
        project.shots[0].layers[0].tiles[{0, 0}] = ink;
        const auto path = directory.filePath("shading.toonproj"); QString error;
        QVERIFY2(saveProject(project, path, &error), qPrintable(error));
        MainWindow window(directory.filePath("workspace.ini")); QVERIFY(window.openProject(path));
        auto *shading = window.findChild<CelShadingPanel *>(); QVERIFY(shading);
        auto *timeline = window.findChild<TimelineController *>(); QVERIFY(timeline);
        auto *save = window.findChild<QAction *>("saveProjectAction"); QVERIFY(save);
        auto *document = window.canvas()->document();
        shading->setOutlineLayerId(document->activeShot().layers[0].id);
        QPainterPath boundary(QPointF(80, 20)); boundary.lineTo(80, 140);
        QVERIFY(shading->addBoundary(boundary, CelShadeKind::Shadow));
        QVERIFY(shading->addFill({40, 70}, CelShadeKind::Shadow, QColor("#77597e")));
        QVERIFY(shading->pending()); QVERIFY(document->drawingAidsDirty());
        QCOMPARE(document->history()->count(), 0);
        QVERIFY(timeline->setFrame(5));
        QCOMPARE(document->project().drawingAids.shadingDrafts.size(), 1);
        QVERIFY(timeline->setFrame(0)); QVERIFY(shading->pending());
        save->trigger(); QVERIFY(!document->drawingAidsDirty());
        QVERIFY(shading->pending()); QCOMPARE(document->history()->count(), 0);
        const auto saved = loadProject(path, &error); QVERIFY2(saved.has_value(), qPrintable(error));
        QCOMPARE(saved->shots[0].layers.size(), 1);
        QCOMPARE(saved->shots[0].layers.last().tiles.value(TileKey(0, 0)), ink);
        QCOMPARE(saved->drawingAids.shadingDrafts.size(), 1);
        QVERIFY(window.openProject(path)); QVERIFY(shading->pending());
        QCOMPARE(shading->boundaries().size(), 1); QCOMPARE(shading->fills().size(), 1);
        QVERIFY(!document->drawingAidsDirty());
        // Saving a draft never silently bakes it into finished artwork.
        QCOMPARE(document->activeShot().layers.size(), 1);
    }
};
QTEST_MAIN(CelWorkflowsTest)
#include "test_cel_workflows.moc"
