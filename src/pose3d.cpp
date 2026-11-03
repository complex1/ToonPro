#include "pose3d.h"
#include "scene3d.h"
#include "scene_model_io.h"
#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSet>
#include <QtMath>
#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>

namespace toon {
namespace {
bool bounded(QVector3D value, float limit = 1000000.f) {
    for (int axis = 0; axis < 3; ++axis) if (!std::isfinite(value[axis]) || std::abs(value[axis]) > limit) return false;
    return true;
}
QQuaternion quaternion(QVector3D angles) {
    return (QQuaternion::fromAxisAndAngle(0,0,1,angles.z())
        * QQuaternion::fromAxisAndAngle(0,1,0,angles.y())
        * QQuaternion::fromAxisAndAngle(1,0,0,angles.x())).normalized();
}
QVector3D euler(QQuaternion rotation) {
    const auto m = rotation.normalized().toRotationMatrix();
    const float y = std::asin(std::clamp(-m(2,0), -1.f, 1.f));
    float x, z;
    if (std::abs(std::cos(y)) > .000001f) {
        x = std::atan2(m(2,1),m(2,2)); z = std::atan2(m(1,0),m(0,0));
    } else { x = std::atan2(-m(1,2),m(1,1)); z = 0; }
    return {qRadiansToDegrees(x),qRadiansToDegrees(y),qRadiansToDegrees(z)};
}
QVector3D constrained(const Pose3D &pose, const QString &id, QVector3D value) {
    for (const auto &limit : pose.limits) if (limit.jointId == id && limit.enabled)
        for (int axis = 0; axis < 3; ++axis) value[axis] = std::clamp(value[axis],limit.minimum[axis],limit.maximum[axis]);
    return value;
}
void putRotation(Pose3D &pose, const QString &id, QVector3D value) {
    value = constrained(pose,id,value);
    for (auto &joint : pose.joints) if (joint.jointId == id) { joint.rotation = value; return; }
    pose.joints.append({id,value});
}
int indexOf(const Rig3D &rig, const QString &id) {
    for (int i = 0; i < rig.joints.size(); ++i) if (rig.joints[i].id == id) return i;
    return -1;
}
int depthOf(const Rig3D &rig, const QString &id) {
    int depth = 0; const auto *joint = rigJoint(rig,id);
    while (joint && !joint->parentId.isEmpty()) { ++depth; joint = rigJoint(rig,joint->parentId); }
    return depth;
}
QString validateTarget(const IKTarget3D &target, const Rig3D &rig) {
    const auto *end = rigJoint(rig,target.jointId);
    const auto *middle = end ? rigJoint(rig,end->parentId) : nullptr;
    const auto *start = middle ? rigJoint(rig,middle->parentId) : nullptr;
    if (!start) return "An IK target needs an end joint with two parent bones.";
    if ((start->position-middle->position).lengthSquared() < 1e-12f
        || (middle->position-end->position).lengthSquared() < 1e-12f)
        return "An IK chain cannot contain a zero-length bone.";
    if (!bounded(target.target) || !bounded(target.pole)) return "IK target and pole coordinates must be finite and within one million metres.";
    return {};
}
PoseEvaluation3D forward(const Rig3D &rig, const Pose3D &pose) {
    PoseEvaluation3D result;
    const int count = rig.joints.size();
    result.positions.resize(count); result.rotations.resize(count); result.skinMatrices.resize(count);
    for (const auto &joint : rig.joints) result.jointIds.append(joint.id);
    QVector<bool> done(count,false);
    std::function<void(int)> evaluate = [&](int i) {
        if (done[i]) return;
        const auto &joint = rig.joints[i]; const int parent = indexOf(rig,joint.parentId);
        const auto local = quaternion(constrained(pose,joint.id,poseJointRotation3D(pose,joint.id)));
        if (parent < 0) {
            result.positions[i] = joint.position + pose.rootTranslation; result.rotations[i] = local;
        } else {
            evaluate(parent);
            result.positions[i] = result.positions[parent] + result.rotations[parent].rotatedVector(joint.position-rig.joints[parent].position);
            result.rotations[i] = (result.rotations[parent]*local).normalized();
        }
        auto &matrix = result.skinMatrices[i]; matrix.translate(result.positions[i]);
        matrix.rotate(result.rotations[i]); matrix.translate(-joint.position);
        done[i] = true;
    };
    for (int i = 0; i < count; ++i) evaluate(i);
    return result;
}
// Solve one chain analytically. Joint limits are applied after each local
// rotation; they may prevent exact reach, but never change a bone's length.
void solveChain(Pose3D &pose, const Rig3D &rig, const IKTarget3D &target) {
    const int end = indexOf(rig,target.jointId), middle = indexOf(rig,rig.joints[end].parentId);
    const int start = indexOf(rig,rig.joints[middle].parentId), parent = indexOf(rig,rig.joints[start].parentId);
    auto evaluated = forward(rig,pose);
    const auto origin = evaluated.positions[start];
    const double a = double((rig.joints[middle].position-rig.joints[start].position).length());
    const double b = double((rig.joints[end].position-rig.joints[middle].position).length());
    auto direction = target.target-origin;
    if (direction.lengthSquared() < 1e-12f) direction = evaluated.positions[end]-origin;
    if (direction.lengthSquared() < 1e-12f) direction = evaluated.positions[middle]-origin;
    direction.normalize();
    // A tiny nonzero distance handles a folded equal-length chain without a
    // division by zero. Targets at maximum reach may straighten completely.
    const double distance = std::clamp(double((target.target-origin).length()),std::max(std::abs(a-b),1e-7*(a+b)),a+b);
    auto bend = target.pole-origin; bend -= direction*QVector3D::dotProduct(bend,direction);
    if (bend.lengthSquared() < 1e-12f) {
        bend = evaluated.positions[middle]-origin; bend -= direction*QVector3D::dotProduct(bend,direction);
    }
    if (bend.lengthSquared() < 1e-12f) {
        const auto axis = std::abs(direction.y()) < .9f ? QVector3D(0,1,0) : QVector3D(1,0,0);
        bend = axis-direction*QVector3D::dotProduct(axis,direction);
    }
    bend.normalize();
    const double along = (a*a+distance*distance-b*b)/(2*distance);
    const double height = std::sqrt(std::max(0.,a*a-along*along));
    const auto elbow = origin+direction*float(along)+bend*float(height);
    const auto destination = origin+direction*float(distance);
    const auto swing = QQuaternion::rotationTo(evaluated.positions[middle]-origin,elbow-origin);
    const auto startGlobal = (swing*evaluated.rotations[start]).normalized();
    const auto startLocal = parent < 0 ? startGlobal : evaluated.rotations[parent].conjugated()*startGlobal;
    putRotation(pose,rig.joints[start].id,euler(startLocal));
    evaluated = forward(rig,pose);
    const auto middleSwing = QQuaternion::rotationTo(evaluated.positions[end]-evaluated.positions[middle],destination-evaluated.positions[middle]);
    const auto middleGlobal = (middleSwing*evaluated.rotations[middle]).normalized();
    putRotation(pose,rig.joints[middle].id,euler(evaluated.rotations[start].conjugated()*middleGlobal));
}
void resolvePins(Pose3D &pose, const Rig3D &rig) {
    QVector<IKTarget3D> targets;
    for(const auto &target:pose.targets)if(target.pinned)targets.append(target);
    std::stable_sort(targets.begin(),targets.end(),[&](const auto &a,const auto &b){return depthOf(rig,a.jointId)<depthOf(rig,b.jointId);});
    for(const auto &target:targets)solveChain(pose,rig,target);
}
QVector3D reflectedPosition(QVector3D point, float plane) { point.setX(2*plane-point.x()); return point; }
QVector3D reflectedRotation(QVector3D value) { return {value.x(),-value.y(),-value.z()}; }
QVector3D normalMap(const QMatrix3x3 &matrix, QVector3D normal) {
    return {matrix(0,0)*normal.x()+matrix(0,1)*normal.y()+matrix(0,2)*normal.z(),
        matrix(1,0)*normal.x()+matrix(1,1)*normal.y()+matrix(1,2)*normal.z(),
        matrix(2,0)*normal.x()+matrix(2,1)*normal.y()+matrix(2,2)*normal.z()};
}
QJsonArray vectorJson(QVector3D value) { return {value.x(),value.y(),value.z()}; }
bool readVector(const QJsonValue &json, QVector3D &value) {
    if (!json.isArray() || json.toArray().size()!=3) return false;
    const auto array=json.toArray();
    for (int i=0;i<3;++i) {
        const auto number=array[i].toDouble(std::numeric_limits<double>::quiet_NaN());
        if (!array[i].isDouble() || !std::isfinite(number) || std::abs(number)>1000000) return false;
        value[i]=float(number);
    }
    return true;
}
}
QVector3D poseJointRotation3D(const Pose3D &pose, const QString &jointId) {
    for (const auto &joint : pose.joints) if (joint.jointId==jointId) return joint.rotation;
    return {};
}
QString validatePose3D(const Pose3D &pose, const Rig3D &rig) {
    const auto rigError=validateRig3D(rig); if(!rigError.isEmpty())return rigError;
    if (pose.joints.size()>maxRig3DJoints || pose.limits.size()>maxRig3DJoints || pose.targets.size()>maxRig3DJoints)
        return "Pose exceeds its 256-joint limit.";
    if (!bounded(pose.rootTranslation)) return "Root translation must be finite and within one million metres.";
    if (rig.joints.isEmpty() && !pose.rootTranslation.isNull()) return "A root translation requires a skeleton.";
    QSet<QString> seen;
    for (const auto &joint : pose.joints) {
        if (seen.contains(joint.jointId) || !rigJoint(rig,joint.jointId)) return "Pose references a missing or duplicate joint.";
        seen.insert(joint.jointId);
        if (!bounded(joint.rotation,180)) return "Local pose angles must be between -180 and 180 degrees.";
    }
    seen.clear();
    for (const auto &limit : pose.limits) {
        if (seen.contains(limit.jointId) || !rigJoint(rig,limit.jointId)) return "Joint limits reference a missing or duplicate joint.";
        seen.insert(limit.jointId);
        if (!bounded(limit.minimum,180) || !bounded(limit.maximum,180)) return "Joint limits must be between -180 and 180 degrees.";
        for (int axis=0;axis<3;++axis) if (limit.minimum[axis]>0 || limit.maximum[axis]<0)
            return "Joint limits must include the zero-angle rest position.";
    }
    seen.clear(); QSet<QString> controlled;
    for (const auto &target : pose.targets) {
        if (seen.contains(target.jointId)) return "Pose contains duplicate IK targets.";
        seen.insert(target.jointId);
        const auto error=validateTarget(target,rig); if(!error.isEmpty())return error;
        if (target.pinned) {
            const auto *middle=rigJoint(rig,rigJoint(rig,target.jointId)->parentId);
            for (const auto &id : {middle->id,middle->parentId}) {
                if (controlled.contains(id)) return "Pinned IK chains cannot control the same joint.";
                controlled.insert(id);
            }
        }
    }
    return {};
}
std::optional<PoseEvaluation3D> evaluatePose3D(const Rig3D &rig, const Pose3D &pose, QString *error) {
    const auto validation=validatePose3D(pose,rig);
    if(!validation.isEmpty()){if(error)*error=validation;return {};}
    auto working=pose; resolvePins(working,rig);
    if(error)error->clear();return forward(rig,working);
}
bool setPoseJointRotation3D(Pose3D &pose, const Rig3D &rig, const QString &jointId, QVector3D rotation, bool mirror, QString *error) {
    auto fail=[&](const QString &message){if(error)*error=message;return false;};
    const auto validation=validatePose3D(pose,rig);if(!validation.isEmpty())return fail(validation);
    const auto *joint=rigJoint(rig,jointId);if(!joint)return fail("Select a skeleton joint to rotate.");
    if(!bounded(rotation,180))return fail("Local pose angles must be between -180 and 180 degrees.");
    auto next=pose;putRotation(next,jointId,rotation);
    if(mirror&&!joint->mirrorId.isEmpty())putRotation(next,joint->mirrorId,reflectedRotation(rotation));
    pose=std::move(next);if(error)error->clear();return true;
}
bool solvePoseIK3D(Pose3D &pose, const Rig3D &rig, const IKTarget3D &target, QString *error) {
    auto fail=[&](const QString &message){if(error)*error=message;return false;};
    const auto originalError=validatePose3D(pose,rig);if(!originalError.isEmpty())return fail(originalError);
    auto next=pose;
    // Bake the current displayed frame before adding, moving, or releasing a
    // control. A pinned ancestor may differ from its stored FK after body/FK
    // edits; solving in that stale frame would make a released child jump.
    resolvePins(next,rig);
    bool found=false;
    for(auto &entry:next.targets)if(entry.jointId==target.jointId){entry=target;found=true;break;}
    if(!found)next.targets.append(target);
    const auto validation=validatePose3D(next,rig);if(!validation.isEmpty())return fail(validation);
    solveChain(next,rig,target);
    pose=std::move(next);if(error)error->clear();return true;
}
bool mirrorPose3D(Pose3D &pose, const Rig3D &rig, QString *error) {
    const auto validation=validatePose3D(pose,rig);if(!validation.isEmpty()){if(error)*error=validation;return false;}
    auto next=pose;next.joints.clear();next.targets.clear();next.rootTranslation.setX(-pose.rootTranslation.x());
    for(const auto &joint:pose.joints){const auto *rest=rigJoint(rig,joint.jointId);
        putRotation(next,rest->mirrorId.isEmpty()?joint.jointId:rest->mirrorId,reflectedRotation(joint.rotation));}
    for(auto target:pose.targets){const auto *rest=rigJoint(rig,target.jointId);
        if(!rest->mirrorId.isEmpty())target.jointId=rest->mirrorId;
        target.target=reflectedPosition(target.target,rig.mirrorPlaneX);target.pole=reflectedPosition(target.pole,rig.mirrorPlaneX);next.targets.append(target);}
    const auto nextError=validatePose3D(next,rig);if(!nextError.isEmpty()){if(error)*error=nextError;return false;}
    pose=std::move(next);if(error)error->clear();return true;
}
void resetPose3D(Pose3D &pose){pose.joints.clear();pose.rootTranslation={};pose.targets.clear();}
bool pose3DIsIdentity(const Pose3D &pose){
    if(!pose.rootTranslation.isNull())return false;
    for(const auto &joint:pose.joints)if(!joint.rotation.isNull())return false;
    for(const auto &target:pose.targets)if(target.pinned)return false;
    return true;
}
qint64 pose3DByteSize(const Pose3D &pose){
    qint64 bytes=sizeof(Pose3D);
    for(const auto &joint:pose.joints)bytes+=sizeof(JointPose3D)+joint.jointId.size()*2;
    for(const auto &limit:pose.limits)bytes+=sizeof(JointLimit3D)+limit.jointId.size()*2;
    for(const auto &target:pose.targets)bytes+=sizeof(IKTarget3D)+target.jointId.size()*2;
    return bytes;
}
QByteArray pose3DCacheKey(const Pose3D &pose){return QCryptographicHash::hash(QJsonDocument(pose3DToJson(pose)).toJson(QJsonDocument::Compact),QCryptographicHash::Sha256);}
Bounds3D posedModelBounds3D(const Model3D &model,const Rig3D &rig,const Pose3D &pose,QString *error){
    const auto evaluated=evaluatePose3D(rig,pose,error);if(!evaluated)return {};
    if(!model.bounds.valid || pose3DIsIdentity(pose))return model.bounds;
    Bounds3D bounds;
    for(const auto &matrix:evaluated->skinMatrices)for(int corner=0;corner<8;++corner)
        bounds.include(matrix.map({corner&1?model.bounds.maximum.x():model.bounds.minimum.x(),
            corner&2?model.bounds.maximum.y():model.bounds.minimum.y(),
            corner&4?model.bounds.maximum.z():model.bounds.minimum.z()}));
    return bounds;
}
std::shared_ptr<const Model3D> deformPosedModel3D(const Model3D &model,const Rig3D &rig,const Skin3D &skin,const Pose3D &pose,QString *error){
    auto fail=[&](const QString &message)->std::shared_ptr<const Model3D>{if(error)*error=message;return {};};
    const auto modelError=validateModel3D(model);if(!modelError.isEmpty())return fail(modelError);
    const auto skinError=validateSkin3D(skin,rig,&model);if(!skinError.isEmpty())return fail(skinError);
    const auto evaluated=evaluatePose3D(rig,pose,error);if(!evaluated)return {};
    if(pose3DIsIdentity(pose)){if(error)error->clear();return std::make_shared<const Model3D>(model);}
    QVector<int> indices;for(const auto &id:skin.jointIds)indices.append(evaluated->jointIds.indexOf(id));
    Model3D result=model;
    for(int m=0;m<model.meshes.size();++m){const auto &source=model.meshes[m];auto &mesh=result.meshes[m];
        const auto inverse=source.transform.inverted();const auto worldNormal=source.transform.normalMatrix(),localNormal=inverse.normalMatrix();
        for(int v=0;v<source.positions.size();++v){
            const auto point=source.transform.map(source.positions[v]);const auto normal=normalMap(worldNormal,source.normals[v]).normalized();
            QVector3D moved,turned;const auto &vertex=skin.meshes[m][v];
            for(int i=0;i<4;++i)if(vertex.weights[i]>0){const int joint=indices[vertex.joints[i]];
                moved+=evaluated->skinMatrices[joint].map(point)*vertex.weights[i];
                turned+=evaluated->rotations[joint].rotatedVector(normal)*vertex.weights[i];}
            mesh.positions[v]=inverse.map(moved);mesh.normals[v]=normalMap(localNormal,turned).normalized();
        }
    }
    return finalizeModel3D(std::move(result),error);
}
QJsonObject pose3DToJson(const Pose3D &pose){
    QJsonArray joints,limits,targets;
    for(const auto &joint:pose.joints)joints.append(QJsonObject{{"jointId",joint.jointId},{"rotation",vectorJson(joint.rotation)}});
    for(const auto &limit:pose.limits)limits.append(QJsonObject{{"jointId",limit.jointId},{"minimum",vectorJson(limit.minimum)},{"maximum",vectorJson(limit.maximum)},{"enabled",limit.enabled}});
    for(const auto &target:pose.targets)targets.append(QJsonObject{{"jointId",target.jointId},{"target",vectorJson(target.target)},{"pole",vectorJson(target.pole)},{"pinned",target.pinned}});
    return {{"joints",joints},{"rootTranslation",vectorJson(pose.rootTranslation)},{"limits",limits},{"targets",targets}};
}
std::optional<Pose3D> pose3DFromJson(const QJsonObject &json,const Rig3D &rig,QString *error){
    auto fail=[&](const QString &message)->std::optional<Pose3D>{if(error)*error=message;return {};};
    Pose3D pose;
    if(json.size()!=4 || !json.value("joints").isArray() || !json.value("limits").isArray() || !json.value("targets").isArray()
        || !readVector(json.value("rootTranslation"),pose.rootTranslation))return fail("Malformed pose metadata.");
    for(const auto *key:{"joints","limits","targets"})if(json.value(key).toArray().size()>maxRig3DJoints)return fail("Pose exceeds its 256-joint limit.");
    for(const auto &value:json.value("joints").toArray()){
        const auto object=value.toObject();JointPose3D joint;joint.jointId=object.value("jointId").toString();
        if(!value.isObject() || object.size()!=2 || !object.value("jointId").isString() || !readVector(object.value("rotation"),joint.rotation))return fail("Malformed posed joint.");
        pose.joints.append(joint);
    }
    for(const auto &value:json.value("limits").toArray()){
        const auto object=value.toObject();JointLimit3D limit;limit.jointId=object.value("jointId").toString();limit.enabled=object.value("enabled").toBool();
        if(!value.isObject() || object.size()!=4 || !object.value("jointId").isString() || !object.value("enabled").isBool()
            || !readVector(object.value("minimum"),limit.minimum) || !readVector(object.value("maximum"),limit.maximum))return fail("Malformed joint limits.");
        pose.limits.append(limit);
    }
    for(const auto &value:json.value("targets").toArray()){
        const auto object=value.toObject();IKTarget3D target;target.jointId=object.value("jointId").toString();target.pinned=object.value("pinned").toBool();
        if(!value.isObject() || object.size()!=4 || !object.value("jointId").isString() || !object.value("pinned").isBool()
            || !readVector(object.value("target"),target.target) || !readVector(object.value("pole"),target.pole))return fail("Malformed IK target.");
        pose.targets.append(target);
    }
    const auto validation=validatePose3D(pose,rig);if(!validation.isEmpty())return fail(validation);
    if(error)error->clear();return pose;
}
} // namespace toon
