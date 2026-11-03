#include "cel_edit.h"
#include "project.h"
#include <algorithm>

namespace toon {
namespace {
int layerIndex(const Shot &shot, const QString &id) {
    for (int i = 0; i < shot.layers.size(); ++i) if (shot.layers[i].id == id) return i;
    return -1;
}
QString folderEditError(const Shot &shot, const QString &id) {
    int index = layerIndex(shot, id);
    if (index < 0) return "This drawing folder no longer exists.";
    QSet<QString> visited;
    while (index >= 0) {
        const auto &layer = shot.layers[index];
        if (visited.contains(layer.id)) return "This drawing has invalid folder ancestry.";
        visited.insert(layer.id);
        if (layer.locked) return "Unlock the drawing and track folders before changing cel timing.";
        if (layer.positionLocked) return "Unlock the drawing and track folder positions before changing cel timing.";
        if (layer.parentId.isEmpty()) break;
        index = layerIndex(shot, layer.parentId);
        if (index < 0) return "This drawing's parent folder no longer exists.";
    }
    return {};
}
}

const AnimationTrack *celTrack(const Shot &shot, const QString &trackId) {
    for (const auto &track : shot.animationTracks) if (track.id == trackId) return &track;
    return nullptr;
}
const CelExposure *celExposure(const Shot &shot, const TimelineCelRef &ref) {
    if (const auto *track = celTrack(shot, ref.trackId))
        for (const auto &exposure : track->exposures) if (exposure.id == ref.exposureId) return &exposure;
    return nullptr;
}
const AnimationDrawing *celDrawing(const AnimationTrack &track, const QString &drawingId) {
    for (const auto &drawing : track.drawings) if (drawing.id == drawingId) return &drawing;
    return nullptr;
}
QString drawingDisplayId(const AnimationTrack &track, const QString &drawingId) {
    for (int i = 0; i < track.drawings.size(); ++i) if (track.drawings[i].id == drawingId)
        return QString("D%1").arg(i + 1, 3, 10, QChar('0'));
    return {};
}
QString celDrawingEditError(const Shot &shot, const QString &trackId, const QString &drawingId) {
    const auto *track = celTrack(shot, trackId);
    if (!track) return "This animation track no longer exists.";
    const auto *drawing = celDrawing(*track, drawingId);
    if (!drawing) return "This animation drawing no longer exists.";
    const auto trackError = folderEditError(shot, track->rootGroupLayerId);
    return trackError.isEmpty() ? folderEditError(shot, drawing->groupLayerId) : trackError;
}
QString celEditError(const Shot &shot, const TimelineCelRef &ref) {
    const auto *exposure = celExposure(shot, ref);
    if (!exposure) return "A selected drawing exposure no longer exists.";
    return celDrawingEditError(shot, ref.trackId, exposure->drawingId);
}
TimelineCelClipboard copyCelExposures(const Shot &shot, const QVector<TimelineCelRef> &refs, QString *error) {
    if (error) error->clear();
    const auto fail = [&](const QString &reason) {
        if (error) *error = reason;
        return TimelineCelClipboard{};
    };
    if (refs.isEmpty()) return fail("Select one or more drawing exposures first.");
    if (refs.size() > maxAnimationExposures) return fail("Too many selected drawing exposures.");
    TimelineCelClipboard result; result.shotId = shot.id;
    QSet<QString> seen;
    for (const auto &ref : refs) {
        const auto *exposure = celExposure(shot, ref);
        if (!exposure) return fail("A selected drawing exposure no longer exists.");
        if (seen.contains(exposure->id)) continue;
        seen.insert(exposure->id);
        result.cels.append({ref.trackId, exposure->drawingId, exposure->startFrame, exposure->durationFrames});
    }
    std::stable_sort(result.cels.begin(), result.cels.end(), [](const auto &a, const auto &b) {
        return a.startFrame < b.startFrame;
    });
    return result;
}
} // namespace toon
