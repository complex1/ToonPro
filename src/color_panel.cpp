#include "color_panel.h"
#include "canvas.h"
#include "icons.h"
#include "ui_theme.h"
#include "ui_metrics.h"

#include <QAction>
#include <QButtonGroup>
#include <QComboBox>
#include <QInputDialog>
#include <QMenu>
#include <QScrollArea>
#include <QScrollBar>
#include <QResizeEvent>
#include <QFocusEvent>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QRegularExpressionValidator>
#include <QSignalBlocker>
#include <QToolButton>
#include <QVBoxLayout>
#include <cmath>

namespace toon {
ColorPicker::ColorPicker(QWidget *parent) : QWidget(parent) {
    setObjectName("colorPicker"); setAccessibleName("Color field and hue strip");
    setAccessibleDescription("Arrow keys adjust saturation and brightness. Shift Left or Right adjusts hue.");
    setToolTip("Choose saturation and brightness; drag the hue strip to change hue.\nArrow keys adjust the color; Shift + Left/Right adjusts hue.");
    setFocusPolicy(Qt::StrongFocus); setCursor(Qt::CrossCursor); setMouseTracking(true); setMinimumSize(160, 140);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
}
QRectF ColorPicker::colorRect() const { return QRectF(4, 4, qMax(1, width() - 42), qMax(1, height() - 8)); }
QRectF ColorPicker::hueRect() const { return QRectF(width() - 28, 4, 24, qMax(1, height() - 8)); }
void ColorPicker::setColor(const QColor &color) {
    if (!color.isValid()) return;
    if (color.hsvHueF() >= 0) m_hue = color.hsvHueF();
    m_saturation = color.hsvSaturationF(); m_value = color.valueF(); m_alpha = color.alphaF(); update();
}
QColor ColorPicker::color() const { return QColor::fromHsvF(m_hue, m_saturation, m_value, m_alpha); }
void ColorPicker::paintEvent(QPaintEvent *) {
    QPainter p(this); p.setRenderHint(QPainter::Antialiasing);
    const auto field = colorRect(), hue = hueRect();
    p.fillRect(field, QColor::fromHsvF(m_hue, 1, 1));
    QLinearGradient white(field.topLeft(), field.topRight()); white.setColorAt(0, Qt::white); white.setColorAt(1, QColor(255, 255, 255, 0));
    p.fillRect(field, white);
    QLinearGradient black(field.topLeft(), field.bottomLeft()); black.setColorAt(0, Qt::transparent); black.setColorAt(1, Qt::black);
    p.fillRect(field, black);
    QLinearGradient rainbow(hue.topLeft(), hue.bottomLeft());
    for (int i = 0; i <= 6; ++i) rainbow.setColorAt(i / 6.0, QColor::fromHsvF(i == 6 ? 0 : i / 6.0, 1, 1));
    p.fillRect(hue, rainbow);
    p.setBrush(Qt::NoBrush); p.setPen(QPen(QColor(ui::border), 1)); p.drawRect(field); p.drawRect(hue);
    const QPointF cursor(field.left() + m_saturation * field.width(), field.top() + (1 - m_value) * field.height());
    p.setPen(QPen(Qt::black, 3)); p.drawEllipse(cursor, 5, 5); p.setPen(QPen(Qt::white, 1.5)); p.drawEllipse(cursor, 5, 5);
    const qreal y = hue.top() + m_hue * hue.height();
    p.setPen(QPen(Qt::black, 4)); p.drawLine(QPointF(hue.left() - 1, y), QPointF(hue.right() + 1, y));
    p.setPen(QPen(Qt::white, 2)); p.drawLine(QPointF(hue.left() - 1, y), QPointF(hue.right() + 1, y));
    if (hasFocus()) { p.setPen(QPen(QColor(ui::focus), 1)); p.drawRoundedRect(QRectF(rect()).adjusted(1, 1, -1, -1), 3, 3); }
}
void ColorPicker::chooseAt(QPointF point) {
    if (m_drag == Drag::Hue) m_hue = qBound(0.0, (point.y() - hueRect().top()) / hueRect().height(), .99999);
    else if (m_drag == Drag::Color) {
        const auto field = colorRect();
        m_saturation = qBound(0.0, (point.x() - field.left()) / field.width(), 1.0);
        m_value = 1 - qBound(0.0, (point.y() - field.top()) / field.height(), 1.0);
    } else return;
    update(); emit colorChanged(color());
}
void ColorPicker::mousePressEvent(QMouseEvent *event) {
    if (event->button() != Qt::LeftButton) return;
    if (hueRect().adjusted(-3, -3, 3, 3).contains(event->position())) m_drag = Drag::Hue;
    else if (colorRect().adjusted(-3, -3, 3, 3).contains(event->position())) m_drag = Drag::Color;
    else return;
    setFocus(); chooseAt(event->position()); event->accept();
}
void ColorPicker::mouseMoveEvent(QMouseEvent *event) {
    if (!(event->buttons() & Qt::LeftButton)) { m_drag = Drag::None; return; }
    if (m_drag != Drag::None) { chooseAt(event->position()); event->accept(); }
}
void ColorPicker::mouseReleaseEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton && m_drag != Drag::None) { chooseAt(event->position()); m_drag = Drag::None; event->accept(); }
}
void ColorPicker::focusOutEvent(QFocusEvent *event) { m_drag = Drag::None; update(); QWidget::focusOutEvent(event); }
void ColorPicker::keyPressEvent(QKeyEvent *event) {
    const int direction = event->key() == Qt::Key_Left || event->key() == Qt::Key_Down ? -1 : 1;
    if (event->key() != Qt::Key_Left && event->key() != Qt::Key_Right && event->key() != Qt::Key_Up && event->key() != Qt::Key_Down) {
        QWidget::keyPressEvent(event); return;
    }
    if (event->modifiers().testFlag(Qt::ShiftModifier)) m_hue = std::fmod(m_hue + direction / 360.0 + 1, 1.0);
    else if (event->key() == Qt::Key_Left || event->key() == Qt::Key_Right) m_saturation = qBound(0.0, m_saturation + direction * .01, 1.0);
    else m_value = qBound(0.0, m_value + direction * .01, 1.0);
    update(); emit colorChanged(color()); event->accept();
}
namespace {
class PaletteStatus final : public QLabel {
public:
    explicit PaletteStatus(QWidget *parent) : QLabel(parent) {
        setTextFormat(Qt::PlainText); setMinimumWidth(0); setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
        setFixedHeight(fontMetrics().height() + ui::metrics::controlGap);
    }
    void message(const QString &text) { m_text = text; setAccessibleName(text); setToolTip(text); updateText(); }
protected:
    void resizeEvent(QResizeEvent *event) override { QLabel::resizeEvent(event); updateText(); }
    void changeEvent(QEvent *event) override {
        QLabel::changeEvent(event);
        if (event->type() == QEvent::FontChange) {
            setFixedHeight(fontMetrics().height() + ui::metrics::controlGap); updateText();
        }
    }
private:
    void updateText() { setText(fontMetrics().elidedText(m_text, Qt::ElideRight, qMax(0, width()))); }
    QString m_text;
};
QString promptName(QWidget *parent, const QString &title, const QString &label, const QString &initial) {
    QInputDialog dialog(parent); dialog.setObjectName("colorPaletteNameDialog"); dialog.setWindowTitle(title);
    dialog.setLabelText(label); dialog.setTextValue(initial); dialog.setInputMode(QInputDialog::TextInput);
    if (auto *edit = dialog.findChild<QLineEdit *>()) edit->setMaxLength(100);
    return dialog.exec() == QDialog::Accepted ? dialog.textValue().trimmed() : QString{};
}
QIcon colorChip(const QColor &color) {
    QPixmap chip(56, 56); chip.setDevicePixelRatio(2); chip.fill(Qt::transparent); QPainter p(&chip); p.setRenderHint(QPainter::Antialiasing);
    p.save(); QPainterPath shape; shape.addRoundedRect(QRectF(1, 1, 26, 26), 3, 3); p.setClipPath(shape);
    for (int y = 1; y < 28; y += 6) for (int x = 1; x < 28; x += 6)
        p.fillRect(QRect(x, y, 6, 6), ((x / 6 + y / 6) % 2) ? QColor("#999999") : QColor("#dddddd"));
    p.fillPath(shape, color); p.restore(); p.setBrush(Qt::NoBrush); p.setPen(QColor(ui::border)); p.drawPath(shape); return QIcon(chip);
}
}
ColorPanel::ColorPanel(Canvas *canvas, QWidget *parent) : QWidget(parent), m_canvas(canvas) {
    setObjectName("colorContent"); setProperty("compactDrawingPanel", true); setMinimumWidth(220);
    auto *layout = new QVBoxLayout(this); layout->setContentsMargins(ui::metrics::panelMargin, ui::metrics::panelMargin, ui::metrics::panelMargin, ui::metrics::panelMargin); layout->setSpacing(ui::metrics::controlGap);
    auto *colors = new QHBoxLayout; colors->setSpacing(ui::metrics::controlGap);
    auto chip = [this, colors](const char *id, const QString &label) {
        auto *button = new QToolButton(this); button->setObjectName(id); button->setProperty("compactDrawingAction", true); button->setText(label);
        button->setToolButtonStyle(Qt::ToolButtonIconOnly); button->setIconSize({22, 22}); button->setFixedSize(ui::metrics::actionTarget, ui::metrics::actionTarget);
        button->setStyleSheet("QToolButton { min-width: 0; min-height: 0; padding: 2px; }");
        button->setCheckable(true); button->setToolTip(label); button->setAccessibleName(label); colors->addWidget(button); return button;
    };
    m_foreground = chip("brushColor", "Edit foreground color"); m_background = chip("backgroundColor", "Edit background color");
    auto *group = new QButtonGroup(this); group->setExclusive(true); group->addButton(m_foreground); group->addButton(m_background);
    auto *swap = new QToolButton(this); swap->setObjectName("swapColors"); swap->setProperty("compactDrawingAction", true); swap->setIcon(appIcon("arrow-left-right"));
    swap->setIconSize({ui::metrics::actionIcon, ui::metrics::actionIcon}); swap->setFixedSize(ui::metrics::actionTarget, ui::metrics::actionTarget); swap->setToolTip("Swap foreground and background colors"); swap->setAccessibleName("Swap foreground and background colors");
    colors->addWidget(swap); colors->addStretch(); layout->addLayout(colors);
    m_picker = new ColorPicker(this); layout->addWidget(m_picker, 1);
    auto *exact = new QHBoxLayout; exact->setSpacing(ui::metrics::controlGap); auto *label = new QLabel("Hex", this); exact->addWidget(label);
    m_hex = new QLineEdit(this); m_hex->setObjectName("colorHex"); m_hex->setProperty("compactDrawingField", true); m_hex->setMaxLength(9); label->setBuddy(m_hex);
    m_hex->setAccessibleName("Hex color"); m_hex->setToolTip("Hex color: #RRGGBB or #AARRGGBB");
    m_hex->setValidator(new QRegularExpressionValidator(QRegularExpression("#[0-9a-fA-F]{0,8}"), m_hex));
    exact->addWidget(m_hex, 1); layout->addLayout(exact);
    auto iconAction = [this](const char *id, const QString &icon, const QString &help) {
        auto *button = new QToolButton(this); button->setObjectName(id); button->setProperty("compactDrawingAction", true); button->setIcon(appIcon(icon));
        button->setIconSize({ui::metrics::actionIcon, ui::metrics::actionIcon}); button->setFixedSize(ui::metrics::actionTarget, ui::metrics::actionTarget);
        button->setToolTip(help); button->setAccessibleName(help); return button;
    };
    auto menuAction = [this](const char *id, const QString &icon, const QString &text) {
        auto *action = new QAction(appIcon(icon), text, this); action->setObjectName(id); action->setToolTip(text); return action;
    };
    m_paletteAdd = menuAction("colorPaletteAdd", "plus", "New palette");
    m_swatchUpdate = menuAction("colorSwatchUpdate", "save", "Replace selected swatch with current color");
    m_swatchDelete = menuAction("colorSwatchDelete", "trash", "Delete selected swatch");
    auto *paletteRow = new QHBoxLayout; paletteRow->setSpacing(ui::metrics::controlGap);
    m_paletteChoice = new QComboBox(this); m_paletteChoice->setObjectName("colorPaletteChoice"); m_paletteChoice->setProperty("compactDrawingField", true);
    m_paletteChoice->setAccessibleName("Color palette"); m_paletteChoice->setToolTip("Choose a saved color palette");
    m_paletteChoice->setMinimumWidth(0); m_paletteChoice->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    m_paletteChoice->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon); m_paletteChoice->setMinimumContentsLength(1);
    paletteRow->addWidget(m_paletteChoice, 1);
    m_swatchAdd = iconAction("colorSwatchAdd", "plus", "Save the current color as a named swatch"); paletteRow->addWidget(m_swatchAdd);
    m_paletteMore = iconAction("colorPaletteMore", "ellipsis", "Palette and selected swatch actions"); paletteRow->addWidget(m_paletteMore);
    layout->addLayout(paletteRow);
    m_swatchScroll = new QScrollArea(this); m_swatchScroll->setObjectName("colorSwatchScroll");
    m_swatchScroll->setWidgetResizable(true); m_swatchScroll->setFrameShape(QFrame::NoFrame);
    m_swatchScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff); m_swatchScroll->setMinimumHeight(60); m_swatchScroll->setMaximumHeight(152);
    m_swatchContent = new QWidget; m_swatchContent->setObjectName("colorSwatches");
    m_swatchGrid = new QGridLayout(m_swatchContent); m_swatchGrid->setContentsMargins(0, 0, 0, 0); m_swatchGrid->setSpacing(ui::metrics::controlGap);
    m_swatchGrid->setAlignment(Qt::AlignTop); m_swatchScroll->setWidget(m_swatchContent); layout->addWidget(m_swatchScroll);
    m_paletteStatus = new PaletteStatus(this); m_paletteStatus->setObjectName("colorPaletteStatus"); layout->addWidget(m_paletteStatus);
    connect(m_paletteChoice, &QComboBox::currentIndexChanged, this, [this](int index) {
        if (m_refreshingPalettes || index < 0) return;
        m_paletteId = m_paletteChoice->itemData(index).toString(); m_swatchId.clear(); m_paletteError.clear();
        refreshSwatches(); emit paletteSelected(m_paletteId);
    });
    connect(m_paletteAdd, &QAction::triggered, this, &ColorPanel::createPalette);
    connect(m_paletteMore, &QToolButton::clicked, this, &ColorPanel::paletteMenu);
    connect(m_swatchAdd, &QToolButton::clicked, this, &ColorPanel::addSwatch);
    connect(m_swatchUpdate, &QAction::triggered, this, &ColorPanel::updateSwatch);
    connect(m_swatchDelete, &QAction::triggered, this, &ColorPanel::deleteSwatch);
    refreshPalettes();
    connect(m_foreground, &QToolButton::clicked, this, [this] { m_editBackground = false; refresh(); });
    connect(m_background, &QToolButton::clicked, this, [this] { m_editBackground = true; refresh(); });
    connect(swap, &QToolButton::clicked, this, [this] {
        auto brush = m_canvas->brush; std::swap(brush.color, brush.backgroundColor); m_canvas->setBrushSettings(brush);
    });
    connect(m_picker, &ColorPicker::colorChanged, this, &ColorPanel::chooseColor);
    connect(m_hex, &QLineEdit::editingFinished, this, [this] {
        const auto color = QColor(m_hex->text()); m_hex->setModified(false);
        if (color.isValid()) chooseColor(color); else refresh();
    });
    connect(canvas, &Canvas::brushChanged, this, &ColorPanel::refresh);
    connect(canvas, &Canvas::documentChanged, this, [this] { setEnabled(m_canvas->document()->isOpen()); });
    refresh();
}
void ColorPanel::refresh() {
    setEnabled(m_canvas->document()->isOpen());
    m_foreground->setChecked(!m_editBackground); m_background->setChecked(m_editBackground);
    m_foreground->setIcon(colorChip(m_canvas->brush.color)); m_background->setIcon(colorChip(m_canvas->brush.backgroundColor));
    const auto color = m_editBackground ? m_canvas->brush.backgroundColor : m_canvas->brush.color;
    m_picker->setColor(color); const QSignalBlocker block(m_hex);
    if (!m_hex->hasFocus() || !m_hex->isModified())
        m_hex->setText(color.name(color.alpha() == 255 ? QColor::HexRgb : QColor::HexArgb).toUpper());
    refreshPaletteActions();
}
void ColorPanel::chooseColor(const QColor &color) {
    if (!color.isValid() || !isEnabled()) return;
    if (m_editBackground) { auto brush = m_canvas->brush; brush.backgroundColor = color; m_canvas->setBrushSettings(brush); }
    else m_canvas->setBrushColor(color);
}
void ColorPanel::setPaletteLibrary(PaletteLibrary *library) {
    if (m_library == library) return;
    if (m_library) disconnect(m_library, nullptr, this, nullptr);
    m_library = library; m_paletteError.clear();
    if (m_library) {
        connect(m_library, &PaletteLibrary::changed, this, &ColorPanel::refreshPalettes);
        connect(m_library, &QObject::destroyed, this, [this] { m_library = nullptr; refreshPalettes(); });
    }
    refreshPalettes();
}
ColorPalette ColorPanel::displayedPalette() const {
    if (m_library) {
        if (const auto *entry = m_library->palette(m_paletteId)) return *entry;
        if (m_library->writable()) return {};
    }
    return standardColorPalette();
}
void ColorPanel::setCurrentPaletteId(const QString &id) {
    const int row = m_paletteChoice->findData(id);
    if (row >= 0) m_paletteChoice->setCurrentIndex(row);
}
void ColorPanel::refreshPalettes() {
    const auto previous = m_paletteId;
    m_refreshingPalettes = true; const QSignalBlocker block(m_paletteChoice);
    m_paletteChoice->clear();
    if (m_library && m_library->writable()) {
        for (const auto &entry : m_library->entries()) m_paletteChoice->addItem(entry.name, entry.id);
    } else if (m_library && !m_library->entries().isEmpty()) {
        for (const auto &entry : m_library->entries()) m_paletteChoice->addItem(entry.name, entry.id);
    } else {
        const auto standard = standardColorPalette(); m_paletteChoice->addItem(standard.name, standard.id);
    }
    int row = m_paletteChoice->findData(m_paletteId); if (row < 0 && m_paletteChoice->count()) row = 0;
    m_paletteChoice->setCurrentIndex(row); m_paletteId = row < 0 ? QString{} : m_paletteChoice->itemData(row).toString();
    if (previous != m_paletteId) m_swatchId.clear();
    m_refreshingPalettes = false; refreshSwatches();
    if (previous != m_paletteId) emit paletteSelected(m_paletteId);
}
void ColorPanel::refreshSwatches() {
    const int scroll = m_swatchScroll->verticalScrollBar()->value();
    while (auto *item = m_swatchGrid->takeAt(0)) { delete item->widget(); delete item; }
    const auto palette = displayedPalette(); bool selectedExists = false;
    for (int i = 0; i < palette.swatches.size(); ++i) {
        const auto swatch = palette.swatches[i]; auto *button = new QToolButton(m_swatchContent);
        button->setObjectName(QString("colorSwatch%1").arg(i)); button->setProperty("swatchId", swatch.id);
        button->setIcon(colorChip(swatch.color)); button->setIconSize({22, 22}); button->setMinimumWidth(ui::metrics::actionTarget); button->setFixedHeight(ui::metrics::actionTarget);
        button->setStyleSheet("QToolButton { min-width: 0; min-height: 0; padding: 2px; }");
        button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed); button->setCheckable(true);
        button->setChecked(swatch.id == m_swatchId); selectedExists |= swatch.id == m_swatchId;
        const auto hex = swatch.color.name(swatch.color.alpha() == 255 ? QColor::HexRgb : QColor::HexArgb).toUpper();
        button->setToolTip(swatch.name + " · " + hex); button->setAccessibleName(swatch.name + " · " + hex);
        button->setContextMenuPolicy(Qt::CustomContextMenu); m_swatchGrid->addWidget(button, i / 6, i % 6);
        connect(button, &QToolButton::clicked, this, [this, swatch] {
            m_swatchId = swatch.id; m_paletteError.clear(); chooseColor(swatch.color); refreshPaletteActions();
        });
        connect(button, &QToolButton::customContextMenuRequested, this, [this, button, id = swatch.id](const QPoint &point) {
            m_swatchId = id; refreshPaletteActions(); swatchMenu(id, button->mapToGlobal(point));
        });
    }
    if (!selectedExists) m_swatchId.clear();
    m_swatchScroll->verticalScrollBar()->setValue(scroll); refreshPaletteActions();
}
void ColorPanel::refreshPaletteActions() {
    const bool editable = m_library && m_library->writable();
    const auto palette = displayedPalette();
    const ColorSwatch *selected = nullptr;
    for (const auto &swatch : palette.swatches) if (swatch.id == m_swatchId) { selected = &swatch; break; }
    m_paletteAdd->setEnabled(editable && m_library->entries().size() < maxColorPalettes);
    m_paletteMore->setEnabled(editable);
    m_swatchAdd->setEnabled(editable && !palette.id.isEmpty() && palette.swatches.size() < maxPaletteSwatches);
    m_swatchUpdate->setEnabled(editable && selected); m_swatchDelete->setEnabled(editable && selected);
    for (auto *button : m_swatchContent->findChildren<QToolButton *>()) button->setChecked(button->property("swatchId").toString() == m_swatchId);
    QString status = m_library && !m_library->writable() ? m_library->loadError() : m_paletteError;
    if (status.isEmpty() && selected) status = selected->name;
    if (status.isEmpty() && editable && palette.id.isEmpty()) status = "Create a palette to save colors.";
    static_cast<PaletteStatus *>(m_paletteStatus)->message(status);
}
void ColorPanel::reportPaletteError(const QString &error) { m_paletteError = error; refreshPaletteActions(); }
void ColorPanel::createPalette() {
    if (!m_library || !m_library->writable()) return;
    const auto name = promptName(this, "New palette", "Palette name", {}); if (name.isEmpty()) return;
    QString error; const auto id = m_library->createPalette(name, &error);
    if (id.isEmpty()) reportPaletteError(error); else { m_paletteError.clear(); setCurrentPaletteId(id); }
}
void ColorPanel::renamePalette() {
    if (!m_library || !m_library->writable()) return;
    const auto palette = displayedPalette(); if (palette.id.isEmpty()) return;
    const auto name = promptName(this, "Rename palette", "Palette name", palette.name); if (name.isEmpty()) return;
    QString error; if (!m_library->renamePalette(palette.id, name, &error)) reportPaletteError(error);
    else { m_paletteError.clear(); refreshPaletteActions(); }
}
void ColorPanel::deletePalette() {
    if (!m_library || !m_library->writable()) return;
    QString error; if (!m_library->removePalette(m_paletteId, &error)) reportPaletteError(error);
    else { m_paletteError.clear(); refreshPaletteActions(); }
}
void ColorPanel::addSwatch() {
    if (!m_library || !m_library->writable() || m_paletteId.isEmpty()) return;
    const auto palette = displayedPalette(); int number = 1;
    auto used = [&](const QString &name) { for (const auto &entry : palette.swatches) if (entry.name.compare(name, Qt::CaseInsensitive) == 0) return true; return false; };
    while (used(QString("Color %1").arg(number))) ++number;
    const auto name = promptName(this, "Save color", "Swatch name", QString("Color %1").arg(number)); if (name.isEmpty()) return;
    const auto color = m_editBackground ? m_canvas->brush.backgroundColor : m_canvas->brush.color;
    QString error; const auto id = m_library->addSwatch(m_paletteId, name, color, &error);
    if (id.isEmpty()) reportPaletteError(error); else { m_paletteError.clear(); m_swatchId = id; refreshPaletteActions(); }
}
void ColorPanel::updateSwatch() {
    if (!m_library || !m_library->writable() || m_swatchId.isEmpty()) return;
    const auto color = m_editBackground ? m_canvas->brush.backgroundColor : m_canvas->brush.color;
    QString error; if (!m_library->updateSwatch(m_paletteId, m_swatchId, color, &error)) reportPaletteError(error);
    else { m_paletteError.clear(); refreshPaletteActions(); }
}
void ColorPanel::renameSwatch() {
    if (!m_library || !m_library->writable() || m_swatchId.isEmpty()) return;
    const auto palette = displayedPalette(); QString initial;
    for (const auto &swatch : palette.swatches) if (swatch.id == m_swatchId) initial = swatch.name;
    const auto name = promptName(this, "Rename swatch", "Swatch name", initial); if (name.isEmpty()) return;
    QString error; if (!m_library->renameSwatch(m_paletteId, m_swatchId, name, &error)) reportPaletteError(error);
    else { m_paletteError.clear(); refreshPaletteActions(); }
}
void ColorPanel::deleteSwatch() {
    if (!m_library || !m_library->writable() || m_swatchId.isEmpty()) return;
    QString error; if (!m_library->removeSwatch(m_paletteId, m_swatchId, &error)) reportPaletteError(error);
    else { m_paletteError.clear(); refreshPaletteActions(); }
}
void ColorPanel::paletteMenu() {
    QMenu menu(this); menu.setObjectName("colorPaletteMenu");
    menu.addAction(m_paletteAdd);
    auto *rename = menu.addAction(appIcon("pen-tool"), "Rename palette"); rename->setObjectName("colorPaletteRename");
    auto *remove = menu.addAction(appIcon("trash"), "Delete palette"); remove->setObjectName("colorPaletteDelete");
    rename->setEnabled(!m_paletteId.isEmpty()); remove->setEnabled(!m_paletteId.isEmpty());
    connect(rename, &QAction::triggered, this, &ColorPanel::renamePalette); connect(remove, &QAction::triggered, this, &ColorPanel::deletePalette);
    menu.addSeparator();
    auto *renameSwatchAction = menu.addAction(appIcon("pen-tool"), "Rename selected swatch"); renameSwatchAction->setObjectName("colorSwatchRenameAction");
    renameSwatchAction->setEnabled(m_swatchUpdate->isEnabled()); connect(renameSwatchAction, &QAction::triggered, this, &ColorPanel::renameSwatch);
    menu.addAction(m_swatchUpdate); menu.addAction(m_swatchDelete);
    menu.exec(m_paletteMore->mapToGlobal(QPoint(0, m_paletteMore->height())));
}
void ColorPanel::swatchMenu(const QString &id, const QPoint &globalPosition) {
    const auto palette = displayedPalette(); QColor color;
    for (const auto &swatch : palette.swatches) if (swatch.id == id) color = swatch.color;
    if (!color.isValid()) return;
    QMenu menu(this); menu.setObjectName("colorSwatchMenu");
    auto *use = menu.addAction("Use color"); connect(use, &QAction::triggered, this, [this, color] { chooseColor(color); });
    menu.addSeparator();
    auto *rename = menu.addAction(appIcon("pen-tool"), "Rename swatch"); rename->setObjectName("colorSwatchRenameAction");
    auto *replace = menu.addAction(appIcon("save"), "Replace with current color"); replace->setObjectName("colorSwatchReplaceAction");
    auto *remove = menu.addAction(appIcon("trash"), "Delete swatch"); remove->setObjectName("colorSwatchDeleteAction");
    for (auto *action : {rename, replace, remove}) action->setEnabled(m_library && m_library->writable());
    connect(rename, &QAction::triggered, this, &ColorPanel::renameSwatch);
    connect(replace, &QAction::triggered, this, &ColorPanel::updateSwatch); connect(remove, &QAction::triggered, this, &ColorPanel::deleteSwatch);
    menu.exec(globalPosition);
}
} // namespace toon
