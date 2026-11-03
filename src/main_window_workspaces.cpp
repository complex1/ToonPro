#include "main_window.h"
#include "canvas.h"
#include "document.h"
#include "icon_column.h"
#include "icons.h"
#include "quick_access_toolbar.h"
#include "timeline_controller.h"
#include "movie_review_controller.h"
#include "workspace_dialog.h"
#include <QAction>
#include <QApplication>
#include <QDockWidget>
#include <QDialog>
#include <QDialogButtonBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QMenu>
#include <QSettings>
#include <QSignalBlocker>
#include <QTabBar>
#include <QStackedWidget>
#include <QToolButton>
#include <QVBoxLayout>
#include <algorithm>

namespace toon {
namespace {
struct Builtin { const char *id, *name, *objectName, *help; };
constexpr Builtin builtins[] = {
    {"illustration","Illustration","drawingWorkspacePresetAction","Large canvas with drawing tools, sizes, colors and layers"},
    {"anime","Anime","animeWorkspacePresetAction","Drawing controls and references beside the canvas, with Timeline below"},
    {"storyboard","Storyboarding","storyboardWorkspacePresetAction","Organize shots beside a large drawing canvas and references"},
    {"animatic","Animatics","animaticWorkspacePresetAction","Shot organization, a taller Timeline and frame, audio and camera controls"},
    {"compact","Compact drawing","compactDrawingWorkspaceAction","Open drawing controls in a drawer without shrinking the canvas"}
};
bool builtin(const QString &id) { for(const auto &entry:builtins)if(id==entry.id)return true;return false; }
QString builtinName(const QString &id) { for(const auto &entry:builtins)if(id==entry.id)return entry.name;return {}; }
}

void MainWindow::restoreInspectorPage(const QString &page) {
    auto *tabs=findChild<QTabBar *>("framePropertiesTabs");
    auto *pages=findChild<QStackedWidget *>("framePropertiesPages");
    if(!tabs||!pages)return;
    // Restoring a view must not activate the camera or clear the artist's
    // timeline selection as a deliberate inspector-tab click would.
    for(int i=0;i<tabs->count();++i)if(tabs->tabData(i).toString()==page) {
        const QSignalBlocker blocked(tabs);tabs->setCurrentIndex(i);pages->setCurrentIndex(i);return;
    }
}

void MainWindow::createWorkspaceMenu(QMenu *view,QToolBar *toolbar) {
    m_workspaceMenu=view->addMenu("Workspace");m_workspaceMenu->setObjectName("workspaceMenu");
    for(const auto &entry:builtins) {
        auto *action=m_workspaceMenu->addAction(appIcon("panels-top-left"),entry.name);
        action->setObjectName(entry.objectName);action->setData(QString(entry.id));action->setCheckable(true);action->setToolTip(entry.help);
        connect(action,&QAction::triggered,this,[this,id=QString(entry.id)]{applyWorkspace(id);});
    }
    m_workspaceSavedSeparator=m_workspaceMenu->addSeparator();
    const auto action=[&](const char *text,const char *id,const char *icon,const char *help){
        auto *result=m_workspaceMenu->addAction(appIcon(icon),text);result->setObjectName(id);result->setToolTip(help);return result;
    };
    auto *save=action("Save workspace as…","saveWorkspaceAsAction","save","Save the current panel layout as a named workspace");
    connect(save,&QAction::triggered,this,&MainWindow::saveWorkspaceDialog);
    auto *update=action("Update saved workspace","updateWorkspaceAction","save","Replace this custom workspace's saved layout with the current arrangement");
    connect(update,&QAction::triggered,this,[this]{QString error;if(!updateCurrentWorkspace(&error)&&!error.isEmpty())showActionFeedback(error,true);});
    auto *reload=action("Reload workspace","reloadWorkspaceAction","rotate-ccw","Restore the current workspace's saved arrangement");
    connect(reload,&QAction::triggered,this,[this]{applyWorkspace(m_activeWorkspaceId);});
    auto *previous=action("Previous layout","previousWorkspaceAction","undo-2","Return to the panel arrangement before the last workspace switch");
    connect(previous,&QAction::triggered,this,&MainWindow::restorePreviousWorkspace);
    auto *manage=action("Manage workspaces…","manageWorkspacesAction","settings-2","Use, rename or delete custom workspaces");
    connect(manage,&QAction::triggered,this,&MainWindow::manageWorkspaces);
    auto *menuAction=toolbar->addAction(appIcon("panels-top-left"),"Workspace");
    menuAction->setObjectName("drawingWorkspaceMenuAction");menuAction->setMenu(m_workspaceMenu);
    if(auto *button=qobject_cast<QToolButton *>(toolbar->widgetForAction(menuAction)))button->setPopupMode(QToolButton::InstantPopup);
    connect(m_workspaceMenu,&QMenu::aboutToShow,this,&MainWindow::refreshWorkspaceMenu);
    connect(m_workspaces.get(),&WorkspaceLibrary::changed,this,[this]{
        if(!builtin(m_activeWorkspaceId)&&!m_workspaces->find(m_activeWorkspaceId))m_activeWorkspaceId.clear();
        refreshWorkspaceMenu();
    });
    refreshWorkspaceMenu();
}

void MainWindow::refreshWorkspaceMenu() {
    if(!m_workspaceMenu)return;
    for(auto *action:m_savedWorkspaceActions){m_workspaceMenu->removeAction(action);action->deleteLater();}
    m_savedWorkspaceActions.clear();
    for(const auto &entry:m_workspaces->entries()) {
        auto *action=new QAction(entry.name,m_workspaceMenu);action->setData(entry.id);action->setCheckable(true);
        action->setToolTip("Use saved workspace: "+entry.name);action->setObjectName("savedWorkspace_"+entry.id);
        m_workspaceMenu->insertAction(m_workspaceSavedSeparator,action);m_savedWorkspaceActions.append(action);
        connect(action,&QAction::triggered,this,[this,id=entry.id]{applyWorkspace(id);});
    }
    const bool known=builtin(m_activeWorkspaceId)||m_workspaces->find(m_activeWorkspaceId);
    const auto name=builtin(m_activeWorkspaceId)?builtinName(m_activeWorkspaceId):known?m_workspaces->find(m_activeWorkspaceId)->name:QString("Current layout");
    for(auto *action:m_workspaceMenu->actions())if(action->isCheckable())action->setChecked(action->data().toString()==m_activeWorkspaceId);
    findChild<QAction *>("saveWorkspaceAsAction")->setEnabled(m_workspaces->writable());
    findChild<QAction *>("updateWorkspaceAction")->setEnabled(m_workspaces->writable()&&m_workspaces->find(m_activeWorkspaceId));
    findChild<QAction *>("reloadWorkspaceAction")->setEnabled(known);
    findChild<QAction *>("reloadWorkspaceAction")->setToolTip("Reload "+name+"; current panel changes remain available through Previous layout");
    findChild<QAction *>("previousWorkspaceAction")->setEnabled(m_previousWorkspace.has_value());
    if(auto *toolbar=findChild<QAction *>("drawingWorkspaceMenuAction"))toolbar->setToolTip("Workspace · "+name);
}

WorkspaceSnapshot MainWindow::captureWorkspace() const {
    WorkspaceSnapshot result;
    result.state=m_drawingMode?m_normalLayout:saveState(3);result.compactDrawing=m_compactDrawingWorkspace;
    result.drawerOpen=m_drawingDrawerOpen;result.drawerPage=m_drawingDrawerTabs?m_drawingDrawerTabs->currentIndex():0;
    if(auto *tabs=findChild<QTabBar *>("framePropertiesTabs"))result.inspectorPage=tabs->tabData(tabs->currentIndex()).toString();
    return result;
}

bool MainWindow::restoreWorkspaceSnapshot(const WorkspaceSnapshot &snapshot) {
    if(!validateWorkspaceSnapshot(snapshot).isEmpty())return false;
    attachDrawingDrawer(false);
    if(!restoreState(snapshot.state,3))return false;
    m_compactDrawingWorkspace=snapshot.compactDrawing;m_drawingDrawerOpen=snapshot.drawerOpen;
    if(m_drawingDrawerTabs)m_drawingDrawerTabs->setCurrentIndex(snapshot.drawerPage);
    if(m_compactDrawingWorkspace)attachDrawingDrawer(true);
    restoreInspectorPage(snapshot.inspectorPage);
    const auto side=toolBarArea(m_toolboxColumn);
    if(side!=Qt::LeftToolBarArea&&side!=Qt::RightToolBarArea)addToolBar(Qt::LeftToolBarArea,m_toolboxColumn);
    recoverFloatingPanels();return true;
}

void MainWindow::arrangeBuiltinWorkspace(const QString &id) {
    const auto dock=[this](const char *name){return findChild<QDockWidget *>(name);};
    attachDrawingDrawer(false);restoreState(m_defaultLayout,3);
    // Floating panels are deliberately hidden too; a task preset starts with
    // a predictable set without disturbing their contents or document state.
    for(auto *panel:m_panels)panel->hide();
    m_compactDrawingWorkspace=id=="compact";m_drawingDrawerOpen=false;
    if(m_compactDrawingWorkspace)attachDrawingDrawer(true);
    m_toolboxColumn->show();m_quickAccess->show();
    auto *tools=dock("toolsPanel"),*sizes=dock("brushPresetPanel"),*properties=dock("propertiesPanel");
    auto *colors=dock("colorsPanel"),*layers=dock("layersPanel"),*refs=dock("referencesPanel");
    auto *timeline=dock("animationPanel"),*frames=dock("framePropertiesPanel"),*shots=dock("shotsPanel");
    const bool drawing=id=="illustration"||id=="compact";
    if(drawing) {
        for(auto *panel:{colors,refs,dock("controlPanel"),layers})panel->show();
        for(auto *panel:{tools,sizes,properties})panel->setVisible(!m_compactDrawingWorkspace);
        colors->raise();layers->raise();
        resizeDocks({tools,colors},{220,m_compactDrawingWorkspace?244:264},Qt::Horizontal);
        resizeDocks({tools,sizes,properties},{318,228,250},Qt::Vertical);
        resizeDocks({colors,layers},{370,430},Qt::Vertical);
        return;
    }
    if(id=="anime") {
        tabifyDockWidget(tools,sizes);for(auto *panel:{tools,sizes,properties,colors,refs,layers,frames,timeline})panel->show();
        tools->raise();refs->raise();frames->raise();
        resizeDocks({tools,colors},{220,260},Qt::Horizontal);
        resizeDocks({tools,properties},{210,350},Qt::Vertical);
        resizeDocks({refs,layers},{200,360},Qt::Vertical);
    } else if(id=="storyboard") {
        shots->setFloating(false);addDockWidget(Qt::LeftDockWidgetArea,shots);tabifyDockWidget(tools,shots);tabifyDockWidget(tools,sizes);
        for(auto *panel:{shots,tools,sizes,properties,refs,colors,layers})panel->show();
        shots->raise();refs->raise();layers->raise();
        resizeDocks({shots,refs},{280,248},Qt::Horizontal);
        resizeDocks({shots,properties},{460,260},Qt::Vertical);
        resizeDocks({refs,layers},{340,380},Qt::Vertical);
    } else if(id=="animatic") {
        shots->setFloating(false);addDockWidget(Qt::LeftDockWidgetArea,shots);
        for(auto *panel:{tools,sizes,properties})tabifyDockWidget(shots,panel);
        for(auto *panel:{shots,tools,sizes,properties,frames,refs,layers,timeline})panel->show();
        // One inspector column keeps timing controls beside the larger canvas.
        tabifyDockWidget(frames,refs);shots->raise();frames->raise();
        resizeDocks({shots,frames},{280,260},Qt::Horizontal);
    }
    restoreInspectorPage("Drawing");
    if(id=="anime"||id=="animatic") {
        addDockWidget(Qt::BottomDockWidgetArea,timeline);
        const int desired=std::clamp(qRound(height()*(id=="animatic"?.30:.24)),180,340);
        resizeDocks({timeline},{desired},Qt::Vertical);
    }
}

void MainWindow::rememberWorkspace(const QString &id) {
    m_activeWorkspaceId=id;m_restoredLayout=true;
    const auto current=captureWorkspace();
    m_settings->setValue("layoutV3",current.state);
    m_settings->setValue("workspace/activeId",id);
    m_settings->setValue("workspace/compactDrawing",current.compactDrawing);
    m_settings->setValue("workspace/drawingDrawerOpen",current.drawerOpen);
    m_settings->setValue("workspace/drawingDrawerPage",current.drawerPage);
    m_settings->setValue("workspace/inspectorPage",current.inspectorPage);
    m_settings->sync();refreshWorkspaceMenu();
}

bool MainWindow::applyWorkspace(const QString &requestedId) {
    // A menu action or caller may alias activeWorkspaceId; retain its identity
    // across synchronous layout/selection signals.
    const QString id=requestedId;
    const auto *entry=m_workspaces->find(id);
    if(!builtin(id)&&!entry){showActionFeedback("That workspace is no longer available.",true);return false;}
    const auto desired=entry?std::optional<WorkspaceSnapshot>(entry->snapshot):std::nullopt;
    if(!canChangeDrawingWorkspace())return false;
    if(m_drawingMode){setDrawingMode(false);if(m_drawingMode)return false;}
    const auto before=captureWorkspace();const auto beforeId=m_activeWorkspaceId;
    m_arrangingWorkspace=true;setUpdatesEnabled(false);m_timeline->setPlaying(false);m_timeline->movieReview()->setMode(ReviewMode::Shot);
    const auto options=dockOptions();setDockOptions(options&~AnimatedDocks);
    const bool restored=!desired||restoreWorkspaceSnapshot(*desired);
    if(!desired)arrangeBuiltinWorkspace(id);
    if(!restored)restoreWorkspaceSnapshot(before);
    setDockOptions(options);
    m_arrangingWorkspace=false;
    if(restored){m_previousWorkspace=before;m_previousWorkspaceId=beforeId;rememberWorkspace(id);}
    refreshDockTabs();refreshDrawingWorkspaceControls();refreshDrawingDrawerVisibility();setUpdatesEnabled(true);
    if(!restored){showActionFeedback("This saved workspace could not be restored. Your previous layout is retained.",true);return false;}
    m_canvas->setFocus(Qt::ShortcutFocusReason);return true;
}

void MainWindow::restorePreviousWorkspace() {
    if(!m_previousWorkspace||!canChangeDrawingWorkspace())return;
    if(m_drawingMode){setDrawingMode(false);if(m_drawingMode)return;}
    const auto before=captureWorkspace();const auto id=m_activeWorkspaceId;
    const auto target=*m_previousWorkspace;const auto targetId=m_previousWorkspaceId;
    m_arrangingWorkspace=true;setUpdatesEnabled(false);m_timeline->setPlaying(false);m_timeline->movieReview()->setMode(ReviewMode::Shot);
    const auto options=dockOptions();setDockOptions(options&~AnimatedDocks);
    const bool restored=restoreWorkspaceSnapshot(target);
    if(!restored)restoreWorkspaceSnapshot(before);
    setDockOptions(options);
    m_arrangingWorkspace=false;
    if(restored){m_previousWorkspace=before;m_previousWorkspaceId=id;rememberWorkspace(targetId);}
    refreshDockTabs();refreshDrawingWorkspaceControls();refreshDrawingDrawerVisibility();setUpdatesEnabled(true);
    if(!restored)showActionFeedback("The previous layout could not be restored.",true);
    m_canvas->setFocus(Qt::ShortcutFocusReason);
}

bool MainWindow::saveWorkspaceAs(const QString &name,QString *error) {
    if(!canChangeDrawingWorkspace()){if(error)*error="Finish the current edit before saving a workspace.";return false;}
    QString id;if(!m_workspaces->add(name,captureWorkspace(),&id,error))return false;
    rememberWorkspace(id);return true;
}
bool MainWindow::updateCurrentWorkspace(QString *error) {
    if(!m_workspaces->find(m_activeWorkspaceId)){if(error)*error="Save a custom workspace before updating it.";return false;}
    if(!canChangeDrawingWorkspace()){if(error)*error="Finish the current edit before updating a workspace.";return false;}
    return m_workspaces->update(m_activeWorkspaceId,captureWorkspace(),error);
}
void MainWindow::saveWorkspaceDialog() {
    if(!canChangeDrawingWorkspace())return;
    const auto snapshot=captureWorkspace();QString id;bool returnFocus=false;
    {
        class SaveWorkspaceDialog final:public QDialog {
        public:
            using QDialog::QDialog;
            bool closedWithFocus=false;
            void done(int result) override {closedWithFocus=QApplication::activeWindow()==this;QDialog::done(result);}
        };
        SaveWorkspaceDialog dialog(this);dialog.setObjectName("saveWorkspaceDialog");dialog.setWindowTitle("Save workspace");
        auto *layout=new QVBoxLayout(&dialog);layout->setContentsMargins(12,12,12,12);layout->setSpacing(8);
        auto *label=new QLabel("Workspace name");layout->addWidget(label);
        auto *name=new QLineEdit("My workspace");name->setObjectName("saveWorkspaceName");name->setAccessibleName("Workspace name");label->setBuddy(name);layout->addWidget(name);
        auto *error=new QLabel;error->setObjectName("workspaceNameError");error->setWordWrap(true);error->hide();layout->addWidget(error);
        auto *buttons=new QDialogButtonBox(QDialogButtonBox::Save|QDialogButtonBox::Cancel);layout->addWidget(buttons);
        buttons->button(QDialogButtonBox::Save)->setObjectName("saveWorkspaceConfirm");
        connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
        connect(buttons,&QDialogButtonBox::accepted,&dialog,[&]{
            QString message;
            if(!m_workspaces->add(name->text(),snapshot,&id,&message)){error->setText(message);error->show();name->setFocus();return;}
            dialog.accept();
        });
        dialog.resize(360,140);name->selectAll();name->setFocus();dialog.exec();returnFocus=dialog.closedWithFocus;
    }
    if(!id.isEmpty()){rememberWorkspace(id);showActionFeedback("Workspace saved.",false);}
    // Native modal destruction can leave Qt without an active QWidget. Repair
    // only that missing state, without pulling focus from another destination.
    if(returnFocus&&isVisible()&&!QApplication::activeModalWidget()&&!QApplication::activePopupWidget()){
        if(!QApplication::activeWindow()&&QGuiApplication::applicationState()==Qt::ApplicationActive)
            QApplication::setActiveWindow(this);
        if(isActiveWindow())m_canvas->setFocus(Qt::OtherFocusReason);
    }
}
void MainWindow::manageWorkspaces() {
    if(!canChangeDrawingWorkspace())return;
    QString error;if(!m_workspaces->load(&error)&&!error.isEmpty())showActionFeedback(error,true);
    QString selected;bool returnFocus=false;
    { WorkspaceDialog dialog(m_workspaces.get(),this);if(dialog.exec()==QDialog::Accepted)selected=dialog.selectedWorkspaceId();returnFocus=dialog.closedWithFocus(); }
    if(!selected.isEmpty())applyWorkspace(selected);else refreshWorkspaceMenu();
    if(returnFocus&&isVisible()&&!QApplication::activeModalWidget()&&!QApplication::activePopupWidget()){
        if(!QApplication::activeWindow()&&QGuiApplication::applicationState()==Qt::ApplicationActive)
            QApplication::setActiveWindow(this);
        if(isActiveWindow())m_canvas->setFocus(Qt::OtherFocusReason);
    }
}
} // namespace toon
