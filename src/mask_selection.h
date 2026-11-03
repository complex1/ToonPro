#pragma once
#include "raster_edit.h"

namespace toon {
// Feather is defined in displayed artwork/world pixels. Resample that coverage
// into source pixels instead of approximating anisotropic feather with a radius.
class MaskSelectionSampler {
public:
    void reset(const RasterSelection &selection, const QTransform &sourceToWorld);
    QRectF sourceBounds() const { return m_sourceBounds; }
    RasterSelectionTile tile(TileKey key) const;
private:
    qreal worldCoverage(int x, int y, bool *valid) const;
    RasterSelectionCache m_world;
    QPainterPath m_path;
    QTransform m_transform;
    QRectF m_worldBounds, m_sourceBounds;
    int m_feather = 0;
    bool m_active = false, m_valid = true;
    mutable QCache<TileKey, RasterSelectionTile> m_local{RasterSelectionCache::maxCachedBytes};
};
} // namespace toon
