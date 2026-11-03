#include "animation.h"
#include "animation_render.h"
#include "animation_test_fixture.h"
#include "camera_animation.h"
#include "keyframe_render.h"
#include "scene3d_cache.h"
#include "raster_tools.h"
#include "sheet.h"
#include "video_export_settings.h"
#include <QColorSpace>
#include <QDir>
#include <QTemporaryDir>
#include <QtTest>
using namespace toon;
namespace {
Project fixture() {
    auto p=Project::create();p.width=128;p.height=128;p.transparent=false;
    auto &shot=p.shots.first();shot.durationFrames=11;shot.sheet.overscan=0;
    shot.layers={RasterLayer::create("Ink")};return p;
}
void rectangle(RasterLayer &layer,QRect area,QColor color) {
    QImage tile(tileSize,tileSize,QImage::Format_ARGB32_Premultiplied);tile.fill(Qt::transparent);
    {QPainter painter(&tile);painter.fillRect(area,color);}layer.tiles.insert({0,0},tile);
}
EditableObject vectorRectangle(QRectF area) {
    auto o=EditableObject::create();o.closed=true;o.stroke=QColor();o.strokeWidth=0;o.fill=Qt::red;
    o.nodes={BezierNode::corner(area.topLeft()),BezierNode::corner(area.topRight()),
             BezierNode::corner(area.bottomRight()),BezierNode::corner(area.bottomLeft())};return o;
}
QImage workspaceSheet(const Project &p,int frame,bool camera,bool references=false) {
    const auto &shot=p.shots.first();QImage image(sheetGeometry(p,shot).paper.size().toSize(),QImage::Format_ARGB32_Premultiplied);
    image.setColorSpace(QColorSpace::SRgb);
    image.fill(Qt::transparent);QPainter painter(&image);
    paintSheet(painter,p,shot,false,nullptr,true,references,frame,camera);return image;
}
QImage artworkCrop(const Project &p,const QImage &sheet) {
    return sheet.copy(sheetGeometry(p,p.shots.first()).output.toRect());
}
}
class CameraRenderTests final:public QObject {
    Q_OBJECT
private slots:
    void init(){clearKeyframeRenderCache();clearObjectLayerCaches();}
    void panZoomRotationAndSheetUseOneTransform() {
        auto p=fixture();auto &shot=p.shots.first();rectangle(shot.layers[0],{90,60,8,8},Qt::red);
        const auto source=renderMovieFrame(p,0);QCOMPARE(source.pixelColor(94,64),QColor(Qt::red));
        shot.camera2D={{30,0},2,90};const auto snapshot=p.toJson();
        const auto movie=renderMovieFrame(p,0);QCOMPARE(movie.pixelColor(64,64),QColor(Qt::red));
        QCOMPARE(movie.pixelColor(94,64),QColor(Qt::white));
        QCOMPARE(artworkCrop(p,workspaceSheet(p,0,true)),movie);
        QCOMPARE(artworkCrop(p,renderSheet(p,shot,false,0)),movie);
        QCOMPARE(artworkCrop(p,workspaceSheet(p,0,false)),source);
        QCOMPARE(p.toJson(),snapshot);
        const auto header=sheetGeometry(p,shot).header.toRect();
        QCOMPARE(workspaceSheet(p,0,true).copy(header),workspaceSheet(p,0,false).copy(header));
    }
    void cameraMotionChangesFramesAndVideoUsesTheSamePixels() {
        auto p=fixture();auto &shot=p.shots.first();rectangle(shot.layers[0],{90,60,8,8},Qt::red);
        shot.camera2DKeys={{0,{},KeyframeInterpolation::Linear},{10,{{30,0},1,0},KeyframeInterpolation::Linear}};
        QVERIFY(animationDisplayKey(shot,0)!=animationDisplayKey(shot,5));
        for(int frame:{0,5,10,0,10}) {
            const auto image=renderMovieFrame(p,frame);QVERIFY(!image.isNull());
            QCOMPARE(image.pixelColor(94-3*frame,64),QColor(Qt::red));
            auto settings=videoExportDefaults(p);settings.firstFrame=frame;settings.lastFrame=frame;settings.size={128,128};
            QString error;const auto video=renderVideoExportFrame(p,settings,frame,&error);
            QVERIFY2(!video.isNull(),qPrintable(error));QCOMPARE(video.pixelColor(94-3*frame,64),QColor(Qt::red));
        }
        QTemporaryDir directory;QString error;
        QVERIFY2(exportPngSequence(p,0,0,10,directory.filePath("frames"),{},&error),qPrintable(error));
        const auto last=renderMovieFrame(p,10);
        QCOMPARE(QImage(directory.filePath("frames/frame-0011.png")).convertToFormat(last.format()),last);
    }
    void notesFollowCameraButStayOutOfMovies() {
        auto p=fixture();auto &shot=p.shots.first();shot.camera2D={{30,0},1,0};
        auto notes=RasterLayer::create("Notes");notes.annotation=true;rectangle(notes,{90,60,8,8},Qt::blue);shot.layers.append(notes);
        QCOMPARE(artworkCrop(p,workspaceSheet(p,0,true)).pixelColor(64,64),QColor(Qt::blue));
        QCOMPARE(artworkCrop(p,workspaceSheet(p,0,false)).pixelColor(94,64),QColor(Qt::blue));
        QCOMPARE(renderMovieFrame(p,0).pixelColor(64,64),QColor(Qt::white));
    }
    void cameraRevealsUncachedStaticVectorGeometry() {
        auto p=fixture();auto &shot=p.shots.first();auto &layer=shot.layers[0];
        layer.kind=LayerKind::Vector;layer.objects={vectorRectangle({200,40,20,20})};
        QString error;QVERIFY(rebuildObjectLayer(layer,{0,0,128,128},maxProjectTiles,&error));QVERIFY(layer.tiles.isEmpty());
        shot.camera2D={{160,0},1,0};const auto original=p.toJson();
        const auto image=renderMovieFrame(p,0);QVERIFY2(!image.isNull(),qPrintable(keyframeRenderError()));
        QCOMPARE(image.pixelColor(50,50),QColor(Qt::red));QVERIFY(layer.tiles.isEmpty());QCOMPARE(p.toJson(),original);
    }
    void eyedropperSamplesRevealedVectorWithoutExpandingPaintBounds() {
        auto p=fixture();auto &shot=p.shots.first();auto &layer=shot.layers[0];layer.kind=LayerKind::Vector;
        layer.objects={vectorRectangle({200,40,20,20})};QString error;
        QVERIFY(rebuildObjectLayer(layer,{0,0,128,128},maxProjectTiles,&error));QVERIFY(layer.tiles.isEmpty());
        shot.camera2D={{160,0},1,0};const auto layerId=layer.id;
        Document document;document.load(p);const auto original=document.project().toJson();
        QVERIFY(!document.drawingBounds().contains({210,50}));
        for(bool sourceMode:{false,true}) {
            document.setKeyframeSourceEditing(sourceMode);
            QCOMPARE(RasterTools::sample(&document,{210,50}),QColor(Qt::red));
            QCOMPARE(RasterTools::sample(&document,{210,50},RasterSampleSource::ActiveLayer),QColor(Qt::red));
            QCOMPARE(RasterTools::sample(&document,{210,50},RasterSampleSource::ReferenceLayer,layerId),QColor(Qt::red));
            QVERIFY(!RasterTools::sample(&document,{-100,0}).isValid());
            QVERIFY(!RasterTools::sample(&document,{1000001,50}).isValid());
        }
        QCOMPARE(document.project().toJson(),original);QCOMPARE(document.history()->count(),0);
        auto raster=fixture();raster.shots[0].camera2D=shot.camera2D;document.load(raster);
        const auto rasterBefore=document.project().toJson();
        QVERIFY(!RasterTools::bucket(&document,{210,50},Qt::blue));
        BrushSettings brush;brush.size=8;brush.pressureSize=false;brush.sizeDynamics.control=BrushControl::Off;
        QVERIFY(document.beginStroke({210,50},1,brush,false));document.endStroke();
        QCOMPARE(document.project().toJson(),rasterBefore);QCOMPARE(document.history()->count(),0);
    }
    void cameraCropCombinesWithLayerTransformAndPathMorph() {
        auto p=fixture();auto &shot=p.shots.first();auto &layer=shot.layers[0];layer.kind=LayerKind::Vector;
        layer.objects={vectorRectangle({40,40,20,20})};QString error;
        QVERIFY(rebuildObjectLayer(layer,{0,0,128,128},maxProjectTiles,&error));
        auto end=layer.objects.first();for(auto &node:end.nodes){node.anchor.rx()+=160;node.inControl.rx()+=160;node.outControl.rx()+=160;}
        LayerAnimation animation;animation.layerId=layer.id;
        animation.paths={{end.id,{{0,pathPose(layer.objects.first()),KeyframeInterpolation::Linear},{10,pathPose(end),KeyframeInterpolation::Linear}}}};
        animation.channels={{KeyframeProperty::PositionX,{{0,0,KeyframeInterpolation::Linear},{10,50,KeyframeInterpolation::Linear}}}};
        shot.layerAnimations={animation};shot.camera2DKeys={{0,{},KeyframeInterpolation::Linear},{10,{{210,0},1,0},KeyframeInterpolation::Linear}};
        const auto first=renderMovieFrame(p,0);QCOMPARE(first.pixelColor(50,50),QColor(Qt::red));
        for(int frame:{5,10,3,0,10})QCOMPARE(renderMovieFrame(p,frame),first);
    }
    void onionUsesCurrentCameraAndNeighborDrawingPose() {
        auto p=animation_fixture::project();auto &shot=p.shots.first();const auto &track=shot.animationTracks.first();
        const auto firstId=track.drawings.first().templateLayers.value("ink");
        const int index=animation_fixture::layerIndex(shot,firstId);auto &layer=shot.layers[index];
        layer.kind=LayerKind::Vector;layer.objects={vectorRectangle({200,40,20,20})};QString error;
        QVERIFY(rebuildObjectLayer(layer,{0,0,128,128},maxProjectTiles,&error));QVERIFY(layer.tiles.isEmpty());
        shot.camera2DKeys={{0,{},KeyframeInterpolation::Hold},{4,{{160,0},1,0},KeyframeInterpolation::Linear}};
        QCOMPARE(evaluateShotCamera(shot,2),Camera2D{});
        const auto original=p.toJson();
        const auto ghosts=onionSkinLayers(shot,4,track.id,-1,{160,0,128,128});QVERIFY(!ghosts.isEmpty());
        QImage image(128,128,QImage::Format_ARGB32_Premultiplied);image.fill(Qt::transparent);
        {QPainter painter(&image);painter.setTransform(shotCameraTransform(shot,4,{128,128}));paintLayers(painter,ghosts,false);}
        QCOMPARE(image.pixelColor(50,50),QColor(Qt::red));QVERIFY(layer.tiles.isEmpty());QCOMPARE(p.toJson(),original);
        // An unrelated world crop should not allocate the off-frame vector.
        const auto clipped=onionSkinLayers(shot,4,track.id,-1,{0,0,128,128});
        for(const auto &ghost:clipped)if(ghost.id==firstId)QVERIFY(ghost.tiles.isEmpty());
    }
    void sceneCameraUsesSameClockAndReferencePolicy() {
        auto p=fixture();auto &shot=p.shots.first();auto reference=RasterLayer::create("3D city");
        reference.kind=LayerKind::Scene3D;reference.scene3d=Scene3D{};reference.scene3d->camera.yaw=17;
        const auto first=sceneCameraPose(reference.scene3d->camera);auto last=first;last.yaw=117;last.target={2,3,4};
        shot.layers.prepend(reference);shot.sceneCameraAnimations={{reference.id,{{0,first,KeyframeInterpolation::Linear},{10,last,KeyframeInterpolation::Linear}}}};
        shot.camera2D={{30,0},1,0};const auto original=p.toJson();
        QVERIFY(animationDisplayKey(shot,0)!=animationDisplayKey(shot,5));
        for(int frame:{0,5,10}) {
            const auto layers=evaluateLayers(shot,frame);QVERIFY(layers[0].scene3d);
            QCOMPARE(layers[0].scene3d->camera,evaluateSceneCamera(shot,reference.id,frame));
            QCOMPARE(evaluateLayers(shot,frame,true)[0].scene3d->camera,layers[0].scene3d->camera);
            QImage referenceImage(128,128,QImage::Format_ARGB32_Premultiplied);referenceImage.fill(Qt::transparent);
            {QPainter painter(&referenceImage);painter.fillRect(QRect(90,60,8,8),frame==0?Qt::red:frame==5?Qt::green:Qt::blue);}
            cacheScene3DPreview(scene3DCacheKey(*layers[0].scene3d,{128,128}),referenceImage);
            const auto displayed=artworkCrop(p,workspaceSheet(p,frame,true,true));
            QCOMPARE(displayed.pixelColor(64,64),QColor(frame==0?Qt::red:frame==5?Qt::green:Qt::blue));
            QCOMPARE(renderMovieFrame(p,frame).pixelColor(64,64),QColor(Qt::white));
            QCOMPARE(artworkCrop(p,renderSheet(p,shot,false,frame)).pixelColor(64,64),QColor(Qt::white));
        }
        QCOMPARE(p.toJson(),original);QCOMPARE(shot.layers[0].scene3d->camera.yaw,17.f);
    }
};
QTEST_MAIN(CameraRenderTests)
#include "camera_render_tests.moc"
