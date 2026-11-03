#include "document.h"
#include "keyframe_render.h"
#include <cmath>
#include <new>

namespace toon {
namespace {
using Coverage = RasterSelectionTile::Coverage;
int tileCoordinate(int pixel) { return int(std::floor(qreal(pixel) / tileSize)); }
bool hasCoveredPixel(const QImage *image, const QRect &part, const RasterSelectionTile &coverage) {
    if (!image && coverage.coverage == Coverage::Full) return true;
    for (int y = part.top(); y <= part.bottom(); ++y) {
        const auto mask = coverage.mask.isNull() ? nullptr : coverage.mask.constScanLine(y);
        const auto row = image ? reinterpret_cast<const QRgb *>(image->constScanLine(y)) : nullptr;
        for (int x = part.left(); x <= part.right(); ++x)
            if ((!mask || mask[x]) && (!row || qAlpha(row[x]))) return true;
    }
    return false;
}
bool hasPaintedPixels(const QImage &image) {
    for (int y = 0; y < image.height(); ++y) {
        const auto row = reinterpret_cast<const QRgb *>(image.constScanLine(y));
        for (int x = 0; x < image.width(); ++x) if (qAlpha(row[x])) return true;
    }
    return false;
}
bool editSelectedLayer(const RasterLayer &original, const QPainterPath &selection, QRectF drawingBounds,
                       QColor color, bool clear, int maxTiles, RasterLayer *result, bool *changed, QString *error, qreal featherRadius) {
    const auto fail = [&](const QString &message) { *error = message; return false; };
    *changed = false;
    auto candidate = original;
    if (!clear && color.alpha() == 0) { *result = candidate; return true; }
    const QRect area = selection.boundingRect().adjusted(-featherRadius, -featherRadius, featherRadius, featherRadius).intersected(drawingBounds).toAlignedRect();
    if (area.isEmpty()) { *result = candidate; return true; }
    RasterSelectionCache cache(&selection, featherRadius);
    QVector<TileKey> keys;
    if (clear || original.alphaLocked) {
        // Clearing and alpha-locked fills can only affect existing painted tiles.
        keys.reserve(original.tiles.size());
        for (auto tile = original.tiles.cbegin(); tile != original.tiles.cend(); ++tile)
            if (area.intersects(QRect(tile.key().first * tileSize, tile.key().second * tileSize, tileSize, tileSize))) keys.append(tile.key());
    } else {
        const int left = tileCoordinate(area.left()), right = tileCoordinate(area.right());
        const int top = tileCoordinate(area.top()), bottom = tileCoordinate(area.bottom());
        for (int y = top; y <= bottom; ++y) for (int x = left; x <= right; ++x) keys.append({x, y});
    }
    for (const auto &key : keys) {
        const auto coverage = cache.tile(key);
        if (coverage.coverage == Coverage::Empty) continue;
        if (coverage.coverage == Coverage::Invalid) return fail("There is not enough memory for the selection.");
        const QPoint origin(key.first * tileSize, key.second * tileSize);
        const auto part = area.intersected(QRect(origin, QSize(tileSize, tileSize))).translated(-origin);
        if (part.isEmpty()) continue;
        const auto old = original.tiles.constFind(key);
        const QImage *existing = old == original.tiles.cend() ? nullptr : &old.value();
        if (!hasCoveredPixel((clear || original.alphaLocked) ? existing : nullptr, part, coverage)) continue;
        if ((clear || original.alphaLocked) && !existing) continue;
        if (qAbs(qint64(key.first)) > 128 || qAbs(qint64(key.second)) > 128)
            return fail("The selection exceeds the supported drawing coordinates.");
        if (!existing && candidate.tiles.size() >= maxTiles) return fail("The 1024-tile memory limit was reached. The selection was left unchanged.");
        if (clear && coverage.coverage == Coverage::Full && part == QRect(0, 0, tileSize, tileSize)) {
            candidate.tiles.remove(key); *changed = true; continue;
        }
        QImage image = existing ? existing->copy() : QImage(tileSize, tileSize, QImage::Format_ARGB32_Premultiplied);
        if (image.isNull()) return fail("There is not enough memory to edit the selection.");
        if (!existing) image.fill(Qt::transparent);
        {
            QPainter painter(&image);
            painter.setCompositionMode(clear ? QPainter::CompositionMode_DestinationOut
                : original.alphaLocked ? QPainter::CompositionMode_SourceAtop : QPainter::CompositionMode_SourceOver);
            if (coverage.coverage == Coverage::Full) painter.fillRect(part, clear ? QColor(Qt::black) : color);
            else {
                // Fill consecutive mask spans with Qt's integer pixel compositor.
                // Mask geometry is rasterized once per tile, never per span.
                for (int y = part.top(); y <= part.bottom(); ++y) {
                    const auto mask = coverage.mask.constScanLine(y);
                    int x = part.left();
                    while (x <= part.right()) {
                        while (x <= part.right() && !mask[x]) ++x;
                        if (x > part.right()) break;
                        const int start = x, value = mask[x];
                        while (x <= part.right() && mask[x] == value) ++x;
                        auto coveredColor = clear ? QColor(Qt::black) : color;
                        coveredColor.setAlpha((coveredColor.alpha() * value + 127) / 255);
                        painter.fillRect(QRect(start, y, x - start, 1), coveredColor);
                    }
                }
            }
        }
        if (original.alphaLocked) {
            // Keep byte-exact original alpha even if a Qt compositor's rounding
            // differs at partially covered edges. Channels remain premultiplied.
            for (int y = part.top(); y <= part.bottom(); ++y) {
                auto row = reinterpret_cast<QRgb *>(image.scanLine(y));
                const auto before = reinterpret_cast<const QRgb *>(existing->constScanLine(y));
                for (int x = part.left(); x <= part.right(); ++x) {
                    const int alpha = qAlpha(before[x]);
                    if (qAlpha(row[x]) != alpha)
                        row[x] = qRgba(qMin(qRed(row[x]), alpha), qMin(qGreen(row[x]), alpha), qMin(qBlue(row[x]), alpha), alpha);
                }
            }
        }
        if (existing && image == *existing) continue;
        if (clear && !hasPaintedPixels(image)) candidate.tiles.remove(key);
        else candidate.tiles.insert(key, image);
        *changed = true;
    }
    *result = std::move(candidate);
    return true;
}
}

bool Document::fillRasterSelection(const QColor &color) {
    const auto animationReason=keyframeRasterEditReason(this);if(!animationReason.isEmpty()){emit warning(animationReason);return false;}
    if (!m_open || editingGestureActive() || layerPropertyEditActive()) return false;
    if (!m_selection.active || m_selection.path.isEmpty()) { emit warning("Select an area to fill."); return false; }
    if (!color.isValid()) { emit warning("Choose a valid fill color."); return false; }
    const auto &layers = m_project.shots[activeShotIndex()].layers;
    if (!layerCanPaint(layers, m_active)) { emit warning("Choose a visible, unlocked drawing layer; its groups must also be visible and unlocked."); return false; }
    const auto &layer = layers[m_active];
    try {
        const int usedTiles = projectTileCount(m_project);
        RasterLayer edited; bool changed = false; QString error;
        if (!editSelectedLayer(layer, m_selection.path, drawingBounds(), color, false,
                               maxProjectTiles - usedTiles + layer.tiles.size(), &edited, &changed, &error, m_selection.featherRadius)) {
            emit warning(error); return false;
        }
        if (changed) {
            auto after = m_project; after.shots[activeShotIndex()].layers[m_active] = std::move(edited);
            commit(m_project, after, "Fill selection", {}, {}, {}, {}, {},
                {{"operation", "fill"}, {"parameters", QJsonObject{{"color", color.name(QColor::HexArgb)}}}});
        }
        return true;
    } catch (const std::bad_alloc &) { emit warning("There is not enough memory to fill the selection."); return false; }
}
bool Document::clearSelectedPixels() {
    const auto animationReason=keyframeRasterEditReason(this);if(!animationReason.isEmpty()){emit warning(animationReason);return false;}
    if (!m_open || editingGestureActive() || layerPropertyEditActive()) return false;
    if (!m_selection.active || m_selection.path.isEmpty()) { emit warning("Select an area to clear."); return false; }
    const auto &layers = m_project.shots[activeShotIndex()].layers;
    if (!layerCanPaint(layers, m_active)) { emit warning("Choose a visible, unlocked drawing layer; its groups must also be visible and unlocked."); return false; }
    const auto &layer = layers[m_active];
    if (layer.alphaLocked) { emit warning("Turn off alpha lock to clear selected pixels."); return false; }
    try {
        RasterLayer edited; bool changed = false; QString error;
        if (!editSelectedLayer(layer, m_selection.path, drawingBounds(), Qt::transparent, true,
                               layer.tiles.size(), &edited, &changed, &error, m_selection.featherRadius)) {
            emit warning(error); return false;
        }
        if (changed) {
            auto after = m_project; after.shots[activeShotIndex()].layers[m_active] = std::move(edited);
            commit(m_project, after, "Clear selected pixels");
        }
        return true;
    } catch (const std::bad_alloc &) { emit warning("There is not enough memory to clear the selection."); return false; }
}
} // namespace toon
