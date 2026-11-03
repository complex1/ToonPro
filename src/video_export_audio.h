#pragma once
#include "video_export_settings.h"
#include <QIODevice>
#include <array>

namespace toon {
// Finite offline audio stream. Mixes only the current shot into a small buffer;
// every cut/clip timestamp is rounded from its global frame index, so fractional
// FPS does not accumulate one-sample errors across many shots.
class VideoExportAudioStream final : public QIODevice {
public:
    VideoExportAudioStream(Project project, const VideoExportSettings &settings);
    bool valid() const { return m_valid; }
    bool isSequential() const override { return true; }
    bool atEnd() const override;
    qint64 bytesAvailable() const override;
protected:
    qint64 readData(char *data, qint64 maximum) override;
    qint64 writeData(const char *, qint64) override { return -1; }
private:
    void fillBuffer();
    Project m_project;
    QVector<VideoExportSegment> m_segments;
    std::array<double, 2048 * 2> m_mix{};
    std::array<char, 2048 * 4> m_buffer{};
    qint64 m_cursor = 0, m_end = 0;
    int m_segment = 0, m_size = 0, m_offset = 0;
    bool m_valid = false;
};
} // namespace toon
