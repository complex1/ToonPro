#pragma once
#include <QByteArray>
#include <QString>
class QIODevice;

namespace toon {
// Correct the timing of a single video sample in a bounded, non-fragmented
// MP4 produced by our Qt recorder. Unsupported layouts fail without mutation.
bool correctSingleFrameMp4Timing(QByteArray *bytes, double fps, QString *error = nullptr);
// Finalize a private, seekable staging file without loading its media payload.
// Reads at most 64 MiB of metadata; validate before writing. An I/O failure may
// leave the private staging file partly patched, so publish it only on success.
bool finalizeMp4AudioPresentation(QIODevice *file, qint64 sampleFrames, int sampleRate,
                                  qint64 prerollFrames, QString *error = nullptr);
} // namespace toon
