#include "brush_response_widget.h"
#include "ui_theme.h"
#include "ui_metrics.h"
#include <QFocusEvent>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QToolButton>
#include <QVBoxLayout>
#include <cmath>

namespace toon {
namespace {
const std::array<BrushResponseWidget::Response, 4> presets{{
    {0, .25, .5, .75, 1}, {0, .5, .707, .866, 1},
    {0, .0625, .25, .5625, 1}, {1, .75, .5, .25, 0}}};
const std::array<const char *, 4> names{"Linear", "Soft", "Firm", "Invert"};
}

BrushResponseWidget::BrushResponseWidget(QWidget *parent) : QWidget(parent) {
    setMinimumSize(200, 142); setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setFocusPolicy(Qt::StrongFocus); setAccessibleName("Brush input response");
    setToolTip("Drag a point to change the response. Left/Right selects a point; Up/Down adjusts it. Shift adjusts faster.");
    auto *layout = new QVBoxLayout(this); layout->setContentsMargins(0, 0, 0, 0);
    layout->addStretch(); auto *buttons = new QHBoxLayout; buttons->setSpacing(3);
    for (int i = 0; i < int(names.size()); ++i) {
        auto *button = new QToolButton(this); button->setText(names[i]);
        button->setObjectName(QString("response%1").arg(names[i]));
        button->setAccessibleName(QString("%1 response").arg(names[i]));
        button->setToolTip(i == 0 ? "Use a linear response" : i == 1 ? "Reach higher output with lighter input"
            : i == 2 ? "Require stronger input for higher output" : "Reduce output as input increases");
        button->setProperty("compactDrawingAction",true);button->setMinimumHeight(ui::metrics::actionTarget); button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        buttons->addWidget(button);
        connect(button, &QToolButton::clicked, this, [this, i] { apply(presets[i]); });
    }
    layout->addLayout(buttons); refreshHelp();
}

QRectF BrushResponseWidget::field() const { return QRectF(14, 10, qMax(1, width() - 28), qMax(1, height() - 54)); }

bool BrushResponseWidget::event(QEvent *event) {
    if (event->type() == QEvent::FocusOut || event->type() == QEvent::Hide ||
        event->type() == QEvent::UngrabMouse || event->type() == QEvent::WindowDeactivate ||
        (event->type() == QEvent::EnabledChange && !isEnabled())) m_dragging = false;
    return QWidget::event(event);
}

void BrushResponseWidget::setResponse(const Response &response) {
    for (const auto value : response) if (!std::isfinite(value) || value < 0 || value > 1) return;
    if (m_response == response) return;
    m_response = response; m_dragging = false; refreshHelp(); update();
}

void BrushResponseWidget::apply(const Response &response) {
    if (m_response == response) return;
    m_response = response; refreshHelp(); update(); emit responseEdited(m_response);
}

void BrushResponseWidget::refreshHelp() {
    setAccessibleDescription(QString("Point %1 of 5. Input %2 percent, output %3 percent. Left and Right choose a point; Up and Down change output.")
        .arg(m_active + 1).arg(m_active * 25).arg(qRound(m_response[m_active] * 100)));
}

void BrushResponseWidget::paintEvent(QPaintEvent *) {
    QPainter painter(this); painter.setRenderHint(QPainter::Antialiasing);
    const auto rect = field(); const auto ink = isEnabled() ? ui::accent : ui::disabled;
    painter.fillRect(rect, QColor(ui::base)); painter.setPen(QPen(QColor(ui::border), 1));
    for (int i = 0; i <= 4; ++i) {
        const auto x = rect.left() + rect.width() * i / 4.;
        const auto y = rect.top() + rect.height() * i / 4.;
        painter.drawLine(QPointF(x, rect.top()), QPointF(x, rect.bottom()));
        painter.drawLine(QPointF(rect.left(), y), QPointF(rect.right(), y));
    }
    painter.setPen(QPen(QColor(ui::muted), 1, Qt::DashLine)); painter.drawLine(rect.bottomLeft(), rect.topRight());
    QPainterPath curve;
    for (int i = 0; i < 5; ++i) {
        const QPointF point(rect.left() + rect.width() * i / 4., rect.bottom() - rect.height() * m_response[i]);
        if (!i) curve.moveTo(point); else curve.lineTo(point);
    }
    painter.setPen(QPen(QColor(ink), 2)); painter.drawPath(curve);
    for (int i = 0; i < 5; ++i) {
        const QPointF point(rect.left() + rect.width() * i / 4., rect.bottom() - rect.height() * m_response[i]);
        painter.setPen(QPen(QColor(i == m_active && hasFocus() ? ui::focus : ink), i == m_active ? 2 : 1.5));
        painter.setBrush(QColor(i == m_active ? ink : ui::raised)); painter.drawEllipse(point, 4.5, 4.5);
    }
    if (hasFocus()) {
        painter.setBrush(Qt::NoBrush); painter.setPen(QPen(QColor(ui::focus), 1, Qt::DotLine));
        painter.drawRoundedRect(rect.adjusted(-8, -8, 8, 8), 3, 3);
    }
}

void BrushResponseWidget::editAt(QPointF position) {
    auto next = m_response; const auto rect = field();
    next[m_active] = qBound(0., (rect.bottom() - position.y()) / rect.height(), 1.); apply(next);
}

void BrushResponseWidget::mousePressEvent(QMouseEvent *event) {
    if (event->button() != Qt::LeftButton || !field().adjusted(-10, -10, 10, 10).contains(event->position())) {
        QWidget::mousePressEvent(event); return;
    }
    setFocus(Qt::MouseFocusReason); m_original = m_response; m_dragging = true;
    m_active = qBound(0, qRound((event->position().x() - field().left()) * 4. / field().width()), 4);
    editAt(event->position()); refreshHelp(); update(); event->accept();
}

void BrushResponseWidget::mouseMoveEvent(QMouseEvent *event) {
    if (!event->buttons().testFlag(Qt::LeftButton)) m_dragging = false;
    if (m_dragging) { editAt(event->position()); event->accept(); }
    else QWidget::mouseMoveEvent(event);
}

void BrushResponseWidget::mouseReleaseEvent(QMouseEvent *event) {
    if (m_dragging && event->button() == Qt::LeftButton) {
        editAt(event->position()); m_dragging = false; event->accept();
    } else QWidget::mouseReleaseEvent(event);
}

void BrushResponseWidget::keyPressEvent(QKeyEvent *event) {
    if (event->key() == Qt::Key_Left || event->key() == Qt::Key_Right) {
        m_active = qBound(0, m_active + (event->key() == Qt::Key_Left ? -1 : 1), 4);
        refreshHelp(); update(); event->accept();
    } else if (event->key() == Qt::Key_Up || event->key() == Qt::Key_Down) {
        auto next = m_response; const qreal step = event->modifiers().testFlag(Qt::ShiftModifier) ? .1 : .01;
        next[m_active] = qBound(0., next[m_active] + (event->key() == Qt::Key_Up ? step : -step), 1.);
        apply(next); event->accept();
    } else if (event->key() == Qt::Key_Escape && m_dragging) {
        m_dragging = false; apply(m_original); event->accept();
    } else QWidget::keyPressEvent(event);
}

void BrushResponseWidget::focusInEvent(QFocusEvent *event) { QWidget::focusInEvent(event); update(); }
void BrushResponseWidget::focusOutEvent(QFocusEvent *event) { QWidget::focusOutEvent(event); update(); }
} // namespace toon
