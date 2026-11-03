#pragma once

#include "audio.h"
#include "movie_playback_sequence.h"
#include <QIODevice>
#include <array>

namespace toon {

// Input assets are always S16LE; QAudioSink output uses native-endian samples.
enum class AudioMixFormat { Signed16, Float32 };

// Sequential, bounded-memory stereo stream. It retains only shared immutable
// assets and a small mixing buffer, even for long shots and repeated loops.
// A non-looping stream supplies silence beyond its endpoint so the playback
// clock can advance to the timeline's final-frame stop condition.
class AudioMixStream final : public QIODevice {
public:
    AudioMixStream(const QVector<AudioClip> &clips, double fps, int startFrame,
                   int rangeFirst, int rangeLast, bool loop,
                   AudioMixFormat format = AudioMixFormat::Signed16);
    AudioMixStream(const Project &project, const QVector<MoviePlaybackSegment> &segments,
                   int startFrame, int rangeFirst, int rangeLast, bool loop,
                   AudioMixFormat format = AudioMixFormat::Signed16);
    bool valid() const { return m_valid; }
    bool hasAudio() const { return !m_clips.isEmpty(); }
    bool isSequential() const override { return true; }
    bool atEnd() const override { return false; }
    qint64 bytesAvailable() const override;
protected:
    qint64 readData(char *data, qint64 maxSize) override;
    qint64 writeData(const char *, qint64) override { return -1; }
private:
    bool initialize(double fps, int startFrame, int rangeFirst, int rangeLast);
    void appendClips(const QVector<AudioClip> &, double fps, int firstFrame, qint64 cutEnd);
    void fillBuffer();
    struct Clip {
        AudioClip audio;
        qint64 timelineStart = 0;
    };
    struct Segment {
        qint64 end = 0;
        qsizetype clipsFirst = 0, clipsEnd = 0;
    };
    static constexpr int bufferFrames = 1024;
    QVector<Clip> m_clips;
    QVector<Segment> m_segments;
    qsizetype m_segment = 0;
    std::array<double, bufferFrames * audioChannels> m_mix{};
    std::array<char, bufferFrames * audioChannels * sizeof(float)> m_buffer{};
    qint64 m_first = 0, m_end = 0, m_cursor = 0;
    int m_bufferSize = 0, m_bufferOffset = 0;
    AudioMixFormat m_format;
    bool m_loop = false, m_valid = false;
};
} // namespace toon
