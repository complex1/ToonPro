#include "document.h"
#include "animation_test_fixture.h"
#include <QtTest>
using namespace toon;
class AnimationDocumentTests: public QObject {
    Q_OBJECT
private slots:
    void frameChangeIsViewOnlyAndRemapsRoles() {
        Document document; document.load(animation_fixture::project());
        const auto &shot=document.project().shots.first(); const auto track=shot.animationTracks.first();
        QVERIFY(document.selectAnimationTrack(track.id));
        const auto snapshot=document.project().toJson(); const auto history=document.history()->count();
        QSignalSpy frames(&document,&Document::frameChanged);
        QVERIFY(document.setCurrentFrame(4)); QCOMPARE(frames.size(),1);
        QCOMPARE(document.project().shots[0].layers[document.activeLayer()].id,track.drawings[1].templateLayers.value("ink"));
        QCOMPARE(document.project().toJson(),snapshot); QCOMPARE(document.history()->count(),history); QVERIFY(document.history()->isClean());
        const int active=document.activeLayer(); document.selectLayer(animation_fixture::layerIndex(shot,track.drawings[0].templateLayers.value("ink"))); QCOMPARE(document.activeLayer(),active);
        QVERIFY(document.setCurrentFrame(3)); QCOMPARE(document.project().shots[0].layers[document.activeLayer()].id,track.rootGroupLayerId);
        BrushSettings brush; QVERIFY(!document.beginStroke({10,10},1,brush,false));
        QVERIFY(document.setCurrentFrame(0)); QVERIFY(document.beginStroke({10,10},1,brush,false));
        QVERIFY(!document.setCurrentFrame(4)); QCOMPARE(document.currentFrame(),0);
        document.cancelStroke(); QVERIFY(document.setCurrentFrame(4));
    }
    void createSplitDuplicateAndUndo() {
        Document document; auto project=Project::create(); project.width=128; project.height=128; document.load(project);
        QVERIFY(document.addAnimationTrack("Character",DrawingTemplate::Anime)); QCOMPARE(document.history()->count(),1);
        QCOMPARE(document.project().shots[0].animationTracks[0].drawingTemplate.size(),6);
        QVERIFY(document.setAnimationExposureTiming(0,6));
        const auto firstId=document.project().shots[0].animationTracks[0].drawings[0].id;
        QVERIFY(document.setCurrentFrame(2)); QVERIFY(document.createAnimationDrawing());
        const auto track=document.project().shots[0].animationTracks[0];
        QCOMPARE(track.exposures.size(),2); QCOMPARE(track.exposures[0].durationFrames,2); QCOMPARE(track.exposures[1].startFrame,2); QCOMPARE(track.exposures[1].durationFrames,4);
        QCOMPARE(track.exposures[0].drawingId,firstId);
        BrushSettings brush; brush.size=8; brush.color=Qt::red;
        QVERIFY(document.beginStroke({20,20},1,brush,false)); document.endStroke();
        const auto source=document.project().shots[0].layers[document.activeLayer()];
        QVERIFY(document.createAnimationDrawing(true)); QCOMPARE(document.currentFrame(),6);
        const auto copy=document.project().shots[0].layers[document.activeLayer()]; QVERIFY(copy.id!=source.id); QCOMPARE(copy.tiles,source.tiles);
        brush.color=Qt::blue; QVERIFY(document.beginStroke({40,40},1,brush,false)); document.endStroke();
        const int sourceIndex=animation_fixture::layerIndex(document.project().shots[0],source.id); QCOMPARE(document.project().shots[0].layers[sourceIndex].tiles,source.tiles);
        document.history()->undo(); QCOMPARE(document.project().shots[0].layers[document.activeLayer()].tiles,copy.tiles);
        document.history()->undo(); QCOMPARE(document.project().shots[0].animationTracks[0].drawings.size(),2); QCOMPARE(document.currentFrame(),2);
        document.history()->redo(); QCOMPARE(document.project().shots[0].animationTracks[0].drawings.size(),3);
    }
    void collisionsDoNotReplaceDrawings() {
        Document document; document.load(animation_fixture::project()); const auto track=document.project().shots[0].animationTracks[0]; QVERIFY(document.selectAnimationTrack(track.id));
        const auto snapshot=document.project().toJson();
        QVERIFY(!document.createAnimationDrawing(false,2)); QCOMPARE(document.project().toJson(),snapshot); QCOMPARE(document.currentFrame(),0); QCOMPARE(document.history()->count(),0);
        QVERIFY(!document.setAnimationExposureTiming(1,5)); QCOMPARE(document.project().toJson(),snapshot);
        QVERIFY(document.setCurrentFrame(3)); QVERIFY(document.createAnimationDrawing(true)); QCOMPARE(document.currentFrame(),3); QCOMPARE(document.project().shots[0].animationTracks[0].drawings.size(),3);
    }
    void protectedContainersAndOrdinaryLayerEdits() {
        Document document; document.load(animation_fixture::project()); const auto track=document.project().shots[0].animationTracks[0]; QVERIFY(document.selectAnimationTrack(track.id));
        const auto snapshot=document.project().toJson();
        document.deleteLayers({track.drawings[0].groupLayerId}); QCOMPARE(document.project().toJson(),snapshot);
        document.duplicateLayers({track.rootGroupLayerId}); QCOMPARE(document.project().toJson(),snapshot);
        document.moveLayers({track.drawings[0].templateLayers.value("ink")},track.drawings[1].groupLayerId); QCOMPARE(document.project().toJson(),snapshot);
        QVERIFY(!document.flattenImage()); QCOMPARE(document.project().toJson(),snapshot);
        document.selectLayer(animation_fixture::layerIndex(document.project().shots[0],track.drawings[0].groupLayerId));
        QVERIFY(document.beginLayerPropertyEdit()); document.previewLayerOpacity(.2); document.previewLayersVisible({track.drawings[0].groupLayerId},false); document.commitLayerPropertyEdit(); QCOMPARE(document.project().toJson(),snapshot);
        QVERIFY(document.selectAnimationTrack(track.id)); document.addLayer(); QCOMPARE(document.project().shots[0].layers.size(),7); QVERIFY(document.project().validate().isEmpty());
        document.deleteLayer(); QVERIFY(document.project().validate().isEmpty());
        QCOMPARE(document.currentFrame(),0);
        QCOMPARE(document.project().shots[0].layers[document.activeLayer()].id,track.drawings[0].templateLayers.value("ink"));
        QCOMPARE(document.history()->count(),2);
    }
    void undoRevealsTheEditedDrawingAndHoldsAvoidSelectionRefresh() {
        Document document; document.load(animation_fixture::project()); const auto track=document.project().shots[0].animationTracks[0]; QVERIFY(document.selectAnimationTrack(track.id));
        QSignalSpy selections(&document,&Document::selectionChanged); QSignalSpy frames(&document,&Document::frameChanged);
        QVERIFY(document.setCurrentFrame(1)); QCOMPARE(selections.size(),0); QCOMPARE(frames.size(),1);
        BrushSettings brush; brush.color=Qt::green; brush.size=6;
        QVERIFY(document.beginStroke({10,10},1,brush,false)); document.endStroke();
        QVERIFY(document.setCurrentFrame(4));
        QPainterPath otherSelection; otherSelection.addRect(80,80,12,12); document.setRasterSelection(otherSelection);
        document.history()->undo(); QCOMPARE(document.currentFrame(),0); QVERIFY(!document.rasterSelection().active);
        QCOMPARE(document.project().shots[0].layers[document.activeLayer()].id,track.drawings[0].templateLayers.value("ink"));
        QVERIFY(document.setCurrentFrame(4)); document.setRasterSelection(otherSelection);
        document.history()->redo(); QCOMPARE(document.currentFrame(),0); QVERIFY(!document.rasterSelection().active);
    }
    void frameJumpPreservesExplicitTransformSelectionHistory() {
        Document document; document.load(animation_fixture::project()); const auto track=document.project().shots[0].animationTracks[0]; QVERIFY(document.selectAnimationTrack(track.id));
        QPainterPath selection; selection.addRect(10,10,12,12); document.setRasterSelection(selection);
        QVERIFY(document.beginTransform()); TransformParameters parameters; parameters.translation={20,0};
        QVERIFY(document.previewTransform(parameters)); QVERIFY(document.applyTransform());
        const auto transformed=document.rasterSelection().path;
        QVERIFY(transformed!=selection);
        QPainterPath otherSelection; otherSelection.addRect(80,80,8,8);
        QVERIFY(document.setCurrentFrame(4)); document.setRasterSelection(otherSelection);
        document.history()->undo(); QCOMPARE(document.currentFrame(),0); QVERIFY(document.rasterSelection().active); QCOMPARE(document.rasterSelection().path,selection);
        QVERIFY(document.setCurrentFrame(4)); document.setRasterSelection(otherSelection);
        document.history()->redo(); QCOMPARE(document.currentFrame(),0); QVERIFY(document.rasterSelection().active); QCOMPARE(document.rasterSelection().path,transformed);
    }
    void reusedDrawingSurvivesRemovingOneExposure() {
        auto project=animation_fixture::project(); auto &track=project.shots[0].animationTracks[0];
        track.exposures.append({animation_fixture::id(),track.drawings[0].id,10,2});
        Document document; document.load(project); QVERIFY(document.selectAnimationTrack(track.id));
        QVERIFY(document.removeAnimationExposure()); QCOMPARE(document.project().shots[0].animationTracks[0].drawings.size(),2); QCOMPARE(document.project().shots[0].layers.size(),6);
        QVERIFY(document.setCurrentFrame(10)); QVERIFY(document.removeAnimationExposure()); QCOMPARE(document.project().shots[0].animationTracks[0].drawings.size(),2); QCOMPARE(document.project().shots[0].layers.size(),6);
        document.history()->undo(); QCOMPARE(document.project().shots[0].layers.size(),6); QVERIFY(document.project().validate().isEmpty());
    }
    void drawingLocksProtectTimingAndRename() {
        auto project=animation_fixture::project(); const auto track=project.shots[0].animationTracks[0];
        project.shots[0].layers[animation_fixture::layerIndex(project.shots[0],track.drawings[0].groupLayerId)].locked=true;
        Document document; document.load(project); QVERIFY(document.selectAnimationTrack(track.id)); const auto snapshot=document.project().toJson();
        QVERIFY(!document.renameAnimationDrawing("Changed")); QVERIFY(!document.setAnimationExposureTiming(0,2)); QVERIFY(!document.removeAnimationExposure());
        QVERIFY(document.setCurrentFrame(1)); QVERIFY(!document.createAnimationDrawing()); QCOMPARE(document.project().toJson(),snapshot); QCOMPARE(document.history()->count(),0);
    }
    void brushBudgetIncludesInactiveDrawings() {
        auto project=animation_fixture::project(); project.width=512; project.height=512;
        const auto track=project.shots[0].animationTracks[0];
        auto &inactive=project.shots[0].layers[animation_fixture::layerIndex(project.shots[0],track.drawings[1].templateLayers.value("ink"))];
        const auto tile=inactive.tiles.first(); inactive.tiles.clear();
        for(int i=0;i<maxProjectTiles-1;++i) inactive.tiles.insert({i%32,i/32},tile);
        for(int i=0;i<62;++i) { auto layer=RasterLayer::create(); layer.parentId=track.drawings[1].groupLayerId; project.shots[0].layers.append(layer); }
        QVERIFY2(project.validate().isEmpty(),qPrintable(project.validate()));
        Document document; document.load(project); QVERIFY(document.selectAnimationTrack(track.id));
        BrushSettings brush; brush.size=8; brush.color=Qt::green;
        QVERIFY(document.beginStroke({20,20},1,brush,false)); document.endStroke();
        const auto before=document.project().toJson(); const auto history=document.history()->count();
        QVERIFY(!document.beginStroke({300,20},1,brush,false)); QVERIFY(!document.strokeActive());
        QCOMPARE(document.history()->count(),history); QCOMPARE(document.project().toJson(),before);
    }
    void removeExposureTrackAndConvertAreUndoable() {
        Document document; document.load(animation_fixture::project()); const auto track=document.project().shots[0].animationTracks[0]; QVERIFY(document.selectAnimationTrack(track.id));
        const auto original=document.project().toJson();
        QVERIFY(document.removeAnimationExposure()); QCOMPARE(document.project().shots[0].animationTracks[0].drawings.size(),2); QCOMPARE(document.project().shots[0].layers.size(),6); QVERIFY(!drawingAtFrame(document.project().shots[0].animationTracks[0],0));
        document.history()->undo(); QCOMPARE(document.project().toJson(),original);
        QVERIFY(document.removeAnimationTrack()); QVERIFY(document.project().shots[0].animationTracks.isEmpty()); QCOMPARE(document.project().shots[0].layers.size(),1);
        document.history()->undo(); QCOMPARE(document.project().toJson(),original);
        Document conversion; conversion.load(Project::create()); const auto layerId=conversion.project().shots[0].layers[0].id;
        QVERIFY(conversion.convertLayerToAnimation("Character")); QCOMPARE(conversion.project().shots[0].animationTracks[0].exposures[0].durationFrames,120);
        QCOMPARE(animationOwner(conversion.project().shots[0],layerId).drawingId,conversion.project().shots[0].animationTracks[0].drawings[0].id);
        conversion.history()->undo(); QVERIFY(conversion.project().shots[0].animationTracks.isEmpty()); QCOMPARE(conversion.project().shots[0].layers[0].id,layerId);
    }
};
QTEST_MAIN(AnimationDocumentTests)
#include "animation_document_tests.moc"
