#include "reference3d.h"
#include "scene3d.h"
#include <QBuffer>
#include <QCryptographicHash>
#include <QFile>
#include <QImageWriter>
#include <QTemporaryDir>
#include <QtEndian>
#include <QtTest>
#include <algorithm>
#include <limits>

using namespace toon;
namespace {
ReferencePrimitive triangle() {
    ReferencePrimitive primitive;
    for (auto point : {QVector3D(0, 0, 0), QVector3D(1, 0, 0), QVector3D(0, 1, 0)}) {
        ReferenceVertex vertex; vertex.position = point; vertex.normal = {0, 0, 1}; primitive.vertices.append(vertex);
    }
    primitive.indices = {0, 1, 2}; return primitive;
}
ReferenceAsset staticAsset() {
    ReferenceAsset asset; asset.name = "Source triangle";
    ReferenceMesh mesh; mesh.name = "Triangle"; mesh.primitives = {triangle()}; asset.meshes = {mesh};
    ReferenceNode node; node.name = "Mesh"; node.meshIndex = 0; asset.nodes = {node}; return asset;
}
ReferenceAsset skinnedAsset() {
    auto asset = staticAsset(); asset.name = "Imported bind transforms";
    asset.nodes[0].translation = {10, 0, 0}; asset.nodes[0].skinIndex = 0;
    ReferenceNode parent; parent.name = "Upper arm"; parent.translation = {2, 0, 0};
    ReferenceNode child; child.name = "Forearm"; child.parentIndex = 1; child.translation = {0, 1, 0};
    asset.nodes.append(parent); asset.nodes.append(child);
    ReferenceSkin skin; skin.name = "Authored skin"; skin.joints = {1, 2}; skin.skeletonRoot = 1;
    QMatrix4x4 parentInverse, childInverse; parentInverse.translate(8, 0, 0); childInverse.translate(8, -1, 0);
    skin.inverseBindMatrices = {parentInverse, childInverse}; asset.skins = {skin};
    auto &vertices = asset.meshes[0].primitives[0].vertices;
    vertices[0].weights[0] = 1;
    vertices[1].joints[0] = 1; vertices[1].weights[0] = 1;
    vertices[2].joints = {0, 1, 0, 0}; vertices[2].weights = {.25f, .75f, 0, 0};
    ReferenceMorphTarget target; target.name = "Longer fingers"; target.positionDeltas = {{0, 0, 0}, {1, 0, 0}, {0, 0, 0}};
    asset.meshes[0].primitives[0].morphTargets = {target}; asset.meshes[0].defaultMorphWeights = {0};
    ReferenceExpression expression; expression.name = "Flex"; expression.bindings = {{0, 0, 1}}; asset.expressions = {expression};
    asset.humanoidBones.insert("leftUpperArm", 1); asset.humanoidBones.insert("leftLowerArm", 2); return asset;
}
ReferenceInstance instance(ReferenceAsset asset) {
    ReferenceInstance result; result.id = "instance-a"; result.name = "Placed reference";
    result.asset = finalizeReferenceAsset(std::move(asset)); return result;
}
bool near(QVector3D actual, QVector3D expected, float tolerance = .0001f) { return (actual - expected).length() < tolerance; }
QVector3D transformedNormal(const QMatrix3x3 &m, QVector3D normal) {
    return QVector3D(m(0, 0) * normal.x() + m(0, 1) * normal.y() + m(0, 2) * normal.z(),
        m(1, 0) * normal.x() + m(1, 1) * normal.y() + m(1, 2) * normal.z(),
        m(2, 0) * normal.x() + m(2, 1) * normal.y() + m(2, 2) * normal.z()).normalized();
}
QByteArray rechecksum(QByteArray bytes) {
    bytes.replace(8, 32, QCryptographicHash::hash(bytes.mid(40), QCryptographicHash::Sha256)); return bytes;
}
QByteArray readFile(const QString &path) { QFile file(path); if (!file.open(QIODevice::ReadOnly)) return {}; return file.readAll(); }
}

class Reference3DTests final : public QObject {
    Q_OBJECT
private slots:
    void localHierarchyAndPlacementUseImportedRotations() {
        auto asset = staticAsset(); ReferenceNode parent; parent.name = "Imported oriented root";
        parent.translation = {2, 0, 0}; parent.rotation = QQuaternion::fromAxisAndAngle(0, 0, 1, 90); parent.scale = {2, 1, 1};
        asset.nodes[0].parentIndex = 1; asset.nodes[0].translation = {1, 0, 0}; asset.nodes.append(parent);
        auto placed = instance(asset); QVERIFY(placed.asset); placed.translation = {5, 0, 0};
        QString error; auto evaluated = evaluateReferenceInstance(placed, &error); QVERIFY2(evaluated, qPrintable(error));
        QCOMPARE(evaluated->nodeGlobals.size(), 2); QCOMPARE(evaluated->primitives.size(), 1);
        const auto &vertices = evaluated->primitives[0].vertices;
        QVERIFY(near(vertices[0].position, {7, 2, 0})); QVERIFY(near(vertices[1].position, {7, 4, 0})); QVERIFY(near(vertices[2].position, {6, 2, 0}));
        QVERIFY(near(evaluated->nodeGlobals[0].map(QVector3D()), {7, 2, 0}));
        QVERIFY(evaluated->bounds.valid); QVERIFY(near(evaluated->bounds.minimum, {6, 2, 0})); QVERIFY(near(evaluated->bounds.maximum, {7, 4, 0}));
        placed.poseRotations.insert(1, QQuaternion::fromAxisAndAngle(0, 0, 1, 180));
        evaluated = evaluateReferenceInstance(placed, &error); QVERIFY2(evaluated, qPrintable(error));
        QVERIFY(near(evaluated->primitives[0].vertices[0].position, {5, 0, 0}));
        QCOMPARE(placed.asset->nodes[1].rotation, canonicalRotationForTest(asset.nodes[1].rotation));
    }
    void skeletonOnlyEvaluationMatchesWorldPoseAndRejectsInvalidState() {
        auto placed = instance(skinnedAsset()); QVERIFY(placed.asset);
        placed.translation = {5, 3, -2}; placed.rotation = QQuaternion::fromAxisAndAngle(0, 0, 1, 90); placed.scale = {2, .5f, -1};
        placed.poseRotations.insert(2, QQuaternion::fromAxisAndAngle(0, 0, 1, 90)); placed.poseTranslations.insert(2, {0, 2, 0});
        placed.morphOverrides.insert(0, {.8f}); placed.expressionWeights.insert("Flex", .4f);
        QString error; const auto globals = evaluateReferenceNodeGlobals(placed, &error); QVERIFY2(globals, qPrintable(error));
        QCOMPARE(globals->size(), 3); QVERIFY(near((*globals)[2].map(QVector3D()), {4, 7, -2}));
        const auto geometry = evaluateReferenceInstance(placed, &error); QVERIFY2(geometry, qPrintable(error));
        QCOMPARE(*globals, geometry->nodeGlobals);
        QVERIFY(near(referencePlacementMatrix(placed).map(QVector3D(2, 2, 0)), {4, 7, -2}));
        auto invalid = placed; invalid.scale.setX(0); QVERIFY(!evaluateReferenceNodeGlobals(invalid, &error));
        invalid = placed; invalid.poseRotations.insert(200, QQuaternion()); QVERIFY(!evaluateReferenceNodeGlobals(invalid, &error));
        invalid = placed; invalid.morphOverrides.insert(0, {}); QVERIFY(!evaluateReferenceNodeGlobals(invalid, &error));
    }
    void immutableCertificationCannotEscapeIntoEditedCopiesOrStaleLifetimes() {
        auto original = finalizeReferenceAsset(skinnedAsset()); QVERIFY(original);
        auto mutableCopy = std::make_shared<ReferenceAsset>(*original);
        ReferenceInstance placed; placed.id = "untrusted-copy"; placed.asset = mutableCopy;
        QString error; QVERIFY2(evaluateReferenceNodeGlobals(placed, &error), qPrintable(error));
        // Validation of a public mutable source must not certify it. Its owner
        // certificate still belongs to the original finalized object.
        mutableCopy->nodes[1].parentIndex = 2;
        QVERIFY(!evaluateReferenceNodeGlobals(placed, &error)); QVERIFY(error.contains("cycle"));
        mutableCopy->nodes[1].parentIndex = -1; QVERIFY(evaluateReferenceNodeGlobals(placed, &error));
        original.reset();
        mutableCopy->meshes[0].primitives[0].vertices[0].weights[0] = .4f;
        QVERIFY(!evaluateReferenceNodeGlobals(placed, &error)); QVERIFY(error.contains("weights"));
        QVERIFY(!evaluateReferenceInstance(placed, &error));
        mutableCopy->meshes[0].primitives[0].vertices[0].weights[0] = 1;
        mutableCopy->sourceImages = {QByteArray("bad image")};
        QVERIFY(!evaluateReferenceNodeGlobals(placed, &error)); QVERIFY(error.contains("source image"));
    }
    void cachedModelPosePlacementPreservesGeneralBindNormalsAndReflectedWinding() {
        auto source = skinnedAsset();
        QMatrix4x4 bindBasis; bindBasis(0, 1) = .3f; bindBasis.scale(1.2f, .8f, 1.1f);
        for (auto &bind : source.skins[0].inverseBindMatrices) bind = bindBasis * bind;
        for (auto &vertex : source.meshes[0].primitives[0].vertices) vertex.normal = QVector3D(.2f, .5f, 1).normalized();
        auto local = instance(source); QVERIFY(local.asset); local.poseRotations.insert(2, QQuaternion::fromEulerAngles(23, 17, 47));
        local.morphOverrides.insert(0, {.6f}); local.expressionWeights.insert("Flex", .2f);
        QString error; const auto cached = evaluateReferenceInstance(local, &error); QVERIFY2(cached, qPrintable(error));
        for (auto scale : {QVector3D(1.7f, .8f, 2.1f), QVector3D(-1.7f, .8f, 2.1f)}) {
            auto placed = local; placed.translation = {2, 3, -1}; placed.rotation = QQuaternion::fromEulerAngles(18, 41, -33); placed.scale = scale;
            const auto world = evaluateReferenceInstance(placed, &error); QVERIFY2(world, qPrintable(error));
            const auto placement = referencePlacementMatrix(placed); const auto normalMatrix = placement.normalMatrix();
            auto indices = cached->primitives[0].indices;
            if (scale.x() < 0) for (int i = 0; i < indices.size(); i += 3) std::swap(indices[i + 1], indices[i + 2]);
            QCOMPARE(world->primitives[0].indices, indices);
            for (int i = 0; i < cached->primitives[0].vertices.size(); ++i) {
                const auto &modelVertex = cached->primitives[0].vertices[i], &worldVertex = world->primitives[0].vertices[i];
                QVERIFY(near(worldVertex.position, placement.map(modelVertex.position), .0003f));
                QVERIFY(near(worldVertex.normal, transformedNormal(normalMatrix, modelVertex.normal), .0003f));
            }
        }
        // A reflected source mesh and a reflected placement cancel; cached
        // identity-placement triangles must be flipped exactly once on placement.
        auto reflectedSource = staticAsset(); reflectedSource.nodes[0].scale = {-1, 1, 1};
        local = instance(reflectedSource); const auto reflectedLocal = evaluateReferenceInstance(local, &error); QVERIFY2(reflectedLocal, qPrintable(error));
        auto placed = local; placed.scale = {-1, 2, 1}; const auto reflectedWorld = evaluateReferenceInstance(placed, &error); QVERIFY2(reflectedWorld, qPrintable(error));
        QCOMPARE(reflectedLocal->primitives[0].indices, (QVector<quint32>{0, 2, 1}));
        QCOMPARE(reflectedWorld->primitives[0].indices, (QVector<quint32>{0, 1, 2}));
    }
    void inverseBindsCancelMeshLocalBindAndMorphPrecedesSkinning() {
        auto placed = instance(skinnedAsset()); QVERIFY(placed.asset); placed.translation = {5, 0, 0};
        QString error; auto evaluated = evaluateReferenceInstance(placed, &error); QVERIFY2(evaluated, qPrintable(error));
        QVERIFY(near(evaluated->primitives[0].vertices[0].position, {15, 0, 0}));
        QVERIFY(near(evaluated->primitives[0].vertices[1].position, {16, 0, 0}));
        placed.poseRotations.insert(2, QQuaternion::fromAxisAndAngle(0, 0, 1, 90));
        evaluated = evaluateReferenceInstance(placed, &error); QVERIFY2(evaluated, qPrintable(error));
        QVERIFY(near(evaluated->primitives[0].vertices[0].position, {15, 0, 0}));
        QVERIFY(near(evaluated->primitives[0].vertices[1].position, {8, 10, 0}));
        QVERIFY(near(evaluated->primitives[0].vertices[2].position, {9, 7, 0}));
        QVERIFY(near(evaluated->nodeGlobals[2].map(QVector3D()), {7, 1, 0}));
        placed.morphOverrides.insert(0, {1});
        evaluated = evaluateReferenceInstance(placed, &error); QVERIFY2(evaluated, qPrintable(error));
        QVERIFY(near(evaluated->primitives[0].vertices[1].position, {8, 11, 0}));
        placed.poseTranslations.insert(2, {0, 2, 0});
        evaluated = evaluateReferenceInstance(placed, &error); QVERIFY2(evaluated, qPrintable(error));
        QVERIFY(near(evaluated->primitives[0].vertices[1].position, {8, 12, 0}));
        QVERIFY(near(evaluated->nodeGlobals[2].map(QVector3D()), {7, 2, 0}));
        QCOMPARE(placed.asset->meshes[0].primitives[0].vertices[1].position, QVector3D(1, 0, 0));
    }
    void normalsUseInverseTransposeAndTangentsKeepReflectionHandedness() {
        auto asset = staticAsset();
        for (auto &vertex : asset.meshes[0].primitives[0].vertices) vertex.normal = QVector3D(1, 1, 0).normalized();
        auto placed = instance(asset); QVERIFY(placed.asset); placed.scale = {2, 1, 1}; placed.rotation = QQuaternion::fromAxisAndAngle(0, 0, 1, 90);
        QString error; auto evaluated = evaluateReferenceInstance(placed, &error); QVERIFY2(evaluated, qPrintable(error));
        QVERIFY(near(evaluated->primitives[0].vertices[0].normal, QVector3D(-1, .5f, 0).normalized()));
        placed = instance(staticAsset()); placed.scale = {-1, 2, 1};
        evaluated = evaluateReferenceInstance(placed, &error); QVERIFY2(evaluated, qPrintable(error));
        QCOMPARE(evaluated->primitives[0].vertices[0].tangent.w(), -1.f);
        const auto &vertex = evaluated->primitives[0].vertices[0];
        QVERIFY(std::abs(QVector3D::dotProduct(vertex.normal, vertex.tangent.toVector3D())) < .0001f);
    }
    void morphDefaultsNodeWeightsAndInstanceOverridesHaveDefinedPrecedence() {
        auto asset = skinnedAsset(); asset.meshes[0].defaultMorphWeights = {.5f};
        auto placed = instance(asset); QString error; auto evaluated = evaluateReferenceInstance(placed, &error); QVERIFY2(evaluated, qPrintable(error));
        QVERIFY(near(evaluated->primitives[0].vertices[1].position, {11.5f, 0, 0}));
        asset.nodes[0].morphWeights = {.25f}; placed = instance(asset);
        evaluated = evaluateReferenceInstance(placed, &error); QVERIFY2(evaluated, qPrintable(error));
        QVERIFY(near(evaluated->primitives[0].vertices[1].position, {11.25f, 0, 0}));
        placed.morphOverrides.insert(0, {.75f}); evaluated = evaluateReferenceInstance(placed, &error); QVERIFY2(evaluated, qPrintable(error));
        QVERIFY(near(evaluated->primitives[0].vertices[1].position, {11.75f, 0, 0}));
        placed.morphOverrides.insert(0, {-1.f}); evaluated = evaluateReferenceInstance(placed, &error); QVERIFY2(evaluated, qPrintable(error));
        QVERIFY(near(evaluated->primitives[0].vertices[1].position, {10, 0, 0}));
    }
    void expressionsAreAtomicAndPreserveUnrelatedMorphChannels() {
        auto asset = skinnedAsset(); asset.meshes[0].primitives[0].morphTargets.append(asset.meshes[0].primitives[0].morphTargets[0]);
        asset.meshes[0].defaultMorphWeights = {.2f, .3f};
        ReferenceExpression expression; expression.name = "Smile"; expression.bindings = {{0, 0, 1}};
        ReferenceExpression binary = expression; binary.name = "Blink"; binary.binary = true; asset.expressions = {expression, binary};
        auto placed = instance(asset); QVERIFY(placed.asset); placed.morphOverrides.insert(0, {.9f, .8f});
        QString error; QVERIFY2(setReferenceExpression(placed, "Smile", .5f, &error), qPrintable(error));
        QCOMPARE(placed.morphOverrides.value(0), (QVector<float>{.9f, .8f})); QCOMPARE(placed.expressionWeights.value("Smile"), .5f);
        QVERIFY(setReferenceExpression(placed, "Blink", .5f, &error)); QCOMPARE(placed.expressionWeights.value("Blink"), 0.f);
        QVERIFY(setReferenceExpression(placed, "Blink", .51f, &error)); QCOMPARE(placed.expressionWeights.value("Blink"), 1.f);
        auto evaluated = evaluateReferenceInstance(placed, &error); QVERIFY2(evaluated, qPrintable(error));
        // Base .9 + Smile .5 + Blink 1, and untouched second base channel .8.
        QVERIFY(near(evaluated->primitives[0].vertices[1].position, {14.2f, 0, 0}));
        const auto previous = placed.morphOverrides; const auto expressions = placed.expressionWeights;
        QVERIFY(!setReferenceExpression(placed, "Missing expression", 1, &error)); QVERIFY(!error.isEmpty()); QCOMPARE(placed.morphOverrides, previous);
        QCOMPARE(placed.expressionWeights, expressions);
        placed.locked = true; QVERIFY(!setReferenceExpression(placed, "Smile", 1, &error)); QCOMPARE(placed.morphOverrides, previous); QCOMPARE(placed.expressionWeights, expressions);
    }
    void canonicalIdsIncludeFullSourceAndIgnoreQuaternionSignAndImageAnnotations() {
        auto asset = skinnedAsset(); ReferenceMaterial material; material.name = "Authored PBR"; material.baseColor = QColor("#a0c4ff"); material.metallic = .37f; material.roughness = .64f;
        QImage texture(2, 1, QImage::Format_RGBA8888); texture.setPixelColor(0, 0, Qt::red); texture.setPixelColor(1, 0, Qt::blue);
        material.baseColorTexture = texture; material.normalTexture = texture; material.emissiveTexture = texture;
        QByteArray png; QBuffer encoded(&png); QVERIFY(encoded.open(QIODevice::WriteOnly)); QVERIFY(texture.save(&encoded, "PNG")); asset.sourceImages = {png};
        asset.materials = {material}; asset.meshes[0].primitives[0].materialIndex = 0; asset.nodes[1].rotation = QQuaternion::fromAxisAndAngle(1, 2, 3, 37);
        asset.metadata = {{"format", "glTF 2.0"}, {"sourceExtensions", QJsonObject{{"unsupportedConstraint", true}}}};
        QString error; const auto first = finalizeReferenceAsset(asset, &error); QVERIFY2(first, qPrintable(error));
        asset.nodes[1].rotation = -asset.nodes[1].rotation; asset.materials[0].baseColorTexture.setText("Comment", "Temporary importer annotation"); asset.materials[0].baseColorTexture.setDevicePixelRatio(2);
        const auto same = finalizeReferenceAsset(asset, &error); QVERIFY2(same, qPrintable(error)); QCOMPARE(first->id, same->id);
        const auto bytes = serializeReferenceAsset(*first, &error); QVERIFY2(!bytes.isEmpty(), qPrintable(error));
        const auto loaded = deserializeReferenceAsset(bytes, &error); QVERIFY2(loaded, qPrintable(error));
        QCOMPARE(loaded->id, first->id); QCOMPARE(serializeReferenceAsset(*loaded, &error), bytes);
        QCOMPARE(loaded->materials[0].normalTexture.pixelColor(1, 0), QColor(Qt::blue)); QCOMPARE(loaded->metadata, first->metadata);
        QCOMPARE(loaded->sourceImages, asset.sourceImages);
        auto mutated = *first; mutated.meshes[0].primitives[0].vertices[0].position.setX(.1f);
        QVERIFY(serializeReferenceAsset(mutated, &error).isEmpty()); QVERIFY(error.contains("content ID"));
        const auto replacement = finalizeReferenceAsset(mutated, &error); QVERIFY2(replacement, qPrintable(error)); QVERIFY(replacement->id != first->id);
    }
    void repeatedSceneSavesKeepUnitQuaternionBytesStable() {
        const QQuaternion unit(0x1.37af92p-1f, -0x1.8a27acp-1f, -0x1.88b082p-3f, -0x1.173edcp-25f);
        for(const auto &rotation:{unit, -unit, unit * 3.5f}) {
            ReferenceScene scene;auto actor=instance(skinnedAsset());QVERIFY(actor.asset);
            actor.rotation=rotation;actor.poseRotations.insert(2,rotation);scene.instances={actor};QString error;
            const auto bytes=serializeReferenceScene(scene,&error);QVERIFY2(!bytes.isEmpty(),qPrintable(error));
            for(int save=0;save<5;++save){
                const auto loaded=deserializeReferenceScene(save?serializeReferenceScene(scene,&error):bytes,&error);QVERIFY2(loaded,qPrintable(error));
                const auto next=serializeReferenceScene(*loaded,&error);QCOMPARE(next,bytes);
                const auto saved=loaded->instances.first().poseRotations.value(2);
                QVERIFY(std::abs(saved.lengthSquared()-1)<.000001f);
                QVERIFY(std::abs(QQuaternion::dotProduct(saved,unit))>.999999f);
                scene=*loaded;
            }
        }
    }
    void sceneRoundTripDeduplicatesAssetsAndRetainsIndependentPoseState() {
        ReferenceScene scene; scene.name = "Two actors"; auto first = instance(skinnedAsset()); QVERIFY(first.asset);
        first.poseRotations.insert(2, QQuaternion::fromAxisAndAngle(0, 0, 1, 47)); first.morphOverrides.insert(0, {.8f}); first.translation = {1, 2, 3};
        first.expressionWeights.insert("Flex", .4f);
        auto second = first; second.id = "instance-b"; second.poseRotations.clear(); second.translation = {-4, 0, 0}; second.visible = false; second.locked = true;
        scene.instances = {first, second}; scene.camera.yaw = 61; scene.light.direction = {1, -3, 2}; scene.light.intensity = 2.5f; scene.textured = false;
        QString error; const auto bytes = serializeReferenceScene(scene, &error); QVERIFY2(!bytes.isEmpty(), qPrintable(error));
        const auto loaded = deserializeReferenceScene(bytes, &error); QVERIFY2(loaded, qPrintable(error));
        QCOMPARE(loaded->instances.size(), 2); QCOMPARE(loaded->instances[0].asset.get(), loaded->instances[1].asset.get());
        QCOMPARE(loaded->instances[0].poseRotations, first.poseRotations); QCOMPARE(loaded->instances[0].morphOverrides, first.morphOverrides);
        QCOMPARE(loaded->instances[0].expressionWeights, first.expressionWeights);
        QVERIFY(loaded->instances[1].poseRotations.isEmpty()); QVERIFY(!loaded->instances[1].visible); QVERIFY(loaded->instances[1].locked);
        QCOMPARE(loaded->camera.yaw, 61.f); QCOMPARE(loaded->light.intensity, 2.5f); QVERIFY(!loaded->textured);
        QCOMPARE(serializeReferenceScene(*loaded, &error), bytes);
        QTemporaryDir directory; const auto path = directory.filePath("posed.toonref");
        QVERIFY2(saveReferenceScene(scene, path, &error), qPrintable(error)); QVERIFY(loadReferenceScene(path, &error)); const auto saved = readFile(path);
        auto invalid = scene; invalid.instances[1].id = invalid.instances[0].id;
        QVERIFY(!saveReferenceScene(invalid, path, &error)); QCOMPARE(readFile(path), saved);
        invalid = scene; auto conflicting = *first.asset; conflicting.name = "Changed without finalizing";
        invalid.instances[0].asset = std::make_shared<const ReferenceAsset>(std::move(conflicting));
        QVERIFY(!saveReferenceScene(invalid, path, &error)); QVERIFY(error.contains("content ID")); QCOMPARE(readFile(path), saved);
    }
    void logicalAssetIdentityAcceptsEquivalentPNGTransportEncodings() {
        auto source = staticAsset(); ReferenceMaterial material; QImage texture(32, 32, QImage::Format_RGBA8888);
        for (int y = 0; y < texture.height(); ++y) for (int x = 0; x < texture.width(); ++x)
            texture.setPixelColor(x, y, QColor((x * 17) % 256, (y * 31) % 256, (x + y) % 256));
        material.baseColorTexture = texture; source.materials = {material}; source.meshes[0].primitives[0].materialIndex = 0;
        QString error; const auto asset = finalizeReferenceAsset(source, &error); QVERIFY2(asset, qPrintable(error));
        const auto original = serializeReferenceAsset(*asset, &error); QVERIFY2(!original.isEmpty(), qPrintable(error));
        const QByteArray signature = QByteArray::fromHex("89504e470d0a1a0a"); const int imageOffset = original.indexOf(signature); QVERIFY(imageOffset > 44);
        const quint32 originalLength = qFromLittleEndian<quint32>(reinterpret_cast<const uchar *>(original.constData() + imageOffset - 4));
        auto annotated = texture; annotated.setText("Transport comment", "Alternate encoder representation");
        QByteArray alternate; QBuffer buffer(&alternate); QVERIFY(buffer.open(QIODevice::WriteOnly)); QImageWriter writer(&buffer, "PNG"); writer.setCompression(0); QVERIFY(writer.write(annotated));
        QVERIFY(alternate != original.mid(imageOffset, originalLength));
        auto repackaged = original; repackaged.replace(imageOffset, originalLength, alternate);
        qToLittleEndian<quint32>(quint32(alternate.size()), reinterpret_cast<uchar *>(repackaged.data() + imageOffset - 4)); repackaged = rechecksum(repackaged);
        const auto loaded = deserializeReferenceAsset(repackaged, &error); QVERIFY2(loaded, qPrintable(error));
        QCOMPARE(loaded->id, asset->id); QCOMPARE(loaded->materials[0].baseColorTexture.size(), texture.size());
        for (int y = 0; y < texture.height(); ++y) for (int x = 0; x < texture.width(); ++x)
            QCOMPARE(loaded->materials[0].baseColorTexture.pixelColor(x, y), texture.pixelColor(x, y));
        auto different = source; different.materials[0].baseColorTexture.setPixelColor(0, 0, Qt::red);
        const auto changed = finalizeReferenceAsset(different, &error); QVERIFY2(changed, qPrintable(error)); QVERIFY(changed->id != asset->id);
        QByteArray changedPng; QBuffer changedBuffer(&changedPng); QVERIFY(changedBuffer.open(QIODevice::WriteOnly)); QVERIFY(different.materials[0].baseColorTexture.save(&changedBuffer, "PNG"));
        auto substituted = original; substituted.replace(imageOffset, originalLength, changedPng);
        qToLittleEndian<quint32>(quint32(changedPng.size()), reinterpret_cast<uchar *>(substituted.data() + imageOffset - 4));
        QVERIFY(!deserializeReferenceAsset(rechecksum(substituted), &error)); QVERIFY(error.contains("logical content ID"));
    }
    void retainedSourceImagesUseSeparateHeaderAndActualEncodedMemoryBudgets() {
        auto source = staticAsset(); ReferenceMaterial material;
        for (int i = 0; i < 3; ++i) {
            QImage image(2048, 2048, QImage::Format_RGBA8888); image.fill(QColor::fromRgb(30 + i * 40, 50, 90));
            QByteArray png; QBuffer buffer(&png); QVERIFY(buffer.open(QIODevice::WriteOnly)); QVERIFY(image.save(&buffer, "PNG")); source.sourceImages.append(png);
            if (i == 0) material.baseColorTexture = image;
            else if (i == 1) material.normalTexture = image;
            else material.metallicRoughnessTexture = image;
        }
        source.materials = {material}; source.meshes[0].primitives[0].materialIndex = 0;
        QString error; const auto asset = finalizeReferenceAsset(source, &error); QVERIFY2(asset, qPrintable(error));
        const auto bytes = serializeReferenceAsset(*asset, &error); QVERIFY2(!bytes.isEmpty(), qPrintable(error));
        const auto loaded = deserializeReferenceAsset(bytes, &error); QVERIFY2(loaded, qPrintable(error));
        QCOMPARE(loaded->sourceImages, source.sourceImages); QCOMPARE(loaded->materials[0].normalTexture.size(), QSize(2048, 2048));
        auto oversized = source;
        oversized.sourceImages.append(source.sourceImages[0]); oversized.sourceImages.append(source.sourceImages[1]);
        QVERIFY(!finalizeReferenceAsset(oversized, &error)); QVERIFY(error.contains("texture budget"));
    }
    void malformedSourceIsRejectedBeforeEvaluationOrPublication() {
        QString error; auto source = skinnedAsset();
        auto invalid = source; invalid.nodes[1].parentIndex = 2; QVERIFY(!finalizeReferenceAsset(invalid, &error)); QVERIFY(error.contains("cycle"));
        invalid = source; invalid.nodes[0].scale.setX(0); QVERIFY(!finalizeReferenceAsset(invalid, &error));
        invalid = source; invalid.nodes[0].translation.setX(std::numeric_limits<float>::quiet_NaN()); QVERIFY(!finalizeReferenceAsset(invalid, &error));
        invalid = source; invalid.meshes[0].primitives[0].vertices[0].weights[0] = .5f; QVERIFY(!finalizeReferenceAsset(invalid, &error));
        invalid = source; invalid.meshes[0].primitives[0].vertices[0].joints[0] = 20; QVERIFY(!finalizeReferenceAsset(invalid, &error));
        invalid = source; invalid.meshes[0].primitives[0].indices[0] = 4; QVERIFY(!finalizeReferenceAsset(invalid, &error));
        invalid = source; invalid.skins[0].inverseBindMatrices[0].fill(0); QVERIFY(!finalizeReferenceAsset(invalid, &error));
        invalid = source; invalid.skins[0].skeletonRoot = 0; QVERIFY(!finalizeReferenceAsset(invalid, &error));
        invalid = source; invalid.meshes[0].primitives[0].morphTargets[0].positionDeltas.removeLast(); QVERIFY(!finalizeReferenceAsset(invalid, &error));
        invalid = source; invalid.humanoidBones.insert("head", 400); QVERIFY(!finalizeReferenceAsset(invalid, &error));
        invalid = source; invalid.metadata.insert("oversized", QString(65536, 'x')); QVERIFY(!finalizeReferenceAsset(invalid, &error));
        invalid = source; invalid.sourceImages = {QByteArray("not an image")}; QVERIFY(!finalizeReferenceAsset(invalid, &error));
        invalid = source; ReferenceMaterial extended; extended.baseColor = QColor::fromRgbF(2, 0, 0); invalid.materials = {extended}; QVERIFY(!finalizeReferenceAsset(invalid, &error));
        auto placed = instance(source); QVERIFY(placed.asset); placed.morphOverrides.insert(0, {}); QVERIFY(!evaluateReferenceInstance(placed, &error));
        placed = instance(source); placed.poseRotations.insert(200, QQuaternion()); QVERIFY(!evaluateReferenceInstance(placed, &error));
        placed = instance(source); auto badReferences = *placed.asset; badReferences.nodes[0].meshIndex = 999;
        placed.asset = std::make_shared<const ReferenceAsset>(std::move(badReferences)); placed.morphOverrides.insert(0, {0});
        ReferenceScene malformedScene; malformedScene.instances = {placed}; QVERIFY(!validateReferenceScene(malformedScene).isEmpty());
        auto large = staticAsset(); for (int i = 0; i < 5; ++i) { ReferenceNode node; node.parentIndex = large.nodes.size() - 1; node.scale = {1000000, 1000000, 1000000}; large.nodes.append(node); }
        placed = instance(large); QVERIFY(placed.asset); QVERIFY(!evaluateReferenceInstance(placed, &error)); QVERIFY(error.contains("world transform"));
    }
    void checkedPackagesRejectTamperingTrailingBytesUnknownVersionsAndHostileCounts() {
        QString error; const auto asset = finalizeReferenceAsset(skinnedAsset(), &error); QVERIFY2(asset, qPrintable(error)); const auto bytes = serializeReferenceAsset(*asset, &error);
        auto corrupt = bytes; corrupt[50] = char(corrupt[50] ^ 0x7f); QVERIFY(!deserializeReferenceAsset(corrupt, &error)); QVERIFY(error.contains("checksum"));
        corrupt = bytes; corrupt[7] = '2'; QVERIFY(!deserializeReferenceAsset(corrupt, &error)); QVERIFY(error.contains("version"));
        corrupt = bytes; corrupt.append('x'); QVERIFY(!deserializeReferenceAsset(rechecksum(corrupt), &error)); QVERIFY(error.contains("trailing"));
        corrupt = bytes; corrupt.chop(1); QVERIFY(!deserializeReferenceAsset(rechecksum(corrupt), &error));
        corrupt = bytes; const int nodeCountOffset = 40 + 4 + asset->id.toUtf8().size() + 4 + asset->name.toUtf8().size();
        qToLittleEndian<quint32>(0xffffffffu, reinterpret_cast<uchar *>(corrupt.data() + nodeCountOffset));
        QVERIFY(!deserializeReferenceAsset(rechecksum(corrupt), &error)); QVERIFY(error.contains("array"));
        ReferenceScene scene; scene.instances = {instance(skinnedAsset())}; const auto sceneBytes = serializeReferenceScene(scene, &error); QVERIFY2(!sceneBytes.isEmpty(), qPrintable(error));
        corrupt = sceneBytes; corrupt.append('x'); QVERIFY(!deserializeReferenceScene(rechecksum(corrupt), &error)); QVERIFY(error.contains("trailing"));
        QVERIFY(!deserializeReferenceScene(bytes, &error));
    }
    void renderBridgeUsesEvaluatedTrianglesWithoutSynthesizingLegacyRigs() {
        auto placed = instance(skinnedAsset()); placed.poseRotations.insert(2, QQuaternion::fromAxisAndAngle(0, 0, 1, 90));
        QString error; const auto model = referenceRenderModel(placed, &error); QVERIFY2(model, qPrintable(error));
        QCOMPARE(model->meshes.size(), 1); QVERIFY(near(model->meshes[0].positions[1], {3, 10, 0}));
        QVERIFY(model->meshes[0].transform.isIdentity()); QVERIFY(!model->id.isEmpty());
        auto reflected = instance(staticAsset()); reflected.scale = {-1, 1, 1};
        const auto mirroredModel = referenceRenderModel(reflected, &error); QVERIFY2(mirroredModel, qPrintable(error));
        QCOMPARE(mirroredModel->meshes[0].indices, (QVector<quint32>{0, 2, 1}));
        const auto &mirroredMesh = mirroredModel->meshes[0];
        const auto a = mirroredMesh.positions[mirroredMesh.indices[0]], b = mirroredMesh.positions[mirroredMesh.indices[1]], c = mirroredMesh.positions[mirroredMesh.indices[2]];
        QVERIFY(QVector3D::dotProduct(QVector3D::crossProduct(b - a, c - a), mirroredMesh.normals[0]) > 0);
        auto source = staticAsset(); ReferenceMaterial material; material.alphaMode = ReferenceAlphaMode::Blend; source.materials = {material}; source.meshes[0].primitives[0].materialIndex = 0;
        placed = instance(source); QVERIFY(placed.asset); QVERIFY(!referenceRenderModel(placed, &error)); QVERIFY(error.contains("transparency"));
        source = staticAsset(); source.meshes[0].primitives[0].vertices[0].color = {1, 0, 0, 1}; placed = instance(source);
        QVERIFY(placed.asset); QVERIFY(!referenceRenderModel(placed, &error)); QVERIFY(error.contains("vertex colors"));
        const auto evaluated = evaluateReferenceInstance(placed, &error); QVERIFY2(evaluated, qPrintable(error)); QCOMPARE(evaluated->primitives[0].vertices[0].color, QVector4D(1, 0, 0, 1));
    }
private:
    static QQuaternion canonicalRotationForTest(QQuaternion value) { value.normalize(); return value.scalar() < 0 ? -value : value; }
};
QTEST_MAIN(Reference3DTests)
#include "test_reference3d.moc"
