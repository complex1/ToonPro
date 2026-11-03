#include "skin3d.h"
#include "scene3d.h"
#include "scene3d_cache.h"
#include "scene_model_io.h"
#include <QCryptographicHash>
#include <QJsonArray>
#include <QTest>
#include <QtEndian>
#include <cmath>
#include <limits>
using namespace toon;
namespace {
Model3DPtr triangle(QVector<QVector3D> positions = {{0,0,0},{5,0,0},{10,0,0}}, QMatrix4x4 transform = {}) {
    Model3D model; model.name = "Skin test triangle";
    ModelMesh3D mesh; mesh.positions = positions; mesh.normals = QVector<QVector3D>(positions.size(), {0,0,1});
    mesh.indices = {0,1,2}; mesh.transform = transform; model.meshes.append(mesh);
    return finalizeModel3D(std::move(model));
}
Rig3D twoRoots() { return {{{"left", "Left", {}, {}, {0,0,0}}, {"right", "Right", {}, {}, {10,0,0}}}, 0}; }
Skin3D uniformSkin(const Model3D &model, const Rig3D &rig, quint16 index) {
    Skin3D skin; for (const auto &joint : rig.joints) skin.jointIds.append(joint.id);
    for (const auto &mesh : model.meshes) {
        SkinVertex3D vertex; vertex.joints[0] = index; vertex.weights[0] = 1;
        skin.meshes.append(QVector<SkinVertex3D>(mesh.positions.size(), vertex));
    }
    return skin;
}
QJsonObject payload(QJsonObject json, const QByteArray &raw) {
    json["data"] = QString::fromLatin1(qCompress(raw, 1).toBase64());
    json["sha256"] = QString::fromLatin1(QCryptographicHash::hash(raw, QCryptographicHash::Sha256).toHex());
    return json;
}
}
class Skin3DTest : public QObject {
    Q_OBJECT
private slots:
    void automaticBindingIsDeterministicNormalizedAndUsesModelCoordinates() {
        const auto model = triangle(); QVERIFY(model); const auto rig = twoRoots(); QString error;
        const auto skin = autoBindSkin3D(*model, rig, &error); QVERIFY2(skin, qPrintable(error));
        QVERIFY(validateSkin3D(*skin, rig, model.get()).isEmpty());
        QCOMPARE(skin->jointIds, QStringList({"left", "right"}));
        QVERIFY(skinJointWeight(skin->meshes[0][0], 0) > .9999f);
        QVERIFY(std::abs(skinJointWeight(skin->meshes[0][1], 0) - .5f) < .00001f);
        QVERIFY(skinJointWeight(skin->meshes[0][2], 1) > .9999f);
        const auto again = autoBindSkin3D(*model, rig); QVERIFY(again); QVERIFY(*again == *skin);
        QMatrix4x4 transform; transform.translate(10,0,0); transform.scale(2,1,1);
        const auto transformed = triangle({{0,0,0},{2.5f,0,0},{5,0,0}}, transform);
        auto translatedRig = rig; translatedRig.joints[0].position.setX(10); translatedRig.joints[1].position.setX(20);
        const auto movedSkin = autoBindSkin3D(*transformed, translatedRig); QVERIFY(movedSkin);
        for (int i = 0; i < 3; ++i) QVERIFY(std::abs(skinJointWeight(movedSkin->meshes[0][i], 0) - skinJointWeight(skin->meshes[0][i], 0)) < .00001f);
    }
    void automaticBindingFollowsBoneSegmentsAndSupportsCancellation() {
        auto rig = twoRoots(); rig.joints[1].parentId = "left"; rig.joints[1].position = {0,10,0};
        rig.joints.append({"other", "Other", {}, {}, {10,0,0}});
        const auto model = triangle({{0,5,0},{1,5,0},{0,6,0}}); QString error;
        const auto skin = autoBindSkin3D(*model, rig, &error); QVERIFY2(skin, qPrintable(error));
        QVERIFY(skinJointWeight(skin->meshes[0][0], 0) > .9999f);
        QVERIFY(skinJointWeight(skin->meshes[0][0], 1) < .0001f);
        const auto cancelled = std::make_shared<std::atomic_bool>(true);
        QVERIFY(!autoBindSkin3D(*model, rig, &error, cancelled)); QVERIFY(error.contains("cancelled"));
        QVERIFY(!autoBindSkin3D(*model, {}, &error)); QVERIFY(error.contains("skeleton"));
    }
    void paintingRetainsFourNormalizedInfluencesAndRejectsImpossibleReduction() {
        SkinVertex3D vertex{{0,1,2,3}, {.4f,.3f,.2f,.1f}}; QString error;
        QVERIFY(setSkinJointWeight(vertex, 4, .25f, &error)); QVERIFY(error.isEmpty());
        QCOMPARE(skinJointWeight(vertex, 4), .25f); QCOMPARE(skinJointWeight(vertex, 3), 0.f);
        float total = 0; for (const auto weight : vertex.weights) total += weight;
        QVERIFY(std::abs(total - 1) < .000001f);
        QVERIFY(adjustSkinJointWeight(vertex, 4, .15f)); QVERIFY(std::abs(skinJointWeight(vertex, 4) - .4f) < .000001f);
        QVERIFY(setSkinJointWeight(vertex, 4, std::nextafter(1.f, 0.f), &error));
        for (const auto weight : vertex.weights) QVERIFY(weight >= 0);
        QVERIFY(setSkinJointWeight(vertex, 4, 1.f)); QCOMPARE(skinJointWeight(vertex, 4), 1.f);
        const auto sole = vertex;
        QVERIFY(!adjustSkinJointWeight(vertex, 4, -.2f, &error)); QVERIFY(error.contains("only joint influence")); QVERIFY(vertex == sole);
        QVERIFY(setSkinJointWeight(vertex, 0, .3f));
        QVERIFY(adjustSkinJointWeight(vertex, 4, -.2f));
        QVERIFY(std::abs(skinJointWeight(vertex, 4) - .5f) < .000001f);
        const auto before = vertex;
        QVERIFY(!setSkinJointWeight(vertex, 256, .5f, &error)); QVERIFY(vertex == before);
        QVERIFY(!setSkinJointWeight(vertex, 0, std::numeric_limits<float>::quiet_NaN(), &error)); QVERIFY(vertex == before);
        QVERIFY(!adjustSkinJointWeight(vertex, 0, std::numeric_limits<float>::infinity(), &error)); QVERIFY(vertex == before);
    }
    void invalidInfluencesCountsAndJointMappingsAreRejected() {
        const auto model = triangle(); const auto rig = twoRoots(); const auto valid = uniformSkin(*model, rig, 0);
        QVERIFY(validateSkin3D(valid, rig, model.get()).isEmpty());
        auto changed = valid; changed.jointIds[1] = "left"; QVERIFY(validateSkin3D(changed, rig).contains("duplicate"));
        changed = valid; changed.jointIds[1] = "missing"; QVERIFY(validateSkin3D(changed, rig).contains("missing"));
        changed = valid; changed.meshes.append(changed.meshes[0]); QVERIFY(validateSkin3D(changed, rig, model.get()).contains("mesh counts"));
        changed = valid; changed.meshes[0].removeLast(); QVERIFY(validateSkin3D(changed, rig, model.get()).contains("vertex counts"));
        changed = valid; changed.meshes[0][0].joints[3] = 9; QVERIFY(validateSkin3D(changed, rig).contains("out-of-range"));
        changed = valid; changed.meshes[0][0].weights = {.5f,.5f,0,0}; QVERIFY(validateSkin3D(changed, rig).contains("duplicate"));
        changed = valid; changed.meshes[0][0].weights[0] = .5f; QVERIFY(validateSkin3D(changed, rig).contains("sum to one"));
        changed = valid; changed.meshes[0][0].weights[0] = -1; QVERIFY(validateSkin3D(changed, rig).contains("finite"));
        changed = valid; changed.meshes[0][0].weights[0] = std::numeric_limits<float>::quiet_NaN();
        QVERIFY(validateSkin3D(changed, rig).contains("finite"));
        // Hot-path metadata checks intentionally leave the per-vertex scan to bind/load/save.
        QVERIFY(validateSkin3D(changed, rig, model.get(), true).isEmpty());
    }
    void posePreviewRotatesRestPivotAndDescendantsWithoutChangingSource() {
        const auto model = triangle({{1,0,0},{2,0,0},{1,1,0}}); const auto original = serializeModel3D(*model);
        Rig3D rig{{{"root", "Root", {}, {}, {0,0,0}}, {"child", "Child", "root", {}, {1,0,0}},
            {"tip", "Tip", "child", {}, {2,0,0}}}, 0};
        auto skin = uniformSkin(*model, rig, 2); QString error;
        const auto identity = deformModel3D(*model, rig, skin, "child", {}, &error);
        QVERIFY2(identity, qPrintable(error)); QCOMPARE(serializeModel3D(*identity), original);
        const auto turned = deformModel3D(*model, rig, skin, "child", {0,0,90}, &error);
        QVERIFY2(turned, qPrintable(error));
        QVERIFY((turned->meshes[0].positions[1] - QVector3D(1,1,0)).length() < .00001f);
        QVERIFY((turned->meshes[0].positions[2] - QVector3D(0,0,0)).length() < .00001f);
        const auto rootTurned = deformModel3D(*model, rig, skin, "root", {0,0,90}, &error);
        QVERIFY(rootTurned); QVERIFY((rootTurned->meshes[0].positions[1] - QVector3D(0,2,0)).length() < .00001f);
        QVERIFY(setSkinJointWeight(skin.meshes[0][1], 0, .5f));
        const auto blended = deformModel3D(*model, rig, skin, "child", {0,0,90}); QVERIFY(blended);
        QVERIFY((blended->meshes[0].positions[1] - QVector3D(1.5f,.5f,0)).length() < .00001f);
        QCOMPARE(serializeModel3D(*model), original);
        QVERIFY(!deformModel3D(*model, rig, skin, "missing", {0,0,90}, &error));
    }
    void transformedMeshDeformsInModelSpaceAndPreservesItsTransform() {
        QMatrix4x4 transform; transform.translate(10,0,0); transform.scale(2,3,1);
        auto model = triangle({{1,0,0},{0,1,0},{0,0,0}}, transform);
        Rig3D rig{{{"root", "Root", {}, {}, {10,0,0}}}, 10};
        const auto skin = uniformSkin(*model, rig, 0); QString error;
        const auto deformed = deformModel3D(*model, rig, skin, "root", {0,0,90}, &error); QVERIFY2(deformed, qPrintable(error));
        QCOMPARE(deformed->meshes[0].transform, transform);
        QVERIFY((transform.map(deformed->meshes[0].positions[0]) - QVector3D(10,2,0)).length() < .00001f);
        QVERIFY((transform.map(deformed->meshes[0].positions[1]) - QVector3D(7,0,0)).length() < .00001f);
        QCOMPARE(model->meshes[0].positions[0], QVector3D(1,0,0));
    }
    void packedWeightsRoundTripAndRejectDamagedOrOversizedStreams() {
        const auto model = triangle(); const auto rig = twoRoots(); const auto skin = autoBindSkin3D(*model, rig); QVERIFY(skin);
        const auto json = skin3DToJson(*skin); QString error;
        const auto restored = skin3DFromJson(json, rig, &error); QVERIFY2(restored, qPrintable(error)); QVERIFY(*restored == *skin);
        QCOMPARE(skin3DByteSize(*skin), qint64(72)); QCOMPARE(skin3DCacheKey(*restored), skin3DCacheKey(*skin));
        auto invalid = json; invalid["sha256"] = QString(64, '0'); QVERIFY(!skin3DFromJson(invalid, rig, &error)); QVERIFY(error.contains("checksum"));
        invalid = json; invalid["data"] = "not base64!"; QVERIFY(!skin3DFromJson(invalid, rig, &error));
        invalid = json; invalid["meshVertexCounts"] = QJsonArray{1000001}; QVERIFY(!skin3DFromJson(invalid, rig, &error));
        invalid = json; invalid["meshVertexCounts"] = QJsonArray{1.5}; QVERIFY(!skin3DFromJson(invalid, rig, &error));
        invalid = json; invalid["jointIds"] = QJsonArray{"left", "left"}; QVERIFY(!skin3DFromJson(invalid, rig, &error));
        auto compressed = QByteArray::fromBase64(json["data"].toString().toLatin1()); compressed.append('x');
        invalid = json; invalid["data"] = QString::fromLatin1(compressed.toBase64()); QVERIFY(!skin3DFromJson(invalid, rig, &error));
        // A forged small qCompress length must not permit decompression beyond the fixed output budget.
        compressed = qCompress(QByteArray(65536, '\0')); qToBigEndian<quint32>(72, compressed.data());
        invalid = json; invalid["data"] = QString::fromLatin1(compressed.toBase64()); QVERIFY(!skin3DFromJson(invalid, rig, &error));
        auto raw = qUncompress(QByteArray::fromBase64(json["data"].toString().toLatin1()));
        qToLittleEndian<quint16>(255, raw.data());
        QVERIFY(!skin3DFromJson(payload(json, raw), rig, &error)); QVERIFY(error.contains("out-of-range"));
        raw = qUncompress(QByteArray::fromBase64(json["data"].toString().toLatin1()));
        qToLittleEndian<quint32>(0x7fc00000, raw.data() + 2);
        QVERIFY(!skin3DFromJson(payload(json, raw), rig, &error)); QVERIFY(error.contains("finite"));
    }
    void sceneCacheIgnoresWeightsAndLegacyScenesCannotDiscardThem() {
        const auto model = triangle(); Scene3D scene; scene.models.insert(model->id, model);
        ModelInstance3D object{"model", "Model", model->id}; object.rig = twoRoots(); object.skin = autoBindSkin3D(*model, object.rig); scene.objects.append(object);
        const auto json = scene3DToJson(scene); QCOMPARE(json["version"].toInt(), 4); QString error;
        const auto decoded = scene3DFromJson(json, scene.models, &error); QVERIFY2(decoded, qPrintable(error)); QVERIFY(*decoded == scene);
        for (int version : {1,2}) { auto old = json; old["version"] = version; QVERIFY(!scene3DFromJson(old, scene.models, &error)); }
        const auto key = scene3DCacheKey(scene, {640,480}); const auto weightKey = skin3DCacheKey(*object.skin);
        auto &vertex = scene.objects[0].skin->meshes[0][1]; QVERIFY(setSkinJointWeight(vertex, 0, .75f));
        QCOMPARE(scene3DCacheKey(scene, {640,480}), key); QVERIFY(skin3DCacheKey(*scene.objects[0].skin) != weightKey);
        const auto cachedJson = scene3DToJson(scene, false)["objects"].toArray()[0].toObject();
        QVERIFY(!cachedJson.contains("rig")); QVERIFY(!cachedJson.contains("skin"));
        QCOMPARE(skinJointWeight(object.skin->meshes[0][1], 0), .5f);
    }
};
QTEST_GUILESS_MAIN(Skin3DTest)
#include "test_skin3d.moc"
