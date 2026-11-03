#include "rig3d.h"
#include "scene3d.h"
#include <QHash>
#include <QSet>
#include <QUuid>
#include <cmath>

namespace toon {
namespace {
bool finite(float value) { return std::isfinite(value) && std::abs(value) <= maxRig3DCoordinate; }
bool finite(const QVector3D &value) { return finite(value.x()) && finite(value.y()) && finite(value.z()); }
QString newId() { return QUuid::createUuid().toString(QUuid::WithoutBraces); }
QVector3D reflected(const Rig3D &rig, QVector3D point) { point.setX(2.f * rig.mirrorPlaneX - point.x()); return point; }
bool accept(Rig3D &target, const Rig3D &candidate, QString *error) {
    const QString validation = validateRig3D(candidate);
    if (error) *error = validation;
    if (!validation.isEmpty()) return false;
    target = candidate;
    return true;
}
}
QString validateRig3D(const Rig3D &rig) {
    if (rig.joints.size() > maxRig3DJoints) return "A rig supports up to 256 joints.";
    if (!finite(rig.mirrorPlaneX)) return "Invalid rig mirror plane.";
    QHash<QString, const RigJoint3D *> joints;
    for (const auto &joint : rig.joints) {
        if (joint.id.trimmed().isEmpty() || joint.id.size() > 128 || joints.contains(joint.id))
            return "Each rig joint needs a unique, valid identifier.";
        if (joint.name.trimmed().isEmpty() || joint.name.size() > 120) return "Joint names need 1 to 120 characters.";
        if (!finite(joint.position)) return "Joint coordinates must be finite and within one million model units.";
        joints.insert(joint.id, &joint);
    }
    for (const auto &joint : rig.joints) {
        if (!joint.parentId.isEmpty() && !joints.contains(joint.parentId)) return "A rig joint refers to a missing parent.";
        QSet<QString> ancestors;
        auto current = &joint;
        while (current) {
            if (ancestors.contains(current->id)) return "Rig joint parents cannot form a cycle.";
            ancestors.insert(current->id);
            current = joints.value(current->parentId, nullptr);
        }
        if (!joint.mirrorId.isEmpty()) {
            const auto *other = joints.value(joint.mirrorId, nullptr);
            if (!other || other == &joint || other->mirrorId != joint.id)
                return "Mirrored joints need reciprocal links to a different existing joint.";
        }
    }
    return {};
}
const RigJoint3D *rigJoint(const Rig3D &rig, const QString &id) {
    for (const auto &joint : rig.joints) if (joint.id == id) return &joint;
    return nullptr;
}
QStringList rigDescendants(const Rig3D &rig, const QString &id) {
    if (!rigJoint(rig, id)) return {};
    QStringList result{id};
    QSet<QString> visited{id};
    for (qsizetype index = 0; index < result.size(); ++index)
        for (const auto &joint : rig.joints) if (joint.parentId == result[index] && !visited.contains(joint.id)) {
            result.append(joint.id);
            visited.insert(joint.id);
        }
    return result;
}
Rig3D createHumanoidRig3D(const Bounds3D &bounds) {
    Rig3D result;
    if (!bounds.valid || !finite(bounds.minimum) || !finite(bounds.maximum)) return result;
    const auto span = bounds.maximum - bounds.minimum;
    if (span.x() < 0 || span.y() <= 0 || span.z() < 0) return result;
    const auto center = bounds.center();
    result.mirrorPlaneX = center.x();
    auto add = [&](const QString &name, const QString &parent, float x, float y, float z = 0.f) {
        const QString id = newId();
        result.joints.append({id, name, parent, {}, {center.x() + span.x() * x,
            bounds.minimum.y() + span.y() * y, center.z() + span.z() * z}});
        return id;
    };
    const QString pelvis = add("Pelvis", {}, 0, .49f);
    const QString spine = add("Spine", pelvis, 0, .60f);
    const QString chest = add("Chest", spine, 0, .72f);
    const QString neck = add("Neck", chest, 0, .81f);
    const QString head = add("Head", neck, 0, .90f);
    add("Head tip", head, 0, .99f);
    auto paired = [&](const QString &name, const QString &leftParent, const QString &rightParent,
                      float x, float y, float z = 0.f) {
        const auto left = add("Left " + name, leftParent, x, y, z);
        const auto right = add("Right " + name, rightParent, -x, y, z);
        result.joints[result.joints.size() - 2].mirrorId = right;
        result.joints.last().mirrorId = left;
        return QPair<QString, QString>(left, right);
    };
    const auto shoulder = paired("shoulder", chest, chest, .23f, .73f);
    const auto elbow = paired("elbow", shoulder.first, shoulder.second, .37f, .53f);
    const auto wrist = paired("wrist", elbow.first, elbow.second, .43f, .36f);
    paired("hand", wrist.first, wrist.second, .46f, .30f);
    const auto hip = paired("hip", pelvis, pelvis, .10f, .48f);
    const auto knee = paired("knee", hip.first, hip.second, .10f, .24f);
    const auto ankle = paired("ankle", knee.first, knee.second, .10f, .055f);
    paired("toe", ankle.first, ankle.second, .10f, .025f, .40f);
    return validateRig3D(result).isEmpty() ? result : Rig3D{};
}
bool moveRigJoint3D(Rig3D &rig, const QString &id, const QVector3D &position, bool mirror, QString *error) {
    const auto *joint = rigJoint(rig, id);
    if (!joint) { if (error) *error = "Select an existing joint first."; return false; }
    Rig3D candidate = rig;
    for (auto &next : candidate.joints) {
        if (next.id == id) next.position = position;
        else if (mirror && !joint->mirrorId.isEmpty() && next.id == joint->mirrorId) next.position = reflected(rig, position);
    }
    return accept(rig, candidate, error);
}
bool mirrorRigBranch3D(Rig3D &rig, const QString &id, QString *error) {
    auto fail = [error](const QString &message) { if (error) *error = message; return false; };
    const auto validation = validateRig3D(rig);
    if (!validation.isEmpty()) return fail(validation);
    const auto *root = rigJoint(rig, id);
    if (!root) return fail("Select an existing joint first.");
    const auto branch = rigDescendants(rig, id);
    if (rig.joints.size() + branch.size() > maxRig3DJoints) return fail("Mirroring would exceed the 256-joint limit.");
    QHash<QString, QString> mirroredIds;
    for (const auto &sourceId : branch) {
        if (!rigJoint(rig, sourceId)->mirrorId.isEmpty()) return fail("This branch already contains mirrored joints.");
        mirroredIds.insert(sourceId, newId());
    }
    QString parentId;
    if (!root->parentId.isEmpty()) {
        const auto *parent = rigJoint(rig, root->parentId);
        if (!parent->mirrorId.isEmpty()) parentId = parent->mirrorId;
        else if (std::abs(parent->position.x() - rig.mirrorPlaneX) <= .0001f) parentId = parent->id;
        else return fail("Mirror the parent first, or attach the branch to a joint on the mirror plane.");
    }
    Rig3D candidate = rig;
    for (const auto &sourceId : branch) {
        auto copy = *rigJoint(rig, sourceId);
        copy.id = mirroredIds.value(sourceId);
        copy.name = copy.name.left(111) + " mirrored";
        copy.parentId = sourceId == id ? parentId : mirroredIds.value(copy.parentId);
        copy.mirrorId = sourceId;
        copy.position = reflected(rig, copy.position);
        candidate.joints.append(copy);
    }
    for (auto &joint : candidate.joints) if (mirroredIds.contains(joint.id)) joint.mirrorId = mirroredIds.value(joint.id);
    return accept(rig, candidate, error);
}
} // namespace toon
