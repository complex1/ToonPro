#include "project.h"
#include "scene_model_io.h"
#include <QCryptographicHash>
#include <QDataStream>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QTest>
#include <QUuid>
using namespace toon;
namespace {
Project projectWithScene() {
    auto project=Project::create();
    RasterLayer layer=RasterLayer::create();
    layer.kind=LayerKind::Scene3D;layer.name="3D reference";layer.scene3d=Scene3D{};
    auto model=builtInModels3D().front();
    layer.scene3d->models.insert(model->id,model);
    layer.scene3d->objects.append({"cube-a","Cube A",model->id,{1,2,3},{10,20,30},{2,2,2}});
    layer.scene3d->objects.append({"cube-b","Cube B",model->id,{-1,0,0}});
    layer.scene3d->camera.yaw=45;layer.scene3d->camera.locked=true;
    project.shots[0].layers.append(layer);
    return project;
}
QByteArray bytes(const QString &path) { QFile f(path);if(!f.open(QIODevice::ReadOnly))return {};return f.readAll(); }
bool put(const QString &path,const QByteArray &data) { QFile f(path);return f.open(QIODevice::WriteOnly)&&f.write(data)==data.size(); }
QJsonObject replaceScene(QJsonObject manifest, const QJsonObject &scene) {
    auto shots = manifest["shots"].toArray(); auto shot = shots[0].toObject(); auto layers = shot["layers"].toArray();
    auto layer = layers[1].toObject(); layer["scene3d"] = scene; layers[1] = layer;
    shot["layers"] = layers; shots[0] = shot; manifest["shots"] = shots; return manifest;
}
QJsonObject jsonScene(const QJsonObject &manifest) {
    return manifest["shots"].toArray()[0].toObject()["layers"].toArray()[1].toObject()["scene3d"].toObject();
}
QJsonObject withoutPose(QJsonObject scene, int version) {
    scene["version"] = version;
    auto objects = scene["objects"].toArray();
    for (int i = 0; i < objects.size(); ++i) { auto object = objects[i].toObject(); object.remove("pose"); objects[i] = object; }
    scene["objects"] = objects; return scene;
}
QJsonObject legacyScene(QJsonObject scene) {
    scene = withoutPose(scene, 1);
    scene["version"] = 1;
    auto objects = scene["objects"].toArray();
    for (int i = 0; i < objects.size(); ++i) { auto object = objects[i].toObject(); object.remove("rig"); object.remove("skin"); objects[i] = object; }
    scene["objects"] = objects; return scene;
}
QByteArray container(int version,const QJsonObject &manifest,const QList<QByteArray> &blobs={}) {
    QByteArray result=QByteArray("TOONPRO")+QByteArray::number(version)+"\n";
    QDataStream s(&result,QIODevice::Append);s.setByteOrder(QDataStream::BigEndian);
    auto append=[&](const QByteArray &blob){s<<quint32(blob.size());s.writeRawData(QCryptographicHash::hash(blob,QCryptographicHash::Sha256).constData(),32);s.writeRawData(blob.constData(),blob.size());};
    append(QJsonDocument(manifest).toJson(QJsonDocument::Compact));
    for(const auto &blob:blobs)append(blob);
    return result;
}
}
class SceneStorageTest:public QObject {
    Q_OBJECT
private slots:
    void modelsRoundTripOnceAndRemainIndependent() {
        QTemporaryDir dir;QVERIFY(dir.isValid());QString error;
        auto project=projectWithScene();
        auto shot=project.shots[0];shot.id=QUuid::createUuid().toString(QUuid::WithoutBraces);
        for(auto &layer:shot.layers)layer.id=QUuid::createUuid().toString(QUuid::WithoutBraces);
        project.shots.append(shot);
        QVERIFY2(project.validate().isEmpty(),qPrintable(project.validate()));
        const auto manifest=project.toJson();QCOMPARE(manifest["version"].toInt(),22);
        QCOMPARE(manifest["modelResources"].toArray().size(),1);
        const auto metadata=Project::fromJson(manifest,&error);QVERIFY2(metadata,qPrintable(error));
        QVERIFY(metadata->validate().contains("3D model resources"));
        const QString path=dir.filePath("models.toon");QVERIFY2(saveProject(project,path,&error),qPrintable(error));
        QCOMPARE(bytes(path).left(10),QByteArray("TOONPRO22\n"));
        auto loaded=loadProject(path,&error);QVERIFY2(loaded,qPrintable(error));
        QVERIFY2(loaded->validate().isEmpty(),qPrintable(loaded->validate()));
        const auto &a=*loaded->shots[0].layers[1].scene3d;
        const auto &b=*loaded->shots[1].layers[1].scene3d;
        QCOMPARE(a.objects,project.shots[0].layers[1].scene3d->objects);
        QCOMPARE(a.camera,project.shots[0].layers[1].scene3d->camera);
        QCOMPARE(a.models.cbegin().value().get(),b.models.cbegin().value().get());
        QCOMPARE(a.models.cbegin().value()->id,project.shots[0].layers[1].scene3d->models.cbegin().value()->id);
        QVERIFY(a.models.cbegin().value().get()!=project.shots[0].layers[1].scene3d->models.cbegin().value().get());
        QCOMPARE(serializeModel3D(*a.models.cbegin().value()),serializeModel3D(*project.shots[0].layers[1].scene3d->models.cbegin().value()));
        QVERIFY(loaded->shots[0].layers[1].tiles.isEmpty());
    }
    void malformedMetadataRejectedBeforeGeometry() {
        QString error;const auto project=projectWithScene();const auto valid=project.toJson();
        auto manifest=valid;manifest["modelResources"]=QJsonArray{};
        QVERIFY(!Project::fromJson(manifest,&error));QVERIFY(error.contains("missing model"));
        manifest=valid;auto resources=manifest["modelResources"].toArray();resources.append(resources.first());manifest["modelResources"]=resources;
        QVERIFY(!Project::fromJson(manifest,&error));QVERIFY(error.contains("duplicate"));
        manifest=valid;auto shots=manifest["shots"].toArray();auto shot=shots[0].toObject();auto layers=shot["layers"].toArray();auto layer=layers[1].toObject();
        layer["kind"]="raster";layers[1]=layer;shot["layers"]=layers;shots[0]=shot;manifest["shots"]=shots;
        QVERIFY(!Project::fromJson(manifest,&error));
        auto invalid=project;invalid.shots[0].layers[1].scene3d.reset();QVERIFY(!invalid.validate().isEmpty());
        invalid=project;invalid.shots[0].layers[1].clipped=true;QVERIFY(invalid.validate().contains("clipping"));
        invalid=project;invalid.shots[0].layers[1].scene3d->objects[0].modelId="missing";QVERIFY(!invalid.validate().isEmpty());
        invalid=project;auto extra=invalid.shots[0].layers[1];extra.id=QUuid::createUuid().toString(QUuid::WithoutBraces);invalid.shots[0].layers.append(extra);
        QVERIFY(invalid.validate().contains("one shared"));
    }
    void corruptMissingAndSubstitutedResourcesRejected() {
        QTemporaryDir dir;QString error;const auto project=projectWithScene();const auto manifest=project.toJson();
        const auto model=project.shots[0].layers[1].scene3d->models.cbegin().value();const auto blob=serializeModel3D(*model);
        const auto good=container(19,manifest,{blob});const auto path=dir.filePath("broken.toon");
        QVERIFY(put(path,good));QVERIFY2(loadProject(path,&error),qPrintable(error));
        auto changed=good;changed[changed.size()-5]^=1;QVERIFY(put(path,changed));QVERIFY(!loadProject(path,&error));QVERIFY(error.contains("checksum"));
        QVERIFY(put(path,container(19,manifest)));QVERIFY(!loadProject(path,&error));
        const auto different=serializeModel3D(*builtInModels3D()[1]);
        QVERIFY(put(path,container(19,manifest,{different})));QVERIFY(!loadProject(path,&error));QVERIFY(error.contains("identifier"));
        QVERIFY(put(path,good+"extra"));QVERIFY(!loadProject(path,&error));QVERIFY(error.contains("trailing"));
    }
    void previousVersionEightStillLoads() {
        QTemporaryDir dir;QString error;auto manifest=Project::create().toJson();manifest["version"]=8;manifest.remove("modelResources");
        auto path=dir.filePath("previous.toon");QVERIFY(put(path,container(8,manifest)));
        auto loaded=loadProject(path,&error);QVERIFY2(loaded,qPrintable(error));QCOMPARE(loaded->toJson()["version"].toInt(),19);
        manifest=projectWithScene().toJson();manifest=replaceScene(manifest,legacyScene(jsonScene(manifest)));manifest["version"]=8;manifest.remove("modelResources");
        QVERIFY(!Project::fromJson(manifest,&error));QVERIFY(error.contains("format 9"));
    }
    void previousVersionNinePreservesStaticScene() {
        QTemporaryDir dir; QString error;
        const auto project = projectWithScene();
        auto manifest = project.toJson(); manifest["version"] = 9;
        manifest = replaceScene(manifest, legacyScene(jsonScene(manifest)));
        const auto model = project.shots[0].layers[1].scene3d->models.cbegin().value();
        const auto path = dir.filePath("previous-nine.toon");
        QVERIFY(put(path, container(9, manifest, {serializeModel3D(*model)})));
        auto loaded = loadProject(path, &error); QVERIFY2(loaded, qPrintable(error));
        QCOMPARE(loaded->toJson()["version"].toInt(), 19);
        QCOMPARE(loaded->shots[0].layers[1].scene3d->objects, project.shots[0].layers[1].scene3d->objects);
        QVERIFY(loaded->shots[0].layers[1].scene3d->objects[0].rig.joints.isEmpty());
        QVERIFY2(saveProject(*loaded, path, &error), qPrintable(error));
        QCOMPARE(bytes(path).left(10), QByteArray("TOONPRO19\n"));
        QVERIFY2(loadProject(path, &error), qPrintable(error));
    }
    void rigsRoundTripPerInstanceAndCopiedShotsRemainIndependent() {
        QTemporaryDir dir; QString error;
        auto project = projectWithScene();
        const auto model = project.shots[0].layers[1].scene3d->models.cbegin().value();
        const auto immutableGeometry = serializeModel3D(*model);
        auto &objects = project.shots[0].layers[1].scene3d->objects;
        objects[0].rig = createHumanoidRig3D(model->bounds);
        objects[0].rig.mirrorPlaneX = .2f;
        objects[1].rig = objects[0].rig;
        const QString selected = objects[1].rig.joints.last().id;
        QVERIFY(moveRigJoint3D(objects[1].rig, selected, {.5f, .25f, -.2f}, true));
        QVERIFY(objects[0].rig != objects[1].rig);
        auto shot = project.shots[0]; shot.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        for (auto &layer : shot.layers) layer.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        project.shots.append(shot);
        const auto path = dir.filePath("rigs.toon");
        QVERIFY2(saveProject(project, path, &error), qPrintable(error));
        auto loaded = loadProject(path, &error); QVERIFY2(loaded, qPrintable(error));
        QCOMPARE(loaded->toJson(), project.toJson());
        QCOMPARE(loaded->toJson()["modelResources"].toArray().size(), 1);
        QCOMPARE(serializeModel3D(*loaded->shots[0].layers[1].scene3d->models.cbegin().value()), immutableGeometry);
        const auto original = loaded->shots[0].layers[1].scene3d->objects[1].rig;
        QVERIFY(moveRigJoint3D(loaded->shots[1].layers[1].scene3d->objects[1].rig, selected, {0, 0, 0}, true));
        QVERIFY(loaded->shots[0].layers[1].scene3d->objects[1].rig == original);
        QCOMPARE(loaded->shots[0].layers[1].scene3d->models.cbegin().value().get(),
            loaded->shots[1].layers[1].scene3d->models.cbegin().value().get());
    }
    void oldFormatsCannotSilentlyDiscardRigs() {
        QString error;
        auto project = projectWithScene();
        auto &object = project.shots[0].layers[1].scene3d->objects[0];
        object.rig = createHumanoidRig3D(project.shots[0].layers[1].scene3d->models.cbegin().value()->bounds);
        const auto valid = project.toJson();
        for (int version = 1; version < 10; ++version) {
            auto manifest = valid; auto rigScene = withoutPose(jsonScene(manifest), 2);
            manifest = replaceScene(manifest, rigScene); manifest["version"] = version;
            QVERIFY(!Project::fromJson(manifest, &error));
            QVERIFY2(error.contains("format 10"), qPrintable(error));
            auto scene = jsonScene(manifest); scene["version"] = 1; manifest = replaceScene(manifest, scene);
            QVERIFY(!Project::fromJson(manifest, &error));
            QVERIFY2(error.contains("format 10"), qPrintable(error));
        }
        auto broken = valid; auto scene = jsonScene(broken); auto objects = scene["objects"].toArray();
        auto json = objects[0].toObject(); auto rig = json["rig"].toObject(); auto joints = rig["joints"].toArray();
        auto root = joints[0].toObject(); root["parentId"] = root["id"]; joints[0] = root;
        rig["joints"] = joints; json["rig"] = rig; objects[0] = json; scene["objects"] = objects;
        QVERIFY(!Project::fromJson(replaceScene(broken, scene), &error));
        QVERIFY2(error.contains("cycle"), qPrintable(error));
        QTemporaryDir dir; const auto path = dir.filePath("bad-header.toon");
        QVERIFY(put(path, container(latestProjectFormatVersion + 1, valid)));
        QVERIFY(!loadProject(path, &error)); QVERIFY(error.contains("container version"));
        QVERIFY(put(path, container(9, valid)));
        QVERIFY(!loadProject(path, &error)); QVERIFY(error.contains("match its container"));
    }
    void historyCountsNewGeometryButNotCameraEdits() {
        const auto before=Project::create();auto after=projectWithScene();
        QVERIFY(editableObjectHistoryCost(before,after)>0);
        auto moved=after;moved.shots[0].layers[1].scene3d->camera.yaw+=10;
        QCOMPARE(editableObjectHistoryCost(after,moved),qint64(0));
        moved.shots[0].layers.removeLast();QVERIFY(editableObjectHistoryCost(after,moved)>0);
    }
    void historyCountsRestSkeletonEditsAndNotPlacementEdits() {
        auto before = projectWithScene(); auto after = before;
        auto &object = after.shots[0].layers[1].scene3d->objects[0];
        object.rig = createHumanoidRig3D(after.shots[0].layers[1].scene3d->models.cbegin().value()->bounds);
        QVERIFY(editableObjectHistoryCost(before, after) >= object.rig.joints.size() * qint64(sizeof(RigJoint3D)));
        before = after;
        after.shots[0].layers[1].scene3d->objects[0].position.setX(20);
        QCOMPARE(editableObjectHistoryCost(before, after), qint64(0));
        before = after;
        const auto selected = after.shots[0].layers[1].scene3d->objects[0].rig.joints.last().id;
        QVERIFY(moveRigJoint3D(after.shots[0].layers[1].scene3d->objects[0].rig, selected, {0, 0, 0}, true));
        QVERIFY(editableObjectHistoryCost(before, after) > 0);
        before = after;
        after.shots[0].layers[1].scene3d->objects[0].rig = {};
        QVERIFY(editableObjectHistoryCost(before, after) > 0);
    }
    void previousVersionTenPreservesRestSkeletonsWithoutBinding() {
        QTemporaryDir dir; QString error;
        auto project = projectWithScene(); auto &scene = *project.shots[0].layers[1].scene3d;
        const auto model = scene.models.cbegin().value();
        scene.objects[0].rig = createHumanoidRig3D(model->bounds);
        auto manifest = project.toJson(); manifest["version"] = 10;
        auto metadata = withoutPose(jsonScene(manifest), 2); manifest = replaceScene(manifest, metadata);
        const auto path = dir.filePath("previous-ten.toon");
        QVERIFY(put(path, container(10, manifest, {serializeModel3D(*model)})));
        const auto loaded = loadProject(path, &error); QVERIFY2(loaded, qPrintable(error));
        QCOMPARE(loaded->toJson()["version"].toInt(), 19);
        QCOMPARE(loaded->shots[0].layers[1].scene3d->objects, scene.objects);
        QVERIFY(!loaded->shots[0].layers[1].scene3d->objects[0].skin);
    }
    void skinWeightsRoundTripAndRemainIndependentPerInstance() {
        QTemporaryDir dir; QString error;
        auto project = projectWithScene(); auto &scene = *project.shots[0].layers[1].scene3d;
        const auto model = scene.models.cbegin().value(); const auto geometry = serializeModel3D(*model);
        scene.objects[0].rig = createHumanoidRig3D(model->bounds);
        scene.objects[0].skin = autoBindSkin3D(*model, scene.objects[0].rig, &error);
        QVERIFY2(scene.objects[0].skin, qPrintable(error));
        scene.objects[1].rig = scene.objects[0].rig; scene.objects[1].skin = scene.objects[0].skin;
        QVERIFY(setSkinJointWeight(scene.objects[1].skin->meshes[0][0], 0, .2f));
        QVERIFY(scene.objects[0].skin != scene.objects[1].skin);
        const auto path = dir.filePath("skin.toon"); QVERIFY2(saveProject(project, path, &error), qPrintable(error));
        QCOMPARE(bytes(path).left(10), QByteArray("TOONPRO19\n"));
        auto loaded = loadProject(path, &error); QVERIFY2(loaded, qPrintable(error));
        QCOMPARE(loaded->toJson(), project.toJson());
        auto &objects = loaded->shots[0].layers[1].scene3d->objects;
        QCOMPARE(objects, scene.objects);
        const auto original = objects[0].skin;
        QVERIFY(setSkinJointWeight(objects[1].skin->meshes[0][0], 0, .4f));
        QCOMPARE(objects[0].skin, original);
        QCOMPARE(serializeModel3D(*loaded->shots[0].layers[1].scene3d->models.cbegin().value()), geometry);
        auto before = project; auto after = before; after.shots[0].layers[1].scene3d->objects[0].skin.reset();
        QVERIFY(editableObjectHistoryCost(before, after) >= skin3DByteSize(*scene.objects[0].skin));
        after = before; QVERIFY(setSkinJointWeight(after.shots[0].layers[1].scene3d->objects[0].skin->meshes[0][0], 0, .6f));
        QVERIFY(editableObjectHistoryCost(before, after) >= 2 * skin3DByteSize(*scene.objects[0].skin));
        after = before; after.shots[0].layers[1].scene3d->camera.yaw += 10;
        QCOMPARE(editableObjectHistoryCost(before, after), qint64(0));
    }
    void skinMetadataCannotHideInOldFormatsAndCountsMustMatchGeometry() {
        QTemporaryDir dir; QString error;
        auto project = projectWithScene(); auto &scene = *project.shots[0].layers[1].scene3d;
        const auto model = scene.models.cbegin().value(); auto &bound = scene.objects[0];
        bound.rig = createHumanoidRig3D(model->bounds); bound.skin = autoBindSkin3D(*model, bound.rig);
        QVERIFY(bound.skin); const auto valid = project.toJson();
        for (int version = 1; version < 11; ++version) {
            auto manifest = replaceScene(valid, withoutPose(jsonScene(valid), 3)); manifest["version"] = version;
            QVERIFY(!Project::fromJson(manifest, &error)); QVERIFY2(error.contains("format 11"), qPrintable(error));
            auto metadata = jsonScene(manifest); metadata["version"] = 2; manifest = replaceScene(manifest, metadata);
            QVERIFY(!Project::fromJson(manifest, &error)); QVERIFY2(error.contains("format 11"), qPrintable(error));
        }
        auto invalid = project; invalid.shots[0].layers[1].scene3d->objects[0].skin->meshes[0][0].weights[0] = -1;
        QVERIFY(!invalid.validate().isEmpty()); QVERIFY(!saveProject(invalid, dir.filePath("invalid.toon"), &error));
        invalid = project;
        invalid.shots[0].layers[1].scene3d->objects[0].skin->meshes[0].append(invalid.shots[0].layers[1].scene3d->objects[0].skin->meshes[0].first());
        const auto path = dir.filePath("mismatch.toon");
        QVERIFY(put(path, container(19, invalid.toJson(), {serializeModel3D(*model)})));
        QVERIFY(!loadProject(path, &error)); QVERIFY2(error.contains("vertex counts"), qPrintable(error));
        auto manifest = valid; auto metadata = jsonScene(manifest); auto objects = metadata["objects"].toArray();
        auto object = objects[0].toObject(); auto skin = object["skin"].toObject(); skin["meshVertexCounts"] = QJsonArray{1000000};
        object["skin"] = skin; objects = {};
        for (int i = 0; i < 3; ++i) { object["id"] = QString("object-%1").arg(i); objects.append(object); }
        metadata["objects"] = objects; manifest = replaceScene(manifest, metadata);
        QVERIFY(!Project::fromJson(manifest, &error)); QVERIFY2(error.contains("48 MiB"), qPrintable(error));
    }
    void previousVersionElevenPreservesBindingsWithEmptyPoses() {
        QTemporaryDir dir; QString error; auto project = projectWithScene();
        auto &scene = *project.shots[0].layers[1].scene3d; const auto model = scene.models.cbegin().value();
        scene.objects[0].rig = createHumanoidRig3D(model->bounds);
        scene.objects[0].skin = autoBindSkin3D(*model, scene.objects[0].rig); QVERIFY(scene.objects[0].skin);
        auto manifest = replaceScene(project.toJson(), withoutPose(jsonScene(project.toJson()), 3)); manifest["version"] = 11;
        const auto path = dir.filePath("previous-eleven.toon");
        QVERIFY(put(path, container(11, manifest, {serializeModel3D(*model)})));
        const auto loaded = loadProject(path, &error); QVERIFY2(loaded, qPrintable(error));
        QCOMPARE(loaded->toJson()["version"].toInt(), 19);
        QCOMPARE(loaded->shots[0].layers[1].scene3d->objects, scene.objects);
        QCOMPARE(loaded->shots[0].layers[1].scene3d->objects[0].pose, Pose3D{});
    }
    void posesRoundTripWithLimitsPinsAndInstanceIndependence() {
        QTemporaryDir dir; QString error; auto project = projectWithScene();
        auto &scene = *project.shots[0].layers[1].scene3d; const auto model = scene.models.cbegin().value();
        auto &a = scene.objects[0]; a.rig.joints = {{"a","A",{},{},{0,0,0}}, {"b","B","a",{},{.5f,0,0}}, {"c","C","b",{},{1,0,0}}};
        a.skin = autoBindSkin3D(*model, a.rig); QVERIFY(a.skin);
        a.pose.rootTranslation = {.1f,.2f,.3f}; a.pose.limits = {{"b",{-90,-90,-120},{90,90,120},true}};
        QVERIFY(solvePoseIK3D(a.pose, a.rig, {"c",{.8f,.6f,.3f},{0,2,0},true}, &error));
        scene.objects[1].rig = a.rig; scene.objects[1].skin = a.skin; scene.objects[1].pose = a.pose;
        QVERIFY(setPoseJointRotation3D(scene.objects[1].pose, a.rig, "c", {15,25,35}));
        QVERIFY(scene.objects[1].pose != a.pose);
        const auto beforeGeometry = serializeModel3D(*model), beforeWeights = skin3DCacheKey(*a.skin);
        const auto path = dir.filePath("poses.toon"); QVERIFY2(saveProject(project, path, &error), qPrintable(error));
        QCOMPARE(bytes(path).left(10), QByteArray("TOONPRO19\n"));
        auto loaded = loadProject(path, &error); QVERIFY2(loaded, qPrintable(error));
        QCOMPARE(loaded->toJson(), project.toJson());
        auto &restored = *loaded->shots[0].layers[1].scene3d;
        const auto first = restored.objects[0].pose; resetPose3D(restored.objects[1].pose);
        QCOMPARE(restored.objects[0].pose, first); QCOMPARE(skin3DCacheKey(*restored.objects[0].skin), beforeWeights);
        QCOMPARE(serializeModel3D(*restored.models.cbegin().value()), beforeGeometry);
        auto after = project; resetPose3D(after.shots[0].layers[1].scene3d->objects[0].pose);
        QVERIFY(editableObjectHistoryCost(project, after) >= pose3DByteSize(a.pose));
        after = project; after.shots[0].layers[1].scene3d->camera.yaw += 10;
        QCOMPARE(editableObjectHistoryCost(project, after), qint64(0));
    }
    void poseMetadataCannotBeDisguisedOrStoredWithoutBinding() {
        QString error; auto project = projectWithScene(); auto &scene = *project.shots[0].layers[1].scene3d;
        const auto model = scene.models.cbegin().value(); auto &object = scene.objects[0];
        object.rig = createHumanoidRig3D(model->bounds); object.skin = autoBindSkin3D(*model, object.rig); QVERIFY(object.skin);
        QVERIFY(setPoseJointRotation3D(object.pose, object.rig, object.rig.joints[0].id, {10,20,30}));
        const auto valid = project.toJson();
        for (int version = 1; version < 12; ++version) {
            auto manifest = valid; manifest["version"] = version;
            QVERIFY(!Project::fromJson(manifest, &error)); QVERIFY2(error.contains("format 12"), qPrintable(error));
            auto old = jsonScene(manifest); old["version"] = 1; manifest = replaceScene(manifest, old);
            QVERIFY(!Project::fromJson(manifest, &error)); QVERIFY2(error.contains("format 12"), qPrintable(error));
        }
        auto invalid = project; invalid.shots[0].layers[1].scene3d->objects[0].skin.reset();
        QVERIFY(invalid.validate().contains("Bind the model"));
        auto metadata = jsonScene(valid); auto objects = metadata["objects"].toArray(); auto entry = objects[0].toObject();
        auto pose = entry["pose"].toObject(); pose["joints"] = QJsonArray{QJsonObject{{"jointId","missing"},{"rotation",QJsonArray{0,0,0}}}};
        entry["pose"] = pose; objects[0] = entry; metadata["objects"] = objects;
        QVERIFY(!Project::fromJson(replaceScene(valid, metadata), &error)); QVERIFY(error.contains("missing"));
        metadata = jsonScene(valid); objects = metadata["objects"].toArray(); entry = objects[0].toObject(); entry.remove("pose");
        objects[0] = entry; metadata["objects"] = objects;
        QVERIFY(!Project::fromJson(replaceScene(valid, metadata), &error)); QVERIFY(error.contains("pose metadata"));
    }
};
QTEST_MAIN(SceneStorageTest)
#include "test_scene_storage.moc"
