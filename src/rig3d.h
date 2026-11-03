#pragma once
#include <QString>
#include <QStringList>
#include <QVector>
#include <QVector3D>

namespace toon {
struct Bounds3D;
inline constexpr int maxRig3DJoints = 256;
inline constexpr float maxRig3DCoordinate = 1000000.f;
struct RigJoint3D {
    QString id, name, parentId, mirrorId;
    QVector3D position; // Absolute model-local rest position, independent of object placement.
    bool operator==(const RigJoint3D &) const = default;
};
struct Rig3D {
    QVector<RigJoint3D> joints;
    float mirrorPlaneX = 0;
    bool operator==(const Rig3D &) const = default;
};
QString validateRig3D(const Rig3D &rig);
const RigJoint3D *rigJoint(const Rig3D &rig, const QString &id);
QStringList rigDescendants(const Rig3D &rig, const QString &id); // Includes the selected joint.
Rig3D createHumanoidRig3D(const Bounds3D &bounds);
// Mutations validate the complete result and leave the input unchanged on failure.
bool moveRigJoint3D(Rig3D &rig, const QString &id, const QVector3D &position, bool mirror, QString *error = nullptr);
bool mirrorRigBranch3D(Rig3D &rig, const QString &id, QString *error = nullptr);
} // namespace toon
