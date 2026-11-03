#include "video_export_audio.h"
#include "audio.h"
#include <QtEndian>
#include <algorithm>
#include <cmath>
#include <cstring>

namespace toon {
VideoExportAudioStream::VideoExportAudioStream(Project project, const VideoExportSettings &settings)
    : m_project(std::move(project)) {
    if (!validateVideoExportSettings(m_project, settings).isEmpty()) return;
    m_segments = videoExportSegments(m_project, settings);
    m_cursor = audioSampleFrameAtTimelineFrame(settings.firstFrame, m_project.fps);
    m_end = audioSampleFrameAtTimelineFrame(qreal(settings.lastFrame) + 1, m_project.fps);
    m_valid = !m_segments.isEmpty() && m_end > m_cursor;
    if (m_valid) open(QIODevice::ReadOnly | QIODevice::Unbuffered);
}
bool VideoExportAudioStream::atEnd() const { return !m_valid || (m_cursor >= m_end && m_offset >= m_size); }
qint64 VideoExportAudioStream::bytesAvailable() const {
    return m_valid ? (m_end - m_cursor) * audioBytesPerSampleFrame + m_size - m_offset + QIODevice::bytesAvailable() : 0;
}
void VideoExportAudioStream::fillBuffer() {
    m_size = m_offset = 0;
    if (m_cursor >= m_end) return;
    while (m_segment < m_segments.size() && m_cursor >= audioSampleFrameAtTimelineFrame(
        qreal(m_segments[m_segment].firstFrame) + m_segments[m_segment].frameCount, m_project.fps)) ++m_segment;
    if (m_segment >= m_segments.size()) return;
    const auto &segment = m_segments[m_segment];
    const auto cutEnd = audioSampleFrameAtTimelineFrame(qreal(segment.firstFrame) + segment.frameCount, m_project.fps);
    const int count = int(std::min<qint64>({2048, m_end - m_cursor, cutEnd - m_cursor}));
    if (count <= 0) return;
    m_mix.fill(0);
    for (const auto &clip : m_project.shots[segment.shotIndex].audioClips) {
        if (!clip.asset || clip.muted || (!clip.gainAnimated && clip.gain <= 0) || clip.sampleCount <= 0) continue;
        const auto start = audioSampleFrameAtTimelineFrame(qreal(segment.firstFrame) + clip.startFrame, m_project.fps);
        const auto first = qMax(m_cursor, start);
        const auto end = std::min<qint64>({m_cursor + count, cutEnd, start + clip.sampleCount});
        if (end <= first) continue;
        const auto source = clip.sourceOffsetSamples + first - start;
        auto *output = m_mix.data() + (first - m_cursor) * audioChannels;
        mixAudioClipSamples(clip, source, end - first, output);
    }
    for (int i = 0; i < count * audioChannels; ++i) {
        const qint16 sample = qint16(std::lround(std::clamp(m_mix[size_t(i)], -32768., 32767.)));
        std::memcpy(m_buffer.data() + i * sizeof(sample), &sample, sizeof(sample));
    }
    m_cursor += count; m_size = count * audioBytesPerSampleFrame;
}
qint64 VideoExportAudioStream::readData(char *data, qint64 maximum) {
    if (!m_valid) return -1;
    qint64 copied = 0;
    while (copied < maximum) {
        if (m_offset == m_size) fillBuffer();
        const int count = int(qMin<qint64>(m_size - m_offset, maximum - copied));
        if (count <= 0) break;
        std::memcpy(data + copied, m_buffer.data() + m_offset, size_t(count));
        m_offset += count; copied += count;
    }
    return copied;
}
} // namespace toon
