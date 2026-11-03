#pragma once
#include <QImage>
#include <QJsonObject>
#include <QPointF>
#include <QSizeF>
#include <QString>
#include <array>

namespace toon {
enum class BrushSurfaceKind { Off, Dots, Lines, Crosshatch, Image, Diamond, Noise, FadeDots };
// Coordinates and frequency are in artwork pixels, independent of dab position.
// Perspective clips the surface to an explicit TL/TR/BR/BL artwork-space plane.
struct BrushSurfacePattern {
    BrushSurfaceKind kind = BrushSurfaceKind::Off;
    qreal frequency = 10; // cycles per 100 artwork pixels
    qreal density = .3;   // ink area fraction, independent of frequency
    qreal angle = 0;
    qreal densityEnd = 0;
    qreal fadeLength = 512;
    quint32 seed = 1;
    QPointF phase{0, 0};
    QSizeF tileSize{128, 128};
    bool perspective = false;
    std::array<QPointF, 4> quad{{{0, 0}, {512, 0}, {512, 512}, {0, 512}}};
    QSizeF planeSize{512, 512};
    QImage image; // immutable premultiplied RGBA tile, at most 2048 x 2048
    bool operator==(const BrushSurfacePattern &) const = default;
};
bool validateBrushSurface(const BrushSurfacePattern &pattern, QString *error = nullptr);
// Resource bytes are encoded by the existing library/project resource layer.
QJsonObject brushSurfaceToJson(const BrushSurfacePattern &pattern);
bool brushSurfaceFromJson(const QJsonObject &json, BrushSurfacePattern &pattern,
                          QString *error = nullptr);
} // namespace toon
