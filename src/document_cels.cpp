#include "document.h"
#include <QScopedValueRollback>
#include <QUuid>
#include <algorithm>
#include <limits>

namespace toon {
namespace {
QString ready(const Document *document) {
    if (!document->isOpen()) return "Open a project before editing cel timing.";
    if (document->editingGestureActive() || document->layerPropertyEditActive())
        return "Finish the current edit before changing cel timing.";
    return {};
}
QString collect(const Shot &shot, const QVector<TimelineCelRef> &refs, QSet<QString> &selected) {
    if (refs.isEmpty()) return "Select one or more drawing exposures first.";
    if (refs.size() > maxAnimationExposures) return "Too many selected drawing exposures.";
    for (const auto &ref : refs) {
        const auto error = celEditError(shot, ref);
        if (!error.isEmpty()) return error;
        selected.insert(ref.exposureId);
    }
    return {};
}
QString sortAndValidate(Shot &shot) {
    for (auto &track : shot.animationTracks) {
        std::sort(track.exposures.begin(), track.exposures.end(), [](const auto &a, const auto &b) {
            return a.startFrame < b.startFrame;
        });
        qint64 end = 0;
        for (const auto &exposure : track.exposures) {
            if (exposure.startFrame < 0 || exposure.durationFrames < 1
                || qint64(exposure.startFrame) + exposure.durationFrames > shot.durationFrames)
                return "The selected cels would extend outside the shot. Extend the shot or choose an earlier frame.";
            if (exposure.startFrame < end)
                return "Drawing exposures cannot overlap. Move or trim the selected cels into empty frames.";
            end = qint64(exposure.startFrame) + exposure.durationFrames;
        }
    }
    return validateAnimation(shot);
}
bool validInterval(qint64 start, qint64 hold, int frames) {
    return start >= 0 && hold >= 1 && start + hold <= frames;
}
int exposureCount(const Shot &shot) {
    int count = 0;
    for (const auto &track : shot.animationTracks) count += track.exposures.size();
    return count;
}
AnimationTrack *mutableTrack(Shot &shot, const QString &id) {
    for (auto &track : shot.animationTracks) if (track.id == id) return &track;
    return nullptr;
}
QString appendClipboard(Shot &shot, const TimelineCelClipboard &clipboard, int frame, int repeats,
                        QVector<TimelineCelRef> &inserted) {
    if (clipboard.shotId != shot.id) return "Copied cels can only be reused in their original shot and tracks.";
    if (clipboard.cels.isEmpty()) return "Copy one or more drawing exposures first.";
    if (repeats < 1 || qint64(clipboard.cels.size()) * repeats + exposureCount(shot) > maxAnimationExposures)
        return "The shot supports up to 16384 drawing exposures. Reduce the repeat count.";
    qint64 first = std::numeric_limits<int>::max(), end = 0;
    for (const auto &cel : clipboard.cels) {
        if (cel.startFrame < 0 || cel.durationFrames < 1
            || qint64(cel.startFrame) + cel.durationFrames > std::numeric_limits<int>::max())
            return "The copied cel timing is invalid.";
        first = std::min(first, qint64(cel.startFrame));
        end = std::max(end, qint64(cel.startFrame) + cel.durationFrames);
        const auto error = celDrawingEditError(shot, cel.trackId, cel.drawingId);
        if (!error.isEmpty()) return error;
    }
    const qint64 span = end - first;
    if (!validInterval(frame, span * repeats, shot.durationFrames))
        return "The copied cels would extend outside the shot. Extend the shot or choose an earlier frame.";
    for (int repeat = 0; repeat < repeats; ++repeat) for (const auto &cel : clipboard.cels) {
        auto *track = mutableTrack(shot, cel.trackId);
        CelExposure added{QUuid::createUuid().toString(QUuid::WithoutBraces), cel.drawingId,
                          int(qint64(frame) + cel.startFrame - first + repeat * span), cel.durationFrames};
        track->exposures.append(added);
        inserted.append({track->id, added.id});
    }
    return sortAndValidate(shot);
}
}

bool Document::commitCelChange(Project project, const QString &label, const QString &selectedId) {
    auto &shot = project.shots[activeShotIndex()];
    const auto &before = activeShot();
    if (shot.animationTracks == before.animationTracks && shot.layers.size() == before.layers.size()) return true;
    const auto error = project.validate();
    if (!error.isEmpty()) { emit warning(error); return false; }
    QScopedValueRollback<bool> animationMutation(m_animationMutation, true);
    return commit(m_project, project, label, selectedId);
}

bool Document::moveAnimationExposures(const QVector<TimelineCelRef> &refs, int deltaFrames) {
    auto error = ready(this); if (!error.isEmpty()) { emit warning(error); return false; }
    auto project = m_project; auto &shot = project.shots[activeShotIndex()]; QSet<QString> selected;
    error = collect(shot, refs, selected); if (!error.isEmpty()) { emit warning(error); return false; }
    for (auto &track : shot.animationTracks) for (auto &cel : track.exposures) if (selected.contains(cel.id)) {
        const qint64 start = qint64(cel.startFrame) + deltaFrames;
        if (!validInterval(start, cel.durationFrames, shot.durationFrames)) {
            emit warning("The selected cels would extend outside the shot."); return false;
        }
        cel.startFrame = int(start);
    }
    error = sortAndValidate(shot); if (!error.isEmpty()) { emit warning(error); return false; }
    return commitCelChange(std::move(project), "Move drawing exposures");
}

bool Document::removeAnimationExposures(const QVector<TimelineCelRef> &refs) {
    auto error = ready(this); if (!error.isEmpty()) { emit warning(error); return false; }
    auto project = m_project; auto &shot = project.shots[activeShotIndex()]; QSet<QString> selected;
    error = collect(shot, refs, selected); if (!error.isEmpty()) { emit warning(error); return false; }
    for (auto &track : shot.animationTracks)
        track.exposures.removeIf([&](const auto &cel) { return selected.contains(cel.id); });
    // Drawings stay in the track's library, including their editable layers,
    // masks and property keys. Only explicit unused-drawing deletion removes them.
    return commitCelChange(std::move(project), "Remove drawing exposures");
}

bool Document::setAnimationExposureHolds(const QVector<TimelineCelRef> &refs, int hold) {
    auto error = ready(this); if (!error.isEmpty()) { emit warning(error); return false; }
    if (hold < 1) { emit warning("A drawing needs at least one exposure frame."); return false; }
    auto project = m_project; auto &shot = project.shots[activeShotIndex()]; QSet<QString> selected;
    error = collect(shot, refs, selected); if (!error.isEmpty()) { emit warning(error); return false; }
    for (auto &track : shot.animationTracks) {
        qint64 next = -1;
        for (auto &cel : track.exposures) if (selected.contains(cel.id)) {
            if (next < 0) next = cel.startFrame;
            if (!validInterval(next, hold, shot.durationFrames)) {
                emit warning("The selected holds would extend outside the shot."); return false;
            }
            cel.startFrame = int(next); cel.durationFrames = hold; next += hold;
        }
    }
    error = sortAndValidate(shot); if (!error.isEmpty()) { emit warning(error); return false; }
    return commitCelChange(std::move(project), "Set drawing holds");
}

bool Document::reverseAnimationExposures(const QVector<TimelineCelRef> &refs) {
    auto error = ready(this); if (!error.isEmpty()) { emit warning(error); return false; }
    auto project = m_project; auto &shot = project.shots[activeShotIndex()]; QSet<QString> selected;
    error = collect(shot, refs, selected); if (!error.isEmpty()) { emit warning(error); return false; }
    for (auto &track : shot.animationTracks) {
        int first = shot.durationFrames, end = 0;
        for (const auto &cel : track.exposures) if (selected.contains(cel.id)) {
            first = std::min(first, cel.startFrame); end = std::max(end, cel.startFrame + cel.durationFrames);
        }
        for (auto &cel : track.exposures) if (selected.contains(cel.id))
            cel.startFrame = first + end - (cel.startFrame + cel.durationFrames);
    }
    error = sortAndValidate(shot); if (!error.isEmpty()) { emit warning(error); return false; }
    return commitCelChange(std::move(project), "Reverse drawing exposures");
}

bool Document::repeatAnimationExposures(const QVector<TimelineCelRef> &refs, int repeats) {
    auto error = ready(this); if (!error.isEmpty()) { emit warning(error); return false; }
    QSet<QString> selected;
    error = collect(activeShot(), refs, selected); if (!error.isEmpty()) { emit warning(error); return false; }
    const auto clipboard = copyCelExposures(activeShot(), refs, &error);
    if (!error.isEmpty()) { emit warning(error); return false; }
    int end = 0; for (const auto &cel : clipboard.cels) end = std::max(end, cel.startFrame + cel.durationFrames);
    auto project = m_project; QVector<TimelineCelRef> inserted;
    error = appendClipboard(project.shots[activeShotIndex()], clipboard, end, repeats, inserted);
    if (!error.isEmpty()) { emit warning(error); return false; }
    return commitCelChange(std::move(project), "Repeat drawing exposures");
}

bool Document::pasteAnimationExposures(const TimelineCelClipboard &clipboard, int startFrame,
                                      QVector<TimelineCelRef> *inserted) {
    auto error = ready(this); if (!error.isEmpty()) { emit warning(error); return false; }
    auto project = m_project; QVector<TimelineCelRef> created;
    error = appendClipboard(project.shots[activeShotIndex()], clipboard, startFrame, 1, created);
    if (!error.isEmpty()) { emit warning(error); return false; }
    if (!commitCelChange(std::move(project), "Paste linked drawing exposures")) return false;
    if (inserted) *inserted = std::move(created);
    return true;
}

bool Document::exposeAnimationDrawing(const QString &trackId, const QString &drawingId, int startFrame,
                                     int hold, TimelineCelRef *inserted) {
    auto error = ready(this); if (!error.isEmpty()) { emit warning(error); return false; }
    auto project = m_project; auto &shot = project.shots[activeShotIndex()];
    error = celDrawingEditError(shot, trackId, drawingId);
    if (!error.isEmpty()) { emit warning(error); return false; }
    if (!validInterval(startFrame, hold, shot.durationFrames)) {
        emit warning("A drawing exposure needs at least one frame and must stay inside the shot."); return false;
    }
    auto *track = mutableTrack(shot, trackId);
    CelExposure exposure{QUuid::createUuid().toString(QUuid::WithoutBraces), drawingId, startFrame, hold};
    track->exposures.append(exposure);
    error = sortAndValidate(shot); if (!error.isEmpty()) { emit warning(error); return false; }
    if (!commitCelChange(std::move(project), "Reuse animation drawing")) return false;
    if (inserted) *inserted = {trackId, exposure.id};
    return true;
}

bool Document::deleteUnusedAnimationDrawing(const QString &trackId, const QString &drawingId) {
    auto error = ready(this); if (!error.isEmpty()) { emit warning(error); return false; }
    auto project = m_project; auto &shot = project.shots[activeShotIndex()];
    error = celDrawingEditError(shot, trackId, drawingId);
    if (!error.isEmpty()) { emit warning(error); return false; }
    auto *track = mutableTrack(shot, trackId);
    for (const auto &cel : track->exposures) if (cel.drawingId == drawingId) {
        emit warning("Remove this drawing's exposures before deleting its artwork from the library."); return false;
    }
    const auto groupId = celDrawing(*track, drawingId)->groupLayerId;
    QSet<QString> removed;
    for (int i = 0; i < shot.layers.size(); ++i)
        if (shot.layers[i].id == groupId || layerIsDescendant(shot.layers, i, groupId)) {
            if (shot.layers[i].locked || layerPositionLocked(shot.layers, i)) {
                emit warning("Unlock the drawing and its layers before deleting unused artwork."); return false;
            }
            removed.insert(shot.layers[i].id);
        }
    shot.layers.removeIf([&](const auto &layer) { return removed.contains(layer.id); });
    track->drawings.removeIf([&](const auto &drawing) { return drawing.id == drawingId; });
    prunePropertyAnimation(shot);
    return commitCelChange(std::move(project), "Delete unused animation drawing");
}
} // namespace toon
