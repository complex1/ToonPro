#pragma once
#include "project.h"
#include "workspace_library.h"
#include <optional>
#include <QMainWindow>
#include <QPointer>
#include <QStringList>
#include <memory>
class QAction;
class QCloseEvent;
class QDockWidget;
class QLabel;
class QSettings;
class QTimer;
class QToolButton;
class QTabBar;
class QStackedWidget;
class QScrollArea;
class QToolBar;

namespace toon {
class Canvas;
class AutoActions;
class BrushLibrary;
class PaletteLibrary;
class ProjectSave;
class ModelLibrary;
class DrawingTemplateLibrary;
class ToolCatalog;
class ToolSession;
class IconColumn;
class QuickAccessToolbar;
class TimelineController;
class ProjectHome;
class ProductionPanel;
class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(const QString &settingsPath = {}, QWidget *parent = nullptr, bool managed = false);
    ~MainWindow() override;
    bool openProject(const QString &path);
    void setManagedDocument(const Project &, const std::function<bool()> &save, const std::function<void()> &recovery);
    bool prepareManagedLeave();
    bool managedDirty() const { return m_dirty; }
    bool saveManaged() { return save(); }
    void markManagedSaved();
    void enforceManagedBoundary();
    Canvas *canvas() const { return m_canvas; }
    ToolCatalog *toolCatalog() const { return m_toolCatalog; }
    bool drawingMode() const { return m_drawingMode; }
    bool compactDrawingWorkspace() const { return m_compactDrawingWorkspace; }
    QMenu *createPopupMenu() override;
    QString activeWorkspaceId() const { return m_activeWorkspaceId; }
    WorkspaceLibrary *workspaceLibrary() const { return m_workspaces.get(); }
    bool applyWorkspace(const QString &id);
    bool saveWorkspaceAs(const QString &name, QString *error = nullptr);
    bool updateCurrentWorkspace(QString *error = nullptr);
public slots:
    void showProjectHome();
    void showProductionWorkspace();
    void showDrawingWorkspacePage();
    void setDrawingMode(bool enabled);
    void applyDrawingWorkspace(bool compact = false);
    void toggleDrawingControls();
protected:
    void closeEvent(QCloseEvent *) override;
    bool event(QEvent *) override;
    bool eventFilter(QObject *, QEvent *) override;
private:
    bool m_managed = false;
    std::function<bool()> m_managedSave;
    std::function<void()> m_managedRecovery;
    void newProject();
    void editProject();
    bool save(bool choosePath = false);
    bool confirmDiscard();
    bool resolveCelWorkflows();
    void displayProject(const Project &project, const QString &path, bool dirty, const QString &studioRoot = {});
    void createProductionWorkspace();
    bool prepareStudioTransition();
    void hideEditorPanels();
    void restoreEditorPanels();
    void rememberCurrentProject();
    bool saveStudioCopy();
    bool createStudioFolder(const Project &, const QString &root, const QString &sourceRoot, QString *error);
    void importArtworkFile(const QString &path);
    void updateTitle();
    void exportImage(bool sheet);
    void exportAnimation();
    void exportVideo();
    void exportAudio();
    void exportShots(const QStringList &shotIds);
    void createPanels();
    void navigateHistory(int index);
    void refreshDockTabs();
    void restoreWorkspace();
    void createWorkspaceMenu(QMenu *view, QToolBar *toolbar);
    void refreshWorkspaceMenu();
    WorkspaceSnapshot captureWorkspace() const;
    void restoreInspectorPage(const QString &page);
    bool restoreWorkspaceSnapshot(const WorkspaceSnapshot &snapshot);
    void arrangeBuiltinWorkspace(const QString &id);
    void rememberWorkspace(const QString &id);
    void saveWorkspaceDialog();
    void manageWorkspaces();
    void restorePreviousWorkspace();
    void writeRecovery();
    void resetWorkspace();
    void showDrawingWorkspace();
    void createDrawingWorkspaceControls();
    void refreshDrawingWorkspaceControls();
    bool canChangeDrawingWorkspace();
    bool drawingControlsVisible() const;
    void createDrawingDrawer();
    void attachDrawingDrawer(bool attached);
    void refreshDrawingDrawerGeometry();
    void refreshDrawingDrawerVisibility();
    void importBrushes();
    void openToolManagement(const QString &entryId = {}, const QString &toolId = {});
    void openBrushManagement();
    void recoverFloatingPanels();
    void refreshStatusFeedback();
    void showActionFeedback(const QString &message, bool blockedAttempt);
    Canvas *m_canvas;
    AutoActions *m_autoActions = nullptr;
    TimelineController *m_timeline;
    BrushLibrary *m_brushLibrary;
    PaletteLibrary *m_palettes;
    ProjectSave *m_recoverySave;
    ModelLibrary *m_modelLibrary;
    DrawingTemplateLibrary *m_drawingTemplates;
    ToolCatalog *m_toolCatalog;
    ToolSession *m_toolSession;
    QWidget *m_welcome;
    ProductionPanel *m_production = nullptr;
    QString m_studioRoot;
    bool m_browsingStudio = false, m_studioWriting = false;
    QByteArray m_editorLayout;
    QList<QPair<QPointer<QWidget>, bool>> m_editorVisibility;
    QLabel *m_status = nullptr;
    QLabel *m_statusIcon = nullptr;
    QWidget *m_statusFeedback = nullptr;
    QTimer *m_statusMessageTimer = nullptr;
    QTimer *m_statusHighlightTimer = nullptr;
    QString m_viewStatus = "Ready · choose New project to begin";
    QString m_interactionHint;
    QString m_actionFeedback;
    bool m_feedbackIsBlockedAttempt = false;

    QString m_path;
    QString m_ownedRecoveryPath;
    quint64 m_documentRevision = 0, m_projectGeneration = 0;
    bool m_dirty = false;
    bool m_restoredLayout = false;
    bool m_dockTabsRefreshPending = false;
    QList<QAction *> m_projectActions;
    QList<QDockWidget *> m_panels;
    IconColumn *m_toolboxColumn = nullptr;
    QuickAccessToolbar *m_quickAccess = nullptr;
    QAction *m_compactDrawingContext = nullptr;
    QToolButton *m_compactDrawingControls = nullptr;
    QToolButton *m_compactDrawingColor = nullptr;
    bool m_compactDrawingWorkspace = false;
    QWidget *m_drawingDrawer = nullptr;
    QTabBar *m_drawingDrawerTabs = nullptr;
    QStackedWidget *m_drawingDrawerStack = nullptr;
    QList<QPointer<QWidget>> m_drawingDrawerContents;
    QList<QPointer<QScrollArea>> m_drawingDrawerPages;
    QList<QPointer<QAction>> m_drawingDrawerPanelActions;
    bool m_drawingDrawerAttached = false;
    bool m_drawingDrawerOpen = false;
    bool m_arrangingWorkspace = false;
    bool m_drawingMode = false;
    bool m_changingDrawingMode = false;
    QByteArray m_normalLayout, m_normalGeometry;
    Qt::WindowStates m_normalWindowState;
    QList<QPair<QPointer<QWidget>, bool>> m_normalVisibility;
    QByteArray m_defaultLayout;
    std::unique_ptr<QSettings> m_settings;
    std::unique_ptr<WorkspaceLibrary> m_workspaces;
    QMenu *m_workspaceMenu = nullptr;
    QString m_activeWorkspaceId = "illustration";
    QString m_previousWorkspaceId;
    std::optional<WorkspaceSnapshot> m_previousWorkspace;
    QList<QAction *> m_savedWorkspaceActions;
    QAction *m_workspaceSavedSeparator = nullptr;
};
void applyTheme();
} // namespace toon
