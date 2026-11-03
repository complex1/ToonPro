#include "onion_settings_dialog.h"
#include "icons.h"
#include "ui_theme.h"
#include "value_slider.h"
#include <QColorDialog>
#include <QDialogButtonBox>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QToolButton>
#include <QVBoxLayout>
#include <QtMath>

namespace toon {
namespace {
QToolButton *colorButton(QWidget *parent, const char *name, const QString &help) {
    auto *button = new QToolButton(parent);
    button->setObjectName(name); button->setFixedSize(30, 30); button->setIconSize({20,20});
    button->setAccessibleName(help); button->setToolTip(help);
    return button;
}
void updateChip(QToolButton *button, const QColor &color) {
    const qreal ratio=button->devicePixelRatioF();
    QPixmap icon(qCeil(20*ratio),qCeil(20*ratio)); icon.setDevicePixelRatio(ratio); icon.fill(Qt::transparent);
    {QPainter p(&icon); p.setRenderHint(QPainter::Antialiasing); p.setPen(QColor(ui::muted)); p.setBrush(color); p.drawRoundedRect(QRectF(1,1,18,18),3,3);}
    button->setIcon(QIcon(icon)); button->setAccessibleDescription(color.name());
}
}
OnionSettingsDialog::OnionSettingsDialog(const OnionSkinSettings &settings, QWidget *parent)
    : QDialog(parent), m_original(validOnionSkinSettings(settings) ? settings : OnionSkinSettings{}), m_settings(m_original) {
    setObjectName("onionSettingsDialog"); setWindowTitle("Onion skin"); setMinimumWidth(240); resize(280, 290);
    auto *layout = new QVBoxLayout(this); layout->setContentsMargins(12,12,12,12); layout->setSpacing(8);
    auto section = [this,layout](const QString &label, const char *name, ValueSlider **value, QToolButton **color, bool previous) {
        auto *row = new QHBoxLayout;
        auto *title = new QLabel(label, this); row->addWidget(title,1);
        *color = colorButton(this, previous ? "onionPreviousColor" : "onionNextColor", label + " drawing color");
        row->addWidget(*color); layout->addLayout(row);
        *value = new ValueSlider(label + " drawings", 0, maxOnionDrawings, previous ? m_settings.previousCount : m_settings.nextCount, 0, {}, this);
        (*value)->setObjectName(name);
        (*value)->setToolTip("Number of distinct drawings · held and repeated drawings are skipped");
        layout->addWidget(*value);
        connect(*color, &QToolButton::clicked, this, [this,previous] { commitEditors(); chooseColor(previous); });
        connect(*value, &ValueSlider::valueChanged, this, [this,previous](double count) {
            if (previous) m_settings.previousCount = qRound(count); else m_settings.nextCount = qRound(count);
            emit settingsChanged(m_settings);
        });
    };
    section("Previous", "onionPreviousCount", &m_previous, &m_previousColor, true);
    section("Next", "onionNextCount", &m_next, &m_nextColor, false);
    layout->addWidget(new QLabel("Opacity",this));
    m_opacity = new ValueSlider("Onion opacity", 0, 100, m_settings.opacity * 100, 0, "%", this);
    m_opacity->setObjectName("onionOpacity"); layout->addWidget(m_opacity);
    connect(m_opacity, &ValueSlider::valueChanged, this, [this](double value) {
        m_settings.opacity = value / 100.; emit settingsChanged(m_settings);
    });
    layout->addStretch();
    auto *footer = new QHBoxLayout;
    auto *reset = new QToolButton(this); reset->setObjectName("onionReset"); reset->setIcon(appIcon("rotate-ccw"));
    reset->setIconSize({20,20}); reset->setFixedSize(30,30); reset->setToolTip("Reset onion skin settings"); reset->setAccessibleName("Reset onion skin settings");
    footer->addWidget(reset); footer->addStretch(); connect(reset,&QToolButton::clicked,this,&OnionSettingsDialog::resetDefaults);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Cancel | QDialogButtonBox::Ok, this);
    buttons->button(QDialogButtonBox::Ok)->setObjectName("onionAccept");
    buttons->button(QDialogButtonBox::Cancel)->setObjectName("onionCancel");
    connect(buttons,&QDialogButtonBox::accepted,this,&OnionSettingsDialog::accept);
    connect(buttons,&QDialogButtonBox::rejected,this,&OnionSettingsDialog::reject);
    footer->addWidget(buttons); layout->addLayout(footer);
    updateControls();
}
void OnionSettingsDialog::commitEditors() {
    m_previous->commitEditing(); m_next->commitEditing(); m_opacity->commitEditing();
}
void OnionSettingsDialog::updateControls() {
    m_previous->setValueSilently(m_settings.previousCount); m_next->setValueSilently(m_settings.nextCount);
    m_opacity->setValueSilently(m_settings.opacity * 100);
    updateChip(m_previousColor,m_settings.previousColor); updateChip(m_nextColor,m_settings.nextColor);
}
void OnionSettingsDialog::chooseColor(bool previous) {
    const QColor before = previous ? m_settings.previousColor : m_settings.nextColor;
    QColorDialog dialog(before, this); dialog.setObjectName("onionColorDialog");
    dialog.setWindowTitle(previous ? "Previous drawing color" : "Next drawing color");
    dialog.setOption(QColorDialog::DontUseNativeDialog);
    connect(&dialog,&QColorDialog::currentColorChanged,this,[this,previous](QColor color) {
        if (!color.isValid()) return;
        color.setAlpha(255);
        if (previous) m_settings.previousColor = color; else m_settings.nextColor = color;
        updateChip(previous ? m_previousColor : m_nextColor,color); emit settingsChanged(m_settings);
    });
    if (dialog.exec() != QDialog::Accepted) {
        if (previous) m_settings.previousColor = before; else m_settings.nextColor = before;
        updateChip(previous ? m_previousColor : m_nextColor,before); emit settingsChanged(m_settings);
    }
}
void OnionSettingsDialog::resetDefaults() {
    m_previous->cancelEditing(); m_next->cancelEditing(); m_opacity->cancelEditing();
    m_settings = {}; updateControls(); emit settingsChanged(m_settings);
}
void OnionSettingsDialog::accept() { commitEditors(); QDialog::accept(); }
void OnionSettingsDialog::reject() {
    m_previous->cancelEditing(); m_next->cancelEditing(); m_opacity->cancelEditing();
    m_settings = m_original; updateControls(); emit settingsChanged(m_settings); QDialog::reject();
}
} // namespace toon
