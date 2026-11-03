#include "document.h"
#include "brush_engine.h"
#include "raster_edit.h"
#include <QTemporaryDir>
#include <QtTest>
#include <cmath>
#include <limits>
#include <algorithm>
#include <array>

using namespace toon;

static Project blankProject() {
    auto project = Project::create();
    project.width = 640; project.height = 480; project.transparent = true;
    return project;
}
static QImage solidMask(int width, int height, int value = 255) {
    QImage mask(width, height, QImage::Format_Grayscale8);
    mask.fill(value);
    return mask;
}
static QColor pixel(const Document &document, int x, int y) {
    const int tx = int(std::floor(qreal(x) / tileSize)), ty = int(std::floor(qreal(y) / tileSize));
    const auto &tiles = document.project().shots.first().layers.first().tiles;
    const auto tile = tiles.constFind({tx, ty});
    return tile == tiles.cend() ? QColor(Qt::transparent) : tile->pixelColor(x - tx * tileSize, y - ty * tileSize);
}
static void tap(Document &document, QPointF position, const BrushSettings &brush, qreal pressure = 1, bool eraser = false) {
    QVERIFY(document.beginStroke(position, pressure, brush, eraser));
    document.endStroke();
}

// Frozen pre-optimization compositor. Exercise the public painting APIs against
// the original floating-point behavior, rather than restating the new byte path.
static QRgb legacyComposite(QRgb source, QRgb backdrop, BrushBlend mode, bool eraser,
                            bool alphaLocked, int x, int y, quint32 seed) {
    using RGB = std::array<qreal, 3>;
    const auto unit = [](qreal value) { return qBound(0.0, value, 1.0); };
    const auto lum = [](const RGB &c) { return .3*c[0]+.59*c[1]+.11*c[2]; };
    const auto sat = [](const RGB &c) { return *std::max_element(c.begin(),c.end())-*std::min_element(c.begin(),c.end()); };
    const auto setLum = [&](RGB c, qreal value) {
        const qreal delta=value-lum(c); for(auto &v:c)v+=delta;
        const qreal low=*std::min_element(c.begin(),c.end()),high=*std::max_element(c.begin(),c.end());
        if(low<0)for(auto &v:c)v=value+(v-value)*value/(value-low);
        if(high>1)for(auto &v:c)v=value+(v-value)*(1-value)/(high-value);
        return c;
    };
    const auto setSat = [](RGB c, qreal value) {
        const auto [lo,hi]=std::minmax_element(c.begin(),c.end()); const qreal low=*lo,range=*hi-low;
        for(auto &v:c)v=range>0?(v-low)*value/range:0; return c;
    };
    qreal sa=qAlpha(source)/255.0,da=qAlpha(backdrop)/255.0;
    if(sa==0)return backdrop;
    if(eraser)return qRgba(qRound(qRed(backdrop)*(1-sa)),qRound(qGreen(backdrop)*(1-sa)),qRound(qBlue(backdrop)*(1-sa)),qRound(qAlpha(backdrop)*(1-sa)));
    const RGB s={qRed(source)/255.0/sa,qGreen(source)/255.0/sa,qBlue(source)/255.0/sa};
    if(mode==BrushBlend::Dissolve){
        const quint64 key=(quint64(quint32(x))<<32)|quint32(y);
        if(brushRandom(seed,key,200)>=sa)return backdrop; sa=1;
    }
    const RGB d=da==0?RGB{0,0,0}:RGB{qRed(backdrop)/255.0/da,qGreen(backdrop)/255.0/da,qBlue(backdrop)/255.0/da};
    RGB blend;
    if(mode==BrushBlend::Hue)blend=setLum(setSat(s,sat(d)),lum(d));
    else if(mode==BrushBlend::Saturation)blend=setLum(setSat(d,sat(s)),lum(d));
    else if(mode==BrushBlend::Color)blend=setLum(s,lum(d));
    else if(mode==BrushBlend::Luminosity)blend=setLum(d,lum(s));
    else for(int i=0;i<3;++i){
        const qreal a=d[i],b=s[i]; qreal value=b;
        switch(mode){
        case BrushBlend::Multiply:value=a*b;break;
        case BrushBlend::Screen:value=1-(1-a)*(1-b);break;
        case BrushBlend::Overlay:value=a<=.5?2*a*b:1-2*(1-a)*(1-b);break;
        case BrushBlend::Darken:value=qMin(a,b);break;
        case BrushBlend::Lighten:value=qMax(a,b);break;
        case BrushBlend::ColorDodge:value=b>=1?1:qMin(1.0,a/(1-b));break;
        case BrushBlend::ColorBurn:value=b<=0?0:1-qMin(1.0,(1-a)/b);break;
        case BrushBlend::LinearDodge:value=qMin(1.0,a+b);break;
        case BrushBlend::LinearBurn:value=qMax(0.0,a+b-1);break;
        case BrushBlend::Difference:value=std::abs(a-b);break;
        case BrushBlend::Exclusion:value=a+b-2*a*b;break;
        case BrushBlend::Subtract:value=qMax(0.0,a-b);break;
        case BrushBlend::Divide:value=b<=0?1:qMin(1.0,a/b);break;
        default:break;
        }
        blend[i]=unit(value);
    }
    RGB result;
    if(alphaLocked){
        for(int i=0;i<3;++i)result[i]=((1-sa)*d[i]+sa*blend[i])*da;
        return qRgba(qRound(unit(result[0])*255),qRound(unit(result[1])*255),qRound(unit(result[2])*255),qAlpha(backdrop));
    }
    for(int i=0;i<3;++i)result[i]=(1-sa)*d[i]*da+(1-da)*s[i]*sa+sa*da*blend[i];
    return qRgba(qRound(unit(result[0])*255),qRound(unit(result[1])*255),qRound(unit(result[2])*255),qRound((sa+da-sa*da)*255));
}
static QImage legacyCompositeImage(const QImage &source, const QImage &backdrop,
                                   BrushBlend mode, bool alphaLocked, bool eraser=false) {
    auto expected=backdrop.copy();
    for(int y=0;y<expected.height();++y){
        const auto *src=reinterpret_cast<const QRgb *>(source.constScanLine(y));
        auto *dst=reinterpret_cast<QRgb *>(expected.scanLine(y));
        for(int x=0;x<expected.width();++x)dst[x]=legacyComposite(src[x],dst[x],mode,eraser,alphaLocked,x,y,713);
    }
    return expected;
}

class BrushEngineTest : public QObject {
    Q_OBJECT
private slots:
    void normalCompositionRetainsEveryAlphaAndChannel() {
        // Every alpha and every valid premultiplied channel occurs on each side;
        // counterpart edge/midpoint coverage plus a full alpha cross-product
        // catches rounding changes in transparent, partial and opaque painting.
        QImage source(tileSize,tileSize,QImage::Format_ARGB32_Premultiplied);
        QImage backdrop(source.size(),source.format());
        const auto check=[&]{
            RasterLayer layer; layer.tiles.insert({0,0},backdrop);
            QVERIFY(compositeBrushAccumulation(layer,{{{0,0},source}},BrushBlend::Normal,false,1,713));
            QCOMPARE(layer.tiles.value({0,0}),legacyCompositeImage(source,backdrop,BrushBlend::Normal,false));
        };
        for(bool reversed:{false,true})for(int alpha:{0,1,17,127,254,255})for(int channel:{0,1,2}){
            for(int y=0;y<tileSize;++y){
                auto *src=reinterpret_cast<QRgb *>(source.scanLine(y));
                auto *dst=reinterpret_cast<QRgb *>(backdrop.scanLine(y));
                for(int x=0;x<tileSize;++x){
                    const int c=qMin(x,y),other=channel==0?0:channel==1?alpha/2:alpha;
                    const QRgb varying=qRgba(c,y-c,(c*37)%(y+1),y);
                    const QRgb fixed=qRgba(other,alpha-other,(other*19)%(alpha+1),alpha);
                    src[x]=reversed?fixed:varying; dst[x]=reversed?varying:fixed;
                }
            }
            check();
        }
        for(int y=0;y<tileSize;++y){
            auto *src=reinterpret_cast<QRgb *>(source.scanLine(y));
            auto *dst=reinterpret_cast<QRgb *>(backdrop.scanLine(y));
            for(int x=0;x<tileSize;++x){
                src[x]=qRgba((x*31)%(y+1),(x*73)%(y+1),(x*17)%(y+1),y);
                dst[x]=qRgba((y*53)%(x+1),(y*11)%(x+1),(y*97)%(x+1),x);
            }
        }
        check();
        // Every mode, including alpha lock and coordinate-seeded dissolve,
        // must still take its original compositing behavior.
        for(int mode=0;mode<=int(BrushBlend::Dissolve);++mode)for(bool locked:{false,true}){
            RasterLayer layer; layer.tiles.insert({0,0},backdrop);
            QVERIFY(compositeBrushAccumulation(layer,{{{0,0},source}},BrushBlend(mode),locked,1,713));
            QCOMPARE(layer.tiles.value({0,0}),legacyCompositeImage(source,backdrop,BrushBlend(mode),locked));
        }
    }
    void paintingDabsRetainTransformedTexturedPixels() {
        BrushSettings brush; brush.size=128; brush.opacity=.29; brush.strokeOpacity=.61;
        brush.hardness=.4; brush.roundness=.7; brush.color=QColor("#4279b3");
        brush.texture.enabled=true; brush.texture.scale=1.7; brush.texture.depth=.63;
        brush.texture.contrast=-.2; brush.texture.brightness=.1; brush.texture.invert=true;
        brush.texture.pattern=solidMask(13,9);
        for(int y=0;y<9;++y)for(int x=0;x<13;++x)brush.texture.pattern.scanLine(y)[x]=uchar((x*43+y*73)%256);
        brush.tipMask=solidMask(48,32);
        for(int y=0;y<32;++y)for(int x=0;x<48;++x)brush.tipMask.scanLine(y)[x]=uchar((x*31+y*17+x*y*3)%256);
        brush.angleDynamics.jitter=.4; brush.flowDynamics.control=BrushControl::Pressure;
        brush.opacityDynamics.control=BrushControl::Pressure; brush.colorDynamics.enabled=true;
        brush.colorDynamics.hue=.3; brush.colorDynamics.perTip=true;
        QPainterPath selection; selection.addEllipse(QRectF(5.5,8.5,233,219));
        const QRectF bounds(.3,.7,250.2,252.1);
        QImage backdrop(tileSize,tileSize,QImage::Format_ARGB32_Premultiplied);
        for(int y=0;y<tileSize;++y){auto *row=reinterpret_cast<QRgb *>(backdrop.scanLine(y));
            for(int x=0;x<tileSize;++x){const int alpha=(x*11+y*17)%256;row[x]=qRgba(alpha/3,alpha/2,alpha*3/4,alpha);}}
        RasterLayer original; original.tiles.insert({0,0},backdrop);
        for(bool colored:{false,true})for(int effects=0;effects<3;++effects)for(int transformIndex=0;transformIndex<3;++transformIndex){
            brush.colorTip={};
            if(colored){
                brush.colorTip=QImage(48,32,QImage::Format_ARGB32_Premultiplied);
                for(int y=0;y<32;++y){auto *row=reinterpret_cast<QRgb *>(brush.colorTip.scanLine(y));
                    for(int x=0;x<48;++x)row[x]=qPremultiply(qRgba((x*17)%256,(y*29)%256,173,brush.tipMask.constScanLine(y)[x]));}
            }
            brush.texture.eachTip=effects==1; brush.wetEdges=effects==2; brush.noise=effects==2;
            brush.dual.enabled=effects==2; brush.dual.tipMask=brush.texture.pattern; brush.dual.size=47;
            QTransform transform;
            if(transformIndex==1){transform.rotate(23);transform.scale(-1.2,.75);}
            if(transformIndex==2){transform.shear(.2,-.1);transform.scale(.8,1.3);}
            RasterLayer preview=original,painting=original,previewAccumulation,paintingAccumulation;
            preview.alphaLocked=painting.alphaLocked=transformIndex==2;
            RasterSelectionCache previewSelection(&selection,4),paintingSelection(&selection,4);
            for(int index=0;index<4;++index){
                BrushInput input; input.pressure=std::array<qreal,4>{.25,1,.43,.8}[index]; input.rotation=index*11;
                const QPointF position(112.35+index*8.83,96.7+index*11.2);
                const auto detailed=evaluateBrushDab(brush,input,position,37,12,index,713);
                const auto paintOnly=evaluateBrushDab(brush,input,position,37,12,index,713,{},BrushDabPurpose::Painting);
                QCOMPARE(paintOnly.colorTip,detailed.colorTip); QCOMPARE(paintOnly.untexturedMask,detailed.untexturedMask);
                QCOMPARE(paintOnly.color,detailed.color); QCOMPARE(paintOnly.flow,detailed.flow);
                QCOMPARE(paintOnly.opacity,detailed.opacity); QCOMPARE(paintOnly.offset,detailed.offset);
                QCOMPARE(paintOnly.canvasTextureDepth,detailed.canvasTextureDepth);
                if(brush.texture.eachTip)QCOMPARE(paintOnly.mask,detailed.mask);
                else {QCOMPARE(paintOnly.mask,paintOnly.untexturedMask);QVERIFY(paintOnly.mask!=detailed.mask);}
                const BrushBlend mode=transformIndex==1?BrushBlend::Multiply:BrushBlend::Normal;
                const bool eraser=effects==1;
                QVERIFY(applyBrushDab(preview,previewAccumulation,original,detailed,position,transform,mode,eraser,bounds,8,713,&selection,&previewSelection));
                QVERIFY(applyBrushDab(painting,paintingAccumulation,original,paintOnly,position,transform,mode,eraser,bounds,8,713,&selection,&paintingSelection));
                QCOMPARE(painting.tiles,preview.tiles); QCOMPARE(paintingAccumulation.tiles,previewAccumulation.tiles);
                if(!paintingAccumulation.tiles.isEmpty()){
                    // Public painting output must also keep legacy normal,
                    // alternate blend, erase and locked-alpha composition.
                    const auto accumulated=paintingAccumulation.tiles.value({0,0});
                    QImage source(tileSize,tileSize,QImage::Format_ARGB32_Premultiplied);
                    for(int y=0;y<tileSize;++y){
                        const auto *src=reinterpret_cast<const QRgba64 *>(accumulated.constScanLine(y));
                        auto *dst=reinterpret_cast<QRgb *>(source.scanLine(y));
                        for(int x=0;x<tileSize;++x)dst[x]=src[x].toArgb32();
                    }
                    QCOMPARE(painting.tiles.value({0,0}),legacyCompositeImage(source,backdrop,mode,painting.alphaLocked,eraser));
                }
            }
        }
    }
    void repeatedTipsPreserveDynamicStamps() {
        BrushSettings brush; brush.size=43; brush.tipMask=solidMask(80,48);
        for(int y=0;y<brush.tipMask.height();++y)for(int x=0;x<brush.tipMask.width();++x)
            brush.tipMask.scanLine(y)[x]=uchar((x*31+y*17+x*y*3)%256);
        brush.texture.pattern=solidMask(13,9);
        for(int y=0;y<9;++y)for(int x=0;x<13;++x)
            brush.texture.pattern.scanLine(y)[x]=uchar((x*43+y*73)%256);
        brush.scatter=4.3; brush.scatterBothAxes=true; brush.opacity=.26;
        brush.colorDynamics.enabled=true; brush.colorDynamics.hue=.2;
        brush.flowDynamics.control=BrushControl::Pressure;
        for(int mode=0;mode<5;++mode){
            brush.texture.enabled=mode>=1; brush.texture.eachTip=mode==2;
            brush.texture.contrast=-.18; brush.texture.scale=1.7; brush.texture.invert=true;
            brush.wetEdges=mode==3; brush.noise=mode==4;
            brush.dual.enabled=mode==4; brush.dual.tipMask=brush.texture.pattern; brush.dual.size=18;
            for(int sample=0;sample<36;++sample){
                BrushInput input; input.pressure=.2+(sample%7)*.12;
                input.tiltX=sample%9; input.rotation=(sample%5)*17;
                brush.hardness=(sample%3)*.5; brush.roundness=.3+(sample%6)*.1;
                brush.angleDynamics.jitter=sample%2?.24:0;
                brush.projection=sample%2; brush.flipX=sample%3==0; brush.flipY=sample%4==0;
                const QPointF position(112.35+sample*.83,96.7-sample*.2);
                const auto warm=evaluateBrushDab(brush,input,position,37,12,sample,19);
                const auto repeated=evaluateBrushDab(brush,input,position,37,12,sample,19);
                auto independent=brush; independent.tipMask=brush.tipMask.copy();
                const auto cold=evaluateBrushDab(independent,input,position,37,12,sample,19);
                QCOMPARE(repeated.mask,warm.mask); QCOMPARE(repeated.mask,cold.mask);
                QCOMPARE(repeated.untexturedMask,cold.untexturedMask);
                QCOMPARE(repeated.color,cold.color); QCOMPARE(repeated.offset,cold.offset);
                QCOMPARE(repeated.flow,cold.flow); QCOMPARE(repeated.opacity,cold.opacity);
            }
        }
    }
    void tipResourceEditsInvalidatePreparedGeometry() {
        BrushSettings brush; brush.pressureSize=false; brush.size=32; brush.tipMask=solidMask(64,64,255);
        const auto before=brushTipMask(brush,{});
        auto other=brush; other.tipMask=solidMask(64,64,0);
        QVERIFY(brushTipMask(other,{})!=before);
        // Editing a shared resource must detach and invalidate its prepared tip,
        // and editing a returned mask must never mutate the cached source.
        auto returned=brushTipMask(brush,{}); returned.fill(0);
        QCOMPARE(brushTipMask(brush,{}),before);
        brush.tipMask.fill(0);
        QCOMPARE(brushTipMask(brush,{}),brushTipMask(other,{}));
        brush.tipMask=solidMask(64,64,255);
        QCOMPARE(brushTipMask(brush,{}),before);
        brush.color=Qt::red; brush.opacity=.2; brush.scatter=8;
        QCOMPARE(brushTipMask(brush,{}),before);
    }
    void maskCoverageTintAndAspect() {
        Document document; document.load(blankProject());
        BrushSettings brush; brush.size = 40; brush.color = QColor("#3a78c2");
        brush.tipMask = solidMask(64, 32);
        for (int y = 0; y < 32; ++y)
            for (int x = 32; x < 64; ++x) brush.tipMask.scanLine(y)[x] = 128;
        tap(document, {100, 100}, brush);
        QCOMPARE(pixel(document, 85, 100), brush.color);
        const auto translucent = pixel(document, 110, 100);
        QVERIFY(qAbs(translucent.alpha() - 128) <= 1);
        QVERIFY(qAbs(translucent.blue() - brush.color.blue()) <= 1);
        QCOMPARE(pixel(document, 100, 112).alpha(), 0);
        QCOMPARE(pixel(document, 121, 100).alpha(), 0);
        QCOMPARE(pixel(document, 83, 92), brush.color);
    }
    void transparentMaskPixelsStayClear() {
        Document document; document.load(blankProject());
        BrushSettings brush; brush.size = 40; brush.tipMask = solidMask(40, 40);
        for (int y = 10; y < 30; ++y)
            for (int x = 10; x < 30; ++x) brush.tipMask.scanLine(y)[x] = 0;
        tap(document, {100, 100}, brush);
        QCOMPARE(pixel(document, 100, 100).alpha(), 0);
        QCOMPARE(pixel(document, 83, 100), brush.color);
    }
    void preparedStampIsBoundedAndPressurePreservesAspect() {
        BrushSettings brush; brush.size = 40; brush.tipMask = solidMask(2048, 1024);
        auto stamp = prepareBrushStamp(brush);
        QCOMPARE(stamp.size(), QSize(40, 20));
        QCOMPARE(stamp.format(), QImage::Format_ARGB32_Premultiplied);
        Document document; document.load(blankProject());
        tap(document, {100, 100}, brush, .25);
        tap(document, {200, 100}, brush, 1);
        QCOMPARE(pixel(document, 100, 100), brush.color);
        QCOMPARE(pixel(document, 107, 100).alpha(), 0);
        QCOMPARE(pixel(document, 100, 104).alpha(), 0);
        QCOMPARE(pixel(document, 217, 107), brush.color);
        brush.pressureSize = false;
        tap(document, {300, 100}, brush, .25);
        QCOMPARE(pixel(document, 317, 107), brush.color);
        brush.size = 500;
        QCOMPARE(prepareBrushStamp(brush).size(), QSize(256, 128));
        brush.size = 1; brush.tipMask = solidMask(2048, 1);
        QVERIFY(!prepareBrushStamp(brush).isNull());
    }
    void opacityAndSampledEraserUndo() {
        Document document; document.load(blankProject());
        BrushSettings brush; brush.size = 40; brush.color = Qt::red;
        brush.tipMask = solidMask(40, 40, 128); brush.opacity = .5;
        tap(document, {100, 100}, brush);
        const auto before = pixel(document, 100, 100);
        QVERIFY(qAbs(before.alpha() - 64) <= 1);
        tap(document, {100, 100}, brush, 1, true);
        QVERIFY(qAbs(pixel(document, 100, 100).alpha() - 48) <= 2);
        QCOMPARE(document.history()->count(), 2);
        document.history()->undo(); QCOMPARE(pixel(document, 100, 100), before);
        document.history()->undo(); QCOMPARE(pixel(document, 100, 100).alpha(), 0);
        document.history()->redo(); QCOMPARE(pixel(document, 100, 100), before);
    }
    void negativeAndPositiveTileEdgesMatchUnsplitDab() {
        BrushSettings brush; brush.size = 48; brush.color = QColor("#3469a4"); brush.opacity = .6;
        brush.tipMask = solidMask(96, 64);
        for (int y = 0; y < brush.tipMask.height(); ++y)
            for (int x = 0; x < brush.tipMask.width(); ++x)
                brush.tipMask.scanLine(y)[x] = uchar((x * 11 + y * 7) % 256);
        Document unsplit, negative, positive;
        unsplit.load(blankProject()); negative.load(blankProject()); positive.load(blankProject());
        tap(unsplit, {199.65, 200.65}, brush, .73);
        tap(negative, {-.35, .65}, brush, .73);
        tap(positive, {255.65, 256.65}, brush, .73);
        for (int y = -24; y <= 24; ++y) for (int x = -24; x <= 24; ++x) {
            QCOMPARE(pixel(negative, x, y), pixel(unsplit, 200 + x, 200 + y));
            QCOMPARE(pixel(positive, 256 + x, 256 + y), pixel(unsplit, 200 + x, 200 + y));
        }
        QCOMPARE(negative.project().shots.first().layers.first().tiles.size(), 4);
        QCOMPARE(positive.project().shots.first().layers.first().tiles.size(), 4);
    }
    void spacingCarriesAcrossInputEvents() {
        BrushSettings brush; brush.size = 32; brush.spacing = 1; brush.tipMask = solidMask(32, 32, 0);
        for (int y = 12; y < 20; ++y)
            for (int x = 12; x < 20; ++x) brush.tipMask.scanLine(y)[x] = 255;
        Document single, segmented; single.load(blankProject()); segmented.load(blankProject());
        QVERIFY(single.beginStroke({100, 100}, 1, brush, false));
        single.continueStroke({196, 100}, 1); single.endStroke();
        QVERIFY(segmented.beginStroke({100, 100}, 1, brush, false));
        for (const int x : {107, 123, 159, 176, 196}) segmented.continueStroke({qreal(x), 100}, 1);
        segmented.endStroke();
        QCOMPARE(segmented.project().shots.first().layers.first().tiles, single.project().shots.first().layers.first().tiles);
        QCOMPARE(pixel(single, 132, 100), brush.color);
        QCOMPARE(pixel(single, 116, 100).alpha(), 0);
        brush.spacing = .1;
        Document dense; dense.load(blankProject());
        QVERIFY(dense.beginStroke({100, 100}, 1, brush, false));
        dense.continueStroke({196, 100}, 1); dense.endStroke();
        QCOMPARE(pixel(dense, 116, 100), brush.color);
    }
    void subDabInputKeepsPressureWithoutArtworkRefresh_data() {
        QTest::addColumn<bool>("vector");
        QTest::addColumn<bool>("eraser");
        QTest::newRow("raster") << false << false;
        QTest::newRow("vector") << true << false;
        QTest::newRow("eraser") << false << true;
    }
    void subDabInputKeepsPressureWithoutArtworkRefresh() {
        QFETCH(bool, vector); QFETCH(bool, eraser);
        auto project = blankProject();
        if (vector) project.shots[0].layers[0].kind = LayerKind::Vector;
        if (eraser) {
            QImage tile(tileSize, tileSize, QImage::Format_ARGB32_Premultiplied);
            tile.fill(QColor("#537ca1"));
            project.shots[0].layers[0].tiles.insert({0, 0}, tile);
        }
        Document document, expected; document.load(project); expected.load(project);
        BrushSettings brush; brush.size = 20; brush.spacing = 1;
        QSignalSpy updates(&document, &Document::strokeUpdated);
        QSignalSpy changes(&document, &Document::changed);
        BrushInput input;
        QVERIFY(document.beginStroke({100, 100}, input, brush, eraser));
        QCOMPARE(updates.count(), 1);
        const auto firstPixels = document.project().shots[0].layers[0].tiles;
        input.pressure = .8; input.tiltX = 10;
        document.continueStroke({105, 100}, input);
        input.pressure = .2; input.tiltX = 20;
        document.continueStroke({110, 100}, input);
        input.pressure = .4; input.tiltX = 30;
        document.continueStroke({110, 100}, input);
        QCOMPARE(updates.count(), 1);
        QCOMPARE(document.project().shots[0].layers[0].tiles, firstPixels);
        input.pressure = 1; input.tiltX = 50;
        document.continueStroke({130, 100}, input);
        QCOMPARE(updates.count(), 2);
        QCOMPARE(changes.count(), 0);
        document.endStroke();
        QCOMPARE(document.history()->count(), 1);
        QVERIFY(changes.count() > 0);
        // The new stamp lies halfway along the most recent input segment,
        // so the stationary pressure update must still affect its coverage.
        const qreal interpolatedPressure = .4 + (1 - .4) * .5;
        tap(expected, {100, 100}, brush, 1, eraser);
        tap(expected, {120, 100}, brush, interpolatedPressure, eraser);
        const auto painted = document.project().shots[0].layers[0].tiles;
        QCOMPARE(painted, expected.project().shots[0].layers[0].tiles);
        if (vector) {
            const auto &objects = document.project().shots[0].layers[0].objects;
            QCOMPARE(objects.size(), 1); QVERIFY(objects[0].brushStroke.has_value());
            const auto &samples = objects[0].brushStroke->samples;
            QCOMPARE(samples.size(), 2);
            QCOMPARE(samples[1].point, QPointF(120, 100));
            QCOMPARE(samples[1].input.pressure, interpolatedPressure);
            QCOMPARE(samples[1].input.tiltX, 40);
        }
        document.history()->undo();
        QCOMPARE(document.project().shots[0].layers[0].tiles, project.shots[0].layers[0].tiles);
        document.history()->redo();
        QCOMPARE(document.project().shots[0].layers[0].tiles, painted);
    }
    void stationaryAirbrushUsesLatestQuietInput() {
        auto project = blankProject(); project.shots[0].layers[0].kind = LayerKind::Vector;
        Document document; document.load(project);
        BrushSettings brush; brush.size = 20; brush.spacing = 1; brush.airbrush = true; brush.opacity = .2;
        QSignalSpy updates(&document, &Document::strokeUpdated);
        QVERIFY(document.beginStroke({100, 100}, 1, brush, false));
        document.continueStroke({100, 100}, .25);
        QCOMPARE(updates.count(), 1);
        document.airbrushTick();
        QCOMPARE(updates.count(), 2);
        document.endStroke();
        const auto &objects = document.project().shots[0].layers[0].objects;
        QCOMPARE(objects.size(), 1); QVERIFY(objects[0].brushStroke.has_value());
        const auto &samples = objects[0].brushStroke->samples;
        QCOMPARE(samples.size(), 2);
        QCOMPARE(samples[1].input.pressure, .25);
        QCOMPARE(samples[1].point, QPointF(100, 100));
    }
    void cancelAndSaveRetainSampledPixels() {
        Document document; document.load(blankProject());
        BrushSettings brush; brush.size = 40; brush.tipMask = solidMask(80, 20); brush.color = Qt::blue;
        tap(document, {256, 100}, brush);
        const auto before = document.project().shots.first().layers.first().tiles;
        QVERIFY(document.beginStroke({100, 200}, 1, brush, false));
        document.continueStroke({300, 200}, 1); document.cancelStroke();
        QCOMPARE(document.project().shots.first().layers.first().tiles, before);
        QCOMPARE(document.history()->count(), 1);
        QTemporaryDir temporary; QString error;
        const auto path = temporary.filePath("sampled.toonproj");
        QVERIFY2(saveProject(document.project(), path, &error), qPrintable(error));
        const auto restored = loadProject(path, &error);
        QVERIFY2(restored.has_value(), qPrintable(error));
        QCOMPARE(restored->shots.first().layers.first().tiles, before);
        brush.spacing = std::numeric_limits<qreal>::quiet_NaN();
        QVERIFY(!document.beginStroke({100, 100}, 1, brush, false));
        QVERIFY(!document.strokeActive());
    }
};

QTEST_GUILESS_MAIN(BrushEngineTest)
#include "test_brush_engine.moc"
