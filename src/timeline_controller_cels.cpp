#include "timeline_controller.h"
#include "canvas.h"
#include "camera_tool.h"
#include "document.h"
#include <algorithm>

namespace toon {
namespace {
QString celIdentity(const TimelineCelRef &ref) { return ref.trackId + '\n' + ref.exposureId; }
}

void TimelineController::pruneCelSelection() {
    if (!document()->isOpen()) { m_selectedCels.clear(); return; }
    const auto &shot = document()->activeShot();
    m_selectedCels.removeIf([&](const auto &ref) { return !celExposure(shot, ref); });
}
void TimelineController::setSelectedCels(const QVector<TimelineCelRef> &cels) {
    if (cels == m_selectedCels) return;
    // Selection can update continuously during a marquee. Do not broadcast a
    // finish-edit request back into that gesture unless an editor needs it.
    // The grid resolves ordinary canvas transitions before starting a gesture.
    if (!m_resolving && (m_pendingEdits || document()->editingGestureActive()
        || document()->layerPropertyEditActive()) && !prepareTransition()) return;
    if (m_playing) setPlaying(false);
    QVector<TimelineCelRef> selected;
    if (document()->isOpen()) {
        const auto &shot = document()->activeShot();
        for (const auto &ref : cels)
            if (celExposure(shot, ref) && !selected.contains(ref)) selected.append(ref);
    }
    if (selected == m_selectedCels) return;
    m_selectedCels = std::move(selected);
    if (!m_selectedCels.isEmpty()) {
        m_selectedCue.clear(); m_selectedSymbolPoseLayer.clear();
        clearSymbolClipSelection();
        m_selectedKeys.clear(); m_selectedAudio.clear();
        m_cameraSelected = false; m_cameraLayerId.clear();
        m_canvas->cameraTool()->setEnabled(false);
    }
    emit changed();
}
void TimelineController::clearCelSelection() {
    if (m_selectedCels.isEmpty()) return;
    m_selectedCels.clear(); emit changed();
}
bool TimelineController::moveSelectedCels(int deltaFrames) {
    if (!document()->isOpen() || m_selectedCels.isEmpty() || !prepareTransition()) return false;
    const auto refs = m_selectedCels;
    const int frame = currentFrame(); bool follows = false;
    for (const auto &ref : refs) if (const auto *cel = celExposure(document()->activeShot(), ref))
        follows |= frame >= cel->startFrame && qint64(frame) < qint64(cel->startFrame) + cel->durationFrames;
    if (!document()->moveAnimationExposures(refs, deltaFrames)) return false;
    if (follows) document()->setCurrentFrame(frame + deltaFrames);
    setSelectedCels(refs); return true;
}
bool TimelineController::deleteSelectedCels() {
    if (!document()->isOpen() || m_selectedCels.isEmpty() || !prepareTransition()) return false;
    const auto refs = m_selectedCels;
    if (!document()->removeAnimationExposures(refs)) return false;
    clearCelSelection(); return true;
}
bool TimelineController::copySelectedCels() {
    if (!document()->isOpen() || m_selectedCels.isEmpty() || !prepareTransition()) return false;
    QString error;
    auto clipboard = copyCelExposures(document()->activeShot(), m_selectedCels, &error);
    if (!error.isEmpty()) { emit document()->warning(error); return false; }
    if (clipboard.cels.isEmpty()) return false;
    m_celClipboard = std::move(clipboard); emit changed(); return true;
}
bool TimelineController::canPasteCels() const {
    if (!document()->isOpen() || m_celClipboard.cels.isEmpty()
        || m_celClipboard.shotId != document()->activeShot().id) return false;
    const auto &shot = document()->activeShot();
    for (const auto &value : m_celClipboard.cels) {
        const auto *track = celTrack(shot, value.trackId);
        if (!track || std::none_of(track->drawings.cbegin(), track->drawings.cend(),
            [&](const auto &drawing) { return drawing.id == value.drawingId; })) return false;
    }
    return true;
}
bool TimelineController::pasteCels() {
    if (!prepareTransition() || !canPasteCels()) return false;
    QVector<TimelineCelRef> inserted;
    if (!document()->pasteAnimationExposures(m_celClipboard, currentFrame(), &inserted)) return false;
    if (!inserted.isEmpty()) document()->selectAnimationTrack(inserted.first().trackId);
    setSelectedCels(inserted); return true;
}
bool TimelineController::setSelectedCelHolds(int durationFrames) {
    if (!document()->isOpen() || m_selectedCels.isEmpty() || !prepareTransition()) return false;
    const auto refs = m_selectedCels;
    return document()->setAnimationExposureHolds(refs, durationFrames);
}
bool TimelineController::reverseSelectedCels() {
    if (!document()->isOpen() || m_selectedCels.isEmpty() || !prepareTransition()) return false;
    const auto refs = m_selectedCels;
    return document()->reverseAnimationExposures(refs);
}
bool TimelineController::repeatSelectedCels() {
    if (!document()->isOpen() || m_selectedCels.isEmpty() || !prepareTransition()) return false;
    QSet<QString> original;
    for (const auto &track : document()->activeShot().animationTracks)
        for (const auto &cel : track.exposures) original.insert(celIdentity({track.id, cel.id}));
    const auto refs = m_selectedCels;
    if (!document()->repeatAnimationExposures(refs)) return false;
    QVector<TimelineCelRef> inserted; int first = frameCount();
    for (const auto &track : document()->activeShot().animationTracks)
        for (const auto &cel : track.exposures) if (!original.contains(celIdentity({track.id, cel.id}))) {
            inserted.append({track.id, cel.id}); first = std::min(first, cel.startFrame);
        }
    if (first < frameCount()) document()->setCurrentFrame(first);
    setSelectedCels(inserted); return true;
}
bool TimelineController::exposeExistingDrawing(const QString &drawingId, int durationFrames) {
    if (!document()->isOpen() || !prepareTransition()) return false;
    TimelineCelRef inserted;
    if (!document()->exposeAnimationDrawing(document()->activeAnimationTrackId(), drawingId,
        currentFrame(), durationFrames, &inserted)) return false;
    document()->selectAnimationTrack(inserted.trackId);
    setSelectedCels({inserted}); return true;
}
bool TimelineController::removeUnusedDrawing(const QString &drawingId) {
    if (!document()->isOpen() || !prepareTransition()) return false;
    return document()->deleteUnusedAnimationDrawing(document()->activeAnimationTrackId(), drawingId);
}
} // namespace toon
