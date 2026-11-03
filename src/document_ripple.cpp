#include "document.h"
#include "timeline_ripple.h"
#include <QScopedValueRollback>

namespace toon {
bool Document::rippleTimeline(const TimelineRippleEdit &edit, bool *changed) {
    if (changed) *changed = false;
    if (!isOpen()) { emit warning("Open a project before editing the timeline."); return false; }
    if (editingGestureActive() || layerPropertyEditActive()) {
        emit warning("Finish the current edit before ripple editing."); return false;
    }
    auto project = m_project;
    const auto error = applyTimelineRipple(project.shots[activeShotIndex()], project.fps, edit);
    if (!error.isEmpty()) { emit warning(error); return false; }
    if (project.toJson() == m_project.toJson()) return true;
    const auto validation = project.validate();
    if (!validation.isEmpty()) { emit warning(validation); return false; }
    const int frame = timelineRippleFrame(m_currentFrame, m_project.shots[activeShotIndex()].durationFrames, edit);
    QScopedValueRollback<bool> editing(m_animationMutation, true);
    if (!commit(m_project, project, timelineRippleLabel(edit.kind))) return false;
    if (changed) *changed = true;
    setCurrentFrame(frame);
    return true;
}
} // namespace toon
