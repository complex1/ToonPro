#include "timeline_organization.h"
#include "document.h"
#include "project.h"
#include <QHash>
#include <QSet>

namespace toon {
namespace {
int indexOf(const Shot &shot,const QString &id) {
    for (int i=0;i<shot.layers.size();++i) if (shot.layers[i].id==id) return i;
    return -1;
}
QString hierarchyError(const Shot &shot) {
    QHash<QString,const RasterLayer *> layers;
    for (const auto &layer:shot.layers) layers.insert(layer.id,&layer);
    for (const auto &layer:shot.layers) {
        QSet<QString> seen{layer.id}; auto parent=layer.parentId; int depth=layer.group?1:0;
        while (!parent.isEmpty()) {
            if (seen.contains(parent)) return "A folder cannot contain itself or its parent.";
            const auto *item=layers.value(parent);
            if (!item || !item->group) return "Choose an existing folder for this track.";
            if (++depth>8) return "Layer groups support at most eight nested levels.";
            seen.insert(parent); parent=item->parentId;
        }
    }
    return {};
}
QHash<QString,QString> clippingBases(const Shot &shot) {
    QHash<QString,QString> bases,result;
    for (const auto &layer:shot.layers) {
        if (layer.clipped) result.insert(layer.id,bases.value(layer.parentId));
        else bases.insert(layer.parentId,layer.id);
    }
    return result;
}
QString prepareMove(const Shot &before,const QString &id,const QString &parent,const QString &beforeId,Shot *after) {
    const int source=indexOf(before,id);
    if (source<0) return "This timeline layer no longer exists.";
    for (const auto &track:before.animationTracks) for (const auto &drawing:track.drawings)
        if (drawing.groupLayerId==id) return "Move or reuse drawings through their exposure cells.";
    const auto hierarchy=hierarchyError(before); if (!hierarchy.isEmpty()) return hierarchy;
    QSet<QString> family{id};
    for (int i=0;i<before.layers.size();++i)
        if (before.layers[i].id==id || layerIsDescendant(before.layers,i,id)) {
            if (layerPositionLocked(before.layers,i)) return "Unlock the track, its contents and their folder positions before moving.";
            family.insert(before.layers[i].id);
        }
    if (!parent.isEmpty()) {
        const int target=indexOf(before,parent);
        if (target<0 || !before.layers[target].group) return "Choose an existing destination folder.";
        if (family.contains(parent)) return "A folder cannot contain itself or its contents.";
        if (layerPositionLocked(before.layers,target)) return "Unlock the destination folder and its position before moving tracks.";
    }
    if (!beforeId.isEmpty()) {
        const int target=indexOf(before,beforeId);
        if (target<0 || before.layers[target].parentId!=parent) return "This drop destination has changed.";
        if (family.contains(beforeId)) {
            if (beforeId==id && before.layers[source].parentId==parent) { if(after)*after=before; return {}; }
            return "Choose a destination outside the moving track.";
        }
    }
    Shot result=before; QVector<RasterLayer> moving,remaining;
    for (auto layer:before.layers) {
        if (family.contains(layer.id)) {
            if (layer.id==id) layer.parentId=parent;
            moving.append(layer);
        } else remaining.append(layer);
    }
    int insertion=remaining.size();
    if (!beforeId.isEmpty()) for (int i=0;i<remaining.size();++i) if (remaining[i].id==beforeId) { insertion=i;break; }
    for (int i=0;i<moving.size();++i) remaining.insert(insertion+i,moving[i]);
    result.layers=std::move(remaining);
    const auto changedHierarchy=hierarchyError(result); if(!changedHierarchy.isEmpty())return changedHierarchy;
    if (clippingBases(before)!=clippingBases(result)) return "Keep clipped layers with their original clipping base when reordering.";
    const auto validation=validateAnimationMutation(before,result); if(!validation.isEmpty())return validation;
    if(after)*after=std::move(result);
    return {};
}
bool fail(Document *document,QString *error,const QString &reason) {
    if(error)*error=reason;
    if(document && !reason.isEmpty())emit document->warning(reason);
    return false;
}
bool ready(Document *document,QString *error) {
    if(error)error->clear();
    if(!document || !document->isOpen())return fail(document,error,"Open a project before organizing tracks.");
    if(document->editingGestureActive() || document->layerPropertyEditActive())
        return fail(document,error,"Finish the current edit before organizing tracks.");
    return true;
}
}
QString timelineOrganizationEditError(const Shot &shot,const QString &id,const QString &parent,const QString &before) {
    return prepareMove(shot,id,parent,before,nullptr);
}
bool moveTimelineLayer(Document *document,const QString &id,const QString &parent,const QString &before,QString *error) {
    if(!ready(document,error))return false;
    auto project=document->project(); auto &shot=project.shots[document->activeShotIndex()];
    Shot candidate;
    const auto reason=prepareMove(shot,id,parent,before,&candidate); if(!reason.isEmpty())return fail(document,error,reason);
    bool same=shot.layers.size()==candidate.layers.size();
    for(int i=0;same && i<shot.layers.size();++i)same=shot.layers[i].id==candidate.layers[i].id && shot.layers[i].parentId==candidate.layers[i].parentId;
    if(same)return true;
    shot=std::move(candidate);
    const auto validation=project.validate();if(!validation.isEmpty())return fail(document,error,validation);
    return document->replaceSymbolProject(project,"Move timeline track");
}
bool addTimelineFolder(Document *document,const QString &parent,const QString &name,QString *createdId,QString *error) {
    if(createdId)createdId->clear();
    if(!ready(document,error))return false;
    const auto title=name.trimmed();
    if(title.isEmpty() || title.size()>100)return fail(document,error,"Give the folder a name of 1 to 100 characters.");
    auto project=document->project();auto &shot=project.shots[document->activeShotIndex()];
    if(!parent.isEmpty()) {
        const int index=indexOf(shot,parent);
        if(index<0 || !shot.layers[index].group || !animationOwner(shot,parent).trackId.isEmpty())
            return fail(document,error,"Create timeline folders outside animation drawings and tracks.");
        if(layerPositionLocked(shot.layers,index))return fail(document,error,"Unlock the destination folder before adding a folder.");
    }
    auto folder=RasterLayer::create(title);folder.group=true;folder.parentId=parent;
    const auto id=folder.id;shot.layers.append(folder);
    const auto validation=project.validate();if(!validation.isEmpty())return fail(document,error,validation);
    if(!document->replaceSymbolProject(project,"Add timeline folder"))return false;
    if(createdId)*createdId=id;
    return true;
}
}
