#pragma once
#include "project.h"
#include "onion_settings.h"
#include <functional>

namespace toon {
struct OnionSkinSample {
    QString drawingId;
    int frame = 0;
    bool operator==(const OnionSkinSample &) const = default;
};
// Nearest distinct drawings first. Holds/reused drawings never add duplicate ghosts.
QVector<OnionSkinSample> onionSkinSamples(const Shot &shot, int frame, const QString &trackId,
                                        int direction, int count);
QVector<RasterLayer> onionSkinLayersForSample(const Shot &shot, int currentFrame, const QString &trackId,
                                            const OnionSkinSample &sample, QRectF artworkBounds = {});
QVector<RasterLayer> onionSkinLayersForSample(const Project &project, const Shot &shot, int currentFrame,
                                            const QString &trackId, const OnionSkinSample &sample,
                                            QRectF artworkBounds = {});
// Compatibility helper: a single previous/next distinct drawing.
QVector<RasterLayer> onionSkinLayers(const Shot &shot, int frame, const QString &trackId, int direction,
                                   QRectF artworkBounds = {});
QString animationDisplayKey(const Shot &shot, int frame, const QString &onionTrackId = {},
                            const OnionSkinSettings &settings = {});
// Destination must be new or empty. Files completed before cancellation are
// retained; existing files are never overwritten. Progress returns false to stop.
bool exportPngSequence(const Project &project, int shotIndex, int firstFrame, int lastFrame,
                       const QString &directory, const std::function<bool(int, int)> &progress, QString *error);
} // namespace toon
