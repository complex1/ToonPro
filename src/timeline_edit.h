#pragma once

#include "property_animation.h"
#include "camera_animation.h"
#include "symbol_types.h"
#include <optional>

namespace toon {
enum class TimelineKeyKind { Layer, Camera2D, Camera3D, SymbolPose };
struct TimelineKeyRef {
    QString layerId;
    KeyframeProperty property = KeyframeProperty::PositionX;
    QString objectId;
    int frame = 0;
    TimelineKeyKind kind = TimelineKeyKind::Layer;
    bool isCamera() const { return kind == TimelineKeyKind::Camera2D || kind == TimelineKeyKind::Camera3D; }
    bool isPath() const { return kind == TimelineKeyKind::Layer && !objectId.isEmpty(); }
    bool isSymbolPose() const { return kind == TimelineKeyKind::SymbolPose; }
    bool operator==(const TimelineKeyRef &) const = default;
};
struct TimelineKeyValue {
    TimelineKeyRef source;
    ScalarKeyframe scalar;
    std::optional<PathKeyframe> path;
    std::optional<Camera2DKeyframe> camera2D;
    std::optional<SceneCameraKeyframe> camera3D;
    // Clipboard timestamps use parent frames; canonical instance keys use clock frames.
    std::optional<SymbolPoseKey> symbolPose;
    QString symbolDefinitionId;
};
// A value snapshot, independent of the current selection and later source edits.
// Keys paste into their original channels within the same shot.
struct TimelineKeyClipboard {
    QString shotId;
    QVector<TimelineKeyValue> keys;
};
} // namespace toon
