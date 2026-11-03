#pragma once

#include "project.h"
#include <atomic>
#include <memory>

namespace toon {
class Document;
struct RecolorTarget {
    QString shotId, layerId, name;
    RasterLayer original;
};
struct RecolorSettings {
    CharacterModel character;
    QString sourceVariantId, targetVariantId; // Empty means Day.
    QSize canvasSize;
};
struct RecolorDrawing {
    RecolorTarget target;
    RasterLayer replacement;
    QImage before, after;
    QRectF previewBounds;
    qint64 changedPixels = 0, matchedPixels = 0, ambiguousEdgePixels = 0;
};
struct RecolorResult {
    RecolorSettings settings;
    QVector<RecolorDrawing> drawings;
    QString error;
    bool cancelled = false;
    qint64 changedPixels = 0, matchedPixels = 0, ambiguousEdgePixels = 0;
};
using RecolorCancellation = std::shared_ptr<std::atomic_bool>;
// Empty means editable raw raster paint. Exposure timing and alpha locks do not
// prevent recoloring; hidden/locked layers or ancestors and notes are excluded.
QString characterRecolorTargetError(const Shot &shot, int layerIndex);
// Exact role-color mapping, with alpha-preserving premultiplied edge matching.
// Opaque edge mixtures require two exact source colors in the original 3x3
// neighborhood. Other mixed colors and ambiguous edges are left as-is.
// Processes full tiles (including overscan), at most 64 drawings / 64M tile pixels.
// Preview images are bounded to 2048 per side and 16M pixels per batch per view.
RecolorResult prepareCharacterRecolor(QVector<RecolorTarget> targets,
    const RecolorSettings &settings, const RecolorCancellation &cancel = {});
// All sources and selected palettes are checked before a single batch Undo edit.
// A valid no-change result succeeds without adding Undo history.
bool applyCharacterRecolor(Document *document, const RecolorResult &result, QString *error = nullptr);
} // namespace toon
