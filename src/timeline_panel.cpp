#include "timeline_panel.h"
#include "movie_review_controller.h"
#include "movie_review_panel.h"
#include "timeline_ripple_dialog.h"
#include "onion_settings_dialog.h"
#include "drawing_template_dialog.h"
#include "drawing_reuse_dialog.h"
#include "cel_edit.h"
#include "symbol_clips.h"
#include "symbol_poses.h"
#include "symbols.h"
#include "symbol_clip_panel.h"
#include "symbol_frame_picker_dialog.h"
#include "timeline_controller.h"
#include "timeline_cues.h"
#include "timeline_cue_panel.h"
#include "animation.h"
#include "canvas.h"
#include "document.h"
#include "icons.h"
#include "ui_theme.h"
#include "ui_metrics.h"
#include "property_row.h"
#include "value_slider.h"
#include "keyframe_panel.h"
#include "camera_panel.h"
#include "camera_animation.h"
#include "audio_panel.h"
#include "audio.h"
#include "property_animation.h"
#include <QStackedWidget>
#include <QAbstractScrollArea>
#include <QApplication>
#include <QKeySequence>
#include "timeline_edit.h"
#include <QAction>
#include <QActionGroup>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QContextMenuEvent>
#include <QHelpEvent>
#include <QInputDialog>
#include <QKeyEvent>
#include <QLabel>
#include <QGridLayout>
#include <QResizeEvent>
#include <QLineEdit>
#include <QMouseEvent>
#include <QMenu>
#include <QPainter>
#include <QPointer>
#include <QPushButton>
#include <QScopedValueRollback>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QTabBar>
#include <QToolButton>
#include <QToolTip>
#include <QTimer>
#include <QTabletEvent>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>
#include <queue>
#include <functional>
#include <vector>

namespace toon {
namespace {
const Shot *timelineShot(TimelineController *controller){
    if(!controller)return nullptr;
    const auto *document=controller->document();
    return document->isOpen()&&!document->project().shots.isEmpty()?&document->activeShot():nullptr;
}
const AudioClip *audioFor(const Shot &shot,const QString &id){for(const auto &clip:shot.audioClips)if(clip.id==id)return &clip;return nullptr;}
const RasterLayer *layerFor(const Shot &shot,const QString &id){for(const auto &layer:shot.layers)if(layer.id==id)return &layer;return nullptr;}
QString layerPath(const Shot &shot,const QString &id){
    QStringList names;QSet<QString> visited;auto current=id;
    while(!current.isEmpty()&&!visited.contains(current)){visited.insert(current);const auto *layer=layerFor(shot,current);if(!layer)break;names.prepend(layer->name);current=layer->parentId;}
    return names.join(" / ");
}
const AnimationTrack *selectedTrack(TimelineController *controller){
    if(const auto *shot=timelineShot(controller))for(const auto &track:shot->animationTracks)if(track.id==controller->selectedTrackId())return &track;return nullptr;
}
const RasterLayer *selectedRoot(TimelineController *controller){
    const auto *shot=timelineShot(controller);if(!shot)return nullptr;
    const auto *track=selectedTrack(controller);return layerFor(*shot,track?track->rootGroupLayerId:controller->selectedTrackId());
}
const CelExposure *currentExposure(TimelineController *controller){const auto *track=selectedTrack(controller);return track?exposureAtFrame(*track,controller->currentFrame()):nullptr;}
const RasterLayer *currentDrawing(TimelineController *controller){
    const auto *shot=timelineShot(controller);const auto *track=selectedTrack(controller);if(!shot||!track)return nullptr;
    const auto *drawing=drawingAtFrame(*track,controller->currentFrame());return drawing?layerFor(*shot,drawing->groupLayerId):nullptr;
}
bool timelineLayerLocked(const Shot &shot,const QString &id);
bool selectedKeysEditable(TimelineController *controller){
    const auto *shot=timelineShot(controller);const auto keys=controller->selectedKeys();
    if(!shot||keys.isEmpty()||controller->playing())return false;
    for(const auto &key:keys){
        if(key.kind==TimelineKeyKind::Camera2D)continue;
        if(key.kind==TimelineKeyKind::Camera3D){if(!sceneCameraEditError(*shot,key.layerId).isEmpty())return false;continue;}
        if(key.isSymbolPose()){if(!symbolPoseEditError(*shot,key.layerId).isEmpty())return false;continue;}
        const auto *layer=layerFor(*shot,key.layerId);if(!layer||timelineLayerLocked(*shot,key.layerId))return false;
        if(!(key.isPath()?propertyAnimationUnavailableReason(*shot,key.layerId):keyframePropertyUnavailableReason(*shot,key.layerId,key.property)).isEmpty())return false;
        if((key.isPath()||keyframePropertyNeedsPositionUnlock(key.property))&&layerPositionLocked(shot->layers,int(layer-shot->layers.constData())))return false;
    }
    return true;
}
QToolButton *timelineButton(QWidget *parent,const char *id,const char *icon,const QString &help,bool checkable=false){
    auto *button=new QToolButton(parent);button->setObjectName(id);button->setIcon(appIcon(icon));button->setIconSize({ui::metrics::actionIcon,ui::metrics::actionIcon});
    button->setFixedSize(ui::metrics::actionTarget,ui::metrics::actionTarget);button->setToolTip(help);button->setAccessibleName(help);button->setCheckable(checkable);return button;
}
void openDrawingReuse(TimelineController *controller,QWidget *parent){
    if(!controller->prepareTimelineEdit())return;
    int result;{DrawingReuseDialog dialog(controller,parent);result=dialog.exec();}
    if(result==QDialog::Accepted)emit controller->drawingPropertiesRequested();
    auto *target=controller->canvas()->isVisible()?static_cast<QWidget *>(controller->canvas()):parent;
    if(target&&target->window()->isActiveWindow())target->setFocus(Qt::OtherFocusReason);
}
class TimelineContext final:public QLabel {
public:using QLabel::QLabel;
protected:void paintEvent(QPaintEvent *)override{QPainter painter(this);painter.setPen(palette().color(QPalette::WindowText));
    painter.drawText(rect(),Qt::AlignLeft|Qt::AlignVCenter,fontMetrics().elidedText(text(),Qt::ElideRight,width()));}
};
// Resolution is a view preference, so opening its menu must not accept an
// unfinished drawing/property value simply because the popup takes focus.
class PlaybackQualityMenu final:public QMenu {
public:
    explicit PlaybackQualityMenu(QWidget *parent):QMenu(parent){
        parent->installEventFilter(this);
        connect(this,&QMenu::aboutToShow,this,[this]{if(m_preserved.isEmpty())preserveEditor();});
        connect(this,&QMenu::aboutToHide,this,[this]{
            QTimer::singleShot(0,this,[this]{
                const auto editor=m_editor;
                if(editor&&editor->isVisible()&&!QApplication::activePopupWidget()){
                    // Some platforms leave the hidden popup as Qt's active
                    // window. Return only that popup's focus, never take focus
                    // from a different window the artist selected meanwhile.
                    if(QApplication::activeWindow()==this)QApplication::setActiveWindow(editor->window());
                    if(editor->window()->isActiveWindow())editor->setFocus(Qt::PopupFocusReason);
                }
                release();
            });
        });
    }
protected:
    bool eventFilter(QObject *watched,QEvent *event)override{
        // QToolButton transfers focus while preparing its menu, before
        // aboutToShow on some platforms. Capture the draft before that starts.
        if(watched==parentWidget()){
            if((event->type()==QEvent::MouseButtonPress&&static_cast<QMouseEvent *>(event)->button()==Qt::LeftButton)
                ||event->type()==QEvent::TabletPress)
                preserveEditor();
            return QMenu::eventFilter(watched,event);
        }
        if(event->type()==QEvent::FocusOut||event->type()==QEvent::WindowDeactivate)return true;
        return QMenu::eventFilter(watched,event);
    }
private:
    void preserveEditor(){
        if(!m_preserved.isEmpty())return;
        auto *focus=QApplication::focusWidget();if(!focus)return;
        ValueSlider *value=nullptr;
        for(auto *widget=focus;widget;widget=widget->parentWidget())if((value=qobject_cast<ValueSlider *>(widget)))break;
        if(value&&value->editingActive()){
            preserve(value);preserve(value->findChild<QDoubleSpinBox *>());preserve(value->findChild<QLineEdit *>());
        }else if(qobject_cast<QLineEdit *>(focus))preserve(focus);
        if(!m_preserved.isEmpty())m_editor=focus;
    }
    void preserve(QObject *object){if(object){object->installEventFilter(this);m_preserved.append(object);}}
    void release(){for(const auto &object:m_preserved)if(object)object->removeEventFilter(this);m_preserved.clear();m_editor.clear();}
    QVector<QPointer<QObject>> m_preserved;
    QPointer<QWidget> m_editor;
};
class FrameTabs final:public QTabBar {
public:using QTabBar::QTabBar;
    QSize minimumSizeHint()const override{auto size=QTabBar::minimumSizeHint();size.setWidth(count()*ui::metrics::actionTarget);return size;}
protected:
    void resizeEvent(QResizeEvent *event)override{QTabBar::resizeEvent(event);updateLabels();}
    void changeEvent(QEvent *event)override{QTabBar::changeEvent(event);if(event->type()==QEvent::FontChange||event->type()==QEvent::StyleChange)updateLabels();}
    void tabInserted(int index)override{QTabBar::tabInserted(index);updateLabels();}
private:
    void updateLabels(){
        if(m_updating)return;const QStringList names{"Drawing","Animation","Audio","Camera","Symbol","Cues"};int fullWidth=0;
        for(int i=0;i<count();++i)if(isTabVisible(i))fullWidth+=QTabBar::tabSizeHint(i).width()+fontMetrics().horizontalAdvance(names.value(i))-fontMetrics().horizontalAdvance(tabText(i));
        const QScopedValueRollback<bool> guard(m_updating,true);
        for(int i=0;i<count();++i)setTabText(i,width()>=fullWidth?names.value(i):QString());
    }
    bool m_updating=false;
};
struct TimelineRow {
    QString id,name;
    const AnimationTrack *track=nullptr;
    bool locked=false,visible=true;
    QString layerId,objectId;
    std::optional<KeyframeProperty> property;
    QString audioId;
    TimelineKeyKind kind=TimelineKeyKind::Layer;
    bool symbolLane=false;
    bool cueLane=false,dialogueLane=false;
    QVector<TimelineCue> cues{};
    int depth=0;
    bool folder=false;
    bool artworkRow()const{return !layerId.isEmpty()&&!propertyLane()&&!cueLane&&audioId.isEmpty();}
    bool cameraLane()const{return kind==TimelineKeyKind::Camera2D||kind==TimelineKeyKind::Camera3D;}
    bool poseLane()const{return kind==TimelineKeyKind::SymbolPose;}
    bool propertyLane()const{return cameraLane()||poseLane()||property.has_value()||!objectId.isEmpty();}
};
const char *cueIcon(TimelineCueKind kind){
    switch(kind){case TimelineCueKind::KeyPose:return "circle";case TimelineCueKind::Breakdown:return "split";
    case TimelineCueKind::Inbetween:return "arrow-left-right";case TimelineCueKind::Dialogue:return "type";default:return "pin";}
}
void populateCueMenu(QMenu *menu,TimelineController *controller){
    for(auto kind:{TimelineCueKind::Marker,TimelineCueKind::KeyPose,TimelineCueKind::Breakdown,TimelineCueKind::Inbetween,TimelineCueKind::Dialogue}){
        auto *action=menu->addAction(appIcon(cueIcon(kind)),timelineCueKindName(kind));
        action->setObjectName("timelineAddCue_"+timelineCueKindKey(kind));
        QObject::connect(action,&QAction::triggered,menu,[controller,kind]{controller->addCue(kind);});
    }
}
void appendCueRows(QVector<TimelineRow> &rows,const Shot &shot,bool dialogue){
    QVector<TimelineCue> cues;for(const auto &cue:shot.timelineCues)if((cue.kind==TimelineCueKind::Dialogue)==dialogue)cues.append(cue);
    std::sort(cues.begin(),cues.end(),[](const auto &a,const auto &b){
        if(a.startFrame!=b.startFrame)return a.startFrame<b.startFrame;
        if(a.durationFrames!=b.durationFrames)return a.durationFrames>b.durationFrames;return a.id<b.id;});
    // Earliest free lane first: bounded O(n log n), even for a thousand overlaps.
    using EndLane=std::pair<int,int>;std::priority_queue<EndLane,std::vector<EndLane>,std::greater<EndLane>> ends;
    QVector<TimelineRow> lanes;
    for(const auto &cue:cues){int lane=lanes.size();if(!ends.empty()&&ends.top().first<=cue.startFrame){lane=ends.top().second;ends.pop();}
        if(lane==lanes.size()){TimelineRow row;row.id=QString("cues:%1:%2").arg(dialogue?"dialogue":"markers").arg(lane);
            row.name=dialogue?"Dialogue":"Markers";if(lane)row.name+=QString(" %1").arg(lane+1);row.cueLane=true;row.dialogueLane=dialogue;lanes.append(row);}
        lanes[lane].cues.append(cue);ends.push({cue.startFrame+cue.durationFrames,lane});
    }
    rows+=lanes;
}
void appendSymbolPoseRow(QVector<TimelineRow> &rows,TimelineController *controller,const Shot &shot,const RasterLayer &layer){
    if(!layer.symbol||layer.symbol->playback!=SymbolPlayback::Poses)return;
    if(layer.symbol->poseKeys.isEmpty()&&controller->selectedSymbolClipId()!=layer.id&&controller->selectedSymbolPoseLayerId()!=layer.id&&controller->selectedPropertyLayerId()!=layer.id)return;
    TimelineRow row;row.id="poses:"+layer.id;row.name="Poses · "+layer.name;row.layerId=layer.id;
    row.kind=TimelineKeyKind::SymbolPose;row.locked=!symbolPoseEditError(shot,layer.id).isEmpty();row.visible=layer.visible;rows.append(row);
}
bool timelineLayerLocked(const Shot &shot,const QString &id){
    QSet<QString> seen;const auto *layer=layerFor(shot,id);
    while(layer){if(layer->locked||seen.contains(layer->id))return true;seen.insert(layer->id);if(layer->parentId.isEmpty())return false;layer=layerFor(shot,layer->parentId);}return true;
}
bool timelineLayerVisible(const Shot &shot,const QString &id){
    QSet<QString> seen;const auto *layer=layerFor(shot,id);
    while(layer){if(!layer->visible||seen.contains(layer->id))return false;seen.insert(layer->id);if(layer->parentId.isEmpty())return true;layer=layerFor(shot,layer->parentId);}return false;
}
QVector<TimelineRow> timelineRows(TimelineController *controller){
    QVector<TimelineRow> rows;const auto *shot=timelineShot(controller);if(!shot)return rows;
    if(!controller->property("symbolEditor").toBool()){appendCueRows(rows,*shot,false);appendCueRows(rows,*shot,true);}
    const QString selectedLayer=controller->selectedPropertyLayerId();
    QHash<QString,QVector<const RasterLayer *>> children;QHash<QString,const AnimationTrack *> tracks;
    const auto owners=animationOwners(*shot);
    for(auto it=shot->layers.crbegin();it!=shot->layers.crend();++it)children[it->parentId].append(&*it);
    for(const auto &track:shot->animationTracks)tracks.insert(track.rootGroupLayerId,&track);
    const auto appendChannels=[&](const RasterLayer &layer,int depth){
        if(layer.id!=selectedLayer)return;const auto *animation=layerAnimation(*shot,layer.id);if(!animation)return;
        for(const auto &channel:animation->channels)if(!channel.keys.isEmpty()){
            TimelineRow row;row.id="property:"+layer.id+":"+keyframePropertyKey(channel.property);row.name=keyframePropertyName(channel.property);
            row.layerId=layer.id;row.property=channel.property;row.locked=timelineLayerLocked(*shot,layer.id);row.visible=timelineLayerVisible(*shot,layer.id);row.depth=depth;rows.append(row);}
        for(int index=0;index<animation->paths.size();++index){const auto &path=animation->paths[index];if(path.keys.isEmpty())continue;
            TimelineRow row;row.id="path:"+layer.id+":"+path.objectId;row.name="Path morph "+QString::number(index+1);row.layerId=layer.id;row.objectId=path.objectId;
            row.locked=timelineLayerLocked(*shot,layer.id);row.visible=timelineLayerVisible(*shot,layer.id);row.depth=depth;rows.append(row);}
    };
    std::function<void(const RasterLayer &,int)> appendLayer=[&](const RasterLayer &layer,int depth){
        const auto *track=tracks.value(layer.id,nullptr);TimelineRow row;
        row.id=layer.symbol?"symbol:"+layer.id:track?track->id:layer.id;row.name=layer.name;row.track=track;row.layerId=layer.id;
        row.locked=layer.symbol?!symbolClipEditError(*shot,layer.id).isEmpty():timelineLayerLocked(*shot,layer.id);row.visible=timelineLayerVisible(*shot,layer.id);
        row.symbolLane=layer.symbol.has_value();row.folder=layer.group;row.depth=depth;rows.append(row);
        if(layer.group&&controller->timelineFolderCollapsed(layer.id))return;
        const int oldCount=rows.size();appendSymbolPoseRow(rows,controller,*shot,layer);if(rows.size()>oldCount){rows.last().depth=depth+1;rows.last().visible=timelineLayerVisible(*shot,layer.id);}
        appendChannels(layer,depth+1);
        if(track){
            // Drawing containers are implementation details of the exposure
            // row. Show their Symbols, including unused drawings, without
            // expanding every stored paint layer into a permanent track.
            for(auto it=shot->layers.crbegin();it!=shot->layers.crend();++it)if(it->symbol&&owners.value(it->id).trackId==track->id){
                const int first=rows.size();appendLayer(*it,depth+1);const auto owner=owners.value(it->id);
                if(!owner.drawingId.isEmpty())rows[first].name+=" · "+drawingDisplayId(*track,owner.drawingId);
            }
            const auto owner=owners.value(selectedLayer);
            if(owner.trackId==track->id&&selectedLayer!=layer.id)if(const auto *selected=layerFor(*shot,selectedLayer);selected&&!selected->symbol)appendChannels(*selected,depth+1);
        }else if(layer.group){for(const auto *child:children.value(layer.id))appendLayer(*child,depth+1);}
    };
    for(const auto *layer:children.value(QString()))appendLayer(*layer,0);
    for(const auto &clip:shot->audioClips)rows.append({"audio:"+clip.id,clip.name,nullptr,false,!clip.muted,{},{},{},clip.id});
    if(!controller->property("symbolEditor").toBool())rows.append({"camera:2d","2D camera",nullptr,false,true,{},{},{},{},TimelineKeyKind::Camera2D});
    for(const auto &layer:shot->layers)if(layer.scene3d)
        rows.append({"camera:3d:"+layer.id,"3D camera · "+layer.name,nullptr,!sceneCameraEditError(*shot,layer.id).isEmpty(),layer.visible,layer.id,{},{},{},TimelineKeyKind::Camera3D});
    return rows;
}
QVector<int> laneFrames(const Shot &shot,const TimelineRow &row){
    QVector<int> frames;
    if(row.poseLane()){
        if(const auto *layer=layerFor(shot,row.layerId);layer&&layer->symbol){const auto &instance=*layer->symbol;const int end=symbolClipEnd(instance,shot.durationFrames);
            for(const auto &key:instance.poseKeys){const int frame=symbolPoseParentFrame(instance,key.frame);if(frame>=instance.startFrame&&frame>=0&&frame<end&&frame<shot.durationFrames)frames.append(frame);}}
    }else if(row.kind==TimelineKeyKind::Camera2D){for(const auto &key:shot.camera2DKeys)frames.append(key.frame);}
    else if(row.kind==TimelineKeyKind::Camera3D){if(const auto *camera=sceneCameraAnimation(shot,row.layerId))for(const auto &key:camera->keys)frames.append(key.frame);}
    else if(row.property){if(const auto *channel=propertyChannel(shot,row.layerId,*row.property))for(const auto &key:channel->keys)frames.append(key.frame);}
    else if(!row.objectId.isEmpty())if(const auto *path=pathAnimation(shot,row.layerId,row.objectId))for(const auto &key:path->keys)frames.append(key.frame);
    return frames;
}

}

class TimelineGrid final:public QAbstractScrollArea {
public:
    std::function<void(qreal)> zoomChanged;
    std::function<void(const QString &)> addFolderRequested;
    qreal zoomPercent()const{return cellWidth*5;}
    void setZoomPercent(qreal percent,int anchorX=-1){
        if(m_gesture!=Gesture::None||m_scrubbing){if(zoomChanged)zoomChanged(zoomPercent());return;}
        const int left=labelWidth(),right=viewport()->width();
        if(anchorX<left||anchorX>=right){
            const qreal playhead=left+(m_controller->currentFrame()+.5)*cellWidth-horizontalScrollBar()->value();
            anchorX=playhead>=left&&playhead<right?qRound(playhead):(left+right)/2;
        }
        const qreal anchor=(anchorX-left+horizontalScrollBar()->value())/cellWidth;
        m_fitShot=false;cellWidth=std::clamp(percent/5,qreal(.00002),qreal(80));
        m_lastFrame=m_controller->currentFrame();refresh();
        horizontalScrollBar()->setValue(qRound(anchor*cellWidth-(anchorX-left)));
        if(zoomChanged)zoomChanged(zoomPercent());
    }
    void centerPlayhead(){
        if(m_gesture!=Gesture::None||m_scrubbing||!timelineShot(m_controller))return;
        const int width=std::max(0,viewport()->width()-labelWidth());
        horizontalScrollBar()->setValue(qRound((m_controller->currentFrame()+.5)*cellWidth-width/2.));
        viewport()->update();
    }
    void fitShot(){fitFrames(0,std::max(0,m_controller->frameCount()-1),true);}
    void fitSelection(){
        int first=m_controller->currentFrame(),last=first;
        if(const auto *shot=timelineShot(m_controller);shot&&!m_controller->selectedCueId().isEmpty()){
            if(const auto *cue=timelineCue(*shot,m_controller->selectedCueId())){first=cue->startFrame;last=first+cue->durationFrames-1;}
        }else if(const auto *shot=timelineShot(m_controller);shot&&!m_controller->selectedSymbolClipId().isEmpty()){
            if(const auto *layer=layerFor(*shot,m_controller->selectedSymbolClipId());layer&&layer->symbol){first=std::min(layer->symbol->startFrame,shot->durationFrames-1);last=std::max(first,symbolClipEnd(*layer->symbol,shot->durationFrames)-1);}
        }else if(!m_controller->selectedCels().isEmpty()&&timelineShot(m_controller)){
            first=m_controller->frameCount()-1;last=0;
            for(const auto &ref:m_controller->selectedCels())if(const auto *cel=celExposure(*timelineShot(m_controller),ref)){first=std::min(first,cel->startFrame);last=std::max(last,cel->startFrame+cel->durationFrames-1);}
        }else if(!m_controller->selectedKeys().isEmpty()){
            first=m_controller->frameCount()-1;last=0;
            for(const auto &key:m_controller->selectedKeys()){first=std::min(first,key.frame);last=std::max(last,key.frame);}
        }else if(const auto *shot=timelineShot(m_controller)){
            if(const auto *audio=audioFor(*shot,m_controller->selectedAudioClipId())){
                first=audio->startFrame;last=std::min(m_controller->frameCount()-1,first+int(std::ceil(qreal(audio->sampleCount)*m_controller->document()->project().fps/audioSampleRate))-1);
            }else if(m_controller->playbackRangeEnabled()){
                first=m_controller->playbackFirstFrame();last=m_controller->playbackLastFrame();
            }else if(const auto *exposure=currentExposure(m_controller)){
                first=exposure->startFrame;last=first+exposure->durationFrames-1;
            }
        }
        fitFrames(first,last,false);
    }
    explicit TimelineGrid(TimelineController *controller,QWidget *parent):QAbstractScrollArea(parent),m_controller(controller){
        setObjectName("timelineGrid");setAccessibleName("Animation timeline");
        setAccessibleDescription("Choose a track and frame. Arrow keys step frames or tracks; Space starts or stops playback.");
        setFocusPolicy(Qt::StrongFocus);setMinimumSize(0,80);setFrameShape(QFrame::NoFrame);
        viewport()->setMouseTracking(true);viewport()->setAttribute(Qt::WA_TabletTracking);horizontalScrollBar()->setSingleStep(cellWidth);verticalScrollBar()->setSingleStep(rowHeight);
        connect(horizontalScrollBar(),&QScrollBar::valueChanged,viewport(),qOverload<>(&QWidget::update));
        connect(verticalScrollBar(),&QScrollBar::valueChanged,this,[this]{refreshHeaderControls();viewport()->update();});
        connect(controller->document(),&Document::loaded,this,[this]{cancelGesture();m_selectedLane.clear();refresh();});
        connect(controller->document(),&Document::shotChanged,this,[this]{cancelGesture();m_selectedLane.clear();refresh();});
        connect(controller->document()->history(),&QUndoStack::indexChanged,this,[this]{refresh();});
        connect(controller,&TimelineController::editResolutionRequested,this,[this]{if(m_gesture==Gesture::TrackMove)cancelGesture();else finishGesture();});
        connect(controller,&TimelineController::editCancellationRequested,this,[this]{cancelGesture();});

    }
    ~TimelineGrid() override {if(m_controller){
        // The parent may already be deleting earlier toolbar children. Avoid
        // refreshing those controls while clearing this grid's pending source.
        disconnect(m_controller,nullptr,parentWidget(),nullptr);disconnect(m_controller,nullptr,this,nullptr);
        cancelGesture();m_controller->endScrub();m_controller->setPendingEdits(this,false);
    }}
    void refresh(){
        if(!m_controller)return;
        rowHeight=std::max(32,fontMetrics().height()+10);
        const auto rows=timelineRows(m_controller);
        setProperty("timelineLabelWidth",labelWidth());setProperty("timelineRowHeight",rowHeight);setProperty("timelineRulerHeight",rulerHeight);
        QStringList rowIds;for(const auto &row:rows)rowIds.append(row.id);setProperty("timelineRowIds",rowIds);
        if(!m_selectedLane.isEmpty()&&std::none_of(rows.cbegin(),rows.cend(),[this](const auto &row){return row.id==m_selectedLane;})&&(!timelineShot(m_controller)||!layerFor(*timelineShot(m_controller),m_controller->selectedPropertyLayerId())))m_selectedLane.clear();
        if(m_gesture!=Gesture::None&&!m_resolving){const auto *shot=timelineShot(m_controller);if(!shot||shot->id!=m_gestureShot||m_controller->document()->history()->index()!=m_gestureHistory||m_controller->selectedPropertyLayerId()!=m_gestureLayer||m_controller->frameCount()!=m_gestureFrames||(cueGesture()&&(!timelineCue(*shot,m_cueOriginal.id)||*timelineCue(*shot,m_cueOriginal.id)!=m_cueOriginal||m_controller->selectedCueId()!=m_cueOriginal.id)))cancelGesture();}
        const int width=std::max(0,viewport()->width()-labelWidth()),height=std::max(0,viewport()->height()-rulerHeight);
        if(m_fitShot)cellWidth=std::clamp(qreal(std::max(1,width-1))/std::max(1,m_controller->frameCount()),qreal(.00002),qreal(80));
        horizontalScrollBar()->setSingleStep(std::max(1,qRound(cellWidth)));
        horizontalScrollBar()->setRange(0,std::max(0,int(std::ceil(m_controller->frameCount()*cellWidth))-width));horizontalScrollBar()->setPageStep(width);
        verticalScrollBar()->setRange(0,std::max(0,int(rows.size())*rowHeight-height));verticalScrollBar()->setPageStep(height);
        if(m_lastFrame!=m_controller->currentFrame()){
            m_lastFrame=m_controller->currentFrame();const int x=m_lastFrame*cellWidth;
            if(x<horizontalScrollBar()->value())horizontalScrollBar()->setValue(x);
            else if(x+cellWidth>horizontalScrollBar()->value()+width)horizontalScrollBar()->setValue(x+cellWidth-width);
        }
        const QString selection=!m_controller->selectedCueId().isEmpty()?cueRowId(rows):!m_controller->selectedSymbolClipId().isEmpty()?"symbol:"+m_controller->selectedSymbolClipId():!m_controller->selectedSymbolPoseLayerId().isEmpty()?"poses:"+m_controller->selectedSymbolPoseLayerId():m_controller->cameraSelected()?cameraRowId():m_controller->selectedAudioClipId().isEmpty()?m_controller->selectedTrackId():"audio:"+m_controller->selectedAudioClipId();
        if(selection!=m_lastTrack){m_lastTrack=selection;for(int row=0;row<rows.size();++row)if(rows[row].id==selection){
            if(row*rowHeight<verticalScrollBar()->value())verticalScrollBar()->setValue(row*rowHeight);
            else if((row+1)*rowHeight>verticalScrollBar()->value()+height)verticalScrollBar()->setValue((row+1)*rowHeight-height);break;}}
        setAccessibleDescription(QString("Frame %1 of %2. Choose a track and frame; arrow keys step, Space plays.").arg(m_controller->currentFrame()+1).arg(m_controller->frameCount()));
        if(m_fitShot&&zoomChanged)zoomChanged(zoomPercent());
        refreshHeaderControls();viewport()->update();
    }
protected:
    void changeEvent(QEvent *event)override{QAbstractScrollArea::changeEvent(event);if(event->type()==QEvent::FontChange||event->type()==QEvent::StyleChange)refresh();}
    void resizeEvent(QResizeEvent *event)override{QAbstractScrollArea::resizeEvent(event);m_lastFrame=-1;m_lastTrack.clear();refresh();}
    void paintEvent(QPaintEvent *)override{
        if(!m_controller)return;
        QPainter painter(viewport());painter.setRenderHint(QPainter::Antialiasing,false);painter.fillRect(viewport()->rect(),QColor(ui::base));
        const auto *shot=timelineShot(m_controller);const auto rows=timelineRows(m_controller);const int label=labelWidth(),left=horizontalScrollBar()->value(),top=verticalScrollBar()->value();
        QSet<QString> selectedCelIds;for(const auto &ref:m_controller->selectedCels())selectedCelIds.insert(ref.exposureId);
        const bool validCelMove=m_gesture!=Gesture::Cels||celMoveValid();
        const int first=std::max(0,int(std::floor(left/cellWidth))),last=std::min(m_controller->frameCount()-1,int(std::ceil((left+viewport()->width()-label)/cellWidth)));
        const auto tickStep=[](qreal minimum){int step=1;while(step<minimum){if(step*2>=minimum)return step*2;if(step*5>=minimum)return step*5;step*=10;}return step;};
        const int gridStep=tickStep(8/cellWidth),labelStep=tickStep((fontMetrics().horizontalAdvance(QString::number(m_controller->frameCount()))+18)/cellWidth);
        painter.fillRect(QRect(0,0,label,viewport()->height()),QColor(ui::panel));
        const int firstRow=std::max(0,top/rowHeight),lastRow=std::min(int(rows.size())-1,(top+viewport()->height()-rulerHeight)/rowHeight);
        for(int row=firstRow;row<=lastRow;++row)if(rowSelected(rows[row]))
            painter.fillRect(QRect(0,rulerHeight+row*rowHeight-top,viewport()->width(),rowHeight),QColor(ui::selected));
        painter.save();painter.setClipRect(QRect(label,rulerHeight,viewport()->width()-label,viewport()->height()-rulerHeight));
        for(int frame=(first/gridStep)*gridStep;frame<=last+1;frame+=gridStep){painter.setPen(QColor(frame%labelStep?ui::raised:ui::border));const int x=frameX(frame);painter.drawLine(x,rulerHeight,x,viewport()->height());}painter.restore();
        for(int row=firstRow;row<=lastRow;++row){const auto &entry=rows[row];const int y=rulerHeight+row*rowHeight-top;
            const bool selected=rowSelected(entry);const bool included=entry.layerId.isEmpty()||m_controller->canvas()->timelineSoloIncludesLayer(entry.layerId);
            painter.save();painter.setOpacity(entry.visible&&included?1.:.4);painter.setClipRect(QRect(label,rulerHeight,viewport()->width()-label,viewport()->height()-rulerHeight));
            if(entry.cueLane&&shot){
                for(const auto &source:entry.cues){const bool preview=cueGesture()&&source.id==m_cueOriginal.id;const auto &cue=preview?m_cuePreview:source;
                    const int x=frameX(cue.startFrame),right=frameX(cue.startFrame+cue.durationFrames);if(right+8<label||x-8>viewport()->width())continue;
                    const bool picked=m_controller->selectedCueId()==cue.id;const bool point=cue.durationFrames==1;
                    const int center=x+qRound(cellWidth/2);QRect block(x+1,y+5,std::max(2,right-x-2),rowHeight-10);
                    painter.setRenderHint(QPainter::Antialiasing,true);painter.setPen(QPen(QColor(picked?ui::focus:ui::border),picked?2:1));
                    painter.setBrush(QColor(picked?ui::selected:ui::raised));
                    if(point){const qreal cy=y+rowHeight/2.;painter.drawPolygon(QPolygonF{{qreal(center),cy-8},{qreal(center+8),cy},{qreal(center),cy+8},{qreal(center-8),cy}});
                        painter.setPen(QPen(cue.color,2));painter.drawLine(center, y+rowHeight/2-4,center,y+rowHeight/2+4);
                    }else{painter.drawRoundedRect(block.adjusted(0,0,-1,-1),3,3);painter.fillRect(block.adjusted(2,2,-2,-block.height()+5),cue.color);
                        if(block.width()>=18){painter.setPen(QColor(picked?ui::focus:ui::muted));painter.drawLine(block.left()+3,block.top()+6,block.left()+3,block.bottom()-3);painter.drawLine(block.right()-3,block.top()+6,block.right()-3,block.bottom()-3);}
                        if(block.width()>30){const QRect icon(block.left()+7,block.center().y()-7,14,14);appIcon(cueIcon(cue.kind)).paint(&painter,icon);}
                        if(block.width()>55){const QString title=cue.speaker.isEmpty()?cue.name:cue.speaker+" · "+cue.name;const QRect text=block.adjusted(25,1,-7,0);painter.setPen(QColor(ui::text));painter.drawText(text,Qt::AlignVCenter|Qt::AlignLeft,fontMetrics().elidedText(title,Qt::ElideRight,text.width()));}}
                    if(point)appIcon(cueIcon(cue.kind)).paint(&painter,QRect(center-5,y+rowHeight/2-5,10,10));
                    if(preview&&!m_cueValid){painter.setPen(QPen(QColor(ui::text),2,Qt::DashLine));painter.drawLine(point?QPoint(center-8,y+8):block.topLeft(),point?QPoint(center+8,y+rowHeight-8):block.bottomRight());}
                    painter.setRenderHint(QPainter::Antialiasing,false);
                }
            }else if(!entry.audioId.isEmpty()&&shot){
                if(const auto *source=audioFor(*shot,entry.audioId)){
                    const auto clip=audioGesture()&&source->id==m_audioOriginal.id?m_audioPreview:*source;
                    const double fps=m_controller->document()->project().fps;const qreal x=frameX(clip.startFrame),width=qreal(clip.sampleCount)*fps/audioSampleRate*cellWidth;
                    const QRectF block(x+1,y+4,std::max(qreal(1),width-2),rowHeight-8);painter.fillRect(block,QColor(selected?ui::selected:ui::raised));painter.setPen(QColor(selected?ui::focus:ui::border));painter.setBrush(Qt::NoBrush);painter.drawRect(block);
                    painter.setPen(QColor(clip.muted?ui::disabled:selected?ui::focus:ui::muted));
                    const int from=std::max(label,int(std::floor(x))+1),to=std::min(viewport()->width()-1,int(std::ceil(x+width))-1);const qreal center=y+rowHeight/2.0,height=(rowHeight-12)/2.0;
                    for(int pixel=from;pixel<=to;++pixel){const qint64 a=std::clamp(audioSampleFrameAtTimelineFrame(qreal(pixel-x)/cellWidth,fps),qint64(0),clip.sampleCount-1),b=std::clamp(audioSampleFrameAtTimelineFrame(qreal(pixel+1-x)/cellWidth,fps),a+1,clip.sampleCount);const auto peak=audioPeak(*clip.asset,clip.sourceOffsetSamples+a,b-a);
                        painter.drawLine(QPointF(pixel,center-std::clamp(qreal(peak.maximum)*clip.gain,qreal(-1),qreal(1))*height),QPointF(pixel,center-std::clamp(qreal(peak.minimum)*clip.gain,qreal(-1),qreal(1))*height));}
                    painter.setPen(QPen(QColor(selected?ui::focus:ui::border),2));painter.drawLine(QPointF(x+3,y+7),QPointF(x+3,y+rowHeight-7));painter.drawLine(QPointF(x+width-3,y+7),QPointF(x+width-3,y+rowHeight-7));
                }
            }else if(entry.symbolLane&&shot){
                if(const auto *layer=layerFor(*shot,entry.layerId);layer&&layer->symbol){
                    const bool preview=symbolGesture()&&m_symbolLayerId==entry.layerId;
                    const auto clip=preview?m_symbolPreview:*layer->symbol;
                    const int end=symbolClipEnd(clip,shot->durationFrames),x=frameX(clip.startFrame),right=frameX(end);
                    QRect block(x+1,y+5,std::max(2,right-x-2),rowHeight-10);
                    // Dashed sections remain editable even where their owning cel
                    // is not exposed. Solid sections indicate visible exposure.
                    painter.setBrush(QColor(ui::base));painter.setPen(QPen(QColor(ui::muted),1,Qt::DashLine));painter.drawRect(block.adjusted(0,0,-1,-1));
                    QVector<QPair<int,int>> intervals;const auto owner=animationOwner(*shot,entry.layerId);
                    if(owner.drawingId.isEmpty())intervals.append({clip.startFrame,end});
                    else if(const auto *track=celTrack(*shot,owner.trackId))for(const auto &exposure:track->exposures)if(exposure.drawingId==owner.drawingId)
                        intervals.append({std::max(clip.startFrame,exposure.startFrame),std::min(end,exposure.startFrame+exposure.durationFrames)});
                    for(const auto &interval:intervals)if(interval.second>interval.first){QRect visible(frameX(interval.first)+1,y+5,std::max(1,frameX(interval.second)-frameX(interval.first)-2),rowHeight-10);painter.fillRect(visible,QColor(selected?ui::selected:ui::raised));painter.setBrush(Qt::NoBrush);painter.setPen(QPen(QColor(selected?ui::focus:ui::border),selected?2:1));painter.drawRect(visible.adjusted(0,0,-1,-1));}
                    painter.setBrush(Qt::NoBrush);painter.setPen(QPen(QColor(selected?ui::focus:ui::border),selected?2:1,owner.drawingId.isEmpty()?Qt::SolidLine:Qt::DashLine));painter.drawRect(block.adjusted(0,0,-1,-1));
                    if(preview&&!m_symbolValid){painter.setPen(QPen(QColor(ui::text),2,Qt::DashLine));painter.drawLine(block.topLeft(),block.bottomRight());}
                    if(block.width()>=18){painter.drawLine(block.left()+2,block.top()+3,block.left()+2,block.bottom()-3);painter.drawLine(block.right()-2,block.top()+3,block.right()-2,block.bottom()-3);}
                    if(clip.playback==SymbolPlayback::Loop||clip.playback==SymbolPlayback::PingPong)if(const auto *definition=findSymbol(m_controller->document()->project(),clip.symbolId)){
                        const int period=clip.playback==SymbolPlayback::Loop?definition->content.durationFrames:2*(definition->content.durationFrames-1);
                        if(period>0&&period/clip.speed*cellWidth>=12){
                            const int from=std::max(first,clip.startFrame),to=std::min(last+1,end);
                            const qreal phase=(from-clip.startFrame+clip.playbackOffset)*clip.speed+clip.firstFrame;
                            qint64 cycle=qint64(std::floor(phase/period))+1;
                            painter.setPen(QColor(ui::muted));
                            // At most one marker per twelve visible pixels; never
                            // walk every frame of a long Symbol definition.
                            for(int n=0;n<viewport()->width()/12+2;++n,++cycle){
                                const qreal boundary=std::ceil((cycle*period-clip.firstFrame)/clip.speed)-clip.playbackOffset+clip.startFrame;
                                if(boundary>=to)break;if(boundary<=from)continue;
                                const int tick=frameX(int(boundary));painter.drawLine(tick,block.top()+2,tick,block.top()+5);painter.drawLine(tick,block.bottom()-5,tick,block.bottom()-2);
                            }
                        }
                    }
                    if(block.width()>45){const QStringList modes{"Loop","Once","Still","Ping-pong","Poses"};const QString caption=clip.playback==SymbolPlayback::Poses?QString("Poses · %1 keys").arg(clip.poseKeys.size()):QString("%1 · %2").arg(modes.value(int(clip.playback))).arg(clip.firstFrame+1);
                        const QRect text=block.adjusted(8,0,-8,0);painter.setPen(QColor(ui::text));painter.drawText(text,Qt::AlignVCenter|Qt::AlignLeft,fontMetrics().elidedText(caption,Qt::ElideRight,text.width()));}
                }
            }else if(entry.poseLane()&&shot){
                paintPoseLane(painter,*shot,entry,y);
            }else if(entry.propertyLane()&&shot){
                const auto frames=laneFrames(*shot,entry);QSet<int> selectedFrames,dragFrames;for(const auto &key:m_controller->selectedKeys())if(sameLane(entry,key))selectedFrames.insert(key.frame);if(m_gesture==Gesture::Keys)for(const auto &key:m_dragKeys)if(sameLane(entry,key))dragFrames.insert(key.frame);painter.setRenderHint(QPainter::Antialiasing,true);
                if(frames.size()>1){painter.setPen(QPen(QColor(ui::border),1));painter.drawLine(label+frames.first()*cellWidth+cellWidth/2-left,y+rowHeight/2,label+frames.last()*cellWidth+cellWidth/2-left,y+rowHeight/2);}
                for(int frame:frames){const bool selectedKey=selectedFrames.contains(frame);const int actual=dragFrames.contains(frame)?frame+m_dragDelta:frame;
                    const int x=label+actual*cellWidth+cellWidth/2-left;if(x+6<label||x-6>viewport()->width())continue;
                    const bool current=actual==m_controller->currentFrame();painter.setPen(QPen(QColor(selectedKey?ui::text:current?ui::focus:ui::text),selectedKey?2:1));painter.setBrush(QColor(selectedKey?ui::accent:current?ui::selected:ui::muted));
                    const qreal cy=y+rowHeight/2;painter.drawPolygon(QPolygonF{QPointF(x,cy-6),QPointF(x+6,cy),QPointF(x,cy+6),QPointF(x-6,cy)});
                }painter.setRenderHint(QPainter::Antialiasing,false);
            }else if(entry.track){
                for(const auto &original:entry.track->exposures){auto exposure=original;const bool preview=exposureGesture()&&entry.id==m_exposureTrack&&original.id==m_exposure.id;const bool moving=m_gesture==Gesture::Cels&&m_dragCelIds.contains(original.id);if(preview){exposure.startFrame=m_exposureStart;exposure.durationFrames=m_exposureHold;}if(moving)exposure.startFrame+=m_dragDelta;const int end=exposure.startFrame+exposure.durationFrames;
                    const bool picked=selectedCelIds.contains(original.id);
                    if(end<=first||exposure.startFrame>last)continue;
                    const int x=frameX(exposure.startFrame);QRect block(x+1,y+5,std::max(1,qRound(exposure.durationFrames*cellWidth)-2),rowHeight-10);
                    painter.fillRect(block,QColor(picked?ui::selected:ui::raised));painter.setPen(QPen(QColor(picked?ui::text:ui::border),picked?2:1));painter.setBrush(Qt::NoBrush);painter.drawRect(block.adjusted(0,0,-1,-1));
                    if((preview&&!exposureValid())||(moving&&!validCelMove)){painter.setPen(QPen(QColor(ui::text),2,Qt::DashLine));painter.drawRect(block.adjusted(0,0,-1,-1));painter.drawLine(block.topLeft(),block.bottomRight());}
                    painter.setPen(QColor(ui::text));painter.setBrush(QColor(ui::text));if(block.width()>=7)painter.drawEllipse(QPointF(x+std::min(cellWidth/2,qreal(block.width()/2)),y+rowHeight/2),3,3);
                    if(exposure.durationFrames>1&&block.width()>cellWidth+6)painter.drawLine(x+cellWidth-3,y+rowHeight/2,block.right()-5,y+rowHeight/2);
                    const AnimationDrawing *drawing=nullptr;for(const auto &candidate:entry.track->drawings)if(candidate.id==exposure.drawingId){drawing=&candidate;break;}
                    const auto *layer=drawing&&shot?layerFor(*shot,drawing->groupLayerId):nullptr;
                    if(layer&&block.width()>40){QRect text=block.adjusted(14,0,-4,0);const auto id=drawingDisplayId(*entry.track,exposure.drawingId);
                        if(text.width()>=fontMetrics().horizontalAdvance(id)){
                            painter.fillRect(text,QColor(picked?ui::selected:ui::raised));
                            const auto label=text.width()>=fontMetrics().horizontalAdvance(id+" · M…")?id+" · "+layer->name:id;
                            painter.drawText(text,Qt::AlignVCenter|Qt::AlignLeft,fontMetrics().elidedText(label,Qt::ElideRight,text.width()));}}
                }
            }else{painter.fillRect(QRect(label,y+9,std::max(0,qRound(m_controller->frameCount()*cellWidth)-left),rowHeight-18),QColor(ui::raised));}
            painter.setPen(QColor(ui::border));painter.drawLine(label,y+rowHeight-1,viewport()->width(),y+rowHeight-1);painter.restore();
            painter.save();painter.setClipRect(QRect(0,rulerHeight,label,viewport()->height()-rulerHeight));
            const int inset=headerIndent(entry);
            const int iconX=entry.artworkRow()?28+inset:6+inset;
            appIcon(entry.cueLane?(entry.dialogueLane?"type":"pin"):!entry.audioId.isEmpty()?(entry.visible?"volume-2":"volume-x"):entry.symbolLane?"toon-symbol":entry.poseLane()?"toon-symbol-pose":entry.cameraLane()?"video":entry.propertyLane()?(entry.property?"sliders-horizontal":"spline"):entry.track?"clapperboard":entry.folder?"folder-open":"image").paint(&painter,QRect(iconX,y+(rowHeight-16)/2,16,16),Qt::AlignCenter,entry.visible&&included?QIcon::Normal:QIcon::Disabled);
            painter.setPen(QColor(entry.visible&&included?ui::text:ui::disabled));const int right=entry.artworkRow()?86:4;QRect name(iconX+22,y,std::max(0,label-iconX-22-right),rowHeight);
            painter.drawText(name,Qt::AlignVCenter|Qt::AlignLeft,fontMetrics().elidedText(entry.name,Qt::ElideRight,name.width()));
            painter.setPen(QColor(ui::border));painter.drawLine(0,y+rowHeight-1,label,y+rowHeight-1);painter.restore();
        }
        if((m_gesture==Gesture::Marquee||m_gesture==Gesture::CelMarquee)&&m_dragMoved){painter.save();painter.setClipRect(QRect(label,rulerHeight,viewport()->width()-label,viewport()->height()-rulerHeight));
            QRect selection(QPoint(frameX(std::min(m_marqueeFrame,m_marqueeEndFrame)),rulerHeight+std::min(m_marqueeRow,m_marqueeEndRow)*rowHeight-top),QPoint(frameX(std::max(m_marqueeFrame,m_marqueeEndFrame)+1),rulerHeight+(std::max(m_marqueeRow,m_marqueeEndRow)+1)*rowHeight-top));
            QColor fill(ui::accent);fill.setAlpha(35);painter.fillRect(selection,fill);painter.setPen(QPen(QColor(ui::focus),1,Qt::DashLine));painter.setBrush(Qt::NoBrush);painter.drawRect(selection);painter.restore();}
        painter.fillRect(QRect(0,0,viewport()->width(),rulerHeight),QColor(ui::panel));painter.setPen(QColor(ui::muted));painter.drawText(QRect(6,0,label-12,rulerHeight),Qt::AlignVCenter|Qt::AlignLeft,"Tracks");
        painter.save();painter.setClipRect(QRect(label,0,viewport()->width()-label,viewport()->height()));
        if(m_controller->playbackRangeEnabled()||rangeGesture()){const int a=rangeGesture()?m_rangeFirst:m_controller->playbackFirstFrame(),b=rangeGesture()?m_rangeLast:m_controller->playbackLastFrame();
            painter.fillRect(QRect(frameX(a),0,(b-a+1)*cellWidth,rulerHeight),QColor(ui::selected));painter.setPen(QPen(QColor(ui::focus),2));painter.drawLine(frameX(a),rulerHeight-2,frameX(b+1),rulerHeight-2);painter.drawLine(frameX(a),3,frameX(a),rulerHeight-2);painter.drawLine(frameX(b+1),3,frameX(b+1),rulerHeight-2);}
        for(int frame=(first/labelStep)*labelStep;frame<=last;frame+=labelStep){const int x=frameX(frame);painter.setPen(QColor(ui::muted));painter.drawText(QRect(x+2,0,qRound(labelStep*cellWidth)-4,rulerHeight),Qt::AlignVCenter|Qt::AlignLeft,QString::number(frame+1));}
        if(shot){const qreal x=label+(m_controller->currentFrame()+.5)*cellWidth-left;const QString number=QString::number(m_controller->currentFrame()+1);const int badge=std::max(20,fontMetrics().horizontalAdvance(number)+8);
            painter.fillRect(QRectF(x-badge/2.0,0,badge,rulerHeight),QColor(ui::selected));painter.setPen(QColor(ui::text));
            painter.drawText(QRectF(x-badge/2.0,0,badge,rulerHeight),Qt::AlignCenter,number);painter.setPen(QPen(QColor(ui::accent),2));painter.drawLine(QPointF(x,rulerHeight),QPointF(x,viewport()->height()));}
        painter.restore();painter.setPen(QColor(ui::border));painter.drawLine(label-1,0,label-1,viewport()->height());
        if(m_gesture==Gesture::TrackMove&&m_dragMoved&&m_dropRow>=0){
            painter.save();painter.setClipRect(QRect(0,rulerHeight,viewport()->width(),viewport()->height()-rulerHeight));
            const int y=rulerHeight+m_dropRow*rowHeight-top;
            painter.setPen(QPen(QColor(m_dropValid?ui::focus:ui::muted),3,m_dropValid?Qt::SolidLine:Qt::DashLine));painter.setBrush(Qt::NoBrush);
            if(m_dropInto)painter.drawRect(QRect(2,y+2,label-5,rowHeight-5));
            else painter.drawLine(4,y,viewport()->width()-4,y);
            if(!m_dropValid){painter.drawLine(8,y-5,18,y+5);painter.drawLine(8,y+5,18,y-5);}painter.restore();
        }
        if(rows.isEmpty()){painter.setPen(QColor(ui::muted));painter.drawText(viewport()->rect().adjusted(label+5,rulerHeight,0,0),Qt::AlignCenter,shot?"Add a track":"Open a project");}
        if(hasFocus()){painter.setPen(QColor(ui::focus));painter.setBrush(Qt::NoBrush);painter.drawRect(viewport()->rect().adjusted(0,0,-1,-1));}
    }
    void mousePressEvent(QMouseEvent *event)override{
        if(event->button()!=Qt::LeftButton){QAbstractScrollArea::mousePressEvent(event);return;}
        setFocus(Qt::MouseFocusReason);QToolTip::hideText();const QPoint point=event->position().toPoint();
        if(!timelineShot(m_controller))return;
        if(point.y()<rulerHeight&&point.x()>=labelWidth()){
            if(!m_controller->prepareTimelineEdit())return;
            const int first=m_controller->playbackFirstFrame(),last=m_controller->playbackLastFrame();
            const int firstX=frameX(first),lastX=frameX(last+1);
            if(event->modifiers()&Qt::ShiftModifier){startGesture(Gesture::RangeNew,point);m_rangeAnchor=frameAt(point.x());m_rangeFirst=m_rangeLast=m_rangeAnchor;}
            else if(m_controller->playbackRangeEnabled()&&std::abs(point.x()-firstX)<=6){startGesture(Gesture::RangeStart,point);m_rangeFirst=first;m_rangeLast=last;}
            else if(m_controller->playbackRangeEnabled()&&std::abs(point.x()-lastX)<=6){startGesture(Gesture::RangeEnd,point);m_rangeFirst=first;m_rangeLast=last;}
            else{m_scrubbing=true;scrub(point.x());}
            viewport()->update();event->accept();return;
        }
        if(!m_controller->prepareTimelineEdit())return;
        const auto rows=timelineRows(m_controller);const int row=rowAt(point.y());
        if(point.y()<rulerHeight||row<0||row>=rows.size())return;const auto entry=rows[row];
        if(entry.artworkRow()&&point.x()<labelWidth()){
            if(!m_controller->selectTimelineLayer(entry.layerId))return;m_selectedLane.clear();
            m_dragLayer=entry.layerId;m_dropValid=false;m_dropInto=false;m_dropError.clear();m_dropRow=-1;
            startGesture(Gesture::TrackMove,point);viewport()->update();event->accept();return;
        }
        if(entry.cueLane){
            const auto hit=cueAtPoint(entry,point.x());
            if(hit){if(!m_controller->selectCue(hit->id))return;m_selectedLane.clear();
                if(!m_controller->playing()){m_cueOriginal=m_cuePreview=*hit;m_cueStart=hit->startFrame;m_cueDuration=hit->durationFrames;m_cueValid=true;
                    const int left=frameX(hit->startFrame),right=frameX(hit->startFrame+hit->durationFrames);const bool trim=hit->durationFrames>1&&right-left>=18;
                    startGesture(trim&&std::abs(point.x()-left)<=6?Gesture::CueStart:trim&&std::abs(point.x()-right)<=6?Gesture::CueEnd:Gesture::CueMove,point);}
            }else if(point.x()<labelWidth()&&!entry.cues.isEmpty()){m_controller->selectCue(entry.cues.first().id);m_selectedLane.clear();}
            else if(point.x()>=labelWidth()){m_controller->clearCueSelection();m_scrubbing=true;scrub(point.x());}
        }else if(!entry.audioId.isEmpty()){
            const auto *source=audioFor(*timelineShot(m_controller),entry.audioId);if(!source)return;const auto clip=*source;
            if(!m_controller->selectAudioClip(entry.audioId))return;m_selectedLane.clear();emit m_controller->audioPropertiesRequested();
            const qreal left=frameX(clip.startFrame),right=left+qreal(clip.sampleCount)*m_controller->document()->project().fps/audioSampleRate*cellWidth;
            if(point.x()>=labelWidth()&&point.x()>=left-1&&point.x()<=right+1&&!m_controller->playing()){
                m_audioOriginal=m_audioPreview=clip;const bool trim=right-left>=18;const auto kind=trim&&std::abs(point.x()-left)<=6?Gesture::AudioStart:trim&&std::abs(point.x()-right)<=6?Gesture::AudioEnd:Gesture::AudioMove;startGesture(kind,point);
            }else if(point.x()>=labelWidth()){m_scrubbing=true;scrub(point.x());}
        }else if(entry.symbolLane){
            const auto *layer=layerFor(*timelineShot(m_controller),entry.layerId);if(!layer||!layer->symbol)return;const auto clip=*layer->symbol;
            if(!m_controller->selectSymbolClip(entry.layerId))return;m_selectedLane.clear();emit m_controller->symbolPropertiesRequested();
            const int left=frameX(clip.startFrame),right=frameX(symbolClipEnd(clip,m_controller->frameCount()));
            if(point.x()>=labelWidth()&&point.x()>=left-1&&point.x()<=right+1&&!entry.locked&&!m_controller->playing()){
                m_symbolLayerId=entry.layerId;m_symbolOriginal=m_symbolPreview=clip;m_symbolValid=true;m_symbolValue=0;
                const bool trim=right-left>=18;const auto kind=(event->modifiers()&Qt::AltModifier)?Gesture::SymbolSlip:trim&&std::abs(point.x()-left)<=6?Gesture::SymbolStart:trim&&std::abs(point.x()-right)<=6?Gesture::SymbolEnd:Gesture::SymbolMove;
                startGesture(kind,point);
            }else if(point.x()>=labelWidth()){m_scrubbing=true;scrub(point.x());}
        }else if(entry.propertyLane()){
            if(!selectKeyRow(entry))return;
            if(point.x()<labelWidth()){viewport()->update();return;}
            const auto *shot=timelineShot(m_controller);const int frame=keyFrameAt(shot,entry,point.x());
            const bool exists=shot&&laneFrames(*shot,entry).contains(frame);const auto key=keyFor(entry,frame);
            if(exists){
                auto selected=m_controller->selectedKeys();
                const bool toggle=event->modifiers()&(Qt::ControlModifier|Qt::MetaModifier);
                if(toggle){
                    if(selected.contains(key)){
                        selected.removeAll(key);
                        if(key.kind==TimelineKeyKind::Layer&&!key.isPath()&&(key.property==KeyframeProperty::PositionX||key.property==KeyframeProperty::PositionY)){
                            const auto *animation=layerAnimation(*shot,key.layerId);
                            if(animation&&hasSpatialMotion(*animation)){
                                auto paired=key;paired.property=key.property==KeyframeProperty::PositionX?KeyframeProperty::PositionY:KeyframeProperty::PositionX;
                                selected.removeAll(paired);
                            }
                        }
                    }else selected.append(key);
                }
                else if(event->modifiers()&Qt::ShiftModifier){
                    if(m_anchor){int anchorRow=-1;for(int i=0;i<rows.size();++i)if(sameLane(rows[i],*m_anchor)){anchorRow=i;break;}
                        if(anchorRow>=0){for(const auto &candidate:keysInRect(rows,std::min(anchorRow,row),std::max(anchorRow,row),std::min(m_anchor->frame,frame),std::max(m_anchor->frame,frame)))if(!selected.contains(candidate))selected.append(candidate);}
                        else if(!selected.contains(key))selected.append(key);
                    }else if(!selected.contains(key))selected.append(key);
                }else if(!selected.contains(key))selected={key};
                m_controller->setSelectedKeys(selected);selected=m_controller->selectedKeys();m_anchor=key;
                if(!seekKeyFrame(keyFor(entry,frame),frame))return;
                if(!entry.locked&&!m_controller->playing()&&selected.contains(key)){
                    m_dragKeys=selected;m_dragDelta=0;m_dragFrame=frame;startGesture(Gesture::Keys,point);
                }
            }else{
                m_originalSelection=m_controller->selectedKeys();m_marqueeBase=(event->modifiers()&(Qt::ShiftModifier|Qt::ControlModifier|Qt::MetaModifier))?m_originalSelection:QVector<TimelineKeyRef>{};
                m_marqueeRow=row;m_marqueeFrame=frame;m_marqueeEndRow=row;m_marqueeEndFrame=frame;
                if(!seekKeyFrame(keyFor(entry,frame),frame))return;
                startGesture(Gesture::Marquee,point);m_controller->setSelectedKeys(m_marqueeBase);
            }
        }else{
            m_selectedLane.clear();m_controller->clearKeySelection();
            const auto *hit=entry.track&&point.x()>=labelWidth()?exposureAtPoint(*entry.track,point.x()):nullptr;
            const auto saved=hit?std::optional<CelExposure>(*hit):std::nullopt;
            if(!(entry.track?m_controller->selectTrack(entry.id):m_controller->selectTimelineLayer(entry.layerId)))return;
            if(point.x()<labelWidth()){m_controller->clearCelSelection();}
            else if(entry.track){
                emit m_controller->drawingPropertiesRequested();
                const auto originalSelection=m_controller->selectedCels();
                if(!m_controller->setFrame(saved?std::clamp(frameAt(point.x()),saved->startFrame,saved->startFrame+saved->durationFrames-1):frameAt(point.x())))return;
                if(saved){
                    const TimelineCelRef ref{entry.id,saved->id};auto selection=originalSelection;
                    const bool toggle=event->modifiers()&(Qt::ControlModifier|Qt::MetaModifier);
                    if(toggle){if(selection.contains(ref))selection.removeAll(ref);else selection.append(ref);}
                    else if((event->modifiers()&Qt::ShiftModifier)&&m_celAnchor){
                        int anchorRow=-1;for(int i=0;i<rows.size();++i)if(rows[i].id==m_celAnchor->trackId)anchorRow=i;
                        if(const auto *anchor=celExposure(*timelineShot(m_controller),*m_celAnchor);anchor&&anchorRow>=0){
                            for(const auto &item:celsInRect(rows,std::min(row,anchorRow),std::max(row,anchorRow),std::min(anchor->startFrame,saved->startFrame),std::max(anchor->startFrame+anchor->durationFrames-1,saved->startFrame+saved->durationFrames-1)))if(!selection.contains(item))selection.append(item);}
                        else if(!selection.contains(ref))selection.append(ref);
                    }else if(!selection.contains(ref))selection={ref};
                    m_controller->setSelectedCels(selection);
                    if(!(event->modifiers()&Qt::ShiftModifier))m_celAnchor=ref;
                    selection=m_controller->selectedCels();
                    if(selection.contains(ref)&&!entry.locked&&!m_controller->playing()){
                        m_exposureTrack=entry.id;m_exposure=*saved;m_exposureStart=saved->startFrame;m_exposureHold=saved->durationFrames;
                        const int left=frameX(saved->startFrame),right=frameX(saved->startFrame+saved->durationFrames);
                        const bool trim=selection.size()==1&&right-left>=18&&!toggle&&!(event->modifiers()&Qt::ShiftModifier);
                        const auto kind=trim&&std::abs(point.x()-left)<=6?Gesture::ExposureStart:trim&&std::abs(point.x()-right)<=6?Gesture::ExposureEnd:Gesture::Cels;
                        m_dragCels=selection;m_dragCelIds.clear();for(const auto &item:selection)m_dragCelIds.insert(item.exposureId);m_dragDelta=0;
                        startGesture(kind,point);
                    }
                }else{
                    m_originalCels=originalSelection;m_celMarqueeBase=(event->modifiers()&(Qt::ShiftModifier|Qt::ControlModifier|Qt::MetaModifier))?originalSelection:QVector<TimelineCelRef>{};
                    m_marqueeRow=m_marqueeEndRow=row;m_marqueeFrame=m_marqueeEndFrame=frameAt(point.x());
                    m_controller->setSelectedCels(m_celMarqueeBase);startGesture(Gesture::CelMarquee,point);
                }
            }else{m_controller->clearCelSelection();if(point.x()>=labelWidth()){m_scrubbing=true;scrub(point.x());}}
        }
        viewport()->update();event->accept();
    }
    void mouseDoubleClickEvent(QMouseEvent *event)override{
        if(event->button()==Qt::LeftButton){const auto rows=timelineRows(m_controller);const int row=rowAt(event->position().y());
            if(event->position().x()<labelWidth()&&event->position().y()>=rulerHeight&&row>=0&&row<rows.size()&&rows[row].folder){
                cancelGesture();m_controller->setTimelineFolderCollapsed(rows[row].layerId,!m_controller->timelineFolderCollapsed(rows[row].layerId));event->accept();return;}
            if(event->position().y()>=rulerHeight&&row>=0&&row<rows.size()&&rows[row].cueLane){
                cancelGesture();if(const auto cue=cueAtPoint(rows[row],event->position().x());cue&&m_controller->selectCue(cue->id)){m_selectedLane.clear();emit m_controller->cuePropertiesRequested();}event->accept();return;}
            if(event->position().y()>=rulerHeight&&row>=0&&row<rows.size()&&rows[row].poseLane()){
                cancelGesture();const int frame=event->position().x()>=labelWidth()?keyFrameAt(timelineShot(m_controller),rows[row],event->position().x()):m_controller->currentFrame();
                pickPose(rows[row],frame);event->accept();return;}
            if(event->position().y()>=rulerHeight&&row>=0&&row<rows.size()&&rows[row].symbolLane){
                cancelGesture();if(m_controller->selectSymbolClip(rows[row].layerId)){m_selectedLane.clear();emit m_controller->symbolPropertiesRequested();openSymbolFramePicker(m_controller,rows[row].layerId,this);}event->accept();return;}}
        QAbstractScrollArea::mouseDoubleClickEvent(event);
    }
    void wheelEvent(QWheelEvent *event)override{
        if(event->modifiers()&(Qt::ControlModifier|Qt::MetaModifier)){
            const qreal delta=!event->angleDelta().isNull()?event->angleDelta().y()/120.0:event->pixelDelta().y()/40.0;
            setZoomPercent(zoomPercent()*std::pow(1.25,delta),qRound(event->position().x()));event->accept();return;
        }
        QAbstractScrollArea::wheelEvent(event);
    }
    void mouseMoveEvent(QMouseEvent *event)override{
        if(m_gesture!=Gesture::None&&(event->buttons()&Qt::LeftButton)){
            const QPoint point=event->position().toPoint();autoScroll(point);
            if(!m_dragMoved&&(point-m_pressPoint).manhattanLength()<QApplication::startDragDistance()){event->accept();return;}
            m_dragMoved=true;const int frame=frameAt(point.x());
            if(m_gesture==Gesture::TrackMove){updateTrackDrop(point);}
            else if(cueGesture()){
                const int delta=qRound(qreal(point.x()-m_pressPoint.x()+horizontalScrollBar()->value()-m_pressScroll)/cellWidth);
                m_cueStart=m_cueOriginal.startFrame+(m_gesture==Gesture::CueEnd?0:delta);
                m_cueDuration=m_cueOriginal.durationFrames+(m_gesture==Gesture::CueEnd?delta:m_gesture==Gesture::CueStart?-delta:0);
                m_cueValid=previewTimelineCueTiming(m_cueOriginal,m_controller->frameCount(),m_cueStart,m_cueDuration,&m_cuePreview);
            }else if(symbolGesture()){
                const int delta=qRound(qreal(point.x()-m_pressPoint.x()+horizontalScrollBar()->value()-m_pressScroll)/cellWidth);
                m_symbolValue=m_gesture==Gesture::SymbolStart?m_symbolOriginal.startFrame+delta:m_gesture==Gesture::SymbolEnd?symbolClipEnd(m_symbolOriginal,m_controller->frameCount())+delta:delta;
                m_symbolValid=previewSymbolClipEdit(m_symbolOriginal,m_controller->frameCount(),symbolOperation(m_gesture),m_symbolValue,&m_symbolPreview);
            }else if(m_gesture==Gesture::Keys){int high=m_controller->frameCount()-1;for(const auto &key:m_dragKeys)high=std::min(high,m_controller->frameCount()-1-key.frame);int minimum=m_controller->frameCount()-1;for(const auto &key:m_dragKeys)minimum=std::min(minimum,key.frame);m_dragDelta=std::clamp(frame-m_dragFrame,-minimum,high);}
            else if(m_gesture==Gesture::Cels){m_dragDelta=qRound(qreal(point.x()-m_pressPoint.x()+horizontalScrollBar()->value()-m_pressScroll)/cellWidth);}
            else if(m_gesture==Gesture::ExposureMove){const int delta=qRound(qreal(point.x()-m_pressPoint.x()+horizontalScrollBar()->value()-m_pressScroll)/cellWidth);m_exposureStart=std::clamp(m_exposure.startFrame+delta,0,m_controller->frameCount()-m_exposure.durationFrames);}
            else if(m_gesture==Gesture::ExposureStart){const int end=m_exposure.startFrame+m_exposure.durationFrames;m_exposureStart=std::clamp(frame,0,end-1);m_exposureHold=end-m_exposureStart;}
            else if(m_gesture==Gesture::ExposureEnd){const int end=std::clamp(qRound(qreal(point.x()-labelWidth()+horizontalScrollBar()->value())/cellWidth),m_exposure.startFrame+1,m_controller->frameCount());m_exposureStart=m_exposure.startFrame;m_exposureHold=end-m_exposure.startFrame;}
            else if(m_gesture==Gesture::AudioMove){const int delta=qRound(qreal(point.x()-m_pressPoint.x()+horizontalScrollBar()->value()-m_pressScroll)/cellWidth);m_audioPreview.startFrame=std::clamp(m_audioOriginal.startFrame+delta,0,m_controller->frameCount()-1);}
            else if(m_gesture==Gesture::AudioStart){const double fps=m_controller->document()->project().fps;const int minimum=std::max(0,m_audioOriginal.startFrame-int(std::floor(qreal(m_audioOriginal.sourceOffsetSamples)*fps/audioSampleRate))),maximum=std::min(m_controller->frameCount()-1,m_audioOriginal.startFrame+int(std::floor(qreal(m_audioOriginal.sampleCount-1)*fps/audioSampleRate)));
                m_audioPreview.startFrame=std::clamp(frame,minimum,maximum);const int frameDelta=m_audioPreview.startFrame-m_audioOriginal.startFrame;const auto delta=(frameDelta<0?-1:1)*audioSampleFrameAtTimelineFrame(std::abs(frameDelta),fps);m_audioPreview.sourceOffsetSamples=m_audioOriginal.sourceOffsetSamples+delta;m_audioPreview.sampleCount=m_audioOriginal.sampleCount-delta;}
            else if(m_gesture==Gesture::AudioEnd){const double fps=m_controller->document()->project().fps;const int end=std::max(m_audioOriginal.startFrame+1,qRound(qreal(point.x()-labelWidth()+horizontalScrollBar()->value())/cellWidth));m_audioPreview.sampleCount=std::clamp(audioSampleFrameAtTimelineFrame(end-m_audioOriginal.startFrame,fps),qint64(1),m_audioOriginal.asset->sampleFrameCount()-m_audioOriginal.sourceOffsetSamples);}
            else if(m_gesture==Gesture::RangeNew){m_rangeFirst=std::min(frame,m_rangeAnchor);m_rangeLast=std::max(frame,m_rangeAnchor);}
            else if(m_gesture==Gesture::RangeStart)m_rangeFirst=std::min(frame,m_rangeLast);
            else if(m_gesture==Gesture::RangeEnd)m_rangeLast=std::max(m_rangeFirst,std::clamp(qRound(qreal(point.x()-labelWidth()+horizontalScrollBar()->value())/cellWidth)-1,0,m_controller->frameCount()-1));
            else if(m_gesture==Gesture::Marquee){const auto rows=timelineRows(m_controller);m_marqueeEndRow=std::clamp(rowAt(point.y()),0,std::max(0,int(rows.size())-1));m_marqueeEndFrame=frame;auto selected=m_marqueeBase;
                for(const auto &key:keysInRect(rows,std::min(m_marqueeRow,m_marqueeEndRow),std::max(m_marqueeRow,m_marqueeEndRow),std::min(m_marqueeFrame,frame),std::max(m_marqueeFrame,frame)))if(!selected.contains(key))selected.append(key);m_controller->setSelectedKeys(selected);}
            else if(m_gesture==Gesture::CelMarquee){const auto rows=timelineRows(m_controller);m_marqueeEndRow=std::clamp(rowAt(point.y()),0,std::max(0,int(rows.size())-1));m_marqueeEndFrame=frame;auto selection=m_celMarqueeBase;
                for(const auto &ref:celsInRect(rows,std::min(m_marqueeRow,m_marqueeEndRow),std::max(m_marqueeRow,m_marqueeEndRow),std::min(m_marqueeFrame,frame),std::max(m_marqueeFrame,frame)))if(!selection.contains(ref))selection.append(ref);m_controller->setSelectedCels(selection);}
            viewport()->update();event->accept();return;
        }
        if(m_scrubbing&&(event->buttons()&Qt::LeftButton)){scrub(event->position().x());event->accept();return;}
        if(m_gesture!=Gesture::None&&!(event->buttons()&Qt::LeftButton))cancelGesture();
        if(m_scrubbing&&!(event->buttons()&Qt::LeftButton)){m_controller->endScrub();m_scrubbing=false;}
        const auto point=event->position().toPoint();Qt::CursorShape cursor=Qt::ArrowCursor;
        if(point.x()>=labelWidth()){
            if(point.y()<rulerHeight&&m_controller->playbackRangeEnabled()&&(std::abs(point.x()-frameX(m_controller->playbackFirstFrame()))<=6||std::abs(point.x()-frameX(m_controller->playbackLastFrame()+1))<=6))cursor=Qt::SizeHorCursor;
            const auto rows=timelineRows(m_controller);const int row=rowAt(point.y());
            if(point.y()>=rulerHeight&&row>=0&&row<rows.size()&&rows[row].cueLane)if(const auto cue=cueAtPoint(rows[row],point.x())){
                const int left=frameX(cue->startFrame),right=frameX(cue->startFrame+cue->durationFrames);cursor=cue->durationFrames>1&&right-left>=18&&(std::abs(point.x()-left)<=6||std::abs(point.x()-right)<=6)?Qt::SizeHorCursor:Qt::OpenHandCursor;}
            if(point.y()>=rulerHeight&&row>=0&&row<rows.size()&&!rows[row].audioId.isEmpty())if(const auto *shot=timelineShot(m_controller))if(const auto *clip=audioFor(*shot,rows[row].audioId)){const qreal left=frameX(clip->startFrame),right=left+qreal(clip->sampleCount)*m_controller->document()->project().fps/audioSampleRate*cellWidth;if(point.x()>=left&&point.x()<=right)cursor=right-left>=18&&(std::abs(point.x()-left)<=6||std::abs(point.x()-right)<=6)?Qt::SizeHorCursor:Qt::OpenHandCursor;}
            if(point.y()>=rulerHeight&&row>=0&&row<rows.size()&&rows[row].symbolLane&&!rows[row].locked)if(const auto *shot=timelineShot(m_controller))if(const auto *layer=layerFor(*shot,rows[row].layerId);layer&&layer->symbol){
                const int left=frameX(layer->symbol->startFrame),right=frameX(symbolClipEnd(*layer->symbol,shot->durationFrames));if(point.x()>=left&&point.x()<=right)cursor=right-left>=18&&(std::abs(point.x()-left)<=6||std::abs(point.x()-right)<=6)?Qt::SizeHorCursor:Qt::OpenHandCursor;}
            if(point.y()>=rulerHeight&&row>=0&&row<rows.size()&&rows[row].poseLane()&&!rows[row].locked)if(const auto *shot=timelineShot(m_controller))
                if(laneFrames(*shot,rows[row]).contains(keyFrameAt(shot,rows[row],point.x())))cursor=Qt::OpenHandCursor;
            if(point.y()>=rulerHeight&&row>=0&&row<rows.size()&&rows[row].track&&!rows[row].locked)if(const auto *exposure=exposureAtPoint(*rows[row].track,point.x()))
                cursor=exposure->durationFrames*cellWidth>=18&&(std::abs(point.x()-frameX(exposure->startFrame))<=6||std::abs(point.x()-frameX(exposure->startFrame+exposure->durationFrames))<=6)?Qt::SizeHorCursor:Qt::OpenHandCursor;
        }
        if(point.x()<labelWidth()&&point.y()>=rulerHeight){const auto rows=timelineRows(m_controller);const int row=rowAt(point.y());if(row>=0&&row<rows.size()&&rows[row].artworkRow())cursor=Qt::OpenHandCursor;}
        viewport()->setCursor(cursor);QAbstractScrollArea::mouseMoveEvent(event);
    }
    void mouseReleaseEvent(QMouseEvent *event)override{
        if(event->button()!=Qt::LeftButton){QAbstractScrollArea::mouseReleaseEvent(event);return;}
        finishGesture(true);m_controller->endScrub();m_scrubbing=false;viewport()->update();event->accept();
    }
    bool event(QEvent *event)override{
        if(!m_controller)return QAbstractScrollArea::event(event);
        if(event->type()==QEvent::WindowDeactivate||event->type()==QEvent::Hide){cancelGesture();m_controller->endScrub();m_scrubbing=false;}
        if(event->type()==QEvent::ShortcutOverride){const auto *key=static_cast<QKeyEvent *>(event);
            if(m_gesture==Gesture::TrackMove){event->accept();return true;}
            if(key->matches(QKeySequence::Copy)||key->matches(QKeySequence::Paste)||key->matches(QKeySequence::SelectAll)||key->key()==Qt::Key_Delete||key->key()==Qt::Key_Backspace||key->key()==Qt::Key_Escape||(key->key()==Qt::Key_M&&key->modifiers()==Qt::NoModifier)){event->accept();return true;}}
        return QAbstractScrollArea::event(event);
    }
    void keyPressEvent(QKeyEvent *event)override{
        // A held header is an organization preview, never a timing command target.
        if(m_gesture==Gesture::TrackMove&&event->key()!=Qt::Key_Escape){event->accept();return;}
        if(event->key()==Qt::Key_Escape){const bool active=m_gesture!=Gesture::None;cancelGesture();m_controller->endScrub();m_scrubbing=false;if(!active){m_controller->clearKeySelection();m_controller->clearCelSelection();m_controller->clearSymbolClipSelection();m_controller->clearCueSelection();if(!m_controller->selectedSymbolPoseLayerId().isEmpty())m_selectedLane.clear();m_controller->clearSymbolPoseSelection();}}
        else if(event->key()==Qt::Key_M&&event->modifiers()==Qt::NoModifier){finishGesture();m_controller->addCue(TimelineCueKind::Marker);}
        else if(event->matches(QKeySequence::Copy)){finishGesture();if(m_controller->selectedCueId().isEmpty()&&m_controller->selectedAudioClipId().isEmpty()&&m_controller->selectedSymbolClipId().isEmpty()){if(!m_controller->selectedCels().isEmpty())m_controller->copySelectedCels();else m_controller->copySelectedKeys();}}
        else if(event->matches(QKeySequence::Paste)){finishGesture();if(m_controller->selectedCueId().isEmpty()&&m_controller->selectedAudioClipId().isEmpty()&&m_controller->selectedSymbolClipId().isEmpty()){if(m_selectedLane.isEmpty()&&!m_controller->cameraSelected()&&m_controller->selectedSymbolPoseLayerId().isEmpty()&&m_controller->canPasteCels())m_controller->pasteCels();else m_controller->pasteKeys();}}
        else if(event->matches(QKeySequence::SelectAll)){finishGesture();if(m_controller->selectedCueId().isEmpty()&&m_controller->selectedAudioClipId().isEmpty()&&m_controller->selectedSymbolClipId().isEmpty()){const auto rows=timelineRows(m_controller);if(m_selectedLane.isEmpty()&&!m_controller->cameraSelected()&&m_controller->selectedSymbolPoseLayerId().isEmpty())m_controller->setSelectedCels(celsInRect(rows,0,int(rows.size())-1,0,m_controller->frameCount()-1));else m_controller->setSelectedKeys(keysInRect(rows,0,int(rows.size())-1,0,m_controller->frameCount()-1));}}
        else if((event->modifiers()&(Qt::ControlModifier|Qt::MetaModifier))&&(event->modifiers()&Qt::ShiftModifier)&&(event->key()==Qt::Key_Up||event->key()==Qt::Key_Down)){
            finishGesture();const auto rows=timelineRows(m_controller);for(const auto &row:rows)if(row.artworkRow()&&rowSelected(row)){moveHeaderBy(row.layerId,event->key()==Qt::Key_Up?1:-1);break;}}
        else if((event->modifiers()&Qt::AltModifier)&&(event->key()==Qt::Key_Left||event->key()==Qt::Key_Right)){finishGesture();const int delta=event->key()==Qt::Key_Left?-1:1;if(!m_controller->selectedCueId().isEmpty()){if(const auto *shot=timelineShot(m_controller))if(const auto *cue=timelineCue(*shot,m_controller->selectedCueId()))m_controller->setSelectedCueTiming(cue->startFrame+delta,cue->durationFrames);}else if(!m_controller->selectedSymbolClipId().isEmpty())m_controller->editSelectedSymbolClip((event->modifiers()&Qt::ShiftModifier)?SymbolClipOperation::Slip:SymbolClipOperation::Move,delta);else if(m_controller->selectedAudioClipId().isEmpty()){if(!m_controller->selectedCels().isEmpty())m_controller->moveSelectedCels(delta);else m_controller->moveSelectedKeys(delta);}else if(const auto *shot=timelineShot(m_controller))if(const auto *source=audioFor(*shot,m_controller->selectedAudioClipId())){auto clip=*source;clip.startFrame=std::clamp(clip.startFrame+delta,0,m_controller->frameCount()-1);m_controller->updateAudioClip(clip);}}
        else if((event->key()==Qt::Key_Return||event->key()==Qt::Key_Enter)&&!m_controller->selectedSymbolPoseLayerId().isEmpty()){
            finishGesture();const auto rows=timelineRows(m_controller);for(const auto &row:rows)if(row.poseLane()&&row.layerId==m_controller->selectedSymbolPoseLayerId()){pickPose(row,m_controller->currentFrame());break;}}
        else if(event->key()==Qt::Key_Left)m_controller->stepFrame(-1);
        else if(event->key()==Qt::Key_Right)m_controller->stepFrame(1);
        else if(event->key()==Qt::Key_Home)m_controller->setFrame(0);
        else if(event->key()==Qt::Key_End)m_controller->setFrame(m_controller->frameCount()-1);
        else if(event->key()==Qt::Key_Space)m_controller->setPlaying(!m_controller->playing());
        else if(event->key()==Qt::Key_Delete||event->key()==Qt::Key_Backspace){finishGesture();if(!m_controller->selectedCueId().isEmpty())m_controller->removeSelectedCue();else if(!m_controller->selectedSymbolClipId().isEmpty())clearSymbolClip();else if(!m_controller->selectedAudioClipId().isEmpty())m_controller->removeAudioClip();else if(!m_controller->selectedCels().isEmpty())m_controller->deleteSelectedCels();else if(!m_controller->selectedKeys().isEmpty())m_controller->deleteSelectedKeys();else if(m_selectedLane.isEmpty()&&!m_controller->cameraSelected()&&m_controller->selectedSymbolClipId().isEmpty()&&m_controller->selectedSymbolPoseLayerId().isEmpty())m_controller->removeExposure();}
        else if(event->key()==Qt::Key_Up||event->key()==Qt::Key_Down){finishGesture();const auto rows=timelineRows(m_controller);const QString selected=!m_controller->selectedCueId().isEmpty()?cueRowId(rows):!m_controller->selectedSymbolClipId().isEmpty()?"symbol:"+m_controller->selectedSymbolClipId():!m_controller->selectedSymbolPoseLayerId().isEmpty()?"poses:"+m_controller->selectedSymbolPoseLayerId():m_controller->cameraSelected()?cameraRowId():!m_controller->selectedAudioClipId().isEmpty()?"audio:"+m_controller->selectedAudioClipId():m_selectedLane.isEmpty()?m_controller->selectedTrackId():m_selectedLane;
            for(int row=0;row<rows.size();++row)if(rows[row].id==selected){const int next=std::clamp(row+(event->key()==Qt::Key_Up?-1:1),0,int(rows.size())-1);
                if(rows[next].cueLane&&!rows[next].cues.isEmpty()){m_controller->selectCue(rows[next].cues.first().id);m_selectedLane.clear();}else if(!rows[next].audioId.isEmpty()){m_controller->selectAudioClip(rows[next].audioId);m_selectedLane.clear();emit m_controller->audioPropertiesRequested();}else if(rows[next].symbolLane){m_controller->selectSymbolClip(rows[next].layerId);m_selectedLane.clear();emit m_controller->symbolPropertiesRequested();}else if(rows[next].propertyLane()){selectKeyRow(rows[next]);}else if(rows[next].track?m_controller->selectTrack(rows[next].id):m_controller->selectTimelineLayer(rows[next].layerId))m_selectedLane.clear();viewport()->update();break;}}
        else{QAbstractScrollArea::keyPressEvent(event);return;}event->accept();
    }
    void contextMenuEvent(QContextMenuEvent *event)override{
        finishGesture();const auto rows=timelineRows(m_controller);const int row=rowAt(event->pos().y());
        if(event->pos().y()<rulerHeight||row<0||row>=rows.size())return;const auto entry=rows[row];
        if(event->pos().x()<labelWidth()&&entry.artworkRow()){
            if(!m_controller->selectTimelineLayer(entry.layerId))return;const auto *shot=timelineShot(m_controller);const auto *layer=shot?layerFor(*shot,entry.layerId):nullptr;if(!layer)return;
            const auto layerId=layer->id,parentId=layer->parentId;const bool visible=layer->visible,locked=layer->locked,solo=m_controller->timelineSoloLayers().contains(layerId);
            QMenu menu(this);menu.setObjectName("timelineOrganizationMenu");
            const auto action=[&](const char *id,const char *icon,const QString &name,std::function<void()> fn,bool enabled=true){auto *a=menu.addAction(appIcon(icon),name);a->setObjectName(id);a->setEnabled(enabled);connect(a,&QAction::triggered,&menu,std::move(fn));return a;};
            action("timelineToggleVisibility",visible?"eye-off":"eye",visible?"Hide track":"Show track",[this,layerId,visible]{m_controller->setTimelineLayerVisible(layerId,!visible);},!m_controller->playing());
            action("timelineToggleLock",locked?"lock-keyhole-open":"lock-keyhole",locked?"Unlock track":"Lock track",[this,layerId,locked]{m_controller->setTimelineLayerLocked(layerId,!locked);},!m_controller->playing());
            action("timelineToggleSolo","focus",solo?"Stop soloing track":"Solo track",[this,layerId]{m_controller->toggleTimelineLayerSolo(layerId);});
            action("timelineClearSolo","layers","Show all soloed tracks normally",[this]{m_controller->clearTimelineSolo();},!m_controller->timelineSoloLayers().isEmpty());
            if(entry.folder)action("timelineToggleFolder",m_controller->timelineFolderCollapsed(layerId)?"chevron-right":"chevron-down",m_controller->timelineFolderCollapsed(layerId)?"Expand folder":"Collapse folder",[this,layerId]{m_controller->setTimelineFolderCollapsed(layerId,!m_controller->timelineFolderCollapsed(layerId));});
            menu.addSeparator();
            action("timelineMoveTrackUp","arrow-up","Move track up",[this,layerId]{moveHeaderBy(layerId,1);},canMoveHeaderBy(layerId,1));
            action("timelineMoveTrackDown","arrow-down","Move track down",[this,layerId]{moveHeaderBy(layerId,-1);},canMoveHeaderBy(layerId,-1));
            if(!parentId.isEmpty())action("timelineMoveTrackToRoot","arrow-left-right","Move to top level",[this,layerId]{m_controller->moveTimelineLayer(layerId,{},{});},m_controller->timelineLayerMoveError(layerId,{},{}).isEmpty());
            const auto folderParent=entry.folder&&!entry.track?layerId:parentId;
            action("timelineNewFolderHere","folder-plus",entry.folder&&!entry.track?"New folder inside…":"New folder beside track…",[this,folderParent]{if(addFolderRequested)QTimer::singleShot(0,this,[this,folderParent]{addFolderRequested(folderParent);});},!m_controller->playing());
            menu.addSeparator();
            if(entry.symbolLane){
                const bool editable=!entry.locked&&!m_controller->playing();
                action("timelinePickSymbolFrame","image","Pick starting frame…",[this,layerId]{QTimer::singleShot(0,this,[this,layerId]{openSymbolFramePicker(m_controller,layerId,this);});},editable);
                action("timelineSymbolProperties","sliders-horizontal","Clip properties",[this]{emit m_controller->symbolPropertiesRequested();});
                action("timelineResetSymbolSlip","rotate-ccw","Reset playback slip",[this,layerId]{if(m_controller->selectSymbolClip(layerId))if(const auto *current=timelineShot(m_controller))if(const auto *source=layerFor(*current,layerId);source&&source->symbol)m_controller->editSelectedSymbolClip(SymbolClipOperation::Slip,-source->symbol->playbackOffset);},editable);
                action("timelineClearSymbolClip","trash","Clear clip exposure",[this,layerId]{if(m_controller->selectSymbolClip(layerId))clearSymbolClip();},editable);
            }else{
                QSet<QString> family;bool frozen=entry.locked,convertible=!entry.track&&parentId.isEmpty()&&!layer->clipped;
                for(int i=0;i<shot->layers.size();++i)if(shot->layers[i].id==layerId||layerIsDescendant(shot->layers,i,layerId)){
                    const auto &member=shot->layers[i];family.insert(member.id);frozen|=member.locked;
                    convertible&=(member.kind==LayerKind::Raster||member.kind==LayerKind::Vector)&&!member.annotation&&!member.adjustment&&!member.locked&&!member.positionLocked;
                }
                bool containsTrack=false,otherArtwork=false;
                for(const auto &track:shot->animationTracks)if(family.contains(track.rootGroupLayerId)&&(!entry.track||track.id!=entry.track->id))containsTrack=true;
                for(const auto &member:shot->layers)if(!family.contains(member.id)&&!member.group&&!member.adjustment)otherArtwork=true;
                if(!entry.track)action("timelineConvertTrack","clapperboard","Convert selected layer to frame animation",[this,layerId]{if(m_controller->selectTimelineLayer(layerId))m_controller->convertSelectedLayer();},convertible&&!containsTrack&&!frozen&&!m_controller->playing());
                auto *remove=action("timelineDeleteTrack","trash",entry.folder&&!entry.track?"Delete folder":"Delete track",[this,layerId]{if(m_controller->selectTimelineLayer(layerId))m_controller->removeTrack();},!frozen&&!containsTrack&&!m_controller->playing()&&(entry.track||otherArtwork));
                if(containsTrack)remove->setToolTip("Delete or move the contained animation tracks before deleting this folder.");
            }
            menu.exec(event->globalPos());return;
        }
        if(entry.cueLane){
            const auto cue=cueAtPoint(entry,event->pos().x());
            if(cue){if(!m_controller->selectCue(cue->id))return;m_selectedLane.clear();}
            QMenu menu(this);menu.setObjectName("timelineCueMenu");
            if(cue){auto *edit=menu.addAction(appIcon("sliders-horizontal"),"Cue properties");edit->setObjectName("timelineEditCue");connect(edit,&QAction::triggered,&menu,[this]{emit m_controller->cuePropertiesRequested();});
                auto *remove=menu.addAction(appIcon("trash"),"Delete cue");remove->setObjectName("timelineDeleteCue");remove->setEnabled(!m_controller->playing());connect(remove,&QAction::triggered,&menu,[this]{m_controller->removeSelectedCue();});menu.addSeparator();}
            auto *previous=menu.addAction(appIcon("chevron-left"),"Previous cue");connect(previous,&QAction::triggered,&menu,[this]{m_controller->stepCue(-1);});
            auto *next=menu.addAction(appIcon("chevron-right"),"Next cue");connect(next,&QAction::triggered,&menu,[this]{m_controller->stepCue(1);});
            auto *add=menu.addMenu(appIcon("plus"),"Add cue at playhead");add->setEnabled(!m_controller->playing());populateCueMenu(add,m_controller);
            menu.exec(event->globalPos());return;
        }
        if(!entry.audioId.isEmpty()){
            if(!m_controller->selectAudioClip(entry.audioId))return;m_selectedLane.clear();emit m_controller->audioPropertiesRequested();
            QMenu menu(this);menu.setObjectName("timelineAudioMenu");const auto *clip=audioFor(*timelineShot(m_controller),entry.audioId);if(!clip)return;const bool muted=clip->muted;const QString name=clip->name;
            auto *mute=menu.addAction(appIcon(muted?"volume-2":"volume-x"),muted?"Unmute audio":"Mute audio");connect(mute,&QAction::triggered,&menu,[this,id=entry.audioId]{if(const auto *shot=timelineShot(m_controller))if(const auto *source=audioFor(*shot,id)){auto clip=*source;clip.muted=!clip.muted;m_controller->updateAudioClip(clip);}});
            auto *rename=menu.addAction(appIcon("pencil"),"Rename audio");connect(rename,&QAction::triggered,&menu,[this,id=entry.audioId,name]{bool ok=false;const QString next=QInputDialog::getText(this,"Rename audio","Name",QLineEdit::Normal,name,&ok);if(ok)if(const auto *shot=timelineShot(m_controller))if(const auto *source=audioFor(*shot,id)){auto clip=*source;clip.name=next.trimmed();m_controller->updateAudioClip(clip);}});
            auto *remove=menu.addAction(appIcon("trash"),"Remove audio");connect(remove,&QAction::triggered,&menu,[this]{m_controller->removeAudioClip();});menu.exec(event->globalPos());return;
        }
        if(entry.symbolLane){
            if(!m_controller->selectSymbolClip(entry.layerId))return;m_selectedLane.clear();emit m_controller->symbolPropertiesRequested();
            QMenu menu(this);menu.setObjectName("timelineSymbolMenu");const bool editable=!entry.locked&&!m_controller->playing();
            auto *pick=menu.addAction(appIcon("image"),"Pick starting frame…");pick->setObjectName("timelinePickSymbolFrame");pick->setEnabled(editable);
            connect(pick,&QAction::triggered,&menu,[this,id=entry.layerId]{QTimer::singleShot(0,this,[this,id]{openSymbolFramePicker(m_controller,id,this);});});
            auto *properties=menu.addAction(appIcon("sliders-horizontal"),"Clip properties");connect(properties,&QAction::triggered,&menu,[this]{emit m_controller->symbolPropertiesRequested();});
            auto *reset=menu.addAction(appIcon("rotate-ccw"),"Reset playback slip");reset->setObjectName("timelineResetSymbolSlip");reset->setEnabled(editable);
            connect(reset,&QAction::triggered,&menu,[this,id=entry.layerId]{if(const auto *shot=timelineShot(m_controller))if(const auto *layer=layerFor(*shot,id);layer&&layer->symbol)m_controller->editSelectedSymbolClip(SymbolClipOperation::Slip,-layer->symbol->playbackOffset);});
            auto *clear=menu.addAction(appIcon("trash"),"Clear clip exposure");clear->setObjectName("timelineClearSymbolClip");clear->setEnabled(editable);connect(clear,&QAction::triggered,&menu,[this]{clearSymbolClip();});
            menu.exec(event->globalPos());return;
        }
        if(entry.propertyLane()){
            if(!selectKeyRow(entry))return;
            const int frame=event->pos().x()>=labelWidth()?keyFrameAt(timelineShot(m_controller),entry,event->pos().x()):m_controller->currentFrame();
            if(!seekKeyFrame(keyFor(entry,frame),frame))return;const auto *shot=timelineShot(m_controller);const bool exists=shot&&laneFrames(*shot,entry).contains(frame);
            if(exists&&!m_controller->selectedKeys().contains(keyFor(entry,frame)))m_controller->setSelectedKeys({keyFor(entry,frame)});
            if(entry.poseLane()){
                QMenu menu(this);menu.setObjectName("timelinePoseMenu");const bool editable=!entry.locked&&!m_controller->playing();
                auto *pick=menu.addAction(appIcon("toon-symbol-pose"),exists?"Change pose…":"Add pose key…");pick->setObjectName("timelinePickSymbolPose");pick->setEnabled(editable);
                connect(pick,&QAction::triggered,&menu,[this,entry,frame]{QTimer::singleShot(0,this,[this,entry,frame]{pickPose(entry,frame);});});
                auto *copy=menu.addAction(appIcon("copy"),"Copy pose keys");copy->setShortcut(QKeySequence::Copy);copy->setEnabled(!m_controller->selectedKeys().isEmpty());connect(copy,&QAction::triggered,&menu,[this]{m_controller->copySelectedKeys();});
                auto *paste=menu.addAction(appIcon("clipboard-paste"),"Paste pose keys at playhead");paste->setShortcut(QKeySequence::Paste);paste->setEnabled(editable&&m_controller->canPasteKeys());connect(paste,&QAction::triggered,&menu,[this]{m_controller->pasteKeys();});
                auto *remove=menu.addAction(appIcon("trash"),"Delete pose keys");remove->setShortcut(QKeySequence::Delete);remove->setEnabled(editable&&!m_controller->selectedKeys().isEmpty());connect(remove,&QAction::triggered,&menu,[this]{m_controller->deleteSelectedKeys();});
                menu.exec(event->globalPos());return;
            }
            QMenu menu(this);menu.setObjectName("timelineKeyMenu");auto *add=menu.addAction(appIcon("plus"),"Add keyframe");add->setEnabled(!exists&&!entry.locked&&!m_controller->playing()&&shot&&(entry.cameraLane()||layerPresentAtFrame(*shot,entry.layerId,frame)));
            connect(add,&QAction::triggered,&menu,[this,entry]{if(entry.cameraLane())m_controller->setCameraKey();else if(entry.property)m_controller->setPropertyKey(*entry.property);else m_controller->setPathKey(entry.objectId);});
            auto *copy=menu.addAction(appIcon("copy"),"Copy selected keys");copy->setShortcut(QKeySequence::Copy);copy->setEnabled(!m_controller->selectedKeys().isEmpty());connect(copy,&QAction::triggered,&menu,[this]{m_controller->copySelectedKeys();});
            auto *paste=menu.addAction(appIcon("clipboard-paste"),"Paste keys at playhead");paste->setShortcut(QKeySequence::Paste);paste->setEnabled(m_controller->canPasteKeys()&&!entry.locked&&!m_controller->playing());connect(paste,&QAction::triggered,&menu,[this]{m_controller->pasteKeys();});
            auto *remove=menu.addAction(appIcon("trash"),"Delete selected keys");remove->setShortcut(QKeySequence::Delete);remove->setEnabled(!m_controller->selectedKeys().isEmpty()&&!entry.locked&&!m_controller->playing());connect(remove,&QAction::triggered,&menu,[this]{m_controller->deleteSelectedKeys();});
            menu.exec(event->globalPos());return;
        }
        m_selectedLane.clear();m_controller->clearKeySelection();if(!(entry.track?m_controller->selectTrack(entry.id):m_controller->selectTimelineLayer(entry.layerId)))return;
        if(event->pos().x()>=labelWidth()&&!m_controller->setFrame(frameAt(event->pos().x())))return;
        const auto *track=selectedTrack(m_controller);const auto *root=selectedRoot(m_controller);const bool unlocked=root&&!timelineLayerLocked(*timelineShot(m_controller),root->id);
        if(track&&event->pos().x()>=labelWidth()){
            if(const auto *cel=exposureAtFrame(*track,m_controller->currentFrame())){const TimelineCelRef ref{track->id,cel->id};if(!m_controller->selectedCels().contains(ref))m_controller->setSelectedCels({ref});}
            else m_controller->clearCelSelection();
        }
        QMenu menu(this);menu.setObjectName("timelineTrackMenu");
        if(!track){auto *convert=menu.addAction(appIcon("clapperboard"),"Convert selected layer to frame animation");convert->setObjectName("timelineConvertTrack");convert->setEnabled(unlocked);connect(convert,&QAction::triggered,&menu,[this]{m_controller->convertSelectedLayer();});}
        else{auto *blank=menu.addAction(appIcon("file-plus"),"New blank drawing");blank->setEnabled(unlocked);connect(blank,&QAction::triggered,&menu,[this]{m_controller->newDrawing();});
            auto *duplicate=menu.addAction(appIcon("copy"),"Duplicate drawing");duplicate->setEnabled(unlocked&&!track->exposures.isEmpty());connect(duplicate,&QAction::triggered,&menu,[this]{m_controller->newDrawing(true);});
            auto *reuse=menu.addAction(appIcon("folder-open"),"Reuse existing drawing…");reuse->setObjectName("timelineReuseDrawing");reuse->setEnabled(unlocked&&!track->drawings.isEmpty());connect(reuse,&QAction::triggered,&menu,[this]{openDrawingReuse(m_controller,this);});
            menu.addSeparator();const bool picked=!m_controller->selectedCels().isEmpty();bool editable=picked;
            for(const auto &ref:m_controller->selectedCels())if(!celEditError(*timelineShot(m_controller),ref).isEmpty())editable=false;
            auto *copy=menu.addAction(appIcon("copy"),"Copy selected cels");copy->setObjectName("timelineCopyCels");copy->setShortcut(QKeySequence::Copy);copy->setEnabled(picked);connect(copy,&QAction::triggered,&menu,[this]{m_controller->copySelectedCels();});
            auto *paste=menu.addAction(appIcon("clipboard-paste"),"Paste linked cels at playhead");paste->setObjectName("timelinePasteCels");paste->setShortcut(QKeySequence::Paste);paste->setEnabled(m_controller->canPasteCels());connect(paste,&QAction::triggered,&menu,[this]{m_controller->pasteCels();});
            auto *holds=menu.addMenu("Set hold");holds->setEnabled(editable);
            for(int count:{1,2,3}){auto *hold=holds->addAction(QString("On %1 · %2 frame%3").arg(count).arg(count).arg(count==1?"":"s"));hold->setObjectName(QString("timelineHold%1").arg(count));connect(hold,&QAction::triggered,&menu,[this,count]{m_controller->setSelectedCelHolds(count);});}
            auto *repeat=menu.addAction(appIcon("repeat"),"Repeat selected cels");repeat->setObjectName("timelineRepeatCels");repeat->setEnabled(editable);connect(repeat,&QAction::triggered,&menu,[this]{m_controller->repeatSelectedCels();});
            auto *reverse=menu.addAction(appIcon("arrow-left-right"),"Reverse selected cels");reverse->setObjectName("timelineReverseCels");reverse->setEnabled(editable);connect(reverse,&QAction::triggered,&menu,[this]{m_controller->reverseSelectedCels();});
            auto *remove=menu.addAction(appIcon("trash"),"Remove selected exposures");remove->setObjectName("timelineDeleteCels");remove->setEnabled(editable);remove->setToolTip("Keep drawings in the reuse library");connect(remove,&QAction::triggered,&menu,[this]{m_controller->deleteSelectedCels();});}
        menu.addSeparator();auto *removeTrack=menu.addAction(appIcon("trash"),"Delete track");removeTrack->setObjectName("timelineDeleteTrack");removeTrack->setEnabled(unlocked);connect(removeTrack,&QAction::triggered,&menu,[this]{m_controller->removeTrack();});menu.exec(event->globalPos());
    }
    bool viewportEvent(QEvent *event)override{
        if(event->type()==QEvent::TabletPress||event->type()==QEvent::TabletMove||event->type()==QEvent::TabletRelease){
            const auto *tablet=static_cast<QTabletEvent *>(event);const auto type=event->type()==QEvent::TabletPress?QEvent::MouseButtonPress:event->type()==QEvent::TabletRelease?QEvent::MouseButtonRelease:QEvent::MouseMove;
            QMouseEvent mouse(type,tablet->position(),tablet->globalPosition(),tablet->button(),tablet->buttons(),tablet->modifiers(),tablet->pointingDevice());
            if(type==QEvent::MouseButtonPress)mousePressEvent(&mouse);else if(type==QEvent::MouseButtonRelease)mouseReleaseEvent(&mouse);else mouseMoveEvent(&mouse);event->accept();return true;}
        if(event->type()==QEvent::ToolTip){if(m_scrubbing||m_gesture!=Gesture::None)return true;auto *help=static_cast<QHelpEvent *>(event);const auto point=help->pos();const auto rows=timelineRows(m_controller);const int row=rowAt(point.y());QString tip;
            if(point.y()>=rulerHeight&&row>=0&&row<rows.size()){const auto &entry=rows[row];tip=entry.name+(!entry.audioId.isEmpty()?" · Audio · Drag to move; drag either edge to trim":entry.symbolLane?" · Symbol clip · Drag to move; drag an edge to trim; Alt-drag to slip; double-click to pick a frame. Dashed sections are outside this drawing’s exposures.":entry.poseLane()?" · Held Symbol poses · Double-click or Enter to pick a pose":entry.propertyLane()?" · Keyframes":entry.track?" · Frame by frame":" · Static");
                if(entry.artworkRow()&&point.x()<labelWidth())tip=layerPath(*timelineShot(m_controller),entry.layerId)+" · Drag the name to reorder; drop in a folder to group";
                if(entry.cueLane){tip=entry.name+" · Shot notes";if(const auto cue=cueAtPoint(entry,point.x())){tip=timelineCueKindName(cue->kind)+" · "+cue->name+QString(" · frames %1–%2").arg(cue->startFrame+1).arg(cue->startFrame+cue->durationFrames);if(!cue->speaker.isEmpty())tip+="\n"+cue->speaker;if(!cue->text.isEmpty())tip+="\n"+cue->text;tip+="\nDrag to move; drag range edges to trim; double-click to edit.";}}
                if(entry.poseLane()&&point.x()>=labelWidth())if(const auto *shot=timelineShot(m_controller))if(const auto *layer=layerFor(*shot,entry.layerId);layer&&layer->symbol)
                    tip+=QString(" · pose frame %1").arg(evaluatedSymbolPose(*layer->symbol,frameAt(point.x()))+1);
                if(entry.propertyLane()&&point.x()>=labelWidth()){const auto *shot=timelineShot(m_controller);const int frame=keyFrameAt(shot,entry,point.x());tip+=QString(" · frame %1").arg(frame+1);if(shot&&laneFrames(*shot,entry).contains(frame))tip+=" · Drag selected keys; Shift selects a range; Ctrl/Cmd toggles";else tip+=" · Drag to select keys";}
                if(point.x()>=labelWidth()&&entry.track){const auto *exposure=exposureAtFrame(*entry.track,frameAt(point.x()));tip=exposure?QString("%1 · %2 · frames %3–%4 · Drag selected cels; Shift selects a range; Ctrl/Cmd toggles; drag an edge of one cel to resize").arg(drawingDisplayId(*entry.track,exposure->drawingId),entry.name).arg(exposure->startFrame+1).arg(exposure->startFrame+exposure->durationFrames):QString("%1 · blank frame %2").arg(entry.name).arg(frameAt(point.x())+1);}}
            else if(point.x()>=labelWidth())tip=QString("Frame %1 · Shift-drag to set playback range; drag range edges to adjust").arg(frameAt(point.x())+1);
            if(!tip.isEmpty())QToolTip::showText(help->globalPos(),tip,viewport());return true;}
        return QAbstractScrollArea::viewportEvent(event);
    }
private:
    std::optional<QPair<QString,QString>> headerMoveTarget(const QString &id,int direction)const{
        const auto *shot=timelineShot(m_controller);const auto *layer=shot?layerFor(*shot,id):nullptr;if(!layer)return {};
        QStringList siblings;for(const auto &candidate:shot->layers)if(candidate.parentId==layer->parentId)siblings.append(candidate.id);
        const int index=siblings.indexOf(id),target=index+(direction>0?1:-1);if(index<0||target<0||target>=siblings.size())return {};
        const QString before=direction>0?(target+1<siblings.size()?siblings[target+1]:QString()):siblings[target];
        return QPair<QString,QString>{layer->parentId,before};
    }
    bool canMoveHeaderBy(const QString &id,int direction)const{const auto target=headerMoveTarget(id,direction);return target&&!m_controller->playing()&&m_controller->timelineLayerMoveError(id,target->first,target->second).isEmpty();}
    void moveHeaderBy(const QString &id,int direction){const auto target=headerMoveTarget(id,direction);if(target)m_controller->moveTimelineLayer(id,target->first,target->second);}
    void refreshHeaderControls(){
        if(!m_controller)return;const auto *shot=timelineShot(m_controller);const auto rows=timelineRows(m_controller);QSet<QString> needed;
        const int first=std::max(0,verticalScrollBar()->value()/rowHeight),last=std::min(int(rows.size())-1,(verticalScrollBar()->value()+viewport()->height()-rulerHeight)/rowHeight);
        const auto ensure=[&](const TimelineRow &row,const QString &action,const char *icon,const QString &help,const QRect &bounds,bool checked,bool enabled){
            const QString key=action+":"+row.layerId;needed.insert(key);auto *button=m_headerButtons.value(key,nullptr);
            if(!button){button=new QToolButton(viewport());button->setObjectName("timelineRow_"+action+"_"+row.layerId);button->setAutoRaise(true);
                button->setProperty("timelineRowAction",action);button->setProperty("timelineLayerId",row.layerId);button->setFocusPolicy(Qt::TabFocus);
                button->setIconSize({ui::metrics::actionIcon,ui::metrics::actionIcon});button->setFixedSize(ui::metrics::actionTarget,ui::metrics::actionTarget);
                button->setCheckable(action=="lock"||action=="solo");m_headerButtons.insert(key,button);
                connect(button,&QToolButton::clicked,this,[this,id=row.layerId,action]{
                    const auto *current=timelineShot(m_controller);const auto *layer=current?layerFor(*current,id):nullptr;if(!layer)return;
                    if(action=="visible")m_controller->setTimelineLayerVisible(id,!layer->visible);
                    else if(action=="lock")m_controller->setTimelineLayerLocked(id,!layer->locked);
                    else if(action=="solo")m_controller->toggleTimelineLayerSolo(id);
                    else if(m_gesture==Gesture::None&&!m_scrubbing&&!m_controller->document()->layerPropertyEditActive())m_controller->setTimelineFolderCollapsed(id,!m_controller->timelineFolderCollapsed(id));
                    refreshHeaderControls();viewport()->update();
                });
            }
            button->setIcon(appIcon(icon));button->setToolTip(help);button->setAccessibleName(help);
            {const QSignalBlocker block(button);button->setChecked(checked);}button->setEnabled(enabled);button->setGeometry(bounds);button->show();
        };
        for(int i=first;shot&&i<=last;++i){const auto &row=rows[i];if(!row.artworkRow())continue;const auto *layer=layerFor(*shot,row.layerId);if(!layer)continue;
            const int y=rulerHeight+i*rowHeight-verticalScrollBar()->value()+(rowHeight-ui::metrics::actionTarget)/2;
            // A partially scrolled row must never cover the ruler.
            if(y<rulerHeight||y+ui::metrics::actionTarget>viewport()->height())continue;
            const int right=labelWidth()-ui::metrics::actionTarget*3-2;const bool editable=!m_controller->playing();
            const auto name=layerPath(*shot,layer->id);
            QString visibility=QString(layer->visible?"Hide %1":"Show %1").arg(name);if(layer->visible&&!row.visible)visibility+=" · Hidden by its folder";
            ensure(row,"visible",row.visible?"eye":"eye-off",visibility,{right,y,28,28},false,editable);
            QString lock=QString(layer->locked?"Unlock %1":"Lock %1").arg(name);if(!layer->locked&&row.locked)lock+=" · A parent folder is locked";
            ensure(row,"lock",row.locked?"lock-keyhole":"lock-keyhole-open",lock,{right+28,y,28,28},layer->locked,editable);
            const bool solo=m_controller->timelineSoloLayers().contains(layer->id);
            ensure(row,"solo","focus",QString(solo?"Stop soloing %1":"Solo %1").arg(name)+" · Preview only; visibility and exports stay unchanged",{right+56,y,28,28},solo,true);
            if(row.folder){const bool collapsed=m_controller->timelineFolderCollapsed(layer->id);
                ensure(row,"collapse",collapsed?"chevron-right":"chevron-down",QString(collapsed?"Expand %1":"Collapse %1").arg(name),{headerIndent(row),y,28,28},false,m_gesture==Gesture::None&&!m_scrubbing&&!m_controller->document()->layerPropertyEditActive());}
        }
        for(auto it=m_headerButtons.begin();it!=m_headerButtons.end();)if(!needed.contains(it.key())){it.value()->hide();it.value()->deleteLater();it=m_headerButtons.erase(it);}else ++it;
    }
    void updateTrackDrop(const QPoint &point){
        m_dropValid=false;m_dropInto=false;m_dropRow=-1;m_dropParent.clear();m_dropBefore.clear();m_dropError.clear();
        const auto *shot=timelineShot(m_controller);const auto rows=timelineRows(m_controller);if(!shot||rows.isEmpty())return;
        const int index=std::clamp(rowAt(point.y()),0,int(rows.size())-1);const auto &target=rows[index];
        if(point.x()>=labelWidth()||point.y()<rulerHeight||!target.artworkRow()){
            m_dropRow=index;m_dropError="Drop on an artwork track or folder name.";
        }else if(const auto *layer=layerFor(*shot,target.layerId)){
            const int offset=point.y()-(rulerHeight+index*rowHeight-verticalScrollBar()->value());
            m_dropInto=target.folder&&!target.track&&offset>=rowHeight/4&&offset<rowHeight*3/4;
            m_dropRow=index;
            if(m_dropInto)m_dropParent=target.layerId;
            else{
                m_dropParent=layer->parentId;
                if(offset>=rowHeight/2){m_dropBefore=layer->id;m_dropRow=index+1;while(m_dropRow<rows.size()&&rows[m_dropRow].depth>target.depth)++m_dropRow;}
                else{
                    // Document sibling order is bottom-to-top; a visual drop
                    // above inserts before the next higher stored sibling.
                    bool found=false;
                    for(int i=0;i<shot->layers.size();++i){const auto &sibling=shot->layers[i];
                        if(sibling.id==layer->id){found=true;continue;}
                        if(found&&sibling.parentId==layer->parentId&&sibling.id!=m_dragLayer&&!layerIsDescendant(shot->layers,i,m_dragLayer)){m_dropBefore=sibling.id;break;}}
                }
            }
            if(layer->id==m_dragLayer)m_dropError="Choose another destination for this track.";
            else m_dropError=m_controller->timelineLayerMoveError(m_dragLayer,m_dropParent,m_dropBefore);
            m_dropValid=m_dropError.isEmpty();
        }
        setProperty("timelineDropValid",m_dropValid);setProperty("timelineDropError",m_dropError);
        viewport()->setCursor(m_dropValid?Qt::ClosedHandCursor:Qt::ForbiddenCursor);
    }
    enum class Gesture {None,TrackMove,CueMove,CueStart,CueEnd,Keys,Cels,CelMarquee,SymbolMove,SymbolStart,SymbolEnd,SymbolSlip,ExposureMove,ExposureStart,ExposureEnd,AudioMove,AudioStart,AudioEnd,RangeNew,RangeStart,RangeEnd,Marquee};
    bool poseRowHasSelectedKeys(const TimelineRow &row)const{
        for(const auto &key:m_controller->selectedKeys())if(key.isSymbolPose()&&key.layerId==row.layerId)return true;return false;
    }
    void paintPoseLane(QPainter &painter,const Shot &shot,const TimelineRow &row,int y){
        const auto *layer=layerFor(shot,row.layerId);if(!layer||!layer->symbol)return;
        auto instance=*layer->symbol;QSet<int> selectedFrames,dragFrames;
        for(const auto &key:m_controller->selectedKeys())if(sameLane(row,key))selectedFrames.insert(key.frame);
        if(m_gesture==Gesture::Keys)for(const auto &key:m_dragKeys)if(sameLane(row,key))dragFrames.insert(key.frame);
        if(!dragFrames.isEmpty()){for(auto &key:instance.poseKeys)if(dragFrames.contains(symbolPoseParentFrame(instance,key.frame)))key.frame+=m_dragDelta;
            std::stable_sort(instance.poseKeys.begin(),instance.poseKeys.end(),[](const auto &a,const auto &b){return a.frame<b.frame;});}
        const int start=std::max(0,instance.startFrame),end=std::min(shot.durationFrames,symbolClipEnd(instance,shot.durationFrames));
        QVector<QPair<int,int>> exposed;
        const auto owner=animationOwner(shot,row.layerId);
        if(owner.drawingId.isEmpty())exposed.append({start,end});
        else if(const auto *track=celTrack(shot,owner.trackId))for(const auto &cel:track->exposures)if(cel.drawingId==owner.drawingId)
            exposed.append({std::max(start,cel.startFrame),std::min(end,cel.startFrame+cel.durationFrames)});
        QVector<QPair<int,int>> poses;
        if(end>start)poses.append({start,evaluatedSymbolPose(instance,start)});
        for(const auto &key:instance.poseKeys){const int frame=symbolPoseParentFrame(instance,key.frame);if(frame>start&&frame<end)poses.append({frame,key.poseFrame});}
        for(int i=0;i<poses.size();++i){const int first=poses[i].first,last=i+1<poses.size()?poses[i+1].first:end;
            if(last<=first)continue;const int x=frameX(first),right=frameX(last);if(right<labelWidth()||x>viewport()->width())continue;
            QRect span(x+1,y+6,std::max(1,right-x-2),rowHeight-12);painter.setBrush(Qt::NoBrush);
            painter.setPen(QPen(QColor(ui::muted),1,Qt::DashLine));painter.drawRect(span.adjusted(0,0,-1,-1));
            for(const auto &visible:exposed){const int a=std::max(first,visible.first),b=std::min(last,visible.second);if(b<=a)continue;
                QRect part(frameX(a)+1,y+6,std::max(1,frameX(b)-frameX(a)-2),rowHeight-12);
                painter.fillRect(part,QColor(ui::raised));painter.setPen(QColor(ui::border));painter.drawRect(part.adjusted(0,0,-1,-1));}
            const QRect text=span.adjusted(qRound(cellWidth/2)+11,0,-3,0);
            if(text.width()>fontMetrics().horizontalAdvance("…")){painter.setPen(QColor(ui::text));painter.drawText(text,Qt::AlignVCenter|Qt::AlignLeft,fontMetrics().elidedText(QString("F%1").arg(poses[i].second+1),Qt::ElideRight,text.width()));}
        }
        painter.setRenderHint(QPainter::Antialiasing,true);
        for(const auto &source:layer->symbol->poseKeys){const int frame=symbolPoseParentFrame(*layer->symbol,source.frame);
            if(frame<start||frame>=end)continue;const bool dragging=dragFrames.contains(frame),picked=selectedFrames.contains(frame);const int actual=frame+(dragging?m_dragDelta:0);
            const int x=frameX(actual)+qRound(cellWidth/2);if(x+7<labelWidth()||x-7>viewport()->width())continue;
            const bool visible=actual>=start&&actual<end&&std::any_of(exposed.cbegin(),exposed.cend(),[actual](const auto &range){return actual>=range.first&&actual<range.second;});
            painter.setPen(QPen(QColor(picked?ui::focus:visible?ui::text:ui::muted),picked?2:1));painter.setBrush(QColor(picked?ui::accent:visible?ui::muted:ui::base));
            const qreal cy=y+rowHeight/2.;painter.drawPolygon(QPolygonF{{qreal(x),cy-6},{qreal(x+6),cy},{qreal(x),cy+6},{qreal(x-6),cy}});
        }
        painter.setRenderHint(QPainter::Antialiasing,false);
    }
    void pickPose(const TimelineRow &row,int frame){
        if(!row.poseLane()||!m_controller->selectSymbolPoseLayer(row.layerId)||!m_controller->setFrame(frame))return;
        m_selectedLane=row.id;
        openSymbolFramePicker(m_controller,row.layerId,this,SymbolFramePickerMode::TimedPose,frame);
    }
    bool cueGesture()const{return m_gesture==Gesture::CueMove||m_gesture==Gesture::CueStart||m_gesture==Gesture::CueEnd;}
    QString cueRowId(const QVector<TimelineRow> &rows)const{for(const auto &row:rows)if(row.cueLane)for(const auto &cue:row.cues)if(cue.id==m_controller->selectedCueId())return row.id;return {};}
    std::optional<TimelineCue> cueAtPoint(const TimelineRow &row,int x)const{
        if(x<labelWidth())return std::nullopt;
        const TimelineCue *nearest=nullptr;qreal distance=1e20;
        for(const auto &cue:row.cues){const int left=frameX(cue.startFrame),right=frameX(cue.startFrame+cue.durationFrames);const qreal center=left+cellWidth/2;
            const bool hit=cue.durationFrames==1?std::abs(x-center)<=std::max(qreal(8),cellWidth/2):x>=left-1&&x<=right+1;
            if(hit){const qreal delta=std::abs(x-(cue.durationFrames==1?center:(left+right)/2.));if(delta<distance){distance=delta;nearest=&cue;}}}
        return nearest?std::optional<TimelineCue>(*nearest):std::nullopt;
    }
    void clearSymbolClip(){if(const auto *shot=timelineShot(m_controller))if(const auto *layer=layerFor(*shot,m_controller->selectedSymbolClipId());layer&&layer->symbol)m_controller->setSelectedSymbolClipTiming(layer->symbol->startFrame,0,layer->symbol->playbackOffset);}
    bool symbolGesture()const{return m_gesture==Gesture::SymbolMove||m_gesture==Gesture::SymbolStart||m_gesture==Gesture::SymbolEnd||m_gesture==Gesture::SymbolSlip;}
    static SymbolClipOperation symbolOperation(Gesture gesture){return gesture==Gesture::SymbolStart?SymbolClipOperation::TrimStart:gesture==Gesture::SymbolEnd?SymbolClipOperation::TrimEnd:gesture==Gesture::SymbolSlip?SymbolClipOperation::Slip:SymbolClipOperation::Move;}
    bool audioGesture()const{return m_gesture==Gesture::AudioMove||m_gesture==Gesture::AudioStart||m_gesture==Gesture::AudioEnd;}
    QString cameraRowId()const{return m_controller->selectedCameraLayerId().isEmpty()?QString("camera:2d"):"camera:3d:"+m_controller->selectedCameraLayerId();}
    bool rowSelected(const TimelineRow &row)const{
        if(!m_controller->selectedCueId().isEmpty()){for(const auto &cue:row.cues)if(cue.id==m_controller->selectedCueId())return true;return false;}
        if(!m_controller->selectedSymbolClipId().isEmpty())return row.symbolLane&&row.layerId==m_controller->selectedSymbolClipId();
        if(!m_controller->selectedSymbolPoseLayerId().isEmpty())return row.poseLane()&&(row.layerId==m_controller->selectedSymbolPoseLayerId()||poseRowHasSelectedKeys(row));
        if(m_controller->cameraSelected())return row.cameraLane()&&row.id==cameraRowId();
        return !row.audioId.isEmpty()?row.audioId==m_controller->selectedAudioClipId():!row.cameraLane()&&m_controller->selectedAudioClipId().isEmpty()&&(row.id==m_controller->selectedTrackId()||row.id==m_selectedLane);
    }
    bool selectKeyRow(const TimelineRow &row){
        if(row.poseLane()){
            if(!m_controller->selectSymbolPoseLayer(row.layerId))return false;
            m_selectedLane=row.id;emit m_controller->symbolPropertiesRequested();
        }else if(row.cameraLane()){
            if(!m_controller->selectCamera(row.layerId))return false;
            m_selectedLane=row.id;emit m_controller->cameraPropertiesRequested();
        }else{
            if(!m_controller->selectPropertyLayer(row.layerId))return false;
            m_selectedLane=row.id;emit m_controller->animationPropertiesRequested();
            if(row.property)emit m_controller->propertyChannelRequested(*row.property);else emit m_controller->pathChannelRequested(row.objectId);
        }
        return true;
    }
    bool seekKeyFrame(const TimelineKeyRef &key,int frame){const auto *shot=timelineShot(m_controller);return shot&&((key.isCamera()||key.isSymbolPose())?m_controller->setFrame(frame):!layerPresentAtFrame(*shot,key.layerId,frame)||m_controller->setFrame(frame));}
    static TimelineKeyRef keyFor(const TimelineRow &row,int frame){return {row.layerId,row.property.value_or(KeyframeProperty::PositionX),row.objectId,frame,row.kind};}
    static bool sameLane(const TimelineRow &row,const TimelineKeyRef &key){return row.propertyLane()&&row.kind==key.kind&&row.layerId==key.layerId&&row.objectId==key.objectId&&(key.isCamera()||key.isSymbolPose()||key.isPath()||row.property==key.property);}
    QVector<TimelineKeyRef> keysInRect(const QVector<TimelineRow> &rows,int firstRow,int lastRow,int firstFrame,int lastFrame)const{
        QVector<TimelineKeyRef> result;const auto *shot=timelineShot(m_controller);if(!shot)return result;
        for(int row=std::max(0,firstRow);row<=lastRow&&row<rows.size();++row)if(rows[row].propertyLane()&&(m_controller->selectedSymbolPoseLayerId().isEmpty()?!rows[row].poseLane():rows[row].poseLane()&&rows[row].layerId==m_controller->selectedSymbolPoseLayerId()))for(int frame:laneFrames(*shot,rows[row]))if(frame>=firstFrame&&frame<=lastFrame)result.append(keyFor(rows[row],frame));return result;
    }
    const CelExposure *exposureAtPoint(const AnimationTrack &track,int x)const{
        // The boundary itself belongs to the preceding hold's end handle. A few
        // pixels inside the following block target its start handle instead.
        for(const auto &exposure:track.exposures)if(x==frameX(exposure.startFrame+exposure.durationFrames))return &exposure;
        return exposureAtFrame(track,frameAt(x));
    }
    QVector<TimelineCelRef> celsInRect(const QVector<TimelineRow> &rows,int firstRow,int lastRow,int firstFrame,int lastFrame)const{
        QVector<TimelineCelRef> result;
        for(int row=std::max(0,firstRow);row<=lastRow&&row<rows.size();++row)if(rows[row].track)
            for(const auto &cel:rows[row].track->exposures)if(cel.startFrame<=lastFrame&&cel.startFrame+cel.durationFrames>firstFrame)result.append({rows[row].id,cel.id});
        return result;
    }
    bool celMoveValid()const{
        const auto *shot=timelineShot(m_controller);if(!shot)return false;
        for(const auto &ref:m_dragCels)if(!celEditError(*shot,ref).isEmpty())return false;
        for(const auto &track:shot->animationTracks){QVector<QPair<int,int>> intervals;bool affected=false;
            for(const auto &cel:track.exposures){const bool selected=m_dragCelIds.contains(cel.id);const qint64 start=qint64(cel.startFrame)+(selected?m_dragDelta:0),end=start+cel.durationFrames;
                if(start<0||end>shot->durationFrames)return false;affected|=selected;intervals.append({int(start),int(end)});}
            if(affected){std::sort(intervals.begin(),intervals.end());for(int i=1;i<intervals.size();++i)if(intervals[i].first<intervals[i-1].second)return false;}}
        return true;
    }
    bool exposureGesture()const{return m_gesture==Gesture::ExposureMove||m_gesture==Gesture::ExposureStart||m_gesture==Gesture::ExposureEnd;}
    bool rangeGesture()const{return m_gesture==Gesture::RangeNew||m_gesture==Gesture::RangeStart||m_gesture==Gesture::RangeEnd;}
    bool exposureValid()const{const auto *shot=timelineShot(m_controller);if(!shot)return false;for(const auto &track:shot->animationTracks)if(track.id==m_exposureTrack)for(const auto &exposure:track.exposures)if(exposure.id!=m_exposure.id&&m_exposureStart<exposure.startFrame+exposure.durationFrames&&exposure.startFrame<m_exposureStart+m_exposureHold)return false;return true;}
    void startGesture(Gesture gesture,const QPoint &point){m_gesture=gesture;m_pressPoint=point;m_pressScroll=horizontalScrollBar()->value();m_dragMoved=false;m_gestureHistory=m_controller->document()->history()->index();m_gestureLayer=m_controller->selectedPropertyLayerId();m_gestureShot=timelineShot(m_controller)->id;m_gestureFrames=m_controller->frameCount();if(gesture!=Gesture::Marquee&&gesture!=Gesture::CelMarquee)m_controller->setPendingEdits(this,true);refreshHeaderControls();}
    void finishGesture(bool pointerReleased=false){
        if(m_gesture==Gesture::TrackMove&&!pointerReleased){cancelGesture();return;}
        if(m_resolving||m_gesture==Gesture::None)return;const QScopedValueRollback<bool> guard(m_resolving,true);const auto gesture=m_gesture;m_gesture=Gesture::None;m_controller->setPendingEdits(this,false);
        if(gesture==Gesture::TrackMove){
            const auto *shot=timelineShot(m_controller);
            if(m_dragMoved&&m_dropValid&&shot&&shot->id==m_gestureShot&&m_controller->document()->history()->index()==m_gestureHistory)
                m_controller->moveTimelineLayer(m_dragLayer,m_dropParent,m_dropBefore);
            m_dragLayer.clear();m_dropRow=-1;setProperty("timelineDropValid",false);setProperty("timelineDropError",QString());viewport()->unsetCursor();
        }else if((gesture==Gesture::CueMove||gesture==Gesture::CueStart||gesture==Gesture::CueEnd)&&m_dragMoved){
            const auto *shot=timelineShot(m_controller);const auto *source=shot?timelineCue(*shot,m_cueOriginal.id):nullptr;
            if(shot&&shot->id==m_gestureShot&&m_controller->document()->history()->index()==m_gestureHistory&&source&&*source==m_cueOriginal&&m_controller->selectedCueId()==m_cueOriginal.id)
                m_controller->setSelectedCueTiming(m_cueStart,m_cueDuration);
        }else if((gesture==Gesture::SymbolMove||gesture==Gesture::SymbolStart||gesture==Gesture::SymbolEnd||gesture==Gesture::SymbolSlip)&&m_dragMoved){
            if(m_controller->selectedSymbolClipId()==m_symbolLayerId)m_controller->editSelectedSymbolClip(symbolOperation(gesture),m_symbolValue);
        }else if(gesture==Gesture::Keys&&m_dragDelta){m_controller->setSelectedKeys(m_dragKeys);if(m_controller->moveSelectedKeys(m_dragDelta)&&!m_dragKeys.isEmpty())seekKeyFrame(m_dragKeys.first(),m_dragFrame+m_dragDelta);}
        else if(gesture==Gesture::Cels&&m_dragDelta){m_controller->setSelectedCels(m_dragCels);m_controller->moveSelectedCels(m_dragDelta);}
        else if((gesture==Gesture::ExposureMove||gesture==Gesture::ExposureStart||gesture==Gesture::ExposureEnd)&&(m_exposureStart!=m_exposure.startFrame||m_exposureHold!=m_exposure.durationFrames)){
            if(m_controller->setExposureTimingById(m_exposureTrack,m_exposure.id,m_exposureStart,m_exposureHold))m_controller->setFrame(m_exposureStart);}
        else if((gesture==Gesture::AudioMove||gesture==Gesture::AudioStart||gesture==Gesture::AudioEnd)&&m_audioPreview!=m_audioOriginal)m_controller->updateAudioClip(m_audioPreview);
        else if(gesture==Gesture::RangeNew||gesture==Gesture::RangeStart||gesture==Gesture::RangeEnd)m_controller->setPlaybackRange(m_rangeFirst,m_rangeLast);
        if(gesture==Gesture::AudioMove||gesture==Gesture::AudioStart||gesture==Gesture::AudioEnd){m_audioOriginal={};m_audioPreview={};}
        refreshHeaderControls();viewport()->update();
    }
    void cancelGesture(){if(m_resolving||m_gesture==Gesture::None)return;const QScopedValueRollback<bool> guard(m_resolving,true);const auto gesture=m_gesture;m_gesture=Gesture::None;m_scrubbing=false;m_controller->setPendingEdits(this,false);if(gesture==Gesture::TrackMove){m_dragLayer.clear();m_dropRow=-1;setProperty("timelineDropValid",false);setProperty("timelineDropError",QString());viewport()->unsetCursor();}if(gesture==Gesture::Marquee)m_controller->setSelectedKeys(m_originalSelection);if(gesture==Gesture::CelMarquee)m_controller->setSelectedCels(m_originalCels);if(gesture==Gesture::AudioMove||gesture==Gesture::AudioStart||gesture==Gesture::AudioEnd){m_audioOriginal={};m_audioPreview={};}refreshHeaderControls();viewport()->update();}
    int labelWidth()const{return std::min(240,std::max(180,viewport()->width()/3));}
    int headerIndent(const TimelineRow &row)const{
        // Keep the three pen-sized controls, identity icon and a readable name
        // distinct even when a deeply nested tree is shown in a narrow dock.
        const int available=row.artworkRow()?labelWidth()-86-50-44:labelWidth()-4-28-44;
        return std::min(std::min(row.depth,5)*12,std::max(0,available)/12*12);
    }
    int keyFrameAt(const Shot *shot,const TimelineRow &row,int x)const{
        int nearest=frameAt(x);qreal distance=7;
        if(shot)for(int frame:laneFrames(*shot,row)){
            const int centre=int(labelWidth()+(frame+.5)*cellWidth-horizontalScrollBar()->value());
            const qreal delta=std::abs(x-centre);
            if(delta<distance){distance=delta;nearest=frame;}
        }
        return nearest;
    }
    int rowAt(int y)const{return (y-rulerHeight+verticalScrollBar()->value())/rowHeight;}
    void fitFrames(int first,int last,bool entireShot){
        if(m_gesture!=Gesture::None||m_scrubbing)return;
        const int width=std::max(1,viewport()->width()-labelWidth());
        const int margin=entireShot?0:std::min(16,width/8);
        cellWidth=std::clamp(qreal(std::max(1,width-2*margin-1))/std::max(1,last-first+1),qreal(.00002),qreal(80));
        m_fitShot=entireShot;m_lastFrame=m_controller->currentFrame();refresh();
        horizontalScrollBar()->setValue(std::max(0,qRound(first*cellWidth)-margin));
        if(zoomChanged)zoomChanged(zoomPercent());
    }
    int frameX(int frame)const{return labelWidth()+qRound(frame*cellWidth)-horizontalScrollBar()->value();}
    int frameAt(int x)const{return std::clamp(int(std::floor((x-labelWidth()+horizontalScrollBar()->value())/cellWidth)),0,std::max(0,m_controller->frameCount()-1));}
    void autoScroll(const QPoint &point){if(m_gesture==Gesture::TrackMove){if(point.y()<rulerHeight+8)verticalScrollBar()->setValue(verticalScrollBar()->value()-rowHeight);else if(point.y()>viewport()->height()-8)verticalScrollBar()->setValue(verticalScrollBar()->value()+rowHeight);return;}if(point.x()<labelWidth())horizontalScrollBar()->setValue(horizontalScrollBar()->value()-std::max(1,qRound(cellWidth)));else if(point.x()>=viewport()->width())horizontalScrollBar()->setValue(horizontalScrollBar()->value()+std::max(1,qRound(cellWidth)));if(m_gesture==Gesture::Marquee||m_gesture==Gesture::CelMarquee){if(point.y()<rulerHeight)verticalScrollBar()->setValue(verticalScrollBar()->value()-rowHeight);else if(point.y()>=viewport()->height())verticalScrollBar()->setValue(verticalScrollBar()->value()+rowHeight);}}
    void scrub(int x){if(x<labelWidth()&&horizontalScrollBar()->value()>0)horizontalScrollBar()->setValue(horizontalScrollBar()->value()-std::max(1,qRound(cellWidth)));else if(x>=viewport()->width())horizontalScrollBar()->setValue(horizontalScrollBar()->value()+std::max(1,qRound(cellWidth)));if(!m_controller->scrubToFrame(frameAt(x)))m_scrubbing=false;}
    QHash<QString,QToolButton *> m_headerButtons;
    QString m_dragLayer,m_dropParent,m_dropBefore,m_dropError;int m_dropRow=-1;bool m_dropValid=false,m_dropInto=false;
    QPointer<TimelineController> m_controller;QString m_lastTrack,m_selectedLane;int m_lastFrame=-1;bool m_scrubbing=false,m_resolving=false,m_dragMoved=false;
    Gesture m_gesture=Gesture::None;QPoint m_pressPoint;int m_pressScroll=0,m_gestureHistory=-1,m_gestureFrames=0;QString m_gestureLayer,m_gestureShot;
    QVector<TimelineKeyRef> m_dragKeys,m_originalSelection,m_marqueeBase;std::optional<TimelineKeyRef> m_anchor;int m_dragDelta=0,m_dragFrame=0;
    QVector<TimelineCelRef> m_dragCels,m_originalCels,m_celMarqueeBase;QSet<QString> m_dragCelIds;std::optional<TimelineCelRef> m_celAnchor;
    TimelineCue m_cueOriginal,m_cuePreview;int m_cueStart=0,m_cueDuration=1;bool m_cueValid=true;
    AudioClip m_audioOriginal,m_audioPreview;
    QString m_symbolLayerId;SymbolInstance m_symbolOriginal,m_symbolPreview;int m_symbolValue=0;bool m_symbolValid=true;
    QString m_exposureTrack;CelExposure m_exposure;int m_exposureStart=0,m_exposureHold=1;
    int m_rangeAnchor=0,m_rangeFirst=0,m_rangeLast=0,m_marqueeRow=0,m_marqueeFrame=0,m_marqueeEndRow=0,m_marqueeEndFrame=0;
    qreal cellWidth=20;
    bool m_fitShot=false;
    int rowHeight=32;
    static constexpr int rulerHeight=26;
};

TimelinePanel::TimelinePanel(TimelineController *controller,QWidget *parent,DrawingTemplateLibrary *templates):QWidget(parent),m_controller(controller),m_templates(templates){
    setObjectName("timelineProperties");setMinimumWidth(240);auto *outer=new QVBoxLayout(this);outer->setContentsMargins(6,5,6,5);outer->setSpacing(3);
    m_reviewModes=new QTabBar(this);m_reviewModes->setObjectName("timelineReviewModes");m_reviewModes->setProperty("compactDrawingTabs",true);
    m_reviewModes->setExpanding(false);m_reviewModes->setDrawBase(false);m_reviewModes->setIconSize({ui::metrics::tabIcon,ui::metrics::tabIcon});m_reviewModes->setAccessibleName("Timeline review scope");
    m_reviewModes->addTab("Shot");m_reviewModes->addTab("Scene");m_reviewModes->addTab("Movie");
    m_reviewModes->setTabToolTip(0,"Edit the current shot");m_reviewModes->setTabToolTip(1,"Review all shots in the current scene with audio");m_reviewModes->setTabToolTip(2,"Review the whole movie with audio");
    m_reviewModes->setAccessibleTabName(0,"Shot editing timeline");m_reviewModes->setAccessibleTabName(1,"Scene playback");m_reviewModes->setAccessibleTabName(2,"Movie playback");outer->addWidget(m_reviewModes);
    m_reviewPages=new QStackedWidget(this);m_reviewPages->setObjectName("timelineReviewPages");outer->addWidget(m_reviewPages,1);
    auto *shotPage=new QWidget(m_reviewPages);shotPage->setObjectName("timelineShotPage");auto *layout=new QVBoxLayout(shotPage);layout->setContentsMargins(0,0,0,0);layout->setSpacing(3);m_reviewPages->addWidget(shotPage);
    m_reviewPages->addWidget(new MovieReviewPanel(controller->movieReview(),m_reviewPages));
    connect(m_reviewModes,&QTabBar::currentChanged,this,[this](int index){m_controller->movieReview()->setMode(static_cast<ReviewMode>(index));refreshReviewMode();});
    connect(controller->movieReview(),&MovieReviewController::changed,this,&TimelinePanel::refreshReviewMode);
    auto *header=new QWidget(this);m_headerLayout=new QGridLayout(header);m_headerLayout->setContentsMargins(0,0,0,0);m_headerLayout->setSpacing(3);layout->addWidget(header);
    m_transportRow=new QWidget(header);auto *transport=new QHBoxLayout(m_transportRow);transport->setContentsMargins(0,0,0,0);transport->setSpacing(2);
    m_previousDrawing=timelineButton(this,"timelinePreviousDrawing","skip-back","Previous drawing");m_previousFrame=timelineButton(this,"timelinePreviousFrame","chevron-left","Previous frame");
    m_play=timelineButton(this,"timelinePlay","play","Play or pause (Space in timeline)");m_nextFrame=timelineButton(this,"timelineNextFrame","chevron-right","Next frame");
    m_nextDrawing=timelineButton(this,"timelineNextDrawing","skip-forward","Next drawing");m_loop=timelineButton(this,"timelineLoop","repeat","Loop playback",true);m_onion=timelineButton(this,"timelineOnion","layers","Onion skin",true);
    m_audioScrub=timelineButton(this,"timelineAudioScrub","volume-2","Audible playhead scrubbing",true);
    for(auto *button:{m_previousDrawing,m_previousFrame,m_play,m_nextFrame,m_nextDrawing,m_loop,m_onion,m_audioScrub})transport->addWidget(button);transport->addStretch();
    m_editingRow=new QWidget(header);m_editingLayout=new QBoxLayout(QBoxLayout::LeftToRight,m_editingRow);m_editingLayout->setContentsMargins(0,0,0,0);m_editingLayout->setSpacing(3);
    auto *drawingActions=new QWidget(m_editingRow);auto *editing=new QHBoxLayout(drawingActions);editing->setContentsMargins(0,0,0,0);editing->setSpacing(2);m_editingLayout->addWidget(drawingActions);
    m_add=timelineButton(this,"timelineAddTrack","plus","Add a static, frame-by-frame or keyframe track");m_blank=timelineButton(this,"timelineNewDrawing","file-plus","New blank drawing at the current frame");
    m_duplicate=timelineButton(this,"timelineDuplicateDrawing","copy","Duplicate the current or previous drawing at this frame");m_remove=timelineButton(this,"timelineRemoveExposure","trash","Remove selected exposures; keep drawings for reuse");
    for(auto *button:{m_add,m_blank,m_duplicate,m_remove}){button->setFixedWidth(28);editing->addWidget(button);}editing->addStretch();
    m_frame=new ValueSlider("Current frame",1,1,1);m_frame->setObjectName("timelineCurrentFrame");m_editingLayout->addWidget(m_frame,1);
    m_headerLayout->addWidget(m_transportRow,0,0);m_headerLayout->addWidget(m_editingRow,1,0);
    m_grid=new TimelineGrid(controller,this);layout->addWidget(m_grid,1);
    auto *footer=new QWidget(this);m_footerLayout=new QGridLayout(footer);m_footerLayout->setContentsMargins(0,0,0,0);m_footerLayout->setSpacing(3);layout->addWidget(footer);
    m_navigationRow=new QWidget(footer);auto *navigation=new QHBoxLayout(m_navigationRow);navigation->setContentsMargins(0,0,0,0);navigation->setSpacing(ui::metrics::controlGap);
    auto *zoomLabel=new QLabel;zoomLabel->setPixmap(appIcon("zoom-in").pixmap(ui::metrics::actionIcon,ui::metrics::actionIcon));zoomLabel->setAccessibleName("Timeline zoom");
    zoomLabel->setFixedWidth(ui::metrics::actionTarget);zoomLabel->setAlignment(Qt::AlignCenter);zoomLabel->setToolTip("Timeline zoom (%) · Ctrl/Cmd + wheel");navigation->addWidget(zoomLabel);
    m_zoom=new ValueSlider("Timeline zoom (%)",.0001,400,100,4);m_zoom->setObjectName("timelineZoom");m_zoom->setSliderRange(1,400);m_zoom->setSingleStep(5);navigation->addWidget(m_zoom,1);
    m_fitShot=timelineButton(this,"timelineFitShot","scan","Fit the entire shot in the timeline");
    m_fitSelection=timelineButton(this,"timelineFitSelection","focus","Fit selected cue, Symbol clip, cels, keys, audio, playback range or drawing; otherwise fit the current frame");
    m_centerFrame=timelineButton(this,"timelineCenterPlayhead","toon-center-playhead","Center the current frame in the timeline without changing zoom");
    navigation->addWidget(m_centerFrame);navigation->addWidget(m_fitShot);navigation->addWidget(m_fitSelection);
    m_grid->zoomChanged=[this](qreal percent){m_zoom->setValueSilently(percent);};
    connect(m_zoom,&ValueSlider::valueChanged,this,[this](double percent){m_grid->setZoomPercent(percent);});
    connect(m_fitShot,&QToolButton::clicked,this,[this]{m_zoom->cancelEditing();m_grid->fitShot();});
    connect(m_fitSelection,&QToolButton::clicked,this,[this]{m_zoom->cancelEditing();m_grid->fitSelection();});
    connect(m_centerFrame,&QToolButton::clicked,this,[this]{m_grid->centerPlayhead();});
    m_rangeRow=new QWidget(footer);auto *rangeRow=new QHBoxLayout(m_rangeRow);rangeRow->setContentsMargins(0,0,0,0);rangeRow->setSpacing(3);
    m_addCue=timelineButton(this,"timelineAddCue","pin","Add a marker, pose or dialogue cue (M adds a marker in Timeline)");
    m_addCue->setPopupMode(QToolButton::InstantPopup);auto *cueMenu=new QMenu(m_addCue);cueMenu->setObjectName("timelineAddCueMenu");populateCueMenu(cueMenu,controller);m_addCue->setMenu(cueMenu);
    m_addFolder=timelineButton(this,"timelineAddFolder","folder-plus","New timeline folder");
    m_clearSolo=timelineButton(this,"timelineClearSolo","focus","Clear timeline solo · Restore the normal preview");
    m_reuse=timelineButton(this,"timelineReuseDrawing","folder-open","Reuse an existing drawing at the playhead");
    m_range=timelineButton(this,"timelineRange","scan","Use selected playback range (Shift-drag the ruler)",true);
    m_rangeLabel=new TimelineContext;m_rangeLabel->setObjectName("timelineRangeLabel");m_rangeLabel->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Fixed);
    m_rangeSettings=timelineButton(this,"timelineRangeSettings","sliders-horizontal","Playback range settings");
    m_onionSettings=timelineButton(this,"timelineOnionSettings","settings-2","Onion skin settings");
    m_ripple=timelineButton(this,"timelineRipple","split","Ripple time across all tracks");
    m_audioImport=timelineButton(this,"timelineImportAudio","music","Import audio into the timeline");m_audioCancel=timelineButton(this,"timelineCancelAudioImport","x","Cancel audio import");
    m_previewQuality=timelineButton(this,"timelinePreviewQuality","sliders-horizontal","Playback preview resolution");
    m_previewQuality->setProperty("compactDrawingAction",true);m_previewQuality->setToolButtonStyle(Qt::ToolButtonTextOnly);
    m_previewQuality->setFocusPolicy(Qt::TabFocus);m_previewQuality->setPopupMode(QToolButton::InstantPopup);
    auto *qualityMenu=new PlaybackQualityMenu(m_previewQuality);qualityMenu->setObjectName("timelinePreviewQualityMenu");
    auto *qualityGroup=new QActionGroup(qualityMenu);qualityGroup->setExclusive(true);
    const QStringList qualityNames{"Full","Half","Quarter","Eighth"};const int divisors[]{1,2,4,8};
    for(int i=0;i<4;++i){const int divisor=divisors[i];
        auto *option=qualityMenu->addAction(i?qualityNames[i]+QString(" · 1/%1 resolution").arg(divisor):QString("Full resolution"));
        option->setObjectName("timelinePreview"+qualityNames[i]);option->setData(divisor);option->setCheckable(true);qualityGroup->addAction(option);
        option->setToolTip("Playback preview only. Paused artwork and exports remain full quality.");
        connect(option,&QAction::triggered,this,[this,divisor]{m_controller->setPlaybackPreviewDivisor(divisor);});
    }
    m_previewQuality->setMenu(qualityMenu);
    rangeRow->addWidget(m_range);rangeRow->addWidget(m_rangeLabel,1);rangeRow->addWidget(m_addFolder);rangeRow->addWidget(m_clearSolo);rangeRow->addWidget(m_reuse);rangeRow->addWidget(m_addCue);rangeRow->addWidget(m_previewQuality);rangeRow->addWidget(m_rangeSettings);rangeRow->addWidget(m_onionSettings);rangeRow->addWidget(m_ripple);rangeRow->addWidget(m_audioImport);rangeRow->addWidget(m_audioCancel);
    m_more=timelineButton(this,"timelineMore","ellipsis","More timeline actions");m_more->setPopupMode(QToolButton::InstantPopup);
    m_options=new QMenu(m_more);m_options->setObjectName("timelineMoreMenu");m_more->setMenu(m_options);rangeRow->addWidget(m_more);
    const QList<QPair<QToolButton *,QString>> options{{m_centerFrame,"Center playhead"},{m_addFolder,"New folder"},{m_reuse,"Reuse drawing…"},{m_addCue,"Add cue"},{m_rangeSettings,"Playback range…"},{m_onionSettings,"Onion skin settings…"},{m_ripple,"Ripple time…"},{m_audioImport,"Import audio…"},{m_audioScrub,"Audible scrubbing"}};
    for(const auto &[source,label]:options){
        auto *action=m_options->addAction(source->icon(),label);action->setObjectName(source->objectName()+"Option");action->setProperty("timelineSource",source->objectName());
        action->setCheckable(source->isCheckable());if(source->menu())action->setMenu(source->menu());
        else connect(action,&QAction::triggered,source,[source]{source->click();});
    }
    connect(m_options,&QMenu::aboutToShow,this,&TimelinePanel::refreshOptions);
    m_footerLayout->addWidget(m_navigationRow,0,0);m_footerLayout->addWidget(m_rangeRow,1,0);
    m_audioStatus=new TimelineContext;m_audioStatus->setObjectName("timelineAudioImportStatus");m_audioStatus->setFixedHeight(fontMetrics().height());m_audioStatus->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Fixed);layout->addWidget(m_audioStatus);
    connect(m_addFolder,&QToolButton::clicked,this,[this]{addFolder({});});
    connect(m_clearSolo,&QToolButton::clicked,controller,[controller]{controller->clearTimelineSolo();});
    m_grid->addFolderRequested=[this](const QString &parentId){addFolder(parentId);};
    connect(m_reuse,&QToolButton::clicked,this,[this]{openDrawingReuse(m_controller,this);});
    connect(m_audioScrub,&QToolButton::toggled,controller,&TimelineController::setAudioScrubbing);
    connect(m_audioImport,&QToolButton::clicked,controller,&TimelineController::importAudio);connect(m_audioCancel,&QToolButton::clicked,controller,&TimelineController::cancelAudioImport);
    connect(m_range,&QToolButton::toggled,controller,&TimelineController::setPlaybackRangeEnabled);
    connect(m_rangeSettings,&QToolButton::clicked,this,&TimelinePanel::editRange);
    connect(m_onionSettings,&QToolButton::clicked,this,&TimelinePanel::editOnionSkin);
    connect(m_ripple,&QToolButton::clicked,this,&TimelinePanel::rippleTime);
    connect(m_add,&QToolButton::clicked,this,&TimelinePanel::addTrack);connect(m_blank,&QToolButton::clicked,this,[this]{m_controller->newDrawing();});
    connect(m_duplicate,&QToolButton::clicked,this,[this]{m_controller->newDrawing(true);});connect(m_remove,&QToolButton::clicked,this,[this]{
        if(!m_controller->selectedCueId().isEmpty())m_controller->removeSelectedCue();
        else if(!m_controller->selectedAudioClipId().isEmpty())m_controller->removeAudioClip();
        else if(!m_controller->selectedKeys().isEmpty())m_controller->deleteSelectedKeys();
        else if(const auto *shot=timelineShot(m_controller);shot&&!m_controller->selectedSymbolClipId().isEmpty()){
            if(const auto *layer=layerFor(*shot,m_controller->selectedSymbolClipId());layer&&layer->symbol)m_controller->setSelectedSymbolClipTiming(layer->symbol->startFrame,0,layer->symbol->playbackOffset);
        }else m_controller->removeExposure();});
    connect(m_previousFrame,&QToolButton::clicked,this,[this]{m_controller->stepFrame(-1);});connect(m_nextFrame,&QToolButton::clicked,this,[this]{m_controller->stepFrame(1);});
    connect(m_previousDrawing,&QToolButton::clicked,this,[this]{m_controller->stepDrawing(-1);});connect(m_nextDrawing,&QToolButton::clicked,this,[this]{m_controller->stepDrawing(1);});
    connect(m_play,&QToolButton::clicked,this,[this]{m_controller->setPlaying(!m_controller->playing());});
    connect(m_loop,&QToolButton::toggled,controller,&TimelineController::setLooping);connect(m_onion,&QToolButton::toggled,controller,&TimelineController::setOnionSkin);
    connect(m_frame,&ValueSlider::editingStarted,this,[this]{if(!m_refreshing)m_controller->setPlaying(false);});
    connect(m_frame,&ValueSlider::valueChanged,this,[this](double frame){if(m_refreshing)return;const QScopedValueRollback<bool> guard(m_settingFrame,true);
        if(!m_controller->setFrame(qRound(frame)-1)){m_displayedFrame=-1;refresh();}});
    connect(controller,&TimelineController::editResolutionRequested,this,[this]{if(!m_settingFrame)m_frame->commitEditing();});
    connect(controller,&TimelineController::changed,this,&TimelinePanel::refresh);arrangeControls();refresh();refreshReviewMode();
}
TimelinePanel::~TimelinePanel(){
    // Grid gesture cleanup can notify the controller while Qt is deleting the
    // Shot page. Its toolbar children may already be gone at that point.
    if(m_controller){
        disconnect(m_controller,nullptr,this,nullptr);
        disconnect(m_controller->movieReview(),nullptr,this,nullptr);
    }
}
void TimelinePanel::refreshReviewMode(){
    const auto mode=m_controller->movieReview()->mode();
    const QSignalBlocker block(m_reviewModes);m_reviewModes->setCurrentIndex(static_cast<int>(mode));
    m_reviewModes->setVisible(!m_controller->property("symbolEditor").toBool());
    m_reviewModes->setTabEnabled(1,m_controller->document()->isOpen());m_reviewModes->setTabEnabled(2,m_controller->document()->isOpen());
    m_reviewPages->setCurrentIndex(mode==ReviewMode::Shot?0:1);
}
void TimelinePanel::resizeEvent(QResizeEvent *event){QWidget::resizeEvent(event);arrangeControls();}
void TimelinePanel::changeEvent(QEvent *event){QWidget::changeEvent(event);if(event->type()==QEvent::FontChange||event->type()==QEvent::StyleChange){arrangeControls();if(m_audioStatus)m_audioStatus->setFixedHeight(fontMetrics().height());}}
void TimelinePanel::arrangeControls(){
    if(m_arrangingControls||!m_more)return;const QScopedValueRollback<bool> guard(m_arrangingControls,true);
    const bool narrow=width()<700,wide=width()>=1000;
    const int mode=(narrow?1:0)|(wide?2:0)|(width()>=760?4:0)|(width()<360?8:0)|(width()<350?16:0)|(width()<420?32:0)|(m_controller->property("symbolEditor").toBool()?64:0);
    if(mode==m_arrangementMode)return;m_arrangementMode=mode;
    // Move existing layout items, rather than re-adding their widgets. Qt's
    // addWidget queues a Show for the row and its children, which resets a
    // spin box's unfinished text to its last committed value during resize.
    const auto moveItem=[](QGridLayout *grid,QWidget *widget,int row,int column){
        if(auto *item=grid->takeAt(grid->indexOf(widget)))grid->addItem(item,row,column);
    };
    moveItem(m_headerLayout,m_transportRow,0,0);moveItem(m_headerLayout,m_editingRow,width()>=760?0:1,width()>=760?1:0);
    m_headerLayout->setColumnStretch(0,width()>=760?0:1);m_headerLayout->setColumnStretch(1,width()>=760?1:0);
    m_editingLayout->setDirection(width()<360?QBoxLayout::TopToBottom:QBoxLayout::LeftToRight);
    moveItem(m_footerLayout,m_navigationRow,0,0);moveItem(m_footerLayout,m_rangeRow,wide?0:1,wide?1:0);
    m_footerLayout->setColumnStretch(0,1);m_footerLayout->setColumnStretch(1,0);
    m_audioScrub->setVisible(width()>=350);m_centerFrame->setVisible(width()>=420);
    for(auto *button:{m_addFolder,m_reuse,m_rangeSettings,m_onionSettings,m_ripple,m_audioImport})button->setVisible(!narrow);
    m_addCue->setVisible(!narrow&&!m_controller->property("symbolEditor").toBool());
    m_more->setVisible(narrow);
    setProperty("timelineCompactActions",narrow);
}
void TimelinePanel::refreshOptions(){
    for(auto *action:m_options->actions())if(auto *source=findChild<QToolButton *>(action->property("timelineSource").toString())){
        action->setEnabled(source->isEnabled());action->setChecked(source->isChecked());action->setToolTip(source->toolTip());
        action->setVisible(source!=m_addCue||!m_controller->property("symbolEditor").toBool());
    }
}
void TimelinePanel::refresh(){
    if(m_refreshing)return;const QScopedValueRollback<bool> guard(m_refreshing,true);
    refreshReviewMode();
    const bool open=timelineShot(m_controller),playing=m_controller->playing();const auto *track=selectedTrack(m_controller);const auto *root=selectedRoot(m_controller);const auto *exposure=currentExposure(m_controller);
    m_zoom->setEnabled(open);m_fitShot->setEnabled(open);m_fitSelection->setEnabled(open);m_centerFrame->setEnabled(open);
    m_addFolder->setEnabled(open&&!playing);m_clearSolo->setVisible(open&&!m_controller->timelineSoloLayers().isEmpty());m_clearSolo->setEnabled(open);
    const bool editable=open&&!playing&&root&&!timelineLayerLocked(*timelineShot(m_controller),root->id)&&m_controller->selectedAudioClipId().isEmpty()&&!m_controller->cameraSelected()&&m_controller->selectedSymbolClipId().isEmpty()&&m_controller->selectedSymbolPoseLayerId().isEmpty()&&m_controller->selectedCueId().isEmpty()&&m_controller->selectedKeys().isEmpty();
    bool canDuplicate=false;if(track)for(const auto &candidate:track->exposures)if(candidate.startFrame<=m_controller->currentFrame())canDuplicate=true;
    m_add->setEnabled(open&&!playing);m_blank->setEnabled(editable&&track);m_duplicate->setEnabled(editable&&canDuplicate);bool celsEditable=!m_controller->selectedCels().isEmpty();if(open)for(const auto &ref:m_controller->selectedCels())if(!celEditError(*timelineShot(m_controller),ref).isEmpty())celsEditable=false;
    const auto symbolId=m_controller->selectedSymbolClipId();const bool symbolSelected=!symbolId.isEmpty();
    const bool poseSelected=!m_controller->selectedSymbolPoseLayerId().isEmpty();
    const bool keysSelected=!m_controller->selectedKeys().isEmpty(),keysEditable=selectedKeysEditable(m_controller);
    const bool audioSelected=!m_controller->selectedAudioClipId().isEmpty();
    const bool currentCelEditable=editable&&track&&exposure&&celEditError(*timelineShot(m_controller),{track->id,exposure->id}).isEmpty();
    const bool cueSelected=!m_controller->selectedCueId().isEmpty();m_addCue->setEnabled(open&&!playing&&!m_controller->property("symbolEditor").toBool());
    m_remove->setToolTip(cueSelected?"Delete selected cue":audioSelected?"Delete selected audio clip":keysSelected?(poseSelected?"Delete selected pose keys":"Delete selected keyframes"):symbolSelected?"Clear Symbol clip exposure; keep the instance for reuse":"Remove selected exposures; keep drawings for reuse");m_remove->setAccessibleName(m_remove->toolTip());
    m_remove->setEnabled(open&&!playing&&(cueSelected||audioSelected||(keysSelected?keysEditable:(symbolSelected?symbolClipEditError(*timelineShot(m_controller),symbolId).isEmpty():(celsEditable||(m_controller->selectedCels().isEmpty()&&currentCelEditable))))));m_reuse->setEnabled(editable&&track&&!track->drawings.isEmpty());
    m_previousFrame->setEnabled(open&&m_controller->currentFrame()>0);m_nextFrame->setEnabled(open&&m_controller->currentFrame()+1<m_controller->frameCount());
    m_previousDrawing->setEnabled(open&&track&&!track->exposures.isEmpty()&&!m_controller->cameraSelected());m_nextDrawing->setEnabled(m_previousDrawing->isEnabled());m_play->setEnabled(open&&m_controller->frameCount()>1);
    m_play->setIcon(appIcon(playing?"pause":"play"));m_play->setToolTip(playing?"Pause playback (Space in timeline)":"Play (Space in timeline)");m_play->setAccessibleName(m_play->toolTip());
    {const QSignalBlocker a(m_loop),b(m_onion);m_loop->setChecked(m_controller->looping());m_onion->setChecked(m_controller->onionSkin());}m_loop->setEnabled(open);m_onion->setEnabled(open);
    {const QSignalBlocker range(m_range);m_range->setChecked(m_controller->playbackRangeEnabled());}
    {const QSignalBlocker block(m_audioScrub);m_audioScrub->setChecked(m_controller->audioScrubbing());}m_audioScrub->setEnabled(open&&!playing);
    m_range->setEnabled(open);m_rangeSettings->setEnabled(open);
    m_onionSettings->setEnabled(open&&!playing);m_ripple->setEnabled(open&&!playing&&!m_controller->audioImportBusy());
    m_audioImport->setEnabled(open&&!playing&&!m_controller->audioImportBusy());m_audioCancel->setVisible(m_controller->audioImportBusy());m_audioCancel->setEnabled(m_controller->audioImportBusy());
    const int previewDivisor=m_controller->playbackPreviewDivisor();
    const QString previewName=previewDivisor==1?QString("Full resolution"):QString("1/%1 resolution").arg(previewDivisor);
    m_previewQuality->setText(QString("1/%1").arg(previewDivisor));
    m_previewQuality->setAccessibleName("Playback preview: "+previewName);
    m_previewQuality->setToolTip("Playback preview: "+previewName+"\nLower resolution for faster playback. Paused artwork and exports remain full quality.");
    for(auto *option:m_previewQuality->menu()->actions())option->setChecked(option->data().toInt()==previewDivisor);
    QString status=m_controller->audioImportStatus();
    if(status.isEmpty()&&playing&&previewDivisor>1){
        const auto fallback=m_controller->canvas()->playbackPreviewFallbackReason();
        status=fallback.isEmpty()?QString("1/%1 playback preview").arg(previewDivisor):QString("Full preview · ")+fallback;
    }
    QString selection;
    if(open){
        const auto &shot=*timelineShot(m_controller);
        if(cueSelected){if(const auto *cue=timelineCue(shot,m_controller->selectedCueId()))selection="Cue · "+cue->name;}
        else if(audioSelected){if(const auto *clip=audioFor(shot,m_controller->selectedAudioClipId()))selection="Audio · "+clip->name;}
        else if(keysSelected)selection=QString("%1 selected %2").arg(m_controller->selectedKeys().size()).arg(poseSelected?"pose keys":"keyframes");
        else if(!m_controller->selectedCels().isEmpty())selection=QString("%1 selected drawings").arg(m_controller->selectedCels().size());
        else if(symbolSelected){if(const auto *layer=layerFor(shot,symbolId))selection="Symbol · "+layer->name;}
        else if(m_controller->cameraSelected())selection="Camera";
        else if(track)selection="Drawings · "+(root?root->name:QString());
        else if(root)selection="Static · "+root->name;
        else selection="Add a track to start animating";
        selection+=QString(" · %1 / %2 · %3 s").arg(m_controller->currentFrame()+1).arg(m_controller->frameCount()).arg(m_controller->currentFrame()/m_controller->document()->project().fps,0,'f',2);
    }else selection="Open a project to use the timeline";
    setProperty("timelineSelectionContext",selection);
    if(status.isEmpty())status=selection;
    m_audioStatus->setText(status);m_audioStatus->setToolTip(status);m_audioStatus->setAccessibleName(status);
    const QString rangeText=m_controller->playbackRangeEnabled()?QString("Range %1–%2").arg(m_controller->playbackFirstFrame()+1).arg(m_controller->playbackLastFrame()+1):QString("Entire shot");
    m_rangeLabel->setText(rangeText);m_rangeLabel->setToolTip(rangeText);m_rangeLabel->setAccessibleName(rangeText);
    const int count=std::max(1,m_controller->frameCount()),frame=m_controller->currentFrame()+1;
    if(m_frameCount!=count){m_frameCount=count;m_frame->setRange(1,count);}if(frame!=m_displayedFrame){m_displayedFrame=frame;m_frame->setValueSilently(frame);}
    m_frame->setEnabled(open);m_frame->setToolTip(open?QString("Frame %1 of %2 · %3 FPS").arg(frame).arg(count).arg(m_controller->document()->project().fps):QString("Open a project"));m_grid->refresh();arrangeControls();refreshOptions();
}
void TimelinePanel::editRange(){
    if(!m_controller->prepareTimelineEdit())return;
    QDialog dialog(this);dialog.setObjectName("timelineRangeDialog");dialog.setWindowTitle("Playback range");dialog.setMinimumWidth(240);dialog.resize(280,200);
    auto *layout=new QVBoxLayout(&dialog);layout->addWidget(new QLabel("Start"));
    auto *first=new ValueSlider("Playback range first frame",1,m_controller->frameCount(),m_controller->playbackFirstFrame()+1);first->setObjectName("timelineRangeFirst");layout->addWidget(first);
    layout->addWidget(new QLabel("End"));auto *last=new ValueSlider("Playback range last frame",1,m_controller->frameCount(),m_controller->playbackLastFrame()+1);last->setObjectName("timelineRangeLast");layout->addWidget(last);
    bool updating=false;const auto apply=[&](bool start){if(updating)return;const QScopedValueRollback<bool> guard(updating,true);int a=qRound(first->value())-1,b=qRound(last->value())-1;if(a>b){if(start)b=a;else a=b;}m_controller->setPlaybackRange(a,b);first->setValueSilently(a+1);last->setValueSilently(b+1);};
    connect(first,&ValueSlider::editingFinished,&dialog,[&]{apply(true);});connect(last,&ValueSlider::editingFinished,&dialog,[&]{apply(false);});
    auto *footer=new QHBoxLayout;auto *reset=timelineButton(&dialog,"timelineRangeReset","rotate-ccw","Reset playback range to the entire shot");footer->addWidget(reset);footer->addStretch();
    auto *close=new QPushButton("Close");close->setAutoDefault(false);footer->addWidget(close);layout->addLayout(footer);
    connect(reset,&QToolButton::clicked,&dialog,[&]{const QScopedValueRollback<bool> guard(updating,true);first->cancelEditing();last->cancelEditing();m_controller->resetPlaybackRange();first->setValueSilently(1);last->setValueSilently(m_controller->frameCount());});
    const auto finish=[&]{if(updating)return;first->commitEditing();last->commitEditing();};
    connect(m_controller,&TimelineController::editResolutionRequested,&dialog,finish);
    connect(close,&QPushButton::clicked,&dialog,[&]{finish();dialog.accept();});dialog.exec();finish();
}
void TimelinePanel::editOnionSkin(){
    if(!m_controller->prepareTimelineEdit())return;
    OnionSettingsDialog dialog(m_controller->onionSkinSettings(),this);
    connect(&dialog,&OnionSettingsDialog::settingsChanged,m_controller,&TimelineController::setOnionSkinSettings);
    if(dialog.exec()==QDialog::Accepted)m_controller->rememberOnionSkinSettings();
}
void TimelinePanel::rippleTime(){
    if(!m_controller->prepareTimelineEdit())return;
    TimelineRippleDialog dialog(m_controller,this);dialog.exec();
}
void TimelinePanel::addFolder(const QString &parentId){
    if(!m_controller->prepareTimelineEdit())return;
    QInputDialog dialog(this);dialog.setObjectName("timelineFolderDialog");dialog.setWindowTitle("New folder");dialog.setLabelText("Folder name");dialog.setTextValue("Folder");dialog.setOkButtonText("Create");
    if(auto *name=dialog.findChild<QLineEdit *>()){name->setMaxLength(100);name->setAccessibleName("Timeline folder name");}
    if(dialog.exec()==QDialog::Accepted&&!dialog.textValue().trimmed().isEmpty())m_controller->addTimelineFolder(parentId,dialog.textValue());
}
void TimelinePanel::addTrack(){
    if(!m_controller->prepareTimelineEdit())return;
    QDialog dialog(this);dialog.setObjectName("addAnimationTrackDialog");dialog.setWindowTitle("Add track");dialog.resize(340,260);auto *layout=new QVBoxLayout(&dialog);
    auto *type=new QTabBar;type->setObjectName("animationTrackType");type->setExpanding(true);type->setDrawBase(false);
    type->addTab(appIcon("image"),"Static");type->addTab(appIcon("clapperboard"),"Frame by frame");type->addTab(appIcon("video"),"Keyframes");
    type->setTabToolTip(0,"One layer visible throughout the shot");type->setTabToolTip(1,"Independent drawings with timed exposures");type->setAccessibleTabName(0,"Static track");type->setAccessibleTabName(1,"Frame-by-frame track");type->setTabToolTip(2,"Draw on a layer and animate transforms, opacity, effects, masks or vector paths");type->setAccessibleTabName(2,"Keyframe animation track");type->setCurrentIndex(1);layout->addWidget(type);
    auto *name=new QLineEdit("Animation");name->setObjectName("animationTrackName");name->setAccessibleName("Track name");name->setPlaceholderText("Track name");name->setMaxLength(100);layout->addWidget(name);
    auto *templates=new DrawingTemplateWidget(m_templates);templates->setBuiltIn(DrawingTemplate::Anime);layout->addWidget(templates);
    connect(type,&QTabBar::currentChanged,&dialog,[=](int index){templates->setVisible(index==1);if(name->text()=="Animation"||name->text()=="Static"||name->text()=="Keyframes")name->setText(index==2?"Keyframes":index?"Animation":"Static");});
    auto *error=new QLabel;error->setObjectName("animationTrackError");error->setWordWrap(true);error->hide();layout->addWidget(error);
    connect(m_controller->document(),&Document::warning,&dialog,[error](const QString &message){error->setText(message);error->show();});
    auto *buttons=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel);buttons->button(QDialogButtonBox::Ok)->setText("Add");buttons->button(QDialogButtonBox::Ok)->setObjectName("confirmAnimationTrack");layout->addWidget(buttons);
    const auto updateAdd=[=]{buttons->button(QDialogButtonBox::Ok)->setEnabled(!name->text().trimmed().isEmpty()&&(type->currentIndex()!=1||templates->validTemplate()));};
    connect(name,&QLineEdit::textChanged,&dialog,updateAdd);connect(templates,&DrawingTemplateWidget::changed,&dialog,updateAdd);connect(type,&QTabBar::currentChanged,&dialog,updateAdd);updateAdd();
    connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);connect(buttons,&QDialogButtonBox::accepted,&dialog,[&]{
        if(auto *focused=dialog.focusWidget())focused->clearFocus();
        error->clear();error->hide();if(m_controller->addTrack(type->currentIndex()==1,templates->templateSlots(),name->text().trimmed())){if(type->currentIndex()==2)emit m_controller->animationPropertiesRequested();dialog.accept();}
        else if(error->text().isEmpty()){error->setText("This track could not be added.");error->show();}});
    dialog.exec();
}

FramePropertiesPanel::FramePropertiesPanel(TimelineController *controller,QWidget *parent):QWidget(parent),m_controller(controller){
    setObjectName("framePropertiesContent");setMinimumWidth(0);auto *outer=new QVBoxLayout(this);outer->setContentsMargins(4,4,4,4);outer->setSpacing(4);
    auto *tabs=new FrameTabs;tabs->setObjectName("framePropertiesTabs");tabs->setProperty("compactDrawingTabs",true);tabs->setIconSize({16,16});tabs->setExpanding(true);tabs->setDrawBase(false);tabs->setUsesScrollButtons(true);
    tabs->addTab(appIcon("clapperboard"),"Drawing");tabs->addTab(appIcon("video"),"Animation");tabs->addTab(appIcon("music"),"Audio");tabs->addTab(appIcon("focus"),"Camera");tabs->addTab(appIcon("toon-symbol"),"Symbol");tabs->addTab(appIcon("pin"),"Cues");
    const QStringList pageIds{"Drawing","Animation","Audio","Camera","Symbol","Cues"};
    for(int i=0;i<tabs->count();++i)tabs->setTabData(i,pageIds[i]);
    tabs->setTabToolTip(0,"Drawing name and exposure timing");tabs->setTabToolTip(1,"Layer property and vector path keyframes");
    tabs->setAccessibleTabName(0,"Drawing properties");tabs->setAccessibleTabName(1,"Animation properties");tabs->setTabToolTip(2,"Audio clip timing, trim and volume");tabs->setAccessibleTabName(2,"Audio properties");tabs->setTabToolTip(3,"2D shot and 3D reference camera animation");tabs->setAccessibleTabName(3,"Camera properties");tabs->setTabToolTip(4,"Symbol clip timing and starting frame");tabs->setAccessibleTabName(4,"Symbol clip properties");tabs->setTabToolTip(5,"Shot markers, poses and dialogue notes");tabs->setAccessibleTabName(5,"Cue properties");tabs->setTabVisible(5,!controller->property("symbolEditor").toBool());outer->addWidget(tabs);
    auto *pages=new QStackedWidget;pages->setObjectName("framePropertiesPages");outer->addWidget(pages,1);
    auto *drawingPage=new QWidget;auto *layout=new QVBoxLayout(drawingPage);layout->setContentsMargins(0,0,0,0);layout->setSpacing(5);pages->addWidget(drawingPage);
    pages->addWidget(new KeyframePanel(controller));pages->addWidget(new AudioPanel(controller));auto *cameraPage=new CameraPanel(controller);pages->addWidget(cameraPage);pages->addWidget(new SymbolClipPanel(controller));pages->addWidget(new TimelineCuePanel(controller));
    connect(tabs,&QTabBar::currentChanged,this,[controller,pages,cameraPage](int index){controller->finishEditing();pages->setCurrentIndex(index);if(index==3)cameraPage->activate();});
    connect(controller,&TimelineController::drawingPropertiesRequested,this,[tabs]{tabs->setCurrentIndex(0);});
    connect(controller,&TimelineController::animationPropertiesRequested,this,[tabs]{tabs->setCurrentIndex(1);});
    connect(controller,&TimelineController::audioPropertiesRequested,this,[tabs]{tabs->setCurrentIndex(2);});
    connect(controller,&TimelineController::cameraPropertiesRequested,this,[tabs]{tabs->setCurrentIndex(3);});
    connect(controller,&TimelineController::symbolPropertiesRequested,this,[tabs]{tabs->setCurrentIndex(4);});
    connect(controller,&TimelineController::cuePropertiesRequested,this,[tabs]{tabs->setCurrentIndex(5);});
    if(!controller->selectedCueId().isEmpty()){tabs->setCurrentIndex(5);pages->setCurrentIndex(5);}else if(!controller->selectedSymbolClipId().isEmpty()||!controller->selectedSymbolPoseLayerId().isEmpty()){tabs->setCurrentIndex(4);pages->setCurrentIndex(4);}else if(controller->cameraSelected()){tabs->setCurrentIndex(3);pages->setCurrentIndex(3);}else if(!controller->selectedAudioClipId().isEmpty()){tabs->setCurrentIndex(2);pages->setCurrentIndex(2);}else if(!selectedTrack(controller)){tabs->setCurrentIndex(1);pages->setCurrentIndex(1);}
    m_context=new TimelineContext;m_context->setObjectName("frameDrawingContext");m_context->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Fixed);m_context->setFixedHeight(fontMetrics().height());layout->addWidget(m_context);
    m_name=new QLineEdit;m_name->setObjectName("frameDrawingName");m_name->setPlaceholderText("Drawing name");m_name->setAccessibleName("Drawing name");m_name->setMaxLength(100);m_name->installEventFilter(this);layout->addWidget(m_name);
    m_start=new ValueSlider("Exposure start frame",1,1,1);m_start->setObjectName("frameExposureStart");layout->addWidget(new PropertyRow("Start",m_start));
    m_hold=new ValueSlider("Exposure duration in frames",1,1,1);m_hold->setObjectName("frameExposureHold");layout->addWidget(new PropertyRow("Hold",m_hold));
    m_celActions=new QWidget;auto *celActions=new QHBoxLayout(m_celActions);celActions->setContentsMargins(0,0,0,0);celActions->setSpacing(2);
    const auto pickCurrent=[this]{if(m_controller->selectedCels().isEmpty())if(const auto *track=selectedTrack(m_controller))if(const auto *cel=currentExposure(m_controller))m_controller->setSelectedCels({{track->id,cel->id}});};
    for(int count:{1,2,3}){auto *button=timelineButton(m_celActions,qPrintable(QString("frameCelsOn%1").arg(count)),"clapperboard",QString("Set selected cels on %1 · Consecutive %1-frame holds").arg(count));button->setText(QString::number(count));button->setToolButtonStyle(Qt::ToolButtonTextOnly);celActions->addWidget(button);
        connect(button,&QToolButton::clicked,this,[this,pickCurrent,count]{pickCurrent();m_controller->setSelectedCelHolds(count);});}
    auto *repeat=timelineButton(m_celActions,"frameRepeatCels","repeat","Repeat selected cels after their range");celActions->addWidget(repeat);connect(repeat,&QToolButton::clicked,this,[this,pickCurrent]{pickCurrent();m_controller->repeatSelectedCels();});
    auto *reverse=timelineButton(m_celActions,"frameReverseCels","arrow-left-right","Reverse selected cels within their range");celActions->addWidget(reverse);connect(reverse,&QToolButton::clicked,this,[this,pickCurrent]{pickCurrent();m_controller->reverseSelectedCels();});
    celActions->addStretch();layout->addWidget(m_celActions);layout->addStretch();
    connect(m_start,&ValueSlider::editingFinished,this,&FramePropertiesPanel::commitTiming);connect(m_hold,&ValueSlider::editingFinished,this,&FramePropertiesPanel::commitTiming);
    connect(m_hold,&ValueSlider::editingStarted,this,[this]{if(!m_refreshing&&m_multi)m_bulkHoldEdited=true;});
    connect(m_name,&QLineEdit::editingFinished,this,&FramePropertiesPanel::commitName);
    connect(m_name,&QLineEdit::textEdited,this,[this]{if(!m_refreshing)m_controller->setPendingEdits(this, true);});
    for(auto *field:{m_start,m_hold}){
        connect(field,&ValueSlider::editingStarted,this,[this]{if(!m_refreshing)m_controller->setPendingEdits(this, true);});
        connect(field,&ValueSlider::editingCancelled,this,&FramePropertiesPanel::cancelEditors);
    }
    connect(controller,&TimelineController::editResolutionRequested,this,&FramePropertiesPanel::commitEditors);
    connect(controller,&TimelineController::editCancellationRequested,this,&FramePropertiesPanel::cancelEditors);
    connect(controller,&TimelineController::changed,this,&FramePropertiesPanel::refresh);
    connect(controller,&TimelineController::changed,this,[controller,tabs,audioSelected=!controller->selectedAudioClipId().isEmpty()]()mutable{
        const bool selected=!controller->selectedAudioClipId().isEmpty();const bool leftAudio=audioSelected&&!selected;audioSelected=selected;
        if(leftAudio&&tabs->currentIndex()==2)tabs->setCurrentIndex(selectedTrack(controller)?0:1);
    });refresh();
}
void FramePropertiesPanel::refresh(){
    if(m_refreshing)return;const QScopedValueRollback<bool> guard(m_refreshing,true);
    const auto selected=m_controller->selectedCels();
    if(selected.size()>1&&timelineShot(m_controller)){
        const auto &shot=*timelineShot(m_controller);int first=shot.durationFrames,hold=1;bool mixed=false,editable=!m_controller->playing();
        for(int i=0;i<selected.size();++i)if(const auto *cel=celExposure(shot,selected[i])){first=std::min(first,cel->startFrame);if(i==0)hold=cel->durationFrames;else mixed|=hold!=cel->durationFrames;editable&=celEditError(shot,selected[i]).isEmpty();}
        const bool different=!m_multi||selected!=m_displayedCels;m_multi=true;m_displayedCels=selected;m_exposureId.clear();m_drawingId.clear();
        if(different){m_start->cancelEditing();m_hold->cancelEditing();m_bulkHoldEdited=false;if(!m_resolving)m_controller->setPendingEdits(this,false);}
        const QString context=QString("%1 selected cels%2").arg(selected.size()).arg(mixed?" · Mixed holds":"");m_context->setText(context);m_context->setToolTip(context);m_context->setAccessibleName(context);
        m_name->setEnabled(false);m_name->clear();m_name->setPlaceholderText("Multiple drawings");m_displayedName.clear();
        m_start->setEnabled(editable);m_hold->setEnabled(editable);m_celActions->setEnabled(editable);
        if(m_frameCount!=shot.durationFrames){m_frameCount=shot.durationFrames;m_start->setRange(1,m_frameCount);m_hold->setRange(1,m_frameCount);}
        if(different||first+1!=m_displayedStart)m_start->setValueSilently(first+1);if(different||hold!=m_displayedHold)m_hold->setValueSilently(hold);
        m_start->setToolTip("Move all selected cels by changing their earliest start");m_hold->setToolTip(mixed?"Mixed holds · Enter a value to set all selected cels and arrange them consecutively per track":"Set every selected cel's hold and arrange them consecutively per track");
        m_displayedStart=first+1;m_displayedHold=hold;return;
    }
    const bool wasMulti=m_multi;m_multi=false;m_displayedCels=selected;m_bulkHoldEdited=false;m_name->setPlaceholderText("Drawing name");m_start->setToolTip("Exposure start frame");m_hold->setToolTip("Exposure duration in frames");
    const auto *track=selectedTrack(m_controller);
    const auto *root=selectedRoot(m_controller),*drawing=currentDrawing(m_controller);const auto *exposure=currentExposure(m_controller);
    const QString id=exposure?exposure->id:QString();const bool different=wasMulti||id!=m_exposureId;m_exposureId=id;m_drawingId=drawing?drawing->id:QString();
    const QString context=!m_controller->selectedCueId().isEmpty()?"Cue selected":!m_controller->selectedSymbolPoseLayerId().isEmpty()?"Symbol poses selected":root?(track?(exposure?drawingDisplayId(*track,exposure->drawingId)+" · "+root->name:"Blank frame"):"Static track"):"Select a track";m_context->setText(context);m_context->setToolTip(context);m_context->setAccessibleName(context);
    const bool editable=m_controller->selectedCueId().isEmpty()&&m_controller->selectedSymbolPoseLayerId().isEmpty()&&exposure&&track&&root&&!m_controller->playing()&&celEditError(*timelineShot(m_controller),{track->id,exposure->id}).isEmpty();m_name->setEnabled(editable);m_start->setEnabled(editable);m_hold->setEnabled(editable);m_celActions->setEnabled(editable);
    const auto name=drawing?drawing->name:QString();if(different||name!=m_displayedName)m_name->setText(name);m_displayedName=name;
    const int start=exposure?exposure->startFrame+1:1,hold=exposure?exposure->durationFrames:1;
    if(different){m_start->cancelEditing();m_hold->cancelEditing();if(!m_resolving)m_controller->setPendingEdits(this, false);}
    const int count=std::max(1,m_controller->frameCount());if(m_frameCount!=count){m_frameCount=count;m_start->setRange(1,count);m_hold->setRange(1,count);}
    if(different||start!=m_displayedStart)m_start->setValueSilently(start);if(different||hold!=m_displayedHold)m_hold->setValueSilently(hold);
    m_displayedStart=start;m_displayedHold=hold;
}
void FramePropertiesPanel::commitTiming(){
    if(m_refreshing||!m_controller->selectedCueId().isEmpty()||!m_controller->selectedSymbolPoseLayerId().isEmpty())return;
    if(m_multi){
        if(m_displayedCels!=m_controller->selectedCels()){refresh();return;}
        const int delta=qRound(m_start->value())-m_displayedStart,hold=qRound(m_hold->value());const bool editHold=m_bulkHoldEdited;m_bulkHoldEdited=false;
        const QScopedValueRollback<bool> guard(m_resolving,true);bool ok=true;
        if(delta)ok=m_controller->moveSelectedCels(delta);else if(editHold)ok=m_controller->setSelectedCelHolds(hold);
        if(!ok){m_displayedStart=m_displayedHold=-1;refresh();}m_controller->setPendingEdits(this,false);return;
    }
    const auto *exposure=currentExposure(m_controller);if(!exposure||exposure->id!=m_exposureId){refresh();return;}
    const int start=qRound(m_start->value())-1,hold=qRound(m_hold->value());if(start==exposure->startFrame&&hold==exposure->durationFrames){m_controller->setPendingEdits(this, false);return;}
    const QScopedValueRollback<bool> guard(m_resolving,true);
    if(!m_controller->setExposureTiming(start,hold)){m_displayedStart=m_displayedHold=-1;refresh();}
    m_controller->setPendingEdits(this, false);
}
void FramePropertiesPanel::commitName(){
    if(m_refreshing||m_multi||!m_controller->selectedCueId().isEmpty()||!m_controller->selectedSymbolPoseLayerId().isEmpty())return;const auto *drawing=currentDrawing(m_controller);if(!drawing||drawing->id!=m_drawingId){refresh();return;}
    if(m_name->text()==drawing->name){m_controller->setPendingEdits(this, false);return;}const QScopedValueRollback<bool> guard(m_resolving,true);
    m_controller->renameDrawing(m_name->text().trimmed());m_displayedName.clear();refresh();
    m_controller->setPendingEdits(this, false);
}
void FramePropertiesPanel::commitEditors(){
    if(m_resolving)return;const QScopedValueRollback<bool> guard(m_resolving,true);
    {const QScopedValueRollback<bool> update(m_refreshing,true);m_start->commitEditing();m_hold->commitEditing();}
    commitName();commitTiming();
}
void FramePropertiesPanel::cancelEditors(){
    if(m_resolving||m_refreshing)return;const QScopedValueRollback<bool> guard(m_resolving,true);
    {const QScopedValueRollback<bool> update(m_refreshing,true);m_start->cancelEditing();m_hold->cancelEditing();}
    m_bulkHoldEdited=false;m_displayedStart=m_displayedHold=-1;m_displayedName.clear();refresh();m_controller->setPendingEdits(this, false);
}
bool FramePropertiesPanel::eventFilter(QObject *watched,QEvent *event){
    if(watched==m_name&&(event->type()==QEvent::KeyPress||event->type()==QEvent::ShortcutOverride)){
        auto *key=static_cast<QKeyEvent *>(event);if(key->key()==Qt::Key_Escape&&m_controller->hasPendingEdits()){
            if(event->type()==QEvent::KeyPress)cancelEditors();event->accept();return true;}}
    return QWidget::eventFilter(watched,event);
}
} // namespace toon
