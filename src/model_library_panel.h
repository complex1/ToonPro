#pragma once
#include <QWidget>
#include <atomic>
#include <memory>
class QLineEdit;
class QTabBar;
class QComboBox;
class QListWidget;
class QLabel;
class QToolButton;
namespace toon {
class ModelLibrary;
class Scene3DController;
class ModelLibraryPanel final : public QWidget {
    Q_OBJECT
public:
    explicit ModelLibraryPanel(ModelLibrary *library, Scene3DController *controller, QWidget *parent = nullptr);
    ~ModelLibraryPanel() override;
    bool importing() const { return m_importing; }
    void importFileAsync(const QString &path);
signals:
    void importFinished(bool success, const QString &message);
private:
    void refresh();
    void refreshActions();
    void addSelected();
    void editTags();
    void renameSelected();
    void removeSelected();
    QString selectedId() const;
    void setStatus(const QString &message);
    ModelLibrary *m_library;
    Scene3DController *m_controller;
    QTabBar *m_tabs;
    QLineEdit *m_search;
    QComboBox *m_tags;
    QListWidget *m_list;
    QLabel *m_status;
    QToolButton *m_import, *m_add, *m_favorite, *m_favoritesOnly, *m_remove, *m_menu;
    bool m_refreshing = false, m_importing = false;
    std::shared_ptr<std::atomic_bool> m_cancelImport;
};
} // namespace toon
