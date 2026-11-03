#include "value_slider.h"
#include "ui_metrics.h"
#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMouseEvent>
#include <QSignalBlocker>
#include <QSlider>
#include <cmath>
#include <limits>

namespace toon {
namespace {
class CompactNumber : public QDoubleSpinBox {
public:
    using QDoubleSpinBox::QDoubleSpinBox;
    QSize sizeHint() const override {
        auto size = QDoubleSpinBox::sizeHint();
        // Match the compact row instead of the platform's taller spin-box hint.
        // Larger fonts still receive their full text height plus field padding.
        size.setHeight(qMax(ui::metrics::propertyHeight, fontMetrics().height() + 2 * ui::metrics::controlGap));
        return size;
    }
    QSize minimumSizeHint() const override { return sizeHint(); }
protected:
    QString textFromValue(double value) const override {
        QString text = QDoubleSpinBox::textFromValue(value);
        const auto point = locale().decimalPoint();
        const auto zero = locale().zeroDigit();
        if (text.contains(point)) {
            while (text.endsWith(zero)) text.chop(zero.size());
            if (text.endsWith(point)) text.chop(point.size());
        }
        return text;
    }
};
}
ValueSlider::ValueSlider(const QString &label, double minimum, double maximum, double initial,
                         int decimals, const QString &suffix, QWidget *parent) : QWidget(parent) {
    m_slider = new QSlider(Qt::Horizontal, this);
    m_slider->setObjectName("valueSliderTrack");
    m_slider->setMinimumSize(52, ui::metrics::propertyHeight);
    m_slider->setAccessibleName(label + " slider");
    m_slider->setFocusPolicy(Qt::StrongFocus);
    m_number = new CompactNumber(this);
    m_number->setObjectName("valueSliderNumber");
    m_number->setDecimals(qBound(0, decimals, 4));
    m_number->setRange(minimum, maximum);
    m_number->setButtonSymbols(QAbstractSpinBox::NoButtons);
    m_number->setKeyboardTracking(false);
    m_number->setFocusPolicy(Qt::StrongFocus);
    m_number->setFixedWidth(ui::metrics::numberWidth);
    m_number->setMinimumHeight(ui::metrics::propertyHeight);
    m_number->setAlignment(Qt::AlignRight);
    m_number->setAccessibleName(label);
    setAccessibleName(label);
    setSuffix(suffix);
    setFocusProxy(m_number);
    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(ui::metrics::controlGap);
    layout->addWidget(m_slider, 1);
    layout->addWidget(m_number);
    configureTrack();
    setValueSilently(initial);
    connect(m_slider, &QSlider::sliderPressed, this, &ValueSlider::beginEditing);
    connect(m_slider, &QSlider::valueChanged, this, [this](int n) {
        if (m_finishing) return;
        beginEditing();
        updateValue(n / m_scale);
        if (!m_slider->isSliderDown()) finishEditing();
    });
    connect(m_slider, &QSlider::sliderReleased, this, &ValueSlider::finishEditing);
    connect(m_number->findChild<QLineEdit *>(), &QLineEdit::textEdited, this, [this] {
        m_textEditing = true;
        beginEditing();
    });
    connect(m_number, &QDoubleSpinBox::valueChanged, this, [this](double n) {
        if (m_finishing || m_current == n) return;
        // The spin box already contains n; m_current retains the pre-edit value.
        beginEditing();
        updateValue(n);
        if (!m_textEditing) finishEditing();
    });
    connect(m_number, &QDoubleSpinBox::editingFinished, this, &ValueSlider::finishEditing);
    installEventFilter(this);
    m_slider->installEventFilter(this);
    m_number->installEventFilter(this);
    m_number->findChild<QLineEdit *>()->installEventFilter(this);
}
ValueSlider::~ValueSlider() {
    removeEventFilter(this);
    for (auto *child : findChildren<QObject *>()) {
        child->removeEventFilter(this);
        child->disconnect(this);
    }
}
double ValueSlider::value() const { return m_number->value(); }
void ValueSlider::configureTrack() {
    double minimum = m_number->minimum(), maximum = m_number->maximum();
    if (m_customTrackRange) {
        minimum = qBound(minimum, m_trackMinimum, maximum);
        maximum = qBound(minimum, m_trackMaximum, maximum);
    }
    m_scale = std::pow(10, m_number->decimals());
    const double magnitude = qMax(1.0, qMax(std::abs(minimum), std::abs(maximum)));
    m_scale = qMin(m_scale, (std::numeric_limits<int>::max() - 1.0) / magnitude);
    const QSignalBlocker block(m_slider);
    m_slider->setRange(qRound(minimum * m_scale), qRound(maximum * m_scale));
    m_slider->setValue(qRound(qBound(minimum, value(), maximum) * m_scale));
    setSingleStep(m_number->singleStep());
}
void ValueSlider::setSingleStep(double step) {
    if (!std::isfinite(step) || step <= 0) return;
    m_number->setSingleStep(step);
    const double limit = std::numeric_limits<int>::max() - 1.0;
    m_slider->setSingleStep(qMax(1, qRound(qMin(limit, step * m_scale))));
    m_slider->setPageStep(qMax(1, qRound(qMin(limit, step * m_scale * 10))));
}
void ValueSlider::setRange(double minimum, double maximum) {
    if (!std::isfinite(minimum) || !std::isfinite(maximum) || maximum < minimum) return;
    const QSignalBlocker block(m_number);
    m_number->setRange(minimum, maximum);
    configureTrack();
    setValueSilently(value());
}
void ValueSlider::setSliderRange(double minimum, double maximum) {
    if (!std::isfinite(minimum) || !std::isfinite(maximum) || maximum < minimum) return;
    m_customTrackRange = true;
    m_trackMinimum = minimum;
    m_trackMaximum = maximum;
    configureTrack();
}
void ValueSlider::setDecimals(int decimals) {
    const QSignalBlocker block(m_number);
    m_number->setDecimals(qBound(0, decimals, 4));
    configureTrack();
    setValueSilently(value());
}
void ValueSlider::setSuffix(const QString &suffix) {
    // Units remain discoverable without consuming space inside the numeric field.
    m_unit = suffix.trimmed();
    m_number->setAccessibleDescription(m_unit);
    m_slider->setAccessibleDescription(m_unit);
    refreshNumberHelp();
}
void ValueSlider::refreshNumberHelp() {
    m_number->setToolTip(QString("%1: %2%3").arg(accessibleName(),
        locale().toString(m_current, 'f', m_number->decimals()),
        m_unit.isEmpty() ? QString() : " " + m_unit));
}
void ValueSlider::setValue(double n) { updateValue(n); }
void ValueSlider::setValueSilently(double n) { updateValue(n, false); }
void ValueSlider::updateValue(double n, bool notify) {
    if (!std::isfinite(n)) return;
    const double previous = m_current;
    const QSignalBlocker a(m_slider), b(m_number);
    // A model echo must preserve partially typed text and the gesture origin.
    if (!m_textEditing || n != m_current || m_finishing) m_number->setValue(n);
    m_current = value();
    refreshNumberHelp();
    const double trackValue = qBound(double(m_slider->minimum()), m_current * m_scale,
                                    double(m_slider->maximum()));
    m_slider->setValue(qRound(trackValue));
    if (!m_editing) m_original = m_current;
    if (notify && previous != m_current) emit valueChanged(m_current);
}
void ValueSlider::beginEditing() {
    if (m_editing || m_finishing) return;
    m_original = m_current;
    m_editing = true;
    emit editingStarted();
}
void ValueSlider::finishEditing() {
    if (!m_editing || m_finishing) return;
    m_editing = false;
    m_textEditing = false;
    m_original = m_current;
    emit editingFinished();
}
void ValueSlider::finishInteraction() {
    if (!m_editing || m_finishing) return;
    if (m_textEditing) m_number->interpretText();
    { const QSignalBlocker block(m_slider); m_slider->setSliderDown(false); }
    finishEditing();
}
void ValueSlider::commitEditing() { finishInteraction(); }
void ValueSlider::cancelEditing() {
    if (!m_editing || m_finishing) return;
    m_finishing = true;
    m_ignoreSliderDrag = m_slider->isSliderDown();
    { const QSignalBlocker block(m_slider); m_slider->setSliderDown(false); }
    m_editing = false;
    m_textEditing = false;
    updateValue(m_original);
    emit editingCancelled();
    m_finishing = false;
}
bool ValueSlider::eventFilter(QObject *watched, QEvent *event) {
    if (event->type() == QEvent::Wheel) {
        const bool onTrack = watched == m_slider;
        const bool onNumber = watched == m_number || watched == m_number->findChild<QLineEdit *>();
        const bool focused = onTrack ? m_slider->hasFocus() :
            m_number->hasFocus() || m_number->findChild<QLineEdit *>()->hasFocus();
        // Scrolling an inspector must not edit a value just because the pen or
        // pointer passes over it. Never interpret an unfinished numeric draft.
        // Ignoring here lets Qt deliver the wheel to the surrounding scroll area.
        if ((onTrack || onNumber) && (!focused || m_textEditing)) {
            event->ignore();
            return true;
        }
    }
    if (event->type() == QEvent::ShortcutOverride || event->type() == QEvent::KeyPress) {
        auto *key = static_cast<QKeyEvent *>(event);
        if ((watched == m_number || watched == m_number->findChild<QLineEdit *>()) &&
            (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter)) {
            // Exact entry must not also activate a dialog's Create/Apply button.
            if (event->type() == QEvent::KeyPress) {
                m_number->interpretText();
                finishEditing();
            }
            event->accept();
            return true;
        }
        if (key->key() == Qt::Key_Escape && m_editing) {
            if (event->type() == QEvent::KeyPress) cancelEditing();
            event->accept();
            return true;
        }
    }
    if (watched == m_slider) {
        if (event->type() == QEvent::MouseButtonPress) m_ignoreSliderDrag = false;
        if (m_ignoreSliderDrag && (event->type() == QEvent::MouseMove || event->type() == QEvent::MouseButtonRelease)) {
            if (event->type() == QEvent::MouseButtonRelease) m_ignoreSliderDrag = false;
            return true;
        }
        if (event->type() == QEvent::MouseMove && m_slider->isSliderDown() &&
            !(static_cast<QMouseEvent *>(event)->buttons() & Qt::LeftButton)) finishInteraction();
        if (event->type() == QEvent::UngrabMouse || event->type() == QEvent::FocusOut) {
            if (m_slider->isSliderDown()) finishInteraction();
        }
    }
    if (watched == this && (event->type() == QEvent::Hide || event->type() == QEvent::WindowDeactivate ||
        (event->type() == QEvent::EnabledChange && !isEnabled()))) finishInteraction();
    return QWidget::eventFilter(watched, event);
}
} // namespace toon
