#pragma once
#include <QDialog>
#include <QPointer>
class QLabel;
class QListWidget;
class QListWidgetItem;
class QPushButton;
class QToolButton;
namespace toon {
class WorkspaceLibrary;
class WorkspaceDialog final : public QDialog {
    Q_OBJECT
public:
    explicit WorkspaceDialog(WorkspaceLibrary *library,QWidget *parent=nullptr);
    QString selectedWorkspaceId() const;
    bool closedWithFocus() const { return m_closedWithFocus; }
public slots:
    void done(int result) override;
private:
    void refresh(const QString &preferredId={});
    void refreshActions();
    void renameSelected();
    void removeSelected();
    void acceptSelected();
    void displayError(const QString &error);
    QPointer<WorkspaceLibrary> m_library;
    QListWidget *m_list;
    QLabel *m_empty,*m_error;
    QToolButton *m_rename,*m_remove;
    QPushButton *m_use;
    bool m_refreshing=false;
    bool m_closedWithFocus=false;
};
} // namespace toon
