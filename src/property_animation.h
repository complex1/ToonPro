#pragma once
#include "editable_object.h"
#include "layer_effects.h"
#include <QJsonArray>
#include <optional>
#include <utility>

namespace toon {
struct Shot;
struct Project;
struct RasterLayer;
inline constexpr int maxPropertyAnimatedLayers = 1024;
inline constexpr int maxPropertyKeyframes = 16384;
inline constexpr int maxPathKeyframeNodes = 262144;
enum class KeyframeProperty {
    PositionX, PositionY, ScaleX, ScaleY, Rotation, Opacity,
    EffectsEnabled, BlurEnabled, BlurRadius,
    ShadowEnabled, ShadowRadius, ShadowOffsetX, ShadowOffsetY, ShadowOpacity,
    GlowEnabled, GlowRadius, GlowOpacity, ColorEnabled, Exposure, Contrast, Saturation, Hue,
    MaskPositionX, MaskPositionY, MaskScaleX, MaskScaleY, MaskRotation, MaskOpacity,
    MaskFeather, MaskEnabled, MaskInverted, Count
};
inline constexpr int keyframePropertyCount = int(KeyframeProperty::Count);
enum class KeyframeInterpolation { Hold, Linear, EaseIn, EaseOut, EaseInOut };
struct CubicEasing {
    QPointF first{1.0 / 3, 1.0 / 3};
    QPointF second{2.0 / 3, 2.0 / 3};
    bool operator==(const CubicEasing &) const = default;
};
struct SpatialTangents {
    // Offsets from this key's position in its parent layer coordinates.
    QPointF incoming, outgoing;
    bool curved = false; // This key's outgoing segment uses the cubic handles.
    bool operator==(const SpatialTangents &) const = default;
};
struct ScalarKeyframe {
    int frame = 0;
    qreal value = 0;
    KeyframeInterpolation interpolation = KeyframeInterpolation::Linear;
    std::optional<CubicEasing> easing = std::nullopt;
    std::optional<SpatialTangents> spatial = std::nullopt;
    bool operator==(const ScalarKeyframe &) const = default;
};
struct PropertyChannel {
    KeyframeProperty property = KeyframeProperty::PositionX;
    QVector<ScalarKeyframe> keys;
    bool operator==(const PropertyChannel &) const = default;
};
// Only editable pose values are copied. Brush image resources, dabs and seeds
// remain on the original object, so morph keys never duplicate source stamps.
struct PathPose {
    QVector<BezierNode> nodes;
    QVector<qreal> widthFactors;
    bool closed = false;
    QTransform transform;
    QColor stroke, fill;
    qreal strokeWidth = 3, opacity = 1;
    bool operator==(const PathPose &) const = default;
};
struct PathKeyframe {
    int frame = 0;
    PathPose pose;
    KeyframeInterpolation interpolation = KeyframeInterpolation::Linear;
    std::optional<CubicEasing> easing = std::nullopt;
    bool operator==(const PathKeyframe &) const = default;
};
struct PathAnimation {
    QString objectId;
    QVector<PathKeyframe> keys;
    bool operator==(const PathAnimation &) const = default;
};
struct LayerAnimation {
    QString layerId;
    QPointF pivot;
    QVector<PropertyChannel> channels;
    QVector<PathAnimation> paths;
    bool orientAlongPath = false;
    bool operator==(const LayerAnimation &) const = default;
};
QString keyframePropertyKey(KeyframeProperty property);
QString keyframePropertyName(KeyframeProperty property);
bool keyframePropertyIsEffect(KeyframeProperty property);
bool keyframePropertyIsMask(KeyframeProperty property);
bool keyframePropertyIsDiscrete(KeyframeProperty property);
bool keyframePropertyIsPercent(KeyframeProperty property);
bool keyframePropertyNeedsPositionUnlock(KeyframeProperty property);
qreal keyframePropertyMinimum(KeyframeProperty property);
qreal keyframePropertyMaximum(KeyframeProperty property);
// Existing transform/opacity channels retain their neutral additive/multiplier
// values. Compositing channels override the stored layer settings absolutely.
qreal keyframeSourceValue(const RasterLayer &layer, KeyframeProperty property);
// Changes only a compositing base parameter, retaining source pixels and all other fields.
bool setKeyframeSourceValue(RasterLayer &layer, KeyframeProperty property, qreal value);
QString keyframeInterpolationKey(KeyframeInterpolation interpolation);
QString keyframeInterpolationName(KeyframeInterpolation interpolation);
qreal keyframeDefaultValue(KeyframeProperty property);
bool validKeyframeValue(KeyframeProperty property, qreal value);
qreal keyframeProgress(qreal t, KeyframeInterpolation interpolation);
bool validCubicEasing(const CubicEasing &easing);
CubicEasing easingForInterpolation(KeyframeInterpolation interpolation);
qreal keyframeProgress(qreal t, KeyframeInterpolation interpolation, const std::optional<CubicEasing> &easing);
// Normalized outgoing curves before/after an inserted interior time. The caller
// retains the original interpolation mode on both keys.
std::pair<std::optional<CubicEasing>, std::optional<CubicEasing>> splitKeyframeEasing(
    qreal t, KeyframeInterpolation interpolation, const std::optional<CubicEasing> &easing);
// Spatial curves share complete X/Y timing and easing; old independent channels remain valid.
bool positionChannelsPaired(const LayerAnimation &animation);
bool hasSpatialMotion(const LayerAnimation &animation);
QPointF evaluateAnimatedPosition(const LayerAnimation &animation, qreal frame);
QPointF animatedPositionDirection(const LayerAnimation &animation, qreal frame);
qreal evaluateProperty(const LayerAnimation &animation, KeyframeProperty property, qreal frame);
QTransform evaluateLayerTransform(const LayerAnimation &animation, qreal frame);
const LayerAnimation *layerAnimation(const Shot &shot, const QString &layerId);
LayerAnimation *layerAnimation(Shot &shot, const QString &layerId);
const PropertyChannel *propertyChannel(const Shot &shot, const QString &layerId, KeyframeProperty property);
const PathAnimation *pathAnimation(const Shot &shot, const QString &layerId, const QString &objectId);
qreal evaluateProperty(const Shot &shot, const QString &layerId, KeyframeProperty property, qreal frame);
QTransform evaluateLayerTransform(const Shot &shot, const QString &layerId, qreal frame);
LayerEffects evaluateAnimatedLayerEffects(const Shot &shot, const QString &layerId, qreal frame,
                                         const LayerEffects &base);
LayerMask evaluateAnimatedLayerMask(const Shot &shot, const QString &layerId, qreal frame,
                                   const LayerMask &base);
QPointF defaultAnimationPivot(const Shot &shot, const QString &layerId, QSizeF fallbackSize = {});
// Symbols (including group descendants) use their artwork at the chosen frame,
// in the selected layer's source coordinates. Before playback starts, use the
// instance's first frame. This is sampled only when creating/resetting a pivot.
QPointF defaultAnimationPivot(const Project &project, const Shot &shot, const QString &layerId, int frame = 0);
QTransform worldLayerTransform(const Shot &shot, const QString &layerId, qreal frame);
PathPose pathPose(const EditableObject &object);
void applyPathPose(EditableObject &object, const PathPose &pose);
EditableObject evaluateAnimatedObject(const Shot &shot, const QString &layerId, const EditableObject &source, qreal frame);
// Checks the layer and its ancestry/descendants; locks are checked separately.
QString propertyAnimationUnavailableReason(const Shot &shot, const QString &layerId);
QString keyframePropertyUnavailableReason(const Shot &shot, const QString &layerId, KeyframeProperty property);
QString validatePropertyAnimation(const Shot &shot);
QString validatePropertyAnimationMutation(const Shot &before, const Shot &after);
// Remove channels only when their layer or object was deliberately removed.
void prunePropertyAnimation(Shot &shot);
// Writes into a candidate shot without changing Document history or signals.
// A failed write restores the candidate's animation channels.
bool writePropertyKeyframe(const Project &project, Shot &shot, const QString &layerId,
                           KeyframeProperty property, int frame, qreal value, QString *error = nullptr);
// Used by the vector editing gesture: the source object remains unchanged.
bool writePathKeyframe(Shot &shot, const QString &layerId, const QString &objectId, int frame,
                       const EditableObject &pose, QString *error = nullptr);
QJsonArray layerAnimationsToJson(const QVector<LayerAnimation> &animations);
bool layerAnimationsFromJson(const QJsonArray &json, QVector<LayerAnimation> *animations, QString *error);
qint64 propertyAnimationHistoryCost(const Shot &before, const Shot &after);
} // namespace toon
