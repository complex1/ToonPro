#pragma once
#include "managed_project_store.h"
#include <QCache>
#include <QHash>
#include <QMainWindow>
#include <QPixmap>
#include <QQueue>
#include <QSet>
#include <QVariantMap>
#include <memory>
class QTabWidget;
class QTabBar;
class QToolBar;
class QToolButton;
class QSlider;
class QAction;
class QTreeWidget;
class QTreeWidgetItem;
class QLineEdit;
class QComboBox;
class QLabel;
class QSettings;
class QMediaPlayer;
class QAudioOutput;
class QVideoWidget;
class QPushButton;
namespace toon {
class MainWindow;
class DashboardGallery;
class TagEditor;
class ProjectWorkspaceWindow final : public QMainWindow {
    Q_OBJECT
  public:
    explicit ProjectWorkspaceWindow(const QString &settingsPath = {}, QWidget *parent = nullptr);
    ~ProjectWorkspaceWindow() override;
    bool openManagedProject(const QString &path, QString *error = nullptr);
    bool createManagedProject(const QString &path, const QString &name, const QString &type,
                              QString *error = nullptr);
    bool openItems(const QStringList &ids);
    std::shared_ptr<ManagedProjectStore> store() const { return m_store; }
    MainWindow *editorForItem(const QString &id) const;
    void refreshDashboard();

  protected:
    void closeEvent(QCloseEvent *) override;
    bool eventFilter(QObject *, QEvent *) override;

  private:
    struct Session {
        QString id, revision;
        MainWindow *editor = nullptr;
        quint64 generation = 0;
    };
    bool prepareNavigation();
    bool closeSession(QWidget *widget);
    bool closeAllSessions();
    bool saveSession(const QString &id, bool recovery = false);
    void continueRecovery();
    void newProjectDialog();
    void newDocumentDialog(bool animation);
    void importAssets();
    void newFolder();
    void organizeSelected(bool move);
    void itemMenu(const QPoint &point);
    void previewSelected();
    void showHome();
    void populateHome();
    void homeMenu(const QPoint &);
    void updateNavigation();
    void requestThumbnails();
    void startNextThumbnail();
    void applyThumbnail(const QString &key, const QImage &image, const QVariantMap &metadata);
    QString folderPath(const QString &id) const;
    QStringList selectedItems() const;
    QString folder() const;
    QString collection() const;
    void rememberProject();
    void stopPreview();
    void feedback(const QString &text, bool error = false);
    std::shared_ptr<ManagedProjectStore> m_store;
    std::unique_ptr<QSettings> m_settings;
    QTabWidget *m_tabs, *m_collections;
    QWidget *m_dashboard, *m_projectPage, *m_home;
    DashboardGallery *m_projects = nullptr, *m_items = nullptr;
    QTreeWidget *m_folders = nullptr;
    QTabBar *m_homeCollections = nullptr, *m_typeTabs = nullptr;
    QLineEdit *m_homeSearch = nullptr;
    QComboBox *m_homeSort = nullptr, *m_sort = nullptr;
    QLabel *m_subtitle = nullptr, *m_breadcrumb = nullptr, *m_homeStatus = nullptr,
           *m_inspectorName = nullptr, *m_mediaTime = nullptr, *m_empty = nullptr;
    QWidget *m_sidebar = nullptr, *m_inspector = nullptr, *m_mediaControls = nullptr, *m_tagRow = nullptr;
    QToolBar *m_navigation = nullptr;
    QToolButton *m_grid = nullptr, *m_list = nullptr;
    QSlider *m_mediaSeek = nullptr;
    TagEditor *m_assetTags = nullptr;
    QAction *m_backupAction = nullptr, *m_locationAction = nullptr;
    QString m_filterTag, m_galleryContext, m_inspectedId;
    QStringList m_inspectedTags;
    bool m_showRemoved = false, m_thumbnailRunning = false;
    struct ThumbnailTask {
        QString key, id, path, kind;
        std::shared_ptr<ManagedProjectStore> store;
    };
    QQueue<ThumbnailTask> m_thumbnailQueue;
    QSet<QString> m_pendingThumbnails;
    QCache<QString, QPixmap> m_thumbnails{256};
    QHash<QString, QVariantMap> m_thumbnailMetadata;
    std::function<void()> m_waitForThumbnail;
    QLineEdit *m_search;
    QComboBox *m_type;
    QLabel *m_title, *m_status, *m_preview, *m_details;
    QPushButton *m_newDrawing, *m_newAnimation, *m_import, *m_addFolder, *m_open;
    QMediaPlayer *m_player;
    QAudioOutput *m_audio;
    QVideoWidget *m_video;
    QHash<QString, Session> m_sessions;
    QStringList m_recoveryQueue;
    bool m_busy = false, m_refreshing = false;
    int m_currentTab = 0;
    quint64 m_previewGeneration = 0;
};
} // namespace toon
