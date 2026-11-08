#pragma once

#include <QColor>
#include <QHash>
#include <QImage>
#include <QJsonObject>
#include <QMatrix4x4>
#include <QQuaternion>
#include <QString>
#include <QVector>
#include <QVector2D>
#include <QVector3D>
#include <QVector4D>
#include <array>
#include <memory>
#include <optional>

namespace toon {
// Independent reference-3D source data. Legacy rig/skin/pose types are not part
// of this schema; the renderer bridge below consumes evaluated triangles only.
inline constexpr int maxReferenceNodes = 4096;
inline constexpr int maxReferenceMeshes = 256;
inline constexpr int maxReferenceMaterials = 256;
inline constexpr int maxReferenceJoints = 256;
inline constexpr int maxReferenceMorphTargets = 128;
inline constexpr int maxReferenceVertices = 1000000;
inline constexpr int maxReferenceTriangles = 1000000;
inline constexpr int maxReferenceInstances = 128;
inline constexpr qint64 maxReferenceAssetBytes = 128LL * 1024 * 1024;
inline constexpr qint64 maxReferenceSceneBytes = 256LL * 1024 * 1024;
inline constexpr qint64 maxReferenceTexturePixels = 16LL * 1024 * 1024;

struct ReferenceVertex {
    QVector3D position;
    QVector3D normal{0, 1, 0};
    QVector2D uv;
    QVector4D tangent{1, 0, 0, 1};
    QVector4D color{1, 1, 1, 1};
    std::array<quint16, 4> joints{}; // Indices into the node's skin.joints table.
    std::array<float, 4> weights{}; // Four normalized weights for skinned nodes.
};
struct ReferenceMorphTarget {
    QString name;
    QVector<QVector3D> positionDeltas;
    QVector<QVector3D> normalDeltas; // Empty means no normal deltas.
    QVector<QVector3D> tangentDeltas;
};
struct ReferencePrimitive {
    QVector<ReferenceVertex> vertices;
    QVector<quint32> indices;
    int materialIndex = -1;
    QVector<ReferenceMorphTarget> morphTargets;
};
struct ReferenceMesh {
    QString name;
    QVector<ReferencePrimitive> primitives;
    QVector<float> defaultMorphWeights;
};
struct ReferenceNode {
    QString name;
    int parentIndex = -1;
    QVector3D translation;
    QQuaternion rotation;
    QVector3D scale{1, 1, 1};
    int meshIndex = -1;
    int skinIndex = -1;
    QVector<float> morphWeights; // Empty inherits mesh defaults.
};
struct ReferenceSkin {
    QString name;
    QVector<int> joints; // Node indices, in imported skin order.
    QVector<QMatrix4x4> inverseBindMatrices;
    int skeletonRoot = -1;
};
enum class ReferenceAlphaMode { Opaque, Mask, Blend };
struct ReferenceMaterial {
    QString name;
    QColor baseColor{Qt::white};
    QImage baseColorTexture;
    float metallic = 0;
    float roughness = 1;
    QColor emissive{Qt::black};
    QImage metallicRoughnessTexture, normalTexture, occlusionTexture, emissiveTexture;
    float normalScale = 1;
    float occlusionStrength = 1;
    ReferenceAlphaMode alphaMode = ReferenceAlphaMode::Opaque;
    float alphaCutoff = .5f;
    bool doubleSided = false;
};
struct ReferenceExpressionBinding {
    int nodeIndex = -1;
    int morphIndex = -1;
    float weight = 1;
};
struct ReferenceExpression {
    QString name;
    QVector<ReferenceExpressionBinding> bindings;
    bool binary = false;
};
struct ReferenceAssetRuntime;
struct ReferenceAssetAccess;
struct ReferenceAsset {
    QString id; // SHA-256 of canonical v1 logical data (raw RGBA pixels + ICC), excluding this field.
    QString name;
    QVector<ReferenceNode> nodes;
    QVector<ReferenceMesh> meshes;
    QVector<ReferenceSkin> skins;
    QVector<ReferenceMaterial> materials;
    // Original bounded PNG/JPEG resources in source-image index order, for
    // extension-only texture references whose shaders are not implemented yet.
    QVector<QByteArray> sourceImages;
    QHash<QString, int> humanoidBones;
    QVector<ReferenceExpression> expressions;
    QJsonObject metadata;
private:
    // Set only by validated publication. The certificate contains a weak owner
    // for this exact object, so copying an asset cannot certify edited source.
    mutable std::shared_ptr<const ReferenceAssetRuntime> m_runtime;
    friend struct ReferenceAssetAccess;
};
using ReferenceAssetPtr = std::shared_ptr<const ReferenceAsset>;

struct ReferenceInstance {
    QString id, name;
    ReferenceAssetPtr asset;
    QVector3D translation;
    QQuaternion rotation;
    QVector3D scale{1, 1, 1};
    bool visible = true;
    bool locked = false;
    // Absolute LOCAL overrides: preserve imported bind orientations when posing.
    // Missing entries inherit the corresponding imported node's local TRS.
    QHash<int, QQuaternion> poseRotations;
    QHash<int, QVector3D> poseTranslations;
    QHash<int, QVector3D> poseScales;
    QHash<int, QVector<float>> morphOverrides; // Node index -> complete weight vector.
    // Additive expression bindings are evaluated over the base morph overrides;
    // the resulting working weights are clamped to [-10, 10]. Binary amounts
    // are quantized to zero/one by setReferenceExpression.
    QHash<QString, float> expressionWeights;
};
struct ReferenceCamera {
    QVector3D target{0, .8f, 0};
    float yaw = 30, pitch = 15, distance = 5, fieldOfView = 45;
    bool orthographic = false;
    float orthoScale = 3;
};
struct ReferenceLight {
    QVector3D direction{-1, -2, -1};
    QColor color{Qt::white};
    float intensity = 1;
    float ambientIntensity = .25f;
    bool shadows = true;
};
struct ReferenceScene {
    QString name;
    QVector<ReferenceInstance> instances;
    ReferenceCamera camera;
    ReferenceLight light;
    bool textured = true;
};
struct ReferenceBounds {
    QVector3D minimum, maximum;
    bool valid = false;
    void include(QVector3D point);
    QVector3D center() const { return (minimum + maximum) * .5f; }
};
struct ReferenceEvaluatedPrimitive {
    int nodeIndex = -1, meshIndex = -1, primitiveIndex = -1, materialIndex = -1;
    QVector<ReferenceVertex> vertices; // World positions/normals; includes placement.
    QVector<quint32> indices;
};
struct ReferenceEvaluation {
    QVector<QMatrix4x4> nodeGlobals; // World matrices, including instance placement.
    QVector<ReferenceEvaluatedPrimitive> primitives;
    ReferenceBounds bounds;
};

QString validateReferenceAsset(const ReferenceAsset &asset);
ReferenceAssetPtr finalizeReferenceAsset(ReferenceAsset asset, QString *error = nullptr);
QString validateReferenceInstance(const ReferenceInstance &instance);
QString validateReferenceScene(const ReferenceScene &scene);
std::optional<ReferenceEvaluation> evaluateReferenceInstance(const ReferenceInstance &instance, QString *error = nullptr);
// Skeleton-only evaluation: no vertex deformation, image decoding or mesh walk.
// Finalized/loaded immutable assets reuse their validated topology/rest matrices.
std::optional<QVector<QMatrix4x4>> evaluateReferenceNodeGlobals(const ReferenceInstance &instance, QString *error = nullptr);
// Placement only, for projecting a cached identity-placement pose. Callers must
// validate the instance before using this unchecked arithmetic helper.
QMatrix4x4 referencePlacementMatrix(const ReferenceInstance &instance);
// Expression assignment is an ordinary pose edit; callers own Undo boundaries.
bool setReferenceExpression(ReferenceInstance &instance, const QString &name, float amount, QString *error = nullptr);

QByteArray serializeReferenceAsset(const ReferenceAsset &asset, QString *error = nullptr);
ReferenceAssetPtr deserializeReferenceAsset(const QByteArray &bytes, QString *error = nullptr);
QByteArray serializeReferenceScene(const ReferenceScene &scene, QString *error = nullptr);
std::optional<ReferenceScene> deserializeReferenceScene(const QByteArray &bytes, QString *error = nullptr);
bool saveReferenceScene(const ReferenceScene &scene, const QString &path, QString *error = nullptr);
std::optional<ReferenceScene> loadReferenceScene(const QString &path, QString *error = nullptr);

struct Model3D;
// Reuses only the old triangle renderer. No legacy rig, weights or poses are
// synthesized. The legacy renderer's material restrictions are reported.
std::shared_ptr<const Model3D> referenceRenderModel(const ReferenceInstance &instance, QString *error = nullptr);
} // namespace toon
