#include "project.h"
#include "motion_controls.h"
#include "keyframe_render.h"
#include "sheet.h"
#include <QtTest>
#include <cmath>
using namespace toon;
namespace {
LayerAnimation curved(const QString &id) {
    LayerAnimation animation{id, {}, {
        {KeyframeProperty::PositionX, {{0, 0}, {20, 80}}},
        {KeyframeProperty::PositionY, {{0, 0}, {20, 0}}}}, {}};
    animation.channels[0].keys[0].spatial = SpatialTangents{{}, {0, 80}, true};
    animation.channels[0].keys[1].spatial = SpatialTangents{{0, 80}, {}, false};
    return animation;
}
Project fixture() { auto p = Project::create(); p.width = p.height = 128; p.transparent = true; p.shots[0].durationFrames = 30; return p; }
bool near(QPointF a, QPointF b, qreal epsilon = .00001) { return QLineF(a, b).length() < epsilon; }
}
class AdvancedMotionTests : public QObject {
    Q_OBJECT
private slots:
    void easingMatchesExistingPresetsAndStaysBounded() {
        for (auto mode : {KeyframeInterpolation::Linear, KeyframeInterpolation::EaseIn, KeyframeInterpolation::EaseOut, KeyframeInterpolation::EaseInOut}) {
            const auto easing = easingForInterpolation(mode); QVERIFY(validCubicEasing(easing));
            for (int step = 0; step <= 100; ++step) {
                const qreal t = step / 100.0;
                QVERIFY(qAbs(keyframeProgress(t, mode) - keyframeProgress(t, KeyframeInterpolation::Linear, easing)) < 1e-12);
            }
        }
        for (CubicEasing easing : {CubicEasing{{0, 0}, {0, 1}}, CubicEasing{{1, 0}, {1, 1}}, CubicEasing{{.1, 1}, {.9, 0}}}) {
            QVERIFY(validCubicEasing(easing)); qreal previous = -1;
            for (int step = 0; step <= 100; ++step) {
                const qreal progress = keyframeProgress(step / 100.0, KeyframeInterpolation::Linear, easing);
                QVERIFY(std::isfinite(progress)); QVERIFY(progress >= 0 && progress <= 1); QVERIFY(progress >= previous); previous = progress;
            }
        }
        QVERIFY(!validCubicEasing({{.9, 0}, {.1, 1}}));
        QVERIFY(!validCubicEasing({{0, -1}, {1, 1}}));
        QVERIFY(!validCubicEasing({{qQNaN(), 0}, {1, 1}}));
    }
    void cubicPositionUsesTemporalProgressAndExactEndpoints() {
        auto p = fixture(); auto &shot = p.shots[0]; const auto id = shot.layers[0].id;
        shot.layerAnimations = {curved(id)}; auto &animation = shot.layerAnimations[0];
        QVERIFY(validatePropertyAnimation(shot).isEmpty()); QVERIFY(positionChannelsPaired(animation));
        QCOMPARE(evaluateAnimatedPosition(animation, -1), QPointF());
        QCOMPARE(evaluateAnimatedPosition(animation, 10), QPointF(40, 60));
        QCOMPARE(evaluateAnimatedPosition(animation, 20), QPointF(80, 0));
        QCOMPARE(evaluateAnimatedPosition(animation, 100), QPointF(80, 0));
        QCOMPARE(evaluateProperty(shot, id, KeyframeProperty::PositionY, 10), 60.0);
        for (auto &channel : animation.channels) channel.keys[0].easing = easingForInterpolation(KeyframeInterpolation::EaseIn);
        QVERIFY(near(evaluateAnimatedPosition(animation, 10), {12.5, 45}));
        const auto source = shot.layerAnimations;
        const auto geometry = buildMotionPath(shot, id, {}, 256);
        for (const auto &sample : geometry.samples) QVERIFY(near(sample.position, animationPivotWorld(shot, id, sample.frame)));
        QCOMPARE(shot.layerAnimations, source);
    }
    void orientationKeepsRotationOffsetAndStableStationaryDirection() {
        auto p = fixture(); auto &shot = p.shots[0]; const auto id = shot.layers[0].id;
        shot.layerAnimations = {curved(id)}; auto &animation = shot.layerAnimations[0]; animation.orientAlongPath = true;
        animation.channels.append({KeyframeProperty::Rotation, {{0, 30}}});
        const auto first = evaluateLayerTransform(animation, 0);
        const QPointF axis = first.map(QPointF(1, 0)) - first.map(QPointF());
        QVERIFY(near(axis, {-.5, std::sqrt(3.0) / 2}));
        QVERIFY(animatedPositionDirection(animation, 20).y() < 0);
        auto stationary = animation;
        stationary.channels[0].keys.append({25, 80}); stationary.channels[1].keys.append({25, 0});
        const auto retained = animatedPositionDirection(stationary, 23);
        QVERIFY(retained.y() < 0); QVERIFY(qAbs(retained.x()) < .00001);
        for (auto &channel : animation.channels) if (channel.property != KeyframeProperty::Rotation) channel.keys[0].interpolation = KeyframeInterpolation::Hold;
        QVERIFY(animatedPositionDirection(animation, 5).y() > 0);
        animation.channels = {{KeyframeProperty::PositionX, {{0, 0}, {10, 0}, {20, 0}}},
            {KeyframeProperty::PositionY, {{0, 0}, {10, 100}, {20, 100}}}};
        QVERIFY(animatedPositionDirection(animation, 15).y() > 0);
        QVERIFY(animatedPositionDirection(animation, 100).y() > 0);
        auto parent = RasterLayer::create("Parent"); parent.group = true; shot.layers[0].parentId = parent.id; shot.layers.append(parent);
        shot.layerAnimations.append({parent.id, {}, {{KeyframeProperty::Rotation, {{0, 90}}}}, {}});
        const auto world = worldLayerTransform(shot, id, 15); const auto worldAxis = world.map(QPointF(1, 0)) - world.map(QPointF());
        QVERIFY(near(worldAxis, {-1, 0}));
        for (const auto &sample : buildMotionPath(shot, id, {}, 64).samples)
            QVERIFY(near(sample.position, animationPivotWorld(shot, id, sample.frame)));
    }
    void independentChannelTimingDoesNotRotateTowardInactiveMotion() {
        LayerAnimation animation; animation.orientAlongPath = true;
        animation.channels = {{KeyframeProperty::PositionX, {{10, 0}, {20, 100}}},
            {KeyframeProperty::PositionY, {{0, 0}, {10, 100}}}};
        auto direction = animatedPositionDirection(animation, 5);
        QCOMPARE(direction.x(), 0.0); QVERIFY(direction.y() > 0);
        direction = animatedPositionDirection(animation, 15);
        QVERIFY(direction.x() > 0); QCOMPARE(direction.y(), 0.0);
        direction = animatedPositionDirection(animation, -10);
        QCOMPARE(direction.x(), 0.0); QVERIFY(direction.y() > 0);
        direction = animatedPositionDirection(animation, 100);
        QVERIFY(direction.x() > 0); QCOMPARE(direction.y(), 0.0);
    }
    void insertedMorphKeyPreservesCustomEasingAndSource() {
        auto p = fixture(); auto &shot = p.shots[0]; auto &layer = shot.layers[0]; layer.kind = LayerKind::Vector;
        auto object = EditableObject::create(); object.nodes = {BezierNode::corner({0, 0}), BezierNode::corner({20, 20})}; layer.objects = {object};
        auto first = pathPose(object), last = first; last.nodes[1].anchor = {100, 60}; last.strokeWidth = 20;
        shot.layerAnimations = {{layer.id, {}, {}, {{object.id, {{0, first}, {20, last}}}}}};
        shot.layerAnimations[0].paths[0].keys[0].easing = CubicEasing{{.15, .8}, {.65, .9}};
        QVector<EditableObject> original;
        for (int frame = 0; frame <= 20; ++frame) original.append(evaluateAnimatedObject(shot, layer.id, object, frame));
        QString error; QVERIFY2(writePathKeyframe(shot, layer.id, object.id, 7, original[7], &error), qPrintable(error));
        QCOMPARE(shot.layerAnimations[0].paths[0].keys.size(), 3);
        for (int frame = 0; frame <= 20; ++frame) {
            const auto now = evaluateAnimatedObject(shot, layer.id, object, frame);
            QVERIFY(near(now.nodes[1].anchor, original[frame].nodes[1].anchor));
            QVERIFY(qAbs(now.strokeWidth - original[frame].strokeWidth) < .00001);
        }
        QCOMPARE(layer.objects[0], object);
        const auto linear = splitKeyframeEasing(.3, KeyframeInterpolation::Linear, {});
        QVERIFY(!linear.first && !linear.second);
        const auto hold = splitKeyframeEasing(.3, KeyframeInterpolation::Hold, {});
        QVERIFY(!hold.first && !hold.second);
    }
    void denseStationaryOrientedPathKeepsExactMarkersWithinSampleBudget() {
        auto p = fixture(); auto &shot = p.shots[0]; shot.durationFrames = 20000;
        LayerAnimation animation; animation.layerId = shot.layers[0].id; animation.pivot = {12, 14}; animation.orientAlongPath = true;
        PropertyChannel x{KeyframeProperty::PositionX, {}}, y{KeyframeProperty::PositionY, {}};
        for (int i = 0; i < maxPropertyKeyframes / 2; ++i) { x.keys.append({i * 2, 10}); y.keys.append({i * 2, 20}); }
        animation.channels = {x, y}; shot.layerAnimations = {animation}; QVERIFY(validatePropertyAnimation(shot).isEmpty());
        const auto markers = motionPositionKeySamples(shot, animation.layerId);
        QCOMPARE(markers.size(), maxPropertyKeyframes / 2);
        for (const auto &marker : markers) QCOMPARE(marker.position, QPointF(22, 34));
        const auto geometry = buildMotionPath(shot, animation.layerId, {}, 128);
        QVERIFY(geometry.samples.size() <= 128);
        for (const auto &sample : geometry.samples) QCOMPARE(sample.position, QPointF(22, 34));
    }
    void customMorphEasingAndReflectionValidation() {
        auto p = fixture(); auto &shot = p.shots[0]; auto &layer = shot.layers[0]; layer.kind = LayerKind::Vector;
        auto object = EditableObject::create(); object.nodes = {BezierNode::corner({0, 0}), BezierNode::corner({20, 20})}; layer.objects = {object};
        auto first = pathPose(object), last = first; last.nodes[1].anchor = {100, 100};
        shot.layerAnimations = {{layer.id, {}, {}, {{object.id, {{0, first}, {20, last}}}}}};
        auto &key = shot.layerAnimations[0].paths[0].keys[0]; key.easing = easingForInterpolation(KeyframeInterpolation::EaseIn);
        QCOMPARE(evaluateAnimatedObject(shot, layer.id, object, 10).nodes[1].anchor, QPointF(40, 40));
        shot.layerAnimations[0].paths[0].keys[1].pose.transform = QTransform::fromScale(-1, 1);
        key.interpolation = KeyframeInterpolation::Hold; QVERIFY(!validatePropertyAnimation(shot).isEmpty());
        key.easing.reset(); QVERIFY(validatePropertyAnimation(shot).isEmpty());
        QCOMPARE(layer.objects[0], object);
    }
    void invalidSpatialDataRejectedWithoutAffectingIndependentChannels() {
        auto p = fixture(); auto &shot = p.shots[0]; const auto id = shot.layers[0].id; shot.layerAnimations = {curved(id)};
        const auto original = shot.layerAnimations;
        shot.layerAnimations[0].channels[1].keys[1].frame = 19; QVERIFY(!validatePropertyAnimation(shot).isEmpty());
        shot.layerAnimations = original; shot.layerAnimations[0].channels[1].keys[0].easing = CubicEasing{}; QVERIFY(!validatePropertyAnimation(shot).isEmpty());
        shot.layerAnimations = original; shot.layerAnimations[0].channels[1].keys[0].spatial = SpatialTangents{}; QVERIFY(!validatePropertyAnimation(shot).isEmpty());
        shot.layerAnimations = original; shot.layerAnimations[0].channels[0].keys[1].spatial->incoming = {32768, 0}; QVERIFY(!validatePropertyAnimation(shot).isEmpty());
        shot.layerAnimations = original; for (auto &key : shot.layerAnimations[0].channels[0].keys) key.spatial.reset();
        shot.layerAnimations[0].channels[1].keys[1].frame = 19; QVERIFY(validatePropertyAnimation(shot).isEmpty());
    }
    void exportedFramesUseCurvesAndOrientationWithoutChangingSource() {
        auto p = fixture(); auto &shot = p.shots[0]; auto &layer = shot.layers[0];
        QImage tile(tileSize, tileSize, QImage::Format_ARGB32_Premultiplied); tile.fill(Qt::transparent);
        for (int y = 0; y < 6; ++y) for (int x = 0; x < 16; ++x) tile.setPixelColor(x, y, Qt::red);
        layer.tiles.insert({0, 0}, tile); const auto original = layer.tiles;
        shot.layerAnimations = {curved(layer.id)}; shot.layerAnimations[0].orientAlongPath = true;
        auto evaluated = shot.layers; QString error;
        QVERIFY2(applyLayerAnimations(shot, 10, evaluated, {0, 0, 128, 128}, &error), qPrintable(error));
        QCOMPARE(evaluated[0].renderTransform, evaluateLayerTransform(shot, layer.id, 10));
        const auto image = renderMovieFrame(p, 10); QVERIFY2(!image.isNull(), qPrintable(keyframeRenderError()));
        QCOMPARE(image.pixelColor(44, 62), QColor(Qt::red)); QCOMPARE(image.pixelColor(4, 2).alpha(), 0);
        const auto end = renderMovieFrame(p, 20); QVERIFY(!end.isNull());
        QCOMPARE(layer.tiles, original);
    }
};
QTEST_MAIN(AdvancedMotionTests)
#include "advanced_motion_tests.moc"
