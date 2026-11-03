#include "easing_graph_widget.h"
#include "ui_theme.h"
#include <QFocusEvent>
#include <QKeyEvent>
#include <QLineF>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <algorithm>
#include <cmath>

namespace toon {
namespace {
QPointF bounded(QPointF value) {
    if (!std::isfinite(value.x()) || !std::isfinite(value.y())) return {};
    return {std::clamp(value.x(), 0., 1.), std::clamp(value.y(), 0., 1.)};
}
}
EasingGraphWidget::EasingGraphWidget(QWidget *parent) : QWidget(parent) {
    setObjectName("keyframeEasingGraph"); setAccessibleName("Keyframe easing graph");
    setToolTip("Drag a tangent handle to shape timing. Arrow keys move the selected handle; Shift uses a larger step. Tab switches handles; Escape cancels.");
    setMinimumWidth(0); setFixedHeight(174); setFocusPolicy(Qt::StrongFocus); setMouseTracking(true);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}
QRectF EasingGraphWidget::graphRect() const { return QRectF(20, 12, std::max(1, width() - 40), std::max(1, height() - 40)); }
QPointF EasingGraphWidget::toScreen(QPointF point) const {
    const auto area = graphRect();
    return {area.left() + point.x() * area.width(), area.bottom() - (point.y() - m_plotLow) / (m_plotHigh - m_plotLow) * area.height()};
}
QPointF EasingGraphWidget::fromScreen(QPointF point) const {
    const auto area = graphRect();
    return {(point.x() - area.left()) / area.width(), m_plotLow + (area.bottom() - point.y()) / area.height() * (m_plotHigh - m_plotLow)};
}
QPointF EasingGraphWidget::handlePosition(int handle) const { return toScreen(handle ? m_second : m_first); }
void EasingGraphWidget::setCurve(QPointF first, QPointF second) {
    if (m_editing) return;
    m_first = bounded(first); m_second = bounded(second);
    m_first.setX(std::min(m_first.x(), m_second.x()));
    m_plotLow = std::min({-.2, m_first.y() - .15, m_second.y() - .15});
    m_plotHigh = std::max({1.2, m_first.y() + .15, m_second.y() + .15});
    update();
}
void EasingGraphWidget::setHold(bool hold) { if (!m_editing) { m_hold = hold; update(); } }
void EasingGraphWidget::setSegment(int firstFrame, int lastFrame, qreal firstValue, qreal lastValue, const QString &unit) {
    m_startLabel = QString::number(firstFrame + 1); m_endLabel = QString::number(lastFrame + 1);
    setAccessibleDescription(QString("Frames %1 to %2; value %3 to %4 %5. Horizontal axis is time; vertical axis is progress.")
        .arg(firstFrame + 1).arg(lastFrame + 1).arg(firstValue, 0, 'g', 8).arg(lastValue, 0, 'g', 8).arg(unit)); update();
}
void EasingGraphWidget::beginEditing() {
    if (m_editing) return;
    m_originalFirst = m_first; m_originalSecond = m_second; m_originalHold = m_hold;
    m_editing = true; m_hold = false; emit editingStarted();
}
void EasingGraphWidget::finishEditing() {
    if (!m_editing) return;
    if(QLineF(m_first,m_originalFirst).length()<1e-10 && QLineF(m_second,m_originalSecond).length()<1e-10){cancelEditing();return;}
    m_editing = m_dragging = false; emit editingFinished(); update();
}
void EasingGraphWidget::cancelEditing() {
    if (!m_editing) return;
    m_first = m_originalFirst; m_second = m_originalSecond; m_hold = m_originalHold;
    m_editing = m_dragging = false; emit editingCancelled(); update();
}
void EasingGraphWidget::moveHandle(QPointF value) {
    value = bounded(value);
    value.setX(m_selected ? std::max(m_first.x(), value.x()) : std::min(m_second.x(), value.x()));
    auto &control = m_selected ? m_second : m_first;
    if (QLineF(control,value).length()<1e-10) return; control = value;
    emit curveChanged(m_first, m_second); update();
}
bool EasingGraphWidget::event(QEvent *event) {
    if (m_editing && (event->type() == QEvent::Hide || event->type() == QEvent::WindowDeactivate || event->type() == QEvent::UngrabMouse)) finishEditing();
    if (event->type() == QEvent::KeyPress) {
        auto *key = static_cast<QKeyEvent *>(event);
        if (key->key() == Qt::Key_Tab && !key->modifiers().testFlag(Qt::ControlModifier)) {
            if (m_selected == 0 && !key->modifiers().testFlag(Qt::ShiftModifier)) { finishEditing(); m_selected = 1; update(); key->accept(); return true; }
        }
        if (key->key() == Qt::Key_Backtab && m_selected == 1) { finishEditing(); m_selected = 0; update(); key->accept(); return true; }
    }
    return QWidget::event(event);
}
void EasingGraphWidget::paintEvent(QPaintEvent *) {
    QPainter painter(this); painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QColor(hasFocus() ? ui::focus : ui::border)); painter.setBrush(QColor(ui::base));
    painter.drawRoundedRect(QRectF(rect()).adjusted(.5, .5, -.5, -.5), 4, 4);
    const auto area = graphRect();
    painter.setPen(QPen(QColor(ui::border), 1, Qt::DotLine));
    for (int i = 0; i <= 4; ++i) {
        const qreal t = i / 4.; painter.drawLine(toScreen({t, m_plotLow}), toScreen({t, m_plotHigh}));
        painter.drawLine(toScreen({0, t}), toScreen({1, t}));
    }
    const QPointF start = toScreen({0, 0}), end = toScreen({1, 1});
    painter.setPen(QPen(QColor(ui::muted), 1, Qt::DashLine)); painter.drawLine(start, end);
    QPainterPath curve(start);
    if (m_hold) { curve.lineTo(toScreen({1, 0})); curve.lineTo(end); }
    else curve.cubicTo(handlePosition(0), handlePosition(1), end);
    painter.setPen(QPen(QColor(isEnabled() ? ui::accent : ui::disabled), 2)); painter.setBrush(Qt::NoBrush); painter.drawPath(curve);
    if (!m_hold) {
        painter.setPen(QPen(QColor(isEnabled() ? ui::muted : ui::disabled), 1));
        painter.drawLine(start, handlePosition(0)); painter.drawLine(end, handlePosition(1));
    }
    for (int i = 0; i < 2; ++i) {
        painter.setPen(QPen(QColor(isEnabled() ? ui::focus : ui::disabled), m_selected == i && hasFocus() ? 2 : 1));
        painter.setBrush(QColor(isEnabled() ? ui::raised : ui::base)); painter.drawEllipse(handlePosition(i), 6, 6);
    }
    painter.setPen(Qt::NoPen); painter.setBrush(QColor(isEnabled() ? ui::text : ui::disabled));
    for (const auto point : {start, end}) painter.drawEllipse(point, 3, 3);
    painter.setPen(QColor(isEnabled() ? ui::muted : ui::disabled));
    painter.drawText(QRectF(area.left(), height() - 23, area.width() / 2, 18), Qt::AlignLeft | Qt::AlignVCenter, m_startLabel);
    painter.drawText(QRectF(area.center().x(), height() - 23, area.width() / 2, 18), Qt::AlignRight | Qt::AlignVCenter, m_endLabel);
}
void EasingGraphWidget::mousePressEvent(QMouseEvent *event) {
    if (event->button() != Qt::LeftButton || !isEnabled()) { QWidget::mousePressEvent(event); return; }
    const qreal a = QLineF(event->position(), handlePosition(0)).length(), b = QLineF(event->position(), handlePosition(1)).length();
    if (std::min(a, b) > 14) { QWidget::mousePressEvent(event); return; }
    setFocus(Qt::MouseFocusReason); m_selected = b < a ? 1 : 0;
    m_pressPosition=fromScreen(event->position());m_pressControl=m_selected?m_second:m_first;beginEditing();
    if (m_editing) m_dragging = true; update(); event->accept();
}
void EasingGraphWidget::mouseMoveEvent(QMouseEvent *event) {
    if (!m_dragging) { QWidget::mouseMoveEvent(event); return; }
    if (!event->buttons().testFlag(Qt::LeftButton)) finishEditing(); else moveHandle(m_pressControl+fromScreen(event->position())-m_pressPosition);
    event->accept();
}
void EasingGraphWidget::mouseReleaseEvent(QMouseEvent *event) {
    if (!m_dragging || event->button() != Qt::LeftButton) { QWidget::mouseReleaseEvent(event); return; }
    moveHandle(m_pressControl+fromScreen(event->position())-m_pressPosition); finishEditing(); event->accept();
}
void EasingGraphWidget::keyPressEvent(QKeyEvent *event) {
    if (event->key() == Qt::Key_Escape && m_editing) { cancelEditing(); event->accept(); return; }
    QPointF delta; const qreal step = event->modifiers().testFlag(Qt::ShiftModifier) ? .1 : .01;
    switch (event->key()) {
    case Qt::Key_Left: delta.setX(-step); break;
    case Qt::Key_Right: delta.setX(step); break;
    case Qt::Key_Up: delta.setY(step); break;
    case Qt::Key_Down: delta.setY(-step); break;
    default: QWidget::keyPressEvent(event); return;
    }
    if (!isEnabled()) return;
    beginEditing(); if (m_editing) { moveHandle((m_selected ? m_second : m_first) + delta); finishEditing(); } event->accept();
}
void EasingGraphWidget::focusOutEvent(QFocusEvent *event) { finishEditing(); QWidget::focusOutEvent(event); }
} // namespace toon
