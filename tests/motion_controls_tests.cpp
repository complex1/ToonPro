#include "motion_controls.h"
#include "document.h"
#include <QtTest>
#include <QTemporaryDir>
#include <cmath>

using namespace toon;
namespace {
Project motionProject() {
    auto project = Project::create(); project.width = project.height = 128;
    auto &shot = project.shots[0]; shot.durationFrames = 25;
    auto &layer = shot.layers[0]; layer.kind = LayerKind::Vector;
    auto object = EditableObject::create(); object.nodes = {BezierNode::corner({10, 10}), BezierNode::corner({40, 30})};
    layer.objects = {object};
    auto group = RasterLayer::create("Parent"); group.group = true; layer.parentId = group.id;
    const auto id = layer.id; const auto pose = pathPose(object);
    shot.layers.append(group);
    shot.layerAnimations = {
        {id, {16, 12}, {
            {KeyframeProperty::PositionX, {{0, 20, KeyframeInterpolation::EaseOut}, {12, 80}, {24, 110}}},
            {KeyframeProperty::Rotation, {{0, 0}, {12, 90}, {24, 180}}},
            {KeyframeProperty::ScaleX, {{0, 2}, {24, 3}}},
            {KeyframeProperty::ScaleY, {{0, .5}, {24, 1}}}
        }, {{object.id, {{0, pose}, {24, pose}}}}},
        {group.id, {20, 25}, {
            {KeyframeProperty::Rotation, {{0, 15}, {24, 60}}},
            {KeyframeProperty::ScaleX, {{0, 1.2}}},
            {KeyframeProperty::PositionY, {{0, 40}, {24, 70}}}
        }, {}}
    };
    return project;
}
bool near(QPointF a, QPointF b) { return QLineF(a, b).length() < 1e-7; }
bool sameTransform(QTransform a, QTransform b) {
    return near(a.map(QPointF(0, 0)), b.map(QPointF(0, 0))) && near(a.map(QPointF(100, 0)), b.map(QPointF(100, 0)))
        && near(a.map(QPointF(0, 100)), b.map(QPointF(0, 100)));
}
}
class MotionControlsTests : public QObject {
    Q_OBJECT
private slots:
    void pivotPreservesCurrentPoseAndCompensatesAllPositionKeys() {
        const auto project = motionProject(); const auto &source = project.shots[0]; const auto id = source.layers[0].id;
        QVERIFY2(project.validate().isEmpty(), qPrintable(project.validate()));
        Document document; document.load(project); QVERIFY(document.setCurrentFrame(8));
        const auto localBefore = evaluateLayerTransform(source, id, 8), worldBefore = worldLayerTransform(source, id, 8);
        const auto frameZeroBefore = worldLayerTransform(source, id, 0);
        const QPointF pivot(50, 60), delta = pivot - layerAnimation(source, id)->pivot;
        const QPointF compensation = localBefore.map(delta) - localBefore.map(QPointF()) - delta;
        QVERIFY(document.beginKeyframeEdit(id, "Move pivot"));
        QVERIFY(document.previewKeyframePivot(pivot));
        const auto &edited = document.project().shots[0];
        QVERIFY(sameTransform(evaluateLayerTransform(edited, id, 8), localBefore));
        QVERIFY(sameTransform(worldLayerTransform(edited, id, 8), worldBefore));
        QVERIFY(!sameTransform(worldLayerTransform(edited, id, 0), frameZeroBefore));
        const auto *originalX = propertyChannel(source, id, KeyframeProperty::PositionX);
        const auto *newX = propertyChannel(edited, id, KeyframeProperty::PositionX);
        QCOMPARE(newX->keys.size(), originalX->keys.size());
        for (int i = 0; i < originalX->keys.size(); ++i) {
            QVERIFY(std::abs(newX->keys[i].value - originalX->keys[i].value - compensation.x()) < 1e-7);
            QCOMPARE(newX->keys[i].interpolation, originalX->keys[i].interpolation);
            QCOMPARE(newX->keys[i].frame, originalX->keys[i].frame);
        }
        const auto *newY = propertyChannel(edited, id, KeyframeProperty::PositionY);
        QVERIFY(newY); QCOMPARE(newY->keys.size(), 1); QCOMPARE(newY->keys[0].frame, 0);
        QVERIFY(std::abs(newY->keys[0].value - compensation.y()) < 1e-7);
        QCOMPARE(edited.layers[0].objects, source.layers[0].objects);
        QCOMPARE(layerAnimation(edited, id)->paths, layerAnimation(source, id)->paths);
        QCOMPARE(edited.layerAnimations[1], source.layerAnimations[1]);
        QCOMPARE(document.history()->count(), 0);
        document.commitKeyframeEdit(); QCOMPARE(document.history()->count(), 1);
        document.history()->undo(); QCOMPARE(document.project().toJson(), project.toJson());
        document.history()->redo(); QCOMPARE(layerAnimation(document.project().shots[0], id)->pivot, pivot);
    }

    void pivotPreviewsDoNotAccumulateAndReturningToOriginIsExact() {
        const auto project = motionProject(); const auto id = project.shots[0].layers[0].id;
        Document repeated, single; repeated.load(project); single.load(project);
        QVERIFY(repeated.setCurrentFrame(8)); QVERIFY(single.setCurrentFrame(8));
        QVERIFY(repeated.beginKeyframeEdit(id)); QVERIFY(single.beginKeyframeEdit(id));
        for (int i = 0; i < 80; ++i) QVERIFY(repeated.previewKeyframePivot({qreal(30 + i), qreal(90 - i)}));
        QVERIFY(single.previewKeyframePivot({109, 11}));
        QCOMPARE(repeated.project().shots[0].layerAnimations, single.project().shots[0].layerAnimations);
        QVERIFY(repeated.previewKeyframePivot(layerAnimation(project.shots[0], id)->pivot));
        QCOMPARE(repeated.project().toJson(), project.toJson());
        repeated.commitKeyframeEdit(); QCOMPARE(repeated.history()->count(), 0);
        single.cancelKeyframeEdit(); QCOMPARE(single.project().toJson(), project.toJson());
    }

    void neutralPivotPersistsWithoutSpuriousMotionAndRoundTrips() {
        auto project = Project::create(); project.width = project.height = 128;
        const auto id = project.shots[0].layers[0].id;
        Document document; document.load(project);
        QCOMPARE(effectiveAnimationPivot(project.shots[0], id, {128, 128}), QPointF(64, 64));
        QVERIFY(document.beginKeyframeEdit(id)); QVERIFY(document.previewKeyframePivot({20, 30}));
        const auto *animation = layerAnimation(document.project().shots[0], id);
        QVERIFY(animation); QCOMPARE(animation->channels.size(), 1); QCOMPARE(animation->channels[0].property, KeyframeProperty::PositionX);
        QCOMPARE(animation->channels[0].keys, QVector<ScalarKeyframe>({{0, 0}}));
        QVERIFY(evaluateLayerTransform(document.project().shots[0], id, 0).isIdentity());
        QVERIFY(document.previewKeyframePivot({64, 64}));
        QVERIFY(document.project().shots[0].layerAnimations.isEmpty());
        document.commitKeyframeEdit(); QCOMPARE(document.history()->count(), 0);
        QVERIFY(document.beginKeyframeEdit(id)); QVERIFY(document.previewKeyframePivot({20, 30})); document.commitKeyframeEdit();
        QTemporaryDir directory; QVERIFY(directory.isValid()); QString error;
        const auto file = directory.filePath("pivot.toon");
        QVERIFY2(saveProject(document.project(), file, &error), qPrintable(error));
        const auto loaded = loadProject(file, &error); QVERIFY2(loaded, qPrintable(error));
        QCOMPARE(loaded->shots[0].layerAnimations, document.project().shots[0].layerAnimations);
        QCOMPARE(effectiveAnimationPivot(loaded->shots[0], id), QPointF(20, 30));
    }

    void pivotFailuresLeaveLastValidPreviewUnchanged() {
        auto project = Project::create(); const auto id = project.shots[0].layers[0].id;
        project.shots[0].layerAnimations = {{id, {}, {
            {KeyframeProperty::PositionX, {{0, 32760}}}, {KeyframeProperty::ScaleX, {{0, 2}}}
        }, {}}};
        Document document; document.load(project); QVERIFY(document.beginKeyframeEdit(id));
        QVERIFY(document.previewKeyframePivot({2, 0}));
        const auto preview = document.project().toJson();
        QVERIFY(!document.previewKeyframePivot({20, 0}));
        QVERIFY(!document.previewKeyframePivot({qQNaN(), 0}));
        QVERIFY(!document.previewKeyframePivot({40000, 0}));
        QCOMPARE(document.project().toJson(), preview);
        document.cancelKeyframeEdit(); QCOMPARE(document.project().toJson(), project.toJson());
        project.shots[0].layers[0].positionLocked = true; document.load(project);
        QVERIFY(document.beginKeyframeEdit(id)); QVERIFY(!document.previewKeyframePivot({2, 0}));
        QVERIFY(!document.previewKeyframePosition({5, 6})); document.cancelKeyframeEdit();
        QCOMPARE(document.history()->count(), 0);
    }

    void pairedPositionPreviewIsAtomicAndCanEditOffTimeMarkers() {
        auto project = Project::create(); const auto id = project.shots[0].layers[0].id;
        project.shots[0].layerAnimations = {{id, {32, 32}, {{KeyframeProperty::PositionX, {{0, 0}, {6, 10, KeyframeInterpolation::EaseOut}}}}, {}}};
        Document document; document.load(project); QVERIFY(document.setCurrentFrame(6));
        QVERIFY(document.beginKeyframeEdit(id)); QVERIFY(document.previewKeyframePosition({12, 22}));
        QCOMPARE(propertyChannel(document.project().shots[0], id, KeyframeProperty::PositionX)->keys.last(),
                 (ScalarKeyframe{6, 12, KeyframeInterpolation::EaseOut}));
        QCOMPARE(propertyChannel(document.project().shots[0], id, KeyframeProperty::PositionY)->keys,
                 QVector<ScalarKeyframe>({{0, 0}, {6, 22}}));
        QVERIFY(document.previewKeyframePosition({30, 50}, 15)); QCOMPARE(document.currentFrame(), 6);
        QCOMPARE(propertyChannel(document.project().shots[0], id, KeyframeProperty::PositionX)->keys.last().frame, 15);
        const auto preview = document.project().toJson();
        QVERIFY(!document.previewKeyframePosition({20, qQNaN()}, 15));
        QVERIFY(!document.previewKeyframePosition({20, 40}, -2));
        QVERIFY(!document.previewKeyframePosition({20, 40}, 120));
        QCOMPARE(document.project().toJson(), preview);
        document.commitKeyframeEdit(); QCOMPARE(document.history()->count(), 1);
        document.history()->undo(); QCOMPARE(document.project().toJson(), project.toJson());
    }

    void metadataBudgetsRejectWholePairedAndPivotPreviews() {
        auto project = Project::create(); auto &shot = project.shots[0]; shot.durationFrames = 20000;
        const auto id = shot.layers[0].id;
        PropertyChannel x{KeyframeProperty::PositionX, {}};
        for (int i = 0; i < maxPropertyKeyframes - 1; ++i) x.keys.append({i, 0});
        shot.layerAnimations = {{id, {}, {x}, {}}};
        Document document; document.load(project); QVERIFY(document.setCurrentFrame(20));
        QVERIFY(document.beginKeyframeEdit(id)); QVERIFY(!document.previewKeyframePosition({12, 22}));
        QCOMPARE(document.project().toJson(), project.toJson()); document.cancelKeyframeEdit();
        auto pivotProject = project;
        pivotProject.shots[0].layerAnimations[0].channels.append({KeyframeProperty::Rotation, {{0, 90}}});
        document.load(pivotProject); QVERIFY(document.beginKeyframeEdit(id));
        QVERIFY(!document.previewKeyframePivot({10, 20})); // Needs a new constant Y channel beyond the key budget.
        QCOMPARE(document.project().toJson(), pivotProject.toJson()); document.cancelKeyframeEdit();
        QCOMPARE(document.history()->count(), 0);
    }

    void geometryMatchesWorldTransformsAndReturnsExactPositionFrames() {
        const auto project = motionProject(); const auto &shot = project.shots[0]; const auto id = shot.layers[0].id;
        QCOMPARE(motionPositionKeyFrames(shot, id), QVector<int>({0, 12, 24}));
        const auto markers = motionPositionKeySamples(shot, id, {128, 128});
        QCOMPARE(markers.size(), 3);
        for (int i = 0; i < markers.size(); ++i) {
            QCOMPARE(markers[i].frame, qreal(i * 12));
            QVERIFY(near(markers[i].position, animationPivotWorld(shot, id, markers[i].frame, {128, 128})));
        }
        const auto geometry = buildMotionPath(shot, id, {128, 128}, 128);
        QVERIFY(!geometry.samples.isEmpty()); QVERIFY(geometry.samples.size() <= 128);
        QCOMPARE(geometry.samples.first().frame, 0); QCOMPARE(geometry.samples.last().frame, 24);
        for (const auto &sample : geometry.samples)
            QVERIFY(near(sample.position, animationPivotWorld(shot, id, sample.frame, {128, 128})));
        QVERIFY(geometry.samples.first().moveTo);
        for (int i = 1; i < geometry.samples.size(); ++i) QVERIFY(!geometry.samples[i].moveTo);
        QVERIFY(!geometry.path.isEmpty());
        auto unanimatedChild = project;
        unanimatedChild.shots[0].layerAnimations.removeFirst();
        const auto childGeometry = buildMotionPath(unanimatedChild.shots[0], id, {128, 128}, 64);
        for (const auto &sample : childGeometry.samples)
            QVERIFY(near(sample.position, animationPivotWorld(unanimatedChild.shots[0], id, sample.frame, {128, 128})));
        QVERIFY(buildMotionPath(shot, "missing", {}, 64).samples.isEmpty());
        QVERIFY(buildMotionPath(shot, id, {}, 0).samples.isEmpty());
        QCOMPARE(buildMotionPath(shot, id, {}, 1).samples.size(), 1);
    }

    void holdJumpsNeverProduceConnectingSegmentsEvenWhenUndersampled() {
        auto project = Project::create(); auto &shot = project.shots[0]; shot.durationFrames = 1000000;
        auto group = RasterLayer::create("Parent"); group.group = true; shot.layers[0].parentId = group.id;
        const auto id = shot.layers[0].id; shot.layers.append(group);
        shot.layerAnimations = {{group.id, {}, {
            {KeyframeProperty::PositionX, {{0, 0, KeyframeInterpolation::Hold}, {100, 100, KeyframeInterpolation::Hold}, {500000, 300}}}
        }, {}}};
        const auto geometry = buildMotionPath(shot, id, {128, 128}, 128);
        QVERIFY(geometry.samples.size() <= 128);
        bool jump100 = false, jump500000 = false;
        for (int i = 1; i < geometry.samples.size(); ++i) {
            const auto &sample = geometry.samples[i]; const auto &previous = geometry.samples[i - 1];
            if ((previous.frame < 100 && sample.frame >= 100) || (previous.frame < 500000 && sample.frame >= 500000)) QVERIFY(sample.moveTo);
            if (sample.frame == 100) { QVERIFY(sample.moveTo); jump100 = true; }
            if (sample.frame == 500000) { QVERIFY(sample.moveTo); jump500000 = true; }
            QVERIFY(near(sample.position, animationPivotWorld(shot, id, sample.frame, {128, 128})));
        }
        QVERIFY(jump100); QVERIFY(jump500000);
        const auto sparse = buildMotionPath(shot, id, {128, 128}, 2);
        QCOMPARE(sparse.samples.size(), 2); QVERIFY(sparse.samples.last().moveTo);
        QCOMPARE(sparse.samples.last().position, QPointF(364, 64));
        auto dense = project;
        auto &keys = dense.shots[0].layerAnimations[0].channels[0].keys; keys.clear();
        for (int i = 0; i < maxPropertyKeyframes; ++i) keys.append({i * 30, qreal(i % 2), KeyframeInterpolation::Hold});
        const auto bounded = buildMotionPath(dense.shots[0], id, {128, 128}, 256);
        QVERIFY(bounded.samples.size() <= 256);
        for (int i = 1; i < bounded.samples.size(); ++i) {
            const auto &previous = bounded.samples[i - 1], &sample = bounded.samples[i];
            const int nextJump = (int(std::floor(previous.frame / 30)) + 1) * 30;
            if (nextJump <= (maxPropertyKeyframes - 1) * 30 && sample.frame >= nextJump) QVERIFY(sample.moveTo);
        }
        dense.shots[0].layerAnimations[0].layerId = id;
        const auto exactMarkers = motionPositionKeySamples(dense.shots[0], id, {128, 128});
        QCOMPARE(exactMarkers.size(), maxPropertyKeyframes);
        QCOMPARE(exactMarkers.last().frame, qreal((maxPropertyKeyframes - 1) * 30));
        QCOMPARE(exactMarkers.last().position, QPointF(1, 0));
    }
};
QTEST_MAIN(MotionControlsTests)
#include "motion_controls_tests.moc"
