#pragma once
#include "rig3d.h"
#include "skin3d.h"
#include "pose3d.h"
#include <QColor>
#include <QHash>
#include <QImage>
#include <QJsonObject>
#include <QMatrix4x4>
#include <QString>
#include <QVector>
#include <QVector2D>
#include <QVector3D>
#include <memory>
#include <optional>

namespace toon {
inline constexpr int maxScene3DObjects = 128;
inline constexpr int maxScene3DModels = 64;
inline constexpr int maxModel3DMeshes = 256;
inline constexpr int maxModel3DVertices = 1000000;
inline constexpr int maxModel3DTriangles = 1000000;
inline constexpr qint64 maxModel3DBytes = 128LL * 1024 * 1024;
inline constexpr qint64 maxModel3DTexturePixels = 16LL * 1024 * 1024;
struct Bounds3D {
    QVector3D minimum, maximum;
    bool valid = false;
    void include(const QVector3D &point);
    QVector3D center() const { return (minimum + maximum) * 0.5f; }
    float radius() const;
    bool operator==(const Bounds3D &) const = default;
};
struct ModelMesh3D {
    QString name;
    QVector<QVector3D> positions;
    QVector<QVector3D> normals;
    QVector<QVector2D> texCoords;
    QVector<quint32> indices;
    QMatrix4x4 transform;
    QColor baseColor{190, 195, 205};
    QImage baseColorTexture;
    bool doubleSided = false;
    float alphaCutoff = -1; // -1 is opaque; >= 0 enables cutout alpha testing.
};
struct Model3D {
    QString id; // SHA-256 of canonical serialized content; immutable once published.
    QString name;
    QVector<ModelMesh3D> meshes;
    Bounds3D bounds;
};
using Model3DPtr = std::shared_ptr<const Model3D>;
struct ModelInstance3D {
    QString id, name, modelId;
    QVector3D position{};
    QVector3D rotation{}; // Euler degrees, Y-up metres; matrix T * Rz * Ry * Rx * S.
    QVector3D scale{1, 1, 1};
    bool visible = true;
    bool locked = false;
    Rig3D rig;
    std::optional<Skin3D> skin;
    Pose3D pose;
    bool operator==(const ModelInstance3D &) const = default;
};
struct Camera3D {
    QVector3D target{0, 0.8f, 0};
    float yaw = 30;
    float pitch = 15;
    float distance = 5;
    float fieldOfView = 45;
    bool orthographic = false;
    float orthoScale = 3; // Vertical world-space span.
    bool locked = false;
    bool operator==(const Camera3D &) const = default;
};
struct Scene3D {
    QHash<QString, Model3DPtr> models;
    QVector<ModelInstance3D> objects;
    Camera3D camera;
    bool visible = true;
    float opacity = 1;
    bool textured = true;
    bool operator==(const Scene3D &other) const;
};
QMatrix4x4 instanceMatrix(const ModelInstance3D &object);
QVector3D cameraPosition(const Camera3D &camera);
QMatrix4x4 cameraViewMatrix(const Camera3D &camera);
QMatrix4x4 cameraProjectionMatrix(const Camera3D &camera, float aspectRatio);
Bounds3D sceneBounds(const Scene3D &scene, bool visibleOnly = true);
Camera3D fitCamera3D(const Camera3D &camera, const Bounds3D &bounds, float aspectRatio = 1.7778f);
QString validateModel3D(const Model3D &model);
QString validateScene3D(const Scene3D &scene);
// Geometry remains external in project chunks; references must resolve in models.
QJsonObject scene3DToJson(const Scene3D &scene, bool includeRig = true);
std::optional<Scene3D> scene3DFromJson(const QJsonObject &json, const QHash<QString, Model3DPtr> &models, QString *error);
QVector<Model3DPtr> builtInModels3D();
} // namespace toon
