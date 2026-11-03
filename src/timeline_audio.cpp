#include "timeline_controller.h"
#include "audio_import.h"
#include "canvas.h"
#include "camera_tool.h"
#include "document.h"
#include <QApplication>
#include <QFileDialog>

namespace toon {
bool TimelineController::selectAudioClip(const QString &id) {
    if (!document()->isOpen() || !prepareTransition()) return false;
    for (const auto &clip : document()->activeShot().audioClips) if (clip.id == id) {
        m_selectedCue.clear(); m_selectedSymbolPoseLayer.clear();
        clearSymbolClipSelection();
        clearCelSelection();
        m_canvas->cameraTool()->setEnabled(false);
        m_selectedAudio = id; m_cameraSelected = false; clearKeySelection(); emit changed(); emit audioPropertiesRequested(); return true;
    }
    return false;
}
bool TimelineController::audioImportBusy() const { return m_audioImport->busy() || !m_importShot.isEmpty(); }
void TimelineController::cancelAudioImport() {
    const bool wasBusy = audioImportBusy(), decoderBusy = m_audioImport->busy();
    m_audioCompletionTimer.stop(); m_pendingAudio.reset(); m_importShot.clear(); m_audioImportStatus.clear();
    m_audioImport->cancel();
    if (wasBusy && !decoderBusy) emit changed();
}
void TimelineController::finishPendingAudioImport() {
    if (!m_pendingAudio) { m_audioCompletionTimer.stop(); return; }
    if (!document()->isOpen() || document()->activeShot().id != m_importShot) { cancelAudioImport(); return; }
    // Decoding can finish during a stroke or a modal dialog that owns a project
    // snapshot. Keep the result until idle instead of forcing an edit transition.
    if (m_resolving || m_pendingEdits || m_playing || document()->editingGestureActive()
        || document()->layerPropertyEditActive() || m_canvas->selectionGestureActive()
        || QApplication::activeModalWidget() || QApplication::activePopupWidget()
        || QApplication::mouseButtons() != Qt::NoButton) return;
    m_audioCompletionTimer.stop();
    const auto asset = m_pendingAudio;
    const auto id = document()->addAudioClip(asset, qBound(0, m_importFrame, frameCount() - 1), asset->name);
    // Document observers can cancel an import or reopen the project during a
    // commit. Never select an ID from a superseded completion.
    if (m_pendingAudio != asset) return;
    m_pendingAudio.reset(); m_importShot.clear(); m_audioImportStatus.clear();
    if (!id.isEmpty()) { clearSymbolClipSelection(); m_canvas->cameraTool()->setEnabled(false); m_selectedAudio = id; m_cameraSelected = false; clearKeySelection(); }
    emit changed();
    if (!id.isEmpty()) emit audioPropertiesRequested();
}
void TimelineController::importAudio() {
    if (!document()->isOpen() || audioImportBusy() || !prepareTransition()) return;
    const auto path = QFileDialog::getOpenFileName(m_canvas, "Import audio", {},
        "Audio (*.wav *.mp3 *.m4a *.aac *.flac *.ogg *.opus *.aif *.aiff);;All files (*)");
    if (!path.isEmpty()) importAudioFile(path);
}
bool TimelineController::importAudioFile(const QString &path) {
    if (!document()->isOpen() || audioImportBusy() || !prepareTransition()) return false;
    if (document()->activeShot().audioClips.size() >= maxAudioClipsPerShot) {
        emit document()->warning("This shot already has 64 audio clips. Remove one before importing another."); return false;
    }
    m_importShot = document()->activeShot().id; m_importFrame = currentFrame();
    return m_audioImport->start(path);
}
bool TimelineController::updateAudioClip(const AudioClip &clip) {
    return document()->isOpen() && prepareTransition() && document()->updateAudioClip(clip);
}
bool TimelineController::removeAudioClip() {
    const auto id = m_selectedAudio;
    return !id.isEmpty() && prepareTransition() && document()->removeAudioClip(id);
}
bool TimelineController::crossfadeAudioClips(const QString &outgoing, const QString &incoming, AudioFadeCurve curve) {
    return document()->isOpen() && prepareTransition() && document()->crossfadeAudioClips(outgoing, incoming, curve);
}
} // namespace toon
