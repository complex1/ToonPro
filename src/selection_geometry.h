#pragma once
#include <QImage>
#include <QPainterPath>
#include <QString>

namespace toon {
inline constexpr int maxSelectionPathElements = 262144;
// Exact binary-mask contours with no internal edges or general polygon union.
// Reports excessive detail before returning any partial selection.
QPainterPath selectionPathFromMask(const QImage &mask, QPoint origin, bool *tooComplex);
enum class SelectionPathOperation { Union, Difference, Intersection };
// Bounded hard-pixel operations for detailed raster selections. The result is
// assigned only on success; callers can keep the previous selection on failure.
bool selectionBooleanPath(const QPainterPath &left, const QPainterPath &right,
                          SelectionPathOperation operation, const QRectF &bounds,
                          QPainterPath *result, QString *error);
bool selectionResizePath(const QPainterPath &path, qreal signedPixels, const QRectF &bounds,
                         QPainterPath *result, QString *error);
} // namespace toon
