#include "object_tools.h"
#include "document.h"
#include "animation.h"
#include "property_animation.h"
#include "keyframe_render.h"
#include "vector_edit_geometry.h"
#include "vector_brush.h"
#include <QPainterPathStroker>
#include <QSet>
#include <QUuid>
#include <cmath>
#include <algorithm>

namespace toon {
namespace {
int availableTiles(const Project &project, const QString &exclude) {
    return maxProjectTiles - projectTileCount(project, exclude);
}
bool ready(Document *doc) {
    if (!doc || !doc->isOpen()) return false;
    if (doc->editingGestureActive() || doc->layerPropertyEditActive()) {
        emit doc->warning("Finish the current edit before editing paths or text."); return false;
    }
    const auto &layers = doc->activeShot().layers;
    if (!layerCanEdit(layers,doc->activeLayer())) {
        emit doc->warning(layers[doc->activeLayer()].group
            ? "Choose a vector or text layer inside the folder to edit its artwork."
            : "Show and unlock this layer and its folders before editing its artwork.");
        return false;
    }
    return true;
}
bool compatible(const RasterLayer &layer, const EditableObject &object) {
    return (layer.kind == LayerKind::Vector && object.kind == ObjectKind::Path)
        || (layer.kind == LayerKind::Text && object.kind == ObjectKind::Text);
}
bool positionChanged(const EditableObject &a, const EditableObject &b) {
    if (a.transform != b.transform || a.position != b.position || a.nodes.size() != b.nodes.size()) return true;
    for (int i = 0; i < a.nodes.size(); ++i)
        if (a.nodes[i].anchor != b.nodes[i].anchor || a.nodes[i].inControl != b.nodes[i].inControl || a.nodes[i].outControl != b.nodes[i].outControl) return true;
    return false;
}
QVector<EditableObject> editingObjects(const Shot &shot, const RasterLayer &layer, const Document *doc) {
    auto objects=layer.objects;
    if(!doc->keyframeSourceEditing()) for(auto &entry:objects)
        entry=evaluateAnimatedObject(shot,layer.id,entry,doc->currentFrame());
    return objects;
}
QString animatedEditError(const Shot &shot,const RasterLayer &layer,const EditableObject &value,const Document *doc) {
    if(!pathAnimation(shot,layer.id,value.id))return {};
    if(doc->keyframeSourceEditing())
        return "Leave Source mode to edit this path's animation, or disable its path animation before changing the source.";
    const auto *source=ObjectTools::object(layer,value.id);
    if(!source || source->nodes.size()!=value.nodes.size() || source->closed!=value.closed)
        return "Animated paths must keep the same nodes and open/closed shape. Disable path animation before adding, deleting or simplifying nodes.";
    if(source->brushStroke!=value.brushStroke)
        return "An animated path keeps its original brush. Disable path animation before changing its brush source.";
    return {};
}
QString destructivePathMessage() {
    return "Disable path animation before deleting, erasing, simplifying or rasterizing this path.";
}
QPainterPath segmentPath(const EditableObject &object, int index) {
    const auto &a = object.nodes[index], &b = object.nodes[(index+1)%object.nodes.size()];
    QPainterPath path; path.moveTo(a.anchor); path.cubicTo(a.outControl,b.inControl,b.anchor);
    return path;
}
QPainterPath strokeArea(const QPainterPath &path, qreal width) {
    QPainterPathStroker stroker; stroker.setWidth(qMax(.01,width));
    stroker.setCapStyle(Qt::RoundCap); stroker.setJoinStyle(Qt::RoundJoin);
    return stroker.createStroke(path);
}
}
ObjectTools::ObjectTools(Document *document) : m_document(document) {}
ObjectTools::~ObjectTools() { cancelEdit(); }
bool ObjectTools::beginEdit(const QString &id) {
    if (m_before || !ready(m_document)) return false;
    const auto &layer = m_document->activeShot().layers[m_document->activeLayer()];
    if (!object(layer,id)) { emit m_document->warning("Select a path or text object to edit."); return false; }
    m_before = m_document->m_project; m_editId = id; m_layerId = layer.id; m_dirty = false; m_label = "Edit object";
    m_positionWarningShown = false;
    m_animationWarningShown = false;
    m_document->m_externalEditActive = true; return true;
}
bool ObjectTools::beginLayerEdit(const QString &label) {
    if (m_before || !ready(m_document)) return false;
    const auto &layer = m_document->activeShot().layers[m_document->activeLayer()];
    if (layer.kind != LayerKind::Vector) { emit m_document->warning("Choose a vector layer to edit paths."); return false; }
    m_before=m_document->m_project;m_editId.clear();m_layerId=layer.id;m_dirty=false;m_label=label;
    m_positionWarningShown=false;
    m_animationWarningShown=false;
    m_document->m_externalEditActive=true;return true;
}
bool ObjectTools::previewEdit(const EditableObject &replacement) {
    if (!m_document || !m_before || replacement.id != m_editId) return false;
    const auto &shot=m_before->shots[m_document->activeShotIndex()];
    const auto &layer=shot.layers[m_document->activeLayer()];
    auto objects=editingObjects(shot,layer,m_document);
    bool found=false;for(auto &entry:objects)if(entry.id==m_editId){entry=replacement;found=true;break;}
    return found && previewObjects(objects);
}
bool ObjectTools::previewObjects(const QVector<EditableObject> &objects) {
    if (!m_document || !m_before || !m_document->m_externalEditActive) return false;
    auto next=*m_before;auto &shot=next.shots[m_document->activeShotIndex()];auto &layer=shot.layers[m_document->activeLayer()];
    if(layer.id!=m_layerId || objects.size()!=layer.objects.size())return false;
    const auto original=editingObjects(shot,layer,m_document);
    bool sourceChanged=false;
    for(int i=0;i<objects.size();++i) {
        if(objects[i].id!=layer.objects[i].id || !compatible(layer,objects[i]))return false;
        if(layerPositionLocked(shot.layers,m_document->activeLayer()) && positionChanged(original[i],objects[i])) {
            if (!m_positionWarningShown) {
                m_positionWarningShown=true;
                emit m_document->warning("Unlock the layer or its folder position before changing path points or moving artwork.");
            }
            return false;
        }
        if(objects[i]==original[i])continue;
        const auto error=animatedEditError(shot,layer,objects[i],m_document);
        if(!error.isEmpty()) {
            if(!m_animationWarningShown){m_animationWarningShown=true;emit m_document->warning(error);}
            return false;
        }
        if(pathAnimation(shot,layer.id,objects[i].id)) {
            QString error;
            if(!writePathKeyframe(shot,layer.id,objects[i].id,m_document->currentFrame(),objects[i],&error)) {
                if(!m_animationWarningShown){m_animationWarningShown=true;emit m_document->warning(error);}
                return false;
            }
        }else {layer.objects[i]=objects[i];sourceChanged=true;}
    }
    const auto &current=m_document->m_project.shots[m_document->activeShotIndex()];
    if(layer.objects==current.layers[m_document->activeLayer()].objects && shot.layerAnimations==current.layerAnimations)return true;
    QString error=validatePropertyAnimation(shot);
    if(!error.isEmpty()){emit m_document->warning(error);return false;}
    if(shot.layerAnimations!=m_before->shots[m_document->activeShotIndex()].layerAnimations) {
        auto evaluated=layer;
        evaluated.objects=editingObjects(shot,layer,m_document);
        if(!rebuildObjectLayer(evaluated,m_document->drawingBounds(),availableTiles(next,layer.id),&error)) {
            if(!m_animationWarningShown){m_animationWarningShown=true;emit m_document->warning(error);}
            return false;
        }
    }
    if(sourceChanged && !rebuildObjectLayer(layer,m_document->drawingBounds(),availableTiles(next,layer.id),&error)){emit m_document->warning(error);return false;}
    m_dirty=layer.objects!=m_before->shots[m_document->activeShotIndex()].layers[m_document->activeLayer()].objects
        || shot.layerAnimations!=m_before->shots[m_document->activeShotIndex()].layerAnimations;
    m_document->m_project=next;emit m_document->changed();return true;
}
void ObjectTools::finishEdit() {
    if (!m_before) return;
    const auto before = *m_before; m_before.reset();
    if (!m_document || !m_document->m_externalEditActive) return;
    m_document->m_externalEditActive = false;
    if (m_dirty) m_document->commit(before,m_document->m_project,m_label,m_layerId);
    m_dirty = false;
}
void ObjectTools::cancelEdit() {
    if (!m_before) return;
    const auto before = *m_before; m_before.reset();
    if (!m_document || !m_document->m_externalEditActive) return;
    m_document->m_externalEditActive = false;
    m_document->m_project = before; emit m_document->changed(); m_dirty = false;
}
bool ObjectTools::addLayer(Document *doc, LayerKind kind) {
    if (doc && kind == LayerKind::Scene3D) {
        emit doc->warning("Add models from the 3D Library to create the shared 3D reference scene."); return false;
    }
    if (!doc || !doc->isOpen() || doc->editingGestureActive() || doc->layerPropertyEditActive() || layerKindKey(kind).isEmpty()) return false;
    auto next = doc->m_project; auto &layers = next.shots[doc->activeShotIndex()].layers;
    if (layers.size() >= (next.shots[doc->activeShotIndex()].animationTracks.isEmpty() ? maxDrawingLayers : maxAnimationLayers)) {
        emit doc->warning("The shot has reached its stored layer limit."); return false;
    }
    const auto &selected = layers[doc->activeLayer()];
    if (!animationOwner(next.shots[doc->activeShotIndex()], selected.id).trackId.isEmpty() && kind != LayerKind::Raster && kind != LayerKind::Vector) {
        emit doc->warning("Animation drawings support raster and vector layers. Add text to a static layer."); return false;
    }
    QString parent = selected.group ? selected.id : selected.parentId;
    QSet<QString> seen;
    while (!parent.isEmpty()) {
        bool found = false;
        for (const auto &entry : layers) if (entry.id == parent) {
            if (!entry.group || entry.locked || seen.contains(parent)) return false;
            seen.insert(parent); parent = entry.parentId; found = true; break;
        }
        if (!found) return false;
    }
    auto layer = RasterLayer::create(kind == LayerKind::Vector ? "Vector" : kind == LayerKind::Text ? "Text" : "Drawing");
    layer.kind = kind; layer.parentId = selected.group ? selected.id : selected.parentId;
    layers.insert(selected.group ? layers.size() : doc->activeLayer()+1,layer);
    return doc->commit(doc->m_project,next,"Add " + layerKindKey(kind) + " layer",layer.id);
}
QString ObjectTools::addObject(Document *doc, EditableObject value) {
    if (!ready(doc)) return {};
    const auto editReason=keyframeRasterEditReason(doc);
    if(!editReason.isEmpty()){emit doc->warning(editReason);return {};}
    auto next = doc->m_project; auto &layer = next.shots[doc->activeShotIndex()].layers[doc->m_active];
    if (!compatible(layer,value)) { emit doc->warning("Choose a matching vector or text layer."); return {}; }
    if (layer.objects.size() >= 1024) { emit doc->warning("A layer supports up to 1024 editable objects."); return {}; }
    if (value.id.isEmpty()) value.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    for (const auto *shot : projectArtworkShots(next)) for (const auto &entry : shot->layers) for (const auto &object : entry.objects)
        if (object.id == value.id) return {};
    layer.objects.append(value); QString error;
    if (!rebuildObjectLayer(layer,doc->drawingBounds(),availableTiles(next,layer.id),&error)) { emit doc->warning(error); return {}; }
    const auto validation = next.validate(); if (!validation.isEmpty()) { emit doc->warning(validation); return {}; }
    doc->commit(doc->m_project,next,value.kind == ObjectKind::Path ? "Add vector path" : "Add text",layer.id); return value.id;
}
QString ObjectTools::addPath(Document *doc, const QVector<BezierNode> &nodes, bool closed, QColor stroke, qreal width, QColor fill) {
    auto value = EditableObject::create(); value.nodes = nodes; value.closed = closed;
    value.stroke = stroke; value.strokeWidth = width; value.fill = fill; return addObject(doc,value);
}
QString ObjectTools::addText(Document *doc, EditableObject value) { value.kind = ObjectKind::Text; return addObject(doc,value); }
bool ObjectTools::updateObject(Document *doc, const EditableObject &value, const QString &label) {
    if (!ready(doc)) return false;
    auto next = doc->m_project; auto &layer = next.shots[doc->activeShotIndex()].layers[doc->m_active];
    if (!compatible(layer,value)) { emit doc->warning("Choose a matching vector or text layer."); return false; }
    const auto *source = object(layer,value.id);
    if (!source) { emit doc->warning("Select a path or text object to edit."); return false; }
    const auto previous=doc->keyframeSourceEditing()?*source:evaluateAnimatedObject(next.shots[doc->activeShotIndex()],layer.id,*source,doc->currentFrame());
    if (layerPositionLocked(next.shots[doc->activeShotIndex()].layers,doc->m_active) && positionChanged(previous,value)) {
        emit doc->warning("Unlock the layer or its folder position before changing path points or moving artwork."); return false;
    }
    if (previous==value) return false;
    QString error=animatedEditError(next.shots[doc->activeShotIndex()],layer,value,doc);
    if(!error.isEmpty()){emit doc->warning(error);return false;}
    if(pathAnimation(next.shots[doc->activeShotIndex()],layer.id,value.id)) {
        if(!writePathKeyframe(next.shots[doc->activeShotIndex()],layer.id,value.id,doc->currentFrame(),value,&error)) {emit doc->warning(error);return false;}
        auto evaluated=layer;evaluated.objects=editingObjects(next.shots[doc->activeShotIndex()],layer,doc);
        if(!rebuildObjectLayer(evaluated,doc->drawingBounds(),availableTiles(next,layer.id),&error)) {emit doc->warning(error);return false;}
    }else {
        for (auto &entry : layer.objects) if (entry.id == value.id) { entry = value; break; }
        if (!rebuildObjectLayer(layer,doc->drawingBounds(),availableTiles(next,layer.id),&error)) { emit doc->warning(error); return false; }
    }
    return doc->commit(doc->m_project,next,label,layer.id);
}
bool ObjectTools::simplify(Document *doc,qreal tolerance,const QString &id) {
    if(!ready(doc))return false;
    if(!std::isfinite(tolerance)||tolerance<=0) { emit doc->warning("Choose a positive simplify tolerance."); return false; }
    auto next=doc->m_project;auto &layer=next.shots[doc->activeShotIndex()].layers[doc->m_active];
    if(layer.kind!=LayerKind::Vector) { emit doc->warning("Choose a vector layer to simplify paths."); return false; }
    if(layerPositionLocked(next.shots[doc->activeShotIndex()].layers,doc->m_active)) {
        emit doc->warning("Unlock the layer or its folder position before simplifying paths."); return false;
    }
    if(!id.isEmpty()&&!object(layer,id)) { emit doc->warning("Select a vector path to simplify."); return false; }
    for(const auto &entry:layer.objects)if((id.isEmpty()||entry.id==id)&&pathAnimation(next.shots[doc->activeShotIndex()],layer.id,entry.id)) {
        emit doc->warning(destructivePathMessage());return false;
    }
    bool changed=false;for(auto &entry:layer.objects)if(id.isEmpty()||entry.id==id) {
        auto simplified=simplifiedVectorPath(entry,tolerance);changed|=simplified.nodes.size()!=entry.nodes.size();entry=simplified;
    }
    if(!changed)return false;QString error;
    if(!rebuildObjectLayer(layer,doc->drawingBounds(),availableTiles(next,layer.id),&error)){emit doc->warning(error);return false;}
    doc->commit(doc->m_project,next,id.isEmpty()?"Simplify vector layer":"Simplify vector path",layer.id);return true;
}
bool ObjectTools::removeObject(Document *doc, const QString &id) {
    if (!ready(doc)) return false;
    auto next = doc->m_project; auto &layer = next.shots[doc->activeShotIndex()].layers[doc->m_active];
    if (!object(layer,id)) return false;
    if(pathAnimation(next.shots[doc->activeShotIndex()],layer.id,id)) {emit doc->warning(destructivePathMessage());return false;}
    layer.objects.removeIf([&](const EditableObject &o){ return o.id == id; });
    QString error;
    if (!rebuildObjectLayer(layer,doc->drawingBounds(),availableTiles(next,layer.id),&error)) { emit doc->warning(error); return false; }
    doc->commit(doc->m_project,next,"Delete object",layer.id); return true;
}
bool ObjectTools::eraseAt(Document *doc, QPointF point, qreal radius, bool segments) {
    QPainterPath sweep; sweep.moveTo(point); sweep.lineTo(point + QPointF(.001,0)); return erasePath(doc,sweep,radius,segments);
}
bool ObjectTools::erasePath(Document *doc, const QPainterPath &sweep, qreal radius, bool segments) {
    if (!ready(doc) || !std::isfinite(radius) || radius <= 0 || radius > 2048 || sweep.isEmpty()) return false;
    auto next = doc->m_project; auto &layer = next.shots[doc->activeShotIndex()].layers[doc->m_active];
    if (layer.kind != LayerKind::Vector) { emit doc->warning("Choose a vector layer to erase paths."); return false; }
    const auto eraser = strokeArea(sweep,2*radius);
    for(const auto &entry:layer.objects)if(pathAnimation(next.shots[doc->activeShotIndex()],layer.id,entry.id)) {
        const auto evaluated=doc->keyframeSourceEditing()?entry:evaluateAnimatedObject(next.shots[doc->activeShotIndex()],layer.id,entry,doc->currentFrame());
        const bool hit=evaluated.brushStroke?vectorBrushIntersects(evaluated,eraser):
            vectorCorrectionIntersects(evaluated,eraser)
            || (evaluated.stroke.isValid()&&eraser.intersects(editableStrokeOutline(evaluated)))
            || (evaluated.fill.isValid()&&eraser.intersects(editableObjectPath(evaluated)));
        if(hit) {emit doc->warning(destructivePathMessage());return false;}
    }
    QVector<EditableObject> output; bool changed = false;
    for (const auto &entry : layer.objects) {
        // Animated paths were checked against their visible pose above. A miss
        // must not erase the source at its different, unanimated coordinates.
        if(pathAnimation(next.shots[doc->activeShotIndex()],layer.id,entry.id)){output.append(entry);continue;}
        if(entry.brushStroke && !segments){
            if(vectorBrushIntersects(entry,eraser)){changed=true;continue;}
            output.append(entry);continue;
        }
        QVector<bool> erased; const int count = entry.nodes.size() - (entry.closed ? 0 : 1);
        QVector<qreal> distances(count+1,0);for(int i=0;i<count;++i)distances[i+1]=distances[i]+segmentPath(entry,i).length();
        bool hit = false;QSet<int> hitSegments;
        if(entry.brushStroke&&distances.last()>0)for(const qreal position:vectorBrushIntersectionPositions(entry,eraser)) {
            const qreal distance=qBound(0.,position,1.)*distances.last();
            const auto found=std::lower_bound(distances.cbegin()+1,distances.cend(),distance);
            hitSegments.insert(qBound(0,int(found-distances.cbegin())-1,count-1));
        }
        if(entry.brushStroke&&distances.last()<=0&&vectorBrushIntersects(entry,eraser))for(int i=0;i<count;++i)hitSegments.insert(i);
        for (int i = 0; i < count; ++i) {
            const auto segment = entry.transform.map(strokeArea(segmentPath(entry,i),qMax(1.0,entry.strokeWidth*qMax(vectorNodeWidthFactor(entry,i),vectorNodeWidthFactor(entry,(i+1)%entry.nodes.size())))));
            bool touches = eraser.intersects(segment) || eraser.contains(segment.boundingRect().center());
            if(entry.brushStroke)touches=hitSegments.contains(i);
            erased.append(touches); hit |= touches;
        }
        if (!hit && !segments && entry.fill.isValid()) hit = eraser.intersects(editableObjectPath(entry));
        if (!hit) { output.append(entry); continue; }
        changed = true;
        if (!segments) continue;
        // Start after a removed edge so a closed path becomes an ordinary sequence.
        int start = 0;
        if (entry.closed) for (int i = 0; i < count; ++i) if (erased[i]) { start = (i+1)%count; break; }
        QVector<BezierNode> run; QVector<qreal> widths; int runStart=-1,runEnd=-1;
        auto flush = [&] {
            if (run.size() >= 2) {
                auto fragment = entry; fragment.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
                fragment.closed = false; fragment.fill = QColor(); fragment.nodes = run;
                fragment.widthFactors = entry.widthFactors.isEmpty()?QVector<qreal>():widths;
                if(fragment.brushStroke && distances.last()>0)
                    sliceVectorBrushStroke(fragment,distances[runStart]/distances.last(),distances[runEnd+1]/distances.last(),distances.last());
                output.append(fragment);
            }
            run.clear();widths.clear();runStart=runEnd=-1;
        };
        for (int k = 0; k < count; ++k) {
            const int i = (start+k)%count;
            if (erased[i]) { flush(); continue; }
            if (run.isEmpty()) { run.append(entry.nodes[i]);widths.append(vectorNodeWidthFactor(entry,i));runStart=i; }
            run.append(entry.nodes[(i+1)%entry.nodes.size()]);widths.append(vectorNodeWidthFactor(entry,(i+1)%entry.nodes.size()));runEnd=i;
        }
        flush();
    }
    if (!changed) return false;
    if (output.size() > 1024) { emit doc->warning("Erasing these segments would exceed the layer's 1024-path limit. Erase whole paths or split the artwork across layers."); return false; }
    layer.objects = output; QString error;
    if (!rebuildObjectLayer(layer,doc->drawingBounds(),availableTiles(next,layer.id),&error)) { emit doc->warning(error); return false; }
    doc->commit(doc->m_project,next,segments ? "Erase vector segments" : "Erase vector paths",layer.id); return true;
}
QString ObjectTools::hitTest(const RasterLayer &layer, QPointF point, qreal tolerance) {
    for (int i = layer.objects.size()-1; i >= 0; --i) {
        const auto &entry = layer.objects[i];
        if(entry.brushStroke){QPainterPath region;region.addEllipse(point,qMax(.5,tolerance),qMax(.5,tolerance));if(vectorBrushIntersects(entry,region))return entry.id;continue;}
        const auto path = editableObjectPath(entry);
        if ((entry.fill.isValid() && path.contains(point)) || strokeArea(path,2*tolerance).contains(point)) return entry.id;
        if(entry.stroke.isValid()){
            if(entry.kind==ObjectKind::Text){auto local=entry;local.transform={};if(entry.transform.map(strokeArea(editableObjectPath(local),entry.strokeWidth)).contains(point))return entry.id;}
            else if(editableStrokeOutline(entry).contains(point))return entry.id;
        }
    }
    return {};
}
const EditableObject *ObjectTools::object(const RasterLayer &layer, const QString &id) {
    for (const auto &entry : layer.objects) if (entry.id == id) return &entry;
    return nullptr;
}
bool ObjectTools::rasterize(Document *doc) {
    if (!ready(doc)) return false;
    auto next = doc->m_project; auto &layer = next.shots[doc->activeShotIndex()].layers[doc->m_active];
    if (layer.kind == LayerKind::Scene3D) {
        emit doc->warning("3D reference layers cannot be rasterized. Draw on a layer above the reference."); return false;
    }
    if (!layerHasEditableObjects(layer)) return false;
    if(const auto *animation=layerAnimation(next.shots[doc->activeShotIndex()],layer.id);animation&&!animation->paths.isEmpty()) {
        emit doc->warning(destructivePathMessage());return false;
    }
    QString error;
    if (!rebuildObjectLayer(layer,doc->drawingBounds(),availableTiles(next,layer.id),&error)) { emit doc->warning(error); return false; }
    layer.kind = LayerKind::Raster; layer.objects.clear();
    doc->commit(doc->m_project,next,"Rasterize layer",layer.id); return true;
}
} // namespace toon
