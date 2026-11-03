// Opt-in real-workspace playback probe. Generates all artwork and settings in
// a temporary directory; never opens the artist's project or running window.
// No grabs or synchronous reference renders occur during the measured interval.
#include "main_window.h"
#include "canvas.h"
#include "document.h"
#include "effects_preview_renderer.h"
#include "sheet.h"
#include "timeline_controller.h"
#include "ui_theme.h"
#include <QApplication>
#include <QDockWidget>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPainter>
#include <QSysInfo>
#include <QTemporaryDir>
#include <QTimer>
#include <QUuid>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>

using namespace toon;
namespace {
constexpr int heartbeatMs=10,playbackMs=4000,quietMs=80,timeoutMs=180000;
void check(bool ok,const QString &message){if(!ok)throw std::runtime_error(message.toStdString());}
double elapsed(const QElapsedTimer &timer){return timer.nsecsElapsed()/1000000.;}
QString id(){return QUuid::createUuid().toString(QUuid::WithoutBraces);}
QJsonObject statistics(QVector<double> values){
    if(values.isEmpty())return {{"samples",0}};
    std::sort(values.begin(),values.end());
    return {{"samples",values.size()},{"min_ms",values.first()},{"median_ms",values[values.size()/2]},
        {"p95_ms",values[qMin(values.size()-1,qsizetype(std::ceil(values.size()*.95))-1)]},{"max_ms",values.last()}};
}
QJsonArray effectiveScales(const QJsonArray &publications){
    QVector<double> scales;
    for(const auto &entry:publications){const double scale=entry.toObject().value("source_scale").toDouble();if(!scales.contains(scale))scales.append(scale);}
    std::sort(scales.begin(),scales.end());QJsonArray result;for(double scale:scales)result.append(scale);return result;
}
Project fixture(){
    auto project=Project::create();project.movie="Generated 4K playback benchmark";
    project.width=3840;project.height=2160;project.fps=24;project.transparent=true;
    auto &shot=project.shots[0];shot.durationFrames=120;shot.sheet.overscan=0;shot.layers.resize(1);
    auto &background=shot.layers[0];background.name="Full-canvas moving blur and glow";background.tiles.clear();
    for(int ty=0;ty*tileSize<project.height;++ty)for(int tx=0;tx*tileSize<project.width;++tx){
        QImage image(tileSize,tileSize,QImage::Format_ARGB32_Premultiplied);image.fill(Qt::transparent);
        for(int y=0;y<tileSize;++y){auto *row=reinterpret_cast<QRgb *>(image.scanLine(y));const int py=ty*tileSize+y;
            for(int x=0;x<tileSize;++x){const int px=tx*tileSize+x;
                if(px<20||py<20||px>=project.width-20||py>=project.height-20)continue;
                const int alpha=(px/19+py/23)%9==0?0:40+(px*7+py*11)%216;
                row[x]=qPremultiply(qRgba((px/3+py/7)%256,(px/13+py/5)%256,(px/17+py/11)%256,alpha));
            }
        }
        background.tiles.insert({tx,ty},image);
    }
    background.effects.blurEnabled=true;background.effects.blurRadius=32;
    background.effects.glowEnabled=true;background.effects.glowRadius=64;background.effects.glowOpacity=.65;
    background.effects.glowColor=QColor(85,130,195);
    background.effects.colorEnabled=true;background.effects.saturation=18;background.effects.contrast=12;
    shot.layerAnimations={{background.id,{},{{KeyframeProperty::PositionX,{{0,0},{119,240}}}}}};

    SymbolDefinition symbol{id(),"Animated glowing prop",Project::create().shots[0]};symbol.content.durationFrames=48;
    auto &prop=symbol.content.layers[0];prop.name="Generated prop";prop.tiles.clear();
    for(int ty=0;ty<2;++ty)for(int tx=0;tx<2;++tx){
        QImage image(tileSize,tileSize,QImage::Format_ARGB32_Premultiplied);image.fill(Qt::transparent);
        QPainter painter(&image);painter.setRenderHint(QPainter::Antialiasing);painter.translate(-tx*tileSize,-ty*tileSize);
        painter.setPen(QPen(QColor(235,150,80),12));painter.setBrush(QColor(65,165,205,230));painter.drawEllipse(QRectF(22,22,468,468));
        painter.setPen(Qt::NoPen);painter.setBrush(QColor(255,225,100));painter.drawRect(QRectF(205,38,102,230));painter.end();
        prop.tiles.insert({tx,ty},image);
    }
    prop.effects.glowEnabled=true;prop.effects.glowRadius=48;prop.effects.glowOpacity=.8;prop.effects.glowColor=QColor(255,190,85);
    symbol.content.layerAnimations={{prop.id,{256,256},{{KeyframeProperty::Rotation,{{0,0},{47,350}}}}}};
    project.symbols={symbol};auto instance=RasterLayer::create("Looping Symbol");instance.kind=LayerKind::Symbol;instance.symbol=SymbolInstance{symbol.id};
    shot.layers.append(instance);
    shot.layerAnimations.append({instance.id,{256,256},{{KeyframeProperty::PositionX,{{0,1000},{119,2000}}},
        {KeyframeProperty::PositionY,{{0,600},{59,1000},{119,600}}}}});
    return project;
}
bool pending(const Canvas &canvas,const Navigator &navigator){
    return canvas.property("effectsPreviewPending").toBool()||navigator.property("effectsPreviewPending").toBool();
}
void settle(Canvas &canvas,Navigator &navigator){
    QEventLoop loop;QTimer poll,deadline;QElapsedTimer quiet;bool timedOut=false;
    poll.setInterval(heartbeatMs);deadline.setSingleShot(true);
    QObject::connect(&poll,&QTimer::timeout,&loop,[&]{
        if(pending(canvas,navigator))quiet.invalidate();
        else if(!quiet.isValid())quiet.start();
        else if(quiet.elapsed()>=quietMs)loop.quit();
    });
    QObject::connect(&deadline,&QTimer::timeout,&loop,[&]{timedOut=true;loop.quit();});
    poll.start();deadline.start(timeoutMs);loop.exec();check(!timedOut,"Paused previews did not settle before deadline");
}
QImage imageFor(const QWidget &widget){
    const auto ratio=widget.devicePixelRatioF();
    QImage image(QSize(qCeil(widget.width()*ratio),qCeil(widget.height()*ratio)),QImage::Format_ARGB32_Premultiplied);
    image.setDevicePixelRatio(ratio);image.setDotsPerMeterX(qRound(widget.logicalDpiX()/.0254));image.setDotsPerMeterY(qRound(widget.logicalDpiY()/.0254));return image;
}
QImage referenceCanvas(const Canvas &canvas){
    auto image=imageFor(canvas);image.fill(QColor(ui::canvas));QPainter painter(&image);painter.setClipRegion(QRegion(canvas.rect()));
    painter.setTransform(canvas.viewTransform());painter.fillRect(canvas.paperRect().translated(8,10),QColor(0,0,0,65));
    paintSheet(painter,canvas.project(),canvas.displayShot(),true,nullptr,canvas.canvasSmoothing(),true,canvas.document()->currentFrame(),canvas.cameraView());return image;
}
QImage referenceNavigator(const Navigator &navigator,const Canvas &canvas){
    auto image=imageFor(navigator);image.fill(QColor(ui::base));QPainter painter(&image);painter.setTransform(navigator.miniatureTransform());
    paintSheet(painter,canvas.project(),canvas.displayShot(),true,nullptr,canvas.canvasSmoothing(),true,canvas.document()->currentFrame(),canvas.cameraView());
    QPen pen(QColor(ui::accent),2);pen.setCosmetic(true);painter.setPen(pen);QColor fill(ui::accent);fill.setAlpha(22);painter.setBrush(fill);painter.drawPolygon(canvas.visiblePolygon());return image;
}
bool sourcePreserved(const Project &before,const Project &after){
    if(before.toJson()!=after.toJson())return false;
    const auto original=projectArtworkShots(before),current=projectArtworkShots(after);
    if(original.size()!=current.size())return false;
    for(int i=0;i<original.size();++i){
        if(original[i]->layers.size()!=current[i]->layers.size())return false;
        for(int j=0;j<original[i]->layers.size();++j)
            if(original[i]->layers[j].tiles!=current[i]->layers[j].tiles||original[i]->layers[j].mask!=current[i]->layers[j].mask)return false;
    }
    return true;
}
class PaintCounter final:public QObject{
public:
    PaintCounter(Canvas *canvas,Navigator *navigator):canvas(canvas),navigator(navigator){canvas->installEventFilter(this);navigator->installEventFilter(this);}
    int canvasPaints=0,navigatorPaints=0;
protected:
    bool eventFilter(QObject *target,QEvent *event)override{
        if(event->type()==QEvent::Paint){if(target==canvas)++canvasPaints;else if(target==navigator)++navigatorPaints;}return false;
    }
private:Canvas *canvas;Navigator *navigator;
};
QJsonObject run(int divisor){
    QTemporaryDir temporary;check(temporary.isValid(),"Cannot create isolated benchmark settings");
    MainWindow window(temporary.filePath("workspace.ini"));window.resize(1600,1000);window.show();
    QString error;const auto generated=fixture();check(generated.validate().isEmpty(),generated.validate());
    const auto path=temporary.filePath("generated-playback.toonproj");check(saveProject(generated,path,&error),error);check(window.openProject(path),"Cannot open generated project");
    auto *canvas=window.canvas();auto *document=canvas->document();auto *timeline=window.findChild<TimelineController *>();
    auto *navigation=window.findChild<QDockWidget *>("controlPanel");check(timeline&&navigation,"Missing playback controller or Navigator dock");
    auto *navigator=navigation->findChild<Navigator *>();check(navigator,"Missing Navigator");
    window.removeDockWidget(navigation);window.addDockWidget(Qt::RightDockWidgetArea,navigation);navigation->setFloating(true);navigation->resize(300,320);navigation->show();navigation->raise();
    auto *timelineDock=window.findChild<QDockWidget *>("timelinePanel");if(timelineDock){timelineDock->show();timelineDock->raise();}
    QApplication::processEvents();canvas->fitSheet();timeline->setOnionSkin(false);timeline->setLooping(true);check(timeline->setFrame(0),"Cannot select starting frame");
    settle(*canvas,*navigator);check(canvas->isVisible()&&navigator->isVisible(),"Canvas and Navigator must be visible");
    const auto original=document->project();const int history=document->history()->count(),historyIndex=document->history()->index();
    canvas->setPlaybackPreviewDivisor(divisor);
    check(canvas->playbackPreviewDivisor()==divisor,"Playback preview quality did not retain the requested divisor");
    auto *canvasRenderer=canvas->findChild<EffectsPreviewRenderer *>();auto *navigatorRenderer=navigator->findChild<EffectsPreviewRenderer *>();check(canvasRenderer&&navigatorRenderer,"Missing preview renderers");
    QEventLoop measured;QTimer heartbeat,stop,deadline;QElapsedTimer total,beat;QVector<double> intervals,lateness,canvasAge,navigatorAge;
    QJsonArray delivered,canvasFrames,navigatorFrames;QString failure;bool measuring=false;int pausedFrame=-1;double playingAt=0,stoppedAt=0,stopDispatch=0;
    PaintCounter paints(canvas,navigator);
    heartbeat.setTimerType(Qt::PreciseTimer);heartbeat.setInterval(heartbeatMs);stop.setSingleShot(true);stop.setTimerType(Qt::PreciseTimer);deadline.setSingleShot(true);
    QObject::connect(&heartbeat,&QTimer::timeout,&measured,[&]{const auto interval=elapsed(beat);beat.restart();intervals.append(interval);lateness.append(qMax(0.,interval-heartbeatMs));});
    QObject::connect(document,&Document::frameChanged,&measured,[&]{if(measuring)delivered.append(QJsonObject{{"at_ms",elapsed(total)},{"frame",document->currentFrame()}});});
    const auto record=[&](QWidget *view,const EffectsPreviewResult &result,QJsonArray &frames,QVector<double> &ages){
        if(!measuring||!result.playback||result.cancelled||!result.error.isEmpty()||result.image.isNull())return;
        // Canvas/Navigator's own earlier connection must have accepted it.
        if(view->property("effectsPreviewPublishedRevision").toULongLong()!=result.revision||view->property("effectsPreviewFrame").toInt()!=result.frame)return;
        const int ageFrames=(timeline->currentFrame()-result.frame+document->activeShot().durationFrames)%document->activeShot().durationFrames;
        const double ageMs=ageFrames*1000./document->project().fps;ages.append(ageMs);
        frames.append(QJsonObject{{"at_ms",elapsed(total)},{"frame",result.frame},{"playhead",timeline->currentFrame()},
            {"revision",double(result.revision)},{"source_scale",result.sourceScale},{"display_age_frames",ageFrames},{"display_age_ms",ageMs}});
    };
    QObject::connect(canvasRenderer,&EffectsPreviewRenderer::completed,&measured,[&](const auto &result){record(canvas,result,canvasFrames,canvasAge);});
    QObject::connect(navigatorRenderer,&EffectsPreviewRenderer::completed,&measured,[&](const auto &result){record(navigator,result,navigatorFrames,navigatorAge);});
    QObject::connect(&stop,&QTimer::timeout,&measured,[&]{
        if(!timeline->playing())failure="Playback stopped before the requested measurement interval";
        measuring=false;stoppedAt=elapsed(total);QElapsedTimer dispatch;dispatch.start();timeline->setPlaying(false);stopDispatch=elapsed(dispatch);
        pausedFrame=timeline->currentFrame();heartbeat.stop();measured.quit();
    });
    QObject::connect(&deadline,&QTimer::timeout,&measured,[&]{failure="Playback measurement exceeded deadline";measuring=false;timeline->setPlaying(false);measured.quit();});
    // Let Qt paint normally. The timer/controller, effects workers and visible
    // workspace all participate; no forced repaint or grab inside this window.
    total.start();beat.start();measuring=true;heartbeat.start();timeline->setPlaying(true);playingAt=elapsed(total);
    check(timeline->playing(),"Playback did not start");stop.start(playbackMs);deadline.start(timeoutMs);measured.exec();
    heartbeat.stop();stop.stop();deadline.stop();check(failure.isEmpty(),failure);
    const int canvasPaints=paints.canvasPaints,navigatorPaints=paints.navigatorPaints;
    QElapsedTimer settling;settling.start();settle(*canvas,*navigator);const auto settleMs=elapsed(settling);
    check(document->currentFrame()==pausedFrame,"Paused playhead changed while settling");
    check(canvas->property("effectsPreviewFrame").toInt()==pausedFrame&&navigator->property("effectsPreviewFrame").toInt()==pausedFrame,"Paused previews did not reach the selected frame");
    check(canvas->property("effectsPreviewExact").toBool()&&navigator->property("effectsPreviewExact").toBool(),"Paused previews are not exact");
    check(canvas->property("effectsPreviewSourceScale").toDouble()==1&&navigator->property("effectsPreviewSourceScale").toDouble()==1,"Paused previews did not restore full source resolution");
    QEvent leave(QEvent::Leave);QApplication::sendEvent(canvas,&leave);
    const auto canvasImage=canvas->grab().toImage().convertToFormat(QImage::Format_ARGB32_Premultiplied),navigatorImage=navigator->grab().toImage().convertToFormat(QImage::Format_ARGB32_Premultiplied);
    check(!pending(*canvas,*navigator),"Final captures unexpectedly invalidated the paused previews");
    const bool canvasExact=canvasImage==referenceCanvas(*canvas),navigatorExact=navigatorImage==referenceNavigator(*navigator,*canvas);
    check(canvasExact&&navigatorExact,"Paused viewport differs from synchronous exact rendering");
    const bool preserved=sourcePreserved(original,document->project());check(preserved,"Playback changed source artwork");
    check(document->history()->count()==history&&document->history()->index()==historyIndex,"Playback changed Undo history");
    const auto duration=stoppedAt-playingAt;
    return {{"profile","heavy_4k_blur_glow_animated_symbol_navigator_visible"},{"requested_playback_ms",playbackMs},{"actual_playback_ms",duration},
        {"requested_playback_divisor",divisor},{"requested_source_scale",1./divisor},
        {"canvas_effective_source_scales",effectiveScales(canvasFrames)},{"navigator_effective_source_scales",effectiveScales(navigatorFrames)},
        {"heartbeat_interval_ms",heartbeatMs},{"heartbeat_interval",statistics(intervals)},{"heartbeat_lateness",statistics(lateness)},
        {"delivered_playhead_frames",delivered.size()},{"playhead_events",delivered},{"canvas_published_frames",canvasFrames.size()},{"canvas_publications",canvasFrames},
        {"navigator_published_frames",navigatorFrames.size()},{"navigator_publications",navigatorFrames},{"canvas_display_fps",duration>0?canvasFrames.size()*1000./duration:0},
        {"navigator_display_fps",duration>0?navigatorFrames.size()*1000./duration:0},{"display_progress",!canvasFrames.isEmpty()&&!navigatorFrames.isEmpty()},
        {"canvas_display_age",statistics(canvasAge)},{"navigator_display_age",statistics(navigatorAge)},
        {"canvas_paints",canvasPaints},{"navigator_paints",navigatorPaints},{"paused_frame",pausedFrame},{"stop_dispatch_ms",stopDispatch},{"paused_exact_settle_ms",settleMs},
        {"canvas_matches_exact_reference",canvasExact},{"navigator_matches_exact_reference",navigatorExact},{"source_preserved",preserved},{"history_unchanged",true},
        {"paused_source_scale",1},
        {"resolution",QJsonArray{3840,2160}},{"fps",24},{"source_tiles",projectTileCount(original)},{"canvas_logical_size",QJsonArray{canvas->width(),canvas->height()}},
        {"canvas_pixel_size",QJsonArray{canvasImage.width(),canvasImage.height()}},{"device_pixel_ratio",canvas->devicePixelRatioF()},{"platform",QApplication::platformName()},
        {"qt",QString::fromLatin1(qVersion())},{"os",QSysInfo::prettyProductName()},{"physical_tablet_test",false}};
}
}
int main(int argc,char **argv){
    QApplication app(argc,argv);applyTheme();
    try{
        const auto requested=app.arguments().value(1,"1");bool numeric=false;const int divisor=requested.toInt(&numeric);
        check(requested=="all"||(numeric&&(divisor==1||divisor==2||divisor==4||divisor==8)),"Usage: toon_animation_playback_bench [1|2|4|8|all]");
        // One JSON line per requested quality. All profiles execute sequentially
        // with fresh temporary projects; no jobs compete between profiles.
        const QVector<int> divisors=requested=="all"?QVector<int>{1,2,4,8}:QVector<int>{divisor};
        for(int current:divisors)std::cout<<QJsonDocument(run(current)).toJson(QJsonDocument::Compact).constData()<<std::endl;
    }
    catch(const std::exception &error){std::cerr<<error.what()<<std::endl;return 1;}return 0;
}
