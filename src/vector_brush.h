#pragma once
#include "editable_object.h"

namespace toon {
QByteArray editableGeometrySignature(const EditableObject &object);
qreal vectorBrushWidthAt(const EditableObject &object, qreal position);
QRectF vectorBrushBounds(const EditableObject &object);
// Test painted dab alpha, including sampled-tip holes, captured selection and
// symmetry. Normal stamp coverage is cached within 16 MiB; oversized transformed
// footprints use bounded query tiles rather than allocating the whole stamp.
bool vectorBrushIntersects(const EditableObject &object, const QPainterPath &artworkRegion);
QVector<qreal> vectorBrushIntersectionPositions(const EditableObject &object, const QPainterPath &artworkRegion, bool firstOnly = false);
// Geometry-only painted outline used for variable-width pen paths and hit tests.
// Brush stamp textures remain raster-rendered by the shared brush engine.
QPainterPath editableStrokeOutline(const EditableObject &object);
bool replayVectorBrushStroke(RasterLayer &layer, const EditableObject &object,
                             QRectF bounds, int maxTiles, QString *error = nullptr);
// Discard derived replay data; source artwork and brush resources are unchanged.
void clearVectorBrushReplayCache();
// Reuse the live brush engine's 16-bit accumulation when a vector stroke ends.
// Captured alpha-locked paint is backdrop-limited and cannot seed this cache.
void rememberVectorBrushPaint(const EditableObject &object, QRectF bounds,
                              const RasterLayer &accumulation);
// fragment already contains the desired subset of nodes/widthFactors. Trim the
// source dabs to the old path interval and reparameterize for the new geometry.
void sliceVectorBrushStroke(EditableObject &fragment, qreal from, qreal to, qreal sourceLength = -1);
} // namespace toon
