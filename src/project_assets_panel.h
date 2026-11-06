#pragma once
#include "studio_project.h"
#include <QPointer>
#include <QWidget>
#include <atomic>
#include <functional>
#include <memory>
#include <optional>

class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QTreeWidget;
class QTabBar;
class QToolButton;
class QPushButton;
class QMediaPlayer;
class QAudioOutput;
class QVideoSink;

namespace toon {
class Document;
class AssetPreview;
class ProjectAssetsPanel final : public QWidget {
    Q_OBJECT
public:
    explicit ProjectAssetsPanel(Document *document, std::function<bool()> prepareTransition,
                                QWidget *parent = nullptr);
    ~ProjectAssetsPanel() override;
    void setProjectRoot(const QString &path);
    QString projectRoot() const;
    bool busy() const { return m_importing; }
    bool finishEditing();
    QString selectedAssetId() const { return m_selected; }
    void importFiles(const QStringList &paths);
    bool recordCurrentShotUse();
signals:
    void createPackageRequested();
    void placeImageRequested(const QString &path);
    void placeAudioRequested(const QString &path);
    void openShotRequested(const QString &shotId);
    void importFinished(bool success, const QString &message);
protected:
    void hideEvent(QHideEvent *event) override;
    void showEvent(QShowEvent *event) override;
private:
    void refreshList();
    void refreshDetails(bool reloadEditors = true);
    void refreshActions();
    void selectedChanged();
    bool commitDetails();
    bool prepareAction();
    void showPreview();
    void stopPreview();
    void togglePlayback();
    void setStatus(const QString &message);
    void addFolder();
    void removeSelected();
    void restoreSelected();
    void useSelected();
    void refreshUsedIn();
    QPointer<Document> m_document;
    std::function<bool()> m_prepareTransition;
    std::optional<StudioProject> m_store;
    QString m_selected;
    std::optional<StudioAsset> m_loadedAsset;
    QLineEdit *m_search, *m_name, *m_tags;
    QComboBox *m_type, *m_folderFilter, *m_folder;
    QTreeWidget *m_list;
    QListWidget *m_usedIn;
    QTabBar *m_tabs;
    QLabel *m_status, *m_info, *m_usageLabel;
    QWidget *m_details;
    AssetPreview *m_preview;
    QToolButton *m_import, *m_cancel, *m_newFolder, *m_reload, *m_remove, *m_restore, *m_play, *m_stop, *m_use, *m_attach;
    QPushButton *m_createPackage;
    QMediaPlayer *m_player = nullptr;
    QAudioOutput *m_audio = nullptr;
    QVideoSink *m_video = nullptr;
    bool m_refreshing = false, m_dirty = false, m_importing = false, m_deferredPreview = false;
    quint64 m_generation = 0, m_previewGeneration = 0;
    std::shared_ptr<std::atomic_bool> m_cancelImport;
};
} // namespace toon
