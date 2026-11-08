#include "raster_tools.h"
#include "animation.h"
#include "keyframe_render.h"
#include "selection_geometry.h"
#include <QPainter>
#include <QPen>
#include <QUuid>
#include <QHash>
#include <QLineF>
#include <QSet>
#include <algorithm>
#include <cmath>
#include <deque>
#include <functional>
#include <new>

namespace toon {
namespace {
using Coverage = RasterSelectionTile::Coverage;
constexpr qint64 maxSamplePixels = 32 * 1024 * 1024;
constexpr qint64 maxEnclosePixels = 16 * 1024 * 1024;
constexpr int maxEnclosePathElements = 8192;
constexpr int maxSelectionElements = 262144;
int tileCoordinate(int value) { return int(std::floor(value / qreal(tileSize))); }
bool finite(QPointF point) { return std::isfinite(point.x()) && std::isfinite(point.y()) && std::abs(point.x()) <= 1000000 && std::abs(point.y()) <= 1000000; }
bool ready(Document *document, bool paint = true) {
    if (!document || !document->isOpen() || document->editingGestureActive() || document->layerPropertyEditActive()) return false;
    if(paint){const auto reason=keyframeRasterEditReason(document);if(!reason.isEmpty()){emit document->warning(reason);return false;}}
    if (paint && !layerCanPaint(document->activeShot().layers, document->activeLayer())) {
        emit document->warning("Choose a visible, unlocked raster layer."); return false;
    }
    return true;
}
QRgb multiply(QRgb pixel, int coverage) {
    return qRgba((qRed(pixel) * coverage + 127) / 255, (qGreen(pixel) * coverage + 127) / 255,
                 (qBlue(pixel) * coverage + 127) / 255, (qAlpha(pixel) * coverage + 127) / 255);
}
QRgb mix(QRgb first, QRgb second, qreal amount) {
    amount = qBound(0.0, amount, 1.0);
    const auto channel = [amount](int a, int b) { return qRound(a + (b - a) * amount); };
    return qRgba(channel(qRed(first), qRed(second)), channel(qGreen(first), qGreen(second)),
                 channel(qBlue(first), qBlue(second)), channel(qAlpha(first), qAlpha(second)));
}

int allowance(const Project &project, const RasterLayer &layer) {
    int count = 0; for (const auto *shot : projectArtworkShots(project)) for (const auto &entry : shot->layers) count += entry.tiles.size();
    return maxProjectTiles - count + layer.tiles.size();
}
bool hasPixels(const QImage &image) {
    for (int y = 0; y < image.height(); ++y) {
        const auto row = reinterpret_cast<const QRgb *>(image.constScanLine(y));
        for (int x = 0; x < image.width(); ++x) if (qAlpha(row[x])) return true;
    }
    return false;
}
QImage renderSource(Document *document, const QRect &area, RasterSampleSource source, const QString &referenceId,
                    bool samplePixel = false, const Shot *displayShot = nullptr) {
    if (int(source) < int(RasterSampleSource::ActiveLayer) || int(source) > int(RasterSampleSource::MarkedReferences)) return {};
    if (source == RasterSampleSource::MarkedReferences) {
        const auto reason = RasterTools::referenceSourceReason(document);
        if (!reason.isEmpty()) { emit document->warning(reason); return {}; }
    }
    if (area.isEmpty() || qint64(area.width()) * area.height() > maxSamplePixels) {
        emit document->warning("The sampling region exceeds the 32-million-pixel working limit."); return {};
    }
    QImage image(area.size(), QImage::Format_ARGB32_Premultiplied);
    if (image.isNull()) { emit document->warning("There is not enough memory to sample the artwork."); return {}; }
    image.fill(Qt::transparent);
    const auto &shot = document->activeShot();
    // Source mode removes layer keys but still evaluates visible geometry and
    // effect halos, exactly as Canvas::displayShot does. In particular, an
    // off-sheet path can cast a shadow into a fill/sampling region.
    auto displayedShot=displayShot && source==RasterSampleSource::AllLayers ? *displayShot : shot;
    if(document->keyframeSourceEditing())displayedShot.layerAnimations.clear();
    if (source != RasterSampleSource::AllLayers) {
        const auto present = activeLayerIds(shot, document->currentFrame());
        QSet<QString> targets;
        if (source == RasterSampleSource::MarkedReferences) {
            for (const auto &layer : shot.layers) if (layer.fillReference && present.contains(layer.id)) targets.insert(layer.id);
            const auto markedTargets = targets;
            for (int i = 0; i < shot.layers.size(); ++i) if (targets.contains(shot.layers[i].id)) {
                for (const auto &root : markedTargets) if (root != shot.layers[i].id && layerIsDescendant(shot.layers, i, root)) {
                    targets.remove(shot.layers[i].id); break;
                }
            }
        } else targets.insert(source == RasterSampleSource::ReferenceLayer ? referenceId : shot.layers[document->activeLayer()].id);
        // Editable objects are rebuilt during evaluation. Normalize root eyes
        // before that stage so hidden or faded references have current geometry,
        // even when an unmarked ancestor moves them. Folder children retain
        // their own visibility unless they are needed ancestors of a root.
        for (const auto &target : targets) {
            QString next = target; QSet<QString> visited;
            while (!next.isEmpty() && !visited.contains(next)) {
                visited.insert(next); bool found = false;
                for (auto &layer : displayedShot.layers) if (layer.id == next) {
                    layer.visible = true; layer.opacity = 1; next = layer.parentId; found = true; break;
                }
                if (!found) break;
            }
        }
    }
    QString evaluationError;
    const auto layers=evaluateLayers(document->project(),displayedShot,document->currentFrame(),false,
        samplePixel?QRectF(area):document->drawingBounds(),&evaluationError);
    if(!evaluationError.isEmpty()){emit document->warning(evaluationError);return {};}
    QPainter painter(&image); painter.translate(-area.topLeft());
    const bool animatedSampling = !document->keyframeSourceEditing() && !shot.layerAnimations.isEmpty();
    if (source == RasterSampleSource::MarkedReferences) {
        QSet<QString> roots;
        for (const auto &layer : layers) if (layer.fillReference) roots.insert(layer.id);
        QVector<RasterLayer> isolated;
        for (int index = 0; index < layers.size(); ++index) {
            const auto &original = layers[index];
            bool inMarkedFolder = false;
            for (const auto &root : roots) if (root != original.id && layerIsDescendant(layers, index, root)) { inMarkedFolder = true; break; }
            if (inMarkedFolder) { isolated.append(original); continue; }
            if (!roots.contains(original.id) || original.adjustment) continue;
            auto layer = original;
            if (!document->keyframeSourceEditing()) layer.renderTransform = worldLayerTransform(shot, layer.id, document->currentFrame());
            layer.parentId.clear(); layer.visible = true; layer.opacity = 1;
            layer.clipped = false; layer.blendMode = LayerBlendMode::Normal;
            isolated.append(std::move(layer));
        }
        paintLayers(painter, isolated, true, nullptr, animatedSampling, {document->project().width, document->project().height});
    }
    else if (source == RasterSampleSource::AllLayers) paintLayers(painter, layers, true, nullptr, animatedSampling, {document->project().width, document->project().height});
    else {
        const QString selectedId=source==RasterSampleSource::ReferenceLayer?referenceId:shot.layers[document->activeLayer()].id;
        if(source==RasterSampleSource::ReferenceLayer&&std::none_of(shot.layers.cbegin(),shot.layers.cend(),
            [&](const auto &layer){return layer.id==selectedId;})) {
            emit document->warning("Choose a reference layer for sampling.");return {};
        }
        int index=-1;for(int i=0;i<layers.size();++i)if(layers[i].id==selectedId){index=i;break;}
        if(index<0) {
            emit document->warning("Choose a drawing exposed at the current frame for sampling."); return {};
        }
        auto layer = layers[index];
        if(!document->keyframeSourceEditing())layer.renderTransform=worldLayerTransform(shot,layer.id,document->currentFrame());
        // A selected reference is sampled in isolation, independent of its eye,
        // layer opacity, clipping, or parent. Groups retain their composited children.
        if (layer.group) {
            QVector<RasterLayer> isolated;
            layer.parentId.clear(); layer.visible = true; layer.opacity = 1; isolated.append(layer);
            for (int i = 0; i < layers.size(); ++i) if (layerIsDescendant(layers, i, layer.id)) isolated.append(layers[i]);
            paintLayers(painter, isolated, true, nullptr, animatedSampling, {document->project().width, document->project().height});
        } else {
            layer.visible = true; layer.parentId.clear(); layer.clipped = false; layer.opacity = 1; layer.blendMode = LayerBlendMode::Normal;
            paintLayers(painter, {layer}, true, nullptr, animatedSampling, {document->project().width, document->project().height});
        }
    }
    painter.end();
    if(const auto error=keyframeRenderError();!error.isEmpty()){emit document->warning(error);return {};}
    if (source == RasterSampleSource::MarkedReferences && !samplePixel && !hasPixels(image)) {
        // A selection can lie inside a closed shape while excluding all of its
        // ink. Verify the whole source, within the shared pixel budget, only
        // for an empty crop so that valid
        // interior fills work without letting truly empty references fill it.
        const auto whole = document->drawingBounds().toAlignedRect();
        if (area == whole) {
            emit document->warning("The marked reference layers contain no visible reference pixels."); return {};
        }
        if (qint64(area.width()) * area.height() + qint64(whole.width()) * whole.height() > maxSamplePixels) {
            emit document->warning("Checking empty reference boundaries exceeds the 32-million-pixel sampling budget. Reduce the drawing or selection size."); return {};
        }
        if (renderSource(document, whole, source, referenceId, false, displayShot).isNull()) return {};
    }
    return image;
}
int colorDistance(QRgb first, QRgb second) {
    // Premultiplied distance avoids undefined RGB differences in transparent pixels.
    return std::max({std::abs(qRed(first) - qRed(second)), std::abs(qGreen(first) - qGreen(second)),
                     std::abs(qBlue(first) - qBlue(second)), std::abs(qAlpha(first) - qAlpha(second))});
}
QImage morphology(const QImage &source, int radius, bool maximum, bool zeroOutside = false) {
    QImage horizontal(source.size(), QImage::Format_Grayscale8), result(source.size(), QImage::Format_Grayscale8);
    if (horizontal.isNull() || result.isNull()) return {};
    const auto run = [radius, maximum, zeroOutside](int count, auto value, auto assign) {
        std::deque<QPair<int, int>> queue;
        for (int i = -radius; i < count + radius; ++i) {
            const int v = zeroOutside && (i < 0 || i >= count) ? 0 : value(qBound(0, i, count - 1));
            while (!queue.empty() && (maximum ? queue.back().second <= v : queue.back().second >= v)) queue.pop_back();
            queue.emplace_back(i, v);
            while (queue.front().first < i - 2 * radius) queue.pop_front();
            if (i >= radius) assign(i - radius, queue.front().second);
        }
    };
    for (int y = 0; y < source.height(); ++y) {
        const auto row = source.constScanLine(y); auto target = horizontal.scanLine(y);
        run(source.width(), [&](int x) { return row[x]; }, [&](int x, int value) { target[x] = uchar(value); });
    }
    for (int x = 0; x < source.width(); ++x)
        run(source.height(), [&](int y) { return horizontal.constScanLine(y)[x]; }, [&](int y, int value) { result.scanLine(y)[x] = uchar(value); });
    return result;
}
QImage colorMask(const QImage &source, QPoint seed, int tolerance, int gapClosing, bool contiguous) {
    if (source.isNull() || !source.rect().contains(seed)) return {};
    const QRgb target = source.pixel(seed);
    QImage blocked(source.size(), QImage::Format_Grayscale8);
    if (blocked.isNull()) return {};
    for (int y = 0; y < source.height(); ++y) {
        const auto row = reinterpret_cast<const QRgb *>(source.constScanLine(y)); auto result = blocked.scanLine(y);
        for (int x = 0; x < source.width(); ++x) result[x] = colorDistance(row[x], target) <= tolerance ? 0 : 255;
    }
    if (gapClosing > 0 && contiguous) {
        const auto dilated = morphology(blocked, gapClosing, true);
        if (dilated.isNull()) return {};
        blocked = morphology(dilated, gapClosing, false);
        if (blocked.isNull()) return {};
    }
    if (!contiguous) {
        for (int y = 0; y < blocked.height(); ++y) {
            auto row = blocked.scanLine(y); for (int x = 0; x < blocked.width(); ++x) row[x] = 255 - row[x];
        }
        return blocked;
    }
    QImage result(source.size(), QImage::Format_Grayscale8); if (result.isNull()) return {};
    result.fill(0);
    if (blocked.constScanLine(seed.y())[seed.x()]) return result;
    // Scanline flood: each visited pixel is marked once. The stack stores spans
    // rather than an artwork-sized queue of individual points.
    QVector<QPoint> stack{seed};
    while (!stack.isEmpty()) {
        const auto point = stack.takeLast();
        const auto barriers = blocked.constScanLine(point.y()); auto row = result.scanLine(point.y());
        if (barriers[point.x()] || row[point.x()]) continue;
        int left = point.x(), right = point.x();
        while (left > 0 && !barriers[left - 1] && !row[left - 1]) --left;
        while (right + 1 < source.width() && !barriers[right + 1] && !row[right + 1]) ++right;
        std::fill(row + left, row + right + 1, uchar(255));
        for (int y : {point.y() - 1, point.y() + 1}) if (y >= 0 && y < source.height()) {
            const auto nextBarrier = blocked.constScanLine(y), nextRow = result.constScanLine(y);
            bool span = false;
            for (int x = left; x <= right; ++x) {
                const bool available = !nextBarrier[x] && !nextRow[x];
                if (available && !span) stack.append({x, y});
                span = available;
            }
        }
    }
    return result;
}

// A flood's tolerance locates the region. Recover adjacent antialiased pixels
// separately so a paint fill reaches the ink without changing color selection.
// Coverage records how much of the seed/background color an edge contains.
QImage bucketEdgeCoverage(const QImage &source, QPoint seed, int tolerance, QImage *mask, bool *hasEdges) {
    QImage coverage(source.size(), QImage::Format_Grayscale8);
    if (coverage.isNull()) return {};
    coverage.fill(0);
    *hasEdges = false;
    const QRgb target = source.pixel(seed);
    const auto backgroundWeight = [&](int x, int y, QRgb pixel) {
        if (!qAlpha(target)) return qAlpha(pixel) > 0 && qAlpha(pixel) < 255 ? 255 - qAlpha(pixel) : 0;
        const int distance = colorDistance(pixel, target);
        if (!distance) return 0;
        // A solid neighboring color is a boundary, not an antialias ramp. Find
        // a stronger, collinear color within two pixels before extending into it.
        int peakDistance = distance;
        for (int ny = std::max(0, y - 2); ny <= std::min(source.height() - 1, y + 2); ++ny) {
            const auto row = reinterpret_cast<const QRgb *>(source.constScanLine(ny));
            for (int nx = std::max(0, x - 2); nx <= std::min(source.width() - 1, x + 2); ++nx) {
                const int candidateDistance = colorDistance(row[nx], target);
                if (candidateDistance <= peakDistance || candidateDistance < std::max(32, tolerance + 8)) continue;
                const qreal fraction = qreal(distance) / candidateDistance;
                const auto aligned = [fraction](int a, int b, int c) { return std::abs((b - a) - (c - a) * fraction) <= 2; };
                if (aligned(qRed(target), qRed(pixel), qRed(row[nx])) && aligned(qGreen(target), qGreen(pixel), qGreen(row[nx])) &&
                    aligned(qBlue(target), qBlue(pixel), qBlue(row[nx])) && aligned(qAlpha(target), qAlpha(pixel), qAlpha(row[nx])))
                    peakDistance = candidateDistance;
            }
        }
        return peakDistance > distance ? qBound(1, qRound(255.0 * (1.0 - qreal(distance) / peakDistance)), 254) : 0;
    };
    // Two pixels cover ordinary raster antialiasing without an uncontrolled
    // grow. Matching but disconnected regions (including closed gaps) have no
    // edge coverage, so this cannot bridge them or cross an opaque line core.
    for (int pass = 0; pass < 2; ++pass) {
        bool extended = false;
        const QImage previous = *mask;
        for (int y = 0; y < source.height(); ++y) {
            const auto sourceRow = reinterpret_cast<const QRgb *>(source.constScanLine(y));
            const auto oldRow = previous.constScanLine(y);
            auto result = mask->scanLine(y), weights = coverage.scanLine(y);
            for (int x = 0; x < source.width(); ++x) {
                if (weights[x] || sourceRow[x] == target) continue;
                bool adjacent = oldRow[x] != 0;
                for (int ny = std::max(0, y - 1); !adjacent && ny <= std::min(source.height() - 1, y + 1); ++ny) {
                    const auto neighbors = previous.constScanLine(ny);
                    for (int nx = std::max(0, x - 1); nx <= std::min(source.width() - 1, x + 1); ++nx) adjacent |= neighbors[nx] != 0;
                }
                if (!adjacent) continue;
                const int weight = backgroundWeight(x, y, sourceRow[x]);
                if (weight) { weights[x] = uchar(weight); result[x] = 255; *hasEdges = true; extended |= !oldRow[x]; }
            }
        }
        if (!extended) break;
    }
    return coverage;
}
QRgb layerPixel(const RasterLayer &layer, QPoint point) {
    const TileKey key{tileCoordinate(point.x()), tileCoordinate(point.y())};
    const auto tile = layer.tiles.constFind(key);
    return tile == layer.tiles.cend() ? 0 : tile->pixel(point.x() - key.first * tileSize, point.y() - key.second * tileSize);
}

bool ownsSampledPixel(const RasterFillSettings &settings, const RasterLayer &layer,
                     QRgb sampled, QPoint point, bool conservative = false) {
    if (settings.source == RasterSampleSource::ActiveLayer ||
        (settings.source == RasterSampleSource::ReferenceLayer && settings.referenceLayerId == layer.id)) return true;
    if (settings.source != RasterSampleSource::AllLayers && settings.source != RasterSampleSource::MarkedReferences) return false;
    const QRgb original = layerPixel(layer, point);
    // Composite opacity, blending, masks or effects can change a layer's ink
    // before sampling. A nonwhite raw ink pixel cannot safely be assumed to belong
    // exclusively to another reference merely because its rendered value differs.
    const bool originalInk = qAlpha(original) > std::min({qRed(original), qGreen(original), qBlue(original)});
    return conservative ? originalInk : qAlpha(sampled) && original == sampled;
}

QImage adjustBucketMask(const QImage &mask, const QImage &source, const QImage &edges,
                        QPoint origin, QRgb target, int tolerance, int expansion,
                        const RasterFillSettings &settings, const RasterLayer &layer) {
    if (!expansion) return mask;
    auto result = morphology(mask, std::abs(expansion), expansion > 0, expansion < 0);
    if (result.isNull() || expansion < 0) return result;
    for (int y = 0; y < result.height(); ++y) {
        auto row = result.scanLine(y); const auto original = mask.constScanLine(y);
        const auto sampled = reinterpret_cast<const QRgb *>(source.constScanLine(y));
        const auto edge = edges.isNull() ? nullptr : edges.constScanLine(y);
        for (int x = 0; x < result.width(); ++x) if (row[x] && !original[x]) {
            // Grow beneath a separate reference, but never bridge into another
            // disconnected background region. Solid source ink stays unchanged
            // when painting on the sampled layer itself.
            if (colorDistance(sampled[x], target) <= tolerance ||
                (ownsSampledPixel(settings, layer, sampled[x], origin + QPoint(x, y), true) && (!edge || !edge[x]))) row[x] = 0;
        }
    }
    return result;
}

QImage gestureCoverage(const QPainterPath &path, const QRect &area) {
    QImage result(area.size(), QImage::Format_Grayscale8);
    if (result.isNull()) return {};
    result.fill(0); RasterSelectionCache cache(&path, 0);
    for (int ty = tileCoordinate(area.top()); ty <= tileCoordinate(area.bottom()); ++ty)
        for (int tx = tileCoordinate(area.left()); tx <= tileCoordinate(area.right()); ++tx) {
            const auto tile = cache.tile({tx, ty});
            if (tile.coverage == Coverage::Invalid) return {};
            if (tile.coverage == Coverage::Empty) continue;
            const QPoint origin(tx * tileSize, ty * tileSize);
            const QRect part = area.intersected(QRect(origin, QSize(tileSize, tileSize)));
            for (int y = part.top(); y <= part.bottom(); ++y) {
                auto row = result.scanLine(y - area.top());
                const auto selected = tile.mask.isNull() ? nullptr : tile.mask.constScanLine(y - origin.y());
                for (int x = part.left(); x <= part.right(); ++x)
                    row[x - area.left()] = selected ? selected[x - origin.x()] : 255;
            }
        }
    return result;
}

// Transparent/white paper has zero ink strength; opaque black or saturated
// line cores have full strength. This intentionally targets flat line art,
// rather than guessing semantic regions in painted or textured references.
int lineInkStrength(QRgb pixel) {
    return qAlpha(pixel) - std::min({qRed(pixel), qGreen(pixel), qBlue(pixel)});
}

QImage lineEdgeWeights(const QImage &source, const QImage &strength, int tolerance) {
    QImage result(source.size(), QImage::Format_Grayscale8);
    if (result.isNull()) return {};
    result.fill(0);
    const auto overWhite = [](QRgb pixel, auto channel) { return channel(pixel) + 255 - qAlpha(pixel); };
    for (int y = 0; y < source.height(); ++y) {
        const auto pixels = reinterpret_cast<const QRgb *>(source.constScanLine(y));
        const auto ink = strength.constScanLine(y); auto weights = result.scanLine(y);
        for (int x = 0; x < source.width(); ++x) {
            if (!ink[x] || ink[x] == 255) continue;
            int peak = ink[x];
            for (int ny = std::max(0, y - 2); ny <= std::min(source.height() - 1, y + 2); ++ny) {
                const auto neighbor = reinterpret_cast<const QRgb *>(source.constScanLine(ny));
                const auto neighborInk = strength.constScanLine(ny);
                for (int nx = std::max(0, x - 2); nx <= std::min(source.width() - 1, x + 2); ++nx) {
                    if (neighborInk[nx] <= peak || neighborInk[nx] < std::max(32, tolerance + 8)) continue;
                    const qreal fraction = qreal(ink[x]) / neighborInk[nx];
                    const auto aligned = [&](auto channel) {
                        return std::abs(overWhite(pixels[x], channel) - 255 - (overWhite(neighbor[nx], channel) - 255) * fraction) <= 2;
                    };
                    if (aligned([](QRgb p) { return qRed(p); }) && aligned([](QRgb p) { return qGreen(p); }) &&
                        aligned([](QRgb p) { return qBlue(p); })) peak = neighborInk[nx];
                }
            }
            // An isolated gray/colored line is still a core. Only a ramp with
            // a stronger aligned ink sample is treated as antialias coverage.
            if (peak > ink[x]) weights[x] = uchar(qBound(1, qRound(255.0 * (1.0 - qreal(ink[x]) / peak)), 254));
        }
    }
    return result;
}

QImage enclosedRegions(const QImage &source, const QImage &gesture, int tolerance, int gaps,
                       QImage *strength, bool *tooComplex) {
    *tooComplex = false;
    *strength = QImage(source.size(), QImage::Format_Grayscale8);
    QImage blocked(source.size(), QImage::Format_Grayscale8);
    if (strength->isNull() || blocked.isNull()) return {};
    for (int y = 0; y < source.height(); ++y) {
        const auto pixels = reinterpret_cast<const QRgb *>(source.constScanLine(y));
        auto ink = strength->scanLine(y), barrier = blocked.scanLine(y);
        for (int x = 0; x < source.width(); ++x) {
            ink[x] = uchar(lineInkStrength(pixels[x])); barrier[x] = ink[x] > tolerance ? 255 : 0;
        }
    }
    if (gaps) {
        const auto grown = morphology(blocked, gaps, true);
        if (grown.isNull()) return {};
        blocked = morphology(grown, gaps, false);
        if (blocked.isNull()) return {};
    }
    QImage visited(source.size(), QImage::Format_Grayscale8), result(source.size(), QImage::Format_Grayscale8);
    if (visited.isNull() || result.isNull()) return {};
    visited.fill(0); result.fill(0);
    struct Span { int y, left, right; };
    QVector<QPoint> pending; QVector<Span> spans; int work = 0;
    for (int startY = 0; startY < source.height(); ++startY)
        for (int startX = 0; startX < source.width(); ++startX) {
            if (blocked.constScanLine(startY)[startX] || visited.constScanLine(startY)[startX]) continue;
            pending.append({startX, startY}); spans.clear(); bool enclosed = true;
            while (!pending.isEmpty()) {
                if (++work > maxSelectionElements || pending.size() > maxSelectionElements) { *tooComplex = true; return {}; }
                const auto point = pending.takeLast(); const int y = point.y();
                const auto barrier = blocked.constScanLine(y); auto seen = visited.scanLine(y);
                if (barrier[point.x()] || seen[point.x()]) continue;
                int left = point.x(), right = point.x();
                while (left > 0 && !barrier[left - 1] && !seen[left - 1]) --left;
                while (right + 1 < source.width() && !barrier[right + 1] && !seen[right + 1]) ++right;
                std::fill(seen + left, seen + right + 1, uchar(255)); spans.append({y, left, right});
                if (!left || right == source.width() - 1 || !y || y == source.height() - 1) enclosed = false;
                const auto inside = gesture.constScanLine(y);
                for (int x = left; enclosed && x <= right; ++x) if (!inside[x]) enclosed = false;
                for (int nextY : {y - 1, y + 1}) if (nextY >= 0 && nextY < source.height()) {
                    const auto nextBarrier = blocked.constScanLine(nextY), nextSeen = visited.constScanLine(nextY);
                    bool inSpan = false;
                    for (int x = left; x <= right; ++x) {
                        const bool available = !nextBarrier[x] && !nextSeen[x];
                        if (available && !inSpan) pending.append({x, nextY});
                        inSpan = available;
                    }
                }
            }
            if (enclosed) for (const auto span : spans)
                std::fill(result.scanLine(span.y) + span.left, result.scanLine(span.y) + span.right + 1, uchar(255));
        }
    return result;
}

bool paintOperation(Document *document, QRectF extent, const std::function<void(QPainter &)> &draw,
                    const QString &label, const QImage &mask = {}, QPoint maskOrigin = {},
                    const std::function<QRgb(QRgb, QRgb, QRgb, QPoint)> &correctEdge = {},
                    const QJsonObject &action = {}) {
    if (!ready(document)) return false;
    const auto &project = document->project(); const auto &original = project.shots[document->activeShotIndex()].layers[document->activeLayer()];
    const auto &selection = document->rasterSelection();
    if (selection.active) extent = extent.intersected(selection.path.boundingRect().adjusted(-selection.featherRadius, -selection.featherRadius, selection.featherRadius, selection.featherRadius));
    const QRect area = extent.intersected(document->drawingBounds()).toAlignedRect();
    if (area.isEmpty()) return true;
    RasterSelectionCache cache(selection.active ? &selection.path : nullptr, selection.featherRadius);
    auto candidate = original; const int maxTiles = allowance(project, original); bool changed = false;
    for (int ty = tileCoordinate(area.top()); ty <= tileCoordinate(area.bottom()); ++ty)
        for (int tx = tileCoordinate(area.left()); tx <= tileCoordinate(area.right()); ++tx) {
            const TileKey key{tx, ty}; const QPoint origin(tx * tileSize, ty * tileSize);
            const auto existing = original.tiles.constFind(key);
            if (original.alphaLocked && existing == original.tiles.cend()) continue;
            const auto coverage = cache.tile(key);
            if (coverage.coverage == Coverage::Empty) continue;
            if (coverage.coverage == Coverage::Invalid) { emit document->warning("There is not enough memory for the selection."); return false; }
            QImage overlay(tileSize, tileSize, QImage::Format_ARGB32_Premultiplied);
            if (overlay.isNull()) return false; overlay.fill(Qt::transparent);
            { QPainter painter(&overlay); painter.setRenderHint(QPainter::Antialiasing); painter.translate(-origin);
                painter.setClipRect(document->drawingBounds()); draw(painter); }
            bool painted = false;
            for (int y = 0; y < tileSize; ++y) {
                auto row = reinterpret_cast<QRgb *>(overlay.scanLine(y));
                const auto selected = coverage.mask.isNull() ? nullptr : coverage.mask.constScanLine(y);
                const int maskY = origin.y() + y - maskOrigin.y();
                const auto region = !mask.isNull() && maskY >= 0 && maskY < mask.height() ? mask.constScanLine(maskY) : nullptr;
                for (int x = 0; x < tileSize; ++x) {
                    int alpha = selected ? selected[x] : 255;
                    if (!mask.isNull()) {
                        const int maskX = origin.x() + x - maskOrigin.x();
                        alpha = region && maskX >= 0 && maskX < mask.width() ? alpha * region[maskX] / 255 : 0;
                    }
                    row[x] = multiply(row[x], alpha); painted |= qAlpha(row[x]) != 0;
                }
            }
            if (!painted) continue;
            if (existing == original.tiles.cend() && candidate.tiles.size() >= maxTiles) {
                emit document->warning("The 1024-tile memory limit was reached. The artwork was left unchanged."); return false;
            }
            QImage image = existing == original.tiles.cend() ? QImage(tileSize, tileSize, QImage::Format_ARGB32_Premultiplied) : existing->copy();
            if (image.isNull()) return false;
            if (existing == original.tiles.cend()) image.fill(Qt::transparent);
            { QPainter painter(&image); painter.setCompositionMode(original.alphaLocked ? QPainter::CompositionMode_SourceAtop : QPainter::CompositionMode_SourceOver); painter.drawImage(0, 0, overlay); }
            if (correctEdge) {
                for (int y = 0; y < tileSize; ++y) {
                    auto row = reinterpret_cast<QRgb *>(image.scanLine(y));
                    const auto paint = reinterpret_cast<const QRgb *>(overlay.constScanLine(y));
                    const auto old = existing == original.tiles.cend() ? nullptr : reinterpret_cast<const QRgb *>(existing->constScanLine(y));
                    for (int x = 0; x < tileSize; ++x) if (qAlpha(paint[x]))
                        row[x] = correctEdge(old ? old[x] : 0, row[x], paint[x], origin + QPoint(x, y));
                }
            }
            if (existing != original.tiles.cend() && image == existing.value()) continue;
            candidate.tiles.insert(key, image); changed = true;
        }
    if (changed) { auto after = project; after.shots[document->activeShotIndex()].layers[document->activeLayer()] = std::move(candidate); document->replaceProject(after, label, action); }
    return true;
}
}

QString RasterTools::referenceSourceReason(Document *document) {
    if (!document || !document->isOpen()) return "Open a drawing first.";
    const auto &shot = document->activeShot();
    const auto present = activeLayerIds(shot, document->currentFrame());
    bool marked = false;
    for (const auto &layer : shot.layers) if (layer.fillReference) {
        marked = true;
        if (!layer.adjustment && present.contains(layer.id)) return {};
    }
    return marked ? "No marked reference artwork is exposed at this frame. Mark an artwork layer or folder."
                  : "Mark a layer as a fill reference in Layers before sampling.";
}

QColor RasterTools::sample(Document *document, QPointF point, RasterSampleSource source, const QString &id,
                          const Shot *displayShot) {
    if (!document || !document->isOpen() || !finite(point)) return {};
    const QRect pixel(int(std::floor(point.x())), int(std::floor(point.y())), 1, 1);
    const auto image = renderSource(document, pixel, source, id, true, displayShot);
    if(image.isNull())return {};
    const auto color=image.pixelColor(0,0);
    // Preserve the old no-sample behavior for blank space beyond the drawing.
    return !document->drawingBounds().contains(point)&&!color.alpha()?QColor():color;
}
bool RasterTools::bucket(Document *document, QPointF point, const QColor &color, const RasterFillSettings &settings) {
    if (!ready(document) || !finite(point) || !color.isValid() || !document->drawingBounds().contains(point)) return false;
    if (int(settings.source) < int(RasterSampleSource::ActiveLayer) || int(settings.source) > int(RasterSampleSource::MarkedReferences)) return false;
    if (settings.expansion < -20 || settings.expansion > 20) { emit document->warning("Fill expansion must be between -20 and 20 pixels."); return false; }
    QJsonObject parameters{{"x", point.x()}, {"y", point.y()}, {"color", color.name(QColor::HexArgb)},
        {"source", int(settings.source)}, {"tolerance", qBound(0, settings.tolerance, 255)},
        {"gapClosing", qBound(0, settings.gapClosing, 20)}};
    if (settings.expansion) parameters.insert("expansion", settings.expansion);
    if (settings.source == RasterSampleSource::ReferenceLayer) {
        int referenceIndex = -1;
        const auto &layers = document->activeShot().layers;
        for (int i=0; i<layers.size(); ++i) if (layers[i].id == settings.referenceLayerId) { referenceIndex=i; break; }
        if (referenceIndex < 0) { emit document->warning("Choose a reference layer for sampling."); return false; }
        parameters.insert("referenceOffset", referenceIndex-document->activeLayer());
    }
    const QJsonObject action{{"operation", "bucket"}, {"parameters", parameters}};
    try {
        QRect area = document->drawingBounds().toAlignedRect();
        const auto &selection = document->rasterSelection();
        if (selection.active) area = area.intersected(selection.path.boundingRect().adjusted(-selection.featherRadius, -selection.featherRadius, selection.featherRadius, selection.featherRadius).toAlignedRect());
        if (!area.contains(QPoint(int(std::floor(point.x())), int(std::floor(point.y()))))) return true;
        const auto source = renderSource(document, area, settings.source, settings.referenceLayerId);
        if (source.isNull()) return false;
        const QPoint seed(int(std::floor(point.x())), int(std::floor(point.y())));
        const int tolerance = qBound(0, settings.tolerance, 255);
        auto mask = colorMask(source, seed - area.topLeft(), tolerance, qBound(0, settings.gapClosing, 20), true);
        if (mask.isNull()) return false;
        const auto &layer = document->activeShot().layers[document->activeLayer()];
        const QRgb target = source.pixel(seed - area.topLeft());
        if (settings.expansion < 0) {
            mask = adjustBucketMask(mask, source, {}, area.topLeft(), target, tolerance, settings.expansion, settings, layer);
            if (mask.isNull()) return false;
            return paintOperation(document, area, [=](QPainter &painter) { painter.fillRect(area, color); }, "Bucket fill", mask, area.topLeft(), {}, action);
        }
        // Alpha lock cannot add the missing background beneath transparent ink.
        if ((layer.alphaLocked && qAlpha(target) < 255) || tolerance == 255) {
            mask = adjustBucketMask(mask, source, {}, area.topLeft(), target, tolerance, settings.expansion, settings, layer);
            if (mask.isNull()) return false;
            return paintOperation(document, area, [=](QPainter &painter) { painter.fillRect(area, color); }, "Bucket fill", mask, area.topLeft(), {}, action);
        }
        bool hasEdges = false;
        const auto edges = bucketEdgeCoverage(source, seed - area.topLeft(), tolerance, &mask, &hasEdges);
        if (edges.isNull()) return false;
        mask = adjustBucketMask(mask, source, edges, area.topLeft(), target, tolerance, settings.expansion, settings, layer);
        if (mask.isNull()) return false;
        if (!hasEdges) return paintOperation(document, area, [=](QPainter &painter) { painter.fillRect(area, color); }, "Bucket fill", mask, area.topLeft(), {}, action);
        const QRgb activeTarget = layerPixel(layer, seed);
        const bool ownSource = settings.source == RasterSampleSource::ActiveLayer ||
            (settings.source == RasterSampleSource::ReferenceLayer && settings.referenceLayerId == layer.id);
        const auto correctEdge = [=](QRgb original, QRgb painted, QRgb overlay, QPoint canvasPoint) {
            const QPoint position = canvasPoint - area.topLeft();
            if (!edges.rect().contains(position)) return painted;
            const int edge = edges.constScanLine(position.y())[position.x()];
            if (!edge) return painted;
            qreal background = edge / 255.0;
            QRgb backgroundColor = target;
            // A separate flat color layer fills all the way under the reference
            // edge. On the ink layer, replace only its background contribution.
            if (!ownSource) {
                if (settings.source != RasterSampleSource::AllLayers && settings.source != RasterSampleSource::MarkedReferences) return painted;
                if (!qAlpha(activeTarget) && qAlpha(original) > 0 && qAlpha(original) < 255) {
                    background = (255 - qAlpha(original)) / 255.0; backgroundColor = activeTarget;
                } else if (original != source.pixel(position) || activeTarget != target) return painted;
            }
            const qreal alpha = qAlpha(overlay) / 255.0;
            const auto channel = [=](int old, int paint, int seed) { return qBound(0, qRound(old + background * (paint - seed * alpha)), 255); };
            const int a = channel(qAlpha(original), qAlpha(overlay), qAlpha(backgroundColor));
            return qRgba(std::min(a, channel(qRed(original), qRed(overlay), qRed(backgroundColor))),
                         std::min(a, channel(qGreen(original), qGreen(overlay), qGreen(backgroundColor))),
                         std::min(a, channel(qBlue(original), qBlue(overlay), qBlue(backgroundColor))), a);
        };
        return paintOperation(document, area, [=](QPainter &painter) { painter.fillRect(area, color); }, "Bucket fill", mask, area.topLeft(), correctEdge, action);
    } catch (const std::bad_alloc &) { emit document->warning("There is not enough memory to fill the artwork."); return false; }
}
bool RasterTools::encloseFill(Document *document, const QPainterPath &path, const QColor &color, const RasterFillSettings &settings) {
    if (!ready(document) || !color.isValid() || path.isEmpty()) return false;
    if (int(settings.source) < int(RasterSampleSource::ActiveLayer) || int(settings.source) > int(RasterSampleSource::MarkedReferences)) return false;
    if (settings.expansion < -20 || settings.expansion > 20) { emit document->warning("Fill expansion must be between -20 and 20 pixels."); return false; }
    if (path.elementCount() > maxEnclosePathElements) { emit document->warning("The enclosing gesture is too detailed. Draw a simpler outline."); return false; }
    for (int i = 0; i < path.elementCount(); ++i) {
        const auto element = path.elementAt(i); if (!finite({element.x, element.y})) return false;
    }
    try {
        const int gaps = qBound(0, settings.gapClosing, 20);
        // Sample beyond the gesture, so the lasso itself never acts as a line
        // barrier or falsely closes a region it merely intersects.
        const int halo = 3 + gaps + std::max(0, settings.expansion);
        const QRect area = path.boundingRect().adjusted(-halo, -halo, halo, halo)
            .intersected(document->drawingBounds()).toAlignedRect();
        if (area.isEmpty()) return true;
        if (qint64(area.width()) * area.height() > maxEnclosePixels) {
            emit document->warning("Enclose fill exceeds the 16-million-pixel working limit. Enclose a smaller area."); return false;
        }
        const auto source = renderSource(document, area, settings.source, settings.referenceLayerId);
        if (source.isNull()) return false;
        const auto gesture = gestureCoverage(path, area);
        if (gesture.isNull()) { emit document->warning("There is not enough memory for the enclosing gesture."); return false; }
        QImage strength; bool tooComplex = false;
        auto mask = enclosedRegions(source, gesture, qBound(0, settings.tolerance, 255), gaps, &strength, &tooComplex);
        if (mask.isNull()) {
            emit document->warning(tooComplex ? "The enclosed line art is too fragmented. Enclose a smaller or simpler area."
                                             : "There is not enough memory for enclose fill."); return false;
        }
        bool anyRegion = false;
        for (int y = 0; !anyRegion && y < mask.height(); ++y) {
            const auto row = mask.constScanLine(y);
            anyRegion = std::any_of(row, row + mask.width(), [](uchar value) { return value != 0; });
        }
        if (!anyRegion) return true;
        const auto &layer = document->activeShot().layers[document->activeLayer()];
        QImage edges(source.size(), QImage::Format_Grayscale8);
        if (edges.isNull()) return false;
        edges.fill(0);
        if (settings.expansion < 0) {
            mask = morphology(mask, -settings.expansion, false, true);
            if (mask.isNull()) return false;
        } else {
            // Add ordinary AA edges without crossing an opaque core. Positive
            // expansion additionally paints beneath a separate reference's
            // core, never into another unselected background component.
            const auto softWeights = lineEdgeWeights(source, strength, qBound(0, settings.tolerance, 255));
            if (softWeights.isNull()) return false;
            auto edgeReach = mask;
            for (int pass = 0; pass < 2; ++pass) {
                auto grown = morphology(edgeReach, 1, true);
                if (grown.isNull()) return false;
                for (int y = 0; y < grown.height(); ++y) {
                    auto row = grown.scanLine(y); const auto previous = edgeReach.constScanLine(y);
                    const auto soft = softWeights.constScanLine(y), inside = gesture.constScanLine(y);
                    for (int x = 0; x < grown.width(); ++x)
                        if (row[x] && !previous[x] && (!inside[x] || !soft[x])) row[x] = 0;
                }
                edgeReach = std::move(grown);
            }
            auto expanded = settings.expansion ? morphology(edgeReach, settings.expansion, true) : edgeReach;
            if (expanded.isNull()) return false;
            for (int y = 0; y < mask.height(); ++y) {
                auto row = mask.scanLine(y), weights = edges.scanLine(y);
                const auto candidate = expanded.constScanLine(y), soft = edgeReach.constScanLine(y);
                const auto ink = strength.constScanLine(y), inside = gesture.constScanLine(y);
                const auto edgeWeight = softWeights.constScanLine(y);
                const auto pixels = reinterpret_cast<const QRgb *>(source.constScanLine(y));
                for (int x = 0; x < mask.width(); ++x) if (!row[x] && candidate[x] && inside[x]) {
                    const QPoint canvasPoint = area.topLeft() + QPoint(x, y);
                    const bool owned = ownsSampledPixel(settings, layer, pixels[x], canvasPoint, true);
                    const QRgb original = layerPixel(layer, canvasPoint);
                    const bool unchangedInk = !qAlpha(original) || original == pixels[x];
                    if (soft[x] && (!owned || (!layer.alphaLocked && unchangedInk))) {
                        row[x] = 255; weights[x] = edgeWeight[x];
                    } else if (settings.expansion && ink[x] > qBound(0, settings.tolerance, 255) && !edgeWeight[x] && !owned) row[x] = 255;
                }
            }
        }
        const auto correctEdge = [=](QRgb original, QRgb painted, QRgb overlay, QPoint canvasPoint) {
            if (layer.alphaLocked) return painted;
            const QPoint point = canvasPoint - area.topLeft();
            const int weight = edges.constScanLine(point.y())[point.x()];
            if (!weight || !ownsSampledPixel(settings, layer, source.pixel(point), canvasPoint)) return painted;
            const bool transparent = qAlpha(source.pixel(point)) < 255;
            const int background = transparent ? 255 - qAlpha(source.pixel(point)) : weight;
            const QRgb backgroundColor = transparent ? 0 : qRgb(255, 255, 255);
            const qreal alpha = qAlpha(overlay) / 255.0;
            const auto channel = [=](int old, int paint, int base) {
                return qBound(0, qRound(old + background / 255.0 * (paint - base * alpha)), 255);
            };
            const int a = channel(qAlpha(original), qAlpha(overlay), qAlpha(backgroundColor));
            return qRgba(std::min(a, channel(qRed(original), qRed(overlay), qRed(backgroundColor))),
                         std::min(a, channel(qGreen(original), qGreen(overlay), qGreen(backgroundColor))),
                         std::min(a, channel(qBlue(original), qBlue(overlay), qBlue(backgroundColor))), a);
        };
        return paintOperation(document, area, [=](QPainter &painter) { painter.fillRect(area, color); },
                              "Enclose fill", mask, area.topLeft(), correctEdge);
    } catch (const std::bad_alloc &) { emit document->warning("There is not enough memory for enclose fill."); return false; }
}
bool RasterTools::selectColor(Document *document, QPointF point, const RasterFillSettings &settings, bool contiguous, SelectionCombine mode) {
    if (!ready(document, false) || !finite(point) || !document->drawingBounds().contains(point)) return false;
    try {
        const QRect area = document->drawingBounds().toAlignedRect();
        const auto source = renderSource(document, area, settings.source, settings.referenceLayerId);
        const auto mask = colorMask(source, QPoint(int(std::floor(point.x())), int(std::floor(point.y()))) - area.topLeft(), qBound(0, settings.tolerance, 255), qBound(0, settings.gapClosing, 20), contiguous);
        if (mask.isNull()) return false;
        bool complex = false; const auto path = selectionPathFromMask(mask, area.topLeft(), &complex);
        if (complex || path.elementCount() > maxSelectionElements) { emit document->warning("The color selection is too detailed. Increase tolerance or select a simpler region."); return false; }
        document->combineRasterSelection(path, mode); return true;
    } catch (const std::bad_alloc &) { emit document->warning("There is not enough memory to select this color."); return false; }
}
bool RasterTools::gradient(Document *document, QPointF start, QPointF end, const QGradientStops &stops, bool radial) {
    if (!ready(document) || !finite(start) || !finite(end) || QLineF(start, end).length() < .01 || stops.size() < 2 || stops.size() > 64) return false;
    for (const auto &stop : stops) if (!std::isfinite(stop.first) || stop.first < 0 || stop.first > 1 || !stop.second.isValid()) return false;
    const auto extent = document->drawingBounds(); QBrush brush;
    auto orderedStops = stops; std::stable_sort(orderedStops.begin(), orderedStops.end(), [](const auto &a, const auto &b) { return a.first < b.first; });
    if (radial) { QRadialGradient gradient(start, QLineF(start, end).length()); gradient.setStops(orderedStops); brush = QBrush(gradient); }
    else { QLinearGradient gradient(start, end); gradient.setStops(orderedStops); brush = QBrush(gradient); }
    return paintOperation(document, extent, [=](QPainter &painter) { painter.fillRect(extent, brush); }, radial ? "Radial gradient" : "Linear gradient");
}
bool RasterTools::drawPath(Document *document, const QPainterPath &path, const QColor &fill, const QColor &stroke, qreal width) {
    if (!ready(document) || path.isEmpty() || path.elementCount() > maxSelectionElements || !std::isfinite(width) || width < 0 || width > 2048) return false;
    for (int i = 0; i < path.elementCount(); ++i) if (!finite(QPointF(path.elementAt(i).x, path.elementAt(i).y))) return false;
    const bool hasStroke = stroke.isValid() && stroke.alpha() && width > 0;
    return paintOperation(document, path.boundingRect().adjusted(-width, -width, width, width), [=](QPainter &painter) {
        painter.setBrush(fill.isValid() ? QBrush(fill) : QBrush(Qt::NoBrush));
        painter.setPen(hasStroke ? QPen(stroke, width, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin) : QPen(Qt::NoPen)); painter.drawPath(path);
    }, "Draw shape");
}
bool RasterTools::featherSelection(Document *document, qreal radius) {
    if (!ready(document, false) || !document->m_selection.active || !std::isfinite(radius) || radius < 0 || radius > 100) return false;
    document->m_selection.featherRadius = radius; emit document->rasterSelectionChanged(); return true;
}
bool RasterTools::saveSelection(Document *document, const QString &name) {
    if (!ready(document, false) || !document->m_selection.active || document->m_selection.path.isEmpty() || name.trimmed().isEmpty() || name.trimmed().size() > 100) return false;
    auto after = document->m_project; auto &saved = after.shots[document->activeShotIndex()].savedSelections;
    for (auto &selection : saved) if (selection.name == name.trimmed()) {
        selection.path = document->m_selection.path; selection.featherRadius = document->m_selection.featherRadius;
        document->commit(document->m_project, after, "Save selection"); return true;
    }
    if (saved.size() >= 64) { emit document->warning("A shot can contain up to 64 saved selections."); return false; }
    saved.append({QUuid::createUuid().toString(QUuid::WithoutBraces), name.trimmed(), document->m_selection.path, document->m_selection.featherRadius});
    document->commit(document->m_project, after, "Save selection"); return true;
}
bool RasterTools::loadSelection(Document *document, const QString &id) {
    if (!ready(document, false)) return false;
    for (const auto &selection : document->m_project.shots[document->activeShotIndex()].savedSelections) if (selection.id == id) {
        document->m_selection = {true, selection.path, selection.featherRadius}; emit document->rasterSelectionChanged(); return true;
    }
    return false;
}
bool RasterTools::deleteSelection(Document *document, const QString &id) {
    if (!ready(document, false)) return false;
    auto after = document->m_project; auto &saved = after.shots[document->activeShotIndex()].savedSelections;
    for (int i = 0; i < saved.size(); ++i) if (saved[i].id == id) {
        saved.removeAt(i); document->commit(document->m_project, after, "Delete saved selection"); return true;
    }
    return false;
}

bool RasterTools::beginRetouch(QPointF point, qreal pressure, const RetouchSettings &settings) {
    if (!ready(m_document) || !finite(point) || !std::isfinite(pressure) || !std::isfinite(settings.size) || !std::isfinite(settings.strength)) return false;
    m_settings = settings; m_settings.size = qBound(1.0, settings.size, 512.0); m_settings.strength = qBound(0.0, settings.strength, 1.0);
    m_document->m_beforeStroke = m_document->m_project; m_retouching = true; m_changed = false;
    m_lastPoint = m_lastDabPoint = point; m_lastPressure = qBound(0.0, pressure, 1.0);
    m_distanceToDab = qMax(1.0, m_settings.size * .12);
    const auto &selection = m_document->m_selection;
    m_selectionCache.reset(selection.active ? &selection.path : nullptr, selection.featherRadius);
    if (settings.mode != RetouchMode::Smudge && !retouchDab(point, m_lastPressure, {})) { cancelRetouch(); return false; }
    return true;
}
void RasterTools::continueRetouch(QPointF point, qreal pressure) {
    if (!m_retouching || !m_document->strokeActive() || !finite(point) || !std::isfinite(pressure)) return;
    pressure = qBound(0.0, pressure, 1.0); const qreal length = QLineF(m_lastPoint, point).length();
    if (length < 1e-9) { m_lastPressure = pressure; return; }
    const qreal spacing = qMax(1.0, m_settings.size * .12);
    // Carry spacing across events so dense tablet input cannot multiply dab work.
    // Extremely distant off-canvas input is bounded to 4096 samples per update.
    const qreal step = qMax(spacing, length / 4096.0);
    qreal distance = m_distanceToDab;
    while (distance <= length + 1e-9) {
        const qreal t = qMin(1.0, distance / length); const auto dabPoint = m_lastPoint + (point - m_lastPoint) * t;
        if (!retouchDab(dabPoint, m_lastPressure + (pressure - m_lastPressure) * t, dabPoint - m_lastDabPoint)) {
            emit m_document->warning("There is not enough memory for this retouch stroke. The stroke was cancelled."); cancelRetouch(); return;
        }
        m_lastDabPoint = dabPoint; distance += step;
    }
    m_distanceToDab = qMax(0.0, distance - length);
    m_lastPoint = point; m_lastPressure = pressure;
}
bool RasterTools::retouchDab(QPointF point, qreal pressure, QPointF movement) {
    if (pressure <= 0 || m_settings.strength <= 0) return true;
    auto &layer = m_document->m_project.shots[m_document->activeShotIndex()].layers[m_document->m_active];
    const auto before = layer; const qreal radius = qMax(.5, m_settings.size * pressure * .5);
    const QRect area = QRectF(point - QPointF(radius, radius), QSizeF(radius * 2, radius * 2)).toAlignedRect().intersected(m_document->drawingBounds().toAlignedRect());
    if (area.isEmpty()) return true;
    QRect sampleArea = area.adjusted(-1, -1, 1, 1);
    if (m_settings.mode == RetouchMode::Smudge || m_settings.mode == RetouchMode::Mixer)
        sampleArea |= area.translated(-movement.toPoint()).adjusted(-1, -1, 1, 1);
    if (qint64(sampleArea.width()) * sampleArea.height() > 4 * 1024 * 1024) return false;
    QImage source(sampleArea.size(), QImage::Format_ARGB32_Premultiplied); if (source.isNull()) return false;
    source.fill(Qt::transparent);
    {
        QPainter painter(&source);
        for (int y = tileCoordinate(sampleArea.top()); y <= tileCoordinate(sampleArea.bottom()); ++y)
            for (int x = tileCoordinate(sampleArea.left()); x <= tileCoordinate(sampleArea.right()); ++x) {
                const auto tile = before.tiles.constFind({x, y});
                if (tile != before.tiles.cend()) painter.drawImage(QPoint(x * tileSize, y * tileSize) - sampleArea.topLeft(), tile.value());
            }
    }
    const auto sampled = [&source, sampleArea](int x, int y) -> QRgb {
        return sampleArea.contains(x, y) ? reinterpret_cast<const QRgb *>(source.constScanLine(y - sampleArea.top()))[x - sampleArea.left()] : 0;
    };
    const int maxTiles = allowance(m_document->m_project, layer); bool changed = false;
    for (int ty = tileCoordinate(area.top()); ty <= tileCoordinate(area.bottom()); ++ty)
        for (int tx = tileCoordinate(area.left()); tx <= tileCoordinate(area.right()); ++tx) {
            const TileKey key{tx, ty}; const QPoint origin(tx * tileSize, ty * tileSize);
            const auto coverage = m_selectionCache.tile(key); if (coverage.coverage == Coverage::Empty) continue;
            if (coverage.coverage == Coverage::Invalid) return false;
            const auto existing = before.tiles.constFind(key);
            if (layer.alphaLocked && existing == before.tiles.cend()) continue;
            QImage image = existing == before.tiles.cend() ? QImage(tileSize, tileSize, QImage::Format_ARGB32_Premultiplied) : existing->copy();
            if (image.isNull()) return false;
            if (existing == before.tiles.cend()) image.fill(Qt::transparent);
            const auto part = area.intersected(QRect(origin, QSize(tileSize, tileSize))); bool touched = false;
            for (int y = part.top(); y <= part.bottom(); ++y) {
                auto row = reinterpret_cast<QRgb *>(image.scanLine(y - origin.y()));
                const auto mask = coverage.mask.isNull() ? nullptr : coverage.mask.constScanLine(y - origin.y());
                for (int x = part.left(); x <= part.right(); ++x) {
                    const qreal distance = QLineF(point, QPointF(x + .5, y + .5)).length() / radius;
                    if (distance >= 1) continue;
                    const int local = x - origin.x(); const qreal selected = mask ? mask[local] / 255.0 : 1;
                    if (selected == 0) continue;
                    const QRgb original = row[local]; if (layer.alphaLocked && !qAlpha(original)) continue;
                    qreal amount = m_settings.strength * pressure * qBound(0.0, (1 - distance) * 3, 1.0) * selected;
                    QRgb target = original;
                    if (m_settings.mode == RetouchMode::Smudge || m_settings.mode == RetouchMode::Mixer) {
                        const QRgb carried = sampled(qRound(x - movement.x()), qRound(y - movement.y()));
                        target = m_settings.mode == RetouchMode::Smudge ? carried : mix(carried, qPremultiply(m_settings.color.rgba()), .35);
                    } else if (m_settings.mode == RetouchMode::Blur || m_settings.mode == RetouchMode::Sharpen) {
                        int red = 0, green = 0, blue = 0, alpha = 0;
                        for (int dy = -1; dy <= 1; ++dy) for (int dx = -1; dx <= 1; ++dx) {
                            const QRgb value = sampled(x + dx, y + dy); red += qRed(value); green += qGreen(value); blue += qBlue(value); alpha += qAlpha(value);
                        }
                        target = qRgba((red + 4) / 9, (green + 4) / 9, (blue + 4) / 9, (alpha + 4) / 9);
                        if (m_settings.mode == RetouchMode::Sharpen) {
                            const int a = qAlpha(original); target = qRgba(qBound(0, qRed(original) * 2 - qRed(target), a), qBound(0, qGreen(original) * 2 - qGreen(target), a), qBound(0, qBlue(original) * 2 - qBlue(target), a), a);
                        }
                    } else if (qAlpha(original)) {
                        const QColor color = QColor::fromRgba(qUnpremultiply(original));
                        const auto channel = [&](int value) { return m_settings.mode == RetouchMode::Dodge ? qRound(value + (255 - value) * .3) : qRound(value * .7); };
                        target = qPremultiply(qRgba(channel(color.red()), channel(color.green()), channel(color.blue()), qAlpha(original)));
                    }
                    QRgb result = mix(original, target, amount);
                    if (layer.alphaLocked && qAlpha(result) != qAlpha(original)) {
                        const auto unpremultiplied = qUnpremultiply(result);
                        result = qPremultiply(qRgba(qRed(unpremultiplied), qGreen(unpremultiplied), qBlue(unpremultiplied), qAlpha(original)));
                    }
                    if (result != original) { row[local] = result; touched = true; }
                }
            }
            if (touched) {
                if (existing == before.tiles.cend() && layer.tiles.size() >= maxTiles) return false;
                if (hasPixels(image)) layer.tiles.insert(key, image); else layer.tiles.remove(key);
                changed = true;
            }
        }
    if (changed) { m_changed = true; emit m_document->changed(); }
    return true;
}
void RasterTools::endRetouch() {
    if (!m_retouching) return;
    if (m_document->strokeActive() && QLineF(m_lastDabPoint, m_lastPoint).length() > 1e-6
        && !retouchDab(m_lastPoint, m_lastPressure, m_lastPoint - m_lastDabPoint)) {
        emit m_document->warning("There is not enough memory for this retouch stroke. The stroke was cancelled."); cancelRetouch(); return;
    }
    m_retouching = false;
    if (!m_document->m_beforeStroke) return;
    const auto before = *m_document->m_beforeStroke; m_document->m_beforeStroke.reset();
    if (m_changed) m_document->commit(before, m_document->m_project, "Retouch stroke");
}
void RasterTools::cancelRetouch() {
    if (!m_retouching) return;
    m_retouching = false;
    if (!m_document->m_beforeStroke) return;
    const auto before = *m_document->m_beforeStroke; m_document->m_beforeStroke.reset(); m_document->apply(before);
}
} // namespace toon
