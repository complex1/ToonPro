#include "document.h"
#include "animation_test_fixture.h"
#include <QtTest>
#include <limits>

using namespace toon;
namespace {
Project keyProject() {
    auto project = Project::create();
    project.width = project.height = 128;
    auto &shot = project.shots.first();
    shot.durationFrames = 64;
    auto &vector = shot.layers.first();
    vector.kind = LayerKind::Vector;
    auto path = EditableObject::create();
    path.nodes = {BezierNode::corner({10, 10}), BezierNode::corner({40, 30})};
    vector.objects = {path};
    auto pose = pathPose(path);
    pose.nodes[1] = BezierNode::corner({70, 40});
    const QString id = vector.id;
    shot.layerAnimations.append({id, {64, 64}, {
        {KeyframeProperty::PositionX, {{2, 20, KeyframeInterpolation::EaseIn}, {6, 60}, {10, 100}}},
        {KeyframeProperty::PositionY, {{2, 12}, {8, 80}}}
    }, {{path.id, {{2, pathPose(path), KeyframeInterpolation::EaseOut}, {6, pose}}}}});
    auto second = RasterLayer::create("Second");
    shot.layers.append(second);
    shot.layerAnimations.append({second.id, {}, {{KeyframeProperty::Opacity, {{4, .4, KeyframeInterpolation::Hold}, {10, 1}}}}, {}});
    return project;
}
TimelineKeyRef scalar(const Shot &shot, int frame, KeyframeProperty property = KeyframeProperty::PositionX, int layer = 0) {
    return {shot.layers[layer].id, property, {}, frame};
}
TimelineKeyRef path(const Shot &shot, int frame) {
    return {shot.layers[0].id, KeyframeProperty::PositionX, shot.layers[0].objects[0].id, frame};
}
QVector<int> frames(const PropertyChannel *channel) {
    QVector<int> result;
    if (channel) for (const auto &key : channel->keys) result.append(key.frame);
    return result;
}
QVector<int> frames(const PathAnimation *channel) {
    QVector<int> result;
    if (channel) for (const auto &key : channel->keys) result.append(key.frame);
    return result;
}
}

class TimelineEditDocumentTests : public QObject {
    Q_OBJECT
private slots:
    void mixedChannelMoveUsesVacatedSlotsAndOneUndo() {
        Document document;
        const auto project = keyProject();
        QVERIFY2(project.validate().isEmpty(), qPrintable(project.validate()));
        document.load(project);
        const auto &shot = project.shots.first();
        const QVector<TimelineKeyRef> selection{scalar(shot, 2), scalar(shot, 6), scalar(shot, 10),
            path(shot, 2), path(shot, 6), scalar(shot, 4, KeyframeProperty::Opacity, 1)};
        QVERIFY(document.moveTimelineKeys(selection, 4));
        QCOMPARE(document.history()->count(), 1);
        const auto &moved = document.project().shots.first();
        QCOMPARE(frames(propertyChannel(moved, shot.layers[0].id, KeyframeProperty::PositionX)), QVector<int>({6, 10, 14}));
        QCOMPARE(frames(pathAnimation(moved, shot.layers[0].id, shot.layers[0].objects[0].id)), QVector<int>({6, 10}));
        QCOMPARE(frames(propertyChannel(moved, shot.layers[1].id, KeyframeProperty::Opacity)), QVector<int>({8, 10}));
        QCOMPARE(moved.layers[0].objects, shot.layers[0].objects);
        QCOMPARE(moved.layerAnimations[0].channels[0].keys[0].interpolation, KeyframeInterpolation::EaseIn);
        document.history()->undo();
        QCOMPARE(document.project().toJson(), project.toJson());
        document.history()->redo();
        QCOMPARE(frames(propertyChannel(document.project().shots[0], shot.layers[0].id, KeyframeProperty::PositionX)), QVector<int>({6, 10, 14}));
    }

    void collisionsBoundsMissingKeysAndNoOpsAreAtomic() {
        Document document;
        const auto project = keyProject();
        document.load(project);
        const auto &shot = project.shots.first();
        const auto snapshot = project.toJson();
        const QVector<TimelineKeyRef> partial{path(shot, 2), scalar(shot, 2), scalar(shot, 6)};
        QVERIFY(!document.moveTimelineKeys(partial, 4)); // X6 would hit the unselected X10.
        QVERIFY(!document.moveTimelineKeys({scalar(shot, 2)}, -3));
        QVERIFY(!document.moveTimelineKeys({scalar(shot, 2)}, std::numeric_limits<int>::max()));
        QVERIFY(!document.moveTimelineKeys({scalar(shot, 3)}, 0));
        QVERIFY(!document.removeTimelineKeys({scalar(shot, 2), path(shot, 3)}));
        QVERIFY(document.moveTimelineKeys({scalar(shot, 2), scalar(shot, 2)}, 0));
        QCOMPARE(document.project().toJson(), snapshot);
        QCOMPARE(document.history()->count(), 0);
        QVERIFY(document.moveTimelineKeys({scalar(shot, 2), scalar(shot, 2)}, 1));
        QCOMPARE(document.history()->count(), 1);
        QCOMPARE(frames(propertyChannel(document.project().shots[0], shot.layers[0].id, KeyframeProperty::PositionX)), QVector<int>({3, 6, 10}));
    }

    void copyPasteIsRelativeSnapshotAcrossPropertiesAndPaths() {
        Document document;
        const auto project = keyProject();
        const auto &shot = project.shots.first();
        document.load(project);
        TimelineKeyClipboard clipboard;
        QVERIFY(document.copyTimelineKeys({path(shot, 6), scalar(shot, 2), scalar(shot, 4, KeyframeProperty::Opacity, 1)}, &clipboard));
        QCOMPARE(document.history()->count(), 0);
        QCOMPARE(clipboard.keys.size(), 3);
        QVERIFY(document.setCurrentFrame(2));
        QVERIFY(document.setKeyframeValue(shot.layers[0].id, KeyframeProperty::PositionX, 222));
        const int history = document.history()->count();
        QVERIFY(document.pasteTimelineKeys(clipboard, 20));
        QCOMPARE(document.history()->count(), history + 1);
        const auto &pasted = document.project().shots.first();
        const auto *x = propertyChannel(pasted, shot.layers[0].id, KeyframeProperty::PositionX);
        QCOMPARE(x->keys.last(), (ScalarKeyframe{20, 20, KeyframeInterpolation::EaseIn}));
        QCOMPARE(pathAnimation(pasted, shot.layers[0].id, shot.layers[0].objects[0].id)->keys.last().pose,
                 shot.layerAnimations[0].paths[0].keys[1].pose);
        QCOMPARE(pathAnimation(pasted, shot.layers[0].id, shot.layers[0].objects[0].id)->keys.last().frame, 24);
        QCOMPARE(propertyChannel(pasted, shot.layers[1].id, KeyframeProperty::Opacity)->keys.last(),
                 (ScalarKeyframe{22, .4, KeyframeInterpolation::Hold}));
        QVERIFY(document.pasteTimelineKeys(clipboard, 30));
        const auto snapshot = document.project().toJson();
        const auto count = document.history()->count();
        QVERIFY(!document.pasteTimelineKeys(clipboard, 20));
        QVERIFY(!document.pasteTimelineKeys(clipboard, 62));
        QCOMPARE(document.project().toJson(), snapshot);
        QCOMPARE(document.history()->count(), count);
        document.history()->undo();
        QCOMPARE(propertyChannel(document.project().shots[0], shot.layers[0].id, KeyframeProperty::PositionX)->keys.last().frame, 20);
    }

    void deleteAllKeysCleansChannelsAndPasteCanRestoreThem() {
        Document document;
        const auto project = keyProject();
        const auto &shot = project.shots.first();
        document.load(project);
        const QVector<TimelineKeyRef> selection{scalar(shot, 2), scalar(shot, 6), scalar(shot, 10), path(shot, 2), path(shot, 6)};
        TimelineKeyClipboard clipboard;
        QVERIFY(document.copyTimelineKeys(selection, &clipboard));
        QVERIFY(document.removeTimelineKeys(selection));
        QCOMPARE(document.history()->count(), 1);
        QVERIFY(!propertyChannel(document.project().shots[0], shot.layers[0].id, KeyframeProperty::PositionX));
        QVERIFY(!pathAnimation(document.project().shots[0], shot.layers[0].id, shot.layers[0].objects[0].id));
        QVERIFY(propertyChannel(document.project().shots[0], shot.layers[0].id, KeyframeProperty::PositionY));
        QVERIFY(document.pasteTimelineKeys(clipboard, 20));
        QCOMPARE(frames(propertyChannel(document.project().shots[0], shot.layers[0].id, KeyframeProperty::PositionX)), QVector<int>({20, 24, 28}));
        QCOMPARE(frames(pathAnimation(document.project().shots[0], shot.layers[0].id, shot.layers[0].objects[0].id)), QVector<int>({20, 24}));
        document.history()->undo();
        document.history()->undo();
        QCOMPARE(document.project().toJson(), project.toJson());
        QVERIFY(document.removeTimelineKeys({scalar(shot, 4, KeyframeProperty::Opacity, 1), scalar(shot, 10, KeyframeProperty::Opacity, 1)}));
        QVERIFY(!layerAnimation(document.project().shots[0], shot.layers[1].id));
        QVERIFY(document.project().validate().isEmpty());
    }

    void copiedSnapshotCanPasteAfterShotWasShortened() {
        auto project = Project::create();
        const auto id = project.shots[0].layers[0].id;
        project.shots[0].durationFrames = 64;
        project.shots[0].layerAnimations = {{id, {}, {{KeyframeProperty::PositionX, {{50, 125, KeyframeInterpolation::EaseOut}, {54, 150}}}}, {}}};
        Document document; document.load(project);
        TimelineKeyClipboard clipboard;
        QVERIFY(document.copyTimelineKeys({{id, KeyframeProperty::PositionX, {}, 50}, {id, KeyframeProperty::PositionX, {}, 54}}, &clipboard));
        auto shortened = project;
        shortened.shots[0].durationFrames = 40;
        shortened.shots[0].layerAnimations.clear();
        document.replaceProject(shortened, "Shorten shot");
        QCOMPARE(document.project().shots[0].durationFrames, 40);
        QVERIFY(document.pasteTimelineKeys(clipboard, 10));
        QCOMPARE(propertyChannel(document.project().shots[0], id, KeyframeProperty::PositionX)->keys,
            QVector<ScalarKeyframe>({{10, 125, KeyframeInterpolation::EaseOut}, {14, 150}}));
    }

    void clipboardRejectsMissingSourcesTopologyAndMalformedValues() {
        Document document;
        const auto project = keyProject();
        const auto &shot = project.shots.first();
        document.load(project);
        TimelineKeyClipboard clipboard;
        QVERIFY(document.copyTimelineKeys({path(shot, 2), scalar(shot, 2)}, &clipboard));
        auto bad = clipboard;
        bad.shotId = "other-shot";
        QVERIFY(!document.pasteTimelineKeys(bad, 20));
        bad = clipboard; bad.keys[1].scalar.value = qQNaN();
        QVERIFY(!document.pasteTimelineKeys(bad, 20));
        bad = clipboard; bad.keys[1].scalar.frame = 3;
        QVERIFY(!document.pasteTimelineKeys(bad, 20));
        bad = clipboard; bad.keys.append(bad.keys[0]);
        QVERIFY(!document.pasteTimelineKeys(bad, 20));
        QCOMPARE(document.project().toJson(), project.toJson());
        QCOMPARE(document.history()->count(), 0);
        TimelineKeyClipboard untouched = clipboard;
        QVERIFY(!document.copyTimelineKeys({scalar(shot, 3)}, &untouched));
        QCOMPARE(untouched.keys.size(), clipboard.keys.size());
        QCOMPARE(untouched.keys[0].path, clipboard.keys[0].path);
        QVERIFY(document.enablePathAnimation(shot.layers[0].id, shot.layers[0].objects[0].id, false));
        auto changed = document.project();
        changed.shots[0].layers[0].objects[0].nodes.append(BezierNode::corner({80, 80}));
        document.load(changed);
        const auto changedSnapshot = document.project().toJson();
        QVERIFY(!document.pasteTimelineKeys(clipboard, 20));
        QCOMPARE(document.project().toJson(), changedSnapshot);
        QCOMPARE(document.history()->count(), 0);
        changed = project;
        changed.shots[0].layers.removeFirst();
        changed.shots[0].layerAnimations.removeFirst();
        document.load(changed);
        QVERIFY(!document.pasteTimelineKeys(clipboard, 20));
        QCOMPARE(document.history()->count(), 0);
    }

    void locksRejectWholeMutationButAllowReadonlyCopy() {
        auto project = keyProject();
        auto &shot = project.shots.first();
        auto group = RasterLayer::create("Locked parent"); group.group = true; group.locked = true;
        shot.layers[0].parentId = group.id;
        shot.layers.append(group);
        const auto referenceShot = shot;
        Document document; document.load(project);
        const QVector<TimelineKeyRef> selection{scalar(referenceShot, 4, KeyframeProperty::Opacity, 1), scalar(referenceShot, 2)};
        TimelineKeyClipboard clipboard;
        QVERIFY(document.copyTimelineKeys(selection, &clipboard));
        QVERIFY(!document.moveTimelineKeys(selection, 1));
        QVERIFY(!document.removeTimelineKeys(selection));
        QVERIFY(!document.pasteTimelineKeys(clipboard, 20));
        QCOMPARE(document.project().toJson(), project.toJson());
        QCOMPARE(document.history()->count(), 0);
        project.shots[0].layers.last().locked = false;
        project.shots[0].layers[0].positionLocked = true;
        project.shots[0].layers[1].positionLocked = true;
        document.load(project);
        QVERIFY(!document.moveTimelineKeys({scalar(referenceShot, 2)}, 1));
        QVERIFY(!document.removeTimelineKeys({path(referenceShot, 2)}));
        QVERIFY(document.moveTimelineKeys({scalar(referenceShot, 4, KeyframeProperty::Opacity, 1)}, 1));
        QCOMPARE(document.history()->count(), 1);
    }

    void keyLimitsAndPendingGesturesRejectWithoutMutation() {
        auto project = Project::create();
        auto &shot = project.shots.first();
        shot.durationFrames = 32768;
        PropertyChannel channel{KeyframeProperty::PositionX, {}};
        for (int i = 0; i < maxPropertyKeyframes; ++i) channel.keys.append({i, qreal(i)});
        shot.layerAnimations = {{shot.layers[0].id, {}, {channel}, {}}};
        Document document; document.load(project);
        TimelineKeyClipboard clipboard;
        const QVector<TimelineKeyRef> selection{scalar(shot, 0)};
        QVERIFY(document.copyTimelineKeys(selection, &clipboard));
        QVERIFY(!document.pasteTimelineKeys(clipboard, 20000));
        QCOMPARE(document.history()->count(), 0);
        QCOMPARE(document.project().shots[0].layerAnimations, shot.layerAnimations);
        QVERIFY(document.beginKeyframeEdit(shot.layers[0].id));
        QVERIFY(!document.moveTimelineKeys(selection, 1));
        QVERIFY(!document.removeTimelineKeys(selection));
        QVERIFY(!document.pasteTimelineKeys(clipboard, 20000));
        document.cancelKeyframeEdit();
        QCOMPARE(document.history()->count(), 0);
    }

    void offPlayheadDrawingKeysRemainEditableAndExposureTimingKeepsKeysAbsolute() {
        auto project = animation_fixture::project();
        auto &shot = project.shots.first();
        const auto track = shot.animationTracks[0];
        const auto firstLayerId = track.drawings[0].templateLayers.value("ink");
        shot.layerAnimations = {{firstLayerId, {}, {{KeyframeProperty::PositionX, {{0, 0}, {2, 20}}}}, {}}};
        Document document; document.load(project);
        QVERIFY(document.selectAnimationTrack(track.id));
        QVERIFY(document.setCurrentFrame(4));
        QVERIFY(document.moveTimelineKeys({{firstLayerId, KeyframeProperty::PositionX, {}, 2}}, 1));
        QCOMPARE(document.currentFrame(), 4);
        const auto animation = document.project().shots[0].layerAnimations;
        const auto drawingTiles = document.project().shots[0].layers[animation_fixture::layerIndex(shot, firstLayerId)].tiles;
        QVERIFY(document.setAnimationExposureTimingById(track.id, track.exposures[0].id, 10, 3));
        QCOMPARE(document.currentFrame(), 4);
        QCOMPARE(document.project().shots[0].layerAnimations, animation);
        QCOMPARE(document.project().shots[0].layers[animation_fixture::layerIndex(shot, firstLayerId)].tiles, drawingTiles);
        const auto &exposures = document.project().shots[0].animationTracks[0].exposures;
        QCOMPARE(exposures.last().id, track.exposures[0].id);
        QCOMPARE(exposures.last().drawingId, track.drawings[0].id);
        QCOMPARE(exposures.last().startFrame, 10);
        QCOMPARE(document.history()->count(), 2);
        document.history()->undo();
        QCOMPARE(document.project().shots[0].animationTracks, shot.animationTracks);
        QCOMPARE(document.project().shots[0].layerAnimations, animation);
    }

    void exposureMoveTrimBoundsCollisionsLocksAndNoOp() {
        const auto project = animation_fixture::project();
        const auto track = project.shots[0].animationTracks[0];
        Document document; document.load(project);
        QVERIFY(document.setAnimationExposureTimingById(track.id, track.exposures[0].id, 0, 3));
        QCOMPARE(document.history()->count(), 0);
        QVERIFY(!document.setAnimationExposureTimingById(track.id, track.exposures[0].id, 2, 3));
        QVERIFY(!document.setAnimationExposureTimingById(track.id, track.exposures[0].id, -1, 3));
        QVERIFY(!document.setAnimationExposureTimingById(track.id, track.exposures[0].id, 1, 0));
        QVERIFY(!document.setAnimationExposureTimingById(track.id, track.exposures[0].id, std::numeric_limits<int>::max(), 3));
        QVERIFY(!document.setAnimationExposureTimingById(track.id, "missing", 10, 2));
        QCOMPARE(document.project().toJson(), project.toJson());
        QCOMPARE(document.history()->count(), 0);
        QVERIFY(document.setAnimationExposureTimingById(track.id, track.exposures[0].id, 1, 3)); // Adjacent to the next block is legal.
        QVERIFY(document.setAnimationExposureTimingById(track.id, track.exposures[0].id, 2, 2)); // Left trim, same end.
        QVERIFY(document.setAnimationExposureTimingById(track.id, track.exposures[0].id, 2, 1)); // Right trim.
        QCOMPARE(document.history()->count(), 3);
        document.history()->undo(); document.history()->undo(); document.history()->undo();
        QCOMPARE(document.project().toJson(), project.toJson());
        for (const QString &id : {track.rootGroupLayerId, track.drawings[0].groupLayerId}) {
            auto locked = project;
            const auto index = animation_fixture::layerIndex(locked.shots[0], id);
            locked.shots[0].layers[index].locked = true;
            document.load(locked);
            QVERIFY(!document.setAnimationExposureTimingById(track.id, track.exposures[0].id, 10, 3));
            QCOMPARE(document.history()->count(), 0);
            locked.shots[0].layers[index].locked = false;
            locked.shots[0].layers[index].positionLocked = true;
            document.load(locked);
            QVERIFY(!document.setAnimationExposureTimingById(track.id, track.exposures[0].id, 10, 3));
            QCOMPARE(document.history()->count(), 0);
        }
    }

    void loadSignalAlsoFiresWhenReopeningTheSameShot() {
        Document document;
        const auto project = keyProject();
        QSignalSpy loaded(&document, &Document::loaded);
        document.load(project); document.load(project);
        QCOMPARE(loaded.size(), 2);
        document.history()->undo();
        QCOMPARE(loaded.size(), 2);
    }
};
QTEST_MAIN(TimelineEditDocumentTests)
#include "timeline_edit_document_tests.moc"
