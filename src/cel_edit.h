#pragma once

#include "animation.h"

namespace toon {

struct TimelineCelRef {
    QString trackId, exposureId;
    bool operator==(const TimelineCelRef &) const = default;
};
struct TimelineCelClipboardEntry {
    QString trackId, drawingId;
    int startFrame = 0, durationFrames = 1;
    bool operator==(const TimelineCelClipboardEntry &) const = default;
};
// Timing snapshots reference the original drawings, rather than copying pixels.
// They can be pasted into their original tracks in this shot only.
struct TimelineCelClipboard {
    QString shotId;
    QVector<TimelineCelClipboardEntry> cels;
};

const AnimationTrack *celTrack(const Shot &shot, const QString &trackId);
const CelExposure *celExposure(const Shot &shot, const TimelineCelRef &ref);
const AnimationDrawing *celDrawing(const AnimationTrack &track, const QString &drawingId);
QString drawingDisplayId(const AnimationTrack &track, const QString &drawingId);
QString celDrawingEditError(const Shot &shot, const QString &trackId, const QString &drawingId);
QString celEditError(const Shot &shot, const TimelineCelRef &ref);
TimelineCelClipboard copyCelExposures(const Shot &shot, const QVector<TimelineCelRef> &refs,
                                     QString *error = nullptr);

} // namespace toon
