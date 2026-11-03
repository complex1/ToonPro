#include "animation.h"
#include "keyframe_render.h"
#include "camera_animation.h"
#include "project.h"
#include "symbols.h"
#include <QJsonObject>
#include <QUuid>
#include <algorithm>
#include <cmath>

namespace toon {
namespace {
QHash<QString, const RasterLayer *> layerMap(const Shot &shot) {
    QHash<QString, const RasterLayer *> result;
    for (const auto &layer : shot.layers) result.insert(layer.id, &layer);
    return result;
}
QHash<QString, AnimationOwner> owners(const Shot &shot) {
    QHash<QString, AnimationOwner> result;
    for (const auto &track : shot.animationTracks) {
        result.insert(track.rootGroupLayerId, {track.id, {}});
        for (const auto &drawing : track.drawings) result.insert(drawing.groupLayerId, {track.id, drawing.id});
    }
    const auto layers = layerMap(shot);
    for (const auto &layer : shot.layers) {
        QString id = layer.id;
        QSet<QString> seen;
        while (!id.isEmpty() && !result.contains(id) && layers.contains(id) && !seen.contains(id)) {
            seen.insert(id); id = layers[id]->parentId;
        }
        const auto owner = result.value(id);
        for (const auto &child : seen) result.insert(child, owner);
    }
    return result;
}
bool validId(const QString &id) { return !QUuid(id).isNull(); }
bool integer(const QJsonObject &object, const char *key, int *value) {
    const auto json = object.value(QLatin1String(key));
    const double number = json.toDouble(-1);
    if (!json.isDouble() || !std::isfinite(number) || number < 0 || number > 1000000 || std::floor(number) != number) return false;
    *value = int(number); return true;
}
bool keys(const QJsonObject &object, std::initializer_list<const char *> expected) {
    if (object.size() != qsizetype(expected.size())) return false;
    for (const auto key : expected) if (!object.contains(QLatin1String(key))) return false;
    return true;
}
}
const CelExposure *exposureAtFrame(const AnimationTrack &track, int frame) {
    const auto it = std::upper_bound(track.exposures.cbegin(), track.exposures.cend(), frame,
        [](int value, const CelExposure &exposure) { return value < exposure.startFrame; });
    if (it == track.exposures.cbegin()) return nullptr;
    const auto &exposure = *std::prev(it);
    return frame >= exposure.startFrame && qint64(frame) < qint64(exposure.startFrame) + exposure.durationFrames ? &exposure : nullptr;
}
const AnimationDrawing *drawingAtFrame(const AnimationTrack &track, int frame) {
    const auto *exposure = exposureAtFrame(track, frame);
    if (exposure) for (const auto &drawing : track.drawings) if (drawing.id == exposure->drawingId) return &drawing;
    return nullptr;
}
QHash<QString, AnimationOwner> animationOwners(const Shot &shot) { return owners(shot); }
AnimationOwner animationOwner(const Shot &shot, const QString &layerId) {
    if (shot.animationTracks.isEmpty()) return {};
    QHash<QString, AnimationOwner> containers;
    for (const auto &track : shot.animationTracks) {
        containers.insert(track.rootGroupLayerId,{track.id,{}});
        for (const auto &drawing : track.drawings) containers.insert(drawing.groupLayerId,{track.id,drawing.id});
    }
    QString id=layerId;
    for (int depth=0;depth<=8 && !id.isEmpty();++depth) {
        if (containers.contains(id)) return containers.value(id);
        QString parent;
        for (const auto &layer : shot.layers) if (layer.id==id) { parent=layer.parentId; break; }
        id=parent;
    }
    return {};
}
QSet<QString> activeLayerIds(const Shot &shot, int frame) {
    QSet<QString> activeDrawings, result;
    for (const auto &track : shot.animationTracks) if (const auto *drawing = drawingAtFrame(track, frame)) activeDrawings.insert(drawing->id);
    const auto ownership = owners(shot);
    for (const auto &layer : shot.layers) {
        const auto owner = ownership.value(layer.id);
        if (owner.drawingId.isEmpty() || activeDrawings.contains(owner.drawingId)) result.insert(layer.id);
    }
    return result;
}
bool layerPresentAtFrame(const Shot &shot, const QString &layerId, int frame) { return activeLayerIds(shot, frame).contains(layerId); }
QVector<RasterLayer> evaluateLayers(const Shot &shot, int frame, bool sourceOnly, QRectF bounds, QString *error) {
    const auto active = activeLayerIds(shot, frame);
    QVector<RasterLayer> result; result.reserve(active.size());
    for (const auto &layer : shot.layers) if (active.contains(layer.id)) result.append(layer);
    if(!sourceOnly&&!applyLayerAnimations(shot,frame,result,bounds,error))return {};
    // Scene references use the shot clock even when editing source artwork.
    // Only the copied scene is evaluated; the saved camera remains unchanged.
    for (auto &layer : result) if (layer.scene3d)
        layer.scene3d->camera = evaluateSceneCamera(shot, layer.id, frame);
    return result;
}
QVector<RasterLayer> evaluateLayers(const Project &project, const Shot &shot, int frame, bool sourceOnly, QRectF bounds, QString *error) {
    QString detail;
    auto result=evaluateLayers(shot,frame,sourceOnly,bounds,&detail);
    if(!detail.isEmpty()){if(error)*error=detail;return {};}
    auto source=shot;
    if(sourceOnly)source.layerAnimations.clear();
    if(!resolveSymbolLayers(project,source,frame,result,bounds,error))return {};
    return result;
}
QVector<DrawingTemplateSlot> drawingTemplateSlots(DrawingTemplate type) {
    switch (type) {
    case DrawingTemplate::Simple: return {{"drawing", "Drawing"}};
    case DrawingTemplate::Rough: return {{"rough", "Rough"}, {"sketch", "Sketch"}, {"lines", "Clean-up"}};
    case DrawingTemplate::Anime: return {{"rough", "Rough"}, {"sketch", "Sketch"}, {"colors", "Colors"},
        {"shadow", "Shadow"}, {"highlight", "Highlights"}, {"lines", "Line Art"}};
    }
    return {};
}
QString validateDrawingTemplate(const QVector<DrawingTemplateSlot> &layerSlots) {
    if (layerSlots.isEmpty() || layerSlots.size() >= maxDrawingLayers) return "Drawing templates need between 1 and 63 layers.";
    QSet<QString> ids;
    for (const auto &slot : layerSlots) {
        if (slot.id.trimmed().isEmpty() || slot.id.size() > 100 || ids.contains(slot.id)
            || slot.name.trimmed().isEmpty() || slot.name.size() > 100)
            return "Each template layer needs a unique identifier and a name of 1 to 100 characters.";
        if (slot.kind != LayerKind::Raster && slot.kind != LayerKind::Vector)
            return "Drawing templates support raster and vector layers.";
        ids.insert(slot.id);
    }
    return {};
}
QString validateAnimation(const Shot &shot) {
    if (shot.animationTracks.isEmpty()) return shot.layers.size() <= maxDrawingLayers ? QString() : QString("A static shot supports up to 64 layers and groups.");
    if (shot.animationTracks.size() > maxAnimationTracks) return "A shot supports up to 16 animation tracks.";
    if (shot.layers.size() > maxAnimationLayers) return "Animated shots support up to 4096 stored layers and groups.";
    const auto layers = layerMap(shot);
    QSet<QString> ids, containers;
    for (const auto &layer : shot.layers) ids.insert(layer.id);
    QSet<QString> animationFolders;
    for (const auto &track : shot.animationTracks) {
        animationFolders.insert(track.rootGroupLayerId);
        for (const auto &drawing : track.drawings) animationFolders.insert(drawing.groupLayerId);
    }
    int drawings = 0, exposures = 0;
    for (const auto &track : shot.animationTracks) {
        if (!validId(track.id) || ids.contains(track.id)) return "Animation track identifiers must be valid and unique.";
        ids.insert(track.id);
        const auto root = layers.value(track.rootGroupLayerId);
        if (!root || !root->group || containers.contains(root->id) || root->annotation)
            return "Each animation track needs its own artwork group.";
        QSet<QString> ancestry{root->id};
        auto parentId=root->parentId;
        while (!parentId.isEmpty()) {
            const auto *parent=layers.value(parentId);
            if (!parent || !parent->group || parent->annotation || ancestry.contains(parentId)
                || animationFolders.contains(parentId) || ancestry.size()>=8)
                return "Animation tracks can be nested only in ordinary artwork folders.";
            ancestry.insert(parentId); parentId=parent->parentId;
        }
        containers.insert(root->id);
        const auto templateError = validateDrawingTemplate(track.drawingTemplate);
        if (!templateError.isEmpty()) return templateError;
        QSet<QString> slotIds;
        for (const auto &slot : track.drawingTemplate) slotIds.insert(slot.id);
        QSet<QString> drawingIds;
        for (const auto &drawing : track.drawings) {
            if (++drawings > maxAnimationDrawings) return "A shot supports up to 256 animation drawings.";
            if (!validId(drawing.id) || ids.contains(drawing.id)) return "Animation drawing identifiers must be valid and unique.";
            ids.insert(drawing.id); drawingIds.insert(drawing.id);
            const auto group = layers.value(drawing.groupLayerId);
            if (!group || !group->group || group->parentId != root->id || containers.contains(group->id)) return "Each drawing needs its own group inside its animation track.";
            if (!group->visible || group->annotation || group->opacity != 1 || group->blendMode != LayerBlendMode::Normal)
                return "Drawing containers must stay visible with Normal blending and full opacity.";
            containers.insert(group->id);
            QSet<QString> assigned;
            for (auto it = drawing.templateLayers.cbegin(); it != drawing.templateLayers.cend(); ++it) {
                if (!slotIds.contains(it.key()) || !layers.contains(it.value()) || assigned.contains(it.value())) return "Invalid drawing template layer assignment.";
                assigned.insert(it.value());
            }
        }
        int previousEnd = 0;
        for (const auto &exposure : track.exposures) {
            if (++exposures > maxAnimationExposures) return "A shot supports up to 16384 drawing exposures.";
            if (!validId(exposure.id) || ids.contains(exposure.id)) return "Exposure identifiers must be valid and unique.";
            ids.insert(exposure.id);
            if (!drawingIds.contains(exposure.drawingId)) return "An exposure refers to a missing drawing.";
            if (exposure.startFrame < previousEnd || exposure.startFrame < 0 || exposure.durationFrames < 1
                || qint64(exposure.startFrame) + exposure.durationFrames > shot.durationFrames)
                return "Drawing exposures must be sorted, non-overlapping and inside the shot duration.";
            previousEnd = exposure.startFrame + exposure.durationFrames;
        }
    }
    const auto ownership = owners(shot);
    QHash<QString, int> nodeCounts, artworkCounts;
    int staticNodes = 0;
    for (const auto &layer : shot.layers) {
        const auto owner = ownership.value(layer.id);
        if (owner.trackId.isEmpty()) { if (++staticNodes > maxDrawingLayers) return "The static layer stack supports up to 64 layers and groups."; continue; }
        if (owner.drawingId.isEmpty()) {
            if (!containers.contains(layer.id)) return "Place animation artwork inside a registered drawing, not directly in its track.";
            continue;
        }
        if (++nodeCounts[owner.drawingId] > maxDrawingLayers) return "Each animation drawing supports up to 64 layers and groups.";
        if ((layer.kind != LayerKind::Raster && layer.kind != LayerKind::Vector && layer.kind != LayerKind::Symbol) || layer.annotation) return "Frame animation drawings support raster, vector and symbol artwork layers and groups.";
        if (!layer.group && !layer.adjustment) ++artworkCounts[owner.drawingId];
    }
    for (const auto &track : shot.animationTracks) for (const auto &drawing : track.drawings) {
        if (!artworkCounts.value(drawing.id)) return "Keep at least one raster or vector artwork layer in each animation drawing.";
        for (const auto &id : drawing.templateLayers)
            if (ownership.value(id).drawingId != drawing.id || layers[id]->group || layers[id]->adjustment)
                return "Template layers must be raster or vector artwork inside their drawing.";
    }
    return {};
}
QString validateAnimationMutation(const Shot &before, const Shot &after) {
    // Painting changes tiles, not ownership; avoid rebuilding the hierarchy on
    // every completed stroke when the existing valid structure is unchanged.
    if (before.durationFrames==after.durationFrames && before.animationTracks==after.animationTracks && before.layers.size()==after.layers.size()) {
        bool same=true;
        for (int i=0;i<before.layers.size() && same;++i) {
            const auto &a=before.layers[i], &b=after.layers[i];
            same=a.id==b.id && a.parentId==b.parentId && a.group==b.group && a.kind==b.kind && a.adjustment==b.adjustment
                && a.annotation==b.annotation && a.visible==b.visible && a.opacity==b.opacity && a.blendMode==b.blendMode;
        }
        if (same) return {};
    }
    const auto validation = validateAnimation(after); if (!validation.isEmpty()) return validation;
    if (before.animationTracks.isEmpty() && after.animationTracks.isEmpty()) return {};
    // Role hints can be removed when their artwork layer is explicitly deleted.
    auto previous = before.animationTracks;
    const auto currentLayers = layerMap(after);
    for (auto &track : previous) for (auto &drawing : track.drawings)
        for (auto it = drawing.templateLayers.begin(); it != drawing.templateLayers.end();)
            if (!currentLayers.contains(it.value())) it = drawing.templateLayers.erase(it); else ++it;
    if (previous != after.animationTracks) return "Use timeline actions to create, duplicate or delete animation drawings and tracks.";
    const auto beforeOwners = owners(before), afterOwners = owners(after);
    QSet<QString> containers, trackRoots;
    for (const auto &track : before.animationTracks) {
        containers.insert(track.rootGroupLayerId); trackRoots.insert(track.rootGroupLayerId);
        for (const auto &drawing : track.drawings) containers.insert(drawing.groupLayerId);
    }
    for (const auto &layer : before.layers) {
        const auto current = currentLayers.value(layer.id);
        if (containers.contains(layer.id) && (!current || !current->group || (!trackRoots.contains(layer.id) && current->parentId != layer.parentId)))
            return "Animation track and drawing folders are managed by the timeline.";
        if (current && beforeOwners.value(layer.id) != afterOwners.value(layer.id))
            return "Layers cannot move between static artwork and animation drawings. Duplicate a drawing using the timeline.";
    }
    return {};
}
QJsonArray animationTracksToJson(const QVector<AnimationTrack> &tracks) {
    QJsonArray result;
    for (const auto &track : tracks) {
        QJsonArray templateSlots, drawings, exposures;
        for (const auto &slot : track.drawingTemplate) {
            QJsonObject object{{"id",slot.id},{"name",slot.name}};
            if (slot.kind != LayerKind::Raster) object.insert("kind",layerKindKey(slot.kind));
            templateSlots.append(object);
        }
        for (const auto &drawing : track.drawings) {
            QJsonObject roles; for (auto it=drawing.templateLayers.cbegin(); it!=drawing.templateLayers.cend(); ++it) roles.insert(it.key(),it.value());
            drawings.append(QJsonObject{{"id",drawing.id},{"groupLayerId",drawing.groupLayerId},{"templateLayers",roles}});
        }
        for (const auto &exposure : track.exposures) exposures.append(QJsonObject{{"id",exposure.id},{"drawingId",exposure.drawingId},
            {"startFrame",exposure.startFrame},{"durationFrames",exposure.durationFrames}});
        result.append(QJsonObject{{"id",track.id},{"rootGroupLayerId",track.rootGroupLayerId},{"drawingTemplate",templateSlots},{"drawings",drawings},{"exposures",exposures}});
    }
    return result;
}
bool animationTracksFromJson(const QJsonArray &json, QVector<AnimationTrack> *tracks, QString *error) {
    auto fail=[error](const QString &message) { if(error) *error=message; return false; };
    if (json.size()>maxAnimationTracks) return fail("Too many animation tracks.");
    QVector<AnimationTrack> parsed; int drawingCount=0, exposureCount=0;
    for (const auto &value : json) {
        const auto object=value.toObject();
        if (!value.isObject() || !keys(object,{"id","rootGroupLayerId","drawingTemplate","drawings","exposures"})
            || !object["id"].isString() || !object["rootGroupLayerId"].isString() || !object["drawingTemplate"].isArray()
            || !object["drawings"].isArray() || !object["exposures"].isArray()) return fail("Invalid animation track metadata.");
        AnimationTrack track; track.id=object["id"].toString(); track.rootGroupLayerId=object["rootGroupLayerId"].toString();
        if (object["drawingTemplate"].toArray().size()>=maxDrawingLayers) return fail("Too many drawing template layers.");
        for (const auto &value : object["drawingTemplate"].toArray()) {
            const auto slot=value.toObject();
            if (!value.isObject() || !(keys(slot,{"id","name"}) || keys(slot,{"id","name","kind"}))
                || !slot["id"].isString() || !slot["name"].isString()) return fail("Invalid drawing template.");
            const auto kind = slot.contains("kind") ? layerKindFromKey(slot["kind"].toString()) : std::optional<LayerKind>(LayerKind::Raster);
            if (!kind || (*kind != LayerKind::Raster && *kind != LayerKind::Vector)) return fail("Invalid drawing template layer type.");
            track.drawingTemplate.append({slot["id"].toString(),slot["name"].toString(),*kind});
        }
        for (const auto &value : object["drawings"].toArray()) {
            const auto object=value.toObject();
            if (++drawingCount>maxAnimationDrawings || !value.isObject() || !keys(object,{"id","groupLayerId","templateLayers"})
                || !object["id"].isString() || !object["groupLayerId"].isString() || !object["templateLayers"].isObject()) return fail("Invalid animation drawing metadata.");
            AnimationDrawing drawing{object["id"].toString(),object["groupLayerId"].toString(),{}};
            const auto roles=object["templateLayers"].toObject();
            if (roles.size()>=maxDrawingLayers) return fail("Too many drawing template assignments.");
            for (auto it=roles.begin();it!=roles.end();++it) { if(!it.value().isString()) return fail("Invalid drawing template assignment."); drawing.templateLayers.insert(it.key(),it.value().toString()); }
            track.drawings.append(drawing);
        }
        for (const auto &value : object["exposures"].toArray()) {
            const auto object=value.toObject(); CelExposure exposure;
            if (++exposureCount>maxAnimationExposures || !value.isObject() || !keys(object,{"id","drawingId","startFrame","durationFrames"})
                || !object["id"].isString() || !object["drawingId"].isString() || !integer(object,"startFrame",&exposure.startFrame)
                || !integer(object,"durationFrames",&exposure.durationFrames)) return fail("Invalid animation exposure metadata.");
            exposure.id=object["id"].toString(); exposure.drawingId=object["drawingId"].toString(); track.exposures.append(exposure);
        }
        parsed.append(track);
    }
    *tracks=std::move(parsed); return true;
}
qint64 animationHistoryCost(const Shot &before, const Shot &after) {
    if (before.animationTracks == after.animationTracks) return 0;
    auto size=[](const Shot &shot) { qint64 bytes=0; for (const auto &track : shot.animationTracks) {
        bytes+=512+track.drawingTemplate.size()*512+track.exposures.size()*256;
        for (const auto &drawing : track.drawings) bytes+=256+drawing.templateLayers.size()*256;
    } return bytes; };
    return size(before)+size(after);
}
} // namespace toon
