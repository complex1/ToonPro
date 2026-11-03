#include "effects_panel.h"
#include "canvas.h"
#include "document.h"
#include "icons.h"
#include "mask_tool.h"
#include "timeline_controller.h"
#include "property_row.h"
#include "ui_theme.h"
#include "ui_metrics.h"
#include "value_slider.h"
#include <QColorDialog>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QScopedValueRollback>
#include <QScrollArea>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QStackedLayout>
#include <QTabBar>
#include <QToolButton>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>

namespace toon {
namespace {
constexpr KeyframeProperty effectFields[]{KeyframeProperty::BlurRadius,KeyframeProperty::ShadowOffsetX,KeyframeProperty::ShadowOffsetY,KeyframeProperty::ShadowRadius,KeyframeProperty::ShadowOpacity,KeyframeProperty::GlowRadius,KeyframeProperty::GlowOpacity,KeyframeProperty::Exposure,KeyframeProperty::Contrast,KeyframeProperty::Saturation,KeyframeProperty::Hue};
constexpr KeyframeProperty maskFields[]{KeyframeProperty::MaskPositionX,KeyframeProperty::MaskPositionY,KeyframeProperty::MaskScaleX,KeyframeProperty::MaskScaleY,KeyframeProperty::MaskRotation,KeyframeProperty::MaskOpacity,KeyframeProperty::MaskFeather};
constexpr KeyframeProperty effectEnables[]{KeyframeProperty::BlurEnabled,KeyframeProperty::ShadowEnabled,KeyframeProperty::GlowEnabled,KeyframeProperty::ColorEnabled};
qreal maskDisplay(KeyframeProperty property,qreal value){return keyframePropertyIsPercent(property)?value*100:value;}
const RasterLayer *activeLayer(const Document *document) {
    if (!document || !document->isOpen() || document->project().shots.isEmpty()) return nullptr;
    const auto &layers = document->activeShot().layers;
    return document->activeLayer() >= 0 && document->activeLayer() < layers.size() ? &layers[document->activeLayer()] : nullptr;
}
class ContextLabel final : public QLabel {
public:
    explicit ContextLabel(QWidget *parent) : QLabel(parent) { updateHeight(); }
protected:
    void changeEvent(QEvent *event) override {
        QLabel::changeEvent(event);
        if(event->type()==QEvent::FontChange)updateHeight();
    }
    void paintEvent(QPaintEvent *) override {
        QPainter painter(this); painter.setPen(palette().color(QPalette::WindowText));
        painter.drawText(rect(), Qt::AlignLeft | Qt::AlignVCenter, fontMetrics().elidedText(text(), Qt::ElideRight, width()));
    }
private:
    void updateHeight(){setFixedHeight(qMax(20,fontMetrics().height()+2));}
};
// Five icon categories remain directly reachable in a narrow dock. Visible
// category names are kept beside the enable toggle, not repeated on every tab.
class EffectsCategoryTabs final : public QTabBar {
public:
    using QTabBar::QTabBar;
    QSize tabSizeHint(int) const override { return {ui::metrics::actionTarget,qMax(ui::metrics::tabHeight,fontMetrics().height()+6)}; }
    QSize minimumTabSizeHint(int index) const override { return tabSizeHint(index); }
};
QToolButton *button(QWidget *parent, const char *id, const char *icon, const QString &help, bool toggle = false) {
    auto *result = new QToolButton(parent); result->setObjectName(id); result->setIcon(appIcon(icon));
    result->setIconSize({ui::metrics::actionIcon,ui::metrics::actionIcon});
    result->setFixedSize(ui::metrics::actionTarget,ui::metrics::actionTarget);
    result->setProperty("compactDrawingAction",true); result->setCheckable(toggle);
    result->setToolTip(help); result->setAccessibleName(help); return result;
}
void colorChip(QToolButton *button, QColor color) {
    QPixmap image(40,40); image.setDevicePixelRatio(2); image.fill(Qt::transparent);
    {QPainter painter(&image); painter.setPen(QColor(ui::muted)); painter.setBrush(color); painter.drawRoundedRect(QRectF(1,1,18,18),3,3);}
    button->setIcon(QIcon(image)); button->setAccessibleDescription(color.name());
}
QLabel *context(QWidget *parent, const char *id) {
    auto *label = new ContextLabel(parent); label->setObjectName(id); label->setFixedHeight(qMax(20,label->fontMetrics().height()+2));
    label->setTextFormat(Qt::PlainText);label->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Fixed); return label;
}
void addColor(QVBoxLayout *layout,QToolButton *button){
    auto *editor=new QWidget(layout->parentWidget());auto *row=new QHBoxLayout(editor);row->setContentsMargins(0,0,0,0);row->setSpacing(0);row->addWidget(button);row->addStretch();
    layout->addWidget(new PropertyRow("Color",editor,layout->parentWidget()));
}
// A simple page must not inherit the height of the Shadow pad and its fields.
// The installed layout owns height-for-width, including wrapped property rows.
class EffectPropertyLayout final : public QStackedLayout {
public:
    explicit EffectPropertyLayout(QWidget *parent) : QStackedLayout(parent) { setSizeConstraint(QLayout::SetNoConstraint); }
    QSize minimumSize() const override {
        return currentWidget() ? currentWidget()->minimumSizeHint().expandedTo(currentWidget()->minimumSize()) : QSize();
    }
    QSize sizeHint() const override { return currentWidget() ? currentWidget()->sizeHint() : QSize(); }
    bool hasHeightForWidth() const override { return true; }
    int heightForWidth(int width) const override {
        if (!currentWidget()) return 0;
        return currentWidget()->hasHeightForWidth() ? currentWidget()->heightForWidth(width) : currentWidget()->sizeHint().height();
    }
};
}

class EffectPropertyPages final : public QWidget {
public:
    EffectPropertyPages() : m_layout(new EffectPropertyLayout(this)) {
        auto policy=QSizePolicy(QSizePolicy::Expanding,QSizePolicy::Preferred);policy.setHeightForWidth(true);setSizePolicy(policy);
        connect(m_layout,&QStackedLayout::currentChanged,this,[this]{updateGeometry();});
    }
    void addWidget(QWidget *widget){m_layout->addWidget(widget);}
    void setCurrentIndex(int index){m_layout->setCurrentIndex(index);}
private:
    EffectPropertyLayout *m_layout;
};

// A coupled spatial control; exact X/Y values remain available underneath it.
class ShadowOffsetWidget final : public QWidget {
public:
    explicit ShadowOffsetWidget(QWidget *parent) : QWidget(parent) {
        setObjectName("effectShadowOffset"); setFixedHeight(100); setMinimumWidth(0);
        setFocusPolicy(Qt::StrongFocus); setMouseTracking(true); setAccessibleName("Shadow offset pad");
        setToolTip("Drag the shadow offset · arrow keys move 1 px · Shift moves 10 px · Escape cancels");
    }
    std::function<bool()> begin;
    std::function<void(QPointF)> preview;
    std::function<void()> finish, cancel;
    void setOffset(QPointF offset) { m_offset = offset; update(); }
    void stop() { m_dragging = false; }
protected:
    bool event(QEvent *event) override {
        if(m_dragging&&(event->type()==QEvent::FocusOut||event->type()==QEvent::UngrabMouse||event->type()==QEvent::WindowDeactivate||event->type()==QEvent::Hide)){
            m_dragging=false;if(finish)finish();
        }
        return QWidget::event(event);
    }
    void paintEvent(QPaintEvent *) override {
        QPainter p(this); p.setRenderHint(QPainter::Antialiasing);
        const QRectF area = QRectF(rect()).adjusted(2,2,-2,-2); const QPointF center = area.center();
        p.setPen(QColor(hasFocus()?ui::focus:ui::border)); p.setBrush(QColor(ui::base)); p.drawRoundedRect(area,4,4);
        p.setPen(QPen(QColor(ui::border),1,Qt::DashLine)); p.drawLine(QPointF(area.left()+6,center.y()),QPointF(area.right()-6,center.y()));
        p.drawLine(QPointF(center.x(),area.top()+6),QPointF(center.x(),area.bottom()-6));
        const QPointF point = center + QPointF(std::clamp(m_offset.x(),-128.,128.)*travel().x()/128., std::clamp(m_offset.y(),-128.,128.)*travel().y()/128.);
        p.setPen(QPen(QColor(isEnabled()?ui::accent:ui::disabled),2)); p.drawLine(center,point);
        p.setBrush(QColor(ui::raised)); p.drawRect(QRectF(center-QPointF(7,7),QSizeF(14,14)));
        p.setBrush(QColor(isEnabled()?ui::accent:ui::disabled)); p.drawEllipse(point,6,6);
    }
    void mousePressEvent(QMouseEvent *event) override {
        if(event->button()!=Qt::LeftButton || !begin || !begin())return;
        setFocus(Qt::MouseFocusReason);m_dragging=true;move(event->position());event->accept();
    }
    void mouseMoveEvent(QMouseEvent *event) override {if(m_dragging){if(event->buttons()&Qt::LeftButton)move(event->position());else{m_dragging=false;if(finish)finish();}event->accept();}}
    void mouseReleaseEvent(QMouseEvent *event) override {if(m_dragging&&event->button()==Qt::LeftButton){move(event->position());m_dragging=false;if(finish)finish();event->accept();}}
    void keyPressEvent(QKeyEvent *event) override {
        if(event->key()==Qt::Key_Escape&&m_dragging){m_dragging=false;if(cancel)cancel();event->accept();return;}
        QPointF delta;const int step=event->modifiers().testFlag(Qt::ShiftModifier)?10:1;
        if(event->key()==Qt::Key_Left)delta.setX(-step);else if(event->key()==Qt::Key_Right)delta.setX(step);
        else if(event->key()==Qt::Key_Up)delta.setY(-step);else if(event->key()==Qt::Key_Down)delta.setY(step);else{QWidget::keyPressEvent(event);return;}
        if(begin&&begin()){const auto next=m_offset+delta;send({std::clamp(next.x(),-512.,512.),std::clamp(next.y(),-512.,512.)});if(finish)finish();}event->accept();
    }
private:
    QPointF travel() const {return {std::max(1.,width()/2.-14.),std::max(1.,height()/2.-14.)};}
    void send(QPointF value){m_offset=value;if(preview)preview(value);update();}
    void move(QPointF position){const auto delta=position-QRectF(rect()).center();send({std::clamp(std::round(delta.x()/travel().x()*128.),-128.,128.),std::clamp(std::round(delta.y()/travel().y()*128.),-128.,128.)});}
    QPointF m_offset; bool m_dragging=false;
};

EffectsPanel::EffectsPanel(Canvas *canvas,QWidget *parent) : QWidget(parent),m_canvas(canvas),m_document(canvas->document()) {
    setObjectName("effectsPanel");setMinimumWidth(0);setProperty("compactDrawingPanel",true);
    auto *outer=new QVBoxLayout(this);outer->setContentsMargins(ui::metrics::panelMargin,ui::metrics::panelMargin,ui::metrics::panelMargin,ui::metrics::panelMargin);outer->setSpacing(ui::metrics::controlGap);
    auto *header=new QHBoxLayout;header->setSpacing(4);m_target=context(this,"effectsTargetName");header->addWidget(m_target,1);
    m_master=button(this,"effectsMasterEnabled","sparkles","Enable layer effects",true);
    m_addAdjustment=button(this,"effectsAddAdjustment","file-plus","Add color adjustment layer");header->addWidget(m_master);header->addWidget(keyButton(this,KeyframeProperty::EffectsEnabled));header->addWidget(m_addAdjustment);outer->addLayout(header);
    m_status=context(this,"effectsStatus");outer->addWidget(m_status);
    m_tabs=new EffectsCategoryTabs(this);m_tabs->setObjectName("effectsTabs");m_tabs->setExpanding(true);m_tabs->setDrawBase(false);m_tabs->setAccessibleName("Effect category");
    m_tabs->setIconSize({ui::metrics::tabIcon,ui::metrics::tabIcon});m_tabs->setProperty("compactDrawingTabs",true);m_tabs->setFocusPolicy(Qt::StrongFocus);
    m_tabs->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Fixed);
    const QStringList icons={"square-dashed",featureIconName(FeatureIcon::Blur),"layers","sun","palette"};const QStringList names={"Mask","Blur","Shadow","Glow","Color"};
    for(int i=0;i<names.size();++i){m_tabs->addTab(appIcon(icons[i]),{});m_tabs->setTabToolTip(i,names[i]);m_tabs->setAccessibleTabName(i,names[i]);}outer->addWidget(m_tabs);
    m_scroll=new QScrollArea(this);m_scroll->setObjectName("effectsScroll");m_scroll->setFrameShape(QFrame::NoFrame);m_scroll->setWidgetResizable(true);m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);outer->addWidget(m_scroll,1);
    m_pages=new EffectPropertyPages;m_pages->setObjectName("effectsPages");m_scroll->setWidget(m_pages);
    auto *maskPage=new QWidget;auto *mask=new QVBoxLayout(maskPage);mask->setContentsMargins(0,0,0,0);mask->setSpacing(ui::metrics::controlGap);m_pages->addWidget(maskPage);
    auto *maskTop=new QHBoxLayout;maskTop->setSpacing(4);m_thumbnail=new QLabel(maskPage);m_thumbnail->setObjectName("maskThumbnail");m_thumbnail->setFixedSize(88,70);m_thumbnail->setAlignment(Qt::AlignCenter);m_thumbnail->setToolTip("Mask source · white reveals, black hides before inversion and placement");m_thumbnail->setAccessibleName("Layer mask source thumbnail");maskTop->addWidget(m_thumbnail);
    auto *maskActions=new QVBoxLayout;maskActions->setSpacing(4);auto *creation=new QHBoxLayout;creation->setSpacing(4);
    m_maskAdd=button(maskPage,"maskAddWhite","plus","Add a white layer mask");m_maskSelection=button(maskPage,"maskFromSelection","square-dashed-mouse-pointer","Create a mask from selection");creation->addWidget(m_maskAdd);creation->addWidget(m_maskSelection);creation->addStretch();maskActions->addLayout(creation);
    auto *existing=new QHBoxLayout;existing->setSpacing(4);m_maskEnabled=button(maskPage,"maskEnabled","eye","Enable layer mask",true);m_maskRemove=button(maskPage,"maskRemove","trash","Remove layer mask");existing->addWidget(m_maskEnabled);existing->addWidget(keyButton(maskPage,KeyframeProperty::MaskEnabled));existing->addStretch();maskActions->addLayout(existing);maskActions->addWidget(m_maskRemove,0,Qt::AlignLeft);maskTop->addLayout(maskActions,1);mask->addLayout(maskTop);
    m_maskPropertyTabs=new QTabBar(maskPage);m_maskPropertyTabs->setObjectName("maskPropertyTabs");m_maskPropertyTabs->setProperty("compactDrawingTabs",true);m_maskPropertyTabs->setExpanding(true);m_maskPropertyTabs->setDrawBase(false);m_maskPropertyTabs->setFocusPolicy(Qt::StrongFocus);
    m_maskPropertyTabs->addTab("Paint");m_maskPropertyTabs->addTab("Placement");m_maskPropertyTabs->setTabToolTip(0,"Paint the shared mask source");m_maskPropertyTabs->setAccessibleTabName(0,"Mask source painting");m_maskPropertyTabs->setTabToolTip(1,"Mask placement, opacity and feather animation");m_maskPropertyTabs->setAccessibleTabName(1,"Mask placement");mask->addWidget(m_maskPropertyTabs);
    connect(m_maskPropertyTabs,&QTabBar::currentChanged,this,[this]{if(!m_refreshing)finishEditing();refresh();});
    m_maskControls=new QWidget(maskPage);m_maskControls->setObjectName("maskControls");
    auto *maskControls=new QVBoxLayout(m_maskControls);maskControls->setContentsMargins(0,0,0,0);maskControls->setSpacing(ui::metrics::controlGap);
    auto *paintRow=new QHBoxLayout;paintRow->setSpacing(ui::metrics::controlGap);m_maskPaint=button(maskPage,"maskPaint","paintbrush","Paint the layer mask",true);paintRow->addWidget(m_maskPaint);
    m_maskMode=new QTabBar(maskPage);m_maskMode->setObjectName("maskMode");m_maskMode->setExpanding(true);m_maskMode->setDrawBase(false);m_maskMode->setAccessibleName("Mask brush mode");
    m_maskMode->setProperty("compactDrawingTabs",true);m_maskMode->setFocusPolicy(Qt::StrongFocus);m_maskMode->setElideMode(Qt::ElideRight);
    for(const auto &name:{QString("Reveal"),QString("Hide")}){const int index=m_maskMode->addTab(name);m_maskMode->setTabToolTip(index,name+" with the mask brush");m_maskMode->setAccessibleTabName(index,name+" mask brush");}paintRow->addWidget(m_maskMode,1);maskControls->addLayout(paintRow);
    auto *fills=new QHBoxLayout;fills->setSpacing(4);m_maskInvert=button(maskPage,"maskInvert","contrast","Invert the displayed mask without changing its source",true);m_maskRevealAll=button(maskPage,"maskRevealAll","eye","Fill mask source white");m_maskHideAll=button(maskPage,"maskHideAll","eye-off","Fill mask source black");fills->addWidget(m_maskInvert);fills->addWidget(m_maskRevealAll);fills->addWidget(m_maskHideAll);fills->addStretch();maskControls->addLayout(fills);
    m_maskBrush=new QWidget(maskPage);auto *brushRows=new QVBoxLayout(m_maskBrush);brushRows->setContentsMargins(0,2,0,0);brushRows->setSpacing(4);
    const char *maskIds[]={"maskSize","maskStrength","maskHardness"};const QStringList maskLabels={"Size","Strength","Hardness"};
    for(int i=0;i<3;++i){const QString unit=i==0?"px":"%";
        auto *field=new ValueSlider("Mask "+maskLabels[i],i==0?1:0,i==0?512:100,i==0?32:100,1,unit,m_maskBrush);field->setObjectName(maskIds[i]);field->setSingleStep(1);m_maskValues[i]=field;brushRows->addWidget(new PropertyRow(maskLabels[i],field,m_maskBrush));
        connect(field,&ValueSlider::valueChanged,this,[this,i](double value){if(m_refreshing||!m_canvas)return;auto *tool=m_canvas->maskTool();if(i==0)tool->setSize(value);else if(i==1)tool->setStrength(value/100);else tool->setHardness(value/100);});}
    maskControls->addWidget(m_maskBrush);mask->addWidget(m_maskControls);
    m_maskPlacement=new QWidget(maskPage);m_maskPlacement->setObjectName("maskPlacement");auto *placement=new QVBoxLayout(m_maskPlacement);placement->setContentsMargins(0,0,0,0);placement->setSpacing(ui::metrics::controlGap);
    const QStringList placementNames{"Position X","Position Y","Scale X","Scale Y","Rotation","Opacity","Feather"};
    for(int i=0;i<7;++i){const auto property=maskFields[i];const auto name=keyframePropertyKey(property);
        auto *field=new ValueSlider(placementNames[i],maskDisplay(property,keyframePropertyMinimum(property)),maskDisplay(property,keyframePropertyMaximum(property)),maskDisplay(property,keyframeDefaultValue(property)),2,i==2||i==3||i==5?"%":i==4?"degrees":"px",m_maskPlacement);
        field->setObjectName(name);field->setSingleStep(i==2||i==3||i==5?1:.5);if(i<2)field->setSliderRange(-1000,1000);else if(i<4)field->setSliderRange(1,200);else if(i==4)field->setSliderRange(-180,180);m_maskPlacementValues[i]=field;
        auto *row=new PropertyRow(placementNames[i],field,m_maskPlacement);row->setObjectName(name+"Row");row->setAccessory(keyButton(m_maskPlacement,property));placement->addWidget(row);
        connect(field,&ValueSlider::editingStarted,this,&EffectsPanel::beginMaskValue);connect(field,&ValueSlider::valueChanged,this,[this,i](double value){previewMaskValue(i,value);});connect(field,&ValueSlider::editingFinished,this,&EffectsPanel::finishValue);connect(field,&ValueSlider::editingCancelled,this,&EffectsPanel::cancelEditing);
    }
    m_maskPresentationInvert=button(m_maskPlacement,"maskPresentationInverted","contrast","Invert the displayed mask without changing its source",true);m_maskPresentationInvert->setToolButtonStyle(Qt::ToolButtonTextOnly);m_maskPresentationInvert->setFixedWidth(48);
    auto *invertRow=new PropertyRow("Invert",m_maskPresentationInvert,m_maskPlacement);invertRow->setAccessory(keyButton(m_maskPlacement,KeyframeProperty::MaskInverted));placement->addWidget(invertRow);
    connect(m_maskPresentationInvert,&QToolButton::clicked,this,[this]{maskAction([this]{m_document->invertLayerMask(true);});});
    mask->addWidget(m_maskPlacement);mask->addStretch();
    const char *toggleIds[]={"effectBlurEnabled","effectShadowEnabled","effectGlowEnabled","effectColorEnabled"};
    for(int i=0;i<4;++i){auto *page=new QWidget;auto *rows=new QVBoxLayout(page);rows->setContentsMargins(0,0,0,0);rows->setSpacing(ui::metrics::controlGap);m_pages->addWidget(page);
        auto *headingWidget=new QWidget(this);headingWidget->setObjectName(QString::fromLatin1(toggleIds[i])+"Heading");m_effectHeadings[i]=headingWidget;
        auto *heading=new QHBoxLayout(headingWidget);heading->setContentsMargins(0,0,0,0);heading->setSpacing(ui::metrics::controlGap);
        auto *enabled=button(headingWidget,toggleIds[i],"eye","Enable "+names[i+1].toLower(),true);m_enabled[i]=enabled;
        heading->addWidget(enabled);heading->addWidget(new QLabel(names[i+1],headingWidget),1);heading->addWidget(keyButton(headingWidget,effectEnables[i]));
        outer->insertWidget(outer->indexOf(m_scroll),headingWidget);headingWidget->hide();
        auto *body=new QWidget(page);body->setObjectName(QString::fromLatin1(toggleIds[i])+"Details");m_effectBodies[i]=body;auto *fields=new QVBoxLayout(body);fields->setContentsMargins(0,0,0,0);fields->setSpacing(ui::metrics::controlGap);rows->addWidget(body);rows->addStretch();
        if(i==0)addValue(fields,"effectBlurRadius","Radius",0,128,4,1,"px",0);
        if(i==1){m_offset=new ShadowOffsetWidget(body);fields->addWidget(m_offset);addValue(fields,"effectShadowX","Offset X",-512,512,8,1,"px",1)->setSliderRange(-128,128);addValue(fields,"effectShadowY","Offset Y",-512,512,8,1,"px",2)->setSliderRange(-128,128);
            addValue(fields,"effectShadowRadius","Softness",0,128,8,1,"px",3);addValue(fields,"effectShadowOpacity","Opacity",0,100,50,1,"%",4);
            m_shadowColor=button(body,"effectShadowColor","circle","Choose shadow color");addColor(fields,m_shadowColor);}
        if(i==2){addValue(fields,"effectGlowRadius","Radius",0,128,8,1,"px",5);addValue(fields,"effectGlowOpacity","Opacity",0,100,50,1,"%",6);
            m_glowColor=button(body,"effectGlowColor","circle","Choose glow color");addColor(fields,m_glowColor);}
        if(i==3){addValue(fields,"effectExposure","Exposure",-4,4,0,2,"stops",7)->setSingleStep(.1);addValue(fields,"effectContrast","Contrast",-100,100,0,1,"%",8);addValue(fields,"effectSaturation","Saturation",-100,100,0,1,"%",9);addValue(fields,"effectHue","Hue",-180,180,0,1,"°",10);}
        connect(enabled,&QToolButton::toggled,this,[this,i](bool enabled){changeEffect([i,enabled](LayerEffects &e){if(i==0)e.blurEnabled=enabled;else if(i==1)e.shadowEnabled=enabled;else if(i==2)e.glowEnabled=enabled;else e.colorEnabled=enabled;});});}
    connect(m_tabs,&QTabBar::currentChanged,this,[this](int index){
        if(!m_refreshing)finishEditing();
        m_scrollPositions[m_currentPage]=m_scroll->verticalScrollBar()->value();m_currentPage=index;m_pages->setCurrentIndex(index);
        for(int i=0;i<4;++i)m_effectHeadings[i]->setVisible(index==i+1);
        // Let the new page settle before restoring its own scroll position.
        QTimer::singleShot(0,this,[this,index]{if(m_currentPage==index)m_scroll->verticalScrollBar()->setValue(m_scrollPositions[index]);});
    });
    connect(m_master,&QToolButton::toggled,this,[this](bool enabled){changeEffect([enabled](LayerEffects &e){e.enabled=enabled;});});
    connect(m_addAdjustment,&QToolButton::clicked,this,[this]{maskAction([this]{if(m_document->addAdjustmentLayer())m_tabs->setCurrentIndex(4);});});
    connect(m_maskAdd,&QToolButton::clicked,this,[this]{maskAction([this]{m_document->addLayerMask();});});
    connect(m_maskSelection,&QToolButton::clicked,this,[this]{maskAction([this]{m_document->addLayerMask(true);});});
    connect(m_maskEnabled,&QToolButton::toggled,this,[this](bool enabled){if(!m_refreshing)maskAction([this,enabled]{m_document->setLayerMaskEnabled(enabled,true);});});
    connect(m_maskRemove,&QToolButton::clicked,this,[this]{maskAction([this]{m_canvas->maskTool()->setEnabled(false);m_document->removeLayerMask();});});
    connect(m_maskInvert,&QToolButton::clicked,this,[this]{maskAction([this]{m_document->invertLayerMask(true);});});
    connect(m_maskRevealAll,&QToolButton::clicked,this,[this]{maskAction([this]{m_document->fillLayerMask(true);});});
    connect(m_maskHideAll,&QToolButton::clicked,this,[this]{maskAction([this]{m_document->fillLayerMask(false);});});
    connect(m_maskPaint,&QToolButton::toggled,this,[this](bool enabled){if(m_refreshing)return;if(prepareAction())m_canvas->maskTool()->setEnabled(enabled);refresh();});
    connect(m_maskMode,&QTabBar::currentChanged,this,[this](int mode){if(!m_refreshing&&m_canvas)m_canvas->maskTool()->setRevealing(mode==0);});
    connect(m_shadowColor,&QToolButton::clicked,this,[this]{pickColor(true);});connect(m_glowColor,&QToolButton::clicked,this,[this]{pickColor(false);});
    m_offset->begin=[this]{finishEditing();beginValue();return m_editing;};
    m_offset->preview=[this](QPointF value){if(!m_editing)return;m_draft.shadowOffset=value;m_document->previewLayerEffects(m_draft);m_values[1]->setValueSilently(value.x());m_values[2]->setValueSilently(value.y());};
    m_offset->finish=[this]{finishValue();};m_offset->cancel=[this]{cancelEditing();};
    connect(m_document,&Document::changed,this,&EffectsPanel::refresh);connect(m_document,&Document::loaded,this,&EffectsPanel::refresh);
    connect(m_document,&Document::selectionChanged,this,&EffectsPanel::refresh);connect(m_document,&Document::frameChanged,this,&EffectsPanel::refresh);
    connect(m_document,&Document::rasterSelectionChanged,this,&EffectsPanel::refresh);connect(canvas,&Canvas::frameViewChanged,this,&EffectsPanel::refresh);connect(canvas,&Canvas::selectionUiChanged,this,&EffectsPanel::refresh);
    connect(m_document,&Document::effectsEditResolutionRequested,this,&EffectsPanel::finishEditing);
    connect(canvas->maskTool(),&MaskTool::changed,this,&EffectsPanel::refresh);
    // Preview completion must not rebuild controls or change their edit state.
    connect(canvas,&Canvas::effectsPreviewPendingChanged,this,[this]{refreshPreviewStatus();});
    connect(m_document,&QObject::destroyed,this,[this]{m_document=nullptr;setEnabled(false);});refresh();
}

EffectsPanel::~EffectsPanel(){if(m_document)m_document->disconnect(this);for(auto *child:findChildren<QObject *>())child->disconnect(this);cancelEditing();}
void EffectsPanel::setTimelineController(TimelineController *controller){
    if(m_timeline==controller)return;
    if(m_timeline)m_timeline->disconnect(this);m_timeline=controller;
    if(controller){
        connect(controller,&TimelineController::changed,this,&EffectsPanel::refresh);
        connect(controller,&TimelineController::propertyChannelRequested,this,&EffectsPanel::selectChannel);
        connect(controller,&QObject::destroyed,this,[this]{m_timeline=nullptr;refresh();});
    }
    refresh();
}
QToolButton *EffectsPanel::keyButton(QWidget *parent,KeyframeProperty property){
    const auto key=keyframePropertyKey(property);auto *result=button(parent,qPrintable("effectsKey_"+key),"circle","Animate "+keyframePropertyName(property),true);
    QPixmap image(40,40);image.setDevicePixelRatio(2);image.fill(Qt::transparent);
    {QPainter painter(&image);painter.setRenderHint(QPainter::Antialiasing);painter.setPen(QPen(QColor(ui::text),1.8));painter.setBrush(Qt::NoBrush);painter.drawPolygon(QPolygonF{QPointF(10,3),QPointF(17,10),QPointF(10,17),QPointF(3,10)});}
    result->setIcon(QIcon(image));result->setFocusPolicy(Qt::StrongFocus);result->setVisible(false);m_keyButtons.insert(int(property),result);
    connect(result,&QToolButton::toggled,this,[this,property](bool enabled){toggleChannel(property,enabled);});return result;
}
void EffectsPanel::toggleChannel(KeyframeProperty property,bool enabled){
    if(m_refreshing||!m_timeline||!m_document)return;
    const auto *layer=activeLayer(m_document);if(!layer)return;const auto id=layer->id,shotId=m_document->activeShotId();
    if(!prepareAction()||!m_document->isOpen()||m_document->activeShotId()!=shotId||!activeLayer(m_document)||activeLayer(m_document)->id!=id){refresh();return;}
    if(m_timeline->selectedPropertyLayerId()!=id&&!m_timeline->selectPropertyLayer(id)){refresh();return;}
    if(m_timeline->setPropertyEnabled(property,enabled)){emit m_timeline->propertyChannelRequested(property);emit m_timeline->animationPropertiesRequested();}
    refresh();
}
void EffectsPanel::selectChannel(KeyframeProperty property){
    if(keyframePropertyIsMask(property)){m_tabs->setCurrentIndex(0);m_maskPropertyTabs->setCurrentIndex(1);return;}
    if(!keyframePropertyIsEffect(property)||property==KeyframeProperty::EffectsEnabled)return;
    int page=4;
    if(property==KeyframeProperty::BlurEnabled||property==KeyframeProperty::BlurRadius)page=1;
    else if(property>=KeyframeProperty::ShadowEnabled&&property<=KeyframeProperty::ShadowOpacity)page=2;
    else if(property>=KeyframeProperty::GlowEnabled&&property<=KeyframeProperty::GlowOpacity)page=3;
    m_tabs->setCurrentIndex(page);
}
void EffectsPanel::refreshKeyButtons(){
    const auto *layer=activeLayer(m_document);
    for(auto it=m_keyButtons.cbegin();it!=m_keyButtons.cend();++it){const auto property=static_cast<KeyframeProperty>(it.key());auto *key=it.value();
        const bool visible=m_timeline&&(!keyframePropertyIsMask(property)||(layer&&layer->mask));key->setVisible(visible);
        const auto *channel=layer?propertyChannel(m_document->activeShot(),layer->id,property):nullptr;const bool enabled=channel&&!channel->keys.isEmpty();
        QString reason=layer?m_document->effectsEditReason():QString("Choose a layer.");
        if(reason.isEmpty())reason=keyframePropertyUnavailableReason(m_document->activeShot(),layer->id,property);
        if(reason.isEmpty()&&m_timeline&&m_timeline->playing())reason="Stop playback to edit animation.";
        if(reason.isEmpty()&&keyframePropertyNeedsPositionUnlock(property)&&layerPositionLocked(m_document->activeShot().layers,m_document->activeLayer()))reason="Unlock the layer position to animate mask placement.";
        const bool editable=m_timeline&&reason.isEmpty();
        key->setEnabled(editable);key->setChecked(enabled);
        bool exact=false;if(channel)for(const auto &frame:channel->keys)if(frame.frame==m_document->currentFrame()){exact=true;break;}
        QString help=(enabled?"Disable ":"Animate ")+keyframePropertyName(property)+(enabled?" · removes its keys; Undo restores them":" · create a key at this frame");if(enabled&&exact)help+=" · key at this frame";if(!reason.isEmpty())help+=" · "+reason;
        key->setToolTip(help);key->setAccessibleName(help);
    }
}
LayerEffects EffectsPanel::displayedEffects() const{
    const auto *layer=activeLayer(m_document);return layer?evaluateAnimatedLayerEffects(m_document->activeShot(),layer->id,m_document->currentFrame(),layer->effects):LayerEffects{};
}
LayerMask EffectsPanel::displayedMask() const{
    const auto *layer=activeLayer(m_document);return layer&&layer->mask?evaluateAnimatedLayerMask(m_document->activeShot(),layer->id,m_document->currentFrame(),*layer->mask):LayerMask{};
}
ValueSlider *EffectsPanel::addValue(QVBoxLayout *layout,const char *id,const QString &label,double low,double high,double initial,int decimals,const QString &unit,int field){
    auto *value=new ValueSlider(label,low,high,initial,decimals,unit,layout->parentWidget());value->setObjectName(id);value->setSingleStep(1);auto *row=new PropertyRow(label,value,layout->parentWidget());row->setObjectName(QString::fromLatin1(id)+"Row");row->setAccessory(keyButton(layout->parentWidget(),effectFields[field]));layout->addWidget(row);m_values[field]=value;
    connect(value,&ValueSlider::editingStarted,this,&EffectsPanel::beginValue);connect(value,&ValueSlider::valueChanged,this,[this,field](double n){previewValue(field,n);});
    connect(value,&ValueSlider::editingFinished,this,&EffectsPanel::finishValue);connect(value,&ValueSlider::editingCancelled,this,&EffectsPanel::cancelEditing);return value;
}
bool EffectsPanel::prepareAction(){
    if(!m_canvas||!m_document)return false;finishEditing();m_canvas->finishStroke();m_canvas->maskTool()->finish();return m_canvas->resolvePendingTransform();
}
void EffectsPanel::maskAction(const std::function<void()> &action){if(prepareAction())action();refresh();}
void EffectsPanel::beginValue(){
    if(m_refreshing||m_editing||!m_document||!m_canvas)return;
    if(!m_canvas->resolvePendingTransform())return;m_canvas->finishStroke();m_canvas->maskTool()->finish();
    const auto *layer=activeLayer(m_document);if(!layer)return;m_draft=displayedEffects();m_editLayerId=layer->id;m_editFrame=m_document->currentFrame();
    m_editing=m_document->beginEffectsEdit(m_editLayerId,"Layer effects",true);if(m_editing)emit pendingEditsChanged(true);
}
void EffectsPanel::beginMaskValue(){
    if(m_refreshing||m_editing)return;beginValue();if(m_editing)m_maskDraft=displayedMask();
}
void EffectsPanel::previewMaskValue(int field,double value){
    if(m_refreshing||!m_editing||!m_document||!m_document->effectsEditActive())return;
    switch(field){case 0:m_maskDraft.position.setX(value);break;case 1:m_maskDraft.position.setY(value);break;case 2:m_maskDraft.scale.setX(value/100);break;case 3:m_maskDraft.scale.setY(value/100);break;case 4:m_maskDraft.rotation=value;break;case 5:m_maskDraft.opacity=value/100;break;case 6:m_maskDraft.feather=value;break;default:return;}
    m_document->previewLayerMask(m_maskDraft);
}
void EffectsPanel::previewValue(int field,double value){
    if(m_refreshing||!m_editing||!m_document||!m_document->effectsEditActive())return;
    switch(field){case 0:m_draft.blurRadius=value;break;case 1:m_draft.shadowOffset.setX(value);break;case 2:m_draft.shadowOffset.setY(value);break;
    case 3:m_draft.shadowRadius=value;break;case 4:m_draft.shadowOpacity=value/100;break;case 5:m_draft.glowRadius=value;break;case 6:m_draft.glowOpacity=value/100;break;
    case 7:m_draft.exposure=value;break;case 8:m_draft.contrast=value;break;case 9:m_draft.saturation=value;break;case 10:m_draft.hue=value;break;default:return;}
    m_document->previewLayerEffects(m_draft);m_offset->setOffset(m_draft.shadowOffset);
}
void EffectsPanel::finishValue(){
    if(m_refreshing)return;if(!m_editing){refresh();return;}m_editing=false;if(m_document)m_document->commitEffectsEdit();emit pendingEditsChanged(false);refresh();
}
void EffectsPanel::finishEditing(){
    if(m_resolving)return;const QScopedValueRollback<bool> guard(m_resolving,true);
    for(auto *value:m_values)if(value)value->commitEditing();for(auto *value:m_maskValues)if(value)value->commitEditing();for(auto *value:m_maskPlacementValues)if(value)value->commitEditing();m_offset->stop();finishValue();
}
void EffectsPanel::cancelEditing(){
    if(m_resolving)return;const QScopedValueRollback<bool> guard(m_resolving,true);
    {const QScopedValueRollback<bool> refreshing(m_refreshing,true);for(auto *value:m_values)if(value)value->cancelEditing();for(auto *value:m_maskValues)if(value)value->cancelEditing();for(auto *value:m_maskPlacementValues)if(value)value->cancelEditing();m_offset->stop();}
    const bool pending=m_editing;m_editing=false;if(pending&&m_document)m_document->cancelEffectsEdit();if(pending)emit pendingEditsChanged(false);refresh();
}
void EffectsPanel::changeEffect(const std::function<void(LayerEffects &)> &change){
    if(m_refreshing||!prepareAction())return;const auto *layer=activeLayer(m_document);if(!layer)return;
    const auto id=layer->id;auto effects=displayedEffects();change(effects);m_document->setLayerEffects(id,effects,true);refresh();
}
void EffectsPanel::pickColor(bool shadow){
    if(!prepareAction())return;beginValue();if(!m_editing)return;
    QColorDialog dialog(shadow?m_draft.shadowColor:m_draft.glowColor,this);dialog.setObjectName("effectColorDialog");dialog.setWindowTitle(shadow?"Shadow color":"Glow color");dialog.setOption(QColorDialog::DontUseNativeDialog);
    connect(&dialog,&QColorDialog::currentColorChanged,this,[this,shadow](QColor color){if(!m_editing||!color.isValid())return;color.setAlpha(255);if(shadow)m_draft.shadowColor=color;else m_draft.glowColor=color;m_document->previewLayerEffects(m_draft);});
    if(dialog.exec()==QDialog::Accepted)finishValue();else cancelEditing();
}
void EffectsPanel::refreshPreviewStatus(){
    if(!m_document||!m_canvas)return;
    QString text=m_document->effectsEditReason();
    if(text.isEmpty()&&m_canvas->animationPlayback())text="Stop playback to edit effects.";
    if(text.isEmpty()&&m_canvas->effectsPreviewPending())text="Updating preview…";
    m_status->setText(text);m_status->setToolTip(text);m_status->setAccessibleName(text);
}
void EffectsPanel::refresh(){
    if(m_refreshing||!m_document||!m_canvas)return;
    auto *layer=activeLayer(m_document);
    if(m_editing&&(!m_document->effectsEditActive()||!layer||layer->id!=m_editLayerId||m_document->currentFrame()!=m_editFrame)){
        // An external context transition owns resolution. Never reinterpret an
        // old draft on the newly selected layer or after an undo/load.
        cancelEditing();layer=activeLayer(m_document);
    }
    const QScopedValueRollback<bool> guard(m_refreshing,true);const bool opened=layer;
    const QString name=layer?layer->name:"No layer";m_target->setText(name);m_target->setToolTip(name);m_target->setAccessibleName(name);
    QString reason=m_document->effectsEditReason();if(reason.isEmpty()&&m_canvas->animationPlayback())reason="Stop playback to edit effects.";
    refreshPreviewStatus();const bool editable=opened&&reason.isEmpty();
    const auto effects=displayedEffects();const bool adjustment=layer&&layer->adjustment;
    m_master->setEnabled(editable);m_master->setChecked(effects.enabled);
    bool canAdd=opened&&!m_canvas->animationPlayback();
    if(layer){const auto &shot=m_document->activeShot();const auto parent=layer->group?layer->id:layer->parentId;
        canAdd=canAdd&&!layerIsAnnotation(shot.layers,m_document->activeLayer())&&shot.layers.size()<(shot.animationTracks.isEmpty()?maxDrawingLayers:maxAnimationLayers)
            &&(parent.isEmpty()||m_document->effectsEditReason(parent).isEmpty());}
    m_addAdjustment->setEnabled(canAdd);
    for(int i=1;i<4;++i)m_tabs->setTabEnabled(i,!adjustment);if(adjustment&&m_tabs->currentIndex()>0&&m_tabs->currentIndex()<4)m_tabs->setCurrentIndex(4);
    const bool enabled[]={effects.blurEnabled,effects.shadowEnabled,effects.glowEnabled,effects.colorEnabled};
    for(int i=0;i<4;++i){m_enabled[i]->setEnabled(editable&&effects.enabled&&(!adjustment||i==3));m_enabled[i]->setChecked(enabled[i]);m_effectBodies[i]->setEnabled(m_enabled[i]->isEnabled()&&enabled[i]);m_effectBodies[i]->setVisible(enabled[i]);}
    if(!m_editing){const double values[]={effects.blurRadius,effects.shadowOffset.x(),effects.shadowOffset.y(),effects.shadowRadius,effects.shadowOpacity*100,effects.glowRadius,effects.glowOpacity*100,effects.exposure,effects.contrast,effects.saturation,effects.hue};for(int i=0;i<11;++i)m_values[i]->setValueSilently(values[i]);m_offset->setOffset(effects.shadowOffset);}
    colorChip(m_shadowColor,effects.shadowColor);colorChip(m_glowColor,effects.glowColor);
    const auto mask=displayedMask();const bool hasMask=layer&&layer->mask.has_value(),maskEnabled=hasMask&&mask.enabled;auto *tool=m_canvas->maskTool();
    m_maskAdd->setVisible(!hasMask);m_maskSelection->setVisible(!hasMask);m_maskEnabled->setVisible(hasMask);m_maskRemove->setVisible(hasMask);m_maskControls->setVisible(hasMask&&m_maskPropertyTabs->currentIndex()==0);m_maskPlacement->setVisible(hasMask&&m_maskPropertyTabs->currentIndex()==1);m_maskPropertyTabs->setVisible(hasMask);
    m_maskAdd->setEnabled(editable&&!hasMask);m_maskSelection->setEnabled(editable&&!hasMask&&m_document->rasterSelection().active&&!m_document->rasterSelection().path.isEmpty());
    for(auto *action:{m_maskEnabled,m_maskRemove,m_maskInvert,m_maskRevealAll,m_maskHideAll})action->setEnabled(editable&&hasMask);
    m_maskEnabled->setChecked(maskEnabled);m_maskPaint->setEnabled(editable&&maskEnabled);m_maskPaint->setChecked(editable&&maskEnabled&&tool->enabled());m_maskMode->setEnabled(editable&&maskEnabled);m_maskMode->setCurrentIndex(tool->revealing()?0:1);m_maskBrush->setEnabled(editable&&maskEnabled);
    m_maskValues[0]->setValueSilently(tool->size());m_maskValues[1]->setValueSilently(tool->strength()*100);m_maskValues[2]->setValueSilently(tool->hardness()*100);
    m_maskPlacement->setEnabled(editable&&hasMask);m_maskPresentationInvert->setChecked(mask.inverted);m_maskPresentationInvert->setText(mask.inverted?"On":"Off");m_maskInvert->setChecked(mask.inverted);
    for(int i=0;i<7;++i){
        m_maskPlacementValues[i]->setEnabled(editable&&hasMask&&(!keyframePropertyNeedsPositionUnlock(maskFields[i])||!layerPositionLocked(m_document->activeShot().layers,m_document->activeLayer())));
        if(!m_editing)m_maskPlacementValues[i]->setValueSilently(maskDisplay(maskFields[i],layer?evaluateProperty(m_document->activeShot(),layer->id,maskFields[i],m_document->currentFrame()):keyframeDefaultValue(maskFields[i])));
    }
    refreshKeyButtons();refreshThumbnail();
}
void EffectsPanel::refreshThumbnail(){
    const auto *layer=activeLayer(m_document);const auto bounds=m_document->drawingBounds();
    quint64 key=layer?qHashMulti(0,layer->id,bounds.x(),bounds.y(),bounds.width(),bounds.height()):1;
    if(layer&&layer->mask){key=qHashMulti(key,layer->mask->enabled,layer->mask->defaultValue,layer->mask->tiles.size());for(auto it=layer->mask->tiles.cbegin();it!=layer->mask->tiles.cend();++it)key=qHashMulti(key,it.key().first,it.key().second,it->cacheKey());}
    if(key==m_thumbnailKey)return;m_thumbnailKey=key;
    QPixmap pixmap(176,140);pixmap.setDevicePixelRatio(2);pixmap.fill(Qt::transparent);
    {QPainter painter(&pixmap);painter.setPen(QColor(ui::border));painter.setBrush(QColor(ui::base));painter.drawRoundedRect(QRectF(1,1,86,68),3,3);
        if(layer&&layer->mask&&!bounds.isEmpty()){
            QSizeF fitted=bounds.size();fitted.scale(80,62,Qt::KeepAspectRatio);const QRectF target(QPointF((88-fitted.width())/2,(70-fitted.height())/2),fitted);
            const auto &mask=*layer->mask;painter.setClipRect(target);painter.fillRect(target,QColor(mask.defaultValue,mask.defaultValue,mask.defaultValue));
            painter.translate(target.topLeft());painter.scale(target.width()/bounds.width(),target.height()/bounds.height());painter.translate(-bounds.topLeft());
            for(auto it=mask.tiles.cbegin();it!=mask.tiles.cend();++it){const QRectF tile(it.key().first*tileSize,it.key().second*tileSize,tileSize,tileSize);if(tile.intersects(bounds))painter.drawImage(tile,it.value());}
        }else{painter.setPen(QColor(ui::muted));painter.drawText(QRectF(1,1,86,68),Qt::AlignCenter,"No mask");}}
    m_thumbnail->setPixmap(pixmap);
}
} // namespace toon
