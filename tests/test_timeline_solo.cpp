#include "animation_test_fixture.h"
#include "animation_render.h"
#include "canvas.h"
#include "drawing_tool_controller.h"
#include "effects_preview_renderer.h"
#include "raster_tools.h"
#include "timeline_solo.h"
#include "ui_theme.h"
#include <QPainter>
#include <QtTest>

using namespace toon;
namespace {
RasterLayer painted(const QString &name, QColor color, const QString &parent={}) {
    auto layer=RasterLayer::create(name); layer.parentId=parent;
    QImage tile(tileSize,tileSize,QImage::Format_ARGB32_Premultiplied); tile.fill(color); layer.tiles.insert({0,0},tile); return layer;
}
Project fixture(bool effects=false) {
    auto p=Project::create(); p.width=p.height=128; p.transparent=true;
    auto folder=RasterLayer::create("Character"); folder.group=true;
    auto base=RasterLayer::create("Clipping base"); base.group=true; base.parentId=folder.id;
    auto ink=painted("Ink",Qt::red,base.id);
    auto overlay=painted("Color",Qt::blue,folder.id); overlay.clipped=true;
    if (effects) { overlay.effects.blurEnabled=true; overlay.effects.blurRadius=3; }
    p.shots[0].layers={painted("Background",Qt::green),folder,base,ink,overlay,
        painted("Other character part",Qt::magenta,folder.id),painted("Foreground",Qt::yellow)};
    return p;
}
QString id(const Shot &shot,const QString &name) { for (const auto &layer:shot.layers) if (layer.name==name) return layer.id; return {}; }
const RasterLayer &named(const Shot &shot,const QString &name) {
    for (const auto &layer:shot.layers) if (layer.name==name) return layer;
    qFatal("Missing test layer");
}
QImage imageFor(const QWidget &widget) {
    const qreal ratio=widget.devicePixelRatioF();
    QImage image(QSize(qCeil(widget.width()*ratio),qCeil(widget.height()*ratio)),QImage::Format_ARGB32_Premultiplied);
    image.setDevicePixelRatio(ratio); image.setDotsPerMeterX(qRound(widget.logicalDpiX()/.0254)); image.setDotsPerMeterY(qRound(widget.logicalDpiY()/.0254)); return image;
}
QImage reference(const Canvas &canvas) {
    auto image=imageFor(canvas); image.fill(QColor(ui::canvas)); QPainter p(&image); p.setClipRegion(QRegion(canvas.rect())); p.setTransform(canvas.viewTransform());
    p.fillRect(canvas.paperRect().translated(8,10),QColor(0,0,0,65));
    paintSheet(p,canvas.project(),canvas.displayShot(),true,nullptr,canvas.canvasSmoothing(),true,canvas.document()->currentFrame(),canvas.cameraView()); return image;
}
QImage reference(const Navigator &navigator,const Canvas &canvas) {
    auto image=imageFor(navigator); image.fill(QColor(ui::base)); QPainter p(&image); p.setTransform(navigator.miniatureTransform());
    paintSheet(p,canvas.project(),canvas.displayShot(),true,nullptr,canvas.canvasSmoothing(),true,canvas.document()->currentFrame(),canvas.cameraView());
    QPen pen(QColor(ui::accent),2); pen.setCosmetic(true); p.setPen(pen); QColor tint(ui::accent); tint.setAlpha(22); p.setBrush(tint); p.drawPolygon(canvas.visiblePolygon()); return image;
}
QImage grab(QWidget &widget) { QEvent leave(QEvent::Leave); QApplication::sendEvent(&widget,&leave); return widget.grab().toImage().convertToFormat(QImage::Format_ARGB32_Premultiplied); }
bool exact(QWidget &widget) {
    widget.repaint(); return QTest::qWaitFor([&] { return !widget.property("effectsPreviewPending").toBool(); },15000);
}
void show(Canvas &canvas,Navigator &navigator) {
    canvas.resize(700,560); navigator.resize(270,240); canvas.show(); navigator.show(); QCoreApplication::processEvents(); canvas.fitSheet();
}
}
class TimelineSoloTests final:public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { applyTheme(); QVERIFY2(fixture().validate().isEmpty(),qPrintable(fixture().validate())); }
    void selectedChildrenKeepAncestorsAndClippingBaseSubtree() {
        const auto p=fixture(); const auto &shot=p.shots[0];
        const auto kept=timelineSoloKeptLayerIds(shot.layers,{id(shot,"Color")});
        for (const auto &name:{"Character","Clipping base","Ink","Color"}) QVERIFY(kept.contains(id(shot,name)));
        for (const auto &name:{"Background","Other character part","Foreground"}) QVERIFY(!kept.contains(id(shot,name)));
        const auto whole=timelineSoloKeptLayerIds(shot.layers,{id(shot,"Character")});
        QVERIFY(whole.contains(id(shot,"Other character part"))); QVERIFY(!whole.contains(id(shot,"Foreground")));
        auto filtered=shot; filtered.layers=timelineSoloPreviewLayers(shot.layers,kept);
        Document doc; doc.load(p); QCOMPARE(RasterTools::sample(&doc,{64,64},RasterSampleSource::AllLayers,{},&filtered),QColor(Qt::blue));
        QCOMPARE(RasterTools::sample(&doc,{64,64}),QColor(Qt::yellow));
    }
    void storedHiddenLayersStayHiddenAndAdjustmentKeepsBackdrop() {
        auto p=fixture(); auto &shot=p.shots[0];
        for (auto &layer:shot.layers) if (layer.name=="Clipping base") layer.visible=false;
        const auto preview=timelineSoloPreviewLayers(shot.layers,timelineSoloKeptLayerIds(shot.layers,{id(shot,"Color")}));
        for (int i=0;i<preview.size();++i) if (!shot.layers[i].visible) QVERIFY(!preview[i].visible);
        auto adjustment=RasterLayer::create("Correction"); adjustment.adjustment=true;
        shot.layers.append(adjustment);
        const auto backdrop=timelineSoloKeptLayerIds(shot.layers,{adjustment.id});
        QCOMPARE(backdrop.size(),shot.layers.size());
        QCOMPARE(timelineSoloKeptLayerIds(shot.layers,{"missing"}).size(),shot.layers.size());
    }
    void soloIsCachedViewStateOutsideProjectHistoryAndExport() {
        Canvas canvas; const auto p=fixture(); canvas.setProject(p); const auto before=canvas.project().toJson(); const auto exported=renderMovieFrame(canvas.project(),0);
        const auto color=id(canvas.shot(),"Color"); QSignalSpy changes(&canvas,&Canvas::timelineSoloChanged);
        QVERIFY(canvas.setTimelineSoloLayers({color})); QCOMPARE(changes.size(),1);
        const auto first=canvas.displayShot(),second=canvas.displayShot(); QCOMPARE(first.layers.constData(),second.layers.constData());
        QVERIFY(!named(first,"Foreground").visible); QVERIFY(named(first,"Color").visible);
        QVERIFY(canvas.setTimelineSoloLayers({color})); QCOMPARE(changes.size(),1);
        QVERIFY(!canvas.setTimelineSoloLayers({"missing"})); QCOMPARE(canvas.timelineSoloLayers(),QSet<QString>{color});
        QCOMPARE(canvas.project().toJson(),before); QCOMPARE(canvas.document()->history()->count(),0); QVERIFY(canvas.document()->history()->isClean());
        QCOMPARE(renderMovieFrame(canvas.project(),0),exported);
        QVERIFY(canvas.setTimelineSoloLayers({})); QVERIFY(named(canvas.displayShot(),"Foreground").visible); QCOMPARE(changes.size(),2);
    }
    void reloadShotChangesAndDeletedIdsClearIsolation() {
        auto p=fixture(); auto other=Project::create(); p.shots.append(other.shots[0]); Canvas canvas; canvas.setProject(p);
        const auto root=id(canvas.shot(),"Character"); QVERIFY(canvas.setTimelineSoloLayers({root}));
        QVERIFY(canvas.document()->selectShot(p.shots[1].id)); QVERIFY(canvas.timelineSoloLayers().isEmpty());
        QVERIFY(canvas.document()->selectShot(p.shots[0].id)); QVERIFY(canvas.setTimelineSoloLayers({root}));
        canvas.document()->load(p); QVERIFY(canvas.timelineSoloLayers().isEmpty());
        const auto foreground=id(canvas.shot(),"Foreground"); QVERIFY(canvas.setTimelineSoloLayers({foreground}));
        canvas.document()->deleteLayers({foreground}); QVERIFY(canvas.timelineSoloLayers().isEmpty());
        QVERIFY(named(canvas.displayShot(),"Background").visible);
        canvas.document()->history()->undo(); QVERIFY(canvas.timelineSoloLayers().isEmpty()); QVERIFY(named(canvas.displayShot(),"Foreground").visible);
    }
    void hiddenActiveArtworkCannotBePaintedAndGesturesVetoSoloChange() {
        Canvas canvas; canvas.setProject(fixture()); const auto color=id(canvas.shot(),"Color");
        QVERIFY(canvas.setTimelineSoloLayers({color}));
        canvas.document()->selectLayer(animation_fixture::layerIndex(canvas.shot(),id(canvas.shot(),"Foreground")));
        for (const auto tool:{Canvas::Tool::Brush,Canvas::Tool::Eraser,Canvas::Tool::Retouch,Canvas::Tool::Transform}) {
            canvas.setTool(tool); QVERIFY(canvas.toolBlockReason().contains("Solo"));
        }
        canvas.setTool(Canvas::Tool::Hand); QVERIFY(canvas.toolBlockReason().isEmpty());
        canvas.setTool(Canvas::Tool::Eyedropper); QVERIFY(canvas.toolBlockReason().isEmpty());
        QVERIFY(canvas.setTimelineSoloLayers({})); canvas.setTool(Canvas::Tool::Brush);
        QVERIFY(canvas.document()->beginStroke({25,25},1,canvas.brush,false));
        QVERIFY(!canvas.setTimelineSoloLayers({color})); QVERIFY(canvas.timelineSoloLayers().isEmpty()); canvas.document()->cancelStroke();
    }
    void eyedropperAndTemporaryAltSampleDisplayedComposite() {
        Canvas canvas; canvas.setProject(fixture()); QVERIFY(canvas.setTimelineSoloLayers({id(canvas.shot(),"Color")}));
        const auto point=canvas.viewTransform().map(sheetGeometry(canvas.project(),canvas.shot()).output.topLeft()+QPointF(64,64));
        canvas.setTool(Canvas::Tool::Eyedropper); QVERIFY(canvas.drawingTools()->press(point,Qt::NoModifier,1)); QCOMPARE(canvas.brush.color,QColor(Qt::blue));
        canvas.setBrushColor(Qt::black); canvas.setTool(Canvas::Tool::Brush);
        QVERIFY(canvas.drawingTools()->press(point,Qt::AltModifier,1)); QCOMPARE(canvas.brush.color,QColor(Qt::blue));
        // Explicit raw-layer sampling deliberately remains independent of Solo.
        QCOMPARE(RasterTools::sample(canvas.document(),{64,64},RasterSampleSource::ReferenceLayer,id(canvas.shot(),"Foreground")),QColor(Qt::yellow));
        QCOMPARE(canvas.document()->history()->count(),0);
    }
    void canvasAndNavigatorRefreshForBothRenderingPaths_data() { QTest::addColumn<bool>("effects"); QTest::newRow("direct")<<false; QTest::newRow("worker")<<true; }
    void canvasAndNavigatorRefreshForBothRenderingPaths() {
        QFETCH(bool,effects); Canvas canvas; canvas.setProject(fixture(effects)); Navigator navigator(&canvas); show(canvas,navigator);
        QVERIFY(exact(canvas)); QVERIFY(exact(navigator)); const auto before=grab(canvas);
        QVERIFY(canvas.setTimelineSoloLayers({id(canvas.shot(),"Color")}));
        QVERIFY(exact(canvas)); QVERIFY(exact(navigator)); QCOMPARE(grab(canvas),reference(canvas)); QCOMPARE(grab(navigator),reference(navigator,canvas)); QVERIFY(grab(canvas)!=before);
        QVERIFY(canvas.setTimelineSoloLayers({})); QVERIFY(exact(canvas)); QVERIFY(exact(navigator)); QCOMPARE(grab(canvas),before); QCOMPARE(grab(navigator),reference(navigator,canvas));
    }
    void reducedPlaybackAndExactPauseUseSameIsolation() {
        Canvas canvas; canvas.setProject(fixture(true)); Navigator navigator(&canvas); show(canvas,navigator); QVERIFY(exact(canvas)); QVERIFY(exact(navigator));
        const auto original=canvas.project().toJson(); const auto exported=renderMovieFrame(canvas.project(),0);
        for (int divisor:{1,2,4,8}) {
            canvas.setPlaybackPreviewDivisor(divisor); canvas.setAnimationPlayback(true);
            QVERIFY(canvas.setTimelineSoloLayers({id(canvas.shot(),"Color")})); canvas.repaint(); navigator.repaint();
            QTRY_COMPARE_WITH_TIMEOUT(canvas.property("effectsPreviewSourceScale").toDouble(),1./divisor,15000);
            QTRY_COMPARE_WITH_TIMEOUT(navigator.property("effectsPreviewSourceScale").toDouble(),1./divisor,15000);
            canvas.setAnimationPlayback(false); QVERIFY(exact(canvas)); QVERIFY(exact(navigator));
            QCOMPARE(grab(canvas),reference(canvas)); QCOMPARE(grab(navigator),reference(navigator,canvas));
            QVERIFY(canvas.setTimelineSoloLayers({})); QVERIFY(exact(canvas)); QVERIFY(exact(navigator));
        }
        QCOMPARE(canvas.project().toJson(),original); QCOMPARE(renderMovieFrame(canvas.project(),0),exported); QCOMPARE(canvas.document()->history()->count(),0);
    }
    void hiddenTracksDoNotContributeOnionGhosts() {
        auto p=animation_fixture::project(); const auto staticId=p.shots[0].layers[0].id;
        Canvas canvas; canvas.setProject(p); QVERIFY(canvas.document()->selectAnimationTrack(p.shots[0].animationTracks[0].id)); QVERIFY(canvas.document()->setCurrentFrame(3));
        canvas.resize(700,560); canvas.show(); canvas.fitSheet(); canvas.setOnionSkin(true);
        QVERIFY(canvas.setTimelineSoloLayers({staticId}));
        QVERIFY(onionSkinSamples(canvas.displayShot(),3,p.shots[0].animationTracks[0].id,1,2).isEmpty());
        QCOMPARE(grab(canvas),reference(canvas));
        canvas.setOnionSkin(false); QCOMPARE(grab(canvas),reference(canvas));
    }
};
QTEST_MAIN(TimelineSoloTests)
#include "test_timeline_solo.moc"
