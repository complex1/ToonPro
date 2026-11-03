#include "document.h"
#include "keyframe_render.h"
#include "sheet.h"
#include <QtTest>
using namespace toon;
namespace {
EditableObject rectangle(QRectF area) {
    auto object=EditableObject::create();object.closed=true;object.stroke=QColor();object.strokeWidth=0;object.fill=Qt::red;
    object.nodes={BezierNode::corner(area.topLeft()),BezierNode::corner(area.topRight()),
                  BezierNode::corner(area.bottomRight()),BezierNode::corner(area.bottomLeft())};
    return object;
}
Project fixture(bool parent=false,QRectF area={40,40,20,20}) {
    auto project=Project::create();project.width=128;project.height=128;project.transparent=true;
    auto &shot=project.shots.first();shot.durationFrames=11;shot.sheet.overscan=0;
    auto layer=RasterLayer::create("Animated drawing");layer.kind=LayerKind::Vector;layer.objects={rectangle(area)};
    QString error;
    if(!rebuildObjectLayer(layer,{0,0,128,128},maxProjectTiles,&error))qFatal("Fixture failed: %s",qPrintable(error));
    if(parent){auto group=RasterLayer::create("Animated folder");group.group=true;layer.parentId=group.id;shot.layers={group,layer};}
    else shot.layers={layer};
    return project;
}
PropertyChannel channel(KeyframeProperty property,qreal first,qreal last) {
    return {property,{{0,first,KeyframeInterpolation::Linear},{10,last,KeyframeInterpolation::Linear}}};
}
bool transparent(const QImage &image) {
    if(image.isNull())return false;
    for(int y=0;y<image.height();++y)for(int x=0;x<image.width();++x)if(image.pixelColor(x,y).alpha())return false;
    return true;
}
}
class KeyframeRenderBoundsTests:public QObject {
    Q_OBJECT
private slots:
    void init(){clearKeyframeRenderCache();clearObjectLayerCaches();}
    void outsidePoseReturnsThroughLayerOrFolderTransform_data() {
        QTest::addColumn<bool>("parent");
        QTest::newRow("layer")<<false;QTest::newRow("folder")<<true;
    }
    void outsidePoseReturnsThroughLayerOrFolderTransform() {
        QFETCH(bool,parent);auto project=fixture(parent);auto &shot=project.shots.first();const auto &layer=shot.layers.last();
        auto end=layer.objects.first();for(auto &node:end.nodes){node.anchor.rx()+=160;node.inControl.rx()+=160;node.outControl.rx()+=160;}
        LayerAnimation morph;morph.layerId=layer.id;morph.paths={{end.id,{{0,pathPose(layer.objects.first()),KeyframeInterpolation::Linear},{10,pathPose(end),KeyframeInterpolation::Linear}}}};
        if(parent){LayerAnimation outer;outer.layerId=layer.parentId;outer.channels={channel(KeyframeProperty::PositionX,0,-160)};shot.layerAnimations={outer,morph};}
        else {morph.channels={channel(KeyframeProperty::PositionX,0,-160)};shot.layerAnimations={morph};}
        QVERIFY2(project.validate().isEmpty(),qPrintable(project.validate()));
        const auto source=shot.layers.last().objects;const auto tiles=shot.layers.last().tiles;
        const auto first=renderMovieFrame(project,0);QVERIFY(!first.isNull());QCOMPARE(first.pixelColor(50,50),QColor(Qt::red));
        for(int frame:{5,10,3,10,0}) {
            const auto rendered=renderMovieFrame(project,frame);QVERIFY2(!rendered.isNull(),qPrintable(keyframeRenderError()));
            QCOMPARE(rendered,first);
        }
        QCOMPARE(shot.layers.last().objects,source);QCOMPARE(shot.layers.last().tiles,tiles);
    }
    void rotatedFolderUsesInverseWorldCrop() {
        auto project=fixture(true);auto &shot=project.shots.first();const auto &layer=shot.layers.last();
        auto end=layer.objects.first();for(auto &node:end.nodes){node.anchor.rx()+=160;node.inControl.rx()+=160;node.outControl.rx()+=160;}
        LayerAnimation morph;morph.layerId=layer.id;morph.paths={{end.id,{{0,pathPose(layer.objects.first()),KeyframeInterpolation::Linear},{10,pathPose(end),KeyframeInterpolation::Linear}}}};
        LayerAnimation outer;outer.layerId=layer.parentId;
        outer.channels={channel(KeyframeProperty::PositionX,0,256),channel(KeyframeProperty::PositionY,0,128),channel(KeyframeProperty::Rotation,0,180)};
        shot.layerAnimations={outer,morph};const auto rendered=renderMovieFrame(project,10);
        QVERIFY2(!rendered.isNull(),qPrintable(keyframeRenderError()));QCOMPARE(rendered.pixelColor(46,78),QColor(Qt::red));
        QCOMPARE(rendered.pixelColor(110,110).alpha(),0);
    }
    void unchangedSourceOutsideSheetCanEnterThroughParentAnimation() {
        auto project=fixture(true,{200,40,20,20});auto &shot=project.shots.first();QVERIFY(shot.layers.last().tiles.isEmpty());
        LayerAnimation outer;outer.layerId=shot.layers.first().id;outer.channels={channel(KeyframeProperty::PositionX,-160,0)};shot.layerAnimations={outer};
        const auto inside=renderMovieFrame(project,0);QVERIFY(!inside.isNull());QCOMPARE(inside.pixelColor(50,50),QColor(Qt::red));
        QVERIFY(transparent(renderMovieFrame(project,10)));QCOMPARE(renderMovieFrame(project,0),inside);
        QVERIFY(shot.layers.last().tiles.isEmpty());
    }
    void zeroScaleSkipsRebuildAndRecoversWithoutStaleErrors() {
        auto project=fixture(true);auto &shot=project.shots.first();
        LayerAnimation outer;outer.layerId=shot.layers.first().id;outer.channels={channel(KeyframeProperty::ScaleX,0,1)};shot.layerAnimations={outer};
        QString error="stale error";auto layers=shot.layers;
        QVERIFY(applyLayerAnimations(shot,0,layers,{0,0,128,128},&error));QVERIFY(error.isEmpty());QVERIFY(keyframeRenderError().isEmpty());
        QVERIFY(layers.last().tiles.isEmpty());QVERIFY(transparent(renderMovieFrame(project,0)));
        const auto visible=renderMovieFrame(project,10);QCOMPARE(visible.pixelColor(50,50),QColor(Qt::red));
    }
    void oversizedInverseCropFailsWithinTileBudgetAndReportsLayer() {
        auto project=fixture(false,{0,0,30000,30000});auto &shot=project.shots.first();
        LayerAnimation outer;outer.layerId=shot.layers.first().id;
        outer.channels={channel(KeyframeProperty::ScaleX,.001,.001),channel(KeyframeProperty::ScaleY,.001,.001)};shot.layerAnimations={outer};
        QString error;const auto layers=evaluateLayers(shot,0,false,{0,0,128,128},&error);
        QVERIFY(layers.isEmpty());QVERIFY(error.contains("Animated drawing"));QVERIFY(error.contains("tile memory"));QCOMPARE(error,keyframeRenderError());
        QVERIFY(renderMovieFrame(project,0).isNull());
        shot.layerAnimations[0].channels[0].keys[0].value=0;
        auto collapsed=shot.layers;QVERIFY(applyLayerAnimations(shot,0,collapsed,{0,0,128,128},&error));QVERIFY(error.isEmpty());QVERIFY(keyframeRenderError().isEmpty());
    }
};
QTEST_MAIN(KeyframeRenderBoundsTests)
#include "keyframe_render_bounds_tests.moc"
