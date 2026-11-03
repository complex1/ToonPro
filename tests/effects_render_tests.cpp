#include "animation.h"
#include "animation_render.h"
#include "effect_render.h"
#include "keyframe_render.h"
#include "raster_tools.h"
#include "sheet.h"
#include "video_export_settings.h"
#include "scene3d_cache.h"
#include <QColorSpace>
#include <QTemporaryDir>
#include <QtTest>
#include <cmath>
#include <array>
#include <cstring>
using namespace toon;
namespace {
void rect(RasterLayer &layer,QRect bounds,QColor color) {
    const int left=int(std::floor(qreal(bounds.left())/tileSize)),right=int(std::floor(qreal(bounds.right())/tileSize));
    const int top=int(std::floor(qreal(bounds.top())/tileSize)),bottom=int(std::floor(qreal(bounds.bottom())/tileSize));
    for(int ty=top;ty<=bottom;++ty)for(int tx=left;tx<=right;++tx) {
        auto tile=layer.tiles.value({tx,ty});if(tile.isNull()){tile=QImage(tileSize,tileSize,QImage::Format_ARGB32_Premultiplied);tile.fill(Qt::transparent);}
        {QPainter p(&tile);p.translate(-tx*tileSize,-ty*tileSize);p.fillRect(bounds,color);}layer.tiles.insert({tx,ty},tile);
    }
}
QImage render(const QVector<RasterLayer> &layers,QRect bounds={0,0,512,256},const RasterLayerPreview *preview=nullptr,bool smooth=false) {
    QImage image(bounds.size(),QImage::Format_ARGB32_Premultiplied);image.fill(Qt::transparent);
    QPainter p(&image);p.translate(-bounds.topLeft());paintLayers(p,layers,true,preview,smooth);return image;
}
RasterLayer ink(QRect bounds={20,20,40,40},QColor color=Qt::red) {auto layer=RasterLayer::create("Ink");rect(layer,bounds,color);return layer;}
Project fixture() {auto p=Project::create();p.width=p.height=128;p.transparent=true;p.shots[0].durationFrames=11;p.shots[0].sheet.overscan=0;p.shots[0].layers={ink()};return p;}
EditableObject vectorRect(QRectF area) {
    auto o=EditableObject::create();o.closed=true;o.stroke=QColor();o.strokeWidth=0;o.fill=Qt::red;
    o.nodes={BezierNode::corner(area.topLeft()),BezierNode::corner(area.topRight()),BezierNode::corner(area.bottomRight()),BezierNode::corner(area.bottomLeft())};return o;
}
LayerMask halfMask() {
    LayerMask mask;QImage tile(tileSize,tileSize,QImage::Format_Grayscale8);tile.fill(255);
    for(int y=0;y<tileSize;++y)memset(tile.scanLine(y),0,40);mask.tiles.insert({0,0},tile);return mask;
}
qreal referenceExposure(int channel,qreal stops) {
    const qreal s=channel/255.;const qreal linear=s<=.04045?s/12.92:std::pow((s+.055)/1.055,2.4);
    const qreal v=qBound(0.,linear*std::exp2(stops),1.);return v<=.0031308?12.92*v:1.055*std::pow(v,1./2.4)-.055;
}
QImage referenceColorCorrection(QImage image,const LayerEffects &fx) {
    if(fx.exposure==0&&fx.contrast==0&&fx.saturation==0&&fx.hue==0)return image;
    std::array<qreal,256> exposure;for(int i=0;i<256;++i)exposure[i]=referenceExposure(i,fx.exposure);
    const qreal contrast=fx.contrast>=0?1+fx.contrast/10.:1+fx.contrast/100.,saturation=1+fx.saturation/100.;
    for(int y=0;y<image.height();++y){auto *row=reinterpret_cast<QRgb *>(image.scanLine(y));for(int x=0;x<image.width();++x){
        if(!qAlpha(row[x]))continue;const auto p=qUnpremultiply(row[x]);
        qreal r=qBound(0.,(exposure[qRed(p)]-.5)*contrast+.5,1.),g=qBound(0.,(exposure[qGreen(p)]-.5)*contrast+.5,1.),b=qBound(0.,(exposure[qBlue(p)]-.5)*contrast+.5,1.);
        const qreal gray=.2126*r+.7152*g+.0722*b;r=qBound(0.,gray+(r-gray)*saturation,1.);g=qBound(0.,gray+(g-gray)*saturation,1.);b=qBound(0.,gray+(b-gray)*saturation,1.);
        QColor color=QColor::fromRgbF(r,g,b);if(fx.hue!=0){float h,s,v;color.getHsvF(&h,&s,&v);if(h>=0){h=std::fmod(h+fx.hue/360.+1.,1.);color=QColor::fromHsvF(h,s,v);}}
        row[x]=qPremultiply(qRgba(color.red(),color.green(),color.blue(),qAlpha(p)));
    }}return image;
}
qreal colorQuantizationBoundary(int target) {
    float low=0,high=1;
    while(std::nextafter(low,1.F)<high) {
        const float middle=float((qreal(low)+high)/2);
        if(QColor::fromRgbF(middle,middle,middle).red()>=target)high=middle;else low=middle;
    }
    return (qreal(low)+high)/2;
}
// Deliberately simple full-image reference, independent of tiled filtering and
// the optimized row/alpha paths. Preserve the historical per-pass byte rounding.
QImage referenceBlur(QImage image,int radius) {
    for(int pass=0;pass<3;++pass)for(bool horizontal:{true,false}) {
        const int r=radius/3+(pass<radius%3?1:0);if(!r)continue;
        QImage output(image.size(),image.format());const int divisor=2*r+1;
        const int outer=horizontal?image.height():image.width(),inner=horizontal?image.width():image.height();
        for(int line=0;line<outer;++line) {
            int red=0,green=0,blue=0,alpha=0;
            const auto pixel=[&](int at){return at<0||at>=inner?QRgb(0):image.pixel(horizontal?at:line,horizontal?line:at);};
            const auto add=[&](QRgb p,int sign){red+=sign*qRed(p);green+=sign*qGreen(p);blue+=sign*qBlue(p);alpha+=sign*qAlpha(p);};
            for(int at=-r;at<=r;++at)add(pixel(at),1);
            for(int at=0;at<inner;++at){output.setPixel(horizontal?at:line,horizontal?line:at,qRgba((red+divisor/2)/divisor,(green+divisor/2)/divisor,(blue+divisor/2)/divisor,(alpha+divisor/2)/divisor));add(pixel(at-r),-1);add(pixel(at+r+1),1);}
        }
        image=std::move(output);
    }
    return image;
}
QImage referenceTint(const QImage &source,QPointF offset,QColor color,qreal opacity) {
    QImage result(source.size(),source.format());
    const int dx=int(std::floor(-offset.x())),dy=int(std::floor(-offset.y()));const qreal fx=-offset.x()-dx,fy=-offset.y()-dy;
    const auto alpha=[&](int x,int y){return x<0||x>=source.width()||y<0||y>=source.height()?0:qAlpha(source.pixel(x,y));};
    for(int y=0;y<result.height();++y)for(int x=0;x<result.width();++x){
        const qreal coverage=alpha(x+dx,y+dy)*(1-fx)*(1-fy)+alpha(x+dx+1,y+dy)*fx*(1-fy)+alpha(x+dx,y+dy+1)*(1-fx)*fy+alpha(x+dx+1,y+dy+1)*fx*fy;
        result.setPixel(x,y,qPremultiply(qRgba(color.red(),color.green(),color.blue(),qRound(coverage*opacity))));
    }
    return result;
}

}
class EffectsRenderTests:public QObject {
    Q_OBJECT
private slots:
    void init(){clearKeyframeRenderCache();clearObjectLayerCaches();clearEffectRenderError();}
    void cancellationScopeDoesNotPublishCachedOrPartialTiles() {
        auto layer=ink({20,20,80,80});layer.effects.blurEnabled=true;layer.effects.blurRadius=16;
        const QVector<RasterLayer> layers{layer};const auto before=render(layers);const auto pixels=layer.tiles;
        std::atomic_bool stop{true},resume{false};
        {
            EffectRenderCancellationScope scope(&stop);QVERIFY(effectRenderCancelled());
            EffectCompositor compositor(layers,true);compositor.visibleTiles({0,0,512,256});
            QVERIFY(compositor.tile({0,0}).isNull());QVERIFY(effectRenderError().isEmpty());
            {
                EffectRenderCancellationScope nested(&resume);QVERIFY(!effectRenderCancelled());
                QCOMPARE(render(layers),before);
            }
            QVERIFY(effectRenderCancelled());QVERIFY(compositor.tile({0,0}).isNull());
        }
        QVERIFY(!effectRenderCancelled());QCOMPARE(render(layers),before);QCOMPARE(layer.tiles,pixels);
        QVERIFY(effectRenderError().isEmpty());
    }
    void preparedSceneImagesAreScopedAndCannotLeakBetweenJobs() {
        const Scene3D scene;const QSize size(24,24);const QString id="prepared-scene";
        QImage original(size,QImage::Format_ARGB32_Premultiplied);original.fill(Qt::red);
        auto prepared=original;prepared.fill(Qt::blue);
        cacheScene3DPreview(scene3DCacheKey(scene,size),original);
        QCOMPARE(scene3DLayerPreview(id,scene,size),original);
        const QHash<QString,QImage> first{{id,prepared}},empty;
        {
            Scene3DLayerPreviewScope scope(first);QCOMPARE(scene3DLayerPreview(id,scene,size),prepared);
            QVERIFY(scene3DLayerPreview("missing",scene,size).isNull());
            {
                Scene3DLayerPreviewScope nested(empty);QVERIFY(scene3DLayerPreview(id,scene,size).isNull());
            }
            QCOMPARE(scene3DLayerPreview(id,scene,size),prepared);
        }
        QCOMPARE(scene3DLayerPreview(id,scene,size),original);
    }
    void masksAreGrayscaleAndSourcePreserving() {
        auto layer=ink({20,20,40,40},QColor(255,0,0,128));const auto original=layer.tiles;
        layer.mask=halfMask();const auto masked=render({layer});
        QCOMPARE(masked.pixelColor(30,30).alpha(),0);QCOMPARE(masked.pixelColor(50,30).alpha(),128);
        layer.mask->defaultValue=64;rect(layer,{-30,20,10,10},Qt::red);
        const auto negative=render({layer},{-40,0,128,128});QCOMPARE(negative.pixelColor(15,25).alpha(),64);
        layer.mask->enabled=false;QCOMPARE(render({layer}).pixelColor(30,30).alpha(),128);
        QCOMPARE(layer.tiles.value({0,0}),original.value({0,0}));QVERIFY(effectRenderError().isEmpty());
    }
    void noOpEffectsKeepExistingAnimatedSamplingExactly() {
        auto layer=ink({10,10,20,20},QColor(220,80,40,128));layer.renderTransform.translate(20.35,10.75);layer.renderTransform.rotate(17);
        const auto original=render({layer},{0,0,128,128},nullptr,true);
        layer.effects.blurEnabled=true;layer.effects.blurRadius=0;layer.effects.colorEnabled=true;
        layer.mask=LayerMask{};QVERIFY(!layerStackHasEffects({layer}));
        QCOMPARE(render({layer},{0,0,128,128},nullptr,true),original);
        layer.mask=halfMask();layer.mask->enabled=false;QVERIFY(!layerStackHasEffects({layer}));
        QCOMPARE(render({layer},{0,0,128,128},nullptr,true),original);
        layer.effects.shadowEnabled=true;layer.effects.enabled=false;QVERIFY(!layerStackHasEffects({layer}));
        QCOMPARE(render({layer},{0,0,128,128},nullptr,true),original);
    }
    void fractionalShadowOffsetsInterpolateCoverage() {
        auto layer=ink({20,20,1,1});layer.effects.shadowEnabled=true;layer.effects.shadowRadius=0;
        layer.effects.shadowOffset={20.5,.25};layer.effects.shadowOpacity=1;const auto output=render({layer});
        QCOMPARE(output.pixelColor(40,20),QColor(0,0,0,96));QCOMPARE(output.pixelColor(41,20),QColor(0,0,0,96));
        QCOMPARE(output.pixelColor(40,21),QColor(0,0,0,32));QCOMPARE(output.pixelColor(41,21),QColor(0,0,0,32));
        layer.effects.shadowOffset={20,0};const auto whole=render({layer});
        QCOMPARE(whole.pixelColor(40,20),QColor(Qt::black));QCOMPARE(whole.pixelColor(41,20).alpha(),0);
    }
    void blurAndLightHalosCrossSourceTilesWithoutSeams() {
        auto layer=ink({244,20,24,80});layer.effects.blurEnabled=true;layer.effects.blurRadius=12;
        layer.effects.glowEnabled=true;layer.effects.glowRadius=9;layer.effects.glowColor=Qt::blue;layer.effects.glowOpacity=.7;
        layer.effects.shadowEnabled=true;layer.effects.shadowRadius=5;layer.effects.shadowOffset={17,3};layer.effects.shadowOpacity=.8;
        const auto source=layer.tiles;const auto output=render({layer});QVERIFY(effectRenderError().isEmpty());
        auto shifted=ink({116,20,24,80});shifted.effects=layer.effects;
        const auto reference=render({shifted});QCOMPARE(output.copy(160,0,240,150),reference.copy(32,0,240,150));
        QVERIFY(output.pixelColor(238,50).alpha()>0);QVERIFY(output.pixelColor(279,50).alpha()>0);
        QCOMPARE(layer.tiles,source);
        const auto bounds=compositedLayerBounds({layer});QVERIFY(bounds.left()<0);QVERIFY(bounds.right()>512);
    }
    void maskPrecedesBlurAndShadow() {
        auto layer=ink();layer.mask=halfMask();layer.effects.blurEnabled=true;layer.effects.blurRadius=6;
        layer.effects.shadowEnabled=true;layer.effects.shadowRadius=0;layer.effects.shadowOffset={80,0};layer.effects.shadowOpacity=1;
        const auto output=render({layer});
        QCOMPARE(output.pixelColor(25,30).alpha(),0);QVERIFY(output.pixelColor(38,30).alpha()>0);
        QCOMPARE(output.pixelColor(105,30).alpha(),0);QVERIFY(output.pixelColor(135,30).alpha()>0);
    }
    void everyColorControlChangesRgbAndPreservesCoverage() {
        auto base=ink({0,0,64,64},QColor(160,80,40,120));const auto original=render({base});
        QVector<LayerEffects> settings;
        for(int control=0;control<4;++control){LayerEffects fx;fx.colorEnabled=true;
            if(control==0)fx.exposure=1;if(control==1)fx.contrast=35;if(control==2)fx.saturation=-100;if(control==3)fx.hue=100;settings.append(fx);}
        for(const auto &fx:settings){auto layer=base;layer.effects=fx;const auto changed=render({layer});
            QVERIFY(changed.pixel(20,20)!=original.pixel(20,20));QCOMPARE(changed.pixelColor(20,20).alpha(),120);
            layer.effects.enabled=false;QCOMPARE(render({layer}),original);}
        auto noChange=base;noChange.effects.colorEnabled=true;QCOMPARE(render({noChange}),original);
    }
    void adjustmentUsesMaskOpacityAndPreservesBackdropAlpha() {
        auto base=ink({0,0,100,100},QColor(80,50,30,128));auto adjustment=RasterLayer::create("Color");
        adjustment.adjustment=true;adjustment.effects.colorEnabled=true;adjustment.effects.exposure=2;
        const auto before=render({base}),full=render({base,adjustment});
        adjustment.opacity=.5;adjustment.mask=halfMask();const auto partial=render({base,adjustment});
        QCOMPARE(partial.pixelColor(20,20),before.pixelColor(20,20));QCOMPARE(partial.pixelColor(60,20).alpha(),128);
        const int r=partial.pixelColor(60,20).red();QVERIFY(r>before.pixelColor(60,20).red());QVERIFY(r<full.pixelColor(60,20).red());
        QCOMPARE(partial.pixelColor(130,20).alpha(),0);
        adjustment.blendMode=LayerBlendMode::Multiply;QCOMPARE(render({base,adjustment}).pixelColor(60,20).alpha(),128);
        auto above=ink({0,0,10,10},Qt::blue);QCOMPARE(render({base,adjustment,above}).pixelColor(5,5),QColor(Qt::blue));
    }
    void animatedAdjustmentMaskUsesFilteredSourceCoordinates() {
        auto base=ink({0,0,100,100},QColor(60,60,60,128));auto adjustment=RasterLayer::create("Masked color");
        adjustment.adjustment=true;adjustment.effects.colorEnabled=true;adjustment.effects.exposure=2;
        const auto full=render({base,adjustment},{0,0,128,128},nullptr,true),original=render({base},{0,0,128,128});
        adjustment.mask=halfMask();adjustment.renderTransform.translate(.5,0);
        const auto output=render({base,adjustment},{0,0,128,128},nullptr,true);
        QCOMPARE(output.pixelColor(39,20),original.pixelColor(39,20));QCOMPARE(output.pixelColor(41,20),full.pixelColor(41,20));
        QVERIFY(qAbs(output.pixelColor(40,20).red()-(original.pixelColor(40,20).red()+full.pixelColor(40,20).red())/2.)<=2);
        QCOMPARE(output.pixelColor(40,20).alpha(),128);
    }
    void folderAdjustmentIsIsolatedAndFolderEffectsApplyOnce() {
        auto underneath=ink({0,0,120,120},QColor(80,80,80));auto folder=RasterLayer::create("Folder");folder.group=true;folder.opacity=.5;
        auto child=ink({20,20,40,40},QColor(100,0,0));child.parentId=folder.id;
        auto adjustment=RasterLayer::create("Folder color");adjustment.parentId=folder.id;adjustment.adjustment=true;
        adjustment.effects.colorEnabled=true;adjustment.effects.saturation=-100;
        const auto output=render({underneath,folder,child,adjustment});
        QCOMPARE(output.pixelColor(10,10),QColor(80,80,80));const auto color=output.pixelColor(30,30);QCOMPARE(color.red(),color.green());
        folder.effects.shadowEnabled=true;folder.effects.shadowRadius=0;folder.effects.shadowOffset={60,0};folder.effects.shadowOpacity=1;
        const auto shadow=render({folder,child,adjustment});QVERIFY(qAbs(shadow.pixelColor(90,30).alpha()-128)<=1);
        QVERIFY(qAbs(shadow.pixelColor(30,30).alpha()-128)<=1);QCOMPARE(shadow.pixelColor(130,30).alpha(),0);
    }
    void clippingRetainsBaseAlphaAndLayerBlendStillWorks() {
        auto base=ink({20,20,40,40},QColor(255,255,255,128));base.opacity=.5;
        auto overlay=ink({0,0,100,100},Qt::red);overlay.clipped=true;overlay.blendMode=LayerBlendMode::Multiply;
        overlay.effects.glowEnabled=true;overlay.effects.glowColor=Qt::blue;overlay.effects.glowRadius=8;
        const auto result=render({base,overlay});QVERIFY(qAbs(result.pixelColor(30,30).alpha()-64)<=1);
        QCOMPARE(result.pixelColor(30,30).red(),255);QCOMPARE(result.pixelColor(10,30).alpha(),0);
        auto background=ink({0,0,100,100},QColor(128,128,128));base.opacity=1;base.blendMode=LayerBlendMode::Multiply;
        const auto blended=render({background,base,overlay});QVERIFY(blended.pixelColor(30,30).green()<blended.pixelColor(10,30).green());
    }
    void adjustmentCannotBecomeAClippingBase() {
        auto p=fixture();auto adjustment=RasterLayer::create("Adjustment");adjustment.adjustment=true;
        auto overlay=ink();p.shots[0].layers.append(adjustment);p.shots[0].layers.append(overlay);
        Document document;document.load(p);document.selectLayer(2);QSignalSpy warnings(&document,&Document::warning);
        document.setLayerClipped(true);QVERIFY(!document.project().shots[0].layers[2].clipped);QVERIFY(!warnings.isEmpty());QCOMPARE(document.history()->count(),0);
        p.shots[0].layers[2].clipped=true;QVERIFY(p.validate().contains("Adjustment"));
        QString error;QVERIFY(!Project::fromJson(p.toJson(),&error));QVERIFY(error.contains("Adjustment"));
        const auto output=render(p.shots[0].layers,{0,0,128,128});QVERIFY(effectRenderError().contains("Adjustment"));
        QCOMPARE(output.pixelColor(30,30).alpha(),0);
        QVERIFY(renderMovieFrame(p,0).isNull());QVERIFY(keyframeRenderError().contains("Adjustment"));
    }
    void masksAndEffectOffsetsFollowLayerAndFolderAnimation() {
        auto folder=RasterLayer::create("Folder");folder.group=true;folder.renderTransform.translate(40,20);folder.renderTransform.scale(2,2);
        auto layer=ink();layer.parentId=folder.id;layer.mask=halfMask();layer.effects.shadowEnabled=true;
        layer.effects.shadowRadius=0;layer.effects.shadowOffset={20,0};layer.effects.shadowOpacity=1;
        const auto result=render({folder,layer},{0,0,256,256});
        QCOMPARE(result.pixelColor(100,80).alpha(),0);QCOMPARE(result.pixelColor(140,80),QColor(Qt::red));
        QCOMPARE(result.pixelColor(180,80),QColor(Qt::black));
    }
    void destructivePreviewCombinesRawPartsBeforeEffects() {
        auto layer=ink({20,20,40,40});layer.effects.shadowEnabled=true;layer.effects.shadowRadius=0;
        layer.effects.shadowOffset={10,0};layer.effects.shadowOpacity=.5;layer.mask=halfMask();
        RasterLayerPreview preview;preview.layerId=layer.id;preview.stationary=layer;preview.stationary.tiles.clear();
        preview.moving=layer;preview.transform.translate(40,0);
        auto expected=layer;expected.tiles.clear();rect(expected,{60,20,40,40},Qt::red);
        QCOMPARE(render({layer},{0,0,128,128},&preview),render({expected},{0,0,128,128}));
        preview.transform={};preview.sourceUnchangedAtIdentity=true;QCOMPARE(render({layer},{0,0,128,128},&preview),render({layer},{0,0,128,128}));
    }
    void offscreenVectorSourceContributesThroughFolderHalo() {
        auto p=fixture();auto &shot=p.shots[0];auto folder=RasterLayer::create("Glow folder");folder.group=true;
        folder.effects.shadowEnabled=true;folder.effects.shadowRadius=0;folder.effects.shadowOffset={-24,0};folder.effects.shadowOpacity=1;
        auto layer=RasterLayer::create("Outside vector");layer.kind=LayerKind::Vector;layer.parentId=folder.id;layer.objects={vectorRect({140,30,10,20})};
        QString error;QVERIFY(rebuildObjectLayer(layer,{0,0,128,128},maxProjectTiles,&error));QVERIFY(layer.tiles.isEmpty());shot.layers={folder,layer};
        const auto source=p.toJson();const auto frame=renderMovieFrame(p,0);QVERIFY2(!frame.isNull(),qPrintable(keyframeRenderError()));
        QCOMPARE(frame.pixelColor(120,40),QColor(Qt::black));QCOMPARE(p.toJson(),source);QVERIFY(shot.layers.last().tiles.isEmpty());
    }
    void editableTextReceivesColorAndMaskWithoutBaking() {
        auto p=fixture();auto &layer=p.shots[0].layers[0];layer.kind=LayerKind::Text;layer.tiles.clear();
        auto text=EditableObject::create(ObjectKind::Text);text.text="Toon";text.position={15,45};text.fontSize=28;text.fill=Qt::red;text.stroke=QColor();
        layer.objects={text};QString error;QVERIFY2(rebuildObjectLayer(layer,{0,0,128,128},maxProjectTiles,&error),qPrintable(error));
        const auto sourceTiles=layer.tiles;const auto sourceObjects=layer.objects;const auto original=renderMovieFrame(p,0);
        layer.effects.colorEnabled=true;layer.effects.hue=120;const auto corrected=renderMovieFrame(p,0);QVERIFY(!corrected.isNull());
        bool sawInk=false;for(int y=0;y<128;++y)for(int x=0;x<128;++x)if(original.pixelColor(x,y).alpha()>100) {
            QVERIFY(corrected.pixelColor(x,y).green()>200);QCOMPARE(corrected.pixelColor(x,y).alpha(),original.pixelColor(x,y).alpha());sawInk=true;
        }
        QVERIFY(sawInk);layer.mask=LayerMask{};layer.mask->defaultValue=0;const auto hidden=renderMovieFrame(p,0);
        for(int y=0;y<128;++y)for(int x=0;x<128;++x)QCOMPARE(hidden.pixelColor(x,y).alpha(),0);
        QCOMPARE(layer.tiles,sourceTiles);QCOMPARE(layer.objects,sourceObjects);
    }
    void sourceModeFillSamplesOffsheetEffectHalo() {
        auto p=fixture();auto &shot=p.shots[0];auto layer=RasterLayer::create("Outside vector");
        layer.kind=LayerKind::Vector;layer.objects={vectorRect({140,30,10,20})};layer.effects.shadowEnabled=true;
        layer.effects.shadowRadius=0;layer.effects.shadowOffset={-24,0};layer.effects.shadowOpacity=1;
        auto paint=RasterLayer::create("Fill");shot.layers={layer,paint};Document document;document.load(p);document.selectLayer(1);document.setKeyframeSourceEditing(true);
        RasterFillSettings settings;settings.tolerance=0;
        QVERIFY(RasterTools::bucket(&document,{120,40},Qt::blue,settings));
        const auto output=render(document.project().shots[0].layers,{0,0,128,128});
        QCOMPARE(output.pixelColor(120,40),QColor(Qt::blue));QCOMPARE(output.pixelColor(50,40).alpha(),0);
        QCOMPARE(document.project().shots[0].layers[0].objects,layer.objects);QVERIFY(document.project().shots[0].layers[0].tiles.isEmpty());
        QCOMPARE(document.history()->count(),1);
    }
    void failedEffectSamplingNeverCommitsAFill() {
        auto p=fixture();auto large=ink({0,0,10,10});rect(large,{32768,32768,10,10},Qt::red);large.effects.blurEnabled=true;
        auto paint=RasterLayer::create("Fill");auto &shot=p.shots[0];shot.layers={large,paint};LayerAnimation animation;animation.layerId=large.id;
        animation.channels={{KeyframeProperty::ScaleX,{{0,.01}}},{KeyframeProperty::ScaleY,{{0,.01}}}};shot.layerAnimations={animation};
        Document document;document.load(p);document.selectLayer(1);document.setKeyframeSourceEditing(false);const auto original=document.project().toJson();
        QSignalSpy warnings(&document,&Document::warning);QVERIFY(!RasterTools::bucket(&document,{10,10},Qt::blue));
        QVERIFY(!warnings.isEmpty());QCOMPARE(document.project().toJson(),original);QCOMPARE(document.history()->count(),0);QVERIFY(document.project().shots[0].layers[1].tiles.isEmpty());
    }
    void hiddenAndZeroOpacityAncestorsDoNotAllocateEffectGeometry() {
        auto p=fixture();auto &shot=p.shots[0];auto folder=RasterLayer::create("Hidden folder");folder.group=true;
        auto huge=RasterLayer::create("Large hidden path");huge.kind=LayerKind::Vector;huge.parentId=folder.id;
        huge.objects={vectorRect({0,0,30000,30000})};huge.effects.blurEnabled=true;huge.effects.blurRadius=128;
        LayerAnimation animation;animation.layerId=huge.id;
        animation.channels={{KeyframeProperty::ScaleX,{{0,.01}}},{KeyframeProperty::ScaleY,{{0,.01}}}};
        const auto expected=renderMovieFrame(p,0);shot.layerAnimations={animation};shot.layers.append(folder);shot.layers.append(huge);
        shot.layers[2].visible=false;QCOMPARE(renderMovieFrame(p,0),expected);QVERIFY(keyframeRenderError().isEmpty());
        shot.layers[2].visible=true;shot.layers[1].visible=false;QCOMPARE(renderMovieFrame(p,0),expected);QVERIFY(keyframeRenderError().isEmpty());
        shot.layers[1].visible=true;shot.layers[1].opacity=0;QCOMPARE(renderMovieFrame(p,0),expected);QVERIFY(keyframeRenderError().isEmpty());
        shot.layers[1].opacity=1;shot.layers[2].opacity=0;QCOMPARE(renderMovieFrame(p,0),expected);QVERIFY(keyframeRenderError().isEmpty());
        QVERIFY(shot.layers[2].tiles.isEmpty());
    }
    void cameraAndAnimatedPathExportsMatchSheetAndStayEditable() {
        auto p=fixture();p.transparent=false;auto &shot=p.shots[0];auto &layer=shot.layers[0];layer.kind=LayerKind::Vector;layer.tiles.clear();
        layer.objects={vectorRect({40,40,20,20})};layer.effects.blurEnabled=true;layer.effects.blurRadius=3;
        layer.effects.shadowEnabled=true;layer.effects.shadowRadius=4;layer.effects.shadowOffset={8,0};layer.mask=halfMask();
        auto last=layer.objects[0];for(auto &node:last.nodes){node.anchor.rx()+=100;node.inControl.rx()+=100;node.outControl.rx()+=100;}
        LayerAnimation animation;animation.layerId=layer.id;animation.paths={{last.id,{{0,pathPose(layer.objects[0]),KeyframeInterpolation::Linear},{10,pathPose(last),KeyframeInterpolation::Linear}}}};
        shot.layerAnimations={animation};shot.camera2DKeys={{0,{},KeyframeInterpolation::Linear},{10,{{100,0},1,0},KeyframeInterpolation::Linear}};
        const auto source=p.toJson();
        for(int frame:{0,5,10,0}) {
            const auto movie=renderMovieFrame(p,frame);QVERIFY2(!movie.isNull(),qPrintable(keyframeRenderError()));
            const auto sheet=renderSheet(p,shot,false,frame);QVERIFY(!sheet.isNull());QCOMPARE(sheet.copy(sheetGeometry(p,shot).output.toRect()),movie);
            auto settings=videoExportDefaults(p);settings.size={128,128};QString error;const auto video=renderVideoExportFrame(p,settings,frame,&error);
            QVERIFY2(!video.isNull(),qPrintable(error));QCOMPARE(video.convertToFormat(movie.format()),movie);
        }
        QTemporaryDir temp;QString error;QVERIFY2(exportPngSequence(p,0,0,1,temp.filePath("frames"),{},&error),qPrintable(error));
        const auto movie=renderMovieFrame(p,1);QCOMPARE(QImage(temp.filePath("frames/frame-0002.png")).convertToFormat(movie.format()),movie);
        QCOMPARE(p.toJson(),source);
    }
    void maskSpansMatchPerPixelCoverageAcrossNegativeTiles() {
        auto layer=ink({-300,-280,900,820},QColor(197,83,241,183));LayerMask mask;
        for(const TileKey key:{TileKey{-1,0},TileKey{0,-1},TileKey{1,1}}) {
            QImage tile(tileSize,tileSize,QImage::Format_Grayscale8);
            for(int y=0;y<tileSize;++y)for(int x=0;x<tileSize;++x)tile.scanLine(y)[x]=uchar((x*3+y*7)%256);
            mask.tiles.insert(key,tile);
        }
        const QRect bounds(-320,-300,960,860);const auto original=render({layer},bounds);
        for(int defaultValue:{0,137,255}) {
            mask.defaultValue=defaultValue;layer.mask=mask;const auto output=render({layer},bounds);auto expected=original;
            for(int y=0;y<expected.height();++y)for(int x=0;x<expected.width();++x) {
                const int px=x+bounds.x(),py=y+bounds.y(),tx=int(std::floor(px/256.)),ty=int(std::floor(py/256.));
                const auto found=mask.tiles.constFind({tx,ty});const int coverage=found==mask.tiles.cend()?defaultValue:found->constScanLine(py-ty*tileSize)[px-tx*tileSize];
                const QRgb p=original.pixel(x,y);expected.setPixel(x,y,qRgba((qRed(p)*coverage+127)/255,(qGreen(p)*coverage+127)/255,(qBlue(p)*coverage+127)/255,(qAlpha(p)*coverage+127)/255));
            }
            QCOMPARE(output,expected);QVERIFY(effectRenderError().isEmpty());
        }
    }
    void optimizedFiltersMatchFullImageReference_data() {
        QTest::addColumn<int>("radius");QTest::addColumn<bool>("lighting");QTest::addColumn<QPointF>("offset");
        for(int r:{0,1,2,3,5,16,127,128}) {
            QTest::newRow(qPrintable(QString("blur-%1").arg(r)))<<r<<false<<QPointF();
            QTest::newRow(qPrintable(QString("lights-%1").arg(r)))<<r<<true<<QPointF(r%2?17.25:-13,r%2?-9.75:8);
        }
    }
    void optimizedFiltersMatchFullImageReference() {
        QFETCH(int,radius);QFETCH(bool,lighting);QFETCH(QPointF,offset);
        auto layer=ink({232,36,64,72},QColor(202,53,137,173));rect(layer,{246,49,26,39},QColor(46,192,77,91));
        layer.mask=halfMask();layer.mask->defaultValue=193;
        // The global reference leaves enough empty halo for blur + lighting.
        const QRect work(-128,-256,768,768),crop(160,-32,224,224);auto source=render({layer},work);
        layer.effects.blurEnabled=true;layer.effects.blurRadius=lighting?7:radius;
        source=referenceBlur(source,lighting?7:radius);QImage expected=source;
        if(lighting) {
            layer.effects.shadowEnabled=layer.effects.glowEnabled=true;layer.effects.shadowRadius=layer.effects.glowRadius=radius;
            layer.effects.shadowOffset=offset;layer.effects.shadowColor=QColor(31,67,141);layer.effects.glowColor=QColor(179,223,41);
            layer.effects.shadowOpacity=.61;layer.effects.glowOpacity=.43;
            const auto softened=referenceBlur(source,radius);expected.fill(Qt::transparent);
            {QPainter painter(&expected);painter.drawImage(QPoint(),referenceTint(softened,offset,layer.effects.shadowColor,.61));painter.drawImage(QPoint(),referenceTint(softened,{},layer.effects.glowColor,.43));painter.drawImage(QPoint(),source);}
        }
        QCOMPARE(render({layer},crop),expected.copy(QRect(crop.topLeft()-work.topLeft(),crop.size())));QVERIFY(effectRenderError().isEmpty());
    }
    void groupedRegionsMatchIndividualTiles_data() {
        QTest::addColumn<int>("radius");QTest::addColumn<int>("folders");
        QTest::newRow("small-halo-negative-tiles")<<5<<0;
        QTest::newRow("largest-halo")<<128<<0;
        QTest::newRow("nested-folder-effects")<<43<<3;
        QTest::newRow("nested-working-budget-fallback")<<128<<6;
    }
    void groupedRegionsMatchIndividualTiles() {
        QFETCH(int,radius);QFETCH(int,folders);
        auto layer=ink({-35,-20,390,310},QColor(131,187,53,179));layer.mask=halfMask();layer.mask->defaultValue=183;
        layer.effects.blurEnabled=true;layer.effects.blurRadius=radius;
        layer.effects.shadowEnabled=true;layer.effects.shadowRadius=17;layer.effects.shadowOffset={-27.25,13.75};layer.effects.shadowOpacity=.43;
        layer.effects.colorEnabled=true;layer.effects.exposure=.7;layer.effects.contrast=-12;layer.effects.saturation=23;layer.effects.hue=41;
        QVector<RasterLayer> layers;QString parent;
        for(int i=0;i<folders;++i){auto folder=RasterLayer::create("Effect folder");folder.group=true;folder.parentId=parent;folder.effects.blurEnabled=true;folder.effects.blurRadius=radius;parent=folder.id;layers.append(folder);}
        layer.parentId=parent;layers.append(layer);const QRect bounds(-256,-256,1024,768);
        QImage expected(bounds.size(),QImage::Format_ARGB32_Premultiplied);expected.fill(Qt::transparent);
        // Calling tile directly deliberately retains the bounded 256px path.
        clearEffectRenderError();EffectCompositor individual(layers,true);
        {QPainter painter(&expected);for(int y=-1;y<2;++y)for(int x=-1;x<3;++x)painter.drawImage(QPoint(x*tileSize,y*tileSize)-bounds.topLeft(),individual.tile({x,y}));}
        QVERIFY2(effectRenderError().isEmpty(),qPrintable(effectRenderError()));
        QCOMPARE(render(layers,bounds),expected);QVERIFY2(effectRenderError().isEmpty(),qPrintable(effectRenderError()));
    }
    void largeVisibleGroupsAndPartialViewsPreserveNegativeSeams() {
        auto layer=ink({-800,-780,1700,1660},QColor(209,79,137,183));layer.mask=halfMask();layer.mask->defaultValue=149;
        layer.effects.blurEnabled=true;layer.effects.blurRadius=128;
        layer.effects.shadowEnabled=true;layer.effects.shadowRadius=71;layer.effects.shadowOffset={-93.25,47.75};layer.effects.shadowOpacity=.73;
        layer.effects.glowEnabled=true;layer.effects.glowRadius=71;layer.effects.glowOpacity=.41;
        const QVector<RasterLayer> layers{layer};const QRect bounds(-1024,-1024,2048,2048);
        QImage expected(bounds.size(),QImage::Format_ARGB32_Premultiplied);expected.fill(Qt::transparent);
        clearEffectRenderError();EffectCompositor individual(layers,true);
        {QPainter painter(&expected);for(int y=-4;y<4;++y)for(int x=-4;x<4;++x)painter.drawImage(QPoint(x*tileSize,y*tileSize)-bounds.topLeft(),individual.tile({x,y}));}
        QVERIFY2(effectRenderError().isEmpty(),qPrintable(effectRenderError()));
        // Four complete 16-tile blocks, spanning both sides of the origin.
        QCOMPARE(render(layers,bounds),expected);QVERIFY(effectRenderError().isEmpty());
        // Partial blocks and a zoomed-in single tile must remain exact too.
        for(const QRect crop:{QRect(-744,-733,723,691),QRect(-128,-128,127,127),QRect(0,0,1024,768)}) {
            QCOMPARE(render(layers,crop),expected.copy(QRect(crop.topLeft()-bounds.topLeft(),crop.size())));QVERIFY(effectRenderError().isEmpty());
        }
    }
    void independentColorLutMatchesOriginalQuantization_data() {
        QTest::addColumn<qreal>("exposure");QTest::addColumn<qreal>("contrast");QTest::addColumn<int>("fixedRed");
        for(qreal stops:{-4.,-2.5,0.,.5,4.})for(qreal contrast:{-100.,-37.5,0.,28.75,100.})
            QTest::newRow(qPrintable(QString("exposure-%1-contrast-%2").arg(stops).arg(contrast)))<<stops<<contrast<<-1;
        // Land exposure/contrast on a Qt float-quantization boundary, where
        // removing gray+(channel-gray) blindly can alter the last output bit.
        for(const auto pair:{QPair<int,int>{32,16},QPair<int,int>{100,64},QPair<int,int>{192,224},QPair<int,int>{224,240}}) {
            const qreal factor=(colorQuantizationBoundary(pair.first)-.5)/(referenceExposure(pair.second,0)-.5);
            const qreal contrast=(factor-1)*100;
            for(int direction:{-1,0,1}) {
                const qreal neighboring=direction?std::nextafter(contrast,direction<0?-100.:100.):contrast;
                QTest::newRow(qPrintable(QString("rgb8-boundary-%1-neighbor-%2").arg(pair.first).arg(direction)))<<qreal(0)<<neighboring<<pair.second;
            }
        }
    }
    void independentColorLutMatchesOriginalQuantization() {
        QFETCH(qreal,exposure);QFETCH(qreal,contrast);QFETCH(int,fixedRed);
        QImage pixels(tileSize,tileSize,QImage::Format_ARGB32_Premultiplied);
        for(int y=0;y<tileSize;++y){auto *row=reinterpret_cast<QRgb *>(pixels.scanLine(y));for(int x=0;x<tileSize;++x)
            row[x]=qPremultiply(fixedRed<0?qRgba(x,y,(x*37+y*19)%256,(x*11+y*7)%256):qRgb(fixedRed,x,y));}
        auto layer=RasterLayer::create("Color rounding reference");layer.tiles.insert({0,0},pixels);layer.effects.colorEnabled=true;layer.effects.exposure=exposure;layer.effects.contrast=contrast;
        const auto expected=referenceColorCorrection(pixels,layer.effects);
        QCOMPARE(render({layer},{0,0,tileSize,tileSize}),expected);QCOMPARE(layer.tiles.value({0,0}),pixels);QVERIFY(effectRenderError().isEmpty());
    }
    void cacheTracksMasksEffectsAndWorkingLimitReportsFailure() {
        auto layer=ink();layer.mask=halfMask();const auto first=render({layer});layer.mask->tiles[TileKey{0,0}].fill(255);
        QVERIFY(render({layer})!=first);layer.effects.colorEnabled=true;layer.effects.hue=120;const auto green=render({layer});QVERIFY(green.pixelColor(50,30).green()>200);
        layer.effects.hue=0;QCOMPARE(render({layer}).pixelColor(50,30),QColor(Qt::red));
        auto large=ink({0,0,10,10});rect(large,{32768,32768,10,10},Qt::red);large.effects.blurEnabled=true;large.renderTransform.scale(.01,.01);
        render({large},{0,0,512,512});QVERIFY(!effectRenderError().isEmpty());
        render({layer});QVERIFY(effectRenderError().isEmpty());
    }
};
QTEST_MAIN(EffectsRenderTests)
#include "effects_render_tests.moc"
