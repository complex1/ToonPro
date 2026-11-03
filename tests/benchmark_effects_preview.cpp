// Opt-in real-workspace Effects responsiveness probe. All files/settings and
// artwork are generated in a temporary directory. Rendering/export verification
// happens outside the heartbeat interval; Qt paints the live workspace naturally.
#include "main_window.h"
#include "canvas.h"
#include "document.h"
#include "effect_render.h"
#include "effects_panel.h"
#include "ui_theme.h"
#include "value_slider.h"
#include <QApplication>
#include <QCryptographicHash>
#include <QDockWidget>
#include <QDir>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSlider>
#include <QSysInfo>
#include <QTabBar>
#include <QTemporaryDir>
#include <QTimer>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>
using namespace toon;
namespace {
constexpr int heartbeatMs=10,stepMs=40,stepCount=12,quietMs=80,timeoutMs=180000;
void check(bool ok,const QString &message){if(!ok)throw std::runtime_error(message.toStdString());}
double elapsed(const QElapsedTimer &timer){return timer.nsecsElapsed()/1000000.;}
QString hash(const QImage &image){return QString::fromLatin1(QCryptographicHash::hash(QByteArrayView(reinterpret_cast<const char *>(image.constBits()),image.sizeInBytes()),QCryptographicHash::Sha256).toHex());}
QJsonObject statistics(QVector<double> values){
    check(!values.isEmpty(),"No timing samples");std::sort(values.begin(),values.end());
    return {{"samples",values.size()},{"min_ms",values.first()},{"median_ms",values[values.size()/2]},
        {"p95_ms",values[qMin(values.size()-1,qsizetype(std::ceil(values.size()*.95))-1)]},{"max_ms",values.last()}};
}
Project fixture(){
    auto project=Project::create();project.width=3840;project.height=2160;project.transparent=true;
    auto &shot=project.shots[0];shot.sheet.overscan=0;shot.layers.resize(1);auto &layer=shot.layers[0];layer.name="Generated Effects preview fixture";layer.tiles.clear();
    for(int ty=0;ty*tileSize<project.height;++ty)for(int tx=0;tx*tileSize<project.width;++tx){
        QImage image(tileSize,tileSize,QImage::Format_ARGB32_Premultiplied);image.fill(Qt::transparent);
        for(int y=0;y<tileSize;++y){auto *row=reinterpret_cast<QRgb *>(image.scanLine(y));const int py=ty*tileSize+y;
            for(int x=0;x<tileSize;++x){const int px=tx*tileSize+x;if(px<23||py<19||px>=project.width-18||py>=project.height-13)continue;
                const int alpha=(px/19+py/23)%9==0?0:40+(px*7+py*11)%216;
                row[x]=qPremultiply(qRgba((px/3+py/7)%256,(px/13+py/5)%256,(px/17+py/11)%256,alpha));
            }
        }layer.tiles.insert({tx,ty},image);
    }
    layer.mask=LayerMask{};layer.mask->defaultValue=224;QImage mask(tileSize,tileSize,QImage::Format_Grayscale8);
    for(int y=0;y<tileSize;++y)for(int x=0;x<tileSize;++x)mask.scanLine(y)[x]=uchar((x+2*y)%256);layer.mask->tiles.insert({1,1},mask);
    auto &fx=layer.effects;fx.blurEnabled=true;fx.blurRadius=24;fx.shadowEnabled=true;fx.shadowRadius=64;fx.shadowOffset={33.25,-6.75};fx.shadowOpacity=.63;fx.shadowColor=QColor(40,72,104);
    fx.colorEnabled=true;fx.exposure=.65;fx.contrast=13;fx.saturation=24;fx.hue=34;return project;
}
QImage artwork(const Project &project){
    clearEffectRenderError();QImage image(project.width,project.height,QImage::Format_ARGB32_Premultiplied);image.fill(Qt::transparent);
    {QPainter painter(&image);painter.setClipRect(image.rect());paintLayers(painter,project.shots[0].layers,true);}
    check(effectRenderError().isEmpty(),effectRenderError());return image;
}
QImage referenceCanvas(const Canvas &canvas){
    const qreal ratio=canvas.devicePixelRatioF();QImage image(QSize(qCeil(canvas.width()*ratio),qCeil(canvas.height()*ratio)),QImage::Format_ARGB32_Premultiplied);
    image.setDevicePixelRatio(ratio);image.setDotsPerMeterX(qRound(canvas.logicalDpiX()/.0254));image.setDotsPerMeterY(qRound(canvas.logicalDpiY()/.0254));image.fill(QColor(ui::canvas));
    QPainter painter(&image);painter.setClipRegion(QRegion(canvas.rect()));painter.setTransform(canvas.viewTransform());painter.fillRect(canvas.paperRect().translated(8,10),QColor(0,0,0,65));
    paintSheet(painter,canvas.project(),canvas.displayShot(),true,nullptr,canvas.canvasSmoothing(),true,canvas.document()->currentFrame(),canvas.cameraView());painter.end();return image;
}
QString viewportDifference(const QImage &actual,const QImage &expected,bool navigator){
    const auto directory=qEnvironmentVariable("TOON_EFFECTS_DIAGNOSTICS");
    if(!directory.isEmpty()){QDir().mkpath(directory);const auto prefix=directory+(navigator?"/navigator-visible":"/navigator-hidden");actual.save(prefix+"-canvas-actual.png");expected.save(prefix+"-canvas-reference.png");}
    if(actual.size()!=expected.size())return QString("Settled Canvas size %1x%2 differs from synchronous reference %3x%4").arg(actual.width()).arg(actual.height()).arg(expected.width()).arg(expected.height());
    int count=0;QRect bounds;for(int y=0;y<actual.height();++y)for(int x=0;x<actual.width();++x)if(actual.pixel(x,y)!=expected.pixel(x,y)){++count;bounds|=QRect(x,y,1,1);}
    return QString("Settled Canvas differs from synchronous paintSheet: %1 pixels in [%2,%3 %4x%5]").arg(count).arg(bounds.x()).arg(bounds.y()).arg(bounds.width()).arg(bounds.height());
}

bool pending(Canvas *canvas,Navigator *navigator){
    // Old versions have no property, which intentionally reads as false.
    return canvas->property("effectsPreviewPending").toBool()||(navigator&&navigator->isVisible()&&navigator->property("effectsPreviewPending").toBool());
}
void settle(Canvas *canvas,Navigator *navigator){
    QEventLoop loop;QTimer poll,deadline;QElapsedTimer quiet;bool timedOut=false;poll.setTimerType(Qt::PreciseTimer);poll.setInterval(heartbeatMs);deadline.setSingleShot(true);
    QObject::connect(&poll,&QTimer::timeout,&loop,[&]{if(pending(canvas,navigator))quiet.invalidate();else if(!quiet.isValid())quiet.start();else if(quiet.elapsed()>=quietMs)loop.quit();});
    QObject::connect(&deadline,&QTimer::timeout,&loop,[&]{timedOut=true;loop.quit();});poll.start();deadline.start(timeoutMs);loop.exec();
    check(!timedOut,"Preview did not settle before deadline");
}
QImage settledCanvas(Canvas *canvas,Navigator *navigator){
    settle(canvas,navigator);QEvent leave(QEvent::Leave);QApplication::sendEvent(canvas,&leave);
    auto image=canvas->grab().toImage().convertToFormat(QImage::Format_ARGB32_Premultiplied);
    // A grab may initiate a previously unpainted view after layout changed.
    if(pending(canvas,navigator)){settle(canvas,navigator);image=canvas->grab().toImage().convertToFormat(QImage::Format_ARGB32_Premultiplied);}
    check(!pending(canvas,navigator),"Settled canvas grab requested another preview");return image;
}
class PaintCounter final:public QObject{
public:
    PaintCounter(Canvas *canvas,Navigator *navigator):canvas(canvas),navigator(navigator){canvas->installEventFilter(this);if(navigator)navigator->installEventFilter(this);}
    int canvasPaints=0,navigatorPaints=0;
protected:
    bool eventFilter(QObject *target,QEvent *event)override{if(event->type()==QEvent::Paint){if(target==canvas)++canvasPaints;else if(target==navigator)++navigatorPaints;}return false;}
private:
    Canvas *canvas;Navigator *navigator;
};
void run(bool showNavigator){
    QTemporaryDir temporary;check(temporary.isValid(),"Cannot create isolated benchmark settings");MainWindow window(temporary.filePath("workspace.ini"));window.resize(1600,1000);window.show();
    const auto original=fixture();QString error;const auto path=temporary.filePath("effects-preview.toonproj");check(saveProject(original,path,&error),error);check(window.openProject(path),"Cannot open generated project");
    auto *canvas=window.canvas();auto *document=canvas->document();auto *panel=window.findChild<EffectsPanel *>();auto *effects=window.findChild<QDockWidget *>("effectsPanel");auto *navigation=window.findChild<QDockWidget *>("controlPanel");
    check(panel&&effects&&navigation,"Missing Effects or Navigator panel");auto *navigator=navigation->findChild<Navigator *>();check(navigator,"Missing Navigator widget");
    effects->show();effects->raise();if(showNavigator){window.removeDockWidget(navigation);window.addDockWidget(Qt::RightDockWidgetArea,navigation);navigation->setFloating(true);navigation->resize(300,320);navigation->show();navigation->raise();}else navigation->hide();
    window.resize(1600,1000);panel->findChild<QTabBar *>("effectsTabs")->setCurrentIndex(1);QApplication::processEvents();canvas->fitSheet();
    const auto beforeArtwork=artwork(original);const auto beforeCanvas=settledCanvas(canvas,navigator);
    check(canvas->isVisible()&&panel->isVisible(),"Canvas/Effects are not visible");check(navigator->isVisible()==showNavigator,"Navigator visibility is not the requested profile");
    auto *field=panel->findChild<ValueSlider *>("effectBlurRadius");check(field,"Missing blur control");auto *slider=field->findChild<QSlider *>();auto *number=field->findChild<QDoubleSpinBox *>();check(slider&&number,"Missing slider or numeric field");
    const double sliderScale=std::pow(10,number->decimals());const int historyBefore=document->history()->count();PaintCounter paints(canvas,navigator);
    QEventLoop loop;QTimer heartbeat,steps,deadline;heartbeat.setTimerType(Qt::PreciseTimer);heartbeat.setInterval(heartbeatMs);steps.setTimerType(Qt::PreciseTimer);steps.setInterval(stepMs);deadline.setSingleShot(true);
    QElapsedTimer total,beatInterval,quiet;QVector<double> dispatch,intervals,lateness;QJsonArray delivered,published;QString failure;int nextStep=0;double releasedAt=-1,releaseMs=0,settledAt=0;
    quint64 observedRevision=canvas->property("effectsPreviewPublishedRevision").toULongLong();
    bool observedExact=canvas->property("effectsPreviewExact").toBool();qreal observedScale=canvas->property("effectsPreviewSourceScale").toDouble();
    int draftPublicationsDuringDrag=0;
    const auto fail=[&](const QString &message){failure=message;steps.stop();loop.quit();};
    QObject::connect(&heartbeat,&QTimer::timeout,&loop,[&]{const double interval=elapsed(beatInterval);beatInterval.restart();intervals.append(interval);lateness.append(qMax(0.,interval-heartbeatMs));
        const auto revisionProperty=canvas->property("effectsPreviewPublishedRevision");
        if(revisionProperty.isValid()) {
            const quint64 revision=revisionProperty.toULongLong();const bool exact=canvas->property("effectsPreviewExact").toBool();const qreal scale=canvas->property("effectsPreviewSourceScale").toDouble();
            if(revision!=observedRevision||exact!=observedExact||scale!=observedScale) {
                published.append(QJsonObject{{"at_ms",elapsed(total)},{"revision",double(revision)},{"exact",exact},{"source_scale",scale},{"during_drag",releasedAt<0},{"delivered_steps",nextStep}});
                if(releasedAt<0&&!exact)++draftPublicationsDuringDrag;
                observedRevision=revision;observedExact=exact;observedScale=scale;
            }
        }
        if(releasedAt<0)return;if(pending(canvas,navigator))quiet.invalidate();else if(!quiet.isValid())quiet.start();else if(quiet.elapsed()>=quietMs){settledAt=elapsed(total);loop.quit();}
    });
    QObject::connect(&steps,&QTimer::timeout,&loop,[&]{
        QElapsedTimer timer;timer.start();const qreal radius=32+nextStep*8;slider->setValue(qRound(radius*sliderScale));dispatch.append(elapsed(timer));delivered.append(elapsed(total));
        if(document->activeShot().layers[0].effects.blurRadius!=radius){fail("Slider did not update the intended radius");return;}
        if(document->history()->count()!=historyBefore){fail("Preview created an Undo entry");return;}
        if(++nextStep==stepCount){steps.stop();timer.restart();slider->setSliderDown(false);releaseMs=elapsed(timer);releasedAt=elapsed(total);
            if(document->history()->count()!=historyBefore+1||document->effectsEditActive())fail("Slider release must commit exactly once");}
    });
    QObject::connect(&deadline,&QTimer::timeout,&loop,[&]{fail("Effects preview exceeded the finite benchmark deadline");});
    // No grab, source render/export or processEvents loop during this interval.
    total.start();beatInterval.start();slider->setSliderDown(true);heartbeat.start();steps.start();deadline.start(timeoutMs);loop.exec();heartbeat.stop();steps.stop();deadline.stop();
    check(failure.isEmpty(),failure);check(nextStep==stepCount&&releasedAt>=0,"Not every slider step was delivered");check(paints.canvasPaints>0,"Qt did not paint the canvas during the measured interval");if(showNavigator)check(paints.navigatorPaints>0,"Visible Navigator did not repaint");
    const int measuredCanvasPaints=paints.canvasPaints,measuredNavigatorPaints=paints.navigatorPaints;
    const auto finalCanvas=settledCanvas(canvas,navigator);const auto canvasReference=referenceCanvas(*canvas);
    check(finalCanvas==canvasReference,finalCanvas==canvasReference?QString():viewportDifference(finalCanvas,canvasReference,showNavigator));
    const auto finalArtwork=artwork(document->project());const auto finalHash=hash(finalArtwork),canvasHash=hash(finalCanvas);
    check(document->activeShot().layers[0].tiles==original.shots[0].layers[0].tiles&&document->activeShot().layers[0].mask==original.shots[0].layers[0].mask,"Effect preview changed source pixels or mask");
    document->history()->undo();check(artwork(document->project())==beforeArtwork,"Undo did not restore full-resolution artwork");check(settledCanvas(canvas,navigator)==beforeCanvas,"Undo did not restore the settled viewport");
    document->history()->redo();check(hash(artwork(document->project()))==finalHash,"Redo changed full-resolution artwork");check(hash(settledCanvas(canvas,navigator))==canvasHash,"Redo changed the settled viewport");
    const QJsonObject result{{"profile",showNavigator?"heavy_4k_navigator_visible":"heavy_4k_navigator_hidden"},{"steps",stepCount},{"requested_step_interval_ms",stepMs},{"heartbeat_interval_ms",heartbeatMs},{"settle_quiet_ms",quietMs},
        {"accepted_preview_publications",published},{"draft_publications_during_drag",draftPublicationsDuringDrag},{"dispatch",statistics(dispatch)},{"heartbeat_interval",statistics(intervals)},{"heartbeat_lateness",statistics(lateness)},{"delivered_step_times_ms",delivered},{"steps_elapsed_ms",releasedAt},{"release_ms",releaseMs},{"final_settle_ms",settledAt-releasedAt},{"measured_total_ms",settledAt},
        {"canvas_paints",measuredCanvasPaints},{"navigator_paints",measuredNavigatorPaints},{"supports_pending_property",canvas->property("effectsPreviewPending").isValid()},{"artwork_sha256",finalHash},{"canvas_sha256",canvasHash},{"canvas_reference_sha256",hash(canvasReference)},{"canvas_matches_reference",true},{"single_undo",true},{"undo_redo_exact",true},{"source_preserved",true},
        {"resolution",QJsonArray{3840,2160}},{"canvas_logical_size",QJsonArray{canvas->width(),canvas->height()}},{"canvas_pixel_size",QJsonArray{finalCanvas.width(),finalCanvas.height()}},{"device_pixel_ratio",canvas->devicePixelRatioF()},{"platform",QApplication::platformName()},{"qt",QString::fromLatin1(qVersion())},{"os",QSysInfo::prettyProductName()}};
    std::cout<<QJsonDocument(result).toJson(QJsonDocument::Compact).constData()<<std::endl;
}
}
int main(int argc,char **argv){
    QApplication app(argc,argv);applyTheme();const auto requested=app.arguments().value(1,"all");
    try{check(requested=="all"||requested=="hidden"||requested=="navigator","Usage: effects_preview_bench [all|hidden|navigator]");if(requested!="navigator")run(false);if(requested!="hidden")run(true);}
    catch(const std::exception &error){std::cerr<<error.what()<<std::endl;return 1;}return 0;
}
