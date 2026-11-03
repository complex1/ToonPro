#include "camera_animation.h"
#include "project.h"
#include <QJsonArray>
#include <QSet>
#include <algorithm>
#include <cmath>

namespace toon {
namespace {
bool finite(qreal value, qreal limit) { return std::isfinite(value) && qAbs(value) <= limit; }
const RasterLayer *sceneLayer(const Shot &shot, const QString &id) {
    for (const auto &layer : shot.layers) if (layer.id == id) return &layer;
    return nullptr;
}
template<class Key> std::pair<const Key *, const Key *> bracket(const QVector<Key> &keys, int frame) {
    const auto next = std::upper_bound(keys.cbegin(), keys.cend(), frame, [](int at, const Key &key) { return at < key.frame; });
    if (next == keys.cbegin()) return {&keys.first(), &keys.first()};
    if (next == keys.cend()) return {&keys.last(), &keys.last()};
    return {&*(next - 1), &*next};
}
qreal mix(qreal a, qreal b, qreal t) { return a + (b - a) * t; }
bool fields(const QJsonObject &o, std::initializer_list<const char *> names) {
    if (o.size() != qsizetype(names.size())) return false;
    for (const auto *name : names) if (!o.contains(QLatin1String(name))) return false;
    return true;
}
bool number(const QJsonValue &v, qreal *out) {
    if (!v.isDouble() || !std::isfinite(v.toDouble())) return false;
    *out = v.toDouble(); return true;
}
QJsonObject poseJson(const Camera2D &pose) {
    return {{"position", QJsonArray{pose.position.x(), pose.position.y()}}, {"zoom", pose.zoom}, {"rotation", pose.rotation}};
}
QJsonObject poseJson(const SceneCameraPose &pose) {
    return {{"target", QJsonArray{pose.target.x(), pose.target.y(), pose.target.z()}}, {"yaw", pose.yaw},
        {"pitch", pose.pitch}, {"distance", pose.distance}, {"fieldOfView", pose.fieldOfView}, {"orthoScale", pose.orthoScale}};
}
bool readPose(const QJsonObject &o, Camera2D *pose) {
    if (!fields(o, {"position", "zoom", "rotation"}) || !o["position"].isArray()) return false;
    const auto position = o["position"].toArray(); qreal x = 0, y = 0;
    if (position.size() != 2 || !number(position[0], &x) || !number(position[1], &y)
        || !number(o["zoom"], &pose->zoom) || !number(o["rotation"], &pose->rotation)) return false;
    pose->position = {x, y}; return validShotCamera(*pose);
}
bool readPose(const QJsonObject &o, SceneCameraPose *pose) {
    if (!fields(o, {"target", "yaw", "pitch", "distance", "fieldOfView", "orthoScale"}) || !o["target"].isArray()) return false;
    const auto target = o["target"].toArray(); qreal x = 0, y = 0, z = 0;
    if (target.size() != 3 || !number(target[0], &x) || !number(target[1], &y) || !number(target[2], &z)
        || !finite(x, 1000000) || !finite(y, 1000000) || !finite(z, 1000000)
        || !number(o["yaw"], &pose->yaw) || !number(o["pitch"], &pose->pitch)
        || !number(o["distance"], &pose->distance) || !number(o["fieldOfView"], &pose->fieldOfView)
        || !number(o["orthoScale"], &pose->orthoScale)) return false;
    pose->target = QVector3D(float(x), float(y), float(z)); return validSceneCameraPose(*pose);
}
template<class Key> QJsonArray keysJson(const QVector<Key> &keys) {
    QJsonArray result;
    for (const auto &key : keys) result.append(QJsonObject{{"frame", key.frame}, {"pose", poseJson(key.pose)}, {"interpolation", keyframeInterpolationKey(key.interpolation)}});
    return result;
}
template<class Key> bool readKeys(const QJsonArray &array, QVector<Key> *keys, int *count) {
    if (array.size() > maxCameraKeyframes - *count) return false;
    int previous = -1;
    for (const auto &value : array) {
        if (!value.isObject()) return false;
        const auto o = value.toObject(); qreal frame = -1; Key key;
        if (!fields(o, {"frame", "pose", "interpolation"}) || !number(o["frame"], &frame)
            || frame < 0 || frame > 999999 || std::floor(frame) != frame || frame <= previous
            || !o["pose"].isObject() || !readPose(o["pose"].toObject(), &key.pose)) return false;
        bool found = false;
        for (int i = 0; i <= int(KeyframeInterpolation::EaseInOut); ++i)
            if (o["interpolation"] == keyframeInterpolationKey(KeyframeInterpolation(i))) { key.interpolation = KeyframeInterpolation(i); found = true; break; }
        if (!found) return false;
        key.frame = int(frame); previous = key.frame; keys->append(key); ++*count;
    }
    return true;
}
template<class Key, class Valid> bool validKeys(const QVector<Key> &keys, int duration, int *count, Valid valid) {
    int previous = -1;
    for (const auto &key : keys) {
        if (++*count > maxCameraKeyframes || key.frame <= previous || key.frame >= duration
            || keyframeInterpolationKey(key.interpolation).isEmpty() || !valid(key.pose)) return false;
        previous = key.frame;
    }
    return true;
}
}
SceneCameraPose sceneCameraPose(const Camera3D &camera) {
    return {camera.target, camera.yaw, camera.pitch, camera.distance, camera.fieldOfView, camera.orthoScale};
}
Camera3D applySceneCameraPose(Camera3D base, const SceneCameraPose &pose) {
    base.target = pose.target; base.yaw = float(pose.yaw); base.pitch = float(pose.pitch);
    base.distance = float(pose.distance); base.fieldOfView = float(pose.fieldOfView); base.orthoScale = float(pose.orthoScale); return base;
}
bool validShotCamera(const Camera2D &camera) {
    return finite(camera.position.x(), 32768) && finite(camera.position.y(), 32768)
        && finite(camera.rotation, 36000) && std::isfinite(camera.zoom) && camera.zoom >= .01 && camera.zoom <= 100;
}
bool validSceneCameraPose(const SceneCameraPose &pose) {
    return finite(pose.target.x(), 1000000) && finite(pose.target.y(), 1000000) && finite(pose.target.z(), 1000000)
        && finite(pose.yaw, 1000000) && finite(pose.pitch, qreal(89.9f)) && std::isfinite(pose.distance)
        && pose.distance >= .001 && pose.distance <= 100000 && std::isfinite(pose.fieldOfView)
        && pose.fieldOfView >= 5 && pose.fieldOfView <= 150 && std::isfinite(pose.orthoScale)
        && pose.orthoScale >= .001 && pose.orthoScale <= 100000;
}
const SceneCameraAnimation *sceneCameraAnimation(const Shot &shot, const QString &id) {
    for (const auto &animation : shot.sceneCameraAnimations) if (animation.layerId == id) return &animation;
    return nullptr;
}
SceneCameraAnimation *sceneCameraAnimation(Shot &shot, const QString &id) {
    for (auto &animation : shot.sceneCameraAnimations) if (animation.layerId == id) return &animation;
    return nullptr;
}
Camera2D evaluateShotCamera(const Shot &shot, int frame) {
    if (shot.camera2DKeys.isEmpty()) return shot.camera2D;
    const auto [a, b] = bracket(shot.camera2DKeys, frame); if (a == b) return a->pose;
    const qreal t = keyframeProgress(qreal(frame - a->frame) / (b->frame - a->frame), a->interpolation);
    return {a->pose.position + (b->pose.position - a->pose.position) * t,
        mix(a->pose.zoom, b->pose.zoom, t), mix(a->pose.rotation, b->pose.rotation, t)};
}
Camera3D evaluateSceneCamera(const Shot &shot, const QString &id, int frame) {
    const auto *layer = sceneLayer(shot, id);
    if (!layer || !layer->scene3d) return {};
    const auto base = layer->scene3d->camera; const auto *animation = sceneCameraAnimation(shot, id);
    if (!animation || animation->keys.isEmpty()) return base;
    const auto [a, b] = bracket(animation->keys, frame); if (a == b) return applySceneCameraPose(base, a->pose);
    const qreal t = keyframeProgress(qreal(frame - a->frame) / (b->frame - a->frame), a->interpolation);
    const auto &p = a->pose; const auto &q = b->pose;
    return applySceneCameraPose(base, {p.target + (q.target - p.target) * float(t), mix(p.yaw, q.yaw, t),
        mix(p.pitch, q.pitch, t), mix(p.distance, q.distance, t), mix(p.fieldOfView, q.fieldOfView, t), mix(p.orthoScale, q.orthoScale, t)});
}
QTransform shotCameraTransform(const Shot &shot, int frame, QSizeF size) {
    const auto camera = evaluateShotCamera(shot, frame); QTransform transform;
    const QPointF center(size.width() / 2, size.height() / 2);
    transform.translate(center.x(), center.y()); transform.scale(camera.zoom, camera.zoom); transform.rotate(-camera.rotation);
    transform.translate(-center.x() - camera.position.x(), -center.y() - camera.position.y()); return transform;
}
QString sceneCameraEditError(const Shot &shot, const QString &id, bool respectLock) {
    const auto *layer = sceneLayer(shot, id);
    if (!layer || layer->kind != LayerKind::Scene3D || !layer->scene3d || layer->group) return "Choose an existing 3D scene camera.";
    const int index = int(layer - shot.layers.constData());
    if (layerIsAnnotation(shot.layers, index)) return "Production notes cannot have animated cameras.";
    if (!respectLock) return {};
    if (layer->scene3d->camera.locked) return "Unlock the 3D camera before editing its keys.";
    if (layerPositionLocked(shot.layers, index)) return "Unlock the 3D layer position before editing its camera.";
    QSet<QString> visited;
    while (layer) {
        if (visited.contains(layer->id)) return "The 3D layer has invalid folder ancestry.";
        visited.insert(layer->id);
        if (layer->locked) return "Unlock the 3D layer and its folders before editing its camera.";
        layer = layer->parentId.isEmpty() ? nullptr : sceneLayer(shot, layer->parentId);
    }
    return {};
}
QString validateCameraAnimation(const Shot &shot) {
    if (!validShotCamera(shot.camera2D)) return "The shot camera position, zoom or rotation is invalid.";
    int count = 0;
    if (!validKeys(shot.camera2DKeys, shot.durationFrames, &count, validShotCamera)) return "Shot camera keys must be ordered, unique, finite and inside the shot.";
    QSet<QString> targets;
    if (shot.sceneCameraAnimations.size() > shot.layers.size()) return "Too many animated 3D cameras.";
    for (const auto &animation : shot.sceneCameraAnimations) {
        if (targets.contains(animation.layerId) || animation.keys.isEmpty()) return "Invalid or duplicate animated 3D camera.";
        targets.insert(animation.layerId);
        const auto error = sceneCameraEditError(shot, animation.layerId, false); if (!error.isEmpty()) return error;
        if (!validKeys(animation.keys, shot.durationFrames, &count, validSceneCameraPose)) return "3D camera keys must be ordered, unique, finite and inside the shot.";
    }
    return {};
}
void pruneCameraAnimation(Shot &shot) {
    shot.sceneCameraAnimations.removeIf([&](const auto &animation) { return !sceneLayer(shot, animation.layerId) || animation.keys.isEmpty(); });
}
qint64 cameraAnimationHistoryCost(const Shot &before, const Shot &after) {
    if (before.camera2D == after.camera2D && before.camera2DKeys == after.camera2DKeys && before.sceneCameraAnimations == after.sceneCameraAnimations) return 0;
    auto bytes = [](const Shot &shot) {
        qint64 size = sizeof(Camera2D) + shot.camera2DKeys.size() * sizeof(Camera2DKeyframe);
        for (const auto &animation : shot.sceneCameraAnimations) size += 128 + animation.keys.size() * sizeof(SceneCameraKeyframe);
        return size;
    };
    return bytes(before) + bytes(after);
}
QJsonObject shotCameraToJson(const Shot &shot) { return {{"base", poseJson(shot.camera2D)}, {"keys", keysJson(shot.camera2DKeys)}}; }
bool shotCameraFromJson(const QJsonObject &json, Shot *shot, QString *error) {
    Camera2D base; QVector<Camera2DKeyframe> keys; int count = 0;
    if (!shot || !fields(json, {"base", "keys"}) || !json["base"].isObject() || !json["keys"].isArray()
        || !readPose(json["base"].toObject(), &base) || !readKeys(json["keys"].toArray(), &keys, &count)) {
        if (error) *error = "Invalid shot camera metadata."; return false;
    }
    shot->camera2D = base; shot->camera2DKeys = std::move(keys); return true;
}
QJsonArray sceneCameraAnimationsToJson(const QVector<SceneCameraAnimation> &animations) {
    QJsonArray result;
    for (const auto &animation : animations) result.append(QJsonObject{{"layerId", animation.layerId}, {"keys", keysJson(animation.keys)}});
    return result;
}
bool sceneCameraAnimationsFromJson(const QJsonArray &json, QVector<SceneCameraAnimation> *animations, QString *error) {
    auto fail = [&] { if (error) *error = "Invalid 3D camera animation metadata."; return false; };
    if (!animations || json.size() > maxCameraKeyframes) return fail();
    QVector<SceneCameraAnimation> result; int count = 0; QSet<QString> ids;
    for (const auto &value : json) {
        const auto item = value.toObject();
        if (!value.isObject() || !fields(item, {"layerId", "keys"}) || !item["layerId"].isString() || !item["keys"].isArray()) return fail();
        SceneCameraAnimation animation; animation.layerId = item["layerId"].toString();
        if (animation.layerId.isEmpty() || animation.layerId.size() > 128 || ids.contains(animation.layerId)
            || !readKeys(item["keys"].toArray(), &animation.keys, &count) || animation.keys.isEmpty()) return fail();
        ids.insert(animation.layerId); result.append(std::move(animation));
    }
    *animations = std::move(result); return true;
}
} // namespace toon
