#include "drawing_tool_panel.h"
#include "animation.h"
#include "drawing_tool_controller.h"
#include "selection_extras.h"
#include "icons.h"
#include "ui_metrics.h"
#include "value_slider.h"
#include "property_row.h"
#include "ui_theme.h"
#include <QBoxLayout>
#include <QButtonGroup>
#include <QColorDialog>
#include <QComboBox>
#include <QFontComboBox>
#include <QFontInfo>
#include <QLabel>
#include <QPainter>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QPlainTextEdit>
#include <QSignalBlocker>
#include <QScopedValueRollback>
#include <QResizeEvent>
#include <QTimer>
#include <memory>
#include <QStackedLayout>
#include <QTabBar>
#include <QToolButton>
#include <algorithm>
#include <cmath>

namespace toon {
namespace {
void showFontFamily(QFontComboBox *control,const QString &family){
    const auto resolved=QFontInfo(QFont(family)).family();control->setCurrentFont(QFont(resolved));
    // macOS system-font aliases can render correctly without appearing in the
    // installed family list. Keep their saved name visible instead of the
    // combo's unrelated first entry.
    if(control->findText(resolved)<0)control->setEditText(family);
    control->setProperty("savedFontFamily",family);
    control->setProperty("displayedFontFamily",control->currentText());
}
QString selectedFontFamily(QFontComboBox *control){
    // Showing a local substitute must not rewrite the saved font when only
    // the text, size or another property changes on a different computer.
    return control->currentText()==control->property("displayedFontFamily").toString()
        ? control->property("savedFontFamily").toString() : control->currentText();
}
// Only the visible inspector should contribute its height. Otherwise the text
// editor makes even the two-row bucket inspector scroll in a short dock.
// The layout must report this itself: QWidgetItem reads an installed layout's
// height-for-width before the widget's own override.
class DrawingPropertyLayout final : public QStackedLayout {
public:
    explicit DrawingPropertyLayout(QWidget *parent) : QStackedLayout(parent) { setSizeConstraint(QLayout::SetNoConstraint); }
    QSize minimumSize() const override {
        return currentWidget() ? currentWidget()->minimumSizeHint().expandedTo(currentWidget()->minimumSize()) : QSize();
    }
    QSize sizeHint() const override { return currentWidget() ? currentWidget()->sizeHint() : QSize(); }
    bool hasHeightForWidth() const override { return true; }
    int heightForWidth(int width) const override {
        if (!currentWidget()) return 0;
        return currentWidget()->hasHeightForWidth() ? currentWidget()->heightForWidth(width)
                                                  : currentWidget()->sizeHint().height();
    }
};
}
class DrawingPropertyPages final : public QWidget {
public:
    DrawingPropertyPages() : m_layout(new DrawingPropertyLayout(this)) {
        auto policy = QSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        policy.setHeightForWidth(true);
        setSizePolicy(policy);
        connect(m_layout, &QStackedLayout::currentChanged, this, [this] { updateGeometry(); });
    }
    void addWidget(QWidget *widget) { m_layout->addWidget(widget); }
    void setCurrentIndex(int index) { m_layout->setCurrentIndex(index); }
private:
    DrawingPropertyLayout *m_layout;
};
namespace {
QToolButton *button(QBoxLayout *layout,const char *icon,const QString &name,const QString&id={}){
    auto *b=new QToolButton;b->setIcon(appIcon(icon));b->setToolTip(name);b->setAccessibleName(name);b->setObjectName(id);b->setProperty("compactDrawingAction",true);b->setFixedSize(ui::metrics::actionTarget,ui::metrics::actionTarget);b->setIconSize({ui::metrics::actionIcon,ui::metrics::actionIcon});layout->addWidget(b,0,Qt::AlignLeft);return b;
}
QHBoxLayout *propertyActions(QVBoxLayout *layout) {
    auto *row = new QHBoxLayout;
    row->setSpacing(ui::metrics::controlGap); row->setAlignment(Qt::AlignLeft);
    layout->addLayout(row); return row;
}
QTabBar *tabs(QVBoxLayout *layout,const QList<QPair<QString,QString>>&items,const QString&id){
    auto *bar=new QTabBar;bar->setProperty("compactDrawingTabs",true);bar->setObjectName(id);bar->setIconSize({ui::metrics::tabIcon,ui::metrics::tabIcon});bar->setExpanding(false);bar->setDrawBase(false);bar->setUsesScrollButtons(true);bar->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Fixed);
    for(auto item:items){int n=bar->addTab(appIcon(item.first),"");bar->setTabToolTip(n,item.second);bar->setAccessibleTabName(n,item.second);}layout->addWidget(bar);return bar;
}
ValueSlider *scalar(QVBoxLayout*layout,const QString&name,const QString&id,qreal low,qreal high,qreal value,const QString&suffix={},int decimals=0){
    auto *v=new ValueSlider(name,low,high,value,decimals,suffix);
    v->setObjectName(id);v->setToolTip(name+": drag the slider or enter an exact value");
    auto *row=new PropertyRow(name,v);layout->addWidget(row);
    v->setProperty("fieldRow",QVariant::fromValue<QObject*>(row));v->setProperty("fieldLabel",QVariant::fromValue<QObject*>(row->labelWidget()));return v;
}
void showScalar(ValueSlider *control,bool visible){qobject_cast<QWidget*>(control->property("fieldRow").value<QObject*>())->setVisible(visible);}
void colorButton(QToolButton*b,const QColor &color){b->setIcon({});b->setStyleSheet(QString("QToolButton { background: %1; border: 1px solid %2; }").arg(color.name(QColor::HexArgb),ui::border));}
}
GradientStopsWidget::GradientStopsWidget(QWidget*parent):QWidget(parent){setObjectName("gradientStops");setMinimumHeight(76);setFocusPolicy(Qt::StrongFocus);setAccessibleName("Gradient stops");setToolTip("Click to add a stop; drag to position; double-click to choose color; Delete to remove");}
void GradientStopsWidget::setStops(const QGradientStops&stops){m_stops=stops;std::sort(m_stops.begin(),m_stops.end(),[](auto a,auto b){return a.first<b.first;});m_selected=qBound(0,m_selected,int(m_stops.size())-1);update();}
qreal GradientStopsWidget::position(qreal x)const{return qBound(0.,(x-12)/qMax(1,width()-24),1.);}
int GradientStopsWidget::hit(QPointF p)const{for(int i=0;i<m_stops.size();++i)if(qAbs(p.x()-(12+m_stops[i].first*(width()-24)))<9)return i;return -1;}
void GradientStopsWidget::paintEvent(QPaintEvent*){QPainter p(this);p.setRenderHint(QPainter::Antialiasing);QRectF bar(12,12,width()-24,32);QLinearGradient g(bar.topLeft(),bar.topRight());g.setStops(m_stops);p.setPen(QColor(ui::border));p.setBrush(g);p.drawRoundedRect(bar,4,4);for(int i=0;i<m_stops.size();++i){qreal x=12+m_stops[i].first*(width()-24);p.setPen(QPen(QColor(i==m_selected?ui::focus:ui::muted),i==m_selected?2:1));p.setBrush(m_stops[i].second);p.drawPolygon(QPolygonF{QPointF(x,43),QPointF(x-6,51),QPointF(x-6,65),QPointF(x+6,65),QPointF(x+6,51)});}}
void GradientStopsWidget::sortSelected(qreal pos){auto color=m_stops[m_selected].second;m_stops.removeAt(m_selected);m_selected=0;while(m_selected<m_stops.size()&&m_stops[m_selected].first<pos)++m_selected;m_stops.insert(m_selected,{pos,color});update();emit stopsChanged();}
void GradientStopsWidget::mousePressEvent(QMouseEvent*e){setFocus();m_selected=hit(e->position());if(e->button()==Qt::RightButton){removeSelected();return;}if(e->button()!=Qt::LeftButton)return;if(m_selected<0&&m_stops.size()<32){qreal pos=position(e->position().x());QColor color=Qt::gray;for(int i=1;i<m_stops.size();++i)if(pos<=m_stops[i].first){auto a=m_stops[i-1],b=m_stops[i];qreal t=qBound(0.,(pos-a.first)/qMax(.00001,b.first-a.first),1.);color=QColor::fromRgbF(a.second.redF()*(1-t)+b.second.redF()*t,a.second.greenF()*(1-t)+b.second.greenF()*t,a.second.blueF()*(1-t)+b.second.blueF()*t,a.second.alphaF()*(1-t)+b.second.alphaF()*t);break;}m_stops.append({pos,color});m_selected=m_stops.size()-1;sortSelected(pos);}m_drag=m_selected>=0;update();}
void GradientStopsWidget::mouseMoveEvent(QMouseEvent*e){if(m_drag && (e->buttons()&Qt::LeftButton))sortSelected(position(e->position().x()));}
void GradientStopsWidget::mouseReleaseEvent(QMouseEvent*){m_drag=false;}
void GradientStopsWidget::mouseDoubleClickEvent(QMouseEvent*e){int i=hit(e->position());if(i<0)return;m_selected=i;m_drag=false;auto color=QColorDialog::getColor(m_stops[i].second,this,"Stop color",QColorDialog::ShowAlphaChannel);if(color.isValid())setSelectedColor(color);}
void GradientStopsWidget::setSelectedColor(const QColor&color){if(m_selected<0||m_selected>=m_stops.size()||!color.isValid())return;m_stops[m_selected].second=color;update();emit stopsChanged();}
void GradientStopsWidget::removeSelected(){if(m_stops.size()<=2||m_selected<0||m_selected>=m_stops.size())return;m_stops.removeAt(m_selected);m_selected=qMin(m_selected,int(m_stops.size())-1);update();emit stopsChanged();}
void GradientStopsWidget::keyPressEvent(QKeyEvent*e){if(e->key()==Qt::Key_Delete||e->key()==Qt::Key_Backspace)removeSelected();else if(m_selected>=0&&(e->key()==Qt::Key_Left||e->key()==Qt::Key_Right))sortSelected(qBound(0.,m_stops[m_selected].first+(e->key()==Qt::Key_Left?-.01:.01),1.));else QWidget::keyPressEvent(e);}

DrawingToolPanel::DrawingToolPanel(Canvas *canvas,QWidget*parent):QScrollArea(parent),m_canvas(canvas),m_pages(new DrawingPropertyPages){
    setObjectName("drawingToolProperties");setProperty("compactDrawingPanel",true);setWidgetResizable(true);setFrameShape(QFrame::NoFrame);setMinimumWidth(220);setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto*content=new QWidget;auto*outer=new QVBoxLayout(content);outer->setContentsMargins(ui::metrics::panelMargin,ui::metrics::panelMargin,ui::metrics::panelMargin,ui::metrics::panelMargin);outer->setSpacing(ui::metrics::controlGap);outer->addWidget(m_pages);outer->addStretch();setWidget(content);
    auto *c=canvas->drawingTools();auto *doc=canvas->document();
    auto page=[&]{auto*w=new QWidget;auto*l=new QVBoxLayout(w);l->setContentsMargins(0,0,0,0);l->setSpacing(ui::metrics::controlGap);m_pages->addWidget(w);return l;};
    // 0: color sampling / flood regions.
    auto*region=page();auto*source=tabs(region,{{"layers","Sample visible layers"},{"image","Sample active layer"},{"pin","Sample reference layer"}},"sampleSource");
    auto *reference=new QComboBox;reference->setObjectName("fillReferenceLayer");reference->setAccessibleName("Reference layer");reference->setToolTip("Layer used to find color boundaries");reference->setMinimumWidth(100);reference->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Fixed);auto *referenceRow=new PropertyRow("Layer",reference);region->addWidget(referenceRow);
    reference->setProperty("layersDirty",true);
    auto rebuildLayers=[doc,reference,c]{
        QStringList state,drawings;
        if(doc->isOpen()) {
            const auto &shot=doc->activeShot();
            for(const auto &track:shot.animationTracks){drawings.append(track.id);const auto *drawing=drawingAtFrame(track,doc->currentFrame());drawings.append(drawing?drawing->id:QString());}
            if(!reference->property("layersDirty").toBool()&&reference->property("displayDrawings").toStringList()==drawings)return;
            const auto present=activeLayerIds(shot,doc->currentFrame());
            for(const auto &layer:shot.layers)if(!layer.group&&present.contains(layer.id))state<<layer.id<<layer.name;
        }
        reference->setProperty("layersDirty",false);reference->setProperty("displayDrawings",drawings);
        if(reference->property("layerState").toStringList()==state)return;
        reference->setProperty("layerState",state);QSignalBlocker block(reference);reference->clear();
        for(int i=0;i+1<state.size();i+=2)reference->addItem(state[i+1],state[i]);
        const int selected=reference->findData(c->fillSettings.referenceLayerId);
        reference->setCurrentIndex(selected>=0?selected:reference->count()?0:-1);
        c->fillSettings.referenceLayerId=reference->currentData().toString();
    };
    connect(doc,&Document::selectionChanged,reference,rebuildLayers);
    connect(doc,&Document::changed,reference,[doc,reference,rebuildLayers]{reference->setProperty("layersDirty",true);if(!doc->editingGestureActive()&&!doc->layerPropertyEditActive())rebuildLayers();});
    connect(doc,&Document::frameChanged,reference,rebuildLayers);connect(doc,&Document::animationSelectionChanged,reference,rebuildLayers);
    rebuildLayers();referenceRow->setVisible(false);
    connect(reference,&QComboBox::currentIndexChanged,this,[=]{c->fillSettings.referenceLayerId=reference->currentData().toString();});
    connect(source,&QTabBar::currentChanged,this,[=](int index){c->fillSettings.source=index==0?RasterSampleSource::AllLayers:index==1?RasterSampleSource::ActiveLayer:RasterSampleSource::ReferenceLayer;referenceRow->setVisible(index==2);});
    auto*tolerance=scalar(region,"Tolerance","fillTolerance",0,255,c->fillSettings.tolerance);connect(tolerance,&ValueSlider::valueChanged,this,[=](qreal v){c->fillSettings.tolerance=qRound(v);});
    auto*gap=scalar(region,"Close gaps","fillGapClosing",0,20,c->fillSettings.gapClosing," px");connect(gap,&ValueSlider::valueChanged,this,[=](qreal v){c->fillSettings.gapClosing=qRound(v);});
    auto*regionSelection=new SelectionExtras(canvas);region->addWidget(regionSelection);region->addStretch();
    // 1: gradient stop editor.
    auto*gradient=page();auto*gradientMode=tabs(gradient,{{featureIconName(FeatureIcon::LinearGradient),"Linear gradient"},{featureIconName(FeatureIcon::RadialGradient),"Radial gradient"}},"gradientMode");connect(gradientMode,&QTabBar::currentChanged,this,[=](int i){c->radialGradient=i==1;});
    auto*stops=new GradientStopsWidget;stops->setStops(c->gradientStops);gradient->addWidget(stops);connect(stops,&GradientStopsWidget::stopsChanged,this,[=]{c->gradientStops=stops->stops();});
    auto*stopActions=propertyActions(gradient);auto*fg=button(stopActions,"pipette","Use foreground for selected stop","gradientForeground");auto*color=button(stopActions,"palette","Choose stop color","gradientColor");auto*remove=button(stopActions,"trash","Remove selected stop","gradientRemoveStop");auto*reverse=button(stopActions,"arrow-left-right","Reverse gradient","gradientReverse");
    connect(fg,&QToolButton::clicked,this,[=]{stops->setSelectedColor(canvas->brush.color);});connect(color,&QToolButton::clicked,this,[=, this]{int i=stops->selectedIndex();if(i<0)return;auto color=QColorDialog::getColor(stops->stops()[i].second,this,"Stop color",QColorDialog::ShowAlphaChannel);stops->setSelectedColor(color);});connect(remove,&QToolButton::clicked,stops,&GradientStopsWidget::removeSelected);connect(reverse,&QToolButton::clicked,this,[=]{auto reversed=stops->stops();for(auto &stop:reversed)stop.first=1-stop.first;stops->setStops(reversed);c->gradientStops=stops->stops();});gradient->addStretch();
    // 2: shape / editable vector style.
    auto*shape=page();auto *paint=propertyActions(shape);auto*stroke=button(paint,"pen-tool","Stroke","shapeStroke");stroke->setCheckable(true);stroke->setChecked(true);auto*fill=button(paint,"paint-bucket","Fill","shapeFill");fill->setCheckable(true);auto*fillChip=button(paint,"palette","Fill color","shapeFillColor");colorButton(fillChip,c->fillColor);
    connect(stroke,&QToolButton::toggled,this,[=](bool on){c->shapeStroke=on;c->applyObjectStyle();});connect(fill,&QToolButton::toggled,this,[=](bool on){c->shapeFill=on;c->applyObjectStyle();});connect(fillChip,&QToolButton::clicked,this,[=, this]{auto color=QColorDialog::getColor(c->fillColor,this,"Fill color",QColorDialog::ShowAlphaChannel);if(color.isValid()){c->fillColor=color;colorButton(fillChip,color);c->applyObjectStyle();}});
    auto*width=scalar(shape,"Stroke","shapeStrokeWidth",.1,1024,c->strokeWidth," px",1);connect(width,&ValueSlider::valueChanged,this,[=](qreal v){c->strokeWidth=v;});connect(width,&ValueSlider::editingFinished,c,&DrawingToolController::applyObjectStyle);
    auto*pathActions=propertyActions(shape);auto*close=button(pathActions,"pentagon","Close path","pathClosed");close->setCheckable(true);auto*finish=button(pathActions,"check","Finish path (Enter); Shift+Enter closes","finishDrawingPath");auto*cancel=button(pathActions,"x","Cancel path (Esc)","cancelDrawingPath");connect(close,&QToolButton::toggled,this,[=](bool on){c->closedPath=on;c->applyObjectStyle();});connect(finish,&QToolButton::clicked,this,[=]{c->finishPath(c->closedPath);canvas->setFocus();});connect(cancel,&QToolButton::clicked,c,&DrawingToolController::cancel);
    auto*strokeColor=button(shape,"pipette","Use foreground color for selected vector","vectorForeground");connect(strokeColor,&QToolButton::clicked,c,&DrawingToolController::applyObjectStyle);shape->addStretch();
    // 3: retouch / vector eraser.
    auto*retouch=page();auto*retouchMode=tabs(retouch,{{"hand","Smudge"},{"blend","Mixer"},{featureIconName(FeatureIcon::Blur),"Blur"},{"triangle","Sharpen"},{"sun","Dodge"},{"flame","Burn"}},"retouchMode");connect(retouchMode,&QTabBar::currentChanged,this,[=](int i){c->retouchSettings.mode=static_cast<RetouchMode>(i);});
    auto*size=scalar(retouch,"Size","retouchSize",1,512,c->retouchSettings.size," px",1);connect(size,&ValueSlider::valueChanged,this,[=](qreal v){c->retouchSettings.size=v;c->notifySettings();});auto*strength=scalar(retouch,"Strength","retouchStrength",1,100,c->retouchSettings.strength*100," %");connect(strength,&ValueSlider::valueChanged,this,[=](qreal v){c->retouchSettings.strength=v/100;});
    auto*eraseMode=tabs(retouch,{{"spline","Erase whole vector stroke"},{"scissors","Erase intersecting Bézier segments"}},"vectorEraseMode");connect(eraseMode,&QTabBar::currentChanged,this,[=](int i){c->eraseSegments=i==1;});retouch->addStretch();
    // 4: text contents and layout.
    auto*text=page();auto*editor=new QPlainTextEdit;editor->setObjectName("artworkText");editor->setAccessibleName("Artwork text");editor->installEventFilter(this);editor->setPlainText(c->textStyle.text);editor->setMinimumHeight(76);editor->setMaximumHeight(96);editor->setMinimumWidth(100);text->addWidget(editor);
    auto*font=new QFontComboBox;font->setObjectName("artworkFont");font->setAccessibleName("Font family");font->setMinimumWidth(100);font->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Fixed);text->addWidget(new PropertyRow("Font",font));showFontFamily(font,c->textStyle.fontFamily);
    auto*fontSize=scalar(text,"Size","artworkFontSize",1,1024,32," px",1);auto*textActions=propertyActions(text);auto*bold=button(textActions,"bold","Bold","artworkBold");bold->setCheckable(true);auto*italic=button(textActions,"italic","Italic","artworkItalic");italic->setCheckable(true);
    auto*alignment=new QComboBox;alignment->setMinimumWidth(68);alignment->setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Fixed);alignment->setObjectName("artworkAlignment");alignment->setAccessibleName("Text alignment");alignment->addItems({"Left","Center","Right"});textActions->addWidget(alignment);
    auto*wrap=scalar(text,"Wrap width","artworkTextWidth",0,10000,0," px");wrap->setToolTip("Zero keeps each line unwrapped");auto*leading=scalar(text,"Line spacing","artworkLineSpacing",.5,4,1.2," ×",2);leading->setSingleStep(.05);
    auto updatingText=std::make_shared<bool>(false);
    auto updateTextDraft=[=]{if(*updatingText)return;const QScopedValueRollback<bool> guard(*updatingText,true);c->textStyle.text=editor->toPlainText();c->textStyle.fontFamily=selectedFontFamily(font);c->textStyle.fontSize=fontSize->value();c->textStyle.bold=bold->isChecked();c->textStyle.italic=italic->isChecked();c->textStyle.alignment=alignment->currentIndex()==0?Qt::AlignLeft:alignment->currentIndex()==1?Qt::AlignHCenter:Qt::AlignRight;c->textStyle.textWidth=wrap->value();c->textStyle.lineSpacing=leading->value();if(const auto selected=c->selectedObject();selected&&selected->kind==ObjectKind::Text){c->textStyle.fill=selected->fill;c->textStyle.stroke=selected->stroke;}else{c->textStyle.fill=canvas->brush.color;c->textStyle.stroke=QColor();}editor->setProperty("draftDirty",true);const auto draft=c->textStyle;if(const auto selected=c->selectedObject();selected&&selected->kind==ObjectKind::Text){if(c->textEditing()||c->beginTextEdit(false)){c->textStyle=draft;c->previewTextStyle();}}};
    connect(editor,&QPlainTextEdit::textChanged,this,updateTextDraft);connect(font,&QFontComboBox::currentFontChanged,this,updateTextDraft);connect(fontSize,&ValueSlider::valueChanged,this,updateTextDraft);connect(bold,&QToolButton::toggled,this,updateTextDraft);connect(italic,&QToolButton::toggled,this,updateTextDraft);connect(alignment,&QComboBox::currentIndexChanged,this,updateTextDraft);connect(wrap,&ValueSlider::valueChanged,this,updateTextDraft);connect(leading,&ValueSlider::valueChanged,this,updateTextDraft);
    for(auto *field:{fontSize,wrap,leading}){connect(field,&ValueSlider::editingFinished,this,[=]{c->finishTextEdit();});connect(field,&ValueSlider::editingCancelled,this,[=]{editor->setProperty("draftDirty",false);c->finishTextEdit(true);c->notifySettings();});}
    connect(font,&QFontComboBox::currentFontChanged,this,[=]{c->finishTextEdit();});connect(bold,&QToolButton::toggled,this,[=]{c->finishTextEdit();});connect(italic,&QToolButton::toggled,this,[=]{c->finishTextEdit();});connect(alignment,&QComboBox::currentIndexChanged,this,[=]{c->finishTextEdit();});
    auto*apply=button(text,"check","Apply text changes","applyArtworkText");connect(apply,&QToolButton::clicked,this,[=]{c->textStyle.text=editor->toPlainText();c->textStyle.fontFamily=selectedFontFamily(font);c->textStyle.fontSize=fontSize->value();c->textStyle.bold=bold->isChecked();c->textStyle.italic=italic->isChecked();c->textStyle.alignment=alignment->currentIndex()==0?Qt::AlignLeft:alignment->currentIndex()==1?Qt::AlignHCenter:Qt::AlignRight;c->textStyle.textWidth=wrap->value();c->textStyle.lineSpacing=leading->value();c->textStyle.fill=canvas->brush.color;c->textStyle.stroke=QColor();editor->setProperty("draftDirty",false);c->applyObjectStyle();canvas->setFocus();});text->addStretch();
    // 5: selection shape, feathering and saved selections.
    auto*selection=page();selection->addWidget(new SelectionExtras(canvas));auto*end=button(selection,"check","Finish polygon selection (Enter)","finishPolygonSelection");connect(end,&QToolButton::clicked,this,[=]{c->finishPath(true);canvas->setFocus();});selection->addStretch();
    // 6: reversible spatial transforms.
    auto*transform=page();auto*mode=tabs(transform,{{"square-dashed-mouse-pointer","Distort / perspective corners"},{featureIconName(FeatureIcon::Warp),"Warp mesh"}},"advancedTransformMode");connect(mode,&QTabBar::currentChanged,this,[=](int i){c->warpMode=i==1;c->notifySettings();});
    auto*targets=propertyActions(transform);auto*contentTarget=button(targets,"layers","Transform selected layers / folder","transformLayerTarget");auto*boundaryTarget=button(targets,"square-dashed","Transform selection boundary only","transformBoundaryTarget");contentTarget->setCheckable(true);boundaryTarget->setCheckable(true);auto*targetGroup=new QButtonGroup(this);targetGroup->addButton(contentTarget);targetGroup->addButton(boundaryTarget);contentTarget->setChecked(true);
    connect(contentTarget,&QToolButton::clicked,this,[=]{c->beginAdvanced({},false);});connect(boundaryTarget,&QToolButton::clicked,this,[=]{c->beginAdvanced({},true);});
    auto*flipRow=propertyActions(transform);auto*horizontal=button(flipRow,"arrow-left-right","Flip horizontally","flipArtworkHorizontal");auto*vertical=button(flipRow,"arrow-down","Flip vertically","flipArtworkVertical");connect(horizontal,&QToolButton::clicked,this,[=]{if(!c->transform()->active())c->beginAdvanced({},c->selectionBoundary);c->transform()->flip(true);});connect(vertical,&QToolButton::clicked,this,[=]{if(!c->transform()->active())c->beginAdvanced({},c->selectionBoundary);c->transform()->flip(false);});
    auto*skewX=scalar(transform,"Skew X","artworkSkewX",-75,75,0," °",1);auto*skewY=scalar(transform,"Skew Y","artworkSkewY",-75,75,0," °",1);auto*skewApply=button(transform,"spline","Preview skew","previewArtworkSkew");connect(skewApply,&QToolButton::clicked,this,[=]{if(!c->transform()->active())c->beginAdvanced({},c->selectionBoundary);c->transform()->skew(std::tan(skewX->value()*M_PI/180),std::tan(skewY->value()*M_PI/180));});
    // Completion remains outside the scrolling settings, including short docks.
    m_transformFooter=new QWidget(this);m_transformFooter->setObjectName("advancedTransformFooter");
    auto*commit=new QHBoxLayout(m_transformFooter);commit->setContentsMargins(ui::metrics::panelMargin,ui::metrics::controlGap,ui::metrics::panelMargin,ui::metrics::panelMargin);commit->setSpacing(ui::metrics::controlGap);
    auto*applyTransform=button(commit,"check","Apply transform (Enter)","applyAdvancedTransform");
    auto*cancelTransform=button(commit,"x","Cancel transform (Esc)","cancelAdvancedTransform");
    applyTransform->setText("Apply");cancelTransform->setText("Cancel");
    for(auto *action:{applyTransform,cancelTransform}){
        action->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);action->setMaximumSize(QWIDGETSIZE_MAX,QWIDGETSIZE_MAX);
        action->setMinimumSize(0,ui::metrics::actionTarget);action->setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Preferred);
        commit->setAlignment(action,{});
    }
    connect(applyTransform,&QToolButton::clicked,this,[=]{c->transform()->apply();});connect(cancelTransform,&QToolButton::clicked,c,&DrawingToolController::cancel);transform->addStretch();
    connect(c,&DrawingToolController::changed,this,[=]{
        if (canvas->property("restoringToolState").toBool()) return;
        {QSignalBlocker a(contentTarget),b(boundaryTarget);contentTarget->setChecked(!c->selectionBoundary);boundaryTarget->setChecked(c->selectionBoundary);}
        const auto selected=c->selectedObject();
        if(selected&&selected->kind==ObjectKind::Text&&!c->textEditing())editor->setProperty("draftDirty",false);
        if(selected&&(canvas->tool()==Canvas::Tool::VectorNode||canvas->tool()==Canvas::Tool::Text)){
            QSignalBlocker s1(stroke),s2(fill),s3(close);c->closedPath=selected->closed;close->setChecked(c->closedPath);c->shapeStroke=selected->stroke.isValid();c->shapeFill=selected->fill.isValid();c->strokeWidth=selected->strokeWidth;c->fillColor=selected->fill;stroke->setChecked(c->shapeStroke);fill->setChecked(c->shapeFill);width->setValueSilently(c->strokeWidth);colorButton(fillChip,c->fillColor);
            // Selection changes synchronize text; pointer previews never overwrite typing.
            if(selected->kind==ObjectKind::Text && editor->property("objectId").toString()==selected->id && editor->property("draftDirty").toBool())updateTextDraft();
            if(selected->kind==ObjectKind::Text && (editor->property("objectId").toString()!=selected->id || !editor->property("draftDirty").toBool())){QSignalBlocker a(editor),b(font),d(bold),e(italic),f(alignment);c->textStyle=*selected;editor->setProperty("objectId",selected->id);editor->setPlainText(selected->text);showFontFamily(font,selected->fontFamily);fontSize->setValueSilently(selected->fontSize);bold->setChecked(selected->bold);italic->setChecked(selected->italic);alignment->setCurrentIndex(selected->alignment==Qt::AlignHCenter?1:selected->alignment==Qt::AlignRight?2:0);wrap->setValueSilently(selected->textWidth);leading->setValueSilently(selected->lineSpacing);editor->setProperty("draftDirty",false);}
        }
    });
    connect(doc->history(),&QUndoStack::indexChanged,this,[=]{editor->setProperty("draftDirty",false);c->notifySettings();});
    const auto updateToolVisibility = [=, this]{auto t=canvas->tool();regionSelection->setVisible(t==Canvas::Tool::MagicWand||t==Canvas::Tool::ColorRange);showScalar(tolerance,t!=Canvas::Tool::Eyedropper);showScalar(gap,t!=Canvas::Tool::Eyedropper&&t!=Canvas::Tool::ColorRange);bool eraser=t==Canvas::Tool::VectorEraser;eraseMode->setVisible(eraser);retouchMode->setVisible(!eraser && !canvas->property("toolCatalogManaged").toBool());showScalar(strength,!eraser);refresh();};
    connect(canvas,&Canvas::toolChanged,this,updateToolVisibility);
    const auto syncState = [=] {
        QSignalBlocker a(source), b(reference), d(gradientMode), e(stroke), f(fill), g(close),
            h(retouchMode), i(eraseMode), j(editor), k(font), l(bold), n(italic), p(alignment), q(mode),
            r(contentTarget), s(boundaryTarget);
        source->setCurrentIndex(c->fillSettings.source == RasterSampleSource::AllLayers ? 0
            : c->fillSettings.source == RasterSampleSource::ActiveLayer ? 1 : 2);
        reference->setCurrentIndex(reference->findData(c->fillSettings.referenceLayerId));
        referenceRow->setVisible(c->fillSettings.source == RasterSampleSource::ReferenceLayer);
        tolerance->setValueSilently(c->fillSettings.tolerance); gap->setValueSilently(c->fillSettings.gapClosing);
        gradientMode->setCurrentIndex(c->radialGradient ? 1 : 0); stops->setStops(c->gradientStops);
        stroke->setChecked(c->shapeStroke); fill->setChecked(c->shapeFill); close->setChecked(c->closedPath);
        width->setValueSilently(c->strokeWidth); colorButton(fillChip,c->fillColor);
        retouchMode->setCurrentIndex(int(c->retouchSettings.mode));
        size->setValueSilently(c->retouchSettings.size); strength->setValueSilently(c->retouchSettings.strength*100);
        eraseMode->setCurrentIndex(c->eraseSegments ? 1 : 0);
        editor->setProperty("draftDirty",false); editor->setProperty("objectId",QString());
        editor->setPlainText(c->textStyle.text); showFontFamily(font,c->textStyle.fontFamily);
        fontSize->setValueSilently(c->textStyle.fontSize); bold->setChecked(c->textStyle.bold); italic->setChecked(c->textStyle.italic);
        alignment->setCurrentIndex(c->textStyle.alignment==Qt::AlignHCenter ? 1 : c->textStyle.alignment==Qt::AlignRight ? 2 : 0);
        wrap->setValueSilently(c->textStyle.textWidth); leading->setValueSilently(c->textStyle.lineSpacing);
        mode->setCurrentIndex(c->warpMode ? 1 : 0); contentTarget->setChecked(!c->selectionBoundary); boundaryTarget->setChecked(c->selectionBoundary);
        const bool managed=canvas->property("toolCatalogManaged").toBool();
        gradientMode->setVisible(!managed); retouchMode->setVisible(!managed && canvas->tool()!=Canvas::Tool::VectorEraser);
        mode->setVisible(!managed); strokeColor->setVisible(!managed);
        updateToolVisibility();
    };
    // Capture genuine property edits after their setters run, not every canvas
    // pointer preview. Restoration blocks these signals to avoid feedback.
    for (auto *control : findChildren<ValueSlider *>()) connect(control,&ValueSlider::valueChanged,canvas,&Canvas::notifyToolSettingsEdited);
    for (auto *control : findChildren<QTabBar *>()) connect(control,&QTabBar::currentChanged,canvas,&Canvas::notifyToolSettingsEdited);
    for (auto *control : findChildren<QComboBox *>()) connect(control,&QComboBox::currentIndexChanged,canvas,&Canvas::notifyToolSettingsEdited);
    for (auto *control : findChildren<QToolButton *>()) {
        connect(control,&QToolButton::toggled,canvas,&Canvas::notifyToolSettingsEdited);
        connect(control,&QToolButton::clicked,canvas,&Canvas::notifyToolSettingsEdited);
    }
    connect(editor,&QPlainTextEdit::textChanged,canvas,&Canvas::notifyToolSettingsEdited);
    connect(stops,&GradientStopsWidget::stopsChanged,canvas,&Canvas::notifyToolSettingsEdited);
    connect(canvas,&Canvas::toolStateApplied,this,syncState);
    connect(canvas,&Canvas::toolAvailabilityChanged,this,&DrawingToolPanel::refresh);
    connect(canvas,&Canvas::documentChanged,this,&DrawingToolPanel::refresh);syncState();
}
void DrawingToolPanel::refresh(){
    using T=Canvas::Tool;auto t=m_canvas->tool();int page=2;
    if(t==T::Bucket||t==T::Eyedropper||t==T::MagicWand||t==T::ColorRange)page=0;
    else if(t==T::Gradient)page=1;else if(t==T::Retouch||t==T::VectorEraser)page=3;else if(t==T::Text)page=4;else if(t==T::EllipseSelect||t==T::PolygonSelect)page=5;else if(t==T::AdvancedTransform)page=6;
    m_pages->setCurrentIndex(page);setEnabled(m_canvas->hasProject() && m_canvas->toolAvailable());
    updateTransformFooter();
    if(auto *close=findChild<QToolButton*>("pathClosed"))close->setVisible(t!=T::Arc);
    if(auto *finish=findChild<QToolButton*>("finishDrawingPath")){
        finish->setToolTip(t==T::Arc?"Finish arc at the current bend (Enter)":"Finish path (Enter); Shift+Enter closes");
        finish->setAccessibleName(t==T::Arc?"Finish arc":"Finish path");
    }
}
void DrawingToolPanel::updateTransformFooter(){
    if(!m_transformFooter)return;
    const bool shown=m_canvas->tool()==Canvas::Tool::AdvancedTransform;
    m_transformFooter->setVisible(shown);
    const int footerHeight=shown?m_transformFooter->sizeHint().height():0;
    if(viewportMargins().bottom()!=footerHeight)setViewportMargins(0,0,0,footerHeight);
    if(shown){const int frame=frameWidth();m_transformFooter->setGeometry(frame,height()-frame-footerHeight,width()-2*frame,footerHeight);m_transformFooter->raise();}
}
void DrawingToolPanel::resizeEvent(QResizeEvent *event){QScrollArea::resizeEvent(event);updateTransformFooter();}
void DrawingToolPanel::changeEvent(QEvent *event){
    QScrollArea::changeEvent(event);
    if(event->type()==QEvent::FontChange||event->type()==QEvent::StyleChange)
        QTimer::singleShot(0,this,[this]{updateTransformFooter();});
}
bool DrawingToolPanel::eventFilter(QObject *watched,QEvent *event){
    if(watched->objectName()=="artworkText"){
        if(event->type()==QEvent::FocusOut)m_canvas->drawingTools()->finishTextEdit();
        if(event->type()==QEvent::ShortcutOverride||event->type()==QEvent::KeyPress){const auto *key=static_cast<QKeyEvent *>(event);const bool cancel=key->key()==Qt::Key_Escape;const bool apply=(key->key()==Qt::Key_Return||key->key()==Qt::Key_Enter)&&(key->modifiers()&(Qt::ControlModifier|Qt::MetaModifier));
            if(cancel||apply){if(event->type()==QEvent::KeyPress){watched->setProperty("draftDirty",false);m_canvas->drawingTools()->finishTextEdit(cancel);m_canvas->drawingTools()->notifySettings();}event->accept();return true;}
        }
    }
    return QScrollArea::eventFilter(watched,event);
}

}
