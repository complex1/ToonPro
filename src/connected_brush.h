#pragma once
#include "brush_engine.h"

namespace toon {
struct ConnectedBrushState {
    QPointF point;
    BrushInput input;
    qreal width = 1, direction = 0, distance = 0;
    bool hasPoint = false, hasDirection = false;
};
// Streams an image along joined cross sections. Temporary images are bounded
// to one raster tile; rasterBandHeight=tileSize provides a full-tile reference; path phase is distance based, independent of input rate.
bool appendConnectedBrush(RasterLayer &layer, RasterLayer &accumulation,
                          const RasterLayer &original, ConnectedBrushState &state,
                          const BrushSettings &brush, QPointF point, BrushInput input,
                          quint64 index, quint32 seed, QSizeF canvasSize, QRectF bounds,
                          int maxTiles, bool eraser = false,
                          const QPainterPath *selection = nullptr,
                          RasterSelectionCache *selectionCache = nullptr,
                          const QTransform &objectTransform = {},
                          int rasterBandHeight = 16);
} // namespace toon
