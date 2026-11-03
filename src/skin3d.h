#pragma once
#include "rig3d.h"
#include <QJsonObject>
#include <array>
#include <atomic>
#include <memory>
#include <optional>

namespace toon {
struct Model3D;
inline constexpr qint64 maxSkin3DBytes = 24000000; // One million vertices, four (u16, f32) influences each.
inline constexpr qint64 maxProjectSkin3DBytes = 48LL * 1024 * 1024;
struct SkinVertex3D {
    std::array<quint16, 4> joints{};
    std::array<float, 4> weights{};
    bool operator==(const SkinVertex3D &) const = default;
};
struct Skin3D {
    QStringList jointIds;
    QVector<QVector<SkinVertex3D>> meshes;
    bool operator==(const Skin3D &) const = default;
};
// metadataOnly checks identities, counts and budgets; full validation also checks every influence.
QString validateSkin3D(const Skin3D &skin, const Rig3D &rig, const Model3D *model = nullptr, bool metadataOnly = false);
std::optional<Skin3D> autoBindSkin3D(const Model3D &model, const Rig3D &rig, QString *error = nullptr,
    std::shared_ptr<std::atomic_bool> cancelled = {});
float skinJointWeight(const SkinVertex3D &vertex, quint16 jointIndex);
bool setSkinJointWeight(SkinVertex3D &vertex, quint16 jointIndex, float weight, QString *error = nullptr);
bool adjustSkinJointWeight(SkinVertex3D &vertex, quint16 jointIndex, float delta, QString *error = nullptr);
qint64 skin3DByteSize(const Skin3D &skin);
QByteArray skin3DCacheKey(const Skin3D &skin);
// A transient model-local Euler rotation about the selected rest joint, inherited by descendants.
std::shared_ptr<const Model3D> deformModel3D(const Model3D &model, const Rig3D &rig, const Skin3D &skin,
    const QString &jointId, const QVector3D &rotationDegrees, QString *error = nullptr);
QJsonObject skin3DToJson(const Skin3D &skin);
std::optional<Skin3D> skin3DFromJson(const QJsonObject &json, const Rig3D &rig, QString *error = nullptr);
} // namespace toon
