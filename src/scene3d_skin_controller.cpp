#include "scene3d_controller.h"
#include "scene3d_cache.h"
#include "canvas.h"
#include <QCryptographicHash>
#include <QDataStream>
#include <QIODevice>
#include <QFutureWatcher>
#include <QLineF>
#include <QTimer>
#include <QtConcurrentRun>
#include <algorithm>
#include <cmath>

namespace toon {
const Skin3D *Scene3DController::selectedSkin() const {
    const auto *object=selectedObject();
    return object && object->skin ? &*object->skin : nullptr;
}
void Scene3DController::resetSkinEditing() {
    if(m_bindingCancel)m_bindingCancel->store(true);
    ++m_bindingGeneration; ++m_projectionGeneration;
    m_skinBindingBusy=false; m_projectionBusy=false; m_weightStroke=false;
    m_paintedSkin.reset(); m_weightDabStrength.clear(); m_weightStrokeError.clear(); m_weightProjection.reset(); m_projectionKey.clear();
    m_rigEditMode=RigEditMode::Rest; m_skinPreviewRotation={}; m_renderBaseKey.clear(); m_previewSkin.reset();
    m_poseMode=PoseMode::FK;m_ikHandle=IKHandle::Target;m_poseRotationAxis=2;m_poseDrag=PoseDrag::None;
    if(m_skinRenderTimer)m_skinRenderTimer->stop();
}
bool Scene3DController::canPaintWeights() const {
    const auto *object=selectedObject();
    return canEdit() && scene()->visible && object && object->visible && !object->locked
        && object->skin && selectedJoint() && m_rigEditMode==RigEditMode::Weights
        && !m_skinBindingBusy && m_skinPreviewRotation.isNull();
}
void Scene3DController::setRigEditMode(RigEditMode mode) {
    if(m_rigEditMode==mode)return;
    endEdit(); m_rigEditMode=mode; m_skinPreviewRotation={};
    invalidateSkinView(); emit changed();
}
void Scene3DController::setWeightPaintMode(WeightPaintMode mode) {
    if(m_weightPaintMode==mode)return; endEdit(); m_weightPaintMode=mode; emit changed();
}
void Scene3DController::setWeightBrushRadius(qreal radius) {
    if(!std::isfinite(radius))return;
    radius=std::clamp(radius,1.,250.); if(m_weightBrushRadius==radius)return;
    m_weightBrushRadius=radius; emit changed(); m_canvas->update();
}
void Scene3DController::setWeightBrushStrength(qreal strength) {
    if(!std::isfinite(strength))return;
    strength=std::clamp(strength,0.,1.); if(m_weightBrushStrength==strength)return;
    m_weightBrushStrength=strength; emit changed();
}
void Scene3DController::setSkinPreviewRotation(QVector3D rotation) {
    for(int axis=0;axis<3;++axis) {
        if(!std::isfinite(rotation[axis]))return;
        rotation[axis]=std::clamp(rotation[axis],-90.f,90.f);
    }
    if(m_skinPreviewRotation==rotation)return;
    const auto *object=selectedObject();
    if(!rotation.isNull() && (!canEdit() || !object || object->locked || !object->visible
        || !selectedSkin() || !selectedJoint() || m_skinBindingBusy))return;
    if(m_weightStroke)endEdit();
    m_skinPreviewRotation=rotation; invalidateSkinView(); emit changed();
}
void Scene3DController::bindSkin() {
    endEdit();
    if(!canEditRig() || !selectedRig() || selectedRig()->joints.isEmpty()) {
        emit warning("Select an unlocked model with rest joints before binding."); return;
    }
    const auto model=scene()->models.value(selectedObject()->modelId);
    if(!model)return;
    const auto rig=*selectedRig(); const auto objectId=m_selected;
    const auto generation=++m_bindingGeneration;
    m_bindingCancel=std::make_shared<std::atomic_bool>(false);
    const auto cancellation=m_bindingCancel;
    m_skinBindingBusy=true; m_rigEditMode=RigEditMode::Weights; m_skinPreviewRotation={}; emit changed();
    using Result=QPair<std::optional<Skin3D>,QString>;
    auto *watcher=new QFutureWatcher<Result>(this);
    connect(watcher,&QFutureWatcher<Result>::finished,this,[this,watcher,generation,objectId,model,rig]{
        auto result=watcher->result(); watcher->deleteLater();
        if(generation!=m_bindingGeneration)return;
        m_skinBindingBusy=false;
        const auto *object=selectedObject();
        if(!object || object->id!=objectId || object->modelId!=model->id || object->rig!=rig || !canEditRig()) {
            emit warning("The binding target changed. Select the model and bind again."); emit changed(); return;
        }
        if(!result.first) { if(!result.second.isEmpty())emit warning(result.second); emit changed(); return; }
        if(m_canvas->document()->editingGestureActive() || m_canvas->document()->layerPropertyEditActive() || editing()) {
            emit warning("Finish the current edit, then bind the model again."); emit changed(); return;
        }
        // Enforce aggregate project limits before publishing the worker result.
        auto candidate=m_canvas->project();
        for(auto &layer:candidate.shots[m_canvas->document()->activeShotIndex()].layers)if(layer.scene3d)
            for(auto &entry:layer.scene3d->objects)if(entry.id==objectId)entry.skin=result.first;
        const auto error=candidate.validate();
        if(!error.isEmpty()){emit warning(error);emit changed();return;}
        if(mutate("Bind model to skeleton",[&](Scene3D &s){for(auto &entry:s.objects)if(entry.id==objectId)entry.skin=std::move(result.first);})) {
            if(!selectedJoint() && !rig.joints.isEmpty())m_selectedJoint=rig.joints.first().id;
            invalidateSkinView();
        }
        emit changed();
    });
    watcher->setFuture(QtConcurrent::run([model,rig,cancellation]{QString error;auto result=autoBindSkin3D(*model,rig,&error,cancellation);return Result(std::move(result),error);}));
}
void Scene3DController::unbindSkin() {
    endEdit(); const auto *object=selectedObject();
    if(!canEdit() || !scene()->visible || !object || object->locked || !object->visible || !object->skin || m_skinBindingBusy)return;
    const auto id=m_selected;
    m_skinPreviewRotation={};
    if(mutate("Unbind model",[&](Scene3D &s){for(auto &entry:s.objects)if(entry.id==id){entry.skin.reset();entry.pose={};}}))invalidateSkinView();
}
Scene3DRenderOptions Scene3DController::skinRenderOptions() const {
    Scene3DRenderOptions result;
    const auto *object=selectedObject();
    if(m_canvas->tool()!=Canvas::Tool::SceneRig || m_rigEditMode==RigEditMode::Pose
        || !object || !object->visible || !object->skin)return result;
    if(m_rigEditMode==RigEditMode::Rest||!selectedJoint()){
        if(!pose3DIsIdentity(object->pose)){result.skinObjectId=object->id;result.restPose=true;}return result;
    }
    result.skinObjectId=object->id; result.skinJointId=m_selectedJoint;
    result.weights=true; result.restPose=true; result.skinRotation=m_skinPreviewRotation; return result;
}
void Scene3DController::invalidateSkinView() {
    const auto previous=m_requestedKey; requestRenders(); prepareWeightProjection();
    if(previous!=m_requestedKey){m_canvas->invalidateArtworkCache();emit previewChanged();}
    m_canvas->update();
}
void Scene3DController::prepareWeightProjection() {
    if(!scene() || !selectedSkin() || m_canvas->tool()!=Canvas::Tool::SceneRig || m_rigEditMode!=RigEditMode::Weights) {
        ++m_projectionGeneration; m_projectionBusy=false; m_weightProjection.reset(); m_projectionKey.clear(); return;
    }
    const QSize frame(m_canvas->project().width,m_canvas->project().height);
    const auto view=m_canvas->artworkViewTransform();
    QByteArray key=scene3DCacheKey(*scene(),frame); QDataStream stream(&key,QIODevice::Append); stream<<view<<m_selected;
    key=QCryptographicHash::hash(key,QCryptographicHash::Sha256);
    if(key==m_projectionKey)return;
    // Coalesce rapid orbit/zoom changes instead of queuing a full surface build
    // for every event. The completed worker schedules the newest view next.
    if(m_projectionBusy){m_weightProjection.reset();return;}
    m_projectionKey=key; m_projectionBusy=true; m_weightProjection.reset();
    const auto generation=++m_projectionGeneration; const auto snapshot=*scene(); const auto id=m_selected;
    using Result=QPair<std::shared_ptr<SkinPaintProjection>,QString>;
    auto *watcher=new QFutureWatcher<Result>(this);
    connect(watcher,&QFutureWatcher<Result>::finished,this,[this,watcher,generation]{
        auto result=watcher->result(); watcher->deleteLater(); if(generation!=m_projectionGeneration)return;
        m_projectionBusy=false; m_weightProjection=std::move(result.first);
        if(!result.second.isEmpty())emit warning(result.second);
        prepareWeightProjection(); emit changed();
    });
    watcher->setFuture(QtConcurrent::run([snapshot,id,frame,view]{
        auto projection=std::make_shared<SkinPaintProjection>(); QString error;
        if(!projection->build(snapshot,id,frame,view,&error))projection.reset();
        return Result(std::move(projection),error);
    }));
}
bool Scene3DController::weightPointerPress(QPointF artwork) {
    if(!canPaintWeights())return true;
    prepareWeightProjection();
    if(!m_weightProjection){emit warning(m_projectionBusy?"Preparing the visible mesh for weight painting…":"The visible mesh is unavailable for weight painting.");return true;}
    if(!beginEdit("Paint skin weights"))return true;
    m_weightStroke=true; m_dragging=true; m_paintedSkin=*selectedSkin(); m_weightDabStrength.clear(); m_weightStrokeError.clear();
    m_weightLastPoint=m_canvas->artworkViewTransform().map(artwork); paintWeightDab(m_weightLastPoint); return true;
}
void Scene3DController::weightPointerMove(QPointF artwork) {
    if(!m_weightStroke || !m_paintedSkin || !canPaintWeights())return;
    const auto point=m_canvas->artworkViewTransform().map(artwork);
    const int count=std::max(1,int(std::ceil(QLineF(m_weightLastPoint,point).length()/std::max(1.,m_weightBrushRadius*.2))));
    const auto start=m_weightLastPoint;
    for(int i=1;i<=std::min(count,2048);++i)paintWeightDab(start+(point-start)*(qreal(i)/std::min(count,2048)));
    m_weightLastPoint=point;
}
void Scene3DController::paintWeightDab(QPointF point) {
    if(!m_weightProjection || !m_paintedSkin || !selectedSkin())return;
    const auto hits=m_weightProjection->verticesInBrush(point,m_weightBrushRadius);
    const int joint=m_paintedSkin->jointIds.indexOf(m_selectedJoint); if(joint<0)return;
    const auto &original=*selectedSkin();
    float smooth=0, total=0;
    if(m_weightPaintMode==WeightPaintMode::Smooth)for(const auto &hit:hits) {
        smooth+=skinJointWeight(original.meshes[hit.mesh][hit.vertex],quint16(joint))*hit.falloff; total+=hit.falloff;
    }
    if(total>0)smooth/=total;
    bool changed=false;
    for(const auto &hit:hits) {
        const quint64 key=(quint64(hit.mesh)<<32)|quint32(hit.vertex);
        const float amount=float(m_weightBrushStrength*std::clamp(m_canvas->m_pointerPressure,0.,1.))*hit.falloff;
        if(amount<=m_weightDabStrength.value(key))continue;
        m_weightDabStrength.insert(key,amount);
        const auto &source=original.meshes[hit.mesh][hit.vertex];
        auto vertex=source; const float old=skinJointWeight(source,quint16(joint));
        const float value=m_weightPaintMode==WeightPaintMode::Smooth?old+(smooth-old)*amount
            :old+(m_weightPaintMode==WeightPaintMode::Add?amount:-amount);
        QString error;
        if(setSkinJointWeight(vertex,quint16(joint),std::clamp(value,0.f,1.f),&error) && vertex!=m_paintedSkin->meshes[hit.mesh][hit.vertex]) {
            m_paintedSkin->meshes[hit.mesh][hit.vertex]=vertex; changed=true;
        }
        if(m_weightStrokeError.isEmpty() && !error.isEmpty())m_weightStrokeError=error;
    }
    if(changed && !m_skinRenderTimer->isActive())m_skinRenderTimer->start();
}
void Scene3DController::finishWeightStroke(bool commit) {
    if(!m_weightStroke)return;
    m_weightStroke=false; m_dragging=false; m_skinRenderTimer->stop();
    auto skin=std::move(m_paintedSkin); m_paintedSkin.reset(); m_weightDabStrength.clear();
    if(commit && skin && selectedSkin() && *skin!=*selectedSkin()) {
        const auto id=m_selected;
        mutate("Paint skin weights",[&](Scene3D &s){for(auto &object:s.objects)if(object.id==id)object.skin=std::move(skin);});
    } else if(commit && !m_weightStrokeError.isEmpty())emit warning(m_weightStrokeError);
    m_weightStrokeError.clear();
    requestRenders(); m_canvas->invalidateArtworkCache(); emit previewChanged();
}
} // namespace toon
