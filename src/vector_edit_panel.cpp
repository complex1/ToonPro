#include "vector_edit_panel.h"
#include "canvas.h"
#include "drawing_tool_controller.h"
#include "icons.h"
#include "ui_metrics.h"
#include "ui_theme.h"
#include "value_slider.h"
#include "raster.h"
#include "property_animation.h"
#include "property_row.h"
#include <QColorDialog>
#include <QEvent>
#include <QLabel>
#include <QPainter>
#include <QPixmap>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QTabBar>
#include <QToolButton>
#include <QVariant>
#include <QVBoxLayout>

namespace toon {
namespace {
class PathStatus final : public QLabel {
public:
    PathStatus() { setTextFormat(Qt::PlainText); setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Fixed); updateHeight(); }
protected:
    void changeEvent(QEvent *event) override {
        QLabel::changeEvent(event);
        if(event->type()==QEvent::FontChange)updateHeight();
    }
    void paintEvent(QPaintEvent *) override {
        QPainter painter(this);painter.setPen(QColor(ui::muted));
        painter.drawText(rect(),Qt::AlignLeft|Qt::AlignVCenter,fontMetrics().elidedText(text(),Qt::ElideRight,width()));
    }
private:
    void updateHeight(){setFixedHeight(qMax(20,fontMetrics().height()+2));}
};
class PathModeTabs final : public QTabBar {
public:
    using QTabBar::QTabBar;
    QSize tabSizeHint(int) const override {return {ui::metrics::actionTarget,qMax(ui::metrics::tabHeight,fontMetrics().height()+6)};}
    QSize minimumTabSizeHint(int index) const override {return tabSizeHint(index);}
};
class VectorPropertyScroll final : public QScrollArea {
public:
    using QScrollArea::QScrollArea;
    QSize minimumSizeHint() const override {return {0,0};}
};
QToolButton *action(QBoxLayout *layout, const QString &icon, const QString &name, const char *id) {
    auto *button = new QToolButton; button->setProperty("compactDrawingAction", true);
    button->setIcon(appIcon(icon)); button->setIconSize({ui::metrics::actionIcon,ui::metrics::actionIcon});
    button->setFixedSize(ui::metrics::actionTarget,ui::metrics::actionTarget);
    button->setToolTip(name); button->setAccessibleName(name); button->setObjectName(id);
    layout->addWidget(button); return button;
}
QTabBar *modes(QVBoxLayout *layout, const QList<QPair<QString,QString>> &items, const char *id) {
    auto *tabs = new PathModeTabs; tabs->setObjectName(id); tabs->setExpanding(true); tabs->setDrawBase(false);
    tabs->setFocusPolicy(Qt::StrongFocus);
    tabs->setIconSize({ui::metrics::tabIcon,ui::metrics::tabIcon}); tabs->setProperty("compactDrawingTabs", true);
    tabs->setAccessibleName(QString::fromLatin1(id) == "pathEditMode" ? "Path editing mode" : "Width brush direction");
    for (const auto &item : items) {
        const int n = tabs->addTab(appIcon(item.first), {});
        tabs->setTabToolTip(n,item.second); tabs->setAccessibleTabName(n,item.second);
    }
    layout->addWidget(tabs); return tabs;
}
ValueSlider *number(QVBoxLayout *layout,const QString &name,const char *id,qreal low,qreal high,qreal initial,int decimals=0,const QString &unit={}) {
    auto *value = new ValueSlider(name,low,high,initial,decimals,unit); value->setObjectName(id);
    auto *row = new PropertyRow(name, value);
    row->setObjectName(QString::fromLatin1(id) + "Row"); row->setLabelWidth(ui::metrics::labelWidth);
    layout->addWidget(row); return value;
}
}
VectorEditPanel::VectorEditPanel(Canvas *canvas,QWidget *parent):QWidget(parent) {
    setObjectName("vectorEditProperties"); setProperty("compactDrawingPanel", true); setMinimumWidth(220);
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(ui::metrics::panelMargin,ui::metrics::panelMargin,ui::metrics::panelMargin,ui::metrics::panelMargin);outer->setSpacing(ui::metrics::controlGap);
    auto *content = new QWidget; auto *layout = new QVBoxLayout(content);
    layout->setContentsMargins(0,0,0,0);layout->setSpacing(ui::metrics::controlGap);
    auto *controller = canvas->drawingTools(); auto *doc = canvas->document();
    auto *mode = modes(outer,{{"move","Transform path: drag to move; corner handles resize; top handle rotates"},
        {"pen-tool","Edit nodes and curve handles"},{"radius","Edit width along the path"}},"pathEditMode");
    auto *direction = modes(outer,{{"plus","Thicken"},{"minus","Thin"}},"widthBrushDirection");
    auto *status = new PathStatus; status->setObjectName("pathSelectionStatus");outer->addWidget(status);
    auto *scroll = new VectorPropertyScroll(this);scroll->setObjectName("vectorEditScroll");scroll->setAccessibleName("Vector properties");
    scroll->setWidgetResizable(true);scroll->setFrameShape(QFrame::NoFrame);scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setMinimumSize(0,0);scroll->setWidget(content);outer->addWidget(scroll,1);
    auto *style = new QWidget; auto *styleLayout = new QVBoxLayout(style); styleLayout->setContentsMargins(0,0,0,0); styleLayout->setSpacing(ui::metrics::controlGap);
    layout->addWidget(style);
    auto *colorRow = new QHBoxLayout; colorRow->setSpacing(ui::metrics::controlGap); styleLayout->addLayout(colorRow);
    colorRow->addWidget(new QLabel("Color")); colorRow->addStretch();
    auto *color = action(colorRow,"palette","Stroke color","pathStrokeColor");
    auto *foreground = action(colorRow,"pipette","Use foreground color","pathUseForeground");
    auto *width = number(styleLayout,"Width","pathStrokeWidth",.1,2048,3,1,"px");
    width->setAccessibleName("Stroke width"); width->setToolTip("Stroke width in artwork pixels");
    width->setSliderRange(.1,256);
    auto *opacity = number(styleLayout,"Opacity","pathStrokeOpacity",0,100,100,0,"%");
    auto *commands = new QWidget(this);commands->setObjectName("vectorEditCommands");
    auto *commandRow = new QHBoxLayout(commands);commandRow->setContentsMargins(0,0,0,0);commandRow->setSpacing(ui::metrics::controlGap);
    auto *reset = action(commandRow,"rotate-ccw","Reset width along the selected path","pathResetWidths");
    outer->addWidget(commands);
    auto *simplify = new QWidget; auto *simplifyLayout = new QVBoxLayout(simplify); simplifyLayout->setContentsMargins(0,0,0,0); simplifyLayout->setSpacing(ui::metrics::controlGap);
    layout->addWidget(simplify);
    auto *tolerance = number(simplifyLayout,"Simplify","pathSimplifyTolerance",.1,30,2,1,"px");
    tolerance->setToolTip("Simplification tolerance in artwork pixels; lower values retain more detail");
    auto *selected = action(commandRow,featureIconName(FeatureIcon::Simplify),"Simplify selected path","pathSimplifySelected");
    auto *layer = action(commandRow,"layers","Simplify all paths on this vector layer","pathSimplifyLayer");
    commandRow->addStretch();
    auto *brush = new QWidget; auto *brushLayout = new QVBoxLayout(brush); brushLayout->setContentsMargins(0,0,0,0); brushLayout->setSpacing(ui::metrics::controlGap);
    layout->addWidget(brush);
    auto *radius = number(brushLayout,"Radius","vectorEditBrushRadius",1,256,30,0,"px");
    auto *strength = number(brushLayout,"Strength","widthBrushStrength",1,100,20,0,"%");
    auto*nudge=new QWidget;auto*nudgeLayout=new QVBoxLayout(nudge);nudgeLayout->setContentsMargins(0,0,0,0);nudgeLayout->setSpacing(ui::metrics::controlGap);layout->addWidget(nudge);
    auto*nudgeRadius=number(nudgeLayout,"Radius","vectorNudgeRadius",1,256,40,0,"px");
    nudgeRadius->setToolTip("Local push radius in artwork pixels. Drag from the area to push; falloff is smooth at its edge.");
    auto*nudgeStrength=number(nudgeLayout,"Strength","vectorNudgeStrength",1,100,75,0,"%");
    nudgeStrength->setToolTip("Percentage of the drag displacement applied at the brush center; pen pressure also controls the push.");
    layout->addStretch();
    connect(mode,&QTabBar::currentChanged,this,[=](int index){
        controller->setPathEditMode(static_cast<DrawingToolController::PathEditMode>(index));
        canvas->notifyToolSettingsEdited();
    });
    auto bindStyle = [=,this](ValueSlider *slider, bool isWidth) {
        connect(slider,&ValueSlider::editingStarted,controller,&DrawingToolController::beginStyleEdit);
        connect(slider,&ValueSlider::valueChanged,this,[=](qreal value){
            if(isWidth) controller->strokeWidth=value; else controller->objectOpacity=value/100;
            controller->previewObjectStyle();
        });
        connect(slider,&ValueSlider::editingFinished,controller,&DrawingToolController::finishStyleEdit);
        connect(slider,&ValueSlider::editingCancelled,controller,&DrawingToolController::cancelStyleEdit);
    };
    bindStyle(width,true); bindStyle(opacity,false);
    const auto setColor=[=](const QColor &chosen){
        auto object=controller->selectedObject(); if(!object||!chosen.isValid())return;
        object->stroke=chosen;
        ObjectTools::updateObject(doc,*object,"Change stroke color");
        controller->notifySettings();
    };
    connect(color,&QToolButton::clicked,this,[=,this]{
        const auto object=controller->selectedObject(); if(!object)return;
        const auto chosen=QColorDialog::getColor(object->stroke,this,"Stroke color",QColorDialog::ShowAlphaChannel);
        if(chosen.isValid()){setColor(chosen);canvas->setBrushColor(chosen);}
    });
    connect(foreground,&QToolButton::clicked,this,[=]{setColor(canvas->brush.color);});
    connect(reset,&QToolButton::clicked,controller,&DrawingToolController::resetWidths);
    connect(tolerance,&ValueSlider::valueChanged,this,[=](qreal value){controller->simplifyTolerance=value;canvas->notifyToolSettingsEdited();});
    connect(selected,&QToolButton::clicked,controller,&DrawingToolController::simplifySelected);
    connect(layer,&QToolButton::clicked,controller,&DrawingToolController::simplifyLayer);
    connect(direction,&QTabBar::currentChanged,this,[=](int index){controller->widthThicken=index==0;canvas->notifyToolSettingsEdited();});
    connect(radius,&ValueSlider::valueChanged,this,[=](qreal value){controller->widthBrushRadius=value;controller->notifySettings();canvas->notifyToolSettingsEdited();});
    connect(strength,&ValueSlider::valueChanged,this,[=](qreal value){controller->widthBrushStrength=value/100;canvas->notifyToolSettingsEdited();});
    connect(nudgeRadius,&ValueSlider::valueChanged,this,[=](qreal value){controller->nudgeRadius=value;controller->notifySettings();canvas->notifyToolSettingsEdited();});
    connect(nudgeStrength,&ValueSlider::valueChanged,this,[=](qreal value){controller->nudgeStrength=value/100;canvas->notifyToolSettingsEdited();});
    const auto refresh=[=,this]{
        using T=Canvas::Tool;
        const auto tool=canvas->tool(); const bool pathEdit=tool==T::VectorNode;
        const bool simplifyBrush=tool==T::VectorSimplify, widthBrush=tool==T::VectorWidth,nudgeBrush=tool==T::VectorNudge;
        bool vector=false,editable=false,animatedLayer=false;
        if(doc->isOpen()){
            const auto &layers=doc->activeShot().layers;const int active=doc->activeLayer();
            vector=layers[active].kind==LayerKind::Vector&&!layers[active].group;
            editable=vector&&layerCanEdit(layers,active);
            const auto *animation=layerAnimation(doc->activeShot(),layers[active].id);
            animatedLayer=animation&&!animation->paths.isEmpty();
        }
        const auto object=controller->selectedObject();
        const bool animated=controller->selectedPathAnimated();
        const bool sourceBlocked=animated&&doc->keyframeSourceEditing();
        mode->setVisible(pathEdit); mode->setEnabled(editable);
        style->setVisible(pathEdit); style->setEnabled(editable&&object.has_value()&&!sourceBlocked);
        const bool simplification=(pathEdit&&controller->pathEditMode==DrawingToolController::PathEditMode::Nodes)||simplifyBrush;
        simplify->setVisible(simplification);selected->setVisible(simplification);layer->setVisible(simplification);
        simplify->setEnabled(editable); selected->setEnabled(editable&&object.has_value()&&!animated);layer->setEnabled(editable&&!animatedLayer);
        const QString simplifyHelp="Disable path animation before simplifying its nodes";
        selected->setToolTip(animated?simplifyHelp:QString("Simplify selected path"));
        layer->setToolTip(animatedLayer?simplifyHelp:QString("Simplify all paths on this vector layer"));
        const bool localWidth=pathEdit&&controller->pathEditMode==DrawingToolController::PathEditMode::Width;
        reset->setVisible(localWidth);reset->setEnabled(editable&&object.has_value()&&!sourceBlocked);
        commands->setVisible(simplification||localWidth);
        brush->setVisible(simplifyBrush||widthBrush); brush->setEnabled(editable);
        nudge->setVisible(nudgeBrush);nudge->setEnabled(editable&&!animatedLayer&&canvas->toolBlockReason().isEmpty());
        direction->setVisible(widthBrush); strength->parentWidget()->setVisible(widthBrush);
        status->setText(!vector?"Select a vector layer":!editable?"Layer locked or hidden":
            sourceBlocked?"Source · animated path locked":
            pathEdit?(object?QString(animated?"Animated path · %1 nodes":"%1 nodes").arg(object->nodes.size()):QString("Select a path")):QString());
        status->setToolTip(sourceBlocked?QString("Leave Source mode to edit this path's animation, or disable its path animation to edit the source"):status->text());
        status->setAccessibleName(status->text());status->setAccessibleDescription(status->toolTip());
        QSignalBlocker a(mode),b(direction);
        mode->setCurrentIndex(int(controller->pathEditMode));direction->setCurrentIndex(controller->widthThicken?0:1);
        tolerance->setValueSilently(controller->simplifyTolerance);radius->setValueSilently(controller->widthBrushRadius);
        strength->setValueSilently(controller->widthBrushStrength*100);
        nudgeRadius->setValueSilently(controller->nudgeRadius);nudgeStrength->setValueSilently(controller->nudgeStrength*100);
        if(object){
            width->setValueSilently(object->strokeWidth);opacity->setValueSilently(object->opacity*100);
            if(color->property("strokeColor").value<QColor>()!=object->stroke){
                const int size=ui::metrics::actionIcon;
                QPixmap swatch(size*2,size*2);swatch.setDevicePixelRatio(2);swatch.fill(Qt::transparent);
                QPainter painter(&swatch);
                for(int y=0;y<size;y+=4)for(int x=0;x<size;x+=4)painter.fillRect(x,y,4,4,QColor((x/4+y/4)%2?ui::raised:ui::muted));
                painter.fillRect(QRect(0,0,size,size),object->stroke);painter.setPen(QColor(ui::border));painter.drawRect(0,0,size-1,size-1);painter.end();
                color->setIcon(QIcon(swatch));color->setProperty("strokeColor",object->stroke);
                color->setAccessibleDescription(object->stroke.name(QColor::HexArgb));
            }
        }
        setEnabled(canvas->toolAvailable());
    };
    connect(controller,&DrawingToolController::changed,this,refresh);
    connect(canvas,&Canvas::toolChanged,this,refresh);
    connect(canvas,&Canvas::toolStateApplied,this,refresh);
    connect(canvas,&Canvas::toolAvailabilityChanged,this,refresh);
    connect(doc,&Document::changed,this,[doc,refresh]{
        // A live brush stroke does not alter the selected object's properties.
        // Refresh after commit/cancel, rather than restyling this hidden page
        // for every incoming pen sample.
        if (!doc->strokeActive()) refresh();
    });
    connect(doc,&Document::selectionChanged,this,refresh);
    refresh();
}
}
