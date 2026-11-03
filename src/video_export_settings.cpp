#include "video_export_settings.h"
#include "audio_mix.h"
#include "keyframe_render.h"
#include "sheet.h"
#include <QColorSpace>
#include <QPainter>
#include <cmath>
#include <limits>

namespace toon {
namespace {
QString validateRange(const Project &project, const VideoExportSettings &settings) {
    if (!std::isfinite(project.fps) || project.fps < 1 || project.fps > 120)
        return "The project frame rate must be between 1 and 120 FPS.";
    if (project.width < 64 || project.width > 8192 || project.height < 64 || project.height > 8192)
        return "The project artwork dimensions are invalid.";
    if (settings.scope != VideoExportScope::Shot && settings.scope != VideoExportScope::Movie)
        return "Choose a supported video export scope.";
    if (settings.scope == VideoExportScope::Shot && (settings.shotIndex < 0 || settings.shotIndex >= project.shots.size()))
        return "Choose an existing shot to export.";
    const int duration = videoExportDurationFrames(project, settings.scope, settings.shotIndex);
    if (duration <= 0) return "The movie or shot duration is invalid or exceeds the export frame limit.";
    if (settings.firstFrame < 0 || settings.lastFrame < settings.firstFrame || settings.lastFrame >= duration)
        return "The export range must stay within the movie or shot, with its end after its start.";
    return {};
}
QString validateSize(QSize size) {
    if (size.width() < 16 || size.height() < 16 || size.width() > 4096 || size.height() > 4096
        || size.width() % 2 || size.height() % 2)
        return "MP4 width and height must be even numbers between 16 and 4096.";
    return {};
}
}
int videoExportDurationFrames(const Project &project, VideoExportScope scope, int shotIndex) {
    if (scope == VideoExportScope::Shot)
        return shotIndex >= 0 && shotIndex < project.shots.size() ? qMax(0, project.shots[shotIndex].durationFrames) : 0;
    if (scope != VideoExportScope::Movie) return 0;
    qint64 total = 0;
    for (const auto &shot : project.shots) {
        if (shot.durationFrames <= 0) return 0;
        total += shot.durationFrames;
        if (total > std::numeric_limits<int>::max()) return 0;
    }
    return int(total);
}
QVector<VideoExportSegment> videoExportSegments(const Project &project, const VideoExportSettings &settings) {
    QVector<VideoExportSegment> segments;
    if (videoExportDurationFrames(project, settings.scope, settings.shotIndex) <= 0) return segments;
    if (settings.scope == VideoExportScope::Shot) return {{settings.shotIndex, 0, project.shots[settings.shotIndex].durationFrames}};
    int cursor = 0;
    for (int i = 0; i < project.shots.size(); ++i) {
        segments.append({i, cursor, project.shots[i].durationFrames});
        cursor += project.shots[i].durationFrames;
    }
    return segments;
}
VideoExportFrameLocation videoExportFrameLocation(const Project &project, const VideoExportSettings &settings, int frame) {
    if (frame < 0) return {};
    for (const auto &segment : videoExportSegments(project, settings))
        if (frame >= segment.firstFrame && frame - segment.firstFrame < segment.frameCount)
            return {segment.shotIndex, frame - segment.firstFrame};
    return {};
}
VideoExportSettings videoExportMovieDefaults(const Project &project) {
    auto settings = videoExportDefaults(project);
    settings.scope = VideoExportScope::Movie;
    settings.lastFrame = qMax(1, videoExportDurationFrames(project, settings.scope)) - 1;
    settings.includeAudio = false;
    for (int i = 0; i < project.shots.size(); ++i) settings.includeAudio |= videoExportDefaults(project, i).includeAudio;
    return settings;
}
VideoExportSettings videoExportDefaults(const Project &project, int shotIndex) {
    VideoExportSettings settings;
    settings.shotIndex = shotIndex;
    const double scale = qMin(1.0, 4096.0 / qMax(1, qMax(project.width, project.height)));
    auto even = [scale](int value) { return qBound(16, 2 * qRound(value * scale / 2), 4096); };
    settings.size = QSize(even(project.width), even(project.height));
    settings.includeAudio = false;
    if (shotIndex >= 0 && shotIndex < project.shots.size()) {
        const auto &shot = project.shots[shotIndex];
        settings.lastFrame = qMax(1, shot.durationFrames) - 1;
        if (std::isfinite(project.fps) && project.fps >= 1 && project.fps <= 120) {
            AudioMixStream mix(shot.audioClips, project.fps, 0, 0, settings.lastFrame, false);
            settings.includeAudio = mix.hasAudio();
        }
    }
    return settings;
}
QString validateVideoExportSettings(const Project &project, const VideoExportSettings &settings, bool validateProject) {
    if (const auto error = validateRange(project, settings); !error.isEmpty()) return error;
    if (const auto error = validateSize(settings.size); !error.isEmpty()) return error;
    switch (settings.quality) {
    case VideoExportQuality::Draft: case VideoExportQuality::Standard: case VideoExportQuality::High: break;
    default: return "Choose a supported video quality.";
    }
    return validateProject ? project.validate() : QString{};
}
qint64 videoExportFrameTimeUSecs(int relativeFrame, double fps) {
    if (relativeFrame < 0 || !std::isfinite(fps) || fps < 1 || fps > 120) return 0;
    // Derive every timestamp from its index, so fractional FPS never accumulates rounding drift.
    return qRound64(double(relativeFrame) * 1000000.0 / fps);
}
qint64 videoExportSampleCount(const Project &project, const VideoExportSettings &settings) {
    if (!validateRange(project, settings).isEmpty()) return 0;
    return audioSampleFrameAtTimelineFrame(qreal(settings.lastFrame) + 1, project.fps)
        - audioSampleFrameAtTimelineFrame(settings.firstFrame, project.fps);
}
QImage renderVideoExportFrame(const Project &project, const VideoExportSettings &settings,
                              int absoluteFrame, QString *error) {
    if (error) error->clear();
    auto fail = [error](const QString &message) { if (error) *error = message; return QImage(); };
    // Full snapshot validation happens once before encoding, rather than for every frame.
    if (const auto invalid = validateRange(project, settings); !invalid.isEmpty()) return fail(invalid);
    if (const auto invalid = validateSize(settings.size); !invalid.isEmpty()) return fail(invalid);
    if (absoluteFrame < settings.firstFrame || absoluteFrame > settings.lastFrame)
        return fail("The requested frame is outside the export range.");
    const auto location = videoExportFrameLocation(project, settings, absoluteFrame);
    if (location.shotIndex < 0) return fail("The requested frame does not belong to a shot.");
    const auto artwork = renderMovieFrame(project, location.frame, location.shotIndex);
    if (artwork.isNull()) {
        const auto detail = keyframeRenderError();
        return fail(detail.isEmpty() ? "Could not render the artwork frame. Try a smaller project." : detail);
    }
    QImage output(settings.size, QImage::Format_RGBA8888);
    if (output.isNull()) return fail("Could not allocate the video frame. Try a smaller output size.");
    output.setColorSpace(QColorSpace::SRgb);
    output.fill(Qt::white);
    QPainter painter(&output);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    const auto fitted = QSizeF(artwork.size()).scaled(QSizeF(settings.size), Qt::KeepAspectRatio);
    const QRectF target((settings.size.width() - fitted.width()) / 2,
                        (settings.size.height() - fitted.height()) / 2, fitted.width(), fitted.height());
    painter.drawImage(target, artwork);
    return output;
}
} // namespace toon
