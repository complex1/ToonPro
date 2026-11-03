#include "timeline_ripple.h"
#include "project.h"
#include "symbol_poses.h"
#include <QUuid>
#include <algorithm>
#include <cmath>
#include <map>

namespace toon {
namespace {
constexpr int maxShotFrames = 1000000;
struct Segment { int first, end, offset; };
struct Plan { QVector<Segment> segments; int duration = 0; bool identity = false; };

QString makePlan(int duration, const TimelineRippleEdit &edit, Plan *plan) {
    if (duration < 1 || duration > maxShotFrames) return "The shot duration is invalid.";
    if (edit.startFrame < 0 || edit.durationFrames < 1) return "Choose a valid start frame and at least one frame.";
    const qint64 start = edit.startFrame, count = edit.durationFrames, end = start + count;
    plan->duration = duration;
    auto append = [&](qint64 first, qint64 last, qint64 offset) {
        if (last > first) plan->segments.append({int(first), int(last), int(offset)});
    };
    switch (edit.kind) {
    case TimelineRippleKind::Insert:
        if (start > duration || qint64(duration) + count > maxShotFrames)
            return "Inserted time must stay within the 1,000,000-frame shot limit.";
        plan->duration = int(duration + count);
        append(0, start, 0); append(start, duration, count);
        break;
    case TimelineRippleKind::Delete:
        if (end > duration || count >= duration) return "Delete a range inside the shot and keep at least one frame.";
        plan->duration = int(duration - count);
        append(0, start, 0); append(end, duration, -count);
        break;
    case TimelineRippleKind::Move: {
        const qint64 destination = edit.destinationFrame;
        if (end > duration || destination < 0 || destination > duration)
            return "Move a range and destination inside the shot.";
        if (destination > start && destination < end) return "Choose a destination outside the moved range.";
        if (destination == start || destination == end) {
            append(0, duration, 0); plan->identity = true;
        } else if (destination < start) {
            append(0, destination, 0); append(destination, start, count);
            append(start, end, destination - start); append(end, duration, 0);
        } else {
            append(0, start, 0); append(start, end, destination - end);
            append(end, destination, -count); append(destination, duration, 0);
        }
        break;
    }
    default: return "This ripple operation is unsupported.";
    }
    return {};
}

QString newId() { return QUuid::createUuid().toString(QUuid::WithoutBraces); }
int layerIndex(const Shot &shot, const QString &id) {
    for (int i = 0; i < shot.layers.size(); ++i) if (shot.layers[i].id == id) return i;
    return -1;
}
QString unlocked(const Shot &shot, const QString &id, bool descendants = true) {
    const int target = layerIndex(shot, id);
    if (target < 0) return "A timeline layer no longer exists.";
    QSet<QString> visited;
    for (int index = target; index >= 0;) {
        const auto &layer = shot.layers[index];
        if (visited.contains(layer.id)) return "This layer has invalid folder ancestry.";
        visited.insert(layer.id);
        if (layer.locked || layer.positionLocked) return "Unlock affected layers and folders before ripple editing.";
        index = layer.parentId.isEmpty() ? -1 : layerIndex(shot, layer.parentId);
    }
    if (descendants && shot.layers[target].group) for (int i = 0; i < shot.layers.size(); ++i)
        if (layerIsDescendant(shot.layers, i, id) && (shot.layers[i].locked || shot.layers[i].positionLocked))
            return "Unlock affected drawing layers before ripple editing.";
    return {};
}
template<class Key> QVector<Key> retimeKeys(const QVector<Key> &source, const Plan &plan) {
    QVector<Key> result;
    result.reserve(source.size());
    for (const auto &key : source) for (const auto &segment : plan.segments)
        if (key.frame >= segment.first && key.frame < segment.end) {
            auto moved = key; moved.frame += segment.offset; result.append(std::move(moved)); break;
        }
    std::sort(result.begin(), result.end(), [](const auto &a, const auto &b) { return a.frame < b.frame; });
    return result;
}
QString validateTimeline(const Shot &shot) {
    for (const auto &error : {validateAnimation(shot), validatePropertyAnimation(shot),
                              validateCameraAnimation(shot), validateAudioClips(shot), validateTimelineCues(shot)})
        if (!error.isEmpty()) return error;
    for (const auto &layer : shot.layers) if (layer.symbol) {
        const auto error = validateSymbolPoses(*layer.symbol,1000000);
        if (!error.isEmpty()) return error;
    }
    return {};
}

QString retimeCues(const Shot &source, Shot &next, const Plan &plan, const TimelineRippleEdit &edit) {
    next.timelineCues.clear();
    for (const auto &cue : source.timelineCues) {
        QVector<QPair<int, int>> pieces;
        const int oldEnd = cue.startFrame + cue.durationFrames;
        if (edit.kind == TimelineRippleKind::Insert) {
            // Inserting within a note extends its span; inserting at its start
            // moves the entire note, while inserting at its end leaves it alone.
            const int first = cue.startFrame + (cue.startFrame >= edit.startFrame ? edit.durationFrames : 0);
            const int end = oldEnd + (oldEnd > edit.startFrame ? edit.durationFrames : 0);
            pieces.append({first, end});
        } else {
            for (const auto &segment : plan.segments) {
                const int first = qMax(cue.startFrame, segment.first), end = qMin(oldEnd, segment.end);
                if (end > first) pieces.append({first + segment.offset, end + segment.offset});
            }
            std::sort(pieces.begin(), pieces.end());
        }
        QVector<QPair<int, int>> joined;
        for (const auto &piece : pieces) {
            if (!joined.isEmpty() && piece.first <= joined.last().second)
                joined.last().second = qMax(joined.last().second, piece.second);
            else joined.append(piece);
        }
        bool first = true;
        for (const auto &piece : joined) {
            if (next.timelineCues.size() >= maxShotTimelineCues) return "This edit would exceed the shot's 1024 timeline-cue limit.";
            auto mapped = cue;
            if (!first) mapped.id = newId();
            first = false; mapped.startFrame = piece.first; mapped.durationFrames = piece.second - piece.first;
            next.timelineCues.append(std::move(mapped));
        }
    }
    return {};
}

// Finite clips preserve both their visibility window and every surviving
// exposed frame's clock. A single clip cannot encode two distinct clocks.
QString retimeFiniteSymbol(const Shot &source, RasterLayer &layer, const SymbolInstance &original,
                           const QVector<Segment> &intervals, const Plan &plan) {
    const qint64 originalEnd = qint64(original.startFrame) + original.durationFrames;
    auto changed = original;
    const auto mappedAnchor = [&]() {
        if (original.startFrame >= source.durationFrames)
            return original.startFrame + plan.duration - source.durationFrames;
        for (const auto &segment : plan.segments)
            if (original.startFrame >= segment.first && original.startFrame < segment.end)
                return original.startFrame + segment.offset;
        // The anchor was deleted: retain the empty clip at the cut boundary.
        for (const auto &segment : plan.segments)
            if (segment.first >= original.startFrame) return segment.first + segment.offset;
        return plan.duration;
    };
    QVector<Segment> pieces;
    for (const auto &segment : plan.segments) {
        const int first = qMax(original.startFrame, segment.first);
        const int end = int(qMin<qint64>(originalEnd, segment.end));
        if (end > first) pieces.append({first + segment.offset, end + segment.offset, segment.offset});
    }
    const auto reject = [&]() {
        return QString("This edit would split or reorder playback inside Symbol \"%1\". "
                       "Move its complete clip or edit time outside its playback.").arg(layer.name);
    };
    if (pieces.isEmpty()) {
        changed.startFrame = mappedAnchor();
        // A genuinely future clip keeps its full duration. A deleted clip is
        // retained empty with its source phase, ready to expose again later.
        if (original.startFrame < source.durationFrames) changed.durationFrames = 0;
    } else {
        int first = plan.duration, end = 0, fallbackOffset = 0;
        for (const auto &piece : pieces) {
            if (piece.first < first) { first = piece.first; fallbackOffset = piece.offset; }
            end = qMax(end, piece.end);
        }
        // A definition can have been shortened after a clip was authored.
        // Preserve any dormant tail beyond its parent instead of silently
        // trimming that saved source window during an unrelated ripple edit.
        if (originalEnd > source.durationFrames)
            end = int(qint64(plan.duration) + originalEnd - source.durationFrames);
        std::optional<int> clockOffset;
        for (const auto &interval : intervals) for (const auto &segment : plan.segments) {
            const int a = qMax(interval.first, segment.first), b = qMin(interval.end, segment.end);
            if (b <= a) continue;
            // Previously hidden surviving frames must remain hidden. This
            // detects moved/disjoint pieces whose envelope would expose a gap.
            const auto intersects = [&](int from, int to) {
                return to > from && qMax(from + segment.offset, first) < qMin(to + segment.offset, end);
            };
            if (intersects(a, qMin(b, original.startFrame))
                || intersects(int(qMax<qint64>(a, originalEnd)), b)) return reject();
            const int activeFirst = qMax(a, original.startFrame);
            const int activeEnd = int(qMin<qint64>(b, originalEnd));
            if (activeEnd <= activeFirst) continue;
            if (original.playback != SymbolPlayback::SingleFrame && original.playback != SymbolPlayback::Poses && clockOffset && *clockOffset != segment.offset)
                return reject();
            clockOffset = segment.offset;
        }
        changed.startFrame = first;
        changed.durationFrames = end - first;
        const qint64 offset = qint64(original.playbackOffset) + first - original.startFrame
            - clockOffset.value_or(fallbackOffset);
        if (offset < -maxShotFrames || offset > maxShotFrames) return reject();
        changed.playbackOffset = int(offset);
    }
    if (changed.startFrame < 0 || changed.startFrame > maxShotFrames
        || changed.durationFrames < 0 || changed.durationFrames > maxShotFrames)
        return QString("The timing of Symbol \"%1\" would exceed the 1,000,000-frame limit.").arg(layer.name);
    if (changed != original) {
        const auto error = unlocked(source, layer.id, false);
        if (!error.isEmpty()) return error;
        layer.symbol = changed;
    }
    return {};
}

// Preserve held poses on every surviving parent frame. A cut can remove the
// key that established a hold, so segment boundaries need explicit samples.
QString retimePoseKeys(const Shot &source, RasterLayer &layer, const SymbolInstance &original, const Plan &plan) {
    if (original.poseKeys.isEmpty() && original.playback != SymbolPlayback::Poses) return {};
    auto changed = *layer.symbol;
    std::map<int,int> poses;
    const auto fail = [&] { return QString("The pose keys of Symbol \"%1\" would exceed their timing or count limits.").arg(layer.name); };
    const auto insert = [&](int parent,int pose) {
        const int clock = symbolPoseClockFrame(changed,parent);
        if (clock < minSymbolPoseClock || clock > maxSymbolPoseClock) return false;
        const auto found = poses.find(clock);
        if (found != poses.end() && found->second != pose) return false;
        poses[clock] = pose; return true;
    };
    for (const auto &key : original.poseKeys) {
        const int parent = symbolPoseParentFrame(original,key.frame);
        if (parent < 0) { if (!insert(parent,key.poseFrame)) return fail(); }
        else if (parent >= source.durationFrames) {
            if (!insert(parent + plan.duration - source.durationFrames,key.poseFrame)) return fail();
        } else for (const auto &segment : plan.segments) if (parent >= segment.first && parent < segment.end) {
            if (!insert(parent + segment.offset,key.poseFrame)) return fail(); break;
        }
    }
    if (original.playback == SymbolPlayback::Poses) {
        std::map<int,int> seams;
        for (const auto &segment : plan.segments)
            seams[symbolPoseClockFrame(changed,segment.first + segment.offset)] = evaluatedSymbolPose(original,segment.first);
        for (const auto &[clock,pose] : seams) {
            if (clock < minSymbolPoseClock || clock > maxSymbolPoseClock) return fail();
            const auto after = poses.upper_bound(clock);
            const int current = after == poses.begin() ? changed.firstFrame : std::prev(after)->second;
            if (current != pose) poses[clock] = pose;
        }
    }
    if (poses.size() > maxSymbolPoseKeys) return fail();
    changed.poseKeys.clear();
    for (const auto &[clock,pose] : poses) changed.poseKeys.append({clock,pose});
    if (changed != *layer.symbol) {
        const auto error = unlocked(source,layer.id,false); if (!error.isEmpty()) return error;
        layer.symbol = std::move(changed);
    }
    return {};
}

// Legacy through-shot Symbols retain their previous retiming behavior.
// Preserve that clock on every surviving frame rather than silently shifting
// only the first key. Exposed Symbols use their drawing's intervals: a gap or
// an edit outside those intervals must not unnecessarily block the operation.
QString retimeSymbols(const Shot &source, Shot &next, const Plan &plan) {
    const auto ownership = animationOwners(source);
    QSet<QString> exposedDrawings;
    for (const auto &track : next.animationTracks) for (const auto &exposure : track.exposures)
        exposedDrawings.insert(exposure.drawingId);
    for (auto &layer : next.layers) {
        if (!layer.symbol) continue;
        const auto originalIndex = layerIndex(source, layer.id);
        if (originalIndex < 0 || !source.layers[originalIndex].symbol) continue;
        const auto &original = *source.layers[originalIndex].symbol;
        QVector<Segment> intervals;
        const auto owner = ownership.value(layer.id);
        // An unused library drawing is already invisible. Keep its source
        // clock usable for a later exposure instead of hiding it at shot end.
        if (!owner.drawingId.isEmpty() && !exposedDrawings.contains(owner.drawingId)) continue;
        if (owner.drawingId.isEmpty()) intervals.append({0, source.durationFrames, 0});
        else for (const auto &track : source.animationTracks) if (track.id == owner.trackId)
            for (const auto &exposure : track.exposures) if (exposure.drawingId == owner.drawingId)
                intervals.append({exposure.startFrame, exposure.startFrame + exposure.durationFrames, 0});
        if (intervals.isEmpty()) continue;

        if (original.durationFrames >= 0) {
            const auto error = retimeFiniteSymbol(source, layer, original, intervals, plan);
            if (!error.isEmpty()) return error;
            const auto poseError = retimePoseKeys(source,layer,original,plan);
            if (!poseError.isEmpty()) return poseError;
            continue;
        }

        std::optional<int> clockOffset;
        int firstActiveFrame = plan.duration, lastInactiveEnd = 0;
        bool differentOffsets = false, hadActiveFrames = false;
        for (const auto &interval : intervals)
            hadActiveFrames |= interval.end > qMax(interval.first, original.startFrame);
        for (const auto &interval : intervals) for (const auto &segment : plan.segments) {
            const int first = qMax(interval.first, segment.first), end = qMin(interval.end, segment.end);
            if (end <= first) continue;
            const int inactiveEnd = qMin(end, original.startFrame);
            if (inactiveEnd > first) lastInactiveEnd = qMax(lastInactiveEnd, inactiveEnd + segment.offset);
            const int activeFirst = qMax(first, original.startFrame);
            if (activeFirst >= end) continue;
            firstActiveFrame = qMin(firstActiveFrame, activeFirst + segment.offset);
            if (clockOffset && *clockOffset != segment.offset) differentOffsets = true;
            else clockOffset = segment.offset;
        }

        int start = original.startFrame;
        const bool still = original.playback == SymbolPlayback::SingleFrame || original.playback == SymbolPlayback::Poses;
        if (clockOffset) start = still ? firstActiveFrame : original.startFrame + *clockOffset;
        else if (original.startFrame >= source.durationFrames)
            start += plan.duration - source.durationFrames; // Keep a future start relative to the shot end.
        else if (hadActiveFrames) start = plan.duration; // Retain a deleted tail instance, hidden.
        else {
            // A cel can end before its Symbol starts. Keep its dormant timing
            // anchor when possible, without making previously hidden frames visible.
            for (const auto &segment : plan.segments)
                if (original.startFrame >= segment.first && original.startFrame < segment.end) {
                    start += segment.offset; break;
                }
            start = qMax(start, lastInactiveEnd);
        }

        if ((!still && differentOffsets) || start < 0 || lastInactiveEnd > start)
            return QString("This edit would split or reorder playback inside Symbol \"%1\". "
                           "Move its complete exposure, edit time before it starts, or break it apart first.").arg(layer.name);
        if (start > maxShotFrames)
            return QString("The start of Symbol \"%1\" would exceed the 1,000,000-frame limit.").arg(layer.name);
        if (start != original.startFrame) {
            const auto error = unlocked(source, layer.id, false);
            if (!error.isEmpty()) return error;
            layer.symbol->startFrame = start;
        }
        const auto poseError = retimePoseKeys(source,layer,original,plan);
        if (!poseError.isEmpty()) return poseError;
    }
    return {};
}
} // namespace

QString timelineRippleLabel(TimelineRippleKind kind) {
    switch (kind) {
    case TimelineRippleKind::Insert: return "Insert timeline time";
    case TimelineRippleKind::Delete: return "Delete timeline time";
    case TimelineRippleKind::Move: return "Move timeline range";
    }
    return "Ripple timeline";
}

int timelineRippleFrame(int originalFrame, int originalDuration, const TimelineRippleEdit &edit) {
    Plan plan;
    if (!makePlan(originalDuration, edit, &plan).isEmpty())
        return qBound(0, originalFrame, qMax(0, originalDuration - 1));
    for (const auto &segment : plan.segments)
        if (originalFrame >= segment.first && originalFrame < segment.end)
            return originalFrame + segment.offset;
    // Deleted frames land on the first surviving frame at the cut boundary.
    return qBound(0, edit.startFrame, plan.duration - 1);
}

QString applyTimelineRipple(Shot &shot, double fps, const TimelineRippleEdit &edit) {
    if (!std::isfinite(fps) || fps < 1 || fps > 120) return "The project frame rate is invalid.";
    Plan plan;
    const auto planError = makePlan(shot.durationFrames, edit, &plan);
    if (!planError.isEmpty()) return planError;
    const auto originalError = validateTimeline(shot);
    if (!originalError.isEmpty()) return originalError;
    if (plan.identity) return {};
    auto next = shot;
    next.durationFrames = plan.duration;
    const auto cueError = retimeCues(shot, next, plan, edit);
    if (!cueError.isEmpty()) return cueError;

    for (int t = 0; t < shot.animationTracks.size(); ++t) {
        const auto &source = shot.animationTracks[t]; auto &track = next.animationTracks[t];
        QVector<CelExposure> exposures;
        QSet<QString> affectedDrawings;
        for (const auto &exposure : source.exposures) {
            const int oldEnd = exposure.startFrame + exposure.durationFrames;
            QVector<CelExposure> pieces;
            for (const auto &segment : plan.segments) {
                const int first = qMax(exposure.startFrame, segment.first), end = qMin(oldEnd, segment.end);
                if (end <= first) continue;
                auto piece = exposure;
                if (!pieces.isEmpty()) piece.id = newId();
                piece.startFrame = first + segment.offset; piece.durationFrames = end - first;
                pieces.append(std::move(piece));
            }
            if (pieces.size() != 1 || pieces.first() != exposure) affectedDrawings.insert(exposure.drawingId);
            exposures += pieces;
        }
        for (const auto &drawing : source.drawings) if (affectedDrawings.contains(drawing.id)) {
            const auto error = unlocked(shot, drawing.groupLayerId); if (!error.isEmpty()) return error;
        }
        std::sort(exposures.begin(), exposures.end(), [](const auto &a, const auto &b) { return a.startFrame < b.startFrame; });
        track.exposures = std::move(exposures);
        // Removing time removes exposures, not their reusable source drawings.
        // Explicit unused-drawing deletion is the only library cleanup action.
    }

    const auto symbolError = retimeSymbols(shot, next, plan);
    if (!symbolError.isEmpty()) return symbolError;

    for (auto &animation : next.layerAnimations) {
        bool changed = false;
        for (auto &channel : animation.channels) {
            auto keys = retimeKeys(channel.keys, plan); changed |= keys != channel.keys; channel.keys = std::move(keys);
        }
        for (auto &path : animation.paths) {
            auto keys = retimeKeys(path.keys, plan); changed |= keys != path.keys; path.keys = std::move(keys);
        }
        if (changed) { const auto error = unlocked(shot, animation.layerId); if (!error.isEmpty()) return error; }
        animation.channels.removeIf([](const auto &channel) { return channel.keys.isEmpty(); });
        animation.paths.removeIf([](const auto &path) { return path.keys.isEmpty(); });
        const bool positionRemains = std::any_of(animation.channels.cbegin(), animation.channels.cend(), [](const auto &channel) {
            return channel.property == KeyframeProperty::PositionX || channel.property == KeyframeProperty::PositionY;
        });
        if (changed && !positionRemains) animation.orientAlongPath = false;
    }
    prunePropertyAnimation(next);
    next.camera2DKeys = retimeKeys(shot.camera2DKeys, plan);
    for (auto &animation : next.sceneCameraAnimations) {
        auto keys = retimeKeys(animation.keys, plan);
        if (keys != animation.keys) { const auto error = unlocked(shot, animation.layerId); if (!error.isEmpty()) return error; }
        animation.keys = std::move(keys);
    }
    // When the last key is removed, the unchanged saved camera base is used.
    pruneCameraAnimation(next);

    next.audioClips.clear();
    for (const auto &clip : shot.audioClips) {
        const qint64 oldStart = audioSampleFrameAtTimelineFrame(clip.startFrame, fps);
        const qint64 oldEnd = oldStart + clip.sampleCount;
        bool firstPiece = true;
        for (const auto &segment : plan.segments) {
            const qint64 first = qMax(oldStart, audioSampleFrameAtTimelineFrame(segment.first, fps));
            const qint64 end = qMin(oldEnd, audioSampleFrameAtTimelineFrame(segment.end, fps));
            if (end <= first) continue;
            auto piece = clip;
            materializeAudioFadeAnchors(piece);
            if (!firstPiece) piece.id = newId();
            piece.startFrame = qMax(clip.startFrame, segment.first) + segment.offset;
            piece.sourceOffsetSamples += first - oldStart;
            const auto capacity = audioSampleFrameAtTimelineFrame(segment.end + segment.offset, fps)
                - audioSampleFrameAtTimelineFrame(piece.startFrame, fps);
            piece.sampleCount = qMin(end - first, capacity);
            if (piece.sampleCount < 1) continue;
            firstPiece = false; next.audioClips.append(std::move(piece));
        }
    }
    // Stable order keeps independent audio rows recognizable after a ripple.
    const auto error = validateTimeline(next);
    if (!error.isEmpty()) return error;
    shot = std::move(next);
    return {};
}
} // namespace toon
