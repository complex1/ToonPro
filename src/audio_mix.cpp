#include "audio_mix.h"
#include "project.h"
#include <QtEndian>
#include <algorithm>
#include <cmath>
#include <cstring>

namespace toon {
AudioMixStream::AudioMixStream(const QVector<AudioClip> &clips, double fps, int startFrame,
                               int rangeFirst, int rangeLast, bool loop, AudioMixFormat format)
    : m_format(format), m_loop(loop) {
    if (clips.size() > maxAudioClipsPerShot || !initialize(fps, startFrame, rangeFirst, rangeLast)) return;
    appendClips(clips, fps, 0, m_end);
    m_segments.append({m_end, 0, m_clips.size()});
    m_valid = true;
    open(QIODevice::ReadOnly | QIODevice::Unbuffered);
}

AudioMixStream::AudioMixStream(const Project &project, const QVector<MoviePlaybackSegment> &segments,
                               int startFrame, int rangeFirst, int rangeLast, bool loop, AudioMixFormat format)
    : m_format(format), m_loop(loop) {
    const int duration = moviePlaybackDuration(segments);
    if (!duration || rangeLast >= duration || !initialize(project.fps, startFrame, rangeFirst, rangeLast)) return;
    // Validate all cuts before keeping any clips: malformed sequences must not
    // silently play a partial movie. The data retained below owns no pixel data.
    for (const auto &segment : segments)
        if (segment.shotIndex >= project.shots.size()
            || project.shots[segment.shotIndex].durationFrames != segment.frameCount
            || project.shots[segment.shotIndex].audioClips.size() > maxAudioClipsPerShot) return;
    const auto audibleFirst = loop ? m_first : m_cursor;
    for (const auto &segment : segments) {
        const auto first = audioSampleFrameAtTimelineFrame(segment.firstFrame, project.fps);
        const auto end = audioSampleFrameAtTimelineFrame(qreal(segment.firstFrame) + segment.frameCount, project.fps);
        if (end <= audibleFirst || first >= m_end) continue;
        const auto begin = m_clips.size();
        appendClips(project.shots[segment.shotIndex].audioClips, project.fps, segment.firstFrame, end);
        m_segments.append({qMin(end, m_end), begin, m_clips.size()});
    }
    if (m_segments.isEmpty()) return;
    m_valid = true;
    open(QIODevice::ReadOnly | QIODevice::Unbuffered);
}

bool AudioMixStream::initialize(double fps, int startFrame, int rangeFirst, int rangeLast) {
    if (!std::isfinite(fps) || fps < 1 || fps > 120 || rangeFirst < 0 || rangeLast < rangeFirst
        || (m_format != AudioMixFormat::Signed16 && m_format != AudioMixFormat::Float32)) return false;
    m_first = audioSampleFrameAtTimelineFrame(rangeFirst, fps);
    m_end = audioSampleFrameAtTimelineFrame(qreal(rangeLast) + 1, fps);
    m_cursor = audioSampleFrameAtTimelineFrame(std::clamp(startFrame, rangeFirst, rangeLast), fps);
    return m_end > m_first;
}

void AudioMixStream::appendClips(const QVector<AudioClip> &clips, double fps, int firstFrame, qint64 cutEnd) {
    const auto audibleFirst = m_loop ? m_first : m_cursor;
    for (const auto &clip : clips) {
        if (!clip.asset || clip.muted || !std::isfinite(clip.gain) || (!clip.gainAnimated && clip.gain <= 0)
            || clip.startFrame < 0 || clip.sourceOffsetSamples < 0 || clip.sampleCount <= 0
            || clip.sourceOffsetSamples >= clip.asset->sampleFrameCount()
            || clip.asset->sampleFrameCount() > maxAudioAssetBytes / audioBytesPerSampleFrame
            || clip.asset->pcm.size() != clip.asset->sampleFrameCount() * audioBytesPerSampleFrame
            || !validateAudioClipFinishing(clip).isEmpty()) continue;
        // Round the global frame once. Adding individually rounded shot/clip
        // offsets would move some fractional-FPS cuts by a sample.
        const auto start = audioSampleFrameAtTimelineFrame(qreal(firstFrame) + clip.startFrame, fps);
        if (start >= qMin(m_end, cutEnd)) continue;
        const auto sourceCount = std::min(clip.sampleCount, clip.asset->sampleFrameCount() - clip.sourceOffsetSamples);
        const auto count = std::min(sourceCount, cutEnd - start);
        if (count <= 0 || start + count <= audibleFirst) continue;
        auto audio = clip;
        audio.sampleCount = sourceCount;
        // Truncating at a hard cut must not pull an unanchored fade-out forward.
        // Its original source envelope stays intact until the cut itself.
        materializeAudioFadeAnchors(audio);
        audio.sampleCount = count; audio.gain = std::clamp(clip.gain, 0.0, 2.0);
        m_clips.append({std::move(audio), start});
    }
}

qint64 AudioMixStream::bytesAvailable() const {
    const int bytesPerFrame = audioChannels * (m_format == AudioMixFormat::Signed16 ? sizeof(qint16) : sizeof(float));
    return (m_valid ? bufferFrames * bytesPerFrame : 0) + QIODevice::bytesAvailable();
}

void AudioMixStream::fillBuffer() {
    m_mix.fill(0);
    int destination = 0;
    while (destination < bufferFrames) {
        if (m_cursor == m_end) {
            if (!m_loop) break;
            m_cursor = m_first;
            m_segment = 0;
        }
        while (m_segment < m_segments.size() && m_cursor >= m_segments[m_segment].end) ++m_segment;
        if (m_segment >= m_segments.size()) break;
        const auto &segment = m_segments[m_segment];
        const int count = int(std::min<qint64>(bufferFrames - destination, segment.end - m_cursor));
        for (qsizetype i = segment.clipsFirst; i < segment.clipsEnd; ++i) {
            const auto &clip = m_clips[i];
            const auto first = std::max(m_cursor, clip.timelineStart);
            const auto end = std::min(m_cursor + count, clip.timelineStart + clip.audio.sampleCount);
            if (end <= first) continue;
            const auto source = clip.audio.sourceOffsetSamples + first - clip.timelineStart;
            auto *mixed = m_mix.data() + (destination + first - m_cursor) * audioChannels;
            mixAudioClipSamples(clip.audio, source, end - first, mixed);
        }
        destination += count;
        m_cursor += count;
    }
    if (m_format == AudioMixFormat::Signed16) {
        for (size_t i = 0; i < m_mix.size(); ++i) {
            const qint16 value = qint16(std::lround(std::clamp(m_mix[i], -32768.0, 32767.0)));
            std::memcpy(m_buffer.data() + i * sizeof(value), &value, sizeof(value));
        }
        m_bufferSize = int(m_mix.size() * sizeof(qint16));
    } else {
        for (size_t i = 0; i < m_mix.size(); ++i) {
            const float value = float(std::clamp(m_mix[i] / 32768.0, -1.0, 1.0));
            std::memcpy(m_buffer.data() + i * sizeof(value), &value, sizeof(value));
        }
        m_bufferSize = int(m_mix.size() * sizeof(float));
    }
    m_bufferOffset = 0;
}

qint64 AudioMixStream::readData(char *data, qint64 maxSize) {
    if (!m_valid) return -1;
    qint64 copied = 0;
    while (copied < maxSize) {
        if (m_bufferOffset == m_bufferSize) fillBuffer();
        const int count = int(std::min<qint64>(m_bufferSize - m_bufferOffset, maxSize - copied));
        std::memcpy(data + copied, m_buffer.data() + m_bufferOffset, size_t(count));
        m_bufferOffset += count;
        copied += count;
    }
    return copied;
}
} // namespace toon
