#include "object_tools.h"
#include "document.h"
#include "animation.h"
#include "property_animation.h"
#include "keyframe_render.h"
#include "vector_edit_geometry.h"
#include "vector_brush.h"
#include "brush_engine.h"
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

// Flattening locates crossings only. Retained artwork is reconstructed from
// exact cubic subcurves, never from the approximation used by the hit query.
struct EraseCubic { QPointF a, b, c, d; };
EraseCubic eraseCubic(const EditableObject &object, int segment) {
    const auto &a=object.nodes[segment], &b=object.nodes[(segment+1)%object.nodes.size()];
    return {a.anchor,a.outControl,b.inControl,b.anchor};
}
QPointF erasePoint(const EraseCubic &c, qreal t) {
    const qreal u=1-t;
    return c.a*(u*u*u)+c.b*(3*u*u*t)+c.c*(3*u*t*t)+c.d*(t*t*t);
}
std::pair<EraseCubic,EraseCubic> splitEraseCubic(const EraseCubic &c,qreal t) {
    const auto a=c.a+(c.b-c.a)*t,b=c.b+(c.c-c.b)*t,d=c.c+(c.d-c.c)*t;
    const auto e=a+(b-a)*t,f=b+(d-b)*t,g=e+(f-e)*t;
    return {{c.a,a,e,g},{g,f,d,c.d}};
}
EraseCubic eraseSubcurve(EraseCubic curve,qreal from,qreal to) {
    if(to<1)curve=splitEraseCubic(curve,to).first;
    return from>0?splitEraseCubic(curve,from/to).second:curve;
}
qreal eraseLength(const EraseCubic &curve) {
    QPainterPath path(curve.a);path.cubicTo(curve.b,curve.c,curve.d);return path.length();
}
QPointF eraseMappedPoint(const EditableObject &object,int segment,qreal t,int copy) {
    auto point=erasePoint(eraseCubic(object,segment),t);
    if(object.brushStroke) {
        const auto &stroke=*object.brushStroke;
        const auto copies=brushSymmetryInstances(stroke.settings.symmetry,point,stroke.canvasSize);
        if(copy<copies.size())point=copies[copy].point;
    }
    return object.transform.map(point);
}
qreal eraseCross(QPointF a,QPointF b) { return a.x()*b.y()-a.y()*b.x(); }
qreal eraseDistance(QPointF point,QPointF a,QPointF b) {
    const auto delta=b-a;const qreal squared=QPointF::dotProduct(delta,delta);
    const qreal t=squared>1e-18?qBound(0.,QPointF::dotProduct(point-a,delta)/squared,1.):0;
    return QLineF(point,a+delta*t).length();
}
struct EraseEdge { QPointF a,b; int segment=0,copy=0; qreal from=0,to=1; };
bool flattenEraseCurve(const EditableObject &object,int segment,int copy,qreal from,qreal to,
                       QPointF a,QPointF b,int depth,QVector<EraseEdge> &edges,int &budget) {
    qreal error=0;
    for(qreal fraction:{.25,.5,.75})
        error=qMax(error,eraseDistance(eraseMappedPoint(object,segment,from+(to-from)*fraction,copy),a,b));
    if(error<=.025) {
        if(--budget<0)return false;
        edges.append({a,b,segment,copy,from,to});return true;
    }
    if(depth>=16)return false;
    const qreal middle=(from+to)/2;const auto point=eraseMappedPoint(object,segment,middle,copy);
    return flattenEraseCurve(object,segment,copy,from,middle,a,point,depth+1,edges,budget)
        && flattenEraseCurve(object,segment,copy,middle,to,point,b,depth+1,edges,budget);
}
bool flattenEraseObject(const EditableObject &object,QVector<EraseEdge> &edges,int &budget) {
    int copies=1;
    if(object.brushStroke){const auto &s=*object.brushStroke;copies=brushSymmetryInstances(s.settings.symmetry,object.nodes.first().anchor,s.canvasSize).size();}
    const int count=object.nodes.size()-(object.closed?0:1);
    for(int segment=0;segment<count;++segment)for(int copy=0;copy<copies;++copy)
        if(!flattenEraseCurve(object,segment,copy,0,1,eraseMappedPoint(object,segment,0,copy),
            eraseMappedPoint(object,segment,1,copy),0,edges,budget))return false;
    return true;
}
bool eraseCrossing(const EditableObject &first,const EraseEdge &a,
                   const EditableObject &second,const EraseEdge &b,qreal &parameter) {
    const auto r=a.b-a.a,s=b.b-b.a;
    const qreal denominator=eraseCross(r,s),scale=QLineF(a.a,a.b).length()*QLineF(b.a,b.b).length();
    // Coincident spans and tangent contacts are deliberately not crossing cuts.
    if(scale<=1e-12||qAbs(denominator)<=scale*1e-8)return false;
    const qreal x=eraseCross(b.a-a.a,s)/denominator,y=eraseCross(b.a-a.a,r)/denominator;
    if(x< -1e-8||x>1+1e-8||y< -1e-8||y>1+1e-8)return false;
    qreal t=a.from+(a.to-a.from)*qBound(0.,x,1.),u=b.from+(b.to-b.from)*qBound(0.,y,1.);
    auto derivative=[](const EditableObject &o,const EraseEdge &edge,qreal value) {
        if(o.transform.isAffine()&&(!o.brushStroke||o.brushStroke->settings.symmetry.mode==SymmetryMode::Off)) {
            const auto c=eraseCubic(o,edge.segment);const qreal inverse=1-value;
            const auto tangent=(c.b-c.a)*(3*inverse*inverse)+(c.c-c.b)*(6*inverse*value)+(c.d-c.c)*(3*value*value);
            return o.transform.map(tangent)-o.transform.map(QPointF());
        }
        const qreal low=qMax(0.,value-1e-5),high=qMin(1.,value+1e-5);
        return (eraseMappedPoint(o,edge.segment,high,edge.copy)-eraseMappedPoint(o,edge.segment,low,edge.copy))/(high-low);
    };
    for(int iteration=0;iteration<32;++iteration) {
        const auto difference=eraseMappedPoint(first,a.segment,t,a.copy)-eraseMappedPoint(second,b.segment,u,b.copy);
        if(QLineF({},difference).length()<1e-12)break;
        const auto da=derivative(first,a,t),db=derivative(second,b,u);
        const qreal determinant=eraseCross(da,db);
        if(qAbs(determinant)<1e-14)return false;
        t=qBound(0.,t-eraseCross(difference,db)/determinant,1.);
        u=qBound(0.,u-eraseCross(difference,da)/determinant,1.);
    }
    if(QLineF(eraseMappedPoint(first,a.segment,t,a.copy),eraseMappedPoint(second,b.segment,u,b.copy)).length()>.001)return false;
    const auto da=derivative(first,a,t),db=derivative(second,b,u);
    if(qAbs(eraseCross(da,db))<=QLineF({},da).length()*QLineF({},db).length()*1e-6)return false;
    parameter=a.segment+t;
    if(&first==&second) {
        qreal distance=qAbs(parameter-b.segment-u);
        if(first.closed)distance=qMin(distance,qAbs(distance-first.nodes.size()));
        if(distance<1e-4)return false; // Adjacent edges are not self-crossings.
    }
    return true;
}
bool eraseStrokeVisible(const EditableObject &object) {
    return object.kind==ObjectKind::Path&&object.nodes.size()>=2&&object.opacity>0
        &&object.stroke.isValid()&&object.stroke.alpha()>0&&object.strokeWidth>0;
}
QVector<qreal> eraseDistances(const EditableObject &object) {
    const int count=object.nodes.size()-(object.closed?0:1);QVector<qreal> result(count+1,0);
    for(int i=0;i<count;++i)result[i+1]=result[i]+eraseLength(eraseCubic(object,i));
    return result;
}
qreal eraseDistanceAt(const EditableObject &object,const QVector<qreal> &distances,qreal parameter) {
    const int count=distances.size()-1;
    if(parameter>=count)return distances.last();
    const int segment=qMax(0,int(std::floor(parameter)));const qreal t=parameter-segment;
    return distances[segment]+(t>0?eraseLength(eraseSubcurve(eraseCubic(object,segment),0,t)):0);
}
EditableObject eraseFragment(const EditableObject &source,qreal from,qreal to,const QVector<qreal> &distances) {
    auto result=source;result.id=QUuid::createUuid().toString(QUuid::WithoutBraces);
    result.closed=false;result.fill=QColor();result.nodes.clear();result.widthFactors.clear();
    const int count=distances.size()-1;
    for(qreal current=from;current<to-1e-8;) {
        const int index=int(std::floor(current+1e-9)),segment=index%count;
        const qreal start=qMax(0.,current-index),end=qMin(1.,to-index);
        const auto curve=eraseSubcurve(eraseCubic(source,segment),start,end);
        const auto width=[&](qreal t) {
            const qreal length=distances[segment+1]-distances[segment];
            const qreal ratio=t<=0?0:t>=1?1:length>1e-9?eraseLength(eraseSubcurve(eraseCubic(source,segment),0,t))/length:t;
            return vectorNodeWidthFactor(source,segment)*(1-ratio)
                +vectorNodeWidthFactor(source,(segment+1)%source.nodes.size())*ratio;
        };
        if(result.nodes.isEmpty()) {
            result.nodes.append({curve.a,curve.a,curve.b});
            if(!source.widthFactors.isEmpty())result.widthFactors.append(width(start));
        }else result.nodes.last().outControl=curve.b;
        result.nodes.append({curve.d,curve.c,curve.d});
        if(!source.widthFactors.isEmpty())result.widthFactors.append(width(end));
        current=index+end;
    }
    if(result.brushStroke&&distances.last()>0) {
        const qreal start=eraseDistanceAt(source,distances,from)/distances.last();
        const qreal end=eraseDistanceAt(source,distances,to>count?to-count:to)/distances.last();
        sliceVectorBrushStroke(result,start,end,distances.last());
    }
    return result;
}
bool eraseIntersectionObjects(const QVector<EditableObject> &sources,const QVector<EditableObject> &evaluated,
                              const QSet<QString> &animated,const QPainterPath &eraser,
                              QVector<EditableObject> &output,bool &changed,QString &error) {
    QVector<QVector<EraseEdge>> geometry(sources.size());QVector<bool> prepared(sources.size(),false);
    int edgeBudget=65536,pairBudget=2000000;
    auto prepare=[&](int i) {
        if(prepared[i])return true;prepared[i]=true;
        return flattenEraseObject(evaluated[i],geometry[i],edgeBudget);
    };
    auto exhausted=[&] {error="This crossing trim exceeds the geometry work limit. Split the artwork across vector layers or erase a whole stroke.";return false;};
    for(int index=0;index<sources.size();++index) {
        const auto &source=sources[index];
        if(animated.contains(source.id)||!eraseStrokeVisible(source)){output.append(source);continue;}
        QVector<qreal> brushHits;
        if(source.brushStroke)brushHits=vectorBrushIntersectionPositions(source,eraser);
        const bool touched=source.brushStroke?!brushHits.isEmpty():eraser.intersects(editableStrokeOutline(source));
        if(!touched){output.append(source);continue;}
        if(!prepare(index))return exhausted();
        const int count=source.nodes.size()-(source.closed?0:1);
        QVector<qreal> cuts;
        for(int other=0;other<sources.size();++other) {
            if(!eraseStrokeVisible(evaluated[other]))continue;
            if(other!=index&&!editableObjectBounds(source).intersects(editableObjectBounds(evaluated[other])))continue;
            if(!prepare(other))return exhausted();
            for(int a=0;a<geometry[index].size();++a)for(int b=other==index?a+1:0;b<geometry[other].size();++b) {
                if(--pairBudget<0)return exhausted();
                const auto &first=geometry[index][a],&second=geometry[other][b];
                if(qMax(qMin(first.a.x(),first.b.x()),qMin(second.a.x(),second.b.x()))>qMin(qMax(first.a.x(),first.b.x()),qMax(second.a.x(),second.b.x()))+.001
                    ||qMax(qMin(first.a.y(),first.b.y()),qMin(second.a.y(),second.b.y()))>qMin(qMax(first.a.y(),first.b.y()),qMax(second.a.y(),second.b.y()))+.001)continue;
                qreal parameter;
                if(eraseCrossing(evaluated[index],first,evaluated[other],second,parameter)) {
                    cuts.append(parameter);
                    if(other==index) { // Retain both locations of a self-crossing.
                        if(eraseCrossing(evaluated[other],second,evaluated[index],first,parameter))cuts.append(parameter);
                    }
                    if(cuts.size()>8192)return exhausted();
                }
            }
        }
        if(!source.closed){cuts.append(0);cuts.append(count);}
        else for(auto &cut:cuts)if(cut>=count-1e-5)cut=0;
        std::sort(cuts.begin(),cuts.end());
        cuts.erase(std::unique(cuts.begin(),cuts.end(),[](qreal a,qreal b){return qAbs(a-b)<1e-5;}),cuts.end());
        if(cuts.isEmpty()||(source.closed&&cuts.size()==1)){changed=true;continue;}
        if(source.closed)cuts.append(cuts.first()+count);
        const auto distances=eraseDistances(source);bool erased=false;QVector<EditableObject> fragments;
        for(int i=1;i<cuts.size();++i) {
            if(cuts[i]-cuts[i-1]<1e-7)continue;
            bool hit=false;
            if(source.brushStroke) {
                const qreal from=eraseDistanceAt(source,distances,cuts[i-1])/qMax(1e-12,distances.last());
                const qreal to=eraseDistanceAt(source,distances,cuts[i]>count?cuts[i]-count:cuts[i])/qMax(1e-12,distances.last());
                const bool wrapped=cuts[i]>count;
                for(qreal position:brushHits)if(wrapped?(position>=from-1e-7||position<=to+1e-7):(position>=from-1e-7&&position<=to+1e-7)){hit=true;break;}
            }
            if(hit){erased=true;continue;}
            auto fragment=eraseFragment(source,cuts[i-1],cuts[i],distances);
            if(!source.brushStroke)hit=eraser.intersects(editableStrokeOutline(fragment));
            if(hit)erased=true;else fragments.append(std::move(fragment));
            if(fragments.size()+output.size()>1024) {
                error="Trimming these crossings would exceed the layer's 1024-path limit.";return false;
            }
        }
        if(!erased){output.append(source);continue;}
        changed=true;output+=fragments;
    }
    return true;
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
    return eraseAt(doc,point,radius,segments?VectorEraseMode::Segment:VectorEraseMode::WholeStroke);
}
bool ObjectTools::erasePath(Document *doc, const QPainterPath &sweep, qreal radius, bool segments) {
    return erasePath(doc,sweep,radius,segments?VectorEraseMode::Segment:VectorEraseMode::WholeStroke);
}
bool ObjectTools::eraseAt(Document *doc,QPointF point,qreal radius,VectorEraseMode mode) {
    QPainterPath sweep;sweep.moveTo(point);sweep.lineTo(point+QPointF(.001,0));return erasePath(doc,sweep,radius,mode);
}
bool ObjectTools::erasePath(Document *doc,const QPainterPath &sweep,qreal radius,VectorEraseMode mode) {
    if (!ready(doc) || !std::isfinite(radius) || radius <= 0 || radius > 2048 || sweep.isEmpty()) return false;
    if(mode!=VectorEraseMode::WholeStroke&&mode!=VectorEraseMode::Segment&&mode!=VectorEraseMode::Intersection)return false;
    const bool segments=mode==VectorEraseMode::Segment;
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
    if(mode==VectorEraseMode::Intersection) {
        QVector<EditableObject> output;bool changed=false;QString error;QSet<QString> animated;
        const auto &shot=next.shots[doc->activeShotIndex()];
        for(const auto &entry:layer.objects)if(pathAnimation(shot,layer.id,entry.id))animated.insert(entry.id);
        if(!eraseIntersectionObjects(layer.objects,editingObjects(shot,layer,doc),animated,eraser,output,changed,error)) {
            emit doc->warning(error);return false;
        }
        if(!changed)return false;
        if(output.size()>1024){emit doc->warning("Trimming these crossings would exceed the layer's 1024-path limit.");return false;}
        layer.objects=output;
        if(!rebuildObjectLayer(layer,doc->drawingBounds(),availableTiles(next,layer.id),&error)){emit doc->warning(error);return false;}
        doc->commit(doc->m_project,next,"Erase vector crossings",layer.id);return true;
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
