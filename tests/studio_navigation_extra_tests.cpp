#include "canvas.h"
#include "document.h"
#include "main_window.h"
#include "production_panel.h"
#include "ui_theme.h"
#include <QAction>
#include <QDockWidget>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QStackedWidget>
#include <QTemporaryDir>
#include <QtTest>

using namespace toon;
namespace {
void settle() {
    QCoreApplication::processEvents();
    QCoreApplication::sendPostedEvents(nullptr,QEvent::LayoutRequest);
}
}
class StudioNavigationExtraTests final:public QObject {
    Q_OBJECT
private slots:
    void initTestCase(){applyTheme();}
    void openingAPanelFromProductionRestoresCanvasAndRequestedPanel() {
        QTemporaryDir directory;QString error;auto project=Project::create();project.width=320;project.height=240;
        const auto path=directory.filePath("film.toonproj");QVERIFY2(saveProject(project,path,&error),qPrintable(error));
        MainWindow window(directory.filePath("workspace.ini"));window.resize(1200,800);window.show();
        QVERIFY(window.openProject(path));QVERIFY(window.applyWorkspace("illustration"));settle();
        auto *effects=window.findChild<QDockWidget *>("effectsPanel");QVERIFY(effects);effects->hide();
        auto *layers=window.findChild<QDockWidget *>("layersPanel");QVERIFY(layers);QVERIFY(!layers->isHidden());
        const auto before=window.canvas()->project().toJson();const auto history=window.canvas()->document()->history()->count();
        window.showProductionWorkspace();settle();
        auto *stack=qobject_cast<QStackedWidget *>(window.centralWidget());QVERIFY(stack);
        QCOMPARE(stack->currentWidget(),window.findChild<ProductionPanel *>());QVERIFY(effects->isHidden());QVERIFY(layers->isHidden());
        effects->toggleViewAction()->trigger();settle();
        QCOMPARE(stack->currentWidget(),window.canvas());QVERIFY(!effects->isHidden());QVERIFY(!layers->isHidden());
        QCOMPARE(window.canvas()->project().toJson(),before);QCOMPARE(window.canvas()->document()->history()->count(),history);
        QVERIFY(window.close());
    }
    void staleNotesAfterUndoCanBeRevertedWithoutOverwritingTheDocument() {
        auto project=Project::create(ProjectKind::Series);project.shots[0].productionNotes="Original plan";
        Document document;document.load(project);ProductionPanel panel(&document,[]{return true;});panel.show();settle();
        const auto id=document.activeShotId();
        QVERIFY(document.setShotProduction(id,"Episode 01","layout","Revised plan"));
        auto *notes=panel.findChild<QPlainTextEdit *>("productionNotes");QVERIFY(notes);
        QCOMPARE(notes->toPlainText(),QString("Revised plan"));
        notes->setPlainText("Unfinished replacement");
        document.history()->undo();
        QCOMPARE(document.activeShot().productionNotes,QString("Original plan"));
        QVERIFY(!panel.finishEditing());QCOMPARE(notes->toPlainText(),QString("Unfinished replacement"));
        const auto unchanged=document.project().toJson();const int index=document.history()->index(),count=document.history()->count();
        auto *revert=panel.findChild<QPushButton *>("productionRevertNotes");QVERIFY(revert);revert->click();
        QVERIFY(panel.finishEditing());QCOMPARE(notes->toPlainText(),QString("Original plan"));
        QCOMPARE(document.project().toJson(),unchanged);QCOMPARE(document.history()->index(),index);QCOMPARE(document.history()->count(),count);
        QCOMPARE(panel.findChild<QLabel *>("productionStatus")->text(),QString());
        notes->setPlainText("New deliberate plan");QVERIFY(panel.finishEditing());
        QCOMPARE(document.activeShot().productionNotes,QString("New deliberate plan"));
        document.history()->undo();QCOMPARE(document.activeShot().productionNotes,QString("Original plan"));
    }
};
QTEST_MAIN(StudioNavigationExtraTests)
#include "studio_navigation_extra_tests.moc"
