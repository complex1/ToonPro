#pragma once
#include "raster.h"
#include <QCache>
#include <QPainterPath>
#include <QTransform>

namespace toon {
enum class SelectionCombine { Replace, Add, Subtract, Intersect };
struct RasterSelection {
    bool active = false;
    QPainterPath path;
    qreal featherRadius = 0;
};
struct TransformParameters {
    QPointF translation;
    qreal scaleX = 1;
    qreal scaleY = 1;
    qreal rotation = 0;
};
struct RasterSelectionTile {
    enum class Coverage { Empty, Full, Partial, Invalid };
    Coverage coverage = Coverage::Empty;
    QImage mask; // Grayscale8 only for Partial; Full and Empty need no bitmap.
};
// A selection is immutable throughout a brush stroke. Cache only its boundary
// masks; interior/exterior tiles are allocation-free after classification.
class RasterSelectionCache {
public:
    explicit RasterSelectionCache(const QPainterPath *path = nullptr, qreal featherRadius = 0);
    void reset(const QPainterPath *path = nullptr, qreal featherRadius = 0);
    RasterSelectionTile tile(TileKey key) const;
    bool enabled() const { return m_enabled; }
    int cachedBytes() const { return m_tiles.totalCost(); }
    static constexpr int maxCachedBytes = 8 * 1024 * 1024;
private:
    bool m_enabled = false;
    bool m_empty = true;
    QPainterPath m_path;
    QRectF m_bounds;
    int m_feather = 0;
    mutable QCache<TileKey, RasterSelectionTile> m_tiles{maxCachedBytes};
};
// Hard selection coverage, shared by painting and pixel extraction.
QImage rasterSelectionMask(const QPainterPath &path, TileKey key, qreal featherRadius = 0);
QRectF selectedPixelBounds(const RasterLayer &layer, const QPainterPath *selection = nullptr, qreal featherRadius = 0);
// Materialize full-resolution transformed pixels from the original on Apply or
// explicit snapshot. Interactive views use RasterLayerPreview instead.
// The committed result clips moved pixels to drawingBounds; moving all selected
// pixels outside removes them. On failure, result is unchanged. maxTiles is the
// total allowance for this layer.
bool transformRasterLayer(const RasterLayer &original, const QPainterPath *selection,
                          const QTransform &transform, QRectF drawingBounds, int maxTiles,
                          RasterLayer *result, QString *error, qreal featherRadius = 0);
QTransform rasterTransform(const QRectF &bounds, const TransformParameters &parameters);
} // namespace toon
