#include "reference3d.h"
#include "scene_model_io.h"
#include <QBuffer>
#include <QCryptographicHash>
#include <QColorSpace>
#include <QDataStream>
#include <QFile>
#include <QImageReader>
#include <QJsonDocument>
#include <QSaveFile>
#include <QSet>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>

namespace toon {
struct ReferenceAssetRuntime {
    std::weak_ptr<const ReferenceAsset> owner;
    QVector<int> nodeOrder;
    QVector<QMatrix4x4> restLocals;
    qint64 decodedBytes = 0;
};
struct ReferenceAssetAccess {
    static std::shared_ptr<const ReferenceAssetRuntime> runtime(const ReferenceAsset &asset) {
        const auto cached = asset.m_runtime;
        return cached && cached->owner.lock().get() == &asset ? cached : nullptr;
    }
    static void publish(const ReferenceAssetPtr &asset, const std::shared_ptr<ReferenceAssetRuntime> &runtime) {
        runtime->owner = asset; asset->m_runtime = runtime;
    }
};
namespace {
constexpr qint64 maxMetadataBytes = 64 * 1024;
constexpr int maxExpressions = 128, maxExpressionBindings = 16384;
constexpr float maxCoordinate = 1000000.f;
void require(bool condition, const QString &message) { if (!condition) throw message; }
bool finite(float value, float bound = maxCoordinate) { return std::isfinite(value) && std::abs(value) <= bound; }
bool finite(QVector2D value, float bound = maxCoordinate) { return finite(value.x(), bound) && finite(value.y(), bound); }
bool finite(QVector3D value, float bound = maxCoordinate) { return finite(value.x(), bound) && finite(value.y(), bound) && finite(value.z(), bound); }
bool finite(QVector4D value, float bound = maxCoordinate) { return finite(value.x(), bound) && finite(value.y(), bound) && finite(value.z(), bound) && finite(value.w(), bound); }
bool validScale(QVector3D value) {
    return finite(value) && std::abs(value.x()) >= .000001f && std::abs(value.y()) >= .000001f && std::abs(value.z()) >= .000001f;
}
bool validRotation(QQuaternion value) {
    return finite(value.x()) && finite(value.y()) && finite(value.z()) && finite(value.scalar())
        && std::isfinite(value.lengthSquared()) && value.lengthSquared() > 1e-12f;
}
QQuaternion canonicalRotation(QQuaternion value) {
    // Re-normalizing an already unit float quaternion can alternate between
    // two encodings. Preserve unit values within float roundoff so repeated
    // scene saves remain byte-stable; normalize scaled source rotations once.
    const double lengthSquared = double(value.scalar()) * value.scalar() + double(value.x()) * value.x()
        + double(value.y()) * value.y() + double(value.z()) * value.z();
    if (std::abs(lengthSquared - 1.) > 4 * std::numeric_limits<float>::epsilon()) {
        const double inverseLength = 1. / std::sqrt(lengthSquared);
        value = QQuaternion(float(value.scalar() * inverseLength), float(value.x() * inverseLength),
            float(value.y() * inverseLength), float(value.z() * inverseLength));
    }
    const float lead = value.scalar() != 0 ? value.scalar() : value.x() != 0 ? value.x() : value.y() != 0 ? value.y() : value.z();
    if (lead < 0) value = -value;
    return value;
}
bool validName(const QString &name, bool empty = true) {
    return (empty || !name.trimmed().isEmpty()) && name.size() <= 256 && !name.contains(QChar::Null);
}
bool validColor(const QColor &color) {
    return color.isValid() && finite(QVector4D(color.redF(), color.greenF(), color.blueF(), color.alphaF()), 1)
        && color.redF() >= 0 && color.greenF() >= 0 && color.blueF() >= 0 && color.alphaF() >= 0;
}
bool matrixFinite(const QMatrix4x4 &matrix, float bound = 1e12f) {
    for (int i = 0; i < 16; ++i) if (!finite(matrix.constData()[i], bound)) return false;
    return true;
}
bool affine(const QMatrix4x4 &matrix) {
    return matrixFinite(matrix) && std::abs(matrix(3, 0)) < 1e-5f && std::abs(matrix(3, 1)) < 1e-5f
        && std::abs(matrix(3, 2)) < 1e-5f && std::abs(matrix(3, 3) - 1) < 1e-5f;
}
QMatrix4x4 trs(QVector3D translation, QQuaternion rotation, QVector3D scale) {
    QMatrix4x4 result; result.translate(translation); result.rotate(canonicalRotation(rotation)); result.scale(scale); return result;
}
int morphCount(const ReferenceMesh &mesh) { return mesh.primitives.isEmpty() ? 0 : mesh.primitives.first().morphTargets.size(); }
bool validMorphWeights(const QVector<float> &weights, int count, bool allowEmpty = true) {
    if (weights.isEmpty() && allowEmpty) return true;
    if (weights.size() != count) return false;
    return std::all_of(weights.cbegin(), weights.cend(), [](float value) { return finite(value, 10); });
}
QVector<float> sourceMorphWeights(const ReferenceAsset &asset, int nodeIndex) {
    const auto &node = asset.nodes[nodeIndex]; const auto &mesh = asset.meshes[node.meshIndex];
    auto result = node.morphWeights.isEmpty() ? mesh.defaultMorphWeights : node.morphWeights;
    if (result.isEmpty()) result.resize(morphCount(mesh));
    return result;
}
qint64 sourceImageCost(const QByteArray &bytes, qint64 &pixels) {
    require(!bytes.isEmpty() && bytes.size() <= maxReferenceAssetBytes, "Reference source image is empty or oversized.");
    QBuffer buffer; buffer.setData(bytes); buffer.open(QIODevice::ReadOnly); QImageReader reader(&buffer);
    const auto format = reader.format().toLower(); require(format == "png" || format == "jpeg" || format == "jpg", "Reference source images must contain PNG or JPEG data.");
    const auto size = reader.size(); require(size.width() > 0 && size.height() > 0 && size.width() <= 4096 && size.height() <= 4096, "Reference source image dimensions are invalid.");
    const qint64 area = qint64(size.width()) * size.height(); pixels += area;
    require(pixels <= maxReferenceTexturePixels, "Reference source images exceed the 16-megapixel texture budget.");
    // Only encoded bytes are retained here. Native material images have their
    // own decoded-memory charge; the separate header pixel cap bounds a future
    // decode without counting the same source image as already decoded twice.
    return bytes.size();
}
qint64 decodedCost(const ReferenceAsset &asset) {
    qint64 bytes = 0;
    for (const auto &mesh : asset.meshes) for (const auto &primitive : mesh.primitives) {
        bytes += qint64(primitive.vertices.size()) * sizeof(ReferenceVertex) + qint64(primitive.indices.size()) * 4;
        for (const auto &target : primitive.morphTargets)
            bytes += qint64(target.positionDeltas.size() + target.normalDeltas.size() + target.tangentDeltas.size()) * 12;
    }
    for (const auto &material : asset.materials)
        for (const auto *image : {&material.baseColorTexture, &material.metallicRoughnessTexture, &material.normalTexture,
                                 &material.occlusionTexture, &material.emissiveTexture})
            bytes += qint64(image->width()) * image->height() * 4;
    qint64 sourcePixels = 0;
    for (const auto &source : asset.sourceImages) bytes += sourceImageCost(source, sourcePixels);
    return bytes;
}
void checkAsset(const ReferenceAsset &asset) {
    require(validName(asset.name, false), "A reference asset needs a name of at most 256 characters.");
    require(asset.nodes.size() > 0 && asset.nodes.size() <= maxReferenceNodes, "Reference asset has an invalid node count.");
    require(asset.meshes.size() <= maxReferenceMeshes && asset.materials.size() <= maxReferenceMaterials
        && asset.skins.size() <= maxReferenceJoints, "Reference asset exceeds its mesh, material or skin limit.");
    require(asset.id.isEmpty() || (asset.id.size() == 64 && std::all_of(asset.id.cbegin(), asset.id.cend(), [](QChar c) {
        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); })), "Reference asset has an invalid content ID.");
    qint64 vertices = 0, triangles = 0, texturePixels = 0; int primitives = 0;
    for (const auto &mesh : asset.meshes) {
        require(validName(mesh.name) && !mesh.primitives.isEmpty(), "Reference mesh is unnamed, invalid or empty.");
        require(validMorphWeights(mesh.defaultMorphWeights, morphCount(mesh)), "Reference mesh has invalid default morph weights.");
        for (const auto &primitive : mesh.primitives) {
            require(++primitives <= maxReferenceMeshes, "Reference asset exceeds 256 render primitives.");
            vertices += primitive.vertices.size(); triangles += primitive.indices.size() / 3;
            require(!primitive.vertices.isEmpty() && !primitive.indices.isEmpty() && primitive.indices.size() % 3 == 0
                && vertices <= maxReferenceVertices && triangles <= maxReferenceTriangles, "Reference geometry exceeds its vertex/triangle budget or is not triangulated.");
            require(primitive.materialIndex >= -1 && primitive.materialIndex < asset.materials.size(), "Reference primitive refers to a missing material.");
            require(primitive.morphTargets.size() == morphCount(mesh) && primitive.morphTargets.size() <= maxReferenceMorphTargets,
                "All primitives in a mesh must have the same bounded morph-target count.");
            for (const auto &vertex : primitive.vertices) {
                require(finite(vertex.position) && finite(vertex.normal) && vertex.normal.lengthSquared() > 1e-12f && finite(vertex.uv)
                    && finite(vertex.tangent) && vertex.tangent.toVector3D().lengthSquared() > 1e-12f
                    && std::abs(std::abs(vertex.tangent.w()) - 1) < .001f, "Reference vertex contains invalid position, normal, tangent or UV data.");
                require(finite(vertex.color) && vertex.color.x() >= 0 && vertex.color.x() <= 1 && vertex.color.y() >= 0 && vertex.color.y() <= 1
                    && vertex.color.z() >= 0 && vertex.color.z() <= 1 && vertex.color.w() >= 0 && vertex.color.w() <= 1, "Reference vertex color is outside the normalized range.");
                float sum = 0;
                for (float weight : vertex.weights) { require(finite(weight, 1) && weight >= 0, "Reference skin contains an invalid weight."); sum += weight; }
                require(sum == 0 || std::abs(sum - 1) <= .001f, "Reference skin weights must sum to one.");
            }
            for (quint32 index : primitive.indices) require(index < quint32(primitive.vertices.size()), "Reference triangle index is out of range.");
            for (const auto &target : primitive.morphTargets) {
                require(validName(target.name), "Reference morph target has an invalid name.");
                for (const auto *deltas : {&target.positionDeltas, &target.normalDeltas, &target.tangentDeltas}) {
                    require(deltas->isEmpty() || deltas->size() == primitive.vertices.size(), "Reference morph target does not match its vertex count.");
                    for (auto delta : *deltas) require(finite(delta), "Reference morph target contains a nonfinite or excessive delta.");
                }
            }
        }
    }
    require(decodedCost(asset) <= maxReferenceAssetBytes, "Reference asset exceeds its 128 MiB decoded-data budget.");
    for (const auto &node : asset.nodes) {
        require(validName(node.name) && node.parentIndex >= -1 && node.parentIndex < asset.nodes.size(), "Reference node has an invalid name or parent.");
        require(finite(node.translation) && validRotation(node.rotation) && validScale(node.scale), "Reference node has an invalid local transform.");
        require(node.meshIndex >= -1 && node.meshIndex < asset.meshes.size() && node.skinIndex >= -1 && node.skinIndex < asset.skins.size(), "Reference node refers to a missing mesh or skin.");
        require(node.skinIndex < 0 || node.meshIndex >= 0, "A skinned reference node needs a mesh.");
        require(node.meshIndex >= 0 ? validMorphWeights(node.morphWeights, morphCount(asset.meshes[node.meshIndex])) : node.morphWeights.isEmpty(),
            "Reference node has invalid morph weights.");
    }
    for (int i = 0; i < asset.nodes.size(); ++i) {
        QSet<int> ancestors; int parent = i;
        while (parent >= 0) {
            require(!ancestors.contains(parent) && ancestors.size() < 256, "Reference node hierarchy contains a cycle or exceeds 256 levels.");
            ancestors.insert(parent); parent = asset.nodes[parent].parentIndex;
        }
    }
    for (const auto &skin : asset.skins) {
        require(validName(skin.name) && !skin.joints.isEmpty() && skin.joints.size() <= maxReferenceJoints
            && skin.inverseBindMatrices.size() == skin.joints.size(), "Reference skin has invalid joints or inverse-bind matrices.");
        require(skin.skeletonRoot >= -1 && skin.skeletonRoot < asset.nodes.size(), "Reference skin has an invalid skeleton root.");
        QSet<int> joints;
        for (int i = 0; i < skin.joints.size(); ++i) {
            const auto joint = skin.joints[i];
            require(joint >= 0 && joint < asset.nodes.size() && !joints.contains(joint), "Reference skin contains missing or duplicate joint nodes."); joints.insert(joint);
            bool invertible = false; const auto inverse = skin.inverseBindMatrices[i].inverted(&invertible); Q_UNUSED(inverse);
            require(affine(skin.inverseBindMatrices[i]) && invertible, "Reference skin has a singular, nonaffine or nonfinite inverse bind.");
            if (skin.skeletonRoot >= 0) {
                int ancestor = joint; while (ancestor >= 0 && ancestor != skin.skeletonRoot) ancestor = asset.nodes[ancestor].parentIndex;
                require(ancestor == skin.skeletonRoot, "Reference skin skeleton root is not an ancestor of every joint.");
            }
        }
    }
    for (const auto &node : asset.nodes) if (node.skinIndex >= 0) {
        const auto &skin = asset.skins[node.skinIndex];
        for (const auto &primitive : asset.meshes[node.meshIndex].primitives) for (const auto &vertex : primitive.vertices) {
            float sum = 0;
            for (int j = 0; j < 4; ++j) if (vertex.weights[j] > 0) {
                require(vertex.joints[j] < skin.joints.size(), "Reference vertex joint index is outside its node's skin."); sum += vertex.weights[j];
            }
            require(std::abs(sum - 1) <= .001f, "Every vertex of a skinned reference node needs normalized influences.");
        }
    }
    for (const auto &material : asset.materials) {
        require(validName(material.name) && validColor(material.baseColor) && validColor(material.emissive), "Reference material has an invalid name or color.");
        require(finite(material.metallic, 1) && material.metallic >= 0 && finite(material.roughness, 1) && material.roughness >= 0
            && finite(material.normalScale, 10) && finite(material.occlusionStrength, 1) && material.occlusionStrength >= 0
            && finite(material.alphaCutoff, 1) && material.alphaCutoff >= 0, "Reference material has invalid scalar properties.");
        require(material.alphaMode == ReferenceAlphaMode::Opaque || material.alphaMode == ReferenceAlphaMode::Mask || material.alphaMode == ReferenceAlphaMode::Blend,
            "Reference material has an unknown alpha mode.");
        for (const auto *image : {&material.baseColorTexture, &material.metallicRoughnessTexture, &material.normalTexture, &material.occlusionTexture, &material.emissiveTexture}) if (!image->isNull()) {
            require(image->width() <= 4096 && image->height() <= 4096 && image->width() > 0 && image->height() > 0, "Reference texture exceeds 4096 pixels per side.");
            texturePixels += qint64(image->width()) * image->height(); require(texturePixels <= maxReferenceTexturePixels, "Reference asset exceeds its 16-megapixel texture budget.");
        }
    }
    require(asset.sourceImages.size() <= maxReferenceMaterials, "Reference asset has too many source image resources.");
    qint64 sourcePixels = 0; for (const auto &source : asset.sourceImages) sourceImageCost(source, sourcePixels);
    require(asset.humanoidBones.size() <= maxReferenceJoints, "Reference asset has too many humanoid bone mappings.");
    for (auto it = asset.humanoidBones.cbegin(); it != asset.humanoidBones.cend(); ++it)
        require(validName(it.key(), false) && it.value() >= 0 && it.value() < asset.nodes.size(), "Reference humanoid bone map refers to a missing node.");
    require(asset.expressions.size() <= maxExpressions, "Reference asset has too many expressions.");
    QSet<QString> expressionNames; int bindings = 0;
    for (const auto &expression : asset.expressions) {
        require(validName(expression.name, false) && !expressionNames.contains(expression.name), "Reference expression names must be valid and unique."); expressionNames.insert(expression.name);
        bindings += expression.bindings.size(); require(bindings <= maxExpressionBindings, "Reference expressions exceed their binding budget.");
        for (const auto &binding : expression.bindings) {
            require(binding.nodeIndex >= 0 && binding.nodeIndex < asset.nodes.size(), "Reference expression refers to a missing node.");
            const int mesh = asset.nodes[binding.nodeIndex].meshIndex;
            require(mesh >= 0 && binding.morphIndex >= 0 && binding.morphIndex < morphCount(asset.meshes[mesh]) && finite(binding.weight, 10),
                "Reference expression has an invalid morph binding.");
        }
    }
    require(QJsonDocument(asset.metadata).toJson(QJsonDocument::Compact).size() <= maxMetadataBytes, "Reference asset metadata exceeds 64 KiB.");
}
std::shared_ptr<ReferenceAssetRuntime> buildRuntime(const ReferenceAsset &asset) {
    auto runtime = std::make_shared<ReferenceAssetRuntime>();
    runtime->decodedBytes = decodedCost(asset); runtime->restLocals.reserve(asset.nodes.size());
    for (const auto &node : asset.nodes) runtime->restLocals.append(trs(node.translation, node.rotation, node.scale));
    QVector<bool> complete(asset.nodes.size());
    std::function<void(int)> visit = [&](int index) {
        if (complete[index]) return;
        const int parent = asset.nodes[index].parentIndex; if (parent >= 0) visit(parent);
        complete[index] = true; runtime->nodeOrder.append(index);
    };
    for (int index = 0; index < asset.nodes.size(); ++index) visit(index);
    return runtime;
}
std::shared_ptr<const ReferenceAssetRuntime> checkedRuntime(const ReferenceAssetPtr &asset) {
    require(bool(asset), "Reference instance has no asset.");
    if (const auto cached = ReferenceAssetAccess::runtime(*asset)) return cached;
    // Public mutable source structs and edited copies are never certified here.
    // Validate them anew on each call; only finalize/load publish trusted owners.
    checkAsset(*asset); return buildRuntime(*asset);
}
ReferenceAssetPtr publishAsset(ReferenceAsset asset) {
    auto runtime = buildRuntime(asset);
    auto published = std::make_shared<const ReferenceAsset>(std::move(asset));
    ReferenceAssetAccess::publish(published, runtime); return published;
}
void checkInstance(const ReferenceInstance &instance) {
    require(validName(instance.id, false) && validName(instance.name), "Reference instance has an invalid identity or name.");
    require(bool(instance.asset) && instance.asset->id.size() == 64, "Reference instance needs a finalized immutable asset.");
    require(finite(instance.translation) && validRotation(instance.rotation) && validScale(instance.scale), "Reference instance placement is invalid.");
    const auto &asset = *instance.asset;
    for (auto it = instance.poseRotations.cbegin(); it != instance.poseRotations.cend(); ++it)
        require(it.key() >= 0 && it.key() < asset.nodes.size() && validRotation(it.value()), "Reference pose contains an invalid local rotation.");
    for (const auto *map : {&instance.poseTranslations, &instance.poseScales}) for (auto it = map->cbegin(); it != map->cend(); ++it)
        require(it.key() >= 0 && it.key() < asset.nodes.size() && (map == &instance.poseScales ? validScale(it.value()) : finite(it.value())), "Reference pose contains an invalid local translation or scale.");
    for (auto it = instance.morphOverrides.cbegin(); it != instance.morphOverrides.cend(); ++it) {
        require(it.key() >= 0 && it.key() < asset.nodes.size(), "Reference pose refers to a missing morph node.");
        const auto mesh = asset.nodes[it.key()].meshIndex;
        require(mesh >= 0 && mesh < asset.meshes.size() && validMorphWeights(it.value(), morphCount(asset.meshes[mesh]), false), "Reference pose has invalid morph weights.");
    }
    for (auto it = instance.expressionWeights.cbegin(); it != instance.expressionWeights.cend(); ++it) {
        const auto expression = std::find_if(asset.expressions.cbegin(), asset.expressions.cend(), [&](const auto &item) { return item.name == it.key(); });
        require(expression != asset.expressions.cend() && finite(it.value(), 1) && it.value() >= 0
            && (!expression->binary || it.value() == 0 || it.value() == 1), "Reference pose has an unknown expression or invalid mixer amount.");
    }
}
QVector<QMatrix4x4> nodeGlobals(const ReferenceInstance &instance, const ReferenceAssetRuntime &runtime) {
    const auto &asset = *instance.asset; QVector<QMatrix4x4> result(asset.nodes.size());
    const auto placement = referencePlacementMatrix(instance);
    for (int index : runtime.nodeOrder) {
        const auto &node = asset.nodes[index];
        const auto local = !instance.poseTranslations.contains(index) && !instance.poseRotations.contains(index) && !instance.poseScales.contains(index)
            ? runtime.restLocals[index] : trs(instance.poseTranslations.value(index, node.translation), instance.poseRotations.value(index, node.rotation), instance.poseScales.value(index, node.scale));
        result[index] = node.parentIndex >= 0 ? result[node.parentIndex] * local : placement * local;
        require(matrixFinite(result[index]), "Reference hierarchy produces a nonfinite or excessive world transform.");
    }
    return result;
}
void setup(QDataStream &stream) { stream.setVersion(QDataStream::Qt_6_0); stream.setByteOrder(QDataStream::LittleEndian); stream.setFloatingPointPrecision(QDataStream::SinglePrecision); }
struct Writer {
    QByteArray bytes; QDataStream stream{&bytes, QIODevice::WriteOnly}; bool identityPixels = false;
    explicit Writer(bool identity = false) : identityPixels(identity) { setup(stream); }
    void number(float value) { stream << (value == 0 ? 0.f : value); }
    void integer(int value) { stream << qint32(value); }
    void count(qsizetype value) { stream << quint32(value); }
    void boolean(bool value) { stream << quint8(value); }
    void blob(const QByteArray &value) { count(value.size()); stream.writeRawData(value.constData(), int(value.size())); }
    void string(const QString &value) { blob(value.toUtf8()); }
    void vector(QVector2D value) { number(value.x()); number(value.y()); }
    void vector(QVector3D value) { number(value.x()); number(value.y()); number(value.z()); }
    void vector(QVector4D value) { number(value.x()); number(value.y()); number(value.z()); number(value.w()); }
    void rotation(QQuaternion value) { value = canonicalRotation(value); vector(QVector4D(value.x(), value.y(), value.z(), value.scalar())); }
    void matrix(const QMatrix4x4 &value) { for (int i = 0; i < 16; ++i) number(value.constData()[i]); }
    void color(const QColor &value) { number(value.redF()); number(value.greenF()); number(value.blueF()); number(value.alphaF()); }
    void weights(const QVector<float> &values) { count(values.size()); for (float value : values) number(value); }
    void vectors(const QVector<QVector3D> &values) { count(values.size()); for (auto value : values) vector(value); }
    void image(const QImage &value) {
        if (value.isNull()) { blob({}); return; }
        const auto clean = value.convertToFormat(QImage::Format_RGBA8888);
        if (identityPixels) {
            // Logical identity is independent of PNG encoder version/settings.
            // Include the retained ICC profile because it changes how RGBA is
            // interpreted. No row padding, DPI, DPR or annotation enters this.
            integer(clean.width()); integer(clean.height()); blob(clean.colorSpace().iccProfile());
            count(qint64(clean.width()) * clean.height() * 4);
            for (int y = 0; y < clean.height(); ++y) stream.writeRawData(reinterpret_cast<const char *>(clean.constScanLine(y)), clean.width() * 4);
            return;
        }
        // setText(key, "") retains an empty PNG tEXt chunk in Qt. Rebuild the
        // pixel image so comments, DPI and DPR cannot change texture identity;
        // preserve its color profile because that changes pixel interpretation.
        QImage pixels(clean.size(), QImage::Format_RGBA8888); require(!pixels.isNull(), "Could not allocate a canonical reference texture.");
        for (int y = 0; y < clean.height(); ++y) std::memcpy(pixels.scanLine(y), clean.constScanLine(y), size_t(clean.width()) * 4);
        pixels.setColorSpace(clean.colorSpace());
        QByteArray png; QBuffer buffer(&png); buffer.open(QIODevice::WriteOnly);
        require(pixels.save(&buffer, "PNG"), "Could not encode a reference texture."); blob(png);
    }
};
struct Reader {
    QBuffer buffer; QDataStream stream; qint64 texturePixels = 0;
    explicit Reader(const QByteArray &bytes) : stream(&buffer) { buffer.setData(bytes); buffer.open(QIODevice::ReadOnly); setup(stream); }
    void ok() { require(stream.status() == QDataStream::Ok, "Reference package is truncated."); }
    quint32 count(quint32 maximum, qint64 minimumBytes = 1) {
        quint32 value = 0; stream >> value; ok();
        require(value <= maximum && qint64(value) * minimumBytes <= buffer.bytesAvailable(), "Reference package has an excessive or truncated array."); return value;
    }
    int integer() { qint32 value = 0; stream >> value; ok(); return value; }
    float number() { float value = 0; stream >> value; ok(); return value; }
    bool boolean() { quint8 value = 0; stream >> value; ok(); require(value <= 1, "Reference package has an invalid boolean."); return value; }
    QByteArray blob(qint64 maximum) { const auto n = count(quint32(maximum)); QByteArray value(n, Qt::Uninitialized); require(stream.readRawData(value.data(), int(n)) == int(n), "Reference package is truncated."); return value; }
    QString string() { const auto bytes = blob(2048); const auto value = QString::fromUtf8(bytes); require(value.toUtf8() == bytes && validName(value), "Reference package has an invalid UTF-8 name."); return value; }
    QVector2D vector2() { const auto x = number(), y = number(); return {x, y}; }
    QVector3D vector3() { const auto x = number(), y = number(), z = number(); return {x, y, z}; }
    QVector4D vector4() { const auto x = number(), y = number(), z = number(), w = number(); return {x, y, z, w}; }
    QQuaternion rotation() { const auto value = vector4(); return {value.w(), value.x(), value.y(), value.z()}; }
    QMatrix4x4 matrix() { float values[16]; for (auto &value : values) value = number(); return QMatrix4x4(values).transposed(); }
    QColor color() { const auto value = vector4(); require(finite(value, 1) && value.x() >= 0 && value.y() >= 0 && value.z() >= 0 && value.w() >= 0, "Reference package has an invalid color."); return QColor::fromRgbF(value.x(), value.y(), value.z(), value.w()); }
    QVector<float> weights() { QVector<float> values; const auto n = count(maxReferenceMorphTargets, 4); values.reserve(n); for (quint32 i = 0; i < n; ++i) values.append(number()); return values; }
    QVector<QVector3D> vectors() { QVector<QVector3D> values; const auto n = count(maxReferenceVertices, 12); values.reserve(n); for (quint32 i = 0; i < n; ++i) values.append(vector3()); return values; }
    QImage image() {
        const auto png = blob(maxReferenceAssetBytes); if (png.isEmpty()) return {};
        QBuffer source; source.setData(png); source.open(QIODevice::ReadOnly); QImageReader reader(&source, "PNG");
        const auto size = reader.size(); require(size.width() > 0 && size.height() > 0 && size.width() <= 4096 && size.height() <= 4096, "Reference texture dimensions are invalid.");
        texturePixels += qint64(size.width()) * size.height(); require(texturePixels <= maxReferenceTexturePixels, "Reference package exceeds its texture budget.");
        const auto result = reader.read(); require(!result.isNull(), "Reference texture could not be decoded."); return result.convertToFormat(QImage::Format_RGBA8888);
    }
    void end() { ok(); require(buffer.atEnd(), "Reference package contains trailing data."); }
};
QByteArray assetPayload(const ReferenceAsset &asset, bool identityMode = false) {
    checkAsset(asset); Writer writer(identityMode); auto &w = writer;
    if (!identityMode) w.string(asset.id);
    w.string(asset.name);
    w.count(asset.nodes.size()); for (const auto &node : asset.nodes) {
        w.string(node.name); w.integer(node.parentIndex); w.vector(node.translation); w.rotation(node.rotation); w.vector(node.scale);
        w.integer(node.meshIndex); w.integer(node.skinIndex); w.weights(node.morphWeights);
    }
    w.count(asset.meshes.size()); for (const auto &mesh : asset.meshes) {
        w.string(mesh.name); w.weights(mesh.defaultMorphWeights); w.count(mesh.primitives.size());
        for (const auto &primitive : mesh.primitives) {
            w.integer(primitive.materialIndex); w.count(primitive.vertices.size());
            for (const auto &vertex : primitive.vertices) {
                w.vector(vertex.position); w.vector(vertex.normal); w.vector(vertex.uv); w.vector(vertex.tangent); w.vector(vertex.color);
                for (auto joint : vertex.joints) w.stream << joint;
                for (float weight : vertex.weights) w.number(weight);
            }
            w.count(primitive.indices.size()); for (auto index : primitive.indices) w.stream << index;
            w.count(primitive.morphTargets.size()); for (const auto &target : primitive.morphTargets) {
                w.string(target.name); w.vectors(target.positionDeltas); w.vectors(target.normalDeltas); w.vectors(target.tangentDeltas);
            }
            require(w.bytes.size() <= maxReferenceAssetBytes, "Reference asset package exceeds 128 MiB.");
        }
    }
    w.count(asset.skins.size()); for (const auto &skin : asset.skins) {
        w.string(skin.name); w.integer(skin.skeletonRoot); w.count(skin.joints.size());
        for (int joint : skin.joints) w.integer(joint);
        for (const auto &matrix : skin.inverseBindMatrices) w.matrix(matrix);
    }
    w.count(asset.materials.size()); for (const auto &material : asset.materials) {
        w.string(material.name); w.color(material.baseColor); w.image(material.baseColorTexture); w.number(material.metallic); w.number(material.roughness);
        w.color(material.emissive); w.image(material.metallicRoughnessTexture); w.image(material.normalTexture); w.image(material.occlusionTexture); w.image(material.emissiveTexture);
        w.number(material.normalScale); w.number(material.occlusionStrength); w.integer(int(material.alphaMode)); w.number(material.alphaCutoff); w.boolean(material.doubleSided);
    }
    w.count(asset.sourceImages.size()); for (const auto &source : asset.sourceImages) w.blob(source);
    auto bones = asset.humanoidBones.keys(); std::sort(bones.begin(), bones.end()); w.count(bones.size());
    for (const auto &bone : bones) { w.string(bone); w.integer(asset.humanoidBones.value(bone)); }
    w.count(asset.expressions.size()); for (const auto &expression : asset.expressions) {
        w.string(expression.name); w.boolean(expression.binary); w.count(expression.bindings.size());
        for (const auto &binding : expression.bindings) { w.integer(binding.nodeIndex); w.integer(binding.morphIndex); w.number(binding.weight); }
    }
    w.blob(QJsonDocument(asset.metadata).toJson(QJsonDocument::Compact));
    require(w.stream.status() == QDataStream::Ok && w.bytes.size() + 40 <= maxReferenceAssetBytes, "Reference asset package exceeds 128 MiB or could not be encoded."); return w.bytes;
}
QByteArray wrap(const char *magic, const QByteArray &payload) { return QByteArray(magic, 8) + QCryptographicHash::hash(payload, QCryptographicHash::Sha256) + payload; }
QByteArray unwrap(const QByteArray &bytes, const char *magic, qint64 maximum) {
    require(bytes.size() > 40 && bytes.size() <= maximum && bytes.startsWith(QByteArray(magic, 8)), "Unsupported, oversized or invalid reference package version.");
    const auto payload = bytes.mid(40); require(QCryptographicHash::hash(payload, QCryptographicHash::Sha256) == bytes.mid(8, 32), "Reference package checksum does not match."); return payload;
}
ReferenceAsset readAsset(const QByteArray &payload) {
    Reader r(payload); ReferenceAsset asset; asset.id = r.string(); asset.name = r.string();
    for (quint32 i = 0, n = r.count(maxReferenceNodes); i < n; ++i) {
        ReferenceNode node; node.name = r.string(); node.parentIndex = r.integer(); node.translation = r.vector3(); node.rotation = r.rotation(); node.scale = r.vector3();
        node.meshIndex = r.integer(); node.skinIndex = r.integer(); node.morphWeights = r.weights(); asset.nodes.append(node);
    }
    for (quint32 i = 0, n = r.count(maxReferenceMeshes); i < n; ++i) {
        ReferenceMesh mesh; mesh.name = r.string(); mesh.defaultMorphWeights = r.weights();
        for (quint32 p = 0, count = r.count(maxReferenceMeshes); p < count; ++p) {
            ReferencePrimitive primitive; primitive.materialIndex = r.integer(); const auto vertices = r.count(maxReferenceVertices, 88); primitive.vertices.reserve(vertices);
            for (quint32 v = 0; v < vertices; ++v) {
                ReferenceVertex vertex; vertex.position = r.vector3(); vertex.normal = r.vector3(); vertex.uv = r.vector2(); vertex.tangent = r.vector4(); vertex.color = r.vector4();
                for (auto &joint : vertex.joints) { r.stream >> joint; r.ok(); }
                for (auto &weight : vertex.weights) weight = r.number(); primitive.vertices.append(vertex);
            }
            const auto indices = r.count(maxReferenceTriangles * 3, 4); primitive.indices.reserve(indices);
            for (quint32 v = 0; v < indices; ++v) { quint32 value = 0; r.stream >> value; r.ok(); primitive.indices.append(value); }
            for (quint32 t = 0, targets = r.count(maxReferenceMorphTargets); t < targets; ++t) {
                ReferenceMorphTarget target; target.name = r.string(); target.positionDeltas = r.vectors(); target.normalDeltas = r.vectors(); target.tangentDeltas = r.vectors(); primitive.morphTargets.append(target);
            }
            mesh.primitives.append(primitive);
        }
        asset.meshes.append(mesh);
        require(decodedCost(asset) <= maxReferenceAssetBytes, "Reference package exceeds its decoded geometry budget.");
    }
    for (quint32 i = 0, n = r.count(maxReferenceJoints); i < n; ++i) {
        ReferenceSkin skin; skin.name = r.string(); skin.skeletonRoot = r.integer(); const auto joints = r.count(maxReferenceJoints, 68);
        for (quint32 j = 0; j < joints; ++j) skin.joints.append(r.integer());
        for (quint32 j = 0; j < joints; ++j) skin.inverseBindMatrices.append(r.matrix()); asset.skins.append(skin);
    }
    for (quint32 i = 0, n = r.count(maxReferenceMaterials); i < n; ++i) {
        ReferenceMaterial material; material.name = r.string(); material.baseColor = r.color(); material.baseColorTexture = r.image(); material.metallic = r.number(); material.roughness = r.number();
        material.emissive = r.color(); material.metallicRoughnessTexture = r.image(); material.normalTexture = r.image(); material.occlusionTexture = r.image(); material.emissiveTexture = r.image();
        material.normalScale = r.number(); material.occlusionStrength = r.number(); material.alphaMode = ReferenceAlphaMode(r.integer()); material.alphaCutoff = r.number(); material.doubleSided = r.boolean(); asset.materials.append(material);
        require(decodedCost(asset) <= maxReferenceAssetBytes, "Reference package exceeds its decoded texture budget.");
    }
    qint64 sourcePixels = 0;
    for (quint32 i = 0, n = r.count(maxReferenceMaterials); i < n; ++i) {
        auto source = r.blob(maxReferenceAssetBytes); sourceImageCost(source, sourcePixels); asset.sourceImages.append(std::move(source));
        require(decodedCost(asset) <= maxReferenceAssetBytes, "Reference package exceeds its retained source-image budget.");
    }
    for (quint32 i = 0, n = r.count(maxReferenceJoints); i < n; ++i) {
        const auto name = r.string(); require(!asset.humanoidBones.contains(name), "Reference package duplicates a humanoid bone mapping."); asset.humanoidBones.insert(name, r.integer());
    }
    int totalBindings = 0;
    for (quint32 i = 0, n = r.count(maxExpressions); i < n; ++i) {
        ReferenceExpression expression; expression.name = r.string(); expression.binary = r.boolean(); const auto bindings = r.count(maxExpressionBindings, 12);
        totalBindings += bindings; require(totalBindings <= maxExpressionBindings, "Reference package exceeds its expression binding budget.");
        for (quint32 j = 0; j < bindings; ++j) { ReferenceExpressionBinding binding; binding.nodeIndex = r.integer(); binding.morphIndex = r.integer(); binding.weight = r.number(); expression.bindings.append(binding); } asset.expressions.append(expression);
    }
    QJsonParseError error; const auto document = QJsonDocument::fromJson(r.blob(maxMetadataBytes), &error);
    require(error.error == QJsonParseError::NoError && document.isObject(), "Reference metadata is malformed."); asset.metadata = document.object(); r.end(); return asset;
}
template<class Map, class Write> void writeMap(Writer &writer, const Map &map, Write write) {
    auto keys = map.keys(); std::sort(keys.begin(), keys.end()); writer.count(keys.size()); for (int key : keys) { writer.integer(key); write(map.value(key)); }
}
template<class Map, class Read> void readMap(Reader &reader, Map &map, Read read) {
    for (quint32 i = 0, n = reader.count(maxReferenceNodes); i < n; ++i) { const int key = reader.integer(); require(!map.contains(key), "Reference pose contains duplicate node overrides."); map.insert(key, read()); }
}
QVector3D multiply(const QMatrix3x3 &matrix, QVector3D vector) {
    return {matrix(0, 0) * vector.x() + matrix(0, 1) * vector.y() + matrix(0, 2) * vector.z(),
            matrix(1, 0) * vector.x() + matrix(1, 1) * vector.y() + matrix(1, 2) * vector.z(),
            matrix(2, 0) * vector.x() + matrix(2, 1) * vector.y() + matrix(2, 2) * vector.z()};
}
float determinant3(const QMatrix4x4 &m) {
    return m(0, 0) * (m(1, 1) * m(2, 2) - m(1, 2) * m(2, 1)) - m(0, 1) * (m(1, 0) * m(2, 2) - m(1, 2) * m(2, 0)) + m(0, 2) * (m(1, 0) * m(2, 1) - m(1, 1) * m(2, 0));
}
}

void ReferenceBounds::include(QVector3D point) {
    if (!finite(point, 1e12f)) return;
    if (!valid) { minimum = maximum = point; valid = true; return; }
    minimum = {std::min(minimum.x(), point.x()), std::min(minimum.y(), point.y()), std::min(minimum.z(), point.z())};
    maximum = {std::max(maximum.x(), point.x()), std::max(maximum.y(), point.y()), std::max(maximum.z(), point.z())};
}
QString validateReferenceAsset(const ReferenceAsset &asset) { try { checkAsset(asset); return {}; } catch (const QString &error) { return error; } }
ReferenceAssetPtr finalizeReferenceAsset(ReferenceAsset asset, QString *error) {
    try {
        checkAsset(asset); for (auto &node : asset.nodes) node.rotation = canonicalRotation(node.rotation);
        const auto payload = assetPayload(asset, true); asset.id = QString::fromLatin1(QCryptographicHash::hash(payload, QCryptographicHash::Sha256).toHex());
        if (error) error->clear(); return publishAsset(std::move(asset));
    } catch (const QString &message) { if (error) *error = message; } catch (const std::bad_alloc &) { if (error) *error = "Not enough memory for this reference asset."; } return {};
}
QString validateReferenceInstance(const ReferenceInstance &instance) {
    try { checkedRuntime(instance.asset); checkInstance(instance); return {}; } catch (const QString &error) { return error; }
}
QString validateReferenceScene(const ReferenceScene &scene) {
    try {
        require(validName(scene.name) && scene.instances.size() <= maxReferenceInstances, "Reference scene has an invalid name or too many instances.");
        QSet<QString> ids, assets; qint64 decoded = 0;
        for (const auto &instance : scene.instances) {
            const auto runtime = checkedRuntime(instance.asset);
            checkInstance(instance); require(!ids.contains(instance.id), "Reference scene contains duplicate instance identities."); ids.insert(instance.id);
            if (!assets.contains(instance.asset->id)) { decoded += runtime->decodedBytes; assets.insert(instance.asset->id); }
            require(decoded <= maxReferenceSceneBytes && assets.size() <= 64, "Reference scene exceeds its unique-asset budget.");
        }
        const auto &camera = scene.camera;
        require(finite(camera.target) && finite(camera.yaw) && finite(camera.pitch, 89.9f) && finite(camera.distance) && camera.distance > 0
            && finite(camera.fieldOfView, 179) && camera.fieldOfView >= 1 && finite(camera.orthoScale) && camera.orthoScale > 0, "Reference scene camera is invalid.");
        require(finite(scene.light.direction) && scene.light.direction.lengthSquared() > 1e-12f && validColor(scene.light.color)
            && finite(scene.light.intensity, 100) && scene.light.intensity >= 0 && finite(scene.light.ambientIntensity, 100) && scene.light.ambientIntensity >= 0, "Reference scene lighting is invalid.");
        return {};
    } catch (const QString &error) { return error; }
}
QMatrix4x4 referencePlacementMatrix(const ReferenceInstance &instance) { return trs(instance.translation, instance.rotation, instance.scale); }
std::optional<QVector<QMatrix4x4>> evaluateReferenceNodeGlobals(const ReferenceInstance &instance, QString *error) {
    try {
        const auto runtime = checkedRuntime(instance.asset); checkInstance(instance);
        auto result = nodeGlobals(instance, *runtime); if (error) error->clear(); return result;
    } catch (const QString &message) { if (error) *error = message; }
    catch (const std::bad_alloc &) { if (error) *error = "Not enough memory to evaluate this reference skeleton."; }
    return std::nullopt;
}
std::optional<ReferenceEvaluation> evaluateReferenceInstance(const ReferenceInstance &instance, QString *error) {
    try {
        const auto runtime = checkedRuntime(instance.asset); const auto &asset = *instance.asset; checkInstance(instance);
        ReferenceEvaluation result; result.nodeGlobals = nodeGlobals(instance, *runtime);
        for (int nodeIndex = 0; nodeIndex < asset.nodes.size(); ++nodeIndex) {
            const auto &node = asset.nodes[nodeIndex]; if (node.meshIndex < 0) continue;
            const auto &mesh = asset.meshes[node.meshIndex]; auto weights = instance.morphOverrides.value(nodeIndex, sourceMorphWeights(asset, nodeIndex));
            // Asset expression order is stable, so overlapping bindings have
            // deterministic additive evaluation independent of QHash order.
            for (const auto &expression : asset.expressions) {
                const float amount = instance.expressionWeights.value(expression.name, 0);
                if (amount == 0) continue;
                for (const auto &binding : expression.bindings) if (binding.nodeIndex == nodeIndex)
                    weights[binding.morphIndex] += binding.weight * amount;
            }
            for (auto &weight : weights) weight = std::clamp(weight, -10.f, 10.f);
            const auto &nodeMatrix = result.nodeGlobals[nodeIndex];
            const auto nodeNormal = nodeMatrix.normalMatrix(); const float nodeDeterminant = determinant3(nodeMatrix);
            QVector<QMatrix4x4> joints; QVector<QMatrix3x3> jointNormals; QVector<float> jointDeterminants;
            if (node.skinIndex >= 0) {
                const auto &skin = asset.skins[node.skinIndex];
                for (int j = 0; j < skin.joints.size(); ++j) {
                    const auto matrix = result.nodeGlobals[skin.joints[j]] * skin.inverseBindMatrices[j];
                    joints.append(matrix); jointNormals.append(matrix.normalMatrix()); jointDeterminants.append(determinant3(matrix));
                }
            }
            for (int p = 0; p < mesh.primitives.size(); ++p) {
                const auto &source = mesh.primitives[p]; ReferenceEvaluatedPrimitive primitive;
                primitive.nodeIndex = nodeIndex; primitive.meshIndex = node.meshIndex; primitive.primitiveIndex = p; primitive.materialIndex = source.materialIndex; primitive.indices = source.indices; primitive.vertices = source.vertices;
                bool mirrored = true;
                for (int v = 0; v < source.vertices.size(); ++v) {
                    const auto &vertex = source.vertices[v]; auto position = vertex.position, normal = vertex.normal, tangent = vertex.tangent.toVector3D();
                    for (int t = 0; t < source.morphTargets.size(); ++t) if (weights[t] != 0) {
                        const auto &target = source.morphTargets[t]; if (!target.positionDeltas.isEmpty()) position += target.positionDeltas[v] * weights[t];
                        if (!target.normalDeltas.isEmpty()) normal += target.normalDeltas[v] * weights[t]; if (!target.tangentDeltas.isEmpty()) tangent += target.tangentDeltas[v] * weights[t];
                    }
                    const QMatrix4x4 *matrix = &nodeMatrix; const QMatrix3x3 *normalMatrix = &nodeNormal; float determinant = nodeDeterminant;
                    std::optional<QMatrix4x4> blended; std::optional<QMatrix3x3> blendedNormal;
                    if (!joints.isEmpty()) {
                        int single = -1, count = 0; float total = 0;
                        for (int j = 0; j < 4; ++j) if (vertex.weights[j] > 0) { single = vertex.joints[j]; ++count; total += vertex.weights[j]; }
                        if (count == 1) { matrix = &joints[single]; normalMatrix = &jointNormals[single]; determinant = jointDeterminants[single]; }
                        else {
                            blended.emplace(); blended->fill(0);
                            for (int j = 0; j < 4; ++j) if (vertex.weights[j] > 0)
                                for (int row = 0; row < 4; ++row) for (int column = 0; column < 4; ++column)
                                    (*blended)(row, column) += joints[vertex.joints[j]](row, column) * (vertex.weights[j] / total);
                            blendedNormal = blended->normalMatrix(); matrix = &*blended; normalMatrix = &*blendedNormal; determinant = determinant3(*blended);
                        }
                    }
                    auto &evaluated = primitive.vertices[v]; evaluated.position = matrix->map(position);
                    require(finite(evaluated.position, 1e12f), "Reference deformation produces a nonfinite or excessive position.");
                    mirrored = mirrored && determinant < 0;
                    if (normal.lengthSquared() < 1e-12f) normal = vertex.normal;
                    auto worldNormal = multiply(*normalMatrix, normal);
                    if (worldNormal.lengthSquared() < 1e-12f || !finite(worldNormal, 1e12f)) worldNormal = multiply(nodeNormal, vertex.normal);
                    if (worldNormal.lengthSquared() < 1e-12f || !finite(worldNormal, 1e12f)) worldNormal = {0, 1, 0};
                    evaluated.normal = worldNormal.normalized(); auto worldTangent = matrix->mapVector(tangent);
                    worldTangent -= evaluated.normal * QVector3D::dotProduct(evaluated.normal, worldTangent);
                    if (worldTangent.lengthSquared() < 1e-12f) worldTangent = QVector3D::crossProduct(evaluated.normal, std::abs(evaluated.normal.x()) < .9f ? QVector3D(1, 0, 0) : QVector3D(0, 1, 0));
                    evaluated.tangent = QVector4D(worldTangent.normalized(), vertex.tangent.w() * (determinant < 0 ? -1 : 1)); result.bounds.include(evaluated.position);
                    // Normalization can overshoot a component of one by an ULP
                    // at axis-aligned rotations. Validate finiteness and length,
                    // rather than clipping normalized components at exactly one.
                    require(finite(evaluated.normal, 2) && finite(evaluated.tangent, 2)
                        && std::abs(evaluated.normal.lengthSquared() - 1) < .001f
                        && std::abs(evaluated.tangent.toVector3D().lengthSquared() - 1) < .001f,
                        "Reference deformation produces a nonfinite normal or tangent.");
                }
                // A common reflection was baked into positions. Reverse winding
                // so outward inverse-transpose normals still match front faces
                // when the renderer receives an identity object transform.
                if (mirrored) for (int index = 0; index < primitive.indices.size(); index += 3)
                    std::swap(primitive.indices[index + 1], primitive.indices[index + 2]);
                result.primitives.append(std::move(primitive));
            }
        }
        if (error) error->clear(); return result;
    } catch (const QString &message) { if (error) *error = message; } catch (const std::bad_alloc &) { if (error) *error = "Not enough memory to evaluate this reference pose."; } return std::nullopt;
}
bool setReferenceExpression(ReferenceInstance &instance, const QString &name, float amount, QString *error) {
    try {
        require(!instance.locked && instance.asset && finite(amount, 1) && amount >= 0, "Unlock the reference and use an expression amount from zero to one.");
        checkedRuntime(instance.asset); checkInstance(instance);
        const auto &expressions = instance.asset->expressions; const auto expression = std::find_if(expressions.cbegin(), expressions.cend(), [&](const auto &item) { return item.name == name; });
        require(expression != expressions.cend(), "This reference has no expression with that name.");
        if (expression->binary) amount = amount > .5f ? 1 : 0;
        auto next = instance; next.expressionWeights.insert(name, amount);
        checkInstance(next); instance = std::move(next); if (error) error->clear(); return true;
    } catch (const QString &message) { if (error) *error = message; return false; }
}
QByteArray serializeReferenceAsset(const ReferenceAsset &asset, QString *error) {
    try {
        const auto identity = assetPayload(asset, true); const auto id = QString::fromLatin1(QCryptographicHash::hash(identity, QCryptographicHash::Sha256).toHex());
        require(asset.id.isEmpty() || asset.id == id, "Reference asset contents no longer match its immutable content ID.");
        auto canonical = asset; canonical.id = id; const auto payload = assetPayload(canonical);
        if (error) error->clear(); return wrap("TOONRFA1", payload);
    } catch (const QString &message) { if (error) *error = message; } catch (const std::bad_alloc &) { if (error) *error = "Not enough memory to save this reference asset."; } return {};
}
ReferenceAssetPtr deserializeReferenceAsset(const QByteArray &bytes, QString *error) {
    try {
        const auto payload = unwrap(bytes, "TOONRFA1", maxReferenceAssetBytes); auto asset = readAsset(payload); checkAsset(asset);
        const auto identity = assetPayload(asset, true);
        require(asset.id == QString::fromLatin1(QCryptographicHash::hash(identity, QCryptographicHash::Sha256).toHex()), "Reference asset logical content ID does not match.");
        for (auto &node : asset.nodes) node.rotation = canonicalRotation(node.rotation);
        if (error) error->clear(); return publishAsset(std::move(asset));
    } catch (const QString &message) { if (error) *error = message; } catch (const std::bad_alloc &) { if (error) *error = "Not enough memory to load this reference asset."; } return {};
}
QByteArray serializeReferenceScene(const ReferenceScene &scene, QString *error) {
    try {
        require(validateReferenceScene(scene).isEmpty(), validateReferenceScene(scene)); Writer w; w.string(scene.name);
        QHash<QString, QByteArray> assets; QSet<const ReferenceAsset *> checked;
        for (const auto &instance : scene.instances) if (!checked.contains(instance.asset.get())) {
            checked.insert(instance.asset.get()); QString problem;
            const auto bytes = serializeReferenceAsset(*instance.asset, &problem); require(!bytes.isEmpty(), problem);
            const auto id = instance.asset->id;
            require(!assets.contains(id) || assets.value(id) == bytes, "Reference scene contains conflicting assets with the same content identity."); assets.insert(id, bytes);
        }
        auto keys = assets.keys(); std::sort(keys.begin(), keys.end()); w.count(keys.size());
        for (const auto &key : keys) { w.string(key); w.blob(assets.value(key)); require(w.bytes.size() <= maxReferenceSceneBytes, "Reference scene exceeds its 256 MiB package budget."); }
        w.count(scene.instances.size()); for (const auto &instance : scene.instances) {
            w.string(instance.id); w.string(instance.name); w.string(instance.asset->id); w.vector(instance.translation); w.rotation(instance.rotation); w.vector(instance.scale); w.boolean(instance.visible); w.boolean(instance.locked);
            writeMap(w, instance.poseRotations, [&](auto value) { w.rotation(value); }); writeMap(w, instance.poseTranslations, [&](auto value) { w.vector(value); });
            writeMap(w, instance.poseScales, [&](auto value) { w.vector(value); }); writeMap(w, instance.morphOverrides, [&](const auto &value) { w.weights(value); });
            auto expressions = instance.expressionWeights.keys(); std::sort(expressions.begin(), expressions.end()); w.count(expressions.size());
            for (const auto &name : expressions) { w.string(name); w.number(instance.expressionWeights.value(name)); }
        }
        w.vector(scene.camera.target); w.number(scene.camera.yaw); w.number(scene.camera.pitch); w.number(scene.camera.distance); w.number(scene.camera.fieldOfView); w.boolean(scene.camera.orthographic); w.number(scene.camera.orthoScale);
        w.vector(scene.light.direction); w.color(scene.light.color); w.number(scene.light.intensity); w.number(scene.light.ambientIntensity); w.boolean(scene.light.shadows); w.boolean(scene.textured);
        require(w.stream.status() == QDataStream::Ok && w.bytes.size() + 40 <= maxReferenceSceneBytes, "Reference scene could not be encoded within its package budget.");
        if (error) error->clear(); return wrap("TOONRFS1", w.bytes);
    } catch (const QString &message) { if (error) *error = message; } catch (const std::bad_alloc &) { if (error) *error = "Not enough memory to save this reference scene."; } return {};
}
std::optional<ReferenceScene> deserializeReferenceScene(const QByteArray &bytes, QString *error) {
    try {
        Reader r(unwrap(bytes, "TOONRFS1", maxReferenceSceneBytes)); ReferenceScene scene; scene.name = r.string(); QHash<QString, ReferenceAssetPtr> assets; qint64 decoded = 0;
        for (quint32 i = 0, n = r.count(64); i < n; ++i) {
            const auto id = r.string(); require(!assets.contains(id), "Reference scene duplicates an immutable asset.");
            QString problem; auto asset = deserializeReferenceAsset(r.blob(maxReferenceAssetBytes), &problem); require(bool(asset), problem); require(asset->id == id, "Reference scene asset content ID does not match.");
            decoded += decodedCost(*asset); require(decoded <= maxReferenceSceneBytes, "Reference scene exceeds its decoded-data budget."); assets.insert(id, std::move(asset));
        }
        QSet<QString> used;
        for (quint32 i = 0, n = r.count(maxReferenceInstances); i < n; ++i) {
            ReferenceInstance instance; instance.id = r.string(); instance.name = r.string(); const auto assetId = r.string(); instance.asset = assets.value(assetId); require(bool(instance.asset), "Reference scene instance refers to a missing asset."); used.insert(assetId);
            instance.translation = r.vector3(); instance.rotation = r.rotation(); instance.scale = r.vector3(); instance.visible = r.boolean(); instance.locked = r.boolean();
            readMap(r, instance.poseRotations, [&] { return r.rotation(); }); readMap(r, instance.poseTranslations, [&] { return r.vector3(); });
            readMap(r, instance.poseScales, [&] { return r.vector3(); }); readMap(r, instance.morphOverrides, [&] { return r.weights(); }); scene.instances.append(std::move(instance));
            auto &stored = scene.instances.last();
            for (quint32 e = 0, count = r.count(maxExpressions); e < count; ++e) {
                const auto name = r.string(); require(!stored.expressionWeights.contains(name), "Reference pose duplicates an expression mixer amount."); stored.expressionWeights.insert(name, r.number());
            }
        }
        require(used.size() == assets.size(), "Reference scene contains unused asset resources.");
        scene.camera.target = r.vector3(); scene.camera.yaw = r.number(); scene.camera.pitch = r.number(); scene.camera.distance = r.number(); scene.camera.fieldOfView = r.number(); scene.camera.orthographic = r.boolean(); scene.camera.orthoScale = r.number();
        scene.light.direction = r.vector3(); scene.light.color = r.color(); scene.light.intensity = r.number(); scene.light.ambientIntensity = r.number(); scene.light.shadows = r.boolean(); scene.textured = r.boolean(); r.end();
        const auto problem = validateReferenceScene(scene); require(problem.isEmpty(), problem); if (error) error->clear(); return scene;
    } catch (const QString &message) { if (error) *error = message; } catch (const std::bad_alloc &) { if (error) *error = "Not enough memory to load this reference scene."; } return std::nullopt;
}
bool saveReferenceScene(const ReferenceScene &scene, const QString &path, QString *error) {
    const auto bytes = serializeReferenceScene(scene, error); if (bytes.isEmpty()) return false;
    QSaveFile file(path); if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) { if (error) *error = "Could not save the reference scene: " + file.errorString(); return false; } return true;
}
std::optional<ReferenceScene> loadReferenceScene(const QString &path, QString *error) {
    QFile file(path); if (!file.open(QIODevice::ReadOnly) || file.size() > maxReferenceSceneBytes || file.size() <= 40) { if (error) *error = "Cannot open reference scene or its size is outside the supported range."; return std::nullopt; }
    const auto bytes = file.readAll(); if (bytes.size() != file.size()) { if (error) *error = "Could not read the complete reference scene."; return std::nullopt; } return deserializeReferenceScene(bytes, error);
}
std::shared_ptr<const Model3D> referenceRenderModel(const ReferenceInstance &instance, QString *error) {
    const auto evaluation = evaluateReferenceInstance(instance, error); if (!evaluation) return {};
    Model3D model; model.name = instance.name.isEmpty() ? instance.asset->name : instance.name;
    for (const auto &primitive : evaluation->primitives) {
        ModelMesh3D mesh; mesh.name = instance.asset->nodes[primitive.nodeIndex].name; mesh.indices = primitive.indices;
        if (primitive.materialIndex >= 0) {
            const auto &material = instance.asset->materials[primitive.materialIndex];
            if (material.alphaMode == ReferenceAlphaMode::Blend) { if (error) *error = "The review renderer cannot display blended transparency yet; the source material is preserved."; return {}; }
            mesh.baseColor = material.baseColor; mesh.baseColorTexture = material.baseColorTexture; mesh.doubleSided = material.doubleSided;
            mesh.alphaCutoff = material.alphaMode == ReferenceAlphaMode::Mask ? material.alphaCutoff : -1;
        }
        for (const auto &vertex : primitive.vertices) {
            if ((vertex.color - QVector4D(1, 1, 1, 1)).lengthSquared() > 1e-8f) { if (error) *error = "The review renderer cannot display vertex colors yet; the source colors are preserved."; return {}; }
            mesh.positions.append(vertex.position); mesh.normals.append(vertex.normal); mesh.texCoords.append(vertex.uv);
        }
        model.meshes.append(std::move(mesh));
    }
    if (model.meshes.isEmpty()) { if (error) *error = "This reference contains no drawable mesh primitives."; return {}; }
    return finalizeModel3D(std::move(model), error);
}
} // namespace toon
