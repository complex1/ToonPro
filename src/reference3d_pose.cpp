#include "reference3d_pose.h"
#include <algorithm>
#include <cmath>

namespace toon {
namespace {
void ensure(bool condition, const QString &error) { if (!condition) throw error; }
bool finite(QVector3D value) {
    return std::isfinite(value.x()) && std::isfinite(value.y()) && std::isfinite(value.z())
        && std::abs(value.x()) <= 1000000 && std::abs(value.y()) <= 1000000 && std::abs(value.z()) <= 1000000;
}
bool positiveUniform(QVector3D value) {
    if (!finite(value) || value.x() <= 0 || value.y() <= 0 || value.z() <= 0) return false;
    const float tolerance = std::max(1e-12f, value.x() * .00001f);
    return std::abs(value.x() - value.y()) <= tolerance && std::abs(value.x() - value.z()) <= tolerance;
}
QQuaternion orientation(const QMatrix4x4 &matrix) {
    QMatrix3x3 rotation;
    for (int column = 0; column < 3; ++column) {
        const QVector3D axis(matrix(0, column), matrix(1, column), matrix(2, column));
        ensure(std::isfinite(axis.lengthSquared()) && axis.lengthSquared() > 1e-16f, "The IK chain has a degenerate world transform.");
        const auto normalized = axis.normalized(); for (int row = 0; row < 3; ++row) rotation(row, column) = normalized[row];
    }
    return QQuaternion::fromRotationMatrix(rotation).normalized();
}
QVector3D position(const QVector<QMatrix4x4> &globals, int index) { return globals[index].map(QVector3D()); }
QQuaternion localRotationForWorld(const ReferenceInstance &instance, const QVector<QMatrix4x4> &globals, int node, QQuaternion world) {
    const auto parent = instance.asset->nodes[node].parentIndex;
    const auto parentRotation = parent < 0 ? instance.rotation.normalized() : orientation(globals[parent]);
    return (parentRotation.conjugated() * world).normalized();
}
}

bool solveReferenceLimbIK(ReferenceInstance &instance, int upperNode, int middleNode,
                          int endNode, QVector3D worldTarget, QVector3D worldPole, QString *error) {
    try {
        ensure(!instance.locked && bool(instance.asset), "Unlock the reference and select an imported skeleton before posing it.");
        ensure(finite(worldTarget) && finite(worldPole), "IK target and pole must be finite world coordinates within the working range.");
        const auto &nodes = instance.asset->nodes;
        ensure(upperNode >= 0 && middleNode >= 0 && endNode >= 0 && upperNode < nodes.size() && middleNode < nodes.size() && endNode < nodes.size()
            && upperNode != middleNode && middleNode != endNode && upperNode != endNode
            && nodes[middleNode].parentIndex == upperNode && nodes[endNode].parentIndex == middleNode, "Select three directly connected joints for two-bone IK.");
        ensure(positiveUniform(instance.scale), "Two-bone IK currently requires positive uniform placement and ancestor scales.");
        int ancestor = endNode, depth = 0;
        while (ancestor >= 0) {
            ensure(ancestor < nodes.size() && ++depth <= 256, "The IK chain has an invalid ancestor hierarchy.");
            ensure(positiveUniform(instance.poseScales.value(ancestor, nodes[ancestor].scale)), "Two-bone IK currently requires positive uniform placement and ancestor scales.");
            ancestor = nodes[ancestor].parentIndex;
        }
        QString problem; const auto original = evaluateReferenceNodeGlobals(instance, &problem); ensure(bool(original), problem);
        const auto upper = position(*original, upperNode), middle = position(*original, middleNode), end = position(*original, endNode);
        const float firstLength = (middle - upper).length(), secondLength = (end - middle).length();
        ensure(std::isfinite(firstLength) && std::isfinite(secondLength) && firstLength > .00001f && secondLength > .00001f
            && firstLength <= 1000000 && secondLength <= 1000000, "The IK chain needs two nonzero bones within the working range.");
        auto direction = worldTarget - upper; float targetDistance = direction.length();
        if (targetDistance <= .000001f) {
            direction = end - upper; if (direction.lengthSquared() < 1e-12f) direction = middle - upper;
        }
        direction.normalize();
        const float minimum = std::abs(firstLength - secondLength), maximum = firstLength + secondLength;
        targetDistance = std::clamp(targetDistance, std::max(minimum, .000001f), maximum);
        const auto target = upper + direction * targetDistance;
        auto bend = worldPole - upper; bend -= direction * QVector3D::dotProduct(bend, direction);
        if (bend.lengthSquared() < 1e-12f) { bend = middle - upper; bend -= direction * QVector3D::dotProduct(bend, direction); }
        if (bend.lengthSquared() < 1e-12f) {
            const auto axis = std::abs(direction.y()) < .9f ? QVector3D(0, 1, 0) : QVector3D(1, 0, 0);
            bend = axis - direction * QVector3D::dotProduct(axis, direction);
        }
        bend.normalize();
        const double along = std::clamp((double(firstLength) * firstLength + double(targetDistance) * targetDistance - double(secondLength) * secondLength)
            / (2 * double(targetDistance)), -double(firstLength), double(firstLength));
        const float height = float(std::sqrt(std::max(0., double(firstLength) * firstLength - along * along)));
        const auto desiredMiddle = upper + direction * float(along) + bend * height;
        auto candidate = instance;
        const auto upperDelta = QQuaternion::rotationTo((middle - upper).normalized(), (desiredMiddle - upper).normalized());
        const auto upperWorld = (upperDelta * orientation((*original)[upperNode])).normalized();
        candidate.poseRotations.insert(upperNode, localRotationForWorld(candidate, *original, upperNode, upperWorld));
        const auto upperSolved = evaluateReferenceNodeGlobals(candidate, &problem); ensure(bool(upperSolved), problem);
        const auto solvedMiddle = position(*upperSolved, middleNode), currentEnd = position(*upperSolved, endNode);
        ensure((target - solvedMiddle).lengthSquared() > 1e-16f, "The IK target creates a degenerate lower-bone direction.");
        const auto middleDelta = QQuaternion::rotationTo((currentEnd - solvedMiddle).normalized(), (target - solvedMiddle).normalized());
        const auto middleWorld = (middleDelta * orientation((*upperSolved)[middleNode])).normalized();
        candidate.poseRotations.insert(middleNode, localRotationForWorld(candidate, *upperSolved, middleNode, middleWorld));
        const auto solved = evaluateReferenceInstance(candidate, &problem); ensure(bool(solved), problem);
        const float tolerance = std::max(.0001f, maximum * .0002f);
        ensure((position(solved->nodeGlobals, endNode) - target).length() <= tolerance, "The IK solve could not reach its clamped target reliably.");
        instance = std::move(candidate); if (error) error->clear(); return true;
    } catch (const QString &message) { if (error) *error = message; } catch (const std::bad_alloc &) { if (error) *error = "Not enough memory to solve this reference pose."; }
    return false;
}
}
