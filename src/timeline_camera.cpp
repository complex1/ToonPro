#include "timeline_controller.h"
#include "feature_flags.h"
#include "canvas.h"
#include "camera_tool.h"
#include "mask_tool.h"
#include "keyframe_transform_tool.h"
#include "scene3d_controller.h"

namespace toon {
bool TimelineController::selectCamera(const QString &layerId) {
    if (!features::legacy3DEditor && !layerId.isEmpty()) return false;
    if(property("symbolEditor").toBool())return false;
    if (!document()->isOpen() || !prepareTransition()) return false;
    if (!layerId.isEmpty()) {
        bool found = false;
        for (const auto &layer : document()->activeShot().layers)
            if (layer.id == layerId && layer.scene3d) { found = true; break; }
        if (!found) return false;
    }
    m_selectedCue.clear(); m_selectedSymbolPoseLayer.clear();
    clearCelSelection();
    clearSymbolClipSelection();
    const bool switched = !m_cameraSelected || m_cameraLayerId != layerId;
    if (switched) { m_selectedKeys.clear(); m_canvas->cameraTool()->setEnabled(false); }
    m_canvas->maskTool()->setEnabled(false);
    m_selectedAudio.clear(); m_cameraSelected = true; m_cameraLayerId = layerId;
    m_canvas->keyframeTransformTool()->setEnabled(false);
    emit changed(); emit cameraPropertiesRequested(); return true;
}
bool TimelineController::setCameraEnabled(bool enabled) {
    if (!m_cameraSelected || !prepareTransition()) return false;
    return m_cameraLayerId.isEmpty() ? document()->enableShotCameraAnimation(enabled)
        : document()->enableSceneCameraAnimation(m_cameraLayerId, enabled);
}
bool TimelineController::beginCameraEdit() {
    if (!m_cameraSelected || !prepareTransition()) return false;
    return document()->beginCameraEdit(m_cameraLayerId);
}
bool TimelineController::previewShotCamera(const Camera2D &camera) { return document()->previewShotCamera(camera); }
bool TimelineController::previewSceneCamera(const Camera3D &camera) { return features::legacy3DEditor && document()->previewSceneCamera(camera); }
void TimelineController::finishCameraEdit() { document()->commitCameraEdit(); }
void TimelineController::cancelCameraEdit() { document()->cancelCameraEdit(); }
bool TimelineController::setCameraKey() {
    if (!m_cameraSelected || !prepareTransition()) return false;
    return m_cameraLayerId.isEmpty() ? document()->addShotCameraKeyframe() : document()->addSceneCameraKeyframe(m_cameraLayerId);
}
bool TimelineController::removeCameraKey(int frame) {
    if (!m_cameraSelected || !prepareTransition()) return false;
    return m_cameraLayerId.isEmpty() ? document()->removeShotCameraKeyframe(frame) : document()->removeSceneCameraKeyframe(m_cameraLayerId, frame);
}
bool TimelineController::moveCameraKey(int from, int to) {
    if (!m_cameraSelected || !prepareTransition()) return false;
    return m_cameraLayerId.isEmpty() ? document()->moveShotCameraKeyframe(from, to) : document()->moveSceneCameraKeyframe(m_cameraLayerId, from, to);
}
bool TimelineController::setCameraInterpolation(int frame, KeyframeInterpolation interpolation) {
    if (!m_cameraSelected || !prepareTransition()) return false;
    return m_cameraLayerId.isEmpty() ? document()->setShotCameraKeyframeInterpolation(frame, interpolation)
        : document()->setSceneCameraKeyframeInterpolation(m_cameraLayerId, frame, interpolation);
}
bool TimelineController::cameraView() const { return m_canvas->cameraView(); }
void TimelineController::setCameraView(bool enabled) {
    if (!prepareTransition()) return;
    m_canvas->setCameraView(enabled); emit changed();
}
bool TimelineController::cameraFramingEditing() const {
    return m_cameraLayerId.isEmpty() ? m_cameraSelected && m_canvas->cameraTool()->enabled()
        : m_cameraSelected && m_canvas->tool() == Canvas::Tool::SceneCamera
            && selectedPropertyLayerId() == m_cameraLayerId;
}
void TimelineController::editCameraFraming() {
    if (!m_cameraSelected) { if (!selectCamera()) return; }
    const bool enabled = !cameraFramingEditing();
    if (!prepareTransition()) return;
    m_canvas->keyframeTransformTool()->setEnabled(false);
    if (m_cameraLayerId.isEmpty()) m_canvas->cameraTool()->setEnabled(enabled);
    else if (enabled) {
        const auto &layers = document()->activeShot().layers;
        for (int i = 0; i < layers.size(); ++i) if (layers[i].id == m_cameraLayerId) { document()->selectLayer(i); break; }
        m_canvas->setTool(Canvas::Tool::SceneCamera);
    } else m_canvas->setTool(Canvas::Tool::Hand);
    emit changed();
}
} // namespace toon
