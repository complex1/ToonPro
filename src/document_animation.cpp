#include "document.h"
#include <QScopedValueRollback>
#include <QUuid>
#include <algorithm>

namespace toon {
namespace {
QString newId() { return QUuid::createUuid().toString(QUuid::WithoutBraces); }
int indexOf(const Shot &shot, const QString &id) { for(int i=0;i<shot.layers.size();++i) if(shot.layers[i].id==id) return i; return -1; }
AnimationTrack *trackById(Shot &shot,const QString &id) { for(auto &track:shot.animationTracks) if(track.id==id) return &track; return nullptr; }
const AnimationTrack *trackById(const Shot &shot,const QString &id) { for(const auto &track:shot.animationTracks) if(track.id==id) return &track; return nullptr; }
bool ancestorLocked(const Shot &shot,QString id) {
    QSet<QString> seen;
    while (!id.isEmpty()) {
        const int index=indexOf(shot,id);
        if(index<0 || seen.contains(id) || shot.layers[index].locked)return true;
        seen.insert(id);id=shot.layers[index].parentId;
    }
    return false;
}
bool trackLocked(const Shot &shot,const AnimationTrack &track) {
    return ancestorLocked(shot,track.rootGroupLayerId);
}
bool drawingLocked(const Shot &shot,const AnimationTrack &track,const QString &drawingId) {
    for(const auto &drawing:track.drawings) if(drawing.id==drawingId) {
        return ancestorLocked(shot,drawing.groupLayerId);
    }
    return true;
}
QString slotForLayer(const Shot &shot,const QString &id) {
    for(const auto &track:shot.animationTracks) for(const auto &drawing:track.drawings)
        for(auto it=drawing.templateLayers.cbegin();it!=drawing.templateLayers.cend();++it) if(it.value()==id) return it.key();
    return {};
}
QString appendBlankDrawing(Shot &shot,AnimationTrack &track,AnimationDrawing *drawing) {
    drawing->id=newId();
    auto group=RasterLayer::create(QString("Drawing %1").arg(track.drawings.size()+1)); group.group=true; group.parentId=track.rootGroupLayerId;
    drawing->groupLayerId=group.id; shot.layers.append(group);
    QString selected;
    for(const auto &slot:track.drawingTemplate) {
        auto layer=RasterLayer::create(slot.name); layer.kind=slot.kind; layer.parentId=group.id;
        drawing->templateLayers.insert(slot.id,layer.id);
        if(selected.isEmpty()) selected=layer.id;
        shot.layers.append(layer);
    }
    return selected;
}
void replaceInterval(AnimationTrack &track,const CelExposure &replacement) {
    QVector<CelExposure> exposures;
    const int start=replacement.startFrame,end=start+replacement.durationFrames;
    for(const auto &source:track.exposures) {
        const int sourceEnd=source.startFrame+source.durationFrames;
        if(sourceEnd<=start || source.startFrame>=end) { exposures.append(source); continue; }
        if(source.startFrame<start) { auto left=source; left.durationFrames=start-source.startFrame; exposures.append(left); }
        if(sourceEnd>end) { auto right=source; if(source.startFrame<start) right.id=newId(); right.startFrame=end; right.durationFrames=sourceEnd-end; exposures.append(right); }
    }
    exposures.append(replacement);
    std::sort(exposures.begin(),exposures.end(),[](const auto &a,const auto &b){return a.startFrame<b.startFrame;});
    track.exposures=std::move(exposures);
}
}
void Document::remapAnimationSelection(const QString &preferredLayerId,const QString &templateSlotId) {
    if(!m_open || m_project.shots.isEmpty() || m_project.shots[activeShotIndex()].layers.isEmpty()) return;
    const auto &shot=m_project.shots[activeShotIndex()];
    const auto active=activeLayerIds(shot,m_currentFrame);
    const int preferred=indexOf(shot,preferredLayerId);
    const auto preferredOwner=animationOwner(shot,preferredLayerId);
    const auto *preferredTrack=trackById(shot,preferredOwner.trackId);
    const bool trackWithDrawing=preferredTrack && preferredOwner.drawingId.isEmpty() && drawingAtFrame(*preferredTrack,m_currentFrame);
    if(preferred>=0 && active.contains(preferredLayerId) && !trackWithDrawing) {
        m_active=preferred;
        m_activeAnimationTrackId=animationOwner(shot,preferredLayerId).trackId;
        return;
    }
    if(const auto *track=trackById(shot,m_activeAnimationTrackId)) {
        if(const auto *drawing=drawingAtFrame(*track,m_currentFrame)) {
            const auto slot=drawing->templateLayers.value(templateSlotId);
            if(active.contains(slot)) { m_active=indexOf(shot,slot); return; }
            for(const auto &role:track->drawingTemplate) {
                const auto id=drawing->templateLayers.value(role.id);
                if(active.contains(id)) { m_active=indexOf(shot,id); return; }
            }
            for(int i=0;i<shot.layers.size();++i) if(!shot.layers[i].group && active.contains(shot.layers[i].id)
                && animationOwner(shot,shot.layers[i].id).drawingId==drawing->id) { m_active=i; return; }
        }
        m_active=indexOf(shot,track->rootGroupLayerId); return;
    }
    m_activeAnimationTrackId.clear();
    for(int i=0;i<shot.layers.size();++i) if(active.contains(shot.layers[i].id) && !shot.layers[i].group) {
        m_active=i; m_activeAnimationTrackId=animationOwner(shot,shot.layers[i].id).trackId; return;
    }
    m_active=0;
}
bool Document::setCurrentFrame(int frame) {
    if(!m_open || m_project.shots.isEmpty() || frame<0 || frame>=m_project.shots[activeShotIndex()].durationFrames) return false;
    if(frame==m_currentFrame) return true;
    if(effectsEditActive()){emit effectsEditResolutionRequested();commitEffectsEdit();}
    if(editingGestureActive() || layerPropertyEditActive()) { emit warning("Finish the current edit before changing frames."); return false; }
    const auto &shot=m_project.shots[activeShotIndex()]; const auto id=shot.layers[m_active].id;
    const auto slot=slotForLayer(shot,id);
    const int previousLayer=m_active; const auto previousTrack=m_activeAnimationTrackId; const bool hadSelection=m_selection.active;
    m_currentFrame=frame; m_selection={};
    remapAnimationSelection(id,slot);
    emit frameChanged();
    if(previousLayer!=m_active) emit selectionChanged();
    if(previousTrack!=m_activeAnimationTrackId) emit animationSelectionChanged();
    if(hadSelection) emit rasterSelectionChanged();
    return true;
}
bool Document::selectAnimationTrack(const QString &id) {
    if(!m_open || editingGestureActive() || layerPropertyEditActive() || !trackById(m_project.shots[activeShotIndex()],id)) return false;
    const auto &shot=m_project.shots[activeShotIndex()]; const auto slot=slotForLayer(shot,shot.layers[m_active].id);
    m_activeAnimationTrackId=id; remapAnimationSelection({},slot);
    emit selectionChanged(); emit animationSelectionChanged(); return true;
}
bool Document::addAnimationTrack(const QString &name,DrawingTemplate type) {
    return addAnimationTrack(name,drawingTemplateSlots(type));
}
bool Document::addAnimationTrack(const QString &name,const QVector<DrawingTemplateSlot> &layerSlots) {
    const auto templateError=validateDrawingTemplate(layerSlots);
    if(!templateError.isEmpty()) { emit warning(templateError); return false; }
    if(!m_open || editingGestureActive() || layerPropertyEditActive()) return false;
    auto project=m_project; auto &shot=project.shots[activeShotIndex()];
    auto root=RasterLayer::create(name.trimmed()); root.group=true; shot.layers.append(root);
    AnimationTrack track; track.id=newId(); track.rootGroupLayerId=root.id; track.drawingTemplate=layerSlots;
    AnimationDrawing drawing; const auto selected=appendBlankDrawing(shot,track,&drawing);
    track.drawings.append(drawing); track.exposures.append({newId(),drawing.id,m_currentFrame,1}); shot.animationTracks.append(track);
    prunePropertyAnimation(shot);
    const auto error=project.validate(); if(!error.isEmpty()) { emit warning(error); return false; }
    QScopedValueRollback<bool> editing(m_animationMutation,true);
    return commit(m_project,project,"Add animation track",selected);
}
bool Document::addStaticTrack(const QString &name) {
    if(!m_open || editingGestureActive() || layerPropertyEditActive()) return false;
    auto project=m_project; auto &shot=project.shots[activeShotIndex()];
    auto group=RasterLayer::create(name.trimmed()); group.group=true;
    auto layer=RasterLayer::create("Drawing"); layer.parentId=group.id;
    shot.layers.append(group); shot.layers.append(layer);
    prunePropertyAnimation(shot);
    const auto error=project.validate(); if(!error.isEmpty()) { emit warning(error); return false; }
    return commit(m_project,project,"Add static track",layer.id);
}
bool Document::createAnimationDrawing(bool duplicate,int durationFrames) {
    if(!m_open || editingGestureActive() || layerPropertyEditActive()) return false;
    auto project=m_project; auto &shot=project.shots[activeShotIndex()]; auto *track=trackById(shot,m_activeAnimationTrackId);
    if(!track) { emit warning("Select an animation track before creating a drawing."); return false; }
    if(trackLocked(shot,*track)) { emit warning("Unlock the animation track before adding a drawing."); return false; }
    if(durationFrames<1) { emit warning("A drawing needs at least one exposure frame."); return false; }
    int insertionFrame=m_currentFrame, insertionDuration=durationFrames;
    const auto *existing=exposureAtFrame(*track,m_currentFrame);
    QString splitId;
    if(existing) {
        if(existing->startFrame==m_currentFrame) insertionFrame=existing->startFrame+existing->durationFrames;
        else {
            if(drawingLocked(shot,*track,existing->drawingId)) { emit warning("Unlock the drawing folder before splitting its exposure."); return false; }
            splitId=existing->id; insertionDuration=qMax(durationFrames,existing->startFrame+existing->durationFrames-m_currentFrame);
        }
    }
    if(qint64(insertionFrame)+insertionDuration>shot.durationFrames) { emit warning("The new drawing would extend past the shot. Extend the shot or choose an earlier frame."); return false; }
    for(const auto &exposure:track->exposures) if(exposure.id!=splitId
        && insertionFrame<exposure.startFrame+exposure.durationFrames && insertionFrame+insertionDuration>exposure.startFrame) {
        emit warning("There is already a drawing at that frame. Choose a blank frame or split an existing hold."); return false;
    }
    AnimationDrawing drawing; QString selected;
    if(duplicate) {
        const auto *source=drawingAtFrame(*track,m_currentFrame);
        if(!source) for(const auto &exposure:track->exposures) {
            if(exposure.startFrame>m_currentFrame) break;
            for(const auto &candidate:track->drawings) if(candidate.id==exposure.drawingId) source=&candidate;
        }
        if(!source) { emit warning("Choose a frame containing a drawing to duplicate, or create a blank drawing first."); return false; }
        QHash<QString,QString> ids, objectIds;
        QVector<RasterLayer> copied;
        for(int i=0;i<shot.layers.size();++i) if(shot.layers[i].id==source->groupLayerId || layerIsDescendant(shot.layers,i,source->groupLayerId)) {
            ids.insert(shot.layers[i].id,newId()); copied.append(shot.layers[i]);
        }
        drawing.id=newId(); drawing.groupLayerId=ids.value(source->groupLayerId);
        for(auto it=source->templateLayers.cbegin();it!=source->templateLayers.cend();++it) drawing.templateLayers.insert(it.key(),ids.value(it.value()));
        const auto currentId=m_project.shots[activeShotIndex()].layers[m_active].id;
        selected=ids.value(currentId);
        for(auto &layer:copied) {
            const auto oldId=layer.id; layer.id=ids.value(oldId);
            for(auto &object:layer.objects) { const auto oldObjectId=object.id; object.id=newId(); objectIds.insert(oldObjectId,object.id); }
            if(ids.contains(layer.parentId)) layer.parentId=ids.value(layer.parentId);
            if(oldId==source->groupLayerId) layer.name=(layer.name+" copy").left(100);
            if(selected.isEmpty() && !layer.group) selected=layer.id;
        }
        shot.layers+=copied;
        const auto originalAnimations=shot.layerAnimations;
        for(auto animation:originalAnimations) if(ids.contains(animation.layerId)) {
            animation.layerId=ids.value(animation.layerId);
            for(auto &path:animation.paths) path.objectId=objectIds.value(path.objectId);
            shot.layerAnimations.append(animation);
        }
    } else selected=appendBlankDrawing(shot,*track,&drawing);
    track->drawings.append(drawing);
    if(!splitId.isEmpty()) for(auto &exposure:track->exposures) if(exposure.id==splitId) exposure.durationFrames=insertionFrame-exposure.startFrame;
    replaceInterval(*track,{newId(),drawing.id,insertionFrame,insertionDuration});
    prunePropertyAnimation(shot);
    const auto error=project.validate(); if(!error.isEmpty()) { emit warning(error); return false; }
    QScopedValueRollback<bool> editing(m_animationMutation,true);
    if(!commit(m_project,project,duplicate?"Duplicate animation drawing":"New animation drawing",selected)) return false;
    if(insertionFrame!=m_currentFrame) {
        setCurrentFrame(insertionFrame);
        const int index=indexOf(m_project.shots[activeShotIndex()],selected); if(index>=0) selectLayer(index);
    }
    return true;
}
bool Document::removeAnimationExposure() {
    if (!m_open || editingGestureActive() || layerPropertyEditActive()) return false;
    const auto *track = trackById(activeShot(), m_activeAnimationTrackId);
    if (!track) return false;
    const auto *exposure = exposureAtFrame(*track, m_currentFrame);
    if (!exposure) return false;
    return removeAnimationExposures({{track->id, exposure->id}});
}
bool Document::removeAnimationTrack() {
    if(!m_open || editingGestureActive() || layerPropertyEditActive()) return false;
    auto project=m_project; auto &shot=project.shots[activeShotIndex()]; const auto *track=trackById(shot,m_activeAnimationTrackId);
    if(!track) return false;
    if(trackLocked(shot,*track)) { emit warning("Unlock the track and its folders before deleting it."); return false; }
    const auto id=track->id, root=track->rootGroupLayerId;
    QSet<QString> removed;
    for(int i=0;i<shot.layers.size();++i) if(shot.layers[i].id==root || layerIsDescendant(shot.layers,i,root)) {
        if(shot.layers[i].locked) { emit warning("Unlock the animation track and its layers before deleting it."); return false; }
        removed.insert(shot.layers[i].id);
    }
    shot.layers.removeIf([&](const auto &layer){return removed.contains(layer.id);});
    shot.animationTracks.removeIf([&](const auto &item){return item.id==id;});
    bool artwork=false; for(const auto &layer:shot.layers) artwork|=!layer.group;
    if(!artwork) shot.layers.append(RasterLayer::create());
    prunePropertyAnimation(shot);
    const auto error=project.validate(); if(!error.isEmpty()) { emit warning(error); return false; }
    QScopedValueRollback<bool> editing(m_animationMutation,true);
    return commit(m_project,project,"Delete animation track");
}
bool Document::setAnimationExposureTiming(int startFrame,int durationFrames) {
    if(!m_open || editingGestureActive() || layerPropertyEditActive()) return false;
    const auto *track=trackById(m_project.shots[activeShotIndex()],m_activeAnimationTrackId);
    if(!track) return false;
    const auto *source=exposureAtFrame(*track,m_currentFrame); if(!source) return false;
    const auto trackId=track->id, exposureId=source->id;
    if(!setAnimationExposureTimingById(trackId,exposureId,startFrame,durationFrames)) return false;
    if(m_currentFrame<startFrame || m_currentFrame>=qint64(startFrame)+durationFrames) setCurrentFrame(startFrame);
    return true;
}
bool Document::renameAnimationDrawing(const QString &name) {
    if(!m_open || editingGestureActive() || layerPropertyEditActive()) return false;
    auto project=m_project; auto &shot=project.shots[activeShotIndex()]; const auto *track=trackById(shot,m_activeAnimationTrackId);
    if(!track) return false; const auto *drawing=drawingAtFrame(*track,m_currentFrame); if(!drawing) return false;
    if(trackLocked(shot,*track) || drawingLocked(shot,*track,drawing->id)) { emit warning("Unlock the animation track and drawing folder before renaming."); return false; }
    const int index=indexOf(shot,drawing->groupLayerId); if(index<0 || shot.layers[index].name==name.trimmed()) return index>=0;
    shot.layers[index].name=name.trimmed();
    prunePropertyAnimation(shot);
    const auto error=project.validate(); if(!error.isEmpty()) { emit warning(error); return false; }
    return commit(m_project,project,"Rename animation drawing");
}
bool Document::convertLayerToAnimation(const QString &name) {
    if(!m_open || editingGestureActive() || layerPropertyEditActive()) return false;
    auto project=m_project; auto &shot=project.shots[activeShotIndex()]; const auto original=shot.layers[m_active];
    if(!animationOwner(shot,original.id).trackId.isEmpty() || !original.parentId.isEmpty() || original.clipped) {
        emit warning("Choose a top-level static layer or folder without clipping to convert into animation."); return false;
    }
    QVector<int> family;
    for(int i=0;i<shot.layers.size();++i) if(shot.layers[i].id==original.id || layerIsDescendant(shot.layers,i,original.id)) {
        if((shot.layers[i].kind!=LayerKind::Raster && shot.layers[i].kind!=LayerKind::Vector) || shot.layers[i].annotation || shot.layers[i].adjustment || shot.layers[i].locked || shot.layers[i].positionLocked) {
            emit warning("Only unlocked raster or vector artwork layers and folders can become animation drawings."); return false;
        }
        family.append(i);
    }
    for(int i=m_active+1;i<shot.layers.size();++i) if(shot.layers[i].parentId.isEmpty()) {
        if(shot.layers[i].clipped) { emit warning("Remove clipping dependents before converting this layer into animation."); return false; }
        break;
    }
    AnimationTrack track; track.id=newId();
    auto root=RasterLayer::create(name.trimmed().isEmpty()?original.name:name.trimmed()); root.group=true; track.rootGroupLayerId=root.id;
    auto group=RasterLayer::create("Drawing 1"); group.group=true; group.parentId=root.id;
    AnimationDrawing drawing{newId(),group.id,{}};
    for(const int i:family) if(!shot.layers[i].group) {
        const QString slot=QString("layer-%1").arg(track.drawingTemplate.size()+1);
        track.drawingTemplate.append({slot,shot.layers[i].name,shot.layers[i].kind}); drawing.templateLayers.insert(slot,shot.layers[i].id);
    }
    shot.layers[m_active].parentId=group.id;
    shot.layers.insert(m_active,group); shot.layers.insert(m_active,root);
    track.drawings.append(drawing); track.exposures.append({newId(),drawing.id,0,shot.durationFrames}); shot.animationTracks.append(track);
    prunePropertyAnimation(shot);
    const auto error=project.validate(); if(!error.isEmpty()) { emit warning(error); return false; }
    QScopedValueRollback<bool> editing(m_animationMutation,true);
    return commit(m_project,project,"Convert to animation",original.id);
}
} // namespace toon
