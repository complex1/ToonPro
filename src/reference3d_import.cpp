#include "reference3d_import.h"
#include <QBuffer>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSet>
#include <QUrl>
#include <QtEndian>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <limits>
#include <new>

#define CGLTF_IMPLEMENTATION
#include "../third-party/cgltf/cgltf.h"

namespace toon {
namespace {
constexpr qint64 maxJsonBytes = 16LL * 1024 * 1024;
constexpr qint64 maxDecodedBytes = 256LL * 1024 * 1024;
[[noreturn]] void fail(const QString &message) { throw message; }
void require(bool valid, const QString &message) { if (!valid) fail(message); }
int integer(const QJsonValue &value, int low, int high, const QString &context) {
    const double n = value.toDouble(std::numeric_limits<double>::quiet_NaN());
    require(value.isDouble() && std::isfinite(n) && n == std::floor(n) && n >= low && n <= high,
            "Invalid " + context + ".");
    return int(n);
}
int number(const QJsonObject &object, const QString &key, int low, int high, int fallback = -1) {
    return object.contains(key) ? integer(object.value(key), low, high, key) : fallback;
}
float finite(double n, const QString &context, float limit = 1000000.f) {
    require(std::isfinite(n) && std::abs(n) <= limit, "Nonfinite or excessive " + context + ".");
    return float(n);
}
QJsonObject objectAt(const QJsonArray &array, int index, const QString &context) {
    require(index >= 0 && index < array.size() && array[index].isObject(), "Invalid " + context + " reference.");
    return array[index].toObject();
}
QJsonArray array(const QJsonObject &object, const QString &key) {
    require(!object.contains(key) || object[key].isArray(), "Invalid " + key + " array.");
    return object[key].toArray();
}
QString text(const char *value) { return value ? QString::fromUtf8(value).left(256) : QString(); }
template<class T> int indexOf(const T *value, const T *first) { return value ? int(value - first) : -1; }
struct AllocationBudget { size_t used = 0; };
struct alignas(std::max_align_t) Allocation { size_t size; };
void *allocate(void *user, cgltf_size size) {
    auto &budget = *static_cast<AllocationBudget *>(user);
    if (size > size_t(maxReferenceAssetBytes) || budget.used > size_t(maxReferenceAssetBytes) - size) return nullptr;
    auto *allocation = static_cast<Allocation *>(std::malloc(sizeof(Allocation) + size));
    if (!allocation) return nullptr;
    allocation->size = size; budget.used += size; return allocation + 1;
}
void release(void *user, void *pointer) {
    if (!pointer) return;
    auto *allocation = static_cast<Allocation *>(pointer) - 1;
    static_cast<AllocationBudget *>(user)->used -= allocation->size; std::free(allocation);
}

class Reader {
public:
    Reader(QString path, const ReferenceImportOptions &options) : path(std::move(path)), options(options) {}
    ReferenceImportResult read();
private:
    QString path, sourceRoot;
    const ReferenceImportOptions &options;
    QJsonObject root;
    QByteArray fileBytes, jsonBytes, binary;
    QVector<QByteArray> buffers;
    QVector<QByteArray> encodedImages;
    QVector<QImage> images;
    cgltf_data *data = nullptr;
    qint64 resourceBytes = 0, decodedBytes = 0, texturePixels = 0, vertices = 0, triangles = 0;
    QStringList warnings;
    void checkCancelled() const { require(!options.cancelled || !options.cancelled->load(), "3D import cancelled."); }
    void warn(const QString &message) { if (!warnings.contains(message)) warnings.append(message); }
    void budget(qint64 count, qint64 elementBytes) {
        require(count >= 0 && elementBytes >= 0 && count <= maxDecodedBytes / qMax<qint64>(1, elementBytes), "Decoded model data exceeds its memory budget.");
        decodedBytes += count * elementBytes;
        require(decodedBytes <= maxDecodedBytes, "Decoded model data exceeds 256 MiB.");
    }
    QByteArray readFile(const QString &file, bool account = true) {
        checkCancelled(); QFile input(file);
        require(input.open(QIODevice::ReadOnly), "Cannot open 3D resource: " + QFileInfo(file).fileName());
        require(input.size() > 0 && input.size() <= maxReferenceAssetBytes, "A 3D resource is empty or exceeds 128 MiB.");
        if (account) {
            resourceBytes += input.size();
            require(resourceBytes <= maxReferenceAssetBytes, "3D file and resources exceed 128 MiB.");
        }
        const auto size = input.size(); auto bytes = input.readAll();
        require(bytes.size() == size, "Could not read the complete 3D resource."); return bytes;
    }
    QByteArray resource(const QString &uri) {
        checkCancelled(); require(!uri.isEmpty() && !uri.contains(QChar::Null), "Missing or invalid 3D resource URI.");
        if (uri.startsWith("data:")) {
            const int comma = uri.indexOf(',');
            require(comma > 0 && uri.left(comma).endsWith(";base64"), "Only base64 data resources are supported.");
            require(uri.size() - comma <= maxReferenceAssetBytes * 4 / 3 + 4, "Embedded 3D resource exceeds its size limit.");
            auto decoded = QByteArray::fromBase64Encoding(uri.mid(comma + 1).toLatin1(), QByteArray::AbortOnBase64DecodingErrors);
            require(bool(decoded), "Invalid base64 3D resource."); resourceBytes += decoded.decoded.size();
            require(resourceBytes <= maxReferenceAssetBytes, "3D file and resources exceed 128 MiB."); return decoded.decoded;
        }
        const QUrl url(uri, QUrl::StrictMode);
        require(url.isValid() && url.isRelative() && url.scheme().isEmpty() && url.authority().isEmpty()
                && !url.hasQuery() && !url.hasFragment(), "3D resources must be local relative files; network access is disabled.");
        const auto relative = QUrl::fromPercentEncoding(uri.toUtf8());
        require(!QDir::isAbsolutePath(relative) && !relative.contains('\\') && !relative.contains(':')
                && !relative.contains(QChar::Null), "Invalid 3D resource path.");
        const auto segments = relative.split('/');
        require(!segments.contains(".."), "3D resource path traversal is not allowed.");
        const auto resolved = QFileInfo(QDir(sourceRoot).filePath(relative)).canonicalFilePath();
        require(!resolved.isEmpty() && resolved.startsWith(sourceRoot + '/'), "3D resources must remain inside their source directory.");
        return readFile(resolved);
    }
    void container();
    void validateJson();
    void loadBuffers();
    void validateAccessors();
    void loadImages();
    QVector<float> unpack(const cgltf_accessor *accessor, cgltf_type type, int components, const QString &context);
    ReferenceMaterial material(int index);
    QImage texture(const cgltf_texture_view &view, const QString &context);
    ReferencePrimitive primitive(const cgltf_primitive &source, const cgltf_mesh &mesh);
    ReferenceNode node(int index);
    void avatar(ReferenceAsset &asset);
};

void Reader::container() {
    const auto extension = QFileInfo(path).suffix().toLower();
    require(extension == "gltf" || extension == "glb" || extension == "vrm", "Choose a glTF 2.0, GLB or VRM model.");
    sourceRoot = QFileInfo(path).absoluteDir().canonicalPath();
    require(!sourceRoot.isEmpty(), "The model source directory is unavailable."); fileBytes = readFile(path);
    const bool glb = fileBytes.size() >= 4 && qFromLittleEndian<quint32>(reinterpret_cast<const uchar *>(fileBytes.constData())) == 0x46546c67;
    if (extension == "gltf") require(!glb, "A .gltf file must contain JSON rather than a GLB container.");
    else require(glb, "A .glb or .vrm file must contain a GLB 2 container.");
    if (glb) {
        require(fileBytes.size() >= 20, "Truncated GLB header.");
        auto u32 = [&](int offset) { return qFromLittleEndian<quint32>(reinterpret_cast<const uchar *>(fileBytes.constData() + offset)); };
        require(u32(4) == 2 && u32(8) == quint32(fileBytes.size()), "GLB version or declared file length is invalid.");
        qint64 cursor = 12; int chunks = 0;
        while (cursor < fileBytes.size()) {
            require(cursor + 8 <= fileBytes.size(), "Truncated GLB chunk header.");
            const auto length = u32(int(cursor)), kind = u32(int(cursor) + 4); cursor += 8;
            require(length % 4 == 0 && cursor + length <= fileBytes.size(), "GLB chunk exceeds its container or is not aligned.");
            if (chunks == 0) {
                require(kind == 0x4e4f534a && length <= maxJsonBytes, "GLB must begin with JSON of at most 16 MiB.");
                jsonBytes = fileBytes.mid(cursor, length);
            } else if (kind == 0x004e4942) {
                require(binary.isEmpty() && chunks == 1, "GLB contains duplicate or misplaced binary chunks."); binary = fileBytes.mid(cursor, length);
            } else fail("Unsupported GLB chunk type.");
            cursor += length; ++chunks;
        }
    } else jsonBytes = fileBytes;
    require(!jsonBytes.isEmpty() && jsonBytes.size() <= maxJsonBytes, "3D JSON exceeds 16 MiB or is empty.");
    QJsonParseError error; const auto document = QJsonDocument::fromJson(jsonBytes, &error);
    require(error.error == QJsonParseError::NoError && document.isObject(), "Invalid 3D JSON: " + error.errorString()); root = document.object();
}

void Reader::validateJson() {
    require(root["asset"].isObject() && root["asset"].toObject()["version"].toString() == "2.0", "Only glTF 2.0 is supported.");
    const auto minimum = root["asset"].toObject()["minVersion"].toString();
    require(minimum.isEmpty() || minimum == "2.0", "The asset requires a newer glTF version.");
    const QHash<QString, int> limits{{"nodes", maxReferenceNodes}, {"meshes", maxReferenceMeshes},
        {"skins", 256}, {"materials", maxReferenceMaterials}, {"textures", 512}, {"images", 256},
        {"buffers", 256}, {"bufferViews", 8192}, {"accessors", 8192}, {"samplers", 512}, {"scenes", 1}, {"animations", 0}};
    for (auto it = limits.cbegin(); it != limits.cend(); ++it)
        require(array(root, it.key()).size() <= it.value(), "Unsupported or excessive " + it.key() + " count (maximum " + QString::number(it.value()) + ").");
    require(!array(root, "meshes").isEmpty() && !array(root, "nodes").isEmpty(), "The reference asset has no mesh nodes.");
    const QSet<QString> supported{"VRM", "VRMC_vrm", "KHR_materials_unlit"};
    const auto usedExtensions = array(root, "extensionsUsed");
    for (const auto &extension : array(root, "extensionsRequired")) {
        require(extension.isString() && supported.contains(extension.toString()), "Required glTF extension is not implemented: " + extension.toString());
        require(usedExtensions.contains(extension), "A required glTF extension must also be listed in extensionsUsed.");
    }
    for (const auto &extension : usedExtensions) {
        require(extension.isString(), "Invalid glTF extension name.");
        if (!supported.contains(extension.toString())) warn("Optional extension data is retained in metadata but its behavior is not implemented: " + extension.toString());
    }
    const auto sourceBuffers = array(root, "buffers"), views = array(root, "bufferViews");
    for (const auto &value : sourceBuffers) {
        require(value.isObject(), "Malformed glTF buffer.");
        require(number(value.toObject(), "byteLength", 1, int(maxReferenceAssetBytes)) > 0, "Missing glTF buffer length.");
    }
    for (int i = 0; i < views.size(); ++i) {
        const auto view = objectAt(views, i, "buffer view");
        const int bi = number(view, "buffer", 0, sourceBuffers.size() - 1);
        require(bi >= 0, "Missing buffer-view buffer reference.");
        const int offset = number(view, "byteOffset", 0, int(maxReferenceAssetBytes), 0);
        const int length = number(view, "byteLength", 1, int(maxReferenceAssetBytes));
        require(length > 0 && qint64(offset) + length <= sourceBuffers[bi].toObject()["byteLength"].toInteger(), "Buffer view exceeds its declared buffer.");
        if (view.contains("byteStride")) require(number(view, "byteStride", 4, 252) % 4 == 0, "Invalid buffer-view stride.");
        require(!view["extensions"].toObject().contains("EXT_meshopt_compression"), "Meshopt-compressed buffers are not implemented.");
    }
    const auto accessors = array(root, "accessors");
    const QSet<QString> shapes{"SCALAR", "VEC2", "VEC3", "VEC4", "MAT2", "MAT3", "MAT4"};
    for (int i = 0; i < accessors.size(); ++i) {
        const auto accessor = objectAt(accessors, i, "accessor");
        const int count = number(accessor, "count", 1, maxReferenceTriangles * 3);
        require(count > 0 && shapes.contains(accessor["type"].toString()), "Invalid accessor shape or count.");
        const int ct = number(accessor, "componentType", 5120, 5126);
        require(ct >= 5120 && ct <= 5126 && ct != 5124, "Missing or unsupported accessor component type.");
        const int componentBytes = ct <= 5121 ? 1 : ct <= 5123 ? 2 : 4;
        const int offset = number(accessor, "byteOffset", 0, int(maxReferenceAssetBytes), 0);
        require(offset % componentBytes == 0, "Misaligned accessor offset.");
        require(!accessor.contains("normalized") || accessor["normalized"].isBool(), "Invalid accessor normalization flag.");
        require(!accessor["normalized"].toBool() || (ct != 5125 && ct != 5126), "Float/uint32 accessors cannot be normalized.");
        if (accessor.contains("bufferView")) {
            const auto view = objectAt(views, number(accessor, "bufferView", 0, views.size() - 1), "accessor buffer view");
            require(number(view, "byteOffset", 0, int(maxReferenceAssetBytes), 0) % componentBytes == 0, "Misaligned buffer-view offset.");
        } else require(accessor.contains("sparse") && offset == 0, "Accessor without a buffer view must have sparse data and no byte offset.");
        if (accessor.contains("sparse")) {
            require(accessor["sparse"].isObject(), "Malformed sparse accessor.");
            const auto sparse = accessor["sparse"].toObject();
            require(number(sparse, "count", 1, count) > 0 && sparse["indices"].isObject() && sparse["values"].isObject(), "Invalid sparse accessor count or data.");
            const auto indices = sparse["indices"].toObject(), values = sparse["values"].toObject();
            const int sparseType = number(indices, "componentType", 5121, 5125);
            require(sparseType == 5121 || sparseType == 5123 || sparseType == 5125, "Invalid sparse index type.");
            for (const auto &entry : {indices, values}) {
                const auto view = objectAt(views, number(entry, "bufferView", 0, views.size() - 1), "sparse buffer view");
                require(!view.contains("byteStride"), "Sparse accessor buffers cannot be interleaved.");
                number(entry, "byteOffset", 0, int(maxReferenceAssetBytes), 0);
            }
        }
    }
    const auto nodes = array(root, "nodes"); QSet<int> children;
    for (int i = 0; i < nodes.size(); ++i) {
        const auto node = objectAt(nodes, i, "node");
        for (const auto &value : array(node, "children")) {
            const int child = integer(value, 0, nodes.size() - 1, "child node");
            require(child != i && !children.contains(child), "Node hierarchy has a self-reference, duplicate child or multiple parents."); children.insert(child);
        }
    }
}

void Reader::loadBuffers() {
    buffers.reserve(int(data->buffers_count));
    for (cgltf_size i = 0; i < data->buffers_count; ++i) {
        checkCancelled(); auto &buffer = data->buffers[i]; QByteArray bytes;
        if (buffer.uri) bytes = resource(QString::fromUtf8(buffer.uri));
        else {
            require(i == 0 && !binary.isEmpty(), "A glTF buffer has no resource."); bytes = binary;
            require(bytes.size() >= qint64(buffer.size) && bytes.size() - qint64(buffer.size) <= 3, "GLB binary length differs from its declared buffer length.");
            bytes.truncate(qsizetype(buffer.size));
        }
        require(bytes.size() == qint64(buffer.size), "Loaded glTF buffer length differs from its declaration.");
        buffers.append(std::move(bytes));
        buffer.data = buffers.last().data(); buffer.data_free_method = cgltf_data_free_method_none;
    }
}

void Reader::validateAccessors() {
    for (cgltf_size i = 0; i < data->nodes_count; ++i) {
        const auto *parent = data->nodes[i].parent; int depth = 0;
        while (parent && depth < 128) { parent = parent->parent; ++depth; }
        require(!parent, "Node hierarchy is cyclic or exceeds 128 levels.");
    }
    for (cgltf_size i = 0; i < data->accessors_count; ++i) {
        const auto &a = data->accessors[i];
        require(a.count > 0 && a.count <= size_t(maxReferenceTriangles * 3), "Invalid accessor count.");
        require(a.stride >= cgltf_calc_size(a.type, a.component_type) && a.stride <= 256,
                "Invalid accessor stride.");
        if (a.buffer_view) {
            const auto bytes = cgltf_calc_size(a.type, a.component_type);
            require(a.offset <= a.buffer_view->size && a.count - 1 <= (a.buffer_view->size - a.offset) / a.stride
                    && a.offset + (a.count - 1) * a.stride + bytes <= a.buffer_view->size, "Accessor exceeds its buffer view.");
        }
        if (a.is_sparse) {
            const auto &s = a.sparse;
            require(s.count > 0 && s.count <= a.count && s.indices_buffer_view && s.values_buffer_view, "Malformed sparse accessor.");
            const auto indexBytes = cgltf_component_size(s.indices_component_type);
            const auto valueBytes = cgltf_calc_size(a.type, a.component_type);
            const auto componentBytes = cgltf_component_size(a.component_type);
            require(indexBytes && valueBytes && componentBytes && s.indices_byte_offset % indexBytes == 0
                    && (s.indices_buffer_view->offset + s.indices_byte_offset) % indexBytes == 0
                    && s.values_byte_offset % componentBytes == 0
                    && (s.values_buffer_view->offset + s.values_byte_offset) % componentBytes == 0
                    && s.indices_byte_offset <= s.indices_buffer_view->size
                    && s.count <= (s.indices_buffer_view->size - s.indices_byte_offset) / indexBytes
                    && s.values_byte_offset <= s.values_buffer_view->size
                    && s.count <= (s.values_buffer_view->size - s.values_byte_offset) / valueBytes, "Sparse accessor exceeds its data buffers.");
            const auto *start = static_cast<const uchar *>(s.indices_buffer_view->buffer->data)
                + s.indices_buffer_view->offset + s.indices_byte_offset;
            quint32 previous = 0;
            for (cgltf_size j = 0; j < s.count; ++j) {
                const auto *p = start + j * indexBytes;
                const quint32 value = indexBytes == 1 ? *p : indexBytes == 2 ? qFromLittleEndian<quint16>(p) : qFromLittleEndian<quint32>(p);
                require(value < a.count && (j == 0 || value > previous), "Sparse accessor indices must be increasing, unique and in range."); previous = value;
            }
        }
    }
    require(cgltf_validate(data) == cgltf_result_success, "glTF structural validation failed.");
}

QVector<float> Reader::unpack(const cgltf_accessor *accessor, cgltf_type type, int components, const QString &context) {
    checkCancelled(); require(accessor && accessor->type == type, "Unexpected " + context + " accessor shape.");
    budget(qint64(accessor->count), components * sizeof(float));
    QVector<float> result(qsizetype(accessor->count) * components);
    require(cgltf_accessor_unpack_floats(accessor, result.data(), size_t(result.size())) == size_t(result.size()), "Could not decode " + context + " accessor.");
    for (const float value : result) finite(value, context);
    return result;
}

void Reader::loadImages() {
    images.reserve(int(data->images_count));
    encodedImages.reserve(int(data->images_count));
    for (cgltf_size i = 0; i < data->images_count; ++i) {
        checkCancelled(); const auto &image = data->images[i]; QByteArray bytes;
        require(bool(image.uri) != bool(image.buffer_view), "An image must have exactly one URI or buffer view.");
        if (image.uri) bytes = resource(QString::fromUtf8(image.uri));
        else {
            require(image.buffer_view->buffer && image.buffer_view->buffer->data, "Image buffer is unavailable.");
            bytes = QByteArray(static_cast<const char *>(image.buffer_view->buffer->data) + image.buffer_view->offset, qsizetype(image.buffer_view->size));
        }
        QBuffer input(&bytes); input.open(QIODevice::ReadOnly); QImageReader decoder(&input);
        const auto format = decoder.format().toLower();
        require(format == "png" || format == "jpeg" || format == "jpg", "Only PNG/JPEG textures are implemented; compressed/WebP/KTX textures are not supported.");
        const auto size = decoder.size();
        require(size.isValid() && size.width() <= 4096 && size.height() <= 4096, "Texture dimensions exceed 4096 pixels per side.");
        texturePixels += qint64(size.width()) * size.height();
        require(texturePixels <= maxReferenceTexturePixels, "Textures exceed the 16-megapixel decoded budget.");
        budget(qint64(size.width()) * size.height(), 4);
        auto decoded = decoder.read(); require(!decoded.isNull(), "Could not decode a model texture.");
        decoded = decoded.convertToFormat(QImage::Format_RGBA8888); decoded.setDevicePixelRatio(1);
        for (const auto &key : decoded.textKeys()) decoded.setText(key, {});
        encodedImages.append(std::move(bytes));
        images.append(std::move(decoded));
    }
}

QImage Reader::texture(const cgltf_texture_view &view, const QString &context) {
    if (!view.texture) return {};
    require(view.texcoord == 0 && !view.has_transform, "Only UV0 without texture-transform extensions is implemented: " + context);
    require(!view.texture->has_basisu && !view.texture->has_webp, "Compressed or WebP textures are not implemented.");
    if (const auto *sampler = view.texture->sampler) {
        require(sampler->wrap_s == cgltf_wrap_mode_repeat && sampler->wrap_t == cgltf_wrap_mode_repeat,
                "Only repeating texture samplers are implemented.");
        if (sampler->mag_filter != cgltf_filter_type_undefined || sampler->min_filter != cgltf_filter_type_undefined)
            warn("Texture filter settings are preserved in source metadata; preview filtering uses renderer defaults.");
    }
    const int image = indexOf(view.texture->image, data->images);
    require(image >= 0 && image < images.size(), "Texture refers to a missing image."); return images[image];
}

ReferenceMaterial Reader::material(int index) {
    const auto &source = data->materials[index]; ReferenceMaterial result; result.name = text(source.name);
    const auto &pbr = source.pbr_metallic_roughness;
    auto factor = [&](float n, const QString &context) { require(std::isfinite(n) && n >= 0 && n <= 1, "Invalid " + context + " factor."); return n; };
    result.baseColor = QColor::fromRgbF(factor(pbr.base_color_factor[0], "base color"), factor(pbr.base_color_factor[1], "base color"),
        factor(pbr.base_color_factor[2], "base color"), factor(pbr.base_color_factor[3], "base color"));
    result.metallic = factor(pbr.metallic_factor, "metallic"); result.roughness = factor(pbr.roughness_factor, "roughness");
    result.emissive = QColor::fromRgbF(factor(source.emissive_factor[0], "emissive"), factor(source.emissive_factor[1], "emissive"), factor(source.emissive_factor[2], "emissive"));
    result.baseColorTexture = texture(pbr.base_color_texture, "base color");
    result.metallicRoughnessTexture = texture(pbr.metallic_roughness_texture, "metallic/roughness");
    result.normalTexture = texture(source.normal_texture, "normal"); result.normalScale = finite(source.normal_texture.scale, "normal scale");
    result.occlusionTexture = texture(source.occlusion_texture, "occlusion"); result.occlusionStrength = factor(source.occlusion_texture.scale, "occlusion");
    result.emissiveTexture = texture(source.emissive_texture, "emissive");
    result.alphaMode = source.alpha_mode == cgltf_alpha_mode_blend ? ReferenceAlphaMode::Blend
        : source.alpha_mode == cgltf_alpha_mode_mask ? ReferenceAlphaMode::Mask : ReferenceAlphaMode::Opaque;
    result.alphaCutoff = factor(source.alpha_cutoff, "alpha cutoff"); result.doubleSided = source.double_sided;
    if (source.unlit) warn("Unlit material semantics are retained in metadata; the initial reference preview uses its shared lighting model.");
    if (source.has_pbr_specular_glossiness || source.has_clearcoat || source.has_transmission || source.has_volume
        || source.has_sheen || source.has_iridescence || source.has_specular || source.has_emissive_strength
        || source.has_anisotropy || source.has_dispersion || source.has_diffuse_transmission || source.has_ior)
        warn("Extended material data is preserved in metadata; only core metallic/roughness material fields are evaluated in this import slice.");
    return result;
}

ReferencePrimitive Reader::primitive(const cgltf_primitive &source, const cgltf_mesh &mesh) {
    checkCancelled(); require(source.type == cgltf_primitive_type_triangles, "Only triangulated mesh primitives are implemented.");
    require(!source.has_draco_mesh_compression && !source.mappings_count, "Draco compression and material-variant primitives are not implemented.");
    require(source.targets_count <= size_t(maxReferenceMorphTargets), "A mesh exceeds 128 morph targets.");
    const cgltf_accessor *positions = nullptr, *normals = nullptr, *uv = nullptr, *tangents = nullptr, *colors = nullptr;
    QHash<int, const cgltf_accessor *> jointSets, weightSets;
    QSet<QString> attributes;
    for (cgltf_size i = 0; i < source.attributes_count; ++i) {
        const auto &attribute = source.attributes[i]; const auto name = text(attribute.name);
        require(!attributes.contains(name), "A mesh contains duplicate vertex attributes."); attributes.insert(name);
        switch (attribute.type) {
        case cgltf_attribute_type_position: positions = attribute.data; break;
        case cgltf_attribute_type_normal: normals = attribute.data; break;
        case cgltf_attribute_type_tangent: tangents = attribute.data; break;
        case cgltf_attribute_type_texcoord:
            require(attribute.index == 0, "Additional UV sets are not implemented; export UV0 for this import slice."); uv = attribute.data; break;
        case cgltf_attribute_type_color:
            require(attribute.index == 0, "Additional vertex color sets are not implemented."); colors = attribute.data; break;
        case cgltf_attribute_type_joints:
            require(attribute.index >= 0 && attribute.index < 4, "More than four joint attribute sets are not supported."); jointSets.insert(attribute.index, attribute.data); break;
        case cgltf_attribute_type_weights:
            require(attribute.index >= 0 && attribute.index < 4, "More than four weight attribute sets are not supported."); weightSets.insert(attribute.index, attribute.data); break;
        default: fail("Unsupported vertex attribute: " + name);
        }
    }
    require(positions && positions->component_type == cgltf_component_type_r_32f && !positions->normalized,
            "Vertex positions require float VEC3 data; quantized geometry is not implemented.");
    require(positions->count <= size_t(maxReferenceVertices), "A primitive exceeds one million vertices.");
    vertices += qint64(positions->count); require(vertices <= maxReferenceVertices, "The asset exceeds one million vertices.");
    ReferencePrimitive result; result.materialIndex = indexOf(source.material, data->materials);
    budget(qint64(positions->count), sizeof(ReferenceVertex)); result.vertices.resize(qsizetype(positions->count));
    const auto p = unpack(positions, cgltf_type_vec3, 3, "position");
    for (qsizetype i = 0; i < result.vertices.size(); ++i) result.vertices[i].position = {p[i * 3], p[i * 3 + 1], p[i * 3 + 2]};
    if (normals) {
        require(normals->component_type == cgltf_component_type_r_32f && !normals->normalized, "Normals require float VEC3 data.");
        const auto n = unpack(normals, cgltf_type_vec3, 3, "normal");
        for (qsizetype i = 0; i < result.vertices.size(); ++i) {
            auto normal = QVector3D(n[i * 3], n[i * 3 + 1], n[i * 3 + 2]);
            require(normal.lengthSquared() > 1e-12f, "A vertex normal has zero length."); result.vertices[i].normal = normal.normalized();
        }
    }
    if (uv) {
        require(uv->component_type == cgltf_component_type_r_32f || (uv->normalized &&
            (uv->component_type == cgltf_component_type_r_8u || uv->component_type == cgltf_component_type_r_16u)), "Invalid UV component format.");
        const auto values = unpack(uv, cgltf_type_vec2, 2, "UV");
        for (qsizetype i = 0; i < result.vertices.size(); ++i) result.vertices[i].uv = {values[i * 2], values[i * 2 + 1]};
    }
    if (tangents) {
        require(tangents->component_type == cgltf_component_type_r_32f && !tangents->normalized, "Tangents require float VEC4 data.");
        const auto values = unpack(tangents, cgltf_type_vec4, 4, "tangent");
        for (qsizetype i = 0; i < result.vertices.size(); ++i) {
            require(std::abs(std::abs(values[i * 4 + 3]) - 1) < 1e-4f, "Tangent handedness must be -1 or 1.");
            result.vertices[i].tangent = {values[i * 4], values[i * 4 + 1], values[i * 4 + 2], values[i * 4 + 3]};
        }
    }
    if (colors) {
        require(colors->type == cgltf_type_vec3 || colors->type == cgltf_type_vec4, "Vertex colors require VEC3/VEC4 data.");
        require(colors->component_type == cgltf_component_type_r_32f || (colors->normalized &&
            (colors->component_type == cgltf_component_type_r_8u || colors->component_type == cgltf_component_type_r_16u)), "Invalid vertex color component format.");
        const int components = colors->type == cgltf_type_vec3 ? 3 : 4;
        const auto values = unpack(colors, colors->type, components, "vertex color");
        for (qsizetype i = 0; i < result.vertices.size(); ++i) {
            for (int c = 0; c < components; ++c) require(values[i * components + c] >= 0 && values[i * components + c] <= 1, "Vertex colors must be between zero and one.");
            result.vertices[i].color = {values[i * components], values[i * components + 1], values[i * components + 2], components == 4 ? values[i * components + 3] : 1};
        }
    }
    require(jointSets.size() == weightSets.size() && (jointSets.isEmpty() || jointSets.contains(0)), "Skin joint/weight attribute sets must be paired and begin at zero.");
    QVector<QVector<float>> joints, weights;
    for (int set = 0; set < jointSets.size(); ++set) {
        require(jointSets.contains(set) && weightSets.contains(set), "Skin attribute sets must be contiguous and paired.");
        const auto *j = jointSets[set], *w = weightSets[set];
        require(!j->normalized && (j->component_type == cgltf_component_type_r_8u || j->component_type == cgltf_component_type_r_16u), "Joint indices require unsigned byte/short VEC4 data.");
        require((w->component_type == cgltf_component_type_r_32f && !w->normalized) ||
            (w->normalized && (w->component_type == cgltf_component_type_r_8u || w->component_type == cgltf_component_type_r_16u)), "Skin weights require float or normalized unsigned VEC4 data.");
        joints.append(unpack(j, cgltf_type_vec4, 4, "joint")); weights.append(unpack(w, cgltf_type_vec4, 4, "weight"));
    }
    for (qsizetype vertex = 0; vertex < result.vertices.size() && !joints.isEmpty(); ++vertex) {
        std::array<quint16, 16> jointIds{}; std::array<float, 16> influenceWeights{}; int used = 0;
        for (qsizetype set = 0; set < joints.size(); ++set) for (int slot = 0; slot < 4; ++slot) {
            const auto at = vertex * 4 + slot; const float weight = weights[set][at], joint = joints[set][at];
            require(weight >= 0 && weight <= 1 && joint >= 0 && joint < maxReferenceJoints && joint == std::floor(joint), "Invalid skin influence.");
            if (weight == 0) continue;
            int existing = 0; while (existing < used && jointIds[existing] != quint16(joint)) ++existing;
            if (existing == used) { jointIds[used] = quint16(joint); ++used; }
            influenceWeights[existing] += weight;
        }
        require(used > 0 && used <= 4, "This import slice supports at most four nonzero influences per vertex; no influences were truncated.");
        float sum = 0; for (int i = 0; i < used; ++i) sum += influenceWeights[i];
        require(std::abs(sum - 1.f) <= .02f, "Skin weights are not normalized.");
        for (int i = 0; i < used; ++i) { result.vertices[vertex].joints[i] = jointIds[i]; result.vertices[vertex].weights[i] = influenceWeights[i] / sum; }
    }
    if (source.indices) {
        const auto values = unpack(source.indices, cgltf_type_scalar, 1, "triangle index");
        require(!source.indices->normalized && values.size() % 3 == 0, "Triangle index count must be divisible by three.");
        result.indices.reserve(values.size());
        for (float value : values) { require(value >= 0 && value < result.vertices.size() && value == std::floor(value), "Triangle index is outside its vertex array."); result.indices.append(quint32(value)); }
    } else {
        require(result.vertices.size() % 3 == 0, "Unindexed triangle vertex count must be divisible by three.");
        result.indices.reserve(result.vertices.size());
        for (qsizetype i = 0; i < result.vertices.size(); ++i) result.indices.append(quint32(i));
    }
    triangles += result.indices.size() / 3; require(triangles <= maxReferenceTriangles, "The asset exceeds one million triangles.");
    if (!normals) {
        for (auto &vertex : result.vertices) vertex.normal = {};
        for (qsizetype i = 0; i < result.indices.size(); i += 3) {
            auto &a = result.vertices[result.indices[i]], &b = result.vertices[result.indices[i + 1]], &c = result.vertices[result.indices[i + 2]];
            const auto normal = QVector3D::crossProduct(b.position - a.position, c.position - a.position); a.normal += normal; b.normal += normal; c.normal += normal;
        }
        for (auto &vertex : result.vertices) vertex.normal = vertex.normal.lengthSquared() > 1e-12f ? vertex.normal.normalized() : QVector3D(0, 1, 0);
    }
    if (source.material && !uv) {
        const auto &m = *source.material;
        require(!m.pbr_metallic_roughness.base_color_texture.texture && !m.pbr_metallic_roughness.metallic_roughness_texture.texture
                && !m.normal_texture.texture && !m.occlusion_texture.texture && !m.emissive_texture.texture, "Textured primitive is missing UV0.");
    }
    for (cgltf_size target = 0; target < source.targets_count; ++target) {
        ReferenceMorphTarget morph;
        morph.name = target < mesh.target_names_count ? text(mesh.target_names[target]) : "Target " + QString::number(target + 1);
        QSet<int> semantics;
        for (cgltf_size attribute = 0; attribute < source.targets[target].attributes_count; ++attribute) {
            const auto &a = source.targets[target].attributes[attribute];
            require(!semantics.contains(int(a.type)), "Duplicate morph target attribute."); semantics.insert(int(a.type));
            require(a.data->component_type == cgltf_component_type_r_32f && !a.data->normalized, "Morph deltas require float VEC3 data.");
            const auto values = unpack(a.data, cgltf_type_vec3, 3, "morph target"); QVector<QVector3D> deltas;
            budget(qint64(a.data->count), sizeof(QVector3D)); deltas.reserve(qsizetype(a.data->count));
            for (qsizetype i = 0; i < qsizetype(a.data->count); ++i) deltas.append({values[i * 3], values[i * 3 + 1], values[i * 3 + 2]});
            if (a.type == cgltf_attribute_type_position) morph.positionDeltas = std::move(deltas);
            else if (a.type == cgltf_attribute_type_normal) morph.normalDeltas = std::move(deltas);
            else if (a.type == cgltf_attribute_type_tangent) morph.tangentDeltas = std::move(deltas);
            else fail("Unsupported morph target attribute.");
        }
        if (morph.positionDeltas.isEmpty()) { budget(result.vertices.size(), sizeof(QVector3D)); morph.positionDeltas.resize(result.vertices.size()); }
        result.morphTargets.append(std::move(morph));
    }
    return result;
}

ReferenceNode Reader::node(int index) {
    const auto &source = data->nodes[index]; ReferenceNode result; result.name = text(source.name);
    result.parentIndex = indexOf(source.parent, data->nodes); result.meshIndex = indexOf(source.mesh, data->meshes); result.skinIndex = indexOf(source.skin, data->skins);
    require(!source.has_mesh_gpu_instancing, "GPU-instanced node transforms are not implemented.");
    const auto json = objectAt(array(root, "nodes"), index, "node");
    require(!source.has_matrix || (!source.has_translation && !source.has_rotation && !source.has_scale), "A node cannot combine matrix and TRS transforms.");
    auto vector = [&](const float *p) { return QVector3D(finite(p[0], "node transform"), finite(p[1], "node transform"), finite(p[2], "node transform")); };
    if (source.has_matrix) {
        QMatrix4x4 matrix(source.matrix); matrix = matrix.transposed();
        for (int i = 0; i < 16; ++i) finite(source.matrix[i], "node matrix");
        require(std::abs(matrix(3, 0)) < 1e-6f && std::abs(matrix(3, 1)) < 1e-6f && std::abs(matrix(3, 2)) < 1e-6f && std::abs(matrix(3, 3) - 1) < 1e-6f,
                "Perspective node matrices are not supported.");
        auto x = matrix.column(0).toVector3D(), y = matrix.column(1).toVector3D(), z = matrix.column(2).toVector3D();
        result.scale = {x.length(), y.length(), z.length()};
        require(result.scale.x() > 1e-8f && result.scale.y() > 1e-8f && result.scale.z() > 1e-8f, "Singular node transforms are not supported.");
        if (QVector3D::dotProduct(QVector3D::crossProduct(x, y), z) < 0) result.scale.setX(-result.scale.x());
        x /= result.scale.x(); y /= result.scale.y(); z /= result.scale.z();
        require(std::abs(QVector3D::dotProduct(x, y)) < 1e-4f && std::abs(QVector3D::dotProduct(x, z)) < 1e-4f && std::abs(QVector3D::dotProduct(y, z)) < 1e-4f,
                "Sheared node matrices cannot be represented by this reference-3D schema.");
        QMatrix3x3 rotation;
        for (int row = 0; row < 3; ++row) { rotation(row, 0) = x[row]; rotation(row, 1) = y[row]; rotation(row, 2) = z[row]; }
        result.rotation = QQuaternion::fromRotationMatrix(rotation).normalized(); result.translation = matrix.column(3).toVector3D();
    } else {
        result.translation = vector(source.translation); result.scale = vector(source.scale);
        result.rotation = QQuaternion(finite(source.rotation[3], "node quaternion"), finite(source.rotation[0], "node quaternion"), finite(source.rotation[1], "node quaternion"), finite(source.rotation[2], "node quaternion"));
        require(std::abs(result.rotation.lengthSquared() - 1) <= .01f, "Node rotation quaternion is not normalized."); result.rotation.normalize();
        require(std::abs(result.scale.x()) > 1e-8f && std::abs(result.scale.y()) > 1e-8f && std::abs(result.scale.z()) > 1e-8f, "Singular node scales are not supported.");
    }
    for (cgltf_size i = 0; i < source.weights_count; ++i) result.morphWeights.append(finite(source.weights[i], "node morph weight", 100));
    if (source.camera || source.light) warn("Imported camera/light definitions are retained in metadata; the reference workspace uses its own camera and lighting.");
    if (!json["extensions"].toObject().isEmpty()) warn("Node extension data is retained in metadata; unsupported constraints and secondary motion remain frozen at the imported pose.");
    return result;
}

void Reader::avatar(ReferenceAsset &asset) {
    const auto extensions = root["extensions"].toObject();
    require(!(extensions.contains("VRM") && extensions.contains("VRMC_vrm")), "A model cannot declare both VRM0 and VRM1 avatar definitions.");
    const bool vrm1 = extensions.contains("VRMC_vrm"), vrm0 = extensions.contains("VRM");
    if (!vrm0 && !vrm1) { require(QFileInfo(path).suffix().compare("vrm", Qt::CaseInsensitive) != 0, "VRM file has no VRM avatar extension."); return; }
    const auto avatar = extensions[vrm1 ? "VRMC_vrm" : "VRM"].toObject();
    require(!avatar.isEmpty(), "Malformed VRM avatar extension.");
    const auto version = avatar[vrm1 ? "specVersion" : "specVersion"].toString();
    require(vrm1 ? version == "1.0" : version.isEmpty() || version == "0.0", "Unsupported VRM specification version: " + version);
    QSet<int> mappedNodes;
    auto addBone = [&](QString name, const QJsonObject &mapping) {
        // VRM0 names the thumb's first two joints Proximal/Intermediate;
        // VRM1 and Toon Pro use Metacarpal/Proximal for those same joints.
        if (vrm0) for (const auto &side : {QString("left"), QString("right")}) {
            if (name == side + "ThumbProximal") name = side + "ThumbMetacarpal";
            else if (name == side + "ThumbIntermediate") name = side + "ThumbProximal";
        }
        require(!name.isEmpty() && !asset.humanoidBones.contains(name), "Invalid or duplicate VRM humanoid bone mapping.");
        const int nodeIndex = number(mapping, "node", 0, asset.nodes.size() - 1);
        require(nodeIndex >= 0 && !mappedNodes.contains(nodeIndex), "VRM humanoid bones require unique node mappings.");
        mappedNodes.insert(nodeIndex); asset.humanoidBones.insert(name, nodeIndex);
    };
    const auto humanoid = avatar["humanoid"].toObject();
    if (vrm1) {
        require(humanoid["humanBones"].isObject(), "VRM1 humanoid mapping is missing.");
        const auto mappings = humanoid["humanBones"].toObject();
        for (auto it = mappings.constBegin(); it != mappings.constEnd(); ++it) { require(it.value().isObject(), "Malformed VRM humanoid mapping."); addBone(it.key(), it.value().toObject()); }
    } else {
        for (const auto &value : array(humanoid, "humanBones")) { require(value.isObject(), "Malformed VRM humanoid mapping."); addBone(value.toObject()["bone"].toString(), value.toObject()); }
    }
    require(!asset.humanoidBones.isEmpty(), "The VRM avatar has no humanoid bone mappings.");
    const QStringList requiredBones{"hips", "spine", "head", "leftUpperLeg", "leftLowerLeg", "leftFoot",
        "rightUpperLeg", "rightLowerLeg", "rightFoot", "leftUpperArm", "leftLowerArm", "leftHand", "rightUpperArm", "rightLowerArm", "rightHand"};
    for (const auto &bone : requiredBones) if (!asset.humanoidBones.contains(bone)) {
        warn("The imported humanoid mapping is partial; full VRM humanoid conformance has not been established."); break;
    }
    auto addExpression = [&](const QString &name, const QJsonObject &expression, bool legacy) {
        ReferenceExpression result; result.name = name; result.binary = expression["isBinary"].toBool();
        require(!name.isEmpty(), "VRM expression has no name.");
        for (const auto &existing : asset.expressions) require(existing.name != name, "Duplicate VRM expression name.");
        const auto binds = array(expression, legacy ? "binds" : "morphTargetBinds");
        for (const auto &value : binds) {
            require(value.isObject(), "Malformed VRM expression binding."); const auto binding = value.toObject();
            const int morph = number(binding, "index", 0, maxReferenceMorphTargets - 1);
            require(morph >= 0 && binding["weight"].isDouble(), "VRM expression binding lacks a target or weight.");
            const float weight = finite(binding["weight"].toDouble(), "expression weight", legacy ? 100 : 1) / (legacy ? 100.f : 1.f);
            require(weight >= 0, "VRM expression weight cannot be negative.");
            QVector<int> nodes;
            if (legacy) {
                const int mesh = number(binding, "mesh", 0, asset.meshes.size() - 1); require(mesh >= 0, "VRM0 expression has no mesh.");
                for (int i = 0; i < asset.nodes.size(); ++i) if (asset.nodes[i].meshIndex == mesh) nodes.append(i);
                require(!nodes.isEmpty(), "VRM0 expression refers to an uninstantiated mesh.");
            } else { const int node = number(binding, "node", 0, asset.nodes.size() - 1); require(node >= 0, "VRM1 expression has no node."); nodes.append(node); }
            for (const int node : nodes) {
                const int mesh = asset.nodes[node].meshIndex;
                require(mesh >= 0 && !asset.meshes[mesh].primitives.isEmpty() && morph < asset.meshes[mesh].primitives.first().morphTargets.size(), "VRM expression morph target is out of range.");
                result.bindings.append({node, morph, weight});
            }
        }
        if (!array(expression, legacy ? "materialValues" : "materialColorBinds").isEmpty()
            || (!legacy && !array(expression, "textureTransformBinds").isEmpty()))
            warn("VRM material/texture expression bindings are retained in metadata; this import slice evaluates morph expression bindings only.");
        if (!legacy && (expression["overrideBlink"].toString("none") != "none"
            || expression["overrideLookAt"].toString("none") != "none" || expression["overrideMouth"].toString("none") != "none"))
            warn("VRM expression override rules are retained in metadata but are not evaluated yet.");
        if (!result.bindings.isEmpty()) asset.expressions.append(std::move(result));
        else warn("Expression has no supported morph bindings and remains metadata-only: " + name);
    };
    if (vrm1) {
        const auto expressions = avatar["expressions"].toObject();
        for (const auto &kind : {QString("preset"), QString("custom")}) {
            const auto group = expressions[kind].toObject();
            for (auto it = group.constBegin(); it != group.constEnd(); ++it) { require(it.value().isObject(), "Malformed VRM expression."); addExpression(it.key(), it.value().toObject(), false); }
        }
    } else {
        for (const auto &value : array(avatar["blendShapeMaster"].toObject(), "blendShapeGroups")) {
            require(value.isObject(), "Malformed VRM0 expression."); const auto expression = value.toObject();
            auto name = expression["presetName"].toString(); if (name.isEmpty() || name == "unknown") name = expression["name"].toString();
            addExpression(name, expression, true);
        }
        if (!array(avatar, "materialProperties").isEmpty()) warn("VRM0 shader/material properties are retained in metadata; MToon rendering is not implemented by this import slice.");
    }
    warn("VRM avatar metadata and humanoid/morph mappings are imported; gaze, spring bones, node constraints and full MToon evaluation remain unimplemented.");
    asset.metadata["avatarVersion"] = vrm1 ? "VRM 1.0" : "VRM 0.x";
    asset.metadata["avatarMeta"] = avatar["meta"].toObject();
}

ReferenceImportResult Reader::read() {
    container(); validateJson(); checkCancelled(); AllocationBudget allocationBudget;
    cgltf_options parser{}; parser.memory = {allocate, release, &allocationBudget};
    cgltf_data *parsed = nullptr; const auto status = cgltf_parse(&parser, fileBytes.constData(), size_t(fileBytes.size()), &parsed);
    std::unique_ptr<cgltf_data, decltype(&cgltf_free)> owner(parsed, &cgltf_free);
    require(status == cgltf_result_success && parsed, "glTF parser rejected the model (code " + QString::number(int(status)) + ")."); data = parsed;
    loadBuffers(); validateAccessors(); loadImages(); ReferenceAsset asset; asset.name = QFileInfo(path).completeBaseName().left(256);
    // Preserve extension/extras blocks with their original source indices.
    // Geometry is canonical data and images have a native encoded pool; verbose
    // accessors, buffer offsets and resource/data URIs do not belong in metadata.
    QJsonObject sourceMetadata{{"asset", root["asset"]}, {"extensions", root["extensions"]},
        {"extensionsUsed", root["extensionsUsed"]}, {"extensionsRequired", root["extensionsRequired"]}, {"extras", root["extras"]}};
    for (const auto &kind : {QString("nodes"), QString("meshes"), QString("skins"), QString("materials"), QString("images"), QString("scenes")}) {
        const auto values = root[kind].toArray(); QJsonArray preserved;
        for (int i = 0; i < values.size(); ++i) {
            const auto value = values[i].toObject(); QJsonObject record{{"index", i}};
            for (const auto &key : {QString("extensions"), QString("extras")}) if (value.contains(key)) record[key] = value[key];
            if (kind == "meshes") {
                const auto primitives = value["primitives"].toArray(); QJsonArray parts;
                for (int p = 0; p < primitives.size(); ++p) {
                    const auto primitive = primitives[p].toObject(); QJsonObject part{{"index", p}};
                    for (const auto &key : {QString("extensions"), QString("extras")}) if (primitive.contains(key)) part[key] = primitive[key];
                    if (part.size() > 1) parts.append(part);
                }
                if (!parts.isEmpty()) record["primitives"] = parts;
            }
            if (record.size() > 1) preserved.append(record);
        }
        if (!preserved.isEmpty()) sourceMetadata[kind] = preserved;
    }
    sourceMetadata["textures"] = root["textures"].toArray(); sourceMetadata["samplers"] = root["samplers"].toArray();
    asset.metadata = {{"sourceFormat", QFileInfo(path).suffix().toLower()}, {"sourceGltf", sourceMetadata},
        {"sourceIndicesPreserved", true}, {"sourceResourceUrisOmitted", true}, {"importer", "cgltf 1.15 / Toon Pro reference import v1"}};
    asset.sourceImages = std::move(encodedImages);
    for (cgltf_size i = 0; i < data->materials_count; ++i) asset.materials.append(material(int(i)));
    for (cgltf_size i = 0; i < data->meshes_count; ++i) {
        const auto &source = data->meshes[i]; require(source.primitives_count > 0 && source.primitives_count <= 256, "Invalid mesh primitive count.");
        ReferenceMesh mesh; mesh.name = text(source.name);
        for (cgltf_size j = 0; j < source.primitives_count; ++j) mesh.primitives.append(primitive(source.primitives[j], source));
        const int count = mesh.primitives.first().morphTargets.size(); mesh.defaultMorphWeights.resize(count);
        require(!source.weights_count || source.weights_count == size_t(count), "Mesh default morph weight count is inconsistent.");
        for (cgltf_size j = 0; j < source.weights_count; ++j) mesh.defaultMorphWeights[int(j)] = finite(source.weights[j], "default morph weight", 100);
        asset.meshes.append(std::move(mesh));
    }
    for (cgltf_size i = 0; i < data->nodes_count; ++i) asset.nodes.append(node(int(i)));
    // Preserve every node and its source index. Rendering inactive mesh nodes
    // would change the selected glTF scene, so refuse that unsupported layout.
    if (data->scenes_count) {
        const auto *scene = data->scene ? data->scene : &data->scenes[0]; QSet<int> reachable;
        std::function<void(const cgltf_node *, int)> visit = [&](const cgltf_node *node, int depth) {
            require(depth < 128, "Scene hierarchy exceeds 128 levels."); const int index = indexOf(node, data->nodes);
            require(!reachable.contains(index), "Scene hierarchy contains duplicate roots or shared/cyclic children."); reachable.insert(index);
            for (cgltf_size i = 0; i < node->children_count; ++i) visit(node->children[i], depth + 1);
        };
        for (cgltf_size i = 0; i < scene->nodes_count; ++i) { require(!scene->nodes[i]->parent, "Scene roots cannot have parents."); visit(scene->nodes[i], 0); }
        for (int i = 0; i < asset.nodes.size(); ++i) require(asset.nodes[i].meshIndex < 0 || reachable.contains(i), "Inactive mesh nodes outside the selected scene are not implemented; export the selected scene only.");
    }
    for (cgltf_size i = 0; i < data->skins_count; ++i) {
        const auto &source = data->skins[i]; ReferenceSkin skin; skin.name = text(source.name); skin.skeletonRoot = indexOf(source.skeleton, data->nodes);
        require(source.joints_count > 0 && source.joints_count <= maxReferenceJoints, "Skin joint count must be between one and 256.");
        QSet<int> unique;
        for (cgltf_size j = 0; j < source.joints_count; ++j) { const int joint = indexOf(source.joints[j], data->nodes); require(joint >= 0 && !unique.contains(joint), "Skin has missing or duplicate joints."); unique.insert(joint); skin.joints.append(joint); }
        if (source.inverse_bind_matrices) {
            require(source.inverse_bind_matrices->count == source.joints_count && source.inverse_bind_matrices->component_type == cgltf_component_type_r_32f, "Inverse-bind matrix count or component format is invalid.");
            const auto matrices = unpack(source.inverse_bind_matrices, cgltf_type_mat4, 16, "inverse bind matrix");
            for (cgltf_size j = 0; j < source.joints_count; ++j) { QMatrix4x4 matrix(matrices.constData() + j * 16); skin.inverseBindMatrices.append(matrix.transposed()); }
        } else skin.inverseBindMatrices.resize(int(source.joints_count));
        asset.skins.append(std::move(skin));
    }
    avatar(asset); checkCancelled(); asset.metadata["importWarnings"] = QJsonArray::fromStringList(warnings);
    QString error; auto finalized = finalizeReferenceAsset(std::move(asset), &error);
    require(bool(finalized), error.isEmpty() ? "Imported reference asset failed validation." : error);
    return {std::move(finalized), warnings, {}};
}
} // namespace

ReferenceImportResult importReferenceAsset(const QString &path, const ReferenceImportOptions &options) {
    try { return Reader(path, options).read(); }
    catch (const QString &message) { return {{}, {}, message}; }
    catch (const std::bad_alloc &) { return {{}, {}, "Not enough memory for this 3D asset. Try a smaller model or texture."}; }
}
} // namespace toon
