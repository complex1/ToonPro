#include "document.h"
#include "symbol_poses.h"
#include "effect_render.h"
#include "keyframe_render.h"
#include "vector_brush.h"
#include "vector_brush_storage.h"
#include <QLineF>
#include <QUndoCommand>
#include <QUuid>
#include <QSet>
#include <QScopedValueRollback>
#include <algorithm>
#include <new>
#include <cmath>

namespace toon {
namespace {
bool fullLocked(const QVector<RasterLayer> &layers, int index, bool includeSelf = true);
int layerCapacity(const Shot &shot) { return shot.animationTracks.isEmpty() ? maxDrawingLayers : maxAnimationLayers; }
bool drawingContainer(const Shot &shot, const QString &id) {
    for (const auto &track : shot.animationTracks) for (const auto &drawing : track.drawings) if (drawing.groupLayerId == id) return true;
    return false;
}
bool animationContainer(const Shot &shot, const QString &id) {
    for (const auto &track : shot.animationTracks) {
        if (track.rootGroupLayerId == id) return true;
        for (const auto &drawing : track.drawings) if (drawing.groupLayerId == id) return true;
    }
    return false;
}
void removeDeletedTemplateRoles(Shot &shot) {
    QSet<QString> ids; for (const auto &layer : shot.layers) ids.insert(layer.id);
    for (auto &track : shot.animationTracks) for (auto &drawing : track.drawings)
        for (auto it=drawing.templateLayers.begin(); it!=drawing.templateLayers.end();)
            if (!ids.contains(it.value())) it=drawing.templateLayers.erase(it); else ++it;
}
}
class ProjectCommand : public QUndoCommand {
public:
    ProjectCommand(Document *doc, Project before, Project after, const QString &label,
                   QString beforeSelection, QString afterSelection, QString beforeShot, QString afterShot,
                   std::optional<RasterSelection> beforeRaster, std::optional<RasterSelection> afterRaster)
        : QUndoCommand(label), m_doc(doc), m_before(std::move(before)), m_after(std::move(after)),
          m_beforeSelection(std::move(beforeSelection)), m_afterSelection(std::move(afterSelection)),
          m_beforeShot(std::move(beforeShot)), m_afterShot(std::move(afterShot)),
          m_beforeRaster(std::move(beforeRaster)), m_afterRaster(std::move(afterRaster)) {
        // History restores artwork only. Do not retain superseded snapshot images
        // or resurrect an old drawing-aid state when undoing an unrelated stroke.
        m_before.drawingAids = {}; m_after.drawingAids = {};
    }
    void undo() override {
        if(!m_doc->committing())emit m_doc->historyNavigationStarting();
        if (m_beforeRaster) m_doc->m_selection = *m_beforeRaster;
        m_doc->apply(m_before, m_beforeSelection, m_beforeShot);
        if (m_beforeRaster) {
            m_doc->m_selection=*m_beforeRaster;
            emit m_doc->rasterSelectionChanged();
        }
    }
    void redo() override {
        if(!m_doc->committing())emit m_doc->historyNavigationStarting();
        if (m_afterRaster) m_doc->m_selection = *m_afterRaster;
        m_doc->apply(m_after, m_afterSelection, m_afterShot);
        if (m_afterRaster) {
            m_doc->m_selection=*m_afterRaster;
            emit m_doc->rasterSelectionChanged();
        }
    }
private:
    Document *m_doc;
    Project m_before, m_after;
    QString m_beforeSelection, m_afterSelection, m_beforeShot, m_afterShot;
    std::optional<RasterSelection> m_beforeRaster, m_afterRaster;
};
Document::Document(QObject *parent) : QObject(parent) {
    m_history.setUndoLimit(50);
    connect(this,&Document::changed,this,[this]{++m_contentRevision;});
}
void Document::load(const Project &project) {
    QScopedValueRollback loading(m_loading, true);
    m_beforeEffectsEdit.reset(); m_effectsLayerId.clear(); m_effectsLabel.clear(); m_effectsShotId.clear(); m_effectsFrame=-1; m_effectsKeyframeProperties=false;
    m_beforeCameraEdit.reset(); m_cameraEditLayerId.clear(); m_cameraEditLabel.clear();
    m_keyframeSourceEditing=false;
    m_externalEditActive = false;
    m_beforeReference3DEdit.reset(); m_reference3DEditLayerId.clear(); m_reference3DEditShotId.clear(); m_reference3DEditFrame=-1;
    m_beforeKeyframeEdit.reset(); m_keyframeEditLayerId.clear(); m_keyframeEditLabel.clear();
    m_beforeLayerPropertyEdit.reset(); m_layerPropertyLabel.clear();
    m_beforeStroke.reset(); m_strokeShapeOriginal.reset(); m_strokePaint.tiles.clear(); m_vectorStroke.reset(); m_history.clear(); m_historyBytes = 0;
    m_selection = {}; m_shotViews.clear(); m_activeShotId.clear(); m_activeShot = 0;
    m_savedDrawingAids = project.drawingAids; m_drawingAidsDirty = false;
    const auto resumeShot = project.drawingAids.activeShotId;
    if (hasDrawingAids(project.drawingAids) && !resumeShot.isEmpty())
        m_shotViews.insert(resumeShot, {{}, {}, project.drawingAids.activeFrame});
    m_open = true; m_active = 0; m_currentFrame = 0; m_activeAnimationTrackId.clear();
    apply(project, {}, hasDrawingAids(project.drawingAids) ? resumeShot : QString(), false); m_history.setClean();
    emit frameChanged();
    m_loading = false;
    emit loaded();
    emit drawingAidsCleanChanged(true);
}
bool Document::setDrawingAids(const DrawingAids &aids, QString *error) {
    const auto fail = [&](const QString &message) { if (error) *error = message; return false; };
    if (error) error->clear();
    if (!m_open) return fail("Open a project before saving drawing aids.");
    if (m_loading) return fail("The project is still loading its drawing aids.");
    auto next = aids;
    // Resume context has no meaning once the last drawing aid is removed, and
    // is omitted from those files. Treat that return to the saved state as clean.
    if (!hasDrawingAids(next)) { next.activeShotId.clear(); next.activeFrame = 0; }
    if (drawingAidsEqual(m_project.drawingAids, next)) return true;
    if (editingGestureActive() || layerPropertyEditActive()) return fail("Finish the current artwork edit before changing drawing aids.");
    const auto validation = validateDrawingAids(next);
    if (!validation.isEmpty()) return fail(validation);
    m_project.drawingAids = next;
    const bool dirty = !drawingAidsEqual(m_savedDrawingAids, next);
    const bool cleanChanged = dirty != m_drawingAidsDirty;
    m_drawingAidsDirty = dirty;
    emit drawingAidsChanged();
    if (cleanChanged) emit drawingAidsCleanChanged(!dirty);
    return true;
}
void Document::markDrawingAidsSaved() {
    m_savedDrawingAids = m_project.drawingAids;
    if (!m_drawingAidsDirty) return;
    m_drawingAidsDirty = false; emit drawingAidsCleanChanged(true);
}
void Document::apply(const Project &project, const QString &selectedId, const QString &requestedShotId, bool preserveDrawingAids) {
    rememberShotView();
    const auto previousShotId = m_activeShotId;
    const int previousShotIndex = m_activeShot;
    const int previousFrame = m_currentFrame;
    m_beforeEffectsEdit.reset(); m_effectsLayerId.clear(); m_effectsLabel.clear(); m_effectsShotId.clear(); m_effectsFrame=-1; m_effectsKeyframeProperties=false;
    m_beforeCameraEdit.reset(); m_cameraEditLayerId.clear(); m_cameraEditLabel.clear();
    m_externalEditActive = false;
    m_beforeReference3DEdit.reset(); m_reference3DEditLayerId.clear(); m_reference3DEditShotId.clear(); m_reference3DEditFrame=-1;
    m_beforeKeyframeEdit.reset(); m_keyframeEditLayerId.clear(); m_keyframeEditLabel.clear();
    const bool hadTransform = transformActive();
    m_stampPlacement.reset(); m_transformPreview.reset(); m_transformRasterized.reset(); m_transformRows.clear();
    m_transformParameters = {}; m_transformBounds = {};
    m_beforeStroke.reset(); m_strokeShapeOriginal.reset(); m_strokePaint.tiles.clear(); m_vectorStroke.reset();
    m_beforeLayerPropertyEdit.reset(); m_layerPropertyLabel.clear();
    const auto drawingAids = m_project.drawingAids;
    m_project = project;
    if (preserveDrawingAids) m_project.drawingAids = drawingAids;
    QString nextShotId = requestedShotId;
    if (nextShotId.isEmpty() && !selectedId.isEmpty())
        for (const auto &shot : m_project.shots) for (const auto &layer : shot.layers)
            if (layer.id == selectedId) nextShotId = shot.id;
    if (nextShotId.isEmpty()) nextShotId = previousShotId;
    m_activeShot = qBound(0, previousShotIndex, int(m_project.shots.size()) - 1);
    for (int i = 0; i < m_project.shots.size(); ++i) if (m_project.shots[i].id == nextShotId) { m_activeShot = i; break; }
    m_activeShotId = m_project.shots[m_activeShot].id;
    const bool switchedShot = previousShotId != m_activeShotId;
    QString preferredLayer = selectedId;
    if (switchedShot) {
        const auto view = m_shotViews.value(m_activeShotId);
        m_currentFrame = view.frame; m_active = 0; m_activeAnimationTrackId = view.animationTrackId;
        if (preferredLayer.isEmpty()) preferredLayer = view.layerId;
        m_selection = {}; m_keyframeSourceEditing = false;
    }
    m_currentFrame=qBound(0,m_currentFrame,m_project.shots[activeShotIndex()].durationFrames-1);
    m_active = qBound(0, m_active, int(m_project.shots[activeShotIndex()].layers.size()) - 1);
    for (int i = 0; i < m_project.shots[activeShotIndex()].layers.size(); ++i)
        if (m_project.shots[activeShotIndex()].layers[i].id == preferredLayer) { m_active = i; break; }
    // Undo and redo reveal the drawing they changed, without recording frame
    // navigation as an artwork command. Choose its closest exposure if reused.
    if (!selectedId.isEmpty()) {
        const auto &shot=m_project.shots[activeShotIndex()];
        const auto owner=animationOwner(shot,selectedId);
        if (!owner.drawingId.isEmpty() && !layerPresentAtFrame(shot,selectedId,m_currentFrame)) {
            int nearest=-1;
            for (const auto &track : shot.animationTracks) if (track.id==owner.trackId)
                for (const auto &exposure : track.exposures) if (exposure.drawingId==owner.drawingId)
                    if (nearest<0 || qAbs(exposure.startFrame-m_currentFrame)<qAbs(nearest-m_currentFrame)) nearest=exposure.startFrame;
            if (nearest>=0) m_currentFrame=nearest;
        }
    }
    const auto preferred=preferredLayer.isEmpty()?m_project.shots[activeShotIndex()].layers[m_active].id:preferredLayer;
    remapAnimationSelection(preferred);
    // Selection areas belong to the displayed drawing. History commands that
    // own a selection snapshot restore it explicitly after revealing the frame.
    if (previousFrame!=m_currentFrame) m_selection={};
    if (switchedShot) emit shotChanged();
    emit changed(); emit selectionChanged(); emit animationSelectionChanged();
    if (switchedShot || previousFrame != m_currentFrame) emit frameChanged();
    emit rasterSelectionChanged();
    if (hadTransform) emit transformChanged();
}
bool Document::bakePuppetWarp(){
    if(!m_open||editingGestureActive()||layerPropertyEditActive())return false;
    const auto &layers=activeShot().layers;if(!layers[m_active].puppetWarp||!layerCanEdit(layers,m_active)||layerPositionLocked(layers,m_active))return false;
    auto after=m_project;after.shots[activeShotIndex()].layers[m_active].puppetWarp.reset();return commit(m_project,after,"Bake Puppet Warp");
}
bool Document::commit(const Project &before, const Project &afterInput, const QString &label, const QString &selectedId,
                      std::optional<RasterSelection> beforeRaster, std::optional<RasterSelection> afterRaster,
                      const QString &requestedBeforeShot, const QString &requestedAfterShot, const QJsonObject &action) {
    auto after = afterInput;
    QMap<QString,const RasterLayer*> retainedBefore;
    for(const auto *shot:projectArtworkShots(before))for(const auto &layer:shot->layers)if(layer.puppetWarp)retainedBefore.insert(layer.id,&layer);
    for(const auto *shot:projectArtworkShots(after))for(const auto &layer:shot->layers){
        const auto *old=retainedBefore.value(layer.id,nullptr);
        if(layer.puppetWarp){
            QString error;if(layer.kind!=LayerKind::Raster||layer.group||layer.adjustment)error="Editable Puppet Warp requires a raster artwork layer.";
            else if(!old||layer.puppetWarp!=old->puppetWarp)error=validatePuppetWarpState(*layer.puppetWarp);
            if(!error.isEmpty()){apply(before);emit warning(error);return false;}
        }
        if(old&&layer.puppetWarp==old->puppetWarp&&layer.tiles!=old->tiles){apply(before);emit warning("Edit the Puppet Warp or Bake warp before changing its raster pixels.");return false;}
    }
    const auto audioError = validateProjectAudio(after);
    if (!audioError.isEmpty()) { emit warning(audioError); return false; }
    const auto poseError = validateProjectSymbolPoses(after);
    if (!poseError.isEmpty()) { emit warning(poseError); return false; }
    int maskTileCount=0;
    for(const auto *shot:projectArtworkShots(after))for(int i=0;i<shot->layers.size();++i) {
        const auto &layer=shot->layers[i];
        QString error=validateLayerEffects(layer.effects);
        if(error.isEmpty()&&layer.mask)error=validateLayerMask(*layer.mask);
        if(layer.mask)maskTileCount+=layer.mask->tiles.size();
        if(error.isEmpty()&&(layer.scene3d||layer.reference3d||layerIsAnnotation(shot->layers,i))
            &&(layer.mask||layer.adjustment||layer.effects!=LayerEffects{}))error="Remove masks and effects before moving this layer into production notes or a 3D reference.";
        if(error.isEmpty()&&layer.adjustment&&(layer.group||layer.kind!=LayerKind::Raster||!layer.tiles.isEmpty()||!layer.objects.isEmpty()
            ||layer.alphaLocked||layer.clipped||layer.effects.blurEnabled||layer.effects.shadowEnabled||layer.effects.glowEnabled||layer.effects.outlineEnabled))
            error="Adjustment layers support color correction and masks; keep artwork on a drawing layer.";
        if(!error.isEmpty()){apply(before);emit warning(error);return false;}
    }
    if(maskTileCount>maxProjectMaskTiles){apply(before);emit warning("The project mask tile limit was reached.");return false;}
    for (auto &shot : after.shots) {
        const Shot *old=nullptr;
        for (const auto &oldShot : before.shots) if (oldShot.id==shot.id) { old=&oldShot; break; }
        if (old && old->layers.size()!=shot.layers.size()) removeDeletedTemplateRoles(shot);
        prunePropertyAnimation(shot);
        pruneCameraAnimation(shot);
        const auto cameraValidation = validateCameraAnimation(shot);
        if (!cameraValidation.isEmpty()) { apply(before); emit warning(cameraValidation); return false; }
        const auto cueValidation = validateTimelineCues(shot);
        if (!cueValidation.isEmpty()) { apply(before); emit warning(cueValidation); return false; }
        const auto propertyValidation = old ? validatePropertyAnimationMutation(*old,shot) : validatePropertyAnimation(shot);
        if (!propertyValidation.isEmpty()) { apply(before); emit warning(propertyValidation); return false; }
        const QString validation=old && !m_animationMutation ? validateAnimationMutation(*old,shot) : validateAnimation(shot);
        if (!validation.isEmpty()) { apply(before); emit warning(validation); return false; }
    }
    if (!m_animationMutation) for (const auto &oldShot : before.shots) if (!oldShot.animationTracks.isEmpty()) {
        bool found=false; for (const auto &shot : after.shots) if (shot.id==oldShot.id) found=true;
        if (!found) { apply(before); emit warning("Animation shots must be managed through the timeline."); return false; }
    }
    // Editable objects retain source geometry outside the current drawing area.
    // Rebuild derived tiles when sheet dimensions or Notes ancestry changes the
    // clipping extent, so the live document matches its save/load result.
    auto extent = [](const Project &project, const Shot &shot, int index) {
        const auto &sheet = shot.sheet;
        QRectF bounds(-sheet.overscan, -sheet.overscan,
                      project.width + 2 * sheet.overscan, project.height + 2 * sheet.overscan);
        if (layerIsAnnotation(shot.layers,index)) bounds.adjust(-sheet.left,-sheet.top,sheet.right,sheet.bottom);
        return bounds;
    };
    QHash<QString,QRectF> oldExtents;
    for (const auto *shot : projectArtworkShots(before)) for (int index=0;index<shot->layers.size();++index)
        if (layerHasEditableObjects(shot->layers[index])) oldExtents.insert(shot->layers[index].id,extent(before,*shot,index));
    int usedTiles=projectTileCount(after);
    for (auto *shot : projectArtworkShots(after)) for (int index=0;index<shot->layers.size();++index) {
        auto &layer=shot->layers[index];
        if (!layerHasEditableObjects(layer) || !oldExtents.contains(layer.id)) continue;
        const auto bounds=extent(after,*shot,index);
        if (oldExtents.value(layer.id)==bounds) continue;
        const int previous=layer.tiles.size(); QString error;
        if (!rebuildObjectLayer(layer,bounds,maxProjectTiles-usedTiles+previous,&error)) { apply(before); emit warning(error); return false; }
        usedTiles+=layer.tiles.size()-previous;
    }
    if (usedTiles > maxProjectTiles) { apply(before); emit warning("The project artwork tile limit was reached."); return false; }
    // Implicitly shared images make snapshots cheap until a touched tile detaches.
    // Count changed/removed tiles conservatively; reset history before exceeding its budget.
    qint64 cost = 4096;
    QMap<QString, QMap<TileKey, QImage>> previous;
    for (const auto *shot : projectArtworkShots(before)) for (const auto &layer : shot->layers) previous[layer.id] = layer.tiles;
    for (const auto *shot : projectArtworkShots(after)) for (const auto &layer : shot->layers) {
        auto old = previous.take(layer.id);
        for (auto it = layer.tiles.cbegin(); it != layer.tiles.cend(); ++it) {
            const auto image = old.take(it.key());
            if (image.cacheKey() != it->cacheKey()) cost += it->sizeInBytes() + image.sizeInBytes();
        }
        for (const auto &image : old) cost += image.sizeInBytes();
    }
    for (const auto &tiles : previous) for (const auto &image : tiles) cost += image.sizeInBytes();
    QMap<qint64,qint64> oldWarpImages,newWarpImages;
    QSet<qint64> artworkImages;
    for(const auto *shot:projectArtworkShots(before))for(const auto &layer:shot->layers)for(const auto &image:layer.tiles)artworkImages.insert(image.cacheKey());
    for(const auto *shot:projectArtworkShots(after))for(const auto &layer:shot->layers)for(const auto &image:layer.tiles)artworkImages.insert(image.cacheKey());
    for(const auto *shot:projectArtworkShots(before))for(const auto &layer:shot->layers)if(layer.puppetWarp)for(const auto &image:layer.puppetWarp->sourceTiles)oldWarpImages.insert(image.cacheKey(),image.sizeInBytes());
    for(const auto *shot:projectArtworkShots(after))for(const auto &layer:shot->layers)if(layer.puppetWarp)for(const auto &image:layer.puppetWarp->sourceTiles)newWarpImages.insert(image.cacheKey(),image.sizeInBytes());
    for(auto image=oldWarpImages.cbegin();image!=oldWarpImages.cend();++image)if(!newWarpImages.contains(image.key())&&!artworkImages.contains(image.key()))cost+=image.value();
    for(auto image=newWarpImages.cbegin();image!=newWarpImages.cend();++image)if(!oldWarpImages.contains(image.key())&&!artworkImages.contains(image.key()))cost+=image.value();
    QMap<QString,QMap<TileKey,QImage>> oldMasks;
    for(const auto *shot:projectArtworkShots(before))for(const auto &layer:shot->layers)if(layer.mask)oldMasks[layer.id]=layer.mask->tiles;
    for(const auto *shot:projectArtworkShots(after))for(const auto &layer:shot->layers)if(layer.mask) {
        auto old=oldMasks.take(layer.id);
        for(auto it=layer.mask->tiles.cbegin();it!=layer.mask->tiles.cend();++it) {
            const auto image=old.take(it.key());if(image.cacheKey()!=it->cacheKey())cost+=image.sizeInBytes()+it->sizeInBytes();
        }
        for(const auto &image:old)cost+=image.sizeInBytes();
    }
    for(const auto &tiles:oldMasks)for(const auto &image:tiles)cost+=image.sizeInBytes();
    QMap<QString, QImage> previousReferences;
    for (const auto &shot : before.shots) for (const auto &reference : shot.references)
        previousReferences.insert(reference.id, reference.image);
    for (const auto &shot : after.shots) for (const auto &reference : shot.references) {
        const auto old = previousReferences.take(reference.id);
        if (old.cacheKey() != reference.image.cacheKey()) cost += old.sizeInBytes() + reference.image.sizeInBytes();
    }
    for (const auto &image : previousReferences) cost += image.sizeInBytes();
    cost += characterModelHistoryCost(before, after);
    cost += editableObjectHistoryCost(before, after);
    cost += audioHistoryCost(before, after);
    // A duplicated or deleted shot retains its animation metadata in history,
    // too. Match by stable ID and count one-sided shots against an empty one.
    // Images, brush resources and model assets remain accounted above, where
    // immutable shared resources are deduplicated independently of shot IDs.
    QHash<QString, const Shot *> oldAnimationShots;
    for (const auto *shot : projectArtworkShots(before)) oldAnimationShots.insert(shot->id, shot);
    const Shot emptyShot;
    const auto animationCost = [](const Shot &oldShot, const Shot &shot) {
        return animationHistoryCost(oldShot, shot) + propertyAnimationHistoryCost(oldShot, shot)
            + cameraAnimationHistoryCost(oldShot, shot) + timelineCueHistoryCost(oldShot, shot) + symbolPoseHistoryCost(oldShot, shot);
    };
    for (const auto *shot : projectArtworkShots(after)) {
        const auto *oldShot = oldAnimationShots.take(shot->id);
        cost += animationCost(oldShot ? *oldShot : emptyShot, *shot);
    }
    for (const auto *oldShot : oldAnimationShots) cost += animationCost(*oldShot, emptyShot);
    m_committing = true;
    if (m_historyBytes + cost > 64 * 1024 * 1024) { m_history.clear(); m_historyBytes = 0; }
    m_historyBytes += cost;
    const auto indexOfShot = [](const Project &project, const QString &id, int fallback) {
        for (int i = 0; i < project.shots.size(); ++i) if (project.shots[i].id == id) return i;
        return qBound(0, fallback, int(project.shots.size()) - 1);
    };
    const int beforeIndex = indexOfShot(before, requestedBeforeShot.isEmpty() ? m_activeShotId : requestedBeforeShot, activeShotIndex());
    const auto &beforeShot = before.shots[beforeIndex];
    const int beforeLayer = beforeShot.id == m_activeShotId ? qBound(0,m_active,int(beforeShot.layers.size())-1) : 0;
    const auto beforeSelection = beforeShot.layers.at(beforeLayer).id;
    auto afterSelection = selectedId.isEmpty() ? beforeSelection : selectedId;
    QString targetShotId = requestedAfterShot;
    if (targetShotId.isEmpty()) for (const auto &shot : after.shots) for (const auto &layer : shot.layers)
        if (layer.id == afterSelection) targetShotId = shot.id;
    const int afterIndex = indexOfShot(after, targetShotId.isEmpty() ? m_activeShotId : targetShotId, activeShotIndex());
    const auto &afterShot = after.shots[afterIndex];
    const auto &afterLayers = afterShot.layers;
    bool selectionSurvives = false;
    for (const auto &layer : afterLayers) if (layer.id == afterSelection) selectionSurvives = true;
    if (!selectionSurvives) {
        const auto active=activeLayerIds(afterShot,qBound(0,m_currentFrame,afterShot.durationFrames-1));
        const auto owners=animationOwners(afterShot);
        const auto &removed=beforeShot.layers.at(beforeLayer);
        const auto oldOwner=animationOwner(beforeShot,removed.id);
        int best=-1,bestRank=100,bestDistance=1000000;
        for (int i=0;i<afterLayers.size();++i) if (active.contains(afterLayers[i].id)) {
            const auto owner=owners.value(afterLayers[i].id);
            int rank=4;
            if (afterLayers[i].parentId==removed.parentId) rank=0;
            else if (!oldOwner.drawingId.isEmpty() && owner.drawingId==oldOwner.drawingId) rank=1;
            else if (!oldOwner.trackId.isEmpty() && owner.trackId==oldOwner.trackId) rank=2;
            else if (owner.trackId.isEmpty()) rank=3;
            if (afterLayers[i].group) rank+=10;
            const int distance=qAbs(i-beforeLayer);
            if (rank<bestRank || (rank==bestRank && distance<bestDistance)) { best=i; bestRank=rank; bestDistance=distance; }
        }
        afterSelection=afterLayers[best>=0?best:0].id;
    }
    const auto recordedBefore = before; // before can alias m_project, replaced by redo() below.
    m_history.push(new ProjectCommand(this, before, after, label, beforeSelection, afterSelection,
                                      beforeShot.id, afterShot.id, std::move(beforeRaster), std::move(afterRaster)));
    m_committing = false;
    emit operationCommitted(recordedBefore, after, label, beforeSelection, afterSelection, action);
    return true;
}
void Document::replaceProject(const Project &project, const QString &label, const QJsonObject &action) {
    if (m_singleShotEditing && (project.shots.size()!=1 || project.shots[0].id!=m_activeShotId)) { emit warning("The editor can edit only its own single document."); return; }
    if (!m_open || editingGestureActive() || layerPropertyEditActive()) return;
    const auto error = project.validate();
    if (!error.isEmpty()) { emit warning(error); return; }
    commit(m_project, project, label, {}, {}, {}, {}, {}, action);
}
bool Document::replaceSymbolProject(const Project &project, const QString &label) {
    if (!m_open || editingGestureActive() || layerPropertyEditActive()) return false;
    const auto error = project.validate();
    if (!error.isEmpty()) { emit warning(error); return false; }
    QScopedValueRollback<bool> mutation(m_animationMutation, true);
    return commit(m_project, project, label, {}, {}, {}, {}, {}, {});
}
void Document::selectLayer(int index) {
    if(effectsEditActive()){emit effectsEditResolutionRequested();commitEffectsEdit();}
    if (!m_open || editingGestureActive() || layerPropertyEditActive() || index < 0 || index >= m_project.shots[activeShotIndex()].layers.size()) return;
    const auto &shot=m_project.shots[activeShotIndex()];
    if (!layerPresentAtFrame(shot,shot.layers[index].id,m_currentFrame)) { emit warning("That drawing is not exposed at the current frame."); return; }
    m_active = index; m_activeAnimationTrackId=animationOwner(shot,shot.layers[index].id).trackId;
    emit selectionChanged(); emit animationSelectionChanged();
}
void Document::addLayer(bool annotation) {
    if (!m_open || editingGestureActive() || layerPropertyEditActive()) return;
    if (m_project.shots[activeShotIndex()].layers.size() >= layerCapacity(m_project.shots[activeShotIndex()])) { emit warning("This preview supports up to 64 layers per shot."); return; }
    auto p = m_project;
    auto layer = RasterLayer::create(annotation ? "Notes" : QString("Drawing %1").arg(p.shots[activeShotIndex()].layers.size() + 1));
    layer.annotation = annotation;
    const auto &selected = p.shots[activeShotIndex()].layers[m_active];
    if (fullLocked(p.shots[activeShotIndex()].layers, m_active, selected.group)) { emit warning("Unlock the destination folder before adding a layer."); return; }
    layer.parentId = selected.group ? selected.id : selected.parentId;
    const int insertion = selected.group ? int(p.shots[activeShotIndex()].layers.size()) : m_active + 1;
    p.shots[activeShotIndex()].layers.insert(insertion, layer);
    commit(m_project, p, annotation ? "Add notes layer" : "Add drawing layer", layer.id);
}
void Document::addGroup() {
    if (!m_open) return;
    groupLayers({m_project.shots[activeShotIndex()].layers[m_active].id});
}
void Document::setLayerParent(const QString &parentId) {
    if (!m_open) return;
    const auto &layer = m_project.shots[activeShotIndex()].layers[m_active];
    if (layer.parentId == parentId) return;
    moveLayers({layer.id}, parentId);
}
void Document::deleteLayer() {
    if (m_open) deleteLayers({m_project.shots[activeShotIndex()].layers[m_active].id});
}
void Document::moveLayer(int direction) {
    if (!m_open || direction == 0) return;
    const auto &layers = m_project.shots[activeShotIndex()].layers;
    const auto &selected = layers[m_active];
    QVector<int> siblings;
    for (int i = 0; i < layers.size(); ++i) if (layers[i].parentId == selected.parentId) siblings.append(i);
    const int at = siblings.indexOf(m_active), destination = at + (direction > 0 ? 1 : -1);
    if (destination < 0 || destination >= siblings.size()) return;
    const auto before = direction < 0 ? layers[siblings[destination]].id
        : destination + 1 < siblings.size() ? layers[siblings[destination + 1]].id : QString();
    moveLayers({selected.id}, selected.parentId, before);
}
namespace {
int layerIndex(const QVector<RasterLayer> &layers, const QString &id) {
    for (int i = 0; i < layers.size(); ++i) if (layers[i].id == id) return i;
    return -1;
}
bool fullLocked(const QVector<RasterLayer> &layers, int index, bool includeSelf) {
    if (!includeSelf && index >= 0) index = layerIndex(layers, layers[index].parentId);
    QSet<QString> visited;
    while (index >= 0 && index < layers.size()) {
        const auto &layer = layers[index];
        if (layer.locked || visited.contains(layer.id)) return true;
        visited.insert(layer.id);
        index = layerIndex(layers, layer.parentId);
    }
    return false;
}
QSet<QString> selectedRoots(const QVector<RasterLayer> &layers, const QStringList &ids) {
    QSet<QString> selected;
    for (const auto &id : ids) if (layerIndex(layers, id) >= 0) selected.insert(id);
    auto roots = selected;
    for (int i = 0; i < layers.size(); ++i) if (selected.contains(layers[i].id))
        for (const auto &id : selected) if (layerIsDescendant(layers, i, id)) { roots.remove(layers[i].id); break; }
    return roots;
}
QSet<QString> withDescendants(const QVector<RasterLayer> &layers, const QSet<QString> &roots) {
    auto expanded = roots;
    for (int i = 0; i < layers.size(); ++i)
        for (const auto &id : roots) if (layerIsDescendant(layers, i, id)) { expanded.insert(layers[i].id); break; }
    return expanded;
}
QHash<QString, QString> clippingBases(const QVector<RasterLayer> &layers) {
    QHash<QString, QString> parentBase, result;
    for (const auto &layer : layers) {
        if (layer.clipped) result.insert(layer.id, parentBase.value(layer.parentId));
        else parentBase.insert(layer.parentId, layer.id);
    }
    return result;
}
bool clippingPreserved(const QVector<RasterLayer> &before, const QVector<RasterLayer> &after) {
    const auto old = clippingBases(before), current = clippingBases(after);
    for (auto it = current.cbegin(); it != current.cend(); ++it)
        if (old.contains(it.key()) && old.value(it.key()) != it.value()) return false;
    return true;
}
bool hasPaint(const QImage &image) {
    for (int y = 0; y < image.height(); ++y) {
        const auto *row = reinterpret_cast<const QRgb *>(image.constScanLine(y));
        for (int x = 0; x < image.width(); ++x) if (qAlpha(row[x])) return true;
    }
    return false;
}
// Preserve full-resolution overscan and effect halos without allocating a
// canvas-sized image. Refuse oversized bakes before touching the document.
bool compositeLayer(const QVector<RasterLayer> &layers, RasterLayer *result, QString *error) {
    QSet<TileKey> keys;
    for (const auto &layer : layers)
        for (auto it = layer.tiles.cbegin(); it != layer.tiles.cend(); ++it) keys.insert(it.key());
    clearEffectRenderError();
    if (layerStackHasEffects(layers)) {
        const auto bounds = compositedLayerBounds(layers);
        if (!bounds.isEmpty()) {
            const int left = int(std::floor(bounds.left() / tileSize)), right = int(std::ceil(bounds.right() / tileSize)) - 1;
            const int top = int(std::floor(bounds.top() / tileSize)), bottom = int(std::ceil(bounds.bottom() / tileSize)) - 1;
            if (qint64(right - left + 1) * (bottom - top + 1) > 1024) {
                *error = "The combined effects exceed the 1,024-tile layer limit. Reduce their extent before combining layers."; return false;
            }
            for (int y = top; y <= bottom; ++y) for (int x = left; x <= right; ++x) keys.insert({x,y});
        }
    }
    if (keys.size() > 1024) { *error = "The combined artwork exceeds the 1,024-tile layer limit."; return false; }
    for (const auto &key : keys) {
        QImage image(tileSize, tileSize, QImage::Format_ARGB32_Premultiplied);
        if (image.isNull()) { *error = "There is not enough memory to combine the layers."; return false; }
        image.fill(Qt::transparent);
        {
            QPainter painter(&image);
            painter.translate(-key.first * tileSize, -key.second * tileSize);
            paintLayers(painter, layers, true);
        }
        if (!effectRenderError().isEmpty()) { *error = effectRenderError(); return false; }
        if (hasPaint(image)) result->tiles.insert(key, image);
    }
    return true;
}
}
void Document::setLayerClipped(bool clipped) {
    if (!m_open || editingGestureActive() || layerPropertyEditActive()) return;
    auto p = m_project; auto &layers = p.shots[activeShotIndex()].layers; auto &layer = layers[m_active];
    if (layer.group || layer.clipped == clipped || fullLocked(layers, m_active)) return;
    if (layer.kind == LayerKind::Scene3D || layer.kind == LayerKind::Reference3D || layer.adjustment) {
        emit warning("3D references and adjustment layers cannot use clipping masks."); return;
    }
    if (clipped) {
        auto candidate = layers; candidate[m_active].clipped = true;
        const auto baseId = clippingBases(candidate).value(layer.id);
        for (const auto &base : layers) if (base.id == baseId && (base.kind == LayerKind::Scene3D || base.kind == LayerKind::Reference3D || base.adjustment)) {
            emit warning("Choose a drawing layer as the clipping base. 3D references and adjustment layers cannot be clipping bases."); return;
        }
    }
    layer.clipped = clipped; commit(m_project, p, "Clipping mask");
}
void Document::setLayerAlphaLocked(bool locked) {
    if (!m_open || editingGestureActive() || layerPropertyEditActive()) return;
    auto p = m_project; auto &layers = p.shots[activeShotIndex()].layers; auto &layer = layers[m_active];
    if (layer.group || layer.adjustment || layer.kind != LayerKind::Raster || layer.alphaLocked == locked || fullLocked(layers, m_active)) return;
    layer.alphaLocked = locked; commit(m_project, p, "Lock transparent pixels");
}
void Document::changeLayer(const QString &name, bool visible, bool locked, bool annotation, qreal opacity) {
    if (!m_open || editingGestureActive() || layerPropertyEditActive() || name.trimmed().isEmpty() || !std::isfinite(opacity)) return;
    auto p = m_project; auto &layers = p.shots[activeShotIndex()].layers; auto &layer = layers[m_active];
    // Visibility and explicit unlocking remain available on frozen layers. A
    // single legacy property call may unlock and then edit an unlocked layer.
    const bool frozen = fullLocked(layers, m_active, false) || (layer.locked && locked);
    layer.visible = visible; layer.locked = locked;
    if (!frozen) { layer.name = name.trimmed().left(100); layer.annotation = annotation; layer.opacity = qBound(0.0, opacity, 1.0); }
    if (p.toJson() != m_project.toJson()) commit(m_project, p, "Layer properties");
}
void Document::setLayersVisible(const QStringList &ids, bool visible) {
    if (!m_open || editingGestureActive() || layerPropertyEditActive()) return;
    auto p = m_project; bool changed = false;
    for (auto &layer : p.shots[activeShotIndex()].layers) if (ids.contains(layer.id) && layer.visible != visible) { layer.visible = visible; changed = true; }
    if (changed) commit(m_project, p, visible ? "Show layers" : "Hide layers");
}
void Document::setLayersLocked(const QStringList &ids, bool locked) {
    if (!m_open || editingGestureActive() || layerPropertyEditActive()) return;
    auto p = m_project; bool changed = false;
    for (auto &layer : p.shots[activeShotIndex()].layers) if (ids.contains(layer.id) && layer.locked != locked) { layer.locked = locked; changed = true; }
    if (changed) commit(m_project, p, locked ? "Lock layers" : "Unlock layers");
}
void Document::setLayersRowColor(const QStringList &ids, const QColor &color) {
    if (!m_open || editingGestureActive() || layerPropertyEditActive()) return;
    if (color.isValid() && color.alpha() != 255) return;
    auto p = m_project; auto &layers = p.shots[activeShotIndex()].layers; bool changed = false;
    for (int i = 0; i < layers.size(); ++i) if (ids.contains(layers[i].id) && layers[i].rowColor != color) {
        if (fullLocked(layers, i)) { emit warning("Unlock the selected layers and their groups before changing their properties."); return; }
        layers[i].rowColor = color; changed = true;
    }
    if (changed) commit(m_project, p, "Layer row color");
}
void Document::setLayerBlendMode(LayerBlendMode mode) {
    if (!m_open || editingGestureActive() || layerPropertyEditActive() || !layerBlendModeFromKey(layerBlendModeKey(mode))) return;
    auto p = m_project; auto &layers = p.shots[activeShotIndex()].layers;
    if (fullLocked(layers, m_active) || layers[m_active].blendMode == mode) return;
    layers[m_active].blendMode = mode; commit(m_project, p, "Layer blend mode");
}
void Document::setLayerPositionLocked(bool locked) {
    if (!m_open || editingGestureActive() || layerPropertyEditActive()) return;
    auto p = m_project; auto &layers = p.shots[activeShotIndex()].layers;
    if (fullLocked(layers, m_active) || layers[m_active].positionLocked == locked) return;
    layers[m_active].positionLocked = locked; commit(m_project, p, locked ? "Lock layer position" : "Unlock layer position");
}
bool Document::beginLayerPropertyEdit(const QString &label) {
    if (!m_open || editingGestureActive() || layerPropertyEditActive()) return false;
    m_beforeLayerPropertyEdit = m_project; m_layerPropertyLabel = label;
    return true;
}
void Document::previewLayerOpacity(qreal opacity) {
    if (!layerPropertyEditActive() || !std::isfinite(opacity)) return;
    auto &layers = m_project.shots[activeShotIndex()].layers;
    if (fullLocked(layers, m_active) || drawingContainer(m_project.shots[activeShotIndex()],layers[m_active].id)) return;
    opacity = qBound(0.0, opacity, 1.0);
    if (layers[m_active].opacity == opacity) return;
    layers[m_active].opacity = opacity; emit changed();
}
void Document::previewLayersVisible(const QStringList &ids, bool visible) {
    if (!layerPropertyEditActive()) return;
    bool changed = false;
    for (auto &layer : m_project.shots[activeShotIndex()].layers) if (ids.contains(layer.id) && layer.visible != visible && !drawingContainer(m_project.shots[activeShotIndex()],layer.id)) { layer.visible = visible; changed = true; }
    if (changed) emit this->changed();
}
void Document::commitLayerPropertyEdit() {
    if (!layerPropertyEditActive()) return;
    const auto before = *m_beforeLayerPropertyEdit; const auto label = m_layerPropertyLabel;
    m_beforeLayerPropertyEdit.reset(); m_layerPropertyLabel.clear();
    if (before.toJson() != m_project.toJson()) commit(before, m_project, label);
    else emit changed();
}
void Document::cancelLayerPropertyEdit() {
    if (!layerPropertyEditActive()) return;
    const auto before = *m_beforeLayerPropertyEdit;
    apply(before);
}
void Document::deleteLayers(const QStringList &ids) {
    if (!m_open || editingGestureActive() || layerPropertyEditActive()) return;
    auto p = m_project; auto &layers = p.shots[activeShotIndex()].layers;
    const auto removed = withDescendants(layers, selectedRoots(layers, ids));
    if (removed.isEmpty()) return;
    QVector<RasterLayer> remaining; bool hasRaster = false;
    for (int i = 0; i < layers.size(); ++i) {
        if (removed.contains(layers[i].id)) {
            if (fullLocked(layers, i)) { emit warning("Unlock the selected layers and their groups before deleting them."); return; }
        } else { remaining.append(layers[i]); hasRaster |= !layers[i].group && !layers[i].adjustment; }
    }
    if (!hasRaster && p.shots[activeShotIndex()].animationTracks.isEmpty()) { emit warning("Keep at least one drawing or notes layer in the shot."); return; }
    if (!clippingPreserved(layers, remaining)) { emit warning("Include the clipped layers when deleting their clipping base."); return; }
    layers = remaining; commit(m_project, p, "Delete layers");
}
void Document::groupLayers(const QStringList &ids) {
    if (!m_open || editingGestureActive() || layerPropertyEditActive()) return;
    auto p = m_project; auto &layers = p.shots[activeShotIndex()].layers;
    const auto roots = selectedRoots(layers, ids), expanded = withDescendants(layers, roots);
    if (roots.isEmpty()) return;
    if (layers.size() >= layerCapacity(p.shots[activeShotIndex()])) { emit warning("This preview supports up to 64 layers and groups per shot."); return; }
    QString parent; bool first = true; int insertion = -1;
    for (int i = 0; i < layers.size(); ++i) {
        if (expanded.contains(layers[i].id) && layerPositionLocked(layers, i)) {
            emit warning("Unlock the selected layers and their group positions before grouping."); return;
        }
        if (!roots.contains(layers[i].id)) continue;
        if (first) { parent = layers[i].parentId; first = false; }
        else if (layers[i].parentId != parent) { emit warning("Select layers from the same folder to group them."); return; }
        insertion = i + 1;
    }
    auto group = RasterLayer::create("Group"); group.group = true; group.parentId = parent;
    for (auto &layer : layers) if (roots.contains(layer.id)) layer.parentId = group.id;
    // A lone clipped layer grouped through the legacy single-layer action has
    // no base inside its new folder. Keep it as a normal drawing layer.
    if (roots.size() == 1) for (auto &layer : layers) if (roots.contains(layer.id)) layer.clipped = false;
    layers.insert(insertion, group);
    if (!clippingPreserved(m_project.shots[activeShotIndex()].layers, layers)) { emit warning("Select the clipping base and all of its clipped layers together before grouping."); return; }
    const auto error = p.validate(); if (!error.isEmpty()) { emit warning(error); return; }
    commit(m_project, p, "Group layers", group.id);
}
void Document::moveLayers(const QStringList &ids, const QString &parentId, const QString &beforeId) {
    if (!m_open || editingGestureActive() || layerPropertyEditActive()) return;
    auto p = m_project; auto &layers = p.shots[activeShotIndex()].layers;
    const auto roots = selectedRoots(layers, ids), expanded = withDescendants(layers, roots);
    if (roots.isEmpty()) return;
    for (int i = 0; i < layers.size(); ++i) if (expanded.contains(layers[i].id) && layerPositionLocked(layers, i)) {
        emit warning("Unlock the selected layers and their group positions before moving them."); return;
    }
    if (!parentId.isEmpty()) {
        const int target = layerIndex(layers, parentId);
        if (target < 0 || !layers[target].group || expanded.contains(parentId)) {
            emit warning("Choose a group outside the selected layers' contents."); return;
        }
        if (layerPositionLocked(layers, target)) { emit warning("Unlock the destination group's position before moving layers into it."); return; }
    }
    if (!beforeId.isEmpty()) {
        const int before = layerIndex(layers, beforeId);
        if (before < 0 || layers[before].parentId != parentId) return;
        if (expanded.contains(beforeId)) return;
    }
    QVector<RasterLayer> moving, remaining;
    for (auto layer : layers) {
        if (expanded.contains(layer.id)) {
            if (roots.contains(layer.id)) layer.parentId = parentId;
            moving.append(layer);
        } else remaining.append(layer);
    }
    const int insertion = beforeId.isEmpty() ? int(remaining.size()) : layerIndex(remaining, beforeId);
    for (int i = 0; i < moving.size(); ++i) remaining.insert(insertion + i, moving[i]);
    if (!clippingPreserved(layers, remaining)) { emit warning("Select the clipping base and all of its clipped layers together before moving."); return; }
    layers = remaining;
    const auto error = p.validate(); if (!error.isEmpty()) { emit warning(error); return; }
    if (p.toJson() != m_project.toJson()) commit(m_project, p, "Move layers");
}
void Document::duplicateLayers(const QStringList &ids) {
    if (!m_open || editingGestureActive() || layerPropertyEditActive()) return;
    auto p = m_project; auto &layers = p.shots[activeShotIndex()].layers;
    const auto roots = selectedRoots(layers, ids), expanded = withDescendants(layers, roots);
    if (roots.isEmpty()) return;
    for (const auto &id : expanded) if (animationContainer(p.shots[activeShotIndex()],id)) {
        emit warning("Duplicate animation drawings through the timeline, so their timing stays separate."); return;
    }
    for (const auto &layer : layers) if (expanded.contains(layer.id) && layer.kind == LayerKind::Scene3D) {
        emit warning("A shot has one shared 3D scene. Duplicate its objects in the 3D Scene panel instead."); return;
    }
    if (layers.size() + expanded.size() > layerCapacity(p.shots[activeShotIndex()])) { emit warning("Duplicating these layers would exceed the 64-layer limit."); return; }
    for (int i = 0; i < layers.size(); ++i) if (roots.contains(layers[i].id) && fullLocked(layers, i, false)) {
        emit warning("Unlock the destination folders before duplicating layers inside them."); return;
    }
    QHash<QString, QString> copies;
    for (const auto &layer : layers) if (expanded.contains(layer.id)) copies.insert(layer.id, QUuid::createUuid().toString(QUuid::WithoutBraces));
    QHash<QString, QVector<int>> siblings;
    for (int i = 0; i < layers.size(); ++i) siblings[layers[i].parentId].append(i);
    QHash<QString, QVector<RasterLayer>> insertAfter;
    QHash<QString,QString> objectCopies;
    for (const auto &children : siblings) {
        QSet<QString> run;
        auto flush = [&](int last) {
            if (run.isEmpty()) return;
            const auto family = withDescendants(layers, run);
            auto &copy = insertAfter[layers[last].id];
            for (auto layer : layers) if (family.contains(layer.id)) {
                const auto original = layer.id;
                layer.id = copies.value(original); layer.name = (layer.name + " copy").left(100);
                for (auto &object : layer.objects) { const auto oldId=object.id; object.id = QUuid::createUuid().toString(QUuid::WithoutBraces); objectCopies.insert(oldId,object.id); }
                if (family.contains(layer.parentId)) layer.parentId = copies.value(layer.parentId);
                copy.append(layer);
            }
            run.clear();
        };
        int last = -1;
        for (const int index : children) {
            if (roots.contains(layers[index].id)) { run.insert(layers[index].id); last = index; }
            else flush(last);
        }
        flush(last);
    }
    QVector<RasterLayer> duplicated;
    for (const auto &layer : layers) { duplicated.append(layer); duplicated += insertAfter.value(layer.id); }
    if (!clippingPreserved(layers, duplicated)) { emit warning("Include the clipped layers when duplicating their clipping base."); return; }
    const auto selected = copies.value(layers[m_active].id, copies.value(*roots.cbegin()));
    layers = duplicated;
    const auto originalAnimations=p.shots[activeShotIndex()].layerAnimations;
    for (auto animation:originalAnimations) if(copies.contains(animation.layerId)) {
        animation.layerId=copies.value(animation.layerId);
        for(auto &path:animation.paths) path.objectId=objectCopies.value(path.objectId);
        p.shots[activeShotIndex()].layerAnimations.append(animation);
    }
    const auto error = p.validate(); if (!error.isEmpty()) { emit warning(error); return; }
    commit(m_project, p, "Duplicate layers", selected);
}
bool Document::mergeDown() {
    if (!m_open || editingGestureActive() || layerPropertyEditActive()) return false;
    auto p = m_project; auto &layers = p.shots[activeShotIndex()].layers;
    const auto upper = layers[m_active]; int lowerIndex = -1, upperSibling = -1;
    for (int i = 0; i < m_active; ++i) if (layers[i].parentId == upper.parentId) lowerIndex = i;
    for (int i = m_active + 1; i < layers.size(); ++i) if (layers[i].parentId == upper.parentId) { upperSibling = i; break; }
    if (lowerIndex < 0) { emit warning("There is no layer below this layer in the same folder."); return false; }
    const auto lower = layers[lowerIndex];
    if (layerAnimation(p.shots[activeShotIndex()],upper.id) || layerAnimation(p.shots[activeShotIndex()],lower.id)) {
        emit warning("Disable layer keyframes before merging animated artwork."); return false;
    }
    if (upper.kind == LayerKind::Scene3D || lower.kind == LayerKind::Scene3D || upper.kind == LayerKind::Reference3D || lower.kind == LayerKind::Reference3D) {
        emit warning("3D reference layers cannot be merged into drawing layers."); return false;
    }
    if (upper.group || lower.group || !layerCanPaint(layers, m_active) || !layerCanPaint(layers, lowerIndex)
        || upper.blendMode != LayerBlendMode::Normal || lower.blendMode != LayerBlendMode::Normal
        || upper.clipped || lower.clipped || (upperSibling >= 0 && layers[upperSibling].clipped)
        || layerIsAnnotation(layers, m_active) != layerIsAnnotation(layers, lowerIndex)) {
        emit warning("Merge Down needs two visible, unlocked Normal raster layers in the same folder, with matching notes status and no clipping dependents."); return false;
    }
    try {
        auto merged = lower; merged.tiles.clear(); merged.opacity = 1; merged.alphaLocked = false;
        merged.fillReference = lower.fillReference || upper.fillReference;
        merged.kind = LayerKind::Raster; merged.objects.clear(); merged.effects={}; merged.mask.reset(); merged.adjustment=false;
        QVector<RasterLayer> pair{lower, upper}; for (auto &layer : pair) layer.parentId.clear();
        QString error; if (!compositeLayer(pair, &merged, &error)) { emit warning(error); return false; }
        layers[lowerIndex] = merged; layers.removeAt(m_active); removeDeletedTemplateRoles(p.shots[activeShotIndex()]);
        const auto validation = p.validate(); if (!validation.isEmpty()) { emit warning(validation); return false; }
        return commit(m_project, p, "Merge down", merged.id);
    } catch (const std::bad_alloc &) { emit warning("There is not enough memory to merge the layers."); return false; }
}
bool Document::flattenImage() {
    if (!m_open || editingGestureActive() || layerPropertyEditActive()) return false;
    auto p = m_project; auto &layers = p.shots[activeShotIndex()].layers;
    if (!p.shots[activeShotIndex()].layerAnimations.isEmpty()) { emit warning("Flattening would discard property keyframes. Disable their animation first."); return false; }
    if (!p.shots[activeShotIndex()].animationTracks.isEmpty()) { emit warning("Flattening animation would discard drawing timing. Merge layers inside an individual drawing instead."); return false; }
    for (const auto &layer : layers) if (layer.kind == LayerKind::Symbol) {
        emit warning("Break apart symbol instances before flattening the image."); return false;
    }
    for (const auto &layer : layers) if (layer.kind == LayerKind::Scene3D || layer.kind == LayerKind::Reference3D) {
        emit warning("Remove the 3D reference layer before flattening. Flattening cannot preserve its editable scene."); return false;
    }
    for (int i = 0; i < layers.size(); ++i) if (fullLocked(layers, i)) {
        emit warning("Unlock all layers and folders before flattening the image."); return false;
    }
    // A separate top notes composite keeps movie exports free of production
    // annotations. Mixed folders or interleaved notes cannot be represented by
    // two ordinary raster layers without changing compositing, so leave those
    // documents untouched rather than silently changing their exports.
    QSet<QString> artworkIds, noteIds;
    bool sawNotes = false;
    for (int root = 0; root < layers.size(); ++root) {
        if (!layers[root].parentId.isEmpty()) continue;
        const auto family = withDescendants(layers, {layers[root].id});
        bool artwork = false, notes = false;
        for (int i = 0; i < layers.size(); ++i) if (family.contains(layers[i].id) && !layers[i].group) {
            if (layerIsAnnotation(layers, i)) notes = true; else artwork = true;
        }
        if ((artwork && notes) || (artwork && sawNotes)) {
            emit warning("Keep notes in separate folders above artwork before flattening, so sheet and movie exports stay unchanged."); return false;
        }
        if (notes) { noteIds.unite(family); sawNotes = true; }
        else artworkIds.unite(family);
    }
    const auto bases = clippingBases(layers);
    for (auto it = bases.cbegin(); it != bases.cend(); ++it)
        if (!it.value().isEmpty() && noteIds.contains(it.key()) != noteIds.contains(it.value())) {
            emit warning("Remove clipping between notes and artwork before flattening."); return false;
        }
    // A notes blend mode that depends on the artwork backdrop cannot be baked
    // into a standalone transparent notes layer.
    for (const auto &layer : layers) if (!artworkIds.isEmpty() && noteIds.contains(layer.id)
        && layer.parentId.isEmpty() && layer.blendMode != LayerBlendMode::Normal) {
        emit warning("Use Normal blending on top-level notes layers before flattening with artwork."); return false;
    }
    try {
        QVector<RasterLayer> artwork, notes;
        for (const auto &layer : layers) { if (noteIds.contains(layer.id)) notes.append(layer); else artwork.append(layer); }
        QVector<RasterLayer> flattened; QString error;
        if (!artwork.isEmpty()) {
            auto merged = RasterLayer::create("Flattened artwork");
            merged.fillReference = std::any_of(artwork.cbegin(), artwork.cend(), [](const RasterLayer &layer) { return layer.fillReference; });
            if (!compositeLayer(artwork, &merged, &error)) { emit warning(error); return false; }
            flattened.append(merged);
        }
        if (!notes.isEmpty()) {
            auto merged = RasterLayer::create("Flattened notes"); merged.annotation = true;
            merged.fillReference = std::any_of(notes.cbegin(), notes.cend(), [](const RasterLayer &layer) { return layer.fillReference; });
            if (!compositeLayer(notes, &merged, &error)) { emit warning(error); return false; }
            flattened.append(merged);
        }
        if (flattened.isEmpty()) flattened.append(RasterLayer::create("Flattened artwork"));
        layers = flattened;
        const auto validation = p.validate(); if (!validation.isEmpty()) { emit warning(validation); return false; }
        return commit(m_project, p, "Flatten image", layers.first().id);
    } catch (const std::bad_alloc &) { emit warning("There is not enough memory to flatten the image."); return false; }
}

bool Document::addReference(const QString &name, const QImage &image, QString *error) {
    auto fail = [error](const QString &message) { if (error) *error = message; return false; };
    if (!m_open) return fail("Open a project before importing a reference.");
    if (editingGestureActive() || layerPropertyEditActive()) return fail("Finish the current edit before importing a reference.");
    auto p = m_project;
    ReferenceImage reference;
    reference.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    reference.name = name.trimmed().left(100);
    reference.image = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    p.shots[activeShotIndex()].references.append(reference);
    const auto validation = p.validate();
    if (!validation.isEmpty()) return fail(validation);
    commit(m_project, p, "Add reference image");
    return true;
}
void Document::removeReference(const QString &id) {
    if (!m_open || editingGestureActive() || layerPropertyEditActive()) return;
    auto p = m_project;
    auto &references = p.shots[activeShotIndex()].references;
    for (int i = 0; i < references.size(); ++i) if (references[i].id == id) {
        references.removeAt(i);
        commit(m_project, p, "Remove reference image");
        return;
    }
}
bool Document::beginStroke(QPointF point, qreal pressure, BrushSettings settings, bool eraser) {
    BrushInput input; input.pressure = pressure;
    return beginStroke(point, input, std::move(settings), eraser);
}
bool Document::beginStroke(QPointF point, BrushInput input, BrushSettings settings, bool eraser) {
    const auto animationReason=keyframeRasterEditReason(this);if(!animationReason.isEmpty()){emit warning(animationReason);return false;}
    if (!m_open || editingGestureActive() || layerPropertyEditActive()) return false;
    if (!std::isfinite(point.x()) || !std::isfinite(point.y()) || !std::isfinite(input.pressure)
        || !std::isfinite(settings.size) || !std::isfinite(settings.opacity) || !std::isfinite(settings.spacing)) return false;
    if (!validateBrushSettings(settings) || !validateBrushTipImages(settings)) return false;
    const auto &layer = m_project.shots[activeShotIndex()].layers.at(m_active);
    if (!layerPresentAtFrame(m_project.shots[activeShotIndex()],layer.id,m_currentFrame)) { emit warning("Choose a drawing exposed at the current frame."); return false; }
    const bool vectorStroke=layer.kind==LayerKind::Vector&&!eraser&&layerCanEdit(m_project.shots[activeShotIndex()].layers,m_active);
    if (!layerCanPaint(m_project.shots[activeShotIndex()].layers, m_active)&&!vectorStroke) {
        emit warning(layer.group ? "Choose a drawing layer inside the group to draw."
                                 : "Choose a visible, unlocked layer; its groups must also be visible and unlocked.");
        return false;
    }
    if(vectorStroke&&layer.objects.size()>=1024){emit warning("A vector layer supports up to 1024 editable strokes.");return false;}
    if (eraser && layer.alphaLocked) { emit warning("Turn off alpha lock to erase this layer."); return false; }
    m_strokeOtherTiles = projectTileCount(m_project, layer.id);
    m_strokeDrawingBounds = drawingBounds();
    m_beforeStroke = m_project; m_strokeShapeOriginal.reset(); m_brush = settings; m_eraser = eraser;
    m_strokeSelectionCache.reset(m_selection.active ? &m_selection.path : nullptr, m_selection.featherRadius);
    m_brush.size = qBound(1.0, settings.size, maxBrushSize);
    m_brush.spacing = qBound(.01, settings.spacing, 4.0);
    m_strokePaint.tiles.clear(); m_connectedStroke = {}; m_dabIndex = 0; m_direction = 0; m_initialDirection = 0;
    m_hasDirection = false;
    m_strokeSeed = m_brush.lockRandomSeed ? m_brush.randomSeed : m_randomSeed;
    if (!m_brush.lockRandomSeed) m_randomSeed += 0x9e3779b9U;
    m_strokeOrigin = m_lastPoint = point; m_lastInput = resolvedBrushInput(m_brush, input); m_initialInput = m_lastInput;
    resolveBrushStrokeColor(m_brush, m_initialInput, m_strokeSeed);
    m_vectorStroke.reset();
    if(vectorStroke){
        m_vectorStroke=EditableObject::create();
        m_vectorStroke->stroke=m_brush.color; m_vectorStroke->strokeWidth=m_brush.size;
        VectorBrushStroke source; source.settings=m_brush; source.seed=m_strokeSeed;
        source.canvasSize=QSizeF(m_project.width,m_project.height);
        source.selectionActive=m_selection.active; source.selection=m_selection.path;
        source.selectionFeather=m_selection.featherRadius; source.alphaLocked=layer.alphaLocked;
        m_vectorStroke->brushStroke=std::move(source);
    }
    m_distanceToDab = qMax(.25, m_brush.size * m_brush.spacing);
    if (!dab(point, m_lastInput)) return false;
    emit strokeUpdated(); return true;
}
bool Document::dab(QPointF point, BrushInput input) {
    if (!std::isfinite(point.x()) || !std::isfinite(point.y())) return true;
    if(m_vectorStroke){
        auto &source=*m_vectorStroke->brushStroke;
        if(source.samples.size()>=maxVectorBrushSamples||m_vectorStroke->nodes.size()>=16384){
            if (m_previewingStrokeShape) { m_strokeShapeFailed = true; return false; }
            cancelStroke(); emit warning("This editable stroke reached its sample limit. Draw the next section as a new stroke.");return false;
        }
        const qreal along=source.samples.isEmpty()?0:source.samples.last().position+QLineF(source.samples.last().point,point).length();
        source.samples.append({m_dabIndex,point,input,m_direction,m_initialDirection,along});
        if(m_vectorStroke->nodes.isEmpty()||QLineF(m_vectorStroke->nodes.last().anchor,point).length()>=2)
            m_vectorStroke->nodes.append(BezierNode::corner(point));
    }
    const auto bounds = m_strokeDrawingBounds;
    if (m_brush.connected.mode != BrushConnectedMode::Off) {
        if (!appendConnectedBrush(m_project.shots[activeShotIndex()].layers[m_active], m_strokePaint,
            m_beforeStroke->shots[activeShotIndex()].layers[m_active], m_connectedStroke, m_brush,
            point, input, m_dabIndex, m_strokeSeed, QSizeF(m_project.width, m_project.height), bounds,
            maxProjectTiles - m_strokeOtherTiles, m_eraser,
            m_selection.active ? &m_selection.path : nullptr, &m_strokeSelectionCache)) {
            if (m_previewingStrokeShape) { m_strokeShapeFailed = true; return false; }
            cancelStroke(); emit warning("The connected stroke exceeds the artwork memory limit."); return false;
        }
        ++m_dabIndex; return true;
    }
    const int copies = brushDabCount(m_brush, input, m_dabIndex, m_strokeSeed);
    for (int i = 0; i < copies; ++i) {
        const quint32 seed = m_strokeSeed ^ (quint32(i) * 0x9e3779b9U);
        const auto stamp = evaluateBrushDab(m_brush, input, point, m_direction, m_initialDirection, m_dabIndex, seed, {}, BrushDabPurpose::Painting, i, m_strokeSeed);
        const auto instances = brushSymmetryInstances(m_brush.symmetry, point + stamp.offset, QSizeF(m_project.width, m_project.height));
        for (const auto &instance : instances) {
            const int count = m_strokeOtherTiles + m_project.shots[activeShotIndex()].layers[m_active].tiles.size();
            if (!applyBrushDab(m_project.shots[activeShotIndex()].layers[m_active], m_strokePaint,
                m_beforeStroke->shots[activeShotIndex()].layers[m_active], stamp, instance.point, instance.transform,
                m_brush.blendMode, m_eraser, bounds, maxProjectTiles - count, m_strokeSeed,
                m_selection.active ? &m_selection.path : nullptr, &m_strokeSelectionCache)) {
                if (m_previewingStrokeShape) { m_strokeShapeFailed = true; return false; }
                cancelStroke();
                emit warning("The 1024-tile memory limit was reached. The current stroke was cancelled; earlier artwork is preserved.");
                return false;
            }
        }
    }
    ++m_dabIndex; return true;
}
quint64 Document::brushCursorIndex() const {
    if(m_brush.connected.mode==BrushConnectedMode::Off)return brushDabIndex();
    const auto sourceSize=m_brush.tipFrames.isEmpty()
        ?(!m_brush.colorTip.isNull()?m_brush.colorTip.size():m_brush.tipMask.size()):m_brush.tipFrames.first().rect.size();
    const qreal aspect=m_brush.connected.mode==BrushConnectedMode::Ribbon&&!sourceSize.isEmpty()
        ?qreal(sourceSize.width())/sourceSize.height():1;
    const qreal period=qMax(.1,m_brush.size*m_brush.connected.repeatLength*aspect);
    return quint64(qMax(0.,std::floor(m_connectedStroke.distance/period)));
}
void Document::continueStroke(QPointF point, qreal pressure) {
    BrushInput input; input.pressure = pressure; continueStroke(point, input);
}
void Document::continueStroke(QPointF point, BrushInput input) {
    if (!strokeActive() || !std::isfinite(point.x()) || !std::isfinite(point.y()) || !std::isfinite(input.pressure)) return;
    input = resolvedBrushInput(m_brush, input);
    const auto delta = point - m_lastPoint;
    const qreal distance = QLineF(m_lastPoint, point).length();
    if (distance > 50000) {
        m_lastPoint = point; m_lastInput = input;
        m_connectedStroke.point = point; m_connectedStroke.hasDirection = false;
        return;
    }
    if (distance > .000001) {
        m_direction = std::atan2(delta.y(), delta.x()) * 180 / 3.14159265358979323846;
        if (!m_hasDirection) { m_initialDirection = m_direction; m_hasDirection = true; }
    }
    const auto firstDab = m_dabIndex;
    if (m_brush.connected.mode != BrushConnectedMode::Off) {
        if (distance > .000001 && !dab(point, input)) return;
        m_lastPoint = point; m_lastInput = input;
        if (m_dabIndex != firstDab) emit strokeUpdated();
        return;
    }
    qreal offset = m_distanceToDab;
    while (offset <= distance && distance > 0) {
        const qreal fraction = offset / distance;
        auto mix = [fraction](qreal a, qreal b) { return a + (b-a)*fraction; };
        BrushInput interpolated;
        interpolated.pressure = mix(m_lastInput.pressure, input.pressure);
        interpolated.tiltX = mix(m_lastInput.tiltX, input.tiltX);
        interpolated.tiltY = mix(m_lastInput.tiltY, input.tiltY);
        const qreal rotationDelta = std::remainder(input.rotation - m_lastInput.rotation, 360.0);
        interpolated.rotation = m_lastInput.rotation + rotationDelta*fraction;
        interpolated.wheel = mix(m_lastInput.wheel, input.wheel);
        interpolated.speed = mix(m_lastInput.speed, input.speed);
        if (!dab(m_lastPoint + delta * fraction, interpolated)) return;
        offset += qMax(.25, m_brush.size * m_brush.spacing);
    }
    m_distanceToDab = offset - distance;
    m_lastPoint = point; m_lastInput = input;
    // Keep every input position and pressure for spacing/interpolation, but
    // there is no artwork to invalidate until a dab was actually evaluated.
    // Stabilization can deliver several samples between two spaced stamps.
    if (m_dabIndex != firstDab) emit strokeUpdated();
}
bool Document::resetStrokePaint(QPointF point, BrushInput resolvedInput) {
    // Only restore this layer's implicitly shared tile map. Do not apply the
    // project or emit changed(): that would rebuild panels on every pen move.
    m_project.shots[activeShotIndex()].layers[m_active].tiles =
        m_beforeStroke->shots[activeShotIndex()].layers[m_active].tiles;
    m_strokePaint.tiles.clear(); m_connectedStroke = {};
    if (m_vectorStroke) {
        m_vectorStroke->nodes.clear();
        m_vectorStroke->brushStroke->samples.clear();
    }
    m_dabIndex = 0; m_direction = m_initialDirection = 0; m_hasDirection = false;
    m_lastPoint = point; m_lastInput = resolvedInput;
    m_distanceToDab = qMax(.25, m_brush.size * m_brush.spacing);
    return dab(point, resolvedInput);
}
void Document::previewStrokeLine(QPointF endpoint, BrushInput input) {
    if (!strokeActive() || !std::isfinite(endpoint.x()) || !std::isfinite(endpoint.y())
        || !std::isfinite(input.pressure)) return;
    if (QLineF(m_strokeOrigin, endpoint).length() > 50000) {
        cancelStroke(); return;
    }
    if (!resetStrokePaint(m_strokeOrigin, m_initialInput)) return;
    continueStroke(endpoint, input);
    // Also invalidate removed pixels when the replacement has no new dabs.
    if (strokeActive()) emit strokeUpdated();
}
Document::StrokeShapeOriginal Document::currentStrokeShapeState() const {
    return StrokeShapeOriginal{
        m_project.shots[activeShotIndex()].layers[m_active].tiles, m_strokePaint, m_vectorStroke,
        m_lastPoint, m_lastInput, m_distanceToDab, m_direction, m_initialDirection, m_dabIndex, m_hasDirection, m_connectedStroke};
}
void Document::applyStrokeShapeState(StrokeShapeOriginal original) {
    m_project.shots[activeShotIndex()].layers[m_active].tiles = std::move(original.tiles);
    m_strokePaint = std::move(original.paint); m_vectorStroke = std::move(original.vector);
    m_lastPoint = original.lastPoint; m_lastInput = original.lastInput;
    m_distanceToDab = original.distanceToDab; m_direction = original.direction;
    m_initialDirection = original.initialDirection; m_dabIndex = original.dabIndex; m_hasDirection = original.hasDirection;
    m_connectedStroke = original.connected;
}
void Document::preserveStrokeShapeOriginal() {
    if (strokeActive() && !m_strokeShapeOriginal) m_strokeShapeOriginal = currentStrokeShapeState();
}
bool Document::restoreStrokeShapeOriginal() {
    if (!strokeActive() || !m_strokeShapeOriginal) return false;
    applyStrokeShapeState(std::move(*m_strokeShapeOriginal)); m_strokeShapeOriginal.reset();
    emit strokeUpdated(); return true;
}
bool Document::previewStrokePath(const QPainterPath &path, const QVector<StrokeInputSample> &profile) {
    if (!strokeActive() || path.isEmpty() || path.elementCount() > 1024 || profile.isEmpty() || profile.size() > 8192) return false;
    for (int i = 0; i < path.elementCount(); ++i) {
        const auto element = path.elementAt(i);
        if (!std::isfinite(element.x) || !std::isfinite(element.y)
            || qAbs(element.x) > 1e7 || qAbs(element.y) > 1e7) return false;
    }
    const auto controls = path.controlPointRect();
    if (controls.width() > 100000 || controls.height() > 100000) return false;
    // Flatten only the fitted geometry. Merge its vertices with the original
    // pressure/pose positions, preserving input variation even on a straight fit.
    const auto polygons = path.toSubpathPolygons(QTransform::fromScale(4, 4));
    if (polygons.size() != 1 || polygons.first().size() < 2 || polygons.first().size() > 16384) return false;
    auto points = polygons.first();
    QVector<qreal> distances{0}, inputDistances{0};
    for (auto &point : points) point /= 4;
    for (int i = 1; i < points.size(); ++i) {
        const qreal distance = distances.last() + QLineF(points[i-1], points[i]).length();
        if (!std::isfinite(distance)) return false;
        distances.append(distance);
    }
    for (int i = 0; i < profile.size(); ++i) {
        const auto &sample = profile[i];
        const auto &input = sample.input;
        if (!std::isfinite(sample.point.x()) || !std::isfinite(sample.point.y())
            || !std::isfinite(input.pressure) || input.pressure < 0 || input.pressure > 1
            || !std::isfinite(input.tiltX) || !std::isfinite(input.tiltY)
            || !std::isfinite(input.rotation) || !std::isfinite(input.wheel)
            || !std::isfinite(input.speed) || input.speed < 0 || input.speed > 1e6
            || qAbs(input.tiltX) > 90 || qAbs(input.tiltY) > 90
            || qAbs(input.rotation) > 1e7 || qAbs(input.wheel) > 1) return false;
        if (i) {
            const qreal distance = inputDistances.last() + QLineF(profile[i-1].point, sample.point).length();
            if (!std::isfinite(distance)) return false;
            inputDistances.append(distance);
        }
    }
    if (distances.last() < .001 || distances.last() > 50000 || inputDistances.last() < .001) return false;
    const qreal length = distances.last(), inputLength = inputDistances.last();
    for (auto &distance : distances) distance /= length;
    for (auto &distance : inputDistances) distance /= inputLength;
    QVector<qreal> stops = distances; stops += inputDistances;
    std::sort(stops.begin(), stops.end());
    stops.erase(std::unique(stops.begin(), stops.end()), stops.end());
    const auto beforePreview = currentStrokeShapeState();
    const QScopedValueRollback<bool> replay(m_previewingStrokeShape, true);
    m_strokeShapeFailed = false;
    const auto failed = [&] {
        applyStrokeShapeState(beforePreview);
        emit strokeUpdated();
        emit warning("This shape exceeds the drawing memory or editable-stroke limit. The previous stroke is preserved.");
        return false;
    };
    if (!resetStrokePaint(points.first(), resolvedBrushInput(m_brush, profile.first().input))) return failed();
    int geometry = 1, sensor = 1;
    // Stationary packets can change pressure/pose before the next movement.
    // Keep them in order rather than blending across the entire next segment.
    while (sensor < inputDistances.size() && inputDistances[sensor] == 0) {
        continueStroke(points.first(), profile[sensor++].input);
    }
    for (const qreal position : stops) {
        if (position <= 0) continue;
        while (geometry < distances.size()-1 && distances[geometry] < position) ++geometry;
        while (sensor < inputDistances.size()-1 && inputDistances[sensor] < position) ++sensor;
        const auto fraction = [](qreal p, qreal a, qreal b) { return b > a ? qBound(0., (p-a)/(b-a), 1.) : 1.; };
        const qreal g = fraction(position, distances[geometry-1], distances[geometry]);
        const qreal t = fraction(position, inputDistances[sensor-1], inputDistances[sensor]);
        const auto &a = profile[sensor-1].input, &b = profile[sensor].input;
        BrushInput input;
        input.pressure = a.pressure + (b.pressure-a.pressure)*t;
        input.tiltX = a.tiltX + (b.tiltX-a.tiltX)*t; input.tiltY = a.tiltY + (b.tiltY-a.tiltY)*t;
        input.rotation = a.rotation + std::remainder(b.rotation-a.rotation, 360.)*t;
        input.wheel = a.wheel + (b.wheel-a.wheel)*t;
        input.speed = a.speed + (b.speed-a.speed)*t;
        // Preserve exact packet endpoints (including wrapped barrel rotation).
        if (t <= 0) input = a;
        else if (t >= 1) input = b;
        const QPointF point = points[geometry-1] + (points[geometry]-points[geometry-1])*g;
        continueStroke(point, input);
        while (sensor + 1 < inputDistances.size() && inputDistances[sensor + 1] == position) {
            continueStroke(point, profile[++sensor].input);
        }
        if (m_strokeShapeFailed) return failed();
        if (!strokeActive()) return false;
    }
    emit strokeUpdated(); return true;
}
void Document::airbrushTick() {
    if (!strokeActive() || !m_brush.airbrush || m_brush.connected.mode != BrushConnectedMode::Off) return;
    if (dab(m_lastPoint, m_lastInput)) emit strokeUpdated();
}
void Document::endStroke() {
    if (!strokeActive()) return;
    const auto before=*m_beforeStroke;
    bool changed=false;
    const auto &old=before.shots[activeShotIndex()].layers.at(m_active).tiles;
    const auto &now=m_project.shots[activeShotIndex()].layers.at(m_active).tiles;
    if(old.size()!=now.size())changed=true;
    for(auto it=now.cbegin();it!=now.cend()&&!changed;++it)changed=old.value(it.key())!=it.value();
    if(changed&&m_vectorStroke){
        auto &object=*m_vectorStroke; auto &source=*object.brushStroke;
        if(!source.samples.isEmpty()){
            const auto last=source.samples.last().point;
            if(object.nodes.size()==1||object.nodes.last().anchor!=last)object.nodes.append(BezierNode::corner(last));
            const qreal length=source.samples.last().position;
            for(auto &sample:source.samples)sample.position=length>0?sample.position/length:0;
        }
        source.sourceGeometry=editableGeometrySignature(object);
        QString error=validateEditableObject(object);
        if(error.isEmpty()){
            m_project.shots[activeShotIndex()].layers[m_active].objects.append(object);
            error=m_project.validate();
        }
        if(!error.isEmpty()){cancelStroke();emit warning(error);return;}
        rememberVectorBrushPaint(object,drawingBounds(),m_strokePaint);
    }
    const bool vector=m_vectorStroke.has_value();
    m_beforeStroke.reset();m_strokeShapeOriginal.reset();m_strokePaint.tiles.clear();m_vectorStroke.reset();
    if(changed)commit(before,m_project,m_eraser?"Erase stroke":vector?"Vector brush stroke":"Brush stroke");
    else emit this->changed();
}
void Document::cancelStroke() {
    if (!strokeActive()) return;
    const auto before = *m_beforeStroke; m_beforeStroke.reset(); m_strokeShapeOriginal.reset(); m_strokePaint.tiles.clear(); m_vectorStroke.reset(); apply(before);
}
} // namespace toon
