#pragma once

#include "raster.h"
#include <QHash>
#include <QJsonArray>
#include <QSet>

namespace toon {
struct Shot;
struct Project;

inline constexpr int maxAnimationTracks = 16;
inline constexpr int maxAnimationDrawings = 256;
inline constexpr int maxAnimationExposures = 16384;
inline constexpr int maxAnimationLayers = 4096;
inline constexpr int maxDrawingLayers = 64;

enum class DrawingTemplate { Simple, Rough, Anime };
struct DrawingTemplateSlot {
    QString id, name;
    LayerKind kind = LayerKind::Raster;
    bool operator==(const DrawingTemplateSlot &) const = default;
};
struct AnimationDrawing {
    QString id, groupLayerId;
    QHash<QString, QString> templateLayers;
    bool operator==(const AnimationDrawing &) const = default;
};
struct CelExposure {
    QString id, drawingId;
    int startFrame = 0, durationFrames = 1;
    bool operator==(const CelExposure &) const = default;
};
struct AnimationTrack {
    QString id, rootGroupLayerId;
    QVector<DrawingTemplateSlot> drawingTemplate;
    QVector<AnimationDrawing> drawings;
    QVector<CelExposure> exposures;
    bool operator==(const AnimationTrack &) const = default;
};
struct AnimationOwner {
    QString trackId, drawingId;
    bool operator==(const AnimationOwner &) const = default;
};

// Frames are zero based. Exposure intervals are [start, start + duration).
const CelExposure *exposureAtFrame(const AnimationTrack &track, int frame);
const AnimationDrawing *drawingAtFrame(const AnimationTrack &track, int frame);
QHash<QString, AnimationOwner> animationOwners(const Shot &shot);
AnimationOwner animationOwner(const Shot &shot, const QString &layerId);
QSet<QString> activeLayerIds(const Shot &shot, int frame);
bool layerPresentAtFrame(const Shot &shot, const QString &layerId, int frame);
QVector<RasterLayer> evaluateLayers(const Shot &shot, int frame, bool sourceOnly = false, QRectF bounds = {}, QString *error = nullptr);
QVector<RasterLayer> evaluateLayers(const Project &project, const Shot &shot, int frame,
                                  bool sourceOnly = false, QRectF bounds = {}, QString *error = nullptr);
QString validateAnimation(const Shot &shot);
// Ordinary layer operations cannot change animation ownership or containers.
QString validateAnimationMutation(const Shot &before, const Shot &after);
QVector<DrawingTemplateSlot> drawingTemplateSlots(DrawingTemplate type);
QString validateDrawingTemplate(const QVector<DrawingTemplateSlot> &layerSlots);
QJsonArray animationTracksToJson(const QVector<AnimationTrack> &tracks);
bool animationTracksFromJson(const QJsonArray &json, QVector<AnimationTrack> *tracks, QString *error);
qint64 animationHistoryCost(const Shot &before, const Shot &after);
} // namespace toon
