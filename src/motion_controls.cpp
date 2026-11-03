#include "motion_controls.h"
#include "project.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <set>

namespace toon {
namespace {
const RasterLayer *findLayer(const Shot &shot, const QString &id) {
    for (const auto &layer : shot.layers) if (layer.id == id) return &layer;
    return nullptr;
}
struct MotionLayer {
    const LayerAnimation *animation = nullptr;
    std::array<const PropertyChannel *, 5> channels{};
};
QVector<MotionLayer> motionAncestry(const Shot &shot, const RasterLayer *source) {
    QVector<MotionLayer> ancestry;
    QSet<QString> visited;
    for (const auto *layer = source; layer && ancestry.size() <= 8; layer = findLayer(shot, layer->parentId)) {
        if (visited.contains(layer->id)) break;
        visited.insert(layer->id);
        MotionLayer motion;
        if (const auto *animation = layerAnimation(shot, layer->id)) {
            motion.animation = animation;
            for (const auto &channel : animation->channels) {
                const int property = int(channel.property);
                if (property >= 0 && property < int(motion.channels.size())) motion.channels[property] = &channel;
            }
        }
        ancestry.append(motion);
        if (layer->parentId.isEmpty()) break;
    }
    return ancestry;
}
QPointF worldPosition(const QVector<MotionLayer> &ancestry, QPointF pivot, qreal frame) {
    for (int index = 0; index < ancestry.size(); ++index) {
        const auto *animation = ancestry[index].animation;
        if (!animation) continue;
        // The source pivot is invariant under its own scale/rotation. Besides
        // avoiding roundoff, this avoids orientation searches for thousands of
        // stationary position markers. Ancestors still transform it normally.
        if (index == 0 && pivot == animation->pivot) pivot += evaluateAnimatedPosition(*animation, frame);
        else pivot = evaluateLayerTransform(*animation, frame).map(pivot);
    }
    return pivot;
}
} // namespace

QPointF effectiveAnimationPivot(const Shot &shot, const QString &layerId, QSizeF fallbackSize) {
    if (const auto *animation = layerAnimation(shot, layerId)) return animation->pivot;
    return defaultAnimationPivot(shot, layerId, fallbackSize);
}
QPointF effectiveAnimationPivot(const Project &project, const Shot &shot, const QString &layerId, int frame) {
    if (const auto *animation = layerAnimation(shot, layerId)) return animation->pivot;
    return defaultAnimationPivot(project, shot, layerId, frame);
}
QPointF animationPivotWorld(const Shot &shot, const QString &layerId, qreal frame, QSizeF fallbackSize) {
    return worldLayerTransform(shot, layerId, frame).map(effectiveAnimationPivot(shot, layerId, fallbackSize));
}
QPointF animationPivotWorld(const Project &project, const Shot &shot, const QString &layerId, int frame) {
    return worldLayerTransform(shot, layerId, frame).map(effectiveAnimationPivot(project, shot, layerId, frame));
}
QVector<int> motionPositionKeyFrames(const Shot &shot, const QString &layerId) {
    QVector<int> frames;
    if (const auto *animation = layerAnimation(shot, layerId)) {
        for (const auto &channel : animation->channels)
            if (channel.property == KeyframeProperty::PositionX || channel.property == KeyframeProperty::PositionY)
                for (const auto &key : channel.keys) frames.append(key.frame);
    }
    std::sort(frames.begin(), frames.end());
    frames.erase(std::unique(frames.begin(), frames.end()), frames.end());
    return frames;
}
QVector<MotionPathSample> motionPositionKeySamples(const Shot &shot, const QString &layerId, QSizeF fallbackSize) {
    QVector<MotionPathSample> samples;
    const auto *source = findLayer(shot, layerId);
    if (!source) return samples;
    const auto frames = motionPositionKeyFrames(shot, layerId);
    if (frames.isEmpty()) return samples;
    const auto ancestry = motionAncestry(shot, source);
    const auto pivot = effectiveAnimationPivot(shot, layerId, fallbackSize);
    samples.reserve(std::min(qsizetype(maxPropertyKeyframes), frames.size()));
    for (const auto frame : frames) {
        if (samples.size() >= maxPropertyKeyframes) break;
        if (frame < 0 || frame >= shot.durationFrames) continue;
        const auto position = worldPosition(ancestry, pivot, frame);
        if (std::isfinite(position.x()) && std::isfinite(position.y())) samples.append({qreal(frame), position, true});
    }
    return samples;
}
MotionPathGeometry buildMotionPath(const Shot &shot, const QString &layerId, QSizeF fallbackSize, int maximumSamples) {
    MotionPathGeometry result;
    const auto *source = findLayer(shot, layerId);
    if (!source || shot.durationFrames < 1 || maximumSamples < 1) return result;
    // The public limit also has a hard ceiling, so malformed callers cannot
    // allocate a point per frame for a million-frame shot.
    const int budget = std::min(maximumSamples, maxPropertyKeyframes);
    const qreal finalFrame = shot.durationFrames - 1;
    const QPointF pivot = effectiveAnimationPivot(shot, layerId, fallbackSize);
    const auto ancestry = motionAncestry(shot, source);
    std::set<qreal> events{0, finalFrame}, jumps;
    for (int level = 0; level < ancestry.size(); ++level) {
        for (int property = 0; property < int(ancestry[level].channels.size()); ++property) {
            const auto *channel = ancestry[level].channels[property];
            if (!channel) continue;
            // A layer's own pivot is unaffected by its own rotation and
            // scale. Every parent motion channel can change its trajectory.
            if (level == 0 && property != int(KeyframeProperty::PositionX)
                && property != int(KeyframeProperty::PositionY)) continue;
            for (int i = 0; i < channel->keys.size(); ++i) {
                const auto &key = channel->keys[i];
                if (key.frame >= 0 && key.frame <= finalFrame) events.insert(key.frame);
                if (i > 0 && channel->keys[i - 1].interpolation == KeyframeInterpolation::Hold
                    && !channel->keys[i - 1].easing && channel->keys[i - 1].value != key.value && key.frame > 0 && key.frame <= finalFrame) {
                    jumps.insert(key.frame);
                    events.insert(std::nextafter(qreal(key.frame), -std::numeric_limits<qreal>::infinity()));
                }
            }
        }
    }
    QVector<qreal> times;
    if (budget == 1 || finalFrame == 0) times.append(0);
    else if (int(events.size()) > budget) {
        // Keep exact event times where possible. If there are more keys than
        // samples, key markers remain exact through motionPositionKeyFrames().
        const QVector<qreal> candidates(events.begin(), events.end());
        for (int i = 0; i < budget; ++i) {
            const auto index = qsizetype(qint64(i) * (candidates.size() - 1) / (budget - 1));
            times.append(candidates[index]);
        }
    } else {
        const int remaining = budget - int(events.size());
        for (int i = 1; i <= remaining; ++i) events.insert(finalFrame * i / (remaining + 1));
        times = QVector<qreal>(events.begin(), events.end());
    }
    result.samples.reserve(times.size());
    bool first = true;
    qreal previousFrame = 0;
    for (const qreal frame : times) {
        const QPointF position = worldPosition(ancestry, pivot, frame);
        if (!std::isfinite(position.x()) || !std::isfinite(position.y())) { first = true; continue; }
        const auto jump = jumps.upper_bound(previousFrame);
        const bool moveTo = first || (jump != jumps.end() && *jump <= frame);
        result.samples.append({frame, position, moveTo});
        if (moveTo) result.path.moveTo(position); else result.path.lineTo(position);
        previousFrame = frame;
        first = false;
    }
    return result;
}
} // namespace toon
