#include "timeline_controller.h"
#include "canvas.h"
#include "camera_tool.h"
#include "document.h"
#include "symbols.h"
#include <QScopedValueRollback>
#include <algorithm>
#include <limits>

namespace toon {
namespace {
int symbolIndex(const Shot &shot, const QString &id) {
    for (int i = 0; i < shot.layers.size(); ++i)
        if (shot.layers[i].id == id && shot.layers[i].kind == LayerKind::Symbol && shot.layers[i].symbol) return i;
    return -1;
}
}
QString TimelineController::selectedSymbolClipId() const {
    if (!document()->isOpen()) return {};
    const auto &shot = document()->activeShot();
    if (!m_selectedSymbolPoseLayer.isEmpty()) return {};
    if (!m_selectedSymbolClip.isEmpty())
        return symbolIndex(shot, m_selectedSymbolClip) >= 0 ? m_selectedSymbolClip : QString();
    if (m_symbolInferenceSuppressed || !m_selectedCels.isEmpty() || !m_selectedKeys.isEmpty()
        || !m_selectedAudio.isEmpty() || !m_selectedCue.isEmpty() || m_cameraSelected) return {};
    const auto id = selectedPropertyLayerId();
    return symbolIndex(shot, id) >= 0 ? id : QString();
}
void TimelineController::clearSymbolClipSelection() {
    const bool hadSelection = !selectedSymbolClipId().isEmpty();
    m_selectedSymbolClip.clear(); m_symbolInferenceSuppressed = true;
    if (hadSelection) emit changed();
}
bool TimelineController::selectSymbolClip(const QString &sourceId) {
    const QString id = sourceId;
    if (!document()->isOpen() || symbolIndex(document()->activeShot(), id) < 0) return false;
    const auto shotId = document()->activeShotId(); const auto generation = m_timelineGeneration;
    if (!prepareTransition() || !document()->isOpen() || document()->activeShotId() != shotId
        || generation != m_timelineGeneration) return false;
    // Resolving a draft can replace the project, so look up the target again.
    if (!document()->isOpen() || symbolIndex(document()->activeShot(), id) < 0) return false;
    const QScopedValueRollback<bool> guard(m_symbolSelectionChanging, true);
    m_selectedCue.clear(); m_selectedSymbolPoseLayer.clear();
    m_selectedKeys.clear(); m_selectedCels.clear(); m_selectedAudio.clear();
    m_cameraSelected = false; m_cameraLayerId.clear(); m_canvas->cameraTool()->setEnabled(false);
    m_selectedSymbolClip = id; m_symbolInferenceSuppressed = false;
    if (!layerPresentAtFrame(document()->activeShot(), id, currentFrame())) {
        const auto &shot = document()->activeShot();
        const auto owner = animationOwner(shot, id);
        int nearest = -1, distance = std::numeric_limits<int>::max();
        for (const auto &track : shot.animationTracks) if (track.id == owner.trackId)
            for (const auto &cel : track.exposures) if (cel.drawingId == owner.drawingId && cel.durationFrames > 0) {
                const int frame = std::clamp(currentFrame(), cel.startFrame, cel.startFrame + cel.durationFrames - 1);
                if (std::abs(frame - currentFrame()) < distance) {
                    nearest = frame; distance = std::abs(frame - currentFrame());
                }
            }
        if (nearest >= 0 && !document()->setCurrentFrame(nearest)) return false;
    }
    // Unexposed drawings still have editable timing; never activate their
    // hidden artwork merely to make the clip inspector available.
    if (layerPresentAtFrame(document()->activeShot(), id, currentFrame()))
        document()->selectLayer(symbolIndex(document()->activeShot(), id));
    emit changed(); emit symbolPropertiesRequested(); return true;
}
bool TimelineController::editSelectedSymbolClip(SymbolClipOperation operation, int value) {
    const auto id = selectedSymbolClipId();
    if (id.isEmpty() || !prepareTransition() || selectedSymbolClipId() != id) return false;
    QString error; const bool result = editSymbolClip(document(), id, operation, value, &error);
    if (!error.isEmpty()) emit document()->warning(error);
    return result;
}
bool TimelineController::setSelectedSymbolClipTiming(int start, int duration, int offset) {
    const auto id = selectedSymbolClipId();
    if (id.isEmpty() || !prepareTransition() || selectedSymbolClipId() != id) return false;
    QString error; const bool result = setSymbolClipTiming(document(), id, start, duration, offset, &error);
    if (!error.isEmpty()) emit document()->warning(error);
    return result;
}
bool TimelineController::setSelectedSymbolPlayback(const SymbolInstance &instance) {
    const auto id = selectedSymbolPoseLayerId().isEmpty() ? selectedSymbolClipId() : selectedSymbolPoseLayerId();
    if (id.isEmpty() || !document()->isOpen()) return false;
    const auto desired = instance;
    const bool wasPoseSelection = !m_selectedSymbolPoseLayer.isEmpty();
    const int originalIndex = symbolIndex(document()->activeShot(),id);
    if (originalIndex < 0) return false;
    const auto source = *document()->activeShot().layers[originalIndex].symbol;
    const auto shotId = document()->activeShot().id;
    if (!prepareTransition() || !document()->isOpen() || document()->activeShot().id != shotId
        || (selectedSymbolClipId() != id && selectedSymbolPoseLayerId() != id)) return false;
    const int resolved = symbolIndex(document()->activeShot(),id);
    if (resolved < 0 || *document()->activeShot().layers[resolved].symbol != source) return false;
    auto error = symbolClipEditError(document()->activeShot(), id);
    if (!error.isEmpty()) { emit document()->warning(error); return false; }
    const int index = symbolIndex(document()->activeShot(), id);
    if (index < 0 || document()->activeShot().layers[index].symbol->symbolId != desired.symbolId) return false;
    const bool result = setSymbolInstance(document(), id, desired, &error);
    if (!error.isEmpty()) emit document()->warning(error);
    if (result && desired.playback != SymbolPlayback::Poses && wasPoseSelection) selectSymbolClip(id);
    return result;
}
} // namespace toon
