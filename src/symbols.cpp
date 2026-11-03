#include "symbols.h"
#include "symbol_poses.h"
#include "animation.h"
#include "effect_render.h"
#include "raster_edit.h"
#include <QCache>
#include <QSet>
#include <cmath>

namespace toon {
namespace {
struct RenderEntry { QMap<TileKey,QImage> tiles; std::optional<QRectF> opaqueBounds; };
struct RenderCache {
    // Retaining this implicitly shared snapshot makes pointer comparison safe,
    // and invalidates all dependent/nested results on any library edit.
    QVector<SymbolDefinition> library;
    QCache<QString,RenderEntry> entries{64 * 1024};
    QCache<QString,QRectF> bounds{4096};
};
RenderCache &renderCache() { static thread_local RenderCache value; return value; }
thread_local QString lastError;
struct RenderContext { const Project &project; QSet<QString> stack; int depth = 0; };
bool fail(QString message, QString *error) {
    lastError = std::move(message); if (error) *error = lastError; return false;
}
bool finiteBounds(QRectF bounds) {
    return std::isfinite(bounds.left()) && std::isfinite(bounds.top())
        && std::isfinite(bounds.right()) && std::isfinite(bounds.bottom());
}
QString cacheKey(const QString &id, int frame, QRectF bounds) {
    return id + '/' + QString::number(frame) + '/' + QString::number(bounds.x(),'g',17)
        + '/' + QString::number(bounds.y(),'g',17) + '/' + QString::number(bounds.width(),'g',17)
        + '/' + QString::number(bounds.height(),'g',17);
}
bool resolve(RenderContext &context, const Shot &shot, int frame, QVector<RasterLayer> &layers,
             QRectF bounds, QString *error);
bool renderDefinition(RenderContext &context, const SymbolDefinition &definition, int frame,
                      QRectF bounds, QMap<TileKey,QImage> *tiles, QString *error) {
    if (context.stack.contains(definition.id)) return fail("A symbol cannot contain itself, directly or through another symbol.",error);
    if (context.depth >= maxSymbolNestingDepth) return fail("Symbols support up to four nested levels.",error);
    if (!finiteBounds(bounds)) return fail("The symbol's transformed drawing bounds are outside the supported range.",error);
    if(!bounds.isEmpty()) {
        // A full cached source (e.g. prepared for transform handles) can serve
        // every instance crop. Viewport clipping still happens in the painter.
        if(const auto *whole=renderCache().entries.object(cacheKey(definition.id,frame,{}))){*tiles=whole->tiles;return true;}
        // Subpixel handle motion must not rebuild an identical symbol on every
        // pointer event. Expand the source crop to stable tile boundaries.
        const qreal left=std::floor(bounds.left()/tileSize)*tileSize,top=std::floor(bounds.top()/tileSize)*tileSize;
        const qreal right=std::ceil(bounds.right()/tileSize)*tileSize,bottom=std::ceil(bounds.bottom()/tileSize)*tileSize;
        bounds=QRectF(QPointF(left,top),QPointF(right,bottom));
    }
    const auto key = cacheKey(definition.id,frame,bounds);
    if (const auto *entry = renderCache().entries.object(key)) { *tiles = entry->tiles; return true; }
    context.stack.insert(definition.id); ++context.depth;
    QString detail;
    auto layers = evaluateLayers(definition.content,frame,false,bounds,&detail);
    bool okay = detail.isEmpty() && resolve(context,definition.content,frame,layers,bounds,&detail);
    context.stack.remove(definition.id); --context.depth;
    if (!okay) return fail(detail.isEmpty()?QString("Could not render symbol %1.").arg(definition.name):detail,error);
    if (!checkEffectClipping(layers)) return fail(effectRenderError(),error);
    EffectCompositor compositor(layers,false,nullptr,true);
    auto area = compositor.bounds();
    if (!bounds.isEmpty()) area = area.intersected(bounds);
    if (!finiteBounds(area)) return fail("The symbol's artwork is outside the supported range.",error);
    if (area.isEmpty()) { tiles->clear(); return true; }
    // Bound work before asking the compositor to enumerate tile coordinates.
    const auto firstX = std::floor(area.left()/tileSize), firstY = std::floor(area.top()/tileSize);
    const auto lastX = std::ceil(area.right()/tileSize)-1, lastY = std::ceil(area.bottom()/tileSize)-1;
    if (firstX < -128 || firstY < -128 || lastX > 128 || lastY > 128
        || (lastX-firstX+1)*(lastY-firstY+1) > maxProjectTiles)
        return fail("The visible symbol exceeds the supported drawing area. Reduce its extent or zoom into a smaller area.",error);
    const auto keys = compositor.visibleTiles(area);
    if (keys.size() > maxProjectTiles) return fail("The symbol preview exceeds the artwork tile limit.",error);
    QMap<TileKey,QImage> output;
    for (const auto &tile : keys) {
        if (effectRenderCancelled()) return fail("Symbol preview cancelled.",error);
        auto image = compositor.tile(tile);
        if (!effectRenderError().isEmpty()) return fail(effectRenderError(),error);
        if (!image.isNull()) output.insert(tile,std::move(image));
    }
    *tiles = output;
    renderCache().entries.insert(key,new RenderEntry{std::move(output),std::nullopt},
        qMax(1,int(tiles->size()*tileSize*tileSize*4/1024)));
    return true;
}
bool resolve(RenderContext &context, const Shot &shot, int frame, QVector<RasterLayer> &layers,
             QRectF bounds, QString *error) {
    for (auto &layer : layers) {
        if (!layer.symbol && layer.kind != LayerKind::Symbol) continue;
        layer.tiles.clear();
        if (!layer.symbol) return fail("A symbol instance is missing its library link.",error);
        const auto *definition = findSymbol(context.project,layer.symbol->symbolId);
        if (!definition) return fail("A symbol instance refers to a missing library symbol.",error);
        const int localFrame = symbolLocalFrame(*layer.symbol,frame,definition->content.durationFrames);
        if (localFrame < 0 || !layer.visible || layer.opacity <= 0) continue;
        QRectF localBounds;
        if (!bounds.isEmpty()) {
            bool invertible = false;
            worldLayerTransform(shot,layer.id,frame).inverted(&invertible);
            if (!invertible) continue;
            localBounds = effectSourceBounds(layers,layer.id,bounds.adjusted(-2,-2,2,2));
        }
        if (!renderDefinition(context,*definition,localFrame,localBounds,&layer.tiles,error)) return false;
    }
    return true;
}
} // namespace
const SymbolDefinition *findSymbol(const Project &project, const QString &id) {
    for (const auto &symbol : project.symbols) if (symbol.id == id) return &symbol;
    return nullptr;
}
int symbolUseCount(const Project &project, const QString &id) {
    int count = 0;
    for (const auto *shot : projectArtworkShots(project)) for (const auto &layer : shot->layers)
        if (layer.symbol && layer.symbol->symbolId == id) ++count;
    return count;
}
int symbolLocalFrame(const SymbolInstance &instance, int parentFrame, int durationFrames) {
    if (durationFrames <= 0 || parentFrame < instance.startFrame || instance.startFrame < 0
        || instance.durationFrames < -1 || instance.durationFrames > 1000000
        || instance.playbackOffset < -1000000 || instance.playbackOffset > 1000000
        || (instance.durationFrames >= 0 && qint64(parentFrame) >= qint64(instance.startFrame) + instance.durationFrames)
        || instance.firstFrame < 0 || instance.firstFrame >= durationFrames
        || !std::isfinite(instance.speed) || instance.speed < .01 || instance.speed > 100) return -1;
    if (instance.playback == SymbolPlayback::SingleFrame) return instance.firstFrame;
    if (instance.playback == SymbolPlayback::Poses) {
        const int pose = evaluatedSymbolPose(instance,parentFrame);
        return pose >= 0 && pose < durationFrames ? pose : -1;
    }
    const double elapsed = std::floor((qint64(parentFrame)-instance.startFrame+instance.playbackOffset)*instance.speed);
    if (!std::isfinite(elapsed) || std::abs(elapsed) > 1000000000000.) return -1;
    const qint64 position = qint64(elapsed) + instance.firstFrame;
    const auto positiveModulo = [](qint64 value, qint64 period) { return (value % period + period) % period; };
    switch (instance.playback) {
    case SymbolPlayback::Loop: return int(positiveModulo(position, durationFrames));
    case SymbolPlayback::Once: return int(qBound<qint64>(qint64(0),position,qint64(durationFrames)-1));
    case SymbolPlayback::SingleFrame: return instance.firstFrame;
    case SymbolPlayback::Poses: return evaluatedSymbolPose(instance,parentFrame);
    case SymbolPlayback::PingPong: {
        if (durationFrames == 1) return 0;
        const qint64 period = 2LL*(durationFrames-1), at = positiveModulo(position, period);
        return int(at < durationFrames ? at : period-at);
    }
    }
    return -1;
}
bool resolveSymbolLayers(const Project &project, const Shot &shot, int frame,
                         QVector<RasterLayer> &layers, QRectF bounds, QString *error) {
    lastError.clear(); if (error) error->clear();
    auto &cache = renderCache();
    if (cache.library.constData() != project.symbols.constData()) {
        cache.entries.clear(); cache.bounds.clear(); cache.library = project.symbols;
    }
    RenderContext context{project,{}};
    return resolve(context,shot,frame,layers,bounds,error);
}
QRectF symbolArtworkBounds(const Project &project, const QString &id, int frame) {
    const auto *definition = findSymbol(project,id); if (!definition) return {};
    lastError.clear();
    auto &cache=renderCache();
    if(cache.library.constData()!=project.symbols.constData()){cache.entries.clear();cache.bounds.clear();cache.library=project.symbols;}
    frame=qBound(0,frame,definition->content.durationFrames-1);
    const auto key=cacheKey(id,frame,{});
    if(const auto *cached=cache.bounds.object(key))return *cached;
    RenderContext context{project,{}};RasterLayer rendered;QString error;
    if(!renderDefinition(context,*definition,frame,{},&rendered.tiles,&error))return {};
    if(auto *entry=cache.entries.object(cacheKey(id,frame,{}))){
        if(!entry->opaqueBounds)entry->opaqueBounds=selectedPixelBounds(rendered);
        cache.bounds.insert(key,new QRectF(*entry->opaqueBounds));return *entry->opaqueBounds;
    }
    const auto bounds=selectedPixelBounds(rendered);cache.bounds.insert(key,new QRectF(bounds));return bounds;
}
void clearSymbolRenderCache() { renderCache().entries.clear(); renderCache().bounds.clear(); renderCache().library.clear(); lastError.clear(); }
QString symbolRenderError() { return lastError; }
} // namespace toon
