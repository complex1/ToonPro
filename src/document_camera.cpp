#include "document.h"
#include <algorithm>

namespace toon {
namespace {
RasterLayer *sceneLayer(Shot &shot, const QString &id) {
    for (auto &layer : shot.layers) if (layer.id == id && layer.kind == LayerKind::Scene3D && layer.scene3d) return &layer;
    return nullptr;
}
QString ready(const Document *document, const QString &id = {}, bool gesture = false, bool unlockOnly = false) {
    if (!document->isOpen()) return "Open a project before editing a camera.";
    if ((document->editingGestureActive() && !(gesture && document->cameraEditActive())) || document->layerPropertyEditActive())
        return "Finish the current edit before changing a camera.";
    if (!id.isEmpty()) {
        auto shot = document->activeShot();
        // Unlocking a camera must remain possible without bypassing layer/folder locks.
        if (unlockOnly) if (auto *layer = sceneLayer(shot, id)) layer->scene3d->camera.locked = false;
        return sceneCameraEditError(shot, id);
    }
    return {};
}
template<class Key, class Pose> void put(QVector<Key> &keys, int frame, const Pose &pose) {
    const auto at = std::lower_bound(keys.begin(), keys.end(), frame, [](const Key &key, int value) { return key.frame < value; });
    if (at != keys.end() && at->frame == frame) at->pose = pose;
    else keys.insert(at, {frame, pose});
}
bool sameCameras(const Shot &a, const Shot &b) {
    if (a.camera2D != b.camera2D || a.camera2DKeys != b.camera2DKeys || a.sceneCameraAnimations != b.sceneCameraAnimations) return false;
    for (const auto &layer : a.layers) if (layer.scene3d) {
        const auto at = std::find_if(b.layers.cbegin(), b.layers.cend(), [&](const auto &other) { return other.id == layer.id; });
        if (at == b.layers.cend() || !at->scene3d || at->scene3d->camera != layer.scene3d->camera) return false;
    }
    return true;
}
bool changeShot(Shot &shot, int frame, const Camera2D &camera) {
    if (!validShotCamera(camera)) return false;
    if (evaluateShotCamera(shot, frame) == camera) return true;
    if (shot.camera2DKeys.isEmpty()) shot.camera2D = camera;
    else put(shot.camera2DKeys, frame, camera);
    return true;
}
bool changeScene(Shot &shot, const QString &id, int frame, const Camera3D &camera) {
    auto *layer = sceneLayer(shot, id); if (!layer || !validSceneCameraPose(sceneCameraPose(camera))) return false;
    const auto evaluated = evaluateSceneCamera(shot, id, frame);
    if (evaluated.locked) {
        auto unlocked = evaluated; unlocked.locked = camera.locked;
        if (unlocked != camera) return false;
    }
    if (auto *animation = sceneCameraAnimation(shot, id)) {
        if (sceneCameraPose(evaluated) != sceneCameraPose(camera)) put(animation->keys, frame, sceneCameraPose(camera));
        layer->scene3d->camera.locked = camera.locked;
        layer->scene3d->camera.orthographic = camera.orthographic;
    } else layer->scene3d->camera = camera;
    return true;
}
template<class Key> bool erase(QVector<Key> &keys, int frame) {
    return keys.removeIf([&](const auto &key) { return key.frame == frame; }) > 0;
}
template<class Key> bool move(QVector<Key> &keys, int from, int to, int duration) {
    if (from < 0 || to < 0 || to >= duration) return false;
    auto at = std::find_if(keys.begin(), keys.end(), [&](const auto &key) { return key.frame == from; });
    if (at == keys.end()) return false;
    if (from == to) return true;
    if (std::any_of(keys.cbegin(), keys.cend(), [&](const auto &key) { return key.frame == to; })) return false;
    at->frame = to; std::sort(keys.begin(), keys.end(), [](const auto &a, const auto &b) { return a.frame < b.frame; }); return true;
}
template<class Key> bool easing(QVector<Key> &keys, int frame, KeyframeInterpolation interpolation) {
    if (keyframeInterpolationKey(interpolation).isEmpty()) return false;
    for (auto &key : keys) if (key.frame == frame) { key.interpolation = interpolation; return true; }
    return false;
}
}
bool Document::setShotCamera(const Camera2D &camera) {
    const auto error = ready(this); if (!error.isEmpty()) { emit warning(error); return false; }
    auto project = m_project;
    if (!changeShot(project.shots[activeShotIndex()], m_currentFrame, camera)) { emit warning("The shot camera value is outside its supported range."); return false; }
    if (sameCameras(project.shots[activeShotIndex()], m_project.shots[activeShotIndex()])) return true;
    return commit(m_project, project, "Adjust shot camera");
}
bool Document::enableShotCameraAnimation(bool enabled) {
    const auto error = ready(this); if (!error.isEmpty()) { emit warning(error); return false; }
    auto project = m_project; auto &shot = project.shots[activeShotIndex()];
    if (enabled == !shot.camera2DKeys.isEmpty()) return true;
    if (enabled) { put(shot.camera2DKeys, 0, shot.camera2D); put(shot.camera2DKeys, m_currentFrame, shot.camera2D); }
    else { shot.camera2D = evaluateShotCamera(shot, m_currentFrame); shot.camera2DKeys.clear(); }
    return commit(m_project, project, enabled ? "Animate shot camera" : "Disable shot camera animation");
}
bool Document::addShotCameraKeyframe() {
    const auto error = ready(this); if (!error.isEmpty()) { emit warning(error); return false; }
    auto project = m_project; auto &shot = project.shots[activeShotIndex()]; const auto pose = evaluateShotCamera(shot, m_currentFrame);
    if (shot.camera2DKeys.isEmpty()) put(shot.camera2DKeys, 0, shot.camera2D);
    put(shot.camera2DKeys, m_currentFrame, pose);
    if (sameCameras(shot, m_project.shots[activeShotIndex()])) return true;
    return commit(m_project, project, "Add shot camera keyframe");
}
bool Document::removeShotCameraKeyframe(int frame) {
    const auto error = ready(this); if (!error.isEmpty()) { emit warning(error); return false; }
    auto project = m_project; auto &shot = project.shots[activeShotIndex()]; const auto pose = evaluateShotCamera(shot, m_currentFrame);
    if (!erase(shot.camera2DKeys, frame)) return false;
    if (shot.camera2DKeys.isEmpty()) shot.camera2D = pose;
    return commit(m_project, project, "Remove shot camera keyframe");
}
bool Document::moveShotCameraKeyframe(int from, int to) {
    const auto error = ready(this); if (!error.isEmpty()) { emit warning(error); return false; }
    auto project = m_project; auto &shot = project.shots[activeShotIndex()];
    if (!move(shot.camera2DKeys, from, to, shot.durationFrames)) { emit warning("Move camera keys to empty frames inside the shot."); return false; }
    if (from == to) return true;
    return commit(m_project, project, "Move shot camera keyframe");
}
bool Document::setShotCameraKeyframeInterpolation(int frame, KeyframeInterpolation value) {
    const auto error = ready(this); if (!error.isEmpty()) { emit warning(error); return false; }
    auto project = m_project;
    if (!easing(project.shots[activeShotIndex()].camera2DKeys, frame, value)) return false;
    if (sameCameras(project.shots[activeShotIndex()], m_project.shots[activeShotIndex()])) return true;
    return commit(m_project, project, "Change camera easing");
}
bool Document::setSceneCamera(const QString &id, const Camera3D &camera) {
    bool unlockOnly = false;
    if (m_open) { auto original = evaluateSceneCamera(m_project.shots[activeShotIndex()], id, m_currentFrame); original.locked = camera.locked; unlockOnly = original == camera; }
    const auto error = ready(this, id, false, unlockOnly); if (!error.isEmpty()) { emit warning(error); return false; }
    if (id.isEmpty()) return false;
    auto project = m_project;
    if (!changeScene(project.shots[activeShotIndex()], id, m_currentFrame, camera)) { emit warning("The 3D camera is locked or its values are outside the supported range."); return false; }
    if (sameCameras(project.shots[activeShotIndex()], m_project.shots[activeShotIndex()])) return true;
    return commit(m_project, project, "Adjust 3D camera");
}
bool Document::enableSceneCameraAnimation(const QString &id, bool enabled) {
    const auto error = ready(this, id); if (!error.isEmpty()) { emit warning(error); return false; }
    if (id.isEmpty()) return false;
    auto project = m_project; auto &shot = project.shots[activeShotIndex()]; auto *layer = sceneLayer(shot, id); if (!layer) return false;
    auto *animation = sceneCameraAnimation(shot, id);
    if (enabled == bool(animation)) return true;
    if (enabled) {
        SceneCameraAnimation added{id, {}}; put(added.keys, 0, sceneCameraPose(layer->scene3d->camera));
        put(added.keys, m_currentFrame, sceneCameraPose(layer->scene3d->camera)); shot.sceneCameraAnimations.append(added);
    } else {
        layer->scene3d->camera = evaluateSceneCamera(shot, id, m_currentFrame);
        shot.sceneCameraAnimations.removeIf([&](const auto &item) { return item.layerId == id; });
    }
    return commit(m_project, project, enabled ? "Animate 3D camera" : "Disable 3D camera animation");
}
bool Document::addSceneCameraKeyframe(const QString &id) {
    const auto error = ready(this, id); if (!error.isEmpty()) { emit warning(error); return false; }
    if (id.isEmpty()) return false;
    auto project = m_project; auto &shot = project.shots[activeShotIndex()]; const auto pose = sceneCameraPose(evaluateSceneCamera(shot, id, m_currentFrame));
    auto *animation = sceneCameraAnimation(shot, id);
    if (!animation) { shot.sceneCameraAnimations.append({id, {{0, pose}}}); animation = &shot.sceneCameraAnimations.last(); }
    put(animation->keys, m_currentFrame, pose);
    if (sameCameras(shot, m_project.shots[activeShotIndex()])) return true;
    return commit(m_project, project, "Add 3D camera keyframe");
}
bool Document::removeSceneCameraKeyframe(const QString &id, int frame) {
    const auto error = ready(this, id); if (!error.isEmpty()) { emit warning(error); return false; }
    auto project = m_project; auto &shot = project.shots[activeShotIndex()]; auto *animation = sceneCameraAnimation(shot, id);
    const auto pose = evaluateSceneCamera(shot, id, m_currentFrame);
    if (!animation || !erase(animation->keys, frame)) return false;
    if (animation->keys.isEmpty()) { sceneLayer(shot, id)->scene3d->camera = pose; pruneCameraAnimation(shot); }
    return commit(m_project, project, "Remove 3D camera keyframe");
}
bool Document::moveSceneCameraKeyframe(const QString &id, int from, int to) {
    const auto error = ready(this, id); if (!error.isEmpty()) { emit warning(error); return false; }
    auto project = m_project; auto &shot = project.shots[activeShotIndex()]; auto *animation = sceneCameraAnimation(shot, id);
    if (!animation || !move(animation->keys, from, to, shot.durationFrames)) { emit warning("Move camera keys to empty frames inside the shot."); return false; }
    if (from == to) return true;
    return commit(m_project, project, "Move 3D camera keyframe");
}
bool Document::setSceneCameraKeyframeInterpolation(const QString &id, int frame, KeyframeInterpolation value) {
    const auto error = ready(this, id); if (!error.isEmpty()) { emit warning(error); return false; }
    auto project = m_project; auto *animation = sceneCameraAnimation(project.shots[activeShotIndex()], id);
    if (!animation || !easing(animation->keys, frame, value)) return false;
    if (sameCameras(project.shots[activeShotIndex()], m_project.shots[activeShotIndex()])) return true;
    return commit(m_project, project, "Change camera easing");
}
bool Document::beginCameraEdit(const QString &id, const QString &label) {
    const auto error = ready(this, id); if (!error.isEmpty()) { emit warning(error); return false; }
    m_beforeCameraEdit = m_project; m_cameraEditLayerId = id; m_cameraEditLabel = label; emit changed(); return true;
}
bool Document::previewShotCamera(const Camera2D &camera) {
    if (!m_beforeCameraEdit || !m_cameraEditLayerId.isEmpty()) return false;
    auto project = *m_beforeCameraEdit;
    if (!changeShot(project.shots[activeShotIndex()], m_currentFrame, camera)) return false;
    const auto error = validateCameraAnimation(project.shots[activeShotIndex()]); if (!error.isEmpty()) { emit warning(error); return false; }
    if (sameCameras(project.shots[activeShotIndex()], m_project.shots[activeShotIndex()])) return true;
    m_project = std::move(project); emit changed(); return true;
}
bool Document::previewSceneCamera(const Camera3D &camera) {
    if (!m_beforeCameraEdit || m_cameraEditLayerId.isEmpty()) return false;
    auto project = *m_beforeCameraEdit;
    if (!changeScene(project.shots[activeShotIndex()], m_cameraEditLayerId, m_currentFrame, camera)) return false;
    const auto error = validateCameraAnimation(project.shots[activeShotIndex()]); if (!error.isEmpty()) { emit warning(error); return false; }
    if (sameCameras(project.shots[activeShotIndex()], m_project.shots[activeShotIndex()])) return true;
    m_project = std::move(project); emit changed(); return true;
}
void Document::commitCameraEdit() {
    if (!m_beforeCameraEdit) return;
    const auto before = *m_beforeCameraEdit; const auto label = m_cameraEditLabel;
    m_beforeCameraEdit.reset(); m_cameraEditLayerId.clear(); m_cameraEditLabel.clear();
    if (!sameCameras(before.shots[activeShotIndex()], m_project.shots[activeShotIndex()])) commit(before, m_project, label);
    else emit changed();
}
void Document::cancelCameraEdit() {
    if (!m_beforeCameraEdit) return;
    const auto before = *m_beforeCameraEdit;
    m_beforeCameraEdit.reset(); m_cameraEditLayerId.clear(); m_cameraEditLabel.clear(); apply(before);
}
} // namespace toon
