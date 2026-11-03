#include "tool_navigation_panel.h"
#include "tool_catalog.h"
#include "brush_engine.h"
#include "icons.h"
#include "icon_column.h"
#include "ui_theme.h"
#include "ui_metrics.h"
#include <QAction>
#include <QApplication>
#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDropEvent>
#include <QElapsedTimer>
#include <QEvent>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QPainter>
#include <QPointer>
#include <QPushButton>
#include <QSignalBlocker>
#include <QScrollBar>
#include <QStyle>
#include <QStyledItemDelegate>
#include <QTabBar>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <functional>
#include <utility>

namespace toon {
namespace {
constexpr int idRole = Qt::UserRole;
constexpr int colorRole = Qt::UserRole + 1;
constexpr QSize rowPreviewSize{ui::metrics::previewWidth, ui::metrics::previewHeight};
constexpr QSize columnPreviewSize{ui::metrics::previewHeight, ui::metrics::previewHeight};
constexpr int rowGlyphSize = ui::metrics::primaryIcon;
constexpr int namedRowHeight = ui::metrics::toolRow;
constexpr int columnRowHeight = ui::metrics::railTarget;
constexpr QSize iconGridSize{52, ui::metrics::railTarget + 2};

QIcon rowSymbol(const QString &name, bool group = false, QSize previewSize = rowPreviewSize,
                const QImage &custom = {})
{
    // Every row reserves the same preview column. Draw symbols at one square
    // size inside it, so brush samples cannot stretch icons or shift labels.
    QIcon icon;
    for (const auto mode : {QIcon::Normal, QIcon::Disabled}) {
        for (const qreal ratio : {1., 2., 3.}) {
            QPixmap image(previewSize * ratio);
            image.setDevicePixelRatio(ratio);
            image.fill(Qt::transparent);
            QPainter painter(&image);
            const QRect glyph((previewSize.width() - rowGlyphSize) / 2,
                              (previewSize.height() - rowGlyphSize) / 2,
                              rowGlyphSize, rowGlyphSize);
            if (custom.isNull()) appIcon(name).paint(&painter, glyph, Qt::AlignCenter, mode);
            else {
                const auto size = custom.size().scaled(glyph.size(), Qt::KeepAspectRatio);
                const QRect target(glyph.center().x() - size.width() / 2 + 1,
                                   glyph.center().y() - size.height() / 2 + 1,
                                   size.width(), size.height());
                painter.setRenderHint(QPainter::SmoothPixmapTransform);
                if (mode == QIcon::Disabled) painter.setOpacity(.4);
                painter.drawImage(target, custom);
                painter.setOpacity(1);
            }
            if (group) {
                const QRect badge(glyph.x() + 11, glyph.y() + 11, 9, 8);
                // Clear under the badge instead of baking in a background color:
                // the selected row's own background must remain visible here.
                painter.setCompositionMode(QPainter::CompositionMode_Clear);
                painter.fillRect(badge.adjusted(-1, -1, 1, 0), Qt::transparent);
                painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
                appIcon("folder-open").paint(&painter, badge, Qt::AlignCenter, mode);
            }
            painter.end();
            icon.addPixmap(image, mode);
        }
    }
    return icon;
}

QIcon toolPreview(const ToolDefinition &tool)
{
    if (tool.engine != Canvas::Tool::Brush && tool.engine != Canvas::Tool::Eraser)
        return rowSymbol(tool.icon);
    auto brush = tool.state.brush;
    brush.color = QColor(ui::text);
    brush.backgroundColor = QColor(ui::base);
    // The stroke renderer reserves 20 px at each end. Render a larger sample
    // before reducing it, otherwise the row's 46 px icon becomes a single dab.
    const qreal originalSize = brush.size;
    brush.size = qBound(12.0, brush.size * 2, 32.0);
    brush.dual.size *= brush.size / qMax(1.0, originalSize);
    auto image = brushStrokePreview(brush, rowPreviewSize * 4, QColor(ui::base))
                     .scaled(rowPreviewSize * 2, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    image.setDevicePixelRatio(2);
    return QIcon(QPixmap::fromImage(image));
}

QToolButton *iconButton(QWidget *parent, const QString &id, const QString &icon, const QString &label)
{
    auto *button = new QToolButton(parent);
    button->setObjectName(id); button->setIcon(appIcon(icon));
    button->setIconSize({ui::metrics::actionIcon, ui::metrics::actionIcon});
    button->setToolTip(label); button->setAccessibleName(label);
    button->setToolButtonStyle(Qt::ToolButtonIconOnly);
    button->setProperty("compactDrawingAction", true);
    button->setFixedSize(ui::metrics::actionTarget, ui::metrics::actionTarget);
    return button;
}

void sizeColumnAction(QAbstractButton *button)
{
    button->setFixedSize(ui::metrics::actionTarget, ui::metrics::actionTarget);
    // A stylesheet's minimum content size is reapplied on polish. Give the
    // column its own box geometry so the shared padding cannot shrink targets.
    const auto content = ui::metrics::actionTarget - 2;
    button->setStyleSheet(QString("QToolButton, QPushButton { padding: 0px; min-width: %1px; max-width: %1px; "
                                 "min-height: %1px; max-height: %1px; }").arg(content));
}

void setupList(QListWidget *list)
{
    list->setIconSize(rowPreviewSize); list->setSpacing(0);
    list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    list->setTextElideMode(Qt::ElideRight);
    list->setUniformItemSizes(true);
}

void enableSearchClearButton(QLineEdit *search)
{
    search->setClearButtonEnabled(true);
    // Qt creates an unnamed QToolButton for this icon. Give it the same help
    // and accessible label as our explicit buttons; the shared tooltip filter
    // supplies the standard hover delay.
    for (auto *button : search->findChildren<QToolButton *>(QString(), Qt::FindDirectChildrenOnly)) {
        button->setObjectName(search->objectName() + "Clear");
        button->setToolTip("Clear tool search"); button->setAccessibleName("Clear tool search");
    }
}

class PlacementList final : public QListWidget {
public:
    using QListWidget::QListWidget;
    std::function<void(const QString &, int)> moved;
    std::function<void()> renameRequested;
protected:
    void keyPressEvent(QKeyEvent *event) override
    {
        if (event->key() == Qt::Key_F2 && renameRequested) {
            renameRequested(); event->accept(); return;
        }
        QListWidget::keyPressEvent(event);
    }
    void dropEvent(QDropEvent *event) override
    {
        const auto id = currentItem() ? currentItem()->data(idRole).toString() : QString();
        int destination = -1;
        {
            const QSignalBlocker blocker(this);
            QListWidget::dropEvent(event);
            if (event->isAccepted() && currentItem()) destination = row(currentItem());
        }
        if (destination >= 0 && !id.isEmpty() && moved) moved(id, destination);
    }
};

// Keep names in the model for accessibility, keyboard search and menus while
// reserving the entire visible row for an icon-sized target.
class NavigationDelegate final : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    bool iconsOnly = false;
    QSize previewSize = rowPreviewSize;
    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        auto size = QStyledItemDelegate::sizeHint(option, index);
        if (!iconsOnly) size.setHeight(qMax(size.height(), option.fontMetrics.height() + ui::metrics::sectionGap));
        return size;
    }
    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        QStyleOptionViewItem frame(option); initStyleOption(&frame, index);
        const auto icon = frame.icon;
        if (iconsOnly) {
            frame.text.clear(); frame.icon = {};
            frame.features &= ~(QStyleOptionViewItem::HasDisplay | QStyleOptionViewItem::HasDecoration);
        }
        const auto *widget = frame.widget;
        const auto *style = widget ? widget->style() : QApplication::style();
        style->drawControl(QStyle::CE_ItemViewItem, &frame, painter, widget);
        if (frame.state & QStyle::State_Selected) {
            painter->fillRect(QRect(frame.rect.left(), frame.rect.top() + 2, 2,
                                   qMax(0, frame.rect.height() - 4)), QColor(ui::accent));
        }
        if (iconsOnly) {
            const QRect symbol(frame.rect.center().x() - previewSize.width() / 2 + 1,
                               frame.rect.center().y() - previewSize.height() / 2 + 1,
                               previewSize.width(), previewSize.height());
            icon.paint(painter, symbol, Qt::AlignCenter,
                       frame.state & QStyle::State_Enabled ? QIcon::Normal : QIcon::Disabled);
        }
        const auto color = index.data(colorRole).value<QColor>();
        if (color.isValid()) {
            painter->save();
            painter->setPen(Qt::NoPen); painter->setBrush(color);
            painter->drawRoundedRect(QRect(frame.rect.left() + 2, frame.rect.top() + 5,
                                          3, qMax(0, frame.rect.height() - 10)), 1, 1);
            painter->restore();
        }
    }
};
} // namespace

// Creating a list (or changing its filter) must not paint every brush in a
// large library. Keep its ordinary symbols immediately available, then render
// just the visible stroke samples between input events. QImage settings stay
// implicitly shared, so validating a cached sample does not encode its assets.
class ToolPreviewQueue final : public QObject {
public:
    ToolPreviewQueue(ToolCatalog *catalog, QListWidget *list)
        : QObject(list), m_catalog(catalog), m_list(list)
    {
        m_timer.setSingleShot(true); m_timer.setInterval(8);
        connect(&m_timer, &QTimer::timeout, this, [this] { renderVisible(); });
        connect(list->verticalScrollBar(), &QScrollBar::valueChanged, this, [this] { schedule(); });
        connect(list->horizontalScrollBar(), &QScrollBar::valueChanged, this, [this] { schedule(); });
        list->viewport()->installEventFilter(this);
        connect(catalog, &ToolCatalog::settingsChanged, this, [this](const QString &id) {
            const auto *tool = m_catalog->tool(id);
            if (!tool || (tool->engine != Canvas::Tool::Brush && tool->engine != Canvas::Tool::Eraser)) return;
            m_cache.remove(id);
            for (int i = 0; i < m_list->count(); ++i)
                if (m_list->item(i)->data(idRole).toString() == id) { m_pending.insert(id); break; }
            schedule();
        });
    }

    void setDocument(Document *document)
    {
        disconnect(m_documentChanged);
        m_document = document;
        if (document) m_documentChanged = connect(document, &Document::changed, this, [this] { schedule(); });
        schedule();
    }
    Document *document() const { return m_document; }

    QIcon previewFor(const ToolDefinition &tool)
    {
        const bool brush = tool.engine == Canvas::Tool::Brush || tool.engine == Canvas::Tool::Eraser;
        if (brush) {
            auto cached = m_cache.constFind(tool.id);
            if (cached != m_cache.cend() && cached->brush == tool.state.brush) return cached->icon;
            m_cache.remove(tool.id);
            m_pending.insert(tool.id);
        }
        if (!m_symbols.contains(tool.icon)) m_symbols.insert(tool.icon, rowSymbol(tool.icon));
        return m_symbols.value(tool.icon);
    }

    void prune()
    {
        for (auto it = m_cache.begin(); it != m_cache.end();)
            if (!m_catalog->tool(it.key())) it = m_cache.erase(it); else ++it;
        m_pending.removeIf([this](const QString &id) { return !m_catalog->tool(id); });
    }

    void schedule()
    {
        if (m_pending.isEmpty() || !m_list->isVisible() || (m_document && m_document->strokeActive())) return;
        if (!m_timer.isActive()) m_timer.start();
    }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (watched == m_list->viewport()) {
            if (event->type() == QEvent::Show || event->type() == QEvent::Resize || event->type() == QEvent::Paint)
                schedule();
            else if (event->type() == QEvent::Hide) m_timer.stop();
        }
        return QObject::eventFilter(watched, event);
    }

private:
    void renderVisible()
    {
        if (!m_list->isVisible() || (m_document && m_document->strokeActive())) return;
        QElapsedTimer elapsed; elapsed.start();
        int built = 0;
        const QSignalBlocker blocker(m_list);
        for (int i = 0; i < m_list->count(); ++i) {
            auto *row = m_list->item(i); const auto id = row->data(idRole).toString();
            if (row->isHidden() || !m_pending.contains(id)
                || !m_list->visualItemRect(row).intersects(m_list->viewport()->rect())) continue;
            const auto *tool = m_catalog->tool(id);
            if (!tool) { m_pending.remove(id); continue; }
            const auto icon = toolPreview(*tool);
            m_cache.insert(id, {tool->state.brush, icon}); m_pending.remove(id);
            row->setIcon(icon);
            // A batch is bounded by both count and elapsed work. Never spin on
            // off-screen rows or while the artist is holding the pen down.
            if (++built >= 3 || elapsed.nsecsElapsed() >= 4'000'000) { schedule(); break; }
        }
    }

    struct CachedPreview { BrushSettings brush; QIcon icon; };
    ToolCatalog *m_catalog;
    QListWidget *m_list;
    QPointer<Document> m_document;
    QMetaObject::Connection m_documentChanged;
    QHash<QString, CachedPreview> m_cache;
    QHash<QString, QIcon> m_symbols;
    QSet<QString> m_pending;
    QTimer m_timer;
};

AddToolsDialog::AddToolsDialog(ToolCatalog *catalog, Context context, QString destinationId, QWidget *parent)
    : QDialog(parent), m_catalog(catalog), m_context(context), m_destinationId(std::move(destinationId))
{
    setObjectName("addToolsDialog"); setWindowTitle("Add tools"); setModal(true);
    resize(420, 590); setMinimumSize(330, 420);
    auto *layout = new QVBoxLayout(this); layout->setSpacing(10);
    m_tabs = new QTabBar(this); m_tabs->setObjectName("toolCreationTabs");
    m_tabs->addTab(appIcon("folder-open"), "Group");
    m_tabs->addTab(appIcon("paintbrush"), "Standalone Tool");
    m_tabs->setTabToolTip(0, "Create a group"); m_tabs->setTabToolTip(1, "Add an individual tool to Toolbox");
    m_tabs->setAccessibleTabName(0, "Group"); m_tabs->setAccessibleTabName(1, "Standalone Tool");
    m_tabs->setExpanding(true); layout->addWidget(m_tabs);
    m_tabs->setVisible(context == Context::Toolbox);
    if (context == Context::StandaloneTools) m_tabs->setCurrentIndex(1);
    if (context == Context::GroupTools) {
        const auto *entry = m_catalog->entry(m_destinationId);
        auto *destination = new QLabel(entry ? entry->name : QString(), this);
        destination->setObjectName("toolDestination"); destination->setTextFormat(Qt::PlainText);
        layout->addWidget(destination);
    }
    m_groupFields = new QWidget(this);
    auto *groupLayout = new QVBoxLayout(m_groupFields); groupLayout->setContentsMargins(0, 0, 0, 0);
    m_name = new QLineEdit(m_groupFields); m_name->setObjectName("newGroupName");
    m_name->setPlaceholderText("Group name"); m_name->setAccessibleName("Group name"); m_name->setMaxLength(120);
    groupLayout->addWidget(m_name);
    auto *iconRow = new QHBoxLayout; iconRow->setSpacing(3); m_icons = new QButtonGroup(this);
    const QList<QPair<QString, QString>> icons = {{"folder-open", "Folder"}, {"paintbrush", "Painting"},
        {"pen-tool", "Inking"}, {"image", "Backgrounds"}, {"palette", "Color"}, {"wand-sparkles", "Effects"},
        {featureIconName(FeatureIcon::PerspectiveGrid), "Guides"}, {"square-dashed", "Selections"}};
    for (int i = 0; i < icons.size(); ++i) {
        auto *button = iconButton(m_groupFields, "groupIcon_" + icons[i].first, icons[i].first, icons[i].second);
        button->setCheckable(true); button->setProperty("iconName", icons[i].first);
        m_icons->addButton(button, i); iconRow->addWidget(button);
        if (i == 0) button->setChecked(true);
    }
    iconRow->addStretch(); groupLayout->addLayout(iconRow); layout->addWidget(m_groupFields);
    m_search = new QLineEdit(this); m_search->setObjectName("toolSearch"); enableSearchClearButton(m_search);
    m_search->setPlaceholderText("Search tools"); m_search->setAccessibleName("Search tools"); layout->addWidget(m_search);
    m_choices = new QListWidget(this); m_choices->setObjectName("availableTools"); m_choices->setAccessibleName("Available raster tools");
    setupList(m_choices); layout->addWidget(m_choices, 1);
    m_previews = new ToolPreviewQueue(m_catalog, m_choices);
    m_variant = new QCheckBox("Create independent copy", this); m_variant->setObjectName("createToolVariant");
    m_variant->setToolTip("Give this tool a separate name and settings"); layout->addWidget(m_variant);
    m_variantName = new QLineEdit(this); m_variantName->setObjectName("newToolName"); m_variantName->setMaxLength(120);
    m_variantName->setPlaceholderText("Tool name"); m_variantName->setAccessibleName("New tool name"); layout->addWidget(m_variantName);
    m_error = new QLabel(this); m_error->setObjectName("toolCreationError"); m_error->setTextFormat(Qt::PlainText);
    m_error->setWordWrap(true); m_error->hide(); layout->addWidget(m_error);
    m_buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    m_buttons->button(QDialogButtonBox::Ok)->setObjectName("confirmAddTools"); layout->addWidget(m_buttons);
    connect(m_buttons, &QDialogButtonBox::accepted, this, &AddToolsDialog::accept);
    connect(m_buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_tabs, &QTabBar::currentChanged, this, &AddToolsDialog::updateMode);
    connect(m_search, &QLineEdit::textChanged, this, &AddToolsDialog::filterChoices);
    connect(m_name, &QLineEdit::textChanged, this, &AddToolsDialog::updateValidity);
    connect(m_variantName, &QLineEdit::textChanged, this, &AddToolsDialog::updateValidity);
    connect(m_variant, &QCheckBox::toggled, this, [this](bool checked) {
        m_variantName->setVisible(checked && !groupMode());
        if (checked && m_variantName->text().trimmed().isEmpty() && m_choices->currentItem())
            m_variantName->setText(m_choices->currentItem()->text() + " copy");
        updateValidity();
    });
    connect(m_choices, &QListWidget::itemChanged, this, &AddToolsDialog::updateValidity);
    connect(m_choices, &QListWidget::itemSelectionChanged, this, &AddToolsDialog::updateValidity);
    connect(m_catalog, &ToolCatalog::changed, this, &AddToolsDialog::refreshChoices);
    updateMode();
}

void AddToolsDialog::setPreviewDocument(Document *document) { m_previews->setDocument(document); }

bool AddToolsDialog::groupMode() const
{
    return m_context == Context::GroupTools || (m_context == Context::Toolbox && m_tabs->currentIndex() == 0);
}

QStringList AddToolsDialog::pickedTools() const
{
    QStringList ids;
    for (int i = 0; i < m_choices->count(); ++i) {
        const auto *item = m_choices->item(i);
        if ((groupMode() && item->checkState() == Qt::Checked) || (!groupMode() && item->isSelected()))
            ids.append(item->data(idRole).toString());
    }
    return ids;
}

void AddToolsDialog::updateMode()
{
    m_groupFields->setVisible(m_context == Context::Toolbox && groupMode());
    m_variant->setVisible(!groupMode());
    m_variantName->setVisible(!groupMode() && m_variant->isChecked());
    m_choices->setSelectionMode(QAbstractItemView::SingleSelection);
    m_buttons->button(QDialogButtonBox::Ok)->setText(m_context == Context::Toolbox && groupMode() ? "Create Group" : "Add");
    refreshChoices();
}

void AddToolsDialog::refreshChoices()
{
    const auto previous = pickedTools();
    const QSignalBlocker blocker(m_choices); m_choices->clear();
    m_previews->prune();
    const auto *destination = m_context == Context::GroupTools ? m_catalog->entry(m_destinationId) : nullptr;
    for (const auto &tool : m_catalog->tools()) {
        if (destination && destination->tools.contains(tool.id)) continue;
        auto *item = new QListWidgetItem(m_previews->previewFor(tool), tool.name, m_choices);
        item->setData(idRole, tool.id); item->setToolTip(tool.name); item->setSizeHint({0, namedRowHeight});
        if (groupMode()) { item->setFlags(item->flags() | Qt::ItemIsUserCheckable); item->setCheckState(previous.contains(tool.id) ? Qt::Checked : Qt::Unchecked); }
        else if (previous.contains(tool.id)) m_choices->setCurrentItem(item);
    }
    filterChoices(); updateValidity();
}

void AddToolsDialog::filterChoices()
{
    const auto term = m_search->text().trimmed();
    for (int i = 0; i < m_choices->count(); ++i) m_choices->item(i)->setHidden(!m_choices->item(i)->text().contains(term, Qt::CaseInsensitive));
    if (!groupMode() && m_choices->currentItem() && m_choices->currentItem()->isHidden()) {
        m_choices->clearSelection(); m_choices->setCurrentItem(nullptr);
    }
    updateValidity();
    m_previews->schedule();
}

void AddToolsDialog::updateValidity()
{
    const bool createGroup = m_context == Context::Toolbox && groupMode();
    const bool valid = createGroup ? !m_name->text().trimmed().isEmpty()
        : !pickedTools().isEmpty() && (groupMode() || !m_variant->isChecked() || !m_variantName->text().trimmed().isEmpty());
    m_buttons->button(QDialogButtonBox::Ok)->setEnabled(valid && m_catalog->writable());
}

void AddToolsDialog::accept()
{
    updateValidity();
    if (!m_buttons->button(QDialogButtonBox::Ok)->isEnabled()) return;
    const auto ids = pickedTools();
    bool ok = false;
    if (m_context == Context::Toolbox && groupMode()) {
        const auto icon = m_icons->checkedButton()->property("iconName").toString();
        ok = !m_catalog->addGroup(m_name->text().trimmed(), icon, ids).isEmpty();
    } else if (m_context == Context::GroupTools) {
        ok = m_catalog->addTools(m_destinationId, ids);
    } else if (m_variant->isChecked()) {
        ok = !m_catalog->duplicateTool(ids.first(), {}, m_variantName->text().trimmed()).isEmpty();
    } else {
        ok = !m_catalog->addStandalone(ids.first()).isEmpty();
    }
    if (ok) QDialog::accept();
    else { m_error->setText("The tool could not be added. Check the tool library or choose fewer items."); m_error->show(); }
}

ToolNavigationPanel::ToolNavigationPanel(ToolCatalog *catalog, bool toolbox, QWidget *parent)
    : QWidget(parent), m_catalog(catalog), m_toolbox(toolbox)
{
    setObjectName(toolbox ? "toolboxContent" : "toolListContent");
    setProperty("compactDrawingPanel", true);
    if (toolbox) {
        setFixedWidth(IconColumn::ContentWidth);
        setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
    } else setMinimumWidth(180);
    auto *layout = new QVBoxLayout(this);
    const int margin = toolbox ? 2 : ui::metrics::panelMargin;
    layout->setContentsMargins(margin, margin, margin, margin); layout->setSpacing(toolbox ? 2 : ui::metrics::controlGap);
    auto *header = new QBoxLayout(toolbox ? QBoxLayout::TopToBottom : QBoxLayout::LeftToRight);
    header->setSpacing(toolbox ? 2 : 3);
    const QString prefix = toolbox ? "toolbox" : "toolList";
    m_add = iconButton(this, prefix + "Add", "plus", toolbox ? "Add group or standalone tool" : "Add tools");
    m_remove = iconButton(this, prefix + "Remove", "trash", toolbox ? "Remove from Toolbox" : "Remove tool placement");
    if (toolbox) { sizeColumnAction(m_add); sizeColumnAction(m_remove); }
    header->addWidget(m_add, 0, toolbox ? Qt::AlignHCenter : Qt::Alignment());
    header->addWidget(m_remove, 0, toolbox ? Qt::AlignHCenter : Qt::Alignment());
    if (!toolbox) header->addStretch();
    auto *more = iconButton(this, prefix + "More", "ellipsis", "Tool organization actions");
    more->setPopupMode(QToolButton::InstantPopup); auto *menu = new QMenu(more);
    m_undo = m_catalog->history()->createUndoAction(this, "Undo organization"); m_undo->setObjectName("organizationUndo");
    m_redo = m_catalog->history()->createRedoAction(this, "Redo organization"); m_redo->setObjectName("organizationRedo");
    m_undo->setIcon(appIcon("undo-2")); m_redo->setIcon(appIcon("redo-2"));
    // The main window routes keyboard undo by focus; these actions are menu-only
    // so they cannot compete with the window's artwork undo shortcuts.
    for (auto *action : {m_undo, m_redo}) { action->setShortcutContext(Qt::WidgetWithChildrenShortcut); addAction(action); menu->addAction(action); }
    menu->addSeparator();
    auto *manage = menu->addAction(appIcon("settings-2"), "Manage tools…");
    manage->setObjectName(prefix + "Manage");
    connect(manage, &QAction::triggered, this, [this] {
        emit manageToolsRequested(m_catalog->selectedEntryId(), m_toolbox ? QString() : currentId());
    });
    auto *manageBrushes = menu->addAction(appIcon("brush"), "Manage brushes…");
    manageBrushes->setObjectName(prefix + "ManageBrushes");
    connect(manageBrushes, &QAction::triggered, this, &ToolNavigationPanel::manageBrushesRequested);
    if (!toolbox) {
        m_iconsOnlyAction = menu->addAction(appIcon("grid-2x2"), "Icons only");
        m_iconsOnlyAction->setObjectName("toolListIconsOnly"); m_iconsOnlyAction->setCheckable(true);
        connect(m_iconsOnlyAction, &QAction::toggled, this, &ToolNavigationPanel::setIconsOnly);
    }
    menu->addSeparator(); auto *importAction = menu->addAction(appIcon("folder-open"), "Import Photoshop brushes…");
    importAction->setObjectName("importBrushes"); connect(importAction, &QAction::triggered, this, &ToolNavigationPanel::importBrushesRequested);
    more->setMenu(menu);
    if (!toolbox) header->addWidget(more);
    layout->addLayout(header);
    if (!toolbox) {
        auto *filters = new QHBoxLayout;
        filters->setSpacing(ui::metrics::controlGap);
        m_tags = new QComboBox(this); m_tags->setObjectName("toolTagFilter");
        m_tags->setAccessibleName("Filter tools by tag"); m_tags->setToolTip("Filter tools by tag");
        m_tags->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
        m_tags->setMinimumContentsLength(1); m_tags->setMinimumWidth(76); m_tags->setMaximumWidth(120);
        filters->addWidget(m_tags, 1);
        m_search = new QLineEdit(this); m_search->setObjectName("toolPanelSearch");
        m_search->setPlaceholderText("Search"); m_search->setAccessibleName("Search tools");
        m_search->setMinimumWidth(0);
        enableSearchClearButton(m_search); filters->addWidget(m_search, 1);
        layout->addLayout(filters);
        connect(m_tags, &QComboBox::currentIndexChanged, this, [this] { setTagFilter(m_tags->currentData().toString()); });
        connect(m_search, &QLineEdit::textChanged, this, &ToolNavigationPanel::filterTools);
    }
    auto *list = new PlacementList(this); m_list = list;
    m_previews = new ToolPreviewQueue(m_catalog, m_list);
    m_list->setObjectName(toolbox ? "toolboxEntries" : "toolEntries"); m_list->setAccessibleName(toolbox ? "Toolbox groups and tools" : "Tools in selected Toolbox entry");
    setupList(m_list); m_list->setSelectionMode(QAbstractItemView::SingleSelection);
    m_list->setFrameShape(QFrame::NoFrame);
    m_list->setStyleSheet("QListWidget { border: 0px; border-radius: 0px; }");
    m_list->setDragDropMode(QAbstractItemView::InternalMove); m_list->setDefaultDropAction(Qt::MoveAction);
    m_list->setEditTriggers(toolbox ? QAbstractItemView::NoEditTriggers : QAbstractItemView::EditKeyPressed);
    m_list->setContextMenuPolicy(Qt::CustomContextMenu);
    auto *delegate = new NavigationDelegate(m_list);
    m_list->setItemDelegate(delegate);
    if (toolbox) {
        // Leave room for the group badge when a short column needs scrolling.
        m_list->setStyleSheet("QListWidget { border: 0px; border-radius: 0px; } "
                              "QScrollBar:vertical { width: 6px; }");
        delegate->iconsOnly = true; delegate->previewSize = columnPreviewSize;
        m_list->setIconSize(columnPreviewSize);
        m_list->setMinimumHeight(0);
        m_list->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
        list->renameRequested = [this] { renameSelected(); };
    }
    layout->addWidget(m_list, 1);
    m_empty = new QWidget(this); auto *emptyLayout = new QVBoxLayout(m_empty);
    emptyLayout->addStretch(); auto *emptyButton = new QPushButton(appIcon("plus"), toolbox ? "Add group or tool" : "Add Tools", m_empty);
    emptyButton->setObjectName(prefix + "EmptyAdd");
    if (toolbox) {
        emptyLayout->setContentsMargins(0, 0, 0, 0);
        emptyButton->setText({}); sizeColumnAction(emptyButton);
        emptyButton->setIconSize({ui::metrics::actionIcon, ui::metrics::actionIcon});
        emptyButton->setToolTip("Add group or standalone tool");
        emptyButton->setAccessibleName("Add group or standalone tool");
    }
    emptyLayout->addWidget(emptyButton, 0, toolbox ? Qt::AlignHCenter : Qt::Alignment()); emptyLayout->addStretch();
    layout->addWidget(m_empty, 1);
    if (!toolbox) {
        m_noMatches = new QLabel("No matching tools", this); m_noMatches->setObjectName("toolFilterNoMatches");
        m_noMatches->setAlignment(Qt::AlignCenter); layout->addWidget(m_noMatches, 1);
        m_clearFilters = iconButton(this, "toolClearFilters", "x", "Clear tool filters");
        layout->addWidget(m_clearFilters, 0, Qt::AlignHCenter);
        connect(m_clearFilters, &QToolButton::clicked, this, [this] {
            m_search->clear(); setTagFilter({});
        });
    }
    if (toolbox) { sizeColumnAction(more); layout->addWidget(more, 0, Qt::AlignHCenter); }
    connect(m_add, &QToolButton::clicked, this, &ToolNavigationPanel::openAddDialog);
    connect(emptyButton, &QPushButton::clicked, this, &ToolNavigationPanel::openAddDialog);
    connect(m_remove, &QToolButton::clicked, this, &ToolNavigationPanel::removeSelected);
    connect(m_list, &QListWidget::currentItemChanged, this, [this] {
        const auto id = currentId(); if (id.isEmpty()) return;
        if (m_toolbox) m_catalog->selectEntry(id); else m_catalog->activateTool(id);
    });
    connect(m_list, &QListWidget::itemClicked, this, [this](QListWidgetItem *item) {
        // A rejected activation has already restored the previous row. Do not
        // reopen its confirmation from the same click's second list signal.
        if (item != m_list->currentItem()) return;
        // Re-clicking the selected item also restores it after a temporary canvas shortcut.
        const auto id = item->data(idRole).toString();
        if (m_toolbox) m_catalog->selectEntry(id); else m_catalog->activateTool(id);
    });
    connect(m_list, &QListWidget::itemChanged, this, [this](QListWidgetItem *item) {
        const auto id = item->data(idRole).toString();
        if (m_toolbox) m_catalog->renameEntry(id, item->text()); else m_catalog->renameTool(id, item->text());
        QTimer::singleShot(0, this, &ToolNavigationPanel::refresh);
    });
    connect(m_list, &QListWidget::customContextMenuRequested, this, &ToolNavigationPanel::showContextMenu);
    list->moved = [this](const QString &id, int index) {
        if (m_toolbox) m_catalog->moveEntry(id, index);
        else if (m_tagFilter.isEmpty() && m_search->text().trimmed().isEmpty() && !m_iconsOnly)
            m_catalog->moveTool(m_catalog->selectedEntryId(), id, index);
        else refresh();
    };
    connect(m_catalog, &ToolCatalog::changed, this, &ToolNavigationPanel::refresh);
    applyViewMode(); refresh();
}

void ToolNavigationPanel::setPreviewDocument(Document *document) { m_previews->setDocument(document); }

QString ToolNavigationPanel::currentId() const
{
    return m_list->currentItem() && !m_list->currentItem()->isHidden()
        ? m_list->currentItem()->data(idRole).toString() : QString();
}

void ToolNavigationPanel::setIconsOnly(bool enabled)
{
    if (m_toolbox || m_iconsOnly == enabled) return;
    m_iconsOnly = enabled;
    const QSignalBlocker blocker(m_iconsOnlyAction); m_iconsOnlyAction->setChecked(enabled);
    applyViewMode(); refresh(); emit viewOptionsChanged();
}

void ToolNavigationPanel::applyViewMode()
{
    if (m_toolbox) return;
    auto *delegate = static_cast<NavigationDelegate *>(m_list->itemDelegate());
    delegate->iconsOnly = m_iconsOnly;
    m_list->setViewMode(m_iconsOnly ? QListView::IconMode : QListView::ListMode);
    m_list->setResizeMode(QListView::Adjust);
    m_list->setMovement(QListView::Static);
    m_list->setFlow(m_iconsOnly ? QListView::LeftToRight : QListView::TopToBottom);
    m_list->setWrapping(m_iconsOnly);
    m_list->setGridSize(m_iconsOnly ? iconGridSize : QSize());
    m_list->setIconSize(rowPreviewSize);
    // Inline editing cannot expose a usable name field in an icon tile.
    m_list->setEditTriggers(m_iconsOnly ? QAbstractItemView::NoEditTriggers : QAbstractItemView::EditKeyPressed);
    static_cast<PlacementList *>(m_list)->renameRequested = m_iconsOnly
        ? std::function<void()>([this] { renameSelected(); }) : std::function<void()>();
}

void ToolNavigationPanel::setTagFilter(const QString &tag)
{
    if (m_toolbox || m_tagFilter == tag) return;
    m_tagFilter = tag;
    refreshTagChoices(); filterTools(); emit viewOptionsChanged();
}

void ToolNavigationPanel::refreshTagChoices()
{
    if (m_toolbox) return;
    QStringList tags;
    const auto *entry = m_catalog->entry(m_catalog->selectedEntryId());
    if (entry && entry->group) tags = entry->tags;
    for (const auto &id : m_catalog->visibleToolIds()) if (const auto *tool = m_catalog->tool(id))
        for (const auto &tag : tool->tags) if (!tags.contains(tag, Qt::CaseInsensitive)) tags.append(tag);
    if (!m_tagFilter.isEmpty() && m_tagFilter != untaggedTagFilter() && !tags.contains(m_tagFilter, Qt::CaseInsensitive))
        tags.append(m_tagFilter);
    tags.sort(Qt::CaseInsensitive);
    const QSignalBlocker blocker(m_tags); m_tags->clear();
    m_tags->addItem("All tags", QString()); m_tags->addItem("Untagged", untaggedTagFilter());
    for (const auto &tag : tags) m_tags->addItem(tag, tag);
    int selected = 0;
    for (int i = 0; i < m_tags->count(); ++i)
        if (m_tags->itemData(i).toString().compare(m_tagFilter, Qt::CaseInsensitive) == 0) { selected = i; break; }
    m_tags->setCurrentIndex(selected);
}

void ToolNavigationPanel::filterTools()
{
    if (m_toolbox) return;
    const QSignalBlocker blocker(m_list);
    const auto *entry = m_catalog->entry(m_catalog->selectedEntryId());
    const QStringList groupTags = entry && entry->group ? entry->tags : QStringList();
    const auto search = m_search->text().trimmed();
    int visible = 0;
    m_list->clearSelection(); m_list->setCurrentItem(nullptr);
    for (int i = 0; i < m_list->count(); ++i) {
        auto *item = m_list->item(i); const auto *tool = m_catalog->tool(item->data(idRole).toString());
        if (!tool) continue;
        const bool tagMatches = m_tagFilter.isEmpty()
            || (m_tagFilter == untaggedTagFilter() ? tool->tags.isEmpty() && groupTags.isEmpty()
                : tool->tags.contains(m_tagFilter, Qt::CaseInsensitive) || groupTags.contains(m_tagFilter, Qt::CaseInsensitive));
        const bool searchMatches = search.isEmpty() || tool->name.contains(search, Qt::CaseInsensitive)
            || tool->tags.join(' ').contains(search, Qt::CaseInsensitive) || groupTags.join(' ').contains(search, Qt::CaseInsensitive);
        const bool show = tagMatches && searchMatches; item->setHidden(!show);
        if (show) {
            ++visible;
            if (tool->id == m_catalog->activeToolId()) m_list->setCurrentItem(item);
        }
    }
    const bool empty = m_list->count() == 0, noMatches = !empty && visible == 0;
    m_list->setVisible(!empty && !noMatches); m_empty->setVisible(empty);
    m_noMatches->setVisible(noMatches); m_clearFilters->setVisible(noMatches);
    m_remove->setEnabled(m_catalog->writable() && !currentId().isEmpty());
    // A filtered row number does not describe its position in the full group.
    // Icon tiles also use a different Qt drop layout, so reorder in named rows.
    const bool reorder = m_catalog->writable() && m_tagFilter.isEmpty() && search.isEmpty() && !m_iconsOnly;
    m_list->setDragEnabled(reorder); m_list->setAcceptDrops(reorder);
    m_previews->schedule();
}

void ToolNavigationPanel::refresh()
{
    // Selecting a row must not replace the list's model underneath a pointer
    // press or drag. Reuse rows while the ordered placement identities match.
    const QSignalBlocker blocker(m_list);
    m_previews->prune();
    QStringList ids;
    if (m_toolbox) { for (const auto &entry : m_catalog->entries()) ids.append(entry.id); }
    else ids = m_catalog->visibleToolIds();
    bool rebuild = ids.size() != m_list->count();
    if (!rebuild) for (int i = 0; i < ids.size(); ++i)
        if (m_list->item(i)->data(idRole).toString() != ids[i]) { rebuild = true; break; }
    if (rebuild) m_list->clear();
    m_list->clearSelection(); m_list->setCurrentItem(nullptr);
    int row = 0;
    auto append = [this, rebuild, &row](const QString &id, const QString &name, const QIcon &icon, const QString &tip,
                                        const QColor &color, bool current) {
        auto *item = rebuild ? new QListWidgetItem(m_list) : m_list->item(row);
        ++row;
        item->setData(idRole, id); item->setData(colorRole, color); item->setText(name); item->setIcon(icon);
        item->setData(Qt::AccessibleTextRole, name); item->setData(Qt::AccessibleDescriptionRole, tip);
        item->setFlags(m_catalog->writable() && !m_toolbox ? item->flags() | Qt::ItemIsEditable : item->flags() & ~Qt::ItemIsEditable);
        item->setToolTip(tip); item->setSizeHint(m_iconsOnly && !m_toolbox
            ? iconGridSize - QSize(2, 2) : QSize(0, m_toolbox ? columnRowHeight : namedRowHeight));
        if (current) m_list->setCurrentItem(item);
    };
    if (m_toolbox) {
        for (const auto &entry : m_catalog->entries()) {
            const auto *tool = !entry.group && !entry.tools.isEmpty() ? m_catalog->tool(entry.tools.first()) : nullptr;
            QString tip = entry.group ? QString("%1 · %2 tools").arg(entry.name).arg(entry.tools.size()) : entry.name;
            const auto tags = tool ? tool->tags : entry.tags;
            if (!tags.isEmpty()) tip += "\n" + tags.join(", ");
            append(entry.id, tool ? tool->name : entry.name,
                rowSymbol(tool ? tool->icon : entry.icon, entry.group, columnPreviewSize, entry.group ? entry.customIcon : QImage()),
                tip, tool ? tool->color : entry.color,
                entry.id == m_catalog->selectedEntryId());
        }
    } else {
        for (const auto &id : m_catalog->visibleToolIds()) if (const auto *tool = m_catalog->tool(id)) {
            const auto tip = tool->name + (tool->tags.isEmpty() ? QString() : "\n" + tool->tags.join(", "));
            append(id, tool->name, m_previews->previewFor(*tool), tip, tool->color, id == m_catalog->activeToolId());
        }
    }
    const bool empty = m_list->count() == 0;
    m_list->setVisible(!empty); m_empty->setVisible(empty);
    m_add->setEnabled(m_catalog->writable()); m_empty->setEnabled(m_catalog->writable());
    m_remove->setEnabled(m_catalog->writable() && !currentId().isEmpty());
    m_list->setDragEnabled(m_catalog->writable()); m_list->setAcceptDrops(m_catalog->writable());
    if (!m_toolbox) { refreshTagChoices(); filterTools(); }
}

void ToolNavigationPanel::openAddDialog()
{
    const auto *entry = m_catalog->entry(m_catalog->selectedEntryId());
    const auto context = m_toolbox || !entry ? AddToolsDialog::Context::Toolbox
        : entry->group ? AddToolsDialog::Context::GroupTools : AddToolsDialog::Context::StandaloneTools;
    AddToolsDialog dialog(m_catalog, context, entry ? entry->id : QString(), this);
    dialog.setPreviewDocument(m_previews->document()); dialog.exec();
}

void ToolNavigationPanel::removeSelected()
{
    const auto id = currentId(); if (id.isEmpty()) return;
    if (m_toolbox) m_catalog->removeEntry(id);
    else m_catalog->removeTool(m_catalog->selectedEntryId(), id);
}

void ToolNavigationPanel::duplicateSelected()
{
    const auto id = currentId(); if (id.isEmpty()) return;
    if (m_toolbox) {
        const auto *entry = m_catalog->entry(id);
        if (entry && !entry->group && !entry->tools.isEmpty()) m_catalog->duplicateTool(entry->tools.first(), id);
    } else m_catalog->duplicateTool(id, m_catalog->selectedEntryId());
}

void ToolNavigationPanel::renameSelected()
{
    auto *item = m_list->currentItem();
    if (!item || item->isHidden() || !m_catalog->writable()) return;
    if (!m_toolbox && !m_iconsOnly) { m_list->editItem(item); return; }
    const auto id = item->data(idRole).toString();
    QInputDialog dialog(this);
    dialog.setObjectName(m_toolbox ? "renameToolboxEntry" : "renameToolDefinition"); dialog.setWindowTitle("Rename");
    dialog.setLabelText("Name"); dialog.setTextValue(item->text()); dialog.resize(300, dialog.height());
    if (auto *field = dialog.findChild<QLineEdit *>()) {
        field->setMaxLength(120); field->setAccessibleName(m_toolbox ? "Toolbox entry name" : "Tool name");
    }
    if (dialog.exec() == QDialog::Accepted) {
        if (m_toolbox) m_catalog->renameEntry(id, dialog.textValue());
        else m_catalog->renameTool(id, dialog.textValue());
    }
}

void ToolNavigationPanel::showContextMenu(const QPoint &position)
{
    auto *item = m_list->itemAt(position); if (!item) return;
    const auto requestedId = item->data(idRole).toString();
    m_list->setCurrentItem(item);
    if (currentId() != requestedId) return; // A pending transform rejected activation.
    QMenu menu(this);
    const auto *entry = m_toolbox ? m_catalog->entry(currentId()) : nullptr;
    const bool group = entry && entry->group;
    const QString toolId = entry ? (group || entry->tools.isEmpty() ? QString() : entry->tools.first()) : currentId();
    const QString entryId = m_toolbox ? currentId() : m_catalog->selectedEntryId();
    if (!entry || !entry->group) {
        auto *duplicate = menu.addAction(appIcon("copy"), "Duplicate"); duplicate->setEnabled(m_catalog->writable());
        connect(duplicate, &QAction::triggered, this, &ToolNavigationPanel::duplicateSelected);
    }
    auto *rename = menu.addAction(appIcon("settings-2"), "Rename"); rename->setEnabled(m_catalog->writable());
    connect(rename, &QAction::triggered, this, &ToolNavigationPanel::renameSelected);
    auto *remove = menu.addAction(appIcon("trash"), "Remove"); remove->setEnabled(m_catalog->writable());
    connect(remove, &QAction::triggered, this, &ToolNavigationPanel::removeSelected);
    menu.addSeparator();
    auto *tagsMenu = menu.addMenu("Tags"); tagsMenu->setObjectName("toolTagsMenu");
    tagsMenu->setEnabled(m_catalog->writable());
    QStringList available, assigned;
    if (group) assigned = entry->tags;
    else if (const auto *tool = m_catalog->tool(toolId)) assigned = tool->tags;
    for (const auto &tool : m_catalog->tools()) for (const auto &tag : tool.tags)
        if (!available.contains(tag, Qt::CaseInsensitive)) available.append(tag);
    for (const auto &placement : m_catalog->entries()) for (const auto &tag : placement.tags)
        if (!available.contains(tag, Qt::CaseInsensitive)) available.append(tag);
    available.sort(Qt::CaseInsensitive);
    for (const auto &tag : available) {
        auto *action = tagsMenu->addAction(tag); action->setCheckable(true);
        action->setChecked(assigned.contains(tag, Qt::CaseInsensitive));
        connect(action, &QAction::triggered, this, [this, group, entryId, toolId, tag](bool checked) {
            bool ok = false;
            if (group) {
                const auto *target = m_catalog->entry(entryId); if (!target) return;
                auto tags = target->tags;
                if (checked) tags.append(tag);
                else tags.removeIf([&tag](const QString &value) { return value.compare(tag, Qt::CaseInsensitive) == 0; });
                ok = m_catalog->setEntryTags(entryId, tags);
            } else if (checked) ok = m_catalog->addToolTag({toolId}, tag);
            else ok = m_catalog->removeToolTag({toolId}, tag);
            if (!ok) emit m_catalog->warning("Could not update the tag. Check that the library is writable and there are no more than 32 tags.");
        });
    }
    if (!available.isEmpty()) tagsMenu->addSeparator();
    auto *newTag = tagsMenu->addAction("New tag…"); newTag->setObjectName("newToolTag");
    newTag->setToolTip("Create one tag without commas, up to 40 characters");
    connect(newTag, &QAction::triggered, this, [this, group, entryId, toolId] {
        QInputDialog dialog(this); dialog.setObjectName("newToolTagDialog"); dialog.setWindowTitle("New tag");
        dialog.setLabelText("Tag name"); dialog.resize(300, dialog.height());
        if (auto *field = dialog.findChild<QLineEdit *>()) {
            field->setMaxLength(ToolCatalog::maxTagLength); field->setAccessibleName("Tag name");
            field->setToolTip("One tag without commas, up to 40 characters");
        }
        if (dialog.exec() != QDialog::Accepted || dialog.textValue().trimmed().isEmpty()) return;
        bool ok = false;
        if (group) {
            const auto *target = m_catalog->entry(entryId); if (!target) return;
            auto tags = target->tags; tags.append(dialog.textValue()); ok = m_catalog->setEntryTags(entryId, tags);
        } else ok = m_catalog->addToolTag({toolId}, dialog.textValue());
        if (!ok) emit m_catalog->warning("Could not add the tag. Use one tag without commas (40 characters maximum), and up to 32 tags.");
    });
    auto *manage = menu.addAction(appIcon("settings-2"), group ? "Manage group…" : "Manage tool…");
    manage->setObjectName("manageSelectedTool");
    connect(manage, &QAction::triggered, this, [this, entryId, toolId] { emit manageToolsRequested(entryId, toolId); });
    menu.addSeparator(); menu.addAction(m_undo); menu.addAction(m_redo);
    menu.exec(m_list->viewport()->mapToGlobal(position));
}
} // namespace toon
