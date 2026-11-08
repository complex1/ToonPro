#include "reference3d_render.h"
#include "raster.h"
#include "effect_render.h"
#include "reference3d_catalog.h"
#include "reference3d_pose_library.h"
#include "scene3d_cache.h"
#include "sheet.h"
#include <QTemporaryDir>
#include <QtTest>
#include <QPainter>
using namespace toon;
namespace {
ReferenceScene quad(ReferenceAlphaMode alpha=ReferenceAlphaMode::Opaque,QColor color=Qt::red) {
    ReferenceAsset source;source.name="Native reference render fixture";
    ReferenceMaterial material;material.baseColor=color;material.alphaMode=alpha;source.materials.append(material);
    ReferencePrimitive primitive;primitive.materialIndex=0;primitive.indices={0,2,1,0,3,2};
    for(const auto &position:QVector<QVector3D>{{-.5f,.5f,0},{.5f,.5f,0},{.5f,-.5f,0},{-.5f,-.5f,0}}){ReferenceVertex vertex;vertex.position=position;vertex.normal={0,0,1};primitive.vertices.append(vertex);}
    ReferenceMesh mesh;mesh.primitives.append(primitive);source.meshes.append(mesh);ReferenceNode node;node.meshIndex=0;source.nodes.append(node);
    ReferenceInstance instance;instance.id="quad";instance.asset=finalizeReferenceAsset(std::move(source));
    ReferenceScene scene;scene.instances={instance};scene.camera.target={0,0,0};scene.camera.yaw=0;scene.camera.pitch=0;scene.camera.distance=3;scene.camera.orthographic=true;scene.camera.orthoScale=2;scene.light.intensity=0;scene.light.ambientIntensity=1;return scene;
}
RasterLayer reference(const ReferenceScene &scene){auto layer=RasterLayer::create("3D reference");layer.kind=LayerKind::Reference3D;layer.reference3d=scene;return layer;}
QImage compose(const QVector<RasterLayer> &layers,QSize frame={256,256}){
    QImage image(frame,QImage::Format_ARGB32_Premultiplied);image.fill(Qt::white);QPainter painter(&image);paintLayers(painter,layers,true,nullptr,false,frame);painter.end();return image;
}
}
class ReferenceLayerRenderTests final:public QObject{
 Q_OBJECT
private slots:
 void transparentBackgroundNeverCoversDrawingPaper(){
    const auto scene=quad();const auto source=serializeReferenceScene(scene);ReferenceRenderCache cache;const auto frame=renderReferenceScene(scene,{256,256},{},cache);
    QVERIFY(frame.error.isEmpty());QVERIFY(!frame.image.isNull());QCOMPARE(frame.image.format(),QImage::Format_ARGB32_Premultiplied);
    QCOMPARE(frame.image.pixelColor(0,0).alpha(),0);QCOMPARE(frame.image.pixelColor(128,128),QColor(Qt::red));QCOMPARE(serializeReferenceScene(scene),source);
    const auto image=compose({reference(scene)});QCOMPARE(image.pixelColor(0,0),QColor(Qt::white));QCOMPARE(image.pixelColor(128,128),QColor(Qt::red));
 }
 void blendedMaterialsKeepPremultipliedCoverage(){
    const auto scene=quad(ReferenceAlphaMode::Blend,QColor(255,0,0,128));QString error;const auto image=renderReferenceLayer(scene,{256,256},&error);QVERIFY2(!image.isNull(),qPrintable(error));
    const auto pixel=image.pixel(128,128);QVERIFY(std::abs(qAlpha(pixel)-128)<=1);QVERIFY(qRed(pixel)<=qAlpha(pixel));QCOMPARE(qGreen(pixel),0);QCOMPARE(qBlue(pixel),0);
    const auto drawn=compose({reference(scene)});const auto color=drawn.pixelColor(128,128);QCOMPARE(color.red(),255);QVERIFY(std::abs(color.green()-127)<=1);QVERIFY(std::abs(color.blue()-127)<=1);
 }
 void paintAboveAndLayerOpacityAreAppliedOnce(){
    auto layer=reference(quad());layer.opacity=.5;auto drawing=RasterLayer::create("Ink above");QImage tile(256,256,QImage::Format_ARGB32_Premultiplied);tile.fill(Qt::transparent);QPainter p(&tile);p.fillRect(QRect(120,120,16,16),Qt::blue);p.end();drawing.tiles.insert({0,0},tile);
    const auto image=compose({layer,drawing});QCOMPARE(image.pixelColor(128,128),QColor(Qt::blue));const auto red=image.pixelColor(100,100);QCOMPARE(red.red(),255);QVERIFY(std::abs(red.green()-128)<=1);
    layer.visible=false;const auto hidden=compose({layer,drawing});QCOMPARE(hidden.pixelColor(100,100),QColor(Qt::white));QCOMPARE(hidden.pixelColor(128,128),QColor(Qt::blue));
 }
 void folderTransformsAndEffectsKeepReferenceCoverage(){
    auto folder=RasterLayer::create("Reference group");folder.group=true;folder.renderTransform.translate(60,0);folder.opacity=.5;
    auto layer=reference(quad());layer.parentId=folder.id;
    const auto transformed=compose({folder,layer});QCOMPARE(transformed.pixelColor(75,128),QColor(Qt::white));QVERIFY(transformed.pixelColor(200,128).green()<200);
    folder.renderTransform={};folder.effects.enabled=true;folder.effects.colorEnabled=true;folder.effects.exposure=1;
    const auto effected=compose({folder,layer});QVERIFY2(effectRenderError().isEmpty(),qPrintable(effectRenderError()));QVERIFY(effected.pixelColor(128,128).green()<200);QCOMPARE(effected.pixelColor(0,0),QColor(Qt::white));
 }
 void cameraAndModelChangesInvalidateTheDerivedImage(){
    auto scene=quad();const auto image=renderReferenceLayer(scene,{256,256});QCOMPARE(image.cacheKey(),renderReferenceLayer(scene,{256,256}).cacheKey());
    scene.instances[0].translation={.8f,0,0};const auto moved=renderReferenceLayer(scene,{256,256});QVERIFY(image!=moved);QCOMPARE(moved.pixelColor(128,128).alpha(),0);
    scene.camera.target={.8f,0,0};const auto recentered=renderReferenceLayer(scene,{256,256});QCOMPARE(recentered.pixelColor(128,128),QColor(Qt::red));
 }
 void pendingCanvasScopeCannotRenderSynchronouslyOrLeakIntoExports(){
    auto layer=reference(quad());QHash<QString,QImage> pending;pending.insert(layer.id,{});
    {ReferenceLayerPreviewScope scope(pending);const auto canvas=compose({layer});QCOMPARE(canvas.pixelColor(128,128),QColor(Qt::white));}
    const auto exported=compose({layer});QCOMPARE(exported.pixelColor(128,128),QColor(Qt::red));
 }
 void renderFailuresAreReportedToExportCallers(){
    auto scene=quad();scene.instances[0].asset.reset();const auto image=compose({reference(scene)});QVERIFY(!image.isNull());QVERIFY(!effectRenderError().isEmpty());
 }
 void actualTexturedCatalogScenesSurviveNativeProjectExportsAndReopen(){
    QString error;auto scene=quad();scene.instances.clear();
    ReferenceInstance anime;anime.id="anime-export";anime.asset=createReferenceCatalogAsset("anime/seed-san",&error);QVERIFY2(anime.asset,qPrintable(error));
    bool textured=false;for(const auto &material:anime.asset->materials)textured|=!material.baseColorTexture.isNull();QVERIFY(textured);
    QVERIFY2(applyReferencePosePreset("body/relaxed-standing",anime,&error),qPrintable(error));QVERIFY2(setReferenceExpression(anime,"happy",.6f,&error),qPrintable(error));anime.translation={-.45f,0,0};scene.instances.append(anime);
    ReferenceInstance pet;pet.id="pet-export";pet.asset=createReferenceCatalogAsset("pet/dog",&error);QVERIFY2(pet.asset,qPrintable(error));pet.translation={.45f,0,0};scene.instances.append(pet);
    scene.camera.target={0,.8f,0};scene.camera.orthoScale=2.2f;
    auto project=Project::create(ProjectKind::DrawingOnly);project.width=320;project.height=240;
    auto layer=reference(scene);layer.opacity=.6;project.shots[0].layers.append(layer);
    auto ink=RasterLayer::create("Ink over models");QImage tile(256,256,QImage::Format_ARGB32_Premultiplied);tile.fill(Qt::transparent);{QPainter painter(&tile);painter.fillRect(QRect(156,116,8,8),Qt::blue);}ink.tiles.insert({0,0},tile);project.shots[0].layers.append(ink);
    QVERIFY2(project.validate().isEmpty(),qPrintable(project.validate()));const auto source=serializeReferenceScene(scene);
    const auto frame=renderMovieFrame(project);QVERIFY(!frame.isNull());QCOMPARE(frame.pixelColor(160,120),QColor(Qt::blue));
    int colored=0;for(int y=0;y<frame.height();++y)for(int x=0;x<frame.width();++x)colored+=frame.pixelColor(x,y)!=QColor(Qt::white);QVERIFY(colored>500);
    const auto sheet=renderSheet(project,project.shots[0],false);QVERIFY(!sheet.isNull());const auto origin=sheetGeometry(project,project.shots[0]).output.topLeft().toPoint();QCOMPARE(sheet.pixelColor(origin+QPoint(160,120)),QColor(Qt::blue));
    QTemporaryDir directory;const auto path=directory.filePath("textured-reference.toon");QVERIFY2(saveProject(project,path,&error),qPrintable(error));const auto loaded=loadProject(path,&error);QVERIFY2(loaded,qPrintable(error));
    QCOMPARE(serializeReferenceScene(*loaded->shots[0].layers[1].reference3d),source);QCOMPARE(loaded->shots[0].layers[1].opacity,.6);QCOMPARE(renderMovieFrame(*loaded),frame);QCOMPARE(renderSheet(*loaded,loaded->shots[0],false),sheet);
    auto hidden=*loaded;hidden.shots[0].layers[1].visible=false;const auto hiddenFrame=renderMovieFrame(hidden);QCOMPARE(hiddenFrame.pixelColor(160,120),QColor(Qt::blue));QVERIFY(hiddenFrame!=frame);
 }
 void legacySceneReferenceGatingRemainsIndependentOfNativeExports(){
    auto project=Project::create(ProjectKind::DrawingOnly);project.width=256;project.height=256;project.shots[0].layers.append(reference(quad()));
    auto legacy=RasterLayer::create("Legacy scene view");legacy.kind=LayerKind::Scene3D;legacy.scene3d=Scene3D{};project.shots[0].layers.append(legacy);
    QImage green(256,256,QImage::Format_ARGB32_Premultiplied);green.fill(Qt::green);cacheScene3DPreview(scene3DCacheKey(*legacy.scene3d,{256,256}),green);
    const auto exported=renderMovieFrame(project);QVERIFY(!exported.isNull());QCOMPARE(exported.pixelColor(128,128),QColor(Qt::red));
    const auto sheet=renderSheet(project,project.shots[0],false);QVERIFY(!sheet.isNull());const auto origin=sheetGeometry(project,project.shots[0]).output.topLeft().toPoint();QCOMPARE(sheet.pixelColor(origin+QPoint(128,128)),QColor(Qt::red));
 }
 void geometryCacheIsReusedAcrossPlacementAndCameraChanges(){
    auto scene=quad();ReferenceRenderCache cache;auto first=renderReferenceScene(scene,{256,256},{},cache);QVERIFY(first.error.isEmpty());QCOMPARE(cache.evaluations,quint64(1));
    scene.instances[0].translation.setX(.2f);auto moved=renderReferenceScene(scene,{256,256},{},cache);QVERIFY(moved.error.isEmpty());QCOMPARE(cache.evaluations,quint64(1));QVERIFY(cache.hits>0);QVERIFY(first.image!=moved.image);
 }
};
QTEST_MAIN(ReferenceLayerRenderTests)
#include "test_reference3d_layer_render.moc"
