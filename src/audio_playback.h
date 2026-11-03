#pragma once

#include <QObject>
#include "movie_playback_sequence.h"
#include <memory>

namespace toon {
struct Shot;
struct Project;

// One unwrapped clock drives both the audio stream and the timeline. Silent is
// useful for headless playback and never enumerates or opens audio hardware.
class AudioPlayback final : public QObject {
    Q_OBJECT
public:
    enum class OutputMode { System, Silent };
    explicit AudioPlayback(QObject *parent = nullptr, OutputMode mode = OutputMode::System);
    ~AudioPlayback() override;
    void start(const Shot &shot, double fps, int startFrame, int rangeFirst, int rangeLast, bool loop);
    void start(const Project &project, const QVector<MoviePlaybackSegment> &segments,
               int startFrame, int rangeFirst, int rangeLast, bool loop);
    void stop();
    bool active() const;
    bool usingAudioClock() const;
    qint64 elapsedUSecs() const;
signals:
    void warning(const QString &message);
private:
    struct Private;
    std::unique_ptr<Private> d;
};
} // namespace toon
