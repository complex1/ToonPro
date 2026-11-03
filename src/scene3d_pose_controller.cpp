#include "scene3d_controller.h"
#include "canvas.h"
#include "ui_theme.h"
#include <QPainter>
#include <QtMath>
#include <algorithm>
#include <cmath>

namespace toon {
namespace {
const RigJoint3D *chainRoot(const Rig3D &rig,const QString &id) {
    const auto *end=rigJoint(rig,id);const auto *middle=end?rigJoint(rig,end->parentId):nullptr;
    return middle?rigJoint(rig,middle->parentId):nullptr;
}
bool controlsJoint(const Rig3D &rig,const IKTarget3D &target,const QString &id) {
    const auto *end=rigJoint(rig,target.jointId);const auto *middle=end?rigJoint(rig,end->parentId):nullptr;
    return middle&&(middle->id==id||middle->parentId==id);
}
QVector3D pointFor(const PoseEvaluation3D &pose,const QString &id) {
    const int i=pose.jointIds.indexOf(id);return i<0?QVector3D():pose.positions[i];
}
bool followUnpinnedControls(const Rig3D &rig,const Pose3D &before,Pose3D &after,QString *error,const QString &active={}) {
    const bool any=std::any_of(after.targets.cbegin(),after.targets.cend(),[](const auto &target){return !target.pinned;});
    if(!any)return true;
    const auto oldFrame=evaluatePose3D(rig,before,error);if(!oldFrame)return false;
    const auto newFrame=evaluatePose3D(rig,after,error);if(!newFrame)return false;
    for(auto &target:after.targets)if(!target.pinned&&target.jointId!=active) {
        const auto previous=std::find_if(before.targets.cbegin(),before.targets.cend(),[&](const auto &entry){return entry.jointId==target.jointId&&!entry.pinned;});
        const auto *root=chainRoot(rig,target.jointId);if(previous==before.targets.cend()||!root)continue;
        const int index=oldFrame->jointIds.indexOf(root->id);
        if(oldFrame->positions[index]==newFrame->positions[index]&&oldFrame->rotations[index]==newFrame->rotations[index])continue;
        const auto rotation=(newFrame->rotations[index]*oldFrame->rotations[index].conjugated()).normalized();
        const auto carried=[&](QVector3D value){return newFrame->positions[index]+rotation.rotatedVector(value-oldFrame->positions[index]);};
        target.target=carried(previous->target);target.pole=carried(previous->pole);
    }
    return true;
}
bool followIKEdit(const Rig3D &rig,const Pose3D &before,Pose3D &after,const QString &active,QString *error) {
    const bool retained=std::any_of(after.targets.cbegin(),after.targets.cend(),[&](const auto &target){return target.jointId==active;});
    if(retained) {
        const auto *end=rigJoint(rig,active);const auto *middle=end?rigJoint(rig,end->parentId):nullptr;
        if(middle)after.targets.removeIf([&](const auto &target){
            return !target.pinned&&target.jointId!=active
                &&(controlsJoint(rig,target,middle->id)||controlsJoint(rig,target,middle->parentId));
        });
    }
    // The edited control keeps the artist's requested coordinates. Other
    // independent unpinned chains follow their newly posed parent frames.
    return followUnpinnedControls(rig,before,after,error,active);
}
IKTarget3D initialTarget(const Rig3D &rig,const Pose3D &pose,const QString &id) {
    IKTarget3D target;target.jointId=id;target.pinned=false;
    const auto evaluated=evaluatePose3D(rig,pose);const auto *root=chainRoot(rig,id);
    const auto *end=rigJoint(rig,id);if(!evaluated||!root||!end)return target;
    const auto a=pointFor(*evaluated,root->id),b=pointFor(*evaluated,end->parentId),c=pointFor(*evaluated,id);
    const auto axis=(c-a).normalized();auto bend=b-a-axis*QVector3D::dotProduct(b-a,axis);
    if(bend.lengthSquared()<1e-8f){bend=QVector3D::crossProduct(axis,{1,0,0});if(bend.lengthSquared()<1e-8f)bend=QVector3D::crossProduct(axis,{0,1,0});}
    target.target=c;target.pole=b+bend.normalized()*std::max(.1f,(b-a).length()+(c-b).length());return target;
}
std::optional<QPointF> projectPoint(const QMatrix4x4 &matrix,QVector3D position,QSize frame) {
    auto p=matrix*QVector4D(position,1);if(p.w()<=.00001f)return {};p/=p.w();
    if(p.z()<-1||p.z()>1)return {};
    return QPointF((p.x()+1)*frame.width()/2.,(1-p.y())*frame.height()/2.);
}
qreal distanceToSegment(QPointF p,QPointF a,QPointF b) {
    const auto d=b-a;const auto l=QPointF::dotProduct(d,d);if(l<.001)return QLineF(p,a).length();
    return QLineF(p,a+d*std::clamp(QPointF::dotProduct(p-a,d)/l,0.,1.)).length();
}
}
bool Scene3DController::canPose() const {
    const auto *object=selectedObject();return canEdit()&&scene()->visible&&object&&object->visible&&!object->locked
        &&object->skin&&!object->rig.joints.isEmpty()&&!m_skinBindingBusy;
}
const Pose3D *Scene3DController::selectedPose() const {const auto *object=selectedObject();return object?&object->pose:nullptr;}
void Scene3DController::setPoseMode(PoseMode mode){if(mode==m_poseMode)return;endEdit();m_poseMode=mode;emit changed();m_canvas->update();}
void Scene3DController::setPoseRotationAxis(int axis){if(axis<0||axis>2||axis==m_poseRotationAxis)return;endEdit();m_poseRotationAxis=axis;emit changed();m_canvas->update();}
void Scene3DController::setIKHandle(IKHandle handle){if(handle==m_ikHandle)return;endEdit();m_ikHandle=handle;emit changed();m_canvas->update();}
QVector3D Scene3DController::poseRotation() const {return selectedPose()?poseJointRotation3D(*selectedPose(),m_selectedJoint):QVector3D();}
QVector3D Scene3DController::poseRootTranslation() const {return selectedPose()?selectedPose()->rootTranslation:QVector3D();}
bool Scene3DController::canRotateSelectedPoseJoint() const {
    if(!canPose()||!selectedJoint())return false;
    for(const auto &target:selectedPose()->targets)if(target.pinned&&controlsJoint(*selectedRig(),target,m_selectedJoint))return false;
    return true;
}
bool Scene3DController::mutatePose(const QString &label,const Pose3D &pose) {
    if(!canPose())return false;const auto error=validatePose3D(pose,*selectedRig());
    if(!error.isEmpty()){emit warning(error);emit changed();return false;}
    if(pose==*selectedPose())return true;
    const auto id=m_selected;return mutate(label,[&](Scene3D &s){for(auto &object:s.objects)if(object.id==id)object.pose=pose;});
}
void Scene3DController::updatePoseRotation(QVector3D rotation) {
    if(!canRotateSelectedPoseJoint()){if(canPose())emit warning("Unpin the IK control before rotating a joint it controls.");return;}
    auto pose=*selectedPose();QString error;
    // FK takes over unpinned chains containing this joint; pinned chains retain their target.
    pose.targets.removeIf([&](const IKTarget3D &target){return !target.pinned&&controlsJoint(*selectedRig(),target,m_selectedJoint);});
    if(!setPoseJointRotation3D(pose,*selectedRig(),m_selectedJoint,rotation,false,&error)){emit warning(error);emit changed();return;}
    if(!followUnpinnedControls(*selectedRig(),*selectedPose(),pose,&error)){emit warning(error);emit changed();return;}
    mutatePose("Rotate pose joint",pose);
}
void Scene3DController::updatePoseRootTranslation(QVector3D translation) {
    if(!canPose())return;auto pose=*selectedPose();pose.rootTranslation=translation;QString error;
    if(!followUnpinnedControls(*selectedRig(),*selectedPose(),pose,&error)){emit warning(error);emit changed();return;}
    mutatePose("Move pose body",pose);
}
bool Scene3DController::canUseSelectedIK() const {
    if(!canPose()||!selectedJoint())return false;const auto *root=chainRoot(*selectedRig(),m_selectedJoint);
    const auto *middle=rigJoint(*selectedRig(),selectedJoint()->parentId);
    return root&&middle&&(root->position-middle->position).length()>1e-6f&&(middle->position-selectedJoint()->position).length()>1e-6f;
}
std::optional<IKTarget3D> Scene3DController::selectedIKTarget() const {
    if(selectedPose())for(const auto &target:selectedPose()->targets)if(target.jointId==m_selectedJoint)return target;return {};
}
void Scene3DController::enableSelectedIK(bool enabled) {
    if(!canUseSelectedIK())return;endEdit();auto pose=*selectedPose();auto target=selectedIKTarget();QString error;
    if(enabled){if(target)return;auto value=initialTarget(*selectedRig(),pose,m_selectedJoint);
        if(!solvePoseIK3D(pose,*selectedRig(),value,&error)){emit warning(error);return;}
    }else{if(!target)return;target->pinned=false;
        if(!solvePoseIK3D(pose,*selectedRig(),*target,&error)){emit warning(error);return;}
        pose.targets.removeIf([&](const IKTarget3D &entry){return entry.jointId==m_selectedJoint;});
    }
    if(!followIKEdit(*selectedRig(),*selectedPose(),pose,m_selectedJoint,&error)){emit warning(error);emit changed();return;}
    mutatePose(enabled?"Add IK control":"Remove IK control",pose);
}
void Scene3DController::updateIKTarget(QVector3D value) {
    if(!canUseSelectedIK())return;auto pose=*selectedPose();auto target=selectedIKTarget().value_or(initialTarget(*selectedRig(),pose,m_selectedJoint));
    target.target=value;QString error;if(!solvePoseIK3D(pose,*selectedRig(),target,&error)){emit warning(error);emit changed();return;}
    if(!followIKEdit(*selectedRig(),*selectedPose(),pose,m_selectedJoint,&error)){emit warning(error);emit changed();return;}
    mutatePose("Move IK target",pose);
}
void Scene3DController::updateIKPole(QVector3D value) {
    if(!canUseSelectedIK())return;auto pose=*selectedPose();auto target=selectedIKTarget().value_or(initialTarget(*selectedRig(),pose,m_selectedJoint));
    target.pole=value;QString error;if(!solvePoseIK3D(pose,*selectedRig(),target,&error)){emit warning(error);emit changed();return;}
    if(!followIKEdit(*selectedRig(),*selectedPose(),pose,m_selectedJoint,&error)){emit warning(error);emit changed();return;}
    mutatePose("Move IK pole",pose);
}
void Scene3DController::setIKPinned(bool pinned) {
    if(!canUseSelectedIK())return;endEdit();auto pose=*selectedPose();auto target=selectedIKTarget().value_or(initialTarget(*selectedRig(),pose,m_selectedJoint));
    target.pinned=pinned;QString error;if(!solvePoseIK3D(pose,*selectedRig(),target,&error)){emit warning(error);emit changed();return;}
    if(!followIKEdit(*selectedRig(),*selectedPose(),pose,m_selectedJoint,&error)){emit warning(error);emit changed();return;}
    mutatePose(pinned?"Pin IK target":"Unpin IK target",pose);
}
JointLimit3D Scene3DController::selectedPoseLimit() const {
    if(selectedPose())for(const auto &limit:selectedPose()->limits)if(limit.jointId==m_selectedJoint)return limit;
    JointLimit3D result;result.jointId=m_selectedJoint;result.enabled=false;return result;
}
void Scene3DController::updatePoseLimit(const JointLimit3D &limit) {
    if(!canPose()||!selectedJoint()||limit.jointId!=m_selectedJoint)return;auto pose=*selectedPose();
    pose.limits.removeIf([&](const JointLimit3D &entry){return entry.jointId==limit.jointId;});pose.limits.append(limit);
    QString error=validatePose3D(pose,*selectedRig());
    if(!error.isEmpty()){emit warning(error);emit changed();return;}
    if(!setPoseJointRotation3D(pose,*selectedRig(),limit.jointId,poseJointRotation3D(pose,limit.jointId),false,&error)){emit warning(error);return;}
    if(!followUnpinnedControls(*selectedRig(),*selectedPose(),pose,&error)){emit warning(error);emit changed();return;}
    mutatePose("Change pose joint limits",pose);
}
void Scene3DController::mirrorPose(){if(!canPose())return;endEdit();auto pose=*selectedPose();QString error;
    if(!mirrorPose3D(pose,*selectedRig(),&error)){emit warning(error);return;}mutatePose("Mirror pose",pose);}
void Scene3DController::resetSelectedPose(){
    if(!canPose()||!selectedJoint())return;endEdit();auto pose=*selectedPose();
    const auto affected=[&](const IKTarget3D &target){return target.jointId==m_selectedJoint||controlsJoint(*selectedRig(),target,m_selectedJoint);};
    // Pins can hold a different evaluated FK frame after body/ancestor edits.
    // Preserve that frame before the requested joint reset releases them.
    const auto controls=pose.targets;QString error;
    for(auto target:controls)if(target.pinned&&affected(target)) {
        target.pinned=false;
        if(!solvePoseIK3D(pose,*selectedRig(),target,&error)){emit warning(error);emit changed();return;}
    }
    pose.targets.removeIf(affected);
    pose.joints.removeIf([&](const JointPose3D &entry){return entry.jointId==m_selectedJoint;});
    if(selectedJoint()->parentId.isEmpty())pose.rootTranslation={};
    if(!followUnpinnedControls(*selectedRig(),*selectedPose(),pose,&error)){emit warning(error);emit changed();return;}
    mutatePose("Reset pose joint",pose);
}
void Scene3DController::resetAllPose(){if(!canPose())return;endEdit();auto pose=*selectedPose();resetPose3D(pose);mutatePose("Reset pose",pose);}
QString Scene3DController::poseStatus() const {
    if(!selectedObject())return "Select a model";if(!selectedSkin())return "Bind the model to pose";
    if(!canPose())return "Show and unlock";if(!selectedJoint())return "Select a joint";
    if(m_poseMode==PoseMode::IK){if(!canUseSelectedIK())return "Select a two-bone endpoint";
        if(const auto target=selectedIKTarget())if(const auto evaluated=evaluatePose3D(*selectedRig(),*selectedPose()))
            if((pointFor(*evaluated,target->jointId)-target->target).length()>.001f)return "Target beyond reach or limits";
        return selectedIKTarget()?"IK control":"Drag to add IK";
    }
    return canRotateSelectedPoseJoint()?"FK rotation":"Unpin to rotate this joint";
}

bool Scene3DController::posePointerPress(QPointF artwork) {
    m_poseDrag=PoseDrag::None;if(!canPose()||!m_rigVisible)return true;
    const auto evaluated=evaluatePose3D(*selectedRig(),*selectedPose());if(!evaluated)return true;
    const QSize frame(m_canvas->project().width,m_canvas->project().height);
    const auto matrix=cameraProjectionMatrix(scene()->camera,float(frame.width())/frame.height())*cameraViewMatrix(scene()->camera)*instanceMatrix(*selectedObject());
    const auto view=m_canvas->artworkViewTransform();const auto pointer=view.map(artwork);
    const auto screen=[&](QVector3D p)->std::optional<QPointF>{auto q=projectPoint(matrix,p,frame);return q?std::optional<QPointF>(view.map(*q)):std::nullopt;};
    const auto selectedPosition=pointFor(*evaluated,m_selectedJoint);const auto selectedPoint=selectedJoint()?screen(selectedPosition):std::nullopt;
    if(m_poseMode==PoseMode::FK&&canRotateSelectedPoseJoint()&&selectedPoint&&std::abs(QLineF(pointer,*selectedPoint).length()-42)<9){
        m_poseDrag=PoseDrag::Rotation;m_posePivot=*selectedPoint;m_poseStartAngle=std::atan2(pointer.y()-m_posePivot.y(),pointer.x()-m_posePivot.x());
    }else if(m_poseMode==PoseMode::IK&&canUseSelectedIK()){
        const auto target=selectedIKTarget().value_or(initialTarget(*selectedRig(),*selectedPose(),m_selectedJoint));
        const auto position=m_ikHandle==IKHandle::Pole?target.pole:target.target;const auto handle=screen(position);
        if(handle&&QLineF(pointer,*handle).length()<14){m_poseDrag=m_ikHandle==IKHandle::Pole?PoseDrag::Pole:PoseDrag::Target;m_poseDragOrigin=position;}
    }
    if(m_poseDrag==PoseDrag::None){
        QString hit;qreal distance=11;
        for(int i=0;i<evaluated->jointIds.size();++i)if(auto p=screen(evaluated->positions[i])){
            const auto d=QLineF(pointer,*p).length();if(d<distance){distance=d;hit=evaluated->jointIds[i];}}
        if(hit.isEmpty())for(const auto &joint:selectedRig()->joints){auto a=screen(pointFor(*evaluated,joint.id));
            auto b=joint.parentId.isEmpty()?std::nullopt:screen(pointFor(*evaluated,joint.parentId));
            if(a&&b){const auto d=distanceToSegment(pointer,*a,*b);if(d<distance){distance=d;hit=joint.id;}}}
        selectJoint(hit);
        if(selectedJoint()&&m_poseMode==PoseMode::IK&&canUseSelectedIK()){
            m_poseDrag=PoseDrag::Target;m_ikHandle=IKHandle::Target;
            m_poseDragOrigin=selectedIKTarget().value_or(initialTarget(*selectedRig(),*selectedPose(),m_selectedJoint)).target;
        }else if(selectedJoint()&&m_poseMode==PoseMode::FK&&selectedJoint()->parentId.isEmpty()){
            m_poseDrag=PoseDrag::Root;m_poseDragOrigin=pointFor(*evaluated,m_selectedJoint);
        }
    }
    if(m_poseDrag!=PoseDrag::None&&beginEdit(m_poseDrag==PoseDrag::Rotation?"Rotate pose joint":m_poseDrag==PoseDrag::Root?"Move pose body":"Move IK control")){
        m_dragStart=artwork;m_dragScene=*scene();m_dragging=true;
    }else m_poseDrag=PoseDrag::None;
    return true;
}
void Scene3DController::posePointerMove(QPointF artwork,Qt::KeyboardModifiers modifiers){
    if(!m_dragging||!editing()||!canPose()||!selectedJoint()||m_poseDrag==PoseDrag::None)return;
    const ModelInstance3D *original=nullptr;for(const auto &object:m_dragScene.objects)if(object.id==m_selected)original=&object;if(!original)return;
    const float precision=modifiers&Qt::ShiftModifier?.1f:1.f;
    if(m_poseDrag==PoseDrag::Rotation){
        const auto point=m_canvas->artworkViewTransform().map(artwork);
        const auto angle=std::atan2(point.y()-m_posePivot.y(),point.x()-m_posePivot.x());
        const auto delta=std::remainder(angle-m_poseStartAngle,2*M_PI);
        auto rotation=poseJointRotation3D(original->pose,m_selectedJoint);rotation[m_poseRotationAxis]+=float(qRadiansToDegrees(delta))*precision;
        rotation[m_poseRotationAxis]=std::clamp(rotation[m_poseRotationAxis],-180.f,180.f);updatePoseRotation(rotation);return;
    }
    const QSize frame(m_canvas->project().width,m_canvas->project().height);
    const auto modelView=cameraViewMatrix(m_dragScene.camera)*instanceMatrix(*original);bool invertible=false;const auto inverse=modelView.inverted(&invertible);if(!invertible)return;
    const auto cameraPoint=modelView.map(m_poseDragOrigin);if(cameraPoint.z()>=-.00001f)return;
    const auto projection=cameraProjectionMatrix(m_dragScene.camera,float(frame.width())/frame.height());
    const auto delta=artwork-m_dragStart;const float depth=m_dragScene.camera.orthographic?1.f:-cameraPoint.z();
    const QVector3D localDelta=inverse.mapVector({float(delta.x()*2/frame.width())*depth/projection(0,0),float(-delta.y()*2/frame.height())*depth/projection(1,1),0})*precision;
    if(m_poseDrag==PoseDrag::Root)updatePoseRootTranslation(original->pose.rootTranslation+localDelta);
    else if(m_poseDrag==PoseDrag::Pole)updateIKPole(m_poseDragOrigin+localDelta);
    else updateIKTarget(m_poseDragOrigin+localDelta);
}
void Scene3DController::paintPose(QPainter &painter) const {
    if(!selectedJoint()||!canPose()||!m_rigVisible)return;
    const auto evaluated=evaluatePose3D(*selectedRig(),*selectedPose());if(!evaluated)return;
    const QSize frame(m_canvas->project().width,m_canvas->project().height);
    const auto matrix=cameraProjectionMatrix(scene()->camera,float(frame.width())/frame.height())*cameraViewMatrix(scene()->camera)*instanceMatrix(*selectedObject());
    const auto view=m_canvas->artworkViewTransform();const auto screen=[&](QVector3D p)->std::optional<QPointF>{auto q=projectPoint(matrix,p,frame);return q?std::optional<QPointF>(view.map(*q)):std::nullopt;};
    const auto point=screen(pointFor(*evaluated,m_selectedJoint));if(!point)return;
    painter.save();painter.setRenderHint(QPainter::Antialiasing);painter.setBrush(Qt::NoBrush);
    if(m_poseMode==PoseMode::FK&&canRotateSelectedPoseJoint()){
        painter.setPen(QPen(QColor(ui::accent),2));painter.drawEllipse(*point,42,42);
        painter.fillRect(QRectF(*point+QPointF(40,-10),QSizeF(20,20)),QColor(ui::panel));painter.setPen(QColor(ui::text));
        painter.drawText(QRectF(*point+QPointF(40,-10),QSizeF(20,20)),Qt::AlignCenter,QString("XYZ")[m_poseRotationAxis]);
    }else if(m_poseMode==PoseMode::IK&&canUseSelectedIK()){
        const auto target=selectedIKTarget().value_or(initialTarget(*selectedRig(),*selectedPose(),m_selectedJoint));
        auto t=screen(target.target),pole=screen(target.pole);
        painter.setPen(QPen(QColor(ui::muted),1,Qt::DashLine));if(t)painter.drawLine(*point,*t);if(t&&pole)painter.drawLine(*t,*pole);
        if(t){painter.setPen(QPen(QColor(m_ikHandle==IKHandle::Target?ui::accent:ui::text),2));painter.setBrush(QColor(ui::panel));
            painter.drawRect(QRectF(*t-QPointF(7,7),QSizeF(14,14)));if(target.pinned){painter.drawLine(*t-QPointF(4,0),*t+QPointF(4,0));painter.drawLine(*t-QPointF(0,4),*t+QPointF(0,4));}}
        if(pole){painter.setPen(QPen(QColor(m_ikHandle==IKHandle::Pole?ui::accent:ui::text),2));painter.setBrush(QColor(ui::panel));
            QPolygonF diamond;diamond<<*pole+QPointF(0,-7)<<*pole+QPointF(7,0)<<*pole+QPointF(0,7)<<*pole+QPointF(-7,0);painter.drawPolygon(diamond);}
    }
    painter.restore();
}
} // namespace toon
