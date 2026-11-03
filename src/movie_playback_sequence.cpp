#include "movie_playback_sequence.h"
#include "project.h"
#include <algorithm>
#include <iterator>
#include <limits>

namespace toon {
QVector<MoviePlaybackSegment> moviePlaybackSegments(const Project &project, MoviePlaybackScope scope,
                                                   const QString &scene) {
    if ((scope != MoviePlaybackScope::Scene && scope != MoviePlaybackScope::Movie)
        || project.shots.size() > maxProjectShots) return {};
    QVector<MoviePlaybackSegment> result;
    qint64 cursor = 0;
    for (int i = 0; i < project.shots.size(); ++i) {
        const auto &shot = project.shots[i];
        if (scope == MoviePlaybackScope::Scene && shot.scene != scene) continue;
        if (shot.durationFrames <= 0 || cursor + shot.durationFrames > std::numeric_limits<int>::max()) return {};
        result.append({i, int(cursor), shot.durationFrames});
        cursor += shot.durationFrames;
    }
    return result;
}
int moviePlaybackDuration(const QVector<MoviePlaybackSegment> &segments) {
    if (segments.size() > maxProjectShots) return 0;
    qint64 cursor = 0;
    for (const auto &segment : segments) {
        if (segment.shotIndex < 0 || segment.firstFrame != cursor || segment.frameCount <= 0) return 0;
        cursor += segment.frameCount;
        if (cursor > std::numeric_limits<int>::max()) return 0;
    }
    return int(cursor);
}
MoviePlaybackLocation moviePlaybackLocation(const QVector<MoviePlaybackSegment> &segments, int globalFrame) {
    if (globalFrame < 0 || segments.isEmpty()) return {};
    const auto next = std::upper_bound(segments.cbegin(), segments.cend(), globalFrame,
        [](int frame, const MoviePlaybackSegment &segment) { return frame < segment.firstFrame; });
    if (next == segments.cbegin()) return {};
    const auto &segment = *std::prev(next);
    if (segment.shotIndex < 0 || segment.firstFrame < 0 || globalFrame - segment.firstFrame >= segment.frameCount) return {};
    return {segment.shotIndex, globalFrame - segment.firstFrame};
}
} // namespace toon
