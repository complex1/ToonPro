#include "reference3d_import.h"
#include <QBuffer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QtEndian>
#include <QtTest>
#include <cstring>

using namespace toon;
namespace {
void u32(QByteArray &bytes, quint32 value) {
    const auto encoded = qToLittleEndian(value); bytes.append(reinterpret_cast<const char *>(&encoded), 4);
}
QByteArray floats(std::initializer_list<float> values) {
    QByteArray bytes;
    for (float value : values) { quint32 bits; std::memcpy(&bits, &value, 4); u32(bytes, bits); }
    return bytes;
}
QByteArray matrixBytes(const QMatrix4x4 &matrix) {
    QByteArray bytes;
    for (int i = 0; i < 16; ++i) { quint32 bits; const float value = matrix.constData()[i]; std::memcpy(&bits, &value, 4); u32(bytes, bits); }
    return bytes;
}
struct Fixture {
    QJsonObject json;
    QByteArray bin;
    int sparseAccessor = -1;
    QMatrix4x4 firstBind;
    int view(const QByteArray &data) {
        while (bin.size() % 4) bin.append(char(0)); const int offset = bin.size(); bin.append(data);
        auto views = json["bufferViews"].toArray(); const int index = views.size();
        views.append(QJsonObject{{"buffer", 0}, {"byteOffset", offset}, {"byteLength", data.size()}}); json["bufferViews"] = views; return index;
    }
    int accessor(const QByteArray &data, int component, const QString &type, int count, bool normalized = false) {
        const int bufferView = view(data); auto accessors = json["accessors"].toArray(); const int index = accessors.size();
        QJsonObject accessor{{"bufferView", bufferView}, {"componentType", component}, {"type", type}, {"count", count}};
        if (normalized) accessor["normalized"] = true;
        accessors.append(accessor); json["accessors"] = accessors; return index;
    }
    QByteArray glb() const {
        auto root = json; root["buffers"] = QJsonArray{QJsonObject{{"byteLength", bin.size()}}};
        auto jsonBytes = QJsonDocument(root).toJson(QJsonDocument::Compact); while (jsonBytes.size() % 4) jsonBytes.append(' ');
        auto binary = bin; while (binary.size() % 4) binary.append(char(0));
        QByteArray bytes; u32(bytes, 0x46546c67); u32(bytes, 2); u32(bytes, 12 + 8 + jsonBytes.size() + 8 + binary.size());
        u32(bytes, jsonBytes.size()); u32(bytes, 0x4e4f534a); bytes.append(jsonBytes); u32(bytes, binary.size()); u32(bytes, 0x004e4942); bytes.append(binary); return bytes;
    }
};
Fixture fixture(const QByteArray &sparseIndices = QByteArray(1, char(1))) {
    Fixture result; result.json["asset"] = QJsonObject{{"version", "2.0"}, {"generator", "handwritten Toon Pro import fixture"}};
    const int positions = result.accessor(floats({0,0,0, 1,0,0, 0,1,0}), 5126, "VEC3", 3);
    const int normals = result.accessor(floats({0,0,1, 0,0,1, 0,0,1}), 5126, "VEC3", 3);
    const int uv = result.accessor(floats({0,0, 1,0, 0,1}), 5126, "VEC2", 3);
    const int tangents = result.accessor(floats({1,0,0,1, 1,0,0,1, 1,0,0,1}), 5126, "VEC4", 3);
    const int colors = result.accessor(QByteArray::fromHex("ff0000ff00ff00ff0000ffff"), 5121, "VEC4", 3, true);
    const int joints = result.accessor(QByteArray::fromHex("000100000001000000010000"), 5121, "VEC4", 3);
    const int weights = result.accessor(floats({1,0,0,0, 0,1,0,0, .5f,.5f,0,0}), 5126, "VEC4", 3);
    const int indices = result.accessor(QByteArray::fromHex("000001000200"), 5123, "SCALAR", 3);
    const int sparseIndexView = result.view(sparseIndices); QByteArray deltas;
    for (int i = 0; i < sparseIndices.size(); ++i) deltas.append(floats({.25f, 0, 0}));
    const int sparseValueView = result.view(deltas);
    auto accessors = result.json["accessors"].toArray(); result.sparseAccessor = accessors.size();
    accessors.append(QJsonObject{{"componentType", 5126}, {"type", "VEC3"}, {"count", 3}, {"sparse", QJsonObject{
        {"count", sparseIndices.size()}, {"indices", QJsonObject{{"bufferView", sparseIndexView}, {"componentType", 5121}}},
        {"values", QJsonObject{{"bufferView", sparseValueView}}}}}}); result.json["accessors"] = accessors;
    const auto rotation = QQuaternion::fromAxisAndAngle(0, 0, 1, 45);
    QMatrix4x4 hip; hip.translate(.1f, .2f, 0); hip.rotate(rotation);
    QMatrix4x4 child; child.translate(0, 1, 0); child.rotate(QQuaternion::fromAxisAndAngle(1, 0, 0, 30));
    QMatrix4x4 mesh; mesh.translate(2, 0, 0);
    result.firstBind = hip.inverted() * mesh;
    const int inverseBind = result.accessor(matrixBytes(result.firstBind) + matrixBytes((hip * child).inverted() * mesh), 5126, "MAT4", 2);
    QImage image(2, 2, QImage::Format_RGBA8888); image.fill(QColor(20, 80, 140, 200));
    QByteArray png; QBuffer output(&png); output.open(QIODevice::WriteOnly); image.save(&output, "PNG"); const int imageView = result.view(png);
    result.json["images"] = QJsonArray{QJsonObject{{"bufferView", imageView}, {"mimeType", "image/png"}}};
    result.json["textures"] = QJsonArray{QJsonObject{{"source", 0}}};
    result.json["materials"] = QJsonArray{QJsonObject{{"name", "Five preserved texture channels"},
        {"pbrMetallicRoughness", QJsonObject{{"baseColorFactor", QJsonArray{.8,.7,.6,1}}, {"metallicFactor", .4}, {"roughnessFactor", .7},
            {"baseColorTexture", QJsonObject{{"index", 0}}}, {"metallicRoughnessTexture", QJsonObject{{"index", 0}}}}},
        {"normalTexture", QJsonObject{{"index", 0}, {"scale", .5}}}, {"occlusionTexture", QJsonObject{{"index", 0}, {"strength", .75}}},
        {"emissiveTexture", QJsonObject{{"index", 0}}}, {"emissiveFactor", QJsonArray{.1,.2,.3}}, {"doubleSided", true}, {"alphaMode", "MASK"}, {"alphaCutoff", .4}}};
    result.json["meshes"] = QJsonArray{QJsonObject{{"name", "Skinned triangle"}, {"weights", QJsonArray{.25}}, {"extras", QJsonObject{{"targetNames", QJsonArray{"Chin"}}}},
        {"primitives", QJsonArray{QJsonObject{{"attributes", QJsonObject{{"POSITION", positions}, {"NORMAL", normals}, {"TEXCOORD_0", uv}, {"TANGENT", tangents}, {"COLOR_0", colors}, {"JOINTS_0", joints}, {"WEIGHTS_0", weights}}},
            {"indices", indices}, {"material", 0}, {"targets", QJsonArray{QJsonObject{{"POSITION", result.sparseAccessor}}}}}}}}};
    const auto childRotation = QQuaternion::fromAxisAndAngle(1, 0, 0, 30);
    result.json["nodes"] = QJsonArray{
        QJsonObject{{"name", "Hips"}, {"translation", QJsonArray{.1,.2,0}}, {"rotation", QJsonArray{rotation.x(),rotation.y(),rotation.z(),rotation.scalar()}}, {"children", QJsonArray{1}}},
        QJsonObject{{"name", "Spine"}, {"translation", QJsonArray{0,1,0}}, {"rotation", QJsonArray{childRotation.x(),childRotation.y(),childRotation.z(),childRotation.scalar()}}},
        QJsonObject{{"name", "Character"}, {"translation", QJsonArray{2,0,0}}, {"mesh", 0}, {"skin", 0}}};
    result.json["skins"] = QJsonArray{QJsonObject{{"name", "Imported bind orientations"}, {"joints", QJsonArray{0,1}}, {"skeleton", 0}, {"inverseBindMatrices", inverseBind}}};
    result.json["scenes"] = QJsonArray{QJsonObject{{"nodes", QJsonArray{0,2}}}}; result.json["scene"] = 0;
    return result;
}
bool write(const QString &path, const QByteArray &bytes) { QFile file(path); return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size(); }
QString writeGlb(QTemporaryDir &directory, const Fixture &fixture, const QString &name = "character.glb") {
    const auto path = directory.filePath(name); return write(path, fixture.glb()) ? path : QString();
}
} // namespace

class Reference3DImportTests final : public QObject {
    Q_OBJECT
private slots:
    void preservesBindRotationsWeightsSparseMorphsAndTextures() {
        QTemporaryDir directory; const auto source = fixture(); const auto path = writeGlb(directory, source); QVERIFY(!path.isEmpty());
        const auto imported = importReferenceAsset(path); QVERIFY2(imported, qPrintable(imported.error));
        const auto &asset = *imported.asset; QCOMPARE(asset.nodes.size(), 3); QCOMPARE(asset.nodes[1].parentIndex, 0);
        QVERIFY(std::abs(QQuaternion::dotProduct(asset.nodes[0].rotation, QQuaternion::fromAxisAndAngle(0,0,1,45))) > .99999f);
        QCOMPARE(asset.nodes[2].translation, QVector3D(2,0,0)); QCOMPARE(asset.nodes[2].skinIndex, 0);
        QCOMPARE(asset.skins[0].joints, (QVector<int>{0,1})); QCOMPARE(asset.skins[0].inverseBindMatrices[0], source.firstBind);
        const auto &primitive = asset.meshes[0].primitives[0]; QCOMPARE(primitive.vertices.size(), 3); QCOMPARE(primitive.indices, (QVector<quint32>{0,1,2}));
        QCOMPARE(primitive.vertices[1].joints[0], quint16(1)); QCOMPARE(primitive.vertices[1].weights[0], 1.f);
        QCOMPARE(primitive.vertices[2].weights[0], .5f); QCOMPARE(primitive.vertices[2].weights[1], .5f);
        QCOMPARE(primitive.vertices[0].tangent, QVector4D(1,0,0,1)); QCOMPARE(primitive.vertices[1].color, QVector4D(0,1,0,1));
        QCOMPARE(primitive.morphTargets[0].name, QString("Chin")); QCOMPARE(primitive.morphTargets[0].positionDeltas[0], QVector3D());
        QCOMPARE(primitive.morphTargets[0].positionDeltas[1], QVector3D(.25f,0,0)); QCOMPARE(asset.meshes[0].defaultMorphWeights, QVector<float>{.25f});
        const auto &material = asset.materials[0]; QCOMPARE(material.baseColorTexture.size(), QSize(2,2)); QCOMPARE(material.baseColorTexture.pixelColor(0,0), QColor(20,80,140,200));
        QCOMPARE(asset.sourceImages.size(), 1); QVERIFY(asset.sourceImages[0].startsWith(QByteArray::fromHex("89504e47")));
        QCOMPARE(material.metallicRoughnessTexture, material.baseColorTexture); QCOMPARE(material.normalTexture, material.baseColorTexture);
        QCOMPARE(material.occlusionTexture, material.baseColorTexture); QCOMPARE(material.emissiveTexture, material.baseColorTexture);
        QCOMPARE(material.normalScale, .5f); QCOMPARE(material.occlusionStrength, .75f); QCOMPARE(material.alphaMode, ReferenceAlphaMode::Mask);
        ReferenceInstance instance; instance.id = "fixture"; instance.asset = imported.asset; QString error;
        const auto evaluated = evaluateReferenceInstance(instance, &error); QVERIFY2(evaluated, qPrintable(error));
        const auto actual = evaluated->primitives[0].vertices[1].position;
        QVERIFY2((actual - QVector3D(3.0625f,0,0)).length() < .0001f, qPrintable(QString("Unexpected bind/morph result: %1,%2,%3").arg(actual.x()).arg(actual.y()).arg(actual.z())));
    }
    void externalGltfResourcesAndEmbeddedDataUrisWork() {
        QTemporaryDir directory; auto source = fixture();
        source.json["buffers"] = QJsonArray{QJsonObject{{"byteLength", source.bin.size()}, {"uri", "mesh.bin"}}};
        QVERIFY(write(directory.filePath("mesh.bin"), source.bin)); const auto path = directory.filePath("character.gltf");
        QVERIFY(write(path, QJsonDocument(source.json).toJson())); const auto external = importReferenceAsset(path); QVERIFY2(external, qPrintable(external.error));
        source.json["buffers"] = QJsonArray{QJsonObject{{"byteLength", source.bin.size()}, {"uri", "data:application/octet-stream;base64," + QString::fromLatin1(source.bin.toBase64())}}};
        QVERIFY(write(path, QJsonDocument(source.json).toJson())); const auto embedded = importReferenceAsset(path); QVERIFY2(embedded, qPrintable(embedded.error));
        QCOMPARE(external.asset->meshes[0].primitives[0].vertices[2].position, embedded.asset->meshes[0].primitives[0].vertices[2].position);
    }
    void vrm0AndVrm1MapHumanoidsAndMorphExpressions() {
        for (const bool legacy : {true, false}) {
            QTemporaryDir directory; auto source = fixture(); QJsonObject avatar;
            if (legacy) {
                avatar = {{"specVersion", "0.0"}, {"meta", QJsonObject{{"title", "Artist character"}, {"licenseName", "CC0"}}},
                    {"humanoid", QJsonObject{{"humanBones", QJsonArray{QJsonObject{{"bone", "hips"}, {"node", 0}}, QJsonObject{{"bone", "spine"}, {"node", 1}}}}}},
                    {"blendShapeMaster", QJsonObject{{"blendShapeGroups", QJsonArray{QJsonObject{{"name", "Smile"}, {"presetName", "joy"}, {"isBinary", true},
                        {"binds", QJsonArray{QJsonObject{{"mesh", 0}, {"index", 0}, {"weight", 75}}}}}}}}}};
            } else {
                avatar = {{"specVersion", "1.0"}, {"meta", QJsonObject{{"name", "Artist character"}, {"allowRedistribution", false}}},
                    {"humanoid", QJsonObject{{"humanBones", QJsonObject{{"hips", QJsonObject{{"node", 0}}}, {"spine", QJsonObject{{"node", 1}}}}}}},
                    {"expressions", QJsonObject{{"preset", QJsonObject{{"happy", QJsonObject{{"isBinary", true}, {"morphTargetBinds", QJsonArray{QJsonObject{{"node", 2}, {"index", 0}, {"weight", .75}}}}}}}}}}};
            }
            const auto key = legacy ? "VRM" : "VRMC_vrm"; source.json["extensions"] = QJsonObject{{key, avatar}};
            source.json["extensionsUsed"] = QJsonArray{key}; source.json["extensionsRequired"] = QJsonArray{key};
            const auto imported = importReferenceAsset(writeGlb(directory, source, "character.vrm")); QVERIFY2(imported, qPrintable(imported.error));
            QCOMPARE(imported.asset->humanoidBones.value("hips", -1), 0); QCOMPARE(imported.asset->humanoidBones.value("spine", -1), 1);
            QCOMPARE(imported.asset->expressions.size(), 1); const auto &expression = imported.asset->expressions[0];
            QCOMPARE(expression.name, QString(legacy ? "joy" : "happy")); QVERIFY(expression.binary);
            QCOMPARE(expression.bindings.size(), 1); QCOMPARE(expression.bindings[0].nodeIndex, 2); QCOMPARE(expression.bindings[0].morphIndex, 0); QCOMPARE(expression.bindings[0].weight, .75f);
            QCOMPARE(imported.asset->metadata["avatarMeta"].toObject(), avatar["meta"].toObject()); QVERIFY(!imported.warnings.isEmpty());
        }
    }
    void rejectsNetworkTraversalAndDeclaredLengthMismatch() {
        QTemporaryDir directory; auto source = fixture(); const auto path = directory.filePath("character.gltf");
        for (const auto &uri : {QString("https://example.invalid/mesh.bin"), QString("../mesh.bin"), QString("%2e%2e/mesh.bin"), QString("file:///tmp/mesh.bin"), QString("C:/mesh.bin")}) {
            source.json["buffers"] = QJsonArray{QJsonObject{{"byteLength", source.bin.size()}, {"uri", uri}}};
            QVERIFY(write(path, QJsonDocument(source.json).toJson())); const auto imported = importReferenceAsset(path);
            QVERIFY(!imported); QVERIFY(!imported.error.isEmpty());
        }
        QVERIFY(write(directory.filePath("mesh.bin"), source.bin + QByteArray(4, char(0))));
        source.json["buffers"] = QJsonArray{QJsonObject{{"byteLength", source.bin.size()}, {"uri", "mesh.bin"}}};
        QVERIFY(write(path, QJsonDocument(source.json).toJson())); const auto imported = importReferenceAsset(path);
        QVERIFY(!imported); QVERIFY(imported.error.contains("length"));
    }
    void vrm0ThumbMappingsUseCanonicalJointSemantics() {
        QTemporaryDir directory; auto source = fixture(); auto nodes = source.json["nodes"].toArray();
        auto spine = nodes[1].toObject(); spine["children"] = QJsonArray{3}; nodes[1] = spine;
        nodes.append(QJsonObject{{"name", "Thumb first"}, {"children", QJsonArray{4}}});
        nodes.append(QJsonObject{{"name", "Thumb second"}, {"translation", QJsonArray{0,.1,0}}}); source.json["nodes"] = nodes;
        source.json["extensions"] = QJsonObject{{"VRM", QJsonObject{{"specVersion", "0.0"},
            {"humanoid", QJsonObject{{"humanBones", QJsonArray{QJsonObject{{"bone", "hips"}, {"node", 0}},
                QJsonObject{{"bone", "leftThumbProximal"}, {"node", 3}}, QJsonObject{{"bone", "leftThumbIntermediate"}, {"node", 4}}}}}}}}};
        const auto imported = importReferenceAsset(writeGlb(directory, source, "thumb.vrm")); QVERIFY2(imported, qPrintable(imported.error));
        QCOMPARE(imported.asset->humanoidBones.value("leftThumbMetacarpal", -1), 3);
        QCOMPARE(imported.asset->humanoidBones.value("leftThumbProximal", -1), 4);
        QVERIFY(!imported.asset->humanoidBones.contains("leftThumbIntermediate"));
    }
    void rejectsMalformedSparseDataAndAccessorBounds() {
        QTemporaryDir directory;
        for (const auto &indices : {QByteArray::fromHex("0101"), QByteArray::fromHex("03")}) {
            const auto imported = importReferenceAsset(writeGlb(directory, fixture(indices)));
            QVERIFY(!imported); QVERIFY2(imported.error.contains("Sparse", Qt::CaseInsensitive), qPrintable(imported.error));
        }
        auto source = fixture(); auto accessors = source.json["accessors"].toArray(); auto accessor = accessors[0].toObject(); accessor["count"] = 1000; accessors[0] = accessor; source.json["accessors"] = accessors;
        auto imported = importReferenceAsset(writeGlb(directory, source)); QVERIFY(!imported); QVERIFY(!imported.error.isEmpty());
        source = fixture(); accessors = source.json["accessors"].toArray(); accessor = accessors[0].toObject(); accessor.remove("componentType"); accessors[0] = accessor; source.json["accessors"] = accessors;
        imported = importReferenceAsset(writeGlb(directory, source)); QVERIFY(!imported); QVERIFY(imported.error.contains("component"));
        source = fixture(); auto bytes = source.glb(); bytes[8] = char(0);
        QVERIFY(write(directory.filePath("bad.glb"), bytes)); imported = importReferenceAsset(directory.filePath("bad.glb")); QVERIFY(!imported); QVERIFY(imported.error.contains("length"));
    }
    void preservesDecomposableMatrixAndRejectsShear() {
        QTemporaryDir directory; auto source = fixture(); auto nodes = source.json["nodes"].toArray(); auto node = nodes[0].toObject();
        QMatrix4x4 transform; transform.translate(.1f,.2f,0); transform.rotate(QQuaternion::fromAxisAndAngle(0,0,1,45));
        QJsonArray values; for (int i = 0; i < 16; ++i) values.append(transform.constData()[i]);
        node.remove("translation"); node.remove("rotation"); node["matrix"] = values; nodes[0] = node; source.json["nodes"] = nodes;
        auto imported = importReferenceAsset(writeGlb(directory, source)); QVERIFY2(imported, qPrintable(imported.error));
        QVERIFY((imported.asset->nodes[0].translation - QVector3D(.1f,.2f,0)).length() < 1e-6f);
        transform(0,1) += .25f; values = {}; for (int i = 0; i < 16; ++i) values.append(transform.constData()[i]); node["matrix"] = values; nodes[0] = node; source.json["nodes"] = nodes;
        imported = importReferenceAsset(writeGlb(directory, source)); QVERIFY(!imported); QVERIFY2(imported.error.contains("Shear", Qt::CaseInsensitive), qPrintable(imported.error));
    }
    void unsupportedFeaturesAndCancellationNeverPublishPartialAssets() {
        QTemporaryDir directory; auto source = fixture(); source.json["extensionsRequired"] = QJsonArray{"KHR_draco_mesh_compression"};
        auto imported = importReferenceAsset(writeGlb(directory, source)); QVERIFY(!imported); QVERIFY(imported.error.contains("extension"));
        source = fixture(); source.json["animations"] = QJsonArray{QJsonObject{}};
        imported = importReferenceAsset(writeGlb(directory, source)); QVERIFY(!imported); QVERIFY(imported.error.contains("animations"));
        auto cancellation = std::make_shared<std::atomic_bool>(true);
        imported = importReferenceAsset(writeGlb(directory, fixture()), {cancellation}); QVERIFY(!imported); QVERIFY(imported.error.contains("cancelled"));
    }
};
QTEST_MAIN(Reference3DImportTests)
#include "test_reference3d_import.moc"
