#include "document.h"
#include <QtTest>
using namespace toon;
namespace {
Project fixture() {
    auto project = Project::create(); project.width = project.height = 128;
    auto &layer = project.shots[0].layers[0];
    layer.effects.blurEnabled = true; layer.effects.blurRadius = 12;
    layer.effects.exposure = .75; layer.mask = LayerMask{};
    layer.mask->position = {20, -15}; layer.mask->opacity = .8;
    return project;
}
TimelineKeyRef ref(const QString &id, KeyframeProperty property, int frame) { return {id, property, {}, frame}; }
}
class AnimatedCompositingDocumentTests : public QObject {
    Q_OBJECT
private slots:
    void previewCommitCancelAndChannelRemovalPreserveStaticBases() {
        const auto project = fixture(); const auto id = project.shots[0].layers[0].id;
        Document document; document.load(project); QVERIFY(document.setCurrentFrame(10));
        QVERIFY(document.beginKeyframeEdit(id));
        QVERIFY(document.previewKeyframeValue(KeyframeProperty::BlurRadius, 24));
        QVERIFY(document.previewKeyframeValue(KeyframeProperty::BlurRadius, 32));
        QVERIFY(document.previewKeyframeValue(KeyframeProperty::MaskPositionX, 60));
        QCOMPARE(document.history()->count(), 0);
        QCOMPARE(document.activeShot().layers[0].effects, project.shots[0].layers[0].effects);
        QCOMPARE(document.activeShot().layers[0].mask, project.shots[0].layers[0].mask);
        document.commitKeyframeEdit(); QCOMPARE(document.history()->count(), 1);
        const auto edited = document.project().toJson();
        QCOMPARE(evaluateProperty(document.activeShot(), id, KeyframeProperty::BlurRadius, 5), 22.0);
        document.history()->undo(); QCOMPARE(document.project().toJson(), project.toJson());
        document.history()->redo(); QCOMPARE(document.project().toJson(), edited);
        QVERIFY(document.beginKeyframeEdit(id));
        QVERIFY(document.previewKeyframeValue(KeyframeProperty::BlurRadius, 100));
        document.cancelKeyframeEdit(); QCOMPARE(document.project().toJson(), edited);
        QVERIFY(document.enableKeyframeChannel(id, KeyframeProperty::BlurRadius, false));
        QCOMPARE(evaluateProperty(document.activeShot(), id, KeyframeProperty::BlurRadius, 10), 12.0);
        document.history()->undo(); QCOMPARE(document.project().toJson(), edited);
    }
    void enablingUsesExistingSettingsAndDiscreteKeysCannotEase() {
        Document document; document.load(fixture()); const auto id = document.activeShot().layers[0].id;
        QVERIFY(document.setCurrentFrame(10));
        QVERIFY(document.enableKeyframeChannel(id, KeyframeProperty::Exposure));
        QCOMPARE(propertyChannel(document.activeShot(), id, KeyframeProperty::Exposure)->keys[0].value, .75);
        QCOMPARE(propertyChannel(document.activeShot(), id, KeyframeProperty::Exposure)->keys[1].value, .75);
        QVERIFY(document.enableKeyframeChannel(id, KeyframeProperty::BlurEnabled));
        QVERIFY(document.setKeyframeValue(id, KeyframeProperty::BlurEnabled, 0));
        const auto before = document.project().toJson(); const auto history = document.history()->count();
        QCOMPARE(evaluateProperty(document.activeShot(), id, KeyframeProperty::BlurEnabled, 9.99), 1.0);
        QCOMPARE(evaluateProperty(document.activeShot(), id, KeyframeProperty::BlurEnabled, 10), 0.0);
        QVERIFY(!document.setKeyframeInterpolation(id, KeyframeProperty::BlurEnabled, 0, KeyframeInterpolation::EaseIn));
        QVERIFY(document.beginKeyframeEdit(id));
        QVERIFY(!document.previewKeyframeEasing(KeyframeProperty::BlurEnabled, 0, CubicEasing{}));
        document.cancelKeyframeEdit(); QCOMPARE(document.project().toJson(), before);
        QCOMPARE(document.history()->count(), history);
    }
    void locksDistinguishMaskMotionFromEffectsAndStrength() {
        Document document; document.load(fixture()); const auto id = document.activeShot().layers[0].id;
        document.setLayerPositionLocked(true);
        QVERIFY(document.setKeyframeValue(id, KeyframeProperty::BlurRadius, 30));
        QVERIFY(document.setKeyframeValue(id, KeyframeProperty::ShadowOffsetX, 20));
        QVERIFY(document.setKeyframeValue(id, KeyframeProperty::MaskOpacity, .5));
        QVERIFY(!document.setKeyframeValue(id, KeyframeProperty::MaskPositionX, 30));
        QVERIFY(!document.enableKeyframeChannel(id, KeyframeProperty::MaskScaleX));
        document.setLayersLocked({id}, true);
        QVERIFY(!document.setKeyframeValue(id, KeyframeProperty::BlurRadius, 40));
        QVERIFY(!document.setKeyframeValue(id, KeyframeProperty::MaskOpacity, .7));
    }
    void removingMaskPrunesItsAnimationAndUndoRestoresBoth() {
        Document document; document.load(fixture()); const auto id = document.activeShot().layers[0].id;
        QVERIFY(document.setKeyframeValue(id, KeyframeProperty::BlurRadius, 30));
        QVERIFY(document.setKeyframeValue(id, KeyframeProperty::MaskPositionX, 30));
        QVERIFY(document.setKeyframeValue(id, KeyframeProperty::MaskEnabled, 0));
        const auto before = document.project().toJson();
        QVERIFY(document.removeLayerMask());
        QVERIFY(!document.activeShot().layers[0].mask);
        QVERIFY(!propertyChannel(document.activeShot(), id, KeyframeProperty::MaskPositionX));
        QVERIFY(!propertyChannel(document.activeShot(), id, KeyframeProperty::MaskEnabled));
        QVERIFY(propertyChannel(document.activeShot(), id, KeyframeProperty::BlurRadius));
        document.history()->undo(); QCOMPARE(document.project().toJson(), before);
        QVERIFY(document.activeShot().layers[0].mask);
    }
    void effectsPadCombinesKeyedAndStaticAxesInOneUndo() {
        auto project = fixture(); auto &layer = project.shots[0].layers[0];
        layer.effects.shadowEnabled = true; layer.effects.shadowOffset = {-12, 7};
        const auto id = layer.id; Document document; document.load(project);
        QVERIFY(document.setCurrentFrame(10));
        QVERIFY(document.setKeyframeValue(id, KeyframeProperty::ShadowOffsetX, 40));
        const auto original = document.project().toJson(); const int count = document.history()->count();
        auto draft = evaluateAnimatedLayerEffects(document.activeShot(), id, 10, document.activeShot().layers[0].effects);
        QCOMPARE(draft.shadowOffset, QPointF(40, 7));
        QVERIFY(document.beginEffectsEdit(id, "Move shadow", true));
        draft.shadowOffset = {50, 25}; QVERIFY(document.previewLayerEffects(draft));
        draft.shadowOffset = {60, 30}; QVERIFY(document.previewLayerEffects(draft));
        QCOMPARE(document.history()->count(), count);
        QCOMPARE(document.activeShot().layers[0].effects.shadowOffset, QPointF(-12, 30));
        QCOMPARE(evaluateProperty(document.activeShot(), id, KeyframeProperty::ShadowOffsetX, 10), 60.0);
        QVERIFY(!propertyChannel(document.activeShot(), id, KeyframeProperty::ShadowOffsetY));
        document.commitEffectsEdit(); QCOMPARE(document.history()->count(), count + 1);
        const auto edited = document.project().toJson();
        document.history()->undo(); QCOMPARE(document.project().toJson(), original);
        document.history()->redo(); QCOMPARE(document.project().toJson(), edited);
    }
    void changingColorRetainsKeyedNumericBasesAndEvaluatedNoOps() {
        Document document; document.load(fixture()); const auto id = document.activeShot().layers[0].id;
        QVERIFY(document.setCurrentFrame(10));
        QVERIFY(document.setKeyframeValue(id, KeyframeProperty::BlurRadius, 40));
        QVERIFY(document.setKeyframeValue(id, KeyframeProperty::Exposure, 2));
        const auto keys = document.activeShot().layerAnimations;
        auto draft = evaluateAnimatedLayerEffects(document.activeShot(), id, 10, document.activeShot().layers[0].effects);
        draft.shadowColor = QColor("#aa3344");
        QVERIFY(document.setLayerEffects(id, draft, true));
        QCOMPARE(document.activeShot().layers[0].effects.blurRadius, 12.0);
        QCOMPARE(document.activeShot().layers[0].effects.exposure, .75);
        QCOMPARE(document.activeShot().layers[0].effects.shadowColor, QColor("#aa3344"));
        QCOMPARE(document.activeShot().layerAnimations, keys);
        const auto before = document.project().toJson(); const auto count = document.history()->count();
        QVERIFY(document.beginEffectsEdit(id, "Effects unchanged", true));
        QVERIFY(document.previewLayerEffects(draft)); QVERIFY(document.previewLayerEffects(draft));
        document.commitEffectsEdit(); QCOMPARE(document.history()->count(), count);
        QCOMPARE(document.project().toJson(), before);
        QVERIFY(document.beginEffectsEdit(id, "Cancel effects", true));
        draft.blurRadius = 80; draft.shadowColor = QColor("#55aacc");
        QVERIFY(document.previewLayerEffects(draft)); document.cancelEffectsEdit();
        QCOMPARE(document.project().toJson(), before); QCOMPARE(document.history()->count(), count);
    }
    void maskPaintChangesSourceCoverageWithoutBakingAnimatedPose() {
        auto project = fixture(); QImage pixels(tileSize, tileSize, QImage::Format_Grayscale8); pixels.fill(77);
        project.shots[0].layers[0].mask->tiles.insert({0, 0}, pixels);
        Document document; document.load(project); const auto id = document.activeShot().layers[0].id;
        QVERIFY(document.setCurrentFrame(10));
        QVERIFY(document.setKeyframeValue(id, KeyframeProperty::MaskPositionX, 90));
        QVERIFY(document.setKeyframeValue(id, KeyframeProperty::MaskOpacity, .3));
        const auto animation = document.activeShot().layerAnimations;
        const auto original = document.project().toJson(); const auto count = document.history()->count();
        auto source = *document.activeShot().layers[0].mask;
        source.tiles[{0, 0}].setPixelColor(12, 15, QColor(140, 140, 140));
        QVERIFY(document.beginEffectsEdit(id, "Paint mask")); // Pixel editing retains source fields.
        QVERIFY(document.previewLayerMask(source)); document.commitEffectsEdit();
        QCOMPARE(document.history()->count(), count + 1);
        QCOMPARE(document.activeShot().layers[0].mask->position, QPointF(20, -15));
        QCOMPARE(document.activeShot().layers[0].mask->opacity, .8);
        QCOMPARE(document.activeShot().layerAnimations, animation);
        QCOMPARE(document.activeShot().layers[0].mask->tiles.value({0, 0}).pixelColor(12, 15).red(), 140);
        document.history()->undo(); QCOMPARE(document.project().toJson(), original);
        QCOMPARE(document.activeShot().layers[0].mask->tiles.value({0, 0}), pixels);
    }
    void evaluatedMaskEditsAndHeldInvertPreserveCoverage() {
        auto project = fixture(); QImage pixels(tileSize, tileSize, QImage::Format_Grayscale8); pixels.fill(77);
        project.shots[0].layers[0].mask->tiles.insert({0, 0}, pixels);
        Document document; document.load(project); const auto id = document.activeShot().layers[0].id;
        QVERIFY(document.setCurrentFrame(10));
        QVERIFY(document.setKeyframeValue(id, KeyframeProperty::MaskPositionX, 90));
        QVERIFY(document.enableKeyframeChannel(id, KeyframeProperty::MaskInverted));
        const auto count = document.history()->count();
        auto draft = evaluateAnimatedLayerMask(document.activeShot(), id, 10, *document.activeShot().layers[0].mask);
        draft.position = {100, 25}; draft.feather = 5;
        QVERIFY(document.beginEffectsEdit(id, "Mask settings", true));
        QVERIFY(document.previewLayerMask(draft)); document.commitEffectsEdit();
        QCOMPARE(document.history()->count(), count + 1);
        QCOMPARE(document.activeShot().layers[0].mask->position, QPointF(20, 25));
        QCOMPARE(evaluateProperty(document.activeShot(), id, KeyframeProperty::MaskPositionX, 10), 100.0);
        QCOMPARE(document.activeShot().layers[0].mask->feather, 5.0);
        QVERIFY(document.invertLayerMask(true));
        const auto &mask = *document.activeShot().layers[0].mask;
        QVERIFY(!mask.inverted); QCOMPARE(mask.defaultValue, 255); QCOMPARE(mask.tiles.value({0, 0}), pixels);
        QCOMPARE(evaluateProperty(document.activeShot(), id, KeyframeProperty::MaskInverted, 9.99), 0.0);
        QCOMPARE(evaluateProperty(document.activeShot(), id, KeyframeProperty::MaskInverted, 10), 1.0);
        QCOMPARE(propertyChannel(document.activeShot(), id, KeyframeProperty::MaskInverted)->keys.last().interpolation,
                 KeyframeInterpolation::Hold);
        const auto inverted = document.project().toJson();
        QVERIFY(document.beginEffectsEdit(id, "Cancel mask", true));
        draft = evaluateAnimatedLayerMask(document.activeShot(), id, 10, *document.activeShot().layers[0].mask);
        draft.position.setX(120); draft.opacity = .2;
        QVERIFY(document.previewLayerMask(draft)); document.cancelEffectsEdit();
        QCOMPARE(document.project().toJson(), inverted);
    }
    void lockedMaskPoseRejectsWholeDraftButAllowsStrength() {
        Document document; document.load(fixture()); const auto id = document.activeShot().layers[0].id;
        QVERIFY(document.setCurrentFrame(10));
        QVERIFY(document.setKeyframeValue(id, KeyframeProperty::MaskPositionX, 90));
        document.setLayerPositionLocked(true);
        const auto before = document.project().toJson(); const auto count = document.history()->count();
        auto draft = evaluateAnimatedLayerMask(document.activeShot(), id, 10, *document.activeShot().layers[0].mask);
        QVERIFY(document.beginEffectsEdit(id, "Locked mask", true));
        draft.position.setX(100); draft.feather = 30;
        QVERIFY(!document.previewLayerMask(draft));
        QCOMPARE(document.project().toJson(), before);
        document.commitEffectsEdit(); QCOMPARE(document.history()->count(), count);
        draft = evaluateAnimatedLayerMask(document.activeShot(), id, 10, *document.activeShot().layers[0].mask);
        draft.opacity = .4;
        QVERIFY(document.beginEffectsEdit(id, "Mask strength", true));
        QVERIFY(document.previewLayerMask(draft)); document.commitEffectsEdit();
        QCOMPARE(document.activeShot().layers[0].mask->opacity, .4);
        QCOMPARE(evaluateProperty(document.activeShot(), id, KeyframeProperty::MaskPositionX, 10), 90.0);
    }
    void newMaskHasArtworkCenteredPivotAndUsesFormat36() {
        auto project = Project::create(); project.width = project.height = 128;
        QImage pixels(tileSize, tileSize, QImage::Format_ARGB32_Premultiplied); pixels.fill(Qt::transparent);
        for (int y = 20; y < 40; ++y) for (int x = 10; x < 50; ++x) pixels.setPixelColor(x, y, Qt::red);
        project.shots[0].layers[0].tiles.insert({0, 0}, pixels);
        Document document; document.load(project); QVERIFY(document.addLayerMask());
        QCOMPARE(document.activeShot().layers[0].mask->pivot, QPointF(30, 30));
        QCOMPARE(document.project().toJson()["version"].toInt(), 36);
        document.history()->undo(); QVERIFY(!document.activeShot().layers[0].mask);
    }
    void mixedTimelineEditingIsAtomicAndMissingMasksRejectClipboard() {
        Document document; document.load(fixture()); const auto id = document.activeShot().layers[0].id;
        QVERIFY(document.setCurrentFrame(10));
        QVERIFY(document.setKeyframeValue(id, KeyframeProperty::BlurRadius, 30));
        QVERIFY(document.setKeyframeValue(id, KeyframeProperty::MaskPositionX, 60));
        QVERIFY(document.setKeyframeValue(id, KeyframeProperty::MaskEnabled, 0));
        const QVector<TimelineKeyRef> selection{
            ref(id, KeyframeProperty::BlurRadius, 10), ref(id, KeyframeProperty::MaskPositionX, 10),
            ref(id, KeyframeProperty::MaskEnabled, 10)};
        TimelineKeyClipboard clipboard; QVERIFY(document.copyTimelineKeys(selection, &clipboard));
        const auto before = document.project().toJson();
        QVERIFY(!document.moveTimelineKeys(selection, -10)); // Occupied source-seeding keys.
        QCOMPARE(document.project().toJson(), before);
        QVERIFY(document.moveTimelineKeys(selection, 5));
        QCOMPARE(propertyChannel(document.activeShot(), id, KeyframeProperty::MaskEnabled)->keys.last().frame, 15);
        document.history()->undo(); QCOMPARE(document.project().toJson(), before);
        QVERIFY(document.pasteTimelineKeys(clipboard, 20));
        QCOMPARE(propertyChannel(document.activeShot(), id, KeyframeProperty::MaskEnabled)->keys.last().interpolation,
                 KeyframeInterpolation::Hold);
        QVERIFY(document.removeTimelineKeys({ref(id, KeyframeProperty::BlurRadius, 20),
            ref(id, KeyframeProperty::MaskPositionX, 20), ref(id, KeyframeProperty::MaskEnabled, 20)}));
        QCOMPARE(document.project().toJson(), before);
        document.setLayerPositionLocked(true);
        QVERIFY(!document.moveTimelineKeys(selection, 5)); // Atomic: mask movement is position locked.
        QVERIFY(document.moveTimelineKeys({ref(id, KeyframeProperty::BlurRadius, 10)}, 5));
        document.setLayerPositionLocked(false); QVERIFY(document.removeLayerMask());
        const auto removed = document.project().toJson();
        QVERIFY(!document.pasteTimelineKeys(clipboard, 30));
        QCOMPARE(document.project().toJson(), removed);
    }
};
QTEST_MAIN(AnimatedCompositingDocumentTests)
#include "animated_compositing_document_tests.moc"
