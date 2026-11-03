#include "timeline_controller.h"
#include "canvas.h"
#include "camera_tool.h"
#include "document.h"
#include "keyframe_transform_tool.h"
#include "mask_tool.h"
#include <QScopedValueRollback>
#include <algorithm>

namespace toon {
QString TimelineController::selectedCueId() const {
    return !m_selectedCue.isEmpty() && document()->isOpen() && timelineCue(document()->activeShot(), m_selectedCue) ? m_selectedCue : QString();
}
void TimelineController::clearCueSelection() {
    if (m_selectedCue.isEmpty()) return;
    m_selectedCue.clear(); emit changed();
}
bool TimelineController::selectCue(QString id, bool seek) {
    if (property("symbolEditor").toBool() || !document()->isOpen()
        || !timelineCue(document()->activeShot(), id) || !prepareTransition()) return false;
    // A pending inspector can replace the project during resolution.
    if (!document()->isOpen()) return false;
    const auto *cue = timelineCue(document()->activeShot(), id);
    if (!cue) return false;
    const int frame = cue->startFrame;
    const QScopedValueRollback<bool> guard(m_symbolSelectionChanging, true);
    m_selectedSymbolPoseLayer.clear();
    m_selectedCue = std::move(id);
    m_selectedKeys.clear(); m_selectedCels.clear(); m_selectedAudio.clear();
    m_selectedSymbolClip.clear(); m_symbolInferenceSuppressed = true;
    m_cameraSelected = false; m_cameraLayerId.clear();
    m_canvas->cameraTool()->setEnabled(false);
    m_canvas->keyframeTransformTool()->setEnabled(false);
    m_canvas->maskTool()->setEnabled(false);
    if (seek && frame != currentFrame() && !document()->setCurrentFrame(frame)) return false;
    emit changed(); return true;
}
QString TimelineController::addCue(TimelineCueKind kind) {
    if (property("symbolEditor").toBool() || !document()->isOpen() || !prepareTransition()) return {};
    if (!document()->isOpen()) return {};
    TimelineCue cue; cue.kind = kind; cue.name = timelineCueKindName(kind); cue.startFrame = currentFrame();
    if (kind == TimelineCueKind::Dialogue)
        cue.durationFrames = std::clamp(qRound(document()->project().fps), 1, frameCount() - currentFrame());
    QString error; const auto id = addTimelineCue(document(), cue, &error);
    if (!error.isEmpty()) emit document()->warning(error);
    if (!id.isEmpty() && selectCue(id, false)) emit cuePropertiesRequested();
    return id;
}
bool TimelineController::updateSelectedCue(const TimelineCue &cue) {
    const auto id = selectedCueId();
    if (property("symbolEditor").toBool() || id.isEmpty() || cue.id != id) return false;
    // The caller may hold a reference into the document. Draft resolution can
    // replace that storage, or change this same cue, before the edit commits.
    const auto desired = cue;
    const auto source = *timelineCue(document()->activeShot(), id);
    const auto shotId = document()->activeShot().id;
    if (!prepareTransition() || selectedCueId() != id || document()->activeShot().id != shotId
        || *timelineCue(document()->activeShot(), id) != source) return false;
    QString error; const bool result = updateTimelineCue(document(), desired, &error);
    if (!error.isEmpty()) emit document()->warning(error);
    return result;
}
bool TimelineController::setSelectedCueTiming(int start, int duration) {
    const auto id = selectedCueId();
    if (property("symbolEditor").toBool() || id.isEmpty()
        || !prepareTransition() || selectedCueId() != id) return false;
    auto cue = *timelineCue(document()->activeShot(), id);
    cue.startFrame = start; cue.durationFrames = duration;
    QString error; const bool result = updateTimelineCue(document(), cue, &error);
    if (!error.isEmpty()) emit document()->warning(error);
    return result;
}
bool TimelineController::removeSelectedCue() {
    const auto id = selectedCueId();
    if (property("symbolEditor").toBool() || id.isEmpty()
        || !prepareTransition() || selectedCueId() != id) return false;
    QString error; const bool result = removeTimelineCue(document(), id, &error);
    if (!error.isEmpty()) emit document()->warning(error);
    return result;
}
bool TimelineController::stepCue(int direction) {
    if (!direction || property("symbolEditor").toBool() || !document()->isOpen() || !prepareTransition()) return false;
    if (!document()->isOpen()) return false;
    auto cues = document()->activeShot().timelineCues;
    std::stable_sort(cues.begin(), cues.end(), [](const auto &a, const auto &b) { return a.startFrame < b.startFrame; });
    const auto selected = std::find_if(cues.cbegin(), cues.cend(), [this](const auto &cue) { return cue.id == m_selectedCue; });
    if (selected != cues.cend()) {
        const int index = int(selected - cues.cbegin()) + (direction > 0 ? 1 : -1);
        return index >= 0 && index < cues.size() && selectCue(cues[index].id);
    }
    if (direction > 0) {
        for (const auto &cue : cues) if (cue.startFrame > currentFrame()) return selectCue(cue.id);
    } else {
        for (auto it = cues.crbegin(); it != cues.crend(); ++it) if (it->startFrame < currentFrame()) return selectCue(it->id);
    }
    return false;
}
} // namespace toon
