#include "project.h"
#include "timeline_ripple.h"
#include <QtTest>
#include <limits>
using namespace toon;
namespace {
Project fixture() {
    auto project = Project::create(); project.width = project.height = 128;
    auto &shot = project.shots[0]; shot.durationFrames = 24;
    auto &layer = shot.layers[0];
    layer.effects.blurEnabled = true; layer.effects.blurRadius = 12;
    layer.effects.shadowOffset = {-12, 7}; layer.effects.shadowColor = QColor("#884422");
    layer.effects.glowColor = QColor("#55aabb"); layer.effects.exposure = .75;
    layer.mask = LayerMask{}; layer.mask->position = {20, -15}; layer.mask->pivot = {64, 32};
    layer.mask->scale = {.75, 1.25}; layer.mask->feather = 3.5; layer.mask->opacity = .8;
    QImage coverage(tileSize, tileSize, QImage::Format_Grayscale8); coverage.fill(97);
    layer.mask->tiles.insert({-1, 0}, coverage);
    return project;
}
}
class AnimatedCompositingTests : public QObject {
    Q_OBJECT
private slots:
    void registryPreservesExistingIdsAndRejectsInvalidValues() {
        QCOMPARE(int(KeyframeProperty::PositionX), 0); QCOMPARE(int(KeyframeProperty::Opacity), 5);
        QCOMPARE(keyframePropertyCount, 31);
        QSet<QString> keys;
        for (int i = 0; i < keyframePropertyCount; ++i) {
            const auto property = KeyframeProperty(i);
            QVERIFY(!keyframePropertyName(property).isEmpty());
            QVERIFY(!keyframePropertyKey(property).isEmpty()); keys.insert(keyframePropertyKey(property));
            QVERIFY(validKeyframeValue(property, keyframeDefaultValue(property)));
            QVERIFY(validKeyframeValue(property, keyframePropertyMinimum(property)));
            QVERIFY(validKeyframeValue(property, keyframePropertyMaximum(property)));
            QVERIFY(!validKeyframeValue(property, keyframePropertyMaximum(property) + 1));
            QVERIFY(!validKeyframeValue(property, std::numeric_limits<qreal>::quiet_NaN()));
            if (keyframePropertyIsDiscrete(property)) QVERIFY(!validKeyframeValue(property, .5));
        }
        QCOMPARE(keys.size(), keyframePropertyCount);
        QVERIFY(keyframePropertyKey(KeyframeProperty::Count).isEmpty());
        QVERIFY(!validKeyframeValue(KeyframeProperty(-1), 0));
        QVERIFY(!keyframePropertyNeedsPositionUnlock(KeyframeProperty::ShadowOffsetX));
        QVERIFY(keyframePropertyNeedsPositionUnlock(KeyframeProperty::MaskScaleY));
        QVERIFY(!keyframePropertyNeedsPositionUnlock(KeyframeProperty::MaskOpacity));
    }
    void channelCreationSeedsStoredValuesWithoutChangingSource() {
        auto project = fixture(); auto &shot = project.shots[0];
        const auto layer = shot.layers[0]; const auto id = layer.id; QString error;
        QVERIFY(writePropertyKeyframe(project, shot, id, KeyframeProperty::BlurRadius, 10, 32, &error));
        QVERIFY(writePropertyKeyframe(project, shot, id, KeyframeProperty::MaskPositionX, 10, 60, &error));
        QCOMPARE(propertyChannel(shot, id, KeyframeProperty::BlurRadius)->keys[0].value, 12.0);
        QCOMPARE(evaluateProperty(shot, id, KeyframeProperty::BlurRadius, 5), 22.0);
        QCOMPARE(evaluateProperty(shot, id, KeyframeProperty::MaskPositionX, 5), 40.0);
        QCOMPARE(evaluateProperty(shot, id, KeyframeProperty::Exposure, 5), .75);
        QCOMPARE(evaluateProperty(shot, id, KeyframeProperty::MaskPositionY, 5), -15.0);
        QCOMPARE(evaluateProperty(shot, id, KeyframeProperty::ScaleX, 5), 1.0);
        QCOMPARE(shot.layers[0].effects, layer.effects); QCOMPARE(shot.layers[0].mask, layer.mask);
        auto effects = evaluateAnimatedLayerEffects(shot, id, 5, layer.effects);
        QCOMPARE(effects.blurRadius, 22.0); QCOMPARE(effects.shadowColor, layer.effects.shadowColor);
        QCOMPARE(effects.glowColor, layer.effects.glowColor); QCOMPARE(effects.exposure, .75);
        auto mask = evaluateAnimatedLayerMask(shot, id, 5, *layer.mask);
        QCOMPARE(mask.position, QPointF(40, -15)); QCOMPARE(mask.pivot, layer.mask->pivot);
        QCOMPARE(mask.tiles, layer.mask->tiles); QCOMPARE(mask.feather, 3.5);
        const auto before = shot.layerAnimations;
        QVERIFY(!writePropertyKeyframe(project, shot, id, KeyframeProperty::BlurRadius, 10, 129, &error));
        QVERIFY(!writePropertyKeyframe(project, shot, id, KeyframeProperty::MaskEnabled, 10, .5, &error));
        QCOMPARE(shot.layerAnimations, before);
    }
    void everyCompositingBaseSetterPreservesPixelsAndUnrelatedFields() {
        const auto source = fixture().shots[0].layers[0];
        for (int i = 6; i < keyframePropertyCount; ++i) {
            const auto property = KeyframeProperty(i); auto layer = source;
            const auto value = keyframePropertyIsDiscrete(property) ? 1.0 - keyframeSourceValue(layer, property)
                : (keyframePropertyMinimum(property) + keyframePropertyMaximum(property)) / 3;
            QVERIFY(setKeyframeSourceValue(layer, property, value));
            QCOMPARE(keyframeSourceValue(layer, property), value);
            QCOMPARE(layer.mask->tiles, source.mask->tiles); QCOMPARE(layer.mask->pivot, source.mask->pivot);
            QCOMPARE(layer.effects.shadowColor, source.effects.shadowColor);
            QCOMPARE(layer.effects.glowColor, source.effects.glowColor);
        }
        auto layer = source;
        QVERIFY(!setKeyframeSourceValue(layer, KeyframeProperty::PositionX, 10));
        QVERIFY(!setKeyframeSourceValue(layer, KeyframeProperty::MaskOpacity, -1));
        QCOMPARE(layer.effects, source.effects); QCOMPARE(layer.mask, source.mask);
        layer.mask.reset(); QVERIFY(!setKeyframeSourceValue(layer, KeyframeProperty::MaskEnabled, 1));
        QVERIFY(!layer.mask);
    }
    void enableAndInvertAreHeldAndRejectEasingOrSpatialMetadata() {
        auto project = fixture(); auto &shot = project.shots[0]; const auto id = shot.layers[0].id;
        for (const auto property : {KeyframeProperty::EffectsEnabled, KeyframeProperty::BlurEnabled,
                KeyframeProperty::ShadowEnabled, KeyframeProperty::GlowEnabled, KeyframeProperty::ColorEnabled,
                KeyframeProperty::MaskEnabled, KeyframeProperty::MaskInverted}) {
            const qreal initial = keyframeSourceValue(shot.layers[0], property);
            QVERIFY(writePropertyKeyframe(project, shot, id, property, 10, 1 - initial));
            QCOMPARE(evaluateProperty(shot, id, property, 9.999), initial);
            QCOMPARE(evaluateProperty(shot, id, property, 10), 1 - initial);
            const auto *channel = propertyChannel(shot, id, property);
            QCOMPARE(channel->keys[0].interpolation, KeyframeInterpolation::Hold);
            QCOMPARE(channel->keys[1].interpolation, KeyframeInterpolation::Hold);
        }
        QVERIFY2(validatePropertyAnimation(shot).isEmpty(), qPrintable(validatePropertyAnimation(shot)));
        auto altered = shot; altered.layerAnimations[0].channels[0].keys[0].interpolation = KeyframeInterpolation::Linear;
        QVERIFY(!validatePropertyAnimation(altered).isEmpty());
        altered = shot; altered.layerAnimations[0].channels[0].keys[0].easing = CubicEasing{};
        QVERIFY(!validatePropertyAnimation(altered).isEmpty());
        altered = shot; altered.layerAnimations[0].channels[0].keys[0].spatial = SpatialTangents{};
        QVERIFY(!validatePropertyAnimation(altered).isEmpty());
    }
    void onlyExistingMasksAndSupportedAdjustmentPropertiesCanAnimate() {
        auto project = fixture(); auto &shot = project.shots[0]; const auto id = shot.layers[0].id;
        QVERIFY(writePropertyKeyframe(project, shot, id, KeyframeProperty::MaskOpacity, 10, .2));
        QVERIFY(writePropertyKeyframe(project, shot, id, KeyframeProperty::Exposure, 10, 2));
        shot.layers[0].mask.reset(); QVERIFY(!validatePropertyAnimation(shot).isEmpty());
        prunePropertyAnimation(shot);
        QVERIFY(!propertyChannel(shot, id, KeyframeProperty::MaskOpacity));
        QVERIFY(propertyChannel(shot, id, KeyframeProperty::Exposure));
        QVERIFY(!writePropertyKeyframe(project, shot, id, KeyframeProperty::MaskEnabled, 10, 1));
        shot.layers[0].effects = {}; shot.layers[0].adjustment = true;
        QVERIFY(keyframePropertyUnavailableReason(shot, id, KeyframeProperty::Exposure).isEmpty());
        QVERIFY(!writePropertyKeyframe(project, shot, id, KeyframeProperty::BlurEnabled, 10, 1));
        QVERIFY(writePropertyKeyframe(project, shot, id, KeyframeProperty::ColorEnabled, 10, 1));
    }
    void rippleMovesEffectAndMaskKeysWithHeldStatesIntact() {
        auto project = fixture(); auto &shot = project.shots[0]; const auto id = shot.layers[0].id;
        QVERIFY(writePropertyKeyframe(project, shot, id, KeyframeProperty::BlurRadius, 10, 32));
        QVERIFY(writePropertyKeyframe(project, shot, id, KeyframeProperty::MaskInverted, 10, 1));
        const auto original = shot.layerAnimations;
        QVERIFY(applyTimelineRipple(shot, project.fps, {TimelineRippleKind::Insert, 5, 3}).isEmpty());
        QCOMPARE(propertyChannel(shot, id, KeyframeProperty::BlurRadius)->keys.last().frame, 13);
        QCOMPARE(evaluateProperty(shot, id, KeyframeProperty::MaskInverted, 12.99), 0.0);
        QCOMPARE(evaluateProperty(shot, id, KeyframeProperty::MaskInverted, 13), 1.0);
        QVERIFY(applyTimelineRipple(shot, project.fps, {TimelineRippleKind::Delete, 5, 3}).isEmpty());
        QCOMPARE(shot.layerAnimations, original);
        QVERIFY(applyTimelineRipple(shot, project.fps, {TimelineRippleKind::Move, 10, 2, 18}).isEmpty());
        QCOMPARE(propertyChannel(shot, id, KeyframeProperty::MaskInverted)->keys.last().frame, 16);
        QVERIFY2(validatePropertyAnimation(shot).isEmpty(), qPrintable(validatePropertyAnimation(shot)));
    }
};
QTEST_MAIN(AnimatedCompositingTests)
#include "animated_compositing_tests.moc"
