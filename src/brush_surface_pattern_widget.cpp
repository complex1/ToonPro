#include "brush_surface_pattern_widget.h"
#include "brush_surface_pattern.h"
#include "icons.h"
#include "property_row.h"
#include "ui_theme.h"
#include "ui_metrics.h"
#include "value_slider.h"
#include <QCheckBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QImageReader>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QSignalBlocker>
#include <QTabBar>
#include <QToolButton>
#include <QVBoxLayout>
#include <functional>
#include <cmath>

namespace toon {
class SurfacePlaneEditor : public QWidget {
public:
    explicit SurfacePlaneEditor(QWidget *parent):QWidget(parent) {
        setObjectName("surfacePlanePreview");setFixedHeight(156);setMinimumWidth(160);
        setFocusPolicy(Qt::StrongFocus);setAccessibleName("Surface preview and perspective plane");
        setToolTip("Drag a corner to shape the perspective plane. Arrow keys move the selected corner; Shift moves 10 pixels. Tab selects the next corner.");
    }
    BrushSurfacePattern pattern;
    std::function<void(std::array<QPointF,4>)> edited;
    std::function<void(int)> selected;
    int corner=0;
    void refresh(){if(!m_dragging)m_view=viewBounds();update();}
    QRectF viewBounds()const {
        if(!pattern.perspective)return {0,0,384,256};
        QRectF bounds(pattern.quad[0],QSizeF(0,0));for(auto point:pattern.quad)bounds|=QRectF(point,QSizeF(.01,.01));
        const qreal margin=std::max(bounds.width(),bounds.height())*.12;
        return bounds.adjusted(-margin,-margin,margin,margin);
    }
    QRectF content()const {return QRectF(rect()).adjusted(9,9,-9,-9);}
    QPointF display(QPointF p)const {const auto r=content();return {r.left()+(p.x()-m_view.left())/m_view.width()*r.width(),r.top()+(p.y()-m_view.top())/m_view.height()*r.height()};}
    QPointF artwork(QPointF p)const {const auto r=content();return {m_view.left()+(p.x()-r.left())/r.width()*m_view.width(),m_view.top()+(p.y()-r.top())/r.height()*m_view.height()};}
protected:
    void paintEvent(QPaintEvent *)override {
        QPainter painter(this);painter.fillRect(rect(),QColor(ui::base));const auto r=content();
        QImage image(qMax(1,int(r.width())),qMax(1,int(r.height())),QImage::Format_ARGB32_Premultiplied);
        BrushSurfaceSampler sampler(pattern,QColor("#343434"));
        for(int y=0;y<image.height();++y){auto *row=reinterpret_cast<QRgb *>(image.scanLine(y));for(int x=0;x<image.width();++x){
            const auto color=sampler.sample(artwork({r.left()+x+.5,r.top()+y+.5}));
            const int backdrop=((x/8+y/8)%2)?220:242,alpha=qAlpha(color);
            row[x]=qRgb(qRed(color)+(backdrop*(255-alpha)+127)/255,qGreen(color)+(backdrop*(255-alpha)+127)/255,qBlue(color)+(backdrop*(255-alpha)+127)/255);
        }}
        if(pattern.kind==BrushSurfaceKind::Off)image.fill(QColor(ui::raised));
        painter.drawImage(r.topLeft(),image);painter.setRenderHint(QPainter::Antialiasing);
        if(pattern.perspective){QPolygonF quad;for(auto p:pattern.quad)quad<<display(p);painter.setPen(QPen(QColor("#242424"),3));painter.drawPolygon(quad);painter.setPen(QPen(QColor(ui::focus),1));painter.drawPolygon(quad);
            for(int i=0;i<4;++i){painter.setBrush(QColor(i==corner?ui::focus:ui::base));painter.setPen(QColor(ui::text));painter.drawEllipse(display(pattern.quad[i]),5,5);}
        }
        painter.setPen(QColor(hasFocus()?ui::focus:ui::border));painter.setBrush(Qt::NoBrush);painter.drawRect(QRectF(rect()).adjusted(.5,.5,-.5,-.5));
    }
    void mousePressEvent(QMouseEvent *event)override {
        if(!pattern.perspective||event->button()!=Qt::LeftButton)return;
        qreal best=15;int chosen=-1;for(int i=0;i<4;++i){const qreal distance=QLineF(display(pattern.quad[i]),event->position()).length();if(distance<best){best=distance;chosen=i;}}
        if(chosen<0)return;corner=chosen;m_dragging=true;setFocus();if(selected)selected(corner);update();event->accept();
    }
    void mouseMoveEvent(QMouseEvent *event)override {if(m_dragging&&(event->buttons()&Qt::LeftButton)){setCorner(artwork(event->position()));event->accept();}}
    void mouseReleaseEvent(QMouseEvent *event)override {if(m_dragging&&event->button()==Qt::LeftButton){m_dragging=false;refresh();event->accept();}}
    void keyPressEvent(QKeyEvent *event)override {
        if(!pattern.perspective){QWidget::keyPressEvent(event);return;}
        auto point=pattern.quad[corner];const qreal step=event->modifiers()&Qt::ShiftModifier?10:1;
        if(event->key()==Qt::Key_Left)point.rx()-=step;else if(event->key()==Qt::Key_Right)point.rx()+=step;
        else if(event->key()==Qt::Key_Up)point.ry()-=step;else if(event->key()==Qt::Key_Down)point.ry()+=step;
        else {QWidget::keyPressEvent(event);return;}setCorner(point);event->accept();
    }
    bool focusNextPrevChild(bool next)override {if(pattern.perspective&&((next&&corner<3)||(!next&&corner>0))){corner+=next?1:-1;if(selected)selected(corner);update();return true;}return QWidget::focusNextPrevChild(next);}
private:
    void setCorner(QPointF point){auto next=pattern;next.quad[corner]=point;if(!validateBrushSurface(next))return;pattern=next;update();if(edited)edited(next.quad);}
    QRectF m_view{0,0,384,256};bool m_dragging=false;
};

BrushSurfacePatternWidget::BrushSurfacePatternWidget(QWidget *parent):QWidget(parent) {
    setObjectName("brushSurfacePattern");setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Maximum);auto *layout=new QVBoxLayout(this);layout->setContentsMargins(0,0,0,0);layout->setSpacing(ui::metrics::controlGap);
    m_kind=new QTabBar;m_kind->setObjectName("surfaceKind");m_kind->setProperty("compactDrawingTabs",true);m_kind->setIconSize({ui::metrics::tabIcon,ui::metrics::tabIcon});m_kind->setAccessibleName("Surface pattern type");m_kind->setExpanding(false);m_kind->setUsesScrollButtons(true);m_kind->setElideMode(Qt::ElideNone);
    const QStringList names{"Off","Dots","Lines","Cross","Image","Diamond","Noise","Fade"};
    for(const auto &name:names){m_kind->addTab(name);m_kind->setTabToolTip(m_kind->count()-1,name+" surface pattern");}layout->addWidget(m_kind);
    m_preview=new SurfacePlaneEditor(this);layout->addWidget(m_preview);
    m_actions=new QWidget(this);m_actions->setObjectName("surfaceActions");m_actions->setMinimumHeight(ui::metrics::actionTarget);m_actions->setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Maximum);
    auto *actions=new QHBoxLayout(m_actions);actions->setContentsMargins(0,0,0,0);actions->setSpacing(ui::metrics::controlGap);
    m_imageName=new QLabel("No image");m_imageName->setObjectName("surfaceImageName");actions->addWidget(m_imageName,1);
    auto button=[&](const char *id,const char *icon,const char *help){auto *b=new QToolButton;b->setObjectName(id);b->setIcon(appIcon(icon));b->setProperty("compactDrawingAction",true);b->setIconSize({ui::metrics::actionIcon,ui::metrics::actionIcon});b->setFixedSize(ui::metrics::actionTarget,ui::metrics::actionTarget);b->setAccessibleName(help);b->setToolTip(help);actions->addWidget(b);return b;};
    m_import=button("surfaceImport","image","Import repeating image tile; original colors and transparency are preserved");
    m_fit=button("surfaceFitArtwork","maximize-2","Fit perspective plane to the current artwork frame. Paint outside the plane stays transparent; source and tile sizes stay unchanged.");m_fit->setAccessibleName("Fit perspective plane to artwork");m_fit->setEnabled(false);
    m_reset=button("surfaceResetPlane","rotate-ccw","Reset perspective plane to a 512-pixel square at the artwork origin");layout->addWidget(m_actions);
    auto row=[&](const char *id,const char *label,qreal min,qreal max,qreal value,int precision,ValueSlider *&slider,const char *help){slider=new ValueSlider(label,min,max,value,precision);slider->setObjectName(id);slider->setToolTip(help);auto *r=new PropertyRow(label,slider);layout->addWidget(r);return r;};
    m_frequencyRow=row("surfaceFrequency","Frequency",.1,50,10,1,m_frequency,"Repeats per 100 artwork pixels. Frequency changes spacing without changing ink density.");
    m_densityRow=row("surfaceDensity","Density",0,100,30,1,m_density,"Percentage of the surface covered by ink.");
    row("surfaceAngle","Angle",-360,360,0,1,m_angle,"Pattern rotation in degrees, around its phase origin.");m_angle->setSliderRange(-180,180);
    m_densityEndRow=row("surfaceDensityEnd","End density",0,100,0,1,m_densityEnd,"Ink percentage reached at the end of the fade.");
    m_fadeLengthRow=row("surfaceFadeLength","Fade length",1,8192,512,0,m_fadeLength,"Distance in source-plane pixels along the rotated pattern Y axis.");m_fadeLength->setSliderRange(1,2048);
    m_seedRow=row("surfaceSeed","Seed",0,4294967295.,1,0,m_seed,"Fixed noise seed. Noise remains aligned between all brush impressions.");m_seed->setSliderRange(0,999);
    m_tileWidthRow=row("surfaceTileWidth","Tile width",1,8192,128,1,m_tileWidth,"Repeating tile width in artwork pixels before perspective mapping.");m_tileWidth->setSliderRange(1,512);
    m_tileHeightRow=row("surfaceTileHeight","Tile height",1,8192,128,1,m_tileHeight,"Repeating tile height in artwork pixels before perspective mapping.");m_tileHeight->setSliderRange(1,512);
    row("surfacePhaseX","Offset X",-32768,32768,0,1,m_phaseX,"Pattern origin in artwork or source-plane pixels.");m_phaseX->setSliderRange(-512,512);
    row("surfacePhaseY","Offset Y",-32768,32768,0,1,m_phaseY,"Pattern origin in artwork or source-plane pixels.");m_phaseY->setSliderRange(-512,512);
    m_perspective=new QCheckBox("Perspective plane");m_perspective->setObjectName("surfacePerspective");m_perspective->setToolTip("Fit the source plane to four explicit artwork-space corners. Painting outside this plane remains transparent.");layout->addWidget(m_perspective);
    m_planeWidthRow=row("surfacePlaneWidth","Source width",8,8192,512,0,m_planeWidth,"Width of the source plane in pixels; increasing it adds more pattern repeats within the destination plane.");m_planeWidth->setSliderRange(8,2048);
    m_planeHeightRow=row("surfacePlaneHeight","Source height",8,8192,512,0,m_planeHeight,"Height of the source plane in pixels.");m_planeHeight->setSliderRange(8,2048);
    m_cornerXRow=row("surfaceCornerX","Corner X",-32768,32768,0,1,m_cornerX,"Artwork X of the corner selected in the preview.");m_cornerX->setSliderRange(-4096,4096);
    m_cornerYRow=row("surfaceCornerY","Corner Y",-32768,32768,0,1,m_cornerY,"Artwork Y of the corner selected in the preview.");m_cornerY->setSliderRange(-4096,4096);
    connect(m_kind,&QTabBar::currentChanged,this,[this](int n){if(m_refreshing||n<0)return;m_pattern.kind=BrushSurfaceKind(n);changed();});
    connect(m_perspective,&QCheckBox::toggled,this,[this](bool on){if(m_refreshing)return;m_pattern.perspective=on;changed();});
    auto scalar=[&](ValueSlider *s,std::function<void(qreal)> assign){connect(s,&ValueSlider::valueChanged,this,[this,assign](qreal value){if(m_refreshing)return;const auto previous=m_pattern;assign(value);if(!validateBrushSurface(m_pattern)){m_pattern=previous;refresh();return;}changed();});};
    scalar(m_frequency,[this](qreal v){m_pattern.frequency=v;});scalar(m_density,[this](qreal v){m_pattern.density=v/100;});scalar(m_angle,[this](qreal v){m_pattern.angle=v;});
    scalar(m_densityEnd,[this](qreal v){m_pattern.densityEnd=v/100;});scalar(m_fadeLength,[this](qreal v){m_pattern.fadeLength=v;});scalar(m_seed,[this](qreal v){m_pattern.seed=quint32(v);});
    scalar(m_tileWidth,[this](qreal v){m_pattern.tileSize.setWidth(v);});scalar(m_tileHeight,[this](qreal v){m_pattern.tileSize.setHeight(v);});
    scalar(m_phaseX,[this](qreal v){m_pattern.phase.setX(v);});scalar(m_phaseY,[this](qreal v){m_pattern.phase.setY(v);});
    scalar(m_planeWidth,[this](qreal v){m_pattern.planeSize.setWidth(v);});scalar(m_planeHeight,[this](qreal v){m_pattern.planeSize.setHeight(v);});
    scalar(m_cornerX,[this](qreal v){m_pattern.quad[m_preview->corner].setX(v);});scalar(m_cornerY,[this](qreal v){m_pattern.quad[m_preview->corner].setY(v);});
    m_preview->edited=[this](const auto &quad){m_pattern.quad=quad;changed();};
    m_preview->selected=[this](int corner){m_cornerX->setValueSilently(m_pattern.quad[corner].x());m_cornerY->setValueSilently(m_pattern.quad[corner].y());};
    connect(m_fit,&QToolButton::clicked,this,&BrushSurfacePatternWidget::fitToArtworkRequested);
    connect(m_reset,&QToolButton::clicked,this,[this]{m_pattern.quad=BrushSurfacePattern{}.quad;m_pattern.planeSize={512,512};changed();});
    connect(m_import,&QToolButton::clicked,this,[this]{const auto path=QFileDialog::getOpenFileName(this,"Import repeating tile",{},"Images (*.png *.webp *.jpg *.jpeg *.bmp *.tif *.tiff)");if(path.isEmpty())return;QString error;if(!importImage(path,&error)){m_imageName->setText("Import failed");m_imageName->setToolTip(error);}});
    layout->addStretch();
    refresh();
}
BrushSurfacePatternWidget::~BrushSurfacePatternWidget(){m_preview->edited={};m_preview->selected={};for(auto *child:findChildren<QObject *>())QObject::disconnect(child,nullptr,this,nullptr);}
void BrushSurfacePatternWidget::setPattern(const BrushSurfacePattern &pattern){
    // Brush size, color, and other unrelated model updates must not erase the
    // artist's image-mode draft while they choose its first tile.
    if(m_pattern.kind==BrushSurfaceKind::Image&&m_pattern.image.isNull()&&pattern==m_syncedPattern){refresh();return;}
    resetPattern(pattern);
}
void BrushSurfacePatternWidget::resetPattern(const BrushSurfacePattern &pattern){m_pattern=pattern;m_syncedPattern=pattern;refresh();}
void BrushSurfacePatternWidget::setArtworkFitAvailable(bool available){m_fit->setEnabled(available);}
bool BrushSurfacePatternWidget::fitToArtwork(const QPolygonF &frame){
    if(!m_pattern.perspective||frame.size()<4||frame.size()>5)return false;
    if(frame.size()==5&&frame.first()!=frame.last())return false;
    auto candidate=m_pattern;for(int i=0;i<4;++i)candidate.quad[i]=frame[i];
    if(!validateBrushSurface(candidate))return false;
    m_pattern=std::move(candidate);changed();return true;
}
void BrushSurfacePatternWidget::changed(){
    refresh();
    // An image mode with no resource is a local chooser state, not paintable
    // brush settings. Keep the active brush valid until import succeeds.
    if(m_pattern.kind==BrushSurfaceKind::Image&&m_pattern.image.isNull())return;
    emit patternEdited(m_pattern);
}
void BrushSurfacePatternWidget::refresh(){
    m_refreshing=true;
    m_kind->setCurrentIndex(int(m_pattern.kind));m_perspective->setChecked(m_pattern.perspective);
    m_frequency->setValueSilently(m_pattern.frequency);m_density->setValueSilently(m_pattern.density*100);m_angle->setValueSilently(m_pattern.angle);
    m_densityEnd->setValueSilently(m_pattern.densityEnd*100);m_fadeLength->setValueSilently(m_pattern.fadeLength);m_seed->setValueSilently(m_pattern.seed);
    m_tileWidth->setValueSilently(m_pattern.tileSize.width());m_tileHeight->setValueSilently(m_pattern.tileSize.height());
    m_phaseX->setValueSilently(m_pattern.phase.x());m_phaseY->setValueSilently(m_pattern.phase.y());
    m_planeWidth->setValueSilently(m_pattern.planeSize.width());m_planeHeight->setValueSilently(m_pattern.planeSize.height());
    m_cornerX->setValueSilently(m_pattern.quad[m_preview->corner].x());m_cornerY->setValueSilently(m_pattern.quad[m_preview->corner].y());
    const bool image=m_pattern.kind==BrushSurfaceKind::Image,enabled=m_pattern.kind!=BrushSurfaceKind::Off;
    m_frequencyRow->setVisible(enabled&&!image);m_densityRow->setVisible(enabled&&!image);
    m_densityEndRow->setVisible(m_pattern.kind==BrushSurfaceKind::FadeDots);m_fadeLengthRow->setVisible(m_pattern.kind==BrushSurfaceKind::FadeDots);m_seedRow->setVisible(m_pattern.kind==BrushSurfaceKind::Noise);
    m_tileWidthRow->setVisible(image);m_tileHeightRow->setVisible(image);m_imageName->setVisible(image);m_import->setVisible(image);
    m_imageName->setText(m_pattern.image.isNull()?"Import a tile":QString("%1 × %2").arg(m_pattern.image.width()).arg(m_pattern.image.height()));
    m_imageName->setToolTip(m_pattern.image.isNull()?"The active brush stays unchanged until a tile image is imported.":"The repeating tile preserves its original colors and transparency.");
    m_preview->setVisible(enabled);m_angle->parentWidget()->setVisible(enabled);m_phaseX->parentWidget()->setVisible(enabled);m_phaseY->parentWidget()->setVisible(enabled);m_perspective->setVisible(enabled);
    m_actions->setVisible(image||(enabled&&m_pattern.perspective));
    m_fit->setVisible(enabled&&m_pattern.perspective);m_reset->setVisible(enabled&&m_pattern.perspective);m_planeWidthRow->setVisible(enabled&&m_pattern.perspective);m_planeHeightRow->setVisible(enabled&&m_pattern.perspective);m_cornerXRow->setVisible(enabled&&m_pattern.perspective);m_cornerYRow->setVisible(enabled&&m_pattern.perspective);
    m_preview->pattern=m_pattern;m_preview->refresh();m_refreshing=false;
}
bool BrushSurfacePatternWidget::importImage(const QString &path,QString *error){
    auto fail=[&](const QString &message){if(error)*error=message;return false;};
    const QFileInfo info(path);if(!info.exists()||info.size()>64*1024*1024)return fail("Choose an image smaller than 64 MiB.");
    QImageReader reader(path);reader.setAutoTransform(true);const QSize source=reader.size();
    if(!source.isValid()||qint64(source.width())*source.height()>64*1024*1024)return fail("Tile image dimensions exceed the 64-million-pixel import limit.");
    if(source.width()>2048||source.height()>2048)reader.setScaledSize(source.scaled(2048,2048,Qt::KeepAspectRatio));
    auto image=reader.read();if(image.isNull())return fail("This tile image could not be decoded: "+reader.errorString());
    if(image.width()>2048||image.height()>2048)image=image.scaled(2048,2048,Qt::KeepAspectRatio,Qt::SmoothTransformation);
    m_pattern.image=image.convertToFormat(QImage::Format_ARGB32_Premultiplied);m_pattern.kind=BrushSurfaceKind::Image;changed();return true;
}
} // namespace toon
