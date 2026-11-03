#include "document.h"
#include "motion_controls.h"
#include "motion_edit.h"
#include <algorithm>
#include <cmath>

namespace toon {
namespace {
int indexOf(const Shot&s,const QString&id){for(int i=0;i<s.layers.size();++i)if(s.layers[i].id==id)return i;return -1;}
QString editable(const Document*d,const QString&id,bool position,bool gesture=false){
    if(!d->isOpen())return "Open a project before animating layers.";
    if((d->editingGestureActive()&&!(gesture&&d->keyframeEditActive()))||d->layerPropertyEditActive())return "Finish the current edit before changing keyframes.";
    const auto&s=d->activeShot();const auto reason=propertyAnimationUnavailableReason(s,id);if(!reason.isEmpty())return reason;
    if(!layerPresentAtFrame(s,id,d->currentFrame()))return "This drawing is not exposed at the current frame.";
    int index=indexOf(s,id);const int target=index;
    while(index>=0){if(s.layers[index].locked)return "Unlock this layer and its folders before editing keyframes.";if(s.layers[index].parentId.isEmpty())break;index=indexOf(s,s.layers[index].parentId);}
    if(position&&layerPositionLocked(s.layers,target))return "Unlock the layer position before editing motion or path keys.";
    return {};
}
LayerAnimation &ensureAnimation(const Project &project,Shot&s,const QString&id,int frame){if(auto*a=layerAnimation(s,id))return *a;s.layerAnimations.append({id,defaultAnimationPivot(project,s,id,frame),{},{}});return s.layerAnimations.last();}
PropertyChannel *channel(Shot&s,const QString&id,KeyframeProperty p){if(auto*a=layerAnimation(s,id))for(auto&c:a->channels)if(c.property==p)return &c;return nullptr;}
PathAnimation *pathChannel(Shot&s,const QString&id,const QString&object){if(auto*a=layerAnimation(s,id))for(auto&p:a->paths)if(p.objectId==object)return &p;return nullptr;}
template<class Key> bool move(QVector<Key>&keys,int from,int to,int duration){if(from==to)return true;if(to<0||to>=duration)return false;int index=-1;for(int i=0;i<keys.size();++i){if(keys[i].frame==to)return false;if(keys[i].frame==from)index=i;}if(index<0)return false;keys[index].frame=to;std::sort(keys.begin(),keys.end(),[](const auto&a,const auto&b){return a.frame<b.frame;});return true;}
template<class Key> bool remove(QVector<Key>&keys,int frame){const auto size=keys.size();keys.erase(std::remove_if(keys.begin(),keys.end(),[&](const auto&k){return k.frame==frame;}),keys.end());return size!=keys.size();}
template<class Key> bool interpolation(QVector<Key>&keys,int frame,KeyframeInterpolation value){if(keyframeInterpolationKey(value).isEmpty())return false;for(auto&k:keys)if(k.frame==frame){k.interpolation=value;k.easing.reset();return true;}return false;}
void clean(Shot&s){for(auto&a:s.layerAnimations){a.channels.erase(std::remove_if(a.channels.begin(),a.channels.end(),[](const auto&c){return c.keys.isEmpty();}),a.channels.end());a.paths.erase(std::remove_if(a.paths.begin(),a.paths.end(),[](const auto&p){return p.keys.isEmpty();}),a.paths.end());}prunePropertyAnimation(s);}
}
void Document::setKeyframeSourceEditing(bool enabled){if(m_keyframeSourceEditing==enabled||editingGestureActive()||layerPropertyEditActive())return;m_keyframeSourceEditing=enabled;m_selection={};emit frameChanged();emit selectionChanged();emit rasterSelectionChanged();}
bool Document::enableKeyframeChannel(const QString&id,KeyframeProperty property,bool enabled){const auto reason=editable(this,id,keyframePropertyNeedsPositionUnlock(property));if(!reason.isEmpty()){emit warning(reason);return false;}const auto propertyReason=keyframePropertyUnavailableReason(activeShot(),id,property);if(!propertyReason.isEmpty()){emit warning(propertyReason);return false;}auto p=m_project;auto&s=p.shots[activeShotIndex()];if(enabled){if(propertyChannel(s,id,property))return true;auto&animation=ensureAnimation(p,s,id,m_currentFrame);const auto source=keyframeSourceValue(s.layers[indexOf(s,id)],property);const auto mode=keyframePropertyIsDiscrete(property)?KeyframeInterpolation::Hold:KeyframeInterpolation::Linear;PropertyChannel c{property,{{0,source,mode}}};if(m_currentFrame>0)c.keys.append({m_currentFrame,source,mode});animation.channels.append(c);}else{if(!propertyChannel(s,id,property))return true;auto*a=layerAnimation(s,id);if(!a)return true;
    if(hasSpatialMotion(*a) && (property==KeyframeProperty::PositionX || property==KeyframeProperty::PositionY)) {
        a->channels.removeIf([](const auto &c){return c.property==KeyframeProperty::PositionX || c.property==KeyframeProperty::PositionY;});
        a->orientAlongPath=false;clean(s);return commit(m_project,p,"Disable animated position",id);
    }
    a->channels.erase(std::remove_if(a->channels.begin(),a->channels.end(),[&](const auto&c){return c.property==property;}),a->channels.end());clean(s);}return commit(m_project,p,enabled?"Enable animated property":"Disable animated property",id);}
bool Document::setKeyframeValue(const QString&id,KeyframeProperty property,qreal value){const auto reason=editable(this,id,keyframePropertyNeedsPositionUnlock(property));if(!reason.isEmpty()){emit warning(reason);return false;}auto p=m_project;QString error;if(!writePropertyKeyframe(p,p.shots[activeShotIndex()],id,property,m_currentFrame,value,&error)){emit warning(error);return false;}if(p.shots[activeShotIndex()].layerAnimations==m_project.shots[activeShotIndex()].layerAnimations)return true;return commit(m_project,p,"Set property keyframe",id);}
bool Document::removeKeyframe(const QString&id,KeyframeProperty property,int frame){const auto reason=editable(this,id,keyframePropertyNeedsPositionUnlock(property));if(!reason.isEmpty()){emit warning(reason);return false;}auto p=m_project;auto&s=p.shots[activeShotIndex()];auto*c=channel(s,id,property);if(!c||!remove(c->keys,frame))return false;
    if(const auto *a=layerAnimation(m_project.shots[activeShotIndex()],id);a && hasSpatialMotion(*a) && (property==KeyframeProperty::PositionX || property==KeyframeProperty::PositionY)) {
        const auto other=property==KeyframeProperty::PositionX?KeyframeProperty::PositionY:KeyframeProperty::PositionX;
        if(auto *paired=channel(s,id,other))remove(paired->keys,frame);
    }
    clean(s);return commit(m_project,p,"Remove property keyframe",id);}
bool Document::moveKeyframe(const QString&id,KeyframeProperty property,int from,int to){const auto reason=editable(this,id,keyframePropertyNeedsPositionUnlock(property));if(!reason.isEmpty()){emit warning(reason);return false;}auto p=m_project;auto&s=p.shots[activeShotIndex()];auto*c=channel(s,id,property);if(!c||!move(c->keys,from,to,s.durationFrames)){emit warning("Choose an empty frame inside the shot for this key.");return false;}if(from==to)return true;
    if(const auto *a=layerAnimation(s,id);a && hasSpatialMotion(*a) && (property==KeyframeProperty::PositionX || property==KeyframeProperty::PositionY)) {
        const auto other=property==KeyframeProperty::PositionX?KeyframeProperty::PositionY:KeyframeProperty::PositionX;
        auto *paired=channel(s,id,other);if(!paired||!move(paired->keys,from,to,s.durationFrames))return false;
    }
    return commit(m_project,p,"Move property keyframe",id);}
bool Document::setKeyframeInterpolation(const QString&id,KeyframeProperty property,int frame,KeyframeInterpolation value){if(keyframePropertyIsDiscrete(property)&&value!=KeyframeInterpolation::Hold){emit warning("Enable and inversion keys use Hold interpolation.");return false;}const auto reason=editable(this,id,keyframePropertyNeedsPositionUnlock(property));if(!reason.isEmpty()){emit warning(reason);return false;}auto p=m_project;auto*c=channel(p.shots[activeShotIndex()],id,property);if(!c||!interpolation(c->keys,frame,value))return false;
    if(const auto *a=layerAnimation(p.shots[activeShotIndex()],id);a && hasSpatialMotion(*a) && (property==KeyframeProperty::PositionX || property==KeyframeProperty::PositionY)) {
        const auto other=property==KeyframeProperty::PositionX?KeyframeProperty::PositionY:KeyframeProperty::PositionX;
        if(auto *paired=channel(p.shots[activeShotIndex()],id,other))interpolation(paired->keys,frame,value);
    }
    if(p.shots[activeShotIndex()].layerAnimations==m_project.shots[activeShotIndex()].layerAnimations)return true;return commit(m_project,p,"Change keyframe easing",id);}
bool Document::setKeyframePivot(const QString&id,QPointF pivot){const auto reason=editable(this,id,true);if(!reason.isEmpty()){emit warning(reason);return false;}if(!std::isfinite(pivot.x())||!std::isfinite(pivot.y())||qAbs(pivot.x())>32768||qAbs(pivot.y())>32768)return false;auto p=m_project;auto*a=layerAnimation(p.shots[activeShotIndex()],id);if(!a)return false;if(a->pivot==pivot)return true;a->pivot=pivot;return commit(m_project,p,"Change animation pivot",id);}
bool Document::enablePathAnimation(const QString&id,const QString&objectId,bool enabled){const auto reason=editable(this,id,true);if(!reason.isEmpty()){emit warning(reason);return false;}auto p=m_project;auto&s=p.shots[activeShotIndex()];const int index=indexOf(s,id);const auto&layer=s.layers[index];const EditableObject*object=nullptr;for(const auto&o:layer.objects)if(o.id==objectId)object=&o;if(!object||object->kind!=ObjectKind::Path||layer.kind!=LayerKind::Vector){emit warning("Select a vector path to animate its shape.");return false;}if(enabled){if(pathAnimation(s,id,objectId))return true;PathAnimation path{objectId,{{0,pathPose(*object)}}};if(m_currentFrame>0)path.keys.append({m_currentFrame,pathPose(*object)});ensureAnimation(p,s,id,m_currentFrame).paths.append(path);}else{if(!pathAnimation(s,id,objectId))return true;auto*a=layerAnimation(s,id);if(!a)return true;a->paths.erase(std::remove_if(a->paths.begin(),a->paths.end(),[&](const auto&path){return path.objectId==objectId;}),a->paths.end());clean(s);}return commit(m_project,p,enabled?"Enable path animation":"Disable path animation",id);}
bool Document::setPathKeyframe(const QString&id,const QString&objectId,const EditableObject&pose){const auto reason=editable(this,id,true);if(!reason.isEmpty()){emit warning(reason);return false;}if(m_keyframeSourceEditing){emit warning("Leave Source mode before changing animated path poses.");return false;}auto p=m_project;QString error;if(!writePathKeyframe(p.shots[activeShotIndex()],id,objectId,m_currentFrame,pose,&error)){emit warning(error);return false;}if(p.shots[activeShotIndex()].layerAnimations==m_project.shots[activeShotIndex()].layerAnimations)return true;
    const auto &shot=p.shots[activeShotIndex()];auto trial=shot.layers[indexOf(shot,id)];
    for(auto &object:trial.objects)object=evaluateAnimatedObject(shot,id,object,m_currentFrame);
    const int otherTiles=projectTileCount(p,id);
    const int overscan=shot.sheet.overscan;
    if(!rebuildObjectLayer(trial,QRectF(-overscan,-overscan,p.width+2*overscan,p.height+2*overscan),maxProjectTiles-otherTiles,&error)){emit warning(error);return false;}
    return commit(m_project,p,"Set path keyframe",id);}
bool Document::removePathKeyframe(const QString&id,const QString&objectId,int frame){const auto reason=editable(this,id,true);if(!reason.isEmpty()){emit warning(reason);return false;}auto p=m_project;auto&s=p.shots[activeShotIndex()];auto*path=pathChannel(s,id,objectId);if(!path||!remove(path->keys,frame))return false;clean(s);return commit(m_project,p,"Remove path keyframe",id);}
bool Document::movePathKeyframe(const QString&id,const QString&objectId,int from,int to){const auto reason=editable(this,id,true);if(!reason.isEmpty()){emit warning(reason);return false;}auto p=m_project;auto&s=p.shots[activeShotIndex()];auto*path=pathChannel(s,id,objectId);if(!path||!move(path->keys,from,to,s.durationFrames)){emit warning("Choose an empty frame inside the shot for this key.");return false;}if(from==to)return true;return commit(m_project,p,"Move path keyframe",id);}
bool Document::setPathKeyframeInterpolation(const QString&id,const QString&objectId,int frame,KeyframeInterpolation value){const auto reason=editable(this,id,true);if(!reason.isEmpty()){emit warning(reason);return false;}auto p=m_project;auto*path=pathChannel(p.shots[activeShotIndex()],id,objectId);if(!path||!interpolation(path->keys,frame,value))return false;if(p.shots[activeShotIndex()].layerAnimations==m_project.shots[activeShotIndex()].layerAnimations)return true;return commit(m_project,p,"Change path easing",id);}
bool Document::beginKeyframeEdit(const QString&id,const QString&label){const auto reason=editable(this,id,false);if(!reason.isEmpty()){emit warning(reason);return false;}m_beforeKeyframeEdit=m_project;m_keyframeEditLayerId=id;m_keyframeEditLabel=label;return true;}
bool Document::previewKeyframeValue(KeyframeProperty property,qreal value){if(!m_beforeKeyframeEdit)return false;const auto reason=editable(this,m_keyframeEditLayerId,keyframePropertyNeedsPositionUnlock(property),true);if(!reason.isEmpty()){emit warning(reason);return false;}auto p=m_project;if(!writePropertyKeyframe(p,p.shots[activeShotIndex()],m_keyframeEditLayerId,property,m_currentFrame,value))return false;const auto error=validatePropertyAnimation(p.shots[activeShotIndex()]);if(!error.isEmpty()){emit warning(error);return false;}if(p.shots[activeShotIndex()].layerAnimations==m_project.shots[activeShotIndex()].layerAnimations)return true;m_project=std::move(p);emit changed();return true;}
bool Document::previewKeyframePosition(QPointF position, int frame) {
    if (!m_beforeKeyframeEdit) return false;
    const auto reason = editable(this, m_keyframeEditLayerId, true, true);
    if (!reason.isEmpty()) { emit warning(reason); return false; }
    if (frame == -1) frame = m_currentFrame;
    if (frame < 0 || frame >= m_project.shots[activeShotIndex()].durationFrames) {
        emit warning("The position key must stay inside the shot."); return false;
    }
    if (!validKeyframeValue(KeyframeProperty::PositionX, position.x())
        || !validKeyframeValue(KeyframeProperty::PositionY, position.y())) {
        emit warning("This animation position is outside its supported range."); return false;
    }
    auto project = m_project;
    auto &shot = project.shots[activeShotIndex()];
    if (!writePropertyKeyframe(project, shot, m_keyframeEditLayerId, KeyframeProperty::PositionX, frame, position.x())
        || !writePropertyKeyframe(project, shot, m_keyframeEditLayerId, KeyframeProperty::PositionY, frame, position.y())) return false;
    const auto error = validatePropertyAnimation(shot);
    if (!error.isEmpty()) { emit warning(error); return false; }
    if (shot.layerAnimations == m_project.shots[activeShotIndex()].layerAnimations) return true;
    m_project = std::move(project); emit changed(); return true;
}
bool Document::previewKeyframePivot(QPointF pivot) {
    if (!m_beforeKeyframeEdit) return false;
    const auto reason = editable(this, m_keyframeEditLayerId, true, true);
    if (!reason.isEmpty()) { emit warning(reason); return false; }
    if (!std::isfinite(pivot.x()) || !std::isfinite(pivot.y())
        || qAbs(pivot.x()) > 32768 || qAbs(pivot.y()) > 32768) {
        emit warning("The animation pivot must stay within the supported coordinate range."); return false;
    }
    // Rebase every preview on the gesture's original data. This avoids drift,
    // preserves easing and makes a drag back to its origin an exact no-op.
    auto project = *m_beforeKeyframeEdit;
    auto &shot = project.shots[activeShotIndex()];
    const QPointF originalPivot = effectiveAnimationPivot(project, shot, m_keyframeEditLayerId, m_currentFrame);
    if (pivot != originalPivot) {
        const auto transform = evaluateLayerTransform(shot, m_keyframeEditLayerId, m_currentFrame);
        const QPointF delta = pivot - originalPivot;
        const QPointF compensation(transform.m11() * delta.x() + transform.m21() * delta.y() - delta.x(),
                                   transform.m12() * delta.x() + transform.m22() * delta.y() - delta.y());
        auto &animation = ensureAnimation(project, shot, m_keyframeEditLayerId, m_currentFrame);
        animation.pivot = pivot;
        for (const auto property : {KeyframeProperty::PositionX, KeyframeProperty::PositionY}) {
            const qreal offset = property == KeyframeProperty::PositionX ? compensation.x() : compensation.y();
            auto *values = channel(shot, m_keyframeEditLayerId, property);
            if (values) {
                for (auto &key : values->keys) {
                    if (!validKeyframeValue(property, key.value + offset)) {
                        emit warning("Moving the pivot would put a position key outside its supported range."); return false;
                    }
                    key.value += offset;
                }
            } else if (offset != 0) {
                if (!validKeyframeValue(property, offset)) {
                    emit warning("Moving the pivot would put a position key outside its supported range."); return false;
                }
                animation.channels.append({property, {{0, offset}}});
            }
        }
        // Format 14 stores pivots on animations. A constant neutral position
        // channel gives an otherwise unanimated layer somewhere to retain it.
        if (animation.channels.isEmpty() && animation.paths.isEmpty())
            animation.channels.append({KeyframeProperty::PositionX, {{0, 0}}});
    }
    const auto error = validatePropertyAnimation(shot);
    if (!error.isEmpty()) { emit warning(error); return false; }
    if (shot.layerAnimations == m_project.shots[activeShotIndex()].layerAnimations) return true;
    m_project = std::move(project); emit changed(); return true;
}
bool Document::previewKeyframeEasing(KeyframeProperty property, int frame, const std::optional<CubicEasing> &easing, const QString &objectId) {
    if(!m_beforeKeyframeEdit || (easing && !validCubicEasing(*easing)))return false;
    if(objectId.isEmpty() && keyframePropertyIsDiscrete(property) && easing){emit warning("Enable and inversion keys use Hold interpolation.");return false;}
    const auto reason=editable(this,m_keyframeEditLayerId,!objectId.isEmpty() || keyframePropertyNeedsPositionUnlock(property),true);
    if(!reason.isEmpty()){emit warning(reason);return false;}
    auto project=m_project;auto &shot=project.shots[activeShotIndex()];bool found=false;
    const auto update=[&](auto &keys){for(auto &key:keys)if(key.frame==frame){key.easing=easing;found=true;break;}};
    if(!objectId.isEmpty()) {if(auto *path=pathChannel(shot,m_keyframeEditLayerId,objectId))update(path->keys);}
    else {
        if(auto *values=channel(shot,m_keyframeEditLayerId,property))update(values->keys);
        if(const auto *a=layerAnimation(shot,m_keyframeEditLayerId);a && hasSpatialMotion(*a) && (property==KeyframeProperty::PositionX || property==KeyframeProperty::PositionY)) {
            const auto other=property==KeyframeProperty::PositionX?KeyframeProperty::PositionY:KeyframeProperty::PositionX;
            if(auto *values=channel(shot,m_keyframeEditLayerId,other))update(values->keys);
        }
    }
    if(!found)return false;
    const auto error=validatePropertyAnimation(shot);if(!error.isEmpty()){emit warning(error);return false;}
    if(shot.layerAnimations==m_project.shots[activeShotIndex()].layerAnimations)return true;
    m_project=std::move(project);emit changed();return true;
}
bool Document::setMotionPathCurved(const QString &id,bool curved,int frame) {
    const auto reason=editable(this,id,true);if(!reason.isEmpty()){emit warning(reason);return false;}
    if(frame==-1)frame=m_currentFrame;
    auto project=m_project;auto &shot=project.shots[activeShotIndex()];auto *a=layerAnimation(shot,id);
    if(!a || frame<0 || frame>=shot.durationFrames)return false;
    if(!curved) {
        const auto *original=propertyChannel(shot,id,KeyframeProperty::PositionX);
        bool active=false;if(original)for(const auto &key:original->keys)if(key.frame==frame)active=key.spatial && key.spatial->curved;
        if(!active)return true;
    }
    pairMotionChannels(*a);
    auto *x=channel(shot,id,KeyframeProperty::PositionX),*y=channel(shot,id,KeyframeProperty::PositionY);
    int index=-1;for(int i=0;i<x->keys.size();++i)if(x->keys[i].frame==frame)index=i;
    if(index<0 || index+1>=x->keys.size()){emit warning("Select a position key with a following key to shape its path.");return false;}
    auto &start=x->keys[index],&end=x->keys[index+1];
    if(curved){
        const QPointF chord(end.value-start.value,y->keys[index+1].value-y->keys[index].value);
        if(!start.spatial)start.spatial=SpatialTangents{{},chord/3,false};
        if(!end.spatial)end.spatial=SpatialTangents{-chord/3,{},false};
        // A point may already own the preceding segment's incoming handle.
        // Initialize its unused outgoing handle when curving the next segment.
        if(!start.spatial->curved && start.spatial->outgoing.isNull())start.spatial->outgoing=chord/3;
        if(end.spatial->incoming.isNull())end.spatial->incoming=-chord/3;
        start.spatial->curved=true;
    } else if(start.spatial)start.spatial->curved=false;
    const auto error=validatePropertyAnimation(shot);if(!error.isEmpty()){emit warning(error);return false;}
    if(shot.layerAnimations==m_project.shots[activeShotIndex()].layerAnimations)return true;
    return commit(m_project,project,curved?"Curve motion path":"Straighten motion path",id);
}
bool Document::setOrientAlongPath(const QString &id,bool enabled) {
    const auto reason=editable(this,id,true);if(!reason.isEmpty()){emit warning(reason);return false;}
    auto project=m_project;auto &shot=project.shots[activeShotIndex()];auto *a=layerAnimation(shot,id);
    if(!a){emit warning("Add position keyframes before orienting artwork along a path.");return false;}
    if(a->orientAlongPath==enabled)return true;
    a->orientAlongPath=enabled;
    return commit(m_project,project,enabled?"Orient along path":"Disable path orientation",id);
}
bool Document::previewKeyframeMotionHandle(int frame,bool incoming,const QPointF &offset) {
    if(!m_beforeKeyframeEdit)return false;
    const auto reason=editable(this,m_keyframeEditLayerId,true,true);if(!reason.isEmpty()){emit warning(reason);return false;}
    auto project=m_project;auto &shot=project.shots[activeShotIndex()];auto *values=channel(shot,m_keyframeEditLayerId,KeyframeProperty::PositionX);
    if(!values)return false;
    int index=-1;for(int i=0;i<values->keys.size();++i)if(values->keys[i].frame==frame)index=i;
    if(index<0)return false;
    if(incoming){if(index==0 || !values->keys[index-1].spatial || !values->keys[index-1].spatial->curved)return false;}
    else if(index+1>=values->keys.size() || !values->keys[index].spatial || !values->keys[index].spatial->curved)return false;
    auto &key=values->keys[index];if(!key.spatial)key.spatial=SpatialTangents{};
    if(incoming)key.spatial->incoming=offset;else key.spatial->outgoing=offset;
    const auto error=validatePropertyAnimation(shot);if(!error.isEmpty()){emit warning(error);return false;}
    if(shot.layerAnimations==m_project.shots[activeShotIndex()].layerAnimations)return true;
    m_project=std::move(project);emit changed();return true;
}
void Document::commitKeyframeEdit(){if(!m_beforeKeyframeEdit)return;const auto before=*m_beforeKeyframeEdit;const auto id=m_keyframeEditLayerId,label=m_keyframeEditLabel;m_beforeKeyframeEdit.reset();m_keyframeEditLayerId.clear();m_keyframeEditLabel.clear();if(before.shots[activeShotIndex()].layerAnimations!=m_project.shots[activeShotIndex()].layerAnimations)commit(before,m_project,label,id);else emit changed();}
void Document::cancelKeyframeEdit(){if(!m_beforeKeyframeEdit)return;const auto before=*m_beforeKeyframeEdit;const auto id=m_keyframeEditLayerId;m_beforeKeyframeEdit.reset();m_keyframeEditLayerId.clear();m_keyframeEditLabel.clear();apply(before,id);}
} // namespace toon
