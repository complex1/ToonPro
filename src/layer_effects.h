#pragma once
#include <QColor>
#include <QImage>
#include <QJsonObject>
#include <QMap>
#include <QPointF>
#include <QString>
#include <QTransform>

namespace toon {
// Parameters are in layer/source pixels; none of these edits alter source tiles.
struct LayerEffects {
    bool enabled = true;
    bool blurEnabled = false;
    qreal blurRadius = 4;
    bool shadowEnabled = false;
    qreal shadowRadius = 8;
    QPointF shadowOffset{8, 8};
    QColor shadowColor{Qt::black};
    qreal shadowOpacity = .5;
    bool glowEnabled = false;
    qreal glowRadius = 8;
    QColor glowColor{Qt::white};
    qreal glowOpacity = .5;
    bool outlineEnabled = false;
    qreal outlineWidth = 3;
    QColor outlineColor{Qt::white};
    bool colorEnabled = false;
    qreal exposure = 0; // Stops.
    qreal contrast = 0; // -100 ... 100.
    qreal saturation = 0; // -100 ... 100.
    qreal hue = 0; // Degrees.
    bool operator==(const LayerEffects &) const = default;
};
struct LayerMask {
    bool enabled = true;
    int defaultValue = 255;
    QMap<QPair<int,int>, QImage> tiles; // 256px Grayscale8; white reveals, black hides.
    QPointF position;
    QPointF scale{1, 1};
    qreal rotation = 0;
    QPointF pivot; // Independent mask-space registration point.
    qreal opacity = 1; // Mask strength: zero bypasses, one fully applies coverage.
    qreal feather = 0; // Mask/source pixels, before its local transform.
    bool inverted = false;
    bool operator==(const LayerMask &) const = default;
};
inline constexpr int maxProjectMaskTiles = 1024;
QString validateLayerEffects(const LayerEffects &effects);
QString validateLayerMask(const LayerMask &mask, bool pixelsRequired = true);
QJsonObject layerEffectsToJson(const LayerEffects &effects);
bool layerEffectsFromJson(const QJsonObject &json, LayerEffects *effects, QString *error);
QJsonObject layerMaskToJson(const LayerMask &mask);
bool layerMaskFromJson(const QJsonObject &json, LayerMask *mask, QString *error);
bool hasLayerEffects(const LayerEffects &effects);
bool hasLayerOutlineSettings(const LayerEffects &effects);
bool layerMaskHasProperties(const LayerMask &mask);
QTransform layerMaskTransform(const LayerMask &mask);
} // namespace toon
