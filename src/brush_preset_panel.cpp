#include "brush_preset_panel.h"

#include "camera_tool.h"
#include "canvas.h"
#include "icons.h"
#include "keyframe_transform_tool.h"
#include "mask_tool.h"
#include "property_row.h"
#include "ui_metrics.h"
#include "ui_theme.h"
#include "value_slider.h"
#include <QDialog>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QPainter>
#include <QPushButton>
#include <QScrollBar>
#include <QResizeEvent>
#include <QSettings>
#include <QSignalBlocker>
#include <QStyle>
#include <QStyledItemDelegate>
#include <QToolButton>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>
#include <functional>

namespace toon {
namespace {
constexpr auto settingsKey = "brushPresets/v1";
constexpr int maxSettingsBytes = 8192;
constexpr double minBrushSize = 1., maxPresetBrushSize = toon::maxBrushSize;
QVector<double> defaults()
{
    return {1, 2, 3, 4, 5, 6, 8, 10, 12, 16, 20, 25, 32, 48, 64, 96, 128, 192, 256};
}
bool validSize(double size)
{
    return std::isfinite(size) && size >= minBrushSize && size <= maxPresetBrushSize;
}
double normalizedSize(double size) { return std::round(size * 10.) / 10.; }
QString sizeLabel(double size) { return QString::number(size, 'f', size == std::floor(size) ? 0 : 1); }
QIcon sizeIcon(double size)
{
    // A compressed visual scale keeps even very large presets recognizable in
    // a compact tile; the label supplies the precise artwork size.
    constexpr int extent = ui::metrics::presetCueSize;
    const double diameter = 2. + (extent - 4.) * std::log2(size) / std::log2(maxPresetBrushSize);
    QIcon result;
    for (const double ratio : {1., 2., 3.}) {
        QPixmap image(QSize(extent, extent) * ratio);
        image.setDevicePixelRatio(ratio); image.fill(Qt::transparent);
        QPainter painter(&image); painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(Qt::NoPen); painter.setBrush(QColor(ui::text));
        painter.drawEllipse(QPointF(extent / 2., extent / 2.), diameter / 2., diameter / 2.);
        painter.end(); result.addPixmap(image);
    }
    return result;
}
class PresetDelegate final : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        painter->save(); painter->setFont(option.font);
        const bool enabled = option.state & QStyle::State_Enabled;
        const bool selected = option.state & QStyle::State_Selected;
        const QRect tile = option.rect.adjusted(1, 1, -1, -1);
        painter->fillRect(tile, QColor(selected ? ui::selected : option.state & QStyle::State_MouseOver ? ui::hover : ui::base));
        if (selected) painter->fillRect(QRect(tile.left(), tile.top(), 2, tile.height()), QColor(ui::focus));
        constexpr int cue = ui::metrics::presetCueSize;
        const int contentHeight = cue + 2 + option.fontMetrics.height();
        const int top = option.rect.top() + (option.rect.height() - contentHeight) / 2;
        const QRect iconRect(option.rect.center().x() - cue / 2, top, cue, cue);
        index.data(Qt::DecorationRole).value<QIcon>().paint(painter, iconRect, Qt::AlignCenter, enabled ? QIcon::Normal : QIcon::Disabled);
        painter->setPen(QColor(enabled ? ui::text : ui::disabled));
        painter->drawText(QRect(option.rect.left() + 4, top + cue + 2, option.rect.width() - 8, option.fontMetrics.height()),
                          Qt::AlignHCenter | Qt::AlignVCenter, index.data(Qt::DisplayRole).toString());
        if (option.state & QStyle::State_HasFocus) {
            painter->setPen(QPen(QColor(ui::focus), 1)); painter->setBrush(Qt::NoBrush);
            painter->drawRect(tile.adjusted(0, 0, -1, -1));
        }
        painter->restore();
    }
};
class PresetList final : public QListWidget {
public:
    using QListWidget::QListWidget;
    std::function<void(QListWidgetItem *)> useItem;
    void refreshTileGeometry()
    {
        int minimumWidth = ui::metrics::presetTileWidth;
        for (int row = 0; row < count(); ++row)
            minimumWidth = qMax(minimumWidth, fontMetrics().horizontalAdvance(item(row)->text()) + 8);
        const int tileHeight = qMax(ui::metrics::presetTileHeight, ui::metrics::presetCueSize + fontMetrics().height() + 6);
        // Compute from the room available without a scrollbar first. Otherwise
        // a short dock's scrollbar can keep a fifth column from returning when
        // the artist makes the dock tall enough to show all presets.
        const QSize maximum = maximumViewportSize();
        // QListView's icon grid needs a trailing pixel at the wrap boundary.
        int availableWidth = qMax(minimumWidth, maximum.width() - 1);
        int columns = qMax(1, availableWidth / minimumWidth);
        const int rows = (count() + columns - 1) / columns;
        const bool needsScroll = rows * tileHeight >= maximum.height();
        if (needsScroll) {
            availableWidth = qMax(minimumWidth, maximum.width() - verticalScrollBar()->sizeHint().width() - 1);
            columns = qMax(1, availableWidth / minimumWidth);
        }
        // When everything fits, release the previous scrollbar before Qt lays
        // out the wider columns. Otherwise it wraps against the old viewport
        // width and keeps the scrollbar alive indefinitely.
        const auto policy = needsScroll ? Qt::ScrollBarAsNeeded : Qt::ScrollBarAlwaysOff;
        const bool changedPolicy = verticalScrollBarPolicy() != policy;
        if (changedPolicy) setVerticalScrollBarPolicy(policy);
        const QSize tile(availableWidth / columns, tileHeight);
        if (gridSize() == tile && !changedPolicy) return;
        const int scroll = verticalScrollBar()->value();
        setGridSize(tile);
        for (int row = 0; row < count(); ++row) item(row)->setSizeHint(tile);
        doItemsLayout();
        verticalScrollBar()->setValue(scroll);
    }
protected:
    void resizeEvent(QResizeEvent *event) override
    {
        QListWidget::resizeEvent(event); refreshTileGeometry();
    }
    void changeEvent(QEvent *event) override
    {
        QListWidget::changeEvent(event);
        if (event->type() == QEvent::FontChange || event->type() == QEvent::StyleChange) refreshTileGeometry();
    }
    void keyPressEvent(QKeyEvent *event) override
    {
        const int key = event->key();
        const bool navigate = key == Qt::Key_Left || key == Qt::Key_Right || key == Qt::Key_Up
            || key == Qt::Key_Down || key == Qt::Key_Home || key == Qt::Key_End
            || key == Qt::Key_PageUp || key == Qt::Key_PageDown;
        if (key == Qt::Key_Space && event->modifiers() == Qt::NoModifier) {
            if (useItem) useItem(currentItem());
            setFocus(Qt::OtherFocusReason); event->accept(); return;
        }
        QListWidget::keyPressEvent(event);
        // A selected tile always means the size actually in use. Keyboard
        // navigation has the same direct application behavior as a click.
        if (navigate && event->modifiers() == Qt::NoModifier && useItem) {
            useItem(currentItem()); setFocus(Qt::OtherFocusReason);
        }
    }
};
} // namespace

BrushPresetPanel::BrushPresetPanel(Canvas *canvas, QSettings *settings, QWidget *parent)
    : QWidget(parent), m_canvas(canvas), m_settings(settings)
{
    setObjectName("brushPresetPanel"); setAccessibleName("Brush Preset");
    setMinimumWidth(108); setMinimumHeight(92);
    auto *layout = new QVBoxLayout(this); layout->setContentsMargins(ui::metrics::panelMargin, ui::metrics::panelMargin, ui::metrics::panelMargin, ui::metrics::panelMargin); layout->setSpacing(ui::metrics::controlGap);
    auto *header = new QHBoxLayout; header->setContentsMargins(0, 0, 0, 0); header->setSpacing(ui::metrics::controlGap);
    auto button = [this](const char *id, const char *icon, const QString &help) {
        auto *result = new QToolButton(this); result->setObjectName(id); result->setIcon(appIcon(icon));
        result->setIconSize({ui::metrics::actionIcon, ui::metrics::actionIcon});
        result->setFixedSize(ui::metrics::actionTarget, ui::metrics::actionTarget); result->setFocusPolicy(Qt::NoFocus);
        result->setProperty("compactDrawingAction", true);
        result->setToolTip(help); result->setAccessibleName(help);
        return result;
    };
    m_add = button("brushPresetAdd", "plus", "Add brush size");
    m_more = button("brushPresetMore", "ellipsis", "Brush size preset options");
    header->addWidget(m_add); header->addStretch(); header->addWidget(m_more); layout->addLayout(header);
    auto *list = new PresetList(this); m_list = list;
    m_list->setObjectName("brushPresetSizes"); m_list->setAccessibleName("Brush size presets");
    m_list->setAccessibleDescription("Choose a saved brush size. Right-click a size to edit or remove it.");
    m_list->setViewMode(QListView::IconMode); m_list->setResizeMode(QListView::Adjust);
    m_list->setMovement(QListView::Static); m_list->setFlow(QListView::LeftToRight); m_list->setWrapping(true);
    m_list->setSelectionMode(QAbstractItemView::SingleSelection); m_list->setSelectionBehavior(QAbstractItemView::SelectItems);
    m_list->setIconSize({ui::metrics::presetCueSize, ui::metrics::presetCueSize}); m_list->setSpacing(0);
    m_list->setItemDelegate(new PresetDelegate(m_list)); m_list->setMouseTracking(true);
    m_list->setUniformItemSizes(true); m_list->setWordWrap(false); m_list->setTextElideMode(Qt::ElideNone);
    m_list->setFrameShape(QFrame::NoFrame); m_list->setMinimumWidth(0); m_list->setMinimumHeight(ui::metrics::presetTileHeight);
    m_list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_list->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_list->setContextMenuPolicy(Qt::CustomContextMenu); layout->addWidget(m_list, 1);
    const auto useItem = [this](QListWidgetItem *item) {
        if (item) activateSize(item->data(Qt::UserRole).toDouble());
        refreshActive();
    };
    list->useItem = useItem;
    connect(m_list, &QListWidget::itemClicked, this, useItem);
    connect(m_list, &QListWidget::itemActivated, this, useItem);
    connect(m_list, &QWidget::customContextMenuRequested, this, &BrushPresetPanel::showPresetMenu);
    connect(m_add, &QToolButton::clicked, this, [this] { showSizeDialog(); });
    connect(m_more, &QToolButton::clicked, this, &BrushPresetPanel::showOptions);
    restore(); rebuild();
    if (canvas) {
        m_playback = canvas->animationPlayback();
        connect(canvas, &Canvas::brushChanged, this, &BrushPresetPanel::refreshActive);
        connect(canvas, &Canvas::toolChanged, this, &BrushPresetPanel::refreshAvailability);
        connect(canvas, &Canvas::toolAvailabilityChanged, this, &BrushPresetPanel::refreshAvailability);
        connect(canvas, &Canvas::documentChanged, this, &BrushPresetPanel::refreshAvailability);
        connect(canvas, &Canvas::selectionUiChanged, this, [this] {
            // Playback shares this notification with selection overlays. Ignore
            // ordinary overlay/cursor updates without inspecting any presets.
            if (!m_canvas || m_playback == m_canvas->animationPlayback()) return;
            m_playback = m_canvas->animationPlayback(); refreshAvailability();
        });
        connect(canvas->maskTool(), &MaskTool::changed, this, &BrushPresetPanel::refreshAvailability);
        connect(canvas->cameraTool(), &CameraTool::changed, this, &BrushPresetPanel::refreshAvailability);
        connect(canvas->keyframeTransformTool(), &KeyframeTransformTool::changed, this, &BrushPresetPanel::refreshAvailability);
        connect(canvas, &QObject::destroyed, this, [this] { m_list->setEnabled(false); m_list->clearSelection(); });
    }
}

void BrushPresetPanel::restore()
{
    m_sizes = defaults();
    if (!m_settings || !m_settings->contains(settingsKey)) return;
    const auto raw = m_settings->value(settingsKey).toByteArray();
    if (raw.size() > maxSettingsBytes) return;
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(raw, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) return;
    const auto object = document.object();
    if (object.size() != 2 || !object.value("version").isDouble() || object.value("version").toDouble() != 1.
        || !object.value("sizes").isArray()) return;
    const auto array = object.value("sizes").toArray();
    if (array.size() > maxPresets) return;
    QVector<double> restored;
    for (const auto &value : array) {
        if (!value.isDouble()) return;
        const double size = value.toDouble();
        if (!validSize(size) || std::abs(size - normalizedSize(size)) > 1e-8) return;
        restored.append(normalizedSize(size));
    }
    std::sort(restored.begin(), restored.end());
    restored.erase(std::unique(restored.begin(), restored.end()), restored.end());
    m_sizes = restored;
}
void BrushPresetPanel::save()
{
    if (!m_settings) return;
    QJsonArray sizes;
    for (const double size : m_sizes) sizes.append(size);
    m_settings->setValue(settingsKey, QJsonDocument(QJsonObject{{"version", 1}, {"sizes", sizes}}).toJson(QJsonDocument::Compact));
}
bool BrushPresetPanel::addSize(double size)
{
    if (!validSize(size) || m_sizes.size() >= maxPresets) return false;
    size = normalizedSize(size);
    if (m_sizes.contains(size)) return false;
    m_sizes.append(size); std::sort(m_sizes.begin(), m_sizes.end()); save(); rebuild(); return true;
}
bool BrushPresetPanel::replaceSize(double oldSize, double newSize)
{
    if (!validSize(oldSize) || !validSize(newSize)) return false;
    oldSize = normalizedSize(oldSize); newSize = normalizedSize(newSize);
    const int index = m_sizes.indexOf(oldSize);
    if (index < 0 || oldSize == newSize || m_sizes.contains(newSize)) return false;
    m_sizes[index] = newSize; std::sort(m_sizes.begin(), m_sizes.end()); save(); rebuild(); return true;
}
bool BrushPresetPanel::removeSize(double size)
{
    if (!validSize(size)) return false;
    const int index = m_sizes.indexOf(normalizedSize(size));
    if (index < 0) return false;
    m_sizes.removeAt(index); save(); rebuild(); return true;
}
void BrushPresetPanel::resetSizes() { m_sizes = defaults(); save(); rebuild(); }

bool BrushPresetPanel::compatibleTool() const
{
    return m_canvas && m_canvas->hasProject() && m_canvas->toolAvailable() && !m_canvas->animationPlayback()
        && (m_canvas->tool() == Canvas::Tool::Brush || m_canvas->tool() == Canvas::Tool::Eraser)
        && !m_canvas->maskTool()->enabled() && !m_canvas->cameraTool()->enabled()
        && !m_canvas->keyframeTransformTool()->enabled();
}
bool BrushPresetPanel::activateSize(double size)
{
    if (!validSize(size) || !compatibleTool() || m_canvas->document()->editingGestureActive()
        || m_canvas->selectionGestureActive()) return false;
    size = normalizedSize(size);
    if (!m_sizes.contains(size)) return false;
    if (m_canvas->brush.size != size) m_canvas->setBrushSize(size);
    m_canvas->setFocus(Qt::OtherFocusReason); refreshActive(); return true;
}
void BrushPresetPanel::rebuild()
{
    const int scroll = m_list->verticalScrollBar()->value();
    const QSignalBlocker block(m_list); m_list->clear();
    for (const double size : m_sizes) {
        auto *item = new QListWidgetItem(sizeIcon(size), sizeLabel(size), m_list);
        const QString help = QString("Brush size %1 px").arg(sizeLabel(size));
        item->setData(Qt::UserRole, size); item->setData(Qt::AccessibleTextRole, help);
        const QString description = "Round tip diameter or sampled tip longest side in artwork pixels. Circle is a relative size guide.";
        item->setData(Qt::AccessibleDescriptionRole, description);
        item->setToolTip(help + "\n" + description); item->setTextAlignment(Qt::AlignHCenter);
        item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        item->setSizeHint(m_list->gridSize());
    }
    static_cast<PresetList *>(m_list)->refreshTileGeometry();
    m_list->verticalScrollBar()->setValue(scroll);
    m_add->setEnabled(m_sizes.size() < maxPresets);
    m_add->setToolTip(m_sizes.size() < maxPresets ? "Add brush size" : "Brush size limit reached (64)");
    refreshAvailability();
}
void BrushPresetPanel::refreshActive()
{
    const bool supported = compatibleTool();
    QListWidgetItem *active = nullptr;
    if (supported) for (int row = 0; row < m_list->count(); ++row) {
        auto *item = m_list->item(row);
        if (std::abs(item->data(Qt::UserRole).toDouble() - m_canvas->brush.size) < 1e-8) { active = item; break; }
    }
    const QSignalBlocker block(m_list);
    if (m_list->currentItem() != active || (active && !active->isSelected()))
        m_list->setCurrentItem(active, QItemSelectionModel::ClearAndSelect);
    if (!active) m_list->clearSelection();
}
void BrushPresetPanel::refreshAvailability()
{
    // Do not observe stroke-start/paint signals or change widget state during
    // pen contact. Gesture safety is checked only when the artist activates.
    const bool supported = compatibleTool();
    if (m_list->isEnabled() != supported) m_list->setEnabled(supported);
    refreshActive();
}
void BrushPresetPanel::showSizeDialog(bool editing, double size)
{
    if (!editing && m_sizes.size() >= maxPresets) return;
    if (editing && !m_sizes.contains(size)) return;
    QDialog dialog(this); dialog.setObjectName("brushPresetSizeDialog");
    dialog.setWindowTitle(editing ? "Edit brush size" : "Add brush size");
    dialog.setMinimumWidth(280); dialog.resize(320, 138);
    auto *layout = new QVBoxLayout(&dialog); layout->setContentsMargins(12, 12, 12, 12); layout->setSpacing(8);
    const double initial = editing ? size : m_canvas ? m_canvas->brush.size : 12.;
    auto *value = new ValueSlider("Brush size", minBrushSize, maxPresetBrushSize, initial, 1, "px", &dialog);
    value->setObjectName("brushPresetSizeValue"); value->setSingleStep(.1);
    layout->addWidget(new PropertyRow("Size", value, &dialog));
    auto *status = new QLabel(&dialog); status->setObjectName("brushPresetSizeStatus");
    status->setFixedHeight(status->fontMetrics().height() + 2); status->setAccessibleName("Brush size status");
    layout->addWidget(status);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    auto *accept = buttons->button(QDialogButtonBox::Ok); accept->setText(editing ? "Save" : "Add");
    accept->setObjectName("brushPresetSizeAccept"); accept->setDefault(false); accept->setAutoDefault(false);
    buttons->button(QDialogButtonBox::Cancel)->setAutoDefault(false); layout->addWidget(buttons);
    connect(value, &ValueSlider::valueChanged, &dialog, [status] { status->clear(); });
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, [this, editing, size, value, status, &dialog] {
        value->commitEditing();
        if (editing && normalizedSize(value->value()) == size) { dialog.accept(); return; }
        const bool changed = editing ? replaceSize(size, value->value()) : addSize(value->value());
        if (changed) dialog.accept();
        else {
            const QString message = m_sizes.contains(normalizedSize(value->value()))
                ? "This size is already saved." : "Unable to save this brush size.";
            status->setText(message); status->setToolTip(message); status->setAccessibleDescription(message);
        }
    });
    dialog.exec();
}
void BrushPresetPanel::showPresetMenu(const QPoint &position)
{
    auto *item = m_list->itemAt(position);
    if (!item && position.x() < 0) item = m_list->currentItem();
    if (!item) return;
    const double size = item->data(Qt::UserRole).toDouble();
    // Context-menu browsing must not claim a different active brush size.
    refreshActive();
    QMenu menu(this);
    auto *edit = menu.addAction(appIcon("pencil"), "Edit size…"); edit->setObjectName("brushPresetEdit");
    auto *remove = menu.addAction(appIcon("trash"), "Remove size"); remove->setObjectName("brushPresetRemove");
    const auto chosen = menu.exec(m_list->viewport()->mapToGlobal(position));
    if (chosen == edit) showSizeDialog(true, size);
    else if (chosen == remove) removeSize(size);
    refreshActive();
}
void BrushPresetPanel::showOptions()
{
    QMenu menu(this);
    auto *reset = menu.addAction(appIcon("rotate-ccw"), "Reset sizes to defaults");
    reset->setObjectName("brushPresetReset");
    if (menu.exec(m_more->mapToGlobal(QPoint(0, m_more->height()))) == reset) resetSizes();
}
} // namespace toon
