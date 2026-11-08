#include "perspective_dialog.h"
#include "document.h"
#include "guide_geometry.h"
#include "icons.h"
#include "property_row.h"
#include "ui_metrics.h"
#include "ui_theme.h"
#include "value_slider.h"
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QListWidget>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QSplitter>
#include <QTabBar>
#include <QThreadPool>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <QtConcurrentRun>
#include <algorithm>
#include <cmath>

namespace toon {
namespace {
QThreadPool &perspectivePool(){static QThreadPool pool;static const bool once=[]{pool.setMaxThreadCount(1);return true;}();Q_UNUSED(once);return pool;}
QTabBar *tabs(QWidget *parent,const char *id,const QStringList &labels){
    auto *bar=new QTabBar(parent);bar->setObjectName(id);bar->setExpanding(true);bar->setDocumentMode(true);bar->setDrawBase(false);bar->setProperty("compactDrawingTabs",true);
    for(const auto &label:labels){const int at=bar->addTab(label);bar->setTabToolTip(at,label);bar->setAccessibleTabName(at,label);}return bar;
}
QToolButton *action(QWidget *parent,const char *id,const char *icon,const QString &help){auto *b=new QToolButton(parent);b->setObjectName(id);b->setIcon(appIcon(icon));b->setIconSize({ui::metrics::actionIcon,ui::metrics::actionIcon});b->setFixedSize(ui::metrics::actionTarget,ui::metrics::actionTarget);b->setAccessibleName(help);b->setToolTip(help);return b;}
qreal distanceToSegment(QPointF p,QLineF line){auto d=line.p2()-line.p1();const qreal n=QPointF::dotProduct(d,d);if(n<=0)return QLineF(p,line.p1()).length();const auto t=qBound(0.,QPointF::dotProduct(p-line.p1(),d)/n,1.);return QLineF(p,line.p1()+t*d).length();}
QColor familyColor(int index){return QColor(index%2?ui::onionPrevious:ui::onionNext);}
}
class PerspectivePreview final:public QWidget {
    Q_OBJECT
public:
    explicit PerspectivePreview(QWidget *parent):QWidget(parent){setMinimumSize(300,280);setFocusPolicy(Qt::StrongFocus);setMouseTracking(true);setAccessibleName("Perspective source and editable guide preview");setToolTip("Drag a vanishing-point circle or the anchor cross; click an edge to exclude it. Wheel zooms; middle drag pans. Region and Two lines select analysis tools.");}
    QImage image;
    QVector<PerspectiveSegment> segments;
    QVector<PerspectiveCandidate> candidates;
    QVector<int> chosen;
    QVector<DrawingGuide> guides;
    QRect region;
    QPointF anchor;
    int interaction=0;
    bool showEdges=true;
    void clearInteraction(){manual.clear();dragging=false;panning=false;handle=-1;space=false;update();}
    void fit(bool includeGuides=false){
        QRectF area(QPointF(),image.size());
        if(includeGuides)for(int i:chosen)if(i>=0&&i<candidates.size()&&!candidates[i].parallel)area|=QRectF(candidates[i].point-QPointF(10,10),QSizeF(20,20));
        if(area.isEmpty())return;center=area.center();zoom=qMin((width()-44)/area.width(),(height()-44)/area.height());zoom=qMax(.00001,zoom);fitted=true;update();
    }
    QTransform mapping() const {return QTransform(zoom,0,0,zoom,width()/2.-center.x()*zoom,height()/2.-center.y()*zoom);}
signals:
    void regionChanged(QRect);
    void segmentToggled(int);
    void candidateMoved(int,QPointF);
    void anchorMoved(QPointF);
    void manualLines(QLineF,QLineF);
protected:
    void resizeEvent(QResizeEvent *)override{if(!fitted&&!image.isNull()){fit();fitted=true;}}
    void paintEvent(QPaintEvent *)override{
        QPainter p(this);p.fillRect(rect(),QColor(ui::canvas));if(image.isNull()){p.setPen(QColor(ui::muted));p.drawText(rect().adjusted(16,16,-16,-16),Qt::AlignCenter|Qt::TextWordWrap,"Choose a layer or image to detect perspective");return;}
        const auto map=mapping();const QRectF imageRect=map.mapRect(QRectF(QPointF(),image.size()));p.fillRect(imageRect,Qt::white);p.setRenderHint(QPainter::SmoothPixmapTransform);p.drawImage(imageRect,image);
        p.setRenderHint(QPainter::Antialiasing);p.save();p.setClipRect(imageRect);
        if(showEdges)for(int i=0;i<segments.size();++i){const auto &s=segments[i];QColor color=s.enabled?QColor(ui::muted):QColor(ui::disabled);for(int c=0;c<candidates.size();++c)if(chosen.contains(c)&&candidates[c].supportingSegments.contains(i))color=familyColor(c);color.setAlpha(s.enabled?180:80);p.setPen(QPen(color,s.enabled?1.5:1.,s.enabled?Qt::SolidLine:Qt::DotLine));p.drawLine(map.map(s.line));}
        for(int i=0;i<guides.size();++i){auto color=familyColor(i);color.setAlpha(160);p.setPen(QPen(color,1));p.drawPath(map.map(guidePath(guides[i],QRectF(QPointF(),image.size()))));}
        p.restore();
        if(!region.isEmpty()){p.setPen(QPen(QColor(ui::focus),1.5,Qt::DashLine));p.setBrush(Qt::NoBrush);p.drawRect(map.mapRect(QRectF(region)));}
        for(int i:chosen)if(i>=0&&i<candidates.size()&&!candidates[i].parallel){const auto pos=map.map(candidates[i].point);p.setPen(QPen(familyColor(i),2));p.setBrush(QColor(ui::panel));p.drawEllipse(pos,7,7);p.drawText(pos+QPointF(10,-8),QString::number(i+1));}
        if(!guides.isEmpty()){const auto a=map.map(anchor);p.setPen(QPen(QColor(ui::text),2));p.drawLine(a-QPointF(7,0),a+QPointF(7,0));p.drawLine(a-QPointF(0,7),a+QPointF(0,7));}
        p.setPen(QPen(QColor(ui::focus),2));for(const auto &line:manual)p.drawLine(map.map(line));
        if(dragging&&interaction==1){p.setPen(QPen(QColor(ui::focus),1.5,Qt::DashLine));p.drawRect(map.mapRect(QRectF(start,current).normalized()));}
        if(dragging&&interaction==2)p.drawLine(map.map(QLineF(start,current)));
    }
    void mousePressEvent(QMouseEvent *e)override{
        if(image.isNull())return;setFocus();last=e->position();
        if(e->button()==Qt::MiddleButton||(e->button()==Qt::LeftButton&&space)){panning=true;return;}
        if(e->button()!=Qt::LeftButton)return;
        start=current=mapping().inverted().map(e->position());dragging=true;handle=-1;
        if(interaction==0){for(int i:chosen)if(!candidates[i].parallel&&QLineF(mapping().map(candidates[i].point),e->position()).length()<12){handle=i;return;}
            if(!guides.isEmpty()&&QLineF(mapping().map(anchor),e->position()).length()<12){handle=-2;return;}}
    }
    void mouseMoveEvent(QMouseEvent *e)override{
        if(panning){center-=(e->position()-last)/zoom;last=e->position();update();return;}
        if(!dragging)return;current=mapping().inverted().map(e->position());
        if(handle>=0){candidates[handle].point=current;emit candidateMoved(handle,current);}
        else if(handle==-2){anchor=current;emit anchorMoved(anchor);}update();
    }
    void mouseReleaseEvent(QMouseEvent *e)override{
        if(panning){panning=false;return;}if(!dragging)return;dragging=false;
        current=mapping().inverted().map(e->position());
        if(interaction==1){region=QRectF(start,current).normalized().toAlignedRect().intersected(QRect(QPoint(),image.size()));if(region.width()<8||region.height()<8)region={};emit regionChanged(region);}
        else if(interaction==2){if(QLineF(start,current).length()>5){manual.append(QLineF(start,current));if(manual.size()==2){emit manualLines(manual[0],manual[1]);manual.clear();}}}
        else if(handle==-1&&QLineF(mapping().map(start),e->position()).length()<5){int near=-1;qreal best=8;for(int i=0;i<segments.size();++i){const auto d=distanceToSegment(e->position(),mapping().map(segments[i].line));if(d<best){best=d;near=i;}}if(near>=0)emit segmentToggled(near);}
        update();
    }
    void wheelEvent(QWheelEvent *e)override{const auto before=mapping().inverted().map(e->position());zoom=qBound(.00001,zoom*std::pow(1.0015,e->angleDelta().y()),50.);center+=before-mapping().inverted().map(e->position());update();e->accept();}
    void keyPressEvent(QKeyEvent *e)override{if(e->key()==Qt::Key_Space){space=true;e->accept();return;}if(e->key()==Qt::Key_Escape&&!manual.isEmpty()){manual.clear();dragging=false;update();e->accept();return;}QWidget::keyPressEvent(e);}
    void keyReleaseEvent(QKeyEvent *e)override{if(e->key()==Qt::Key_Space){space=false;return;}QWidget::keyReleaseEvent(e);}
private:
    QPointF center,last,start,current;
    qreal zoom=1;
    int handle=-1;
    bool fitted=false,dragging=false,panning=false,space=false;
    QVector<QLineF> manual;
};
PerspectiveDialog::PerspectiveDialog(Document *document,QWidget *parent):QDialog(parent),m_document(document){
    setObjectName("perspectiveDialog");setWindowTitle("Detect perspective");setWindowModality(Qt::WindowModal);resize(1080,730);setMinimumSize(740,500);
    m_shotId=document->activeShotId();m_sourceId=document->activeShot().layers[document->activeLayer()].id;m_revision=document->contentRevision();m_frame=document->currentFrame();
    auto *layout=new QVBoxLayout(this);layout->setContentsMargins(12,12,12,12);layout->setSpacing(8);
    auto *sourceRow=new QHBoxLayout;m_sources=tabs(this,"perspectiveSourceTabs",{"Selected layer","Image file"});m_sources->setAccessibleName("Perspective source");sourceRow->addWidget(m_sources,1);
    m_chooseFile=action(this,"perspectiveChooseImage","folder-open","Choose an image file for perspective detection");sourceRow->addWidget(m_chooseFile);layout->addLayout(sourceRow);
    m_sourceName=new QLabel(document->activeShot().layers[document->activeLayer()].name,this);m_sourceName->setTextFormat(Qt::PlainText);m_sourceName->setWordWrap(true);m_sourceName->setObjectName("perspectiveSourceName");layout->addWidget(m_sourceName);
    auto *split=new QSplitter(this);layout->addWidget(split,1);auto *controls=new QWidget(split);controls->setMinimumWidth(260);auto *box=new QVBoxLayout(controls);box->setContentsMargins(0,0,6,0);box->setSpacing(8);
    m_modes=tabs(controls,"perspectiveModeTabs",{"Auto","One-point","Two-point"});m_modes->setAccessibleName("Perspective model");box->addWidget(m_modes);
    m_sensitivity=new ValueSlider("Edge detail",0,100,50,0,{},controls);m_sensitivity->setObjectName("perspectiveSensitivity");box->addWidget(new PropertyRow("Detail",m_sensitivity,controls));
    m_density=new ValueSlider("Grid density",1,64,8,0,{},controls);m_density->setObjectName("perspectiveDensity");box->addWidget(new PropertyRow("Density",m_density,controls));
    auto *directions=new QLabel("Directions",controls);box->addWidget(directions);
    m_candidates=new QListWidget(controls);m_candidates->setObjectName("perspectiveCandidates");m_candidates->setAccessibleName("Choose perspective directions");m_candidates->setMinimumHeight(100);box->addWidget(m_candidates,1);
    auto *note=new QLabel("Choose up to two vanishing points. Parallel directions add rulers. The source stays unchanged.",controls);note->setWordWrap(true);box->addWidget(note);
    auto *previewArea=new QWidget(split);auto *previewLayout=new QVBoxLayout(previewArea);previewLayout->setContentsMargins(4,0,0,0);
    auto *tools=new QHBoxLayout;m_interaction=tabs(previewArea,"perspectiveInteractionTabs",{"Adjust","Region","Two lines"});m_interaction->setAccessibleName("Perspective preview tool");m_interaction->setTabToolTip(0,"Drag vanishing points and anchor; click a detected edge to exclude or restore it");m_interaction->setTabToolTip(1,"Drag an image region, then analyze only that region");m_interaction->setTabToolTip(2,"Draw two straight lines that share a vanishing direction; repeat for a second direction");tools->addWidget(m_interaction,1);
    auto *fitImage=action(previewArea,"perspectiveFitImage","scan","Fit the source image");auto *fitGuides=action(previewArea,"perspectiveFitGuides","maximize-2","Fit the image and chosen vanishing points");auto *reset=action(previewArea,"perspectiveReset","rotate-ccw","Reset region, exclusions and manual corrections");tools->addWidget(fitImage);tools->addWidget(fitGuides);tools->addWidget(reset);previewLayout->addLayout(tools);
    m_preview=new PerspectivePreview(previewArea);m_preview->setObjectName("perspectivePreview");previewLayout->addWidget(m_preview,1);split->setSizes({280,760});split->setCollapsible(0,false);split->setCollapsible(1,false);
    m_status=new QLabel(this);m_status->setObjectName("perspectiveStatus");m_status->setTextFormat(Qt::PlainText);m_status->setWordWrap(true);layout->addWidget(m_status);
    auto *buttons=new QDialogButtonBox(QDialogButtonBox::Cancel,this);m_add=buttons->addButton("Add guides",QDialogButtonBox::AcceptRole);m_add->setObjectName("perspectiveAddGuides");m_add->setAutoDefault(false);m_add->setEnabled(false);layout->addWidget(buttons);
    m_timer=new QTimer(this);m_timer->setSingleShot(true);m_timer->setInterval(100);
    connect(m_timer,&QTimer::timeout,this,&PerspectiveDialog::startJob);connect(&m_watcher,&QFutureWatcher<PerspectivePreviewJob>::finished,this,&PerspectiveDialog::finished);
    connect(buttons,&QDialogButtonBox::rejected,this,&PerspectiveDialog::reject);connect(m_add,&QPushButton::clicked,this,&PerspectiveDialog::addGuides);
    auto choose=[this]{const auto path=QFileDialog::getOpenFileName(this,"Choose perspective image",{},"Images (*.png *.jpg *.jpeg *.webp *.bmp *.tif *.tiff)");if(!path.isEmpty())useImageFile(path);else if(!m_fileSource){QSignalBlocker block(m_sources);m_sources->setCurrentIndex(0);}};
    connect(m_chooseFile,&QToolButton::clicked,this,choose);connect(m_sources,&QTabBar::currentChanged,this,[this,choose](int i){if(i==0)useSelectedLayer();else if(!m_fileSource)choose();});
    connect(m_modes,&QTabBar::currentChanged,this,[this]{m_settings.mode=mode();schedule(!m_result.segments.isEmpty());});
    connect(m_sensitivity,&ValueSlider::valueChanged,this,[this](qreal value){m_settings.sensitivity=value/100.;schedule();});
    connect(m_density,&ValueSlider::valueChanged,this,[this]{refreshProposal();});
    connect(m_candidates,&QListWidget::itemChanged,this,[this]{if(!m_refreshing)refreshProposal();});
    connect(m_interaction,&QTabBar::currentChanged,this,[this](int i){m_preview->interaction=i;m_preview->update();});
    connect(fitImage,&QToolButton::clicked,this,[this]{m_preview->fit();});connect(fitGuides,&QToolButton::clicked,this,[this]{m_preview->fit(true);});
    connect(reset,&QToolButton::clicked,this,[this]{m_settings.region={};m_preview->region={};schedule();});
    connect(m_preview,&PerspectivePreview::regionChanged,this,[this](QRect r){m_settings.region=r;schedule();});
    connect(m_preview,&PerspectivePreview::segmentToggled,this,[this](int i){if(busy()||i<0||i>=m_result.segments.size())return;m_result.segments[i].enabled=!m_result.segments[i].enabled;schedule(true);});
    connect(m_preview,&PerspectivePreview::candidateMoved,this,[this](int i,QPointF point){if(i<0||i>=m_result.candidates.size())return;m_result.candidates[i].point=point;m_result.candidates[i].strong=false;refreshProposal();});
    connect(m_preview,&PerspectivePreview::anchorMoved,this,[this](QPointF){refreshProposal();});
    connect(m_preview,&PerspectivePreview::manualLines,this,[this](QLineF a,QLineF b){
        if(busy()||m_invalid||m_source.image.isNull())return;auto candidate=perspectiveFromLines(a,b);if(!candidate){m_status->setText("Use two distinct straight edges; a single repeated line cannot locate perspective.");return;}candidate->supportingSegments.clear();
        if(m_manualFamilies==0){m_refreshing=true;for(int i=0;i<m_candidates->count();++i)m_candidates->item(i)->setCheckState(Qt::Unchecked);m_refreshing=false;}
        const auto selected=chosenCandidates();m_result.candidates.append(*candidate);++m_manualFamilies;refreshCandidates(false);
        m_refreshing=true;for(int i=0;i<m_candidates->count();++i)m_candidates->item(i)->setCheckState(selected.contains(i)||i==m_candidates->count()-1?Qt::Checked:Qt::Unchecked);m_refreshing=false;refreshProposal();
    });
    connect(document,&Document::changed,this,&PerspectiveDialog::invalidateContext);connect(document,&Document::frameChanged,this,&PerspectiveDialog::invalidateContext);connect(document,&Document::shotChanged,this,&PerspectiveDialog::invalidateContext);connect(document,&Document::selectionChanged,this,&PerspectiveDialog::invalidateContext);
    connect(document,&QObject::destroyed,this,[this]{m_invalid=true;m_ready=false;m_timer->stop();m_pending=false;if(m_cancel)m_cancel->store(true);m_add->setEnabled(false);m_status->setText("The drawing was closed.");});
    schedule();
}
PerspectiveDialog::~PerspectiveDialog(){if(m_cancel)m_cancel->store(true);}
bool PerspectiveDialog::busy()const{return m_running||m_pending||m_timer->isActive();}
PerspectiveMode PerspectiveDialog::mode()const{return PerspectiveMode(qBound(0,m_modes->currentIndex(),2));}
void PerspectiveDialog::useImageFile(const QString &path){if(m_invalid||path.isEmpty())return;m_filePath=path;m_fileSource=true;m_source={};m_settings.region={};m_preview->region={};{QSignalBlocker b(m_sources);m_sources->setCurrentIndex(1);}schedule();}
void PerspectiveDialog::useSelectedLayer(){if(m_invalid)return;m_fileSource=false;m_source={};m_settings.region={};m_preview->region={};{QSignalBlocker b(m_sources);m_sources->setCurrentIndex(0);}schedule();}
void PerspectiveDialog::schedule(bool refit){if(m_invalid)return;++m_generation;m_preview->clearInteraction();m_ready=false;m_refit=refit;m_manualFamilies=0;if(m_cancel)m_cancel->store(true);m_add->setEnabled(false);m_timer->start();m_status->setText("Detecting perspective…");}
void PerspectiveDialog::startJob(){
    if(m_invalid||!m_document)return;if(m_running){m_pending=true;return;}m_pending=false;m_running=true;m_runningGeneration=m_generation;
    m_cancel=std::make_shared<std::atomic_bool>(false);const auto cancel=m_cancel;
    const auto cached=m_source;const auto settings=m_settings;const auto segments=m_result.segments;const bool refit=m_refit&&!cached.image.isNull();
    const bool file=m_fileSource;const auto path=m_filePath;const auto project=m_document->project();const int shot=m_document->activeShotIndex(),frame=m_frame;const auto id=m_sourceId;const bool sourceEditing=m_document->keyframeSourceEditing();
    m_watcher.setFuture(QtConcurrent::run(&perspectivePool(),[cached,settings,segments,refit,file,path,project,shot,frame,id,sourceEditing,cancel]{
        PerspectivePreviewJob job;
        try{job.source=cached;if(job.source.image.isNull())job.source=file?loadPerspectiveImage(path,{project.width,project.height},cancel.get()):capturePerspectiveSource(project,shot,id,frame,sourceEditing,cancel.get());
            if(!job.source.error.isEmpty()){job.detection.error=job.source.error;return job;}
            if(cancel->load()){job.detection.cancelled=true;return job;}
            const auto stopped=[cancel]{return cancel->load();};job.detection=refit?fitPerspectiveSegments(segments,job.source.image.size(),settings,stopped):detectPerspective(job.source.image,settings,stopped);
        }catch(const std::exception &){job.detection.error="The source could not be analyzed within the available resources.";}return job;
    }));
}
void PerspectiveDialog::finished(){
    m_running=false;if(m_invalid)return;auto job=m_watcher.result();
    if(m_runningGeneration!=m_generation||job.detection.cancelled||(m_cancel&&m_cancel->load())){if(!m_timer->isActive())startJob();return;}
    m_pending=false;if(!job.detection.error.isEmpty()){m_ready=false;m_status->setText(job.detection.error);m_add->setEnabled(false);emit previewReady();return;}
    const bool fresh=m_source.image.isNull();m_source=std::move(job.source);m_result=std::move(job.detection);m_preview->image=m_source.image;m_preview->segments=m_result.segments;m_preview->region=m_settings.region;
    m_sourceName->setText(m_source.description+(m_fileSource?" · fitted to drawing; image remains external":" · read-only layer snapshot"));
    if(fresh){m_preview->anchor=QPointF(m_source.image.width()*.5,m_source.image.height()*.7);m_preview->fit();}
    m_ready=true;refreshCandidates(true);refreshProposal();emit previewReady();
}
QVector<int> PerspectiveDialog::chosenCandidates()const{QVector<int> chosen;for(int i=0;i<m_candidates->count();++i)if(m_candidates->item(i)->checkState()==Qt::Checked)chosen.append(i);return chosen;}
void PerspectiveDialog::refreshCandidates(bool defaults){
    m_refreshing=true;const auto old=chosenCandidates();m_candidates->clear();
    int finite=0,parallel=0;
    for(int i=0;i<m_result.candidates.size();++i){const auto &c=m_result.candidates[i];const bool check=defaults?(c.parallel?parallel++<1:finite++<(mode()==PerspectiveMode::OnePoint?1:2)):old.contains(i);
        const auto title=c.supportingSegments.isEmpty()?QString("Manual %1 %2").arg(c.parallel?"parallel":"vanishing point").arg(i+1):QString("%1 %2 · %3 edges").arg(c.parallel?"Parallel":"Vanishing point").arg(i+1).arg(c.supportingSegments.size());
        auto *item=new QListWidgetItem(title,m_candidates);item->setFlags(item->flags()|Qt::ItemIsUserCheckable);item->setCheckState(check?Qt::Checked:Qt::Unchecked);item->setToolTip(c.strong?"Strong support across detected edges":"Review suggested: inspect the detected lines and adjust if needed");}
    m_refreshing=false;
}
void PerspectiveDialog::refreshProposal(){
    auto chosen=chosenCandidates();const int finiteCount=std::count_if(chosen.cbegin(),chosen.cend(),[this](int i){return !m_result.candidates[i].parallel;});
    m_proposal=createPerspectiveGuides(m_result.candidates,chosen,m_preview->anchor,QRectF(QPointF(),m_source.image.size()),mode());
    for(auto &g:m_proposal)g.subdivisions=qRound(m_density->value());
    m_preview->candidates=m_result.candidates;m_preview->chosen=chosen;m_preview->guides=m_proposal;m_preview->update();
    const int maxFinite=mode()==PerspectiveMode::OnePoint?1:2;
    const bool valid=m_ready&&!m_invalid&&!busy()&&!m_proposal.isEmpty()&&finiteCount<=maxFinite;
    m_add->setEnabled(valid);
    if(m_invalid)return;
    if(finiteCount>maxFinite)m_status->setText(maxFinite==1?"Choose one vanishing direction in One-point mode.":"Choose at most two vanishing directions for this guide set.");
    else if(m_proposal.isEmpty())m_status->setText(m_result.message.isEmpty()?"Not enough supported directions. Choose a region or use Two lines to place a direction.":m_result.message);
    else {bool strong=!chosen.isEmpty();for(int i:chosen)strong&=m_result.candidates[i].strong;m_status->setText(strong?"Strong match · review the grid, then Add guides":"Review suggested · adjust points or use Two lines, then Add guides");}
}
void PerspectiveDialog::invalidateContext(){
    if(m_applying||!m_document)return;
    if(m_document->isOpen()&&m_document->contentRevision()==m_revision&&m_document->activeShotId()==m_shotId&&m_document->currentFrame()==m_frame&&m_document->activeShot().layers[m_document->activeLayer()].id==m_sourceId)return;
    m_invalid=true;m_timer->stop();m_pending=false;if(m_cancel)m_cancel->store(true);m_add->setEnabled(false);m_status->setText("The drawing or source changed. Close and reopen detection to use the current image.");
}
void PerspectiveDialog::addGuides(){
    m_density->commitEditing();if(busy()||m_invalid||!m_add->isEnabled()||!m_document)return;
    auto guides=m_proposal;for(auto &g:guides){for(auto &point:g.points)point=m_source.imageToArtwork.map(point);g.area=m_source.imageToArtwork.mapRect(g.area);}
    QString reason;const auto connection=connect(m_document,&Document::warning,this,[&reason](const QString &s){reason=s;});m_applying=true;
    const bool success=m_document->addPerspectiveGuides(guides,m_shotId,m_revision,m_frame,m_sourceId);m_applying=false;disconnect(connection);
    if(success){emit guidesAdded();accept();}else m_status->setText(reason.isEmpty()?"The guides could not be added. Check the source and guide limits.":reason);
}
void PerspectiveDialog::reject(){m_invalid=true;m_ready=false;m_timer->stop();m_pending=false;if(m_cancel)m_cancel->store(true);m_add->setEnabled(false);QDialog::reject();}
}
#include "perspective_dialog.moc"
