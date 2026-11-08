#include "document.h"
#include <algorithm>
#include "reference3d_layer_serialization.h"
#include <QSet>

namespace toon {
namespace {
int layerIndex(const Shot &shot,const QString &id){for(int i=0;i<shot.layers.size();++i)if(shot.layers[i].id==id)return i;return -1;}
bool fail(QString *error,const QString &message){if(error)*error=message;return false;}
QString editable(const Shot &shot,int index,int frame){
    if(index<0||index>=shot.layers.size()||!shot.layers[index].reference3d)return "Choose a 3D reference layer.";
    if(!layerPresentAtFrame(shot,shot.layers[index].id,frame))return "Choose a reference exposed at the current frame.";
    if(!layerCanEdit(shot.layers,index))return "Show and unlock the reference layer and its folders before posing.";
    if(layerPositionLocked(shot.layers,index))return "Unlock the reference layer position before changing its scene.";
    return {};
}
}
QString Document::addReference3DLayer(const ReferenceScene &scene,const QString &requestedName,QString *error){
    if(error)error->clear();
    if(!m_open||editingGestureActive()||layerPropertyEditActive()){fail(error,"Finish the active edit before adding a 3D reference layer.");return {};}
    const auto sceneError=validateReferenceScene(scene);if(!sceneError.isEmpty()){fail(error,sceneError);return {};}
    const auto &shot=activeShot();if(shot.layers.size()>=(shot.animationTracks.isEmpty()?maxDrawingLayers:maxAnimationLayers)){fail(error,"The drawing layer limit was reached.");return {};}
    const auto &selected=shot.layers[m_active];
    // A new sibling may be inserted above a locked leaf, but never in a locked folder.
    QString parent=selected.group?selected.id:selected.parentId;QSet<QString>seen;
    for(QString id=parent;!id.isEmpty();){const int at=layerIndex(shot,id);if(at<0||seen.contains(id)||shot.layers[at].locked){fail(error,"Unlock the destination folder before adding a reference.");return {};}seen.insert(id);id=shot.layers[at].parentId;}
    auto layer=RasterLayer::create(requestedName.trimmed().isEmpty()?(scene.name.trimmed().isEmpty()?QString("3D reference"):scene.name.trimmed()):requestedName.trimmed());
    layer.kind=LayerKind::Reference3D;layer.reference3d=scene;layer.parentId=parent;
    auto next=m_project;auto &layers=next.shots[activeShotIndex()].layers;int insertion=m_active+1;
    if(selected.group){while(insertion<layers.size()&&layerIsDescendant(layers,insertion,selected.id))++insertion;}
    layers.insert(insertion,layer);const auto problem=next.validate();if(!problem.isEmpty()){fail(error,problem);return {};}
    if(!commit(m_project,next,"Add 3D reference layer",layer.id))return {};
    return layer.id;
}
bool Document::setReference3DScene(const QString &id,const ReferenceScene &scene,QString *error){
    if(error)error->clear();if(!m_open||editingGestureActive()||layerPropertyEditActive())return fail(error,"Finish the active edit before changing a 3D reference.");
    const int at=layerIndex(activeShot(),id);const auto reason=editable(activeShot(),at,m_currentFrame);if(!reason.isEmpty())return fail(error,reason);
    const auto problem=validateReferenceScene(scene);if(!problem.isEmpty())return fail(error,problem);
    if(referenceLayerScenesEqual(*activeShot().layers[at].reference3d,scene))return true;
    auto next=m_project;next.shots[activeShotIndex()].layers[at].reference3d=scene;
    const auto validation=next.validate();if(!validation.isEmpty())return fail(error,validation);
    return commit(m_project,next,"Change 3D reference",id);
}
bool Document::beginReference3DEdit(const QString &id,QString *error){
    if(error)error->clear();if(!m_open||editingGestureActive()||layerPropertyEditActive())return fail(error,"Finish the active edit before posing the reference.");
    const int at=layerIndex(activeShot(),id);const auto reason=editable(activeShot(),at,m_currentFrame);if(!reason.isEmpty())return fail(error,reason);
    m_beforeReference3DEdit=m_project;m_reference3DEditLayerId=id;m_reference3DEditShotId=activeShotId();m_reference3DEditFrame=m_currentFrame;return true;
}
bool Document::previewReference3DScene(const ReferenceScene &scene,QString *error){
    if(error)error->clear();if(!reference3DEditActive())return fail(error,"Start a 3D reference edit first.");
    if(m_reference3DEditShotId!=activeShotId()||m_reference3DEditFrame!=m_currentFrame){cancelReference3DEdit();return fail(error,"The active drawing changed during reference posing.");}
    const int at=layerIndex(activeShot(),m_reference3DEditLayerId);const auto reason=editable(activeShot(),at,m_currentFrame);if(!reason.isEmpty()){cancelReference3DEdit();return fail(error,reason);}
    const auto problem=validateReferenceScene(scene);if(!problem.isEmpty())return fail(error,problem);
    if(referenceLayerScenesEqual(*activeShot().layers[at].reference3d,scene))return true;
    m_project.shots[activeShotIndex()].layers[at].reference3d=scene;emit changed();return true;
}
void Document::commitReference3DEdit(){
    if(!reference3DEditActive())return;const auto before=*m_beforeReference3DEdit;const auto id=m_reference3DEditLayerId;
    const auto shotId=m_reference3DEditShotId;const int frame=m_reference3DEditFrame;
    m_beforeReference3DEdit.reset();m_reference3DEditLayerId.clear();m_reference3DEditShotId.clear();m_reference3DEditFrame=-1;
    if(activeShotId()!=shotId||m_currentFrame!=frame){apply(before);return;}
    const int at=layerIndex(activeShot(),id);const auto reason=editable(activeShot(),at,m_currentFrame);
    const auto validation=m_project.validate();if(!reason.isEmpty()||!validation.isEmpty()){apply(before);emit warning(reason.isEmpty()?validation:reason);return;}
    const auto oldShot=std::find_if(before.shots.begin(),before.shots.end(),[&](const Shot&s){return s.id==shotId;});
    if(oldShot!=before.shots.end()){const int oldAt=layerIndex(*oldShot,id);if(oldAt>=0&&referenceLayerScenesEqual(*oldShot->layers[oldAt].reference3d,*activeShot().layers[at].reference3d)){emit changed();return;}}
    commit(before,m_project,"Pose 3D reference",id);
}
void Document::cancelReference3DEdit(){
    if(!reference3DEditActive())return;const auto before=*m_beforeReference3DEdit;m_beforeReference3DEdit.reset();m_reference3DEditLayerId.clear();m_reference3DEditShotId.clear();m_reference3DEditFrame=-1;apply(before);
}
} // namespace toon
