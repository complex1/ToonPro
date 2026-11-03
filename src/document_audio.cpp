#include "document.h"
#include <QUuid>

namespace toon {
namespace {
QString ready(const Document *document) {
    if (!document->isOpen()) return "Open a project before editing audio.";
    if (document->editingGestureActive() || document->layerPropertyEditActive()) return "Finish the current edit before changing audio.";
    return {};
}
}
QString Document::addAudioClip(AudioAssetPtr asset, int startFrame, const QString &name) {
    const auto reason = ready(this); if (!reason.isEmpty()) { emit warning(reason); return {}; }
    if (!asset) { emit warning("Choose a valid audio source."); return {}; }
    auto project = m_project;
    AudioClip clip;
    clip.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    clip.name = name.trimmed().isEmpty() ? asset->name : name.trimmed();
    clip.asset = asset; clip.startFrame = startFrame; clip.sampleCount = asset->sampleFrameCount();
    // Share an existing immutable resource when the same samples are imported again.
    const auto resources = audioResources(m_project);
    if (resources.contains(asset->id)) clip.asset = resources.value(asset->id);
    project.shots[activeShotIndex()].audioClips.append(clip);
    const auto error = validateProjectAudio(project); if (!error.isEmpty()) { emit warning(error); return {}; }
    return commit(m_project, project, "Import audio clip") ? clip.id : QString();
}
bool Document::removeAudioClip(const QString &id) {
    const auto reason = ready(this); if (!reason.isEmpty()) { emit warning(reason); return false; }
    auto project = m_project;
    if (!project.shots[activeShotIndex()].audioClips.removeIf([&](const auto &clip) { return clip.id == id; })) return false;
    return commit(m_project, project, "Remove audio clip");
}
bool Document::updateAudioClip(const AudioClip &clip) {
    const auto reason = ready(this); if (!reason.isEmpty()) { emit warning(reason); return false; }
    auto project = m_project; auto replacement = clip; bool found = false;
    const auto resources = audioResources(m_project);
    if (replacement.asset && resources.contains(replacement.asset->id)) replacement.asset = resources.value(replacement.asset->id);
    for (auto &current : project.shots[activeShotIndex()].audioClips) if (current.id == clip.id) {
        if (current.asset == replacement.asset && (current.sourceOffsetSamples != replacement.sourceOffsetSamples || current.sampleCount != replacement.sampleCount)) {
            // Trimming removes samples, not the envelope's original source position.
            // An explicitly changed fade remains relative to the new visible edge.
            auto anchored = current; materializeAudioFadeAnchors(anchored);
            if (replacement.fadeInStartSample == current.fadeInStartSample && replacement.fadeInSamples == current.fadeInSamples
                && replacement.fadeInCurve == current.fadeInCurve) replacement.fadeInStartSample = anchored.fadeInStartSample;
            if (replacement.fadeOutEndSample == current.fadeOutEndSample && replacement.fadeOutSamples == current.fadeOutSamples
                && replacement.fadeOutCurve == current.fadeOutCurve) replacement.fadeOutEndSample = anchored.fadeOutEndSample;
        }
        if (current == replacement) return true;
        current = replacement; found = true; break;
    }
    if (!found) { emit warning("This audio clip no longer exists."); return false; }
    const auto error = validateProjectAudio(project); if (!error.isEmpty()) { emit warning(error); return false; }
    return commit(m_project, project, "Edit audio clip");
}
bool Document::crossfadeAudioClips(const QString &firstId, const QString &secondId, AudioFadeCurve curve) {
    const auto reason = ready(this); if (!reason.isEmpty()) { emit warning(reason); return false; }
    auto project = m_project;
    AudioClip *first = nullptr, *second = nullptr;
    for (auto &clip : project.shots[activeShotIndex()].audioClips) {
        if (clip.id == firstId) first = &clip;
        if (clip.id == secondId) second = &clip;
    }
    if (!first || !second || first == second) { emit warning("Choose two different audio clips in this shot."); return false; }
    const auto a = audioSampleFrameAtTimelineFrame(first->startFrame, project.fps);
    const auto b = audioSampleFrameAtTimelineFrame(second->startFrame, project.fps);
    const auto endA = a + first->sampleCount, endB = b + second->sampleCount;
    if (!(a < b && b < endA && endA <= endB)) {
        emit warning("Overlap the end of the first clip with the start of the second clip before crossfading."); return false;
    }
    first->fadeOutSamples = second->fadeInSamples = endA - b;
    first->fadeOutCurve = second->fadeInCurve = curve;
    first->fadeOutEndSample = first->sourceOffsetSamples + first->sampleCount;
    second->fadeInStartSample = second->sourceOffsetSamples;
    const auto validation = validateProjectAudio(project);
    if (!validation.isEmpty()) { emit warning(validation); return false; }
    return commit(m_project, project, "Crossfade audio clips");
}
} // namespace toon
