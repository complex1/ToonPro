#include "animation_test_fixture.h"
#include "canvas.h"
#include "document.h"
#include "layer_panel.h"
#include "timeline_controller.h"
#include "ui_theme.h"
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <QtTest>

using namespace toon;
namespace {
Project fixture() {
    auto p = animation_fixture::project();
    auto folder = RasterLayer::create("Backgrounds"); folder.group = true;
    auto child = RasterLayer::create("City"); child.parentId = folder.id;
    p.shots[0].layers.append(folder); p.shots[0].layers.append(child);
    auto second = Project::create().shots[0]; second.name = "Second shot"; p.shots.append(second);
    return p;
}
QString id(const Canvas &canvas,const QString &name) {
    for (const auto &layer : canvas.shot().layers) if (layer.name == name) return layer.id;
    return {};
}
const RasterLayer &layer(const Canvas &canvas,const QString &id) {
    for (const auto &layer : canvas.shot().layers) if (layer.id == id) return layer;
    qFatal("Missing layer in test");
}
}
class TimelineOrganizationControlsTests final : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { applyTheme(); QVERIFY2(fixture().validate().isEmpty(),qPrintable(fixture().validate())); }
    void visibilityLocksShareDocumentAndUndoWithoutChangingTiming() {
        Canvas canvas; canvas.setProject(fixture()); TimelineController controller(&canvas);
        const auto track = canvas.shot().animationTracks.first();
        QVERIFY(controller.selectTrack(track.id));
        const auto cel = track.exposures.first(); controller.setSelectedCels({{track.id,cel.id}});
        const auto selection = controller.selectedCels(); const auto active = controller.selectedPropertyLayerId();
        const auto before = canvas.project().toJson();
        QVERIFY(controller.setTimelineLayerVisible(track.rootGroupLayerId,false));
        QCOMPARE(layer(canvas,track.rootGroupLayerId).visible,false); QCOMPARE(controller.selectedCels(),selection);
        QCOMPARE(controller.selectedPropertyLayerId(),active); QCOMPARE(canvas.shot().animationTracks.first(),track);
        QVERIFY(controller.setTimelineLayerVisible(track.rootGroupLayerId,false)); QCOMPARE(canvas.document()->history()->count(),1);
        QVERIFY(controller.setTimelineLayerLocked(track.rootGroupLayerId,true));
        QCOMPARE(layer(canvas,track.rootGroupLayerId).locked,true); QVERIFY(!controller.moveSelectedCels(1));
        QCOMPARE(canvas.document()->history()->count(),2); canvas.document()->history()->undo(); canvas.document()->history()->undo();
        QCOMPARE(canvas.project().toJson(),before);
        canvas.document()->setLayersVisible({track.rootGroupLayerId},false);
        QVERIFY(controller.setTimelineLayerVisible(track.rootGroupLayerId,true)); QVERIFY(layer(canvas,track.rootGroupLayerId).visible);
    }
    void nestedSelectionAndFolderMovesKeepDrawingIdentity() {
        Canvas canvas; canvas.setProject(fixture()); TimelineController controller(&canvas);
        const auto folder = id(canvas,"Backgrounds"), child = id(canvas,"City");
        QVERIFY(controller.selectTimelineLayer(child)); QCOMPARE(controller.selectedTrackId(),child);
        QCOMPARE(controller.selectedPropertyLayerId(),child);
        QVERIFY(controller.selectTimelineLayer(folder)); QCOMPARE(controller.selectedTrackId(),folder);
        QCOMPARE(controller.selectedPropertyLayerId(),folder);
        const auto tracks = canvas.shot().animationTracks;
        QVERIFY(controller.moveTimelineLayer(tracks[0].rootGroupLayerId,folder,{}));
        QCOMPARE(layer(canvas,tracks[0].rootGroupLayerId).parentId,folder); QCOMPARE(canvas.shot().animationTracks,tracks);
        QVERIFY(controller.selectTimelineLayer(tracks[0].rootGroupLayerId)); QCOMPARE(controller.selectedTrackId(),tracks[0].id);
        QCOMPARE(controller.currentFrame(),0); QCOMPARE(canvas.document()->history()->count(),1);
        canvas.document()->history()->undo(); QVERIFY(layer(canvas,tracks[0].rootGroupLayerId).parentId.isEmpty());
    }
    void layersPanelTracksTimelineHierarchyAndSharedControls() {
        Canvas canvas; canvas.setProject(fixture()); TimelineController controller(&canvas);
        LayerPanel panel(canvas.document()); panel.resize(320,600); panel.show();
        auto *tree=panel.findChild<QTreeWidget *>(); QVERIFY(tree);
        auto item=[&](const QString &layerId)->QTreeWidgetItem * {
            for(QTreeWidgetItemIterator it(tree);*it;++it)
                if((*it)->data(0,Qt::UserRole).toString()==layerId)return *it;
            return nullptr;
        };
        const auto root=canvas.shot().animationTracks.first().rootGroupLayerId;
        const auto folder=id(canvas,"Backgrounds");
        QTRY_VERIFY(item(root)); QVERIFY(!item(root)->parent());
        QVERIFY(controller.moveTimelineLayer(root,folder,{}));
        QTRY_VERIFY(item(root)&&item(root)->parent());
        QCOMPARE(item(root)->parent()->data(0,Qt::UserRole).toString(),folder);
        QVERIFY(controller.setTimelineLayerVisible(root,false));
        QTRY_COMPARE(item(root)->data(0,Qt::AccessibleTextRole).toString(),QString("Hidden"));
        QVERIFY(controller.setTimelineLayerLocked(root,true));
        QTRY_COMPARE(item(root)->data(2,Qt::AccessibleTextRole).toString(),QString("Locked"));
        canvas.document()->history()->undo(); canvas.document()->history()->undo();
        QTRY_COMPARE(item(root)->data(0,Qt::AccessibleTextRole).toString(),QString("Visible"));
        QTRY_COMPARE(item(root)->data(2,Qt::AccessibleTextRole).toString(),QString("Unlocked"));
        canvas.document()->history()->undo();
        QTRY_VERIFY(item(root)&&!item(root)->parent());
    }
    void collapseIsPerShotViewStateAndResetsOnReload() {
        Canvas canvas; const auto project = fixture(); canvas.setProject(project); TimelineController controller(&canvas);
        const auto folder = id(canvas,"Backgrounds"); const auto shot = canvas.document()->activeShotId();
        const auto before = canvas.project().toJson();
        controller.setPendingEdits(true);
        QVERIFY(controller.setTimelineFolderCollapsed(folder,true)); QVERIFY(controller.hasPendingEdits());
        QVERIFY(controller.timelineFolderCollapsed(folder)); QCOMPARE(canvas.project().toJson(),before);
        QCOMPARE(canvas.document()->history()->count(),0); controller.setPendingEdits(false);
        QVERIFY(canvas.document()->selectShot(project.shots[1].id)); QVERIFY(!controller.timelineFolderCollapsed(folder));
        QVERIFY(canvas.document()->selectShot(shot)); QVERIFY(controller.timelineFolderCollapsed(folder));
        canvas.setProject(project); QVERIFY(!controller.timelineFolderCollapsed(folder));
    }
    void rejectedTransitionsLeaveStatesAndDraftsAlone() {
        Canvas canvas; canvas.setProject(fixture()); TimelineController controller(&canvas);
        const auto folder = id(canvas,"Backgrounds"), child = id(canvas,"City"); const auto before = canvas.project().toJson();
        controller.setPendingEdits(true); controller.setTransitionGuard([] { return false; });
        QVERIFY(!controller.setTimelineLayerVisible(child,false)); QVERIFY(!controller.setTimelineLayerLocked(child,true));
        QVERIFY(!controller.moveTimelineLayer(child,{},{})); QVERIFY(!controller.addTimelineFolder(folder,"Props"));
        QVERIFY(!controller.selectTimelineLayer(folder)); QVERIFY(!controller.toggleTimelineLayerSolo(child));
        QVERIFY(controller.hasPendingEdits()); QCOMPARE(canvas.project().toJson(),before);
        QVERIFY(controller.timelineSoloLayers().isEmpty()); QCOMPARE(canvas.document()->history()->count(),0);
        controller.setTransitionGuard({}); controller.setPendingEdits(false);
    }
    void sameIdReloadDuringResolutionCannotReceiveOldCommand() {
        for (int operation=0; operation<5; ++operation) {
            Canvas canvas; const auto original = fixture(); canvas.setProject(original); TimelineController controller(&canvas);
            const auto child = id(canvas,"City"), folder = id(canvas,"Backgrounds");
            controller.setPendingEdits(true);
            controller.setTransitionGuard([&] { canvas.setProject(original); return true; });
            const bool accepted = operation==0 ? controller.setTimelineLayerVisible(child,false)
                : operation==1 ? controller.setTimelineLayerLocked(child,true)
                : operation==2 ? controller.moveTimelineLayer(child,{},{})
                : operation==3 ? controller.addTimelineFolder(folder,"Props") : controller.toggleTimelineLayerSolo(child);
            QVERIFY(!accepted); QCOMPARE(canvas.project().toJson(),original.toJson());
            QVERIFY(controller.timelineSoloLayers().isEmpty()); QCOMPARE(canvas.document()->history()->count(),0);
        }
    }
    void soloCanPreviewDuringPlaybackWithoutDirtyingProject() {
        Canvas canvas; canvas.setProject(fixture()); TimelineController controller(&canvas);
        const auto folder = id(canvas,"Backgrounds"), child = id(canvas,"City"); const auto before=canvas.project().toJson();
        controller.setPlaying(true); QVERIFY(controller.playing()); QVERIFY(controller.toggleTimelineLayerSolo(folder));
        QVERIFY(controller.playing()); QCOMPARE(controller.timelineSoloLayers(),QSet<QString>{folder});
        QVERIFY(controller.toggleTimelineLayerSolo(child)); QCOMPARE(controller.timelineSoloLayers(),(QSet<QString>{folder,child}));
        QVERIFY(controller.clearTimelineSolo()); QVERIFY(controller.playing()); QVERIFY(controller.timelineSoloLayers().isEmpty());
        controller.setPlaying(false); QCOMPARE(canvas.project().toJson(),before); QCOMPARE(canvas.document()->history()->count(),0);
    }
    void protectedDrawingContainersAndUnexposedArtworkStayProtected() {
        Canvas canvas; canvas.setProject(fixture()); TimelineController controller(&canvas);
        const auto track=canvas.shot().animationTracks.first(); const auto group=track.drawings[1].groupLayerId;
        const auto hidden=track.drawings[1].templateLayers.value("ink"); const auto active=controller.selectedPropertyLayerId();
        QVERIFY(!controller.setTimelineLayerVisible(group,false)); QVERIFY(!controller.setTimelineLayerLocked(group,true));
        QVERIFY(!controller.setTimelineFolderCollapsed(group,true)); QVERIFY(!controller.selectTimelineLayer(hidden));
        QCOMPARE(controller.selectedPropertyLayerId(),active); QCOMPARE(controller.currentFrame(),0); QCOMPARE(canvas.document()->history()->count(),0);
        QVERIFY(controller.setTimelineFolderCollapsed(track.rootGroupLayerId,true));
    }
    void folderCreationIsUndoableAndSelectsTheNewFolder() {
        Canvas canvas; canvas.setProject(fixture()); TimelineController controller(&canvas);
        const auto parent = id(canvas,"Backgrounds"); QVERIFY(controller.setTimelineFolderCollapsed(parent,true));
        QVERIFY(controller.addTimelineFolder(parent,"Props")); const auto created=id(canvas,"Props"); QVERIFY(!created.isEmpty());
        QCOMPARE(layer(canvas,created).parentId,parent); QCOMPARE(controller.selectedTrackId(),created);
        QVERIFY(!controller.timelineFolderCollapsed(parent)); QCOMPARE(canvas.document()->history()->count(),1);
        canvas.document()->history()->undo(); QVERIFY(id(canvas,"Props").isEmpty());
        canvas.document()->history()->redo(); QCOMPARE(id(canvas,"Props"),created);
    }
    void delegatedSelectionResolvesOnceAndFoldersDoNotSelectHiddenDrawings() {
        auto p=fixture(); auto folder=RasterLayer::create("Cast"); folder.group=true;
        const auto root=p.shots[0].animationTracks[0].rootGroupLayerId;
        for(auto &layer:p.shots[0].layers)if(layer.id==root)layer.parentId=folder.id;
        p.shots[0].layers.append(folder); Canvas canvas; canvas.setProject(p); TimelineController controller(&canvas);
        int resolutions=0; controller.setTransitionGuard([&]{++resolutions;return true;});
        QVERIFY(controller.selectTimelineLayer(root)); QCOMPARE(resolutions,1);
        QVERIFY(controller.selectTrack(folder.id)); QCOMPARE(controller.selectedPropertyLayerId(),folder.id);
        QCOMPARE(controller.selectedTrackId(),folder.id); QCOMPARE(controller.currentFrame(),0);
        controller.setTransitionGuard([&]{canvas.setProject(p);return true;});
        QVERIFY(!controller.selectTimelineLayer(root)); QCOMPARE(canvas.project().toJson(),p.toJson());
        QCOMPARE(canvas.document()->history()->count(),0);
    }
};
QTEST_MAIN(TimelineOrganizationControlsTests)
#include "test_timeline_organization_controls.moc"
