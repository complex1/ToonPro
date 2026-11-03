#pragma once
#include "rig3d.h"
#include "skin3d.h"
#include <QByteArray>
#include <QJsonObject>
#include <QMatrix4x4>
#include <QQuaternion>
#include <memory>
#include <optional>

namespace toon {
struct Model3D;
struct Bounds3D;
struct JointPose3D {
    QString jointId;
    QVector3D rotation; // Local degrees, Rz * Ry * Rx; identity rest axes.
    bool operator==(const JointPose3D &) const = default;
};
struct JointLimit3D {
    QString jointId;
    QVector3D minimum{-180, -180, -180}, maximum{180, 180, 180};
    bool enabled = true;
    bool operator==(const JointLimit3D &) const = default;
};
struct IKTarget3D {
    QString jointId; // End of a two-bone chain: grandparent -> parent -> this joint.
    QVector3D target, pole; // Absolute model-local coordinates.
    bool pinned = true;
    bool operator==(const IKTarget3D &) const = default;
};
struct Pose3D {
    QVector<JointPose3D> joints;
    QVector3D rootTranslation; // Applied to every skeleton root.
    QVector<JointLimit3D> limits;
    QVector<IKTarget3D> targets;
    bool operator==(const Pose3D &) const = default;
};
struct PoseEvaluation3D {
    QStringList jointIds; // All arrays follow the original Rig3D::joints order.
    QVector<QVector3D> positions;
    QVector<QQuaternion> rotations;
    QVector<QMatrix4x4> skinMatrices; // Posed global joint * inverse rest global joint.
};
QString validatePose3D(const Pose3D &pose, const Rig3D &rig);
QVector3D poseJointRotation3D(const Pose3D &pose, const QString &jointId);
std::optional<PoseEvaluation3D> evaluatePose3D(const Rig3D &rig, const Pose3D &pose, QString *error = nullptr);
bool setPoseJointRotation3D(Pose3D &pose, const Rig3D &rig, const QString &jointId,
    QVector3D rotation, bool mirror = false, QString *error = nullptr);
bool solvePoseIK3D(Pose3D &pose, const Rig3D &rig, const IKTarget3D &target, QString *error = nullptr);
bool mirrorPose3D(Pose3D &pose, const Rig3D &rig, QString *error = nullptr);
void resetPose3D(Pose3D &pose); // Keep joint limits; clear the current pose and pins.
bool pose3DIsIdentity(const Pose3D &pose);
qint64 pose3DByteSize(const Pose3D &pose);
QByteArray pose3DCacheKey(const Pose3D &pose);
// Conservative model-local envelope, O(joints), with no vertex deformation.
Bounds3D posedModelBounds3D(const Model3D &model, const Rig3D &rig, const Pose3D &pose, QString *error = nullptr);
std::shared_ptr<const Model3D> deformPosedModel3D(const Model3D &model, const Rig3D &rig,
    const Skin3D &skin, const Pose3D &pose, QString *error = nullptr);
QJsonObject pose3DToJson(const Pose3D &pose);
std::optional<Pose3D> pose3DFromJson(const QJsonObject &json, const Rig3D &rig, QString *error = nullptr);
} // namespace toon
