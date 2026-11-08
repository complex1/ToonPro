#include "line_art_dialog.h"
#include "line_art_document.h"
#include "document.h"
#include "icons.h"
#include "property_row.h"
#include "value_slider.h"
#include <QCheckBox>
#include <QColorDialog>
#include <QDialogButtonBox>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QSplitter>
#include <QStackedWidget>
#include <QTabBar>
#include <QThreadPool>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QtConcurrentRun>
#include <cmath>

namespace toon {
namespace {
QThreadPool &conversionPool(){static QThreadPool pool;static const bool ready=[] {pool.setMaxThreadCount(1);return true;}();Q_UNUSED(ready);return pool;}
QTabBar *tabs(QWidget *parent,const char *name,const QStringList &labels,const QStringList &icons={}){
    auto *bar=new QTabBar(parent);bar->setObjectName(name);bar->setAccessibleName(name);bar->setDrawBase(false);bar->setExpanding(true);
    bar->setProperty("compactDrawingTabs",true);bar->setFocusPolicy(Qt::StrongFocus);bar->setIconSize({16,16});
    for(int i=0;i<labels.size();++i){bar->addTab(i<icons.size()?appIcon(icons[i]):QIcon(),labels[i]);bar->setTabToolTip(i,labels[i]);bar->setAccessibleTabName(i,labels[i]);}return bar;
}
void chip(QToolButton *button,QColor color){QPixmap pixmap(40,40);pixmap.setDevicePixelRatio(2);pixmap.fill(Qt::transparent);QPainter p(&pixmap);p.setPen(QColor("#aaaaaa"));p.setBrush(color);p.drawRoundedRect(QRectF(1,1,18,18),3,3);p.end();button->setIcon(QIcon(pixmap));button->setAccessibleDescription(color.name(QColor::HexArgb));}
int presetIndex(LineArtShading shading){switch(shading){case LineArtShading::None:return 0;case LineArtShading::Cel:return 1;case LineArtShading::Screentone:return 2;case LineArtShading::Grayscale:return 3;}return 0;}
}
class LineArtPreview final:public QWidget {
public:
    explicit LineArtPreview(QWidget *parent):QWidget(parent){setMinimumSize(240,220);setAccessibleName("Line art conversion preview");}
    QImage source,result;bool original=false;
protected:
    void paintEvent(QPaintEvent *)override{
        QPainter p(this);p.fillRect(rect(),QColor("#303030"));const auto &image=original?source:result;
        if(image.isNull()){p.setPen(QColor("#cccccc"));p.drawText(rect(),Qt::AlignCenter,"Preparing preview…");return;}
        QSizeF size=image.size();size.scale(QSizeF(width()-24,height()-24),Qt::KeepAspectRatio);
        QRectF area(QPointF((width()-size.width())*.5,(height()-size.height())*.5),size);
        p.save();p.setClipRect(area);p.fillRect(area,Qt::white);
        for(int y=int(area.top());y<area.bottom();y+=12)for(int x=int(area.left());x<area.right();x+=12)
            if(((x-int(area.left()))/12+(y-int(area.top()))/12)%2)p.fillRect(QRect(x,y,12,12),QColor("#eeeeee"));
        p.setRenderHint(QPainter::SmoothPixmapTransform);p.drawImage(area,image);p.restore();
    }
};
class LineArtRamp final:public QWidget {
    Q_OBJECT
public:
    explicit LineArtRamp(QWidget *parent):QWidget(parent){setMinimumHeight(52);setFocusPolicy(Qt::StrongFocus);setAccessibleName("Tone boundaries");setToolTip("Drag the shade boundaries. Left/Right adjusts; Up/Down chooses a boundary.");}
    int dark=85,bright=175,active=0;
    void setValues(int d,int b){dark=d;bright=b;update();}
signals:void edited(int dark,int bright);
protected:
    void paintEvent(QPaintEvent *)override{QPainter p(this);p.setRenderHint(QPainter::Antialiasing);const QRectF rail(10,8,width()-20,22);
        const double a=rail.left()+dark/255.*rail.width(),b=rail.left()+bright/255.*rail.width();
        p.fillRect(QRectF(rail.left(),rail.top(),a-rail.left(),rail.height()),QColor("#454552"));p.fillRect(QRectF(a,rail.top(),b-a,rail.height()),QColor("#9696a3"));p.fillRect(QRectF(b,rail.top(),rail.right()-b,rail.height()),Qt::white);
        for(int i=0;i<2;++i){double x=i?b:a;p.setPen(QPen(hasFocus()&&active==i?QColor("#ffffff"):QColor("#999999"),2));p.setBrush(QColor("#303030"));p.drawEllipse(QPointF(x,31),5,7);}
    }
    void change(double x){int v=qBound(1,int((x-10)/qMax(1,width()-20)*255),254);if(active==0)dark=qMin(v,bright-1);else bright=qMax(v,dark+1);update();emit edited(dark,bright);}
    void mousePressEvent(QMouseEvent *e)override{if(e->button()!=Qt::LeftButton)return;setFocus();double v=(e->position().x()-10)/qMax(1,width()-20)*255;active=std::abs(v-dark)<std::abs(v-bright)?0:1;change(e->position().x());}
    void mouseMoveEvent(QMouseEvent *e)override{if(e->buttons()&Qt::LeftButton)change(e->position().x());}
    void keyPressEvent(QKeyEvent *e)override{if(e->key()==Qt::Key_Up||e->key()==Qt::Key_Down){active=1-active;update();return;}int step=e->key()==Qt::Key_Left?-1:e->key()==Qt::Key_Right?1:0;if(step){if(active)bright=qBound(dark+1,bright+step,254);else dark=qBound(1,dark+step,bright-1);update();emit edited(dark,bright);return;}QWidget::keyPressEvent(e);}
};
LineArtDialog::LineArtDialog(Document *document,QWidget *parent):QDialog(parent),m_document(document) {
    setObjectName("lineArtDialog");setWindowTitle("Convert to line art");setWindowModality(Qt::WindowModal);resize(1080,740);setMinimumSize(720,480);
    m_sourceId=document->activeShot().layers[document->activeLayer()].id;m_shotId=document->activeShotId();m_revision=document->contentRevision();m_frame=document->currentFrame();
    const auto &source=document->activeShot().layers[document->activeLayer()];
    auto *layout=new QVBoxLayout(this);layout->setContentsMargins(12,12,12,12);layout->setSpacing(8);
    auto *heading=new QLabel(QString("%1 · %2 · %3 × %4 px").arg(source.name,"Image / drawing").arg(document->project().width).arg(document->project().height),this);
    heading->setTextFormat(Qt::PlainText);heading->setWordWrap(true);layout->addWidget(heading);
    m_presets=tabs(this,"lineArtPresets",{"Lines only","Cel shading","Manga tones","Grayscale"},{"pen-tool","contrast","grid-2x2","palette"});m_presets->setAccessibleName("Conversion preset");layout->addWidget(m_presets);
    auto *splitter=new QSplitter(this);layout->addWidget(splitter,1);
    m_controls=new QWidget(splitter);m_controls->setMinimumWidth(300);auto *controlsLayout=new QVBoxLayout(m_controls);controlsLayout->setContentsMargins(0,0,6,0);controlsLayout->setSpacing(6);
    auto *categories=tabs(m_controls,"lineArtCategories",{"Lines","Shading","Output"},{"pen-tool","contrast","layers"});categories->setAccessibleName("Conversion settings");controlsLayout->addWidget(categories);
    auto *pages=new QStackedWidget(m_controls);controlsLayout->addWidget(pages,1);
    auto page=[&](){auto *scroll=new QScrollArea(pages);scroll->setWidgetResizable(true);scroll->setFrameShape(QFrame::NoFrame);auto *body=new QWidget;auto *box=new QVBoxLayout(body);box->setContentsMargins(4,4,4,4);box->setSpacing(9);scroll->setWidget(body);pages->addWidget(scroll);return qMakePair(body,box);};
    auto lines=page(),shading=page(),output=page();m_shadingPage=shading.first;
    auto scalar=[&](QPair<QWidget *,QVBoxLayout *> page,const char *id,const QString &title,double lo,double hi,double value,int decimals){auto *s=new ValueSlider(title,lo,hi,value,decimals,{},page.first);s->setObjectName(id);page.second->addWidget(new PropertyRow(title,s,page.first));return s;};
    auto color=[&](QPair<QWidget *,QVBoxLayout *> page,const QString &title,QColor LineArtSettings::*field){auto *b=new QToolButton(page.first);b->setFixedSize(30,28);b->setIconSize({20,20});b->setAccessibleName(title);b->setToolTip(title);page.second->addWidget(new PropertyRow(title,b,page.first));connect(b,&QToolButton::clicked,this,[this,field]{auto chosen=QColorDialog::getColor(m_settings.*field,this,"Choose conversion color",QColorDialog::ShowAlphaChannel);if(chosen.isValid()){m_settings.*field=chosen;refreshControls();schedulePreview();}});return b;};
    auto *background=new QPushButton(appIcon("image"),"Background cleanup",lines.first);
    background->setObjectName("lineArtBackgroundPreset");background->setAccessibleName("Apply background cleanup preset");
    background->setToolTip("Reduce texture noise and smooth background lines; keep current colors and shading output");
    background->setAutoDefault(false);lines.second->addWidget(background);
    connect(background,&QPushButton::clicked,this,[this]{setSettings(backgroundLineArtSettings(m_settings));});
    m_detail=scalar(lines,"lineArtDetail","Detail",0,100,65,0);m_width=scalar(lines,"lineArtWidth","Width px",.5,12,1.5,1);
    m_texture=scalar(lines,"lineArtTextureSuppression","Denoise %",0,100,55,0);
    m_texture->setToolTip("Suppress image texture and grain while preserving stronger boundaries");
    m_smoothing=scalar(lines,"lineArtSmoothing","Smooth",0,4,1.5,1);m_fragments=scalar(lines,"lineArtFragments","Dust px²",0,4096,3,0);m_fragments->setSliderRange(0,100);
    m_ink=color(lines,"Line color",&LineArtSettings::ink);
    lines.second->addStretch();
    m_ramp=new LineArtRamp(shading.first);m_ramp->setObjectName("lineArtRamp");shading.second->addWidget(m_ramp);
    m_dark=scalar(shading,"lineArtDark","Deep shade",1,253,85,0);m_bright=scalar(shading,"lineArtBright","Shade limit",2,254,175,0);
    m_shade=color(shading,"Shade color",&LineArtSettings::shade);m_deep=color(shading,"Deep color",&LineArtSettings::deepShade);
    m_spacing=scalar(shading,"lineArtSpacing","Dots px",2,64,8,1);m_angle=scalar(shading,"lineArtAngle","Angle °",0,180,45,0);
    auto *note=new QLabel("Image tones use the existing light and dark areas.",shading.first);note->setWordWrap(true);shading.second->addWidget(note);
    shading.second->addStretch();
    m_base=new QCheckBox("White base layer",output.first);m_base->setObjectName("lineArtBase");output.second->addWidget(m_base);
    m_hideSource=new QCheckBox("Hide original after conversion",output.first);m_hideSource->setObjectName("lineArtHideSource");m_hideSource->setChecked(!source.locked);m_hideSource->setEnabled(!source.locked);output.second->addWidget(m_hideSource);
    auto *outputNote=new QLabel("Editable raster layers\nOriginal source retained",output.first);outputNote->setWordWrap(true);output.second->addWidget(outputNote);output.second->addStretch();
    auto *previewArea=new QWidget(splitter);auto *previewLayout=new QVBoxLayout(previewArea);previewLayout->setContentsMargins(6,0,0,0);previewLayout->setSpacing(6);
    auto *comparison=tabs(previewArea,"lineArtComparison",{"Source","Result"});comparison->setAccessibleName("Preview comparison");comparison->setCurrentIndex(1);previewLayout->addWidget(comparison);
    m_preview=new LineArtPreview(previewArea);m_preview->setObjectName("lineArtPreview");previewLayout->addWidget(m_preview,1);splitter->setSizes({340,700});splitter->setCollapsible(0,false);splitter->setCollapsible(1,false);
    m_status=new QLabel(this);m_status->setObjectName("lineArtStatus");m_status->setTextFormat(Qt::PlainText);m_status->setWordWrap(true);layout->addWidget(m_status);
    auto *buttons=new QDialogButtonBox(QDialogButtonBox::Cancel,this);m_create=buttons->addButton("Create layers",QDialogButtonBox::AcceptRole);m_create->setObjectName("lineArtCreate");m_create->setDefault(false);m_create->setAutoDefault(false);layout->addWidget(buttons);
    connect(buttons,&QDialogButtonBox::rejected,this,&LineArtDialog::reject);connect(m_create,&QPushButton::clicked,this,&LineArtDialog::createLayers);
    connect(categories,&QTabBar::currentChanged,pages,&QStackedWidget::setCurrentIndex);
    connect(comparison,&QTabBar::currentChanged,this,[this](int index){m_preview->original=index==0;m_preview->update();});
    connect(m_presets,&QTabBar::currentChanged,this,[this](int index){if(m_loading)return;const LineArtShading modes[]={LineArtShading::None,LineArtShading::Cel,LineArtShading::Screentone,LineArtShading::Grayscale};m_settings.shading=modes[qBound(0,index,3)];m_settings.base=index!=0;refreshControls();schedulePreview();});
    auto bind=[&](ValueSlider *slider,auto member){connect(slider,&ValueSlider::valueChanged,this,[this,member](double value){if(!m_loading){m_settings.*member=value;schedulePreview();}});};
    bind(m_detail,&LineArtSettings::detail);bind(m_width,&LineArtSettings::width);bind(m_texture,&LineArtSettings::textureSuppression);bind(m_smoothing,&LineArtSettings::smoothing);bind(m_fragments,&LineArtSettings::fragments);bind(m_spacing,&LineArtSettings::toneSpacing);bind(m_angle,&LineArtSettings::toneAngle);
    connect(m_dark,&ValueSlider::valueChanged,this,[this](double v){if(m_loading)return;m_settings.darkCutoff=qMin(int(v),m_settings.lightCutoff-1);refreshControls();schedulePreview();});
    connect(m_bright,&ValueSlider::valueChanged,this,[this](double v){if(m_loading)return;m_settings.lightCutoff=qMax(int(v),m_settings.darkCutoff+1);refreshControls();schedulePreview();});
    connect(m_ramp,&LineArtRamp::edited,this,[this](int d,int b){m_settings.darkCutoff=d;m_settings.lightCutoff=b;refreshControls();schedulePreview();});
    connect(m_base,&QCheckBox::toggled,this,[this](bool v){if(!m_loading){m_settings.base=v;schedulePreview();}});
    m_timer=new QTimer(this);m_timer->setSingleShot(true);m_timer->setInterval(90);connect(m_timer,&QTimer::timeout,this,[this]{startJob(false);});
    connect(&m_watcher,&QFutureWatcher<LineArtResult>::finished,this,&LineArtDialog::finished);
    connect(document,&Document::changed,this,&LineArtDialog::invalidateContext);connect(document,&Document::frameChanged,this,&LineArtDialog::invalidateContext);connect(document,&Document::shotChanged,this,&LineArtDialog::invalidateContext);connect(document,&Document::selectionChanged,this,&LineArtDialog::invalidateContext);
    refreshControls();schedulePreview();
}
LineArtDialog::~LineArtDialog(){if(m_cancel)m_cancel->store(true);}
bool LineArtDialog::busy()const{return m_jobActive||m_timer->isActive()||m_pending;}
void LineArtDialog::setSettings(const LineArtSettings &s){if(!validateLineArtSettings(s).isEmpty()||m_final)return;m_settings=s;refreshControls();schedulePreview();}
void LineArtDialog::refreshControls(){
    m_loading=true;m_presets->setCurrentIndex(presetIndex(m_settings.shading));
    m_detail->setValueSilently(m_settings.detail);m_width->setValueSilently(m_settings.width);m_smoothing->setValueSilently(m_settings.smoothing);m_fragments->setValueSilently(m_settings.fragments);
    m_texture->setValueSilently(m_settings.textureSuppression);
    m_dark->setValueSilently(m_settings.darkCutoff);m_bright->setValueSilently(m_settings.lightCutoff);m_spacing->setValueSilently(m_settings.toneSpacing);m_angle->setValueSilently(m_settings.toneAngle);
    m_ramp->setValues(m_settings.darkCutoff,m_settings.lightCutoff);m_base->setChecked(m_settings.base);
    chip(m_ink,m_settings.ink);chip(m_shade,m_settings.shade);chip(m_deep,m_settings.deepShade);
    m_shadingPage->setEnabled(m_settings.shading!=LineArtShading::None);
    const bool cel=m_settings.shading==LineArtShading::Cel,tone=m_settings.shading==LineArtShading::Screentone;
    m_shade->setEnabled(cel);m_deep->setEnabled(cel);m_spacing->setEnabled(tone);m_angle->setEnabled(tone);
    m_shade->parentWidget()->setVisible(cel);m_deep->parentWidget()->setVisible(cel);
    m_spacing->parentWidget()->setVisible(tone);m_angle->parentWidget()->setVisible(tone);
    m_ramp->setEnabled(cel||tone);m_dark->setEnabled(cel||tone);m_bright->setEnabled(cel||tone);
    m_loading=false;
}
void LineArtDialog::invalidateContext(){
    if(m_applying||!m_document)return;
    if(m_document->contentRevision()==m_revision&&m_document->activeShotId()==m_shotId&&m_document->currentFrame()==m_frame&&m_document->activeShot().layers[m_document->activeLayer()].id==m_sourceId)return;
    m_invalid=true;m_timer->stop();m_pending=false;if(m_cancel)m_cancel->store(true);m_create->setEnabled(false);m_status->setText("The source changed. Close and reopen conversion to use the current drawing.");
}
void LineArtDialog::schedulePreview(){if(m_loading||m_invalid||m_final)return;m_create->setEnabled(false);if(m_cancel)m_cancel->store(true);m_timer->start();}
void LineArtDialog::startJob(bool final){
    if(m_invalid||!m_document)return;
    if(m_jobActive){m_pending=true;return;}
    m_pending=false;m_final=final;m_cancel=std::make_shared<std::atomic_bool>(false);
    const auto cancellation=m_cancel;const auto project=m_document->project();const auto shot=m_document->activeShotIndex(),frame=m_frame;const auto id=m_sourceId;const auto settings=m_settings;
    m_create->setEnabled(false);m_controls->setEnabled(!final);m_presets->setEnabled(!final);m_status->setText(final?"Creating full-resolution layers…":"Preparing preview…");
    m_jobActive=true;
    m_watcher.setFuture(QtConcurrent::run(&conversionPool(),[project,shot,frame,id,settings,cancellation,final]{
        try {QString error;const auto input=lineArtSource(project,shot,id,frame,&error);if(!error.isEmpty()){LineArtResult r;r.error=error;return r;}return convertLineArt(input,settings,cancellation,final?0:768);}
        catch(const std::exception &){LineArtResult r;r.error="The source could not be prepared within the available resources.";return r;}
    }));
}
void LineArtDialog::finished(){
    m_jobActive=false;
    if(m_invalid)return;
    auto result=m_watcher.result();
    if(m_pending||m_timer->isActive()||result.cancelled||(m_cancel&&m_cancel->load())){if(m_pending&&!m_timer->isActive())startJob(false);return;}
    m_controls->setEnabled(true);m_presets->setEnabled(true);
    if(!result.error.isEmpty()){m_final=false;m_status->setText(result.error);return;}
    if(m_final){
        QString failure;
        const auto errorConnection=connect(m_document,&Document::warning,this,[&failure](const QString &message){failure=message;});
        m_applying=true;bool success=m_document&&m_document->createLineArtLayers(m_sourceId,m_revision,m_frame,result,m_hideSource->isChecked());m_applying=false;m_final=false;
        disconnect(errorConnection);
        if(success){emit layersCreated();accept();return;}
        m_status->setText(failure.isEmpty()?"Layers could not be created. Check the source, folder locks and project limits.":failure);return;
    }
    m_previewResult=std::move(result);m_preview->source=m_previewResult.source;m_preview->result=m_previewResult.composite;m_preview->update();m_create->setEnabled(true);
    m_status->setText("Image lines · shading follows existing image tones");emit previewReady();
}
void LineArtDialog::createLayers(){if(busy()||m_invalid||m_final||!m_create->isEnabled())return;startJob(true);}
void LineArtDialog::reject(){m_timer->stop();m_pending=false;if(m_cancel)m_cancel->store(true);QDialog::reject();}
}
#include "line_art_dialog.moc"
