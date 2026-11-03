#include "canvas.h"
#include "document.h"
#include "drawing_tool_controller.h"
#include "effect_render.h"
#include "effects_panel.h"
#include "effects_preview_renderer.h"
#include "ui_theme.h"
#include "value_slider.h"
#include "animation_test_fixture.h"
#include <QDoubleSpinBox>
#include <QDir>
#include <QLabel>
#include <QLineEdit>
#include <QPointer>
#include <QSignalSpy>
#include <QSlider>
#include <QTabBar>
#include <QMouseEvent>
#include <QtTest>
#include <cstring>
using namespace toon;
namespace {
Project fixture(int flavor=0){
    auto project=Project::create();project.width=512;project.height=384;project.transparent=true;
    auto &shot=project.shots[0];shot.sheet.overscan=0;shot.durationFrames=20;shot.name=QString("Effects fixture %1").arg(flavor);shot.layers.resize(1);auto &layer=shot.layers[0];layer.tiles.clear();
    for(int ty=0;ty<2;++ty)for(int tx=0;tx<2;++tx){QImage image(tileSize,tileSize,QImage::Format_ARGB32_Premultiplied);image.fill(Qt::transparent);
        for(int y=0;y<tileSize;++y){auto *row=reinterpret_cast<QRgb *>(image.scanLine(y));for(int x=0;x<tileSize;++x){const int px=tx*tileSize+x,py=ty*tileSize+y;
            if(px>22&&px<480&&py>17&&py<362)row[x]=qPremultiply(qRgba((px+flavor*71)%256,(py*3+flavor*37)%256,(px+py*2)%256,72+(px*3+py*7)%184));}}
        layer.tiles.insert({tx,ty},image);}
    layer.mask=LayerMask{};layer.mask->defaultValue=223;QImage mask(tileSize,tileSize,QImage::Format_Grayscale8);mask.fill(255);for(int y=0;y<tileSize;++y)memset(mask.scanLine(y),64,85);layer.mask->tiles.insert({1,0},mask);
    layer.effects.blurEnabled=true;layer.effects.blurRadius=12;layer.effects.shadowEnabled=true;layer.effects.shadowRadius=9;layer.effects.shadowOffset={14.25,-7.75};layer.effects.shadowOpacity=.61;
    layer.effects.colorEnabled=true;layer.effects.exposure=.35;layer.effects.hue=31;layer.effects.saturation=20;return project;
}
void leave(QWidget &widget){QEvent event(QEvent::Leave);QApplication::sendEvent(&widget,&event);}
bool exact(QWidget &widget){
    widget.repaint();
    return QTest::qWaitFor([&]{return widget.property("effectsPreviewPending").isValid()&&!widget.property("effectsPreviewPending").toBool();},30000);
}
void show(Canvas &canvas){canvas.resize(800,620);canvas.show();QCoreApplication::processEvents();canvas.fitSheet();leave(canvas);}
QImage imageFor(const QWidget &widget){
    const qreal ratio=widget.devicePixelRatioF();QImage image(QSize(qCeil(widget.width()*ratio),qCeil(widget.height()*ratio)),QImage::Format_ARGB32_Premultiplied);
    image.setDevicePixelRatio(ratio);image.setDotsPerMeterX(qRound(widget.logicalDpiX()/.0254));image.setDotsPerMeterY(qRound(widget.logicalDpiY()/.0254));return image;
}
QImage viewportReference(const Canvas &canvas){
    auto image=imageFor(canvas);image.fill(QColor(ui::canvas));QPainter painter(&image);painter.setClipRegion(QRegion(canvas.rect()));painter.setTransform(canvas.viewTransform());
    painter.fillRect(canvas.paperRect().translated(8,10),QColor(0,0,0,65));
    paintSheet(painter,canvas.project(),canvas.displayShot(),true,nullptr,canvas.canvasSmoothing(),true,canvas.document()->currentFrame(),canvas.cameraView());return image;
}
QImage navigatorReference(const Navigator &navigator,const Canvas &canvas){
    auto image=imageFor(navigator);image.fill(QColor(ui::base));QPainter painter(&image);painter.setTransform(navigator.miniatureTransform());
    paintSheet(painter,canvas.project(),canvas.displayShot(),true,nullptr,canvas.canvasSmoothing(),true,canvas.document()->currentFrame(),canvas.cameraView());
    QPen pen(QColor(ui::accent),2);pen.setCosmetic(true);painter.setPen(pen);QColor tint(ui::accent);tint.setAlpha(22);painter.setBrush(tint);painter.drawPolygon(canvas.visiblePolygon());return image;
}
QImage grab(QWidget &widget){return widget.grab().toImage().convertToFormat(QImage::Format_ARGB32_Premultiplied);}
QString difference(const QImage &actual,const QImage &expected){
    const auto directory=qEnvironmentVariable("TOON_EFFECTS_DIAGNOSTICS");
    if(!directory.isEmpty()){QDir().mkpath(directory);const auto prefix=directory+"/"+QString::fromLatin1(QTest::currentTestFunction());actual.save(prefix+"-actual.png");expected.save(prefix+"-expected.png");}
    if(actual.size()!=expected.size())return QString("Size %1x%2 expected %3x%4").arg(actual.width()).arg(actual.height()).arg(expected.width()).arg(expected.height());
    int count=0;QRect bounds;QString first;
    for(int y=0;y<actual.height();++y)for(int x=0;x<actual.width();++x)if(actual.pixel(x,y)!=expected.pixel(x,y)){
        ++count;bounds|=QRect(x,y,1,1);if(first.isEmpty())first=QString("Pixel(%1,%2): %3 expected %4").arg(x).arg(y).arg(actual.pixel(x,y),8,16,QChar('0')).arg(expected.pixel(x,y),8,16,QChar('0'));
    }
    return count?first+QString("; %1 pixels in [%2,%3 %4x%5]").arg(count).arg(bounds.x()).arg(bounds.y()).arg(bounds.width()).arg(bounds.height()):QString("Image metadata differs");
}
void compareCanvas(Canvas &canvas){leave(canvas);const auto actual=grab(canvas),expected=viewportReference(canvas);QVERIFY2(actual==expected,qPrintable(difference(actual,expected)));}
bool sameSources(const Project &before,const Project &after){
    if(before.toJson()!=after.toJson())return false;
    const auto sources=projectArtworkShots(before),current=projectArtworkShots(after);
    if(sources.size()!=current.size())return false;
    for(int i=0;i<sources.size();++i){
        if(sources[i]->layers.size()!=current[i]->layers.size())return false;
        for(int j=0;j<sources[i]->layers.size();++j)
            if(sources[i]->layers[j].tiles!=current[i]->layers[j].tiles||sources[i]->layers[j].mask!=current[i]->layers[j].mask)return false;
    }
    return true;
}
}
class EffectsPreviewCanvasTests final:public QObject{
    Q_OBJECT
private slots:
    void initTestCase(){applyTheme();}
    void latestExactCommitUndoRedoPreserveSource(){
        Canvas canvas;const auto project=fixture();canvas.setProject(project);show(canvas);QVERIFY(exact(canvas));compareCanvas(canvas);const auto initial=grab(canvas);
        auto *document=canvas.document();QSignalSpy pending(&canvas,SIGNAL(effectsPreviewPendingChanged(bool)));QVERIFY(pending.isValid());
        QVERIFY(document->beginEffectsEdit());auto fx=canvas.shot().layers[0].effects;
        for(qreal radius:{42.,96.,7.5}){fx.blurRadius=radius;fx.hue+=17;QVERIFY(document->previewLayerEffects(fx));canvas.repaint();QVERIFY(canvas.property("effectsPreviewPending").toBool());}
        QCOMPARE(document->history()->count(),0);document->commitEffectsEdit();QVERIFY(exact(canvas));compareCanvas(canvas);const auto final=grab(canvas);
        QVERIFY(final!=initial);QCOMPARE(canvas.shot().layers[0].effects,fx);QCOMPARE(canvas.shot().layers[0].tiles,project.shots[0].layers[0].tiles);QCOMPARE(canvas.shot().layers[0].mask,project.shots[0].layers[0].mask);
        QCOMPARE(document->history()->count(),1);QVERIFY(canvas.property("effectsPreviewExact").toBool());QCOMPARE(canvas.property("effectsPreviewSourceScale").toDouble(),1.);QVERIFY(pending.count()>=2);
        document->history()->undo();QVERIFY(exact(canvas));QCOMPARE(grab(canvas),initial);document->history()->redo();QVERIFY(exact(canvas));QCOMPARE(grab(canvas),final);
    }
    void keepLastGoodUntilPublicationThenCancelAndDisableLastEffect(){
        auto project=fixture();auto &layer=project.shots[0].layers[0];layer.mask.reset();layer.effects={};layer.effects.blurEnabled=true;layer.effects.blurRadius=8;
        Canvas canvas;canvas.setProject(project);show(canvas);QVERIFY(exact(canvas));const auto initial=grab(canvas);auto *document=canvas.document();
        QVERIFY(document->beginEffectsEdit());auto fx=layer.effects;fx.blurRadius=120;QVERIFY(document->previewLayerEffects(fx));canvas.repaint();QVERIFY(canvas.property("effectsPreviewPending").toBool());
        // Completion is queued to the GUI thread: before processing it, the
        // last exact viewport remains visible instead of blanking the sheet.
        QCOMPARE(grab(canvas),initial);document->cancelEffectsEdit();QVERIFY(exact(canvas));QCOMPARE(grab(canvas),initial);QCOMPARE(document->history()->count(),0);
        QVERIFY(document->beginEffectsEdit());QVERIFY(document->previewLayerEffects(fx));canvas.repaint();fx.enabled=false;QVERIFY(document->previewLayerEffects(fx));document->commitEffectsEdit();
        QVERIFY(exact(canvas));compareCanvas(canvas);QVERIFY(!layerStackHasEffects(canvas.shot().layers));QCOMPARE(document->history()->count(),1);
    }
    void pendingViewResizeAndSamplingChangesUseLatestContext_data(){QTest::addColumn<bool>("smooth");QTest::newRow("nearest")<<false;QTest::newRow("filtered")<<true;}
    void pendingViewResizeAndSamplingChangesUseLatestContext(){
        QFETCH(bool,smooth);Canvas canvas;canvas.setProject(fixture());show(canvas);QVERIFY(exact(canvas));auto fx=canvas.shot().layers[0].effects;fx.blurRadius=83;
        QVERIFY(canvas.document()->setLayerEffects(canvas.shot().layers[0].id,fx));canvas.repaint();QVERIFY(canvas.property("effectsPreviewPending").toBool());
        canvas.setZoom(.93);canvas.setRotation(27);canvas.centerOn(canvas.paperRect().center()+QPointF(31,-19));canvas.setCanvasSmoothing(smooth);canvas.resize(740,560);canvas.setTool(Canvas::Tool::Hand);
        QEvent ratio(QEvent::DevicePixelRatioChange);QApplication::sendEvent(&canvas,&ratio);QVERIFY(exact(canvas));compareCanvas(canvas);
        QCOMPARE(canvas.zoom(),.93);QCOMPARE(canvas.rotation(),27.);QCOMPARE(canvas.canvasSmoothing(),smooth);
    }
    void fitted4KViewportRetainsOriginalRegionClipAntialiasing(){
        // Match the large-sheet fit and viewport allocation of the production
        // benchmark. Sparse source pixels keep this regression inexpensive;
        // the full frame/margin guide edges still exercise fractional AA.
        auto project=fixture();project.width=3840;project.height=2160;
        Canvas canvas;canvas.setProject(project);canvas.resize(1068,915);canvas.show();QCoreApplication::processEvents();canvas.fitSheet();leave(canvas);
        QVERIFY(exact(canvas));QVERIFY(canvas.zoom()<.3);compareCanvas(canvas);
    }
    void loadAndShotSwitchRejectOlderProjectResults(){
        auto project=fixture();project.shots.append(fixture(3).shots[0]);Canvas canvas;canvas.setProject(project);show(canvas);QVERIFY(exact(canvas));
        auto fx=canvas.shot().layers[0].effects;fx.blurRadius=121;QVERIFY(canvas.document()->setLayerEffects(canvas.shot().layers[0].id,fx));canvas.repaint();QVERIFY(canvas.property("effectsPreviewPending").toBool());
        QVERIFY(canvas.document()->selectShot(project.shots[1].id));QVERIFY(exact(canvas));compareCanvas(canvas);QCOMPARE(canvas.document()->activeShotId(),project.shots[1].id);
        fx=canvas.shot().layers[0].effects;fx.blurRadius=117;QVERIFY(canvas.document()->setLayerEffects(canvas.shot().layers[0].id,fx));canvas.repaint();const auto replacement=fixture(7);canvas.setProject(replacement);
        QVERIFY(exact(canvas));compareCanvas(canvas);QCOMPARE(canvas.project().toJson(),replacement.toJson());QCOMPARE(canvas.document()->history()->count(),0);
    }
    void frameAndSourceModeTransitionsUseLatestDrawing(){
        auto project=animation_fixture::project();for(auto &layer:project.shots[0].layers)if(!layer.tiles.isEmpty()){layer.effects.blurEnabled=true;layer.effects.blurRadius=17;layer.effects.shadowEnabled=true;}
        Canvas canvas;canvas.setProject(project);show(canvas);QVERIFY(canvas.document()->selectAnimationTrack(canvas.shot().animationTracks[0].id));QVERIFY(exact(canvas));
        canvas.setRotation(11);canvas.repaint();QVERIFY(canvas.document()->setCurrentFrame(4));QVERIFY(exact(canvas));compareCanvas(canvas);QCOMPARE(canvas.document()->currentFrame(),4);
        canvas.setCanvasSmoothing(false);canvas.repaint();canvas.document()->setKeyframeSourceEditing(true);QVERIFY(exact(canvas));compareCanvas(canvas);
        canvas.document()->setKeyframeSourceEditing(false);QVERIFY(canvas.document()->setCurrentFrame(0));QVERIFY(exact(canvas));compareCanvas(canvas);
    }
    void playbackUsesOneContextAndStopRestoresExactCurrentFrame_data(){
        QTest::addColumn<bool>("symbol");QTest::newRow("four-k-cels")<<false;QTest::newRow("four-k-animated-symbol")<<true;
    }
    void playbackUsesOneContextAndStopRestoresExactCurrentFrame(){
        QFETCH(bool,symbol);auto project=animation_fixture::project();project.width=3840;project.height=2160;
        for(auto &layer:project.shots[0].layers)if(!layer.tiles.isEmpty()){
            layer.effects.blurEnabled=true;layer.effects.blurRadius=18;layer.effects.glowEnabled=true;layer.effects.glowRadius=32;
        }
        if(symbol){
            SymbolDefinition definition{animation_fixture::id(),"Animated character",project.shots[0]};
            definition.content.id=animation_fixture::id();
            project.symbols={definition};auto instance=RasterLayer::create("Character instance");instance.kind=LayerKind::Symbol;instance.symbol=SymbolInstance{definition.id};
            project.shots[0].layers={instance};project.shots[0].animationTracks.clear();
        }
        QVERIFY2(project.validate().isEmpty(),qPrintable(project.validate()));
        Canvas canvas;canvas.setProject(project);show(canvas);
        if(!symbol)QVERIFY(canvas.document()->selectAnimationTrack(canvas.shot().animationTracks[0].id));
        Navigator navigator(&canvas);navigator.resize(280,240);navigator.show();QVERIFY(exact(canvas));QVERIFY(exact(navigator));
        auto *canvasRenderer=canvas.findChild<EffectsPreviewRenderer *>();auto *navigatorRenderer=navigator.findChild<EffectsPreviewRenderer *>();QVERIFY(canvasRenderer&&navigatorRenderer);
        QSignalSpy canvasResults(canvasRenderer,&EffectsPreviewRenderer::completed),navigatorResults(navigatorRenderer,&EffectsPreviewRenderer::completed);
        canvas.setAnimationPlayback(true);
        // Includes active-cel changes and a loop back. Those selection changes
        // must not invalidate otherwise publishable playback work.
        for(int frame:{4,1,5}){
            QVERIFY(canvas.document()->setCurrentFrame(frame));canvas.repaint();navigator.repaint();
            QTRY_COMPARE_WITH_TIMEOUT(canvas.property("effectsPreviewFrame").toInt(),frame,30000);
            QTRY_COMPARE_WITH_TIMEOUT(navigator.property("effectsPreviewFrame").toInt(),frame,30000);
        }
        for(const auto *results:{&canvasResults,&navigatorResults}){
            QVERIFY(results->size()>=3);const auto context=qvariant_cast<EffectsPreviewResult>(results->first()[0]).context;quint64 revision=0;
            for(const auto &arguments:*results){const auto result=qvariant_cast<EffectsPreviewResult>(arguments[0]);
                QCOMPARE(result.context,context);QVERIFY(result.playback);QVERIFY(result.revision>revision);revision=result.revision;}
        }
        // Stop with work outstanding: only the exact selected frame may be
        // installed afterward, including the Navigator's independent worker.
        QVERIFY(canvas.document()->setCurrentFrame(1));canvas.repaint();navigator.repaint();
        QVERIFY(canvas.document()->setCurrentFrame(4));canvas.setAnimationPlayback(false);
        QVERIFY(exact(canvas));QVERIFY(exact(navigator));compareCanvas(canvas);
        QCOMPARE(canvas.property("effectsPreviewFrame").toInt(),4);QCOMPARE(navigator.property("effectsPreviewFrame").toInt(),4);
        const auto miniature=grab(navigator),expected=navigatorReference(navigator,canvas);QVERIFY2(miniature==expected,qPrintable(difference(miniature,expected)));
        QVERIFY(!qvariant_cast<EffectsPreviewResult>(canvasResults.last()[0]).playback);QVERIFY(!qvariant_cast<EffectsPreviewResult>(navigatorResults.last()[0]).playback);
        QCOMPARE(canvas.project().toJson(),project.toJson());QCOMPARE(canvas.document()->history()->count(),0);
    }
    void reducedPlaybackChangesQualityAndPausesToExact_data(){
        QTest::addColumn<bool>("symbol");QTest::newRow("cels")<<false;QTest::newRow("animated-symbol")<<true;
    }
    void reducedPlaybackChangesQualityAndPausesToExact(){
        QFETCH(bool,symbol);auto project=animation_fixture::project();project.width=3840;project.height=2160;
        for(auto &layer:project.shots[0].layers)if(!layer.tiles.isEmpty()){
            layer.effects.blurEnabled=true;layer.effects.blurRadius=18;layer.effects.glowEnabled=true;layer.effects.glowRadius=32;
        }
        if(symbol){
            SymbolDefinition definition{animation_fixture::id(),"Reusable character",project.shots[0]};definition.content.id=animation_fixture::id();
            project.symbols={definition};auto instance=RasterLayer::create("Character instance");instance.kind=LayerKind::Symbol;instance.symbol=SymbolInstance{definition.id};
            project.shots[0].layers={instance};project.shots[0].animationTracks.clear();
        }
        QVERIFY2(project.validate().isEmpty(),qPrintable(project.validate()));
        Canvas canvas;canvas.setProject(project);show(canvas);Navigator navigator(&canvas);navigator.resize(280,240);navigator.show();
        QVERIFY(exact(canvas));QVERIFY(exact(navigator));canvas.setPlaybackPreviewDivisor(4);QCOMPARE(canvas.playbackPreviewDivisor(),4);
        // Selecting a rehearsal quality while paused does not degrade editing.
        QVERIFY(exact(canvas));QVERIFY(exact(navigator));QCOMPARE(canvas.property("effectsPreviewSourceScale").toDouble(),1.);compareCanvas(canvas);
        auto *canvasRenderer=canvas.findChild<EffectsPreviewRenderer *>();auto *navigatorRenderer=navigator.findChild<EffectsPreviewRenderer *>();
        QSignalSpy canvasResults(canvasRenderer,&EffectsPreviewRenderer::completed),navigatorResults(navigatorRenderer,&EffectsPreviewRenderer::completed);
        canvas.setAnimationPlayback(true);
        for(int frame:{4,1,5}){
            QVERIFY(canvas.document()->setCurrentFrame(frame));canvas.repaint();navigator.repaint();
            QTRY_COMPARE_WITH_TIMEOUT(canvas.property("effectsPreviewFrame").toInt(),frame,30000);
            QTRY_COMPARE_WITH_TIMEOUT(navigator.property("effectsPreviewFrame").toInt(),frame,30000);
            QCOMPARE(canvas.property("effectsPreviewSourceScale").toDouble(),.25);QCOMPARE(navigator.property("effectsPreviewSourceScale").toDouble(),.25);
            QVERIFY(!canvas.property("effectsPreviewExact").toBool());QVERIFY(!navigator.property("effectsPreviewExact").toBool());
        }
        const auto previousCanvasContext=qvariant_cast<EffectsPreviewResult>(canvasResults.last()[0]).context;
        const auto previousNavigatorContext=qvariant_cast<EffectsPreviewResult>(navigatorResults.last()[0]).context;
        canvas.setPlaybackPreviewDivisor(2);canvas.repaint();navigator.repaint();
        QTRY_COMPARE_WITH_TIMEOUT(canvas.property("effectsPreviewSourceScale").toDouble(),.5,30000);
        QTRY_COMPARE_WITH_TIMEOUT(navigator.property("effectsPreviewSourceScale").toDouble(),.5,30000);
        QVERIFY(qvariant_cast<EffectsPreviewResult>(canvasResults.last()[0]).context!=previousCanvasContext);
        QVERIFY(qvariant_cast<EffectsPreviewResult>(navigatorResults.last()[0]).context!=previousNavigatorContext);
        canvas.setPlaybackPreviewDivisor(1);canvas.repaint();navigator.repaint();
        QTRY_COMPARE_WITH_TIMEOUT(canvas.property("effectsPreviewSourceScale").toDouble(),1.,30000);
        QTRY_COMPARE_WITH_TIMEOUT(navigator.property("effectsPreviewSourceScale").toDouble(),1.,30000);
        QVERIFY(canvas.animationPlayback());QVERIFY(canvas.property("effectsPreviewExact").toBool());QVERIFY(navigator.property("effectsPreviewExact").toBool());
        // Stop with a reduced render still pending: only the exact selected
        // frame can survive the context change, including the miniature.
        canvas.setPlaybackPreviewDivisor(8);QVERIFY(canvas.document()->setCurrentFrame(1));canvas.repaint();navigator.repaint();
        QVERIFY(canvas.document()->setCurrentFrame(4));canvas.setAnimationPlayback(false);
        QVERIFY(exact(canvas));QVERIFY(exact(navigator));QCOMPARE(canvas.property("effectsPreviewFrame").toInt(),4);QCOMPARE(navigator.property("effectsPreviewFrame").toInt(),4);
        QCOMPARE(canvas.property("effectsPreviewSourceScale").toDouble(),1.);QCOMPARE(navigator.property("effectsPreviewSourceScale").toDouble(),1.);
        QVERIFY(canvas.property("effectsPreviewExact").toBool());QVERIFY(navigator.property("effectsPreviewExact").toBool());compareCanvas(canvas);
        const auto miniature=grab(navigator),expected=navigatorReference(navigator,canvas);QVERIFY2(miniature==expected,qPrintable(difference(miniature,expected)));
        QVERIFY(sameSources(project,canvas.project()));QCOMPARE(canvas.document()->history()->count(),0);
    }
    void reducedPlaybackWithoutEffectsRestoresEditingAndLeavesExportFullQuality(){
        auto project=fixture();auto &shot=project.shots[0];shot.layers[0].effects={};
        shot.layerAnimations={{shot.layers[0].id,{256,192},{{KeyframeProperty::PositionX,{{0,0},{19,90}}},{KeyframeProperty::Rotation,{{0,0},{19,24}}}}}};
        shot.camera2DKeys={{0,{{0,0},1,0}},{19,{{20,10},1.2,-7}}};
        QVERIFY2(project.validate().isEmpty(),qPrintable(project.validate()));
        const auto exported=renderMovieFrame(project,7);QVERIFY(!exported.isNull());
        Canvas canvas;canvas.setProject(project);show(canvas);Navigator navigator(&canvas);navigator.resize(280,240);navigator.show();
        canvas.setPlaybackPreviewDivisor(4);canvas.setAnimationPlayback(true);QVERIFY(canvas.document()->setCurrentFrame(7));canvas.repaint();navigator.repaint();
        QTRY_COMPARE_WITH_TIMEOUT(canvas.property("effectsPreviewSourceScale").toDouble(),.25,30000);
        QTRY_COMPARE_WITH_TIMEOUT(navigator.property("effectsPreviewSourceScale").toDouble(),.25,30000);
        QCOMPARE(renderMovieFrame(canvas.project(),7),exported);
        canvas.setAnimationPlayback(false);QVERIFY(exact(canvas));QVERIFY(exact(navigator));compareCanvas(canvas);
        QCOMPARE(canvas.property("effectsPreviewSourceScale").toDouble(),1.);QCOMPARE(navigator.property("effectsPreviewSourceScale").toDouble(),1.);
        QCOMPARE(renderMovieFrame(canvas.project(),7),exported);QVERIFY(sameSources(project,canvas.project()));QCOMPARE(canvas.document()->history()->count(),0);
    }
    void pendingReducedPreviewRejectsOldViewSeekAndProject(){
        auto project=fixture();Canvas canvas;canvas.setProject(project);show(canvas);Navigator navigator(&canvas);navigator.resize(280,240);navigator.show();
        canvas.setPlaybackPreviewDivisor(8);canvas.setAnimationPlayback(true);QVERIFY(canvas.document()->setCurrentFrame(3));canvas.repaint();navigator.repaint();
        canvas.setPlaybackPreviewDivisor(2);canvas.setZoom(.93);canvas.setRotation(19);canvas.resize(740,560);navigator.resize(250,220);
        QVERIFY(canvas.document()->setCurrentFrame(8));canvas.repaint();navigator.repaint();
        canvas.setAnimationPlayback(false);QVERIFY(exact(canvas));QVERIFY(exact(navigator));compareCanvas(canvas);
        QCOMPARE(canvas.property("effectsPreviewFrame").toInt(),8);QCOMPARE(navigator.property("effectsPreviewFrame").toInt(),8);
        const auto miniature=grab(navigator),expected=navigatorReference(navigator,canvas);QVERIFY2(miniature==expected,qPrintable(difference(miniature,expected)));
        canvas.setAnimationPlayback(true);canvas.repaint();navigator.repaint();const auto replacement=fixture(7);canvas.setProject(replacement);canvas.setAnimationPlayback(false);
        QVERIFY(exact(canvas));QVERIFY(exact(navigator));compareCanvas(canvas);QVERIFY(sameSources(replacement,canvas.project()));
        QCOMPARE(canvas.property("effectsPreviewSourceScale").toDouble(),1.);QCOMPARE(navigator.property("effectsPreviewSourceScale").toDouble(),1.);QCOMPARE(canvas.document()->history()->count(),0);
    }
    void navigatorUsesWholeSheetAndLatestViewport(){
        Canvas canvas;canvas.setProject(fixture());show(canvas);Navigator navigator(&canvas);navigator.resize(280,240);navigator.show();QVERIFY(exact(canvas));QVERIFY(exact(navigator));
        auto fx=canvas.shot().layers[0].effects;fx.blurRadius=75;QVERIFY(canvas.document()->setLayerEffects(canvas.shot().layers[0].id,fx));canvas.repaint();navigator.repaint();
        canvas.setZoom(1.4);canvas.centerOn(canvas.paperRect().center()+QPointF(90,70));canvas.setRotation(-19);QVERIFY(exact(canvas));QVERIFY(exact(navigator));compareCanvas(canvas);
        const auto actual=grab(navigator),expected=navigatorReference(navigator,canvas);QVERIFY2(actual==expected,qPrintable(difference(actual,expected)));
    }
    void navigatorUpdatesDuringContactBeforeHistoryCommit(){
        auto project=fixture();auto &layer=project.shots[0].layers[0];layer.tiles.clear();layer.mask.reset();layer.effects={};
        layer.effects.shadowEnabled=true;layer.effects.shadowRadius=0;layer.effects.shadowOffset={280,0};layer.effects.shadowColor=Qt::blue;layer.effects.shadowOpacity=1;
        Canvas canvas;canvas.setProject(project);show(canvas);canvas.brush.size=20;canvas.brush.hardness=1;canvas.brush.pressureSize=false;canvas.brush.opacity=1;canvas.brush.strokeOpacity=1;canvas.brush.color=Qt::red;
        Navigator navigator(&canvas);navigator.resize(300,260);navigator.show();QVERIFY(exact(canvas));QVERIFY(exact(navigator));const auto initial=grab(navigator);
        const auto mouse=[&](QEvent::Type type,QPointF artwork){const auto view=canvas.viewTransform().map(artwork+sheetGeometry(canvas.project(),canvas.shot()).output.topLeft());
            QMouseEvent event(type,view,view,canvas.mapToGlobal(view),type==QEvent::MouseMove?Qt::NoButton:Qt::LeftButton,type==QEvent::MouseButtonRelease?Qt::NoButton:Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(&canvas,&event);};
        auto revision=navigator.property("effectsPreviewPublishedRevision").toULongLong();mouse(QEvent::MouseButtonPress,{40,80});QVERIFY(canvas.document()->strokeActive());
        QTRY_VERIFY_WITH_TIMEOUT(navigator.property("effectsPreviewPublishedRevision").toULongLong()>revision,30000);QVERIFY(exact(navigator));
        auto current=grab(navigator),expected=navigatorReference(navigator,canvas);QVERIFY2(current==expected,qPrintable(difference(current,expected)));QVERIFY(current!=initial);QCOMPARE(canvas.document()->history()->count(),0);
        revision=navigator.property("effectsPreviewPublishedRevision").toULongLong();mouse(QEvent::MouseMove,{80,80});QVERIFY(canvas.document()->strokeActive());
        QTRY_VERIFY_WITH_TIMEOUT(navigator.property("effectsPreviewPublishedRevision").toULongLong()>revision,30000);QVERIFY(exact(navigator));
        const auto moved=grab(navigator);expected=navigatorReference(navigator,canvas);QVERIFY2(moved==expected,qPrintable(difference(moved,expected)));QVERIFY(moved!=current);QCOMPARE(canvas.document()->history()->count(),0);
        mouse(QEvent::MouseButtonRelease,{80,80});QCOMPARE(canvas.document()->history()->count(),1);QVERIFY(exact(navigator));
    }
    void retouchEffectsAdvanceBeforeRelease_data(){
        QTest::addColumn<int>("mode");QTest::newRow("blur")<<int(RetouchMode::Blur);QTest::newRow("dodge")<<int(RetouchMode::Dodge);
    }
    void retouchEffectsAdvanceBeforeRelease(){
        QFETCH(int,mode);auto project=fixture();auto &layer=project.shots[0].layers[0];layer.effects.blurRadius=3;
        QImage checker(tileSize,tileSize,QImage::Format_ARGB32_Premultiplied);checker.fill(Qt::transparent);
        {QPainter painter(&checker);for(int y=0;y<8;++y)for(int x=0;x<8;++x)painter.fillRect(x*32,y*32,32,32,(x+y)%2?QColor(190,150,100,230):QColor(40,80,120,190));}
        layer.tiles.insert({0,0},checker);const auto source=layer.tiles;
        Canvas canvas;canvas.setProject(project);show(canvas);canvas.setTool(Canvas::Tool::Retouch);auto *controller=canvas.drawingTools();
        controller->retouchSettings.mode=RetouchMode(mode);controller->retouchSettings.size=80;controller->retouchSettings.strength=.9;controller->notifySettings();
        Navigator navigator(&canvas);navigator.resize(300,260);navigator.show();QVERIFY(exact(canvas));QVERIFY(exact(navigator));leave(canvas);const auto beforeCanvas=grab(canvas),beforeNavigator=grab(navigator);
        auto *canvasRenderer=canvas.findChild<EffectsPreviewRenderer *>();auto *navigatorRenderer=navigator.findChild<EffectsPreviewRenderer *>();QVERIFY(canvasRenderer&&navigatorRenderer);
        QSignalSpy canvasResults(canvasRenderer,&EffectsPreviewRenderer::completed),navigatorResults(navigatorRenderer,&EffectsPreviewRenderer::completed);
        const auto mouse=[&](QEvent::Type type,QPointF artwork){const auto view=canvas.viewTransform().map(artwork+sheetGeometry(canvas.project(),canvas.shot()).output.topLeft());
            QMouseEvent event(type,view,view,canvas.mapToGlobal(view),type==QEvent::MouseMove?Qt::NoButton:Qt::LeftButton,type==QEvent::MouseButtonRelease?Qt::NoButton:Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(&canvas,&event);};
        quint64 canvasRevision=canvas.property("effectsPreviewPublishedRevision").toULongLong(),navigatorRevision=navigator.property("effectsPreviewPublishedRevision").toULongLong();
        mouse(QEvent::MouseButtonPress,{118,150});for(int i=0;i<6;++i){mouse(QEvent::MouseMove,{126+i*13.,150+(i%2)*6.});canvas.repaint();}
        QVERIFY(controller->dragging());QVERIFY(canvas.document()->strokeActive());QCOMPARE(canvas.document()->history()->count(),0);QVERIFY(canvas.shot().layers[0].tiles!=source);
        QTRY_VERIFY_WITH_TIMEOUT(canvas.property("effectsPreviewPublishedRevision").toULongLong()>canvasRevision,30000);
        QTRY_VERIFY_WITH_TIMEOUT(navigator.property("effectsPreviewPublishedRevision").toULongLong()>navigatorRevision,30000);QVERIFY(exact(canvas));QVERIFY(exact(navigator));compareCanvas(canvas);
        auto miniature=grab(navigator),expected=navigatorReference(navigator,canvas);QVERIFY2(miniature==expected,qPrintable(difference(miniature,expected)));QVERIFY(miniature!=beforeNavigator);QVERIFY(grab(canvas)!=beforeCanvas);
        canvasRevision=canvas.property("effectsPreviewPublishedRevision").toULongLong();navigatorRevision=navigator.property("effectsPreviewPublishedRevision").toULongLong();
        for(int i=0;i<7;++i){mouse(QEvent::MouseMove,{204+i*11.,158+(i%2)*6.});canvas.repaint();}
        QTRY_VERIFY_WITH_TIMEOUT(canvas.property("effectsPreviewPublishedRevision").toULongLong()>canvasRevision,30000);
        QTRY_VERIFY_WITH_TIMEOUT(navigator.property("effectsPreviewPublishedRevision").toULongLong()>navigatorRevision,30000);QVERIFY(exact(canvas));QVERIFY(exact(navigator));compareCanvas(canvas);
        const auto moved=grab(navigator);expected=navigatorReference(navigator,canvas);QVERIFY2(moved==expected,qPrintable(difference(moved,expected)));QVERIFY(moved!=miniature);QCOMPARE(canvas.document()->history()->count(),0);
        // Every dab belongs to one contact context, even though retouch emits
        // Document::changed rather than strokeUpdated. Per-dab context resets
        // would continually invalidate otherwise publishable live previews.
        for(const auto *results:{&canvasResults,&navigatorResults}){QVERIFY(!results->isEmpty());const auto context=qvariant_cast<EffectsPreviewResult>(results->first()[0]).context;
            for(const auto &arguments:*results)QCOMPARE(qvariant_cast<EffectsPreviewResult>(arguments[0]).context,context);}
        mouse(QEvent::MouseButtonRelease,{270,158});QVERIFY(!controller->dragging());QVERIFY(!canvas.document()->strokeActive());QCOMPARE(canvas.document()->history()->count(),1);
        QVERIFY(exact(canvas));QVERIFY(exact(navigator));compareCanvas(canvas);const auto committed=canvas.shot().layers[0].tiles;
        QVERIFY(committed!=source);QCOMPARE(canvas.shot().layers[0].effects,layer.effects);QCOMPARE(canvas.shot().layers[0].mask,layer.mask);QCOMPARE(committed.value({1,1}),source.value({1,1}));
        canvas.document()->history()->undo();QCOMPARE(canvas.shot().layers[0].tiles,source);QVERIFY(exact(canvas));QVERIFY(exact(navigator));leave(canvas);QCOMPARE(grab(canvas),beforeCanvas);QCOMPARE(grab(navigator),beforeNavigator);
        canvas.document()->history()->redo();QCOMPARE(canvas.shot().layers[0].tiles,committed);QVERIFY(exact(canvas));QVERIFY(exact(navigator));compareCanvas(canvas);
    }
    void pendingStatusDoesNotReplaceTypedNumberOrDisableControls(){
        Canvas canvas;canvas.setProject(fixture());show(canvas);EffectsPanel panel(&canvas);panel.resize(260,460);panel.show();panel.findChild<QTabBar *>("effectsTabs")->setCurrentIndex(1);QVERIFY(exact(canvas));
        auto *field=panel.findChild<ValueSlider *>("effectBlurRadius");auto *number=field->findChild<QDoubleSpinBox *>();auto *slider=field->findChild<QSlider *>();auto *status=panel.findChild<QLabel *>("effectsStatus");QVERIFY(number&&slider&&status);
        slider->setSliderDown(true);slider->setValue(650);canvas.repaint();QVERIFY(canvas.property("effectsPreviewPending").toBool());QVERIFY(field->isEnabled());QVERIFY(status->text().contains("Updating"));
        slider->setSliderDown(false);number->setFocus();number->selectAll();QTest::keyClicks(number,"37.5");const auto draft=number->findChild<QLineEdit *>()->text();QVERIFY(panel.editing());
        // A new view must finish rendering without committing or replacing the
        // unfinished numeric text in the Effects panel.
        canvas.setZoom(canvas.zoom()+.01);canvas.repaint();QVERIFY(canvas.property("effectsPreviewPending").toBool());
        QVERIFY(exact(canvas));QCOMPARE(number->findChild<QLineEdit *>()->text(),draft);QVERIFY(panel.editing());QVERIFY(field->isEnabled());panel.finishEditing();QVERIFY(exact(canvas));
        QCOMPARE(canvas.shot().layers[0].effects.blurRadius,37.5);QVERIFY(!status->text().contains("Updating"));compareCanvas(canvas);
    }
    void destroyingPendingCanvasAllowsFreshViewport(){
        auto *canvas=new Canvas;canvas->setProject(fixture());show(*canvas);QVERIFY(exact(*canvas));auto *renderer=canvas->findChild<EffectsPreviewRenderer *>();QVERIFY(renderer);int completions=0;
        connect(renderer,&EffectsPreviewRenderer::completed,this,[&](const EffectsPreviewResult &){++completions;});auto fx=canvas->shot().layers[0].effects;fx.blurRadius=128;
        QVERIFY(canvas->document()->setLayerEffects(canvas->shot().layers[0].id,fx));canvas->repaint();QVERIFY(canvas->property("effectsPreviewPending").toBool());QPointer<Canvas> guard=canvas;QPointer<EffectsPreviewRenderer> worker=renderer;
        delete canvas;QVERIFY(guard.isNull());QVERIFY(worker.isNull());const int atDestruction=completions;
        // The scheduler's gated lifetime test owns worker-drain verification;
        // this integration check keeps a fresh Canvas alive while queued GUI
        // completions are delivered, without consulting the unrelated global pool.
        Canvas replacement;replacement.setProject(fixture(6));show(replacement);QVERIFY(exact(replacement));QCoreApplication::processEvents();compareCanvas(replacement);QCOMPARE(completions,atDestruction);
    }
};
QTEST_MAIN(EffectsPreviewCanvasTests)
#include "test_effects_preview_canvas.moc"
