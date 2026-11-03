#include "character_recolor.h"
#include "document.h"
#include <QHash>
#include <QPainter>
#include <QSet>
#include <algorithm>
#include <array>
#include <cmath>
#include <new>

namespace toon {
namespace {
constexpr qint64 maximumPixels = 64LL * 1024 * 1024;
constexpr qint64 previewPixelBudget = 16LL * 1024 * 1024;
struct PixelMapping { QRgb color = 0; bool ambiguous = false; };
struct OpaquePaletteBounds {
    int redMin = 255, greenMin = 255, blueMin = 255;
    int redMax = 0, greenMax = 0, blueMax = 0;
    bool changes = false;
    bool contains(QRgb color) const {
        return changes && qRed(color) >= redMin - 1 && qRed(color) <= redMax + 1
            && qGreen(color) >= greenMin - 1 && qGreen(color) <= greenMax + 1
            && qBlue(color) >= blueMin - 1 && qBlue(color) <= blueMax + 1;
    }
};
bool cancelled(const RecolorCancellation &flag) {
    return flag && flag->load(std::memory_order_relaxed);
}
bool validVariant(const CharacterModel &character, const QString &id) {
    return id.isEmpty() || std::any_of(character.colorVariants.cbegin(), character.colorVariants.cend(),
        [&](const auto &variant) { return variant.id == id; });
}
bool sameTiles(const RasterLayer &left, const RasterLayer &right) {
    if (left.tiles.size() != right.tiles.size()) return false;
    auto a = left.tiles.cbegin(), b = right.tiles.cbegin();
    for (; a != left.tiles.cend(); ++a, ++b)
        if (a.key() != b.key() || a->cacheKey() != b->cacheKey()) return false;
    return true;
}
QString buildMapping(const RecolorSettings &settings, QHash<QRgb, PixelMapping> *mapping, OpaquePaletteBounds *bounds) {
    const auto &character = settings.character;
    if (character.id.isEmpty() || character.colors.isEmpty() || character.colors.size() > 256
        || !validVariant(character, settings.sourceVariantId) || !validVariant(character, settings.targetVariantId))
        return "Choose a character with colors and valid source and target palettes.";
    const auto source = characterVariantColors(character, settings.sourceVariantId);
    const auto destination = characterVariantColors(character, settings.targetVariantId);
    if (source.size() != character.colors.size() || destination.size() != source.size())
        return "The character palettes do not contain the same color roles.";
    QHash<QRgb, QPair<QRgb, QString>> colors;
    QSet<QString> roles;
    for (int index = 0; index < source.size(); ++index) {
        const auto &from = source[index]; const auto &to = destination[index];
        if (from.id.isEmpty() || roles.contains(from.id) || from.id != to.id
            || !from.color.isValid() || !to.color.isValid()) return "The character palette has invalid color roles.";
        roles.insert(from.id);
        const auto sourceRgb = from.color.rgb(), destinationRgb = to.color.rgb();
        const auto existing = colors.constFind(sourceRgb);
        if (existing != colors.cend() && existing->first != destinationRgb)
            return QString("%1 and %2 share a source color but need different replacements. Give those roles distinct source colors first.")
                .arg(existing->second, from.name);
        colors.insert(sourceRgb, {destinationRgb, from.name});
    }
    mapping->reserve(colors.size() * 255);
    for (auto it = colors.cbegin(); it != colors.cend(); ++it) {
        bounds->redMin = qMin(bounds->redMin, qRed(it.key())); bounds->redMax = qMax(bounds->redMax, qRed(it.key()));
        bounds->greenMin = qMin(bounds->greenMin, qGreen(it.key())); bounds->greenMax = qMax(bounds->greenMax, qGreen(it.key()));
        bounds->blueMin = qMin(bounds->blueMin, qBlue(it.key())); bounds->blueMax = qMax(bounds->blueMax, qBlue(it.key()));
        bounds->changes = bounds->changes || it.key() != it->first;
        for (int alpha = 1; alpha <= 255; ++alpha) {
            const auto from = qPremultiply((it.key() & 0xffffffu) | (quint32(alpha) << 24));
            const auto to = qPremultiply((it->first & 0xffffffu) | (quint32(alpha) << 24));
            auto existing = mapping->find(from);
            if (existing == mapping->end()) mapping->insert(from, {to, false});
            else if (existing->color != to) existing->ambiguous = true;
        }
    }
    return {};
}
// Read only the original tile neighborhood. Endpoints must both be present as
// exact opaque palette colors within one pixel; a broad painted gradient never
// supplies those two endpoints and is left alone. The work is bounded by eight
// neighboring pixels, independent of the number of roles in the palette.
std::optional<PixelMapping> opaqueEdgeMapping(QRgb pixel, int x, int y,
        const std::array<const QImage *, 9> &tiles, const QHash<QRgb, PixelMapping> &mapping,
        const OpaquePaletteBounds &bounds) {
    if (qAlpha(pixel) != 255 || !bounds.contains(pixel)) return {};
    std::array<QRgb, 8> sources{}, destinations{}; int count = 0;
    for (int dy = -1; dy <= 1; ++dy) for (int dx = -1; dx <= 1; ++dx) {
        if (!dx && !dy) continue;
        int px = x + dx, py = y + dy, tx = 1, ty = 1;
        if (px < 0) { --tx; px += tileSize; } else if (px >= tileSize) { ++tx; px -= tileSize; }
        if (py < 0) { --ty; py += tileSize; } else if (py >= tileSize) { ++ty; py -= tileSize; }
        const auto *tile = tiles[ty * 3 + tx]; if (!tile) continue;
        const auto neighbor = reinterpret_cast<const QRgb *>(tile->constScanLine(py))[px];
        if (qAlpha(neighbor) != 255 || neighbor == pixel) continue;
        bool seen = false; for (int i = 0; i < count; ++i) if (sources[i] == neighbor) { seen = true; break; }
        if (seen) continue;
        const auto exact = mapping.constFind(neighbor);
        if (exact == mapping.cend() || exact->ambiguous) continue;
        sources[count] = neighbor; destinations[count] = exact->color; ++count;
    }
    std::optional<PixelMapping> result;
    for (int a = 0; a < count; ++a) for (int b = a + 1; b < count; ++b) {
        // Canonical ordering keeps interpolation rounding independent of which
        // direction the boundary happens to face.
        const int first = sources[a] < sources[b] ? a : b, second = first == a ? b : a;
        const auto from = sources[first], to = sources[second];
        const double dr = qRed(to) - qRed(from), dg = qGreen(to) - qGreen(from), db = qBlue(to) - qBlue(from);
        const double length = dr * dr + dg * dg + db * db;
        if (length < 16) continue; // Near-identical endpoints cannot establish reliable coverage.
        const double t = ((qRed(pixel) - qRed(from)) * dr + (qGreen(pixel) - qGreen(from)) * dg + (qBlue(pixel) - qBlue(from)) * db) / length;
        if (t <= 0 || t >= 1
            || std::abs(qRed(pixel) - (qRed(from) + t * dr)) > 1.25
            || std::abs(qGreen(pixel) - (qGreen(from) + t * dg)) > 1.25
            || std::abs(qBlue(pixel) - (qBlue(from) + t * db)) > 1.25) continue;
        const auto targetFrom = destinations[first], targetTo = destinations[second];
        // Apply the interpolated change, retaining the original one-byte
        // rounding residual. Identity palettes therefore never alter an edge.
        const auto channel = [t](int original, int sourceA, int sourceB, int targetA, int targetB) {
            return qBound(0, qRound(original + (1 - t) * (targetA - sourceA) + t * (targetB - sourceB)), 255);
        };
        const auto color = qRgb(channel(qRed(pixel), qRed(from), qRed(to), qRed(targetFrom), qRed(targetTo)),
                               channel(qGreen(pixel), qGreen(from), qGreen(to), qGreen(targetFrom), qGreen(targetTo)),
                               channel(qBlue(pixel), qBlue(from), qBlue(to), qBlue(targetFrom), qBlue(targetTo)));
        if (!result) result = PixelMapping{color, false};
        else if (result->color != color) return PixelMapping{pixel, true};
    }
    return result;
}
QImage previewImage(const RasterLayer &layer, const QRectF &bounds, int side,
                    const RecolorCancellation &cancel) {
    const auto scale = qMin(1.0, qMin(side / bounds.width(), side / bounds.height()));
    const QSize size(qMax(1, int(std::ceil(bounds.width() * scale))), qMax(1, int(std::ceil(bounds.height() * scale))));
    QImage image(size, QImage::Format_ARGB32_Premultiplied);
    if (image.isNull()) return {};
    image.fill(Qt::transparent);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, scale < 1);
    painter.scale(scale, scale); painter.translate(-bounds.topLeft());
    for (auto tile = layer.tiles.cbegin(); tile != layer.tiles.cend(); ++tile) {
        if (cancelled(cancel)) return {};
        painter.drawImage(QPoint(tile.key().first * tileSize, tile.key().second * tileSize), tile.value());
    }
    return image;
}
} // namespace

QString characterRecolorTargetError(const Shot &shot, int layerIndex) {
    if (layerIndex < 0 || layerIndex >= shot.layers.size()) return "This drawing no longer exists.";
    const auto &layer = shot.layers[layerIndex];
    if (layer.kind != LayerKind::Raster || layer.group || layer.scene3d || layer.adjustment)
        return "Choose a raster paint layer.";
    if (layerIsAnnotation(shot.layers, layerIndex)) return "Notes layers are not character paint.";
    if (!layerCanEdit(shot.layers, layerIndex)) return "Show and unlock this drawing and its folders first.";
    return {};
}

RecolorResult prepareCharacterRecolor(QVector<RecolorTarget> targets, const RecolorSettings &settings,
                                      const RecolorCancellation &cancel) {
    RecolorResult result; result.settings = settings;
    const auto stop = [&]() { result.cancelled = true; result.drawings.clear(); result.changedPixels = result.matchedPixels = result.ambiguousEdgePixels = 0; };
    if (cancelled(cancel)) { stop(); return result; }
    if (targets.isEmpty() || targets.size() > 64 || settings.canvasSize.isEmpty()
        || settings.canvasSize.width() > 32768 || settings.canvasSize.height() > 32768) {
        result.error = "Choose between 1 and 64 raster drawings from an open project."; return result;
    }
    QSet<QString> identities; qint64 pixelCount = 0;
    for (const auto &target : targets) {
        const auto identity = target.shotId + '/' + target.layerId;
        const auto &layer = target.original;
        if (target.shotId.isEmpty() || target.layerId.isEmpty() || target.layerId != layer.id || identities.contains(identity)
            || layer.kind != LayerKind::Raster || layer.group || layer.scene3d || layer.adjustment || layer.annotation) {
            result.error = "Choose unique raster paint drawings."; return result;
        }
        identities.insert(identity);
        pixelCount += qint64(layer.tiles.size()) * tileSize * tileSize;
        if (pixelCount > maximumPixels) { result.error = "Choose fewer drawings: a recolor batch supports 64 million stored pixels."; return result; }
        for (auto tile = layer.tiles.cbegin(); tile != layer.tiles.cend(); ++tile)
            if (tile->size() != QSize(tileSize, tileSize) || tile->format() != QImage::Format_ARGB32_Premultiplied
                || qint64(tile.key().first) < -128 || qint64(tile.key().first) > 128
                || qint64(tile.key().second) < -128 || qint64(tile.key().second) > 128) {
                result.error = "A drawing contains an invalid raster tile."; return result;
            }
    }
    try {
        QHash<QRgb, PixelMapping> mapping; OpaquePaletteBounds opaqueBounds;
        result.error = buildMapping(settings, &mapping, &opaqueBounds);
        if (!result.error.isEmpty()) return result;
        const int previewSide = qMin(2048, int(std::sqrt(previewPixelBudget / targets.size())));
        for (auto &target : targets) {
            if (cancelled(cancel)) { stop(); return result; }
            RecolorDrawing drawing; drawing.target = std::move(target); drawing.replacement = drawing.target.original;
            QRect content;
            for (auto tile = drawing.target.original.tiles.cbegin(); tile != drawing.target.original.tiles.cend(); ++tile) {
                QImage replacement; int left = tileSize, top = tileSize, right = -1, bottom = -1;
                std::array<const QImage *, 9> neighbors{};
                for (int dy = -1; dy <= 1; ++dy) for (int dx = -1; dx <= 1; ++dx) {
                    const auto neighbor = drawing.target.original.tiles.constFind({tile.key().first + dx, tile.key().second + dy});
                    if (neighbor != drawing.target.original.tiles.cend()) neighbors[(dy + 1) * 3 + dx + 1] = &neighbor.value();
                }
                for (int y = 0; y < tileSize; ++y) {
                    if (cancelled(cancel)) { stop(); return result; }
                    const auto *pixels = reinterpret_cast<const QRgb *>(tile->constScanLine(y));
                    QRgb *changed = replacement.isNull() ? nullptr : reinterpret_cast<QRgb *>(replacement.scanLine(y));
                    for (int x = 0; x < tileSize; ++x) {
                        const auto pixel = pixels[x];
                        if (!qAlpha(pixel)) continue;
                        left = qMin(left, x); right = qMax(right, x); top = qMin(top, y); bottom = y;
                        const auto match = mapping.constFind(pixel);
                        std::optional<PixelMapping> edge;
                        if (match == mapping.cend()) edge = opaqueEdgeMapping(pixel, x, y, neighbors, mapping, opaqueBounds);
                        if (match == mapping.cend() && !edge) continue;
                        const auto resolved = edge ? *edge : match.value();
                        if (resolved.ambiguous) { ++drawing.ambiguousEdgePixels; continue; }
                        ++drawing.matchedPixels;
                        if (resolved.color == pixel) continue;
                        if (replacement.isNull()) { replacement = tile->copy(); if (replacement.isNull()) throw std::bad_alloc(); changed = reinterpret_cast<QRgb *>(replacement.scanLine(y)); }
                        changed[x] = resolved.color; ++drawing.changedPixels;
                    }
                }
                if (!replacement.isNull()) drawing.replacement.tiles[tile.key()] = replacement;
                if (right >= left) content |= QRect(QPoint(left + tile.key().first * tileSize, top + tile.key().second * tileSize),
                                                    QPoint(right + tile.key().first * tileSize, bottom + tile.key().second * tileSize));
            }
            drawing.previewBounds = QRectF(QPointF(), settings.canvasSize).united(content);
            drawing.before = previewImage(drawing.target.original, drawing.previewBounds, previewSide, cancel);
            drawing.after = drawing.changedPixels ? previewImage(drawing.replacement, drawing.previewBounds, previewSide, cancel) : drawing.before;
            if (cancelled(cancel)) { stop(); return result; }
            if (drawing.before.isNull() || drawing.after.isNull()) throw std::bad_alloc();
            result.changedPixels += drawing.changedPixels; result.matchedPixels += drawing.matchedPixels;
            result.ambiguousEdgePixels += drawing.ambiguousEdgePixels; result.drawings.append(std::move(drawing));
        }
    } catch (const std::bad_alloc &) {
        result.error = "Not enough memory to preview these drawings. Select fewer drawings and try again.";
        result.drawings.clear(); result.changedPixels = result.matchedPixels = result.ambiguousEdgePixels = 0;
    }
    return result;
}

bool applyCharacterRecolor(Document *document, const RecolorResult &result, QString *error) {
    const auto fail = [&](const QString &message) { if (error) *error = message; return false; };
    if (error) error->clear();
    if (!document || !document->isOpen() || document->loading() || document->editingGestureActive() || document->layerPropertyEditActive())
        return fail("Finish the current drawing or property edit first.");
    if (result.cancelled || !result.error.isEmpty() || result.drawings.isEmpty() || result.drawings.size() > 64)
        return fail("Preview valid recoloring before applying it.");
    auto project = document->project();
    if (QSize(project.width, project.height) != result.settings.canvasSize)
        return fail("The canvas size changed. Preview the recoloring again.");
    const auto character = std::find_if(project.characters.cbegin(), project.characters.cend(),
        [&](const auto &entry) { return entry.id == result.settings.character.id; });
    if (character == project.characters.cend() || !validVariant(*character, result.settings.sourceVariantId)
        || !validVariant(*character, result.settings.targetVariantId)
        || characterVariantColors(*character, result.settings.sourceVariantId) != characterVariantColors(result.settings.character, result.settings.sourceVariantId)
        || characterVariantColors(*character, result.settings.targetVariantId) != characterVariantColors(result.settings.character, result.settings.targetVariantId))
        return fail("The character palettes changed. Preview the recoloring again.");
    QSet<QString> seen; bool changed = false;
    for (const auto &drawing : result.drawings) {
        const auto identity = drawing.target.shotId + '/' + drawing.target.layerId;
        if (seen.contains(identity)) return fail("A recolor drawing was duplicated.");
        seen.insert(identity);
        auto shot = std::find_if(project.shots.begin(), project.shots.end(), [&](const auto &entry) { return entry.id == drawing.target.shotId; });
        if (shot == project.shots.end()) return fail("A source shot changed. Preview the recoloring again.");
        int index = -1;
        for (int i = 0; i < shot->layers.size(); ++i) if (shot->layers[i].id == drawing.target.layerId) { index = i; break; }
        if (index < 0 || !sameTiles(shot->layers[index], drawing.target.original))
            return fail("A source drawing changed. Preview the recoloring again.");
        const auto unavailable = characterRecolorTargetError(*shot, index);
        if (!unavailable.isEmpty()) return fail(unavailable);
        if (drawing.changedPixels > 0) { shot->layers[index].tiles = drawing.replacement.tiles; changed = true; }
    }
    if (!changed) return true;
    const auto validation = project.validate(); if (!validation.isEmpty()) return fail(validation);
    bool committed = false;
    const auto connection = QObject::connect(document, &Document::operationCommitted, document, [&] { committed = true; });
    document->replaceProject(project, "Recolor character drawings");
    QObject::disconnect(connection);
    if (!committed) return fail("Recoloring could not be applied. The artwork is unchanged.");
    return true;
}
} // namespace toon
