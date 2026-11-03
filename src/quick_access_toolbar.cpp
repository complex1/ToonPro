#include "quick_access_toolbar.h"

#include "canvas.h"
#include "icons.h"
#include "tool_catalog.h"
#include "ui_metrics.h"
#include "ui_theme.h"
#include <QAction>
#include <QColorDialog>
#include <QDialog>
#include <QDialogButtonBox>
#include <QEvent>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QPainter>
#include <QPushButton>
#include <QRegularExpression>
#include <QSettings>
#include <QSignalBlocker>
#include <QToolButton>
#include <QVBoxLayout>
#include <algorithm>
#include <utility>

namespace toon {
namespace {
constexpr auto settingsKey = "quickAccess/v1";
constexpr int maxSettingsBytes = 32768;
struct Navigation { const char *id; const char *name; const char *icon; };
constexpr Navigation navigation[] = {
    {"hand", "Hand", "hand"}, {"zoom-in", "Zoom in", "zoom-in"},
    {"zoom-out", "Zoom out", "minus"}, {"fit-sheet", "Fit sheet", "focus"},
    {"reset-rotation", "Reset view rotation", "rotate-ccw"}
};
const Navigation *navigationFor(const QString &id)
{
    for (const auto &entry : navigation) if (id == QLatin1String(entry.id)) return &entry;
    return nullptr;
}
QString kindName(QuickAccessItem::Kind kind)
{
    switch (kind) {
    case QuickAccessItem::Kind::Tool: return "tool";
    case QuickAccessItem::Kind::Color: return "color";
    case QuickAccessItem::Kind::Navigation: return "navigation";
    }
    return {};
}
QIcon colorIcon(QColor color)
{
    QIcon result;
    for (const qreal ratio : {1., 2., 3.}) {
        QPixmap pixmap(QSize(18, 18) * ratio);
        pixmap.setDevicePixelRatio(ratio); pixmap.fill(Qt::transparent);
        QPainter painter(&pixmap);
        for (int y = 2; y < 16; y += 4) for (int x = 2; x < 16; x += 4)
            painter.fillRect(QRect(x, y, qMin(4, 16 - x), qMin(4, 16 - y)),
                             ((x + y) / 4) % 2 ? QColor("#999999") : QColor("#eeeeee"));
        painter.fillRect(QRect(2, 2, 14, 14), color);
        painter.setPen(QColor(ui::muted)); painter.drawRect(QRect(1, 1, 15, 15));
        painter.end(); result.addPixmap(pixmap);
    }
    return result;
}
QString firstTool(const ToolCatalog *catalog, Canvas::Tool engine)
{
    if (catalog) for (const auto &tool : catalog->tools()) if (tool.engine == engine) return tool.id;
    return {};
}
class ToolChoiceList final : public QListWidget {
public:
    using QListWidget::QListWidget;
    void refreshRows()
    {
        const int height = qMax(ui::metrics::toolRow, fontMetrics().height() + 2 * ui::metrics::controlGap);
        for (int row = 0; row < count(); ++row) item(row)->setSizeHint({180, height});
    }
protected:
    void changeEvent(QEvent *event) override
    {
        QListWidget::changeEvent(event);
        if (event->type() == QEvent::FontChange || event->type() == QEvent::StyleChange) refreshRows();
    }
};
} // namespace

QString QuickAccessItem::id() const { return kindName(kind) + ':' + value; }

QuickAccessToolbar::QuickAccessToolbar(Canvas *canvas, ToolCatalog *catalog, QSettings *settings, QWidget *parent)
    : QToolBar("Quick access", parent), m_canvas(canvas), m_catalog(catalog), m_settings(settings)
{
    setObjectName("quickAccessToolbar"); setAccessibleName("Quick access toolbar");
    setAllowedAreas(Qt::TopToolBarArea); setMovable(false); setFloatable(false);
    setOrientation(Qt::Horizontal); setIconSize({ui::metrics::actionIcon, ui::metrics::actionIcon});
    setToolButtonStyle(Qt::ToolButtonIconOnly);
    setMinimumWidth(100); setMinimumHeight(ui::metrics::actionTarget + ui::metrics::controlGap);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    // Keep the strip compact without fixing its height: a contextual text
    // control supplied by the workspace can grow with the application font.
    setStyleSheet(QString("QToolBar#quickAccessToolbar { spacing: 2px; padding: 2px; }"
                          "QToolBar#quickAccessToolbar::separator { width: 1px; margin: 6px 3px; }"
                          "QToolButton[quickAccessPin=true]:checked { border-bottom: 1px solid %1; }")
                      .arg(ui::accent));
    setContextMenuPolicy(Qt::CustomContextMenu);
    connect(this, &QWidget::customContextMenuRequested, this, &QuickAccessToolbar::showItemMenu);

    // Keep the exit and customization controls before pins, so Qt's native
    // overflow never strands an artist in the drawing workspace.
    m_drawingModeAction = addAction(appIcon("maximize-2"), "Drawing mode (Tab)");
    m_drawingModeAction->setObjectName("drawingModeAction");
    m_drawingModeAction->setCheckable(true); configureButton(m_drawingModeAction);
    connect(m_drawingModeAction, &QAction::triggered, this, [this] {
        // The window decides whether a mode transition is currently possible.
        // Reflect its actual state rather than QAction's optimistic toggle.
        setDrawingMode(m_drawingMode); emit drawingModeRequested();
    });
    m_addAction = addAction(appIcon("plus"), "Customize quick access");
    m_addAction->setObjectName("quickAccessAdd"); configureButton(m_addAction);
    connect(m_addAction, &QAction::triggered, this, &QuickAccessToolbar::showAddMenu);
    addSeparator();
    if (auto *overflow = findChild<QToolButton *>("qt_toolbar_ext_button")) {
        overflow->setAccessibleName("More quick access items");
        overflow->setToolTip("More quick access items");
    }
    restore(); rebuildItems();
    if (catalog) connect(catalog, &ToolCatalog::changed, this, &QuickAccessToolbar::refreshCatalog);
    if (canvas) {
        connect(canvas, &Canvas::brushChanged, this, &QuickAccessToolbar::refreshActive);
        connect(canvas, &Canvas::toolChanged, this, &QuickAccessToolbar::refreshActive);
        connect(canvas, &Canvas::documentChanged, this, &QuickAccessToolbar::refreshAvailability);
    }
}

void QuickAccessToolbar::configureButton(QAction *action)
{
    action->setToolTip(action == m_drawingModeAction
        ? (m_drawingMode ? "Return to workspace (Tab on canvas)" : "Drawing mode (Tab on canvas)")
        : action->text());
    if (auto *button = qobject_cast<QToolButton *>(widgetForAction(action))) {
        button->setObjectName(action->objectName() + "Button");
        button->setAccessibleName(action->text()); button->setToolTip(action->toolTip());
        if (action == m_drawingModeAction)
            button->setAccessibleDescription("Tab switches drawing mode on the canvas; in panels it moves between controls.");
        button->setProperty("compactDrawingAction", true);
        const bool pin = action->data().toString().startsWith("tool:") ||
                         action->data().toString().startsWith("color:");
        button->setProperty("quickAccessPin", pin || action->data().toString() == "navigation:hand");
        const int iconSize = pin ? ui::metrics::primaryIcon : ui::metrics::actionIcon;
        // Keyboard traversal can reach pins without pen clicks taking focus
        // from a property draft before its action has been accepted.
        button->setFocusPolicy(Qt::TabFocus); button->setIconSize({iconSize, iconSize});
        button->setFixedSize(ui::metrics::actionTarget, ui::metrics::actionTarget);
    }
}

void QuickAccessToolbar::setDrawingMode(bool enabled)
{
    m_drawingMode = enabled;
    const QSignalBlocker blocker(m_drawingModeAction);
    m_drawingModeAction->setChecked(enabled);
    m_drawingModeAction->setText(enabled ? "Return to workspace (Tab)" : "Drawing mode (Tab)");
    m_drawingModeAction->setIcon(appIcon(enabled ? "panels-top-left" : "maximize-2"));
    configureButton(m_drawingModeAction);
}

bool QuickAccessToolbar::addTool(const QString &toolId)
{
    if (!m_catalog || !m_catalog->tool(toolId)) return false;
    return appendItem({QuickAccessItem::Kind::Tool, toolId});
}
bool QuickAccessToolbar::addColor(const QColor &color)
{
    return color.isValid() && appendItem({QuickAccessItem::Kind::Color, color.name(QColor::HexArgb)});
}
bool QuickAccessToolbar::addNavigation(const QString &navigationId)
{
    return navigationFor(navigationId) && appendItem({QuickAccessItem::Kind::Navigation, navigationId});
}
bool QuickAccessToolbar::appendItem(QuickAccessItem item)
{
    if (m_items.size() >= maxItems || item.value.size() > 256 ||
        std::any_of(m_items.cbegin(), m_items.cend(), [&](const auto &existing) { return existing.id() == item.id(); })) return false;
    m_items.append(std::move(item)); save(); rebuildItems(); return true;
}
bool QuickAccessToolbar::removeItem(const QString &itemId)
{
    const auto index = std::find_if(m_items.cbegin(), m_items.cend(), [&](const auto &item) { return item.id() == itemId; });
    if (index == m_items.cend()) return false;
    m_items.removeAt(index - m_items.cbegin()); save(); rebuildItems(); return true;
}
bool QuickAccessToolbar::moveItem(const QString &itemId, int index)
{
    if (index < 0 || index >= m_items.size()) return false;
    const auto item = std::find_if(m_items.cbegin(), m_items.cend(), [&](const auto &entry) { return entry.id() == itemId; });
    if (item == m_items.cend()) return false;
    const auto oldIndex = item - m_items.cbegin();
    if (oldIndex == index) return true;
    m_items.move(oldIndex, index); save(); rebuildItems(); return true;
}
QAction *QuickAccessToolbar::itemAction(const QString &itemId) const { return m_itemActions.value(itemId); }

void QuickAccessToolbar::restore()
{
    if (!m_settings || !m_settings->contains(settingsKey)) {
        if (m_catalog && m_catalog->tool(m_catalog->activeToolId()))
            m_items.append({QuickAccessItem::Kind::Tool, m_catalog->activeToolId()});
        const auto eraser = firstTool(m_catalog, Canvas::Tool::Eraser);
        if (!eraser.isEmpty() && (m_items.isEmpty() || m_items.first().value != eraser))
            m_items.append({QuickAccessItem::Kind::Tool, eraser});
        m_items.append({QuickAccessItem::Kind::Color, "#ff202530"});
        m_items.append({QuickAccessItem::Kind::Color, "#ffffffff"});
        m_items.append({QuickAccessItem::Kind::Navigation, "hand"});
        m_items.append({QuickAccessItem::Kind::Navigation, "fit-sheet"});
        save(); return;
    }
    const auto value = m_settings->value(settingsKey);
    if (value.metaType().id() != QMetaType::QByteArray && value.metaType().id() != QMetaType::QString) return;
    const auto raw = value.toByteArray();
    if (raw.size() > maxSettingsBytes) return;
    QJsonParseError error;
    const auto json = QJsonDocument::fromJson(raw, &error);
    if (error.error != QJsonParseError::NoError || !json.isObject()) return;
    const auto object = json.object();
    if (object.size() != 2 || object.value("version") != QJsonValue(1) || !object.value("items").isArray()) return;
    const auto entries = object.value("items").toArray();
    if (entries.size() > maxItems) return;
    QVector<QuickAccessItem> result;
    QSet<QString> seen;
    static const QRegularExpression colorPattern("^#[0-9a-fA-F]{8}$");
    for (const auto &entry : entries) {
        if (!entry.isObject()) return;
        const auto item = entry.toObject();
        if (item.size() != 2 || !item.value("kind").isString() || !item.value("value").isString()) return;
        const auto kind = item.value("kind").toString();
        QString text = item.value("value").toString();
        if (text.isEmpty() || text.size() > 256 || text.contains(QChar::Null)) return;
        QuickAccessItem candidate;
        if (kind == "tool") {
            candidate.kind = QuickAccessItem::Kind::Tool;
            if (!m_catalog || !m_catalog->tool(text)) continue; // A removed brush is not a corrupt preference file.
        } else if (kind == "color") {
            if (!colorPattern.match(text).hasMatch()) return;
            candidate.kind = QuickAccessItem::Kind::Color; text = text.toLower();
        } else if (kind == "navigation") {
            if (!navigationFor(text)) return;
            candidate.kind = QuickAccessItem::Kind::Navigation;
        } else return;
        candidate.value = text;
        if (seen.contains(candidate.id())) return;
        seen.insert(candidate.id()); result.append(std::move(candidate));
    }
    m_items = std::move(result);
}
void QuickAccessToolbar::save()
{
    if (!m_settings) return;
    QJsonArray entries;
    for (const auto &item : m_items) entries.append(QJsonObject{{"kind", kindName(item.kind)}, {"value", item.value}});
    m_settings->setValue(settingsKey, QJsonDocument(QJsonObject{{"version", 1}, {"items", entries}}).toJson(QJsonDocument::Compact));
}

void QuickAccessToolbar::rebuildItems()
{
    for (auto *action : std::as_const(m_itemActions)) { removeAction(action); action->deleteLater(); }
    m_itemActions.clear();
    for (auto *separator : std::as_const(m_groupSeparators)) { removeAction(separator); separator->deleteLater(); }
    m_groupSeparators.clear();
    const QuickAccessItem *previous = nullptr;
    for (const auto &item : std::as_const(m_items)) {
        // Delimit adjacent kinds without sorting the artist's saved pins.
        if (previous && previous->kind != item.kind) m_groupSeparators.append(addSeparator());
        previous = &item;
        auto *action = new QAction(this);
        action->setObjectName("quickAccessItem/" + item.id());
        action->setData(item.id());
        action->setCheckable(item.kind != QuickAccessItem::Kind::Navigation || item.value == "hand");
        if (item.kind == QuickAccessItem::Kind::Color) {
            action->setText("Foreground " + item.value); action->setIcon(colorIcon(QColor(item.value)));
        } else if (item.kind == QuickAccessItem::Kind::Navigation) {
            const auto *entry = navigationFor(item.value);
            action->setText(QString::fromLatin1(entry->name)); action->setIcon(appIcon(QString::fromLatin1(entry->icon)));
        }
        addAction(action); m_itemActions.insert(item.id(), action);
        connect(action, &QAction::triggered, this, [this, item] { activate(item); });
        configureButton(action);
    }
    refreshCatalog();
    refreshAvailability();
}
void QuickAccessToolbar::refreshCatalog()
{
    bool removed = false;
    for (int index = m_items.size() - 1; index >= 0; --index) {
        const auto &item = m_items[index];
        if (item.kind != QuickAccessItem::Kind::Tool) continue;
        const auto *tool = m_catalog ? m_catalog->tool(item.value) : nullptr;
        if (!tool) { m_items.removeAt(index); removed = true; continue; }
        if (auto *action = itemAction(item.id())) {
            if (action->text() != tool->name || action->property("toolIcon").toString() != tool->icon) {
                action->setText(tool->name); action->setIcon(appIcon(tool->icon));
                action->setProperty("toolIcon", tool->icon); configureButton(action);
            }
        }
    }
    if (removed) { save(); rebuildItems(); return; }
    refreshActive();
}
void QuickAccessToolbar::refreshActive()
{
    // Only compare a bounded set of IDs/colors. Never sample artwork or build
    // brush previews when the foreground/tool changes during a pen session.
    for (const auto &item : std::as_const(m_items)) if (auto *action = itemAction(item.id())) {
        bool checked = false;
        if (item.kind == QuickAccessItem::Kind::Tool) checked = m_catalog && m_catalog->activeToolId() == item.value;
        else if (item.kind == QuickAccessItem::Kind::Color)
            checked = m_canvas && m_canvas->brush.color.name(QColor::HexArgb) == item.value;
        else if (item.value == "hand") checked = m_canvas && m_canvas->tool() == Canvas::Tool::Hand;
        if (action->isCheckable() && action->isChecked() != checked) action->setChecked(checked);
    }
}
void QuickAccessToolbar::refreshAvailability()
{
    const bool available = m_canvas && m_canvas->hasProject();
    if (m_drawingModeAction->isEnabled() != available) m_drawingModeAction->setEnabled(available);
    for (auto *action : std::as_const(m_itemActions))
        if (action->isEnabled() != available) action->setEnabled(available);
}
void QuickAccessToolbar::activate(const QuickAccessItem &item)
{
    if (!m_canvas || !m_catalog || !m_canvas->hasProject()) return;
    // Check only on activation, not on each tablet packet. Preserve pending
    // transform decisions through the catalog's existing activation guard.
    if (m_canvas->document()->strokeActive() || m_canvas->selectionGestureActive()) {
        refreshActive();
        return;
    }
    bool activated = true;
    if (item.kind == QuickAccessItem::Kind::Tool) activated = m_catalog->activateTool(item.value);
    else if (item.kind == QuickAccessItem::Kind::Color) m_canvas->setBrushColor(QColor(item.value));
    else if (item.value == "hand") activated = m_catalog->activateTool(firstTool(m_catalog, Canvas::Tool::Hand));
    else if (item.value == "zoom-in") m_canvas->setZoom(m_canvas->zoom() * 1.25);
    else if (item.value == "zoom-out") m_canvas->setZoom(m_canvas->zoom() / 1.25);
    else if (item.value == "fit-sheet") m_canvas->fitSheet();
    else if (item.value == "reset-rotation") m_canvas->setRotation(0);
    refreshActive();
    if (activated) m_canvas->setFocus(Qt::OtherFocusReason);
}

void QuickAccessToolbar::showAddMenu()
{
    QMenu menu(this); menu.setObjectName("quickAccessAddMenu");
    auto *currentTool = menu.addAction(appIcon("pin"), "Pin current tool");
    currentTool->setEnabled(m_catalog && m_catalog->tool(m_catalog->activeToolId()) && m_items.size() < maxItems &&
                            !itemAction("tool:" + m_catalog->activeToolId()));
    connect(currentTool, &QAction::triggered, this, [this] { if (m_catalog) addTool(m_catalog->activeToolId()); });
    auto *currentColor = menu.addAction(appIcon("palette"), "Pin current color");
    currentColor->setEnabled(m_canvas && m_items.size() < maxItems &&
                             !itemAction("color:" + m_canvas->brush.color.name(QColor::HexArgb)));
    connect(currentColor, &QAction::triggered, this, [this] { if (m_canvas) addColor(m_canvas->brush.color); });
    menu.addSeparator();
    auto *tool = menu.addAction(appIcon("brush"), "Add tool…");
    tool->setEnabled(m_catalog && m_items.size() < maxItems);
    connect(tool, &QAction::triggered, this, &QuickAccessToolbar::chooseTool);
    auto *color = menu.addAction(appIcon("palette"), "Add color…");
    color->setEnabled(m_items.size() < maxItems);
    connect(color, &QAction::triggered, this, [this] {
        const auto selected = QColorDialog::getColor(m_canvas ? m_canvas->brush.color : QColor(Qt::black), this,
                                                     "Pin color", QColorDialog::ShowAlphaChannel);
        if (selected.isValid()) addColor(selected);
    });
    auto *navigationMenu = menu.addMenu(appIcon("hand"), "Navigation");
    for (const auto &entry : navigation) {
        const auto id = QString::fromLatin1(entry.id);
        auto *action = navigationMenu->addAction(appIcon(QString::fromLatin1(entry.icon)), QString::fromLatin1(entry.name));
        action->setEnabled(m_items.size() < maxItems && !itemAction("navigation:" + id));
        connect(action, &QAction::triggered, this, [this, id] { addNavigation(id); });
    }
    const auto *button = widgetForAction(m_addAction);
    menu.exec(mapToGlobal(button ? button->geometry().bottomLeft() : QPoint(0, height())));
}
void QuickAccessToolbar::chooseTool()
{
    if (!m_catalog) return;
    QDialog dialog(this); dialog.setObjectName("quickAccessToolChooser"); dialog.setWindowTitle("Add tool to quick access");
    auto *layout = new QVBoxLayout(&dialog);
    auto *search = new QLineEdit(&dialog); search->setObjectName("quickAccessToolSearch");
    search->setPlaceholderText("Search tools"); search->setAccessibleName("Search tools"); search->setClearButtonEnabled(true);
    auto *list = new ToolChoiceList(&dialog); list->setObjectName("quickAccessToolChoices"); list->setAccessibleName("Available tools");
    list->setIconSize({18, 18}); list->setUniformItemSizes(true);
    for (const auto &tool : m_catalog->tools()) {
        if (itemAction("tool:" + tool.id)) continue;
        auto *item = new QListWidgetItem(appIcon(tool.icon), tool.name, list);
        item->setData(Qt::UserRole, tool.id); item->setData(Qt::UserRole + 1, tool.name + ' ' + tool.category + ' ' + tool.tags.join(' '));
        item->setToolTip(tool.name);
    }
    list->refreshRows();
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    buttons->button(QDialogButtonBox::Ok)->setText("Add"); buttons->button(QDialogButtonBox::Ok)->setEnabled(false);
    layout->addWidget(search); layout->addWidget(list); layout->addWidget(buttons);
    connect(search, &QLineEdit::textChanged, &dialog, [list, buttons](const QString &text) {
        for (int row = 0; row < list->count(); ++row)
            list->item(row)->setHidden(!list->item(row)->data(Qt::UserRole + 1).toString().contains(text.trimmed(), Qt::CaseInsensitive));
        buttons->button(QDialogButtonBox::Ok)->setEnabled(list->currentItem() && !list->currentItem()->isHidden());
    });
    connect(list, &QListWidget::currentItemChanged, &dialog, [buttons](QListWidgetItem *item) {
        buttons->button(QDialogButtonBox::Ok)->setEnabled(item && !item->isHidden());
    });
    connect(list, &QListWidget::itemDoubleClicked, &dialog, [&dialog](QListWidgetItem *) { dialog.accept(); });
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    dialog.resize(340, 420); search->setFocus();
    if (dialog.exec() == QDialog::Accepted && list->currentItem() && !list->currentItem()->isHidden())
        addTool(list->currentItem()->data(Qt::UserRole).toString());
}
void QuickAccessToolbar::showItemMenu(const QPoint &position)
{
    const auto *action = actionAt(position);
    if (!action) return;
    const auto id = action->data().toString();
    const auto entry = std::find_if(m_items.cbegin(), m_items.cend(), [&](const auto &item) { return item.id() == id; });
    if (entry == m_items.cend()) return;
    const int index = entry - m_items.cbegin();
    QMenu menu(this); menu.setObjectName("quickAccessItemMenu");
    auto *left = menu.addAction(appIcon("chevron-left"), "Move left"); left->setEnabled(index > 0);
    auto *right = menu.addAction(appIcon("chevron-right"), "Move right"); right->setEnabled(index + 1 < m_items.size());
    menu.addSeparator(); auto *remove = menu.addAction(appIcon("trash"), "Remove from quick access");
    const auto *selected = menu.exec(mapToGlobal(position));
    if (selected == left) moveItem(id, index - 1);
    else if (selected == right) moveItem(id, index + 1);
    else if (selected == remove) removeItem(id);
}
} // namespace toon
