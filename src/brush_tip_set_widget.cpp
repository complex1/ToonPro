#include "brush_tip_set_widget.h"
#include "brush_engine.h"
#include "icons.h"
#include "ui_theme.h"
#include "ui_metrics.h"
#include "value_slider.h"
#include "property_row.h"
#include <QFileDialog>
#include <QImageReader>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QSignalBlocker>
#include <QTabBar>
#include <QToolButton>
#include <QVBoxLayout>
#include <functional>
#include <algorithm>

namespace toon {
class TipAnchorWidget : public QWidget {
public:
    explicit TipAnchorWidget(QWidget *parent) : QWidget(parent) {
        setFixedHeight(114); setMinimumWidth(120); setFocusPolicy(Qt::StrongFocus);
        setObjectName("brushTipAnchor"); setAccessibleName("Tip root anchor");
        setToolTip("Drag the cross to place this tip's root. Arrow keys adjust it; Home centers it; End places it at the bottom.");
    }
    QImage image; QPointF anchor{.5,.5}; std::function<void(QPointF)> edited;
    QRectF field() const { const auto area=QRectF(rect()).adjusted(12,12,-12,-12); if(image.isNull())return area; const auto size=QSizeF(image.size()).scaled(area.size(),Qt::KeepAspectRatio); return QRectF(area.center()-QPointF(size.width()/2,size.height()/2),size); }
protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this); p.fillRect(rect(),QColor(ui::base));
        const auto area=field();
        if (!image.isNull()) {
            auto preview=image;
            if(image.format()==QImage::Format_Grayscale8){
                preview=QImage(image.size(),QImage::Format_ARGB32_Premultiplied);
                for(int y=0;y<image.height();++y)for(int x=0;x<image.width();++x){const int a=image.constScanLine(y)[x];preview.setPixel(x,y,qPremultiply(qRgba(220,220,220,a)));}
            }
            p.drawImage(area,preview);
        }
        const QPointF point(area.left()+anchor.x()*area.width(),area.top()+anchor.y()*area.height());
        p.setPen(QPen(Qt::black,4));p.drawLine(point-QPointF(8,0),point+QPointF(8,0));p.drawLine(point-QPointF(0,8),point+QPointF(0,8));
        p.setPen(QPen(Qt::white,1));p.drawLine(point-QPointF(8,0),point+QPointF(8,0));p.drawLine(point-QPointF(0,8),point+QPointF(0,8));
        p.setPen(QColor(hasFocus()?ui::focus:ui::border));p.drawRect(area);
    }
    void setAt(QPointF point){const auto a=field();anchor={qBound(0.,(point.x()-a.left())/a.width(),1.),qBound(0.,(point.y()-a.top())/a.height(),1.)};update();if(edited)edited(anchor);}
    void mousePressEvent(QMouseEvent *e) override {if(e->button()==Qt::LeftButton){setFocus();setAt(e->position());e->accept();}}
    void mouseMoveEvent(QMouseEvent *e) override {if(e->buttons()&Qt::LeftButton)setAt(e->position());}
    void keyPressEvent(QKeyEvent *e) override {
        QPointF a=anchor;const qreal step=e->modifiers()&Qt::ShiftModifier?.1:.01;
        if(e->key()==Qt::Key_Left)a.rx()-=step;else if(e->key()==Qt::Key_Right)a.rx()+=step;
        else if(e->key()==Qt::Key_Up)a.ry()-=step;else if(e->key()==Qt::Key_Down)a.ry()+=step;
        else if(e->key()==Qt::Key_Home)a={.5,.5};else if(e->key()==Qt::Key_End)a={.5,1};else {QWidget::keyPressEvent(e);return;}
        anchor={qBound(0.,a.x(),1.),qBound(0.,a.y(),1.)};update();if(edited)edited(anchor);e->accept();
    }
};
BrushTipSetWidget::BrushTipSetWidget(QWidget *parent):QWidget(parent) {
    setObjectName("brushTipSet");auto *layout=new QVBoxLayout(this);layout->setContentsMargins(0,0,0,0);layout->setSpacing(ui::metrics::controlGap);
    auto *actions=new QHBoxLayout;actions->setSpacing(2);
    auto button=[&](const char *id,const char *icon,const char *tip){auto *b=new QToolButton(this);b->setObjectName(id);b->setIcon(appIcon(icon));b->setProperty("compactDrawingAction",true);b->setIconSize({ui::metrics::actionIcon,ui::metrics::actionIcon});b->setFixedSize(ui::metrics::actionTarget,ui::metrics::actionTarget);b->setToolTip(tip);b->setAccessibleName(tip);actions->addWidget(b);return b;};
    m_count=new QLabel; m_count->setObjectName("brushTipCount"); actions->addWidget(m_count); actions->addStretch();
    m_add=button("brushTipAdd","plus","Add stamp images");
    m_remove=button("brushTipRemove","trash","Remove selected tip");
    m_left=button("brushTipEarlier","arrow-up","Move stamp earlier");m_right=button("brushTipLater","arrow-down","Move stamp later");layout->addLayout(actions);
    m_list=new QListWidget;m_list->setObjectName("brushTipFrames");m_list->setAccessibleName("Brush tip images");m_list->setViewMode(QListView::IconMode);m_list->setResizeMode(QListView::Adjust);m_list->setMovement(QListView::Static);m_list->setIconSize({36,36});m_list->setGridSize({46,46});m_list->setFixedHeight(100);layout->addWidget(m_list);
    m_order=new QTabBar;m_order->setObjectName("brushTipOrder");m_order->setProperty("compactDrawingTabs",true);m_order->setIconSize({ui::metrics::tabIcon,ui::metrics::tabIcon});m_order->setAccessibleName("Tip selection order");
    const QStringList labels{"Random","Ordered","Shuffle","Pressure","Direction"};
    const QStringList orderHelp{
        "Pick a stamp for each impression using its weight; repeats are possible",
        "Repeat stamps in the displayed order for each impression",
        "Shuffle every stamp once per cycle, with no immediate repeats",
        "Choose the stamp for each impression using pen pressure",
        "Choose the stamp for each impression using stroke direction"};
    for(int i=0;i<labels.size();++i){m_order->addTab(labels[i]);m_order->setTabToolTip(i,orderHelp[i]);}
    m_order->setExpanding(false);m_order->setUsesScrollButtons(true);layout->addWidget(m_order);
    m_anchor=new TipAnchorWidget(this);layout->addWidget(m_anchor);
    m_weight=new ValueSlider("Random tip weight",.01,100,1,2);m_weight->setObjectName("brushTipWeight");
    m_weight->setToolTip("Relative likelihood of this tip in Random mode. Shuffle uses every tip once.");
    layout->addWidget(new PropertyRow("Weight",m_weight));
    connect(m_weight,&ValueSlider::valueChanged,this,[this](qreal value){const int i=m_list->currentRow();if(!m_refreshing&&i>=0&&!m_brush.tipFrames.isEmpty()){m_brush.tipFrames[i].weight=value;emit brushEdited(m_brush);}});
    connect(m_add,&QToolButton::clicked,this,[this]{const auto paths=QFileDialog::getOpenFileNames(this,"Add stamp images",{},"Images (*.png *.jpg *.jpeg *.webp *.bmp *.tif *.tiff)");if(paths.isEmpty())return;QString error;if(!addImages(paths,&error))QMessageBox::warning(this,"Brush tips",error);});
    connect(m_remove,&QToolButton::clicked,this,&BrushTipSetWidget::removeTip);
    connect(m_left,&QToolButton::clicked,this,[this]{moveTip(-1);});connect(m_right,&QToolButton::clicked,this,[this]{moveTip(1);});
    connect(m_order,&QTabBar::currentChanged,this,[this](int index){if(!m_refreshing&&index>=0){m_brush.tipOrder=BrushTipOrder(index);refresh();emit brushEdited(m_brush);}});
    connect(m_list,&QListWidget::currentRowChanged,this,[this]{refresh();});
    m_anchor->edited=[this](QPointF anchor){const int i=m_list->currentRow();if(i<0||m_refreshing)return;if(m_brush.tipFrames.isEmpty()){const auto image=brushTipFrameImage(m_brush,-1);if(image.isNull())return;m_brush.tipFrames={{image.rect(),anchor,1}};}else m_brush.tipFrames[i].anchor=anchor;emit brushEdited(m_brush);};
    refresh();
}
BrushTipSetWidget::~BrushTipSetWidget(){
    m_anchor->edited={};
    // Focused numeric editors can finish while QWidget tears down its children.
    // Their callbacks must not access the already-destroyed brush value.
    for(auto *child:findChildren<QObject *>())QObject::disconnect(child,nullptr,this,nullptr);
}
void BrushTipSetWidget::setBrush(const BrushSettings &brush){
    const bool changed=m_brush.tipFrames!=brush.tipFrames||m_brush.tipMask.cacheKey()!=brush.tipMask.cacheKey()||m_brush.colorTip.cacheKey()!=brush.colorTip.cacheKey();
    m_brush=brush;if(changed){const QSignalBlocker block(m_list);const int selected=m_list->currentRow();m_list->clear();const int count=qMax(1,int(brush.tipFrames.size()));for(int i=0;i<count;++i){auto image=brushTipFrameImage(brush,brush.tipFrames.isEmpty()?-1:i);if(image.isNull())continue;if(image.format()==QImage::Format_Grayscale8){QImage c(image.size(),QImage::Format_ARGB32_Premultiplied);c.fill(Qt::transparent);for(int y=0;y<image.height();++y)for(int x=0;x<image.width();++x)c.setPixel(x,y,qPremultiply(qRgba(225,225,225,image.constScanLine(y)[x])));image=c;}auto *item=new QListWidgetItem(QIcon(QPixmap::fromImage(image)),{},m_list);item->setToolTip(QString("Tip %1").arg(i+1));item->setData(Qt::AccessibleTextRole,item->toolTip());}if(m_list->count())m_list->setCurrentRow(qBound(0,selected,m_list->count()-1));}refresh();
}
void BrushTipSetWidget::refresh(){
    m_refreshing=true;const int i=m_list->currentRow(),count=m_list->count();const bool set=!m_brush.tipFrames.isEmpty();
    m_count->setText(QString("Stamps %1/16").arg(count));
    m_count->setAccessibleName(QString("%1 of 16 stamp images").arg(count));
    m_add->setEnabled(count<16);
    m_add->setToolTip(QString("Add stamp images; existing stamps stay. Up to 16 images, fitted to 512 pixels each. %1")
        .arg(m_brush.colorTip.isNull()?"Transparent images use alpha; opaque images use inverted brightness and paint with the brush color.":"Original image colors and transparency are preserved."));
    m_remove->setEnabled(count>1);m_left->setEnabled(set&&i>0);m_right->setEnabled(set&&i>=0&&i<count-1);m_order->setEnabled(count>1);
    m_list->setVisible(count>0);m_list->setFixedHeight(count<=4?50:100);
    m_remove->setVisible(count>0);m_left->setVisible(count>1);m_right->setVisible(count>1);
    m_order->setVisible(count>1);m_anchor->setVisible(count>0);
    m_weight->parentWidget()->setVisible(set&&i>=0&&m_brush.tipOrder==BrushTipOrder::Random);
    {const QSignalBlocker block(m_order);m_order->setCurrentIndex(int(m_brush.tipOrder));}
    {const QSignalBlocker block(m_weight);m_weight->setValue(set&&i>=0?m_brush.tipFrames[i].weight:1);m_weight->setEnabled(set&&i>=0&&m_brush.tipOrder==BrushTipOrder::Random);}
    m_anchor->setEnabled(i>=0&&m_brush.connected.mode==BrushConnectedMode::Off);m_anchor->image=i<0?QImage():brushTipFrameImage(m_brush,set?i:-1);m_anchor->anchor=set&&i>=0?m_brush.tipFrames[i].anchor:QPointF(.5,.5);m_anchor->update();m_refreshing=false;
}
bool BrushTipSetWidget::addImages(const QStringList &paths,QString *error){
    QVector<QImage> images;QVector<QPointF> anchors;const bool colored=!m_brush.colorTip.isNull();
    const int current=m_brush.tipFrames.isEmpty()?(m_brush.tipMask.isNull()&&m_brush.colorTip.isNull()?0:1):int(m_brush.tipFrames.size());
    if(paths.isEmpty()||current+paths.size()>16){if(error)*error="A brush supports up to 16 tips.";return false;}
    for(int i=0;i<current;++i){auto image=brushTipFrameImage(m_brush,m_brush.tipFrames.isEmpty()?-1:i);if(image.width()>512||image.height()>512)image=image.scaled(512,512,Qt::KeepAspectRatio,Qt::SmoothTransformation);images.append(image);anchors.append(m_brush.tipFrames.isEmpty()?QPointF(.5,.5):m_brush.tipFrames[i].anchor);}
    for(const auto &path:paths){QImageReader reader(path);reader.setAutoTransform(true);const auto size=reader.size();if(!size.isValid()||qint64(size.width())*size.height()>16000000){if(error)*error="Choose readable tip images up to 16 million pixels each.";return false;}auto image=reader.read();if(image.isNull()){if(error)*error="Could not read tip: "+reader.errorString();return false;}if(image.width()>512||image.height()>512)image=image.scaled(512,512,Qt::KeepAspectRatio,Qt::SmoothTransformation);image=image.convertToFormat(QImage::Format_ARGB32_Premultiplied);if(!colored){QImage gray(image.size(),QImage::Format_Grayscale8);bool transparency=false;for(int y=0;y<image.height()&&!transparency;++y)for(int x=0;x<image.width();++x)if(qAlpha(image.pixel(x,y))<255){transparency=true;break;}for(int y=0;y<image.height();++y)for(int x=0;x<image.width();++x){const auto c=image.pixel(x,y);gray.scanLine(y)[x]=transparency?qAlpha(c):255-qGray(c);}image=gray;}images.append(image);anchors.append({.5,.5});}
    auto next=m_brush;if(!setBrushTipImages(next,images,anchors,error))return false;for(int i=0;i<m_brush.tipFrames.size();++i)next.tipFrames[i].weight=m_brush.tipFrames[i].weight;setBrush(next);m_list->setCurrentRow(m_list->count()-1);emit brushEdited(m_brush);return true;
}
void BrushTipSetWidget::removeTip(){const int selected=m_list->currentRow();if(selected<0||m_brush.tipFrames.size()<2)return;QVector<QImage> images;QVector<QPointF> anchors;QVector<qreal> weights;for(int i=0;i<m_brush.tipFrames.size();++i)if(i!=selected){images.append(brushTipFrameImage(m_brush,i));anchors.append(m_brush.tipFrames[i].anchor);weights.append(m_brush.tipFrames[i].weight);}auto next=m_brush;if(!setBrushTipImages(next,images,anchors))return;for(int i=0;i<weights.size();++i)next.tipFrames[i].weight=weights[i];setBrush(next);emit brushEdited(m_brush);}
void BrushTipSetWidget::moveTip(int delta){const int i=m_list->currentRow(),to=i+delta;if(i<0||to<0||to>=m_brush.tipFrames.size())return;auto next=m_brush;next.tipFrames.swapItemsAt(i,to);setBrush(next);m_list->setCurrentRow(to);emit brushEdited(m_brush);}
}
