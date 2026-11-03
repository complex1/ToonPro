#include "scene3d_controller.h"
#include "scene3d_renderer.h"
#include "scene3d_cache.h"
#include "camera_animation.h"
#include "canvas.h"
#include "ui_theme.h"
#include <QUuid>
#include <QtMath>
#include <QPainter>
#include <QScopedValueRollback>
#include <QCryptographicHash>
#include <QDataStream>
#include <QIODevice>
#include <QTimer>
#include <algorithm>

namespace toon {
namespace {
QString uuid(){return QUuid::createUuid().toString(QUuid::WithoutBraces);}
bool sceneTool(Canvas::Tool t){return t==Canvas::Tool::SceneObject||t==Canvas::Tool::SceneCamera||t==Canvas::Tool::SceneRig;}
bool sameSkinStorage(const Skin3D &a,const Skin3D &b){
    if(a.jointIds!=b.jointIds || a.meshes.size()!=b.meshes.size())return false;
    for(qsizetype i=0;i<a.meshes.size();++i)
        if(a.meshes[i].size()!=b.meshes[i].size() || a.meshes[i].constData()!=b.meshes[i].constData())return false;
    return true;
}
Bounds3D objectLocalBounds(const Scene3D &scene,const ModelInstance3D &object){
    const auto model=scene.models.value(object.modelId);if(!model)return {};
    return object.skin&&!pose3DIsIdentity(object.pose)?posedModelBounds3D(*model,object.rig,object.pose):model->bounds;
}
QRectF projectedBounds(const Scene3D &scene,const ModelInstance3D &o,QSize size){
    auto model=scene.models.value(o.modelId);if(!model||size.isEmpty())return {};
    const auto matrix=cameraProjectionMatrix(scene.camera,float(size.width())/size.height())*cameraViewMatrix(scene.camera)*instanceMatrix(o);
    QRectF rect; bool first=true;
    const auto bounds=objectLocalBounds(scene,o);
    for(int i=0;i<8;++i){const auto &b=bounds;
        QVector4D p=matrix*QVector4D(i&1?b.maximum.x():b.minimum.x(),i&2?b.maximum.y():b.minimum.y(),i&4?b.maximum.z():b.minimum.z(),1);
        if(p.w()<=.00001f)continue; p/=p.w();
        QPointF point((p.x()+1)*.5*size.width(),(1-p.y())*.5*size.height());
        if(first){rect=QRectF(point,QSizeF());first=false;}else rect=QRectF(QPointF(qMin(rect.left(),point.x()),qMin(rect.top(),point.y())),QPointF(qMax(rect.right(),point.x()),qMax(rect.bottom(),point.y())));
    }return rect;
}
}
Scene3DController::Scene3DController(Canvas *canvas):QObject(canvas),m_canvas(canvas),m_renderer(new Scene3DRenderService(this)){
    m_skinViewId=QUuid::createUuid().toRfc4122();
    m_skinRenderTimer=new QTimer(this);m_skinRenderTimer->setSingleShot(true);m_skinRenderTimer->setInterval(33);
    connect(m_skinRenderTimer,&QTimer::timeout,this,[this]{requestRenders();});
    connect(canvas,&Canvas::viewChanged,this,[this]{if(m_weightStroke)endEdit();prepareWeightProjection();});
    connect(canvas,&Canvas::toolChanged,this,[this]{
        if(m_canvas->tool()!=Canvas::Tool::SceneRig)m_skinPreviewRotation={};
        invalidateSkinView();
    });
    connect(canvas->document(),&Document::changed,this,&Scene3DController::refresh);
    connect(canvas->document(),&Document::frameChanged,this,&Scene3DController::refresh);
    connect(canvas->document(),&Document::selectionChanged,this,[this]{refresh();});
    connect(m_renderer,&Scene3DRenderService::frameReady,this,[this](quint64 id,const QImage &image){
        if(id!=m_request)return;cacheScene3DPreview(m_requestedKey,image);m_lastImage=image;m_lastImageCamera=m_requestedCamera;m_rendering=false;
        m_canvas->invalidateArtworkCache();emit previewChanged();emit changed();
    });
    connect(m_renderer,&Scene3DRenderService::failed,this,[this](quint64 id,const QString &message){
        if(id!=m_request)return;m_rendering=false;m_requestedKey.clear();clearPendingScene3DPreviews();
        m_canvas->invalidateArtworkCache();emit previewChanged();emit warning(message);emit changed();
    });
    connect(this,&Scene3DController::warning,canvas,&Canvas::actionBlocked);
}
Scene3DController::~Scene3DController(){resetSkinEditing();}
void Scene3DController::reset(){
    cancelEdit();resetSkinEditing();m_renderer->clear();clearPendingScene3DPreviews();m_request=0;m_requestedKey.clear();m_lastImage={};
    m_layerId.clear();m_selected.clear();m_selectedJoint.clear();m_observedScene.reset();m_evaluatedScene.reset();m_observedFrame={};m_lastImageCamera.reset();
    m_mirrorEdits=true;m_rigVisible=true;
    m_hadProject=false;m_wasEditable=false;m_rendering=false;
}
int Scene3DController::sceneLayerIndex()const{
    if(!m_canvas->hasProject())return -1;
    const auto &layers=m_canvas->shot().layers;
    for(int i=0;i<layers.size();++i)if(layers[i].kind==LayerKind::Scene3D&&layers[i].scene3d)return i;
    return -1;
}
const Scene3D *Scene3DController::scene()const{return m_evaluatedScene?&*m_evaluatedScene:nullptr;}
const ModelInstance3D *Scene3DController::selectedObject()const{
    const auto *s=scene();if(s)for(const auto &o:s->objects)if(o.id==m_selected)return &o;return nullptr;
}
bool Scene3DController::canEdit()const{
    const int i=sceneLayerIndex();return i>=0&&layerCanEdit(m_canvas->shot().layers,i)&&!layerPositionLocked(m_canvas->shot().layers,i);
}
void Scene3DController::refresh(){
    if(m_before&&!m_canvas->document()->externalEditActive()&&!m_mutating){m_before.reset();m_dragging=false;}
    if(m_cameraEditing&&!m_canvas->document()->cameraEditActive()){m_cameraEditing=false;m_dragging=false;}
    const int i=sceneLayerIndex();
    const QString layerId=i<0?QString():m_canvas->shot().layers[i].id;
    if(i<0)m_evaluatedScene.reset();
    else {
        m_evaluatedScene=*m_canvas->shot().layers[i].scene3d;
        m_evaluatedScene->camera=evaluateSceneCamera(m_canvas->shot(),layerId,m_canvas->document()->currentFrame());
    }
    const auto *current=scene();
    const QSize frame=m_canvas->hasProject()?QSize(m_canvas->project().width,m_canvas->project().height):QSize();
    const bool editable=canEdit();
    const bool sameScene=current?m_observedScene&&*m_observedScene==*current:!m_observedScene;
    if(sameScene && layerId==m_layerId && frame==m_observedFrame && editable==m_wasEditable
        && m_hadProject==m_canvas->hasProject() && (!current || !m_requestedKey.isEmpty())){
        // A 2D shot-camera change leaves the 3D image intact but changes the
        // widget-space surface used by weight painting.
        prepareWeightProjection();return;
    }
    if(layerId!=m_layerId){m_renderer->clear();clearPendingScene3DPreviews();m_requestedKey.clear();m_lastImage={};m_lastImageCamera.reset();m_request=0;}
    m_layerId=layerId;m_observedScene=current?std::optional<Scene3D>(*current):std::nullopt;
    m_observedFrame=frame;m_hadProject=m_canvas->hasProject();m_wasEditable=editable;
    if(!selectedObject())m_selected.clear();
    if(!selectedJoint())m_selectedJoint.clear();
    requestRenders();prepareWeightProjection();emit changed();m_canvas->update();
}
void Scene3DController::requestRenders(){
    const auto *s=scene();if(!s){m_renderer->clear();m_requestedKey.clear();m_renderBaseKey.clear();m_lastImage={};m_lastImageCamera.reset();m_rendering=false;return;}
    const QSize frame(m_canvas->project().width,m_canvas->project().height);
    m_renderBaseKey=scene3DCacheKey(*s,frame);
    auto snapshot=*s; auto options=skinRenderOptions();
    QByteArray key=m_renderBaseKey;
    if(!options.skinObjectId.isEmpty())for(auto &object:snapshot.objects)if(object.id==options.skinObjectId && object.skin){
        if(m_paintedSkin)object.skin=m_paintedSkin;
        // A retained snapshot forces subsequent writes to detach. Comparing
        // buffer identity avoids packing/hashing millions of weights on the UI.
        if(!m_previewSkin || !sameSkinStorage(*m_previewSkin,*object.skin)){
            m_previewSkin=*object.skin;++m_skinViewRevision;
        }
        options.skinRevision=m_skinViewRevision;
        QDataStream stream(&key,QIODevice::Append);
        stream<<m_skinViewId<<options.skinObjectId<<options.skinJointId<<options.skinRotation<<options.weights<<options.skinRevision<<options.restPose;
        for(const auto &joint:object.rig.joints)stream<<joint.id<<joint.parentId<<joint.position;
        key=QCryptographicHash::hash(key,QCryptographicHash::Sha256);break;
    }
    if(key==m_requestedKey)return;
    m_requestedKey=key;
    m_requestedCamera=snapshot.camera;
    const auto completed=cachedScene3DPreview(key,true);
    if(!completed.isNull()){
        m_renderer->clear();m_request=0;m_lastImage=completed;m_lastImageCamera=snapshot.camera;m_rendering=false;
        m_canvas->invalidateArtworkCache();emit previewChanged();return;
    }
    // Never pair drawing/handles at this frame with a different camera image.
    // Weight-paint feedback may retain its previous image only at the same pose.
    if(!m_lastImageCamera || *m_lastImageCamera!=snapshot.camera){m_lastImage={};clearPendingScene3DPreviews();}
    // A transient weight image must never be a fallback for an ordinary scene.
    if(options.skinObjectId.isEmpty())m_lastImage=scene3DPreview(*s,frame,true);
    if(!m_lastImage.isNull())cacheScene3DPreview(key,m_lastImage,false);
    m_rendering=true;
    m_request=m_renderer->request(snapshot,frame.scaled(QSize(2048,2048),Qt::KeepAspectRatio),options);
}
quint64 Scene3DController::renderCount()const{return m_renderer->renderCount();}
bool Scene3DController::createScene(){
    if(!m_canvas->hasProject())return false;
    if(!m_canvas->resolvePendingTransform())return false;
    m_canvas->finishStroke();endEdit();
    auto *doc=m_canvas->document();int index=sceneLayerIndex();
    if(index>=0){doc->selectLayer(index);return true;}
    if(doc->editingGestureActive()||doc->layerPropertyEditActive())return false;
    auto after=doc->project();auto &layers=after.shots[doc->activeShotIndex()].layers;
    if(layers.size()>=(after.shots[doc->activeShotIndex()].animationTracks.isEmpty()?maxDrawingLayers:maxAnimationLayers)){
        emit warning("This shot has reached its stored layer limit.");return false;
    }
    auto layer=RasterLayer::create("3D reference");layer.kind=LayerKind::Scene3D;layer.scene3d=Scene3D{};
    const auto id=layer.id;layers.prepend(layer);
    if(!doc->commit(doc->project(),after,"Add 3D scene",id))return false;
    emit sceneAdded();return true;
}
bool Scene3DController::addModel(Model3DPtr model,const QString &name){
    if(!model)return false;
    if(!m_canvas->hasProject()){emit warning("Create or open a project before adding a 3D model.");return false;}
    if(!createScene()||!canEdit()){emit warning("Show and unlock the 3D layer and its position to add a model.");return false;}
    if(scene()->objects.size()>=maxScene3DObjects){emit warning("This scene has reached its 128-object limit.");return false;}
    if(!scene()->models.contains(model->id)&&scene()->models.size()>=maxScene3DModels){emit warning("This scene has reached its model limit.");return false;}
    const QString id=uuid();
    const bool ok=mutate("Add 3D model",[&](Scene3D &s){
        const bool first=s.objects.isEmpty();s.models.insert(model->id,model);ModelInstance3D o;o.id=id;o.name=name.isEmpty()?model->name:name;o.modelId=model->id;s.objects.append(o);
        if(first&&!s.camera.locked)s.camera=fitCamera3D(s.camera,sceneBounds(s),float(m_canvas->project().width)/m_canvas->project().height);
    });
    if(ok){m_selected=id;selectTool(false);emit changed();}return ok;
}
void Scene3DController::selectObject(const QString &id){
    endEdit();
    if(m_selected!=id){m_selectedJoint.clear();m_skinPreviewRotation={};}
    m_selected=id; if(!selectedObject())m_selected.clear();
    if(sceneLayerIndex()>=0)m_canvas->document()->selectLayer(sceneLayerIndex());invalidateSkinView();emit changed();m_canvas->update();
}
bool Scene3DController::beginEdit(const QString &label){
    if(m_cameraEditing)endEdit();
    if(m_before)return true;
    if(!canEdit()){emit warning("Show and unlock the 3D layer and its position to edit the scene.");return false;}
    if(!m_canvas->resolvePendingTransform())return false;m_canvas->finishStroke();
    auto *doc=m_canvas->document();if(doc->editingGestureActive()||doc->layerPropertyEditActive())return false;
    doc->selectLayer(sceneLayerIndex());m_before=doc->project();m_editLabel=label;doc->m_externalEditActive=true;emit changed();return true;
}
bool Scene3DController::beginCameraEdit(const QString &label){
    if(m_cameraEditing)return true;
    if(m_before)endEdit();
    if(!canEdit()||!scene()||scene()->camera.locked)return false;
    if(!m_canvas->resolvePendingTransform())return false;
    m_canvas->finishStroke();
    auto *doc=m_canvas->document();doc->selectLayer(sceneLayerIndex());
    m_cameraEditing=doc->beginCameraEdit(m_layerId,label);
    if(m_cameraEditing)emit changed();
    return m_cameraEditing;
}
void Scene3DController::endEdit(){
    if(m_resolvingEditors)return;
    const QScopedValueRollback<bool> editorGuard(m_resolvingEditors,true);
    emit editResolutionRequested(true);
    finishWeightStroke(true);
    if(m_cameraEditing){m_cameraEditing=false;m_dragging=false;m_canvas->document()->commitCameraEdit();emit changed();return;}
    m_dragging=false;m_poseDrag=PoseDrag::None;if(!m_before)return;auto *doc=m_canvas->document();
    auto before=std::move(*m_before);m_before.reset();const auto after=doc->project();doc->m_externalEditActive=false;
    const int i=sceneLayerIndex();
    if(i>=0&&before.shots[doc->activeShotIndex()].layers[i].scene3d!=after.shots[doc->activeShotIndex()].layers[i].scene3d){
        const auto &oldScene=before.shots[doc->activeShotIndex()].layers[i].scene3d;
        const auto &newScene=after.shots[doc->activeShotIndex()].layers[i].scene3d;
        const QSize frame(after.width,after.height);
        const bool restOnly=oldScene&&newScene&&scene3DCacheKey(*oldScene,frame)==scene3DCacheKey(*newScene,frame);
        const QScopedValueRollback<bool> restGuard(m_changingRestSkeleton,m_changingRestSkeleton||restOnly);
        m_mutating=true;doc->commit(before,after,m_editLabel,m_layerId);m_mutating=false;
    }emit changed();
}
void Scene3DController::cancelEdit(){
    if(m_resolvingEditors)return;
    const QScopedValueRollback<bool> editorGuard(m_resolvingEditors,true);
    emit editResolutionRequested(false);
    finishWeightStroke(false);
    if(m_cameraEditing){m_cameraEditing=false;m_dragging=false;m_canvas->document()->cancelCameraEdit();emit changed();return;}
    m_dragging=false;m_poseDrag=PoseDrag::None;if(!m_before)return;auto *doc=m_canvas->document();const auto before=std::move(*m_before);m_before.reset();doc->m_externalEditActive=false;doc->apply(before,m_layerId);emit changed();
}
bool Scene3DController::mutate(const QString &label,const std::function<void(Scene3D&)> &edit){
    if(m_cameraEditing)endEdit();
    const bool own=!editing();if(own&&!beginEdit(label))return false;
    auto *doc=m_canvas->document();const int i=sceneLayerIndex();if(i<0)return false;
    Scene3D next=*doc->m_project.shots[doc->activeShotIndex()].layers[i].scene3d;edit(next);
    // Full immutable geometry validation occurs on import/load. Scene transforms are bounded here.
    const auto error=validateScene3D(next);if(!error.isEmpty()){emit warning(error);if(own)cancelEdit();return false;}
    const auto &current = *doc->m_project.shots[doc->activeShotIndex()].layers[i].scene3d;
    const auto skinBytes = [](const Scene3D &scene) {
        qint64 bytes = 0;
        for (const auto &object : scene.objects) if (object.skin) bytes += skin3DByteSize(*object.skin);
        return bytes;
    };
    const auto extraSkinBytes = skinBytes(next) - skinBytes(current);
    if (extraSkinBytes > 0) {
        // Shared model geometry does not imply shared per-instance bindings.
        // Account for other shots before allowing duplication or a new binding.
        qint64 bytes = extraSkinBytes;
        for (const auto &shot : doc->m_project.shots) for (const auto &layer : shot.layers)
            if (layer.scene3d) bytes += skinBytes(*layer.scene3d);
        if (bytes > maxProjectSkin3DBytes) {
            emit warning("Project skin weights exceed the 48 MiB decoded limit.");
            if (own) cancelEdit();
            return false;
        }
    }
    bool newAsset = false;
    for (auto it = next.models.cbegin(); it != next.models.cend(); ++it)
        if (!current.models.contains(it.key())) { newAsset = true; break; }
    if (newAsset) {
        // Importing an asset can exceed the project-wide decoded-memory budget.
        // Validate that rare change fully; ordinary camera/object drags retain
        // their cheap metadata-only validation above.
        auto candidate = doc->m_project;
        candidate.shots[doc->activeShotIndex()].layers[i].scene3d = next;
        const auto validation = candidate.validate();
        if (!validation.isEmpty()) {
            emit warning(validation); if (own) cancelEdit(); return false;
        }
    }
    doc->m_project.shots[doc->activeShotIndex()].layers[i].scene3d=std::move(next);emit doc->changed();if(own)endEdit();return true;
}
void Scene3DController::removeSelected(){const auto *o=selectedObject();if(!o||o->locked)return;const auto id=o->id;
    mutate("Remove 3D object",[&](Scene3D&s){s.objects.removeIf([&](const auto &v){return v.id==id;});QSet<QString>used;for(const auto&v:s.objects)used.insert(v.modelId);for(auto it=s.models.begin();it!=s.models.end();)if(!used.contains(it.key()))it=s.models.erase(it);else++it;});
}
void Scene3DController::duplicateSelected(){const auto *o=selectedObject();if(!o||scene()->objects.size()>=maxScene3DObjects)return;auto copy=*o;copy.id=uuid();copy.name+=" copy";copy.locked=false;
    if(mutate("Duplicate 3D object",[&](Scene3D&s){s.objects.append(copy);})){m_selected=copy.id;m_skinPreviewRotation={};invalidateSkinView();emit changed();}
}
void Scene3DController::updateObject(const ModelInstance3D &value){const auto *old=selectedObject();if(!old||old->id!=value.id||old->modelId!=value.modelId)return;
    if(old->locked&&(old->position!=value.position||old->rotation!=value.rotation||old->scale!=value.scale||old->rig!=value.rig||old->skin!=value.skin||old->pose!=value.pose))return;
    if(old->skin && old->rig!=value.rig){emit warning("Unbind the model before changing its rest skeleton.");return;}
    mutate("Change 3D object",[&](Scene3D&s){for(auto&o:s.objects)if(o.id==value.id){o=value;break;}});
}
void Scene3DController::updateCamera(const Camera3D &camera){if(!scene())return;
    if(scene()->camera.locked){auto unlocked=scene()->camera;unlocked.locked=camera.locked;if(unlocked!=camera)return;}
    auto *doc=m_canvas->document();
    if(m_cameraEditing)doc->previewSceneCamera(camera);
    else {
        if(m_before)endEdit();
        if(!m_canvas->resolvePendingTransform())return;
        m_canvas->finishStroke();
        doc->setSceneCamera(m_layerId,camera);
    }
}
void Scene3DController::setTextured(bool v){mutate("3D materials",[&](Scene3D&s){s.textured=v;});}
void Scene3DController::fitScene(){if(scene()&&!scene()->camera.locked)updateCamera(fitCamera3D(scene()->camera,sceneBounds(*scene()),float(m_canvas->project().width)/m_canvas->project().height));}
void Scene3DController::fitSelected(){if(!selectedObject()||scene()->camera.locked)return;auto s=*scene();s.objects={*selectedObject()};updateCamera(fitCamera3D(s.camera,sceneBounds(s),float(m_canvas->project().width)/m_canvas->project().height));}
void Scene3DController::resetSelected(){if(auto*o=selectedObject()){auto v=*o;v.position={};v.rotation={};v.scale={1,1,1};updateObject(v);}}
void Scene3DController::selectTool(bool camera){const auto requested=camera?Canvas::Tool::SceneCamera:Canvas::Tool::SceneObject;m_canvas->setTool(requested);if(m_canvas->tool()!=requested)return;if(sceneLayerIndex()>=0)m_canvas->document()->selectLayer(sceneLayerIndex());setManipulation(camera?Manipulation::Orbit:Manipulation::Move);}
void Scene3DController::setManipulation(Manipulation mode){if(m_manipulation==mode)return;endEdit();m_manipulation=mode;emit changed();}
bool Scene3DController::pointerPress(QPointF point,Qt::KeyboardModifiers){
    if(!sceneTool(m_canvas->tool()))return false;if(!scene()||!canEdit())return true;
    if(m_canvas->tool()==Canvas::Tool::SceneRig){if(m_rigEditMode==RigEditMode::Pose)return posePointerPress(point);return m_rigEditMode==RigEditMode::Weights?weightPointerPress(point):rigPointerPress(point);}
    if(m_canvas->tool()==Canvas::Tool::SceneObject){
        QString hit;float depth=std::numeric_limits<float>::max();const QSize frame(m_canvas->project().width,m_canvas->project().height);
        for(const auto&o:scene()->objects)if(o.visible&&projectedBounds(*scene(),o,frame).contains(point)){
            const auto center=instanceMatrix(o).map(objectLocalBounds(*scene(),o).center());float d=(cameraPosition(scene()->camera)-center).lengthSquared();if(d<depth){depth=d;hit=o.id;}}
        selectObject(hit);if(!selectedObject()||selectedObject()->locked)return true;
    }else if(scene()->camera.locked){emit warning("Unlock the 3D camera to move it.");return true;}
    const bool camera=m_canvas->tool()==Canvas::Tool::SceneCamera;
    if(camera?beginCameraEdit("Move 3D camera"):beginEdit("Transform 3D object")){m_dragStart=point;m_dragScene=*scene();m_dragging=true;}return true;
}
void Scene3DController::pointerMove(QPointF point,Qt::KeyboardModifiers modifiers){
    if(m_canvas->tool()==Canvas::Tool::SceneRig){if(m_rigEditMode==RigEditMode::Pose)posePointerMove(point,modifiers);else if(m_rigEditMode==RigEditMode::Weights)weightPointerMove(point);else rigPointerMove(point,modifiers);return;}
    if(!m_dragging||!editing())return;auto delta=point-m_dragStart;if(modifiers&Qt::ShiftModifier)delta*=.1;
    const auto &camera=m_dragScene.camera;const auto inv=cameraViewMatrix(camera).inverted();
    const float span=camera.orthographic?camera.orthoScale:2*camera.distance*qTan(qDegreesToRadians(camera.fieldOfView*.5f));
    const auto world=inv.mapVector(QVector3D(delta.x(),-delta.y(),0))*span/m_canvas->project().height;
    if(m_canvas->tool()==Canvas::Tool::SceneCamera){auto c=camera;
        if(m_manipulation==Manipulation::Pan)c.target-=world;
        else if(m_manipulation==Manipulation::Dolly){const auto factor=std::exp(delta.y()*.005);if(c.orthographic)c.orthoScale=qBound(.01, c.orthoScale*factor,100000.0);else c.distance=qBound(.01,c.distance*factor,100000.0);}
        else{c.yaw+=delta.x()*.3;c.pitch=qBound(-89.0,double(c.pitch+delta.y()*.3),89.0);}updateCamera(c);
    }else for(const auto&start:m_dragScene.objects)if(start.id==m_selected){auto o=start;
        if(m_manipulation==Manipulation::Rotate)o.rotation.setY(o.rotation.y()+delta.x()*.5);
        else if(m_manipulation==Manipulation::Scale)o.scale*=qBound(.001,std::exp(-delta.y()*.005),1000.0);
        else o.position+=world;updateObject(o);break;}
}
void Scene3DController::pointerRelease(){if(m_dragging)endEdit();}
void Scene3DController::paintHandles(QPainter &p)const{
    if(m_canvas->tool()==Canvas::Tool::SceneRig){paintRig(p);if(m_rigEditMode==RigEditMode::Pose)paintPose(p);return;}
    if(m_canvas->tool()!=Canvas::Tool::SceneObject||!selectedObject()||!selectedObject()->visible)return;
    if(!scene()->visible)return;
    const auto &layers=m_canvas->shot().layers;
    int layer=sceneLayerIndex();
    for(int depth=0;layer>=0&&depth<=8;++depth){
        if(!layers[layer].visible)return;
        const auto parent=layers[layer].parentId;if(parent.isEmpty())break;
        layer=-1;for(int i=0;i<layers.size();++i)if(layers[i].id==parent){layer=i;break;}
    }
    const auto bounds=projectedBounds(*scene(),*selectedObject(),{m_canvas->project().width,m_canvas->project().height});if(bounds.isEmpty())return;
    p.save();p.setTransform(m_canvas->artworkViewTransform());QPen pen(QColor(ui::accent),1.5,Qt::DashLine);pen.setCosmetic(true);p.setPen(pen);p.setBrush(Qt::NoBrush);p.drawRect(bounds);p.restore();
}
}
