#include "scrub_value.h"
#include "ui_theme.h"

#include <QApplication>
#include <QDoubleValidator>
#include <QFocusEvent>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QScopedValueRollback>
#include <cmath>

namespace toon {
ScrubValue::ScrubValue(const QString &label, double minimum, double maximum, double initial,
                       QWidget *parent) : QWidget(parent), m_editor(new QLineEdit(this))
{
    setAccessibleName(label);
    setToolTip(label + ": drag to adjust, click to enter a value");
    setFocusPolicy(Qt::StrongFocus);
    setCursor(Qt::SizeHorCursor);
    setMouseTracking(true);
    setAttribute(Qt::WA_Hover);
    setMinimumSize(minimumSizeHint());
    setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::Fixed);
    m_editor->setObjectName("scrubEditor");
    m_editor->setAccessibleName(label);
    m_editor->setAlignment(Qt::AlignCenter);
    m_editor->setFrame(false);
    // Override the shared field padding so the temporary editor fits the same
    // compact hit area, including when the control is only 28 pixels tall.
    m_editor->setStyleSheet("QLineEdit { border: 0; padding: 0; min-height: 0; background: transparent; }");
    m_editor->setCursor(Qt::IBeamCursor);
    auto *validator = new QDoubleValidator(m_editor);
    validator->setNotation(QDoubleValidator::StandardNotation);
    m_editor->setValidator(validator);
    m_editor->installEventFilter(this);
    m_editor->hide();
    connect(m_editor, &QLineEdit::textEdited, this, [this] { updateFromText(); });
    setRange(minimum, maximum);
    setValue(initial);
}

QSize ScrubValue::sizeHint() const { return {80, 30}; }
QSize ScrubValue::minimumSizeHint() const { return {64, 28}; }

double ScrubValue::bounded(double value) const
{
    return qBound(m_minimum, value, m_maximum);
}
void ScrubValue::setRange(double minimum, double maximum)
{
    if (!std::isfinite(minimum) || !std::isfinite(maximum)) return;
    if (minimum > maximum) std::swap(minimum, maximum);
    m_minimum = minimum;
    m_maximum = maximum;
    setValue(m_value);
}
void ScrubValue::setSingleStep(double step)
{
    if (std::isfinite(step) && step > 0) m_step = step;
}
void ScrubValue::setDecimals(int decimals)
{
    m_decimals = qBound(0, decimals, 6);
    update();
}
void ScrubValue::setSuffix(const QString &suffix)
{
    m_suffix = suffix;
    update();
}
void ScrubValue::setValue(double value)
{
    if (!std::isfinite(value)) return;
    m_value = bounded(value);
    if (m_mode != Mode::Text) m_editor->setText(numberText());
    update();
}
QString ScrubValue::numberText() const
{
    return locale().toString(m_value, 'f', m_decimals);
}
void ScrubValue::applyValue(double value)
{
    if (!std::isfinite(value)) return;
    value = bounded(value);
    if (value == m_value) return;
    m_value = value;
    update();
    emit valueChanged(m_value);
}
void ScrubValue::beginEditing(Mode mode)
{
    m_original = m_value;
    m_mode = mode;
    emit editingStarted();
}
void ScrubValue::finishEditing()
{
    if (m_mode == Mode::Idle || m_finishing) return;
    const QScopedValueRollback guard(m_finishing, true);
    if (m_mode == Mode::Text) updateFromText();
    m_mode = Mode::Idle;
    m_editor->hide();
    update();
    emit editingFinished();
}
void ScrubValue::cancelEditing()
{
    if (m_mode == Mode::Idle || m_finishing) return;
    const QScopedValueRollback guard(m_finishing, true);
    m_mode = Mode::Idle;
    m_editor->hide();
    applyValue(m_original);
    update();
    emit editingCancelled();
}
void ScrubValue::openEditor()
{
    m_mode = Mode::Text;
    m_editor->setText(numberText());
    m_editor->show();
    m_editor->setFocus(Qt::MouseFocusReason);
    m_editor->selectAll();
    update();
}
void ScrubValue::updateFromText()
{
    bool ok = false;
    const double value = locale().toDouble(m_editor->text(), &ok);
    if (ok) applyValue(value);
}

void ScrubValue::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setBrush(QColor(isEnabled() && underMouse() ? ui::hover : ui::base));
    painter.setPen(QColor(hasFocus() || m_editor->hasFocus() ? ui::focus : ui::border));
    painter.drawRoundedRect(QRectF(rect()).adjusted(.5, .5, -.5, -.5), 4, 4);
    if (m_mode != Mode::Text) {
        painter.setPen(QColor(isEnabled() ? ui::text : ui::disabled));
        painter.drawText(rect().adjusted(4, 0, -4, 0), Qt::AlignCenter, numberText() + m_suffix);
    }
}
void ScrubValue::resizeEvent(QResizeEvent *)
{
    m_editor->setGeometry(rect().adjusted(3, 2, -3, -2));
}
void ScrubValue::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton) { QWidget::mousePressEvent(event); return; }
    if (m_mode != Mode::Idle) finishEditing();
    setFocus(Qt::MouseFocusReason);
    m_pressPosition = event->position();
    beginEditing(Mode::Pressed);
    event->accept();
}
void ScrubValue::mouseMoveEvent(QMouseEvent *event)
{
    if (m_mode != Mode::Pressed && m_mode != Mode::Dragging) {
        QWidget::mouseMoveEvent(event); return;
    }
    // Recover from a release lost to a window switch or another native surface.
    if (!(event->buttons() & Qt::LeftButton)) {
        if (m_mode == Mode::Dragging) finishEditing(); else cancelEditing();
        event->accept();
        return;
    }
    const qreal delta = event->position().x() - m_pressPosition.x();
    if (m_mode == Mode::Pressed && std::abs(delta) < QApplication::startDragDistance()) return;
    m_mode = Mode::Dragging;
    const double multiplier = event->modifiers().testFlag(Qt::ShiftModifier) ? 10.0 : 1.0;
    applyValue(m_original + std::round(delta / 2.0) * m_step * multiplier);
    event->accept();
}
void ScrubValue::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton) { QWidget::mouseReleaseEvent(event); return; }
    if (m_mode == Mode::Pressed) openEditor();
    else if (m_mode == Mode::Dragging) finishEditing();
    event->accept();
}
void ScrubValue::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape && m_mode != Mode::Idle) {
        cancelEditing(); event->accept(); return;
    }
    if (m_mode == Mode::Idle && (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter
                              || event->key() == Qt::Key_Space)) {
        beginEditing(Mode::Text);
        openEditor();
        event->accept(); return;
    }
    int direction = 0;
    if (event->key() == Qt::Key_Up || event->key() == Qt::Key_Right) direction = 1;
    if (event->key() == Qt::Key_Down || event->key() == Qt::Key_Left) direction = -1;
    if (direction && m_mode == Mode::Idle) {
        beginEditing(Mode::Dragging);
        applyValue(m_value + direction * m_step * (event->modifiers().testFlag(Qt::ShiftModifier) ? 10 : 1));
        finishEditing();
        event->accept(); return;
    }
    QWidget::keyPressEvent(event);
}
void ScrubValue::focusOutEvent(QFocusEvent *event)
{
    if (m_mode == Mode::Dragging) finishEditing();
    else if (m_mode == Mode::Pressed) cancelEditing();
    update();
    QWidget::focusOutEvent(event);
}
bool ScrubValue::event(QEvent *event)
{
    switch (event->type()) {
    case QEvent::ShortcutOverride:
        if (m_mode != Mode::Idle && static_cast<QKeyEvent *>(event)->key() == Qt::Key_Escape) {
            event->accept();
            return true;
        }
        break;
    case QEvent::UngrabMouse:
        if (m_mode == Mode::Dragging) finishEditing();
        else if (m_mode == Mode::Pressed) cancelEditing();
        break;
    case QEvent::WindowDeactivate:
    case QEvent::Hide:
        finishEditing();
        break;
    case QEvent::EnabledChange:
        if (!isEnabled()) finishEditing();
        break;
    default: break;
    }
    return QWidget::event(event);
}
bool ScrubValue::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_editor && m_mode == Mode::Text && !m_finishing) {
        if (event->type() == QEvent::ShortcutOverride
            && static_cast<QKeyEvent *>(event)->key() == Qt::Key_Escape) {
            event->accept();
            return true;
        } else if (event->type() == QEvent::KeyPress) {
            const int key = static_cast<QKeyEvent *>(event)->key();
            if (key == Qt::Key_Escape || key == Qt::Key_Return || key == Qt::Key_Enter) {
                if (key == Qt::Key_Escape) cancelEditing(); else finishEditing();
                setFocus(Qt::OtherFocusReason);
                return true;
            }
        } else if (event->type() == QEvent::FocusOut) finishEditing();
    }
    return QWidget::eventFilter(watched, event);
}
}
