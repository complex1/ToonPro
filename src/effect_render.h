#pragma once
#include "raster.h"
#include <QSet>
#include <memory>
#include <atomic>

namespace toon {
// Worker-only cancellation state. A scope never changes source pixels or the
// normal synchronous export path; cancelled partial results must not be used.
class EffectRenderCancellationScope {
public:
    explicit EffectRenderCancellationScope(const std::atomic_bool *cancelled);
    ~EffectRenderCancellationScope();
    EffectRenderCancellationScope(const EffectRenderCancellationScope &) = delete;
    EffectRenderCancellationScope &operator=(const EffectRenderCancellationScope &) = delete;
private:
    const std::atomic_bool *m_previous;
};
bool effectRenderCancelled();
bool layerStackHasEffects(const QVector<RasterLayer> &layers);
QString effectRenderError();
void clearEffectRenderError();
// Propagate exact native-layer render failures to canvas/export callers.
void reportLayerRenderError(const QString &error);
// Bounded inspector preview of evaluated coverage in the layer's local space.
QImage layerMaskCoveragePreview(const LayerMask &mask, QRectF layerBounds, QSize size, bool smooth = true);
bool checkEffectClipping(const QVector<RasterLayer> &layers);
// Bounds include filter halos and animated layer/folder transforms, but not the
// shot camera. Adjustment layers never create coverage of their own.
QRectF compositedLayerBounds(const QVector<RasterLayer> &layers, bool annotations = true,
                             QSize sceneFrameSize = {});
// Inverse-map an output crop through the layer's folders and retain every
// source pixel that can contribute through their effects.
QRectF effectSourceBounds(const QVector<RasterLayer> &layers, const QString &layerId,
                         QRectF outputBounds);

class EffectCompositor {
public:
    EffectCompositor(const QVector<RasterLayer> &layers, bool annotations,
                     const RasterLayerPreview *preview = nullptr, bool smooth = false,
                     QSize sceneFrameSize = {});
    ~EffectCompositor();
    QRectF bounds() const;
    QSet<TileKey> visibleTiles(QRectF artworkClip) const;
    QImage tile(const TileKey &key);
private:
    class Impl;
    std::unique_ptr<Impl> m_impl;
};
} // namespace toon
