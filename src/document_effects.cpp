#include "document.h"
#include "mask_selection.h"
#include "property_animation.h"
#include <QSet>
#include <cmath>

namespace toon {
namespace {
int indexOf(const Shot &shot, const QString &id) {
    for (int i=0;i<shot.layers.size();++i) if(shot.layers[i].id==id)return i;
    return -1;
}
int maskTiles(const Project &project) {
    return projectMaskTileCount(project);
}
bool compatibleEffects(const RasterLayer &layer,const LayerEffects &effects) {
    return !layer.adjustment||(!effects.blurEnabled&&!effects.shadowEnabled&&!effects.glowEnabled);
}
// The inspector passes evaluated values. Keep keyed source settings unchanged
// and write only the fields the artist actually changed at this frame.
QString routeAnimatedValues(Project &project,int shotIndex,const RasterLayer &source,
                            const RasterLayer &requested,int frame,bool mask) {
    auto &shot=project.shots[shotIndex];
    const int index=indexOf(shot,source.id);
    if(index<0)return "This layer no longer exists.";
    QVector<QPair<KeyframeProperty,qreal>> writes;
    for(int i=0;i<keyframePropertyCount;++i) {
        const auto property=KeyframeProperty(i);
        if(mask?!keyframePropertyIsMask(property):!keyframePropertyIsEffect(property))continue;
        const qreal wanted=keyframeSourceValue(requested,property);
        const auto *channel=propertyChannel(shot,source.id,property);
        // Evaluate against the stored source, not the inspector's candidate.
        const qreal displayed=channel&&!channel->keys.isEmpty()
            ? evaluateProperty(*layerAnimation(shot,source.id),property,frame)
            : keyframeSourceValue(source,property);
        if(wanted!=displayed&&keyframePropertyNeedsPositionUnlock(property)
            &&layerPositionLocked(shot.layers,index))return "Unlock the layer position before moving its mask.";
        if(!channel||channel->keys.isEmpty())continue;
        if(!setKeyframeSourceValue(shot.layers[index],property,keyframeSourceValue(source,property)))
            return "This animated effect or mask setting is unavailable.";
        if(wanted!=displayed)writes.append({property,wanted});
    }
    if(mask&&source.mask&&requested.mask&&source.mask->pivot!=requested.mask->pivot
        &&layerPositionLocked(shot.layers,index))return "Unlock the layer position before changing its mask pivot.";
    for(const auto &[property,value]:writes) {
        QString error;
        if(!writePropertyKeyframe(project,shot,source.id,property,frame,value,&error))return error.isEmpty()?QString("Could not update this effect keyframe."):error;
    }
    return validatePropertyAnimation(shot);
}
}
QString Document::effectsEditReason(const QString &requested) const {
    if(!m_open)return "Open a project to edit effects.";
    const auto &shot=m_project.shots[activeShotIndex()];
    const QString id=requested.isEmpty()?shot.layers[m_active].id:requested;
    int index=indexOf(shot,id);
    if(index<0)return "Choose a layer.";
    if(!layerPresentAtFrame(shot,id,m_currentFrame))return "Choose a drawing exposed at this frame.";
    if(shot.layers[index].scene3d)return "Effects apply to artwork, not 3D references.";
    if(layerIsAnnotation(shot.layers,index))return "Effects apply to artwork, not production notes.";
    QSet<QString> visited;
    while(index>=0) {
        const auto &layer=shot.layers[index];
        if(visited.contains(layer.id))return "Invalid folder hierarchy.";
        visited.insert(layer.id);
        if(layer.locked)return "Unlock the layer and its folders to edit effects.";
        if(!layer.visible)return "Show the layer and its folders to edit effects.";
        if(layer.parentId.isEmpty())return {};
        index=indexOf(shot,layer.parentId);
    }
    return "The layer's folder is missing.";
}
bool Document::beginEffectsEdit(const QString &requested,const QString &label,bool keyframeProperties) {
    if(!m_open||editingGestureActive()||layerPropertyEditActive())return false;
    const auto reason=effectsEditReason(requested);
    if(!reason.isEmpty()){emit warning(reason);return false;}
    m_effectsLayerId=requested.isEmpty()?m_project.shots[activeShotIndex()].layers[m_active].id:requested;
    m_effectsLabel=label;m_effectsShotId=activeShotId();m_effectsFrame=m_currentFrame;
    m_effectsKeyframeProperties=keyframeProperties;
    if(keyframeProperties)if(const auto *animation=layerAnimation(activeShot(),m_effectsLayerId))
        for(const auto &channel:animation->channels)if(keyframePropertyIsEffect(channel.property)||keyframePropertyIsMask(channel.property)) {
            setKeyframeSourceEditing(false);break;
        }
    m_beforeEffectsEdit=m_project;return true;
}
bool Document::previewLayerEffects(const LayerEffects &effects) {
    if(!m_beforeEffectsEdit||!m_open||m_effectsShotId!=activeShotId()||m_effectsFrame!=m_currentFrame)return false;
    const auto error=validateLayerEffects(effects);
    if(!error.isEmpty()){emit warning(error);return false;}
    const int index=indexOf(activeShot(),m_effectsLayerId);if(index<0)return false;
    const auto source=activeShot().layers[index];
    if(!compatibleEffects(source,effects)){emit warning("Adjustment layers support color correction; add blur or lighting to an artwork layer or folder.");return false;}
    if(!m_effectsKeyframeProperties){if(source.effects==effects)return true;m_project.shots[activeShotIndex()].layers[index].effects=effects;emit changed();return true;}
    auto next=m_project;auto requested=source;requested.effects=effects;
    next.shots[activeShotIndex()].layers[index].effects=effects;
    const auto reason=routeAnimatedValues(next,activeShotIndex(),source,requested,m_currentFrame,false);
    if(!reason.isEmpty()){emit warning(reason);return false;}
    if(next.shots[activeShotIndex()].layers[index].effects==source.effects
        &&next.shots[activeShotIndex()].layerAnimations==activeShot().layerAnimations)return true;
    m_project=std::move(next);emit changed();return true;
}
bool Document::previewLayerMask(const LayerMask &mask) {
    if(!m_beforeEffectsEdit||!m_open||m_effectsShotId!=activeShotId()||m_effectsFrame!=m_currentFrame)return false;
    const auto error=validateLayerMask(mask);
    if(!error.isEmpty()){emit warning(error);return false;}
    const int index=indexOf(activeShot(),m_effectsLayerId);if(index<0)return false;
    const auto source=activeShot().layers[index];
    if(maskTiles(m_project)-(source.mask?source.mask->tiles.size():0)+mask.tiles.size()>maxProjectMaskTiles){emit warning("The project mask tile limit was reached.");return false;}
    if(!m_effectsKeyframeProperties){if(source.mask&&*source.mask==mask)return true;m_project.shots[activeShotIndex()].layers[index].mask=mask;emit changed();return true;}
    auto next=m_project;auto requested=source;requested.mask=mask;
    next.shots[activeShotIndex()].layers[index].mask=mask;
    const auto reason=routeAnimatedValues(next,activeShotIndex(),source,requested,m_currentFrame,true);
    if(!reason.isEmpty()){emit warning(reason);return false;}
    if(source.mask&&*next.shots[activeShotIndex()].layers[index].mask==*source.mask
        &&next.shots[activeShotIndex()].layerAnimations==activeShot().layerAnimations)return true;
    m_project=std::move(next);emit changed();return true;
}
void Document::commitEffectsEdit() {
    if(!m_beforeEffectsEdit)return;
    if(!m_open||m_effectsShotId!=activeShotId()||m_effectsFrame!=m_currentFrame
        ||indexOf(activeShot(),m_effectsLayerId)<0
        ||indexOf(m_beforeEffectsEdit->shots[activeShotIndex()],m_effectsLayerId)<0){cancelEffectsEdit();return;}
    const auto before=*m_beforeEffectsEdit;const auto after=m_project;
    const auto id=m_effectsLayerId,label=m_effectsLabel;
    const auto &old=before.shots[activeShotIndex()].layers[indexOf(before.shots[activeShotIndex()],id)];
    const auto &now=after.shots[activeShotIndex()].layers[indexOf(after.shots[activeShotIndex()],id)];
    m_beforeEffectsEdit.reset();m_effectsLayerId.clear();m_effectsLabel.clear();m_effectsShotId.clear();m_effectsFrame=-1;m_effectsKeyframeProperties=false;
    if(old.effects==now.effects&&old.mask==now.mask&&before.shots[activeShotIndex()].layerAnimations==after.shots[activeShotIndex()].layerAnimations){emit changed();return;}
    m_project=before;
    if(!commit(before,after,label,id)){m_project=before;emit changed();}
}
void Document::cancelEffectsEdit() {
    if(!m_beforeEffectsEdit)return;
    m_project=*m_beforeEffectsEdit;m_beforeEffectsEdit.reset();m_effectsLayerId.clear();m_effectsLabel.clear();m_effectsShotId.clear();m_effectsFrame=-1;m_effectsKeyframeProperties=false;emit changed();
}
bool Document::setLayerEffects(const QString &id,const LayerEffects &effects,bool keyframeProperties) {
    if(!beginEffectsEdit(id,"Edit layer effects",keyframeProperties))return false;
    if(!previewLayerEffects(effects)){cancelEffectsEdit();return false;}
    commitEffectsEdit();return true;
}
bool Document::addAdjustmentLayer() {
    if(!m_open||editingGestureActive()||layerPropertyEditActive())return false;
    auto after=m_project;auto &shot=after.shots[activeShotIndex()];const auto &selected=shot.layers[m_active];
    if(shot.layers.size()>=(shot.animationTracks.isEmpty()?maxDrawingLayers:maxAnimationLayers)){emit warning("The shot layer limit was reached.");return false;}
    // A locked artwork layer may have a new sibling; its destination folder must be editable.
    const QString parent=selected.group?selected.id:selected.parentId;
    if(!parent.isEmpty()) {const auto reason=effectsEditReason(parent);if(!reason.isEmpty()){emit warning(reason);return false;}}
    if(layerIsAnnotation(shot.layers,m_active)){emit warning("Add adjustment layers inside the artwork stack.");return false;}
    auto layer=RasterLayer::create("Color adjustment");layer.adjustment=true;layer.effects.colorEnabled=true;layer.parentId=parent;
    int insertion=selected.group?int(shot.layers.size()):m_active+1;
    if(!selected.group) {
        // An adjustment must not split an existing clipping run: the clipped
        // siblings still need their original painted base underneath them.
        for(int i=m_active+1;i<shot.layers.size();++i) {
            if(shot.layers[i].parentId!=parent)continue;
            if(!shot.layers[i].clipped)break;
            insertion=i+1;
        }
    }
    shot.layers.insert(insertion,layer);
    return commit(m_project,after,"Add color adjustment",layer.id);
}
bool Document::addLayerMask(bool fromSelection) {
    if(!m_open||editingGestureActive()||layerPropertyEditActive())return false;
    const auto reason=effectsEditReason();if(!reason.isEmpty()){emit warning(reason);return false;}
    const auto &shot=m_project.shots[activeShotIndex()];const auto &layer=shot.layers[m_active];if(layer.mask)return false;
    LayerMask mask;mask.pivot=defaultAnimationPivot(m_project,shot,layer.id,m_currentFrame);
    if(fromSelection) {
        if(!m_selection.active||m_selection.path.isEmpty()){emit warning("Select an area before creating a mask from selection.");return false;}
        const auto transform=m_keyframeSourceEditing?QTransform():worldLayerTransform(shot,layer.id,m_currentFrame);
        if(!transform.isInvertible()){emit warning("A zero-scale layer cannot receive a mask selection.");return false;}
        MaskSelectionSampler cache;cache.reset(m_selection,transform);
        const auto bounds=cache.sourceBounds().intersected(drawingBounds()).toAlignedRect();
        if(bounds.isEmpty())return false;
        const int x0=int(std::floor(bounds.left()/qreal(tileSize))),x1=int(std::floor(bounds.right()/qreal(tileSize)));
        const int y0=int(std::floor(bounds.top()/qreal(tileSize))),y1=int(std::floor(bounds.bottom()/qreal(tileSize)));
        if(qint64(x1-x0+1)*(y1-y0+1)+maskTiles(m_project)>maxProjectMaskTiles){emit warning("The selected mask exceeds the project mask tile limit.");return false;}
        mask.defaultValue=0;mask.pivot=cache.sourceBounds().center();
        for(int x=x0;x<=x1;++x)for(int y=y0;y<=y1;++y) {
            const auto coverage=cache.tile({x,y});
            if(coverage.coverage==RasterSelectionTile::Coverage::Invalid){emit warning("There is not enough memory for this mask.");return false;}
            if(coverage.coverage==RasterSelectionTile::Coverage::Empty)continue;
            QImage image(tileSize,tileSize,QImage::Format_Grayscale8);
            if(image.isNull()){emit warning("There is not enough memory for this mask.");return false;}
            if(coverage.coverage==RasterSelectionTile::Coverage::Full)image.fill(255);else image=coverage.mask;
            mask.tiles.insert({x,y},image);
        }
    }
    auto after=m_project;after.shots[activeShotIndex()].layers[m_active].mask=mask;
    return commit(m_project,after,"Add layer mask");
}
bool Document::removeLayerMask() {
    if(!m_open||editingGestureActive()||layerPropertyEditActive())return false;
    const auto reason=effectsEditReason();if(!reason.isEmpty()){emit warning(reason);return false;}
    if(!m_project.shots[activeShotIndex()].layers[m_active].mask)return false;
    auto after=m_project;after.shots[activeShotIndex()].layers[m_active].mask.reset();return commit(m_project,after,"Remove layer mask");
}
bool Document::setLayerMaskEnabled(bool enabled,bool keyframeProperties) {
    if(!m_open||!m_project.shots[activeShotIndex()].layers[m_active].mask||!beginEffectsEdit({},"Toggle layer mask",keyframeProperties))return false;
    const auto &layer=activeShot().layers[m_active];
    auto mask=keyframeProperties?evaluateAnimatedLayerMask(activeShot(),layer.id,m_currentFrame,*layer.mask):*layer.mask;mask.enabled=enabled;
    const bool result=previewLayerMask(mask);if(result)commitEffectsEdit();else cancelEffectsEdit();return result;
}
bool Document::invertLayerMask(bool keyframeProperties) {
    if(!m_open||!m_project.shots[activeShotIndex()].layers[m_active].mask||!beginEffectsEdit({},"Invert layer mask",keyframeProperties))return false;
    if(keyframeProperties) {
        const auto &layer=activeShot().layers[m_active];auto mask=evaluateAnimatedLayerMask(activeShot(),layer.id,m_currentFrame,*layer.mask);mask.inverted=!mask.inverted;
        const bool result=previewLayerMask(mask);if(result)commitEffectsEdit();else cancelEffectsEdit();return result;
    }
    auto mask=*m_project.shots[activeShotIndex()].layers[m_active].mask;mask.defaultValue=255-mask.defaultValue;
    for(auto &image:mask.tiles) {image=image.copy();if(image.isNull()){cancelEffectsEdit();emit warning("There is not enough memory to invert the mask.");return false;}for(int y=0;y<image.height();++y){auto *row=image.scanLine(y);for(int x=0;x<image.width();++x)row[x]=255-row[x];}}
    const bool result=previewLayerMask(mask);if(result)commitEffectsEdit();else cancelEffectsEdit();return result;
}
bool Document::fillLayerMask(bool reveal) {
    if(!m_open||!m_project.shots[activeShotIndex()].layers[m_active].mask||!beginEffectsEdit({},reveal?"Reveal entire mask":"Hide entire mask"))return false;
    auto mask=*m_project.shots[activeShotIndex()].layers[m_active].mask;mask.defaultValue=reveal?255:0;mask.tiles.clear();
    const bool result=previewLayerMask(mask);if(result)commitEffectsEdit();else cancelEffectsEdit();return result;
}
} // namespace toon
