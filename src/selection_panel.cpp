#include "selection_panel.h"
#include "selection_extras.h"
#include "canvas.h"
#include "icons.h"
#include "ui_metrics.h"
#include "property_row.h"
#include "value_slider.h"

#include <QKeySequence>
#include <QEvent>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QToolButton>
#include <QVBoxLayout>

namespace toon {
namespace {
class SelectionStatus final : public QLabel {
public:
    SelectionStatus() {
        setTextFormat(Qt::PlainText);
        setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
        updateHeight();
    }
protected:
    void changeEvent(QEvent *event) override {
        QLabel::changeEvent(event);
        if (event->type() == QEvent::FontChange) updateHeight();
    }
    void paintEvent(QPaintEvent *) override {
        QPainter painter(this); painter.setPen(palette().color(QPalette::WindowText));
        painter.drawText(contentsRect(), Qt::AlignLeading | Qt::AlignVCenter,
            fontMetrics().elidedText(text(), Qt::ElideRight, contentsRect().width()));
    }
private:
    void updateHeight() { setFixedHeight(qMax(ui::metrics::actionTarget, fontMetrics().height() + 2)); }
};
}
SelectionPanel::SelectionPanel(Canvas *canvas, QWidget *parent)
    : QWidget(parent), m_canvas(canvas), m_document(canvas->document()), m_content(new QWidget) {
    setObjectName("selectionProperties"); setProperty("compactDrawingPanel", true); setMinimumSize(220, 180);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(ui::metrics::panelMargin, ui::metrics::panelMargin, ui::metrics::panelMargin, ui::metrics::panelMargin); layout->setSpacing(ui::metrics::controlGap);
    m_selectionActions = new QWidget; m_selectionActions->setObjectName("selectionActionRow");
    auto *actions = new QHBoxLayout(m_selectionActions); actions->setContentsMargins(0, 0, 0, 0); actions->setSpacing(ui::metrics::controlGap);
    auto action = [actions](const char *icon, const QString &label, const char *id, const QKeySequence &shortcut) {
        auto *button = new QToolButton; button->setObjectName(id); button->setIcon(appIcon(icon));
        button->setToolButtonStyle(Qt::ToolButtonIconOnly); button->setIconSize({ui::metrics::actionIcon, ui::metrics::actionIcon});
        button->setToolTip(label + " (" + shortcut.toString(QKeySequence::NativeText) + ")");
        button->setAccessibleName(label); button->setProperty("compactDrawingAction", true); button->setFixedSize(ui::metrics::actionTarget, ui::metrics::actionTarget);
        actions->addWidget(button); return button;
    };
    m_selectAll = action("scan", "Select all pixels", "selectAllPixels", QKeySequence::SelectAll);
    m_invert = action("contrast", "Invert selection", "invertPixelSelection", QKeySequence("Ctrl+Shift+I"));
    m_deselect = action("x", "Deselect", "deselectPixels", QKeySequence("Ctrl+D"));
    layout->addWidget(m_selectionActions);
    m_status = new SelectionStatus; m_status->setObjectName("pixelSelectionStatus"); layout->addWidget(m_status);
    m_begin = new QPushButton; m_begin->setObjectName("beginPixelTransform");
    m_begin->setIcon(appIcon("move")); m_begin->setToolTip("Transform pixels (" + QKeySequence("Ctrl+T").toString(QKeySequence::NativeText) + ")");
    m_begin->setProperty("compactDrawingButton", true);
    m_begin->setAccessibleName("Transform pixels");
    m_begin->setIconSize({ui::metrics::actionIcon, ui::metrics::actionIcon}); m_begin->setFixedSize(ui::metrics::actionTarget, ui::metrics::actionTarget);
    actions->addStretch(); actions->addWidget(m_begin);
    auto *scroll = new QScrollArea; scroll->setObjectName("selectionPropertiesScroll");
    scroll->setWidgetResizable(true); scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto *contents = new QVBoxLayout(m_content);
    contents->setContentsMargins(0, 0, 0, 0); contents->setSpacing(ui::metrics::controlGap);
    scroll->setWidget(m_content); layout->addWidget(scroll, 1);
    m_transformControls = new QWidget; m_transformControls->setObjectName("transformControls");
    auto *transformLayout = new QVBoxLayout(m_transformControls);
    transformLayout->setContentsMargins(0, 0, 0, 0); transformLayout->setSpacing(ui::metrics::controlGap);
    const int transformLabelWidth = qMax(ui::metrics::labelWidth, fontMetrics().horizontalAdvance("Width") + 2 + ui::metrics::controlGap + ui::metrics::actionTarget);
    auto number = [this, transformLayout, transformLabelWidth](const QString &label, const char *id, qreal minimum, qreal maximum,
                                qreal value, const QString &suffix) {
        auto *control = new ValueSlider(label, minimum, maximum, value, 1, suffix);
        control->setObjectName(id); control->setAccessibleName(label); control->setToolTip(label);
        control->setMinimumHeight(ui::metrics::actionTarget);
        control->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        auto *row = new PropertyRow(label, control); row->setLabelWidth(transformLabelWidth);
        row->setObjectName(control->objectName() + "Row"); transformLayout->addWidget(row);
        connect(control, &ValueSlider::valueChanged, this, [this, control] { editTransform(control); });
        return control;
    };
    m_x = number("X", "transformX", -32768, 32768, 0, " px");
    m_y = number("Y", "transformY", -32768, 32768, 0, " px");
    m_x->setToolTip("Horizontal displacement from the original pixels");
    m_y->setToolTip("Vertical displacement from the original pixels");
    m_width = number("Width", "transformWidth", 1, 1600, 100, " %");
    m_height = number("Height", "transformHeight", 1, 1600, 100, " %");
    m_width->setSliderRange(1, 400);
    m_height->setSliderRange(1, 400);
    m_rotation = number("Rotate", "transformRotation", -180, 180, 0, " °");
    m_aspect = new QToolButton; m_aspect->setObjectName("transformAspectLink");
    m_aspect->setCheckable(true); m_aspect->setIcon(appIcon("link-2")); m_aspect->setProperty("compactDrawingAction", true); m_aspect->setIconSize({ui::metrics::actionIcon, ui::metrics::actionIcon}); m_aspect->setFixedSize(ui::metrics::actionTarget, ui::metrics::actionTarget);
    m_aspect->setAccessibleName("Link width and height");
    m_aspect->setChecked(true); m_aspect->setToolTip("Keep the current proportions when changing width or height");
    qobject_cast<PropertyRow *>(m_width->parentWidget())->setAccessory(m_aspect);
    contents->addWidget(m_transformControls); contents->addWidget(new SelectionExtras(m_canvas)); contents->addStretch();
    m_commitControls = new QWidget; m_commitControls->setObjectName("transformCommitControls");
    auto *commit = new QHBoxLayout(m_commitControls); commit->setContentsMargins(0, 0, 0, 0); commit->setSpacing(ui::metrics::controlGap);
    m_apply = new QPushButton("Apply"); m_apply->setObjectName("applyPixelTransform");
    m_apply->setIcon(appIcon("check")); m_apply->setToolTip("Apply transform (Enter)");
    m_apply->setAccessibleName("Apply transform");
    m_cancel = new QPushButton("Cancel"); m_cancel->setObjectName("cancelPixelTransform");
    m_cancel->setIcon(appIcon("x")); m_cancel->setToolTip("Cancel transform (Esc)");
    m_cancel->setAccessibleName("Cancel transform");
    for (auto *button : {m_apply, m_cancel}) { button->setProperty("compactDrawingButton", true); button->setIconSize({ui::metrics::actionIcon, ui::metrics::actionIcon}); button->setMinimumHeight(ui::metrics::actionTarget); commit->addWidget(button); }
    layout->addWidget(m_commitControls);
    connect(m_selectAll, &QToolButton::clicked, m_document, &Document::selectAllPixels);
    connect(m_invert, &QToolButton::clicked, m_document, &Document::invertRasterSelection);
    connect(m_deselect, &QToolButton::clicked, m_document, &Document::clearRasterSelection);
    connect(m_begin, &QPushButton::clicked, this, [this] {
        if (m_canvas->tool() == Canvas::Tool::Transform) m_document->beginTransform();
        else m_canvas->setTool(Canvas::Tool::Transform);
        refresh();
    });
    connect(m_apply, &QPushButton::clicked, m_document, &Document::applyTransform);
    connect(m_cancel, &QPushButton::clicked, m_document, &Document::cancelTransform);
    connect(m_document, &Document::rasterSelectionChanged, this, &SelectionPanel::refresh);
    connect(m_document, &Document::transformChanged, this, &SelectionPanel::refresh);
    connect(m_document, &Document::selectionChanged, this, &SelectionPanel::refresh);
    connect(m_document, &Document::changed, this, &SelectionPanel::refresh);
    refresh();
}
void SelectionPanel::refresh() {
    m_refreshing = true;
    const bool open = m_document->isOpen();
    const bool active = m_document->transformActive();
    const bool stamp = m_document->stampPlacementActive();
    const bool canSelect = open && !m_document->editingGestureActive();
    m_content->setEnabled(open);
    const auto &selection = m_document->rasterSelection();
    m_selectAll->setEnabled(canSelect); m_invert->setEnabled(canSelect);
    m_deselect->setEnabled(canSelect && selection.active);
    m_selectAll->setVisible(!stamp); m_invert->setVisible(!stamp); m_deselect->setVisible(!stamp);
    m_selectionActions->setVisible(!active);
    if (auto *extras = findChild<SelectionExtras *>()) extras->setVisible(!stamp);
    if (stamp) m_status->setText("Stamp placement");
    else if (!selection.active) m_status->setText("No selection");
    else if (selection.path.isEmpty()) m_status->setText("Empty selection");
    else {
        const auto bounds = selection.path.boundingRect();
        m_status->setText(QString("Selection · %1 × %2 px").arg(bounds.width(), 0, 'f', 0).arg(bounds.height(), 0, 'f', 0));
    }
    m_status->setToolTip(m_status->text()); m_status->setAccessibleName(m_status->text());
    bool canTransform = canSelect && (!selection.active || !selection.path.isEmpty());
    if (canTransform) {
        const auto &layers = m_document->activeShot().layers;
        const auto &layer = layers[m_document->activeLayer()];
        canTransform = layerCanPaint(layers, m_document->activeLayer()) && !layer.alphaLocked && !layer.tiles.isEmpty();
    }
    m_begin->setVisible(!active); m_begin->setEnabled(canTransform);
    m_transformControls->setVisible(active);
    m_commitControls->setVisible(active);
    m_apply->setEnabled(active); m_cancel->setEnabled(active);
    m_apply->setText(stamp ? "Place" : "Apply");
    m_apply->setAccessibleName(stamp ? "Place stamp" : "Apply transform");
    m_cancel->setAccessibleName(stamp ? "Discard stamp" : "Cancel transform");
    m_apply->setToolTip(stamp ? "Place stamp (Enter)" : "Apply transform (Enter)");
    m_cancel->setToolTip(stamp ? "Discard stamp (Esc)" : "Cancel transform (Esc)");
    const auto parameters = m_document->transformParameters();
    const QSignalBlocker xBlock(m_x), yBlock(m_y), widthBlock(m_width), heightBlock(m_height), rotationBlock(m_rotation);
    const auto extent = open ? m_document->drawingBounds().size() : QSizeF(1920, 1080);
    m_x->setSliderRange(-extent.width(), extent.width());
    m_y->setSliderRange(-extent.height(), extent.height());
    m_x->setValueSilently(parameters.translation.x()); m_y->setValueSilently(parameters.translation.y());
    m_width->setValueSilently(parameters.scaleX * 100); m_height->setValueSilently(parameters.scaleY * 100);
    m_rotation->setValueSilently(parameters.rotation);
    m_refreshing = false;
}
void SelectionPanel::editTransform(ValueSlider *source) {
    if (m_refreshing || !m_document->transformActive()) return;
    const auto previous = m_document->transformParameters();
    auto parameters = previous;
    if (source == m_x) parameters.translation.setX(m_x->value());
    else if (source == m_y) parameters.translation.setY(m_y->value());
    else if (source == m_rotation) parameters.rotation = m_rotation->value();
    else if (source == m_width) {
        parameters.scaleX = m_width->value() / 100;
        if (m_aspect->isChecked()) parameters.scaleY = previous.scaleY * parameters.scaleX / previous.scaleX;
    } else if (source == m_height) {
        parameters.scaleY = m_height->value() / 100;
        if (m_aspect->isChecked()) parameters.scaleX = previous.scaleX * parameters.scaleY / previous.scaleY;
    }
    if (!m_document->previewTransform(parameters)) refresh();
}
} // namespace toon
