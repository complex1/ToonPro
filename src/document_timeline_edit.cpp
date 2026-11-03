#include "document.h"
#include "symbol_poses.h"
#include <QScopedValueRollback>
#include <algorithm>
#include <limits>
#include <map>
#include <tuple>

namespace toon {
namespace {
using ChannelId = std::tuple<int, QString, QString, int>;
using SelectedFrames = std::map<ChannelId, QSet<int>>;
constexpr int maxTimelineKeys = maxPropertyKeyframes + maxCameraKeyframes + maxProjectSymbolPoseKeys;

ChannelId channelId(const TimelineKeyRef &key) {
    return {int(key.kind), key.layerId, key.objectId, (key.isCamera() || key.isSymbolPose() || key.isPath()) ? -1 : int(key.property)};
}
int layerIndex(const Shot &shot, const QString &id) {
    for (int i = 0; i < shot.layers.size(); ++i) if (shot.layers[i].id == id) return i;
    return -1;
}
QString ready(const Document *document) {
    if (!document->isOpen()) return "Open a project before editing the timeline.";
    if (document->editingGestureActive() || document->layerPropertyEditActive())
        return "Finish the current edit before changing timeline timing.";
    return {};
}
QString unlocked(const Shot &shot, const QString &id, bool position) {
    const int target = layerIndex(shot, id);
    if (target < 0) return "This timeline layer no longer exists.";
    QSet<QString> visited;
    for (int index = target; index >= 0;) {
        const auto &layer = shot.layers[index];
        if (visited.contains(layer.id)) return "This layer has invalid folder ancestry.";
        visited.insert(layer.id);
        if (layer.locked) return "Unlock the layer and its folders before editing timeline keys.";
        index = layer.parentId.isEmpty() ? -1 : layerIndex(shot, layer.parentId);
    }
    if (position && layerPositionLocked(shot.layers, target))
        return "Unlock the layer or folder position before changing motion or path timing.";
    return {};
}
QString validReference(const Shot &shot, const TimelineKeyRef &key, bool editing, bool sourceInsideShot = true) {
    if (key.frame < 0 || (sourceInsideShot && key.frame >= shot.durationFrames)) return "This timeline key is outside the shot.";
    if (key.isCamera()) {
        if (!key.objectId.isEmpty() || (key.kind == TimelineKeyKind::Camera2D && !key.layerId.isEmpty())) return "This camera key reference is invalid.";
        return key.kind == TimelineKeyKind::Camera3D ? sceneCameraEditError(shot, key.layerId, editing) : QString();
    }
    if (key.isSymbolPose()) {
        const int index = layerIndex(shot,key.layerId);
        if (!key.objectId.isEmpty() || index < 0 || !shot.layers[index].symbol)
            return "This Symbol pose-key reference is invalid.";
        return editing ? symbolPoseEditError(shot,key.layerId) : QString();
    }
    if (key.kind != TimelineKeyKind::Layer) return "This timeline key kind is unsupported.";
    const auto reason = key.isPath() ? propertyAnimationUnavailableReason(shot, key.layerId)
        : keyframePropertyUnavailableReason(shot, key.layerId, key.property);
    if (!reason.isEmpty()) return reason;
    if ((!key.isPath() && keyframePropertyKey(key.property).isEmpty()) || key.frame < 0
        || (sourceInsideShot && key.frame >= shot.durationFrames))
        return "This timeline key is invalid or outside the shot.";
    if (editing) return unlocked(shot, key.layerId, key.isPath() || keyframePropertyNeedsPositionUnlock(key.property));
    return {};
}
PropertyChannel *mutableChannel(Shot &shot, const QString &id, KeyframeProperty property) {
    if (auto *animation = layerAnimation(shot, id))
        for (auto &channel : animation->channels) if (channel.property == property) return &channel;
    return nullptr;
}
PathAnimation *mutablePath(Shot &shot, const QString &id, const QString &objectId) {
    if (auto *animation = layerAnimation(shot, id))
        for (auto &path : animation->paths) if (path.objectId == objectId) return &path;
    return nullptr;
}
template<class Key> const Key *keyAt(const QVector<Key> &keys, int frame) {
    const auto it = std::lower_bound(keys.cbegin(), keys.cend(), frame,
                                    [](const auto &key, int at) { return key.frame < at; });
    return it != keys.cend() && it->frame == frame ? &*it : nullptr;
}
QString collectKeys(const Shot &shot, const QVector<TimelineKeyRef> &refs, bool editing,
                    SelectedFrames *selected, TimelineKeyClipboard *snapshot = nullptr) {
    if (refs.isEmpty()) return "Select at least one timeline key.";
    if (refs.size() > maxTimelineKeys) return "Too many timeline keys are selected.";
    auto expanded=refs;
    for(const auto &ref:refs) if(ref.kind == TimelineKeyKind::Layer && !ref.isPath()
        && (ref.property==KeyframeProperty::PositionX || ref.property==KeyframeProperty::PositionY)) {
        const auto *animation=layerAnimation(shot,ref.layerId);
        if(animation && hasSpatialMotion(*animation)) {
            auto paired=ref;paired.property=ref.property==KeyframeProperty::PositionX?KeyframeProperty::PositionY:KeyframeProperty::PositionX;
            if(!expanded.contains(paired))expanded.append(paired);
        }
    }
    if(expanded.size()>maxTimelineKeys)return "Too many timeline keys are selected.";
    for (const auto &ref : expanded) {
        const auto reason = validReference(shot, ref, editing);
        if (!reason.isEmpty()) return reason;
        auto &frames = (*selected)[channelId(ref)];
        if (frames.contains(ref.frame)) continue;
        TimelineKeyValue value;
        value.source = ref;
        if (ref.kind == TimelineKeyKind::Camera2D) {
            const auto *key = keyAt(shot.camera2DKeys, ref.frame);
            if (!key) return "A selected shot camera key no longer exists.";
            value.camera2D = *key;
        } else if (ref.kind == TimelineKeyKind::Camera3D) {
            const auto *animation = sceneCameraAnimation(shot, ref.layerId);
            const auto *key = animation ? keyAt(animation->keys, ref.frame) : nullptr;
            if (!key) return "A selected 3D camera key no longer exists.";
            value.camera3D = *key;
        } else if (ref.isSymbolPose()) {
            const auto &instance = *shot.layers[layerIndex(shot,ref.layerId)].symbol;
            const auto *key = symbolPoseKeyAtParent(instance,ref.frame);
            if (!key) return "A selected Symbol pose key no longer exists.";
            value.symbolPose = *key; value.symbolPose->frame = ref.frame;
            value.symbolDefinitionId = instance.symbolId;
        } else if (ref.isPath()) {
            const auto *path = pathAnimation(shot, ref.layerId, ref.objectId);
            const auto *key = path ? keyAt(path->keys, ref.frame) : nullptr;
            if (!key) return "A selected path key no longer exists.";
            value.path = *key;
        } else {
            const auto *channel = propertyChannel(shot, ref.layerId, ref.property);
            const auto *key = channel ? keyAt(channel->keys, ref.frame) : nullptr;
            if (!key) return "A selected property key no longer exists.";
            value.scalar = *key;
        }
        frames.insert(ref.frame);
        if (snapshot) snapshot->keys.append(std::move(value));
    }
    return {};
}
template<class Key> bool moveKeys(QVector<Key> &keys, const QSet<int> &selected, int delta, int duration) {
    // Check against the original set before moving anything. Selected keys may
    // land in slots vacated by other selected keys in this same gesture.
    for (const int frame : selected) {
        const qint64 destination = qint64(frame) + delta;
        if (destination < 0 || destination >= duration) return false;
        if (!selected.contains(int(destination)) && keyAt(keys, int(destination))) return false;
    }
    for (auto &key : keys) if (selected.contains(key.frame)) key.frame += delta;
    std::sort(keys.begin(), keys.end(), [](const auto &a, const auto &b) { return a.frame < b.frame; });
    return true;
}
bool movePoseKeys(SymbolInstance &instance, const QSet<int> &parents, int delta, int duration) {
    for (int frame : parents) {
        const qint64 target = qint64(frame) + delta;
        if (target < 0 || target >= duration || (!parents.contains(int(target)) && symbolPoseKeyAtParent(instance,int(target)))) return false;
        const int clock = symbolPoseClockFrame(instance,int(target));
        if (clock < minSymbolPoseClock || clock > maxSymbolPoseClock) return false;
    }
    for (auto &key : instance.poseKeys) if (parents.contains(symbolPoseParentFrame(instance,key.frame))) key.frame += delta;
    std::sort(instance.poseKeys.begin(),instance.poseKeys.end(),[](const auto &a,const auto &b) { return a.frame < b.frame; });
    return true;
}
void cleanChannels(Shot &shot) {
    for (auto &animation : shot.layerAnimations) {
        animation.channels.removeIf([](const auto &channel) { return channel.keys.isEmpty(); });
        animation.paths.removeIf([](const auto &path) { return path.keys.isEmpty(); });
    }
    prunePropertyAnimation(shot);
    pruneCameraAnimation(shot);
}
QString validateTimeline(const Shot &shot) {
    const auto properties = validatePropertyAnimation(shot);
    return properties.isEmpty() ? validateCameraAnimation(shot) : properties;
}
LayerAnimation &ensureAnimation(const Project &project, Shot &shot, const QString &id, int frame) {
    if (auto *animation = layerAnimation(shot, id)) return *animation;
    shot.layerAnimations.append({id, defaultAnimationPivot(project, shot, id, frame), {}, {}});
    return shot.layerAnimations.last();
}
} // namespace

bool Document::moveTimelineKeys(const QVector<TimelineKeyRef> &keys, int deltaFrames) {
    const auto unavailable = ready(this);
    if (!unavailable.isEmpty()) { emit warning(unavailable); return false; }
    SelectedFrames selected;
    const auto reason = collectKeys(m_project.shots[activeShotIndex()], keys, true, &selected);
    if (!reason.isEmpty()) { emit warning(reason); return false; }
    if (!deltaFrames) return true;
    auto project = m_project;
    auto &shot = project.shots[activeShotIndex()];
    for (const auto &[channel, frames] : selected) {
        const auto &[kind, id, objectId, property] = channel;
        const bool moved = kind == int(TimelineKeyKind::Camera2D)
            ? moveKeys(shot.camera2DKeys, frames, deltaFrames, shot.durationFrames)
            : kind == int(TimelineKeyKind::Camera3D)
            ? moveKeys(sceneCameraAnimation(shot, id)->keys, frames, deltaFrames, shot.durationFrames)
            : kind == int(TimelineKeyKind::SymbolPose)
            ? movePoseKeys(*shot.layers[layerIndex(shot,id)].symbol,frames,deltaFrames,shot.durationFrames)
            : objectId.isEmpty()
            ? moveKeys(mutableChannel(shot, id, KeyframeProperty(property))->keys, frames, deltaFrames, shot.durationFrames)
            : moveKeys(mutablePath(shot, id, objectId)->keys, frames, deltaFrames, shot.durationFrames);
        if (!moved) { emit warning("Move keys to empty frames inside the shot. Existing keys will not be replaced."); return false; }
    }
    const auto error = validateTimeline(shot);
    if (!error.isEmpty()) { emit warning(error); return false; }
    return commit(m_project, project, "Move timeline keys");
}

bool Document::removeTimelineKeys(const QVector<TimelineKeyRef> &keys) {
    const auto unavailable = ready(this);
    if (!unavailable.isEmpty()) { emit warning(unavailable); return false; }
    SelectedFrames selected;
    const auto reason = collectKeys(m_project.shots[activeShotIndex()], keys, true, &selected);
    if (!reason.isEmpty()) { emit warning(reason); return false; }
    auto project = m_project;
    auto &shot = project.shots[activeShotIndex()];
    for (const auto &[channel, frames] : selected) {
        const auto &[kind, id, objectId, property] = channel;
        if (kind == int(TimelineKeyKind::Camera2D)) {
            const auto pose = evaluateShotCamera(shot, m_currentFrame);
            shot.camera2DKeys.removeIf([&](const auto &key) { return frames.contains(key.frame); });
            if (shot.camera2DKeys.isEmpty()) shot.camera2D = pose;
        } else if (kind == int(TimelineKeyKind::Camera3D)) {
            const auto pose = evaluateSceneCamera(shot, id, m_currentFrame);
            auto *animation = sceneCameraAnimation(shot, id);
            animation->keys.removeIf([&](const auto &key) { return frames.contains(key.frame); });
            if (animation->keys.isEmpty()) for (auto &layer : shot.layers) if (layer.id == id) layer.scene3d->camera = pose;
        } else if (kind == int(TimelineKeyKind::SymbolPose)) {
            auto &instance = *shot.layers[layerIndex(shot,id)].symbol;
            instance.poseKeys.removeIf([&](const auto &key) { return frames.contains(symbolPoseParentFrame(instance,key.frame)); });
        } else if (objectId.isEmpty()) mutableChannel(shot, id, KeyframeProperty(property))->keys.removeIf([&](const auto &key) { return frames.contains(key.frame); });
        else mutablePath(shot, id, objectId)->keys.removeIf([&](const auto &key) { return frames.contains(key.frame); });
    }
    cleanChannels(shot);
    const auto error = validateTimeline(shot);
    if (!error.isEmpty()) { emit warning(error); return false; }
    return commit(m_project, project, "Delete timeline keys");
}

bool Document::copyTimelineKeys(const QVector<TimelineKeyRef> &keys, TimelineKeyClipboard *out) {
    if (!out) return false;
    const auto unavailable = ready(this);
    if (!unavailable.isEmpty()) { emit warning(unavailable); return false; }
    TimelineKeyClipboard snapshot;
    snapshot.shotId = m_project.shots[activeShotIndex()].id;
    SelectedFrames selected;
    const auto reason = collectKeys(m_project.shots[activeShotIndex()], keys, false, &selected, &snapshot);
    if (!reason.isEmpty()) { emit warning(reason); return false; }
    *out = std::move(snapshot);
    return true;
}

bool Document::pasteTimelineKeys(const TimelineKeyClipboard &clipboard, int atFrame) {
    const auto unavailable = ready(this);
    if (!unavailable.isEmpty()) { emit warning(unavailable); return false; }
    const auto &original = m_project.shots[activeShotIndex()];
    if (clipboard.shotId != original.id) { emit warning("Copied timeline keys belong to another shot."); return false; }
    if (clipboard.keys.isEmpty() || clipboard.keys.size() > maxTimelineKeys) {
        emit warning("Copy a valid selection of timeline keys first."); return false;
    }
    if (atFrame < 0 || atFrame >= original.durationFrames) { emit warning("Paste keys at a frame inside the shot."); return false; }
    int firstFrame = std::numeric_limits<int>::max();
    for (const auto &value : clipboard.keys) firstFrame = qMin(firstFrame, value.source.frame);
    auto project = m_project;
    auto &shot = project.shots[activeShotIndex()];
    for (const auto &value : clipboard.keys) {
        const auto &ref = value.source;
        // The snapshot remains usable after shortening the shot. Its stored
        // source times determine spacing; only destinations use today's bounds.
        const auto reason = validReference(shot, ref, true, false);
        if (!reason.isEmpty()) { emit warning(reason); return false; }
        const bool is2D = ref.kind == TimelineKeyKind::Camera2D, is3D = ref.kind == TimelineKeyKind::Camera3D, isPose = ref.isSymbolPose();
        if (ref.isPath() != value.path.has_value() || is2D != value.camera2D.has_value() || is3D != value.camera3D.has_value()
            || isPose != value.symbolPose.has_value() || (!isPose && !value.symbolDefinitionId.isEmpty())
            || (is2D ? value.camera2D->frame : is3D ? value.camera3D->frame : isPose ? value.symbolPose->frame : ref.isPath() ? value.path->frame : value.scalar.frame) != ref.frame) {
            emit warning("The copied timeline key data is invalid."); return false;
        }
        const qint64 destination = qint64(atFrame) + ref.frame - firstFrame;
        if (destination < 0 || destination >= shot.durationFrames) { emit warning("The pasted keys would extend outside the shot."); return false; }
        if (is2D) {
            if (keyAt(shot.camera2DKeys, int(destination))) { emit warning("Pasted camera keys overlap existing keys."); return false; }
            auto key = *value.camera2D; key.frame = int(destination);
            const auto at = std::lower_bound(shot.camera2DKeys.begin(), shot.camera2DKeys.end(), key.frame, [](const auto &item, int frame) { return item.frame < frame; });
            shot.camera2DKeys.insert(at, key);
        } else if (is3D) {
            auto *animation = sceneCameraAnimation(shot, ref.layerId);
            if (!animation) { shot.sceneCameraAnimations.append({ref.layerId, {}}); animation = &shot.sceneCameraAnimations.last(); }
            if (keyAt(animation->keys, int(destination))) { emit warning("Pasted camera keys overlap existing keys."); return false; }
            auto key = *value.camera3D; key.frame = int(destination);
            const auto at = std::lower_bound(animation->keys.begin(), animation->keys.end(), key.frame, [](const auto &item, int frame) { return item.frame < frame; });
            animation->keys.insert(at, key);
        } else if (isPose) {
            auto &instance = *shot.layers[layerIndex(shot,ref.layerId)].symbol;
            if (instance.symbolId != value.symbolDefinitionId) { emit warning("The copied pose belongs to a different source Symbol."); return false; }
            const int clock = symbolPoseClockFrame(instance,int(destination));
            if (symbolPoseKeyAtParent(instance,int(destination))) { emit warning("Pasted Symbol poses overlap existing keys."); return false; }
            auto key = *value.symbolPose; key.frame = clock;
            const auto at = std::lower_bound(instance.poseKeys.begin(),instance.poseKeys.end(),clock,[](const auto &item,int frame) { return item.frame < frame; });
            instance.poseKeys.insert(at,key); instance.playback = SymbolPlayback::Poses;
        } else if (ref.isPath()) {
            auto &animation = ensureAnimation(project, shot, ref.layerId, int(destination));
            auto *path = mutablePath(shot, ref.layerId, ref.objectId);
            if (!path) { animation.paths.append({ref.objectId, {}}); path = &animation.paths.last(); }
            if (keyAt(path->keys, int(destination))) { emit warning("Pasted keys overlap existing keys. Choose an empty frame range."); return false; }
            auto key = *value.path;
            key.frame = int(destination);
            const auto at = std::lower_bound(path->keys.begin(), path->keys.end(), key.frame, [](const auto &item, int frame) { return item.frame < frame; });
            path->keys.insert(at, std::move(key));
        } else {
            auto &animation = ensureAnimation(project, shot, ref.layerId, int(destination));
            auto *channel = mutableChannel(shot, ref.layerId, ref.property);
            if (!channel) { animation.channels.append({ref.property, {}}); channel = &animation.channels.last(); }
            if (keyAt(channel->keys, int(destination))) { emit warning("Pasted keys overlap existing keys. Choose an empty frame range."); return false; }
            auto key = value.scalar;
            key.frame = int(destination);
            const auto at = std::lower_bound(channel->keys.begin(), channel->keys.end(), key.frame, [](const auto &item, int frame) { return item.frame < frame; });
            channel->keys.insert(at, std::move(key));
        }
    }
    const auto error = validateTimeline(shot);
    if (!error.isEmpty()) { emit warning(error); return false; }
    return commit(m_project, project, "Paste timeline keys");
}

bool Document::setAnimationExposureTimingById(const QString &trackId, const QString &exposureId,
                                            int startFrame, int durationFrames) {
    const auto unavailable = ready(this);
    if (!unavailable.isEmpty()) { emit warning(unavailable); return false; }
    auto project = m_project;
    auto &shot = project.shots[activeShotIndex()];
    AnimationTrack *track = nullptr;
    for (auto &candidate : shot.animationTracks) if (candidate.id == trackId) { track = &candidate; break; }
    if (!track) { emit warning("This animation track no longer exists."); return false; }
    CelExposure *exposure = nullptr;
    for (auto &candidate : track->exposures) if (candidate.id == exposureId) { exposure = &candidate; break; }
    if (!exposure) { emit warning("This drawing exposure no longer exists."); return false; }
    QString groupId;
    for (const auto &drawing : track->drawings) if (drawing.id == exposure->drawingId) groupId = drawing.groupLayerId;
    auto reason = unlocked(shot, track->rootGroupLayerId, true);
    if (reason.isEmpty()) reason = unlocked(shot, groupId, true);
    if (!reason.isEmpty()) { emit warning(reason); return false; }
    const qint64 end = qint64(startFrame) + durationFrames;
    if (startFrame < 0 || durationFrames < 1 || end > shot.durationFrames) {
        emit warning("A drawing exposure needs at least one frame and must stay inside the shot."); return false;
    }
    if (startFrame == exposure->startFrame && durationFrames == exposure->durationFrames) return true;
    for (const auto &other : track->exposures) if (other.id != exposureId
        && startFrame < qint64(other.startFrame) + other.durationFrames && end > other.startFrame) {
        emit warning("Drawing exposures cannot overlap. Move or trim into empty frames."); return false;
    }
    exposure->startFrame = startFrame;
    exposure->durationFrames = durationFrames;
    std::sort(track->exposures.begin(), track->exposures.end(), [](const auto &a, const auto &b) { return a.startFrame < b.startFrame; });
    const auto error = project.validate();
    if (!error.isEmpty()) { emit warning(error); return false; }
    QScopedValueRollback<bool> animationMutation(m_animationMutation, true);
    return commit(m_project, project, "Change drawing exposure");
}
} // namespace toon
