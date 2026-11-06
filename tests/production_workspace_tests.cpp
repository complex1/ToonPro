#include "canvas.h"
#include "document.h"
#include "main_window.h"
#include "production_panel.h"
#include "project_assets_panel.h"
#include "project_home.h"
#include "project_dialog.h"
#include "studio_project.h"
#include "shots_panel.h"
#include "ui_theme.h"
#include <QAction>
#include <QComboBox>
#include <QDir>
#include <QDockWidget>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QStackedWidget>
#include <QTabWidget>
#include <QTabBar>
#include <QTemporaryDir>
#include <QTreeWidget>
#include <QtTest>
using namespace toon;
namespace {
void settle(){QCoreApplication::processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::LayoutRequest);}
void clean(MainWindow &window){window.canvas()->document()->history()->setClean();window.canvas()->document()->markDrawingAidsSaved();}
Project small(ProjectKind kind){
    auto project=Project::create(kind);project.movie="Production example";project.width=320;project.height=240;return project;
}
void capture(QWidget &widget,const QString &name){
    const auto root=qEnvironmentVariable("TOON_STUDIO_CAPTURE");if(root.isEmpty())return;
    QDir().mkpath(root);settle();widget.grab().save(QDir(root).filePath(name+".png"));
}
}
class ProductionWorkspaceTests:public QObject{
    Q_OBJECT
private slots:
    void initTestCase(){applyTheme();}
    void productionMetadataUndoAndRejection(){
        Document document;auto project=small(ProjectKind::Series);document.load(project);
        const auto id=document.activeShotId();
        QVERIFY(document.setShotProduction(id,"Episode 02","layout","Camera tracks across the station."));
        QCOMPARE(document.activeShot().episode,QString("Episode 02"));QCOMPARE(document.history()->count(),1);
        document.history()->undo();QCOMPARE(document.activeShot().productionNotes,QString());
        document.history()->redo();QCOMPARE(document.activeShot().productionStage,QString("layout"));
        QVERIFY(!document.setShotProduction(id,"Episode 02","invented",{}));QCOMPARE(document.history()->count(),1);
        const auto next=document.createShot();QVERIFY(!next.isEmpty());QCOMPARE(document.activeShot().episode,QString("Episode 02"));
    }
    void drawingArtworksStayStatic(){
        Document document;document.load(small(ProjectKind::DrawingOnly));
        const auto next=document.createShot();QVERIFY(!next.isEmpty());
        QVERIFY(document.activeShot().number.startsWith('D'));QCOMPARE(document.activeShot().durationFrames,1);
        QVERIFY(document.activeShot().animationTracks.isEmpty());QVERIFY(document.activeShot().camera2DKeys.isEmpty());
    }
    void filmPackageNavigationRetainsLayoutAndEdits(){
        QTemporaryDir directory;QString error;const auto root=directory.filePath("Film.toonstudio");
        QVERIFY2(StudioProject::create(root,small(ProjectKind::Film),&error),qPrintable(error));
        MainWindow window(directory.filePath("workspace.ini"));window.resize(1200,800);window.show();settle();
        capture(window,"project-home");
        for(auto *tabs:window.findChildren<QTabBar *>(QString(),Qt::FindDirectChildrenOnly))QVERIFY(!tabs->isVisible());
        QVERIFY(window.openProject(root));
        auto *stack=qobject_cast<QStackedWidget *>(window.centralWidget());auto *production=window.findChild<ProductionPanel *>();
        QVERIFY(production);QCOMPARE(stack->currentWidget(),production);
        QVERIFY(window.findChild<QDockWidget *>("layersPanel")->isHidden());
        auto *notes=production->findChild<QPlainTextEdit *>("productionNotes");notes->setPlainText("Background: warm window light.");
        window.showDrawingWorkspacePage();QCOMPARE(stack->currentWidget(),window.canvas());
        QCOMPARE(window.canvas()->shot().productionNotes,QString("Background: warm window light."));
        auto *layers=window.findChild<QDockWidget *>("layersPanel");QVERIFY(!layers->isHidden());
        window.canvas()->setBrushSize(37);window.canvas()->setRotation(17);
        const int history=window.canvas()->document()->history()->count();
        window.showProductionWorkspace();QCOMPARE(stack->currentWidget(),production);
        capture(window,"film-project");
        window.showProjectHome();QVERIFY(qobject_cast<ProjectHome *>(stack->currentWidget()));
        QVERIFY(!window.findChild<QTreeWidget *>("homeProjects")->findItems("Production example",Qt::MatchContains,0).isEmpty());
        window.showDrawingWorkspacePage();QCOMPARE(window.canvas()->brush.size,37.0);
        QCOMPARE(window.canvas()->rotation(),17.0);QCOMPARE(window.canvas()->document()->history()->count(),history);
        QVERIFY(!layers->isHidden());
        window.findChild<QAction *>("saveProjectAction")->trigger();
        const auto saved=loadProject(QDir(root).filePath("project.toonproj"),&error);QVERIFY2(saved,qPrintable(error));
        QCOMPARE(saved->shots[0].productionNotes,QString("Background: warm window light."));
        window.showProjectHome();QVERIFY(window.close());
        MainWindow reopened(directory.filePath("workspace.ini"));reopened.show();QVERIFY(reopened.openProject(root));
        reopened.showDrawingWorkspacePage();QVERIFY(!reopened.findChild<QDockWidget *>("layersPanel")->isHidden());QVERIFY(reopened.close());
    }
    void drawingPackageOpensCanvasAndImageAssetsArePlaceable(){
        QTemporaryDir directory;QString error;const auto root=directory.filePath("Backgrounds.toonstudio");
        QVERIFY2(StudioProject::create(root,small(ProjectKind::DrawingOnly),&error),qPrintable(error));
        MainWindow window(directory.filePath("workspace.ini"));window.resize(1200,800);window.show();QVERIFY(window.openProject(root));
        auto *stack=qobject_cast<QStackedWidget *>(window.centralWidget());QCOMPARE(stack->currentWidget(),window.canvas());
        QCOMPARE(window.activeWorkspaceId(),QString("illustration"));
        QVERIFY(window.findChild<QDockWidget *>("animationPanel")->isHidden());
        QVERIFY(window.findChild<QDockWidget *>("framePropertiesPanel")->isHidden());
        window.showProductionWorkspace();auto *production=window.findChild<ProductionPanel *>();production->showAssets();
        QImage image(32,32,QImage::Format_ARGB32_Premultiplied);image.fill(QColor("#b45e36"));const auto path=directory.filePath("wall.png");QVERIFY(image.save(path));
        auto *assets=production->assetsPanel();QSignalSpy imported(assets,&ProjectAssetsPanel::importFinished);
        assets->importFiles({path});QTRY_COMPARE_WITH_TIMEOUT(imported.count(),1,10000);QVERIFY(imported[0][0].toBool());
        auto *list=assets->findChild<QTreeWidget *>("projectAssetsList");QCOMPARE(list->topLevelItemCount(),1);list->setCurrentItem(list->topLevelItem(0));settle();
        capture(window,"drawing-assets");
        const auto source=StudioProject::open(root,&error);QVERIFY(source);
        const int layers=window.canvas()->shot().layers.size();
        QVERIFY(QMetaObject::invokeMethod(assets,"placeImageRequested",Qt::DirectConnection,Q_ARG(QString,source->assetPath(source->assets()[0].id))));
        QCOMPARE(stack->currentWidget(),window.canvas());QCOMPARE(window.canvas()->shot().layers.size(),layers+1);
        QVERIFY(window.canvas()->document()->transformActive());
        window.canvas()->document()->cancelTransform();clean(window);QVERIFY(window.close());
    }
    void episodeFilterReconcilesAfterRenameAndDraftCanRevert(){
        QTemporaryDir directory;QString error;auto project=small(ProjectKind::Series);project.shots[0].episode="Episode 01";
        const auto root=directory.filePath("Series.toonstudio");QVERIFY(StudioProject::create(root,project,&error));
        MainWindow window(directory.filePath("workspace.ini"));window.show();QVERIFY(window.openProject(root));
        auto *production=window.findChild<ProductionPanel *>();auto *filter=production->findChild<QComboBox *>("productionEpisodeFilter");
        filter->setCurrentIndex(filter->findData("Episode 01"));
        auto *episode=production->findChild<QLineEdit *>("productionEpisode");episode->setFocus();episode->selectAll();QTest::keyClicks(episode,"Episode 02");QTest::keyClick(episode,Qt::Key_Return);settle();
        QCOMPARE(filter->currentData().toString(),QString());
        auto *list=production->findChild<ShotsPanel *>()->findChild<QTreeWidget *>();QVERIFY(list);QVERIFY(list->topLevelItem(0));QVERIFY(!list->topLevelItem(0)->isHidden());
        auto *notes=production->findChild<QPlainTextEdit *>("productionNotes");notes->setPlainText(QString(16001,'x'));
        QVERIFY(!production->finishEditing());production->findChild<QPushButton *>("productionRevertNotes")->click();QVERIFY(production->finishEditing());
        clean(window);QVERIFY(window.close());
    }
};
QTEST_MAIN(ProductionWorkspaceTests)
#include "production_workspace_tests.moc"
