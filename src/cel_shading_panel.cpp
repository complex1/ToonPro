#include "cel_shading_panel.h"
#include "canvas.h"
#include "camera_tool.h"
#include "drawing_tool_controller.h"
#include "effect_render.h"
#include "keyframe_render.h"
#include "keyframe_transform_tool.h"
#include "mask_tool.h"
#include "icons.h"
#include "property_row.h"
#include "ui_metrics.h"
#include "value_slider.h"
#include <QColorDialog>
#include <QColorSpace>
#include <QComboBox>
#include <QHideEvent>
#include <QLabel>
#include <QLineF>
#include <QShowEvent>
#include <QKeyEvent>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QSignalBlocker>
#include <QScopedValueRollback>
#include <QTabBar>
#include <QTabletEvent>
#include <QToolButton>
#include <QThreadPool>
#include <QTimer>
#include <QVBoxLayout>
#include <QtConcurrentRun>
#include <cmath>

namespace toon {
namespace {
QThreadPool &shadingPool(){static QThreadPool pool;static const bool ready=[] {pool.setMaxThreadCount(1);return true;}();Q_UNUSED(ready);return pool;}
QToolButton *action(QWidget *parent,const char *id,const char *icon,const QString &help,bool toggle=false){
    auto *button=new QToolButton(parent);button->setObjectName(id);button->setIcon(appIcon(icon));
    button->setIconSize({ui::metrics::actionIcon,ui::metrics::actionIcon});button->setFixedSize(ui::metrics::actionTarget,ui::metrics::actionTarget);
    button->setProperty("compactDrawingAction",true);button->setCheckable(toggle);button->setToolTip(help);button->setAccessibleName(help);return button;
}
void chip(QToolButton *button,QColor color){
    QPixmap pixmap(40,40);pixmap.setDevicePixelRatio(2);pixmap.fill(Qt::transparent);
    {QPainter painter(&pixmap);painter.setPen(QColor("#b5b5b5"));painter.setBrush(color);painter.drawRoundedRect(QRectF(1,1,18,18),3,3);}
    QIcon icon;for(auto mode:{QIcon::Normal,QIcon::Active,QIcon::Selected,QIcon::Disabled})icon.addPixmap(pixmap,mode);button->setIcon(icon);button->setAccessibleDescription(color.name(QColor::HexArgb));
}
int layerIndex(const QVector<RasterLayer> &layers,const QString &id){for(int i=0;i<layers.size();++i)if(layers[i].id==id)return i;return -1;}
QImage rawOutline(const RasterLayer &layer,QSize size){QImage image(size,QImage::Format_ARGB32_Premultiplied);image.fill(Qt::transparent);QPainter painter(&image);paintRasterLayerPixels(painter,layer);return image;}
RasterLayer outputLayer(const QImage &image,const QString &name,const QString &parent){
    auto layer=RasterLayer::create(name);layer.parentId=parent;
    for(int y=0;y<image.height();y+=tileSize)for(int x=0;x<image.width();x+=tileSize){
        bool occupied=false;const QRect area=QRect(x,y,tileSize,tileSize).intersected(image.rect());
        for(int yy=area.top();yy<=area.bottom()&&!occupied;++yy){const auto *row=reinterpret_cast<const QRgb*>(image.constScanLine(yy));for(int xx=area.left();xx<=area.right();++xx)if(qAlpha(row[xx])){occupied=true;break;}}
        if(!occupied)continue;QImage tile(tileSize,tileSize,QImage::Format_ARGB32_Premultiplied);tile.fill(Qt::transparent);
        {QPainter painter(&tile);painter.drawImage(QPoint(),image,area);}layer.tiles.insert({x/tileSize,y/tileSize},tile);
    }
    return layer;
}
}
class CelShadingOverlay final:public QWidget{
public:
    CelShadingOverlay(Canvas *canvas,CelShadingPanel *panel):QWidget(canvas),m_panel(panel){setObjectName("celShadingOverlay");setAttribute(Qt::WA_TransparentForMouseEvents);setAttribute(Qt::WA_NoSystemBackground);setGeometry(canvas->rect());hide();}
protected:void paintEvent(QPaintEvent*)override{QPainter painter(this);m_panel->paintOverlay(painter);}
private:CelShadingPanel *m_panel;
};

CelShadingPanel::CelShadingPanel(Canvas *canvas,QWidget *parent):QWidget(parent),m_canvas(canvas){
    setObjectName("celShadingPanel");setProperty("compactDrawingPanel",true);setMinimumWidth(0);
    auto *layout=new QVBoxLayout(this);layout->setContentsMargins(ui::metrics::panelMargin,ui::metrics::panelMargin,ui::metrics::panelMargin,ui::metrics::panelMargin);layout->setSpacing(ui::metrics::controlGap);
    auto *header=new QHBoxLayout;header->setSpacing(4);m_enable=action(this,"celShadingEnabled","pen-tool","Draw shadow and highlight boundaries",true);header->addWidget(m_enable);
    m_outline=new QComboBox(this);m_outline->setObjectName("celShadingOutline");m_outline->setAccessibleName("Outline layer");m_outline->setToolTip("Transparent line-art layer used to enclose shading regions; its pixels remain unchanged");m_outline->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);m_outline->setMinimumContentsLength(1);header->addWidget(m_outline,1);layout->addLayout(header);
    auto tabs=[this](const char *name,const QStringList &titles,const QStringList &icons){auto *bar=new QTabBar(this);bar->setObjectName(name);bar->setProperty("compactDrawingTabs",true);bar->setExpanding(true);bar->setDrawBase(false);bar->setIconSize({16,16});bar->setFocusPolicy(Qt::StrongFocus);bar->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Fixed);bar->setElideMode(Qt::ElideRight);for(int i=0;i<titles.size();++i){bar->addTab(appIcon(icons[i]),titles[i]);bar->setTabToolTip(i,titles[i]);bar->setAccessibleTabName(i,titles[i]);}return bar;};
    m_kindTabs=tabs("celShadingKind",{"Shadow","Highlight"},{"contrast","sun"});m_kindTabs->setAccessibleName("Shading type");layout->addWidget(m_kindTabs);
    m_modeTabs=tabs("celShadingMode",{"Boundary","Fill"},{"pen-tool","paint-bucket"});m_modeTabs->setAccessibleName("Shading operation");layout->addWidget(m_modeTabs);
    auto *colors=new QWidget(this);auto *colorLayout=new QHBoxLayout(colors);colorLayout->setContentsMargins(0,0,0,0);colorLayout->setSpacing(4);
    m_color=action(this,"celShadingColor","palette","Use current drawing color for this shade");m_guideColor=action(this,"celShadingGuideColor","pen-tool","Choose boundary guide color; never changes finished artwork");colorLayout->addWidget(m_color);colorLayout->addWidget(m_guideColor);colorLayout->addStretch();layout->addWidget(new PropertyRow("Colors",colors,this));
    m_width=new ValueSlider("Guide width",1,12,2,0,{},this);m_width->setObjectName("celShadingWidth");layout->addWidget(new PropertyRow("Width",m_width,this));
    m_finishTabs=tabs("celShadingFinish",{"Hide","Color"},{"eye-off","palette"});m_finishTabs->setAccessibleName("Finished boundary lines");m_finishTabs->setTabToolTip(0,"Remove the guide line; paint meets at its centerline");m_finishTabs->setTabToolTip(1,"Color the guide width with each adjacent region's fill");layout->addWidget(new PropertyRow("Finish",m_finishTabs,this));
    m_status=new QLabel(this);m_status->setObjectName("celShadingStatus");m_status->setWordWrap(true);m_status->setTextFormat(Qt::PlainText);m_status->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Preferred);layout->addWidget(m_status);layout->addStretch();
    auto *footer=new QHBoxLayout;footer->setSpacing(4);m_undo=action(this,"celShadingUndo","undo-2","Undo last boundary or region");footer->addWidget(m_undo);footer->addStretch();m_cancel=action(this,"celShadingCancel","x","Discard shading preview");m_apply=action(this,"celShadingApply","check","Apply shading to separate layers");footer->addWidget(m_cancel);footer->addWidget(m_apply);layout->addLayout(footer);
    m_overlay=new CelShadingOverlay(canvas,this);canvas->installEventFilter(this);
    connect(m_enable,&QToolButton::toggled,this,&CelShadingPanel::setActive);
    connect(m_color,&QToolButton::clicked,this,&CelShadingPanel::useForeground);
    connect(m_guideColor,&QToolButton::clicked,this,[this]{auto &color=m_kindTabs->currentIndex()==0?m_shadowGuide:m_highlightGuide;const auto chosen=QColorDialog::getColor(color,this,"Boundary guide color");if(chosen.isValid()){color=chosen;persistDraft();notifyState();}});
    connect(m_kindTabs,&QTabBar::currentChanged,this,[this]{persistDraft();notifyState();});
    connect(m_modeTabs,&QTabBar::currentChanged,this,[this]{persistDraft();notifyState();});
    connect(m_width,&ValueSlider::editingFinished,this,[this]{persistDraft();});
    connect(m_finishTabs,&QTabBar::currentChanged,this,[this](int index){setBoundaryFinish(index?CelBoundaryFinish::RegionColor:CelBoundaryFinish::Hidden);});
    connect(m_outline,&QComboBox::currentIndexChanged,this,[this]{
        if(m_restoring)return;
        // The previous target is already stored. Switching the source selects
        // its own draft rather than overwriting or discarding another drawing.
        m_contextOutlineId=outlineLayerId();clearLocalDraft();restoreDraft(true);persistDraft();
    });
    connect(m_undo,&QToolButton::clicked,this,[this]{if(m_operations.isEmpty())return;const bool fill=m_operations.takeLast();if(fill&&!m_fills.isEmpty())m_fills.removeLast();else if(!fill&&!m_boundaries.isEmpty())m_boundaries.removeLast();persistDraft();renderPreview();notifyState();});
    connect(m_cancel,&QToolButton::clicked,this,&CelShadingPanel::cancelEditing);connect(m_apply,&QToolButton::clicked,this,&CelShadingPanel::apply);
    for(auto signal:{&Document::changed,&Document::shotChanged,&Document::frameChanged})connect(canvas->document(),signal,this,&CelShadingPanel::contextChanged);
    connect(canvas->document(),&Document::loaded,this,[this]{m_contextShotId.clear();m_contextOutlineId.clear();m_contextFrame=-1;setActive(false);clearLocalDraft();restoreDraft(true);});
    connect(canvas->document(),&Document::drawingAidsChanged,this,[this]{if(!m_publishing)restoreDraft();});
    connect(canvas,&Canvas::toolChanged,this,[this]{setActive(false);});
    connect(canvas,&Canvas::viewChanged,m_overlay,qOverload<>(&QWidget::update));
    connect(canvas,&Canvas::cameraViewChanged,m_overlay,qOverload<>(&QWidget::update));
    restoreDraft(true);notifyState();
}
CelShadingPanel::~CelShadingPanel(){invalidateWorker();if(m_canvas){m_canvas->removeEventFilter(this);if(m_active)m_canvas->refreshLayerSelectionContext();}delete m_overlay.data();}
QString CelShadingPanel::outlineLayerId()const{return m_outline->currentData().toString();}
void CelShadingPanel::setOutlineLayerId(const QString &id){const int index=m_outline->findData(id);if(index>=0)m_outline->setCurrentIndex(index);}
void CelShadingPanel::refreshSources(){
    const auto selected=outlineLayerId();QSignalBlocker blocker(m_outline);m_outline->clear();if(!m_canvas)return;auto *document=m_canvas->document();if(!document->isOpen())return;
    const auto &shot=document->activeShot();
    for(int i=shot.layers.size()-1;i>=0;--i){const auto &layer=shot.layers[i];if(layer.kind!=LayerKind::Raster||layer.group||layer.adjustment||layerIsAnnotation(shot.layers,i)||!layerPresentAtFrame(shot,layer.id,document->currentFrame()))continue;m_outline->addItem(layer.name,layer.id);}
    int index=m_outline->findData(selected);if(index<0&&document->activeLayer()>=0&&document->activeLayer()<shot.layers.size())index=m_outline->findData(shot.layers[document->activeLayer()].id);if(index>=0)m_outline->setCurrentIndex(index);
}
QString CelShadingPanel::unavailableReason()const{
    if(!m_canvas)return "Open a drawing to shade.";
    auto *document=m_canvas->document();if(!document->isOpen())return "Open a drawing to shade.";
    if(m_canvas->animationPlayback())return "Pause playback to shade a drawing.";
    if(document->editingGestureActive()||document->layerPropertyEditActive())return "Finish the current edit before shading.";
    const auto &shot=document->activeShot();const int at=layerIndex(shot.layers,outlineLayerId());if(at<0)return "Choose a transparent line-art layer.";
    const auto &source=shot.layers[at];if(source.kind!=LayerKind::Raster||source.group||source.adjustment||source.scene3d||layerIsAnnotation(shot.layers,at))return "The saved outline is not a raster line-art layer.";if(!layerPresentAtFrame(shot,source.id,document->currentFrame()))return "That drawing is not exposed at this frame.";
    if(!document->keyframeSourceEditing()&&!worldLayerTransform(shot,source.id,document->currentFrame()).isIdentity())return "Enable source drawing editing before shading a transformed layer.";
    QString parent=source.parentId;while(!parent.isEmpty()){const int index=layerIndex(shot.layers,parent);if(index<0)return "The drawing folder is unavailable.";const auto &folder=shot.layers[index];if(folder.locked||!folder.visible)return "Unlock and show the drawing folder before shading.";parent=folder.parentId;}
    if(!source.visible)return "Show the outline layer before shading.";
    if(qint64(document->project().width)*document->project().height>16000000)return "Cel shading supports canvases up to 16 million pixels.";
    return {};
}
void CelShadingPanel::setActive(bool active){
    if(active==m_active){notifyState();return;}
    if(active&&!m_canvas)return;
    if(active){m_canvas->finishStroke();m_canvas->drawingTools()->finishGesture();if(!m_canvas->resolvePendingTransform()){notifyState();return;}const auto reason=unavailableReason();if(!reason.isEmpty()){setStatus(reason);notifyState();return;}m_canvas->maskTool()->setEnabled(false);m_canvas->cameraTool()->setEnabled(false);m_canvas->keyframeTransformTool()->setEnabled(false);m_canvas->setCursor(Qt::CrossCursor);}
    else{m_dragging=false;m_tablet=false;m_livePath={};if(m_canvas)m_canvas->refreshLayerSelectionContext();}
    m_active=active;notifyState();emit activeChanged(active);
}
void CelShadingPanel::setStatus(const QString &message){m_status->setText(message);m_status->setToolTip(message);}
void CelShadingPanel::notifyState(){
    if(!m_canvas||!m_overlay)return;
    {QSignalBlocker blocker(m_enable);m_enable->setChecked(m_active);}chip(m_color,m_kindTabs->currentIndex()==0?m_shadow:m_highlight);chip(m_guideColor,m_kindTabs->currentIndex()==0?m_shadowGuide:m_highlightGuide);
    m_apply->setEnabled(!m_busy&&!m_fills.isEmpty()&&m_preview.rejectedFills.isEmpty()&&unavailableReason().isEmpty());m_cancel->setEnabled(pending()||m_busy);m_undo->setEnabled(!m_operations.isEmpty());
    m_overlay->setVisible(isVisible()&&(m_active||pending())&&!m_canvas->document()->editingGestureActive());if(m_overlay->isVisible())m_overlay->raise();m_overlay->update();
    const bool now=pending();if(now!=m_lastPending){m_lastPending=now;emit pendingChanged(now);}
}
CelShadingDraft CelShadingPanel::currentDraft() const {
    CelShadingDraft draft;
    draft.shotId=m_contextShotId;draft.outlineLayerId=m_contextOutlineId;draft.frame=qMax(0,m_contextFrame);
    draft.boundaries=m_boundaries;draft.fills=m_fills;draft.operations=m_operations;draft.finish=m_finish;
    draft.shadow=m_shadow;draft.highlight=m_highlight;draft.shadowGuide=m_shadowGuide;draft.highlightGuide=m_highlightGuide;
    draft.width=m_width->value();draft.kind=m_kindTabs->currentIndex();draft.mode=m_modeTabs->currentIndex();
    return draft;
}
bool CelShadingPanel::persistDraft() {
    if(m_restoring||m_publishing)return true;
    if(!m_canvas||!m_canvas->hasProject())return false;
    auto *document=m_canvas->document();
    if(m_contextShotId.isEmpty()){
        m_contextShotId=document->activeShotId();m_contextFrame=document->currentFrame();m_contextOutlineId=outlineLayerId();
    }
    if(m_contextOutlineId.isEmpty())return m_boundaries.isEmpty()&&m_fills.isEmpty();
    auto aids=document->project().drawingAids;
    const auto draft=currentDraft();
    auto &drafts=aids.shadingDrafts;
    int existing=-1;
    for(int i=0;i<drafts.size();++i)if(drafts[i].shotId==draft.shotId&&drafts[i].outlineLayerId==draft.outlineLayerId&&drafts[i].frame==draft.frame){existing=i;break;}
    const bool hasWork=!draft.boundaries.isEmpty()||!draft.fills.isEmpty();
    if(!hasWork){if(existing<0){m_persistedDraft.reset();return true;}drafts.removeAt(existing);}
    else {
        if(existing>=0&&celShadingDraftEqual(drafts[existing],draft)&&existing==drafts.size()-1){m_persistedDraft=draft;return true;}
        if(existing>=0)drafts.removeAt(existing);
        drafts.append(draft);
    }
    aids.activeShotId=document->activeShotId();aids.activeFrame=document->currentFrame();
    QScopedValueRollback<bool> publishing(m_publishing,true);
    QString error;
    if(!document->setDrawingAids(aids,&error)){setStatus(error.isEmpty()?"Could not store the shading draft.":error);return false;}
    m_persistedDraft=hasWork?std::optional<CelShadingDraft>(draft):std::nullopt;
    return true;
}
bool CelShadingPanel::finishPersistence() {
    if(m_dragging){setStatus("Finish the current boundary stroke before saving or changing drawings.");return false;}
    m_width->commitEditing();
    if(!pending())return true;
    return persistDraft();
}
void CelShadingPanel::restoreDraft(bool sourceChanged) {
    if(m_restoring||m_publishing||m_applying||!m_canvas)return;
    auto *document=m_canvas->document();
    if(!document->isOpen()){clearLocalDraft();m_contextShotId.clear();m_contextOutlineId.clear();m_contextFrame=-1;m_persistedDraft.reset();refreshSources();notifyState();return;}
    const auto shotId=document->activeShotId();const int frame=document->currentFrame();
    const bool sameContext=m_contextShotId==shotId&&m_contextFrame==frame;
    QString desired=sameContext?m_contextOutlineId:QString();
    const auto &drafts=document->project().drawingAids.shadingDrafts;
    if(desired.isEmpty())for(int i=drafts.size()-1;i>=0;--i)if(drafts[i].shotId==shotId&&drafts[i].frame==frame){desired=drafts[i].outlineLayerId;break;}
    if(desired.isEmpty()&&document->activeLayer()>=0&&document->activeLayer()<document->activeShot().layers.size())desired=document->activeShot().layers[document->activeLayer()].id;
    const CelShadingDraft *saved=nullptr;
    for(const auto &draft:drafts)if(draft.shotId==shotId&&draft.frame==frame&&draft.outlineLayerId==desired){saved=&draft;break;}
    // Another drawing aid can publish while a guide or width adjustment is in
    // progress. Only replace local work when this draft's stored inputs changed.
    if(!sourceChanged&&sameContext&&desired==m_contextOutlineId&&
        ((saved&&m_persistedDraft&&celShadingDraftEqual(*saved,*m_persistedDraft))||(!saved&&!m_persistedDraft)))return;
    // A value copy is essential: local signals may synchronously update document metadata.
    const auto copy=saved?std::optional<CelShadingDraft>(*saved):std::nullopt;
    QScopedValueRollback<bool> restoring(m_restoring,true);
    if(!sameContext)setActive(false);
    clearLocalDraft();m_contextShotId=shotId;m_contextFrame=frame;m_contextOutlineId=desired;m_persistedDraft=copy;
    refreshSources();
    {
        const QSignalBlocker blocker(m_outline);
        int index=m_outline->findData(desired);
        if(index<0&&copy){m_outline->addItem("Unavailable outline · saved draft",desired);index=m_outline->count()-1;}
        if(index>=0)m_outline->setCurrentIndex(index);
        else m_contextOutlineId=outlineLayerId();
    }
    if(copy){
        const auto &draft=*copy;m_boundaries=draft.boundaries;m_fills=draft.fills;m_operations=draft.operations;m_finish=draft.finish;
        m_shadow=draft.shadow;m_highlight=draft.highlight;m_shadowGuide=draft.shadowGuide;m_highlightGuide=draft.highlightGuide;
        m_width->setValueSilently(draft.width);
        {const QSignalBlocker kind(m_kindTabs),mode(m_modeTabs),finish(m_finishTabs);
            m_kindTabs->setCurrentIndex(draft.kind);m_modeTabs->setCurrentIndex(draft.mode);m_finishTabs->setCurrentIndex(draft.finish==CelBoundaryFinish::Hidden?0:1);}
        m_previewDirty=!m_fills.isEmpty();
        const auto reason=unavailableReason();
        setStatus(reason.isEmpty()?"Saved shading draft restored.":reason+" Draft retained.");
        if(m_previewDirty&&isVisible()&&reason.isEmpty())renderPreview();
    } else setStatus({});
    notifyState();emit previewChanged();
}
void CelShadingPanel::contextChanged(){
    if(m_applying||m_publishing||!m_canvas)return;
    restoreDraft(true);
}
void CelShadingPanel::invalidateWorker(){++m_generation;if(m_cancelled)m_cancelled->store(true,std::memory_order_relaxed);m_cancelled.reset();m_busy=false;}
void CelShadingPanel::clearLocalDraft(){
    invalidateWorker();m_boundaries.clear();m_fills.clear();m_operations.clear();m_livePath={};m_dragging=m_tablet=false;m_preview={};m_previewComposite={};m_previewDirty=false;
}
void CelShadingPanel::cancelEditing(){
    clearLocalDraft();persistDraft();setStatus({});notifyState();emit previewChanged();
}
bool CelShadingPanel::finishEditing(){
    if(!pending()&&!m_busy)return true;
    const auto choice=QMessageBox::question(this,"Finish cel shading","Apply the previewed shading before continuing?",QMessageBox::Apply|QMessageBox::Discard|QMessageBox::Cancel,QMessageBox::Apply);
    if(choice==QMessageBox::Discard){cancelEditing();return true;}if(choice!=QMessageBox::Apply)return false;return apply();
}
void CelShadingPanel::setBoundaryFinish(CelBoundaryFinish finish){if(m_finish==finish)return;m_finish=finish;{QSignalBlocker blocker(m_finishTabs);m_finishTabs->setCurrentIndex(finish==CelBoundaryFinish::Hidden?0:1);}persistDraft();renderPreview();}
void CelShadingPanel::useForeground(){if(!m_canvas)return;auto &color=m_kindTabs->currentIndex()==0?m_shadow:m_highlight;color=m_canvas->brush.color;persistDraft();notifyState();}
bool CelShadingPanel::addBoundary(const QPainterPath &path,CelShadeKind kind){
    const auto reason=unavailableReason();if(!reason.isEmpty()){setStatus(reason);return false;}
    if(path.elementCount()<2||path.elementCount()>32768||m_boundaries.size()>=256){setStatus("A preview supports up to 256 boundaries.");return false;}
    for(int i=0;i<path.elementCount();++i){const auto e=path.elementAt(i);if(!std::isfinite(e.x)||!std::isfinite(e.y)||std::abs(e.x)>1000000||std::abs(e.y)>1000000)return false;}
    m_boundaries.append({path,kind,kind==CelShadeKind::Shadow?m_shadowGuide:m_highlightGuide,m_width->value()});m_operations.append(false);if(!persistDraft()){m_boundaries.removeLast();m_operations.removeLast();notifyState();return false;}renderPreview();notifyState();return true;
}
bool CelShadingPanel::addFill(QPointF point,CelShadeKind kind,QColor color){
    const auto reason=unavailableReason();if(!reason.isEmpty()){setStatus(reason);return false;}
    if(!std::isfinite(point.x())||!std::isfinite(point.y())||std::abs(point.x())>1000000||std::abs(point.y())>1000000||!color.isValid()||color.alpha()==0||m_fills.size()>=64){setStatus("Choose a visible color; a preview supports up to 64 regions.");return false;}
    m_fills.append({QPoint(int(std::floor(point.x())),int(std::floor(point.y()))),kind,color});m_operations.append(true);if(!persistDraft()){m_fills.removeLast();m_operations.removeLast();notifyState();return false;}renderPreview();notifyState();return true;
}
void CelShadingPanel::renderPreview(){
    invalidateWorker();m_preview={};m_previewComposite={};m_previewDirty=false;
    if(!m_canvas)return;auto *document=m_canvas->document();if(!document->isOpen())return;
    if(m_fills.isEmpty()){setStatus(m_boundaries.isEmpty()?QString():"Choose Fill and click an enclosed region.");notifyState();return;}
    const auto reason=unavailableReason();if(!reason.isEmpty()){setStatus(reason+" Draft retained.");notifyState();return;}
    const int at=layerIndex(document->activeShot().layers,outlineLayerId());if(at<0)return;
    m_shotId=document->activeShotId();m_frame=document->currentFrame();const auto source=document->activeShot().layers[at];const auto shot=document->activeShot();const auto project=document->project();const int frame=document->currentFrame();const bool sourceOnly=document->keyframeSourceEditing();const QSize size(document->project().width,document->project().height);
    const auto boundaries=m_boundaries;const auto fills=m_fills;const auto finish=m_finish;const auto generation=m_generation;
    m_cancelled=std::make_shared<std::atomic_bool>(false);const auto cancellation=m_cancelled;m_busy=true;setStatus("Preparing shading preview…");notifyState();
    using WorkerResult=QPair<CelShadingResult,QImage>;auto *watcher=new QFutureWatcher<WorkerResult>(this);
    connect(watcher,&QFutureWatcher<WorkerResult>::finished,this,[this,watcher,generation]{
        auto result=watcher->result();watcher->deleteLater();completePreview(generation,std::move(result.first),std::move(result.second));
    });
    watcher->setFuture(QtConcurrent::run(&shadingPool(),[source,size,shot,project,at,frame,sourceOnly,boundaries,fills,finish,cancellation]() -> WorkerResult {
        try {
        if(cancellation->load(std::memory_order_relaxed)){CelShadingResult cancelled;cancelled.cancelled=true;return WorkerResult(cancelled,{});}
        const auto outline=rawOutline(source,size);auto result=renderCelShading(outline,boundaries,fills,finish,cancellation);
        if(result.cancelled||!result.error.isEmpty()||cancellation->load(std::memory_order_relaxed))return WorkerResult(result,{});
        auto prospective=shot;int index=at;
        const auto shadow=outputLayer(result.shadow,"Shadows",source.parentId),highlight=outputLayer(result.highlight,"Highlights",source.parentId);
        if(!shadow.tiles.isEmpty())prospective.layers.insert(index++,shadow);if(!highlight.tiles.isEmpty())prospective.layers.insert(index,highlight);
        const QSize previewSize=size.scaled(QSize(2048,2048),Qt::KeepAspectRatio).boundedTo(size);
        QImage composite(previewSize,QImage::Format_ARGB32_Premultiplied);composite.setColorSpace(QColorSpace::SRgb);composite.fill(Qt::white);
        if(composite.isNull()){result.error="Not enough memory to prepare the shading preview.";return WorkerResult(result,{});}
        EffectRenderCancellationScope effectCancellation(cancellation.get());QString error;
        const auto layers=evaluateLayers(project,prospective,frame,sourceOnly,QRectF(QPointF(),size),&error);
        if(!error.isEmpty()){result.error=error;return WorkerResult(result,{});}
        {QPainter painter(&composite);painter.scale(qreal(previewSize.width())/size.width(),qreal(previewSize.height())/size.height());paintLayers(painter,layers,true,nullptr,true,size);}
        if(!effectRenderError().isEmpty())result.error=effectRenderError();
        if(!keyframeRenderError().isEmpty())result.error=keyframeRenderError();
        if(cancellation->load(std::memory_order_relaxed)){result.cancelled=true;return WorkerResult(result,{});}
        return WorkerResult(result,composite);
        } catch(const std::exception &){CelShadingResult failed;failed.error="Could not prepare the shading preview. Try fewer regions.";return WorkerResult(failed,{});}
    }));
}
void CelShadingPanel::completePreview(quint64 generation,CelShadingResult result,QImage composite){
    if(generation!=m_generation||!m_canvas)return;
    if(m_canvas->document()->editingGestureActive()||m_canvas->document()->layerPropertyEditActive()){
        QTimer::singleShot(50,this,[this,generation,result=std::move(result),composite=std::move(composite)]() mutable {
            completePreview(generation,std::move(result),std::move(composite));
        });return;
    }
    m_busy=false;
    if(result.cancelled||(m_cancelled&&m_cancelled->load(std::memory_order_relaxed))){notifyState();return;}
    m_preview=std::move(result);m_previewComposite=std::move(composite);
    if(!m_preview.error.isEmpty())setStatus(m_preview.error);
    else if(!m_preview.rejectedFills.isEmpty())
        setStatus("A region is open or on a line. Correct the boundary or undo the region; the draft is retained.");
    else setStatus(QString("%1 region(s) previewed · Apply creates separate shade layers").arg(m_fills.size()));
    notifyState();emit previewChanged();
}
bool CelShadingPanel::apply(){
    if(m_previewDirty){renderPreview();setStatus("Preparing the restored draft. Apply when its preview is ready.");return false;}
    if(m_busy){setStatus("Wait for the shading preview to finish before applying.");return false;}
    const auto reason=unavailableReason();if(!reason.isEmpty()){setStatus(reason);return false;}
    if(m_fills.isEmpty()){cancelEditing();return true;}
    if(!m_preview.error.isEmpty()||!m_preview.rejectedFills.isEmpty()||m_preview.shadow.isNull()||m_preview.highlight.isNull()){setStatus("Create a valid shading preview first.");return false;}
    auto *document=m_canvas->document();if(document->activeShotId()!=m_shotId||document->currentFrame()!=m_frame){restoreDraft(true);setStatus("The drawing changed. Review its saved draft before applying.");return false;}
    auto project=document->project();auto &shot=project.shots[document->activeShotIndex()];int at=layerIndex(shot.layers,outlineLayerId());if(at<0)return false;const auto parent=shot.layers[at].parentId;
    const auto shadow=outputLayer(m_preview.shadow,"Shadows",parent),highlight=outputLayer(m_preview.highlight,"Highlights",parent);
    QStringList added;if(!shadow.tiles.isEmpty()){shot.layers.insert(at++,shadow);added.append(shadow.id);}if(!highlight.tiles.isEmpty()){shot.layers.insert(at,highlight);added.append(highlight.id);}
    if(added.isEmpty()){cancelEditing();return true;}const auto error=project.validate();if(!error.isEmpty()){setStatus(error);return false;}
    m_applying=true;document->replaceProject(project,"Apply cel shading");m_applying=false;
    for(const auto &id:added)if(layerIndex(document->activeShot().layers,id)<0){setStatus("Shading could not be applied; the preview is retained.");return false;}
    cancelEditing();refreshSources();setStatus("Shading applied to separate layers.");return true;
}
QTransform CelShadingPanel::canvasTransform()const{if(!m_canvas||!m_canvas->hasProject())return {};auto transform=m_canvas->viewTransform();const auto origin=sheetGeometry(m_canvas->project(),m_canvas->shot()).output.topLeft();transform.translate(origin.x(),origin.y());return m_canvas->cameraTransform()*transform;}
void CelShadingPanel::paintOverlay(QPainter &painter){
    if(!m_canvas||!m_canvas->hasProject()||m_canvas->document()->editingGestureActive())return;painter.setTransform(canvasTransform());painter.setRenderHint(QPainter::Antialiasing,true);painter.setRenderHint(QPainter::SmoothPixmapTransform,m_canvas->canvasSmoothing());
    if(!m_previewComposite.isNull())painter.drawImage(QRectF(0,0,m_canvas->project().width,m_canvas->project().height),m_previewComposite);
    if(m_modeTabs->currentIndex()==0)for(const auto &boundary:m_boundaries){painter.setPen(QPen(boundary.guideColor,boundary.width,Qt::SolidLine,Qt::RoundCap,Qt::RoundJoin));painter.drawPath(boundary.path);}
    if(m_dragging){painter.setPen(QPen(m_kindTabs->currentIndex()==0?m_shadowGuide:m_highlightGuide,m_width->value(),Qt::SolidLine,Qt::RoundCap,Qt::RoundJoin));painter.drawPath(m_livePath);}
}
void CelShadingPanel::drawStart(QPointF position){if(!unavailableReason().isEmpty()){setStatus(unavailableReason());return;}m_canvas->setFocus(Qt::MouseFocusReason);const auto point=m_canvas->artworkPoint(position);
    if(m_modeTabs->currentIndex()==1){addFill(point,m_kindTabs->currentIndex()==0?CelShadeKind::Shadow:CelShadeKind::Highlight,m_kindTabs->currentIndex()==0?m_shadow:m_highlight);return;}
    m_dragging=true;m_livePath=QPainterPath(point);notifyState();}
void CelShadingPanel::drawMove(QPointF position){if(!m_dragging)return;const auto point=m_canvas->artworkPoint(position);if(QLineF(point,m_livePath.currentPosition()).length()<.25)return;if(m_livePath.elementCount()<32768)m_livePath.lineTo(point);m_overlay->update();}
void CelShadingPanel::drawFinish(QPointF position){if(!m_dragging)return;drawMove(position);m_dragging=false;const auto path=m_livePath;m_livePath={};addBoundary(path,m_kindTabs->currentIndex()==0?CelShadeKind::Shadow:CelShadeKind::Highlight);notifyState();}
void CelShadingPanel::showEvent(QShowEvent *event){QWidget::showEvent(event);if(m_previewDirty)renderPreview();notifyState();}
void CelShadingPanel::hideEvent(QHideEvent *event){setActive(false);if(m_overlay)m_overlay->hide();QWidget::hideEvent(event);}
bool CelShadingPanel::eventFilter(QObject *watched,QEvent *event){
    if(!m_canvas||watched!=m_canvas)return false;if(event->type()==QEvent::Resize){m_overlay->setGeometry(m_canvas->rect());return false;}
    if(!m_active||!isVisible()){
        // A stored draft must not cover fresh artwork while an ordinary pen
        // stroke is in progress. Document::changed regenerates it on release.
        if(event->type()==QEvent::MouseButtonPress||event->type()==QEvent::TabletPress){
            if(m_overlay)m_overlay->hide();
            if(m_cancelled)m_cancelled->store(true,std::memory_order_relaxed);
        }
        return false;
    }
    if(event->type()==QEvent::KeyPress||event->type()==QEvent::KeyRelease){auto *key=static_cast<QKeyEvent*>(event);if(key->key()==Qt::Key_Space){m_space=event->type()==QEvent::KeyPress;if(m_dragging){m_dragging=false;m_livePath={};notifyState();}return false;}if(event->type()==QEvent::KeyPress&&key->key()==Qt::Key_Escape){if(m_dragging){m_dragging=false;m_livePath={};notifyState();}else setActive(false);event->accept();return true;}}
    if(event->type()==QEvent::FocusOut||event->type()==QEvent::WindowDeactivate){m_space=false;if(m_dragging){m_dragging=false;m_livePath={};notifyState();}m_tablet=false;return false;}
    if(m_space)return false;
    if(event->type()==QEvent::MouseButtonPress||event->type()==QEvent::MouseMove||event->type()==QEvent::MouseButtonRelease){auto *mouse=static_cast<QMouseEvent*>(event);if(mouse->source()!=Qt::MouseEventNotSynthesized||m_tablet){event->accept();return true;}if(mouse->button()==Qt::MiddleButton||mouse->buttons().testFlag(Qt::MiddleButton))return false;
        m_canvas->setCursor(Qt::CrossCursor);if(event->type()==QEvent::MouseButtonPress&&mouse->button()==Qt::LeftButton)drawStart(mouse->position());else if(event->type()==QEvent::MouseMove){if(mouse->buttons().testFlag(Qt::LeftButton))drawMove(mouse->position());else if(m_dragging)drawFinish(mouse->position());}else if(event->type()==QEvent::MouseButtonRelease&&mouse->button()==Qt::LeftButton)drawFinish(mouse->position());else return false;event->accept();return true;
    }
    if(event->type()==QEvent::TabletPress||event->type()==QEvent::TabletMove||event->type()==QEvent::TabletRelease){auto *tablet=static_cast<QTabletEvent*>(event);m_canvas->setCursor(Qt::CrossCursor);if(event->type()==QEvent::TabletPress&&tablet->button()==Qt::LeftButton){m_tablet=true;drawStart(tablet->position());}else if(event->type()==QEvent::TabletMove&&m_tablet){if(tablet->buttons().testFlag(Qt::LeftButton))drawMove(tablet->position());else{drawFinish(tablet->position());m_tablet=false;}}else if(event->type()==QEvent::TabletRelease&&m_tablet){drawFinish(tablet->position());m_tablet=false;}event->accept();return true;}
    return false;
}
} // namespace toon
