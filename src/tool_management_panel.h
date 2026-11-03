#pragma once

#include <QWidget>
#include <QStringList>

class QButtonGroup;
class QLabel;
class QLineEdit;
class QListWidget;
class QTabWidget;
class QToolButton;

namespace toon {
class ToolCatalog;

// Editing selection belongs to this panel; browsing never activates a drawing tool.
class ToolManagementPanel final : public QWidget {
    Q_OBJECT
public:
    explicit ToolManagementPanel(ToolCatalog *catalog, QWidget *parent = nullptr);
    bool importGroupIcon(const QString &path);
public slots:
    void focusEntry(QString entryId, QString toolId = {});
private:
    void refresh();
    void refreshGroup();
    void refreshTools();
    void filterGroups();
    void filterTools();
    void setError(const QString &message);
    void createGroup();
    void editMembership();
    void editToolTag(bool add);
    QStringList selectedTools() const;
    ToolCatalog *m_catalog;
    QTabWidget *m_tabs;
    QListWidget *m_groups;
    QListWidget *m_tools;
    QLineEdit *m_groupSearch;
    QLineEdit *m_toolSearch;
    QWidget *m_groupProperties;
    QWidget *m_toolProperties;
    QLineEdit *m_groupName;
    QLineEdit *m_groupTags;
    QLineEdit *m_toolTag;
    QLabel *m_toolSummary;
    QLabel *m_toolTags;
    QLabel *m_memberCount;
    QLabel *m_iconPreview;
    QLabel *m_error;
    QWidget *m_groupColors;
    QWidget *m_toolColors;
    QButtonGroup *m_icons;
    QToolButton *m_addGroup;
    QToolButton *m_removeGroup;
    QToolButton *m_clearIcon;
    QToolButton *m_members;
    QString m_groupId;
    bool m_refreshing = false;
};
} // namespace toon
