#pragma once
#include "project.h"
#include <QImage>
#include <QPainter>
#include <QRectF>

namespace toon {
struct SheetGeometry {
    QRectF paper;
    QRectF header;
    QRectF drawing;
    QRectF output;
};
SheetGeometry sheetGeometry(const Project &project, const Shot &shot);
// Split for background previews: GUI-prepared text/paper retain identical
// native font rasterization; immutable artwork can then render on a worker.
void paintSheetBackground(QPainter &painter, const Project &project, const Shot &shot, const SheetGeometry &geometry);
void paintSheetArtwork(QPainter &painter, const Project &project, const Shot &shot, const SheetGeometry &geometry,
                       bool guides = true, const RasterLayerPreview *preview = nullptr, bool smoothDisplay = false,
                       bool sceneReferences = false, int frame = 0, bool applyCamera = true);
void paintSheet(QPainter &painter, const Project &project, const Shot &shot, bool guides = true,
                const RasterLayerPreview *preview = nullptr, bool smoothDisplay = false, bool sceneReferences = false,
                int frame = 0, bool applyCamera = true);
QImage renderSheet(const Project &project, const Shot &shot, bool guides = true, int frame = 0);
QImage renderMovieFrame(const Project &project, int frame = 0, int shotIndex = 0);
bool savePng(const QImage &image, const QString &path, QString *error);
} // namespace toon
