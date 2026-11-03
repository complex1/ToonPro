#include "skin3d_view.h"
#include "scene3d_renderer.h"
#include <QElapsedTimer>
#include <QTest>
#include <algorithm>
#include <cmath>
#include <limits>

using namespace toon;
namespace {
ModelMesh3D square(float z = 0) {
    ModelMesh3D mesh; mesh.name = "Surface";
    mesh.positions = {{-1,-1,z},{1,-1,z},{1,1,z},{-1,1,z},{0,0,z}};
    mesh.normals = QVector<QVector3D>(5, {0,0,1});
    mesh.texCoords = {{0,1},{1,1},{1,0},{0,0},{.5f,.5f}};
    mesh.indices = {0,1,4,1,2,4,2,3,4,3,0,4}; return mesh;
}
Model3DPtr model(QVector<ModelMesh3D> meshes, QChar id = 'a') {
    auto result = std::make_shared<Model3D>(); result->id = QString(64, id); result->name = "Paint target"; result->meshes = std::move(meshes);
    for (const auto &mesh : result->meshes) for (const auto &vertex : mesh.positions) result->bounds.include(mesh.transform.map(vertex));
    return result;
}
Scene3D scene(Model3DPtr mesh) {
    Scene3D result; result.camera.target = {}; result.camera.yaw = result.camera.pitch = 0;
    result.camera.distance = 3; result.camera.orthographic = true; result.camera.orthoScale = 4;
    result.models.insert(mesh->id, mesh); result.objects.append({"target", "Target", mesh->id}); return result;
}
QPointF projected(const Scene3D &scene, int mesh, int vertex, QSize size, QTransform widget = {}) {
    const auto &object = scene.objects.first(); const auto model = scene.models.value(object.modelId);
    const auto matrix = cameraProjectionMatrix(scene.camera, float(size.width()) / size.height()) * cameraViewMatrix(scene.camera)
        * instanceMatrix(object) * model->meshes[mesh].transform;
    auto clip = matrix * QVector4D(model->meshes[mesh].positions[vertex], 1); clip /= clip.w();
    return widget.map(QPointF((clip.x() + 1) * size.width() / 2, (1 - clip.y()) * size.height() / 2));
}
}
class Skin3DViewTests final : public QObject {
    Q_OBJECT
private slots:
    void paletteMatchesWeightsAndClamps() {
        QCOMPARE(skinWeightColor(0), QColor("#2563eb")); QCOMPARE(skinWeightColor(1), QColor("#ff3030"));
        QCOMPARE(skinWeightColor(1.f/3), QColor("#00cfff")); QCOMPARE(skinWeightColor(2.f/3), QColor("#ffdb00"));
        QCOMPARE(skinWeightColor(-1), skinWeightColor(0)); QCOMPARE(skinWeightColor(2), skinWeightColor(1));
        QCOMPARE(skinWeightColor(std::numeric_limits<float>::quiet_NaN()), skinWeightColor(0));
    }
    void brushUsesWidgetCoordinatesAndSmoothFalloff_data() {
        QTest::addColumn<bool>("perspective"); QTest::newRow("orthographic") << false; QTest::newRow("perspective") << true;
    }
    void brushUsesWidgetCoordinatesAndSmoothFalloff() {
        QFETCH(bool, perspective);
        auto mesh = square(); mesh.transform.translate(.1f, .15f, 0); auto source = scene(model({mesh}));
        source.camera.orthographic = !perspective; source.camera.yaw = 12; source.camera.pitch = -8;
        source.objects[0].position = {.2f, -.1f, .1f}; source.objects[0].rotation = {5, 13, 17}; source.objects[0].scale = {1.1f,.8f,1.3f};
        QTransform view; view.translate(240, 65); view.rotate(23); view.scale(1.4, 1.4);
        SkinPaintProjection picker; QString error; QVERIFY2(picker.build(source, "target", {600,400}, view, &error), qPrintable(error));
        const auto center = projected(source, 0, 4, {600,400}, view);
        const auto hits = picker.verticesInBrush(center, 20); QCOMPARE(hits.size(), 1);
        QCOMPARE(hits.first().mesh, 0); QCOMPARE(hits.first().vertex, 4); QVERIFY(hits.first().falloff > .999f);
        const auto half = picker.verticesInBrush(center + QPointF(10,0), 20); QCOMPARE(half.size(), 1);
        QVERIFY(std::abs(half.first().falloff - .5f) < .001f);
        QVERIFY(picker.verticesInBrush(center + QPointF(30,0), 20).isEmpty());
        auto copy = picker; QVERIFY(picker.build(source, "missing", {600,400}, view, &error) == false);
        QCOMPARE(picker.visibleVertexCount(), 0); QCOMPARE(copy.verticesInBrush(center,20).size(), 1);
    }
    void frontSurfaceRejectsBackVerticesAndOtherObjectsOcclude() {
        auto source = scene(model({square(.1f), square(0)}));
        SkinPaintProjection picker; QVERIFY(picker.build(source,"target",{400,400},{}));
        const auto hits = picker.verticesInBrush({200,200},30); QCOMPARE(hits.size(),1); QCOMPARE(hits.first().mesh,0);
        auto blocker = model({square()}, 'b'); source.models.insert(blocker->id, blocker);
        source.objects.append({"blocker","Blocker",blocker->id,{0,0,.3f},{},{.5f,.5f,1}});
        QVERIFY(picker.build(source,"target",{400,400},{})); QVERIFY(picker.verticesInBrush({200,200},30).isEmpty());
        QVERIFY(!picker.verticesInBrush(projected(source,0,0,{400,400}),10).isEmpty());
        source.objects[1].visible=false; QVERIFY(picker.build(source,"target",{400,400},{}));
        QCOMPARE(picker.verticesInBrush({200,200},30).size(),1);
    }
    void alphaHolesDoNotHideTheTarget() {
        auto source = scene(model({square()})); auto transparent = square(); transparent.alphaCutoff=.5f;
        transparent.baseColorTexture = QImage(4,4,QImage::Format_RGBA8888); transparent.baseColorTexture.fill(Qt::transparent);
        auto blocker = model({transparent},'b'); source.models.insert(blocker->id,blocker);
        source.objects.append({"blocker","Cutout",blocker->id,{0,0,.2f}});
        SkinPaintProjection picker; QVERIFY(picker.build(source,"target",{400,400},{}));
        QCOMPARE(picker.verticesInBrush({200,200},20).size(),1);
        source.textured=false;
        QVERIFY(picker.build(source,"target",{400,400},{}));
        QVERIFY(picker.verticesInBrush({200,200},20).isEmpty());
        transparent.baseColor=Qt::transparent;
        source.models[blocker->id]=model({transparent},'b');
        QVERIFY(picker.build(source,"target",{400,400},{}));
        QCOMPARE(picker.verticesInBrush({200,200},20).size(),1);
    }
    void selectedCutoutIsPaintableWhereOpaqueHeatmapIsVisible() {
        auto transparent=square(); transparent.alphaCutoff=.5f; transparent.baseColor=Qt::transparent;
        transparent.baseColorTexture=QImage(4,4,QImage::Format_RGBA8888); transparent.baseColorTexture.fill(Qt::transparent);
        auto source=scene(model({transparent})); SkinPaintProjection picker;
        QVERIFY(picker.build(source,"target",{400,400},{}));
        QCOMPARE(picker.visibleVertexCount(),5);
        QCOMPARE(picker.verticesInBrush({200,200},20).size(),1);
        auto opaque=model({square()},'b'); source.models.insert(opaque->id,opaque);
        source.objects.append({"blocker","Opaque",opaque->id,{0,0,.2f}});
        QVERIFY(picker.build(source,"target",{400,400},{}));
        QVERIFY(picker.verticesInBrush({200,200},20).isEmpty());
        QCOMPARE(source.models.value(source.objects[0].modelId)->meshes[0].baseColor, QColor(Qt::transparent));
    }
    void targetStaysAtRestWhileOtherObjectsOccludeInTheirSavedPoses() {
        const auto bind=[](ModelInstance3D &object) {
            object.rig.joints={{"root","Root",{},{},{}}};
            SkinVertex3D vertex; vertex.weights={1,0,0,0};
            Skin3D skin; skin.jointIds={"root"}; skin.meshes={QVector<SkinVertex3D>(5,vertex)}; object.skin=skin;
        };
        auto source=scene(model({square()})); bind(source.objects[0]); source.objects[0].pose.rootTranslation={2,0,0};
        auto blocker=model({square()},'b'); source.models.insert(blocker->id,blocker);
        source.objects.append({"blocker","Posed blocker",blocker->id,{3,0,.2f}}); bind(source.objects[1]);
        source.objects[1].pose.rootTranslation={-3,0,0};
        const auto original=source; SkinPaintProjection picker;
        QVERIFY(picker.build(source,"target",{400,400},{}));
        QVERIFY(picker.verticesInBrush({200,200},20).isEmpty());
        source.objects[1].pose.rootTranslation={};
        QVERIFY(picker.build(source,"target",{400,400},{}));
        QCOMPARE(picker.verticesInBrush({200,200},20).size(),1);
        QCOMPARE(source.objects[0],original.objects[0]);
        QCOMPARE(source.models.value(blocker->id),blocker);
    }
    void depthBufferAndQueriesStayBounded() {
        auto source=scene(model({square()})); SkinPaintProjection picker;
        QVERIFY(picker.build(source,"target",{16384,8192},{}));
        QCOMPARE(picker.depthSize(),QSize(1024,512));
        QCOMPARE(picker.verticesInBrush({8192,4096},20).size(),1);
        QVERIFY(picker.verticesInBrush({8192,4096},0).isEmpty());
        QVERIFY(picker.verticesInBrush({8192,4096},std::numeric_limits<qreal>::infinity()).isEmpty());
        QString error; QVERIFY(!picker.build(source,"target",{}, {},&error)); QVERIFY(!error.isEmpty());
        QVERIFY(!picker.build(source,"target",{400,400},QTransform::fromScale(0,0),&error));
        source.objects[0].visible=false; QVERIFY(!picker.build(source,"target",{400,400},{},&error));
    }
    void malformedTrianglesAreRejectedAndClippedTrianglesRemainPickable() {
        auto broken=square(); broken.indices[0]=999; auto source=scene(model({broken})); SkinPaintProjection picker; QString error;
        QVERIFY(!picker.build(source,"target",{400,400},{},&error)); QVERIFY(error.contains("triangle"));
        auto wide=square(); wide.positions[0].setX(-20); wide.positions[1].setX(20);
        source=scene(model({wide})); QVERIFY2(picker.build(source,"target",{400,400},{},&error),qPrintable(error));
        QCOMPARE(picker.verticesInBrush({200,200},20).size(),1);
    }
    void nearAndFarSurfacesClippedByRendererCannotBePainted() {
        auto source=scene(model({square()})); SkinPaintProjection picker;
        for(bool orthographic:{true,false}) {
            source.camera.orthographic=orthographic;
            for(float depth:{.005f,101.f}) {
                source.objects[0].position.setZ(source.camera.distance-depth);
                QVERIFY(picker.build(source,"target",{400,400},{}));
                QCOMPARE(picker.visibleVertexCount(),0);
                QVERIFY(picker.verticesInBrush({200,200},20).isEmpty());
            }
            source.objects[0].position.setZ(0);
            QVERIFY(picker.build(source,"target",{400,400},{}));
            QVERIFY(picker.visibleVertexCount()>0);
            QCOMPARE(picker.verticesInBrush({200,200},20).size(),1);
        }
    }
    void largeMeshProjectionAndRepeatedBrushQueries() {
        if (!qEnvironmentVariableIsSet("TOON_SKIN_BENCHMARK")) QSKIP("Set TOON_SKIN_BENCHMARK for the large-mesh projection check");
        constexpr int side=704;
        ModelMesh3D mesh; mesh.name="Dense surface"; mesh.positions.reserve(side*side); mesh.normals.reserve(side*side);
        for(int y=0;y<side;++y)for(int x=0;x<side;++x) {
            mesh.positions.append({float(x)*2/(side-1)-1,float(y)*2/(side-1)-1,0}); mesh.normals.append({0,0,1});
        }
        for(int y=0;y<side-1;++y)for(int x=0;x<side-1;++x) {
            const quint32 a=y*side+x,b=a+1,c=a+side+1,d=a+side;
            mesh.indices.append({a,b,c,a,c,d});
        }
        auto source=scene(model({mesh})); SkinPaintProjection picker; QString error; QElapsedTimer timer; timer.start();
        QVERIFY2(picker.build(source,"target",{2048,2048},{},&error),qPrintable(error)); const auto buildMs=timer.elapsed();
        QCOMPARE(picker.depthSize(),QSize(1024,1024)); QCOMPARE(picker.visibleVertexCount(),side*side);
        timer.restart(); qsizetype affected=0;
        for(int i=0;i<1000;++i) affected+=picker.verticesInBrush({1024.+i%20,1024.+i%15},12).size();
        const auto queryMs=timer.elapsed(); QVERIFY(affected>1000); QVERIFY(affected<1000000);
        qInfo().nospace()<<"skin-picking vertices="<<side*side<<" build_ms="<<buildMs<<" queries=1000 query_ms="<<queryMs<<" affected="<<affected;
        Rig3D rig; rig.joints={{"root","Root",{},{},{0,-.8f,0}}, {"tip","Tip","root",{},{0,.8f,0}}};
        timer.restart(); const auto skin=autoBindSkin3D(*source.models.value(source.objects.first().modelId),rig,&error);
        QVERIFY2(skin.has_value(),qPrintable(error)); const auto bindMs=timer.elapsed();
        timer.restart(); QByteArray fingerprint;
        for(int i=0;i<10;++i) fingerprint=skin3DCacheKey(*skin);
        QVERIFY(fingerprint.size()==32);
        qInfo().nospace()<<"skin-binding vertices="<<side*side<<" auto_bind_ms="<<bindMs
            <<" hash_iterations=10 hash_total_ms="<<timer.elapsed()<<" packed_bytes="<<skin3DByteSize(*skin);
        source.objects[0].rig=rig; source.objects[0].skin=skin;
        Scene3DRenderService renderer; Scene3DRenderOptions options{"target","root",{},true,0};
        timer.restart(); quint64 request=0;
        for(int i=0;i<10;++i) { ++options.skinRevision; request=renderer.request(source,{2048,2048},options); }
        const auto requestNs=timer.nsecsElapsed();
        QCOMPARE(request,quint64(10)); QCOMPARE(renderer.request(source,{2048,2048},options),request);
        QCOMPARE(renderer.renderCount(),quint64(0)); renderer.clear(); QVERIFY(!renderer.busy());
        qInfo().nospace()<<"skin-revisioned-render-request vertices="<<side*side
            <<" iterations=10 total_us="<<requestNs/1000<<" render_count="<<renderer.renderCount();
    }
};
QTEST_GUILESS_MAIN(Skin3DViewTests)
#include "test_skin3d_view.moc"
