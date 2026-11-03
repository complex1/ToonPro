#pragma once
#include "cel_shading.h"
#include <QJsonObject>
#include <QTransform>
#include <optional>

namespace toon {
inline constexpr int maxLightTablePins = 8;
inline constexpr int maxLightTablePinDimension = 2048;
inline constexpr qint64 maxLightTablePinBytes = 64LL * 1024 * 1024;
inline constexpr int maxCelShadingDrafts = 64;
inline constexpr int maxCelDraftPathElements = 1000000;

struct LightTablePin {
    QString id, name, shotId, layerId;
    QImage image;
    QRectF bounds;
    QPointF offset;
    qreal scale = 1, rotation = 0, opacity = .45;
    bool visible = true;
    QTransform transform() const;
};
struct LightTableState {
    QVector<LightTablePin> pins;
    QString selectedPinId;
    bool solo = false, visible = true;
};
struct CelShadingDraft {
    QString shotId, outlineLayerId;
    int frame = 0;
    QVector<CelBoundary> boundaries;
    QVector<CelShadeFill> fills;
    QVector<bool> operations;
    CelBoundaryFinish finish = CelBoundaryFinish::Hidden;
    QColor shadow = QColor("#77597e"), highlight = QColor("#fff0b5");
    QColor shadowGuide = QColor("#5cb8ed"), highlightGuide = QColor("#ef8991");
    qreal width = 2;
    int kind = 0, mode = 0;
};
struct DrawingAids {
    LightTableState lightTable;
    QVector<CelShadingDraft> shadingDrafts;
    QString activeShotId;
    int activeFrame = 0;
};

// Empty state keeps the established project format. Snapshot pixels are binary
// resources; metadata-only parses leave them empty until loadProject reads PNGs.
bool hasDrawingAids(const DrawingAids &aids);
QString validateDrawingAids(const DrawingAids &aids, bool requireImages = true);
QJsonObject drawingAidsToJson(const DrawingAids &aids);
std::optional<DrawingAids> drawingAidsFromJson(const QJsonObject &json, QString *error);
bool celShadingDraftEqual(const CelShadingDraft &a, const CelShadingDraft &b);
bool lightTableStateEqual(const LightTableState &a, const LightTableState &b);
bool drawingAidsEqual(const DrawingAids &a, const DrawingAids &b);
} // namespace toon
