#pragma once
#include "raster.h"
#include <QSet>

namespace toon {
// Workspace isolation never changes saved visibility. Keep selected subtrees,
// their compositing ancestors, and the artwork required by clipping/adjustment
// layers. Empty or wholly stale selections show the ordinary layer stack.
QSet<QString> timelineSoloKeptLayerIds(const QVector<RasterLayer> &layers,
                                     const QSet<QString> &roots);
QVector<RasterLayer> timelineSoloPreviewLayers(const QVector<RasterLayer> &layers,
                                             const QSet<QString> &kept);
}
