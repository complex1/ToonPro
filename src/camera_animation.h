#pragma once
#include "property_animation.h"
#include "scene3d.h"
#include <QJsonObject>
#include <QSizeF>

namespace toon {
struct Shot;
inline constexpr int maxCameraKeyframes = 16384;
struct Camera2D {
    // Offset from the artwork centre, in artwork pixels. Positive rotation
    // rotates the camera clockwise; the viewed artwork rotates oppositely.
    QPointF position;
    qreal zoom = 1, rotation = 0;
    bool operator==(const Camera2D &) const = default;
};
struct Camera2DKeyframe {
    int frame = 0;
    Camera2D pose;
    KeyframeInterpolation interpolation = KeyframeInterpolation::Linear;
    bool operator==(const Camera2DKeyframe &) const = default;
};
struct SceneCameraPose {
    QVector3D target{0, .8f, 0};
    qreal yaw = 30, pitch = 15, distance = 5, fieldOfView = 45, orthoScale = 3;
    bool operator==(const SceneCameraPose &) const = default;
};
struct SceneCameraKeyframe {
    int frame = 0;
    SceneCameraPose pose;
    KeyframeInterpolation interpolation = KeyframeInterpolation::Linear;
    bool operator==(const SceneCameraKeyframe &) const = default;
};
struct SceneCameraAnimation {
    QString layerId;
    QVector<SceneCameraKeyframe> keys;
    bool operator==(const SceneCameraAnimation &) const = default;
};
SceneCameraPose sceneCameraPose(const Camera3D &camera);
Camera3D applySceneCameraPose(Camera3D base, const SceneCameraPose &pose);
bool validShotCamera(const Camera2D &camera);
bool validSceneCameraPose(const SceneCameraPose &pose);
const SceneCameraAnimation *sceneCameraAnimation(const Shot &shot, const QString &layerId);
SceneCameraAnimation *sceneCameraAnimation(Shot &shot, const QString &layerId);
Camera2D evaluateShotCamera(const Shot &shot, int frame);
Camera3D evaluateSceneCamera(const Shot &shot, const QString &layerId, int frame);
QTransform shotCameraTransform(const Shot &shot, int frame, QSizeF outputSize);
QString validateCameraAnimation(const Shot &shot);
// Editing guard for a scene camera; copy/read operations may ignore locks.
QString sceneCameraEditError(const Shot &shot, const QString &layerId, bool respectLock = true);
void pruneCameraAnimation(Shot &shot);
qint64 cameraAnimationHistoryCost(const Shot &before, const Shot &after);
QJsonObject shotCameraToJson(const Shot &shot);
bool shotCameraFromJson(const QJsonObject &json, Shot *shot, QString *error);
QJsonArray sceneCameraAnimationsToJson(const QVector<SceneCameraAnimation> &animations);
bool sceneCameraAnimationsFromJson(const QJsonArray &json, QVector<SceneCameraAnimation> *animations, QString *error);
} // namespace toon
