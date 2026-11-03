#include "scene3d_renderer.h"
#include "scene3d_cache.h"
#include <QSignalSpy>
#include <QTest>
#include <QDir>
#include <QPainter>
#include <array>

using namespace toon;
#ifdef TOON_HAS_FILAMENT
#define REQUIRE_FILAMENT_RENDERER() ((void)0)
#else
#define REQUIRE_FILAMENT_RENDERER() QSKIP("This build has no GPU reference renderer; unavailable-renderer behavior is tested separately.")
#endif
namespace {
Model3DPtr squareModel(QColor color, QChar key, const QImage &texture = {}, float alphaCutoff = -1) {
    auto model = std::make_shared<Model3D>();
    model->id = QString(64, key);
    model->name = "Square";
    ModelMesh3D mesh;
    mesh.name = "Surface";
    mesh.positions = {{-.7f,-.7f,0},{.7f,-.7f,0},{.7f,.7f,0},{-.7f,.7f,0}};
    mesh.normals = QVector<QVector3D>(4, {0,0,1});
    mesh.texCoords = {{0,1},{1,1},{1,0},{0,0}};
    mesh.indices = {0,1,2,0,2,3};
    mesh.baseColor = color;
    mesh.baseColorTexture = texture;
    mesh.alphaCutoff = alphaCutoff;
    model->meshes.append(mesh);
    for (const auto &point : mesh.positions) model->bounds.include(point);
    return model;
}
Scene3D squareScene() {
    Scene3D scene;
    scene.camera.target = {};
    scene.camera.yaw = scene.camera.pitch = 0;
    scene.camera.distance = 3;
    scene.camera.orthographic = true;
    scene.camera.orthoScale = 2;
    auto model = squareModel(Qt::red, 'a');
    scene.models.insert(model->id, model);
    scene.objects.append({"front", "Front", model->id});
    return scene;
}
void bindSquare(ModelInstance3D &object, float rootWeight = 1) {
    object.rig.joints = {{"root","Root",{},{},{0,0,0}}, {"tip","Tip","root",{},{0,.5f,0}}};
    Skin3D skin; skin.jointIds = {"root","tip"};
    SkinVertex3D vertex; vertex.joints = {0,1,0,0}; vertex.weights = {rootWeight,1-rootWeight,0,0};
    skin.meshes = {QVector<SkinVertex3D>(4,vertex)}; object.skin = skin;
}
}
class Scene3DRendererTest : public QObject {
    Q_OBJECT
private slots:
    void skinRenderIdentityTracksCowStorageAndEviction() {
        SkinVertex3D vertex; vertex.joints={0,1,0,0}; vertex.weights={1,0,0,0};
        Skin3D skin; skin.jointIds={"root","tip"}; skin.meshes={QVector<SkinVertex3D>(1,vertex)};
        const auto first=skin3DRenderKey(skin); const auto copy=skin;
        QCOMPARE(skin3DRenderKey(copy),first);
        QVERIFY(setSkinJointWeight(skin.meshes[0][0],1,.5f));
        QVERIFY(skin3DRenderKey(skin)!=first); QCOMPARE(skin3DRenderKey(copy),first);
        // More than the 64MiB identity budget evicts old snapshots. Looking up
        // identical old storage then gets a new token, never an alias to an old image.
        QVector<Skin3D> pressure;
        for(int i=0;i<3;++i) {
            Skin3D large; large.jointIds={"root","tip"}; large.meshes={QVector<SkinVertex3D>(1000000,vertex)};
            pressure.append(large); QVERIFY(skin3DRenderKey(large)!=first);
        }
        QVERIFY(skin3DRenderKey(copy)!=first);
    }
    void ordinarySceneCacheIncludesPosedBindingButIgnoresRestOnlyMetadata() {
        auto scene=squareScene(); bindSquare(scene.objects[0]);
        const auto rest=scene3DCacheKey(scene,{80,80});
        scene.objects[0].rig.joints[1].position.setY(.6f);
        QVERIFY(setSkinJointWeight(scene.objects[0].skin->meshes[0][0],1,.5f));
        QCOMPARE(scene3DCacheKey(scene,{80,80}),rest);
        scene.objects[0].pose.joints={{"tip",{0,0,30}}};
        const auto posed=scene3DCacheKey(scene,{80,80}); QVERIFY(posed!=rest);
        const auto unchanged=scene; QCOMPARE(scene3DCacheKey(unchanged,{80,80}),posed);
        QVERIFY(setSkinJointWeight(scene.objects[0].skin->meshes[0][0],1,.8f));
        const auto weighted=scene3DCacheKey(scene,{80,80}); QVERIFY(weighted!=posed);
        scene.objects[0].rig.joints[1].position.setY(.8f);
        QVERIFY(scene3DCacheKey(scene,{80,80})!=weighted);
        QCOMPARE(scene3DCacheKey(unchanged,{80,80}),posed);
        QImage saved(80,80,QImage::Format_ARGB32_Premultiplied); saved.fill(Qt::blue);
        QImage restPreview(80,80,QImage::Format_ARGB32_Premultiplied); restPreview.fill(Qt::green);
        const QByteArray transient=posed+"/rest";
        cacheScene3DPreview(posed,saved); cacheScene3DPreview(transient,restPreview);
        { Scene3DPreviewScope scope(posed,transient);
            QCOMPARE(scene3DPreview(unchanged,{80,80}),restPreview);
            QCOMPARE(scene3DPreview(unchanged,{80,80},true),saved);
        }
        QCOMPARE(scene3DPreview(unchanged,{80,80}),saved);
    }
    void transparentDepthCorrectAndCached() {
        REQUIRE_FILAMENT_RENDERER();
        Scene3DRenderService renderer;
        QSignalSpy ready(&renderer, &Scene3DRenderService::frameReady);
        QSignalSpy errors(&renderer, &Scene3DRenderService::failed);
        Scene3D scene = squareScene();
        auto back = squareModel(Qt::blue, 'b');
        scene.models.insert(back->id, back);
        scene.objects.append({"back", "Back", back->id, {0,0,-.3f}});
        const auto first = renderer.request(scene, {160,120});
        QTRY_VERIFY_WITH_TIMEOUT(!ready.isEmpty() || !errors.isEmpty(), 30000);
        QVERIFY2(errors.isEmpty(), errors.isEmpty() ? "" : qPrintable(errors.first().at(1).toString()));
        const auto image = qvariant_cast<QImage>(ready.last().at(1));
        QCOMPARE(image.size(), QSize(160,120));
        QCOMPARE(image.pixelColor(0,0).alpha(), 0);
        const QColor center = image.pixelColor(80,60);
        QVERIFY(center.alpha() > 240);
        QVERIFY2(center.red() > center.blue() * 2, qPrintable(center.name()));
        QCOMPARE(renderer.renderCount(), quint64(1));
        QCOMPARE(renderer.request(scene, {160,120}), first);
        QTest::qWait(40);
        QCOMPARE(renderer.renderCount(), quint64(1));
        QCOMPARE(ready.size(), 1);
        scene.objects[0].position.setY(.4f);
        renderer.request(scene,{160,120});
        QTRY_COMPARE_WITH_TIMEOUT(ready.size(),2,10000);
        const auto shifted=qvariant_cast<QImage>(ready.last().at(1));
        QVERIFY(shifted.pixelColor(80,15).red()>shifted.pixelColor(80,15).blue()*2);
        QVERIFY(shifted.pixelColor(80,100).blue()>shifted.pixelColor(80,100).red()*2);
        scene.objects[0].visible = false;
        renderer.request(scene, {160,120});
        QTRY_COMPARE_WITH_TIMEOUT(ready.size(), 3, 10000);
        const QColor exposed = qvariant_cast<QImage>(ready.last().at(1)).pixelColor(80,60);
        QVERIFY(exposed.blue() > exposed.red() * 2);
    }
    void textureOrientationSolidModeAndAlpha() {
        REQUIRE_FILAMENT_RENDERER();
        Scene3DRenderService renderer;
        QSignalSpy ready(&renderer, &Scene3DRenderService::frameReady);
        QSignalSpy errors(&renderer, &Scene3DRenderService::failed);
        Scene3D scene = squareScene();
        QImage texture(8,8,QImage::Format_RGBA8888);
        texture.fill(Qt::blue);
        for (int y=0;y<4;++y) for(int x=0;x<8;++x) texture.setPixelColor(x,y,Qt::red);
        auto model = squareModel(Qt::white, 'c', texture);
        scene.models.clear(); scene.models.insert(model->id, model); scene.objects[0].modelId=model->id;
        renderer.request(scene,{120,120});
        QTRY_VERIFY_WITH_TIMEOUT(!ready.isEmpty() || !errors.isEmpty(), 30000);
        QVERIFY2(errors.isEmpty(), errors.isEmpty() ? "" : qPrintable(errors.first().at(1).toString()));
        const auto image = qvariant_cast<QImage>(ready.last().at(1));
        const QColor top = image.pixelColor(60,35), bottom = image.pixelColor(60,85);
        QVERIFY2(top.red()>top.blue()*2, qPrintable(top.name()));
        QVERIFY2(bottom.blue()>bottom.red()*2, qPrintable(bottom.name()));
        scene.textured=false;
        renderer.request(scene,{120,120});
        QTRY_COMPARE_WITH_TIMEOUT(ready.size(),2,10000);
        const QColor solid=qvariant_cast<QImage>(ready.last().at(1)).pixelColor(60,35);
        QVERIFY(std::abs(solid.red()-solid.blue())<35);
        QImage hole(4,4,QImage::Format_RGBA8888); hole.fill(Qt::transparent);
        model=squareModel(Qt::white,'d',hole,.5f);
        scene.models.clear(); scene.models.insert(model->id,model);scene.objects[0].modelId=model->id;scene.textured=true;
        renderer.request(scene,{120,120});
        QTRY_COMPARE_WITH_TIMEOUT(ready.size(),3,10000);
        QCOMPARE(qvariant_cast<QImage>(ready.last().at(1)).pixelColor(60,60).alpha(),0);
    }
    void coalescesAndSuppressesCancelledFrames() {
        REQUIRE_FILAMENT_RENDERER();
        Scene3DRenderService renderer;
        QSignalSpy ready(&renderer,&Scene3DRenderService::frameReady);
        QSignalSpy errors(&renderer,&Scene3DRenderService::failed);
        auto scene=squareScene(); quint64 latest=0;
        for(int i=0;i<20;++i) { scene.camera.yaw=float(i); latest=renderer.request(scene,{128,128}); }
        QTRY_VERIFY_WITH_TIMEOUT(!ready.isEmpty() || !errors.isEmpty(),30000);
        QVERIFY2(errors.isEmpty(), errors.isEmpty() ? "" : qPrintable(errors.first().at(1).toString()));
        QCOMPARE(ready.size(),1);
        QCOMPARE(ready.first().at(0).toULongLong(),latest);
        QCOMPARE(renderer.renderCount(),quint64(1));
        scene.camera.yaw=30; renderer.request(scene,{128,128});renderer.clear();
        QTest::qWait(60);
        QCOMPARE(ready.size(),1);
        QVERIFY(!renderer.busy());
    }
    void invalidInputDoesNotCrashAndCanRecover() {
        REQUIRE_FILAMENT_RENDERER();
        Scene3DRenderService renderer;
        QSignalSpy ready(&renderer,&Scene3DRenderService::frameReady);
        QSignalSpy errors(&renderer,&Scene3DRenderService::failed);
        auto scene=squareScene();scene.camera.distance=0;
        renderer.request(scene,{64,64});
        QTRY_COMPARE_WITH_TIMEOUT(errors.size(),1,10000);
        QVERIFY(errors.first().at(1).toString().contains("camera",Qt::CaseInsensitive));
        scene.camera.distance=3;renderer.request(scene,{64,64});
        QTRY_VERIFY_WITH_TIMEOUT(!ready.isEmpty() || errors.size()>1,30000);
        QCOMPARE(errors.size(),1);QCOMPARE(ready.size(),1);
        auto broken=std::make_shared<Model3D>(*scene.models.cbegin().value());
        broken->id=QString(64,'f');broken->meshes[0].indices[0]=9999;
        scene.models.clear();scene.models.insert(broken->id,broken);scene.objects[0].modelId=broken->id;
        renderer.request(scene,{64,64});
        QTRY_COMPARE_WITH_TIMEOUT(errors.size(),2,10000);
        QVERIFY(errors.last().at(1).toString().contains("triangle index"));
        renderer.request(squareScene(),{64,64});
        QTRY_VERIFY_WITH_TIMEOUT(ready.size()>1 || errors.size()>2,30000);
        QCOMPARE(errors.size(),2);QCOMPARE(ready.size(),2);
    }
#ifndef TOON_HAS_FILAMENT
    void unavailableRendererReportsFailureWithoutPublishingPixels() {
        Scene3DRenderService renderer;
        QSignalSpy ready(&renderer, &Scene3DRenderService::frameReady);
        QSignalSpy errors(&renderer, &Scene3DRenderService::failed);
        const auto scene = squareScene();
        const auto first = renderer.request(scene, {64,64});
        QTRY_COMPARE_WITH_TIMEOUT(errors.size(), 1, 10000);
        QCOMPARE(errors.first().at(0).toULongLong(), first);
        QVERIFY(errors.first().at(1).toString().contains("unavailable", Qt::CaseInsensitive));
        QVERIFY(ready.isEmpty()); QVERIFY(!renderer.busy());
        // A failed render is not cached as success: a later request gets its
        // own explicit failure, while cancelled queued work emits nothing.
        const auto second = renderer.request(scene, {64,64});
        QVERIFY(second > first);
        QTRY_COMPARE_WITH_TIMEOUT(errors.size(), 2, 10000);
        QCOMPARE(errors.last().at(0).toULongLong(), second);
        renderer.request(scene, {96,96}); renderer.clear();
        QTest::qWait(30);
        QVERIFY(ready.isEmpty()); QCOMPARE(errors.size(), 2); QVERIFY(!renderer.busy());
    }
#endif
    void shutdownWithPendingRender() {
        auto renderer=std::make_unique<Scene3DRenderService>();
        renderer->request(squareScene(),{256,256});
        QCoreApplication::processEvents();
        renderer.reset();
    }
    void weightsAreTransientInstanceLocalAndIncludedInRequestIdentity() {
        REQUIRE_FILAMENT_RENDERER();
        Scene3DRenderService renderer; QSignalSpy ready(&renderer,&Scene3DRenderService::frameReady);
        QSignalSpy errors(&renderer,&Scene3DRenderService::failed);
        auto scene=squareScene(); auto model=squareModel(QColor(40,220,80),'e');
        scene.models.clear(); scene.models.insert(model->id,model);
        scene.objects[0].modelId=model->id; scene.objects[0].position.setX(-1); bindSquare(scene.objects[0]);
        auto other=scene.objects[0]; other.id="other"; other.position.setX(1); scene.objects.append(other);
        scene.camera.orthoScale=2.5f; scene.textured=false;
        const auto original=scene; const auto originalPositions=model->meshes.first().positions;
        renderer.request(scene,{256,128});
        QTRY_VERIFY_WITH_TIMEOUT(!ready.isEmpty()||!errors.isEmpty(),30000);
        QVERIFY2(errors.isEmpty(),errors.isEmpty()?"":qPrintable(errors.last().at(1).toString()));
        const auto plain=qvariant_cast<QImage>(ready.last().at(1));
        QVERIFY(plain.pixelColor(77,64).green()>plain.pixelColor(77,64).red()*1.5);
        Scene3DRenderOptions options{"front","root",{},true};
        const auto heatRequest=renderer.request(scene,{256,128},options);
        QTRY_COMPARE_WITH_TIMEOUT(ready.size(),2,10000);
        const auto heat=qvariant_cast<QImage>(ready.last().at(1)); const auto hot=heat.pixelColor(77,64);
        QVERIFY2(hot.red()>hot.blue()*1.5,qPrintable(hot.name()));
        QCOMPARE(heat.pixelColor(179,64),plain.pixelColor(179,64)); QCOMPARE(heat.pixelColor(0,0).alpha(),0);
        QCOMPARE(renderer.request(scene,{256,128},options),heatRequest); QTest::qWait(20); QCOMPARE(ready.size(),2);
        options.skinJointId="tip"; renderer.request(scene,{256,128},options);
        QTRY_COMPARE_WITH_TIMEOUT(ready.size(),3,10000);
        auto cold=qvariant_cast<QImage>(ready.last().at(1)).pixelColor(77,64);
        QVERIFY2(cold.blue()>cold.red()*1.5,qPrintable(cold.name()));
        // Same object/joint/rotation, new normalized skin values: no stale GPU model.
        for(auto &vertex:scene.objects[0].skin->meshes[0]) vertex.weights={0,1,0,0};
        renderer.request(scene,{256,128},options); QTRY_COMPARE_WITH_TIMEOUT(ready.size(),4,10000);
        const auto updated=qvariant_cast<QImage>(ready.last().at(1)).pixelColor(77,64);
        QVERIFY2(updated.red()>updated.blue()*1.5,qPrintable(updated.name()));
        renderer.request(scene,{256,128}); QTRY_COMPARE_WITH_TIMEOUT(ready.size(),5,10000);
        QCOMPARE(qvariant_cast<QImage>(ready.last().at(1)),plain);
        QCOMPARE(model->meshes.first().positions,originalPositions); QCOMPARE(scene.models.value(model->id),model);
        QCOMPARE(scene.objects[1],original.objects[1]); QVERIFY(errors.isEmpty());
        // Weight metadata alone must not redraw an ordinary reference.
        scene.objects[0].skin=original.objects[0].skin;
        const auto normal=renderer.latestRequest(); QCOMPARE(renderer.request(scene,{256,128}),normal);
    }
    void revisionedWeightsUpdateAndCameraChangesKeepTheSameRevision() {
        REQUIRE_FILAMENT_RENDERER();
        Scene3DRenderService renderer; QSignalSpy ready(&renderer,&Scene3DRenderService::frameReady);
        QSignalSpy errors(&renderer,&Scene3DRenderService::failed);
        auto scene=squareScene(); bindSquare(scene.objects[0]);
        Scene3DRenderOptions options{"front","root",{},true,17};
        const auto first=renderer.request(scene,{120,120},options);
        QTRY_VERIFY_WITH_TIMEOUT(!ready.isEmpty()||!errors.isEmpty(),30000);
        QVERIFY2(errors.isEmpty(),errors.isEmpty()?"":qPrintable(errors.last().at(1).toString()));
        const auto red=qvariant_cast<QImage>(ready.last().at(1));
        QVERIFY(red.pixelColor(60,60).red()>red.pixelColor(60,60).blue()*2);
        QCOMPARE(renderer.request(scene,{120,120},options),first);
        scene.camera.target.setX(.5f);
        QVERIFY(renderer.request(scene,{120,120},options)!=first);
        QTRY_COMPARE_WITH_TIMEOUT(ready.size(),2,10000);
        const auto moved=qvariant_cast<QImage>(ready.last().at(1));
        QVERIFY(moved!=red); QCOMPARE(options.skinRevision,quint64(17));
        QVERIFY(moved.pixelColor(30,60).red()>moved.pixelColor(30,60).blue()*2);
        for(auto &vertex:scene.objects[0].skin->meshes[0]) QVERIFY(setSkinJointWeight(vertex,1,1));
        ++options.skinRevision;
        renderer.request(scene,{120,120},options);
        QTRY_COMPARE_WITH_TIMEOUT(ready.size(),3,10000);
        const auto blue=qvariant_cast<QImage>(ready.last().at(1));
        QVERIFY(blue.pixelColor(30,60).blue()>blue.pixelColor(30,60).red()*2);
        const auto final=renderer.latestRequest(); QCOMPARE(renderer.request(scene,{120,120},options),final);
        QVERIFY(errors.isEmpty());
    }
    void savedPosesRenderPerInstanceAndRestPreviewIsTransient() {
        REQUIRE_FILAMENT_RENDERER();
        Scene3DRenderService renderer; QSignalSpy ready(&renderer,&Scene3DRenderService::frameReady);
        QSignalSpy errors(&renderer,&Scene3DRenderService::failed);
        auto scene=squareScene(); bindSquare(scene.objects[0]); scene.objects[0].position.setX(-1);
        auto second=scene.objects[0]; second.id="second"; second.position.setX(1); scene.objects.append(second);
        renderer.request(scene,{256,128}); QTRY_VERIFY_WITH_TIMEOUT(!ready.isEmpty()||!errors.isEmpty(),30000);
        QVERIFY2(errors.isEmpty(),errors.isEmpty()?"":qPrintable(errors.last().at(1).toString()));
        const auto rest=qvariant_cast<QImage>(ready.last().at(1));
        scene.objects[0].pose.rootTranslation={0,.6f,0};
        scene.objects[1].pose.joints={{"root",{0,0,25}}};
        const auto saved=scene;
        renderer.request(scene,{256,128}); QTRY_COMPARE_WITH_TIMEOUT(ready.size(),2,10000);
        const auto posed=qvariant_cast<QImage>(ready.last().at(1)); QVERIFY(posed!=rest);
        QCOMPARE(posed.pixelColor(64,100).alpha(),0); QVERIFY(posed.pixelColor(192,64).red()>100);
        Scene3DRenderOptions options; options.skinObjectId="front"; options.restPose=true;
        renderer.request(scene,{256,128},options); QTRY_COMPARE_WITH_TIMEOUT(ready.size(),3,10000);
        const auto editing=qvariant_cast<QImage>(ready.last().at(1));
        QCOMPARE(editing.copy(0,0,128,128),rest.copy(0,0,128,128));
        QCOMPARE(editing.copy(128,0,128,128),posed.copy(128,0,128,128));
        options.weights=true; options.skinJointId="root";
        renderer.request(scene,{256,128},options); QTRY_COMPARE_WITH_TIMEOUT(ready.size(),4,10000);
        QVERIFY(qvariant_cast<QImage>(ready.last().at(1)).pixelColor(64,100).alpha()>240);
        renderer.request(scene,{256,128}); QTRY_COMPARE_WITH_TIMEOUT(ready.size(),5,10000);
        QCOMPARE(qvariant_cast<QImage>(ready.last().at(1)),posed); QCOMPARE(scene,saved); QVERIFY(errors.isEmpty());
    }
    void savedPoseChangesWhenBindingWeightsChange() {
        REQUIRE_FILAMENT_RENDERER();
        Scene3DRenderService renderer; QSignalSpy ready(&renderer,&Scene3DRenderService::frameReady);
        QSignalSpy errors(&renderer,&Scene3DRenderService::failed);
        auto scene=squareScene(); bindSquare(scene.objects[0]); scene.objects[0].pose.joints={{"tip",{0,0,45}}};
        renderer.request(scene,{160,160}); QTRY_VERIFY_WITH_TIMEOUT(!ready.isEmpty()||!errors.isEmpty(),30000);
        QVERIFY2(errors.isEmpty(),errors.isEmpty()?"":qPrintable(errors.last().at(1).toString()));
        const auto original=qvariant_cast<QImage>(ready.last().at(1));
        for(auto &vertex:scene.objects[0].skin->meshes[0])QVERIFY(setSkinJointWeight(vertex,1,1));
        renderer.request(scene,{160,160}); QTRY_COMPARE_WITH_TIMEOUT(ready.size(),2,10000);
        QVERIFY(qvariant_cast<QImage>(ready.last().at(1))!=original); QVERIFY(errors.isEmpty());
    }
    void posedBoundsExtendTheRenderClipRange() {
        REQUIRE_FILAMENT_RENDERER();
        Scene3DRenderService renderer; QSignalSpy ready(&renderer,&Scene3DRenderService::frameReady);
        QSignalSpy errors(&renderer,&Scene3DRenderService::failed); auto scene=squareScene();
        scene.objects[0].position.setX(-1); auto blue=squareModel(Qt::blue,'p'); scene.models.insert(blue->id,blue);
        ModelInstance3D distant{"distant","Distant",blue->id,{1,0,0}}; bindSquare(distant);
        distant.pose.rootTranslation={0,0,-200}; scene.objects.append(distant);
        renderer.request(scene,{256,128}); QTRY_VERIFY_WITH_TIMEOUT(!ready.isEmpty()||!errors.isEmpty(),30000);
        QVERIFY2(errors.isEmpty(),errors.isEmpty()?"":qPrintable(errors.last().at(1).toString()));
        const auto image=qvariant_cast<QImage>(ready.last().at(1));
        QVERIFY(image.pixelColor(64,64).red()>image.pixelColor(64,64).blue()*2);
        QVERIFY(image.pixelColor(192,64).blue()>image.pixelColor(192,64).red()*2);
    }
    void bendPreviewPreservesMaterialsAndOriginalGeometry() {
        REQUIRE_FILAMENT_RENDERER();
        Scene3DRenderService renderer; QSignalSpy ready(&renderer,&Scene3DRenderService::frameReady);
        QSignalSpy errors(&renderer,&Scene3DRenderService::failed);
        auto scene=squareScene(); bindSquare(scene.objects[0]);
        const auto original=scene; const auto model=scene.models.value(scene.objects[0].modelId);
        renderer.request(scene,{160,160}); QTRY_VERIFY_WITH_TIMEOUT(!ready.isEmpty()||!errors.isEmpty(),30000);
        QVERIFY2(errors.isEmpty(),errors.isEmpty()?"":qPrintable(errors.last().at(1).toString()));
        const auto plain=qvariant_cast<QImage>(ready.last().at(1));
        Scene3DRenderOptions options{"front","root",{0,0,45},false};
        renderer.request(scene,{160,160},options); QTRY_COMPARE_WITH_TIMEOUT(ready.size(),2,10000);
        const auto bent=qvariant_cast<QImage>(ready.last().at(1)); QVERIFY(bent!=plain);
        QVERIFY(bent.pixelColor(80,80).red()>bent.pixelColor(80,80).blue()*2);
        QCOMPARE(scene,original); QCOMPARE(scene.models.value(model->id),model);
        renderer.request(scene,{160,160}); QTRY_COMPARE_WITH_TIMEOUT(ready.size(),3,10000);
        QCOMPARE(qvariant_cast<QImage>(ready.last().at(1)),plain); QVERIFY(errors.isEmpty());
    }
    void invalidSkinPreviewRecoversWithoutReplacingNormalReference() {
        REQUIRE_FILAMENT_RENDERER();
        Scene3DRenderService renderer; QSignalSpy ready(&renderer,&Scene3DRenderService::frameReady);
        QSignalSpy errors(&renderer,&Scene3DRenderService::failed); auto scene=squareScene();
        Scene3DRenderOptions options{"front","root",{},true}; renderer.request(scene,{80,80},options);
        QTRY_COMPARE_WITH_TIMEOUT(errors.size(),1,30000); QVERIFY(errors.last().at(1).toString().contains("Bind"));
        bindSquare(scene.objects[0]); options.skinJointId="missing"; renderer.request(scene,{80,80},options);
        QTRY_COMPARE_WITH_TIMEOUT(errors.size(),2,10000);
        renderer.request(scene,{80,80}); QTRY_COMPARE_WITH_TIMEOUT(ready.size(),1,10000);
        QVERIFY(qvariant_cast<QImage>(ready.last().at(1)).pixelColor(40,40).red()>100);
    }
    void weightHeatmapIsOpaqueWithoutChangingCutoutMaterial() {
        REQUIRE_FILAMENT_RENDERER();
        Scene3DRenderService renderer; QSignalSpy ready(&renderer,&Scene3DRenderService::frameReady);
        QSignalSpy errors(&renderer,&Scene3DRenderService::failed); auto scene=squareScene();
        QImage transparent(4,4,QImage::Format_RGBA8888); transparent.fill(Qt::transparent);
        auto cutout=squareModel(Qt::white,'f',transparent,.5f);
        scene.models.clear(); scene.models.insert(cutout->id,cutout); scene.objects[0].modelId=cutout->id; bindSquare(scene.objects[0]);
        renderer.request(scene,{80,80}); QTRY_VERIFY_WITH_TIMEOUT(!ready.isEmpty()||!errors.isEmpty(),30000);
        QVERIFY(errors.isEmpty()); QCOMPARE(qvariant_cast<QImage>(ready.last().at(1)).pixelColor(40,40).alpha(),0);
        renderer.request(scene,{80,80},{"front","root",{},true}); QTRY_COMPARE_WITH_TIMEOUT(ready.size(),2,10000);
        QVERIFY(qvariant_cast<QImage>(ready.last().at(1)).pixelColor(40,40).alpha()>240);
        QCOMPARE(cutout->meshes[0].alphaCutoff,.5f); QCOMPARE(cutout->meshes[0].baseColorTexture,transparent);
        renderer.request(scene,{80,80}); QTRY_COMPARE_WITH_TIMEOUT(ready.size(),3,10000);
        QCOMPARE(qvariant_cast<QImage>(ready.last().at(1)).pixelColor(40,40).alpha(),0);
    }
    void previewHumanoidMannequinPose() {
        REQUIRE_FILAMENT_RENDERER();
        const auto output=qEnvironmentVariable("TOON_UI_PREVIEW");
        if(output.isEmpty())QSKIP("Opt-in bound mannequin pose preview.");
        Model3DPtr mannequin;
        for(const auto &model:builtInModels3D())if(model->name.startsWith("Mannequin")){mannequin=model;break;}
        QVERIFY(mannequin);
        Scene3D scene; scene.models.insert(mannequin->id,mannequin);
        ModelInstance3D object{"mannequin","Bound mannequin",mannequin->id};
        object.rig=createHumanoidRig3D(mannequin->bounds); QVERIFY(!object.rig.joints.isEmpty());
        QString error; object.skin=autoBindSkin3D(*mannequin,object.rig,&error);
        QVERIFY2(object.skin.has_value(),qPrintable(error));
        const auto jointId=[&](const QString &name) {
            for(const auto &joint:object.rig.joints)if(joint.name==name)return joint.id;
            return QString();
        };
        const auto wrist=jointId("Left wrist"),ankle=jointId("Right ankle");
        QVERIFY(!wrist.isEmpty()); QVERIFY(!ankle.isEmpty());
        const QVector3D handTarget(.65f,1.5f,.15f),footTarget(-.28f,.38f,.25f);
        QVERIFY2(solvePoseIK3D(object.pose,object.rig,{wrist,handTarget,{.95f,1.1f,.03f},true},&error),qPrintable(error));
        QVERIFY2(solvePoseIK3D(object.pose,object.rig,{ankle,footTarget,{-.14f,.55f,1.f},true},&error),qPrintable(error));
        const auto evaluated=evaluatePose3D(object.rig,object.pose,&error); QVERIFY2(evaluated.has_value(),qPrintable(error));
        QVERIFY((evaluated->positions[evaluated->jointIds.indexOf(wrist)]-handTarget).length()<.005f);
        QVERIFY((evaluated->positions[evaluated->jointIds.indexOf(ankle)]-footTarget).length()<.005f);
        scene.objects.append(object);
        scene.camera.target={0,1,0}; scene.camera.yaw=scene.camera.pitch=0;
        scene.camera.distance=4; scene.camera.orthographic=true; scene.camera.orthoScale=2.6f;
        Scene3DRenderService renderer; QSignalSpy ready(&renderer,&Scene3DRenderService::frameReady);
        QSignalSpy errors(&renderer,&Scene3DRenderService::failed);
        Scene3DRenderOptions restOptions; restOptions.skinObjectId=object.id; restOptions.restPose=true;
        renderer.request(scene,{640,800},restOptions);
        QTRY_VERIFY_WITH_TIMEOUT(!ready.isEmpty()||!errors.isEmpty(),30000);
        QVERIFY2(errors.isEmpty(),errors.isEmpty()?"":qPrintable(errors.last().at(1).toString()));
        const auto rest=qvariant_cast<QImage>(ready.last().at(1));
        renderer.request(scene,{640,800}); QTRY_COMPARE_WITH_TIMEOUT(ready.size(),2,10000);
        const auto front=qvariant_cast<QImage>(ready.last().at(1)); QVERIFY(front!=rest);
        scene.camera.orthographic=false; scene.camera.yaw=28; scene.camera.pitch=12; scene.camera.fieldOfView=40;
        renderer.request(scene,{640,800}); QTRY_COMPARE_WITH_TIMEOUT(ready.size(),3,10000);
        const auto perspective=qvariant_cast<QImage>(ready.last().at(1));
        QVERIFY(errors.isEmpty()); QVERIFY(perspective!=front);
        QCOMPARE(scene.objects[0],object); QCOMPARE(scene.models.value(mannequin->id),mannequin);
        QDir().mkpath(output);
        QVERIFY(rest.save(output+"/scene3d-mannequin-rest.png"));
        QVERIFY(front.save(output+"/scene3d-mannequin-posed-front.png"));
        QVERIFY(perspective.save(output+"/scene3d-mannequin-posed-perspective.png"));
        QImage comparison(1920,856,QImage::Format_ARGB32_Premultiplied); comparison.fill(QColor("#eceef2"));
        { QPainter painter(&comparison); painter.setPen(QColor("#202530"));
            auto font=painter.font();font.setPixelSize(24);painter.setFont(font);
            const std::array<QString,3> labels{"Rest skeleton + automatic binding","IK pose · bent elbow and knee","Same pose · perspective view"};
            const std::array<QImage,3> images{rest,front,perspective};
            for(int i=0;i<3;++i){painter.drawText(QRect(i*640,0,640,56),Qt::AlignCenter,labels[i]);painter.drawImage(i*640,56,images[i]);}
        }
        QVERIFY(comparison.save(output+"/scene3d-mannequin-pose-comparison.png"));
    }
    void previewBuiltins() {
        REQUIRE_FILAMENT_RENDERER();
        const auto output=qEnvironmentVariable("TOON_UI_PREVIEW");
        if(output.isEmpty()) QSKIP("Opt-in 3D render preview.");
        Scene3DRenderService renderer;
        QSignalSpy ready(&renderer,&Scene3DRenderService::frameReady);
        QSignalSpy errors(&renderer,&Scene3DRenderService::failed);
        Scene3D scene;int index=0;
        for(const auto &model:builtInModels3D()) {
            scene.models.insert(model->id,model);
            scene.objects.append({QString::number(index),model->name,model->id,{float(index)*1.5f,0,0}});
            ++index;
        }
        scene.camera=fitCamera3D(scene.camera,sceneBounds(scene));
        renderer.request(scene,{1000,640});
        QTRY_VERIFY_WITH_TIMEOUT(!ready.isEmpty() || !errors.isEmpty(),30000);
        QVERIFY2(errors.isEmpty(), errors.isEmpty() ? "" : qPrintable(errors.first().at(1).toString()));
        QDir().mkpath(output);
        QVERIFY(qvariant_cast<QImage>(ready.last().at(1)).save(output+"/scene3d-starter-models.png"));
    }
};
QTEST_MAIN(Scene3DRendererTest)
#include "test_scene3d_renderer.moc"
