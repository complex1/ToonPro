#include "cel_paint_check_panel.h"
#include "canvas.h"
#include "icons.h"
#include "property_row.h"
#include "reference_panel.h"
#include "ui_metrics.h"
#include "ui_theme.h"
#include "value_slider.h"
#include <QComboBox>
#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QScopedValueRollback>
#include <QSignalBlocker>
#include <QTabWidget>
#include <QToolButton>
#include <QTimer>
#include <QHideEvent>
#include <QShowEvent>
#include <QVBoxLayout>
#include <QtConcurrentRun>
#include <functional>

namespace toon {
namespace {
class CheckOverlay final:public QWidget {
public:
    CheckOverlay(QWidget *parent,std::function<void(QPainter&)> paint):QWidget(parent),m_paint(std::move(paint)) {
        setAttribute(Qt::WA_TransparentForMouseEvents);setAttribute(Qt::WA_NoSystemBackground);
        setAttribute(Qt::WA_TranslucentBackground);setFocusPolicy(Qt::NoFocus);
    }
protected:
    void paintEvent(QPaintEvent*)override{QPainter painter(this);m_paint(painter);}
private:
    std::function<void(QPainter&)>m_paint;
};
QColor issueColor(CelPaintIssueKind kind) {
    switch(kind){case CelPaintIssueKind::Hole:return QColor("#ff74b9");case CelPaintIssueKind::Leak:return QColor("#ff9a4b");
    case CelPaintIssueKind::Stray:return QColor("#ffdb67");case CelPaintIssueKind::OutsidePalette:return QColor("#66d6ee");}
    return Qt::white;
}
QString layerLabel(const Shot &shot,const RasterLayer &layer) {
    QStringList names{layer.name};QString parent=layer.parentId;QSet<QString>seen;
    while(!parent.isEmpty()&&!seen.contains(parent)){
        seen.insert(parent);bool found=false;for(const auto &entry:shot.layers)if(entry.id==parent){names.prepend(entry.name);parent=entry.parentId;found=true;break;}
        if(!found)break;
    }
    return names.join(" / ");
}
}
CelPaintCheckPanel::CelPaintCheckPanel(Canvas *canvas,QWidget *parent):QWidget(parent),m_canvas(canvas),m_document(canvas->document()) {
    setObjectName("celPaintCheckPanel");setProperty("compactDrawingPanel",true);setMinimumWidth(210);
    auto *layout=new QVBoxLayout(this);layout->setContentsMargins(4,4,4,4);layout->setSpacing(4);
    const auto action=[this](const QString&id,const QString&icon,const QString&label,bool text=false){
        auto*b=new QToolButton(this);b->setObjectName(id);b->setIcon(appIcon(icon));b->setAccessibleName(label);b->setToolTip(label);
        b->setProperty("compactDrawingAction",true);b->setIconSize({ui::metrics::actionIcon,ui::metrics::actionIcon});
        if(text){b->setText(label);b->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);b->setMinimumHeight(ui::metrics::actionTarget);}
        else b->setFixedSize(ui::metrics::actionTarget,ui::metrics::actionTarget);return b;
    };
    auto *top=new QHBoxLayout;top->setSpacing(4);
    m_check=action("celPaintCheckRun","toon-paint-check","Check",true);
    m_check->setToolTip("Inspect checked raster drawings in the artwork frame. Results are suspects; review before repair.");
    top->addWidget(m_check);top->addStretch();
    m_previous=action("celPaintCheckPrevious","chevron-left","Previous problem");m_next=action("celPaintCheckNext","chevron-right","Next problem");
    top->addWidget(m_previous);top->addWidget(m_next);layout->addLayout(top);
    m_tabs=new QTabWidget(this);m_tabs->setObjectName("celPaintCheckTabs");m_tabs->setProperty("compactDrawingTabs",true);
    auto *drawings=new QWidget;auto *inputs=new QVBoxLayout(drawings);inputs->setContentsMargins(0,4,0,0);inputs->setSpacing(4);
    m_palette=new QComboBox; m_palette->setObjectName("celPaintCheckPalette");m_palette->setAccessibleName("Approved character palette");m_palette->setProperty("compactDrawingField",true);
    m_palette->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);m_palette->setMinimumContentsLength(4);
    m_palette->setMinimumWidth(0);m_palette->setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Fixed);
    inputs->addWidget(new PropertyRow("Palette",m_palette));
    m_tolerance=new ValueSlider("Color tolerance",0,64,12,0);m_tolerance->setObjectName("celPaintCheckTolerance");
    m_holes=new ValueSlider("Largest automatically repairable hole in pixels",1,4096,256,0);m_holes->setSliderRange(1,512);m_holes->setObjectName("celPaintCheckHoleSize");
    m_strays=new ValueSlider("Largest stray island in pixels",1,256,4,0);m_strays->setObjectName("celPaintCheckStraySize");
    inputs->addWidget(new PropertyRow("Tolerance",m_tolerance));inputs->addWidget(new PropertyRow("Fill limit",m_holes));inputs->addWidget(new PropertyRow("Stray size",m_strays));
    m_targets=new QListWidget;m_targets->setObjectName("celPaintCheckTargets");m_targets->setAccessibleName("Drawings to inspect");m_targets->setMinimumHeight(90);
    m_targets->setToolTip("Check paint layers from this shot, including drawings exposed on other frames. Line art is inspected separately.");inputs->addWidget(m_targets,1);
    m_tabs->addTab(drawings,appIcon("layers"),"Drawings");m_tabs->setTabToolTip(0,"Drawing targets and check settings");
    auto *results=new QWidget;auto *review=new QVBoxLayout(results);review->setContentsMargins(0,4,0,0);review->setSpacing(4);
    m_problems=new QListWidget;m_problems->setObjectName("celPaintCheckProblems");m_problems->setAccessibleName("Suspected paint problems");m_problems->setMinimumHeight(90);review->addWidget(m_problems,1);
    auto *previewRow=new QHBoxLayout; m_caption=new QLabel("Source drawing");m_caption->setTextFormat(Qt::PlainText);m_caption->setWordWrap(true);
    m_original=action("celPaintCheckOriginal","eye-off","Show original drawing");m_original->setCheckable(true);previewRow->addWidget(m_caption,1);previewRow->addWidget(m_original);review->addLayout(previewRow);
    m_preview=new ReferenceView;m_preview->setObjectName("celPaintCheckPreview");m_preview->setMinimumHeight(110);m_preview->setEmptyText("Run a check, then select a problem");review->addWidget(m_preview,1);
    m_tabs->addTab(results,appIcon("toon-paint-check"),"Problems");m_tabs->setTabToolTip(1,"Review suspects and compare repairs with the original");layout->addWidget(m_tabs,1);
    m_status=new QLabel("Choose drawings, then Check.");m_status->setObjectName("celPaintCheckStatus");m_status->setTextFormat(Qt::PlainText);m_status->setWordWrap(true);layout->addWidget(m_status);
    auto *bottom=new QHBoxLayout;bottom->setSpacing(4);
    m_previewButton=action("celPaintCheckPreviewRepairs","eye","Preview",true);m_previewButton->setToolTip("Preview checked repairs: holes use the foreground, stray/leak regions are erased, other colors snap to the approved palette.");
    m_apply=action("celPaintCheckApply","check","Apply",true);m_cancel=action("celPaintCheckCancel","x","Cancel check or repair preview");
    bottom->addWidget(m_previewButton);bottom->addStretch();bottom->addWidget(m_apply);bottom->addWidget(m_cancel);layout->addLayout(bottom);
    m_overlay=new CheckOverlay(canvas,[this](QPainter&p){
        if(!m_canvas||!m_document||m_highlightBounds.isEmpty()||!m_canvas->hasProject()||m_document->editingGestureActive())return;
        auto transform=m_canvas->viewTransform();const auto origin=sheetGeometry(m_canvas->project(),m_canvas->shot()).output.topLeft();transform.translate(origin.x(),origin.y());
        transform=m_canvas->cameraTransform()*transform;
        const int issueIndex=m_problems->currentRow();
        if(!m_document->keyframeSourceEditing() && issueIndex>=0 && issueIndex<m_result.issues.size()) {
            const auto &target=m_result.targets[m_result.issues[issueIndex].target];
            transform=worldLayerTransform(m_document->activeShot(),target.layerId,m_document->currentFrame())*transform;
        }
        p.setTransform(transform);
        if(!m_highlightMask.isNull())p.drawImage(m_highlightBounds.topLeft(),m_highlightMask);
        QPen pen(QColor("#ffbd66"),1.5,Qt::DashLine);pen.setCosmetic(true);p.setPen(pen);p.setBrush(Qt::NoBrush);p.drawRect(QRectF(m_highlightBounds).adjusted(-2,-2,2,2));
    });
    m_overlay->setObjectName("celPaintCheckOverlay");m_overlay->setGeometry(canvas->rect());m_overlay->hide();canvas->installEventFilter(this);
    connect(canvas,&Canvas::viewChanged,m_overlay,qOverload<>(&QWidget::update));
    connect(canvas,&Canvas::cameraViewChanged,m_overlay,qOverload<>(&QWidget::update));
    connect(m_check,&QToolButton::clicked,this,&CelPaintCheckPanel::startCheck);
    connect(m_previewButton,&QToolButton::clicked,this,&CelPaintCheckPanel::previewRepairs);
    connect(m_apply,&QToolButton::clicked,this,&CelPaintCheckPanel::applyRepairs);
    connect(m_cancel,&QToolButton::clicked,this,&CelPaintCheckPanel::cancelEditing);
    connect(m_previous,&QToolButton::clicked,this,[this]{const int count=m_problems->count();if(count)m_problems->setCurrentRow((m_problems->currentRow()+count-1)%count);});
    connect(m_next,&QToolButton::clicked,this,[this]{const int count=m_problems->count();if(count)m_problems->setCurrentRow((m_problems->currentRow()+1)%count);});
    connect(m_problems,&QListWidget::currentRowChanged,this,[this]{showProblem();});
    connect(m_problems,&QListWidget::itemChanged,this,[this]{if(!m_refreshing){discardRepairs();showProblem(false);updateActions();}});
    connect(m_original,&QToolButton::toggled,this,[this]{showProblem(false);});
    connect(m_document,&Document::changed,this,[this]{invalidate();refreshSources();});
    connect(m_document,&Document::loaded,this,[this]{invalidate();refreshSources();});
    connect(m_document,&Document::shotChanged,this,[this]{invalidate();refreshSources();});
    connect(m_document,&Document::frameChanged,this,[this]{if(!m_navigating){m_overlay->hide();m_highlightBounds={};}});
    connect(m_palette,&QComboBox::currentIndexChanged,this,[this]{
        if(m_refreshing)return;
        const auto characterId=m_palette->currentData().toString(),variantId=m_palette->currentData(Qt::UserRole+1).toString();
        if(!selectPalette(characterId,variantId))syncPaletteSelection();
    });
    for(auto *value:{m_tolerance,m_holes,m_strays})connect(value,&ValueSlider::valueChanged,this,[this]{if(!m_refreshing)invalidate();});
    connect(m_targets,&QListWidget::itemChanged,this,[this]{if(!m_refreshing)invalidate();});
    connect(&m_checkWorker,&QFutureWatcher<CelPaintCheckResult>::finished,this,&CelPaintCheckPanel::finishCheck);
    connect(&m_repairWorker,&QFutureWatcher<CelPaintRepairResult>::finished,this,&CelPaintCheckPanel::finishRepair);
    connect(canvas,&QObject::destroyed,this,[this]{
        if(m_checkCancellation)m_checkCancellation->store(true);
        if(m_repairCancellation)m_repairCancellation->store(true);
        setPending(false);setEnabled(false);
    });
    refreshSources();updateActions();
}
void CelPaintCheckPanel::finishCheck(){
    if(!m_document||!m_canvas){m_checking=false;return;}
    if(m_document->editingGestureActive()||m_document->layerPropertyEditActive()){
        QTimer::singleShot(50,this,&CelPaintCheckPanel::finishCheck);return;
    }

        m_checking=false;const auto result=m_checkWorker.result();
        if(m_checkRevision!=m_revision||result.cancelled||(m_checkCancellation&&m_checkCancellation->load())){updateActions();return;}
        m_result=result;
        if(!result.error.isEmpty()){status(result.error);updateActions();emit inspectionFinished();return;}
        QScopedValueRollback<bool>refresh(m_refreshing,true);m_problems->clear();
        for(const auto &issue:result.issues){
            const auto &target=result.targets[issue.target];auto *item=new QListWidgetItem(celPaintIssueName(issue.kind)+" · "+QString::number(issue.pixelCount),m_problems);
            item->setData(Qt::UserRole,m_problems->count()-1);item->setCheckState(Qt::Unchecked);
            item->setToolTip(target.name+"\n"+celPaintIssueName(issue.kind)+(issue.kind==CelPaintIssueKind::Leak?" — reaches frame edge; may be intentional":"")
                +(issue.repairable?"\nCheck this item to preview a repair.":"\nLarge region: manual repair only."));
            if(!issue.repairable)item->setFlags(item->flags()&~Qt::ItemIsUserCheckable);
        }
        status(result.issues.isEmpty()?"No suspects found in the checked raw paint layers.":QString::number(result.issues.size())+" suspects. Check only the repairs you want."+(result.truncated?" Large results are inspection-only.":""));
        m_tabs->setCurrentIndex(1);if(m_problems->count())m_problems->setCurrentRow(0);updateActions();emit inspectionFinished();

}
void CelPaintCheckPanel::finishRepair(){
    if(!m_document||!m_canvas){m_preparing=false;return;}
    if(m_document->editingGestureActive()||m_document->layerPropertyEditActive()){
        QTimer::singleShot(50,this,&CelPaintCheckPanel::finishRepair);return;
    }

        m_preparing=false;const auto result=m_repairWorker.result();
        if(m_repairRevision!=m_revision||result.cancelled||(m_repairCancellation&&m_repairCancellation->load())){updateActions();return;}
        m_repairs=result;
        if(!result.error.isEmpty()){setPending(false);status(result.error);}else{setPending(true);status("Repair preview · "+QString::number(result.drawings.size())+" drawings. Apply commits one Undo.");}
        showProblem(false);updateActions();emit previewFinished();

}
CelPaintCheckPanel::~CelPaintCheckPanel(){if(m_checkCancellation)m_checkCancellation->store(true);if(m_repairCancellation)m_repairCancellation->store(true);delete m_overlay.data();}
bool CelPaintCheckPanel::pending()const{return m_pending;}
void CelPaintCheckPanel::setPending(bool value){if(m_pending==value)return;m_pending=value;emit pendingChanged(value);}
void CelPaintCheckPanel::status(const QString&message){m_status->setText(message);}
int CelPaintCheckPanel::paletteIndex(const QString &characterId,const QString &variantId)const{
    for(int index=0;index<m_palette->count();++index)
        if(m_palette->itemData(index).toString()==characterId&&m_palette->itemData(index,Qt::UserRole+1).toString()==variantId)return index;
    return -1;
}
void CelPaintCheckPanel::syncPaletteSelection(){
    const QSignalBlocker blocker(m_palette);
    m_palette->setCurrentIndex(paletteIndex(m_paletteCharacterId,m_paletteVariantId));
    m_palette->setToolTip(m_palette->currentText());
}
bool CelPaintCheckPanel::selectPalette(const QString &characterId,const QString &variantId){
    if(!m_document||!m_document->isOpen())return false;
    if(paletteIndex(characterId,variantId)<0)return false;
    if(characterId==m_paletteCharacterId&&variantId==m_paletteVariantId)return true;
    if(m_document->editingGestureActive()||m_document->layerPropertyEditActive()){
        status("Finish the current drawing or property edit before changing the palette.");return false;
    }
    if(!finishEditing())return false;
    // Applying a preview can synchronously refresh the source lists. Resolve
    // the requested stable identities again before publishing this selection.
    if(paletteIndex(characterId,variantId)<0)return false;
    m_paletteCharacterId=characterId;m_paletteVariantId=variantId;
    syncPaletteSelection();invalidate();return true;
}
void CelPaintCheckPanel::refreshSources(){
    QScopedValueRollback<bool>refresh(m_refreshing,true);QSet<QString>checked;for(int i=0;i<m_targets->count();++i)if(m_targets->item(i)->checkState()==Qt::Checked)checked.insert(m_targets->item(i)->data(Qt::UserRole).toString());
    m_palette->clear();m_palette->addItem("No palette check",QString());m_targets->clear();
    if(!m_document||!m_document->isOpen())return;
    for(const auto &character:m_document->project().characters){
        m_palette->addItem(character.name+" · Day",character.id);
        for(const auto &variant:character.colorVariants){
            m_palette->addItem(character.name+" · "+variant.name,character.id);
            m_palette->setItemData(m_palette->count()-1,variant.id,Qt::UserRole+1);
        }
    }
    if(paletteIndex(m_paletteCharacterId,m_paletteVariantId)<0){
        m_paletteVariantId.clear();
        if(paletteIndex(m_paletteCharacterId,{})<0)m_paletteCharacterId.clear();
    }
    syncPaletteSelection();
    const auto &shot=m_document->activeShot();for(int i=0;i<shot.layers.size();++i){const auto &layer=shot.layers[i];
        if(layer.group||layer.kind!=LayerKind::Raster||layer.scene3d||layer.adjustment||layerIsAnnotation(shot.layers,i))continue;
        auto *item=new QListWidgetItem(layerLabel(shot,layer),m_targets);item->setData(Qt::UserRole,layer.id);item->setToolTip(item->text());
        item->setCheckState(checked.contains(layer.id)||(checked.isEmpty()&&i==m_document->activeLayer())?Qt::Checked:Qt::Unchecked);
    }
}
void CelPaintCheckPanel::invalidate(){
    ++m_revision;if(m_checkCancellation)m_checkCancellation->store(true);discardRepairs();
    m_result={};m_problems->clear();m_highlightBounds={};m_highlightMask={};m_overlay->hide();m_preview->setImage({});
    status("Choose drawings, then Check.");updateActions();
}
void CelPaintCheckPanel::discardRepairs(){if(m_repairCancellation)m_repairCancellation->store(true);m_repairs={};setPending(false);m_original->setChecked(false);}
void CelPaintCheckPanel::cancelEditing(){
    if(m_checkCancellation)m_checkCancellation->store(true);
    discardRepairs();showProblem(false);status("Preview cancelled. Artwork unchanged.");updateActions();
}
void CelPaintCheckPanel::startCheck(){
    if(busy())return;
    if(!finishEditing())return;
    if(!m_document||!m_document->isOpen()||m_document->editingGestureActive()||m_document->layerPropertyEditActive()){status("Finish the current drawing or property edit first.");return;}
    invalidate();QVector<CelPaintCheckTarget>targets;const auto &shot=m_document->activeShot();
    for(int i=0;i<m_targets->count();++i){auto *item=m_targets->item(i);if(item->checkState()!=Qt::Checked)continue;
        const auto id=item->data(Qt::UserRole).toString();for(const auto &layer:shot.layers)if(layer.id==id){targets.append({shot.id,id,item->text(),QRect(0,0,m_document->project().width,m_document->project().height),layer,{}});break;}
    }
    CelPaintCheckSettings settings;settings.tolerance=qRound(m_tolerance->value());settings.maximumHolePixels=qRound(m_holes->value());settings.maximumStrayPixels=qRound(m_strays->value());
    for(const auto &character:m_document->project().characters)if(character.id==m_paletteCharacterId)
        for(const auto &color:characterVariantColors(character,m_paletteVariantId))settings.approvedColors.append(color.color);
    if(!m_paletteCharacterId.isEmpty()&&settings.approvedColors.isEmpty()){
        status("Add colors to this palette, or choose No palette check.");updateActions();return;
    }
    m_checkCancellation=std::make_shared<std::atomic_bool>(false);m_checkRevision=m_revision;m_checking=true;status("Checking drawings…");updateActions();
    m_checkWorker.setFuture(QtConcurrent::run([targets=std::move(targets),settings,cancel=m_checkCancellation]{return inspectCelPaint(targets,settings,cancel);}));
}
void CelPaintCheckPanel::previewRepairs(){
    if(!m_document||!m_canvas||busy()||m_result.targets.isEmpty())return;
    if(m_document->editingGestureActive()||m_document->layerPropertyEditActive()){status("Finish the current drawing or property edit first.");return;}
    QVector<int>checked;for(int i=0;i<m_problems->count();++i)if(m_problems->item(i)->checkState()==Qt::Checked)checked.append(i);
    if(checked.isEmpty()){status("Check the problems you want to repair.");return;}
    discardRepairs();m_repairCancellation=std::make_shared<std::atomic_bool>(false);m_repairRevision=m_revision;m_preparing=true;setPending(true);status("Preparing repair previews…");updateActions();
    m_repairWorker.setFuture(QtConcurrent::run([inspection=m_result,checked,color=m_canvas->brush.color,cancel=m_repairCancellation]{return prepareCelPaintRepairs(inspection,checked,color,cancel);}));
}
bool CelPaintCheckPanel::applyRepairs(){
    if(m_preparing){status("Wait for the repair preview, or cancel it.");return false;}
    if(!pending()){status("Preview checked repairs first.");return false;}
    QString error;const auto repairs=m_repairs;if(!applyCelPaintRepairs(m_document,repairs,&error)){status(error);return false;}
    discardRepairs();status("Repairs applied. Undo restores all drawings.");updateActions();return true;
}
bool CelPaintCheckPanel::finishEditing(){
    if(!pending())return true;
    QMessageBox dialog(QMessageBox::Question,"Cel paint repairs","Apply the previewed repairs before continuing?",QMessageBox::NoButton,this);
    auto *apply=dialog.addButton("Apply",QMessageBox::AcceptRole);auto *discard=dialog.addButton("Discard",QMessageBox::DestructiveRole);dialog.addButton(QMessageBox::Cancel);apply->setEnabled(!m_preparing);dialog.exec();
    if(dialog.clickedButton()==apply)return applyRepairs();
    if(dialog.clickedButton()==discard){cancelEditing();return true;}return false;
}
void CelPaintCheckPanel::showProblem(bool navigate){
    if(!m_canvas||!m_document||!m_overlay)return;
    const int index=m_problems->currentRow();if(index<0||index>=m_result.issues.size()){m_overlay->hide();return;}
    const auto &issue=m_result.issues[index];const auto &target=m_result.targets[issue.target];
    bool repaired=false;QImage image=target.image;
    if(!m_original->isChecked())for(const auto &repair:m_repairs.drawings)if(repair.source.layerId==target.layerId&&repair.source.shotId==target.shotId){image=repair.preview;repaired=true;break;}
    m_caption->setText((repaired?"Repair preview · ":"Original · ")+target.name);m_preview->setImage(image);
    if(navigate&&m_document->activeShotId()==target.shotId&&!m_document->editingGestureActive()&&!m_document->layerPropertyEditActive()){
        QScopedValueRollback<bool>jump(m_navigating,true);const auto &shot=m_document->activeShot();const auto owner=animationOwner(shot,target.layerId);
        for(const auto &track:shot.animationTracks)if(track.id==owner.trackId)for(const auto &exposure:track.exposures)if(exposure.drawingId==owner.drawingId){m_document->setCurrentFrame(exposure.startFrame);break;}
        for(int i=0;i<shot.layers.size();++i)if(shot.layers[i].id==target.layerId){m_document->selectLayer(i);break;}
        const auto origin=sheetGeometry(m_canvas->project(),m_canvas->shot()).output.topLeft();
        const auto sourceTransform=m_document->keyframeSourceEditing()?QTransform():worldLayerTransform(shot,target.layerId,m_document->currentFrame());
        m_canvas->centerOn(m_canvas->cameraTransform().map(sourceTransform.map(QPointF(issue.bounds.center())))+origin);
    }
    m_highlightBounds=issue.bounds;m_highlightMask={};
    if(issue.pixels.size()<=65536&&qint64(issue.bounds.width())*issue.bounds.height()<=1024*1024&&!issue.pixels.isEmpty()){
        m_highlightMask=QImage(issue.bounds.size(),QImage::Format_ARGB32_Premultiplied);m_highlightMask.fill(Qt::transparent);auto color=issueColor(issue.kind);color.setAlpha(150);
        for(int pixel:issue.pixels){const QPoint point(pixel%target.image.width(),pixel/target.image.width());m_highlightMask.setPixel(point+target.bounds.topLeft()-issue.bounds.topLeft(),qPremultiply(color.rgba()));}
    }
    if(isVisible()&&m_document->activeShotId()==target.shotId&&layerPresentAtFrame(m_document->activeShot(),target.layerId,m_document->currentFrame())){m_overlay->show();m_overlay->raise();m_overlay->update();}else m_overlay->hide();
}
void CelPaintCheckPanel::updateActions(){
    const bool open=m_document&&m_document->isOpen();m_check->setEnabled(open&&!busy());m_previous->setEnabled(m_problems->count()>0&&!busy());m_next->setEnabled(m_problems->count()>0&&!busy());
    m_previewButton->setEnabled(!busy()&&!m_result.issues.isEmpty());m_apply->setEnabled(pending()&&!busy()&&!m_repairs.drawings.isEmpty());m_cancel->setEnabled(busy()||pending());m_original->setEnabled(!m_repairs.drawings.isEmpty());
}
void CelPaintCheckPanel::hideEvent(QHideEvent *event){if(m_overlay)m_overlay->hide();QWidget::hideEvent(event);}
void CelPaintCheckPanel::showEvent(QShowEvent *event){QWidget::showEvent(event);showProblem(false);}
bool CelPaintCheckPanel::eventFilter(QObject *watched,QEvent *event){
    if(watched==m_canvas){
        if(event->type()==QEvent::Resize)m_overlay->setGeometry(m_canvas->rect());
        if(event->type()==QEvent::MouseButtonPress||event->type()==QEvent::TabletPress) {
            // Contact belongs to the pen. Keep this path allocation-free: a
            // completed stroke invalidates snapshots through Document::changed,
            // and worker completion already defers while a gesture is active.
            // Cancelling here would rebuild the source preview and mask just as
            // the first tablet samples arrive.
            m_overlay->hide();
        }
    }
    return QWidget::eventFilter(watched,event);
}
}
