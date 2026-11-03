#include "canvas.h"
#include "document.h"
#include "effect_render.h"
#include "effects_preview_renderer.h"
#include "ui_theme.h"
#include <QPainter>
#include <QSignalSpy>
#include <QUuid>
#include <QtTest>
#include <cstring>

using namespace toon;
namespace {
QString id(){return QUuid::createUuid().toString(QUuid::WithoutBraces);}
Project fixture(int feature,bool nested=false) {
    auto project=Project::create();project.width=512;project.height=384;project.transparent=true;
    auto &shot=project.shots[0];shot.sheet.overscan=0;shot.durationFrames=24;shot.layers.resize(1);
    auto &layer=shot.layers[0];layer.tiles.clear();
    for(int y=0;y<2;++y)for(int x=0;x<2;++x) {
        QImage image(tileSize,tileSize,QImage::Format_ARGB32_Premultiplied);image.fill(Qt::transparent);
        QPainter painter(&image);painter.translate(-x*tileSize,-y*tileSize);
        painter.fillRect(QRect(40,40,410,280),QColor(152,65,31));
        painter.fillRect(QRect(120,95,120,135),QColor(47,125,172));painter.end();layer.tiles.insert({x,y},image);
    }
    KeyframeProperty property=KeyframeProperty::BlurEnabled;
    layer.effects.blurRadius=18;
    if(feature==1){layer.effects.enabled=false;layer.effects.blurEnabled=true;property=KeyframeProperty::EffectsEnabled;}
    if(feature==2){
        property=KeyframeProperty::MaskEnabled;layer.mask=LayerMask{};layer.mask->enabled=false;layer.mask->defaultValue=0;
        for(int y=0;y<2;++y){QImage image(tileSize,tileSize,QImage::Format_Grayscale8);image.fill(0);
            for(int row=0;row<tileSize;++row)std::memset(image.scanLine(row),255,200);
            layer.mask->tiles.insert({0,y},image);}
    }
    shot.layerAnimations={{layer.id,{},{{property,{{0,0,KeyframeInterpolation::Hold},{10,1,KeyframeInterpolation::Hold}}}}, {}}};
    if(nested) {
        auto innerShot=shot;innerShot.id=id();SymbolDefinition inner{id(),"Animated source",innerShot};
        auto middle=Project::create().shots[0];middle.durationFrames=24;
        auto child=RasterLayer::create("Inner instance");child.kind=LayerKind::Symbol;child.symbol=SymbolInstance{inner.id};
        middle.layers={child};SymbolDefinition outer{id(),"Nested source",middle};
        auto instance=RasterLayer::create("Outer instance");instance.kind=LayerKind::Symbol;instance.symbol=SymbolInstance{outer.id};
        shot.layers={instance};shot.layerAnimations.clear();project.symbols={inner,outer};
    }
    return project;
}
void leave(QWidget &widget){QEvent event(QEvent::Leave);QApplication::sendEvent(&widget,&event);}
void show(Canvas &canvas,Navigator &navigator) {
    canvas.resize(800,620);canvas.show();navigator.resize(280,240);navigator.show();
    QCoreApplication::processEvents();canvas.fitSheet();leave(canvas);leave(navigator);
}
bool exact(QWidget &widget) {
    widget.repaint();
    return QTest::qWaitFor([&]{return widget.property("effectsPreviewPublishedRevision").toULongLong()>0
        &&!widget.property("effectsPreviewPending").toBool()&&widget.property("effectsPreviewExact").toBool()
        &&widget.property("effectsPreviewSourceScale").toDouble()==1.;},30000);
}
QImage imageFor(const QWidget &widget) {
    const auto ratio=widget.devicePixelRatioF();QImage image(QSize(qCeil(widget.width()*ratio),qCeil(widget.height()*ratio)),QImage::Format_ARGB32_Premultiplied);
    image.setDevicePixelRatio(ratio);image.setDotsPerMeterX(qRound(widget.logicalDpiX()/.0254));image.setDotsPerMeterY(qRound(widget.logicalDpiY()/.0254));return image;
}
QImage canvasReference(const Canvas &canvas) {
    auto image=imageFor(canvas);image.fill(QColor(ui::canvas));QPainter painter(&image);
    painter.setClipRegion(QRegion(canvas.rect()));painter.setTransform(canvas.viewTransform());
    painter.fillRect(canvas.paperRect().translated(8,10),QColor(0,0,0,65));
    paintSheet(painter,canvas.project(),canvas.displayShot(),true,nullptr,canvas.canvasSmoothing(),true,canvas.document()->currentFrame(),canvas.cameraView());return image;
}
QImage navigatorReference(const Navigator &navigator,const Canvas &canvas) {
    auto image=imageFor(navigator);image.fill(QColor(ui::base));QPainter painter(&image);painter.setTransform(navigator.miniatureTransform());
    paintSheet(painter,canvas.project(),canvas.displayShot(),true,nullptr,canvas.canvasSmoothing(),true,canvas.document()->currentFrame(),canvas.cameraView());
    QPen pen(QColor(ui::accent),2);pen.setCosmetic(true);painter.setPen(pen);auto tint=QColor(ui::accent);tint.setAlpha(22);
    painter.setBrush(tint);painter.drawPolygon(canvas.visiblePolygon());return image;
}
QImage grab(QWidget &widget){leave(widget);return widget.grab().toImage().convertToFormat(QImage::Format_ARGB32_Premultiplied);}
bool reducedDraft(const QSignalSpy &results) {
    for(const auto &arguments:results){const auto result=qvariant_cast<EffectsPreviewResult>(arguments[0]);
        if(result.quality==EffectsPreviewQuality::Draft&&result.sourceScale==.125&&!result.cancelled&&result.error.isEmpty())return true;}
    return false;
}
void verifyResults(const QSignalSpy &results,int frame) {
    QVERIFY(!results.isEmpty());const auto last=qvariant_cast<EffectsPreviewResult>(results.last()[0]);
    QVERIFY2(last.error.isEmpty(),qPrintable(last.error));QVERIFY(!last.cancelled);QVERIFY(!last.playback);
    QCOMPARE(last.frame,frame);QCOMPARE(last.quality,EffectsPreviewQuality::Exact);QCOMPARE(last.sourceScale,1.);
}
}
class AnimatedEffectsCanvasTests final:public QObject {
    Q_OBJECT
private slots:
    void initTestCase(){applyTheme();}
    void heldEnablesUseWorkersEvenWhenEveryStaticBaseIsDisabled_data() {
        QTest::addColumn<int>("feature");QTest::addColumn<bool>("nested");
        for(bool nested:{false,true})for(int feature=0;feature<3;++feature)
            QTest::newRow(qPrintable(QString("%1-%2").arg(nested?"nested-symbol":"layer").arg(feature)))<<feature<<nested;
    }
    void heldEnablesUseWorkersEvenWhenEveryStaticBaseIsDisabled() {
        QFETCH(int,feature);QFETCH(bool,nested);const auto project=fixture(feature,nested);
        QVERIFY2(project.validate().isEmpty(),qPrintable(project.validate()));
        for(const auto *shot:projectArtworkShots(project))QVERIFY(!layerStackHasEffects(shot->layers));
        Canvas canvas;canvas.setProject(project);Navigator navigator(&canvas);
        auto *canvasRenderer=canvas.findChild<EffectsPreviewRenderer *>();auto *navigatorRenderer=navigator.findChild<EffectsPreviewRenderer *>();
        QVERIFY(canvasRenderer&&navigatorRenderer);QSignalSpy canvasResults(canvasRenderer,&EffectsPreviewRenderer::completed),navigatorResults(navigatorRenderer,&EffectsPreviewRenderer::completed);
        show(canvas,navigator);QVERIFY(exact(canvas));QVERIFY(exact(navigator));
        verifyResults(canvasResults,0);verifyResults(navigatorResults,0);
        const auto initial=grab(canvas);QCOMPARE(initial,canvasReference(canvas));QCOMPARE(grab(navigator),navigatorReference(navigator,canvas));
        QVERIFY(canvas.document()->setCurrentFrame(10));QVERIFY(exact(canvas));QVERIFY(exact(navigator));
        verifyResults(canvasResults,10);verifyResults(navigatorResults,10);
        QVERIFY(grab(canvas)!=initial);QCOMPARE(grab(canvas),canvasReference(canvas));QCOMPARE(grab(navigator),navigatorReference(navigator,canvas));
        // Seek twice without delivering a completion. Results from frame zero
        // must not replace the latest requested frame five on either surface.
        QVERIFY(canvas.document()->setCurrentFrame(0));canvas.repaint();navigator.repaint();
        QVERIFY(canvas.document()->setCurrentFrame(5));QVERIFY(exact(canvas));QVERIFY(exact(navigator));
        verifyResults(canvasResults,5);verifyResults(navigatorResults,5);QCOMPARE(grab(canvas),initial);
        QCOMPARE(grab(navigator),navigatorReference(navigator,canvas));QCOMPARE(canvas.project().toJson(),project.toJson());
        QCOMPARE(canvas.document()->history()->count(),0);
    }
    void sourceArtworkModeAndAnimatedModeKeepTheirOwnExactResults() {
        Canvas canvas;const auto project=fixture(0);canvas.setProject(project);Navigator navigator(&canvas);show(canvas,navigator);
        QVERIFY(canvas.document()->setCurrentFrame(10));QVERIFY(exact(canvas));QVERIFY(exact(navigator));const auto animated=grab(canvas);
        canvas.document()->setKeyframeSourceEditing(true);QVERIFY(exact(canvas));QVERIFY(exact(navigator));
        const auto source=grab(canvas);QVERIFY(source!=animated);QCOMPARE(source,canvasReference(canvas));QCOMPARE(grab(navigator),navigatorReference(navigator,canvas));
        canvas.document()->setKeyframeSourceEditing(false);QVERIFY(exact(canvas));QVERIFY(exact(navigator));
        QCOMPARE(grab(canvas),animated);QCOMPARE(grab(navigator),navigatorReference(navigator,canvas));QCOMPARE(canvas.project().toJson(),project.toJson());
    }
    void fourKKeyframeGesturesUseDraftsAndDiscardStaleResults_data() {
        QTest::addColumn<bool>("mask");QTest::newRow("animated-blur")<<false;QTest::newRow("animated-mask-feather")<<true;
    }
    void fourKKeyframeGesturesUseDraftsAndDiscardStaleResults() {
        QFETCH(bool,mask);auto project=fixture(mask?2:0);project.width=3840;project.height=2160;
        Canvas canvas;canvas.setProject(project);Navigator navigator(&canvas);show(canvas,navigator);
        auto *document=canvas.document();QVERIFY(document->setCurrentFrame(10));QVERIFY(exact(canvas));QVERIFY(exact(navigator));
        const auto initial=grab(canvas),initialNavigator=grab(navigator);const auto source=canvas.shot().layers[0];
        const auto channel=mask?KeyframeProperty::MaskFeather:KeyframeProperty::BlurRadius;
        auto *canvasRenderer=canvas.findChild<EffectsPreviewRenderer *>();auto *navigatorRenderer=navigator.findChild<EffectsPreviewRenderer *>();
        QVERIFY(canvasRenderer&&navigatorRenderer);QSignalSpy canvasResults(canvasRenderer,&EffectsPreviewRenderer::completed),navigatorResults(navigatorRenderer,&EffectsPreviewRenderer::completed);
        QVERIFY(document->beginKeyframeEdit(source.id));QVERIFY(document->previewKeyframeValue(channel,96));canvas.repaint();navigator.repaint();
        QVERIFY(canvas.property("effectsPreviewPending").toBool());QVERIFY(navigator.property("effectsPreviewPending").toBool());
        QCOMPARE(grab(canvas),initial);QCOMPARE(grab(navigator),initialNavigator);
        QTRY_VERIFY_WITH_TIMEOUT(reducedDraft(canvasResults),30000);QTRY_VERIFY_WITH_TIMEOUT(reducedDraft(navigatorResults),30000);
        QVERIFY(document->keyframeEditActive());QCOMPARE(document->history()->count(),0);
        // Cancel while a newer draft is outstanding. Keep original pixels and
        // keys, and do not publish the cancelled draft after the exact restore.
        QVERIFY(document->previewKeyframeValue(channel,120));canvas.repaint();navigator.repaint();document->cancelKeyframeEdit();
        QVERIFY(exact(canvas));QVERIFY(exact(navigator));verifyResults(canvasResults,10);verifyResults(navigatorResults,10);
        QCOMPARE(grab(canvas),initial);QCOMPARE(grab(navigator),initialNavigator);QCOMPARE(canvas.project().toJson(),project.toJson());QCOMPARE(document->history()->count(),0);
        canvasResults.clear();navigatorResults.clear();QVERIFY(document->beginKeyframeEdit(source.id));
        QVERIFY(document->previewKeyframeValue(channel,48));canvas.repaint();navigator.repaint();
        QTRY_VERIFY_WITH_TIMEOUT(reducedDraft(canvasResults),30000);QTRY_VERIFY_WITH_TIMEOUT(reducedDraft(navigatorResults),30000);
        document->commitKeyframeEdit();QVERIFY(exact(canvas));QVERIFY(exact(navigator));verifyResults(canvasResults,10);verifyResults(navigatorResults,10);
        const auto final=grab(canvas);QVERIFY(final!=initial);QCOMPARE(final,canvasReference(canvas));QCOMPARE(grab(navigator),navigatorReference(navigator,canvas));
        QCOMPARE(document->history()->count(),1);QCOMPARE(canvas.shot().layers[0].effects,source.effects);QCOMPARE(canvas.shot().layers[0].mask,source.mask);QCOMPARE(canvas.shot().layers[0].tiles,source.tiles);
        document->history()->undo();QVERIFY(exact(canvas));QVERIFY(exact(navigator));QCOMPARE(grab(canvas),initial);QCOMPARE(grab(navigator),initialNavigator);
        document->history()->redo();QVERIFY(exact(canvas));QVERIFY(exact(navigator));QCOMPARE(grab(canvas),final);QCOMPARE(grab(navigator),navigatorReference(navigator,canvas));
    }
};
QTEST_MAIN(AnimatedEffectsCanvasTests)
#include "test_animated_effects_canvas.moc"
