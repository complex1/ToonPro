#include "document.h"
#include "animation_test_fixture.h"
#include <QTemporaryDir>
#include <QtTest>
#include <limits>

using namespace toon;
namespace {
Project twoTracks() {
    auto project = animation_fixture::project();
    auto other = animation_fixture::project();
    auto &shot = project.shots[0]; shot.durationFrames = 80;
    for (int i = 1; i < other.shots[0].layers.size(); ++i) shot.layers.append(other.shots[0].layers[i]);
    auto second = other.shots[0].animationTracks[0];
    second.exposures[0].startFrame = 2; second.exposures[0].durationFrames = 2;
    second.exposures[1].startFrame = 8; second.exposures[1].durationFrames = 4;
    shot.animationTracks.append(second);
    const auto ink = shot.animationTracks[0].drawings[0].templateLayers.value("ink");
    shot.layerAnimations.append({ink, {}, {{KeyframeProperty::Opacity, {{0, .4}, {18, 1.}}}}});
    return project;
}
QVector<TimelineCelRef> refs(const Shot &shot) {
    QVector<TimelineCelRef> result;
    for (const auto &track : shot.animationTracks) for (const auto &cel : track.exposures)
        result.append({track.id, cel.id});
    return result;
}
QVector<int> starts(const AnimationTrack &track) {
    QVector<int> result; for (const auto &cel : track.exposures) result.append(cel.startFrame); return result;
}
void verifyArtwork(const Shot &actual, const Shot &expected) {
    QCOMPARE(actual.layers.size(), expected.layers.size());
    QCOMPARE(actual.layerAnimations, expected.layerAnimations);
    for (int i = 0; i < actual.layers.size(); ++i) {
        QCOMPARE(actual.layers[i].id, expected.layers[i].id);
        QCOMPARE(actual.layers[i].tiles, expected.layers[i].tiles);
        QCOMPARE(actual.layers[i].objects, expected.layers[i].objects);
        QCOMPARE(actual.layers[i].mask, expected.layers[i].mask);
    }
    for (int i = 0; i < actual.animationTracks.size(); ++i)
        QCOMPARE(actual.animationTracks[i].drawings, expected.animationTracks[i].drawings);
}
}

class CelEditTests : public QObject {
    Q_OBJECT
private slots:
    void batchMoveUsesVacatedSlotsAndKeepsArtworkAndKeys() {
        const auto project = twoTracks(); QVERIFY2(project.validate().isEmpty(), qPrintable(project.validate()));
        Document document; document.load(project); const auto selected = refs(project.shots[0]);
        QVERIFY(document.moveAnimationExposures(selected, 4)); QCOMPARE(document.history()->count(), 1);
        QCOMPARE(starts(document.activeShot().animationTracks[0]), QVector<int>({4, 8}));
        QCOMPARE(starts(document.activeShot().animationTracks[1]), QVector<int>({6, 12}));
        verifyArtwork(document.activeShot(), project.shots[0]);
        for (const auto &ref : selected) QVERIFY(celExposure(document.activeShot(), ref));
        document.history()->undo(); QCOMPARE(document.project().toJson(), project.toJson());
        document.history()->redo(); QCOMPARE(starts(document.activeShot().animationTracks[0]), QVector<int>({4, 8}));
    }
    void moveConflictsBoundsStaleRefsAndNoOpsAreAtomic() {
        const auto project = twoTracks(); Document document; document.load(project);
        const auto selected = refs(project.shots[0]); const auto before = project.toJson();
        QVERIFY(!document.moveAnimationExposures({selected[0], selected[2]}, 3));
        QCOMPARE(document.project().toJson(), before); QCOMPARE(document.history()->count(), 0);
        QVERIFY(!document.moveAnimationExposures(selected, -1));
        QVERIFY(!document.moveAnimationExposures(selected, std::numeric_limits<int>::max()));
        QVERIFY(!document.moveAnimationExposures({selected[0], {selected[1].trackId, animation_fixture::id()}}, 10));
        QVERIFY(!document.moveAnimationExposures({}, 10));
        QCOMPARE(document.project().toJson(), before); QCOMPARE(document.history()->count(), 0);
        QVERIFY(document.moveAnimationExposures(selected, 0)); QCOMPARE(document.history()->count(), 0);
        QVERIFY(document.moveAnimationExposures({selected[0], selected[0]}, 1));
        QCOMPARE(celExposure(document.activeShot(), selected[0])->startFrame, 1); QCOMPARE(document.history()->count(), 1);
    }
    void holdsRepackPerTrackWithOneUndoAndRejectCollisions() {
        const auto project = twoTracks(); Document document; document.load(project);
        QVERIFY(document.setAnimationExposureHolds(refs(project.shots[0]), 2));
        QCOMPARE(starts(document.activeShot().animationTracks[0]), QVector<int>({0, 2}));
        QCOMPARE(starts(document.activeShot().animationTracks[1]), QVector<int>({2, 4}));
        for (const auto &track : document.activeShot().animationTracks)
            for (const auto &cel : track.exposures) QCOMPARE(cel.durationFrames, 2);
        QCOMPARE(document.history()->count(), 1); verifyArtwork(document.activeShot(), project.shots[0]);
        document.history()->undo(); QCOMPARE(document.project().toJson(), project.toJson());

        auto conflict = animation_fixture::project(); auto &track = conflict.shots[0].animationTracks[0];
        track.exposures[0].durationFrames = 1; track.exposures[1].startFrame = 8; track.exposures[1].durationFrames = 1;
        const QVector<TimelineCelRef> selected{{track.id, track.exposures[0].id}, {track.id, track.exposures[1].id}};
        track.exposures.insert(1, {animation_fixture::id(), track.drawings[0].id, 4, 2});
        document.load(conflict); QVERIFY(!document.setAnimationExposureHolds(selected, 3));
        QVERIFY(!document.setAnimationExposureHolds(selected, 0));
        QVERIFY(!document.setAnimationExposureHolds(selected, std::numeric_limits<int>::max()));
        QCOMPARE(document.project().toJson(), conflict.toJson()); QCOMPARE(document.history()->count(), 0);
    }
    void reverseMirrorsDurationsAndGapsWithoutRetimingMotion() {
        auto project = twoTracks(); auto &track = project.shots[0].animationTracks[0];
        track.exposures[0].durationFrames = 2; track.exposures[1].startFrame = 6; track.exposures[1].durationFrames = 4;
        const auto selected = refs(project.shots[0]); const auto firstDrawing = track.exposures[0].drawingId;
        Document document; document.load(project); QVERIFY(document.reverseAnimationExposures(selected));
        const auto &changed = document.activeShot().animationTracks[0];
        QCOMPARE(starts(changed), QVector<int>({0, 8})); QCOMPARE(changed.exposures[1].drawingId, firstDrawing);
        QCOMPARE(changed.exposures[1].durationFrames, 2); QCOMPARE(changed.exposures[0].durationFrames, 4);
        verifyArtwork(document.activeShot(), project.shots[0]); QCOMPARE(document.history()->count(), 1);
        QVERIFY(document.reverseAnimationExposures(selected)); QCOMPARE(document.project().toJson(), project.toJson());
        auto conflict = project; auto &blocked = conflict.shots[0].animationTracks[0];
        blocked.exposures.insert(1, {animation_fixture::id(), blocked.drawings[0].id, 3, 1});
        document.load(conflict); QVERIFY(!document.reverseAnimationExposures(selected));
        QCOMPARE(document.project().toJson(), conflict.toJson()); QCOMPARE(document.history()->count(), 0);
    }
    void repeatPreservesMultiTrackAlignmentAndReusesDrawings() {
        const auto project = twoTracks(); Document document; document.load(project);
        QVERIFY(document.repeatAnimationExposures(refs(project.shots[0]), 2));
        QCOMPARE(starts(document.activeShot().animationTracks[0]), QVector<int>({0, 4, 12, 16, 24, 28}));
        QCOMPARE(starts(document.activeShot().animationTracks[1]), QVector<int>({2, 8, 14, 20, 26, 32}));
        verifyArtwork(document.activeShot(), project.shots[0]); QCOMPARE(document.history()->count(), 1);
        QSet<QString> exposureIds;
        for (const auto &track : document.activeShot().animationTracks) {
            QCOMPARE(track.drawings.size(), 2);
            for (const auto &cel : track.exposures) { QVERIFY(!exposureIds.contains(cel.id)); exposureIds.insert(cel.id); }
            QCOMPARE(track.exposures[2].drawingId, track.exposures[0].drawingId);
        }
        document.history()->undo(); QCOMPARE(document.project().toJson(), project.toJson());
        QVERIFY(!document.repeatAnimationExposures(refs(project.shots[0]), 0));
        QVERIFY(!document.repeatAnimationExposures(refs(project.shots[0]), std::numeric_limits<int>::max()));
        QCOMPARE(document.project().toJson(), project.toJson());
    }
    void clipboardKeepsTimingSnapshotAndSurvivesSourceExposureRemoval() {
        const auto project = twoTracks(); Document document; document.load(project);
        const auto selected = refs(project.shots[0]); QString error;
        const auto clipboard = copyCelExposures(project.shots[0], selected, &error); QVERIFY(error.isEmpty());
        QCOMPARE(clipboard.cels.size(), 4);
        QVERIFY(document.moveAnimationExposures(selected, 20));
        QVector<TimelineCelRef> pasted;
        QVERIFY(document.pasteAnimationExposures(clipboard, 40, &pasted)); QCOMPARE(pasted.size(), 4);
        QCOMPARE(starts(document.activeShot().animationTracks[0]), QVector<int>({20, 24, 40, 44}));
        QCOMPARE(starts(document.activeShot().animationTracks[1]), QVector<int>({22, 28, 42, 48}));
        verifyArtwork(document.activeShot(), project.shots[0]);
        QVERIFY(document.removeAnimationExposures(selected));
        QVERIFY(document.pasteAnimationExposures(clipboard, 0));
        verifyArtwork(document.activeShot(), project.shots[0]);
        auto wrongShot = clipboard; wrongShot.shotId = animation_fixture::id();
        const auto before = document.project().toJson(); const auto history = document.history()->count();
        QVERIFY(!document.pasteAnimationExposures(wrongShot, 60));
        QVERIFY(!document.pasteAnimationExposures(clipboard, 41));
        QVERIFY(!document.pasteAnimationExposures(clipboard, 79));
        auto malformed = clipboard; malformed.cels[0].durationFrames = 0;
        QVERIFY(!document.pasteAnimationExposures(malformed, 60));
        malformed = clipboard; malformed.cels[0].startFrame = std::numeric_limits<int>::max();
        QVERIFY(!document.pasteAnimationExposures(malformed, 60));
        QCOMPARE(document.project().toJson(), before); QCOMPARE(document.history()->count(), history);
    }
    void reuseSharesEditableArtworkAndUndoOnlyRemovesTheNewExposure() {
        const auto project = animation_fixture::project(); const auto track = project.shots[0].animationTracks[0];
        Document document; document.load(project); TimelineCelRef inserted;
        QVERIFY(document.exposeAnimationDrawing(track.id, track.drawings[0].id, 10, 2, &inserted));
        QCOMPARE(inserted.trackId, track.id); QVERIFY(!inserted.exposureId.isEmpty());
        verifyArtwork(document.activeShot(), project.shots[0]); QCOMPARE(document.history()->count(), 1);
        QVERIFY(document.selectAnimationTrack(track.id)); QVERIFY(document.setCurrentFrame(0));
        const auto inkId = track.drawings[0].templateLayers.value("ink");
        QCOMPARE(document.activeShot().layers[document.activeLayer()].id, inkId);
        BrushSettings brush; brush.color = Qt::green; brush.size = 12;
        QVERIFY(document.beginStroke({40,40}, 1, brush, false)); document.endStroke();
        const auto painted = document.activeShot().layers[document.activeLayer()].tiles;
        QVERIFY(painted != project.shots[0].layers[animation_fixture::layerIndex(project.shots[0], inkId)].tiles);
        QVERIFY(document.setCurrentFrame(10)); QCOMPARE(document.activeShot().layers[document.activeLayer()].id, inkId);
        QCOMPARE(document.activeShot().layers[document.activeLayer()].tiles, painted);
        document.history()->undo(); document.history()->undo();
        QCOMPARE(document.project().toJson(), project.toJson());
        document.history()->redo(); QVERIFY(celExposure(document.activeShot(), inserted));
    }
    void removingLastExposureKeepsPortableUnusedArtworkAndExplicitDeletionIsUndoable() {
        const auto project = animation_fixture::project(); const auto track = project.shots[0].animationTracks[0];
        Document document; document.load(project); QVERIFY(document.removeAnimationExposures(refs(project.shots[0])));
        QCOMPARE(document.activeShot().animationTracks[0].exposures.size(), 0); verifyArtwork(document.activeShot(), project.shots[0]);
        QVERIFY2(document.project().validate().isEmpty(), qPrintable(document.project().validate()));
        QTemporaryDir directory; QString error; const auto path = directory.filePath("unused.toonproj");
        QVERIFY2(saveProject(document.project(), path, &error), qPrintable(error));
        const auto restored = loadProject(path, &error); QVERIFY2(restored.has_value(), qPrintable(error));
        QCOMPARE(restored->toJson(), document.project().toJson());
        Document reopened; reopened.load(*restored);
        QVERIFY(reopened.exposeAnimationDrawing(track.id, track.drawings[0].id, 5, 3));
        verifyArtwork(reopened.activeShot(), project.shots[0]);
        QVERIFY(!reopened.deleteUnusedAnimationDrawing(track.id, track.drawings[0].id));
        const auto before = reopened.project().toJson(); const auto beforeCount = reopened.history()->count();
        QVERIFY(reopened.deleteUnusedAnimationDrawing(track.id, track.drawings[1].id));
        QCOMPARE(reopened.activeShot().animationTracks[0].drawings.size(), 1); QCOMPARE(reopened.activeShot().layers.size(), 4);
        QCOMPARE(reopened.history()->count(), beforeCount + 1);
        reopened.history()->undo(); QCOMPARE(reopened.project().toJson(), before);
    }
    void locksProtectTheEntireBatch_data() {
        QTest::addColumn<bool>("trackLock"); QTest::addColumn<bool>("position");
        QTest::newRow("track") << true << false; QTest::newRow("track-position") << true << true;
        QTest::newRow("drawing") << false << false; QTest::newRow("drawing-position") << false << true;
    }
    void locksProtectTheEntireBatch() {
        QFETCH(bool, trackLock); QFETCH(bool, position);
        auto project = twoTracks(); const auto track = project.shots[0].animationTracks[1];
        const auto folder = trackLock ? track.rootGroupLayerId : track.drawings[1].groupLayerId;
        auto &layer = project.shots[0].layers[animation_fixture::layerIndex(project.shots[0], folder)];
        if (position) layer.positionLocked = true; else layer.locked = true;
        Document document; document.load(project); const auto selected = refs(project.shots[0]);
        QString error; const auto clipboard = copyCelExposures(project.shots[0], selected, &error); QVERIFY(error.isEmpty());
        QVERIFY(!document.moveAnimationExposures(selected, 20)); QVERIFY(!document.removeAnimationExposures(selected));
        QVERIFY(!document.setAnimationExposureHolds(selected, 1)); QVERIFY(!document.reverseAnimationExposures(selected));
        QVERIFY(!document.repeatAnimationExposures(selected)); QVERIFY(!document.pasteAnimationExposures(clipboard, 20));
        QVERIFY(!document.exposeAnimationDrawing(track.id, track.drawings[1].id, 40, 2));
        QCOMPARE(document.project().toJson(), project.toJson()); QCOMPARE(document.history()->count(), 0);
    }
    void unusedDeletionHonorsChildLocksAndInvalidatesLinkedClipboard() {
        auto project = animation_fixture::project(); const auto track = project.shots[0].animationTracks[0];
        const auto selected = refs(project.shots[0]); const auto clipboard = copyCelExposures(project.shots[0], selected);
        auto &ink = project.shots[0].layers[animation_fixture::layerIndex(project.shots[0], track.drawings[0].templateLayers.value("ink"))];
        ink.locked = true; Document document; document.load(project);
        QVERIFY(document.removeAnimationExposures(selected)); const auto removed = document.project().toJson();
        QVERIFY(!document.deleteUnusedAnimationDrawing(track.id, track.drawings[0].id)); QCOMPARE(document.project().toJson(), removed);
        QVERIFY(document.deleteUnusedAnimationDrawing(track.id, track.drawings[1].id));
        const auto before = document.project().toJson(); const auto count = document.history()->count();
        QVERIFY(!document.pasteAnimationExposures(clipboard, 10));
        QCOMPARE(document.project().toJson(), before); QCOMPARE(document.history()->count(), count);
        document.history()->undo(); QVERIFY(document.pasteAnimationExposures(clipboard, 10));
    }
    void staleSelectionsMalformedClipboardAndExposureLimitRejectAtomically() {
        auto project = animation_fixture::project(); auto &shot = project.shots[0]; shot.durationFrames = 20000;
        auto &track = shot.animationTracks[0]; track.exposures.clear();
        for (int i = 0; i < maxAnimationExposures; ++i)
            track.exposures.append({animation_fixture::id(), track.drawings[0].id, i, 1});
        QVERIFY2(project.validate().isEmpty(), qPrintable(project.validate()));
        Document document; document.load(project);
        const auto clipboard = copyCelExposures(shot, {{track.id, track.exposures[0].id}});
        QVERIFY(!document.pasteAnimationExposures(clipboard, 18000));
        QVERIFY(!document.exposeAnimationDrawing(track.id, track.drawings[1].id, 18000, 1));
        auto malformed = clipboard; malformed.cels[0].startFrame = std::numeric_limits<int>::max();
        QVERIFY(!document.pasteAnimationExposures(malformed, 18000));
        QString error; const auto invalid = copyCelExposures(shot, {{animation_fixture::id(), track.exposures[0].id}}, &error);
        QVERIFY(!error.isEmpty()); QVERIFY(invalid.cels.isEmpty());
        QCOMPARE(document.project().toJson(), project.toJson()); QCOMPARE(document.history()->count(), 0);
    }
    void activeDrawingGestureRejectsTimingWithoutResolvingIt() {
        const auto project = animation_fixture::project(); Document document; document.load(project);
        const auto track = project.shots[0].animationTracks[0]; QVERIFY(document.selectAnimationTrack(track.id));
        BrushSettings brush; QVERIFY(document.beginStroke({20,20}, 1, brush, false));
        QVERIFY(!document.removeAnimationExposures(refs(project.shots[0]))); QVERIFY(document.strokeActive());
        QVERIFY(!document.moveAnimationExposures(refs(project.shots[0]), 10)); QVERIFY(document.strokeActive());
        document.cancelStroke(); QCOMPARE(document.project().toJson(), project.toJson()); QCOMPARE(document.history()->count(), 0);
    }
};
QTEST_MAIN(CelEditTests)
#include "test_cel_edit.moc"
