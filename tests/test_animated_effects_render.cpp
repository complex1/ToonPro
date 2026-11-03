#include "animation_render.h"
#include "canvas.h"
#include "document.h"
#include "effect_render.h"
#include "effects_preview_renderer.h"
#include "keyframe_render.h"
#include "mask_tool.h"
#include "sheet.h"
#include "ui_theme.h"
#include <QJsonArray>
#include <QMouseEvent>
#include <QPainter>
#include <QUuid>
#include <QtTest>
#include <cmath>

using namespace toon;
namespace {
QString id(){return QUuid::createUuid().toString(QUuid::WithoutBraces);}
void rectangle(RasterLayer &layer,QRect bounds,QColor color) {
    for(int ty=int(std::floor(bounds.top()/qreal(tileSize)));ty<=int(std::floor(bounds.bottom()/qreal(tileSize)));++ty)
        for(int tx=int(std::floor(bounds.left()/qreal(tileSize)));tx<=int(std::floor(bounds.right()/qreal(tileSize)));++tx) {
            auto image=layer.tiles.value({tx,ty});if(image.isNull()){image=QImage(tileSize,tileSize,QImage::Format_ARGB32_Premultiplied);image.fill(Qt::transparent);}
            QPainter p(&image);p.translate(-tx*tileSize,-ty*tileSize);p.fillRect(bounds,color);p.end();layer.tiles.insert({tx,ty},image);
        }
}
LayerMask windowMask(QRect bounds,int outside=0) {
    RasterLayer source=RasterLayer::create("Coverage");rectangle(source,bounds,outside?Qt::black:Qt::white);
    LayerMask mask;mask.defaultValue=outside;
    for(auto it=source.tiles.cbegin();it!=source.tiles.cend();++it){QImage image(tileSize,tileSize,QImage::Format_Grayscale8);image.fill(outside);
        for(int y=0;y<tileSize;++y)for(int x=0;x<tileSize;++x)if(qAlpha(it->pixel(x,y)))image.scanLine(y)[x]=outside?0:255;
        mask.tiles.insert(it.key(),image);}
    return mask;
}
Project fixture(QSize size={128,128}) {
    auto p=Project::create();p.width=size.width();p.height=size.height();p.transparent=true;
    auto &shot=p.shots[0];shot.durationFrames=24;shot.sheet.overscan=0;shot.layers.resize(1);
    rectangle(shot.layers[0],{0,0,size.width(),size.height()},QColor(120,70,30));return p;
}
PropertyChannel channel(KeyframeProperty property,qreal first,qreal last) {
    const auto mode=keyframePropertyIsDiscrete(property)?KeyframeInterpolation::Hold:KeyframeInterpolation::Linear;
    return {property,{{0,first,mode},{10,last,mode}}};
}
QImage render(const QVector<RasterLayer> &layers,QRect bounds={0,0,512,256},bool smooth=false) {
    QImage result(bounds.size(),QImage::Format_ARGB32_Premultiplied);result.fill(Qt::transparent);
    QPainter p(&result);p.translate(-bounds.topLeft());paintLayers(p,layers,true,nullptr,smooth);return result;
}
EffectsPreviewSnapshot snapshot(const Project &project,int frame,int divisor) {
    EffectsPreviewSnapshot s;s.context=1;s.revision=1;s.project=project;s.shot=project.shots[0];s.frame=frame;
    s.playback=true;s.playbackDivisor=divisor;s.pixelSize={640,600};s.guides=false;s.drawPaperShadow=false;
    prepareEffectsPreviewBackground(s);return s;
}
QColor previewPixel(const EffectsPreviewSnapshot &s,const QImage &image,QPointF point) {
    return image.pixelColor((sheetGeometry(s.project,s.shot).output.topLeft()+point).toPoint());
}
void mouse(Canvas &canvas,QEvent::Type type,QPointF artwork) {
    const auto point=canvas.viewTransform().map(artwork+sheetGeometry(canvas.project(),canvas.shot()).output.topLeft());
    QMouseEvent e(type,point,point,canvas.mapToGlobal(point),Qt::LeftButton,
        type==QEvent::MouseButtonRelease?Qt::NoButton:Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(&canvas,&e);
}
int maskPixel(const LayerMask &mask,int x,int y) {
    const int tx=int(std::floor(x/qreal(tileSize))),ty=int(std::floor(y/qreal(tileSize)));
    const auto tile=mask.tiles.value({tx,ty});return tile.isNull()?mask.defaultValue:tile.constScanLine(y-ty*tileSize)[x-tx*tileSize];
}
}
class AnimatedEffectsRenderTests final:public QObject {
    Q_OBJECT
private slots:
    void initTestCase(){applyTheme();}
    void init(){clearEffectRenderError();clearKeyframeRenderCache();}
    void legacyMaskMetadataStaysCompactAndAdvancedFieldsAreStrict() {
        LayerMask legacy;QVERIFY(!layerMaskHasProperties(legacy));QVERIFY(!layerMaskToJson(legacy).contains("properties"));
        auto mask=windowMask({20,20,30,30});mask.position={12,-7};mask.scale={-2,.5};mask.rotation=45;
        mask.pivot={30,40};mask.opacity=.35;mask.feather=12;mask.inverted=true;
        LayerMask restored;QString error;const auto json=layerMaskToJson(mask);
        QVERIFY2(layerMaskFromJson(json,&restored,&error),qPrintable(error));restored.tiles=mask.tiles;QCOMPARE(restored,mask);
        for(const auto &name:{QString("opacity"),QString("feather"),QString("rotation")}) {
            auto bad=json;auto values=bad["properties"].toObject();values[name]=100000;bad["properties"]=values;
            QVERIFY(!layerMaskFromJson(bad,&restored,&error));
        }
        auto bad=json;auto values=bad["properties"].toObject();values["unknown"]=1;bad["properties"]=values;
        QVERIFY(!layerMaskFromJson(bad,&restored,&error));
        bad=json;values=bad["properties"].toObject();values["scale"]=QJsonArray{1,QString("invalid")};bad["properties"]=values;
        QVERIFY(!layerMaskFromJson(bad,&restored,&error));
    }
    void identityMaskKeepsExactPixelsAndStrengthBypassesOnlyCoverage() {
        auto p=fixture();auto &layer=p.shots[0].layers[0];const auto unmasked=renderMovieFrame(p);
        layer.mask=windowMask({16,16,32,32});const auto source=layer.tiles;const auto mask=*layer.mask;
        auto exact=renderMovieFrame(p);QCOMPARE(exact.pixelColor(24,24),unmasked.pixelColor(24,24));QCOMPARE(exact.pixelColor(80,80).alpha(),0);
        layer.mask->opacity=.5;auto half=renderMovieFrame(p);QCOMPARE(half.pixelColor(80,80).alpha(),128);QCOMPARE(half.pixelColor(24,24),unmasked.pixelColor(24,24));
        layer.mask->opacity=0;QCOMPARE(renderMovieFrame(p),unmasked);QVERIFY(!layerStackHasEffects(p.shots[0].layers));
        layer.mask=mask;layer.mask->inverted=true;auto inverted=renderMovieFrame(p);QCOMPARE(inverted.pixelColor(24,24).alpha(),0);QCOMPARE(inverted.pixelColor(80,80),unmasked.pixelColor(80,80));
        QCOMPARE(layer.tiles,source);QCOMPARE(layer.mask->tiles,mask.tiles);QVERIFY(effectRenderError().isEmpty());
    }
    void maskMovesIndependentlyOfArtworkAndUsesItsOwnPivot() {
        auto p=fixture();auto &shot=p.shots[0];auto &layer=shot.layers[0];layer.mask=windowMask({16,16,16,32});
        layer.mask->pivot={16,16};layer.mask->scale={2,1};layer.mask->rotation=90;layer.mask->position={64,32};
        // Rectangle relative to its mask pivot maps to [48,80] x [48,80].
        auto result=renderMovieFrame(p);QVERIFY(result.pixelColor(60,60).alpha()>250);QCOMPARE(result.pixelColor(24,24).alpha(),0);QCOMPARE(result.pixelColor(90,60).alpha(),0);
        const auto pixels=layer.tiles;const auto maskPixels=layer.mask->tiles;
        shot.layerAnimations={{layer.id,{110,110},{channel(KeyframeProperty::MaskPositionX,0,64)}, {}}};
        result=renderMovieFrame(p,0);QCOMPARE(result.pixelColor(60,60).alpha(),0);
        result=renderMovieFrame(p,10);QVERIFY(result.pixelColor(60,60).alpha()>250);
        QCOMPARE(layer.tiles,pixels);QCOMPARE(layer.mask->tiles,maskPixels);
    }
    void featherCrossesTileEdgesAndPreservesOutsideDefaults() {
        auto layer=RasterLayer::create("Paint");rectangle(layer,{-256,-128,1024,512},Qt::red);
        layer.mask=windowMask({244,20,24,80});layer.mask->feather=18;
        const auto before=*layer.mask;const auto output=render({layer});
        auto shifted=layer;shifted.mask=windowMask({116,20,24,80});shifted.mask->feather=18;
        const auto reference=render({shifted});QCOMPARE(output.copy(160,0,200,150),reference.copy(32,0,200,150));
        QVERIFY(output.pixelColor(240,50).alpha()>0);QVERIFY(output.pixelColor(240,50).alpha()<255);
        QCOMPARE(output.pixelColor(40,50).alpha(),0);QCOMPARE(*layer.mask,before);
        layer.mask=windowMask({-12,20,24,80},255);layer.mask->feather=18;
        auto negative=render({layer},{-128,0,256,128});QCOMPARE(negative.pixelColor(20,50).alpha(),255);
        QVERIFY(negative.pixelColor(128,50).alpha()<255);QVERIFY(negative.pixelColor(128,50).alpha()>0);
        QVERIFY(effectRenderError().isEmpty());
    }
    void collapsedAndTinyMaskScalesRemainBoundedAndUseOutsideCoverage() {
        auto p=fixture();auto &layer=p.shots[0].layers[0];layer.mask=windowMask({16,16,32,32});layer.mask->feather=128;
        layer.mask->scale={0,1};QCOMPARE(renderMovieFrame(p).pixelColor(24,24).alpha(),0);
        layer.mask->defaultValue=255;QCOMPARE(renderMovieFrame(p).pixelColor(24,24).alpha(),255);
        layer.mask->inverted=true;QCOMPARE(renderMovieFrame(p).pixelColor(24,24).alpha(),0);
        layer.mask->opacity=.5;QCOMPARE(renderMovieFrame(p).pixelColor(24,24).alpha(),128);
        layer.mask->scale={1e-12,-1e-12};QCOMPARE(renderMovieFrame(p).pixelColor(80,80).alpha(),128);
        QVERIFY(effectRenderError().isEmpty());
    }
    void everyNumericEffectChannelMatchesFrozenMidpointExport() {
        auto p=fixture();auto &shot=p.shots[0];auto &layer=shot.layers[0];layer.tiles.clear();rectangle(layer,{32,32,30,30},QColor(120,70,30));
        layer.effects.blurEnabled=layer.effects.shadowEnabled=layer.effects.glowEnabled=layer.effects.colorEnabled=true;
        shot.layerAnimations={{layer.id,{}, {
            channel(KeyframeProperty::BlurRadius,0,8),channel(KeyframeProperty::ShadowRadius,0,12),
            channel(KeyframeProperty::ShadowOffsetX,-8,20),channel(KeyframeProperty::ShadowOffsetY,0,10),
            channel(KeyframeProperty::ShadowOpacity,0,1),channel(KeyframeProperty::GlowRadius,0,10),
            channel(KeyframeProperty::GlowOpacity,0,1),channel(KeyframeProperty::Exposure,0,2),
            channel(KeyframeProperty::Contrast,0,30),channel(KeyframeProperty::Saturation,0,-50),
            channel(KeyframeProperty::Hue,0,60)}, {}}};
        QVERIFY2(p.validate().isEmpty(),qPrintable(p.validate()));const auto source=layer;
        auto frozen=p;frozen.shots[0].layerAnimations.clear();auto &fx=frozen.shots[0].layers[0].effects;
        fx.blurRadius=4;fx.shadowRadius=6;fx.shadowOffset={6,5};fx.shadowOpacity=.5;fx.glowRadius=5;fx.glowOpacity=.5;
        fx.exposure=1;fx.contrast=15;fx.saturation=-25;fx.hue=30;
        QCOMPARE(renderMovieFrame(p,5),renderMovieFrame(frozen,0));QCOMPARE(layer.tiles,source.tiles);QCOMPARE(layer.effects,source.effects);
        QVERIFY(renderMovieFrame(p,0)!=renderMovieFrame(p,10));
    }
    void heldEnableKeysPrepareVectorHalosBeforeRasterization() {
        auto p=fixture();auto &shot=p.shots[0];auto &layer=shot.layers[0];layer.tiles.clear();layer.kind=LayerKind::Vector;
        auto object=EditableObject::create();object.closed=true;object.stroke=QColor();object.strokeWidth=0;object.fill=Qt::red;
        object.nodes={BezierNode::corner({-8,40}),BezierNode::corner({-4,40}),BezierNode::corner({-4,64}),BezierNode::corner({-8,64})};
        layer.objects={object};layer.effects.glowRadius=16;layer.effects.glowOpacity=1;layer.effects.glowColor=Qt::green;
        shot.layerAnimations={{layer.id,{}, {channel(KeyframeProperty::GlowEnabled,0,1)}, {}}};
        QVERIFY2(p.validate().isEmpty(),qPrintable(p.validate()));QCOMPARE(renderMovieFrame(p,9).pixelColor(1,50).alpha(),0);
        QVERIFY(renderMovieFrame(p,10).pixelColor(1,50).alpha()>0);QVERIFY(layer.tiles.isEmpty());QVERIFY(effectRenderError().isEmpty());
    }
    void adjustmentUsesEvaluatedMaskPositionAndStrength() {
        auto p=fixture();auto &shot=p.shots[0];auto adjustment=RasterLayer::create("Color model");adjustment.adjustment=true;
        adjustment.effects.colorEnabled=true;adjustment.effects.exposure=2;adjustment.mask=windowMask({0,0,64,128});shot.layers.append(adjustment);
        shot.layerAnimations={{adjustment.id,{}, {channel(KeyframeProperty::MaskPositionX,0,64),channel(KeyframeProperty::MaskOpacity,1,.5)}, {}}};
        const auto first=renderMovieFrame(p,0),last=renderMovieFrame(p,10);QCOMPARE(first.pixelColor(100,64),QColor(120,70,30));
        QVERIFY(first.pixelColor(20,64).red()>first.pixelColor(100,64).red());
        QVERIFY(last.pixelColor(20,64).red()>first.pixelColor(100,64).red());
        QVERIFY(last.pixelColor(20,64).red()<last.pixelColor(100,64).red());
        QCOMPARE(last.pixelColor(20,64).alpha(),255);QVERIFY(effectRenderError().isEmpty());
    }
    void nestedSymbolsStayInSyncAtEveryPlaybackQualityAndSameFrameEdit() {
        auto content=fixture();auto &source=content.shots[0];source.durationFrames=11;source.layers[0].mask=windowMask({0,0,64,128});
        source.layers[0].effects.colorEnabled=true;
        source.layerAnimations={{source.layers[0].id,{}, {channel(KeyframeProperty::MaskPositionX,0,64),channel(KeyframeProperty::Exposure,0,1)}, {}}};
        SymbolDefinition inner{id(),"Animated mask",source};auto middle=fixture().shots[0];middle.layers.clear();
        auto child=RasterLayer::create("Inner");child.kind=LayerKind::Symbol;child.symbol=SymbolInstance{inner.id};child.symbol->playback=SymbolPlayback::Once;middle.layers={child};
        SymbolDefinition outer{id(),"Nested",middle};auto p=fixture();p.symbols={inner,outer};auto &instance=p.shots[0].layers[0];instance.tiles.clear();
        instance.kind=LayerKind::Symbol;instance.symbol=SymbolInstance{outer.id};instance.symbol->startFrame=4;
        QVERIFY2(p.validate().isEmpty(),qPrintable(p.validate()));const auto original=p.toJson();
        for(int divisor:{1,2,4,8})for(int frame:{4,14}) {
            auto s=snapshot(p,frame,divisor);auto result=renderEffectsPreview(s,std::make_shared<std::atomic_bool>(false));
            QVERIFY2(result.error.isEmpty(),qPrintable(result.error));QCOMPARE(result.sourceScale,1./divisor);QVERIFY(result.playbackFallbackReason.isEmpty());
            const QPoint point(frame==4?16:96,64);QCOMPARE(previewPixel(s,result.image,point),renderMovieFrame(p,frame).pixelColor(point));
            s.playback=false;auto paused=renderEffectsPreview(s,std::make_shared<std::atomic_bool>(false));QCOMPARE(paused.sourceScale,1.);
            QCOMPARE(previewPixel(s,paused.image,point),renderMovieFrame(p,frame).pixelColor(point));
        }
        auto a=snapshot(p,14,4);const auto before=renderEffectsPreview(a,std::make_shared<std::atomic_bool>(false));
        auto changed=p;changed.symbols[0].content.layers[0].mask->inverted=true;
        auto b=snapshot(changed,14,4);const auto after=renderEffectsPreview(b,std::make_shared<std::atomic_bool>(false));QVERIFY(after.image!=before.image);
        QCOMPARE(renderEffectsPreview(a,std::make_shared<std::atomic_bool>(false)).image,before.image);QCOMPARE(p.toJson(),original);
    }
    void animatedDraftRefinesToExactAndNeverChangesSource() {
        auto p=fixture();p.width=3840;p.height=2160;auto &shot=p.shots[0];auto &layer=shot.layers[0];
        layer.mask=windowMask({0,0,64,128});layer.effects.colorEnabled=true;
        shot.layerAnimations={{layer.id,{}, {channel(KeyframeProperty::MaskPositionX,0,64),channel(KeyframeProperty::Exposure,0,1)}, {}}};
        const auto original=p.toJson();auto s=snapshot(p,10,1);s.playback=false;s.quality=EffectsPreviewQuality::Draft;
        auto draft=renderEffectsPreview(s,std::make_shared<std::atomic_bool>(false));
        QVERIFY2(draft.error.isEmpty(),qPrintable(draft.error));QCOMPARE(draft.sourceScale,.125);QVERIFY(!draft.playback);
        s.quality=EffectsPreviewQuality::Exact;const auto exact=renderEffectsPreview(s,std::make_shared<std::atomic_bool>(false));
        QVERIFY2(exact.error.isEmpty(),qPrintable(exact.error));QCOMPARE(exact.sourceScale,1.);
        QCOMPARE(previewPixel(s,draft.image,{96,64}),previewPixel(s,exact.image,{96,64}));
        QCOMPARE(previewPixel(s,draft.image,{16,64}),previewPixel(s,exact.image,{16,64}));
        QCOMPARE(p.toJson(),original);
    }
    void paintMapsThroughAnimatedMaskAndLayerWithoutBakingKeys() {
        auto p=fixture({256,192});auto &shot=p.shots[0];auto &layer=shot.layers[0];layer.mask=LayerMask{};
        shot.layerAnimations={{layer.id,{}, {channel(KeyframeProperty::PositionX,20,20),channel(KeyframeProperty::MaskPositionX,50,50),
            channel(KeyframeProperty::MaskScaleX,2,2),channel(KeyframeProperty::MaskInverted,1,1)}, {}}};
        Canvas canvas;canvas.setProject(p);canvas.resize(900,720);canvas.show();canvas.fitSheet();QCoreApplication::processEvents();
        auto *tool=canvas.maskTool();tool->setSize(16);tool->setHardness(1);tool->setRevealing(true);tool->setEnabled(true);QVERIFY(tool->enabled());
        const auto keys=canvas.shot().layerAnimations;const auto source=canvas.shot().layers[0];
        // Local mask (30,40) is drawn at artwork (20 + 50 + 2*30, 40).
        mouse(canvas,QEvent::MouseButtonPress,{130,40});mouse(canvas,QEvent::MouseButtonRelease,{130,40});
        QCOMPARE(maskPixel(*canvas.shot().layers[0].mask,30,40),0);QCOMPARE(maskPixel(*canvas.shot().layers[0].mask,130,40),255);
        QCOMPARE(canvas.shot().layerAnimations,keys);QCOMPARE(canvas.shot().layers[0].tiles,source.tiles);
        QCOMPARE(canvas.shot().layers[0].mask->position,source.mask->position);QCOMPARE(canvas.shot().layers[0].mask->inverted,source.mask->inverted);
        QCOMPARE(canvas.document()->history()->count(),1);canvas.document()->history()->undo();QCOMPARE(canvas.shot().layers[0].mask,source.mask);
    }
    void effectiveMaskRestrictionsBlockInvisibleOrSingularPaint() {
        for(const auto property:{KeyframeProperty::MaskEnabled,KeyframeProperty::MaskOpacity,KeyframeProperty::MaskScaleX}) {
            auto p=fixture();auto &shot=p.shots[0];shot.layers[0].mask=LayerMask{};
            shot.layerAnimations={{shot.layers[0].id,{}, {channel(property,0,0)}, {}}};
            Canvas canvas;canvas.setProject(p);auto *tool=canvas.maskTool();QVERIFY(!tool->unavailableReason().isEmpty());tool->setEnabled(true);QVERIFY(!tool->enabled());
            QCOMPARE(canvas.document()->history()->count(),0);
        }
    }
};
QTEST_MAIN(AnimatedEffectsRenderTests)
#include "test_animated_effects_render.moc"
