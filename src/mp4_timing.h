#pragma once
#include <QByteArray>
#include <QString>

namespace toon {
// Correct the timing of a single video sample in a bounded, non-fragmented
// MP4 produced by our Qt recorder. Unsupported layouts fail without mutation.
bool correctSingleFrameMp4Timing(QByteArray *bytes, double fps, QString *error = nullptr);
// Exclude encoder-only trailing silence from a short clip's presentation, while
// retaining encoded packets and the encoder's existing priming offset. Requires
// the recorder's ordinary one-video/one-audio layout and one audio edit.
bool trimShortMp4AudioPresentation(QByteArray *bytes, qint64 sampleFrames, int sampleRate,
                                   QString *error = nullptr);
} // namespace toon
