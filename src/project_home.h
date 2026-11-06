#pragma once
#include <QWidget>
#include <QPointer>
#include <QVector>

class QSettings;
class QTreeWidget;
class QTabBar;
class QLabel;
class QPushButton;
class QToolButton;

namespace toon {
class ProjectHome final : public QWidget {
    Q_OBJECT
public:
    explicit ProjectHome(QSettings *settings, QWidget *parent = nullptr);
    void rememberProject(const QString &path, const QString &title, const QString &kindKey);
    void setCurrentProject(const QString &title);
    void refresh();
signals:
    void newProjectRequested();
    void openFileRequested();
    void openFolderRequested();
    void projectRequested(const QString &path);
    void resumeRequested();
protected:
    void showEvent(QShowEvent *event) override;
private:
    struct Entry { QString path, title, kind; qint64 opened = 0; bool pinned = false, archived = false; };
    void load();
    void store();
    void updateActions();
    void changeEntry(int operation);
    QString selectedPath() const;
    QPointer<QSettings> m_settings;
    QVector<Entry> m_entries;
    QTreeWidget *m_list;
    QTabBar *m_tabs;
    QLabel *m_status;
    QPushButton *m_open, *m_resume, *m_archive;
    QToolButton *m_pin, *m_remove;
    bool m_readOnly = false;
};
} // namespace toon
