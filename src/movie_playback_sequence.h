#pragma once

#include <QString>
#include <QVector>

namespace toon {
struct Project;
enum class MoviePlaybackScope { Scene, Movie };
struct MoviePlaybackSegment {
    int shotIndex = 0, firstFrame = 0, frameCount = 0;
    bool operator==(const MoviePlaybackSegment &) const = default;
};
struct MoviePlaybackLocation {
    int shotIndex = -1, frame = -1;
    bool operator==(const MoviePlaybackLocation &) const = default;
};
// Scene review compacts matching shots in their existing movie order. The
// empty scene name is a real, selectable group of unassigned shots.
QVector<MoviePlaybackSegment> moviePlaybackSegments(const Project &, MoviePlaybackScope,
                                                   const QString &scene = {});
// Zero denotes an empty or invalid/non-contiguous sequence, including overflow.
int moviePlaybackDuration(const QVector<MoviePlaybackSegment> &);
MoviePlaybackLocation moviePlaybackLocation(const QVector<MoviePlaybackSegment> &, int globalFrame);
} // namespace toon
