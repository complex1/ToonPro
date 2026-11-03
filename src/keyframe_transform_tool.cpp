#include "keyframe_transform_tool.h"
#include "symbols.h"
#include "canvas.h"
#include "mask_tool.h"
#include "camera_tool.h"
#include "property_animation.h"
#include "motion_controls.h"
#include "raster_edit.h"
#include "ui_theme.h"
#include <QMouseEvent>
#include <QTabletEvent>
#include <QKeyEvent>
#include <QLineF>
#include <QPainter>
#include <QScopeGuard>
#include <cmath>
#include <algorithm>
namespace toon {
KeyframeTransformTool::KeyframeTransformTool(Canvas *canvas):QObject(canvas),m_canvas(canvas){
    canvas->installEventFilter(this);
    connect(canvas,&Canvas::toolChanged,this,[this]{setEnabled(false);});
    connect(canvas->document(),&Document::selectionChanged,this,[this]{
        const bool switched=!m_canvas->hasProject()||m_canvas->shot().layers[m_canvas->document()->activeLayer()].id!=m_layerId;
        if(dragging()&&switched)cancel();
        if(switched)m_selectedMotionFrame=-1;
        m_motionDirty=true;m_canvas->update();
    });
    connect(canvas->document(),&Document::frameChanged,this,[this]{
        if(dragging()&&(m_canvas->document()->currentFrame()!=m_canvasFrame||m_canvas->document()->keyframeSourceEditing()))cancel();
        m_canvas->update();
    });
    connect(canvas->document(),&Document::changed,this,[this]{
        m_motionDirty=true;
        m_canvas->update();
    });
    connect(canvas->document(),&Document::loaded,this,[this]{
        m_drag=0;m_tablet=false;m_enabled=false;m_motionDirty=true;m_selectedMotionFrame=-1;m_rasterBounds.clear();emit changed();
    });
}
QString KeyframeTransformTool::unavailableReason()const{
    if(!m_canvas->hasProject())return "Open a project to animate a layer.";
    if(m_canvas->animationPlayback())return "Pause playback to edit keyframes.";
    if(m_canvas->document()->keyframeSourceEditing())return "Leave Source mode to edit animation motion.";
    const auto &shot=m_canvas->shot();const auto index=m_canvas->document()->activeLayer();
    if(index<0||index>=shot.layers.size())return "Select a layer to animate.";
    const auto &layer=shot.layers[index];
    auto reason=propertyAnimationUnavailableReason(shot,layer.id);if(!reason.isEmpty())return reason;
    if(!layerPresentAtFrame(shot,layer.id,m_canvas->document()->currentFrame()))return "This drawing is not exposed at the current frame.";
    if(layerPositionLocked(shot.layers,index))return "Unlock layer and folder positions to animate their transform.";
    QString parent=layer.id;
    for(int depth=0;!parent.isEmpty()&&depth<10;++depth){for(const auto &entry:shot.layers)if(entry.id==parent){if(entry.locked||!entry.visible)return "Show and unlock the layer and its folders to animate it.";parent=entry.parentId;break;}}
    if(m_mode!=Mode::MotionPath&&!layer.parentId.isEmpty()&&!worldLayerTransform(shot,layer.parentId,m_canvas->document()->currentFrame()).isInvertible())return "The parent has zero scale. Change its scale before dragging this layer.";
    if(m_mode==Mode::Pivot&&!worldLayerTransform(shot,layer.id,m_canvas->document()->currentFrame()).isInvertible())return "The layer has zero scale. Change its scale before moving the pivot.";
    return {};
}
void KeyframeTransformTool::setMode(Mode mode){
    if(m_mode==mode)return;finish();m_mode=mode;m_selectedMotionFrame=-1;
    m_canvas->refreshPointerCursor();m_canvas->update();emit changed();
}
void KeyframeTransformTool::setEnabled(bool enabled){
    if(m_enabled==enabled)return;finish();
    if(enabled){m_canvas->maskTool()->setEnabled(false);m_canvas->cameraTool()->setEnabled(false);const auto reason=unavailableReason();if(!reason.isEmpty()){emit m_canvas->document()->warning(reason);return;}m_canvas->document()->clearRasterSelection();}
    m_enabled=enabled;m_canvas->refreshPointerCursor();m_canvas->update();m_canvas->updateSelectionUi();emit changed();
}
QRectF KeyframeTransformTool::contentBounds()const{
    if(!m_canvas->hasProject())return {};
    const auto &shot=m_canvas->shot();const auto &selected=shot.layers[m_canvas->document()->activeLayer()];const int frame=m_canvas->document()->currentFrame();
    QRectF bounds;const auto active=activeLayerIds(shot,frame);const auto inverse=worldLayerTransform(shot,selected.id,frame).inverted();
    for(int i=0;i<shot.layers.size();++i){const auto &layer=shot.layers[i];if(layer.group||!active.contains(layer.id))continue;
        if(layer.id!=selected.id&&!layerIsDescendant(shot.layers,i,selected.id))continue;
        QRectF area;
        if(layer.symbol){
            if(const auto *definition=findSymbol(m_canvas->project(),layer.symbol->symbolId)) {
                const int local=symbolLocalFrame(*layer.symbol,frame,definition->content.durationFrames);
                if(local>=0)area=symbolArtworkBounds(m_canvas->project(),layer.symbol->symbolId,local);
            }
        }
        else if(layerHasEditableObjects(layer)){for(const auto &object:layer.objects)area|=editableObjectBounds(evaluateAnimatedObject(shot,layer.id,object,frame));}
        else {
            // Animated transforms leave source tiles unchanged. Avoid scanning
            // every painted pixel again for each hover, handle and drag frame.
            QMap<QPair<int,int>, qint64> revisions;
            for (auto tile = layer.tiles.cbegin(); tile != layer.tiles.cend(); ++tile)
                revisions.insert(tile.key(), tile->cacheKey());
            const auto cached = m_rasterBounds.constFind(layer.id);
            if (cached != m_rasterBounds.cend() && cached->revisions == revisions) area = cached->bounds;
            else {
                area = selectedPixelBounds(layer);
                if (m_rasterBounds.size() >= 64) m_rasterBounds.clear();
                m_rasterBounds.insert(layer.id, {std::move(revisions), area});
            }
        }
        if(!area.isEmpty())bounds|=(worldLayerTransform(shot,layer.id,frame)*inverse).mapRect(area);
    }
    if(bounds.isEmpty())bounds=QRectF(0,0,m_canvas->project().width,m_canvas->project().height);
    if(bounds.width()<1)bounds.adjust(-.5,0,.5,0);if(bounds.height()<1)bounds.adjust(0,-.5,0,.5);
    return bounds;
}
QPolygonF KeyframeTransformTool::box()const{
    if(!m_canvas->hasProject())return {};
    const auto &layer=m_canvas->shot().layers[m_canvas->document()->activeLayer()];
    return (worldLayerTransform(m_canvas->shot(),layer.id,m_canvas->document()->currentFrame())*m_canvas->artworkViewTransform()).map(QPolygonF(contentBounds()));
}
QPointF KeyframeTransformTool::rotationHandle()const{
    const auto polygon=box();if(polygon.size()<4)return {};
    const auto center=(polygon[0]+polygon[2])/2,top=(polygon[0]+polygon[1])/2;QLineF line(center,top);
    if(line.length()<.1)return top-QPointF(0,28);line.setLength(line.length()+28);return line.p2();
}
QPointF KeyframeTransformTool::pivotHandle()const{
    if(!m_canvas->hasProject())return {};
    const auto *document=m_canvas->document();const auto &shot=m_canvas->shot();const auto &layer=shot.layers[document->activeLayer()];
    return m_canvas->artworkViewTransform().map(animationPivotWorld(m_canvas->project(),shot,layer.id,document->currentFrame()));
}
int KeyframeTransformTool::selectedMotionFrame()const{
    if(m_selectedMotionFrame>=0&&m_canvas->hasProject()){
        const auto &shot=m_canvas->shot();const int index=m_canvas->document()->activeLayer();
        if(index>=0&&index<shot.layers.size()&&motionPositionKeyFrames(shot,shot.layers[index].id).contains(m_selectedMotionFrame))return m_selectedMotionFrame;
    }
    return m_canvas->document()->currentFrame();
}
QVector<KeyframeTransformTool::MotionControlHandle> KeyframeTransformTool::motionControlHandles()const{
    QVector<MotionControlHandle> controls;
    if(!m_enabled||m_mode!=Mode::MotionPath||!m_canvas->hasProject()||m_canvas->animationPlayback()||m_canvas->document()->keyframeSourceEditing())return controls;
    const auto &shot=m_canvas->shot();const int index=m_canvas->document()->activeLayer();
    if(index<0||index>=shot.layers.size())return controls;
    const auto &layer=shot.layers[index];const auto *channel=propertyChannel(shot,layer.id,KeyframeProperty::PositionX);
    if(!channel)return controls;
    const int frame=selectedMotionFrame();
    const auto key=std::lower_bound(channel->keys.cbegin(),channel->keys.cend(),frame,[](const auto &key,int frame){return key.frame<frame;});
    if(key==channel->keys.cend()||key->frame!=frame)return controls;
    const auto parent=layer.parentId.isEmpty()?QTransform():worldLayerTransform(shot,layer.parentId,frame);
    const auto view=m_canvas->artworkViewTransform();
    const auto anchor=effectiveAnimationPivot(m_canvas->project(),shot,layer.id,frame)
        +QPointF(evaluateProperty(shot,layer.id,KeyframeProperty::PositionX,frame),evaluateProperty(shot,layer.id,KeyframeProperty::PositionY,frame));
    const auto append=[&](bool incoming,QPointF offset){controls.append({frame,incoming,view.map(parent.map(anchor)),view.map(parent.map(anchor+offset))});};
    if(key!=channel->keys.cbegin()&&(key-1)->spatial&&(key-1)->spatial->curved)
        append(true,key->spatial?key->spatial->incoming:QPointF());
    if(key+1!=channel->keys.cend()&&key->spatial&&key->spatial->curved)
        append(false,key->spatial->outgoing);
    return controls;
}
void KeyframeTransformTool::refreshMotionGeometry()const{
    if(!m_motionDirty)return;m_motionDirty=false;m_motionPath={};m_motionMarkers.clear();
    if(!m_canvas->hasProject())return;
    const auto &shot=m_canvas->shot();const auto &layer=shot.layers[m_canvas->document()->activeLayer()];
    const QSizeF size(m_canvas->project().width,m_canvas->project().height);
    m_motionPath=buildMotionPath(shot,layer.id,size,2048).path;
    for(const auto &sample:motionPositionKeySamples(shot,layer.id,size))m_motionMarkers.append({int(sample.frame),sample.position});
}
QString KeyframeTransformTool::gestureLabel()const{
    if(m_mode==Mode::MotionPath)return m_drag>=6?"Shape motion path":"Edit motion path position";
    if(m_mode==Mode::Pivot)return "Move animation pivot";
    return "Transform animation keyframes";
}
void KeyframeTransformTool::paint(QPainter&p)const{
    if(!m_enabled||!m_canvas->hasProject()||m_canvas->animationPlayback()||m_canvas->document()->keyframeSourceEditing())return;
    if(m_mode==Mode::Pivot){
        const auto point=pivotHandle();p.save();p.setRenderHint(QPainter::Antialiasing);
        p.setPen(QPen(QColor(ui::focus),1.5));p.setBrush(QColor(ui::panel));p.drawEllipse(point,7,7);
        p.drawLine(point-QPointF(13,0),point+QPointF(13,0));p.drawLine(point-QPointF(0,13),point+QPointF(0,13));p.restore();return;
    }
    if(m_mode==Mode::MotionPath){
        refreshMotionGeometry();const auto view=m_canvas->artworkViewTransform();const int current=m_canvas->document()->currentFrame();
        p.save();p.setRenderHint(QPainter::Antialiasing);p.setPen(QPen(QColor(ui::focus),1.5));p.setBrush(Qt::NoBrush);p.drawPath(view.map(m_motionPath));
        const QRectF visible=m_canvas->rect().adjusted(-20,-20,20,20);QPointF lastLabel(-1e9,-1e9);
        const auto label=[&](QPointF point,int frame){const auto text=QString::number(frame+1);const auto area=p.fontMetrics().boundingRect(text).adjusted(-3,-2,3,2);const QRectF rect(point+QPointF(9,-19),area.size());p.fillRect(rect,QColor(ui::panel));p.setPen(QColor(ui::text));p.drawText(rect,Qt::AlignCenter,text);};
        for(const auto &control:motionControlHandles()){
            p.setPen(QPen(QColor(ui::muted),1));p.setBrush(Qt::NoBrush);p.drawLine(control.anchor,control.position);
            const bool dragging=(m_drag==6&&control.incoming)||(m_drag==7&&!control.incoming);
            p.setPen(QPen(QColor(ui::focus),1.5));p.setBrush(QColor(dragging?ui::accent:ui::panel));p.drawEllipse(control.position,5,5);
        }
        for(const auto &marker:m_motionMarkers){
            const auto point=view.map(marker.position);if(!visible.contains(point))continue;
            const bool selected=marker.frame==m_selectedMotionFrame;
            p.setPen(QPen(QColor(selected?ui::accent:ui::focus),1.5));p.setBrush(QColor(selected?ui::accent:ui::panel));p.drawRect(QRectF(point-QPointF(4,4),QSizeF(8,8)));
            if(marker.frame!=current&&(selected||QLineF(lastLabel,point).length()>32)){label(point,marker.frame);lastLabel=point;}
        }
        const auto currentPoint=pivotHandle();p.setPen(QPen(QColor(ui::text),1.5));p.setBrush(QColor(ui::accent));
        p.drawPolygon(QPolygonF{currentPoint-QPointF(0,6),currentPoint+QPointF(6,0),currentPoint+QPointF(0,6),currentPoint-QPointF(6,0)});
        label(currentPoint,current);p.restore();return;
    }
    const auto polygon=box();if(polygon.size()<4)return;p.save();p.setRenderHint(QPainter::Antialiasing);p.setBrush(Qt::NoBrush);p.setPen(QPen(QColor(ui::focus),1.5));p.drawPolygon(polygon);
    const auto handle=rotationHandle();p.drawLine((polygon[0]+polygon[1])/2,handle);p.setBrush(QColor(ui::panel));
    for(int i=0;i<4;++i)p.drawRect(QRectF(polygon[i]-QPointF(4,4),QSizeF(8,8)));p.drawEllipse(handle,5,5);p.restore();
}
bool KeyframeTransformTool::press(QPointF view,Qt::KeyboardModifiers modifiers){
    const auto reason=unavailableReason();if(!reason.isEmpty()){emit m_canvas->actionBlocked(reason);return true;}
    bool tangentIsLocal=false;
    m_drag=0;m_moved=false;auto *document=m_canvas->document();m_dragFrame=m_canvasFrame=document->currentFrame();
    if(m_mode==Mode::Pivot){if(QLineF(view,pivotHandle()).length()<16)m_drag=5;}
    else if(m_mode==Mode::MotionPath){
        refreshMotionGeometry();qreal closest=12;const auto transform=m_canvas->artworkViewTransform();
        const qreal currentDistance=QLineF(view,pivotHandle()).length();if(currentDistance<closest){closest=currentDistance;m_drag=4;}
        for(const auto &marker:m_motionMarkers){const qreal distance=QLineF(view,transform.map(marker.position)).length();
            if(distance<closest-.01){closest=distance;m_dragFrame=marker.frame;m_drag=4;}}
        // Markers own the centre when a tangent is collapsed onto its key.
        // Otherwise the circular controls have priority over nearby path lines.
        for(const auto &control:motionControlHandles()){
            if(QLineF(control.anchor,control.position).length()<7)continue;
            const qreal distance=QLineF(view,control.position).length();
            if(distance<closest-.01){
                closest=distance;m_dragFrame=control.frame;m_drag=control.incoming?6:7;
                m_tangentStart=control.position;
            }
        }
        if(m_drag==4&&(modifiers&Qt::AltModifier)){
            const auto &shot=m_canvas->shot();const auto &layer=shot.layers[document->activeLayer()];
            if(const auto *channel=propertyChannel(shot,layer.id,KeyframeProperty::PositionX)){
                for(int i=0;i<channel->keys.size();++i){
                    const auto &key=channel->keys[i];if(key.frame!=m_dragFrame)continue;
                    const bool incoming=modifiers&Qt::ShiftModifier;
                    const bool available=incoming?(i>0&&channel->keys[i-1].spatial&&channel->keys[i-1].spatial->curved)
                        :(i+1<channel->keys.size()&&key.spatial&&key.spatial->curved);
                    if(available){m_drag=incoming?6:7;m_tangentStart=key.spatial?(incoming?key.spatial->incoming:key.spatial->outgoing):QPointF();tangentIsLocal=true;}
                    break;
                }
            }
        }
    }else{
        const auto polygon=box();
        if(QLineF(view,rotationHandle()).length()<12)m_drag=2;
        for(int i=0;i<4&&!m_drag;++i)if(QLineF(view,polygon[i]).length()<12)m_drag=3;
        if(!m_drag&&polygon.containsPoint(view,Qt::OddEvenFill))m_drag=1;
    }
    if(!m_drag)return true;
    const auto &shot=m_canvas->shot();const auto &layer=shot.layers[document->activeLayer()];m_layerId=layer.id;
    const auto parent=layer.parentId.isEmpty()?QTransform():worldLayerTransform(shot,layer.parentId,m_dragFrame);
    bool invertible=false;m_parentInverse=parent.inverted(&invertible);
    if(!invertible){m_drag=0;emit m_canvas->actionBlocked("The parent has zero scale at this keyframe. Change its scale before dragging this position.");return true;}
    m_start=m_parentInverse.map(m_canvas->artworkPoint(view));
    for(int i=0;i<6;++i)m_values[i]=evaluateProperty(shot,layer.id,KeyframeProperty(i),m_dragFrame);
    m_pivot=effectiveAnimationPivot(m_canvas->project(),shot,layer.id,m_canvas->document()->currentFrame());
    m_transformRotation=m_values[4];
    if(const auto *animation=layerAnimation(shot,layer.id);animation&&animation->orientAlongPath){
        const auto direction=animatedPositionDirection(*animation,m_dragFrame);
        if(std::hypot(direction.x(),direction.y())>1e-9)m_transformRotation+=std::atan2(direction.y(),direction.x())*180/M_PI;
    }
    if(m_mode==Mode::Pivot){m_sourceInverse=worldLayerTransform(shot,layer.id,m_dragFrame).inverted(&invertible);if(!invertible){m_drag=0;emit m_canvas->actionBlocked("Change the layer's zero scale before moving its pivot.");return true;}m_start=m_sourceInverse.map(m_canvas->artworkPoint(view));}
    if(m_drag>=6&&!tangentIsLocal)m_tangentStart=m_parentInverse.map(m_canvas->artworkPoint(m_tangentStart))-m_pivot-QPointF(m_values[0],m_values[1]);
    const auto fromPivot=m_start-m_pivot-QPointF(m_values[0],m_values[1]);
    m_lastAngle=std::atan2(fromPivot.y(),fromPivot.x());m_rotationDelta=0;
    if(!document->beginKeyframeEdit(layer.id,gestureLabel())){m_drag=0;return true;}
    if(m_mode==Mode::MotionPath)m_selectedMotionFrame=m_dragFrame;
    m_canvas->setFocus();emit changed();return true;
}
void KeyframeTransformTool::move(QPointF view,bool proportional){
    if(!dragging())return;auto*document=m_canvas->document();if(!document->keyframeEditActive()){m_drag=0;return;}
    if(document->currentFrame()!=m_canvasFrame||document->keyframeSourceEditing()||m_canvas->animationPlayback()){cancel();return;}
    const auto point=(m_drag==5?m_sourceInverse:m_parentInverse).map(m_canvas->artworkPoint(view));const auto delta=point-m_start;
    const auto restoreStart=[&]{
        if(m_moved){
            document->cancelKeyframeEdit();
            if(!document->beginKeyframeEdit(m_layerId,gestureLabel()))m_drag=0;
            m_moved=false;
        }
    };
    if(m_drag!=2&&QLineF(point,m_start).length()<.001){
        restoreStart();
        return;
    }
    m_moved=true;
    using P=KeyframeProperty;
    if(m_drag>=6)document->previewKeyframeMotionHandle(m_dragFrame,m_drag==6,
        {qBound(-32768.,m_tangentStart.x()+delta.x(),32768.),qBound(-32768.,m_tangentStart.y()+delta.y(),32768.)});
    else if(m_drag==5)document->previewKeyframePivot({qBound(-32768.,m_pivot.x()+delta.x(),32768.),qBound(-32768.,m_pivot.y()+delta.y(),32768.)});
    else if(m_drag==4)document->previewKeyframePosition({qBound(-32768.,m_values[0]+delta.x(),32768.),qBound(-32768.,m_values[1]+delta.y(),32768.)},m_dragFrame);
    else if(m_drag==1)document->previewKeyframePosition({qBound(-32768.,m_values[0]+delta.x(),32768.),qBound(-32768.,m_values[1]+delta.y(),32768.)});
    else {
        const auto center=m_pivot+QPointF(m_values[0],m_values[1]);
        if(m_drag==2){
            const auto b=point-center;const qreal angle=std::atan2(b.y(),b.x());
            m_rotationDelta+=std::remainder(angle-m_lastAngle,2*M_PI)*180/M_PI;m_lastAngle=angle;
            if(qAbs(m_rotationDelta)<.001){restoreStart();return;}
            document->previewKeyframeValue(P::Rotation,qBound(-36000.,m_values[4]+m_rotationDelta,36000.));
        }
        else {QTransform rotation;rotation.rotate(-m_transformRotation);const auto a=rotation.map(m_start-center),b=rotation.map(point-center);
            qreal x=qAbs(a.x())>.001?b.x()/a.x():1,y=qAbs(a.y())>.001?b.y()/a.y():1;if(proportional){const qreal scale=qAbs(x-1)>qAbs(y-1)?x:y;x=y=scale;}
            document->previewKeyframeValue(P::ScaleX,qBound(-100.,m_values[2]*x,100.));document->previewKeyframeValue(P::ScaleY,qBound(-100.,m_values[3]*y,100.));}
    }m_canvas->update();
}
void KeyframeTransformTool::finish(){m_tablet=false;if(!dragging())return;m_drag=0;m_canvas->document()->commitKeyframeEdit();emit changed();}
void KeyframeTransformTool::cancel(){m_tablet=false;if(!dragging())return;m_drag=0;m_canvas->document()->cancelKeyframeEdit();emit changed();m_canvas->update();}
bool KeyframeTransformTool::eventFilter(QObject *watched,QEvent*event){
    if(!m_enabled || watched != m_canvas)return false;
    if(event->type()==QEvent::FocusOut){finish();return false;}
    if(event->type()==QEvent::KeyPress){auto*k=static_cast<QKeyEvent*>(event);if(k->key()==Qt::Key_Escape){if(dragging())cancel();else setEnabled(false);return true;}return false;}
    if(m_canvas->m_space||m_canvas->m_dragging)return false;
    const auto *tablet = event->type() == QEvent::TabletPress || event->type() == QEvent::TabletMove
        || event->type() == QEvent::TabletRelease ? static_cast<QTabletEvent *>(event) : nullptr;
    // This filter consumes pen events before Canvas::tabletEvent runs.
    const auto syncCursor = qScopeGuard([this, tablet] {
        if (tablet) m_canvas->syncTabletCursor(tablet->position());
    });
    if(event->type()==QEvent::MouseButtonPress){auto*e=static_cast<QMouseEvent*>(event);if(e->button()!=Qt::LeftButton)return false;if(m_tablet||e->source()!=Qt::MouseEventNotSynthesized)return true;return press(e->position(),e->modifiers());}
    if(event->type()==QEvent::MouseMove){auto*e=static_cast<QMouseEvent*>(event);if(m_tablet||e->source()!=Qt::MouseEventNotSynthesized)return true;if(dragging()){if(e->buttons()&Qt::LeftButton)move(e->position(),e->modifiers()&Qt::ShiftModifier);else finish();}return true;}
    if(event->type()==QEvent::MouseButtonRelease){auto*e=static_cast<QMouseEvent*>(event);if(e->button()!=Qt::LeftButton)return false;if(m_tablet||e->source()!=Qt::MouseEventNotSynthesized)return true;move(e->position(),e->modifiers()&Qt::ShiftModifier);finish();return true;}
    if(event->type()==QEvent::TabletPress){auto*e=static_cast<QTabletEvent*>(event);if(e->button()!=Qt::LeftButton)return true;m_tablet=true;press(e->position(),e->modifiers());event->accept();return true;}
    if(event->type()==QEvent::TabletMove){auto*e=static_cast<QTabletEvent*>(event);if(m_tablet){if(e->buttons()&Qt::LeftButton)move(e->position(),e->modifiers()&Qt::ShiftModifier);else finish();}event->accept();return true;}
    if(event->type()==QEvent::TabletRelease){auto*e=static_cast<QTabletEvent*>(event);if(m_tablet){move(e->position(),e->modifiers()&Qt::ShiftModifier);finish();}event->accept();return true;}
    return false;
}
}
