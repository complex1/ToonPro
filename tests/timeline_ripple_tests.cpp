#include "timeline_ripple.h"
#include "animation_test_fixture.h"
#include "sheet.h"
#include "symbols.h"
#include <QtTest>
#include <algorithm>
#include <limits>

using namespace toon;
namespace {
Project mixedProject() {
    auto project = animation_fixture::project();
    auto &shot = project.shots.first();
    shot.animationTracks[0].exposures[0].durationFrames = 4;
    const auto id = shot.layers[0].id;
    ScalarKeyframe first{1, 10, KeyframeInterpolation::EaseIn};
    first.easing = CubicEasing{{.2, .1}, {.7, .9}};
    first.spatial = SpatialTangents{{-1, -2}, {4, 5}, true};
    ScalarKeyframe second{8, 80}; second.spatial = SpatialTangents{{-4, -5}, {1, 2}, false};
    auto yFirst = first, ySecond = second;
    yFirst.value = 20; yFirst.spatial.reset(); ySecond.value = 160; ySecond.spatial.reset();
    shot.layerAnimations = {{id, {32, 32}, {{KeyframeProperty::PositionX, {first, second}},
                                            {KeyframeProperty::PositionY, {yFirst, ySecond}}}, {}, true}};
    shot.camera2D = {{4, 5}, 1.2, 10};
    shot.camera2DKeys = {{1, {{20, 30}, 1.5, 25}, KeyframeInterpolation::EaseIn}, {8, {{40, 60}, 2, 50}}};
    auto scene = RasterLayer::create("Scene"); scene.kind = LayerKind::Scene3D; scene.scene3d = Scene3D{};
    shot.layers.append(scene);
    SceneCameraPose a, b; b.yaw = 65;
    shot.sceneCameraAnimations = {{scene.id, {{1, a}, {8, b, KeyframeInterpolation::EaseOut}}}};
    const auto asset = makeAudioAsset("Sound", QByteArray(48000 * audioBytesPerSampleFrame, '\x14'));
    shot.audioClips = {{animation_fixture::id(), "Sound", asset, 0, 111, 30000, .75, false}};
    return project;
}
QVector<int> keyFrames(const Shot &shot) {
    QVector<int> frames;
    for (const auto &key : shot.layerAnimations[0].channels[0].keys) frames.append(key.frame);
    return frames;
}
Project symbolProject() {
    auto project = Project::create(); project.width = project.height = 128; project.transparent = true;
    SymbolDefinition symbol{animation_fixture::id(), "Blink", animation_fixture::project().shots[0]};
    project.symbols.append(symbol);
    auto layer = RasterLayer::create("Blink instance"); layer.kind = LayerKind::Symbol;
    layer.symbol = SymbolInstance{symbol.id, SymbolPlayback::Loop, 20, 0, .5};
    auto &shot = project.shots[0]; shot.durationFrames = 60; shot.layers = {layer};
    shot.layerAnimations = {{layer.id, {}, {{KeyframeProperty::PositionX, {{20, 0}, {50, 30}}}}}};
    return project;
}
}

class TimelineRippleTests : public QObject {
    Q_OBJECT
private slots:
    void insertSplitsHoldsAudioAndRetimesAllKeys() {
        auto project = mixedProject(); const auto before = project;
        QVERIFY2(project.validate().isEmpty(), qPrintable(project.validate()));
        auto &shot = project.shots.first();
        QVERIFY(applyTimelineRipple(shot, project.fps, {TimelineRippleKind::Insert, 2, 3}).isEmpty());
        QCOMPARE(shot.durationFrames, 23);
        const auto &exposures = shot.animationTracks[0].exposures;
        QCOMPARE(exposures.size(), 3);
        QCOMPARE(exposures[0].startFrame, 0); QCOMPARE(exposures[0].durationFrames, 2);
        QCOMPARE(exposures[1].startFrame, 5); QCOMPARE(exposures[1].durationFrames, 2);
        QCOMPARE(exposures[2].startFrame, 7);
        QCOMPARE(exposures[0].id, before.shots[0].animationTracks[0].exposures[0].id);
        QVERIFY(exposures[1].id != exposures[0].id);
        QCOMPARE(exposures[0].drawingId, exposures[1].drawingId);
        QVERIFY(!exposureAtFrame(shot.animationTracks[0], 3));
        QCOMPARE(keyFrames(shot), QVector<int>({1, 11}));
        QCOMPARE(shot.layerAnimations[0].channels[0].keys[0], before.shots[0].layerAnimations[0].channels[0].keys[0]);
        QVERIFY(positionChannelsPaired(shot.layerAnimations[0]));
        QCOMPARE(shot.camera2DKeys.last().frame, 11);
        QCOMPARE(shot.sceneCameraAnimations[0].keys.last().frame, 11);
        QCOMPARE(shot.audioClips.size(), 2);
        QCOMPARE(shot.audioClips[0].sampleCount, qint64(4000));
        QCOMPARE(shot.audioClips[1].startFrame, 5);
        QCOMPARE(shot.audioClips[1].sourceOffsetSamples, qint64(4111));
        QCOMPARE(shot.audioClips[1].sampleCount, qint64(26000));
        QCOMPARE(shot.audioClips[1].asset, before.shots[0].audioClips[0].asset);
        QCOMPARE(shot.layers[3].tiles[TileKey(0, 0)].cacheKey(), before.shots[0].layers[3].tiles[TileKey(0, 0)].cacheKey());
        QVERIFY2(project.validate().isEmpty(), qPrintable(project.validate()));
    }

    void deleteClosesGapKeepsUnusedDrawingsAndFallsBackToCameraBase() {
        auto project = mixedProject(); auto &shot = project.shots[0];
        const auto base = shot.camera2D;
        const auto removedGroup = shot.animationTracks[0].drawings[1].groupLayerId;
        const auto removedLayer = shot.animationTracks[0].drawings[1].templateLayers["ink"];
        shot.layerAnimations.append({removedLayer, {}, {{KeyframeProperty::Opacity, {{4, .5}}}}, {}});
        QVERIFY(applyTimelineRipple(shot, project.fps, {TimelineRippleKind::Delete, 0, 10}).isEmpty());
        QCOMPARE(shot.durationFrames, 10);
        QCOMPARE(shot.animationTracks[0].drawings.size(), 2);
        QVERIFY(shot.animationTracks[0].exposures.isEmpty());
        QVERIFY(animation_fixture::layerIndex(shot, removedGroup) >= 0);
        QVERIFY(animation_fixture::layerIndex(shot, removedLayer) >= 0);
        QVERIFY(shot.layerAnimations.isEmpty());
        QVERIFY(shot.camera2DKeys.isEmpty()); QVERIFY(shot.sceneCameraAnimations.isEmpty());
        QCOMPARE(evaluateShotCamera(shot, 2), base);
        QCOMPARE(shot.audioClips.size(), 1);
        QCOMPARE(shot.audioClips[0].startFrame, 0);
        QCOMPARE(shot.audioClips[0].sourceOffsetSamples, qint64(20111));
        QCOMPARE(shot.audioClips[0].sampleCount, qint64(10000));
        QVERIFY2(project.validate().isEmpty(), qPrintable(project.validate()));
    }

    void unusedDrawingsSurviveEveryRippleWithWholeShotKeyTiming() {
        auto original = animation_fixture::project();
        auto &source = original.shots[0]; const auto drawing = source.animationTracks[0].drawings[0];
        source.animationTracks[0].exposures.removeFirst();
        const auto ink = drawing.templateLayers.value("ink");
        source.layerAnimations.append({ink, {}, {{KeyframeProperty::Opacity, {{1, .2}, {9, .6}, {12, 1.}}}}});
        for (const auto edit : {TimelineRippleEdit{TimelineRippleKind::Insert, 2, 3},
                                TimelineRippleEdit{TimelineRippleKind::Delete, 5, 2},
                                TimelineRippleEdit{TimelineRippleKind::Move, 8, 3, 2}}) {
            auto project = original; auto &shot = project.shots[0];
            const auto error = applyTimelineRipple(shot, project.fps, edit);
            QVERIFY2(error.isEmpty(), qPrintable(error));
            QCOMPARE(shot.animationTracks[0].drawings, source.animationTracks[0].drawings);
            QCOMPARE(shot.layers.size(), source.layers.size());
            const auto index = animation_fixture::layerIndex(shot, ink); QVERIFY(index >= 0);
            QCOMPARE(shot.layers[index].tiles, source.layers[animation_fixture::layerIndex(source, ink)].tiles);
            for (const auto &cel : shot.animationTracks[0].exposures) QVERIFY(cel.drawingId != drawing.id);
            QVector<int> expected;
            for (const auto &key : source.layerAnimations[0].channels[0].keys)
                expected.append(timelineRippleFrame(key.frame, source.durationFrames, edit));
            std::sort(expected.begin(), expected.end());
            QCOMPARE(keyFrames(shot), expected);
            QVERIFY2(project.validate().isEmpty(), qPrintable(project.validate()));
        }
    }

    void moveInEitherDirectionUsesOriginalDestinationAndPreservesMetadata() {
        const auto original = mixedProject();
        for (const auto edit : {TimelineRippleEdit{TimelineRippleKind::Move, 0, 4, 12},
                                TimelineRippleEdit{TimelineRippleKind::Move, 4, 4, 0}}) {
            auto project = original; auto &shot = project.shots[0];
            QVERIFY(applyTimelineRipple(shot, project.fps, edit).isEmpty());
            QCOMPARE(shot.durationFrames, 20);
            QCOMPARE(shot.animationTracks[0].drawings, original.shots[0].animationTracks[0].drawings);
            QVERIFY(positionChannelsPaired(shot.layerAnimations[0]));
            for (const auto &old : original.shots[0].layerAnimations[0].channels[0].keys) {
                auto expected = old; expected.frame = timelineRippleFrame(old.frame, 20, edit);
                QVERIFY(shot.layerAnimations[0].channels[0].keys.contains(expected));
            }
            for (const auto &clip : shot.audioClips) QCOMPARE(clip.asset, original.shots[0].audioClips[0].asset);
            QVERIFY2(project.validate().isEmpty(), qPrintable(project.validate()));
        }
        auto project = original;
        QVERIFY(applyTimelineRipple(project.shots[0], project.fps, {TimelineRippleKind::Move, 0, 4, 12}).isEmpty());
        QCOMPARE(project.shots[0].animationTracks[0].exposures.last().startFrame, 8);
        QCOMPARE(timelineRippleFrame(1, 20, {TimelineRippleKind::Move, 0, 4, 12}), 9);
        QCOMPARE(timelineRippleFrame(8, 20, {TimelineRippleKind::Move, 0, 4, 12}), 4);
    }

    void fractionalAudioCutsUseGlobalSampleClockAndRemoveHiddenTail() {
        auto project = Project::create(); project.fps = 29.97; auto &shot = project.shots[0]; shot.durationFrames = 17;
        auto asset = makeAudioAsset("Sound", QByteArray(48000 * audioBytesPerSampleFrame, '\0'));
        shot.audioClips = {{animation_fixture::id(), "Sound", asset, 1, 7, 47000, 1, false}};
        const auto oldStart = audioSampleFrameAtTimelineFrame(1, project.fps);
        const auto cutStart = audioSampleFrameAtTimelineFrame(5, project.fps);
        const auto cutEnd = audioSampleFrameAtTimelineFrame(8, project.fps);
        const auto oldEnd = audioSampleFrameAtTimelineFrame(17, project.fps);
        QVERIFY(applyTimelineRipple(shot, project.fps, {TimelineRippleKind::Delete, 5, 3}).isEmpty());
        QCOMPARE(shot.audioClips.size(), 2);
        QCOMPARE(shot.audioClips[0].sampleCount, cutStart - oldStart);
        QCOMPARE(shot.audioClips[1].sourceOffsetSamples, 7 + cutEnd - oldStart);
        QCOMPARE(shot.audioClips[1].sampleCount, qMin(oldEnd - cutEnd,
            audioSampleFrameAtTimelineFrame(14, project.fps) - audioSampleFrameAtTimelineFrame(5, project.fps)));
        QCOMPARE(shot.audioClips[1].asset, asset);
    }

    void pathKeysKeepSourceGeometryAndCustomEasing() {
        auto project = Project::create(); auto &shot = project.shots[0]; shot.durationFrames = 30;
        auto &layer = shot.layers[0]; layer.kind = LayerKind::Vector;
        auto object = EditableObject::create();
        object.nodes = {BezierNode::corner({10, 10}), BezierNode::corner({40, 30})};
        layer.objects = {object};
        PathKeyframe a{2, pathPose(object)}, b{12, pathPose(object)};
        a.easing = CubicEasing{{.1, .3}, {.8, .9}};
        b.pose.nodes[1].anchor = {80, 60};
        shot.layerAnimations = {{layer.id, {}, {}, {{object.id, {a, b}}}}};
        QVERIFY2(project.validate().isEmpty(), qPrintable(project.validate()));
        QVERIFY(applyTimelineRipple(shot, project.fps, {TimelineRippleKind::Move, 0, 5, 20}).isEmpty());
        auto expected = a; expected.frame = 17;
        QCOMPARE(shot.layerAnimations[0].paths[0].keys.last(), expected);
        QCOMPARE(shot.layerAnimations[0].paths[0].keys.first().frame, 7);
        QCOMPARE(shot.layers[0].objects[0], object);
        QVERIFY2(project.validate().isEmpty(), qPrintable(project.validate()));
    }

    void deletingAllMotionKeysRetainsOtherChannelsWithoutFollowPath() {
        auto project = mixedProject(); auto &shot = project.shots[0];
        shot.layerAnimations[0].channels.append({KeyframeProperty::Opacity, {{15, .4}}});
        QVERIFY(shot.layerAnimations[0].orientAlongPath);
        QVERIFY(applyTimelineRipple(shot, project.fps, {TimelineRippleKind::Delete, 0, 10}).isEmpty());
        QCOMPARE(shot.layerAnimations.size(), 1);
        QCOMPARE(shot.layerAnimations[0].channels.size(), 1);
        QCOMPARE(shot.layerAnimations[0].channels[0].property, KeyframeProperty::Opacity);
        QCOMPARE(shot.layerAnimations[0].channels[0].keys[0].frame, 5);
        QVERIFY(!shot.layerAnimations[0].orientAlongPath);
        QCOMPARE(evaluateLayerTransform(shot.layerAnimations[0], 5), QTransform{});
        QVERIFY2(project.validate().isEmpty(), qPrintable(project.validate()));
    }

    void locksBoundsAndLimitFailuresAreAtomic() {
        const auto original = mixedProject();
        for (const auto edit : {TimelineRippleEdit{TimelineRippleKind::Insert, -1, 2},
                                TimelineRippleEdit{TimelineRippleKind::Insert, 2, std::numeric_limits<int>::max()},
                                TimelineRippleEdit{TimelineRippleKind::Delete, 0, 20},
                                TimelineRippleEdit{TimelineRippleKind::Delete, 19, 2},
                                TimelineRippleEdit{TimelineRippleKind::Move, 3, 5, 4},
                                TimelineRippleEdit{TimelineRippleKind::Move, 3, 5, 21}}) {
            auto project = original;
            QVERIFY(!applyTimelineRipple(project.shots[0], project.fps, edit).isEmpty());
            QCOMPARE(project.toJson(), original.toJson());
        }
        for (int layer : {0, 1, 3, int(original.shots[0].layers.size()) - 1}) {
            auto project = original;
            project.shots[0].layers[layer].positionLocked = true;
            const auto snapshot = project.toJson();
            QVERIFY(!applyTimelineRipple(project.shots[0], project.fps, {TimelineRippleKind::Insert, 2, 3}).isEmpty());
            QCOMPARE(project.toJson(), snapshot);
        }
        auto project = original;
        project.shots[0].audioClips.clear();
        const auto asset = original.shots[0].audioClips[0].asset;
        for (int i = 0; i < maxAudioClipsPerShot; ++i)
            project.shots[0].audioClips.append({animation_fixture::id(), "Sound", asset, 0, 0, 30000, 1, false});
        const auto snapshot = project.toJson();
        QVERIFY(!applyTimelineRipple(project.shots[0], project.fps, {TimelineRippleKind::Insert, 2, 3}).isEmpty());
        QCOMPARE(project.toJson(), snapshot);
    }

    void identityDoesNotSplitAudioOrTouchLockedContent() {
        auto project = mixedProject(); for (auto &layer : project.shots[0].layers) layer.locked = true;
        const auto snapshot = project.toJson();
        for (int destination : {2, 5})
            QVERIFY(applyTimelineRipple(project.shots[0], project.fps, {TimelineRippleKind::Move, 2, 3, destination}).isEmpty());
        QCOMPARE(project.toJson(), snapshot);
    }

    void symbolStartAndMotionStayTogetherBeforePlayback() {
        const auto original = symbolProject();
        QVERIFY2(original.validate().isEmpty(), qPrintable(original.validate()));
        for (const auto edit : {TimelineRippleEdit{TimelineRippleKind::Insert, 10, 12},
                                TimelineRippleEdit{TimelineRippleKind::Insert, 20, 4},
                                TimelineRippleEdit{TimelineRippleKind::Delete, 5, 8},
                                TimelineRippleEdit{TimelineRippleKind::Move, 2, 5, 18}}) {
            auto project = original; auto &shot = project.shots[0];
            const auto error = applyTimelineRipple(shot, project.fps, edit);
            QVERIFY2(error.isEmpty(), qPrintable(error));
            QCOMPARE(shot.layers[0].symbol->startFrame, timelineRippleFrame(20, 60, edit));
            QCOMPARE(keyFrames(shot).first(), shot.layers[0].symbol->startFrame);
            for (int frame = 20; frame < 60; ++frame)
                QCOMPARE(symbolLocalFrame(*shot.layers[0].symbol, timelineRippleFrame(frame, 60, edit), 20),
                         symbolLocalFrame(*original.shots[0].layers[0].symbol, frame, 20));
            QCOMPARE(project.toJson().value("symbols"), original.toJson().value("symbols"));
            QVERIFY2(project.validate().isEmpty(), qPrintable(project.validate()));
        }
    }

    void symbolCrossingCutsFailAtomicallyInsteadOfChangingItsClock() {
        for (const auto mode : {SymbolPlayback::Loop, SymbolPlayback::Once, SymbolPlayback::PingPong}) {
            auto original = symbolProject(); original.shots[0].layers[0].symbol->playback = mode;
            for (const auto edit : {TimelineRippleEdit{TimelineRippleKind::Insert, 30, 4},
                                    TimelineRippleEdit{TimelineRippleKind::Delete, 30, 4},
                                    TimelineRippleEdit{TimelineRippleKind::Delete, 10, 15},
                                    TimelineRippleEdit{TimelineRippleKind::Move, 30, 4, 45},
                                    TimelineRippleEdit{TimelineRippleKind::Move, 20, 40, 0}}) {
                auto project = original;
                const auto error = applyTimelineRipple(project.shots[0], project.fps, edit);
                QVERIFY2(error.contains("Symbol"), qPrintable(error));
                QCOMPARE(project.toJson(), original.toJson());
            }
        }
    }

    void stillSymbolsSupportInteriorCutsAndRemovedStarts() {
        auto original = symbolProject(); original.shots[0].layers[0].symbol->playback = SymbolPlayback::SingleFrame;
        original.shots[0].layers[0].symbol->firstFrame = 4;
        for (const auto edit : {TimelineRippleEdit{TimelineRippleKind::Insert, 30, 4},
                                TimelineRippleEdit{TimelineRippleKind::Delete, 30, 4},
                                TimelineRippleEdit{TimelineRippleKind::Delete, 10, 15},
                                TimelineRippleEdit{TimelineRippleKind::Move, 30, 4, 45}}) {
            auto project = original; auto &shot = project.shots[0];
            const auto error = applyTimelineRipple(shot, project.fps, edit);
            QVERIFY2(error.isEmpty(), qPrintable(error));
            for (int frame = 0; frame < 60; ++frame) {
                if (edit.kind == TimelineRippleKind::Delete && frame >= edit.startFrame
                    && frame < edit.startFrame + edit.durationFrames) continue;
                QCOMPARE(symbolLocalFrame(*shot.layers[0].symbol, timelineRippleFrame(frame, 60, edit), 20),
                         symbolLocalFrame(*original.shots[0].layers[0].symbol, frame, 20));
            }
            QVERIFY2(project.validate().isEmpty(), qPrintable(project.validate()));
        }
    }

    void exposedSymbolMovesWithItsDrawingWithoutRetimingSharedNestedContent() {
        auto original = symbolProject();
        SymbolDefinition nested{animation_fixture::id(), "Face", Project::create().shots[0]};
        nested.content.durationFrames = 20; nested.content.layers = {original.shots[0].layers[0]};
        nested.content.layers[0].id = animation_fixture::id();
        nested.content.layers[0].symbol->startFrame = 1;
        original.symbols.append(nested);
        auto &shot = original.shots[0]; auto instance = shot.layers[0];
        shot = animation_fixture::project().shots[0]; shot.durationFrames = 60;
        for (auto &layer : shot.layers) layer.tiles.clear();
        auto &track = shot.animationTracks[0];
        track.exposures = {{animation_fixture::id(), track.drawings[0].id, 20, 8}};
        instance.parentId = track.drawings[0].groupLayerId; instance.symbol->symbolId = nested.id;
        instance.symbol->speed = 1; shot.layers.insert(4, instance);
        QVERIFY2(original.validate().isEmpty(), qPrintable(original.validate()));
        for (const auto edit : {TimelineRippleEdit{TimelineRippleKind::Move, 20, 8, 4},
                                TimelineRippleEdit{TimelineRippleKind::Move, 20, 8, 40},
                                TimelineRippleEdit{TimelineRippleKind::Insert, 10, 3}}) {
            auto project = original; auto &moved = project.shots[0];
            const auto error = applyTimelineRipple(moved, project.fps, edit);
            QVERIFY2(error.isEmpty(), qPrintable(error));
            const auto index = animation_fixture::layerIndex(moved, instance.id);
            QCOMPARE(moved.layers[index].symbol->startFrame, timelineRippleFrame(20, 60, edit));
            for (int frame = 20; frame < 28; ++frame)
                QCOMPARE(renderMovieFrame(project, timelineRippleFrame(frame, 60, edit)), renderMovieFrame(original, frame));
            QCOMPARE(project.toJson().value("symbols"), original.toJson().value("symbols"));
            QVERIFY2(project.validate().isEmpty(), qPrintable(project.validate()));
        }
        auto removed = original;
        QVERIFY(applyTimelineRipple(removed.shots[0], removed.fps, {TimelineRippleKind::Delete, 20, 8}).isEmpty());
        const auto unusedIndex = animation_fixture::layerIndex(removed.shots[0], instance.id);
        QVERIFY(unusedIndex >= 0);
        QCOMPARE(removed.shots[0].layers[unusedIndex].symbol, instance.symbol);
        QCOMPARE(removed.shots[0].animationTracks[0].drawings, original.shots[0].animationTracks[0].drawings);
        QVERIFY(removed.shots[0].animationTracks[0].exposures.isEmpty());
        QCOMPARE(removed.toJson().value("symbols"), original.toJson().value("symbols"));
        QVERIFY2(removed.validate().isEmpty(), qPrintable(removed.validate()));
    }

    void repeatedDrawingCannotSilentlyGiveOneSymbolTwoClocks() {
        auto project = animation_fixture::project();
        const auto symbols = symbolProject(); project.symbols = symbols.symbols;
        auto &shot = project.shots[0]; auto &track = shot.animationTracks[0];
        auto layer = symbols.shots[0].layers[0]; layer.parentId = track.drawings[0].groupLayerId;
        layer.symbol->startFrame = 0; shot.layers.insert(4, layer);
        track.exposures.append({animation_fixture::id(), track.drawings[0].id, 10, 3});
        QVERIFY2(project.validate().isEmpty(), qPrintable(project.validate()));
        const auto snapshot = project.toJson();
        const auto error = applyTimelineRipple(shot, project.fps, {TimelineRippleKind::Insert, 8, 3});
        QVERIFY2(error.contains("Symbol"), qPrintable(error));
        QCOMPARE(project.toJson(), snapshot);
    }

    void editingAfterADormantCelKeepsItsSymbolStart() {
        auto project = animation_fixture::project(); const auto symbols = symbolProject();
        project.symbols = symbols.symbols;
        auto &shot = project.shots[0]; shot.durationFrames = 60;
        auto layer = symbols.shots[0].layers[0];
        layer.parentId = shot.animationTracks[0].drawings[0].groupLayerId;
        shot.layers.insert(4, layer);
        QVERIFY2(project.validate().isEmpty(), qPrintable(project.validate()));
        QVERIFY(applyTimelineRipple(shot, project.fps, {TimelineRippleKind::Insert, 40, 3}).isEmpty());
        QCOMPARE(shot.layers[animation_fixture::layerIndex(shot, layer.id)].symbol->startFrame, 20);
    }

    void symbolLocksFutureStartsAndTailDeletionAreSafe() {
        auto original = symbolProject();
        for (bool lockParent : {false, true}) {
            auto project = original; auto &shot = project.shots[0];
            if (lockParent) {
                auto parent = RasterLayer::create("Folder"); parent.group = true; parent.locked = true;
                shot.layers[0].parentId = parent.id; shot.layers.prepend(parent);
            } else shot.layers[0].positionLocked = true;
            const auto snapshot = project.toJson();
            QVERIFY(!applyTimelineRipple(shot, project.fps, {TimelineRippleKind::Insert, 10, 3}).isEmpty());
            QCOMPARE(project.toJson(), snapshot);
        }
        auto project = original; auto &shot = project.shots[0]; shot.layers[0].symbol->startFrame = 80;
        QVERIFY(applyTimelineRipple(shot, project.fps, {TimelineRippleKind::Insert, 60, 5}).isEmpty());
        QCOMPARE(shot.layers[0].symbol->startFrame, 85);
        project = original;
        QVERIFY(applyTimelineRipple(project.shots[0], project.fps, {TimelineRippleKind::Delete, 20, 40}).isEmpty());
        QCOMPARE(project.shots[0].layers[0].symbol->startFrame, 20);
        QCOMPARE(symbolLocalFrame(*project.shots[0].layers[0].symbol, 19, 20), -1);
        QVERIFY2(project.validate().isEmpty(), qPrintable(project.validate()));
        project = original; project.shots[0].layers[0].symbol->startFrame = 1000000;
        const auto snapshot = project.toJson();
        QVERIFY(!applyTimelineRipple(project.shots[0], project.fps, {TimelineRippleKind::Insert, 60, 1}).isEmpty());
        QCOMPARE(project.toJson(), snapshot);
    }
};
QTEST_MAIN(TimelineRippleTests)
#include "timeline_ripple_tests.moc"
