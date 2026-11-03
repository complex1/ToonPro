#include "keyframe_render.h"
#include "document.h"
#include "camera_animation.h"
#include "effect_render.h"
#include "symbols.h"
#include <QCache>
#include <QHash>
#include <cmath>
namespace toon {
namespace {
struct MorphCache {
    QVector<EditableObject> source, evaluated;
    QMap<TileKey,QImage> tiles;
    QRectF bounds;
};
QCache<QString,MorphCache> &cache() { static thread_local QCache<QString,MorphCache> value(128*1024); return value; }
thread_local QString lastError;
}
void clearKeyframeRenderCache() { cache().clear(); clearSymbolRenderCache(); }
QString keyframeRenderError() { return !lastError.isEmpty() ? lastError : (!symbolRenderError().isEmpty()?symbolRenderError():effectRenderError()); }
QString keyframeRasterEditReason(const Document *document, const QString &requested) {
    if (!document || !document->isOpen() || document->keyframeSourceEditing()) return {};
    const auto &shot=document->activeShot();
    const auto id=requested.isEmpty()?shot.layers[document->activeLayer()].id:requested;
    if (!worldLayerTransform(shot,id,document->currentFrame()).isIdentity())
        return "Enable Source artwork in Frame Properties → Animation to edit this layer's original drawing, or use animated transform handles.";
    return {};
}
bool applyLayerAnimations(const Shot &shot,int frame,QVector<RasterLayer> &layers,QRectF bounds,QString *error) {
    lastError.clear();
    clearEffectRenderError();
    if(error)error->clear();
    const auto fail=[&](const RasterLayer &layer,const QString &detail) {
        lastError=QString("Could not render animated artwork on %1: %2").arg(layer.name,detail);
        if(error)*error=lastError;
        return false;
    };
    const bool cameraMoved = evaluateShotCamera(shot, frame) != Camera2D{};
    // Folder effects require inverse-mapping their halos through the complete
    // evaluated ancestry, so prepare every transform before rasterizing paths.
    for (auto &layer:layers) {
        const auto *animation=layerAnimation(shot,layer.id);
        if(animation) {
            layer.renderTransform=evaluateLayerTransform(shot,layer.id,frame);
            layer.opacity*=evaluateProperty(shot,layer.id,KeyframeProperty::Opacity,frame);
            layer.effects=evaluateAnimatedLayerEffects(shot,layer.id,frame,layer.effects);
            if(layer.mask)layer.mask=evaluateAnimatedLayerMask(shot,layer.id,frame,*layer.mask);
        }
    }
    // Animated enable/radius channels must be applied before determining the
    // vector source halo; a currently disabled base effect may be enabled now.
    const bool effects = layerStackHasEffects(layers);
    QHash<QString,const RasterLayer*> evaluatedById;
    for(const auto &layer:layers)evaluatedById.insert(layer.id,&layer);
    const auto contributes=[&](const RasterLayer &layer) {
        const RasterLayer *current=&layer;
        for(int depth=0;current&&depth<=8;++depth) {
            if(!current->visible||current->opacity<=0)return false;
            current=current->parentId.isEmpty()?nullptr:evaluatedById.value(current->parentId);
        }
        return true;
    };
    for (auto &layer:layers) {
        const auto *animation=layerAnimation(shot,layer.id);
        if(!layerHasEditableObjects(layer)||layer.group)continue;
        const auto source=layer.objects;
        if(animation&&!animation->paths.isEmpty())
            for(auto &object:layer.objects)object=evaluateAnimatedObject(shot,layer.id,object,frame);
        // Hidden/zero-opacity folders cannot contribute to a filter halo. Keep
        // evaluated pose metadata, but do not allocate their offscreen geometry.
        if(!contributes(layer))continue;
        const auto world=worldLayerTransform(shot,layer.id,frame);
        // A camera pan can reveal editable geometry whose original tile cache
        // was clipped to the drawing area, even without layer/path keyframes.
        if(layer.objects==source&&world.isIdentity()&&!cameraMoved&&!effects)continue;
        bool invertible=false;const auto inverse=world.inverted(&invertible);
        // A zero layer scale deliberately hides the artwork. Never inverse-map
        // its crop through QTransform's identity fallback or allocate its tiles.
        if(!invertible){layer.tiles.clear();continue;}
        QRectF contentBounds;
        for(const auto &object:layer.objects) {
            const auto validation=validateEditableObject(object);
            if(!validation.isEmpty())return fail(layer,validation);
            contentBounds|=editableObjectBounds(object).adjusted(-2,-2,2,2);
        }
        QRectF renderBounds=contentBounds;
        if(!bounds.isEmpty()) {
            // These are source tiles, subsequently transformed by the layer and
            // all its folders. Clip in that source space so artwork outside the
            // original sheet can move back into view. The small output-space
            // inset also retains the neighbors used by transformed filtering.
            const auto localBounds=effects ? effectSourceBounds(layers,layer.id,bounds)
                : inverse.mapRect(bounds.adjusted(-2,-2,2,2));
            if(!std::isfinite(localBounds.left())||!std::isfinite(localBounds.top())
                ||!std::isfinite(localBounds.right())||!std::isfinite(localBounds.bottom()))
                return fail(layer,"The transformed drawing bounds are outside the supported range.");
            renderBounds=contentBounds.intersected(localBounds);
        }
        if(renderBounds.isEmpty()){layer.tiles.clear();continue;}
        if(const auto *old=cache().object(layer.id);old&&old->source==source&&old->evaluated==layer.objects&&old->bounds==renderBounds){layer.tiles=old->tiles;continue;}
        QString detail;
        if(!rebuildObjectLayer(layer,renderBounds,maxProjectTiles,&detail))return fail(layer,detail);
        auto *entry=new MorphCache{source,layer.objects,layer.tiles,renderBounds};
        cache().insert(layer.id,entry,qMax(1,int(layer.tiles.size()*tileSize*tileSize*4/1024)));
    }
    return true;
}
}
