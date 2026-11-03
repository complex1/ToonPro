#pragma once

#include <QImage>
#include <QJsonObject>
#include <QString>

namespace toon {
struct VectorBrushStroke;
inline constexpr int maxVectorBrushSamples = 65536;
inline constexpr int maxProjectVectorBrushSamples = 262144;
inline constexpr int maxVectorBrushResourceDimension = 2048;
inline constexpr qint64 maxProjectVectorBrushResourcePixels = 16000000;
inline constexpr int maxProjectVectorBrushResources = 1024;

// Source settings and tablet records are metadata; image resources are stored
// once per project in the binary container, independently of the brush library.
bool usesModernVectorBrushCapture(const VectorBrushStroke &stroke);
QJsonObject vectorBrushStrokeToJson(const VectorBrushStroke &stroke);
bool vectorBrushStrokeFromJson(const QJsonObject &object, VectorBrushStroke *stroke, QString *error);
bool validateVectorBrushStroke(const VectorBrushStroke &stroke, QString *error = nullptr);
QString vectorBrushResourceId(const QImage &image);
bool validVectorBrushResourceId(const QString &id);
} // namespace toon
