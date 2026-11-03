#include "animation_test_fixture.h"
#include "canvas.h"
#include "document.h"
#include "timeline_controller.h"
#include "ui_theme.h"
#include <QtTest>

using namespace toon;
namespace {
Project fixture() {
    auto project = animation_fixture::project();
    auto other = animation_fixture::project();
    auto &shot = project.shots[0]; shot.durationFrames = 80;
    for (int i = 1; i < other.shots[0].layers.size(); ++i) shot.layers.append(other.shots[0].layers[i]);
    shot.animationTracks.append(other.shots[0].animationTracks[0]);
    const auto ink = shot.animationTracks[0].drawings[0].templateLayers.value("ink");
    shot.layerAnimations = {{ink, {}, {{KeyframeProperty::PositionX, {{0, 0}, {2, 8}}}}}};
    return project;
}
TimelineCelRef ref(const Shot &shot, int track, int exposure = 0) {
    return {shot.animationTracks[track].id, shot.animationTracks[track].exposures[exposure].id};
}
void verifySameLayers(const Canvas &canvas, const QVector<RasterLayer> &original) {
    QCOMPARE(canvas.shot().layers.size(), original.size());
    for (int i = 0; i < original.size(); ++i) {
        const auto &layer = canvas.shot().layers[i];
        QCOMPARE(layer.id, original[i].id); QCOMPARE(layer.parentId, original[i].parentId);
        QCOMPARE(layer.name, original[i].name); QCOMPARE(layer.kind, original[i].kind);
        QCOMPARE(layer.tiles, original[i].tiles);
    }
}
}
class CelControllerTests final : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { applyTheme(); }
    void selectionSpansTracksSurvivesNavigationAndExcludesKeysAndCameras() {
        Canvas canvas; TimelineController controller(&canvas); canvas.setProject(fixture());
        const auto a = ref(canvas.shot(), 0), b = ref(canvas.shot(), 1);
        const auto before = canvas.project().toJson();
        controller.setSelectedCels({a, b, a, {a.trackId, "missing"}});
        QCOMPARE(controller.selectedCels(), QVector<TimelineCelRef>({a, b}));
        QVERIFY(controller.selectTrack(b.trackId)); QVERIFY(controller.setFrame(5));
        QCOMPARE(controller.selectedCels(), QVector<TimelineCelRef>({a, b}));
        QVERIFY(controller.selectTrack(a.trackId)); QVERIFY(controller.setFrame(0));
        const auto ink = canvas.shot().animationTracks[0].drawings[0].templateLayers.value("ink");
        QVERIFY(controller.selectPropertyLayer(ink)); QVERIFY(controller.selectedCels().isEmpty());
        controller.setSelectedKeys({{ink, KeyframeProperty::PositionX, {}, 0}});
        QCOMPARE(controller.selectedKeys().size(), 1);
        controller.setSelectedCels({a, b}); QVERIFY(controller.selectedKeys().isEmpty());
        controller.clearKeySelection(); QCOMPARE(controller.selectedCels().size(), 2);
        QVERIFY(controller.selectCamera()); QVERIFY(controller.selectedCels().isEmpty());
        QCOMPARE(canvas.project().toJson(), before); QCOMPARE(canvas.document()->history()->count(), 0);
    }
    void batchMoveIsOneUndoAndTracksPlayheadWithoutMovingPropertyKeys() {
        Canvas canvas; TimelineController controller(&canvas); canvas.setProject(fixture());
        const auto a = ref(canvas.shot(), 0), b = ref(canvas.shot(), 1);
        QVERIFY(controller.selectTrack(a.trackId)); QVERIFY(controller.setFrame(1));
        controller.setSelectedCels({a, b}); const auto animations = canvas.shot().layerAnimations;
        QVERIFY(controller.moveSelectedCels(8)); QCOMPARE(controller.currentFrame(), 9);
        QCOMPARE(celExposure(canvas.shot(), a)->startFrame, 8); QCOMPARE(celExposure(canvas.shot(), b)->startFrame, 8);
        QCOMPARE(canvas.shot().layerAnimations, animations); QCOMPARE(canvas.document()->history()->count(), 1);
        QCOMPARE(controller.selectedCels(), QVector<TimelineCelRef>({a, b}));
        canvas.document()->history()->undo(); QCOMPARE(celExposure(canvas.shot(), a)->startFrame, 0);
        QCOMPARE(controller.selectedCels(), QVector<TimelineCelRef>({a, b}));
        const auto before = canvas.project().toJson(); const int index = canvas.document()->history()->index();
        QVERIFY(!controller.moveSelectedCels(4)); // Each track has an unselected cel here.
        QCOMPARE(canvas.project().toJson(), before); QCOMPARE(canvas.document()->history()->index(), index);
        QVERIFY(!controller.moveSelectedCels(-10)); QCOMPARE(canvas.project().toJson(), before);
    }
    void transitionGuardPreservesDraftAndSelectionAndPlaybackStopsForEdits() {
        Canvas canvas; TimelineController controller(&canvas); canvas.setProject(fixture());
        const auto a = ref(canvas.shot(), 0), b = ref(canvas.shot(), 1);
        controller.setSelectedCels({a}); controller.setPendingEdits(true);
        controller.setTransitionGuard([] { return false; });
        controller.setSelectedCels({b}); QCOMPARE(controller.selectedCels(), QVector<TimelineCelRef>({a}));
        QVERIFY(!controller.moveSelectedCels(8)); QVERIFY(controller.hasPendingEdits());
        QCOMPARE(canvas.document()->history()->count(), 0);
        controller.setTransitionGuard([] { return true; }); QSignalSpy resolved(&controller, &TimelineController::editResolutionRequested);
        QVERIFY(controller.moveSelectedCels(8)); QVERIFY(!controller.hasPendingEdits()); QVERIFY(!resolved.isEmpty());
        controller.setPlaying(true); QVERIFY(controller.playing());
        QVERIFY(controller.moveSelectedCels(4)); QVERIFY(!controller.playing());
    }
    void clipboardAndReuseKeepOriginalDrawingAfterLastExposureRemoval() {
        Canvas canvas; TimelineController controller(&canvas); canvas.setProject(fixture());
        const auto a = ref(canvas.shot(), 0);
        const auto sourceDrawing = celExposure(canvas.shot(), a)->drawingId;
        const auto originalLayers = canvas.shot().layers;
        QVERIFY(controller.selectTrack(a.trackId)); controller.setSelectedCels({a}); QVERIFY(controller.copySelectedCels());
        const auto keyLayer = canvas.shot().animationTracks[0].drawings[0].templateLayers.value("ink");
        controller.setSelectedKeys({{keyLayer, KeyframeProperty::PositionX, {}, 0}});
        QVERIFY(controller.copySelectedKeys()); QVERIFY(controller.canPasteKeys()); QVERIFY(controller.canPasteCels());
        controller.setSelectedCels({a}); QVERIFY(controller.removeExposure()); QVERIFY(controller.selectedCels().isEmpty());
        verifySameLayers(canvas, originalLayers); QCOMPARE(canvas.shot().animationTracks[0].drawings.size(), 2);
        QVERIFY(controller.canPasteCels()); QVERIFY(controller.setFrame(12)); QVERIFY(controller.pasteCels());
        QCOMPARE(controller.selectedCels().size(), 1);
        const auto pasted = controller.selectedCels().first(); const auto *cel = celExposure(canvas.shot(), pasted);
        QVERIFY(cel); QCOMPARE(cel->drawingId, sourceDrawing); QCOMPARE(cel->startFrame, 12);
        verifySameLayers(canvas, originalLayers); QVERIFY(controller.canPasteKeys());
        QVERIFY(controller.setFrame(20)); QVERIFY(controller.exposeExistingDrawing(sourceDrawing, 2));
        QCOMPARE(celExposure(canvas.shot(), controller.selectedCels().first())->drawingId, sourceDrawing);
        verifySameLayers(canvas, originalLayers);
        const int index = canvas.document()->history()->index(); const auto selected = controller.selectedCels();
        QVERIFY(!controller.exposeExistingDrawing(sourceDrawing, 2)); QCOMPARE(canvas.document()->history()->index(), index);
        QCOMPARE(controller.selectedCels(), selected);
    }
    void clipboardAndSelectionCannotLeakAcrossReloadWithTheSameIds() {
        auto project = fixture(); Canvas canvas; TimelineController controller(&canvas); canvas.setProject(project);
        controller.setSelectedCels({ref(canvas.shot(), 0)}); QVERIFY(controller.copySelectedCels());
        QVERIFY(controller.canPasteCels()); canvas.setProject(project);
        QVERIFY(controller.selectedCels().isEmpty()); QVERIFY(!controller.canPasteCels()); QVERIFY(!controller.pasteCels());
        QCOMPARE(canvas.document()->history()->count(), 0);
    }
    void externalExposureRemovalPrunesOnlyMissingSelection() {
        Canvas canvas; TimelineController controller(&canvas); canvas.setProject(fixture());
        const auto a = ref(canvas.shot(), 0), b = ref(canvas.shot(), 1);
        controller.setSelectedCels({a, b});
        QVERIFY(canvas.document()->removeAnimationExposures({a}));
        QCOMPARE(controller.selectedCels(), QVector<TimelineCelRef>({b}));
        canvas.document()->history()->undo();
        QVERIFY(celExposure(canvas.shot(), a)); QCOMPARE(controller.selectedCels(), QVector<TimelineCelRef>({b}));
    }
    void bulkHoldsReverseRepeatAndLocksPreserveBatchSelection() {
        Canvas canvas; TimelineController controller(&canvas); canvas.setProject(fixture());
        const auto a = ref(canvas.shot(), 0), b = ref(canvas.shot(), 0, 1);
        QVERIFY(controller.selectTrack(a.trackId)); controller.setSelectedCels({a, b});
        QVERIFY(controller.setSelectedCelHolds(2)); QCOMPARE(celExposure(canvas.shot(), a)->durationFrames, 2);
        QCOMPARE(celExposure(canvas.shot(), b)->startFrame, 2); QCOMPARE(controller.selectedCels().size(), 2);
        QVERIFY(controller.reverseSelectedCels()); QCOMPARE(celExposure(canvas.shot(), a)->startFrame, 2);
        QCOMPARE(celExposure(canvas.shot(), b)->startFrame, 0);
        QVERIFY(controller.repeatSelectedCels()); QCOMPARE(controller.selectedCels().size(), 2); QCOMPARE(controller.currentFrame(), 4);
        for (const auto &selected : controller.selectedCels()) { QVERIFY(selected != a); QVERIFY(selected != b); }
        const auto group = canvas.shot().animationTracks[0].rootGroupLayerId;
        canvas.document()->setLayersLocked({group}, true); const auto before = canvas.project().toJson();
        QVERIFY(!controller.deleteSelectedCels()); QCOMPARE(canvas.project().toJson(), before);
        QCOMPARE(controller.selectedCels().size(), 2);
    }
    void newDrawingReplacesOldCelSelectionAndUnusedDrawingDeletionInvalidatesPaste() {
        Canvas canvas; TimelineController controller(&canvas); canvas.setProject(fixture());
        const auto a = ref(canvas.shot(), 0), b = ref(canvas.shot(), 0, 1);
        QVERIFY(controller.selectTrack(a.trackId)); controller.setSelectedCels({a, b});
        QVERIFY(controller.setFrame(15)); QVERIFY(controller.newDrawing());
        QCOMPARE(controller.selectedCels().size(), 1); const auto created = controller.selectedCels().first();
        QVERIFY(created != a); QVERIFY(created != b);
        const auto drawing = celExposure(canvas.shot(), created)->drawingId;
        QVERIFY(controller.copySelectedCels()); QVERIFY(controller.deleteSelectedCels()); QVERIFY(controller.canPasteCels());
        QVERIFY(controller.removeUnusedDrawing(drawing)); QVERIFY(!controller.canPasteCels());
        canvas.document()->history()->undo(); QVERIFY(controller.canPasteCels());
    }
};
QTEST_MAIN(CelControllerTests)
#include "test_cel_controller.moc"
