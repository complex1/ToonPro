#include "brush_engine.h"
#include "brush_library.h"
#include "document.h"
#include "object_tools.h"
#include "sheet.h"
#include "vector_brush.h"
#include "vector_brush_storage.h"
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QtTest>
#include <cmath>
using namespace toon;
namespace {
QImage coloredTip() {
    QImage image(32,16,QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    for(int y=0;y<16;++y) for(int x=0;x<32;++x) {
        if(x>=12 && x<20 && y>=6 && y<10) continue;
        image.setPixelColor(x,y,x<16?QColor(255,0,0):QColor(0,0,255,128));
    }
    return image;
}
BrushSettings brush() {
    BrushSettings b;b.colorTip=coloredTip();b.size=64;b.pressureSize=false;b.spacing=.4;
    b.color=Qt::green;return b;
}
Project project(bool vector=false) {
    auto p=Project::create();p.width=p.height=256;p.transparent=true;
    p.shots[0].sheet.overscan=0;p.shots[0].layers[0].kind=vector?LayerKind::Vector:LayerKind::Raster;
    return p;
}
const RasterLayer &layer(const Document &d){return d.project().shots[0].layers[0];}
QColor pixel(const Document &d,int x,int y) {
    const auto it=layer(d).tiles.constFind({x/256,y/256});
    return it==layer(d).tiles.cend()?QColor(Qt::transparent):it->pixelColor(x%256,y%256);
}
void tap(Document &d,const BrushSettings &b,BrushInput in={}) {
    QVERIFY(d.beginStroke({128,128},in,b,false));d.endStroke();
}
void draw(Document &d,const BrushSettings &b) {
    BrushInput in;in.pressure=.4;
    QVERIFY(d.beginStroke({48,85},in,b,false));
    for(int i=1;i<=12;++i){in.pressure=.3+.05*i;in.rotation=4*i;d.continueStroke({48+12.*i,85+24*std::sin(i*.4)},in);}
    d.endStroke();
}
QRect footprint(const QImage &mask) {
    QRect out;for(int y=0;y<mask.height();++y)for(int x=0;x<mask.width();++x)
        if(mask.constScanLine(y)[x]>16)out|=QRect(x,y,1,1);
    return out;
}
}
class ColoredStampTest:public QObject {
    Q_OBJECT
private slots:
    void retainsColorsAlphaAndTransparentHoles() {
        Document d;d.load(project());auto b=brush();tap(d,b);
        QCOMPARE(pixel(d,104,120),QColor(Qt::red));
        QCOMPARE(pixel(d,152,120),QColor(0,0,255,128));
        QCOMPARE(pixel(d,128,128).alpha(),0);
        QCOMPARE(pixel(d,128,100).alpha(),0);
        const auto dab=evaluateBrushDab(b,{}, {},0,0,0,1);
        QVERIFY(!dab.colorTip.isNull());QCOMPARE(dab.mask.size(),dab.colorTip.size());
        for(int y=0;y<dab.mask.height();++y)for(int x=0;x<dab.mask.width();++x)
            QCOMPARE(int(dab.mask.constScanLine(y)[x]),qAlpha(dab.colorTip.pixel(x,y)));
        const auto tiles=layer(d).tiles;d.history()->undo();QVERIFY(layer(d).tiles.isEmpty());
        d.history()->redo();QCOMPARE(layer(d).tiles,tiles);
    }
    void angleFlipRoundnessPressureAndSoftEdges() {
        auto b=brush();Document d;d.load(project());b.angle=90;tap(d,b);
        QCOMPARE(pixel(d,136,104),QColor(Qt::red));QCOMPARE(pixel(d,136,152),QColor(0,0,255,128));
        d.load(project());b.angle=0;b.flipX=true;tap(d,b);
        QCOMPARE(pixel(d,104,120),QColor(0,0,255,128));QCOMPARE(pixel(d,152,120),QColor(Qt::red));
        b.flipX=false;const auto normal=footprint(brushTipMask(b,{}));
        b.roundness=.4;const auto flat=footprint(brushTipMask(b,{}));
        QCOMPARE(flat.width(),normal.width());QVERIFY(flat.height()<normal.height()*.5);
        b.roundness=1;b.pressureSize=true;BrushInput in;in.pressure=.5;
        const auto half=footprint(brushTipMask(b,in));QVERIFY(half.width()<normal.width()*.6);
        b.pressureSize=false;b.hardness=.2;const auto soft=evaluateBrushDab(b,{}, {},0,0,0,1);
        bool partialRed=false;
        for(int y=0;y<soft.colorTip.height();++y)for(int x=0;x<soft.colorTip.width()/3;++x){
            const auto c=soft.colorTip.pixelColor(x,y);if(c.alpha()>4&&c.alpha()<250){QVERIFY(c.red()>250);partialRed=true;}
        }
        QVERIFY(partialRed);
    }
    void opacitySelectionAndTextureRespectSourceColor() {
        auto b=brush();b.strokeOpacity=.5;Document d;d.load(project());tap(d,b);
        QVERIFY(std::abs(pixel(d,104,120).alpha()-128)<=1);QCOMPARE(pixel(d,104,120).red(),255);
        QVERIFY(std::abs(pixel(d,152,120).alpha()-64)<=1);QCOMPARE(pixel(d,152,120).blue(),255);
        d.load(project());QPainterPath selection;selection.addRect(128,0,128,256);d.setRasterSelection(selection);tap(d,b);
        QCOMPARE(pixel(d,104,120).alpha(),0);QVERIFY(pixel(d,152,120).alpha()>0);
        d.load(project());b.strokeOpacity=1;b.texture.enabled=true;
        b.texture.pattern=QImage(4,4,QImage::Format_Grayscale8);b.texture.pattern.fill(128);tap(d,b);
        QVERIFY(std::abs(pixel(d,104,120).alpha()-128)<=1);QCOMPARE(pixel(d,104,120).red(),255);
        QVERIFY(std::abs(pixel(d,152,120).alpha()-64)<=1);QCOMPARE(pixel(d,152,120).blue(),255);
    }
    void additiveTextureKeepsTransparentHolesAndAccuratePicking() {
        auto b=brush();b.texture.enabled=true;b.texture.mode=MaskBlend::Add;
        b.texture.pattern=QImage(2,2,QImage::Format_Grayscale8);b.texture.pattern.fill(255);
        Document d;d.load(project(true));tap(d,b);
        QCOMPARE(pixel(d,128,128).alpha(),0);
        QPainterPath hole;hole.addRect(126,126,2,2);
        QVERIFY(!vectorBrushIntersects(layer(d).objects.first(),hole));
        QPainterPath paint;paint.addRect(104,120,2,2);
        QVERIFY(vectorBrushIntersects(layer(d).objects.first(),paint));
        const auto cursor=brushTipMask(b,{},0,0,0,1,{128,128});
        QCOMPARE(cursor.constScanLine(cursor.height()/2)[cursor.width()/2],uchar(0));
    }
    void monochromeAndUniformColorStampAgree() {
        auto mono=brush();mono.colorTip={};mono.tipMask=QImage(32,16,QImage::Format_Grayscale8);mono.tipMask.fill(255);
        mono.color=QColor("#306090");auto color=mono;color.tipMask={};
        color.colorTip=QImage(32,16,QImage::Format_ARGB32_Premultiplied);color.colorTip.fill(mono.color);
        color.color=Qt::magenta;
        Document a,b;a.load(project());b.load(project());tap(a,mono);tap(b,color);QCOMPARE(layer(a).tiles,layer(b).tiles);
    }
    void colorVariationRequiresOptInAndLeavesMonochromeUnchanged() {
        auto b=brush();const auto original=evaluateBrushDab(b,{}, {},0,0,3,42);
        b.colorDynamics.enabled=true;b.colorDynamics.hue=.8;b.colorDynamics.saturation=.6;
        b.colorDynamics.brightness=.5;b.colorDynamics.purity=-.4;b.colorDynamics.foregroundBackground=1;
        const auto legacy=evaluateBrushDab(b,{}, {},0,0,3,42);
        QCOMPARE(legacy.colorTip,original.colorTip);QCOMPARE(legacy.mask,original.mask);
        Document a,c;a.load(project());c.load(project());auto baseline=brush();draw(a,baseline);draw(c,b);
        QCOMPARE(layer(a).tiles,layer(c).tiles);
        b.colorDynamics.applyToColorTip=true;b.colorDynamics.enabled=false;
        QCOMPARE(evaluateBrushDab(b,{}, {},0,0,3,42).colorTip,original.colorTip);
        b.colorDynamics.enabled=true;b.colorDynamics.hue=0;b.colorDynamics.saturation=0;
        b.colorDynamics.brightness=0;b.colorDynamics.purity=0;
        QCOMPARE(evaluateBrushDab(b,{}, {},0,0,3,42).colorTip,original.colorTip);
        // The new setting never changes the existing grayscale/foreground path.
        b.colorTip={};b.tipMask=QImage(16,16,QImage::Format_Grayscale8);b.tipMask.fill(180);
        b.colorDynamics.hue=.6;b.colorDynamics.saturation=.3;
        const auto mono=evaluateBrushDab(b,{}, {},0,0,3,42);
        b.colorDynamics.applyToColorTip=false;
        const auto oldMono=evaluateBrushDab(b,{}, {},0,0,3,42);
        QCOMPARE(mono.mask,oldMono.mask);QCOMPARE(mono.color,oldMono.color);
    }
    void colorVariationPreservesAlphaPaletteAndDeterministicDabs() {
        auto b=brush();b.hardness=.3;
        const auto original=evaluateBrushDab(b,{}, {},0,0,0,81);
        const auto source=b.colorTip;
        b.colorDynamics.enabled=true;b.colorDynamics.applyToColorTip=true;
        b.colorDynamics.hue=.4;b.colorDynamics.saturation=.7;
        b.colorDynamics.brightness=.3;b.colorDynamics.purity=-.2;
        const auto first=evaluateBrushDab(b,{}, {},0,0,0,81);
        const auto next=evaluateBrushDab(b,{}, {},0,0,1,81);
        QVERIFY(first.colorTip!=original.colorTip);QVERIFY(first.colorTip!=next.colorTip);
        QCOMPARE(first.mask,original.mask);QCOMPARE(next.mask,original.mask);
        QCOMPARE(first.colorTip,evaluateBrushDab(b,{}, {},0,0,0,81).colorTip);
        QVERIFY(first.colorTip!=evaluateBrushDab(b,{}, {},0,0,0,82).colorTip);
        QCOMPARE(b.colorTip,source);
        bool translucent=false,empty=false;
        for(int y=0;y<first.colorTip.height();++y)for(int x=0;x<first.colorTip.width();++x){
            const auto before=original.colorTip.pixel(x,y),after=first.colorTip.pixel(x,y);
            QCOMPARE(qAlpha(before),qAlpha(after));QCOMPARE(qAlpha(after),int(first.mask.constScanLine(y)[x]));
            QVERIFY(qRed(after)<=qAlpha(after));QVERIFY(qGreen(after)<=qAlpha(after));QVERIFY(qBlue(after)<=qAlpha(after));
            if(!qAlpha(before)){QCOMPARE(after,QRgb(0));empty=true;continue;}
            translucent|=qAlpha(before)<255;
            float h,s,v;QColor::fromRgba(qUnpremultiply(before)).getHsvF(&h,&s,&v);if(h<0)h=0;
            const auto &d=b.colorDynamics;
            h=std::fmod(h+(brushRandom(81,0,102)*2-1)*d.hue+2,1.);
            s=qBound(0.,s+(brushRandom(81,0,103)*2-1)*d.saturation,1.);
            s=d.purity>=0?s+(1-s)*d.purity:s*(1+d.purity);
            v=qBound(0.,v+(brushRandom(81,0,104)*2-1)*d.brightness,1.);
            auto expected=QColor::fromHsvF(h,s,v);expected.setAlpha(qAlpha(before));
            const auto pixel=qPremultiply(expected.rgba());
            QVERIFY(std::abs(qRed(after)-qRed(pixel))<=1);
            QVERIFY(std::abs(qGreen(after)-qGreen(pixel))<=1);
            QVERIFY(std::abs(qBlue(after)-qBlue(pixel))<=1);
        }
        QVERIFY(translucent);QVERIFY(empty);
        b.color=Qt::yellow;b.backgroundColor=Qt::cyan;b.colorDynamics.foregroundBackground=1;
        b.colorDynamics.control=BrushControl::Pressure;
        QCOMPARE(first.colorTip,evaluateBrushDab(b,{}, {},0,0,0,81).colorTip);
        b.colorDynamics.applyToColorTip=false;
        QCOMPARE(original.colorTip,evaluateBrushDab(b,{}, {},0,0,0,81).colorTip);
    }
    void perStrokeColorVariationSurvivesCapturePreviewAndStampPlacement() {
        auto b=brush();b.colorDynamics.enabled=true;b.colorDynamics.applyToColorTip=true;
        b.colorDynamics.perTip=false;b.colorDynamics.hue=.5;b.colorDynamics.saturation=.3;
        b.colorDynamics.purity=-.5;b.lockRandomSeed=true;b.randomSeed=19;
        const auto first=evaluateBrushDab(b,{}, {},0,0,0,19);
        BrushInput input;input.pressure=.2;
        QCOMPARE(first.colorTip,evaluateBrushDab(b,input,{},0,0,17,19).colorTip);
        auto disabled=b;disabled.colorDynamics.applyToColorTip=false;
        QVERIFY(brushStrokePreview(b)!=brushStrokePreview(disabled));
        Document raster,vector,placement;
        raster.load(project());vector.load(project(true));placement.load(project(true));
        tap(raster,b);tap(vector,b);
        QCOMPARE(layer(vector).tiles,layer(raster).tiles);
        QVERIFY(!layer(vector).objects.first().brushStroke->settings.colorDynamics.enabled);
        QVERIFY(layer(vector).objects.first().brushStroke->settings.colorDynamics.applyToColorTip);
        QVERIFY(layer(vector).objects.first().brushStroke->settings.colorTip!=b.colorTip);
        QVERIFY(placement.beginStampPlacement({128,128},{},b));QVERIFY(placement.applyTransform());
        QCOMPARE(layer(placement).tiles,layer(raster).tiles);
        QVERIFY(!layer(placement).objects.first().brushStroke->settings.colorDynamics.enabled);
        auto rebuilt=layer(vector);QString error;
        QVERIFY2(rebuildObjectLayer(rebuilt,vector.drawingBounds(),maxProjectTiles,&error),qPrintable(error));
        QCOMPARE(rebuilt.tiles,layer(raster).tiles);
    }
    void perStrokeColorIsSharedByAllCountCopies() {
        auto b=brush();b.size=24;b.count=4;b.scatter=.55;b.scatterBothAxes=true;
        b.colorDynamics.enabled=true;b.colorDynamics.applyToColorTip=true;b.colorDynamics.perTip=false;
        b.colorDynamics.hue=.7;b.colorDynamics.saturation=.5;b.colorDynamics.purity=-.4;
        b.lockRandomSeed=true;b.randomSeed=319;
        const auto original=b.colorTip;auto frozen=b;
        resolveBrushStrokeColor(frozen,{},b.randomSeed);
        QVERIFY(!frozen.colorDynamics.enabled);QVERIFY(frozen.colorTip!=original);QCOMPARE(b.colorTip,original);
        const auto first=evaluateBrushDab(frozen,{}, {},0,0,0,b.randomSeed);
        for(int copy=1;copy<b.count;++copy){
            const auto dab=evaluateBrushDab(frozen,{}, {},0,0,7,b.randomSeed^(quint32(copy)*0x9e3779b9U));
            QCOMPARE(dab.colorTip,first.colorTip);
        }
        Document raster,vector,expected;
        raster.load(project());vector.load(project(true));expected.load(project());
        draw(raster,b);draw(vector,b);draw(expected,frozen);
        QCOMPARE(layer(raster).tiles,layer(expected).tiles);QCOMPARE(layer(vector).tiles,layer(expected).tiles);
        auto rebuilt=layer(vector);QString error;
        QVERIFY2(rebuildObjectLayer(rebuilt,vector.drawingBounds(),maxProjectTiles,&error),qPrintable(error));
        QCOMPARE(rebuilt.tiles,layer(expected).tiles);
    }
    void multiStampColorSizeOrderReplayAndSave() {
        auto b=brush();QString error;
        QVERIFY2(setBrushTipImages(b,{b.colorTip,b.colorTip.flipped(Qt::Horizontal)},{},&error),qPrintable(error));
        b.tipOrder=BrushTipOrder::Shuffle;b.size=30;b.sizeDynamics.jitter=.55;
        b.angleDynamics.jitter=.3;b.colorDynamics.enabled=true;b.colorDynamics.applyToColorTip=true;
        b.colorDynamics.hue=.4;b.colorDynamics.saturation=.6;b.colorDynamics.brightness=.3;
        b.colorDynamics.purity=-.2;b.lockRandomSeed=true;b.randomSeed=781;
        Document raster,vector;raster.load(project());vector.load(project(true));draw(raster,b);draw(vector,b);
        QCOMPARE(layer(raster).tiles,layer(vector).tiles);
        auto rebuilt=layer(vector);
        QVERIFY2(rebuildObjectLayer(rebuilt,vector.drawingBounds(),maxProjectTiles,&error),qPrintable(error));
        QCOMPARE(rebuilt.tiles,layer(raster).tiles);
        QTemporaryDir dir;const auto file=dir.filePath("dynamic-color.toonproj");
        QVERIFY2(saveProject(vector.project(),file,&error),qPrintable(error));
        auto loaded=loadProject(file,&error);QVERIFY2(loaded.has_value(),qPrintable(error));
        const auto &restored=loaded->shots[0].layers[0];
        QVERIFY(restored.objects.first().brushStroke->settings.colorDynamics.applyToColorTip);
        QCOMPARE(restored.objects.first().brushStroke->settings.tipFrames,b.tipFrames);
        auto replayed=restored;
        QVERIFY2(rebuildObjectLayer(replayed,vector.drawingBounds(),maxProjectTiles,&error),qPrintable(error));
        QCOMPARE(replayed.tiles,layer(raster).tiles);
    }
    void vectorReplayTransformsAndProjectRoundTrip() {
        auto b=brush();b.size=30;b.pressureSize=true;b.angle=17;b.roundness=.7;b.opacity=.7;b.strokeOpacity=.6;
        Document raster,vector;raster.load(project());vector.load(project(true));draw(raster,b);draw(vector,b);
        QCOMPARE(layer(vector).objects.size(),1);QCOMPARE(layer(vector).tiles,layer(raster).tiles);
        auto object=layer(vector).objects.first();object.transform.translate(9,7);object.transform.rotate(13);object.opacity=.7;
        QVERIFY(ObjectTools::updateObject(&vector,object));const auto expected=layer(vector).tiles;
        auto rebuilt=layer(vector);QString error;
        QVERIFY2(rebuildObjectLayer(rebuilt,vector.drawingBounds(),maxProjectTiles,&error),qPrintable(error));QCOMPARE(rebuilt.tiles,expected);
        QTemporaryDir dir;const auto file=dir.filePath("colored.toonproj");
        QVERIFY2(saveProject(vector.project(),file,&error),qPrintable(error));
        QFile saved(file);QVERIFY(saved.open(QIODevice::ReadOnly));QCOMPARE(saved.read(10),QByteArray("TOONPRO20\n"));
        auto loaded=loadProject(file,&error);QVERIFY2(loaded.has_value(),qPrintable(error));
        QCOMPARE(loaded->shots[0].layers[0].tiles,expected);
        QCOMPARE(loaded->shots[0].layers[0].objects[0].brushStroke->settings.colorTip,b.colorTip);
        QCOMPARE(renderMovieFrame(*loaded),renderMovieFrame(vector.project()));
        auto old=vector.project().toJson();old["version"]=19;QVERIFY(!Project::fromJson(old,&error));
        QVERIFY(error.contains("format 20"));
        auto wrong=vector.project().toJson();auto resources=wrong["brushResources"].toArray();
        auto descriptor=resources[0].toObject();descriptor.remove("format");resources[0]=descriptor;wrong["brushResources"]=resources;
        QVERIFY(!Project::fromJson(wrong,&error));QVERIFY(error.contains("color format"));
    }
    void largeSourcePressureCacheTracksImageChanges() {
        auto b=brush();b.colorTip=QImage(2048,1024,QImage::Format_ARGB32_Premultiplied);b.colorTip.fill(Qt::red);
        b.size=96;b.pressureSize=true;BrushInput in;in.pressure=.37;
        const auto small=evaluateBrushDab(b,in,{},0,0,0,1);
        QCOMPARE(small.colorTip.pixelColor(small.colorTip.width()/2,small.colorTip.height()/2),QColor(Qt::red));
        in.pressure=.73;const auto larger=evaluateBrushDab(b,in,{},0,0,1,1);
        QVERIFY(larger.mask.width()>small.mask.width());
        b.colorTip.fill(Qt::blue);const auto changed=evaluateBrushDab(b,in,{},0,0,1,1);
        QCOMPARE(changed.colorTip.pixelColor(changed.colorTip.width()/2,changed.colorTip.height()/2),QColor(Qt::blue));
        b.size=1;b.colorTip=QImage(2048,1,QImage::Format_ARGB32_Premultiplied);b.colorTip.fill(Qt::red);
        QVERIFY(!prepareBrushStamp(b).isNull());QVERIFY(!brushTipMask(b,{}).isNull());
    }
    void coloredLibraryRoundTripAndInvalidResources() {
        QTemporaryDir dir;const auto file=dir.filePath("brushes.json");QString error;
        BrushLibrary library(file);QVERIFY(library.load(&error));auto b=brush();
        QVERIFY2(library.saveCurrent("Colored leaf",b,0,&error),qPrintable(error));
        BrushLibrary loaded(file);QVERIFY2(loaded.load(&error),qPrintable(error));
        QCOMPARE(loaded.presets().last().brush.colorTip,b.colorTip);
        QVERIFY(loaded.presets().last().brush.tipMask.isNull());
        QFile saved(file);QVERIFY(saved.open(QIODevice::ReadOnly));
        QCOMPARE(QJsonDocument::fromJson(saved.readAll()).object()["version"].toInt(),3);
        b.colorTip.fill(Qt::transparent);QVERIFY(!validateBrushSettings(b,&error));QVERIFY(error.contains("transparent"));
        b.colorTip=QImage(4,4,QImage::Format_RGB32);b.colorTip.fill(Qt::red);
        QVERIFY(!validateBrushSettings(b,&error));QVERIFY(error.contains("RGBA"));
        b.colorTip=QImage(2,2,QImage::Format_ARGB32_Premultiplied);b.colorTip.fill(Qt::transparent);
        b.colorTip.setPixel(0,0,qRgba(255,0,0,32));
        QVERIFY(!validateBrushSettings(b,&error));QVERIFY(error.contains("premultiplied"));
        b.colorTip=QImage(2049,1,QImage::Format_ARGB32_Premultiplied);b.colorTip.fill(Qt::red);
        QVERIFY(!validateBrushSettings(b,&error));QVERIFY(error.contains("2048"));
    }
};
QTEST_GUILESS_MAIN(ColoredStampTest)
#include "test_colored_stamp.moc"
