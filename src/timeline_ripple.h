#pragma once
#include <QString>

namespace toon {
struct Shot;
enum class TimelineRippleKind { Insert, Delete, Move };
struct TimelineRippleEdit {
    TimelineRippleKind kind = TimelineRippleKind::Insert;
    int startFrame = 0;
    int durationFrames = 1;
    // Original shot boundary, before cutting the source interval.
    int destinationFrame = 0;
    bool operator==(const TimelineRippleEdit &) const = default;
};
// Intervals are half-open; Insert allows startFrame == shot duration. Failure
// leaves the complete shot untouched. Source artwork/audio resources are shared.
// Symbol clocks and finite clip boundaries move with surviving exposures.
// Edits requiring multiple clocks in one clip are rejected atomically.
QString applyTimelineRipple(Shot &shot, double fps, const TimelineRippleEdit &edit);
int timelineRippleFrame(int originalFrame, int originalDuration, const TimelineRippleEdit &edit);
QString timelineRippleLabel(TimelineRippleKind kind);
} // namespace toon
