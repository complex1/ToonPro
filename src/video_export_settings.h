#pragma once

#include "project.h"
#include <QSize>

namespace toon {
enum class VideoExportScope { Shot, Movie };
enum class VideoExportQuality { Draft, Standard, High };
struct VideoExportSettings {
    int shotIndex = 0;
    int firstFrame = 0;
    int lastFrame = 0;
    QSize size;
    VideoExportQuality quality = VideoExportQuality::High;
    bool includeAudio = true;
    VideoExportScope scope = VideoExportScope::Shot;
    // Required by queued jobs when replacing an existing output.
    bool overwriteApproved = true;
};
struct VideoExportSegment { int shotIndex = 0, firstFrame = 0, frameCount = 0; };
struct VideoExportFrameLocation { int shotIndex = -1, frame = -1; };
int videoExportDurationFrames(const Project &project, VideoExportScope scope, int shotIndex = 0);
QVector<VideoExportSegment> videoExportSegments(const Project &project, const VideoExportSettings &settings);
VideoExportFrameLocation videoExportFrameLocation(const Project &project, const VideoExportSettings &settings, int frame);
VideoExportSettings videoExportMovieDefaults(const Project &project);
VideoExportSettings videoExportDefaults(const Project &project, int shotIndex = 0);
QString validateVideoExportSettings(const Project &project, const VideoExportSettings &settings, bool validateProject = true);
qint64 videoExportFrameTimeUSecs(int relativeFrame, double fps);
qint64 videoExportSampleCount(const Project &project, const VideoExportSettings &settings);
QImage renderVideoExportFrame(const Project &project, const VideoExportSettings &settings,
                              int absoluteFrame, QString *error = nullptr);
} // namespace toon
