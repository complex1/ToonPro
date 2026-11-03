// Opt-in full-resolution Effects slider workloads. Generated artwork only;
// no user files, settings, native windows, reduced-quality previews or timers.
#include "document.h"
#include "effect_render.h"
#include "main_window.h"
#include "canvas.h"
#include "effects_panel.h"
#include "ui_theme.h"
#include "value_slider.h"
#include <QApplication>
#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QJsonArray>
#include <QSysInfo>
#include <QTemporaryDir>
#include <QDockWidget>
#include <QSlider>
#include <QTabBar>
#include <QToolButton>
#include <algorithm>
#include <iostream>
#include <stdexcept>
using namespace toon;
namespace {
void check(bool value,const QString &message) { if(!value)throw std::runtime_error(message.toStdString()); }
QString digest(const QImage &image) {
    return QString::fromLatin1(QCryptographicHash::hash(QByteArrayView(reinterpret_cast<const char *>(image.constBits()),image.sizeInBytes()),QCryptographicHash::Sha256).toHex());
}
QImage render(const Project &project) {
    clearEffectRenderError();
    QImage image(project.width,project.height,QImage::Format_ARGB32_Premultiplied);image.fill(Qt::transparent);
    {QPainter painter(&image);painter.setClipRect(image.rect());paintLayers(painter,project.shots[0].layers,true);}
    check(effectRenderError().isEmpty(),effectRenderError());return image;
}
Project fixture(QSize size) {
    auto project=Project::create();project.width=size.width();project.height=size.height();project.transparent=true;
    auto &shot=project.shots[0];shot.sheet.overscan=0;shot.layers.resize(1);auto &layer=shot.layers[0];layer.name="Generated Effects fixture";layer.tiles.clear();
    for(int ty=0;ty*tileSize<size.height();++ty)for(int tx=0;tx*tileSize<size.width();++tx) {
        QImage tile(tileSize,tileSize,QImage::Format_ARGB32_Premultiplied);tile.fill(Qt::transparent);
        for(int y=0;y<tileSize;++y) {auto *row=reinterpret_cast<QRgb *>(tile.scanLine(y));const int py=ty*tileSize+y;
            for(int x=0;x<tileSize;++x) {const int px=tx*tileSize+x;if(px>=size.width()-18||py>=size.height()-13||px<23||py<19)continue;
                const int alpha=((px/19+py/23)%9==0)?0:40+(px*7+py*11)%216;
                row[x]=qPremultiply(qRgba((px/3+py/7)%256,(px/13+py/5)%256,(px/17+py/11)%256,alpha));
            }
        }
        layer.tiles.insert({tx,ty},tile);
    }
    // Include a mask boundary crossing an output tile and non-opaque defaults.
    layer.mask=LayerMask{};layer.mask->defaultValue=224;
    QImage mask(tileSize,tileSize,QImage::Format_Grayscale8);
    for(int y=0;y<tileSize;++y)for(int x=0;x<tileSize;++x)mask.scanLine(y)[x]=uchar((x+2*y)%256);
    layer.mask->tiles.insert({1,1},mask);return project;
}
QJsonObject timing(QVector<double> values) {
    std::sort(values.begin(),values.end());return {{"samples",values.size()},{"min_ms",values.first()},{"median_ms",values[values.size()/2]},{"max_ms",values.last()}};
}
LayerEffects settings(const QString &profile,int sample) {
    LayerEffects fx;
    if(profile=="blur"||profile=="combined"){fx.blurEnabled=true;fx.blurRadius=24+sample*24;}
    if(profile=="shadow"||profile=="combined") {fx.shadowEnabled=true;fx.shadowRadius=32+sample*16;fx.shadowOffset={18.25+sample*7.5,-12.75+sample*3};fx.shadowOpacity=.63;fx.shadowColor=QColor(40,72,104);}
    if(profile=="glow") {fx.glowEnabled=true;fx.glowRadius=32+sample*24;fx.glowOpacity=.7;fx.glowColor=QColor(98,160,238);}
    if(profile=="color"||profile=="combined") {fx.colorEnabled=true;fx.exposure=.25+sample*.4;fx.contrast=13;fx.saturation=24;fx.hue=sample*17;}
    return fx;
}
void workload(QSize size,const QString &profile,int samples) {
    Document document;document.load(fixture(size));const auto original=document.project();const auto originalImage=render(original);
    check(document.beginEffectsEdit({},"Adjust effects"),"Cannot begin effects edit");QVector<double> previewTimes,renderTimes;QJsonArray hashes;
    for(int i=0;i<samples;++i) {
        QElapsedTimer timer;timer.start();check(document.previewLayerEffects(settings(profile,i)),"Cannot preview effects");previewTimes.append(timer.nsecsElapsed()/1000000.);
        timer.restart();const auto output=render(document.project());renderTimes.append(timer.nsecsElapsed()/1000000.);hashes.append(digest(output));
    }
    check(document.history()->count()==0,"Preview created Undo entries");document.commitEffectsEdit();check(!document.effectsEditActive(),"Effect gesture remained active after commit");
    check(document.history()->count()==1,"Effects drag must create one Undo entry");
    check(digest(render(document.project()))==hashes.last().toString(),"Committed image differs from last preview");
    document.history()->undo();check(render(document.project())==originalImage,"Undo did not restore artwork");document.history()->redo();
    check(digest(render(document.project()))==hashes.last().toString(),"Redo changed the effect output");
    check(document.activeShot().layers[0].tiles==original.shots[0].layers[0].tiles,"Effects modified source pixels");
    const QJsonObject result{{"profile",profile},{"width",size.width()},{"height",size.height()},{"render",timing(renderTimes)},
        {"document_preview",timing(previewTimes)},{"sha256",hashes},{"undo_redo_exact",true},{"source_preserved",true},{"qt",QString::fromLatin1(qVersion())},{"os",QSysInfo::prettyProductName()}};
    std::cout<<QJsonDocument(result).toJson(QJsonDocument::Compact).constData()<<std::endl;
}
void workspaceWorkload(int samples) {
    QTemporaryDir temporary;check(temporary.isValid(),"Cannot create isolated settings");applyTheme();MainWindow window(temporary.filePath("workspace.ini"));
    window.resize(1600,1000);window.show();const auto project=fixture({1920,1080});QString error;const auto path=temporary.filePath("effects.toonproj");
    check(saveProject(project,path,&error),error);check(window.openProject(path),"Cannot open generated project");window.resize(1600,1000);
    auto *canvas=window.canvas();auto *document=canvas->document();auto *panel=window.findChild<EffectsPanel *>();check(panel,"No Effects panel");
    auto *dock=window.findChild<QDockWidget *>("effectsPanel");check(dock,"No Effects dock");dock->show();dock->raise();
    canvas->fitSheet();QApplication::processEvents();
    QImage surface(window.size()*window.devicePixelRatioF(),QImage::Format_ARGB32_Premultiplied);surface.setDevicePixelRatio(window.devicePixelRatioF());window.render(&surface);
    for(const QString profile:{"blur","shadow","color"}) {
        canvas->setProject(project);QApplication::processEvents();
        const char *toggle=profile=="blur"?"effectBlurEnabled":profile=="shadow"?"effectShadowEnabled":"effectColorEnabled";
        const char *field=profile=="blur"?"effectBlurRadius":profile=="shadow"?"effectShadowRadius":"effectExposure";
        const int tab=profile=="blur"?1:profile=="shadow"?2:4;panel->findChild<QTabBar *>("effectsTabs")->setCurrentIndex(tab);
        panel->findChild<QToolButton *>(toggle)->click();QApplication::processEvents();window.render(&surface);
        auto *value=panel->findChild<ValueSlider *>(field);check(value,"No effect value");auto *slider=value->findChild<QSlider *>();check(slider,"No effect slider");
        const int before=document->history()->count();slider->setSliderDown(true);QVector<double> dispatch,display;QJsonArray hashes;
        for(int i=0;i<samples;++i) {
            QElapsedTimer timer;timer.start();slider->setValue(profile=="color"?25+i*40:(24+i*24)*10);dispatch.append(timer.nsecsElapsed()/1000000.);
            timer.restart();QApplication::processEvents();window.render(&surface);display.append(timer.nsecsElapsed()/1000000.);
            hashes.append(digest(render(document->project())));check(document->history()->count()==before,"Slider step created an Undo entry");
        }
        slider->setSliderDown(false);check(document->history()->count()==before+1,"Slider release did not commit once");
        check(digest(render(document->project()))==hashes.last().toString(),"Workspace commit changed preview");
        std::cout<<QJsonDocument(QJsonObject{{"profile",QString("workspace_%1").arg(profile)},{"width",1920},{"height",1080},
            {"preview_dispatch",timing(dispatch)},{"display_flush",timing(display)},{"sha256",hashes},
            {"device_pixel_ratio",window.devicePixelRatioF()},{"platform",QApplication::platformName()},{"single_undo",true}}).toJson(QJsonDocument::Compact).constData()<<std::endl;
    }
}

}
int main(int argc,char **argv) {
    QApplication application(argc,argv);const auto arguments=application.arguments();
    const QString requested=arguments.value(1,"all");const int samples=qBound(1,arguments.value(2,"3").toInt(),4);
    try {if(requested=="all"||requested=="workspace")workspaceWorkload(samples);for(const QSize size:{QSize(1920,1080),QSize(3840,2160)})for(const QString profile:{"blur","shadow","glow","color","combined"})
        if(requested=="all"||requested==profile)workload(size,profile,samples);
    } catch(const std::exception &error) {std::cerr<<error.what()<<std::endl;return 1;}
}
