#pragma once
#include "project.h"
namespace toon {
class Document;
bool applyLayerAnimations(const Shot &shot, int frame, QVector<RasterLayer> &layers, QRectF bounds = {}, QString *error = nullptr);
QString keyframeRasterEditReason(const Document *document, const QString &layerId = {});
QString keyframeRenderError();
void clearKeyframeRenderCache();
}
