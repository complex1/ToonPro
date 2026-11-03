#include "document.h"
#include "keyframe_render.h"
#include "selection_geometry.h"
#include <QPainterPathStroker>
#include <cmath>

namespace toon {
namespace {
constexpr int directSelectionLimit = 4096;
bool validSelectionPath(const QPainterPath &path) {
    if (path.elementCount() > 262144) return false;
    for (int i = 0; i < path.elementCount(); ++i) {
        const auto element = path.elementAt(i);
        if (!std::isfinite(element.x) || !std::isfinite(element.y)
            || std::abs(element.x) > 1000000 || std::abs(element.y) > 1000000) return false;
    }
    return true;
}
bool clippedSelection(const QPainterPath &path, const QRectF &bounds, QPainterPath *result, QString *error) {
    if (path.isEmpty() || bounds.contains(path.controlPointRect())) { *result = path; return true; }
    QPainterPath extent; extent.addRect(bounds);
    if (path.elementCount() > directSelectionLimit)
        return selectionBooleanPath(path, extent, SelectionPathOperation::Intersection, bounds, result, error);
    *result = path.intersected(extent); return true;
}
// Split only once when entering Transform. During dragging these images remain
// implicitly shared; the preview renderer changes their placement, not pixels.
bool prepareTransform(const RasterLayer &layer, const RasterSelection &selection,
                      RasterLayerPreview *preview, QVector<QRect> *rows, QRectF *bounds) {
    preview->layerId = layer.id;
    preview->sourceUnchangedAtIdentity = true;
    preview->stationary = layer; preview->moving = layer; preview->moving.tiles.clear();
    QRect pixels;
    RasterSelectionCache cache(selection.active ? &selection.path : nullptr, selection.featherRadius);
    for (auto tile = layer.tiles.cbegin(); tile != layer.tiles.cend(); ++tile) {
        if (qAbs(qint64(tile.key().first)) > 128 || qAbs(qint64(tile.key().second)) > 128
            || tile->size() != QSize(tileSize, tileSize) || tile->format() != QImage::Format_ARGB32_Premultiplied)
            return false;
        const QPoint origin(tile.key().first * tileSize, tile.key().second * tileSize);
        const auto coverageTile = cache.tile(tile.key());
        if (coverageTile.coverage == RasterSelectionTile::Coverage::Empty) continue;
        if (coverageTile.coverage == RasterSelectionTile::Coverage::Invalid) return false;
        const bool full = coverageTile.coverage == RasterSelectionTile::Coverage::Full;
        const auto &mask = coverageTile.mask;
        bool hasSelected = false, hasUnselected = false;
        for (int y = 0; y < tileSize; ++y) {
            const auto *source = reinterpret_cast<const QRgb *>(tile->constScanLine(y));
            const auto *coverage = full ? nullptr : mask.constScanLine(y);
            int first = tileSize, last = -1;
            if (full) {
                first = 0;
                while (first < tileSize && !qAlpha(source[first])) ++first;
                if (first < tileSize) {
                    last = tileSize - 1;
                    while (last > first && !qAlpha(source[last])) --last;
                }
            } else {
                for (int x = 0; x < tileSize; ++x) if (qAlpha(source[x])) {
                    if (coverage[x]) { first = qMin(first, x); last = x; }
                    if (coverage[x] < 255) hasUnselected = true;
                }
            }
            if (last >= first) {
                const QRect span(origin.x() + first, origin.y() + y, last - first + 1, 1);
                rows->append(span); pixels |= span; hasSelected = true;
            }
        }
        if (!hasSelected) continue;
        if (!hasUnselected) {
            preview->moving.tiles.insert(tile.key(), tile.value());
            preview->stationary.tiles.remove(tile.key());
        } else {
            auto moving = tile->copy(), stationary = tile->copy();
            if (moving.isNull() || stationary.isNull()) return false;
            for (int y = 0; y < tileSize; ++y) {
                auto *moveRow = reinterpret_cast<QRgb *>(moving.scanLine(y));
                auto *stayRow = reinterpret_cast<QRgb *>(stationary.scanLine(y));
                const auto *coverage = mask.constScanLine(y);
                for (int x = 0; x < tileSize; ++x) {
                    const auto scalePixel = [](QRgb pixel, int scale) {
                        return qRgba((qRed(pixel) * scale + 127) / 255, (qGreen(pixel) * scale + 127) / 255,
                                     (qBlue(pixel) * scale + 127) / 255, (qAlpha(pixel) * scale + 127) / 255);
                    };
                    stayRow[x] = scalePixel(stayRow[x], 255 - coverage[x]);
                    moveRow[x] = scalePixel(moveRow[x], coverage[x]);
                }
            }
            preview->moving.tiles.insert(tile.key(), moving);
            preview->stationary.tiles.insert(tile.key(), stationary);
        }
    }
    *bounds = pixels;
    return true;
}
}
QRectF Document::drawingBounds() const {
    if (!m_open) return {};
    const auto &shot = m_project.shots[activeShotIndex()];
    const auto &sheet = shot.sheet;
    const qreal extra = sheet.overscan;
    QRectF bounds(-extra, -extra, m_project.width + extra * 2, m_project.height + extra * 2);
    if (layerIsAnnotation(shot.layers, m_active)) bounds.adjust(-sheet.left, -sheet.top, sheet.right, sheet.bottom);
    return bounds;
}
void Document::setRasterSelection(const QPainterPath &path) {
    if (!m_open || editingGestureActive() || layerPropertyEditActive()) return;
    if (!validSelectionPath(path)) {
        emit warning("The selection is invalid or too complex. Use fewer points."); return;
    }
    // Color masks are already clipped. Avoid a general polygon intersection
    // across thousands of independent texture contours in this common case.
    QPainterPath clipped; QString error;
    if (!clippedSelection(path, drawingBounds(), &clipped, &error)) { emit warning(error); return; }
    if (!validSelectionPath(clipped)) { emit warning("The selection is too complex. Use fewer points."); return; }
    m_selection = {true, clipped};
    emit rasterSelectionChanged();
}
void Document::combineRasterSelection(const QPainterPath &path, SelectionCombine mode) {
    if (!m_open || editingGestureActive() || layerPropertyEditActive()) return;
    if (!validSelectionPath(path)) { emit warning("The selection is invalid or too complex. Use fewer points."); return; }
    if (!m_selection.active || mode == SelectionCombine::Replace) { setRasterSelection(path); return; }
    if (m_selection.path.elementCount() + path.elementCount() > directSelectionLimit) {
        const auto operation = mode == SelectionCombine::Add ? SelectionPathOperation::Union
            : mode == SelectionCombine::Subtract ? SelectionPathOperation::Difference : SelectionPathOperation::Intersection;
        QPainterPath combined; QString error;
        if (!selectionBooleanPath(m_selection.path, path, operation, drawingBounds(), &combined, &error)) {
            emit warning(error); return;
        }
        setRasterSelection(combined); return;
    }
    switch (mode) {
    case SelectionCombine::Add: setRasterSelection(m_selection.path.united(path)); break;
    case SelectionCombine::Subtract: setRasterSelection(m_selection.path.subtracted(path)); break;
    case SelectionCombine::Intersect: setRasterSelection(m_selection.path.intersected(path)); break;
    case SelectionCombine::Replace: break;
    }
}
void Document::modifyRasterSelection(qreal signedPixels) {
    if (!m_open || editingGestureActive() || layerPropertyEditActive() || !m_selection.active || m_selection.path.isEmpty()) return;
    if (!std::isfinite(signedPixels) || std::abs(signedPixels) < 1 || std::abs(signedPixels) > 100) {
        emit warning("Use an amount from 1 to 100 pixels."); return;
    }
    if (m_selection.path.elementCount() > directSelectionLimit) {
        QPainterPath resized; QString error;
        if (!selectionResizePath(m_selection.path, signedPixels, drawingBounds(), &resized, &error)) {
            emit warning(error); return;
        }
        setRasterSelection(resized); return;
    }
    QPainterPathStroker stroker;
    stroker.setWidth(std::abs(signedPixels) * 2);
    stroker.setCapStyle(Qt::RoundCap); stroker.setJoinStyle(Qt::RoundJoin);
    const auto border = stroker.createStroke(m_selection.path);
    setRasterSelection(signedPixels > 0 ? m_selection.path.united(border) : m_selection.path.subtracted(border));
}
void Document::selectAllPixels() {
    if (!m_open || editingGestureActive() || layerPropertyEditActive()) return;
    QPainterPath path; path.addRect(drawingBounds()); setRasterSelection(path);
}
void Document::invertRasterSelection() {
    if (!m_open || editingGestureActive() || layerPropertyEditActive()) return;
    QPainterPath extent; extent.addRect(drawingBounds());
    // No selection means unrestricted painting, but Invert begins by selecting all.
    if (m_selection.active && m_selection.path.elementCount() > directSelectionLimit) {
        QPainterPath inverted; QString error;
        if (!selectionBooleanPath(extent, m_selection.path, SelectionPathOperation::Difference, drawingBounds(), &inverted, &error)) {
            emit warning(error); return;
        }
        setRasterSelection(inverted); return;
    }
    setRasterSelection(m_selection.active ? extent.subtracted(m_selection.path) : extent);
}
void Document::clearRasterSelection() {
    if (!m_open || editingGestureActive() || layerPropertyEditActive() || !m_selection.active) return;
    m_selection = {}; emit rasterSelectionChanged();
}
bool Document::beginTransform() {
    const auto animationReason=keyframeRasterEditReason(this);if(!animationReason.isEmpty()){emit warning(animationReason);return false;}
    if (!m_open || strokeActive() || layerPropertyEditActive()) return false;
    if (transformActive()) return true;
    const auto &layers = m_project.shots[activeShotIndex()].layers;
    const auto &layer = layers[m_active];
    if (layer.kind == LayerKind::Scene3D) {
        emit warning("Use the 3D Scene panel to move, rotate or scale 3D objects."); return false;
    }
    if (!layerCanPaint(layers, m_active)) {
        emit warning("Choose a visible, unlocked drawing layer to transform. Groups cannot be transformed yet.");
        return false;
    }
    if (layerPositionLocked(layers, m_active)) { emit warning("Unlock the layer or its group position before transforming pixels."); return false; }
    if (layer.alphaLocked) { emit warning("Turn off alpha lock before transforming pixels."); return false; }
    RasterLayerPreview prepared; QVector<QRect> rows; QRectF bounds;
    if (!prepareTransform(layer, m_selection, &prepared, &rows, &bounds)) {
        emit warning("Could not prepare the transform. Check the drawing tiles and available memory."); return false;
    }
    if (bounds.isEmpty()) {
        emit warning(m_selection.active ? "The selection contains no painted pixels on this layer."
                                        : "This layer has no painted pixels to transform.");
        return false;
    }
    m_transformBounds = bounds; m_transformRows = std::move(rows);
    m_transformParameters = {}; m_transformPreview = std::move(prepared);
    m_transformRasterized.reset();
    emit transformChanged();
    return true;
}
bool Document::previewTransform(const TransformParameters &parameters) {
    if (!transformActive()) return false;
    if (!std::isfinite(parameters.translation.x()) || !std::isfinite(parameters.translation.y())
        || !std::isfinite(parameters.scaleX) || !std::isfinite(parameters.scaleY) || !std::isfinite(parameters.rotation)
        || std::abs(parameters.translation.x()) > 32768 || std::abs(parameters.translation.y()) > 32768
        || parameters.scaleX < .01 || parameters.scaleX > 16 || parameters.scaleY < .01 || parameters.scaleY > 16
        || std::abs(parameters.rotation) > 36000) {
        emit warning("Use finite transform values, scales from 1% to 1600%, and offsets within 32768 pixels.");
        return false;
    }
    auto normalized = parameters; normalized.rotation = std::remainder(normalized.rotation, 360.0);
    const auto transform = rasterTransform(m_transformBounds, normalized);
    if (transform == m_transformPreview->transform) return true;
    // A pending transform may leave the artwork area and return without losing
    // source pixels. Only Apply clips the transformed result to drawingBounds().
    const auto target = transform.mapRect(m_transformBounds);
    if (!std::isfinite(target.left()) || !std::isfinite(target.top())
        || !std::isfinite(target.right()) || !std::isfinite(target.bottom())
        || std::abs(target.left()) > 32768 || std::abs(target.top()) > 32768
        || std::abs(target.right()) > 32768 || std::abs(target.bottom()) > 32768) {
        emit warning("The transform exceeds the supported drawing coordinates."); return false;
    }
    if (stampPlacementActive()) return previewStampPlacement(normalized, transform);
    m_transformPreview->transform = transform;
    m_transformParameters = normalized;
    m_transformRasterized.reset();
    emit transformChanged();
    return true;
}
bool Document::rasterizeTransform(QString *error) const {
    if (m_transformRasterized) return true;
    if (!transformActive()) return false;
    if (stampPlacementActive()) return rasterizeStampPlacement(error);
    const auto &layer = m_project.shots[activeShotIndex()].layers[m_active];
    const int usedTiles = projectTileCount(m_project);
    RasterLayer transformed;
    if (!transformRasterLayer(layer, m_selection.active ? &m_selection.path : nullptr, transformMatrix(), drawingBounds(),
                              maxProjectTiles - usedTiles + layer.tiles.size(), &transformed, error, m_selection.featherRadius)) return false;
    m_transformRasterized = m_project;
    m_transformRasterized->shots[activeShotIndex()].layers[m_active] = std::move(transformed);
    return true;
}
const Project &Document::displayProject() const {
    if (transformActive()) {
        QString error;
        if (rasterizeTransform(&error)) return *m_transformRasterized;
    }
    return m_project;
}
bool Document::applyTransform() {
    emit transformApplyRequested();
    if (!transformActive()) return false;
    if (stampPlacementActive()) return applyStampPlacement();
    QString error;
    if (!rasterizeTransform(&error)) { emit warning(error); return false; }
    const auto before = m_project;
    const auto after = *m_transformRasterized;
    const auto beforeSelection = m_selection;
    auto afterSelection = m_selection;
    if (afterSelection.active) {
        const auto transformedSelection = transformMatrix().map(afterSelection.path);
        if (!clippedSelection(transformedSelection, drawingBounds(), &afterSelection.path, &error)) { emit warning(error); return false; }
        if (afterSelection.path.isEmpty()) afterSelection = {};
    }
    const bool pixelsChanged = before.shots[activeShotIndex()].layers[m_active].tiles != after.shots[activeShotIndex()].layers[m_active].tiles;
    const bool selectionChanged = beforeSelection.active != afterSelection.active
        || beforeSelection.path != afterSelection.path
        || beforeSelection.featherRadius != afterSelection.featherRadius;
    if (pixelsChanged || selectionChanged) {
        // Record the final gesture, before commit clears its preview. Bounds and
        // pixels belong to the target artwork when the action is played again.
        const QJsonObject parameters{{"translationX", m_transformParameters.translation.x()},
                                     {"translationY", m_transformParameters.translation.y()},
                                     {"scaleX", m_transformParameters.scaleX},
                                     {"scaleY", m_transformParameters.scaleY},
                                     {"rotation", m_transformParameters.rotation}};
        return commit(before, after, "Transform pixels", {}, beforeSelection, afterSelection, {}, {},
                      {{"operation", "transform"}, {"parameters", parameters}});
    }
    else {
        m_selection = afterSelection;
        cancelTransform();
        emit rasterSelectionChanged();
    }
    return true;
}
void Document::cancelTransform() {
    if (!transformActive()) return;
    m_stampPlacement.reset();
    m_transformPreview.reset(); m_transformRasterized.reset(); m_transformRows.clear();
    m_transformParameters = {}; m_transformBounds = {};
    emit transformChanged();
}
} // namespace toon
