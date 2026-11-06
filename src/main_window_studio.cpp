#include "main_window.h"
#include "canvas.h"
#include "clipboard_tools.h"
#include "drawing_tool_controller.h"
#include "icon_column.h"
#include "production_panel.h"
#include "project_assets_panel.h"
#include "project_home.h"
#include "project_save.h"
#include "quick_access_toolbar.h"
#include "studio_project.h"
#include "timeline_controller.h"
#include "movie_review_controller.h"
#include "motion_study_window.h"
#include "icons.h"
#include <QMenuBar>
#include <QMenu>
#include <QAction>
#include <QApplication>
#include <QDir>
#include <QDockWidget>
#include <QEventLoop>
#include <QFileDialog>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QImageReader>
#include <QMessageBox>
#include <QProgressDialog>
#include <QScopedValueRollback>
#include <QStackedWidget>
#include <QStatusBar>
#include <QtConcurrentRun>
#include <atomic>

namespace toon {
void MainWindow::createProductionWorkspace() {
    auto *studyAction=new QAction(appIcon("clapperboard"),"Motion Study…",this);
    studyAction->setObjectName("motionStudyAction");
    for(auto *menu:menuBar()->findChildren<QMenu *>())if(menu->title().remove('&')=="Panels"){menu->addAction(studyAction);break;}
    connect(studyAction,&QAction::triggered,this,[this]{
        auto *window=new MotionStudyWindow(this);window->show();
        connect(window,&MotionStudyWindow::insertRequested,this,[this,window](const MotionStudy &study,const QString &frames,bool reference){
            if(!m_canvas->hasProject()||!prepareStudioTransition()){showActionFeedback("Open a shot and finish its current edit before inserting a study.",true);return;}
            const auto generation=m_projectGeneration,revision=m_documentRevision;
            auto snapshot=m_canvas->project();const int shot=m_canvas->document()->activeShotIndex(),frame=m_canvas->document()->currentFrame();
            struct Result{Project project;QString error;};QFutureWatcher<Result> watcher;QEventLoop wait;
            QProgressDialog progress("Preparing motion-study cels…","Cancel",0,0,window);progress.setWindowModality(Qt::ApplicationModal);progress.setMinimumDuration(0);
            connect(&watcher,&QFutureWatcher<Result>::finished,&wait,&QEventLoop::quit);
            watcher.setFuture(QtConcurrent::run([snapshot,shot,frame,study,frames,reference]()mutable{Result r;r.project=std::move(snapshot);appendMotionStudy(&r.project,shot,frame,study,frames,reference,&r.error);return r;}));
            progress.show();if(!watcher.isFinished())wait.exec();const bool cancelled=progress.wasCanceled();progress.close();const auto result=watcher.result();
            if(cancelled)return;
            if(!result.error.isEmpty()){QMessageBox::warning(window,"Could not insert study",result.error);return;}
            if(generation!=m_projectGeneration||revision!=m_documentRevision){QMessageBox::warning(window,"Study not inserted","The project changed while preparing the study. Try again.");return;}
            if(!m_canvas->document()->insertMotionStudyProject(result.project,reference?"Insert motion reference":"Insert motion study cels")){QMessageBox::warning(window,"Study not inserted","Finish the active edit and try again.");return;}
            showDrawingWorkspacePage();
            if(!reference)m_canvas->document()->selectAnimationTrack(result.project.shots[shot].animationTracks.last().id);
            if(auto *timeline=findChild<QDockWidget *>("animationPanel")){timeline->show();timeline->raise();}
        });
    });
    m_production=new ProductionPanel(m_canvas->document(),[this]{return m_timeline->prepareTimelineEdit();},this);
    qobject_cast<QStackedWidget *>(centralWidget())->addWidget(m_production);
    connect(m_production,&ProductionPanel::drawingRequested,this,&MainWindow::showDrawingWorkspacePage);
    connect(m_production,&ProductionPanel::exportRequested,this,&MainWindow::exportShots);
    auto *assets=m_production->assetsPanel();
    connect(assets,&ProjectAssetsPanel::createPackageRequested,this,[this]{saveStudioCopy();});
    connect(assets,&ProjectAssetsPanel::placeImageRequested,this,&MainWindow::importArtworkFile);
    connect(assets,&ProjectAssetsPanel::placeAudioRequested,this,[this](const QString &path){
        if(!prepareStudioTransition())return;
        showDrawingWorkspacePage();
        if(!m_browsingStudio)m_timeline->importAudioFile(path);
    });
    connect(assets,&ProjectAssetsPanel::openShotRequested,this,[this](const QString &id){
        if(prepareStudioTransition()&&m_canvas->document()->selectShot(id))showDrawingWorkspacePage();
    });
    for(auto *dock:m_panels)connect(dock->toggleViewAction(),&QAction::triggered,this,[this,dock](bool checked){
        if(checked&&m_browsingStudio){
            showDrawingWorkspacePage();
            if(!m_browsingStudio){dock->show();dock->raise();}else dock->hide();
        }
    });
}
bool MainWindow::prepareStudioTransition() {
    if(m_studioWriting)return false;
    if(m_canvas->hasProject()&&m_canvas->document()->strokeActive()){
        showActionFeedback("Finish the stroke before changing workspace.",true);return false;
    }
    if(!resolveCelWorkflows())return false;
    if(m_canvas->hasProject()&&!m_timeline->prepareTimelineEdit())return false;
    return canChangeDrawingWorkspace();
}
void MainWindow::hideEditorPanels() {
    if(m_browsingStudio)return;
    if(m_drawingMode){setDrawingMode(false);if(m_drawingMode)return;}
    m_editorLayout=saveState(3);m_editorVisibility.clear();
    const auto remember=[this](QWidget *widget){if(widget)m_editorVisibility.append({widget,!widget->isHidden()});};
    for(auto *panel:m_panels)remember(panel);
    remember(m_toolboxColumn);remember(m_quickAccess);remember(m_drawingDrawer);
    m_browsingStudio=true;
    const QScopedValueRollback guard(m_arrangingWorkspace,true);
    setUpdatesEnabled(false);
    for(const auto &entry:m_editorVisibility)if(entry.first)entry.first->hide();
    refreshDockTabs();
    setUpdatesEnabled(true);
}
void MainWindow::restoreEditorPanels() {
    if(!m_browsingStudio)return;
    const QScopedValueRollback guard(m_arrangingWorkspace,true);
    setUpdatesEnabled(false);m_browsingStudio=false;
    restoreState(m_editorLayout,3);
    for(const auto &entry:m_editorVisibility)if(entry.first)entry.first->setVisible(entry.second);
    m_editorVisibility.clear();refreshDrawingDrawerVisibility();setUpdatesEnabled(true);
}
void MainWindow::showProjectHome() {
    if(m_managed)return;
    if(!prepareStudioTransition())return;
    hideEditorPanels();if(!m_browsingStudio)return;
    auto *home=qobject_cast<ProjectHome *>(m_welcome);
    home->setCurrentProject(m_canvas->hasProject()?m_canvas->project().movie:QString());home->refresh();
    qobject_cast<QStackedWidget *>(centralWidget())->setCurrentWidget(home);
    home->setFocus(Qt::OtherFocusReason);
}
void MainWindow::showProductionWorkspace() {
    if(m_managed)return;
    if(!m_canvas->hasProject()){showProjectHome();return;}
    if(!prepareStudioTransition())return;
    hideEditorPanels();if(!m_browsingStudio)return;
    qobject_cast<QStackedWidget *>(centralWidget())->setCurrentWidget(m_production);
}
void MainWindow::showDrawingWorkspacePage() {
    if(!m_canvas->hasProject()||!prepareStudioTransition())return;
    restoreEditorPanels();
    qobject_cast<QStackedWidget *>(centralWidget())->setCurrentWidget(m_canvas);
    refreshDrawingDrawerVisibility();m_canvas->setFocus(Qt::OtherFocusReason);
}
void MainWindow::rememberCurrentProject() {
    if(!m_canvas->hasProject()||m_path.isEmpty())return;
    auto *home=qobject_cast<ProjectHome *>(m_welcome);
    home->rememberProject(m_studioRoot.isEmpty()?m_path:m_studioRoot,m_canvas->project().movie,projectKindKey(m_canvas->project().kind));
    home->setCurrentProject(m_canvas->project().movie);
}
bool MainWindow::createStudioFolder(const Project &project,const QString &root,const QString &sourceRoot,QString *error) {
    const QScopedValueRollback writing(m_studioWriting,true);
    auto cancelled=std::make_shared<std::atomic_bool>(false);
    struct Result {bool saved=false;QString error;};
    QFutureWatcher<Result> watcher;
    QProgressDialog progress("Preparing project folder…","Cancel",0,0,this);
    progress.setObjectName("studioSaveProgress");progress.setWindowTitle("Project folder");
    progress.setWindowModality(Qt::WindowModal);progress.setMinimumDuration(0);
    QEventLoop wait;
    connect(&progress,&QProgressDialog::canceled,this,[cancelled]{cancelled->store(true);});
    connect(&watcher,&QFutureWatcher<Result>::finished,&wait,&QEventLoop::quit);
    watcher.setFuture(QtConcurrent::run([project,root,sourceRoot,cancelled]{
        Result result;result.saved=StudioProject::create(root,project,&result.error,sourceRoot,[cancelled]{return cancelled->load();});return result;
    }));
    progress.show();if(!watcher.isFinished())wait.exec();progress.close();
    const auto result=watcher.result();
    if(error)*error=result.error;
    return result.saved;
}
bool MainWindow::saveStudioCopy() {
    if(!m_canvas->hasProject()||!prepareStudioTransition())return false;
    auto target=QFileDialog::getSaveFileName(this,"Create a project folder",
        QDir(QFileInfo(m_path).absolutePath()).filePath(m_canvas->project().movie+".toonstudio"),"Toon Pro project folder (*.toonstudio)");
    if(target.isEmpty())return false;
    if(!target.endsWith(".toonstudio",Qt::CaseInsensitive))target+=".toonstudio";
    const auto generation=m_projectGeneration,revision=m_documentRevision;
    const auto snapshot=m_canvas->project();QString error;
    if(!createStudioFolder(snapshot,target,m_studioRoot,&error)){
        if(!error.contains("cancel",Qt::CaseInsensitive))QMessageBox::warning(this,"Could not create project folder",error);
        return false;
    }
    if(generation!=m_projectGeneration)return false;
    m_recoverySave->cancel();m_studioRoot=QFileInfo(target).absoluteFilePath();m_path=QDir(m_studioRoot).filePath("project.toonproj");
    const bool exact=revision==m_documentRevision;
    if(exact){m_canvas->document()->history()->setClean();m_canvas->document()->markDrawingAidsSaved();}
    else m_canvas->document()->history()->resetClean();
    m_dirty=!exact;m_production->setProjectRoot(m_studioRoot);rememberCurrentProject();updateTitle();
    statusBar()->showMessage("Project folder saved",5000);return exact;
}
void MainWindow::importArtworkFile(const QString &path) {
    if(!m_canvas->hasProject()||!prepareStudioTransition())return;
    QImageReader reader(path);reader.setAutoTransform(true);
    const auto size=reader.size();
    if(!size.isValid()||qint64(size.width())*size.height()>64000000){
        showActionFeedback("Choose an image of up to 64 million pixels.",true);return;
    }
    auto image=reader.read();
    if(image.isNull()){showActionFeedback("Could not read image: "+reader.errorString(),true);return;}
    const auto &project=m_canvas->project();const bool fitted=image.width()>project.width||image.height()>project.height;
    if(fitted)image=image.scaled(project.width,project.height,Qt::KeepAspectRatio,Qt::SmoothTransformation);
    const QPointF origin((project.width-image.width())/2.0,(project.height-image.height())/2.0);
    if(ClipboardTools::importImage(m_canvas->document(),image,QFileInfo(path).completeBaseName(),origin)){
        restoreEditorPanels();qobject_cast<QStackedWidget *>(centralWidget())->setCurrentWidget(m_canvas);
        m_canvas->document()->clearRasterSelection();m_canvas->setTool(Canvas::Tool::Transform);m_canvas->setFocus(Qt::OtherFocusReason);
        if(fitted)statusBar()->showMessage("Image fitted to canvas. Use the handles to position it.",5000);
    }
}
}
