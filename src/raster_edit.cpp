#include "raster_edit.h"
#include <QHash>
#include <QSet>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>

namespace toon {
namespace {
int tileCoordinate(int pixel) { return pixel >= 0 ? pixel / tileSize : (pixel - tileSize + 1) / tileSize; }
bool finiteRect(const QRectF &rect) {
    return std::isfinite(rect.x()) && std::isfinite(rect.y()) && std::isfinite(rect.width()) && std::isfinite(rect.height());
}
bool finitePath(const QPainterPath &path) {
    for (int i = 0; i < path.elementCount(); ++i) {
        const auto point = path.elementAt(i);
        if (!std::isfinite(point.x) || !std::isfinite(point.y)) return false;
    }
    return true;
}
bool validTile(const TileKey &key, const QImage &image) {
    return qAbs(qint64(key.first)) <= 128 && qAbs(qint64(key.second)) <= 128
        && image.size() == QSize(tileSize, tileSize) && image.format() == QImage::Format_ARGB32_Premultiplied;
}
bool selected(const QImage &mask, int x, int y) { return mask.isNull() || mask.constScanLine(y)[x] != 0; }
QRgb coveragePixel(QRgb pixel, int coverage) {
    return qRgba((qRed(pixel) * coverage + 127) / 255, (qGreen(pixel) * coverage + 127) / 255,
                 (qBlue(pixel) * coverage + 127) / 255, (qAlpha(pixel) * coverage + 127) / 255);
}
QRgb over(QRgb source, QRgb destination) {
    const int inverse = 255 - qAlpha(source);
    const auto channel = [inverse](int s, int d) { return s + (d * inverse + 127) / 255; };
    return qRgba(channel(qRed(source), qRed(destination)), channel(qGreen(source), qGreen(destination)),
                 channel(qBlue(source), qBlue(destination)), channel(qAlpha(source), qAlpha(destination)));
}
struct SelectedTile {
    QImage original;
    QImage mask;
    QRect pixels;
};
using SelectedTiles = QHash<TileKey, SelectedTile>;
QImage denseSelectedSource(const SelectedTiles &tiles, const QRect &bounds) {
    // A 4K frame fits comfortably; widely separated sparse marks stay sparse.
    constexpr qint64 maxScratchBytes = 64 * 1024 * 1024;
    if (qint64(bounds.width()) * bounds.height() * sizeof(QRgb) > maxScratchBytes) return {};
    QImage image(bounds.size(), QImage::Format_ARGB32_Premultiplied);
    if (image.isNull()) return {}; // The sparse sampler remains a valid fallback.
    image.fill(Qt::transparent);
    for (auto tile = tiles.cbegin(); tile != tiles.cend(); ++tile) {
        const QPoint origin(tile.key().first * tileSize, tile.key().second * tileSize);
        const auto local = tile->pixels.translated(-origin);
        for (int y = local.top(); y <= local.bottom(); ++y) {
            const auto source = reinterpret_cast<const QRgb *>(tile->original.constScanLine(y));
            auto target = reinterpret_cast<QRgb *>(image.scanLine(origin.y() + y - bounds.top()))
                + (origin.x() + local.left() - bounds.left());
            if (tile->mask.isNull()) std::memcpy(target, source + local.left(), size_t(local.width()) * sizeof(QRgb));
            else {
                const auto mask = tile->mask.constScanLine(y);
                for (int x = local.left(); x <= local.right(); ++x) if (mask[x]) target[x - local.left()] = coveragePixel(source[x], mask[x]);
            }
        }
    }
    return image;
}
struct DenseSourceView {
    const QRgb *pixels = nullptr;
    qsizetype stride = 0;
    int width = 0, height = 0;
};
QRgb sourcePixel(const SelectedTiles &tiles, int x, int y) {
    const TileKey key{tileCoordinate(x), tileCoordinate(y)};
    const auto tile = tiles.constFind(key);
    if (tile == tiles.cend()) return 0;
    const int localX = x - key.first * tileSize, localY = y - key.second * tileSize;
    if (!selected(tile->mask, localX, localY)) return 0;
    const QRgb pixel = reinterpret_cast<const QRgb *>(tile->original.constScanLine(localY))[localX];
    return tile->mask.isNull() ? pixel : coveragePixel(pixel, tile->mask.constScanLine(localY)[localX]);
}
QRgb sample(const SelectedTiles &tiles, const QRect &bounds, const DenseSourceView &dense, QPointF point) {
    const qreal x = point.x() - .5, y = point.y() - .5;
    if (!std::isfinite(x) || !std::isfinite(y) || x <= bounds.left() - 1.0 || y <= bounds.top() - 1.0
        || x >= bounds.right() + 1.0 || y >= bounds.bottom() + 1.0) return 0;
    const int left = int(std::floor(x)), top = int(std::floor(y));
    const qreal fx = x - left, fy = y - top;
    std::array<QRgb, 4> pixels;
    if (dense.pixels) {
        const int localX = left - bounds.left(), localY = top - bounds.top();
        const auto first = localY >= 0 && localY < dense.height ? dense.pixels + localY * dense.stride : nullptr;
        const auto second = localY + 1 >= 0 && localY + 1 < dense.height ? dense.pixels + (localY + 1) * dense.stride : nullptr;
        const bool hasLeft = localX >= 0 && localX < dense.width, hasRight = localX + 1 >= 0 && localX + 1 < dense.width;
        pixels = {first && hasLeft ? first[localX] : 0, first && hasRight ? first[localX + 1] : 0,
                  second && hasLeft ? second[localX] : 0, second && hasRight ? second[localX + 1] : 0};
    } else pixels = {sourcePixel(tiles, left, top), sourcePixel(tiles, left + 1, top),
                     sourcePixel(tiles, left, top + 1), sourcePixel(tiles, left + 1, top + 1)};
    const std::array<qreal, 4> weights{(1 - fx) * (1 - fy), fx * (1 - fy), (1 - fx) * fy, fx * fy};
    qreal red = 0, green = 0, blue = 0, alpha = 0;
    for (int i = 0; i < 4; ++i) {
        red += qRed(pixels[i]) * weights[i]; green += qGreen(pixels[i]) * weights[i];
        blue += qBlue(pixels[i]) * weights[i]; alpha += qAlpha(pixels[i]) * weights[i];
    }
    return qRgba(qRound(red), qRound(green), qRound(blue), qRound(alpha));
}
bool integerTranslation(const QTransform &transform) {
    return transform.m11() == 1 && transform.m22() == 1 && transform.m12() == 0 && transform.m21() == 0
        && transform.dx() == std::round(transform.dx()) && transform.dy() == std::round(transform.dy());
}
}

QImage rasterSelectionMask(const QPainterPath &path, TileKey key, qreal featherRadius) {
    RasterSelectionCache cache(&path, featherRadius);
    const auto coverage = cache.tile(key);
    if (coverage.coverage == RasterSelectionTile::Coverage::Invalid) return {};
    if (coverage.coverage == RasterSelectionTile::Coverage::Partial) return coverage.mask;
    QImage image(tileSize, tileSize, QImage::Format_Grayscale8);
    image.fill(coverage.coverage == RasterSelectionTile::Coverage::Full ? 255 : 0);
    return image;
}
RasterSelectionCache::RasterSelectionCache(const QPainterPath *path, qreal featherRadius) { reset(path, featherRadius); }
void RasterSelectionCache::reset(const QPainterPath *path, qreal featherRadius) {
    const int radius = std::isfinite(featherRadius) ? qBound(0, qRound(featherRadius), 100) : 0;
    if (m_enabled == bool(path) && (!path || *path == m_path) && radius == m_feather) return;
    m_feather = radius;
    m_enabled = bool(path); m_path = path ? *path : QPainterPath();
    m_empty = !path || path->isEmpty() || !finitePath(*path);
    m_bounds = m_empty ? QRectF() : m_path.boundingRect().adjusted(-m_feather, -m_feather, m_feather, m_feather);
    m_tiles.clear();
}
RasterSelectionTile RasterSelectionCache::tile(TileKey key) const {
    using Coverage = RasterSelectionTile::Coverage;
    if (!m_enabled) return {Coverage::Full, {}};
    if (m_empty) return {Coverage::Empty, {}};
    if (const auto cached = m_tiles.object(key)) return *cached;
    const QRectF rectangle(qreal(key.first) * tileSize, qreal(key.second) * tileSize, tileSize, tileSize);
    RasterSelectionTile result;
    if (m_bounds.intersects(rectangle)) {
        if (m_path.contains(rectangle.adjusted(-m_feather, -m_feather, m_feather, m_feather))) result.coverage = Coverage::Full;
        else if (m_path.intersects(rectangle.adjusted(-m_feather, -m_feather, m_feather, m_feather))) {
            QImage mask(tileSize + m_feather * 2, tileSize + m_feather * 2, QImage::Format_Grayscale8);
            if (mask.isNull()) return {Coverage::Invalid, {}};
            mask.fill(0);
            {
                QPainter painter(&mask);
                painter.setRenderHint(QPainter::Antialiasing, false);
                painter.translate(-rectangle.topLeft() + QPointF(m_feather, m_feather));
                painter.setPen(Qt::NoPen); painter.setBrush(Qt::white); painter.drawPath(m_path);
            }
            if (m_feather) {
                // Separable box feather with a halo avoids seams at tile borders.
                // Memory is bounded to a single 456×456 scratch tile.
                QImage horizontal(mask.size(), QImage::Format_Grayscale8);
                QImage blurred(tileSize, tileSize, QImage::Format_Grayscale8);
                if (horizontal.isNull() || blurred.isNull()) return {Coverage::Invalid, {}};
                const int diameter = 2 * m_feather + 1;
                for (int y = 0; y < mask.height(); ++y) {
                    const auto source = mask.constScanLine(y); auto target = horizontal.scanLine(y);
                    int sum = 0; for (int x = 0; x < diameter; ++x) sum += source[x];
                    for (int x = 0; x < tileSize; ++x) {
                        target[x + m_feather] = uchar((sum + diameter / 2) / diameter);
                        if (x + diameter < mask.width()) sum += source[x + diameter] - source[x];
                    }
                }
                for (int x = 0; x < tileSize; ++x) {
                    int sum = 0; for (int y = 0; y < diameter; ++y) sum += horizontal.constScanLine(y)[x + m_feather];
                    for (int y = 0; y < tileSize; ++y) {
                        blurred.scanLine(y)[x] = uchar((sum + diameter / 2) / diameter);
                        if (y + diameter < horizontal.height()) sum += horizontal.constScanLine(y + diameter)[x + m_feather] - horizontal.constScanLine(y)[x + m_feather];
                    }
                }
                mask = std::move(blurred);
            }
            bool hasEmpty = false, hasFull = false;
            for (int y = 0; y < tileSize && !(hasEmpty && hasFull); ++y) {
                const auto row = mask.constScanLine(y);
                for (int x = 0; x < tileSize; ++x) {
                    hasEmpty |= row[x] != 255; hasFull |= row[x] != 0;
                    if (hasEmpty && hasFull) break;
                }
            }
            result.coverage = !hasFull ? Coverage::Empty : !hasEmpty ? Coverage::Full : Coverage::Partial;
            if (result.coverage == Coverage::Partial) result.mask = std::move(mask);
        }
    }
    const int cost = int(sizeof(RasterSelectionTile) + result.mask.sizeInBytes());
    m_tiles.insert(key, new RasterSelectionTile(result), cost);
    return result;
}
QRectF selectedPixelBounds(const RasterLayer &layer, const QPainterPath *selection, qreal featherRadius) {
    if (layer.group || (selection && (selection->isEmpty() || !finitePath(*selection)))) return {};
    RasterSelectionCache cache(selection, featherRadius);
    QRect result;
    for (auto tile = layer.tiles.cbegin(); tile != layer.tiles.cend(); ++tile) {
        if (!validTile(tile.key(), tile.value())) continue;
        const QPoint origin(tile.key().first * tileSize, tile.key().second * tileSize);
        const auto coverage = cache.tile(tile.key());
        if (coverage.coverage == RasterSelectionTile::Coverage::Empty) continue;
        if (coverage.coverage == RasterSelectionTile::Coverage::Invalid) return {};
        int left = tileSize, right = -1, top = tileSize, bottom = -1;
        for (int y = 0; y < tileSize; ++y) {
            const auto row = reinterpret_cast<const QRgb *>(tile->constScanLine(y));
            const auto mask = coverage.mask.isNull() ? nullptr : coverage.mask.constScanLine(y);
            int first = 0;
            while (first < tileSize && (!qAlpha(row[first]) || (mask && !mask[first]))) ++first;
            if (first == tileSize) continue;
            int last = tileSize - 1;
            while (last > first && (!qAlpha(row[last]) || (mask && !mask[last]))) --last;
            left = qMin(left, first); right = qMax(right, last); top = qMin(top, y); bottom = y;
        }
        if (right >= left) result |= QRect(origin + QPoint(left, top), origin + QPoint(right, bottom));
    }
    return result;
}
QTransform rasterTransform(const QRectF &bounds, const TransformParameters &parameters) {
    const auto center = bounds.center();
    QTransform transform;
    transform.translate(center.x() + parameters.translation.x(), center.y() + parameters.translation.y());
    transform.rotate(parameters.rotation); transform.scale(parameters.scaleX, parameters.scaleY);
    transform.translate(-center.x(), -center.y());
    return transform;
}
bool transformRasterLayer(const RasterLayer &original, const QPainterPath *selection,
                          const QTransform &transform, QRectF drawingBounds, int maxTiles,
                          RasterLayer *result, QString *error, qreal featherRadius) {
    const auto fail = [&](const QString &message) { if (error) *error = message; return false; };
    if (!result) return fail("No transform destination was provided.");
    if (original.group) return fail("Select a drawing layer to transform.");
    if (!finiteRect(drawingBounds) || drawingBounds.isEmpty()
        || drawingBounds.left() < -100000 || drawingBounds.top() < -100000
        || drawingBounds.right() > 100000 || drawingBounds.bottom() > 100000)
        return fail("The drawing bounds are invalid.");
    const std::array<qreal, 9> values{transform.m11(), transform.m12(), transform.m13(), transform.m21(), transform.m22(),
                                     transform.m23(), transform.m31(), transform.m32(), transform.m33()};
    for (auto value : values) if (!std::isfinite(value)) return fail("The transform contains an invalid value.");
    if (!transform.isAffine() || transform.m33() != 1) return fail("Only affine drawing transforms are supported.");
    bool invertible = false; const auto inverse = transform.inverted(&invertible);
    if (!invertible) return fail("The transform must have a nonzero scale.");
    if (selection && !finitePath(*selection)) return fail("The selection is invalid.");
    if (maxTiles < 0 || original.tiles.size() > maxProjectTiles) return fail("The layer exceeds the tile memory limit.");
    maxTiles = qMin(maxTiles, maxProjectTiles);
    for (auto tile = original.tiles.cbegin(); tile != original.tiles.cend(); ++tile)
        if (!validTile(tile.key(), tile.value())) return fail("The drawing contains an invalid tile.");
    if (transform.isIdentity()) {
        if (original.tiles.size() > maxTiles) return fail("The transform exceeds the tile memory limit.");
        *result = original; if (error) error->clear(); return true;
    }

    auto candidate = original;
    RasterSelectionCache selectionCache(selection, featherRadius);
    const bool translationOnly = integerTranslation(transform);
    // Retain only complete destination pixels within the drawing extent. Source
    // pixels remain untouched until this candidate is successfully committed.
    const int clipLeft = int(std::ceil(drawingBounds.left())), clipTop = int(std::ceil(drawingBounds.top()));
    const int clipRight = int(std::floor(drawingBounds.right())), clipBottom = int(std::floor(drawingBounds.bottom()));
    const QRect pixelExtent(clipLeft, clipTop, qMax(0, clipRight - clipLeft), qMax(0, clipBottom - clipTop));
    SelectedTiles sources;
    QRect sourceBounds;
    for (auto tile = original.tiles.cbegin(); tile != original.tiles.cend(); ++tile) {
        const QPoint origin(tile.key().first * tileSize, tile.key().second * tileSize);
        const auto coverage = selectionCache.tile(tile.key());
        if (coverage.coverage == RasterSelectionTile::Coverage::Empty) continue;
        if (coverage.coverage == RasterSelectionTile::Coverage::Invalid) return fail("There is not enough memory for the selection.");
        const auto &mask = coverage.mask;
        int left = tileSize, right = -1, top = tileSize, bottom = -1;
        bool unselectedPixels = false;
        for (int y = 0; y < tileSize; ++y) {
            const auto row = reinterpret_cast<const QRgb *>(tile->constScanLine(y));
            int first = tileSize, last = -1;
            if (mask.isNull()) {
                first = 0;
                while (first < tileSize && !qAlpha(row[first])) ++first;
                if (first < tileSize) {
                    last = tileSize - 1;
                    while (last > first && !qAlpha(row[last])) --last;
                }
            } else {
                const auto selectedRow = mask.constScanLine(y);
                for (int x = 0; x < tileSize; ++x) if (qAlpha(row[x])) {
                    if (selectedRow[x]) { first = qMin(first, x); last = x; }
                    if (selectedRow[x] < 255) unselectedPixels = true;
                }
            }
            if (last < first) continue;
            left = qMin(left, first); right = qMax(right, last); top = qMin(top, y); bottom = y;
        }
        if (right < left) continue;
        const QRect selectedBounds(origin + QPoint(left, top), origin + QPoint(right, bottom));
        const auto movedBounds = transform.mapRect(QRectF(selectedBounds));
        if (!finiteRect(movedBounds) || movedBounds.left() < -100000 || movedBounds.top() < -100000
            || movedBounds.right() > 100000 || movedBounds.bottom() > 100000)
            return fail("The transform exceeds the supported drawing coordinates.");
        sources.insert(tile.key(), {tile.value(), mask, selectedBounds}); sourceBounds |= selectedBounds;
        if (!unselectedPixels) candidate.tiles.remove(tile.key());
        else {
            auto cut = tile.value().copy();
            if (cut.isNull()) return fail("There is not enough memory for the transform.");
            for (int y = 0; y < tileSize; ++y) {
                auto row = reinterpret_cast<QRgb *>(cut.scanLine(y));
                for (int x = 0; x < tileSize; ++x) if (selected(mask, x, y)) row[x] = mask.isNull() ? 0 : coveragePixel(row[x], 255 - mask.constScanLine(y)[x]);
            }
            candidate.tiles[tile.key()] = cut;
        }
    }
    if (sourceBounds.isEmpty()) {
        if (candidate.tiles.size() > maxTiles) return fail("The transform exceeds the tile memory limit.");
        *result = candidate; if (error) error->clear(); return true;
    }
    if (translationOnly) {
        if (std::abs(transform.dx()) > 100000 || std::abs(transform.dy()) > 100000)
            return fail("The transform exceeds the supported drawing coordinates.");
        const int dx = int(transform.dx()), dy = int(transform.dy());
        for (auto source = sources.cbegin(); source != sources.cend(); ++source) {
            const QPoint origin(source.key().first * tileSize, source.key().second * tileSize);
            auto selectedImage = source->original;
            if (!source->mask.isNull()) {
                selectedImage = selectedImage.copy();
                if (selectedImage.isNull()) return fail("There is not enough memory for the transform.");
                for (int y = 0; y < tileSize; ++y) {
                    auto row = reinterpret_cast<QRgb *>(selectedImage.scanLine(y));
                    const auto selectedRow = source->mask.constScanLine(y);
                    for (int x = 0; x < tileSize; ++x) row[x] = coveragePixel(row[x], selectedRow[x]);
                }
            }
            const auto destination = source->pixels.translated(dx, dy).intersected(pixelExtent);
            if (destination.isEmpty()) continue;
            for (int ty = tileCoordinate(destination.top()); ty <= tileCoordinate(destination.bottom()); ++ty)
                for (int tx = tileCoordinate(destination.left()); tx <= tileCoordinate(destination.right()); ++tx) {
                    const TileKey key{tx, ty}; const QPoint tileOrigin(tx * tileSize, ty * tileSize);
                    const auto part = destination.intersected(QRect(tileOrigin, QSize(tileSize, tileSize)));
                    const auto sourcePart = part.translated(-QPoint(dx, dy) - origin);
                    bool hasPixels = false;
                    for (int y = sourcePart.top(); y <= sourcePart.bottom() && !hasPixels; ++y) {
                        const auto row = reinterpret_cast<const QRgb *>(selectedImage.constScanLine(y));
                        for (int x = sourcePart.left(); x <= sourcePart.right(); ++x)
                            if (qAlpha(row[x])) { hasPixels = true; break; }
                    }
                    if (!hasPixels) continue;
                    if (std::abs(tx) > 128 || std::abs(ty) > 128) return fail("The transformed pixels exceed the supported drawing coordinates.");
                    if (!candidate.tiles.contains(key)) {
                        if (candidate.tiles.size() >= maxTiles) return fail("The transform exceeds the tile memory limit.");
                        QImage image(tileSize, tileSize, QImage::Format_ARGB32_Premultiplied);
                        if (image.isNull()) return fail("There is not enough memory for the transform.");
                        image.fill(Qt::transparent); candidate.tiles.insert(key, image);
                    }
                    auto &image = candidate.tiles[key];
                    if (!image.isDetached()) {
                        image = image.copy();
                        if (image.isNull()) return fail("There is not enough memory for the transform.");
                    }
                    QPainter painter(&image);
                    painter.drawImage(part.translated(-tileOrigin), selectedImage, sourcePart);
                }
        }
    } else {
        // Enumerate transformed source-tile neighborhoods, not a full document
        // rectangle. Inverse sampling reads adjacent source tiles seamlessly.
        QSet<TileKey> outputKeys;
        for (const auto &source : sources) {
            auto area = transform.mapRect(QRectF(source.pixels).adjusted(-.5, -.5, .5, .5));
            if (!finiteRect(area) || area.left() < -100000 || area.top() < -100000 || area.right() > 100000 || area.bottom() > 100000)
                return fail("The transform exceeds the supported drawing coordinates.");
            area = area.intersected(drawingBounds);
            if (area.isEmpty()) continue;
            const int left = int(std::floor(area.left() / tileSize)), right = int(std::floor(area.right() / tileSize));
            const int top = int(std::floor(area.top() / tileSize)), bottom = int(std::floor(area.bottom() / tileSize));
            if (qint64(right - left + 1) * (bottom - top + 1) > 4096) return fail("The transform is too large to preview.");
            for (int y = top; y <= bottom; ++y) for (int x = left; x <= right; ++x) {
                outputKeys.insert({x, y});
                if (outputKeys.size() > 4096) return fail("The transform is too large to preview.");
            }
        }
        const auto denseImage = denseSelectedSource(sources, sourceBounds);
        const DenseSourceView dense{reinterpret_cast<const QRgb *>(denseImage.constBits()),
                                    denseImage.bytesPerLine() / qsizetype(sizeof(QRgb)), denseImage.width(), denseImage.height()};
        for (const auto &key : outputKeys) {
            const int left = key.first * tileSize, top = key.second * tileSize;
            const bool tileInside = drawingBounds.contains(QRectF(left, top, tileSize, tileSize));
            QImage *target = nullptr;
            for (int y = top; y < top + tileSize; ++y) {
                QRgb *targetRow = nullptr;
                for (int x = left; x < left + tileSize; ++x) {
                    if (!tileInside && !pixelExtent.contains(x, y)) continue;
                    const auto color = sample(sources, sourceBounds, dense, inverse.map(QPointF(x + .5, y + .5)));
                    if (!qAlpha(color)) continue;
                    if (!target) {
                        if (std::abs(key.first) > 128 || std::abs(key.second) > 128)
                            return fail("The transformed pixels exceed the supported drawing coordinates.");
                        auto found = candidate.tiles.find(key);
                        if (found == candidate.tiles.end()) {
                            if (candidate.tiles.size() >= maxTiles) return fail("The transform exceeds the tile memory limit.");
                            QImage image(tileSize, tileSize, QImage::Format_ARGB32_Premultiplied);
                            if (image.isNull()) return fail("There is not enough memory for the transform.");
                            image.fill(Qt::transparent); found = candidate.tiles.insert(key, image);
                        }
                        target = &found.value();
                        if (!target->isDetached()) {
                            *target = target->copy();
                            if (target->isNull()) return fail("There is not enough memory for the transform.");
                        }
                    }
                    if (!targetRow) targetRow = reinterpret_cast<QRgb *>(target->scanLine(y - top));
                    const int at = x - left; targetRow[at] = over(color, targetRow[at]);
                }
            }
        }
    }
    if (candidate.tiles.size() > maxTiles) return fail("The transform exceeds the tile memory limit.");
    *result = candidate; if (error) error->clear(); return true;
}
} // namespace toon
