#pragma once

#include <QDialog>
#include <QStringList>
#include <QWidget>

class QAction;
class QButtonGroup;
class QCheckBox;
class QComboBox;
class QDialogButtonBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QTabBar;
class QToolButton;

namespace toon {
class ToolCatalog;
class Document;
class ToolPreviewQueue;

// All creation paths use this dialog; opening or cancelling it never edits the catalog.
class AddToolsDialog : public QDialog {
    Q_OBJECT
public:
    enum class Context { Toolbox, GroupTools, StandaloneTools };
    explicit AddToolsDialog(ToolCatalog *catalog, Context context = Context::Toolbox,
                            QString destinationId = {}, QWidget *parent = nullptr);
    void setPreviewDocument(Document *document);
public slots:
    void accept() override;
private:
    bool groupMode() const;
    QStringList pickedTools() const;
    void updateMode();
    void updateValidity();
    void refreshChoices();
    void filterChoices();
    ToolCatalog *m_catalog;
    Context m_context;
    QString m_destinationId;
    QTabBar *m_tabs;
    QWidget *m_groupFields;
    QLineEdit *m_name, *m_search, *m_variantName;
    QButtonGroup *m_icons;
    QListWidget *m_choices;
    QCheckBox *m_variant;
    QLabel *m_error;
    QDialogButtonBox *m_buttons;
    ToolPreviewQueue *m_previews;
};

class ToolNavigationPanel : public QWidget {
    Q_OBJECT
public:
    explicit ToolNavigationPanel(ToolCatalog *catalog, bool toolbox, QWidget *parent = nullptr);
    // Brush samples yield to the active pen; document changes only resume
    // pending samples, never invalidate previews after an artwork stroke.
    void setPreviewDocument(Document *document);
    bool iconsOnly() const { return m_toolbox || m_iconsOnly; }
    void setIconsOnly(bool enabled);
    QString tagFilter() const { return m_tagFilter; }
    void setTagFilter(const QString &tag);
    // Empty selects All. This reserved value selects tools with no effective tags.
    static QString untaggedTagFilter() { return QString(QChar(0x1f)) + QStringLiteral("untagged"); }
signals:
    void importBrushesRequested();
    void manageToolsRequested(const QString &entryId, const QString &toolId);
    void manageBrushesRequested();
    void viewOptionsChanged();
public slots:
    void openAddDialog();
private:
    void refresh();
    void showContextMenu(const QPoint &position);
    void removeSelected();
    void duplicateSelected();
    void renameSelected();
    void refreshTagChoices();
    void filterTools();
    void applyViewMode();
    QString currentId() const;
    ToolCatalog *m_catalog;
    bool m_toolbox;
    bool m_iconsOnly = false;
    QString m_tagFilter;
    QListWidget *m_list;
    QWidget *m_empty;
    QLabel *m_noMatches = nullptr;
    QToolButton *m_clearFilters = nullptr;
    QLineEdit *m_search = nullptr;
    QComboBox *m_tags = nullptr;
    QToolButton *m_add, *m_remove;
    QAction *m_undo, *m_redo;
    QAction *m_iconsOnlyAction = nullptr;
    ToolPreviewQueue *m_previews;
};
} // namespace toon
