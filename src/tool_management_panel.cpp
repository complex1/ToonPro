#include "tool_management_panel.h"
#include "tool_catalog.h"
#include "icons.h"
#include "property_row.h"
#include "ui_theme.h"
#include "ui_metrics.h"

#include <QAction>
#include <QButtonGroup>
#include <QColorDialog>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QImageReader>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPainter>
#include <QPushButton>
#include <QResizeEvent>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSplitter>
#include <QTabBar>
#include <QTabWidget>
#include <QToolButton>
#include <QVBoxLayout>
#include <algorithm>
#include <functional>

namespace toon {
namespace {
const QList<QPair<QString, QString>> groupIcons = {{"folder-open", "Folder"}, {"paintbrush", "Painting"},
    {"pen-tool", "Inking"}, {"image", "Backgrounds"}, {"palette", "Color"}, {"wand-sparkles", "Effects"},
    {"grid-2x2", "Guides"}, {"square-dashed", "Selections"}};
QToolButton *iconButton(const QString &id, const QString &icon, const QString &help) {
    auto *button = new QToolButton;
    button->setObjectName(id); button->setIcon(appIcon(icon));
    button->setIconSize({ui::metrics::actionIcon, ui::metrics::actionIcon});
    button->setFixedSize(ui::metrics::actionTarget, ui::metrics::actionTarget);
    button->setProperty("compactDrawingAction", true);
    button->setToolTip(help); button->setAccessibleName(help);
    return button;
}
QLineEdit *field(const QString &id, const QString &hint) {
    auto *edit = new QLineEdit; edit->setObjectName(id); edit->setPlaceholderText(hint);
    edit->setAccessibleName(hint); edit->setMinimumWidth(0);
    edit->setProperty("compactDrawingField", true);
    return edit;
}
void enableSearchClear(QLineEdit *edit) {
    edit->setClearButtonEnabled(true);
    // Qt's embedded clear button has no label or name of its own.
    for (auto *button : edit->findChildren<QToolButton *>()) {
        button->setObjectName(edit->objectName() + "Clear");
        button->setToolTip("Clear search"); button->setAccessibleName("Clear search");
    }
}
class ManagementList final : public QListWidget {
public:
    using QListWidget::QListWidget;
protected:
    void changeEvent(QEvent *event) override {
        QListWidget::changeEvent(event);
        if (event->type() == QEvent::FontChange) {
            const int height = qMax(ui::metrics::toolRow, fontMetrics().height() + ui::metrics::sectionGap);
            for (int row = 0; row < count(); ++row) item(row)->setSizeHint({0, height});
            updateGeometry();
        }
    }
};
QListWidget *list(const QString &id, const QString &name) {
    auto *widget = new ManagementList; widget->setObjectName(id); widget->setAccessibleName(name);
    widget->setIconSize({ui::metrics::primaryIcon, ui::metrics::primaryIcon}); widget->setUniformItemSizes(true);
    widget->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    widget->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff); widget->setTextElideMode(Qt::ElideRight);
    return widget;
}
// The list remains independently usable while long group metadata scrolls.
// Changing orientation reuses both panes, preserving selection and text drafts.
class ManagementSplitter final : public QSplitter {
public:
    explicit ManagementSplitter(const QString &name) : QSplitter(Qt::Vertical) {
        setObjectName(name); setChildrenCollapsible(false);
        setHandleWidth(ui::metrics::controlGap);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    }
protected:
    void resizeEvent(QResizeEvent *event) override {
        const auto direction = width() >= 560 ? Qt::Horizontal : Qt::Vertical;
        if (orientation() != direction) {
            setOrientation(direction);
            setStretchFactor(0, direction == Qt::Horizontal ? 2 : 3);
            setStretchFactor(1, direction == Qt::Horizontal ? 3 : 2);
            const int extent = direction == Qt::Horizontal ? width() : height();
            setSizes(direction == Qt::Horizontal ? QList<int>{extent * 2 / 5, extent * 3 / 5}
                                                : QList<int>{extent * 3 / 5, extent * 2 / 5});
        }
        QSplitter::resizeEvent(event);
    }
};
QLabel *label(const QString &text = {}) {
    auto *widget = new QLabel(text); widget->setTextFormat(Qt::PlainText); return widget;
}
class ElidedLabel final : public QLabel {
public:
    ElidedLabel() {
        setTextFormat(Qt::PlainText);
        setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
        setFixedHeight(fontMetrics().height() + 2);
    }
    void setFullText(const QString &text) {
        m_fullText = text;
        setToolTip(text); setAccessibleName(text);
        updateText();
    }
protected:
    void changeEvent(QEvent *event) override {
        QLabel::changeEvent(event);
        if (event->type() == QEvent::FontChange) {
            setFixedHeight(fontMetrics().height() + 2); updateText();
        }
    }
    void resizeEvent(QResizeEvent *event) override {
        QLabel::resizeEvent(event); updateText();
    }
private:
    void updateText() { setText(fontMetrics().elidedText(m_fullText, Qt::ElideRight, contentsRect().width())); }
    QString m_fullText;
};
PropertyRow *managementPropertyRow(const QString &name, QWidget *editor) {
    auto *row = new PropertyRow(name, editor); row->setLabelWidth(44); return row;
}
QListWidgetItem *itemFor(QListWidget *widget, const QString &id) {
    for (int row = 0; row < widget->count(); ++row)
        if (widget->item(row)->data(Qt::UserRole).toString() == id) return widget->item(row);
    return nullptr;
}
bool sameRows(QListWidget *widget, const QStringList &ids) {
    if (widget->count() != ids.size()) return false;
    for (int i = 0; i < ids.size(); ++i) if (widget->item(i)->data(Qt::UserRole).toString() != ids[i]) return false;
    return true;
}
void setModelText(QLineEdit *edit, const QString &value, bool newTarget) {
    // Unrelated navigation/metadata refreshes must not replace unfinished text.
    if (newTarget || edit->property("metadataText").toString() != value) edit->setText(value);
    edit->setProperty("metadataText", value);
}
QIcon colorIcon(QColor color) {
    QPixmap image(40, 40); image.setDevicePixelRatio(2); image.fill(Qt::transparent);
    QPainter painter(&image); painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(QColor(ui::border), 1)); painter.setBrush(color);
    painter.drawRoundedRect(QRectF(1, 1, 18, 18), 4, 4); painter.end(); return QIcon(image);
}
QWidget *colorRow(const QString &prefix, const std::function<void(QColor)> &edit) {
    auto *row = new QWidget; auto *layout = new QHBoxLayout(row); layout->setContentsMargins(0, 0, 0, 0); layout->setSpacing(3);
    const QStringList colors{"#5681FF", "#58BB94", "#E5A867", "#A78BF4"};
    for (int i = 0; i < colors.size(); ++i) {
        const QColor color(colors[i]);
        auto *button = iconButton(prefix + QString("Preset%1").arg(i), "palette", "Set color " + colors[i]);
        button->setIcon(colorIcon(color)); button->setCheckable(true); button->setProperty("metadataColor", color);
        QObject::connect(button, &QToolButton::clicked, row, [edit, color] { edit(color); }); layout->addWidget(button);
    }
    auto *custom = iconButton(prefix + "Custom", "palette", "Choose a custom color…");
    custom->setProperty("customColorButton", true);
    QObject::connect(custom, &QToolButton::clicked, row, [row, edit] {
        auto current = row->property("currentColor").value<QColor>();
        if (!current.isValid()) current = QColor(ui::accent);
        const auto color = QColorDialog::getColor(current, row, "Label color");
        if (color.isValid()) edit(color);
    }); layout->addWidget(custom);
    auto *clear = iconButton(prefix + "Clear", "x", "Clear color");
    clear->setCheckable(true); clear->setProperty("clearColorButton", true);
    QObject::connect(clear, &QToolButton::clicked, row, [edit] { edit({}); }); layout->addWidget(clear); layout->addStretch();
    return row;
}
void showColor(QWidget *row, QColor color, bool mixed = false) {
    row->setProperty("currentColor", color);
    for (auto *button : row->findChildren<QToolButton *>()) {
        if (button->property("metadataColor").isValid())
            button->setChecked(!mixed && color == button->property("metadataColor").value<QColor>());
        if (button->property("clearColorButton").toBool()) button->setChecked(!mixed && !color.isValid());
        if (button->property("customColorButton").toBool()) {
            button->setIcon(color.isValid() && !mixed ? colorIcon(color) : appIcon("palette"));
            button->setToolTip(mixed ? "Choose a color for all selected tools (mixed colors)"
                : color.isValid() ? "Choose a custom color… Current: " + color.name().toUpper() : "Choose a custom color…");
        }
    }
}
QScrollArea *scrollPage(QWidget *content, const QString &name) {
    auto *scroll = new QScrollArea; scroll->setWidgetResizable(true); scroll->setFrameShape(QFrame::NoFrame);
    scroll->setObjectName(name); scroll->setMinimumHeight(0);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff); scroll->setWidget(content); return scroll;
}
QString metadataText(const QString &name, const QStringList &tags) {
    return tags.isEmpty() ? name : name + " · " + tags.join(", ");
}
} // namespace

ToolManagementPanel::ToolManagementPanel(ToolCatalog *catalog, QWidget *parent) : QWidget(parent), m_catalog(catalog) {
    setObjectName("toolManagementContent"); setMinimumSize(240, 220);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(ui::metrics::panelMargin, ui::metrics::panelMargin, ui::metrics::panelMargin, ui::metrics::panelMargin);
    layout->setSpacing(ui::metrics::controlGap);
    m_tabs = new QTabWidget; m_tabs->setObjectName("managementTabs"); m_tabs->setDocumentMode(true);
    m_tabs->setIconSize({ui::metrics::tabIcon, ui::metrics::tabIcon});
    m_tabs->tabBar()->setProperty("compactDrawingTabs", true);
    m_tabs->tabBar()->setFocusPolicy(Qt::StrongFocus);
    auto *groupsPage = new QWidget; auto *groupsLayout = new QVBoxLayout(groupsPage);
    groupsLayout->setContentsMargins(0, 4, 0, 0); groupsLayout->setSpacing(4);
    auto *groupHeader = new QHBoxLayout; groupHeader->setSpacing(4);
    m_groupSearch = field("manageGroupSearch", "Search groups"); enableSearchClear(m_groupSearch);
    groupHeader->addWidget(m_groupSearch, 1);
    m_addGroup = iconButton("manageAddGroup", "plus", "Create a group"); groupHeader->addWidget(m_addGroup);
    m_removeGroup = iconButton("manageRemoveGroup", "trash", "Remove this group from Toolbox"); groupHeader->addWidget(m_removeGroup);
    groupsLayout->addLayout(groupHeader);
    auto *groupSplit = new ManagementSplitter("manageGroupsSplit");
    m_groups = list("managedGroups", "Groups to organize"); m_groups->setMinimumSize(96, 60);
    groupSplit->addWidget(m_groups);
    auto *groupEditor = new QWidget; auto *groupEditorLayout = new QVBoxLayout(groupEditor);
    groupEditorLayout->setContentsMargins(0, 0, 0, 0); groupEditorLayout->setSpacing(ui::metrics::controlGap);
    auto *membership = new QHBoxLayout; membership->setSpacing(ui::metrics::controlGap);
    m_memberCount = new ElidedLabel; m_memberCount->setObjectName("manageMemberCount"); membership->addWidget(m_memberCount, 1);
    auto *members = iconButton("manageMembership", "layers", "Choose which tools belong to this group…");
    m_members = members;
    membership->addWidget(members); groupEditorLayout->addLayout(membership);
    m_groupProperties = new QWidget; auto *properties = new QVBoxLayout(m_groupProperties);
    properties->setContentsMargins(0, 0, 0, 0); properties->setSpacing(4);
    m_groupName = field("manageGroupName", "Group name"); m_groupName->setMaxLength(120); properties->addWidget(managementPropertyRow("Name", m_groupName));
    m_groupTags = field("manageGroupTags", "Tags, separated by commas"); m_groupTags->setMaxLength(ToolCatalog::maxTags * (ToolCatalog::maxTagLength + 2));
    m_groupTags->setToolTip("Group tags, separated by commas. Tools inherit them in this group’s filter."); properties->addWidget(managementPropertyRow("Tags", m_groupTags));
    m_groupColors = colorRow("manageGroupColor", [this](QColor color) {
        if (!m_catalog->setEntryColor(m_groupId, color)) setError("Could not change this group’s color."); else setError({});
    }); properties->addWidget(managementPropertyRow("Color", m_groupColors));
    auto *iconsHeader = new QHBoxLayout; iconsHeader->setSpacing(4); iconsHeader->addWidget(label("Icon")); iconsHeader->addStretch();
    m_iconPreview = label(); m_iconPreview->setObjectName("manageGroupIconPreview");
    m_iconPreview->setFixedSize(ui::metrics::actionTarget, ui::metrics::actionTarget);
    m_iconPreview->setAlignment(Qt::AlignCenter);
    m_iconPreview->setAccessibleName("Current group icon"); iconsHeader->addWidget(m_iconPreview);
    auto *upload = iconButton("manageUploadIcon", "image", "Upload a group icon (PNG, JPEG or WebP)…"); iconsHeader->addWidget(upload);
    m_clearIcon = iconButton("manageClearIcon", "rotate-ccw", "Restore this group’s built-in icon"); iconsHeader->addWidget(m_clearIcon);
    properties->addLayout(iconsHeader);
    auto *icons = new QGridLayout; icons->setContentsMargins(0, 0, 0, 0); icons->setSpacing(3); m_icons = new QButtonGroup(this);
    for (int i = 0; i < groupIcons.size(); ++i) {
        const auto &choice = groupIcons[i]; auto *button = iconButton("manageGroupIcon_" + choice.first, choice.first, choice.second + " group icon");
        button->setIconSize({ui::metrics::primaryIcon, ui::metrics::primaryIcon});
        button->setCheckable(true); button->setProperty("iconName", choice.first); m_icons->addButton(button, i);
        icons->addWidget(button, i / 4, i % 4, Qt::AlignLeft);
    }
    properties->addLayout(icons);
    properties->addStretch();
    auto *groupScroll = scrollPage(m_groupProperties, "manageGroupsScroll"); groupScroll->setMinimumHeight(56);
    groupEditorLayout->addWidget(groupScroll, 1); groupSplit->addWidget(groupEditor);
    groupSplit->setStretchFactor(0, 3); groupSplit->setStretchFactor(1, 2); groupSplit->setSizes({300, 200});
    groupsLayout->addWidget(groupSplit, 1);

    auto *toolsPage = new QWidget; auto *toolsLayout = new QVBoxLayout(toolsPage);
    toolsLayout->setContentsMargins(0, 4, 0, 0); toolsLayout->setSpacing(4);
    m_toolSearch = field("manageToolSearch", "Search tools or tags"); enableSearchClear(m_toolSearch); toolsLayout->addWidget(m_toolSearch);
    auto *toolSplit = new ManagementSplitter("manageToolsSplit");
    m_tools = list("managedTools", "Tools to organize; select several to edit tags and colors together");
    m_tools->setSelectionMode(QAbstractItemView::ExtendedSelection); m_tools->setMinimumSize(96, 60); toolSplit->addWidget(m_tools);
    auto *toolEditor = new QWidget; auto *toolEditorLayout = new QVBoxLayout(toolEditor);
    toolEditorLayout->setContentsMargins(0, 0, 0, 0); toolEditorLayout->setSpacing(ui::metrics::controlGap);
    m_toolSummary = new ElidedLabel; m_toolSummary->setObjectName("manageToolSummary"); toolEditorLayout->addWidget(m_toolSummary);
    m_toolProperties = new QWidget; auto *toolProperties = new QVBoxLayout(m_toolProperties); toolProperties->setContentsMargins(0, 0, 0, 0); toolProperties->setSpacing(4);
    m_toolTags = new ElidedLabel; m_toolTags->setObjectName("manageToolTags"); toolProperties->addWidget(m_toolTags);
    auto *tags = new QHBoxLayout; tags->setSpacing(4);
    m_toolTag = field("manageToolTag", "Tag"); m_toolTag->setMaxLength(ToolCatalog::maxTagLength); tags->addWidget(m_toolTag, 1);
    m_toolTag->setToolTip("One tag, up to 40 characters, without commas or control characters");
    auto *addTag = iconButton("manageAddTag", "plus", "Add this tag to all selected tools"); tags->addWidget(addTag);
    auto *removeTag = iconButton("manageRemoveTag", "minus", "Remove this tag from all selected tools"); tags->addWidget(removeTag); toolProperties->addLayout(tags);
    m_toolColors = colorRow("manageToolColor", [this](QColor color) {
        if (!m_catalog->setToolColor(selectedTools(), color)) setError("Could not change the selected tools’ colors."); else setError({});
    }); toolProperties->addWidget(managementPropertyRow("Color", m_toolColors)); toolProperties->addStretch();
    auto *toolScroll = scrollPage(m_toolProperties, "manageToolsScroll"); toolScroll->setMinimumHeight(56);
    toolEditorLayout->addWidget(toolScroll, 1); toolSplit->addWidget(toolEditor);
    toolSplit->setStretchFactor(0, 3); toolSplit->setStretchFactor(1, 2); toolSplit->setSizes({300, 200});
    toolsLayout->addWidget(toolSplit, 1);
    m_tabs->addTab(groupsPage, appIcon("folder-open"), "Groups");
    m_tabs->addTab(toolsPage, appIcon("paintbrush"), "Tools");
    m_tabs->setTabToolTip(0, "Organize groups, membership, tags, colors and icons");
    m_tabs->setTabToolTip(1, "Tag and color several tools together");
    m_tabs->tabBar()->setAccessibleName("Tool management categories");
    m_tabs->tabBar()->setAccessibleTabName(0, "Groups"); m_tabs->tabBar()->setAccessibleTabName(1, "Tools");
    layout->addWidget(m_tabs, 1);
    m_error = new ElidedLabel; m_error->setObjectName("toolManagementError");
    auto *history = new QHBoxLayout; history->setSpacing(4);
    auto *undo = iconButton("manageUndo", "undo-2", "Undo tool organization"); auto *undoAction = m_catalog->history()->createUndoAction(this, "Undo tool organization");
    undoAction->setIcon(appIcon("undo-2")); undo->setDefaultAction(undoAction); history->addWidget(undo);
    auto *redo = iconButton("manageRedo", "redo-2", "Redo tool organization"); auto *redoAction = m_catalog->history()->createRedoAction(this, "Redo tool organization");
    redoAction->setIcon(appIcon("redo-2")); redo->setDefaultAction(redoAction); history->addWidget(redo); history->addWidget(m_error, 1); layout->addLayout(history);

    connect(m_catalog, &ToolCatalog::changed, this, &ToolManagementPanel::refresh);
    connect(m_groups, &QListWidget::currentItemChanged, this, [this](QListWidgetItem *item) {
        if (m_refreshing) return;
        m_groupId = item ? item->data(Qt::UserRole).toString() : QString(); setError({}); refreshGroup();
    });
    connect(m_tools, &QListWidget::itemSelectionChanged, this, [this] { if (!m_refreshing) { setError({}); refreshTools(); } });
    connect(m_groupSearch, &QLineEdit::textChanged, this, &ToolManagementPanel::filterGroups);
    connect(m_toolSearch, &QLineEdit::textChanged, this, &ToolManagementPanel::filterTools);
    connect(m_addGroup, &QToolButton::clicked, this, &ToolManagementPanel::createGroup);
    connect(m_removeGroup, &QToolButton::clicked, this, [this] { m_catalog->removeEntry(m_groupId); });
    connect(m_groupName, &QLineEdit::editingFinished, this, [this] {
        if (m_refreshing || m_groupId.isEmpty()) return;
        if (!m_catalog->renameEntry(m_groupId, m_groupName->text().trimmed())) { setError("Enter a group name of 1–120 characters."); refreshGroup(); }
        else setError({});
    });
    connect(m_groupTags, &QLineEdit::editingFinished, this, [this] {
        if (m_refreshing || m_groupId.isEmpty()) return;
        auto tags = m_groupTags->text().split(',', Qt::SkipEmptyParts);
        for (auto &tag : tags) tag = tag.trimmed();
        if (!m_catalog->setEntryTags(m_groupId, tags)) setError("Use up to 32 tags, each 1–40 characters, without control characters.");
        else { setError({}); refreshGroup(); }
    });
    connect(m_icons, &QButtonGroup::idClicked, this, [this](int index) {
        if (m_refreshing || m_groupId.isEmpty()) return;
        if (!m_catalog->setEntryIcon(m_groupId, groupIcons[index].first)) setError("Could not change this group’s icon."); else setError({});
    });
    connect(upload, &QToolButton::clicked, this, [this] {
        const auto path = QFileDialog::getOpenFileName(this, "Group icon", {}, "Images (*.png *.jpg *.jpeg *.webp)");
        if (!path.isEmpty()) importGroupIcon(path);
    });
    connect(m_clearIcon, &QToolButton::clicked, this, [this] {
        if (const auto *entry = m_catalog->entry(m_groupId)) m_catalog->setEntryIcon(m_groupId, entry->icon);
        setError({});
    });
    connect(members, &QToolButton::clicked, this, &ToolManagementPanel::editMembership);
    connect(addTag, &QToolButton::clicked, this, [this] { editToolTag(true); });
    connect(removeTag, &QToolButton::clicked, this, [this] { editToolTag(false); });
    connect(m_toolTag, &QLineEdit::returnPressed, this, [this] { editToolTag(true); });
    m_groupId = catalog->selectedEntryId(); refresh();
    if (auto *item = itemFor(m_tools, catalog->activeToolId())) item->setSelected(true);
}

void ToolManagementPanel::setError(const QString &message) { static_cast<ElidedLabel *>(m_error)->setFullText(message); }
QStringList ToolManagementPanel::selectedTools() const {
    QStringList result; for (auto *item : m_tools->selectedItems()) if (!item->isHidden()) result.append(item->data(Qt::UserRole).toString()); return result;
}
void ToolManagementPanel::refresh() {
    if (m_refreshing) return;
    m_refreshing = true;
    const auto selected = selectedTools();
    const QSignalBlocker groupBlock(m_groups), toolBlock(m_tools);
    QStringList groupIds, toolIds;
    for (const auto &entry : m_catalog->availableEntries()) if (entry.group) groupIds.append(entry.id);
    for (const auto &tool : m_catalog->availableTools()) toolIds.append(tool.id);
    const bool rebuildGroups = !sameRows(m_groups, groupIds), rebuildTools = !sameRows(m_tools, toolIds);
    if (rebuildGroups) m_groups->clear();
    if (rebuildTools) m_tools->clear();
    int groupRow = 0;
    for (const auto &entry : m_catalog->availableEntries()) if (entry.group) {
        auto *item = rebuildGroups ? new QListWidgetItem(m_groups) : m_groups->item(groupRow);
        ++groupRow;
        item->setText(entry.name); item->setIcon(entry.customIcon.isNull() ? appIcon(entry.icon) : QIcon(QPixmap::fromImage(entry.customIcon)));
        item->setData(Qt::UserRole, entry.id); item->setData(Qt::UserRole + 1, metadataText(entry.name, entry.tags));
        item->setToolTip(metadataText(entry.name, entry.tags)); item->setData(Qt::AccessibleTextRole, item->toolTip());
        item->setSizeHint({0, qMax(ui::metrics::toolRow, m_groups->fontMetrics().height() + ui::metrics::sectionGap)});
    }
    if (!itemFor(m_groups, m_groupId)) m_groupId = m_groups->count() ? m_groups->item(0)->data(Qt::UserRole).toString() : QString();
    m_groups->setCurrentItem(itemFor(m_groups, m_groupId));
    int toolRow = 0;
    for (const auto &tool : m_catalog->availableTools()) {
        auto *item = rebuildTools ? new QListWidgetItem(m_tools) : m_tools->item(toolRow);
        ++toolRow;
        item->setText(tool.name); item->setIcon(appIcon(tool.icon)); item->setData(Qt::UserRole, tool.id);
        item->setData(Qt::UserRole + 1, metadataText(tool.name, tool.tags)); item->setToolTip(metadataText(tool.name, tool.tags));
        item->setData(Qt::AccessibleTextRole, item->toolTip());
        item->setSizeHint({0, qMax(ui::metrics::toolRow, m_tools->fontMetrics().height() + ui::metrics::sectionGap)}); item->setSelected(selected.contains(tool.id));
    }
    m_refreshing = false; filterGroups(); filterTools(); refreshGroup(); refreshTools();
    m_addGroup->setEnabled(m_catalog->writable() && m_catalog->entries().size() < ToolCatalog::maxEntries);
}
void ToolManagementPanel::refreshGroup() {
    const auto *entry = m_catalog->entry(m_groupId); const bool available = entry && entry->group;
    m_groupProperties->setEnabled(available && m_catalog->writable()); m_removeGroup->setEnabled(available && m_catalog->writable());
    m_members->setEnabled(available && m_catalog->writable());
    if (!available) { m_groupName->clear(); m_groupTags->clear(); m_iconPreview->clear(); static_cast<ElidedLabel *>(m_memberCount)->setFullText({}); m_groupProperties->setProperty("entryId", QString()); return; }
    const bool newTarget = m_groupProperties->property("entryId").toString() != entry->id;
    m_groupProperties->setProperty("entryId", entry->id);
    setModelText(m_groupName, entry->name, newTarget); setModelText(m_groupTags, entry->tags.join(", "), newTarget);
    showColor(m_groupColors, entry->color);
    m_icons->setExclusive(false);
    for (auto *button : m_icons->buttons()) button->setChecked(entry->customIcon.isNull() && button->property("iconName").toString() == entry->icon);
    m_icons->setExclusive(true);
    const auto icon = entry->customIcon.isNull() ? appIcon(entry->icon) : QIcon(QPixmap::fromImage(entry->customIcon));
    m_iconPreview->setPixmap(icon.pixmap(QSize(ui::metrics::primaryIcon, ui::metrics::primaryIcon), devicePixelRatioF()));
    m_iconPreview->setToolTip(entry->customIcon.isNull() ? "Built-in group icon" : "Custom group icon stored in your tool library");
    m_clearIcon->setEnabled(!entry->customIcon.isNull());
    const auto count = std::count_if(entry->tools.cbegin(), entry->tools.cend(), [this](const auto &id) { return m_catalog->toolEnabled(id); });
    static_cast<ElidedLabel *>(m_memberCount)->setFullText(QString("%1 tool%2").arg(count).arg(count == 1 ? "" : "s"));
}
void ToolManagementPanel::refreshTools() {
    const auto ids = selectedTools(); m_toolProperties->setEnabled(!ids.isEmpty() && m_catalog->writable());
    static_cast<ElidedLabel *>(m_toolSummary)->setFullText(ids.isEmpty() ? "Select tools" : QString("%1 tool%2 selected").arg(ids.size()).arg(ids.size() == 1 ? "" : "s"));
    if (ids.isEmpty()) { static_cast<ElidedLabel *>(m_toolTags)->setFullText({}); showColor(m_toolColors, {}); return; }
    const auto *first = m_catalog->tool(ids.first()); if (!first) return;
    auto common = first->tags; bool mixed = false;
    for (const auto &id : ids) if (const auto *tool = m_catalog->tool(id)) {
        common.removeIf([&](const QString &tag) { return !tool->tags.contains(tag, Qt::CaseInsensitive); });
        mixed |= tool->color != first->color;
    }
    const auto full = common.isEmpty() ? (ids.size() == 1 ? "No tags" : "No shared tags") : common.join(", ");
    static_cast<ElidedLabel *>(m_toolTags)->setFullText(full);
    showColor(m_toolColors, first->color, mixed);
}
void ToolManagementPanel::filterGroups() {
    const auto query = m_groupSearch->text().trimmed();
    for (int i = 0; i < m_groups->count(); ++i) {
        auto *item = m_groups->item(i); item->setHidden(!item->data(Qt::UserRole + 1).toString().contains(query, Qt::CaseInsensitive));
    }
    if (auto *current = m_groups->currentItem(); current && current->isHidden()) {
        const QSignalBlocker block(m_groups); m_groups->setCurrentItem(nullptr); m_groups->clearSelection();
        m_groupId.clear(); refreshGroup();
    }
}
void ToolManagementPanel::filterTools() {
    const QSignalBlocker block(m_tools); const auto query = m_toolSearch->text().trimmed();
    for (int i = 0; i < m_tools->count(); ++i) {
        auto *item = m_tools->item(i); item->setHidden(!item->data(Qt::UserRole + 1).toString().contains(query, Qt::CaseInsensitive));
        if (item->isHidden()) item->setSelected(false);
    }
    refreshTools();
}
void ToolManagementPanel::focusEntry(QString entryId, QString toolId) {
    const auto *entry = m_catalog->entry(entryId);
    if (toolId.isEmpty() && entry && !entry->group && !entry->tools.isEmpty()) toolId = entry->tools.first();
    if (!toolId.isEmpty() && m_catalog->tool(toolId)) {
        m_toolSearch->clear(); m_tabs->setCurrentIndex(1); m_tools->clearSelection();
        if (auto *item = itemFor(m_tools, toolId)) { m_tools->setCurrentItem(item); item->setSelected(true); m_tools->scrollToItem(item); }
    } else if (entry && entry->group) {
        m_groupSearch->clear(); m_tabs->setCurrentIndex(0); m_groupId = entryId;
        m_groups->setCurrentItem(itemFor(m_groups, entryId)); m_groups->scrollToItem(itemFor(m_groups, entryId)); refreshGroup();
    }
    setError({});
}
void ToolManagementPanel::createGroup() {
    const auto id = m_catalog->addGroup("New group", "folder-open");
    if (id.isEmpty()) return;
    focusEntry(id); m_groupName->setFocus(); m_groupName->selectAll();
}
void ToolManagementPanel::editToolTag(bool add) {
    const auto tag = m_toolTag->text().trimmed();
    const bool success = add ? m_catalog->addToolTag(selectedTools(), tag) : m_catalog->removeToolTag(selectedTools(), tag);
    if (!success) { setError("Use one tag of 1–40 characters, without commas or control characters; up to 32 tags per tool."); return; }
    setError({}); m_toolTag->clear();
}
bool ToolManagementPanel::importGroupIcon(const QString &path) {
    const auto *entry = m_catalog->entry(m_groupId);
    if (!entry || !entry->group || !m_catalog->writable()) { setError("Select an editable group first."); return false; }
    const QFileInfo file(path);
    if (!file.isFile() || file.size() <= 0 || file.size() > 8 * 1024 * 1024) { setError("Choose an image file smaller than 8 MB."); return false; }
    QImageReader reader(path); reader.setDecideFormatFromContent(true); reader.setAutoTransform(true);
    const auto format = reader.format().toLower();
    if (format != "png" && format != "jpeg" && format != "jpg" && format != "webp") { setError("Use a PNG, JPEG or WebP image."); return false; }
    const auto size = reader.size();
    if (!size.isValid() || size.width() > 4096 || size.height() > 4096 || qint64(size.width()) * size.height() > 16 * 1024 * 1024) {
        setError("Group icons must be at most 4096 × 4096 pixels."); return false;
    }
    if (size.width() > ToolCatalog::maxIconSide || size.height() > ToolCatalog::maxIconSide)
        reader.setScaledSize(size.scaled(ToolCatalog::maxIconSide, ToolCatalog::maxIconSide, Qt::KeepAspectRatio));
    auto image = reader.read();
    if (image.isNull()) { setError("This image could not be read."); return false; }
    if (image.width() > ToolCatalog::maxIconSide || image.height() > ToolCatalog::maxIconSide)
        image = image.scaled(ToolCatalog::maxIconSide, ToolCatalog::maxIconSide, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    image = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    const auto id = entry->id, builtin = entry->icon;
    if (!m_catalog->setEntryIcon(id, builtin, image)) { setError("Could not save this group icon."); return false; }
    setError({}); return true;
}
void ToolManagementPanel::editMembership() {
    const auto *entry = m_catalog->entry(m_groupId); if (!entry || !entry->group) return;
    const auto entryId = entry->id;
    QDialog dialog(this); dialog.setObjectName("groupMembershipDialog"); dialog.setWindowTitle("Tools in " + entry->name);
    dialog.resize(390, 520); dialog.setMinimumSize(300, 330);
    auto *layout = new QVBoxLayout(&dialog); layout->setContentsMargins(8, 8, 8, 8); layout->setSpacing(ui::metrics::controlGap);
    auto *search = field("membershipSearch", "Search tools or tags"); enableSearchClear(search); layout->addWidget(search);
    auto *choices = list("membershipTools", "Tools in this group"); layout->addWidget(choices, 1);
    for (const auto &tool : m_catalog->availableTools()) {
        auto *item = new QListWidgetItem(appIcon(tool.icon), tool.name, choices);
        item->setData(Qt::UserRole, tool.id); item->setToolTip(metadataText(tool.name, tool.tags));
        item->setData(Qt::AccessibleTextRole, item->toolTip()); item->setCheckState(entry->tools.contains(tool.id) ? Qt::Checked : Qt::Unchecked);
        item->setSizeHint({0, qMax(ui::metrics::toolRow, choices->fontMetrics().height() + ui::metrics::sectionGap)});
    }
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel); buttons->button(QDialogButtonBox::Ok)->setText("Apply");
    for (auto *button : buttons->buttons()) button->setProperty("compactDrawingButton", true);
    buttons->button(QDialogButtonBox::Ok)->setObjectName("applyGroupMembership"); layout->addWidget(buttons);
    connect(search, &QLineEdit::textChanged, &dialog, [choices](const QString &query) {
        for (int i = 0; i < choices->count(); ++i) choices->item(i)->setHidden(!choices->item(i)->toolTip().contains(query.trimmed(), Qt::CaseInsensitive));
    });
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, [this, choices, entryId, &dialog] {
        const auto *current = m_catalog->entry(entryId); if (!current) { dialog.reject(); return; }
        QStringList checked;
        for (int i = 0; i < choices->count(); ++i) if (choices->item(i)->checkState() == Qt::Checked) checked.append(choices->item(i)->data(Qt::UserRole).toString());
        // Quarantined definitions are absent from this dialog. Editing visible
        // membership must not silently delete their saved placements.
        for (const auto &id : current->tools) if (!m_catalog->toolEnabled(id)) checked.append(id);
        QStringList ordered; for (const auto &id : current->tools) if (checked.contains(id)) ordered.append(id);
        for (const auto &id : checked) if (!ordered.contains(id)) ordered.append(id);
        // A cancelled pending-transform guard keeps this dialog and its choices.
        if (m_catalog->setEntryTools(entryId, ordered)) dialog.accept();
    });
    dialog.exec();
}
} // namespace toon
