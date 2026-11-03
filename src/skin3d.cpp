#include "skin3d.h"
#include "scene3d.h"
#include "scene_model_io.h"
#include <QCryptographicHash>
#include <QDataStream>
#include <QIODevice>
#include <QJsonArray>
#include <QSet>
#include <QtEndian>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <zlib.h>

namespace toon {
namespace {
QString validateVertex(const SkinVertex3D &vertex, int jointCount) {
    double sum = 0;
    for (int i = 0; i < 4; ++i) {
        if (vertex.joints[i] >= jointCount) return "Skin contains an out-of-range joint index.";
        const auto weight = vertex.weights[i];
        if (!std::isfinite(weight) || weight < 0 || weight > 1) return "Skin weights must be finite values between zero and one.";
        if (weight > 0) for (int j = 0; j < i; ++j)
            if (vertex.weights[j] > 0 && vertex.joints[j] == vertex.joints[i]) return "Skin contains duplicate joint influences.";
        sum += weight;
    }
    return std::abs(sum - 1.) > .0001 ? QString("Skin vertex weights must sum to one.") : QString();
}
double segmentDistanceSquared(QVector3D point, QVector3D a, QVector3D b) {
    const auto d = b - a;
    const double length = QVector3D::dotProduct(d, d);
    const double t = length > 1e-18 ? std::clamp(double(QVector3D::dotProduct(point - a, d)) / length, 0., 1.) : 0.;
    return double((point - a - float(t) * d).lengthSquared());
}
QVector3D normalMap(const QMatrix3x3 &matrix, const QVector3D &normal) {
    return {matrix(0,0)*normal.x()+matrix(0,1)*normal.y()+matrix(0,2)*normal.z(),
        matrix(1,0)*normal.x()+matrix(1,1)*normal.y()+matrix(1,2)*normal.z(),
        matrix(2,0)*normal.x()+matrix(2,1)*normal.y()+matrix(2,2)*normal.z()};
}
QByteArray packedSkin(const Skin3D &skin) {
    QByteArray packed; packed.resize(skin3DByteSize(skin)); char *at = packed.data();
    for (const auto &mesh : skin.meshes) for (const auto &vertex : mesh) for (int i = 0; i < 4; ++i) {
        qToLittleEndian(vertex.joints[i], at); at += 2;
        quint32 bits; std::memcpy(&bits, &vertex.weights[i], 4); qToLittleEndian(bits, at); at += 4;
    }
    return packed;
}
}
qint64 skin3DByteSize(const Skin3D &skin) {
    qint64 count = 0; for (const auto &mesh : skin.meshes) count += mesh.size();
    return count * 24;
}
QByteArray skin3DCacheKey(const Skin3D &skin) {
    QCryptographicHash hash(QCryptographicHash::Sha256);
    QByteArray identity; QDataStream stream(&identity, QIODevice::WriteOnly); stream << skin.jointIds;
    for (const auto &mesh : skin.meshes) stream << quint32(mesh.size());
    hash.addData(identity); hash.addData(packedSkin(skin)); return hash.result();
}
QString validateSkin3D(const Skin3D &skin, const Rig3D &rig, const Model3D *model, bool metadataOnly) {
    if (!metadataOnly) {
        const auto rigError = validateRig3D(rig);
        if (!rigError.isEmpty()) return rigError;
    }
    if (skin.jointIds.isEmpty() || skin.jointIds.size() > maxRig3DJoints || skin.jointIds.size() != rig.joints.size())
        return "Skin must reference every joint in its rest skeleton.";
    QSet<QString> ids;
    for (const auto &id : skin.jointIds) {
        if (ids.contains(id) || !rigJoint(rig, id)) return "Skin references a missing or duplicate rest joint.";
        ids.insert(id);
    }
    if (skin.meshes.isEmpty() || skin.meshes.size() > maxModel3DMeshes || skin3DByteSize(skin) > maxSkin3DBytes)
        return "Skin exceeds its mesh or one-million-vertex limit.";
    if (model && model->meshes.size() != skin.meshes.size()) return "Skin mesh counts do not match the model.";
    for (qsizetype mesh = 0; mesh < skin.meshes.size(); ++mesh) {
        if (skin.meshes[mesh].isEmpty() || (model && model->meshes[mesh].positions.size() != skin.meshes[mesh].size()))
            return "Skin vertex counts do not match the model.";
        if (!metadataOnly) for (const auto &vertex : skin.meshes[mesh]) {
            const auto error = validateVertex(vertex, int(skin.jointIds.size()));
            if (!error.isEmpty()) return error;
        }
    }
    return {};
}
std::optional<Skin3D> autoBindSkin3D(const Model3D &model, const Rig3D &rig, QString *error, std::shared_ptr<std::atomic_bool> cancelled) {
    auto fail = [error](const QString &message) -> std::optional<Skin3D> { if (error) *error = message; return {}; };
    if (cancelled && cancelled->load()) return fail("Skin binding cancelled.");
    const auto rigError = validateRig3D(rig), modelError = validateModel3D(model);
    if (!rigError.isEmpty()) return fail(rigError);
    if (!modelError.isEmpty()) return fail(modelError);
    if (rig.joints.isEmpty()) return fail("Create a rest skeleton before binding the model.");
    Skin3D skin;
    QVector<QVector<QVector3D>> children(rig.joints.size());
    for (const auto &joint : rig.joints) skin.jointIds.append(joint.id);
    for (const auto &joint : rig.joints) {
        const int parent = int(skin.jointIds.indexOf(joint.parentId));
        if (parent >= 0) children[parent].append(joint.position);
    }
    const double epsilon = std::max(1e-12, double(model.bounds.radius()) * model.bounds.radius() * 1e-10);
    for (const auto &mesh : model.meshes) {
        if (cancelled && cancelled->load()) return fail("Skin binding cancelled.");
        QVector<SkinVertex3D> weights; weights.reserve(mesh.positions.size());
        for (const auto &local : mesh.positions) {
            if (weights.size() % 256 == 0 && cancelled && cancelled->load()) return fail("Skin binding cancelled.");
            const auto point = mesh.transform.map(local);
            std::array<std::pair<double, quint16>, 4> nearest;
            nearest.fill({std::numeric_limits<double>::infinity(), 0});
            for (int index = 0; index < rig.joints.size(); ++index) {
                const auto position = rig.joints[index].position;
                double distance = double((point - position).lengthSquared());
                for (const auto &child : children[index]) distance = std::min(distance, segmentDistanceSquared(point, position, child));
                const std::pair<double, quint16> candidate(distance, quint16(index));
                for (int slot = 0; slot < 4; ++slot) if (candidate < nearest[slot]) {
                    for (int shift = 3; shift > slot; --shift) nearest[shift] = nearest[shift - 1];
                    nearest[slot] = candidate; break;
                }
            }
            SkinVertex3D vertex; double total = 0; std::array<double, 4> raw{};
            for (int i = 0; i < std::min(4, int(rig.joints.size())); ++i) {
                vertex.joints[i] = nearest[i].second;
                raw[i] = 1. / std::max(epsilon, nearest[i].first); total += raw[i];
            }
            float sum = 0;
            for (int i = 0; i < 4; ++i) { vertex.weights[i] = float(raw[i] / total); sum += vertex.weights[i]; }
            vertex.weights[0] += 1.f - sum;
            weights.append(vertex);
        }
        skin.meshes.append(std::move(weights));
    }
    const auto validation = validateSkin3D(skin, rig, &model);
    if (!validation.isEmpty()) return fail(validation);
    if (error) error->clear();
    return skin;
}
float skinJointWeight(const SkinVertex3D &vertex, quint16 jointIndex) {
    for (int i = 0; i < 4; ++i) if (vertex.joints[i] == jointIndex && vertex.weights[i] > 0) return vertex.weights[i];
    return 0;
}
bool setSkinJointWeight(SkinVertex3D &vertex, quint16 jointIndex, float weight, QString *error) {
    auto fail = [error](const QString &message) { if (error) *error = message; return false; };
    if (jointIndex >= maxRig3DJoints || !std::isfinite(weight) || weight < 0 || weight > 1)
        return fail("Choose a valid joint and a weight between zero and one.");
    const auto validation = validateVertex(vertex, maxRig3DJoints);
    if (!validation.isEmpty()) return fail(validation);
    if (skinJointWeight(vertex, jointIndex) == weight) { if (error) error->clear(); return true; }
    QVector<std::pair<float, quint16>> others;
    for (int i = 0; i < 4; ++i) if (vertex.joints[i] != jointIndex && vertex.weights[i] > 0) others.append({vertex.weights[i], vertex.joints[i]});
    std::sort(others.begin(), others.end(), [](auto a, auto b) { return a.first != b.first ? a.first > b.first : a.second < b.second; });
    if (weight > 0 && others.size() > 3) others.resize(3);
    if (weight < 1 && others.isEmpty()) return fail("This is the vertex's only joint influence. Add weight to another joint before reducing it.");
    SkinVertex3D next; int slot = 0;
    if (weight > 0) { next.joints[0] = jointIndex; next.weights[0] = weight; slot = 1; }
    if (weight < 1) {
        double sum = 0; for (const auto &item : others) sum += item.first;
        for (const auto &item : others) { next.joints[slot] = item.second; next.weights[slot] = float(item.first / sum * (1. - weight)); ++slot; }
    }
    const auto resultError = validateVertex(next, maxRig3DJoints);
    if (!resultError.isEmpty()) return fail(resultError);
    vertex = next; if (error) error->clear(); return true;
}
bool adjustSkinJointWeight(SkinVertex3D &vertex, quint16 jointIndex, float delta, QString *error) {
    if (!std::isfinite(delta)) { if (error) *error = "Weight adjustment must be finite."; return false; }
    return setSkinJointWeight(vertex, jointIndex, std::clamp(skinJointWeight(vertex, jointIndex) + delta, 0.f, 1.f), error);
}
std::shared_ptr<const Model3D> deformModel3D(const Model3D &model, const Rig3D &rig, const Skin3D &skin,
    const QString &jointId, const QVector3D &rotationDegrees, QString *error) {
    auto fail = [error](const QString &message) -> std::shared_ptr<const Model3D> { if (error) *error = message; return {}; };
    const auto modelError = validateModel3D(model), rigError = validateRig3D(rig);
    if (!modelError.isEmpty()) return fail(modelError);
    if (!rigError.isEmpty()) return fail(rigError);
    const auto validation = validateSkin3D(skin, rig, &model);
    if (!validation.isEmpty()) return fail(validation);
    const auto *joint = rigJoint(rig, jointId);
    if (!joint) return fail("Select a bound joint for the pose preview.");
    for (int axis = 0; axis < 3; ++axis) if (!std::isfinite(rotationDegrees[axis]) || std::abs(rotationDegrees[axis]) > 36000)
        return fail("Invalid preview rotation.");
    if (rotationDegrees.isNull()) { if (error) error->clear(); return std::make_shared<const Model3D>(model); }
    const auto branch = rigDescendants(rig, jointId);
    QVector<bool> affected; affected.reserve(skin.jointIds.size());
    for (const auto &id : skin.jointIds) affected.append(branch.contains(id));
    QMatrix4x4 rotation; rotation.rotate(rotationDegrees.z(), 0, 0, 1); rotation.rotate(rotationDegrees.y(), 0, 1, 0); rotation.rotate(rotationDegrees.x(), 1, 0, 0);
    Model3D result = model;
    for (int m = 0; m < model.meshes.size(); ++m) {
        const auto &source = model.meshes[m]; auto &mesh = result.meshes[m];
        const auto inverse = source.transform.inverted();
        const auto toWorldNormal = source.transform.normalMatrix(), toLocalNormal = inverse.normalMatrix();
        for (int v = 0; v < source.positions.size(); ++v) {
            const auto &weights = skin.meshes[m][v]; float influence = 0;
            for (int i = 0; i < 4; ++i) if (affected[weights.joints[i]]) influence += weights.weights[i];
            const auto point = source.transform.map(source.positions[v]);
            const auto moved = joint->position + rotation.mapVector(point - joint->position);
            mesh.positions[v] = inverse.map(point * (1.f - influence) + moved * influence);
            const auto normal = normalMap(toWorldNormal, source.normals[v]).normalized();
            mesh.normals[v] = normalMap(toLocalNormal, normal * (1.f - influence) + rotation.mapVector(normal) * influence).normalized();
        }
    }
    return finalizeModel3D(std::move(result), error);
}
QJsonObject skin3DToJson(const Skin3D &skin) {
    QJsonArray ids, counts; for (const auto &id : skin.jointIds) ids.append(id);
    const auto packed = packedSkin(skin);
    for (const auto &mesh : skin.meshes) counts.append(int(mesh.size()));
    return {{"jointIds", ids}, {"meshVertexCounts", counts}, {"encoding", "u16-f32-le-zlib"},
        {"sha256", QString::fromLatin1(QCryptographicHash::hash(packed, QCryptographicHash::Sha256).toHex())},
        {"data", QString::fromLatin1(qCompress(packed, 1).toBase64())}};
}
std::optional<Skin3D> skin3DFromJson(const QJsonObject &json, const Rig3D &rig, QString *error) {
    auto fail = [error](const QString &message) -> std::optional<Skin3D> { if (error) *error = message; return {}; };
    if (json.size() != 5 || !json.value("jointIds").isArray() || !json.value("meshVertexCounts").isArray()
        || json.value("encoding") != "u16-f32-le-zlib" || !json.value("data").isString() || !json.value("sha256").isString())
        return fail("Malformed skin weight metadata.");
    const auto ids = json.value("jointIds").toArray(), counts = json.value("meshVertexCounts").toArray();
    if (ids.isEmpty() || ids.size() > maxRig3DJoints || counts.isEmpty() || counts.size() > maxModel3DMeshes)
        return fail("Skin exceeds its joint or mesh limit.");
    Skin3D skin; QSet<QString> seen;
    if (ids.size() != rig.joints.size()) return fail("Skin must reference every joint in its rest skeleton.");
    for (const auto &id : ids) {
        if (!id.isString() || seen.contains(id.toString()) || !rigJoint(rig, id.toString())) return fail("Invalid skin joint identifier.");
        seen.insert(id.toString()); skin.jointIds.append(id.toString());
    }
    qint64 vertices = 0;
    for (const auto &count : counts) {
        const double n = count.toDouble(-1);
        if (!count.isDouble() || !std::isfinite(n) || n < 1 || n > maxModel3DVertices || std::floor(n) != n)
            return fail("Invalid skin vertex count.");
        vertices += qint64(n);
        if (vertices * 24 > maxSkin3DBytes) return fail("Skin exceeds its one-million-vertex limit.");
    }
    const auto encoded = json.value("data").toString().toLatin1();
    if (encoded.size() > (maxSkin3DBytes + 65536) * 4 / 3 + 4) return fail("Skin compressed payload exceeds its limit.");
    const auto decoded = QByteArray::fromBase64Encoding(encoded, QByteArray::AbortOnBase64DecodingErrors);
    if (!decoded || decoded.decoded.size() < 5 || qFromBigEndian<quint32>(decoded.decoded.constData()) != quint32(vertices * 24))
        return fail("Skin compressed size does not match its vertex counts.");
    QByteArray packed(vertices * 24, Qt::Uninitialized);
    uLongf outputLength = uLongf(packed.size()); uLong sourceLength = uLong(decoded.decoded.size() - 4);
    const auto status = uncompress2(reinterpret_cast<Bytef *>(packed.data()), &outputLength,
        reinterpret_cast<const Bytef *>(decoded.decoded.constData() + 4), &sourceLength);
    if (status != Z_OK || outputLength != uLongf(packed.size()) || sourceLength != uLong(decoded.decoded.size() - 4)
        || json.value("sha256").toString() != QString::fromLatin1(QCryptographicHash::hash(packed, QCryptographicHash::Sha256).toHex()))
        return fail("Damaged skin weight payload or checksum.");
    const char *at = packed.constData();
    for (const auto &count : counts) {
        QVector<SkinVertex3D> mesh; mesh.resize(count.toInt());
        for (auto &vertex : mesh) for (int i = 0; i < 4; ++i) {
            vertex.joints[i] = qFromLittleEndian<quint16>(at); at += 2;
            const auto bits = qFromLittleEndian<quint32>(at); at += 4; std::memcpy(&vertex.weights[i], &bits, 4);
        }
        skin.meshes.append(std::move(mesh));
    }
    const auto validation = validateSkin3D(skin, rig);
    if (!validation.isEmpty()) return fail(validation);
    if (error) error->clear(); return skin;
}
} // namespace toon
