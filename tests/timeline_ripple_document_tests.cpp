#include "document.h"
#include "timeline_ripple.h"
#include "animation_test_fixture.h"
#include <QtTest>

using namespace toon;
class TimelineRippleDocumentTests : public QObject {
    Q_OBJECT
private slots:
    void editIsOneUndoAndRetainsSourcePixels() {
        Document document; const auto project = animation_fixture::project(); document.load(project);
        QVERIFY(document.setCurrentFrame(5));
        const auto sourceId = project.shots[0].animationTracks[0].drawings[1].templateLayers["ink"];
        const int originalIndex = animation_fixture::layerIndex(project.shots[0], sourceId);
        const auto pixels = project.shots[0].layers[originalIndex].tiles;
        QVERIFY(document.rippleTimeline({TimelineRippleKind::Insert, 2, 4}));
        QCOMPARE(document.history()->count(), 1); QCOMPARE(document.currentFrame(), 9);
        const auto inserted = document.project();
        const int movedIndex = animation_fixture::layerIndex(inserted.shots[0], sourceId);
        QCOMPARE(inserted.shots[0].layers[movedIndex].tiles[TileKey(0, 0)].cacheKey(), pixels[TileKey(0, 0)].cacheKey());
        document.history()->undo(); QCOMPARE(document.project().toJson(), project.toJson());
        document.history()->redo(); QCOMPARE(document.project().toJson(), inserted.toJson());
    }

    void deletingLastExposureKeepsReusableDrawingAndUndoRestoresTiming() {
        Document document; const auto project = animation_fixture::project(); document.load(project);
        QVERIFY(document.setCurrentFrame(5));
        const auto sourceId = project.shots[0].animationTracks[0].drawings[1].templateLayers["ink"];
        document.selectLayer(animation_fixture::layerIndex(project.shots[0], sourceId));
        QVERIFY(document.rippleTimeline({TimelineRippleKind::Delete, 4, 3}));
        QCOMPARE(document.currentFrame(), 4);
        QCOMPARE(document.project().shots[0].animationTracks[0].drawings.size(), 2);
        const auto storedIndex = animation_fixture::layerIndex(document.project().shots[0], sourceId);
        QVERIFY(storedIndex >= 0);
        QCOMPARE(document.activeShot().layers[storedIndex].tiles,
                 project.shots[0].layers[animation_fixture::layerIndex(project.shots[0], sourceId)].tiles);
        QVERIFY2(document.project().validate().isEmpty(), qPrintable(document.project().validate()));
        document.history()->undo(); QCOMPARE(document.project().toJson(), project.toJson());
        document.history()->redo();
        const auto &track = project.shots[0].animationTracks[0];
        QVERIFY(document.exposeAnimationDrawing(track.id, track.drawings[1].id, 8, 3));
        QCOMPARE(exposureAtFrame(document.activeShot().animationTracks[0], 8)->drawingId, track.drawings[1].id);
        QCOMPARE(document.activeShot().layers.size(), project.shots[0].layers.size());
    }

    void rejectedAndIdentityEditsDoNotEnterHistory() {
        Document document; const auto project = animation_fixture::project(); document.load(project);
        bool changed = true;
        QVERIFY(document.rippleTimeline({TimelineRippleKind::Move, 4, 3, 7}, &changed));
        QVERIFY(!changed);
        QCOMPARE(document.history()->count(), 0);
        changed = true;
        QVERIFY(!document.rippleTimeline({TimelineRippleKind::Delete, 0, 20}, &changed));
        QVERIFY(!changed);
        QCOMPARE(document.project().toJson(), project.toJson());
        QCOMPARE(document.history()->count(), 0);
        auto locked = project; locked.shots[0].layers.last().locked = true; document.load(locked);
        QVERIFY(!document.rippleTimeline({TimelineRippleKind::Move, 4, 3, 0}));
        QCOMPARE(document.project().toJson(), locked.toJson());
        QCOMPARE(document.history()->count(), 0);
        document.load(project);
        QVERIFY(document.rippleTimeline({TimelineRippleKind::Insert, 2, 1}, &changed));
        QVERIFY(changed);
    }

    void activeGestureBlocksRippleWithoutResolvingDraft() {
        Document document; const auto project = animation_fixture::project(); document.load(project);
        const auto id = project.shots[0].layers[0].id;
        QVERIFY(document.beginKeyframeEdit(id));
        QVERIFY(document.previewKeyframeValue(KeyframeProperty::PositionX, 40));
        const auto preview = document.project().toJson();
        QVERIFY(!document.rippleTimeline({TimelineRippleKind::Insert, 2, 4}));
        QVERIFY(document.keyframeEditActive());
        QCOMPARE(document.project().toJson(), preview);
        document.cancelKeyframeEdit(); QCOMPARE(document.project().toJson(), project.toJson());
    }

    void removingFollowPathMotionRestoresSourceAndUndoRestoresHandles() {
        auto project = Project::create(); auto &shot = project.shots[0]; shot.durationFrames = 20;
        ScalarKeyframe x{2, 25}; x.spatial = SpatialTangents{{-1, 0}, {12, 8}, true};
        ScalarKeyframe y{2, 35};
        shot.layerAnimations = {{shot.layers[0].id, {}, {{KeyframeProperty::PositionX, {x}},
            {KeyframeProperty::PositionY, {y}}, {KeyframeProperty::Opacity, {{12, .5}}}}, {}, true}};
        Document document; document.load(project);
        QVERIFY(document.rippleTimeline({TimelineRippleKind::Delete, 1, 4}));
        QCOMPARE(document.history()->count(), 1);
        const auto &animation = document.project().shots[0].layerAnimations[0];
        QVERIFY(!animation.orientAlongPath);
        QCOMPARE(animation.channels.size(), 1);
        QCOMPARE(evaluateLayerTransform(animation, 8), QTransform{});
        document.history()->undo(); QCOMPARE(document.project().toJson(), project.toJson());
        QCOMPARE(document.project().shots[0].layerAnimations[0].channels[0].keys[0].spatial, x.spatial);
    }

    void symbolTimingIsOneUndoAndRejectedSplitDoesNotEnterHistory() {
        auto project = Project::create();
        SymbolDefinition symbol{animation_fixture::id(), "Blink", Project::create().shots[0]};
        symbol.content.durationFrames = 20; project.symbols.append(symbol);
        auto &shot = project.shots[0]; auto &layer = shot.layers[0];
        layer.kind = LayerKind::Symbol; layer.symbol = SymbolInstance{symbol.id, SymbolPlayback::Loop, 20, 3, .5};
        shot.layerAnimations = {{layer.id, {}, {{KeyframeProperty::PositionX, {{20, 0}, {50, 30}}}}}};
        QVERIFY2(project.validate().isEmpty(), qPrintable(project.validate()));
        Document document; document.load(project); QVERIFY(document.setCurrentFrame(21));
        QVERIFY(document.rippleTimeline({TimelineRippleKind::Insert, 10, 12}));
        QCOMPARE(document.history()->count(), 1); QCOMPARE(document.currentFrame(), 33);
        QCOMPARE(document.activeShot().layers[0].symbol->startFrame, 32);
        const auto inserted = document.project().toJson();
        document.history()->undo(); QCOMPARE(document.project().toJson(), project.toJson());
        document.history()->redo(); QCOMPARE(document.project().toJson(), inserted);
        bool changed = true;
        QVERIFY(!document.rippleTimeline({TimelineRippleKind::Delete, 40, 4}, &changed));
        QVERIFY(!changed); QCOMPARE(document.history()->count(), 1);
        QCOMPARE(document.project().toJson(), inserted);
    }
};
QTEST_MAIN(TimelineRippleDocumentTests)
#include "timeline_ripple_document_tests.moc"
