#include "mask_selection.h"
#include <cmath>

namespace toon {
void MaskSelectionSampler::reset(const RasterSelection &selection, const QTransform &sourceToWorld) {
    m_active = selection.active; m_path = selection.path; m_transform = sourceToWorld;
    m_feather = std::isfinite(selection.featherRadius) ? qBound(0, qRound(selection.featherRadius), 100) : 0;
    m_world.reset(m_active ? &m_path : nullptr, m_feather); m_local.clear();
    const auto inverse = sourceToWorld.inverted(&m_valid);
    m_valid = m_valid && sourceToWorld.isAffine();
    for (qreal number : {sourceToWorld.m11(), sourceToWorld.m12(), sourceToWorld.m21(), sourceToWorld.m22(),
                        sourceToWorld.dx(), sourceToWorld.dy()}) m_valid = m_valid && std::isfinite(number);
    m_worldBounds = m_path.isEmpty() ? QRectF() : m_path.boundingRect().adjusted(-m_feather - 1, -m_feather - 1, m_feather + 1, m_feather + 1);
    m_sourceBounds = m_valid ? inverse.mapRect(m_worldBounds) : QRectF();
}
qreal MaskSelectionSampler::worldCoverage(int x, int y, bool *valid) const {
    if (!m_worldBounds.contains(QPointF(x + .5, y + .5))) return 0;
    const int tx = int(std::floor(qreal(x) / tileSize)), ty = int(std::floor(qreal(y) / tileSize));
    const auto coverage = m_world.tile({tx, ty});
    if (coverage.coverage == RasterSelectionTile::Coverage::Invalid) { *valid = false; return 0; }
    if (coverage.coverage == RasterSelectionTile::Coverage::Full) return 255;
    if (coverage.coverage == RasterSelectionTile::Coverage::Empty) return 0;
    return coverage.mask.constScanLine(y - ty * tileSize)[x - tx * tileSize];
}
RasterSelectionTile MaskSelectionSampler::tile(TileKey key) const {
    using Coverage = RasterSelectionTile::Coverage;
    if (!m_active) return {Coverage::Full, {}};
    if (!m_valid) return {Coverage::Invalid, {}};
    if (m_path.isEmpty()) return {Coverage::Empty, {}};
    if (m_transform.isIdentity()) return m_world.tile(key);
    if (const auto *cached = m_local.object(key)) return *cached;
    const QRectF source(qreal(key.first) * tileSize, qreal(key.second) * tileSize, tileSize, tileSize);
    RasterSelectionTile result;
    if (m_sourceBounds.intersects(source)) {
        const auto world = m_transform.mapRect(source);
        if (m_path.contains(world.adjusted(-m_feather - 1, -m_feather - 1, m_feather + 1, m_feather + 1))) {
            result.coverage = Coverage::Full;
        } else {
            QImage image(tileSize, tileSize, QImage::Format_Grayscale8);
            if (image.isNull()) return {Coverage::Invalid, {}};
            int minimum = 255, maximum = 0; bool valid = true;
            for (int y = 0; y < tileSize; ++y) {
                auto *row = image.scanLine(y);
                for (int x = 0; x < tileSize; ++x) {
                    auto point = m_transform.map(source.topLeft() + QPointF(x + .5, y + .5));
                    qreal value = 0;
                    // Valid selections are bounded by the drawing extent. Test
                    // that extent before converting extreme transforms to ints.
                    if (std::isfinite(point.x()) && std::isfinite(point.y()) && m_worldBounds.contains(point)) {
                        point -= QPointF(.5, .5);
                        const int wx = int(std::floor(point.x())), wy = int(std::floor(point.y()));
                        const qreal fx = point.x() - wx, fy = point.y() - wy;
                        value = worldCoverage(wx, wy, &valid) * (1 - fx) * (1 - fy)
                            + worldCoverage(wx + 1, wy, &valid) * fx * (1 - fy)
                            + worldCoverage(wx, wy + 1, &valid) * (1 - fx) * fy
                            + worldCoverage(wx + 1, wy + 1, &valid) * fx * fy;
                        if (!valid) return {Coverage::Invalid, {}};
                    }
                    row[x] = uchar(qBound(0, qRound(value), 255));
                    minimum = qMin(minimum, int(row[x])); maximum = qMax(maximum, int(row[x]));
                }
            }
            if (minimum == 255) result.coverage = Coverage::Full;
            else if (maximum != 0) { result.coverage = Coverage::Partial; result.mask = std::move(image); }
        }
    }
    m_local.insert(key, new RasterSelectionTile(result), int(result.mask.sizeInBytes()) + int(sizeof(RasterSelectionTile)));
    return result;
}
} // namespace toon
