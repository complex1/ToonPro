#include "keyframe_panel.h"
#include "timeline_controller.h"
#include "canvas.h"
#include "document.h"
#include "drawing_tool_controller.h"
#include "easing_graph_widget.h"
#include "keyframe_transform_tool.h"
#include "motion_controls.h"
#include "property_row.h"
#include "icons.h"
#include "ui_metrics.h"
#include "ui_theme.h"
#include "value_slider.h"
#include <QLabel>
#include <QPainter>
#include <QScopedValueRollback>
#include <QScrollArea>
#include <QResizeEvent>
#include <QSignalBlocker>
#include <QStyleOptionToolButton>
#include <QStylePainter>
#include <QTabBar>
#include <QToolButton>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>

namespace toon {
namespace {
QIcon diamondIcon() {
    QPixmap image(40, 40); image.setDevicePixelRatio(2); image.fill(Qt::transparent);
    QPainter painter(&image); painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(QColor(ui::text), 1.8)); painter.setBrush(Qt::NoBrush);
    painter.drawPolygon(QPolygonF{QPointF(10, 3), QPointF(17, 10), QPointF(10, 17), QPointF(3, 10)});
    return QIcon(image);
}
QToolButton *action(QWidget *parent, const QString &id, const QString &icon, const QString &help, bool checkable = false) {
    auto *button = new QToolButton(parent); button->setObjectName(id);
    button->setIcon(icon == "diamond" ? diamondIcon() : appIcon(icon));
    button->setIconSize({ui::metrics::actionIcon, ui::metrics::actionIcon});
    button->setFixedSize(ui::metrics::actionTarget, ui::metrics::actionTarget);
    button->setProperty("compactDrawingAction", true); button->setFocusPolicy(Qt::StrongFocus);
    button->setToolTip(help); button->setAccessibleName(help); button->setCheckable(checkable); return button;
}
const Shot *shot(TimelineController *controller) {
    const auto *document = controller->document();
    return document->isOpen() && !document->project().shots.isEmpty() ? &document->activeShot() : nullptr;
}
const RasterLayer *layer(const Shot *shot, const QString &id) {
    if (shot) for (const auto &candidate : shot->layers) if (candidate.id == id) return &candidate;
    return nullptr;
}
bool locked(const Shot &shot, const QString &id, bool position = false) {
    QString current = id;
    for (int depth = 0; depth <= 8 && !current.isEmpty(); ++depth) {
        const auto *candidate = layer(&shot, current); if (!candidate) return true;
        if (candidate->locked || (position && candidate->positionLocked)) return true;
        current = candidate->parentId;
    }
    return false;
}
qreal displayValue(KeyframeProperty property, qreal value) {
    return keyframePropertyIsPercent(property) ? value * 100 : value;
}
qreal storedValue(KeyframeProperty property, qreal value) {
    return keyframePropertyIsPercent(property) ? value / 100 : value;
}
int propertyCategory(KeyframeProperty property) { return keyframePropertyIsMask(property)?2:keyframePropertyIsEffect(property)?1:0; }
QString propertyUnit(KeyframeProperty property) {
    if(keyframePropertyIsDiscrete(property))return {};
    if(keyframePropertyIsPercent(property)||property==KeyframeProperty::Contrast||property==KeyframeProperty::Saturation)return "%";
    if(property==KeyframeProperty::Rotation||property==KeyframeProperty::MaskRotation||property==KeyframeProperty::Hue)return "degrees";
    return property==KeyframeProperty::Exposure?"stops":"px";
}
class ElidedLabel final : public QLabel {
public: using QLabel::QLabel;
protected: void paintEvent(QPaintEvent *) override {
    QPainter painter(this); painter.setPen(palette().color(QPalette::WindowText));
    painter.drawText(rect(), Qt::AlignLeft | Qt::AlignVCenter, fontMetrics().elidedText(text(), Qt::ElideRight, width()));
}
};
class CompactChannelLabel final : public QToolButton {
public: using QToolButton::QToolButton;
    QSize minimumSizeHint() const override { auto size=QToolButton::minimumSizeHint();size.setWidth(ui::metrics::actionTarget);return size; }
protected:
    void paintEvent(QPaintEvent *) override {
        QStylePainter painter(this);QStyleOptionToolButton option;initStyleOption(&option);
        option.text=fontMetrics().elidedText(text(),Qt::ElideRight,std::max(0,contentsRect().width()-8));
        painter.drawComplexControl(QStyle::CC_ToolButton,option);
    }
};
class ChannelCategoryTabs final : public QTabBar {
public: using QTabBar::QTabBar;
protected:
    void resizeEvent(QResizeEvent *event) override { QTabBar::resizeEvent(event); fitLabels(); }
    void changeEvent(QEvent *event) override { QTabBar::changeEvent(event); if(event->type()==QEvent::FontChange)fitLabels(); }
private:
    void fitLabels(){
        const QStringList names{"Transform","Effects","Mask"};int needed=0;
        for(const auto &name:names)needed+=fontMetrics().horizontalAdvance(name)+32;
        for(int i=0;i<std::min(count(),int(names.size()));++i)setTabText(i,width()>=needed?names[i]:QString());
    }
};
}

KeyframePanel::KeyframePanel(TimelineController *controller, QWidget *parent) : QWidget(parent), m_controller(controller) {
    setObjectName("keyframeProperties"); setMinimumWidth(0);
    auto *outer = new QVBoxLayout(this); outer->setContentsMargins(0, 0, 0, 0);
    auto *scroll = new QScrollArea; scroll->setObjectName("keyframePropertiesScroll"); scroll->setWidgetResizable(true); scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff); outer->addWidget(scroll);
    auto *content = new QWidget; scroll->setWidget(content); auto *layout = new QVBoxLayout(content); layout->setContentsMargins(4, 4, 4, 4); layout->setSpacing(5);
    auto *header = new QHBoxLayout; header->setSpacing(2);
    m_context = new ElidedLabel; m_context->setObjectName("keyframeLayerName"); m_context->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed); m_context->setMinimumHeight(ui::metrics::propertyHeight); header->addWidget(m_context, 1);
    m_source = action(this, "keyframeSourceEditing", "paintbrush", "Edit source artwork at its original position", true);
    m_transform = action(this, "keyframeTransform", "move", "Edit animated transform on the canvas", true);
    header->addWidget(m_source); layout->addLayout(header);
    m_categories=new ChannelCategoryTabs; m_categories->setObjectName("keyframeCategories");m_categories->setProperty("compactDrawingTabs",true);
    m_categories->setIconSize({16,16});m_categories->setExpanding(true);m_categories->setDrawBase(false);m_categories->setUsesScrollButtons(true);m_categories->setFocusPolicy(Qt::StrongFocus);
    const QStringList categoryNames{"Transform","Effects","Mask"},categoryIcons{"move","sparkles","square-dashed"};
    for(int i=0;i<3;++i){const int tab=m_categories->addTab(appIcon(categoryIcons[i]),categoryNames[i]);m_categories->setTabToolTip(tab,categoryNames[i]+" animation channels");m_categories->setAccessibleTabName(tab,categoryNames[i]+" animation channels");}
    m_categories->setStyleSheet("QTabBar::tab { min-width: 0px; padding-left: 2px; padding-right: 2px; }");layout->addWidget(m_categories);
    connect(m_categories,&QTabBar::currentChanged,this,&KeyframePanel::setCategory);
    m_transformControls=new QWidget; m_transformControls->setObjectName("keyframeTransformControls");
    auto *modes = new QHBoxLayout(m_transformControls);modes->setContentsMargins(0,0,0,0); modes->setSpacing(2);
    m_motionPath = action(this, "keyframeMotionPath", "spline", "Edit the motion path and position keys on the canvas", true);
    const QString pivotHelp = "Edit the layer pivot. The pivot is shared across the whole animation. Moving it offsets position keys to preserve the current frame; other rotated or scaled poses may change.";
    m_pivot = action(this, "keyframePivot", "focus", pivotHelp, true);
    for (auto *button : {m_transform, m_motionPath, m_pivot}) modes->addWidget(button);
    modes->addStretch(); layout->addWidget(m_transformControls);
    m_motionControls = new QWidget; m_motionControls->setObjectName("keyframeMotionControls");
    auto *motionActions = new QHBoxLayout(m_motionControls); motionActions->setContentsMargins(0, 0, 0, 0); motionActions->setSpacing(3);
    m_motionCurve = action(this, "keyframeMotionCurve", "spline", "Curve the outgoing position segment. Position X and Y use shared timing. Drag tangent circles to shape motion. For a collapsed handle, Alt-drag its key (Alt+Shift for incoming).", true);
    m_orientAlongPath = action(this, "keyframeOrientAlongPath", "mouse-pointer-2", "Orient artwork along the motion path. Rotation becomes the angle offset.", true);
    for (auto *button : {m_motionCurve, m_orientAlongPath}) { button->setMinimumWidth(0); button->setMaximumWidth(QWIDGETSIZE_MAX); button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon); motionActions->addWidget(button, 1); }
    m_motionCurve->setText("Curve"); m_orientAlongPath->setText("Follow path"); layout->addWidget(m_motionControls);
    connect(m_motionCurve, &QToolButton::clicked, this, [this](bool curved) {
        if (!m_refreshing) { commitEditors(); m_controller->setMotionPathCurved(curved); refresh(); }
    });
    connect(m_orientAlongPath, &QToolButton::clicked, this, [this](bool orient) {
        if (!m_refreshing) { commitEditors(); m_controller->setOrientAlongPath(orient); refresh(); }
    });
    m_pivotControls = new QWidget; m_pivotControls->setObjectName("keyframePivotControls");
    auto *pivotLayout = new QVBoxLayout(m_pivotControls); pivotLayout->setContentsMargins(0, 0, 0, 0); pivotLayout->setSpacing(3);
    auto *pivotHeading = new QHBoxLayout; pivotHeading->addWidget(new QLabel("Pivot"), 1);
    m_pivotReset = action(this, "keyframePivotReset", "rotate-ccw", "Center the shared pivot on the source artwork, preserving this frame's appearance"); pivotHeading->addWidget(m_pivotReset); pivotLayout->addLayout(pivotHeading);
    m_pivotX = new ValueSlider("Pivot X in source pixels", -32768, 32768, 0, 2, "px"); m_pivotX->setObjectName("keyframePivotX");
    m_pivotY = new ValueSlider("Pivot Y in source pixels", -32768, 32768, 0, 2, "px"); m_pivotY->setObjectName("keyframePivotY");
    for (int axis = 0; axis < 2; ++axis) {
        auto *field = axis ? m_pivotY : m_pivotX; field->setSliderRange(-1000, 1000); field->setSingleStep(.5); field->setToolTip(pivotHelp);
        auto *row = new PropertyRow(axis ? "Y" : "X", field); row->setObjectName(axis ? "keyframePivotYRow" : "keyframePivotXRow"); pivotLayout->addWidget(row);
        connect(field, &ValueSlider::editingStarted, this, &KeyframePanel::beginPivot);
        connect(field, &ValueSlider::valueChanged, this, [this](double) {
            if (!m_refreshing && m_pivotEditing) m_controller->previewPivot({m_pivotX->value(), m_pivotY->value()});
        });
        connect(field, &ValueSlider::editingFinished, this, &KeyframePanel::finishValue);
        connect(field, &ValueSlider::editingCancelled, this, &KeyframePanel::cancelEditors);
    }
    layout->addWidget(m_pivotControls);
    connect(m_motionPath, &QToolButton::clicked, this, [this] { if (!m_refreshing) { m_controller->editMotionPath(); refresh(); } });
    connect(m_pivot, &QToolButton::clicked, this, [this] { if (!m_refreshing) { m_controller->editAnimationPivot(); refresh(); } });
    connect(m_pivotReset, &QToolButton::clicked, this, [this] { if (!m_refreshing) { commitEditors(); m_controller->resetAnimationPivot(); refresh(); } });
    connect(m_source, &QToolButton::toggled, this, [this](bool enabled) { if (!m_refreshing) { m_controller->setSourceEditing(enabled); refresh(); } });
    connect(m_transform, &QToolButton::clicked, this, [this] { if (!m_refreshing) { m_controller->editAnimatedTransform(); refresh(); } });
    const QStringList shortLabels{"Pos X", "Pos Y", "Scale X", "Scale Y", "Rotate", "Opacity"};
    for (int i = 0; i < keyframePropertyCount; ++i) {
        const auto property = static_cast<KeyframeProperty>(i); auto &row = m_channels[i]; const QString key = keyframePropertyKey(property),label=keyframePropertyName(property);
        auto *channelLabel = new QWidget; auto *heading = new QHBoxLayout(channelLabel); heading->setContentsMargins(0,0,0,0); heading->setSpacing(2);
        row.enable = action(this, "keyframeEnable_" + key, "diamond", "Enable " + label + " animation", true); heading->addWidget(row.enable);
        row.label = keyframePropertyIsDiscrete(property)?new CompactChannelLabel:new QToolButton; row.label->setObjectName("keyframeSelect_" + key); row.label->setText(i<6?shortLabels[i]:label);
        row.label->setCheckable(true);row.label->setToolButtonStyle(Qt::ToolButtonTextOnly);row.label->setSizePolicy(keyframePropertyIsDiscrete(property)?QSizePolicy::Expanding:QSizePolicy::Fixed,QSizePolicy::Fixed);row.label->setMinimumHeight(ui::metrics::actionTarget);
        row.label->setProperty("compactDrawingAction",true);row.label->setFocusPolicy(Qt::StrongFocus);
        row.label->setAccessibleName("Select "+label+" animation channel");row.label->setToolTip(row.label->accessibleName());heading->addWidget(row.label);
        QWidget *editor=nullptr;
        if(keyframePropertyIsDiscrete(property)) {
            row.toggle=action(this,"keyframeToggle_"+key,"eye",label+" at this frame · held until the next key",true);
            row.toggle->setToolButtonStyle(Qt::ToolButtonTextOnly);row.toggle->setFixedWidth(48);row.toggle->setMaximumHeight(QWIDGETSIZE_MAX);row.toggle->setSizePolicy(QSizePolicy::Fixed,QSizePolicy::Fixed);editor=row.toggle;
            // The shared icon-button rule reapplies its 26px content minimum
            // during font/style polish. Keep this text control's 48px target.
            row.toggle->setStyleSheet(QString("QToolButton { padding: 0; min-width: %1px; max-width: %1px; }").arg(ui::metrics::numberWidth-2));
            connect(row.toggle,&QToolButton::clicked,this,[this,property](bool value){if(m_refreshing)return;beginValue(property);if(m_valueEditing)m_controller->previewPropertyValue(property,value?1:0);finishValue();});
        } else {
            const auto minimum=displayValue(property,keyframePropertyMinimum(property)),maximum=displayValue(property,keyframePropertyMaximum(property));
            row.value=new ValueSlider(label,minimum,maximum,displayValue(property,keyframeDefaultValue(property)),property==KeyframeProperty::Opacity?0:2,propertyUnit(property));
            row.value->setObjectName("keyframeValue_"+key);row.value->setSingleStep(keyframePropertyIsPercent(property)?1:property==KeyframeProperty::Exposure?.1:.5);
            if(property==KeyframeProperty::PositionX||property==KeyframeProperty::PositionY||property==KeyframeProperty::MaskPositionX||property==KeyframeProperty::MaskPositionY)row.value->setSliderRange(-1000,1000);
            if(property==KeyframeProperty::ScaleX||property==KeyframeProperty::ScaleY||property==KeyframeProperty::MaskScaleX||property==KeyframeProperty::MaskScaleY)row.value->setSliderRange(std::max(-200.,minimum),200);
            if(property==KeyframeProperty::Rotation||property==KeyframeProperty::MaskRotation)row.value->setSliderRange(-180,180);
            if(property==KeyframeProperty::ShadowOffsetX||property==KeyframeProperty::ShadowOffsetY)row.value->setSliderRange(-128,128);
            editor=row.value;
            connect(row.value,&ValueSlider::editingStarted,this,[this,property]{beginValue(property);});
            connect(row.value,&ValueSlider::valueChanged,this,[this,property](double value){if(!m_refreshing&&m_valueEditing)m_controller->previewPropertyValue(property,storedValue(property,value));});
            connect(row.value,&ValueSlider::editingFinished,this,&KeyframePanel::finishValue);
            connect(row.value,&ValueSlider::editingCancelled,this,&KeyframePanel::cancelEditors);
        }
        if(row.toggle){
            // A switch needs only its 48px control, not a numeric track's reserved width.
            row.container=new QWidget;row.container->setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Fixed);
            auto *switchRow=new QHBoxLayout(row.container);switchRow->setContentsMargins(0,0,0,0);switchRow->setSpacing(ui::metrics::controlGap);
            switchRow->addWidget(channelLabel,1);switchRow->addWidget(row.toggle);
        }else{
            auto *propertyRow=new PropertyRow({},editor);propertyRow->setLabelWidth(0);propertyRow->setAccessory(channelLabel);row.container=propertyRow;
        }
        row.container->setObjectName("keyframeRow_"+key);row.container->setAccessibleName(label);layout->addWidget(row.container);
        connect(row.enable,&QToolButton::toggled,this,[this,property](bool enabled){if(m_refreshing)return;selectProperty(property);m_controller->setPropertyEnabled(property,enabled);refresh();});
        connect(row.label,&QToolButton::clicked,this,[this,property]{selectProperty(property);});
    }
    m_pathControls=new QWidget; m_pathControls->setObjectName("keyframePathControls");auto *path=new QHBoxLayout(m_pathControls);path->setContentsMargins(0,0,0,0);path->setSpacing(3);
    m_pathEnable = action(this, "keyframeEnablePath", "diamond", "Enable path morph animation for the selected vector path", true); path->addWidget(m_pathEnable);
    m_pathLabel = new QToolButton; m_pathLabel->setObjectName("keyframeSelectPath"); m_pathLabel->setText("Path morph"); m_pathLabel->setCheckable(true);
    m_pathLabel->setToolButtonStyle(Qt::ToolButtonTextOnly); m_pathLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed); m_pathLabel->setMinimumHeight(28);
    m_pathLabel->setToolTip("Select a vector path with Path Edit, enable morphing, then edit its nodes at another frame"); m_pathLabel->setAccessibleName("Select path morph channel"); path->addWidget(m_pathLabel, 1); layout->addWidget(m_pathControls);
    connect(m_pathEnable, &QToolButton::toggled, this, [this](bool enabled) {
        if (m_refreshing) return; selectPath(); m_controller->setPathEnabled(m_objectId, enabled); refresh();
    });
    connect(m_pathLabel, &QToolButton::clicked, this, &KeyframePanel::selectPath);
    m_keyContext = new ElidedLabel; m_keyContext->setObjectName("keyframeSelectedChannel"); m_keyContext->setMinimumHeight(22); m_keyContext->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed); layout->addWidget(m_keyContext);
    auto *keys = new QHBoxLayout; keys->setSpacing(3);
    m_previous = action(this, "keyframePrevious", "skip-back", "Previous keyframe"); m_add = action(this, "keyframeAdd", "diamond", "Add a keyframe at the current value");
    m_remove = action(this, "keyframeRemove", "trash", "Remove this keyframe"); m_next = action(this, "keyframeNext", "skip-forward", "Next keyframe");
    for (auto *button : {m_previous, m_add, m_remove, m_next}) keys->addWidget(button); keys->addStretch(); layout->addLayout(keys);
    connect(m_previous, &QToolButton::clicked, this, [this] { stepKey(-1); }); connect(m_next, &QToolButton::clicked, this, [this] { stepKey(1); });
    connect(m_add, &QToolButton::clicked, this, &KeyframePanel::addKey); connect(m_remove, &QToolButton::clicked, this, &KeyframePanel::removeKey);
    m_keyTime = new ValueSlider("Keyframe time", 1, 1, 1); m_keyTime->setObjectName("keyframeTime"); layout->addWidget(new PropertyRow("Key frame",m_keyTime));
    connect(m_keyTime, &ValueSlider::editingStarted, this, [this] { if (!m_refreshing) m_controller->setPendingEdits(this, true); });
    connect(m_keyTime, &ValueSlider::editingFinished, this, &KeyframePanel::moveKey); connect(m_keyTime, &ValueSlider::editingCancelled, this, &KeyframePanel::cancelEditors);
    m_interpolation = new QTabBar; m_interpolation->setObjectName("keyframeInterpolation"); m_interpolation->setDrawBase(false); m_interpolation->setExpanding(true); m_interpolation->setUsesScrollButtons(true);
    m_interpolation->setProperty("compactDrawingTabs",true); m_interpolation->setFocusPolicy(Qt::StrongFocus); m_interpolation->setAccessibleName("Keyframe interpolation");
    for (int i = 0; i < 5; ++i) { const auto mode = static_cast<KeyframeInterpolation>(i); const QString name = keyframeInterpolationName(mode);
        const int tab = m_interpolation->addTab(QStringList{"Hold", "Line", "In", "Out", "In/Out"}[i]); m_interpolation->setTabToolTip(tab, name + " interpolation to the next keyframe"); m_interpolation->setAccessibleTabName(tab, name + " interpolation"); }
    m_interpolation->setStyleSheet("QTabBar::tab { min-width: 0px; padding-left: 3px; padding-right: 3px; }");
    const int customTab = m_interpolation->addTab("Curve");
    m_interpolation->setTabToolTip(customTab, "Custom easing with editable tangent handles"); m_interpolation->setAccessibleTabName(customTab, "Custom easing curve");
    layout->addWidget(m_interpolation);
    connect(m_interpolation, &QTabBar::currentChanged, this, [this](int index) {
        if (m_refreshing || index < 0) return;
        if (index == 5) { beginEasing(); if (m_easingEditing) m_controller->previewEasing({m_easingGraph->firstControl(), m_easingGraph->secondControl()}); finishValue(); return; }
        const auto mode = static_cast<KeyframeInterpolation>(index);
        if (m_pathSelected) m_controller->setPathInterpolation(m_objectId, m_controller->currentFrame(), mode);
        else m_controller->setPropertyInterpolation(m_property, m_controller->currentFrame(), mode); refresh();
    });
    auto *easingHeading = new QHBoxLayout; easingHeading->setSpacing(3);
    m_easingContext = new ElidedLabel; m_easingContext->setObjectName("keyframeEasingContext"); m_easingContext->setMinimumHeight(ui::metrics::propertyHeight); m_easingContext->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed); easingHeading->addWidget(m_easingContext, 1);
    m_easingReset = action(this, "keyframeEasingReset", "rotate-ccw", "Reset custom easing to the selected timing preset"); easingHeading->addWidget(m_easingReset); layout->addLayout(easingHeading);
    m_easingGraph = new EasingGraphWidget; layout->addWidget(m_easingGraph);
    connect(m_easingGraph, &EasingGraphWidget::editingStarted, this, &KeyframePanel::beginEasing);
    connect(m_easingGraph, &EasingGraphWidget::curveChanged, this, [this](QPointF first, QPointF second) {
        if (!m_refreshing && m_easingEditing) m_controller->previewEasing({first, second});
    });
    connect(m_easingGraph, &EasingGraphWidget::editingFinished, this, &KeyframePanel::finishValue);
    connect(m_easingGraph, &EasingGraphWidget::editingCancelled, this, &KeyframePanel::cancelEditors);
    connect(m_easingReset, &QToolButton::clicked, this, [this] { if (!m_refreshing) { commitEditors(); m_controller->resetEasing(m_property, m_pathSelected ? m_objectId : QString()); refresh(); } });
    m_status = new ElidedLabel; m_status->setObjectName("keyframeStatus"); m_status->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed); m_status->setMinimumHeight(24); layout->addWidget(m_status); layout->addStretch();
    connect(controller, &TimelineController::changed, this, &KeyframePanel::refresh);
    connect(controller->canvas()->keyframeTransformTool(), &KeyframeTransformTool::changed, this, &KeyframePanel::refresh);
    connect(controller, &TimelineController::editResolutionRequested, this, &KeyframePanel::commitEditors);
    connect(controller, &TimelineController::editCancellationRequested, this, &KeyframePanel::cancelEditors);
    connect(controller->canvas()->drawingTools(), &DrawingToolController::changed, this, &KeyframePanel::refresh);
    connect(controller, &TimelineController::propertyChannelRequested, this, &KeyframePanel::selectProperty);
    connect(controller, &TimelineController::pathChannelRequested, this, [this](const QString &id) {
        commitEditors(); if (m_controller->canvas()->drawingTools()->selectAnimatedPath(id)) selectPath();
    });
    refresh();
}

void KeyframePanel::refresh() {
    if (m_refreshing) return; const QScopedValueRollback<bool> guard(m_refreshing, true);
    const auto *current = shot(m_controller); const QString selected = m_controller->selectedPropertyLayerId();
    const bool changedLayer = selected != m_layerId;
    if (changedLayer && (m_valueEditing || m_pivotEditing || m_easingEditing)) { m_controller->cancelPropertyEdit(); m_valueEditing = m_pivotEditing = m_easingEditing = false; }
    if (changedLayer) { for (auto &row : m_channels) if(row.value)row.value->cancelEditing(); m_pivotX->cancelEditing(); m_pivotY->cancelEditing(); m_keyTime->cancelEditing(); m_easingGraph->cancelEditing(); m_controller->setPendingEdits(this, false); }
    m_layerId = selected; const auto *target = layer(current, selected);
    const auto path = m_controller->canvas()->drawingTools()->selectedObject();
    m_objectId = path && path->kind == ObjectKind::Path ? path->id : QString();
    const QString name = target ? target->name : "Select a layer"; m_context->setText(name); m_context->setToolTip(name); m_context->setAccessibleName(name);
    QString reason = current && target ? (m_pathSelected?propertyAnimationUnavailableReason(*current, selected):keyframePropertyUnavailableReason(*current,selected,m_property)) : "Select a layer";
    if (reason.isEmpty() && locked(*current, selected)) reason = "Layer is locked";
    if (reason.isEmpty() && m_controller->playing()) reason = "Playback";
    const bool editable = current && target && !locked(*current,selected) && !m_controller->playing(); const int frame = m_controller->currentFrame();
    m_source->setEnabled(current && !m_controller->playing()); m_source->setChecked(m_controller->sourceEditing());
    const bool motionEditable = editable && propertyAnimationUnavailableReason(*current,selected).isEmpty() && !locked(*current, selected, true);
    m_transformControls->setVisible(m_category==0);m_pathControls->setVisible(m_category==0);
    m_transform->setEnabled(motionEditable); m_transform->setChecked(m_controller->animatedTransformEditing());
    m_motionPath->setEnabled(motionEditable); m_motionPath->setChecked(m_controller->motionPathEditing());
    m_pivot->setEnabled(motionEditable); m_pivot->setChecked(m_controller->pivotEditing());
    const auto *animation = current ? layerAnimation(*current, selected) : nullptr;
    const bool following = animation && animation->orientAlongPath;
    m_motionControls->setVisible(m_category==0&&m_controller->motionPathEditing());
    const int motionFrame = m_controller->canvas()->keyframeTransformTool()->selectedMotionFrame();
    bool curve = false, motionSegment = false;
    const auto *position = current ? propertyChannel(*current, selected, KeyframeProperty::PositionX) : nullptr;
    if (position) for (int i = 0; i < position->keys.size(); ++i) if (position->keys[i].frame == motionFrame) {
        curve = position->keys[i].spatial && position->keys[i].spatial->curved; motionSegment = i + 1 < position->keys.size();
    }
    const auto motionFrames = current ? motionPositionKeyFrames(*current, selected) : QVector<int>{};
    if (!motionSegment) motionSegment = motionFrames.contains(motionFrame) && !motionFrames.isEmpty() && motionFrame < motionFrames.last();
    m_motionCurve->setChecked(curve); m_motionCurve->setEnabled(motionEditable && motionSegment);
    m_orientAlongPath->setChecked(following); m_orientAlongPath->setEnabled(motionEditable && (following || motionFrames.size() >= 2));
    auto *rotationLabel=m_channels[static_cast<int>(KeyframeProperty::Rotation)].label;
    rotationLabel->setText(following ? "Offset" : "Rotate");
    rotationLabel->setToolTip(following ? "Select rotation offset from the motion path direction" : "Select Rotation animation channel");
    rotationLabel->setAccessibleName(rotationLabel->toolTip());
    m_pivotControls->setVisible(m_category==0&&m_controller->pivotEditing());
    m_pivotControls->setEnabled(motionEditable && !m_controller->sourceEditing());
    if (!m_pivotEditing) { const auto pivot = m_controller->animationPivot(); m_pivotX->setValueSilently(pivot.x()); m_pivotY->setValueSilently(pivot.y()); }
    for (int i = 0; i < keyframePropertyCount; ++i) {
        const auto property = static_cast<KeyframeProperty>(i); auto &row = m_channels[i];
        row.container->setVisible(propertyCategory(property)==m_category);
        const auto *channel = current ? propertyChannel(*current, selected, property) : nullptr; const bool enabled = channel && !channel->keys.isEmpty();
        QString channelReason=current&&target?keyframePropertyUnavailableReason(*current,selected,property):QString("Select a layer");
        if(channelReason.isEmpty()&&locked(*current,selected))channelReason="Layer is locked";
        if(channelReason.isEmpty()&&m_controller->playing())channelReason="Stop playback to edit animation";
        if(channelReason.isEmpty()&&keyframePropertyNeedsPositionUnlock(property)&&locked(*current,selected,true))channelReason="Layer position is locked";
        const bool canEdit = editable && channelReason.isEmpty();
        row.enable->setChecked(enabled); row.enable->setEnabled(canEdit);
        const QString help = (enabled ? "Disable " : "Enable ") + keyframePropertyName(property) + " animation" + (enabled ? " (removes its keys; Undo restores them)" : QString()) + (channelReason.isEmpty()?QString():" · "+channelReason);
        row.enable->setToolTip(help); row.enable->setAccessibleName(help);
        row.label->setChecked(!m_pathSelected && property == m_property); row.label->setEnabled(target);
        const qreal value=current?evaluateProperty(*current,selected,property,frame):keyframeDefaultValue(property);
        if(row.value){row.value->setEnabled(canEdit&&enabled);if(!m_valueEditing||property!=m_property)row.value->setValueSilently(displayValue(property,value));}
        if(row.toggle){row.toggle->setEnabled(canEdit&&enabled);row.toggle->setChecked(value>=.5);row.toggle->setText(value>=.5?"On":"Off");}
    }
    const auto *pathTrack = current ? pathAnimation(*current, selected, m_objectId) : nullptr;
    m_pathEnable->setChecked(pathTrack && !pathTrack->keys.isEmpty()); m_pathEnable->setEnabled(editable && path.has_value() && !m_objectId.isEmpty() && !locked(*current, selected, true));
    m_pathEnable->setToolTip(pathTrack ? "Disable path morph animation (removes its keys; Undo restores them)" : "Enable path morph animation for the selected vector path"); m_pathEnable->setAccessibleName(m_pathEnable->toolTip());
    m_pathLabel->setEnabled(!m_objectId.isEmpty()); m_pathLabel->setChecked(m_pathSelected);
    const auto frames = keyFrames(); const bool exact = frames.contains(frame), enabled = !frames.isEmpty();
    const bool discrete=!m_pathSelected&&keyframePropertyIsDiscrete(m_property);
    const bool keyEditable = editable && reason.isEmpty() && ((!m_pathSelected&&!keyframePropertyNeedsPositionUnlock(m_property)) || !locked(*current, selected, true));
    m_previous->setEnabled(!m_controller->playing() && std::any_of(frames.cbegin(), frames.cend(), [frame](int f) { return f < frame; }));
    m_next->setEnabled(!m_controller->playing() && std::any_of(frames.cbegin(), frames.cend(), [frame](int f) { return f > frame; }));
    m_add->setEnabled(keyEditable && enabled && !exact && !(m_pathSelected && m_controller->sourceEditing())); m_remove->setEnabled(keyEditable && exact);
    m_keyContext->setText((m_pathSelected ? "Path morph" : keyframePropertyName(m_property)) + (exact ? " · key" : ""));
    m_keyContext->setToolTip(m_keyContext->text()); m_keyContext->setAccessibleName(m_keyContext->text());
    const int count = std::max(1, m_controller->frameCount());
    if (m_keyTime->property("frameCount").toInt() != count) { m_keyTime->setProperty("frameCount", count); m_keyTime->setRange(1, count); }
    if (changedLayer || frame != m_displayedFrame) { m_keyTime->setValueSilently(frame + 1); m_displayedFrame = frame; }
    m_keyTime->setEnabled(keyEditable && exact); m_interpolation->setEnabled(keyEditable && exact && !discrete);
    KeyframeInterpolation interpolation = KeyframeInterpolation::Linear;
    std::optional<CubicEasing> customEasing; int nextFrame = -1; qreal firstValue = 0, lastValue = 1;
    if (exact && current) {
        if (m_pathSelected && pathTrack) for (int i = 0; i < pathTrack->keys.size(); ++i) if (pathTrack->keys[i].frame == frame) {
            const auto &key = pathTrack->keys[i]; interpolation = key.interpolation; customEasing = key.easing;
            if (i + 1 < pathTrack->keys.size()) nextFrame = pathTrack->keys[i + 1].frame;
        }
        if (!m_pathSelected) if (const auto *channel = propertyChannel(*current, selected, m_property)) for (int i = 0; i < channel->keys.size(); ++i) if (channel->keys[i].frame == frame) {
            const auto &key = channel->keys[i]; interpolation = key.interpolation; customEasing = key.easing; firstValue = displayValue(m_property, key.value);
            if (i + 1 < channel->keys.size()) { nextFrame = channel->keys[i + 1].frame; lastValue = displayValue(m_property, channel->keys[i + 1].value); }
        }
    }
    m_interpolation->setCurrentIndex(customEasing ? 5 : static_cast<int>(interpolation));
    m_interpolation->setTabEnabled(5, keyEditable && exact && nextFrame >= 0 && !discrete);
    const bool graphEditable = keyEditable && exact && nextFrame >= 0 && !discrete && !(m_pathSelected && m_controller->sourceEditing());
    m_easingGraph->setEnabled(graphEditable); m_easingReset->setEnabled(keyEditable && !discrete && customEasing.has_value());
    if (!m_easingEditing) {
        const auto curve = customEasing.value_or(easingForInterpolation(interpolation)); m_easingGraph->setCurve(curve.first, curve.second);
        m_easingGraph->setHold(!customEasing && interpolation == KeyframeInterpolation::Hold);
        const QString unit=m_pathSelected?"progress":propertyUnit(m_property);
        m_easingGraph->setSegment(frame, std::max(frame, nextFrame), firstValue, lastValue, unit);
    }
    const QString easingContext = discrete ? "On/off values hold until the next key" : !exact ? "Select a key for easing" : nextFrame < 0 ? "No following key" : QString("Easing · %1 → %2").arg(frame + 1).arg(nextFrame + 1);
    m_easingContext->setText(easingContext); m_easingContext->setToolTip(easingContext); m_easingContext->setAccessibleName(easingContext);
    if (reason.isEmpty() && m_pathSelected && m_objectId.isEmpty()) reason = "Select a path with Path Edit";
    if (reason.isEmpty() && !keyEditable && current && target && locked(*current,selected,true)) reason = "Layer position is locked";
    if (reason.isEmpty() && m_pathSelected && m_controller->sourceEditing()) reason = "Leave Source to edit path poses";
    m_status->setText(reason); m_status->setToolTip(reason); m_status->setAccessibleName(reason);
}
void KeyframePanel::selectProperty(KeyframeProperty property) {
    if (m_property != property || m_pathSelected) commitEditors(); m_property = property; m_pathSelected = false;
    m_category=propertyCategory(property);{const QSignalBlocker block(m_categories);m_categories->setCurrentIndex(m_category);}refresh();
}
void KeyframePanel::selectPath() { if (!m_pathSelected) commitEditors(); m_pathSelected = true; m_category=0;{const QSignalBlocker block(m_categories);m_categories->setCurrentIndex(0);}refresh(); }
void KeyframePanel::setCategory(int category) {
    if(m_refreshing||category<0||category>2||category==m_category)return;
    commitEditors();m_category=category;m_pathSelected=false;
    if(propertyCategory(m_property)!=category)m_property=category==0?KeyframeProperty::PositionX:category==1?KeyframeProperty::BlurRadius:KeyframeProperty::MaskPositionX;
    refresh();
}
void KeyframePanel::beginValue(KeyframeProperty property) {
    if (m_refreshing || m_resolving) return;
    m_property = property; m_pathSelected = false;
    { const QScopedValueRollback<bool> guard(m_resolving, true); m_valueEditing = m_controller->beginPropertyEdit(property); }
    m_controller->setPendingEdits(this, m_valueEditing); refresh();
}
void KeyframePanel::beginPivot() {
    if (m_refreshing || m_resolving) return;
    { const QScopedValueRollback<bool> guard(m_resolving, true); m_pivotEditing = m_controller->beginPivotEdit(); }
    m_controller->setPendingEdits(this, m_pivotEditing); refresh();
}
void KeyframePanel::beginEasing() {
    if (m_refreshing || m_resolving) return;
    { const QScopedValueRollback<bool> guard(m_resolving, true); m_easingEditing = m_controller->beginEasingEdit(m_property, m_pathSelected ? m_objectId : QString()); }
    m_controller->setPendingEdits(this, m_easingEditing);
    if (!m_easingEditing) m_easingGraph->cancelEditing(); refresh();
}
void KeyframePanel::finishValue() {
    if (m_refreshing) return;
    if (m_valueEditing || m_pivotEditing || m_easingEditing) { m_valueEditing = m_pivotEditing = m_easingEditing = false; m_controller->finishPropertyEdit(); }
    m_controller->setPendingEdits(this, false); refresh();
}
void KeyframePanel::commitEditors() {
    if (m_resolving) return; const QScopedValueRollback<bool> guard(m_resolving, true);
    for (auto &row : m_channels) if(row.value)row.value->commitEditing(); m_pivotX->commitEditing(); m_pivotY->commitEditing(); m_keyTime->commitEditing(); m_easingGraph->finishEditing(); finishValue();
}
void KeyframePanel::cancelEditors() {
    if (m_resolving || m_refreshing) return; const QScopedValueRollback<bool> guard(m_resolving, true);
    { const QScopedValueRollback<bool> update(m_refreshing, true); for (auto &row : m_channels) if(row.value)row.value->cancelEditing(); m_pivotX->cancelEditing(); m_pivotY->cancelEditing(); m_keyTime->cancelEditing(); m_easingGraph->cancelEditing(); }
    if (m_valueEditing || m_pivotEditing || m_easingEditing) m_controller->cancelPropertyEdit(); m_valueEditing = m_pivotEditing = m_easingEditing = false; m_displayedFrame = -1;
    m_controller->setPendingEdits(this, false); refresh();
}
QVector<int> KeyframePanel::keyFrames() const {
    QVector<int> frames; const auto *current = shot(m_controller); if (!current) return frames;
    if (m_pathSelected) { if (const auto *path = pathAnimation(*current, m_layerId, m_objectId)) for (const auto &key : path->keys) frames.append(key.frame); }
    else if (const auto *channel = propertyChannel(*current, m_layerId, m_property)) for (const auto &key : channel->keys) frames.append(key.frame);
    return frames;
}
void KeyframePanel::stepKey(int direction) {
    const auto frames = keyFrames(); const int current = m_controller->currentFrame(); int next = current;
    for (int frame : frames) if ((direction < 0 && frame < current && (next == current || frame > next)) || (direction > 0 && frame > current && (next == current || frame < next))) next = frame;
    m_controller->setFrame(next);
}
void KeyframePanel::addKey() {
    if (m_pathSelected) m_controller->setPathKey(m_objectId); else m_controller->setPropertyKey(m_property); refresh();
}
void KeyframePanel::removeKey() {
    if (m_pathSelected) m_controller->removePathKey(m_objectId, m_controller->currentFrame()); else m_controller->removePropertyKey(m_property, m_controller->currentFrame()); refresh();
}
void KeyframePanel::moveKey() {
    if (m_refreshing) return; const int from = m_controller->currentFrame(), to = qRound(m_keyTime->value()) - 1;
    if (from != to) {
        const QScopedValueRollback<bool> guard(m_resolving, true);
        const bool moved = m_pathSelected ? m_controller->movePathKey(m_objectId, from, to) : m_controller->movePropertyKey(m_property, from, to);
        if (moved) m_controller->setFrame(to);
    }
    m_displayedFrame = -1; m_controller->setPendingEdits(this, false); refresh();
}
} // namespace toon
