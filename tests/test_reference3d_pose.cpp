#include "reference3d_pose.h"
#include <QtTest>
#include <cmath>
#include <limits>

using namespace toon;
namespace {
constexpr int upperNode = 1, middleNode = 2, endNode = 3;
ReferenceInstance fixture(bool oriented = false, float upperLength = 1, float lowerLength = 1) {
    ReferenceAsset asset; asset.name = "Authored quaternion limb";
    ReferenceNode parent; parent.name = "Parent";
    ReferenceNode upper; upper.name = "Upper"; upper.parentIndex = 0;
    ReferenceNode middle; middle.name = "Middle"; middle.parentIndex = upperNode; middle.translation = {upperLength, 0, 0};
    ReferenceNode end; end.name = "End"; end.parentIndex = middleNode; end.translation = {lowerLength, 0, 0};
    if (oriented) {
        parent.translation = {2, -1, .4f}; parent.rotation = QQuaternion::fromEulerAngles(10, 20, 35); parent.scale = {1.2f, 1.2f, 1.2f};
        upper.translation = {.5f, .3f, 0}; upper.rotation = QQuaternion::fromEulerAngles(25, 0, 45);
        middle.rotation = QQuaternion::fromAxisAndAngle(1, 0, 0, 90);
    }
    asset.nodes = {parent, upper, middle, end};
    ReferenceInstance instance; instance.id = "limb"; instance.asset = finalizeReferenceAsset(asset);
    const auto bind = evaluateReferenceInstance(instance); if (!bind) return {};
    const auto tip = bind->nodeGlobals[endNode].map(QVector3D());
    ReferencePrimitive primitive;
    for (auto position : {tip, tip + QVector3D(.1f, 0, 0), tip + QVector3D(0, .1f, 0)}) {
        ReferenceVertex vertex; vertex.position = position; vertex.normal = {0, 0, 1}; vertex.weights[0] = 1; primitive.vertices.append(vertex);
    }
    primitive.indices = {0, 1, 2}; ReferenceMesh mesh; mesh.name = "Tip mesh"; mesh.primitives = {primitive}; asset.meshes = {mesh};
    ReferenceNode meshNode; meshNode.name = "Skinned mesh"; meshNode.meshIndex = 0; meshNode.skinIndex = 0; asset.nodes.append(meshNode);
    ReferenceSkin skin; skin.joints = {endNode}; skin.inverseBindMatrices = {bind->nodeGlobals[endNode].inverted()}; skin.skeletonRoot = 0; asset.skins = {skin};
    instance.asset = finalizeReferenceAsset(asset); return instance;
}
QVector3D point(const ReferenceEvaluation &evaluation, int node) { return evaluation.nodeGlobals[node].map(QVector3D()); }
bool near(QVector3D first, QVector3D second, float tolerance = .001f) { return (first - second).length() <= tolerance; }
QByteArray bytes(const ReferenceInstance &instance) { ReferenceScene scene; scene.instances = {instance}; return serializeReferenceScene(scene); }
}
class Reference3DPoseTests final : public QObject {
    Q_OBJECT
private slots:
    void poleSelectsBendAndMeshActuallyFollowsEffector() {
        auto instance = fixture(); QVERIFY(instance.asset); QString error;
        QVERIFY2(solveReferenceLimbIK(instance, upperNode, middleNode, endNode, {1, 1, 0}, {0, 3, 0}, &error), qPrintable(error));
        auto evaluated = evaluateReferenceInstance(instance, &error); QVERIFY2(evaluated, qPrintable(error));
        QVERIFY(near(point(*evaluated, middleNode), {0, 1, 0})); QVERIFY(near(point(*evaluated, endNode), {1, 1, 0}));
        QVERIFY(near(evaluated->primitives[0].vertices[0].position, {1, 1, 0}));
        instance = fixture();
        QVERIFY2(solveReferenceLimbIK(instance, upperNode, middleNode, endNode, {1, 1, 0}, {3, 0, 0}, &error), qPrintable(error));
        evaluated = evaluateReferenceInstance(instance, &error); QVERIFY2(evaluated, qPrintable(error));
        QVERIFY(near(point(*evaluated, middleNode), {1, 0, 0})); QVERIFY(near(point(*evaluated, endNode), {1, 1, 0}));
    }
    void arbitraryRestAxesOrientedParentAndPlacementRemainCorrect() {
        auto instance = fixture(true); QVERIFY(instance.asset); instance.translation = {-3, 2, 1};
        instance.rotation = QQuaternion::fromEulerAngles(-20, 15, 63); instance.scale = {1.3f, 1.3f, 1.3f};
        instance.poseRotations.insert(0, QQuaternion::fromEulerAngles(17, -8, 27));
        instance.poseTranslations.insert(0, {2.1f, -1, .4f});
        const auto translations = instance.poseTranslations, scales = instance.poseScales; const auto rootRotation = instance.poseRotations.value(0);
        QString error; const auto before = evaluateReferenceInstance(instance, &error); QVERIFY2(before, qPrintable(error));
        const auto upper = point(*before, upperNode), middle = point(*before, middleNode), end = point(*before, endNode);
        const auto target = upper + QVector3D(1.1f, .2f, .6f), pole = upper + QVector3D(0, 5, 3);
        QVERIFY2(solveReferenceLimbIK(instance, upperNode, middleNode, endNode, target, pole, &error), qPrintable(error));
        const auto after = evaluateReferenceInstance(instance, &error); QVERIFY2(after, qPrintable(error));
        QVERIFY(near(point(*after, upperNode), upper)); QVERIFY(near(point(*after, endNode), target));
        QVERIFY(std::abs((point(*after, middleNode) - upper).length() - (middle - upper).length()) < .001f);
        QVERIFY(std::abs((point(*after, endNode) - point(*after, middleNode)).length() - (end - middle).length()) < .001f);
        QCOMPARE(instance.poseTranslations, translations); QCOMPARE(instance.poseScales, scales); QCOMPARE(instance.poseRotations.value(0), rootRotation);
        QVERIFY(near(after->primitives[0].vertices[0].position, target));
    }
    void unreachableOuterAndInnerTargetsClampWithoutStretching() {
        QString error; auto instance = fixture();
        QVERIFY2(solveReferenceLimbIK(instance, upperNode, middleNode, endNode, {10, 0, 0}, {0, 2, 0}, &error), qPrintable(error));
        auto evaluated = evaluateReferenceInstance(instance, &error); QVERIFY2(evaluated, qPrintable(error)); QVERIFY(near(point(*evaluated, endNode), {2, 0, 0}));
        instance = fixture(false, 2, 1);
        QVERIFY2(solveReferenceLimbIK(instance, upperNode, middleNode, endNode, {0, 0, 0}, {0, 2, 0}, &error), qPrintable(error));
        evaluated = evaluateReferenceInstance(instance, &error); QVERIFY2(evaluated, qPrintable(error));
        QVERIFY(near(point(*evaluated, endNode), {1, 0, 0}));
        QVERIFY(std::abs((point(*evaluated, middleNode) - point(*evaluated, upperNode)).length() - 2) < .001f);
        QVERIFY(std::abs((point(*evaluated, endNode) - point(*evaluated, middleNode)).length() - 1) < .001f);
        instance = fixture();
        QVERIFY2(solveReferenceLimbIK(instance, upperNode, middleNode, endNode, {0, 0, 0}, {0, 2, 0}, &error), qPrintable(error));
        evaluated = evaluateReferenceInstance(instance, &error); QVERIFY2(evaluated, qPrintable(error)); QVERIFY(near(point(*evaluated, endNode), {0, 0, 0}));
    }
    void collinearPoleAndRepeatedEndpointDragsAreStable() {
        auto instance = fixture(); QString error;
        QVERIFY2(solveReferenceLimbIK(instance, upperNode, middleNode, endNode, {1, 0, 0}, {3, 0, 0}, &error), qPrintable(error));
        for (int i = 0; i < 40; ++i) {
            const float angle = i * .13f; const QVector3D target(1.1f * std::cos(angle), .7f + .3f * std::sin(angle), .25f * std::sin(angle * .5f));
            QVERIFY2(solveReferenceLimbIK(instance, upperNode, middleNode, endNode, target, {0, 3, 2}, &error), qPrintable(error));
            const auto evaluation = evaluateReferenceInstance(instance, &error); QVERIFY2(evaluation, qPrintable(error));
            QVERIFY(near(point(*evaluation, endNode), target));
            QVERIFY(std::abs((point(*evaluation, middleNode) - point(*evaluation, upperNode)).length() - 1) < .001f);
            QVERIFY(std::abs((point(*evaluation, endNode) - point(*evaluation, middleNode)).length() - 1) < .001f);
        }
    }
    void unsupportedScalesAndMalformedChainsRejectAtomically() {
        QString error;
        for (auto scale : {QVector3D(1, 2, 1), QVector3D(-1, -1, -1), QVector3D(0, 0, 0)}) {
            auto instance = fixture(); instance.scale = scale; const auto previous = instance.poseRotations;
            QVERIFY(!solveReferenceLimbIK(instance, upperNode, middleNode, endNode, {1, 1, 0}, {0, 3, 0}, &error));
            QCOMPARE(instance.poseRotations, previous); QVERIFY(!error.isEmpty());
        }
        auto instance = fixture(); instance.poseScales.insert(0, {1, 2, 1}); auto original = bytes(instance);
        QVERIFY(!original.isEmpty()); QVERIFY(!solveReferenceLimbIK(instance, upperNode, middleNode, endNode, {1, 1, 0}, {0, 3, 0}, &error)); QCOMPARE(bytes(instance), original);
        instance = fixture(); instance.poseScales.insert(0, {.000001f, .0000011f, .000001f}); instance.scale = {1000000, 1000000, 1000000}; original = bytes(instance);
        QVERIFY(!original.isEmpty()); QVERIFY(!solveReferenceLimbIK(instance, upperNode, middleNode, endNode, {1, 1, 0}, {0, 3, 0}, &error)); QVERIFY(error.contains("uniform")); QCOMPARE(bytes(instance), original);
        instance = fixture(); original = bytes(instance);
        QVERIFY(!solveReferenceLimbIK(instance, 0, middleNode, endNode, {1, 1, 0}, {0, 3, 0}, &error)); QCOMPARE(bytes(instance), original);
        QVERIFY(!solveReferenceLimbIK(instance, upperNode, middleNode, 200, {1, 1, 0}, {0, 3, 0}, &error)); QCOMPARE(bytes(instance), original);
        QVERIFY(!solveReferenceLimbIK(instance, upperNode, middleNode, endNode, {std::numeric_limits<float>::quiet_NaN(), 0, 0}, {0, 3, 0}, &error)); QCOMPARE(bytes(instance), original);
        instance.locked = true; original = bytes(instance);
        QVERIFY(!solveReferenceLimbIK(instance, upperNode, middleNode, endNode, {1, 1, 0}, {0, 3, 0}, &error)); QCOMPARE(bytes(instance), original);
        instance = fixture(false, 0, 1); QVERIFY(instance.asset); original = bytes(instance);
        QVERIFY(!solveReferenceLimbIK(instance, upperNode, middleNode, endNode, {1, 1, 0}, {0, 3, 0}, &error)); QCOMPARE(bytes(instance), original);
    }
};
QTEST_MAIN(Reference3DPoseTests)
#include "test_reference3d_pose.moc"
