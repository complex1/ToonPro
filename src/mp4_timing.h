#pragma once
#include <QByteArray>
#include <QString>

namespace toon {
// Correct the timing of a single video sample in a bounded, non-fragmented
// MP4 produced by our Qt recorder. Unsupported layouts fail without mutation.
bool correctSingleFrameMp4Timing(QByteArray *bytes, double fps, QString *error = nullptr);
} // namespace toon
