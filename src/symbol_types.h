#pragma once
#include <QString>
#include <QtGlobal>
#include <QVector>

namespace toon {
enum class SymbolPlayback { Loop, Once, SingleFrame, PingPong, Poses };
struct SymbolPoseKey {
    // Unscaled instance clock: parent frame - clip start + playback offset.
    int frame = 0;
    int poseFrame = 0;
    bool operator==(const SymbolPoseKey &) const = default;
};
struct SymbolInstance {
    QString symbolId;
    SymbolPlayback playback = SymbolPlayback::Loop;
    int startFrame = 0;
    int firstFrame = 0;
    qreal speed = 1;
    // -1 keeps the legacy through-shot exposure; zero is a retained empty clip.
    int durationFrames = -1;
    // Parent-frame phase before speed is applied, so trimming preserves subframes.
    int playbackOffset = 0;
    QVector<SymbolPoseKey> poseKeys{};
    bool operator==(const SymbolInstance &) const = default;
};
} // namespace toon
