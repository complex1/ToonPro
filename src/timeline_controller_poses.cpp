#include "timeline_controller.h"
#include "canvas.h"
#include "camera_tool.h"
#include "document.h"
#include "keyframe_transform_tool.h"
#include "mask_tool.h"
#include <QScopedValueRollback>
#include <algorithm>
#include <optional>

namespace toon {
namespace {
int poseLayerIndex(const Shot &shot, const QString &id) {
    for (int i = 0; i < shot.layers.size(); ++i)
        if (shot.layers[i].id == id && shot.layers[i].kind == LayerKind::Symbol && shot.layers[i].symbol) return i;
    return -1;
}
TimelineKeyRef poseRef(const QString &id, int frame) { return {id,KeyframeProperty::PositionX,{},frame,TimelineKeyKind::SymbolPose}; }
struct PoseContext { QString layerId, shotId; SymbolInstance instance; };
std::optional<PoseContext> poseContext(const TimelineController *controller) {
    if (!controller->document()->isOpen()) return {};
    const auto id = controller->selectedSymbolPoseLayerId().isEmpty() ? controller->selectedSymbolClipId() : controller->selectedSymbolPoseLayerId();
    const auto &shot = controller->document()->activeShot(); const int index = poseLayerIndex(shot,id);
    if (index < 0) return {};
    return PoseContext{id,shot.id,*shot.layers[index].symbol};
}
bool samePoseContext(const TimelineController *controller, const PoseContext &context) {
    const auto current = poseContext(controller);
    return current && current->layerId == context.layerId && current->shotId == context.shotId && current->instance == context.instance;
}
}
QString TimelineController::selectedSymbolPoseLayerId() const {
    return document()->isOpen() && poseLayerIndex(document()->activeShot(),m_selectedSymbolPoseLayer) >= 0
        ? m_selectedSymbolPoseLayer : QString();
}
void TimelineController::clearSymbolPoseSelection() {
    if (m_selectedSymbolPoseLayer.isEmpty()) return;
    m_selectedSymbolPoseLayer.clear();
    m_selectedKeys.removeIf([](const auto &key) { return key.isSymbolPose(); });
    emit changed();
}
bool TimelineController::selectSymbolPoseLayer(const QString &layerId) {
    const QString id = layerId;
    if (!document()->isOpen() || poseLayerIndex(document()->activeShot(),id) < 0 || !prepareTransition()) return false;
    if (!document()->isOpen()) return false;
    const int index = poseLayerIndex(document()->activeShot(),id);
    if (index < 0) return false;
    const QScopedValueRollback<bool> guard(m_symbolSelectionChanging,true);
    if (m_selectedSymbolPoseLayer != id) m_selectedKeys.clear();
    m_selectedSymbolPoseLayer = id; m_selectedSymbolClip.clear(); m_symbolInferenceSuppressed = true;
    m_selectedCue.clear(); m_selectedCels.clear(); m_selectedAudio.clear();
    m_cameraSelected = false; m_cameraLayerId.clear();
    m_canvas->cameraTool()->setEnabled(false); m_canvas->keyframeTransformTool()->setEnabled(false); m_canvas->maskTool()->setEnabled(false);
    // A pose lane remains editable when its drawing is unexposed. Selecting it
    // must not expose hidden artwork or move the parent playhead.
    if (layerPresentAtFrame(document()->activeShot(),id,currentFrame())) document()->selectLayer(index);
    emit changed(); emit symbolPropertiesRequested(); return true;
}
bool TimelineController::setSelectedSymbolPose(int parentFrame, int poseFrame) {
    if (parentFrame < 0 || parentFrame >= frameCount()) return false;
    const auto context = poseContext(this);
    if (!context || !prepareTransition() || !samePoseContext(this,*context)) return false;
    const auto &instance = context->instance;
    const bool inside = parentFrame >= instance.startFrame
        && (instance.durationFrames < 0 || parentFrame < qint64(instance.startFrame) + instance.durationFrames);
    if (!inside && !symbolPoseKeyAtParent(instance,parentFrame)) {
        emit document()->warning("Move the playhead inside this Symbol clip to add a pose."); return false;
    }
    QString error;
    if (!setSymbolPose(document(),context->layerId,parentFrame,poseFrame,&error)) { if (!error.isEmpty()) emit document()->warning(error); return false; }
    if (!selectSymbolPoseLayer(context->layerId)) return false;
    setSelectedKeys({poseRef(context->layerId,parentFrame)}); return true;
}
bool TimelineController::removeSelectedSymbolPose(int parentFrame) {
    if (parentFrame < 0 || parentFrame >= frameCount()) return false;
    const auto context = poseContext(this);
    if (!context || !prepareTransition() || !samePoseContext(this,*context)) return false;
    QString error; const bool result = removeSymbolPose(document(),context->layerId,parentFrame,&error);
    if (!error.isEmpty()) emit document()->warning(error);
    return result;
}
bool TimelineController::moveSelectedSymbolPose(int fromParent, int toParent) {
    if (fromParent < 0 || toParent < 0 || fromParent >= frameCount() || toParent >= frameCount()) return false;
    const auto context = poseContext(this);
    if (!context || !prepareTransition() || !samePoseContext(this,*context)) return false;
    if (!document()->moveTimelineKeys({poseRef(context->layerId,fromParent)},toParent-fromParent)) return false;
    if (!selectSymbolPoseLayer(context->layerId)) return false;
    setSelectedKeys({poseRef(context->layerId,toParent)}); return true;
}
bool TimelineController::stepSymbolPose(int direction) {
    const auto id = selectedSymbolPoseLayerId().isEmpty() ? selectedSymbolClipId() : selectedSymbolPoseLayerId();
    if (!direction || id.isEmpty() || !prepareTransition() || !document()->isOpen()) return false;
    if (id != selectedSymbolPoseLayerId() && id != selectedSymbolClipId()) return false;
    const int index = poseLayerIndex(document()->activeShot(),id);
    if (index < 0) return false;
    const auto instance = *document()->activeShot().layers[index].symbol;
    int target = -1;
    for (const auto &key : instance.poseKeys) {
        const int frame = symbolPoseParentFrame(instance,key.frame);
        if (frame < 0 || frame >= frameCount()) continue;
        if (direction > 0 && frame > currentFrame()) { target = frame; break; }
        if (direction < 0 && frame < currentFrame()) target = frame;
    }
    if (target < 0 || !selectSymbolPoseLayer(id) || !setFrame(target)) return false;
    setSelectedKeys({poseRef(id,target)}); return true;
}
} // namespace toon
