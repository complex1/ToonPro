#include "rig3d.h"
#include "scene3d.h"
#include "scene3d_cache.h"
#include <QJsonArray>
#include <QSet>
#include <QTest>
#include <cmath>
#include <limits>
using namespace toon;
namespace {
Rig3D branchRig() {
    return {{{"pelvis", "Pelvis", {}, {}, {2, 1, 0}},
        {"arm", "Arm", "pelvis", {}, {3, 2, 0}},
        {"hand", "Hand", "arm", {}, {4, 2, .25f}}}, 2};
}
Scene3D sceneWithRig() {
    Scene3D scene;
    const auto model = builtInModels3D().front();
    scene.models.insert(model->id, model);
    ModelInstance3D object{"cube", "Cube", model->id};
    object.rig = branchRig();
    scene.objects.append(object);
    return scene;
}
}
class Rig3DTest : public QObject {
    Q_OBJECT
private slots:
    void hierarchyValidationAndTraversal() {
        QVERIFY(validateRig3D({}).isEmpty());
        auto rig = branchRig();
        QVERIFY(validateRig3D(rig).isEmpty());
        QCOMPARE(rigDescendants(rig, "arm"), QStringList({"arm", "hand"}));
        QVERIFY(rigDescendants(rig, "missing").isEmpty());
        QVERIFY(!rigJoint(rig, "missing"));
        QCOMPARE(rigJoint(rig, "hand")->position, QVector3D(4, 2, .25f));
        rig.joints[0].parentId = "hand";
        QVERIFY(validateRig3D(rig).contains("cycle"));
        QCOMPARE(rigDescendants(rig, "arm").size(), 3); // Malformed cycles still terminate.
        rig = branchRig(); rig.joints[1].parentId = "absent";
        QVERIFY(validateRig3D(rig).contains("missing parent"));
        rig = branchRig(); rig.joints[1].parentId = "arm";
        QVERIFY(validateRig3D(rig).contains("cycle"));
        rig = branchRig(); rig.joints[1].id = "pelvis";
        QVERIFY(validateRig3D(rig).contains("identifier"));
        rig = branchRig(); rig.joints[0].id = " ";
        QVERIFY(validateRig3D(rig).contains("identifier"));
    }
    void malformedCoordinatesNamesAndLinksAreRejected() {
        auto rig = branchRig(); rig.joints[0].name = "  ";
        QVERIFY(validateRig3D(rig).contains("names"));
        rig = branchRig(); rig.joints[0].name = QString(121, 'a');
        QVERIFY(validateRig3D(rig).contains("names"));
        for (float invalid : {std::numeric_limits<float>::quiet_NaN(),
                std::numeric_limits<float>::infinity(), -1000001.f, 1000001.f}) {
            rig = branchRig(); rig.joints[0].position.setY(invalid);
            QVERIFY(validateRig3D(rig).contains("coordinates"));
            rig = branchRig(); rig.mirrorPlaneX = invalid;
            QVERIFY(validateRig3D(rig).contains("mirror plane"));
        }
        rig = branchRig(); rig.joints[1].mirrorId = "arm";
        QVERIFY(validateRig3D(rig).contains("reciprocal"));
        rig.joints[1].mirrorId = "hand";
        QVERIFY(validateRig3D(rig).contains("reciprocal"));
        rig.joints[2].mirrorId = "arm";
        QVERIFY(validateRig3D(rig).isEmpty());
        rig.joints[2].mirrorId = "missing";
        QVERIFY(validateRig3D(rig).contains("reciprocal"));
        rig = {};
        for (int i = 0; i <= maxRig3DJoints; ++i) rig.joints.append({QString::number(i), "Joint", {}, {}, {}});
        QVERIFY(validateRig3D(rig).contains("256"));
    }
    void mirroringClonesHierarchyAndMovesPairsAtomically() {
        auto rig = branchRig(); QString error = "old error";
        QVERIFY2(mirrorRigBranch3D(rig, "arm", &error), qPrintable(error));
        QVERIFY(error.isEmpty());
        QCOMPARE(rig.joints.size(), 5);
        const auto *arm = rigJoint(rig, "arm");
        const auto *otherArm = rigJoint(rig, arm->mirrorId);
        QVERIFY(otherArm);
        QCOMPARE(otherArm->parentId, QString("pelvis"));
        QCOMPARE(otherArm->position, QVector3D(1, 2, 0));
        QCOMPARE(otherArm->mirrorId, QString("arm"));
        const QString mirroredArmId = otherArm->id;
        const QString mirroredHandId = rigJoint(rig, "hand")->mirrorId;
        QCOMPARE(rigJoint(rig, mirroredHandId)->parentId, mirroredArmId);
        QCOMPARE(rigJoint(rig, mirroredHandId)->position, QVector3D(0, 2, .25f));
        QVERIFY(moveRigJoint3D(rig, "arm", {3.5f, 3, -.5f}, true, &error));
        QCOMPARE(rigJoint(rig, mirroredArmId)->position, QVector3D(.5f, 3, -.5f));
        QCOMPARE(rigJoint(rig, "hand")->position, QVector3D(4, 2, .25f)); // Rest editing does not drag descendants.
        QVERIFY(moveRigJoint3D(rig, "arm", {4, 3, -.5f}, false, &error));
        QCOMPARE(rigJoint(rig, mirroredArmId)->position, QVector3D(.5f, 3, -.5f));
        const auto before = rig;
        QVERIFY(!mirrorRigBranch3D(rig, "arm", &error));
        QVERIFY(error.contains("already"));
        QVERIFY(rig == before);
        QVERIFY(!moveRigJoint3D(rig, "missing", {}, true, &error));
        QVERIFY(rig == before);
        QVERIFY(!moveRigJoint3D(rig, "arm", {std::numeric_limits<float>::infinity(), 0, 0}, true, &error));
        QVERIFY(rig == before);
        QVERIFY(!moveRigJoint3D(rig, "arm", {-maxRig3DCoordinate, 0, 0}, true, &error)); // Reflected point would exceed limit.
        QVERIFY(rig == before);
    }
    void mirrorParentRulesAndLimitFailuresAreAtomic() {
        auto rig = branchRig(); QString error;
        rig.joints[0].position.setX(3);
        auto before = rig;
        QVERIFY(!mirrorRigBranch3D(rig, "arm", &error));
        QVERIFY(error.contains("parent"));
        QVERIFY(rig == before);
        rig = branchRig();
        rig.joints.append({"otherPelvis", "Other pelvis", {}, "pelvis", {2, 1, 0}});
        rig.joints[0].mirrorId = "otherPelvis";
        QVERIFY(mirrorRigBranch3D(rig, "arm", &error));
        QCOMPARE(rigJoint(rig, rigJoint(rig, "arm")->mirrorId)->parentId, QString("otherPelvis"));
        rig = branchRig();
        for (int i = 3; i < maxRig3DJoints; ++i) rig.joints.append({QString::number(i), "Joint", {}, {}, {}});
        before = rig;
        QVERIFY(!mirrorRigBranch3D(rig, "arm", &error));
        QVERIFY(error.contains("256"));
        QVERIFY(rig == before);
    }
    void humanoidFitsBoundsAndHasIndependentIds() {
        Bounds3D bounds; bounds.include({10, 20, 30}); bounds.include({12, 24, 32});
        const auto rig = createHumanoidRig3D(bounds);
        QVERIFY2(validateRig3D(rig).isEmpty(), qPrintable(validateRig3D(rig)));
        QCOMPARE(rig.joints.size(), 22);
        QCOMPARE(rig.mirrorPlaneX, 11.f);
        QSet<QString> ids;
        for (const auto &joint : rig.joints) {
            ids.insert(joint.id);
            for (int axis = 0; axis < 3; ++axis) {
                QVERIFY(joint.position[axis] >= bounds.minimum[axis]);
                QVERIFY(joint.position[axis] <= bounds.maximum[axis]);
            }
            if (!joint.mirrorId.isEmpty()) {
                const auto *pair = rigJoint(rig, joint.mirrorId);
                QVERIFY(pair);
                QVERIFY(std::abs(joint.position.x() + pair->position.x() - 22.f) < .0001f);
                QCOMPARE(joint.position.y(), pair->position.y());
                QCOMPARE(joint.position.z(), pair->position.z());
            }
        }
        const auto second = createHumanoidRig3D(bounds);
        for (const auto &joint : second.joints) QVERIFY(!ids.contains(joint.id));
        QVERIFY(createHumanoidRig3D({}).joints.isEmpty());
        bounds.maximum.setY(bounds.minimum.y());
        QVERIFY(createHumanoidRig3D(bounds).joints.isEmpty());
    }
    void sceneJsonRoundTripAndLegacyRejectsHiddenRigs() {
        const auto scene = sceneWithRig(); QString error;
        const auto json = scene3DToJson(scene);
        QCOMPARE(json.value("version"), QJsonValue(4));
        auto parsed = scene3DFromJson(json, scene.models, &error);
        QVERIFY2(parsed, qPrintable(error));
        QVERIFY(*parsed == scene);
        auto old = json; old["version"] = 1;
        QVERIFY(!scene3DFromJson(old, scene.models, &error));
        QVERIFY(error.contains("version 2"));
        auto objects = old["objects"].toArray(); auto object = objects[0].toObject();
        object.remove("rig"); object.remove("pose"); objects[0] = object; old["objects"] = objects;
        parsed = scene3DFromJson(old, scene.models, &error);
        QVERIFY2(parsed, qPrintable(error));
        QVERIFY(parsed->objects[0].rig.joints.isEmpty());
        old["version"] = 2;
        QVERIFY(!scene3DFromJson(old, scene.models, &error));
        QVERIFY(error.contains("rig metadata"));
        old["version"] = 1.5;
        QVERIFY(!scene3DFromJson(old, scene.models, &error));
        auto malformed = json; objects = malformed["objects"].toArray(); object = objects[0].toObject();
        auto rig = object["rig"].toObject(); auto joints = rig["joints"].toArray();
        auto joint = joints[0].toObject(); joint["parentId"] = "hand"; joints[0] = joint;
        rig["joints"] = joints; object["rig"] = rig; objects[0] = object; malformed["objects"] = objects;
        QVERIFY(!scene3DFromJson(malformed, scene.models, &error));
        QVERIFY(error.contains("cycle"));
    }
    void rigChangesReuseMeshPreviewAndKeepInstanceStateIndependent() {
        auto scene = sceneWithRig();
        const auto original = scene;
        const QSize size(640, 480);
        const auto key = scene3DCacheKey(scene, size);
        QImage image(size, QImage::Format_ARGB32_Premultiplied); image.fill(Qt::red);
        cacheScene3DPreview(key, image);
        QVERIFY(moveRigJoint3D(scene.objects[0].rig, "hand", {5, 3, 1}, false));
        scene.objects[0].rig.mirrorPlaneX = 3;
        QVERIFY(!(scene == original));
        QCOMPARE(original.objects[0].rig.joints[2].position, QVector3D(4, 2, .25f));
        QCOMPARE(scene3DCacheKey(scene, size), key);
        QCOMPARE(scene3DPreview(scene, size, true), image);
        scene.objects[0].position.setX(1);
        QVERIFY(scene3DCacheKey(scene, size) != key);
    }
};
QTEST_GUILESS_MAIN(Rig3DTest)
#include "test_rig3d.moc"
