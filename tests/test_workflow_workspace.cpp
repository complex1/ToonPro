#include "main_window.h"
#include "canvas.h"
#include "history_panel.h"
#include "auto_actions_panel.h"
#include "auto_actions.h"
#include <QDockWidget>
#include <QAction>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTimer>
#include <QtTest>
using namespace toon;
class WorkflowWorkspaceTest:public QObject {
    Q_OBJECT
private slots:
    void historyNavigationAndWorkflowReplay() {
        QTemporaryDir dir;MainWindow window(dir.filePath("workspace.ini"));
        const auto path=dir.filePath("shot.toon");QString error;
        QVERIFY(saveProject(Project::create(),path,&error));QVERIFY(window.openProject(path));window.show();
        auto *doc=window.canvas()->document();
        auto *history=window.findChild<HistoryPanel*>();auto *panel=window.findChild<AutoActionsPanel*>();
        auto *actions=window.findChild<AutoActions*>();QVERIFY(history&&panel&&actions);
        for(const char *id:{"historyPanel","autoActionsPanel"}) {
            auto *dock=window.findChild<QDockWidget*>(id);QVERIFY(dock);QVERIFY(!dock->toggleViewAction()->text().isEmpty());
            dock->show();dock->raise();QVERIFY(dock->isVisible());
        }
        const auto action=actions->create("Ink setup");QVERIFY(actions->startRecording(action));
        doc->addLayer();doc->changeLayer("Ink",true,false,false,.65);actions->stopRecording();
        QCOMPARE(doc->history()->count(),2);history->requestHistoryIndex(0);
        QCOMPARE(doc->activeShot().layers.size(),1);history->requestHistoryIndex(2);
        QCOMPARE(doc->activeShot().layers.size(),2);
        doc->load(Project::create());panel->selectAction(action);panel->playRequested(action);
        QCOMPARE(doc->activeShot().layers.size(),2);QCOMPARE(doc->activeShot().layers[doc->activeLayer()].name,QString("Ink"));
        QCOMPARE(doc->activeShot().layers[doc->activeLayer()].opacity,.65);QCOMPARE(doc->history()->count(),1);
        history->requestHistoryIndex(0);QCOMPARE(doc->activeShot().layers.size(),1);
        doc->history()->setClean();
    }
    void recordFillTransformFillThroughWorkspace() {
        QTemporaryDir dir;MainWindow window(dir.filePath("workspace.ini"));
        auto project=Project::create();project.width=320;project.height=240;project.shots[0].sheet.overscan=0;
        window.canvas()->setProject(project);auto *doc=window.canvas()->document();
        QPainterPath selection;selection.addRect(40,40,30,20);doc->setRasterSelection(selection);
        auto *actions=window.findChild<AutoActions*>();const auto id=actions->create("Fill and place");QVERIFY(actions->startRecording(id));
        auto *fill=window.findChild<QAction*>("fillSelectionAction");QVERIFY(fill);QVERIFY(fill->isEnabled());
        auto brush=window.canvas()->brush;brush.color=Qt::red;window.canvas()->setBrushSettings(brush);fill->trigger();
        QVERIFY(doc->beginTransform());TransformParameters params;params.translation={25,15};QVERIFY(doc->previewTransform(params));QVERIFY(doc->applyTransform());
        brush.color=Qt::green;window.canvas()->setBrushSettings(brush);fill->trigger();actions->stopRecording();
        QCOMPARE(actions->action(id)->steps.size(),3);
        doc->load(project);QPainterPath target;target.addRect(100,100,50,40);doc->setRasterSelection(target);
        window.findChild<AutoActionsPanel*>()->playRequested(id);
        QCOMPARE(doc->history()->count(),1);QCOMPARE(doc->rasterSelection().path.boundingRect(),QRectF(125,115,50,40));
        QVERIFY(!doc->activeShot().layers[0].tiles.isEmpty());const auto finalTiles=doc->activeShot().layers[0].tiles;
        doc->history()->undo();QVERIFY(doc->activeShot().layers[0].tiles.isEmpty());QCOMPARE(doc->rasterSelection().path,target);
        doc->history()->redo();QCOMPARE(doc->activeShot().layers[0].tiles,finalTiles);
        doc->history()->setClean();
    }
    void activeStrokeCannotBeInterruptedByPanels() {
        QTemporaryDir dir;MainWindow window(dir.filePath("workspace.ini"));window.canvas()->setProject(Project::create());
        auto *doc=window.canvas()->document();doc->addLayer();
        auto *history=window.findChild<HistoryPanel*>();auto *panel=window.findChild<AutoActionsPanel*>();
        auto *actions=window.findChild<AutoActions*>();const auto id=actions->create("Layer");
        QVERIFY(actions->startRecording(id));doc->addLayer();actions->stopRecording();
        BrushSettings b;QVERIFY(doc->beginStroke({20,20},1,b,false));
        const int index=doc->history()->index();history->requestHistoryIndex(0);panel->playRequested(id);
        QVERIFY(doc->strokeActive());QCOMPARE(doc->history()->index(),index);
        doc->cancelStroke();doc->history()->setClean();
    }
    void pendingTransformCancelKeepsHistoryAndPreview() {
        QTemporaryDir dir;MainWindow window(dir.filePath("workspace.ini"));window.canvas()->setProject(Project::create());
        auto *doc=window.canvas()->document();BrushSettings b;b.size=30;
        QVERIFY(doc->beginStroke({80,80},1,b,false));doc->endStroke();
        QVERIFY(doc->beginTransform());auto params=doc->transformParameters();params.translation={30,15};doc->previewTransform(params);
        const int index=doc->history()->index();QTimer::singleShot(0,[] {
            if(auto *dialog=qobject_cast<QMessageBox*>(QApplication::activeModalWidget()))
                if(auto *cancel=dialog->findChild<QPushButton*>("cancelPendingTransform"))cancel->click();
        });
        window.findChild<HistoryPanel*>()->requestHistoryIndex(0);
        QCOMPARE(doc->history()->index(),index);QVERIFY(doc->transformActive());
        doc->cancelTransform();doc->history()->setClean();
    }
};
QTEST_MAIN(WorkflowWorkspaceTest)
#include "test_workflow_workspace.moc"
