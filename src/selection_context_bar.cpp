#include "selection_context_bar.h"
#include "canvas.h"
#include "icons.h"
#include "value_slider.h"

#include <QActionGroup>
#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QMouseEvent>
#include <QPushButton>
#include <QSignalBlocker>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidgetAction>
#include <QtMath>
#include <array>

namespace toon {
namespace {
constexpr int margin = 8;
constexpr int gap = 10;
struct ModeEntry { SelectionCombine mode; const char *label; const char *icon; const char *id; const char *modifier; };
constexpr std::array<ModeEntry, 4> modes{{
    {SelectionCombine::Replace, "New selection", "square-dashed", "contextSelectionReplace", ""},
    {SelectionCombine::Add, "Add to selection", "square-plus", "contextSelectionAdd", "Shift"},
    {SelectionCombine::Subtract, "Subtract from selection", "square-minus", "contextSelectionSubtract", "Alt/Option"},
    {SelectionCombine::Intersect, "Intersect selection", "combine", "contextSelectionIntersect", "Shift + Alt/Option"}
}};
}
SelectionContextBar::SelectionContextBar(Canvas *canvas)
    : QFrame(canvas), m_canvas(canvas), m_document(canvas->document()) {
    setObjectName("selectionContextBar"); setAccessibleName("Selection actions");
    setFocusPolicy(Qt::NoFocus); setCursor(Qt::ArrowCursor); setAttribute(Qt::WA_StyledBackground);
    // Qt also observes this barrier for tablet propagation, while still allowing
    // ignored pen events to become mouse clicks on the toolbar buttons.
    setAttribute(Qt::WA_NoMousePropagation);
    auto *layout = new QHBoxLayout(this); layout->setContentsMargins(5, 5, 5, 5); layout->setSpacing(4);
    auto button = [this, layout](const char *id, const QString &icon, const QString &label, int width = 30) {
        auto *control = new QToolButton(this); control->setObjectName(id);
        control->setIcon(appIcon(icon)); control->setIconSize({17, 17}); control->setFixedSize(width, 30);
        control->setFocusPolicy(Qt::NoFocus); control->setToolTip(label); control->setAccessibleName(label);
        layout->addWidget(control); return control;
    };
    m_grip = button("contextDragGrip", "grip-vertical", "Drag to pin selection actions", 18);
    m_grip->setCursor(Qt::SizeAllCursor); m_grip->installEventFilter(this);
    auto *modeGroup = new QActionGroup(this); modeGroup->setExclusive(true);
    for (const auto &entry : modes) {
        auto *action = new QAction(appIcon(entry.icon), entry.label, this);
        action->setObjectName(entry.id); action->setCheckable(true); action->setData(int(entry.mode)); modeGroup->addAction(action);
        if (*entry.modifier) action->setToolTip(QString("%1 (hold %2 while dragging)").arg(entry.label, entry.modifier));
        auto *control = button(entry.id, entry.icon, entry.label);
        control->setDefaultAction(action); control->setToolButtonStyle(Qt::ToolButtonIconOnly); m_modes.append(control);
        connect(action, &QAction::triggered, this, [this, mode = entry.mode] { m_canvas->setSelectionCombine(mode); });
    }
    m_transform = button("contextTransform", "move", "Transform selected pixels (" + QKeySequence("Ctrl+T").toString(QKeySequence::NativeText) + ")");
    m_transform->setProperty("primary", true);
    m_invert = button("contextInvert", "contrast", "Invert selection (" + QKeySequence("Ctrl+Shift+I").toString(QKeySequence::NativeText) + ")");
    m_deselect = button("contextDeselect", "x", "Deselect (" + QKeySequence("Ctrl+D").toString(QKeySequence::NativeText) + ")");
    m_apply = button("contextApply", "check", "Apply transform (Enter)"); m_apply->setProperty("primary", true);
    m_cancel = button("contextCancel", "x", "Cancel transform (Esc)");
    m_more = button("contextMore", "ellipsis", "More selection actions");
    m_moreMenu = new QMenu(m_more); m_moreMenu->setObjectName("contextMoreMenu");
    auto action = [this](const QString &label, const char *id, const QString &icon = {}) {
        auto *item = m_moreMenu->addAction(icon.isEmpty() ? QIcon() : appIcon(icon), label); item->setObjectName(id); return item;
    };
    m_fill = action("Fill with foreground", "contextFill", "paint-bucket");
    m_clear = action("Clear selected pixels", "contextClear", "eraser");
    m_moreMenu->addSeparator();
    addResizeMenu(m_moreMenu, "Expand selection…", "contextExpand", 1);
    m_expand = m_moreMenu->actions().last();
    addResizeMenu(m_moreMenu, "Contract selection…", "contextContract", -1);
    m_contract = m_moreMenu->actions().last();
    m_selectAll = action("Select all", "contextSelectAll", "scan");
    m_moreMenu->addSeparator();
    m_focus = action("Shade outside selection", "contextFocus", "focus"); m_focus->setCheckable(true);
    m_pin = action("Pin position", "contextPin", "pin"); m_pin->setCheckable(true);
    auto *reset = action("Reset position", "contextReset", "rotate-ccw");
    auto *hide = action("Hide selection bar", "contextHide", "x");
    m_more->setMenu(m_moreMenu); m_more->setPopupMode(QToolButton::InstantPopup);
    connect(m_transform, &QToolButton::clicked, this, [this] {
        if (m_canvas->tool() == Canvas::Tool::Transform) m_document->beginTransform();
        else m_canvas->setTool(Canvas::Tool::Transform);
    });
    connect(m_invert, &QToolButton::clicked, m_document, &Document::invertRasterSelection);
    connect(m_deselect, &QToolButton::clicked, m_document, &Document::clearRasterSelection);
    connect(m_apply, &QToolButton::clicked, m_document, &Document::applyTransform);
    connect(m_cancel, &QToolButton::clicked, m_document, &Document::cancelTransform);
    connect(m_fill, &QAction::triggered, this, [this] { m_document->fillRasterSelection(m_canvas->brush.color); });
    connect(m_clear, &QAction::triggered, m_document, &Document::clearSelectedPixels);
    connect(m_selectAll, &QAction::triggered, m_document, &Document::selectAllPixels);
    connect(m_focus, &QAction::toggled, m_canvas, &Canvas::setSelectionFocusOverlay);
    connect(m_pin, &QAction::toggled, this, &SelectionContextBar::setPositionPinned);
    connect(reset, &QAction::triggered, this, &SelectionContextBar::resetPosition);
    connect(hide, &QAction::triggered, this, [this] { m_canvas->setSelectionBarVisible(false); });
    connect(m_moreMenu, &QMenu::aboutToShow, this, &SelectionContextBar::refresh);
    connect(canvas, &Canvas::selectionUiChanged, this, &SelectionContextBar::refresh);
    connect(canvas, &Canvas::selectionCombineChanged, this, &SelectionContextBar::refresh);
    connect(canvas, &Canvas::selectionBarVisibilityChanged, this, &SelectionContextBar::refresh);
    connect(canvas, &Canvas::viewChanged, this, &SelectionContextBar::updatePosition);
    connect(m_document, &Document::rasterSelectionChanged, this, &SelectionContextBar::refresh);
    connect(m_document, &Document::transformChanged, this, &SelectionContextBar::refresh);
    connect(m_document, &Document::selectionChanged, this, &SelectionContextBar::refresh);
    connect(m_document, &Document::changed, this, &SelectionContextBar::refresh);
    canvas->installEventFilter(this);
    refresh();
}
void SelectionContextBar::addResizeMenu(QMenu *menu, const QString &title, const QString &id, int direction) {
    auto *popup = menu->addMenu(title); popup->menuAction()->setObjectName(id);
    auto *item = new QWidgetAction(popup); auto *content = new QWidget;
    content->setMinimumWidth(240); auto *layout = new QVBoxLayout(content); layout->setContentsMargins(8, 8, 8, 8);
    auto *label = new QLabel(title); layout->addWidget(label);
    auto *amount = new ValueSlider("Pixels", 1, 100, 5, 0, " px", content);
    amount->setObjectName(id + "Amount"); layout->addWidget(amount);
    auto *apply = new QPushButton("Apply"); apply->setObjectName(id + "Apply"); layout->addWidget(apply);
    item->setDefaultWidget(content); popup->addAction(item);
    connect(apply, &QPushButton::clicked, this, [this, amount, direction, popup, menu] {
        m_document->modifyRasterSelection(direction * amount->value()); popup->close(); menu->close();
    });
}
void SelectionContextBar::refresh() {
    const bool transform = m_document->transformActive();
    const bool stamp = m_document->stampPlacementActive();
    const auto &selection = m_document->rasterSelection();
    // Placement needs an explicit Done/Discard surface even when the artist
    // has hidden the optional selection toolbar in their workspace.
    const bool show = m_canvas->hasProject() && !m_canvas->selectionGestureActive()
        && (stamp || (m_canvas->tool() != Canvas::Tool::Guides && m_canvas->selectionBarVisible()
            && (transform || selection.active)));
    setAccessibleName(stamp ? "Stamp placement" : "Selection actions");
    const QString gripHelp = stamp ? "Drag to pin stamp actions" : "Drag to pin selection actions";
    m_grip->setToolTip(gripHelp); m_grip->setAccessibleName(gripHelp);
    const QString applyHelp = stamp ? "Place stamp (Enter)" : "Apply transform (Enter)";
    const QString cancelHelp = stamp ? "Discard stamp (Esc)" : "Cancel transform (Esc)";
    m_apply->setToolTip(applyHelp); m_apply->setAccessibleName(applyHelp);
    m_cancel->setToolTip(cancelHelp); m_cancel->setAccessibleName(cancelHelp);
    for (auto *control : m_modes) control->setVisible(!transform);
    m_transform->setVisible(!transform);
    m_invert->setVisible(!transform); m_deselect->setVisible(!transform);
    m_apply->setVisible(transform); m_cancel->setVisible(transform);
    m_more->setVisible(!stamp);
    if (stamp && m_moreMenu->isVisible()) m_moreMenu->close();
    bool paintable = false, alphaLocked = false, painted = false;
    if (m_canvas->hasProject()) {
        const auto &layers = m_document->activeShot().layers;
        const auto index = m_document->activeLayer();
        paintable = layerCanPaint(layers, index); alphaLocked = layers[index].alphaLocked;
        painted = !layers[index].tiles.isEmpty();
    }
    const bool selected = selection.active && !selection.path.isEmpty();
    m_transform->setEnabled(selected && paintable && painted && !alphaLocked && !transform);
    m_fill->setEnabled(selected && paintable && !transform);
    m_clear->setEnabled(selected && paintable && !alphaLocked && !transform);
    m_expand->setEnabled(selected && !transform); m_contract->setEnabled(selected && !transform); m_selectAll->setEnabled(!transform);
    for (auto *control : m_modes) {
        auto *action = control->defaultAction(); const QSignalBlocker blocker(action);
        action->setChecked(action->data().toInt() == int(m_canvas->selectionCombine()));
    }
    { QSignalBlocker blocker(m_focus); m_focus->setChecked(m_canvas->selectionFocusOverlay()); }
    { QSignalBlocker blocker(m_pin); m_pin->setChecked(m_pinned); }
    setVisible(show);
    if (show) { updatePosition(); raise(); }
}
QPoint SelectionContextBar::clampPosition(QPoint position) const {
    return {qBound(margin, position.x(), qMax(margin, m_canvas->width() - width() - margin)),
            qBound(margin, position.y(), qMax(margin, m_canvas->height() - height() - margin))};
}
void SelectionContextBar::updatePosition() {
    layout()->activate(); adjustSize();
    if (m_pinned) { move(clampPosition(m_pinnedPoint)); return; }
    const QRectF selection = m_canvas->selectionViewportBounds();
    const QRect available = m_canvas->rect().adjusted(margin, margin, -margin, -margin);
    const int x = qRound(selection.center().x() - width() / 2.0);
    QPoint target(x, qCeil(selection.bottom()) + gap);
    if (target.y() + height() > available.bottom() + 1) {
        target.setY(qFloor(selection.top()) - gap - height());
        if (target.y() < available.top()) {
            const int y = qRound(selection.center().y() - height() / 2.0);
            if (qCeil(selection.right()) + gap + width() <= available.right() + 1)
                target = {qCeil(selection.right()) + gap, y};
            else if (qFloor(selection.left()) - gap - width() >= available.left())
                target = {qFloor(selection.left()) - gap - width(), y};
            else target.setY(available.bottom() + 1 - height());
        }
    }
    move(clampPosition(target));
}
void SelectionContextBar::resetPosition() { setPositionPinned(false); }
void SelectionContextBar::setPositionPinned(bool pinned) {
    if (pinned && !m_pinned) m_pinnedPoint = pos();
    m_pinned = pinned;
    { QSignalBlocker blocker(m_pin); m_pin->setChecked(pinned); }
    updatePosition();
}
bool SelectionContextBar::eventFilter(QObject *object, QEvent *event) {
    if (object == m_canvas && (event->type() == QEvent::Resize || event->type() == QEvent::Show)) updatePosition();
    if (object == m_grip) {
        if (event->type() == QEvent::MouseButtonPress) {
            auto *mouse = static_cast<QMouseEvent *>(event);
            if (mouse->button() == Qt::LeftButton) {
                m_dragging = true; m_dragOrigin = mouse->globalPosition().toPoint(); m_dragPosition = pos();
                mouse->accept(); return true;
            }
        } else if (event->type() == QEvent::MouseMove && m_dragging) {
            auto *mouse = static_cast<QMouseEvent *>(event); setPositionPinned(true);
            m_pinnedPoint = clampPosition(m_dragPosition + mouse->globalPosition().toPoint() - m_dragOrigin);
            move(m_pinnedPoint); mouse->accept(); return true;
        } else if (event->type() == QEvent::MouseButtonRelease && m_dragging) {
            m_dragging = false; event->accept(); return true;
        }
    }
    return QFrame::eventFilter(object, event);
}
} // namespace toon
