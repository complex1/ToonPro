#include "animation.h"
#include "animation_test_fixture.h"
#include <QtTest>
using namespace toon;
class AnimationTests: public QObject {
    Q_OBJECT
private slots:
    void exposuresAndIndependentVisibility() {
        auto project=animation_fixture::project(); auto &shot=project.shots.first(); auto &track=shot.animationTracks.first();
        QVERIFY2(project.validate().isEmpty(),qPrintable(project.validate()));
        QCOMPARE(exposureAtFrame(track,0)->drawingId,track.drawings[0].id);
        QCOMPARE(drawingAtFrame(track,2)->id,track.drawings[0].id);
        QVERIFY(!drawingAtFrame(track,3)); QVERIFY(!drawingAtFrame(track,-1)); QVERIFY(!drawingAtFrame(track,7));
        const auto original=project.toJson();
        const auto active=activeLayerIds(shot,0); QCOMPARE(active.size(),4);
        QVERIFY(active.contains(shot.layers.first().id)); QVERIFY(active.contains(track.rootGroupLayerId));
        QVERIFY(!active.contains(track.drawings[1].groupLayerId));
        QCOMPARE(evaluateLayers(shot,3).size(),2); QCOMPARE(project.toJson(),original);
        shot.layers[animation_fixture::layerIndex(shot,track.rootGroupLayerId)].visible=false;
        QCOMPARE(activeLayerIds(shot,0),active);
        const auto owner=animationOwner(shot,track.drawings[0].templateLayers.value("ink"));
        QCOMPARE(owner.trackId,track.id); QCOMPARE(owner.drawingId,track.drawings[0].id);
        QCOMPARE(animationOwners(shot).value(track.drawings[0].groupLayerId),owner);
    }
    void rejectsInvalidGraphAndTiming() {
        const auto original=animation_fixture::project();
        auto invalid=original; invalid.shots[0].animationTracks[0].exposures[1].startFrame=2; QVERIFY(!invalid.validate().isEmpty());
        invalid=original; invalid.shots[0].animationTracks[0].exposures[0].durationFrames=0; QVERIFY(!invalid.validate().isEmpty());
        invalid=original; invalid.shots[0].animationTracks[0].exposures[1].durationFrames=100; QVERIFY(!invalid.validate().isEmpty());
        invalid=original; invalid.shots[0].animationTracks[0].exposures[0].drawingId=animation_fixture::id(); QVERIFY(!invalid.validate().isEmpty());
        invalid=original; invalid.shots[0].animationTracks[0].drawings[1].id=invalid.shots[0].animationTracks[0].drawings[0].id; QVERIFY(!invalid.validate().isEmpty());
        invalid=original; invalid.shots[0].layers[2].parentId.clear(); QVERIFY(!invalid.validate().isEmpty());
        invalid=original; auto layer=RasterLayer::create(); layer.parentId=invalid.shots[0].animationTracks[0].rootGroupLayerId;
        invalid.shots[0].layers.append(layer); QVERIFY(!invalid.validate().isEmpty());
        invalid=original; invalid.shots[0].animationTracks[0].drawings[0].templateLayers["ink"]=invalid.shots[0].animationTracks[0].drawings[1].templateLayers.value("ink"); QVERIFY(!invalid.validate().isEmpty());
        invalid=original; invalid.shots[0].layers[2].visible=false; QVERIFY(!invalid.validate().isEmpty());
    }
    void ownershipProtectionAndStaticInterleaving() {
        const auto original=animation_fixture::project(); auto changed=original;
        const auto &track=original.shots[0].animationTracks[0];
        changed.shots[0].layers[3].parentId=track.drawings[1].groupLayerId;
        changed.shots[0].animationTracks[0].drawings[0].templateLayers.clear();
        QVERIFY(!validateAnimationMutation(original.shots[0],changed.shots[0]).isEmpty());
        changed=original; changed.shots[0].layers[3].name="Clean linework";
        QVERIFY(validateAnimationMutation(original.shots[0],changed.shots[0]).isEmpty());
        auto staticTop=RasterLayer::create("Foreground"); changed.shots[0].layers.append(staticTop);
        const auto evaluated=evaluateLayers(changed.shots[0],4);
        QCOMPARE(evaluated.first().id,original.shots[0].layers.first().id); QCOMPARE(evaluated.last().id,staticTop.id);
    }
    void boundedTemplatesAndStrictJson() {
        const auto rough=drawingTemplateSlots(DrawingTemplate::Rough); QCOMPARE(rough.size(),3); QCOMPARE(rough.last().name,QString("Clean-up"));
        const auto anime=drawingTemplateSlots(DrawingTemplate::Anime); QCOMPARE(anime.size(),6); QCOMPARE(anime.last().id,QString("lines"));
        auto project=animation_fixture::project(); const auto &tracks=project.shots[0].animationTracks;
        QVector<AnimationTrack> restored; QString error;
        QVERIFY(animationTracksFromJson(animationTracksToJson(tracks),&restored,&error)); QCOMPARE(restored,tracks);
        auto json=animationTracksToJson(tracks); auto object=json[0].toObject(); object["unexpected"]=true; json[0]=object;
        QVERIFY(!animationTracksFromJson(json,&restored,&error));
        object.remove("unexpected"); auto exposures=object["exposures"].toArray(); auto exposure=exposures[0].toObject(); exposure["startFrame"]=.5; exposures[0]=exposure; object["exposures"]=exposures; json[0]=object;
        QVERIFY(!animationTracksFromJson(json,&restored,&error));
        auto staticProject=Project::create(); for(int i=0;i<64;++i) staticProject.shots[0].layers.append(RasterLayer::create()); QVERIFY(!staticProject.validate().isEmpty());
        for(int i=0;i<62;++i) { auto layer=RasterLayer::create(); layer.parentId=tracks[0].drawings[0].groupLayerId; project.shots[0].layers.append(layer); }
        QVERIFY2(project.validate().isEmpty(),qPrintable(project.validate()));
        auto layer=RasterLayer::create(); layer.parentId=tracks[0].drawings[0].groupLayerId; project.shots[0].layers.append(layer); QVERIFY(!project.validate().isEmpty());
    }
};
QTEST_APPLESS_MAIN(AnimationTests)
#include "animation_tests.moc"
