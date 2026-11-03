#include "document.h"
#include "effect_render.h"
#include "sheet.h"
#include <QPainter>
#include <QtTest>
#include <limits>
#include <cmath>
using namespace toon;
namespace {
Project fixture() {
    auto p=Project::create();p.width=512;p.height=256;p.transparent=true;
    p.shots[0].layers.resize(1);auto &layer=p.shots[0].layers[0];
    QImage tile(tileSize,tileSize,QImage::Format_ARGB32_Premultiplied);tile.fill(Qt::transparent);
    {QPainter painter(&tile);painter.fillRect(QRect(20,20,236,80),QColor(160,80,40));}
    layer.tiles.insert({0,0},tile);return p;
}
const RasterLayer &selected(const Document &d){return d.project().shots[0].layers[d.activeLayer()];}
int maskValue(const LayerMask &m,int x,int y){const auto tile=m.tiles.value({int(std::floor(x/qreal(tileSize))),int(std::floor(y/qreal(tileSize)))});return tile.isNull()?m.defaultValue:tile.constScanLine((y%tileSize+tileSize)%tileSize)[(x%tileSize+tileSize)%tileSize];}
}
class EffectsDocumentTests final:public QObject {
    Q_OBJECT
private slots:
    void transactionPreviewCancelNoOpAndHistory(){
        Document d;d.load(fixture());const auto source=selected(d).tiles;const auto original=d.project().toJson();
        QVERIFY(d.beginEffectsEdit());auto fx=selected(d).effects;fx.blurEnabled=true;fx.blurRadius=12;
        QVERIFY(d.previewLayerEffects(fx));QCOMPARE(d.history()->count(),0);QCOMPARE(selected(d).tiles,source);
        d.cancelEffectsEdit();QCOMPARE(d.project().toJson(),original);
        QVERIFY(d.beginEffectsEdit());QVERIFY(d.previewLayerEffects(fx));QVERIFY(d.previewLayerEffects({}));d.commitEffectsEdit();QCOMPARE(d.history()->count(),0);
        QVERIFY(d.setLayerEffects(selected(d).id,fx));QCOMPARE(d.history()->count(),1);QCOMPARE(selected(d).tiles,source);
        d.history()->undo();QCOMPARE(d.project().toJson(),original);d.history()->redo();QCOMPARE(selected(d).effects,fx);
        auto invalid=fx;invalid.blurRadius=std::numeric_limits<qreal>::quiet_NaN();QVERIFY(!d.setLayerEffects(selected(d).id,invalid));QCOMPARE(selected(d).effects,fx);QVERIFY(!d.effectsEditActive());
    }
    void masksAreIndependentFromSelectionAndSource(){
        Document d;d.load(fixture());const auto source=selected(d).tiles;QPainterPath selection;selection.addRect(30,30,60,40);d.setRasterSelection(selection);
        QVERIFY(d.addLayerMask(true));QVERIFY(selected(d).mask);auto mask=*selected(d).mask;
        QCOMPARE(maskValue(mask,50,50),255);QCOMPARE(maskValue(mask,120,50),0);QCOMPARE(selected(d).tiles,source);
        d.clearRasterSelection();QCOMPARE(*selected(d).mask,mask);QVERIFY(d.invertLayerMask());QCOMPARE(maskValue(*selected(d).mask,50,50),0);QCOMPARE(maskValue(*selected(d).mask,120,50),255);
        d.history()->undo();QCOMPARE(*selected(d).mask,mask);QVERIFY(d.setLayerMaskEnabled(false));QVERIFY(!selected(d).mask->enabled);
        QVERIFY(d.fillLayerMask(false));QCOMPARE(selected(d).mask->defaultValue,0);QVERIFY(selected(d).mask->tiles.isEmpty());
        QVERIFY(d.fillLayerMask(true));QCOMPARE(selected(d).mask->defaultValue,255);QVERIFY(d.removeLayerMask());QVERIFY(!selected(d).mask);d.history()->undo();QVERIFY(selected(d).mask);
        QCOMPARE(selected(d).tiles,source);
    }
    void selectionMaskUsesAnimatedLayerCoordinates(){
        auto p=fixture();auto &shot=p.shots[0];const auto id=shot.layers[0].id;
        shot.layerAnimations={{id,{},{{KeyframeProperty::PositionX,{{0,80}}},{KeyframeProperty::ScaleX,{{0,2}}},{KeyframeProperty::ScaleY,{{0,2}}}}, {}}};
        Document d;d.load(p);QPainterPath local;local.addRect(30,30,60,40);d.setRasterSelection(worldLayerTransform(shot,id,0).map(local));
        QVERIFY(d.addLayerMask(true));QCOMPARE(maskValue(*selected(d).mask,50,50),255);QCOMPARE(maskValue(*selected(d).mask,120,50),0);
    }
    void navigationResolvesEffectTransactionBeforeChangingTarget(){
        auto p=fixture();p.shots[0].layers.append(RasterLayer::create("Second"));Document d;d.load(p);
        QVERIFY(d.beginEffectsEdit());LayerEffects fx;fx.glowEnabled=true;fx.glowRadius=20;QVERIFY(d.previewLayerEffects(fx));d.selectLayer(1);
        QVERIFY(!d.effectsEditActive());QCOMPARE(d.activeLayer(),1);QCOMPARE(d.history()->count(),1);QCOMPARE(d.project().shots[0].layers[0].effects,fx);
        QVERIFY(d.beginEffectsEdit());QVERIFY(d.previewLayerEffects(fx));QVERIFY(d.setCurrentFrame(8));QCOMPARE(d.currentFrame(),8);QCOMPARE(d.history()->count(),2);QVERIFY(!d.effectsEditActive());
    }
    void eligibilityAndAdjustmentLayerSafety(){
        Document d;d.load(fixture());const auto art=selected(d).id;QVERIFY(d.addAdjustmentLayer());QVERIFY(selected(d).adjustment);const auto adjustment=selected(d).id;
        QCOMPARE(d.project().shots[0].layers.size(),2);QVERIFY(!layerCanPaint(d.project().shots[0].layers,d.activeLayer()));QVERIFY(!d.beginStroke({30,30},1,BrushSettings{},false));
        LayerEffects fx;fx.blurEnabled=true;QVERIFY(!d.setLayerEffects(adjustment,fx));QVERIFY(d.addLayerMask());
        d.setLayerClipped(true);QVERIFY(!selected(d).clipped);d.setLayerAlphaLocked(true);QVERIFY(!selected(d).alphaLocked);
        d.deleteLayers({art});QCOMPARE(d.project().shots[0].layers.size(),2);
        d.setLayersLocked({adjustment},true);QVERIFY(!d.effectsEditReason().isEmpty());QVERIFY(!d.removeLayerMask());
        d.setLayersLocked({adjustment},false);d.addLayer(true);QVERIFY(!d.effectsEditReason().isEmpty());QVERIFY(!d.addLayerMask());
    }
    void adjustmentInsideDrawingTemplateRemainsValid(){
        Document d;d.load(fixture());QVERIFY(d.addAnimationTrack("Character",DrawingTemplate::Anime));
        QVERIFY(d.addAdjustmentLayer());QVERIFY(selected(d).adjustment);QVERIFY2(d.project().validate().isEmpty(),qPrintable(d.project().validate()));
        const auto id=selected(d).id;QVERIFY(!animationOwner(d.project().shots[0],id).drawingId.isEmpty());
        d.history()->undo();QVERIFY(!selected(d).adjustment);d.history()->redo();QVERIFY(selected(d).adjustment);
    }
    void insertingAdjustmentPreservesExistingClippingRun(){
        auto p=fixture();auto overlay=RasterLayer::create("Clip");overlay.clipped=true;p.shots[0].layers.append(overlay);
        Document d;d.load(p);QVERIFY(d.addAdjustmentLayer());const auto &layers=d.project().shots[0].layers;
        QCOMPARE(layers[1].id,overlay.id);QVERIFY(layers[2].adjustment);QVERIFY(layers[1].clipped);
        QVERIFY2(d.project().validate().isEmpty(),qPrintable(d.project().validate()));
    }
    void duplicateDetachAndUndoPreserveIndependentMaskPixels(){
        Document d;d.load(fixture());QVERIFY(d.addLayerMask());QVERIFY(d.beginEffectsEdit());auto mask=*selected(d).mask;
        QImage tile(tileSize,tileSize,QImage::Format_Grayscale8);tile.fill(64);mask.tiles.insert({0,0},tile);QVERIFY(d.previewLayerMask(mask));d.commitEffectsEdit();
        const auto id=selected(d).id;d.duplicateLayers({id});QCOMPARE(d.project().shots[0].layers.size(),2);
        QVERIFY(d.invertLayerMask());QCOMPARE(maskValue(*selected(d).mask,50,50),191);QCOMPARE(maskValue(*d.project().shots[0].layers[0].mask,50,50),64);
        d.history()->undo();QCOMPARE(maskValue(*selected(d).mask,50,50),64);d.history()->redo();QCOMPARE(maskValue(*selected(d).mask,50,50),191);
    }
    void mergeAndFlattenBakeEffectsIncludingNeighborTilesOnce(){
        auto p=fixture();auto &layer=p.shots[0].layers[0];layer.effects.shadowEnabled=true;layer.effects.shadowRadius=6;layer.effects.shadowOffset={40,0};layer.effects.shadowOpacity=1;
        p.shots[0].layers.append(RasterLayer::create("Upper"));const auto expected=renderMovieFrame(p);QVERIFY(expected.pixelColor(280,50).alpha()>0);
        Document d;d.load(p);d.selectLayer(1);QVERIFY(d.mergeDown());QCOMPARE(d.project().shots[0].layers.size(),1);QCOMPARE(selected(d).effects,LayerEffects{});QVERIFY(!selected(d).mask);
        QCOMPARE(renderMovieFrame(d.project()),expected);d.history()->undo();QCOMPARE(renderMovieFrame(d.project()),expected);
        QVERIFY(d.addAdjustmentLayer());LayerEffects fx;fx.colorEnabled=true;fx.saturation=-100;QVERIFY(d.setLayerEffects(selected(d).id,fx));const auto adjusted=renderMovieFrame(d.project());
        QVERIFY(d.flattenImage());QCOMPARE(d.project().shots[0].layers.size(),1);QVERIFY(!selected(d).adjustment);QCOMPARE(renderMovieFrame(d.project()),adjusted);
    }
};
QTEST_MAIN(EffectsDocumentTests)
#include "effects_document_tests.moc"
