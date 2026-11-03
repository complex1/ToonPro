#include "document.h"
#include "advanced_transform.h"
#include "clipboard_tools.h"
#include "object_tools.h"
#include "raster_tools.h"
#include "scene_model_io.h"
#include "animation_test_fixture.h"
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>
using namespace toon;
namespace {
QString id() { return QUuid::createUuid().toString(QUuid::WithoutBraces); }
Project blank() { auto p = Project::create(); p.width = p.height = 128; p.transparent = true; p.shots[0].sheet.overscan = 0; return p; }
void tap(Document &d, QColor color, QPointF point = {30,30}) {
    BrushSettings b; b.size = 12; b.pressureSize = false; b.color = color;
    QVERIFY(d.beginStroke(point, 1, b, false)); d.endStroke();
}
QJsonObject shotJson(const Document &d, int index) { return d.project().toJson()["shots"].toArray()[index].toObject(); }
Project rich() {
    auto p = animation_fixture::project(); auto &s = p.shots[0]; s.sheet.overscan = 0;
    s.scene = "Opening";
    auto vector = RasterLayer::create("Vector prop"); vector.kind = LayerKind::Vector;
    auto path = EditableObject::create(); path.nodes = {BezierNode::corner({20,20}),BezierNode::corner({65,30})};
    vector.objects.append(path); QString error; rebuildObjectLayer(vector,{0,0,128,128},maxProjectTiles,&error);
    s.layers.append(vector);
    LayerAnimation animation; animation.layerId = vector.id; animation.pivot = {40,25};
    animation.channels.append({KeyframeProperty::PositionX,{{0,0},{10,15}}});
    auto pose = pathPose(path); auto moved = pose; moved.nodes[1].anchor += QPointF(0,20); moved.nodes[1].inControl = moved.nodes[1].outControl = moved.nodes[1].anchor;
    animation.paths.append({path.id,{{0,pose},{10,moved}}}); s.layerAnimations.append(animation);
    QImage reference(8,8,QImage::Format_ARGB32_Premultiplied); reference.fill(Qt::cyan);
    s.references.append({id(),"Reference",reference});
    DrawingGuide guide; guide.id = id(); guide.name = "Horizon"; guide.type = GuideType::ParallelRuler; guide.points = {{0,50},{100,50}}; s.guides.append(guide);
    QPainterPath selection; selection.addRect(4,4,20,20); s.savedSelections.append({id(),"Face",selection,2});
    const auto audio = makeAudioAsset("Voice",QByteArray(4800*4,'\x10'));
    s.audioClips.append({id(),"Voice",audio,1,0,4800,.8,false});
    s.camera2DKeys = {{0,{}},{10,{{15,5},1.2,5}}};
    Model3D model; model.name = "Triangle"; ModelMesh3D mesh;
    mesh.positions={{0,0,0},{1,0,0},{0,1,0}}; mesh.normals=QVector<QVector3D>(3,{0,0,1}); mesh.indices={0,1,2}; model.meshes.append(mesh);
    auto asset = finalizeModel3D(std::move(model));
    auto sceneLayer = RasterLayer::create("Reference scene"); sceneLayer.kind = LayerKind::Scene3D; sceneLayer.scene3d.emplace();
    sceneLayer.scene3d->models.insert(asset->id,asset);
    ModelInstance3D instance; instance.id=id(); instance.name="Rigged triangle"; instance.modelId=asset->id;
    instance.rig.joints={{"root","Root",{},"mirror",{0,0,0}}, {"mirror","Mirror",{},"root",{1,0,0}}, {"elbow","Elbow","root",{},{0,1,0}}, {"tip","Tip","elbow",{},{0,2,0}}};
    Skin3D skin; for(const auto &joint:instance.rig.joints)skin.jointIds.append(joint.id);
    SkinVertex3D vertex; vertex.weights[0]=1; skin.meshes.append(QVector<SkinVertex3D>(3,vertex)); instance.skin=skin;
    instance.pose.joints={{"elbow",{0,0,15}}}; instance.pose.limits={{"elbow",{-45,-45,-45},{45,45,45},true}};
    instance.pose.targets={{"tip",{.2f,1.8f,0},{1,1,0},false}};
    sceneLayer.scene3d->objects.append(instance); s.layers.append(sceneLayer);
    s.sceneCameraAnimations.append({sceneLayer.id,{{0,{}},{10,{{0,1,0},60,20,6,50,4}}}});
    return p;
}
QSet<QString> owned(const Shot &s) {
    QSet<QString> result{s.id};
    for(const auto &l:s.layers){result.insert(l.id);for(const auto&o:l.objects)result.insert(o.id);if(l.scene3d)for(const auto&o:l.scene3d->objects){result.insert(o.id);for(const auto&j:o.rig.joints)result.insert(j.id);}}
    for(const auto&t:s.animationTracks){result.insert(t.id);for(const auto&d:t.drawings)result.insert(d.id);for(const auto&e:t.exposures)result.insert(e.id);}
    for(const auto&r:s.references)result.insert(r.id);for(const auto&g:s.guides)result.insert(g.id);for(const auto&v:s.savedSelections)result.insert(v.id);for(const auto&a:s.audioClips)result.insert(a.id);
    return result;
}
}
class ShotsTest : public QObject {
    Q_OBJECT
private slots:
    void threeShotsKeepDrawingFramesAndSelectedLayers() {
        Document d; d.load(blank()); const auto a=d.activeShotId(); tap(d,Qt::red);
        QVERIFY(d.setCurrentFrame(7)); d.addLayer(); const auto aLayer=d.activeShot().layers[d.activeLayer()].id;
        const auto b=d.createShot("Middle"); QVERIFY(!b.isEmpty()); QCOMPARE(d.activeShotId(),b); QCOMPARE(d.activeShotIndex(),1);
        QVERIFY(d.activeShot().layers[0].tiles.isEmpty()); tap(d,Qt::green); QVERIFY(d.setCurrentFrame(12));
        const auto c=d.createShot("Ending","Finale"); QVERIFY(!c.isEmpty()); tap(d,Qt::blue); QCOMPARE(d.activeShot().scene,QString("Finale"));
        const int history=d.history()->count(); QVERIFY(d.selectShot(a)); QCOMPARE(d.currentFrame(),7);
        QCOMPARE(d.activeShot().layers[d.activeLayer()].id,aLayer); QVERIFY(d.activeShot().layers[d.activeLayer()].tiles.isEmpty());
        QCOMPARE(d.activeShot().layers[0].tiles.value({0,0}).pixelColor(30,30),QColor(Qt::red));
        QVERIFY(d.selectShot(b)); QCOMPARE(d.currentFrame(),12); QCOMPARE(d.history()->count(),history);
        QCOMPARE(d.activeShot().layers[0].tiles.value({0,0}).pixelColor(30,30),QColor(Qt::green));
        QVERIFY(d.moveShot(c,0)); QCOMPARE(d.activeShotId(),b); QCOMPARE(d.activeShotIndex(),2); QCOMPARE(d.project().shots[0].id,c);
        tap(d,Qt::yellow,{70,70}); const auto painted=d.activeShot().layers[0].tiles;
        QVERIFY(d.selectShot(a)); d.history()->undo(); QCOMPARE(d.activeShotId(),b); QVERIFY(d.activeShot().layers[0].tiles!=painted);
        d.history()->redo(); QCOMPARE(d.activeShotId(),b); QCOMPARE(d.activeShot().layers[0].tiles,painted);
        QVERIFY2(d.project().validate().isEmpty(),qPrintable(d.project().validate()));
    }
    void activeShotEditsCoverRasterObjectsClipboardAndTransforms() {
        Document d; d.load(blank()); auto first=shotJson(d,0); first["scene"]="Scene 1"; const auto second=d.createShot("Work"); QVERIFY(!second.isEmpty());
        QPainterPath shape; shape.addRect(15,15,25,25); QVERIFY(RasterTools::drawPath(&d,shape,Qt::red,{},0));
        QCOMPARE(RasterTools::sample(&d,{25,25}),QColor(Qt::red));
        QImage image(8,8,QImage::Format_ARGB32_Premultiplied);image.fill(Qt::green);QString error;
        QVERIFY2(ClipboardTools::importImage(&d,image,"Image",{50,50},&error),qPrintable(error));
        AdvancedTransform transform; QVERIFY(transform.begin(&d));QTransform move;move.translate(10,0);QVERIFY(transform.setMatrix(move));QVERIFY(transform.apply());
        QCOMPARE(RasterTools::sample(&d,{62,52},RasterSampleSource::ActiveLayer),QColor(Qt::green));
        QVERIFY(ObjectTools::addLayer(&d,LayerKind::Vector));auto object=EditableObject::create();object.nodes={BezierNode::corner({10,70}),BezierNode::corner({80,70})};
        QVERIFY(!ObjectTools::addPath(&d,object.nodes,false,Qt::black,3).isEmpty());QCOMPARE(d.activeShot().layers[d.activeLayer()].objects.size(),1);
        QCOMPARE(shotJson(d,0),first);QVERIFY(d.project().shots[0].layers[0].tiles.isEmpty());
        const auto layer=d.activeShot().layers[d.activeLayer()].id;QVERIFY(d.enableKeyframeChannel(layer,KeyframeProperty::PositionX));
        QVERIFY(d.setCurrentFrame(8));QVERIFY(d.setKeyframeValue(layer,KeyframeProperty::PositionX,20));
        QCOMPARE(d.activeShot().layerAnimations.size(),1);QVERIFY(d.project().shots[0].layerAnimations.isEmpty());
    }
    void duplicateRemapsAllOwnershipAndKeepsAssetsShared() {
        auto p=rich();QVERIFY2(p.validate().isEmpty(),qPrintable(p.validate()));Document d;d.load(p);
        const auto original=d.activeShot();const auto copyId=d.duplicateShot(original.id);QVERIFY(!copyId.isEmpty());
        const auto copy=d.activeShot();QCOMPARE(copy.id,copyId);QVERIFY(owned(original).intersect(owned(copy)).isEmpty());
        QCOMPARE(copy.scene,original.scene);QCOMPARE(copy.layers.size(),original.layers.size());
        QCOMPARE(copy.animationTracks[0].drawingTemplate,original.animationTracks[0].drawingTemplate);
        QCOMPARE(copy.animationTracks[0].exposures[1].drawingId,copy.animationTracks[0].drawings[1].id);
        QCOMPARE(copy.layers[2].parentId,copy.animationTracks[0].rootGroupLayerId);
        QCOMPARE(copy.animationTracks[0].drawings[0].groupLayerId,copy.layers[2].id);
        QCOMPARE(copy.animationTracks[0].drawings[0].templateLayers["ink"],copy.layers[3].id);
        const int v=6,scene=7;
        QCOMPARE(copy.layerAnimations[0].layerId,copy.layers[v].id);QCOMPARE(copy.layerAnimations[0].paths[0].objectId,copy.layers[v].objects[0].id);
        QCOMPARE(copy.sceneCameraAnimations[0].layerId,copy.layers[scene].id);
        QCOMPARE(copy.references[0].image.cacheKey(),original.references[0].image.cacheKey());
        QCOMPARE(copy.layers[3].tiles.value({0,0}).cacheKey(),original.layers[3].tiles.value({0,0}).cacheKey());
        QCOMPARE(copy.audioClips[0].asset,original.audioClips[0].asset);
        const auto &a=original.layers[scene].scene3d->objects[0],&b=copy.layers[scene].scene3d->objects[0];
        QCOMPARE(copy.layers[scene].scene3d->models[a.modelId],original.layers[scene].scene3d->models[a.modelId]);
        QCOMPARE(b.rig.joints[0].mirrorId,b.rig.joints[1].id);QCOMPARE(b.rig.joints[2].parentId,b.rig.joints[0].id);
        QCOMPARE(b.skin->jointIds[2],b.rig.joints[2].id);QCOMPARE(b.skin->meshes,a.skin->meshes);
        QCOMPARE(b.pose.joints[0].jointId,b.rig.joints[2].id);QCOMPARE(b.pose.limits[0].jointId,b.rig.joints[2].id);QCOMPARE(b.pose.targets[0].jointId,b.rig.joints[3].id);
        QVERIFY2(d.project().validate().isEmpty(),qPrintable(d.project().validate()));
        d.selectLayer(0);tap(d,Qt::yellow);QVERIFY(d.project().shots[0].layers[0].tiles.isEmpty());
        QTemporaryDir dir;QString error;const auto file=dir.filePath("duplicate.toon");QVERIFY2(saveProject(d.project(),file,&error),qPrintable(error));
        const auto loaded=loadProject(file,&error);QVERIFY2(loaded,qPrintable(error));QCOMPARE(loaded->toJson(),d.project().toJson());
        QCOMPARE(loaded->shots[0].audioClips[0].asset,loaded->shots[1].audioClips[0].asset);
    }
    void deletionAndHistoryRevealTheRelevantAnimatedShot() {
        Document d;d.load(animation_fixture::project());const auto animated=d.activeShotId();const auto original=d.activeShot().animationTracks;
        const auto next=d.createShot("Next");QVERIFY(!next.isEmpty());const int before=d.history()->count();
        QVERIFY(d.deleteShot(animated));QCOMPARE(d.project().shots.size(),1);QCOMPARE(d.activeShotId(),next);QCOMPARE(d.history()->count(),before+1);
        d.history()->undo();QCOMPARE(d.project().shots.size(),2);QCOMPARE(d.activeShotId(),animated);QCOMPARE(d.activeShot().animationTracks,original);
        d.history()->redo();QCOMPARE(d.activeShotId(),next);QVERIFY(!d.deleteShot(next));QCOMPARE(d.project().shots.size(),1);
        d.history()->undo();QVERIFY(d.selectShot(next));QVERIFY(d.deleteShot(next));QCOMPARE(d.activeShotId(),animated);
        d.history()->undo();QCOMPARE(d.activeShotId(),next);QCOMPARE(d.project().shots.size(),2);
    }
    void crossShotUndoRestoresSelectionEvenAtTheSameFrame() {
        Document d; d.load(blank()); const auto a = d.activeShotId(), b = d.createShot("Other");
        QVERIFY(d.selectShot(a)); tap(d, Qt::red);
        QPainterPath selection; selection.addRect(20,20,30,30); d.setRasterSelection(selection);
        const auto before = d.rasterSelection();
        AdvancedTransform transform; QVERIFY(transform.begin(&d, {}, true));
        QTransform matrix; matrix.translate(15,5); QVERIFY(transform.setMatrix(matrix)); QVERIFY(transform.apply());
        const auto after = d.rasterSelection(); QVERIFY(after.path != before.path);
        QVERIFY(d.selectShot(b)); QCOMPARE(d.currentFrame(),0); QVERIFY(!d.rasterSelection().active);
        d.history()->undo(); QCOMPARE(d.activeShotId(),a); QCOMPARE(d.rasterSelection().path,before.path);
        QCOMPARE(d.rasterSelection().active,before.active);
        QVERIFY(d.selectShot(b)); d.history()->redo(); QCOMPARE(d.activeShotId(),a);
        QCOMPARE(d.rasterSelection().path,after.path); QCOMPARE(d.rasterSelection().active,after.active);
    }
    void addedAndDeletedAnimationMetadataCountTowardHistoryBudget() {
        auto project = blank(); auto &shot = project.shots[0];
        auto layer = RasterLayer::create("Morph"); layer.kind = LayerKind::Vector;
        auto object = EditableObject::create();
        object.nodes = QVector<BezierNode>(16384, BezierNode::corner({30,30}));
        layer.objects.append(object); shot.layers.append(layer);
        LayerAnimation animation; animation.layerId = layer.id;
        PathAnimation path; path.objectId = object.id;
        const auto pose = pathPose(object);
        for (int frame = 0; frame < 16; ++frame) path.keys.append({frame,pose});
        animation.paths.append(path); shot.layerAnimations.append(animation);
        QVERIFY2(project.validate().isEmpty(),qPrintable(project.validate()));
        QVERIFY(propertyAnimationHistoryCost(Shot{},shot) > 12*1024*1024);
        Document d; d.load(project); const auto source = d.activeShotId(); QStringList copies;
        for (int i = 0; i < 8; ++i) {
            const auto copy = d.duplicateShot(source); QVERIFY(!copy.isEmpty()); copies.append(copy);
        }
        // Eight metadata-heavy additions exceed 64 MiB. Source geometry alone
        // is smaller than that budget, so omitting newly added shots fails here.
        QVERIFY(d.history()->count() < copies.size());
        const auto duplicated = d.project(); d.load(duplicated);
        for (const auto &copy : copies) QVERIFY(d.deleteShot(copy));
        QVERIFY(d.history()->count() < copies.size());
        QCOMPARE(d.project().shots.size(),1); d.history()->undo(); QCOMPARE(d.project().shots.size(),2);
    }
    void shotMutationsAreAtomicAndRefusePendingGestures() {
        Document d;d.load(blank());const auto a=d.activeShotId(),b=d.createShot("Second");QVERIFY(d.selectShot(a));
        const auto before=d.project().toJson();const auto history=d.history()->count();
        QVERIFY(!d.renameShot(a,"", "Bad"));QVERIFY(!d.setShotScene(a,"bad\nscene"));QVERIFY(!d.moveShot(a,9));QVERIFY(!d.deleteShot("missing"));QVERIFY(d.createShot(QString(201,'x')).isEmpty());
        QCOMPARE(d.project().toJson(),before);QCOMPARE(d.history()->count(),history);
        BrushSettings brush;QVERIFY(d.beginStroke({30,30},1,brush,false));
        QVERIFY(!d.selectShot(b));QVERIFY(d.createShot("Blocked").isEmpty());QVERIFY(d.duplicateShot(a).isEmpty());QVERIFY(!d.deleteShot(b));QVERIFY(!d.renameShot(a,"C010","Blocked"));d.cancelStroke();
        QVERIFY(d.beginLayerPropertyEdit());d.previewLayerOpacity(.3);QVERIFY(!d.selectShot(b));d.cancelLayerPropertyEdit();
        QVERIFY(d.beginEffectsEdit());QVERIFY(!d.selectShot(b));d.cancelEffectsEdit();
        tap(d,Qt::red);QVERIFY(d.beginTransform());QVERIFY(!d.selectShot(b));d.cancelTransform();
        QCOMPARE(d.activeShotId(),a);QVERIFY(d.selectShot(b));
    }
    void scenesOrderAndSaveLoadSupportLegacyAndStrict22() {
        Document d;d.load(blank());const auto a=d.activeShotId();QCOMPARE(d.project().toJson()["version"].toInt(),19);
        const auto b=d.createShot("Middle","Opening"),c=d.createShot("End","Finale");QVERIFY(!b.isEmpty()&&!c.isEmpty());
        QVERIFY(d.renameShot(b,"CUT-20","Middle revised"));QVERIFY(d.setShotScene(a,"Opening"));QVERIFY(d.moveShot(c,0));
        QTemporaryDir dir;QString error;const auto file=dir.filePath("movie.toon");QVERIFY2(saveProject(d.project(),file,&error),qPrintable(error));
        QFile saved(file);QVERIFY(saved.open(QIODevice::ReadOnly));QCOMPARE(saved.read(10),QByteArray("TOONPRO22\n"));
        const auto loaded=loadProject(file,&error);QVERIFY2(loaded,qPrintable(error));QCOMPARE(loaded->toJson(),d.project().toJson());QCOMPARE(loaded->shots[0].id,c);
        auto legacy=d.project().toJson();legacy["version"]=19;auto shots=legacy["shots"].toArray();
        for(int i=0;i<shots.size();++i){auto shot=shots[i].toObject();shot.remove("scene");shots[i]=shot;}legacy["shots"]=shots;
        const auto old=Project::fromJson(legacy,&error);QVERIFY2(old,qPrintable(error));for(const auto &shot:old->shots)QCOMPARE(shot.scene,QString("Scene 1"));
        for(const auto &bad:QList<QJsonValue>{QJsonValue(),QJsonValue(7),QJsonValue(""),QJsonValue(" bad "),QJsonValue("bad\nscene"),QJsonValue(QString(101,'x'))}) {
            auto malformed=d.project().toJson();auto entries=malformed["shots"].toArray();auto shot=entries[0].toObject();shot["scene"]=bad;entries[0]=shot;malformed["shots"]=entries;
            QVERIFY(!Project::fromJson(malformed,&error));
        }
        auto disguised=d.project().toJson();disguised["version"]=21;QVERIFY(!Project::fromJson(disguised,&error));QVERIFY(error.contains("format 22"));
        auto future=d.project().toJson();future["version"]=latestProjectFormatVersion + 1;QVERIFY(!Project::fromJson(future,&error));
    }
};
QTEST_MAIN(ShotsTest)
#include "test_shots.moc"
