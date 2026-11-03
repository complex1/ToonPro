// Opt-in, isolated benchmark. No user documents, settings or native windows.
#include "document.h"
#include "object_tools.h"
#include <QGuiApplication>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QJsonObject>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>

using namespace toon;
namespace {
QImage pattern(int side) {
    QImage image(side, side, QImage::Format_Grayscale8);
    for (int y=0; y<side; ++y) for (int x=0; x<side; ++x)
        image.scanLine(y)[x]=uchar((x*31+y*17+x*y*3)%256);
    return image;
}
void run(bool rich) {
    Project project=Project::create(); project.width=1920; project.height=1080; project.transparent=true;
    project.shots[0].layers[0].kind=LayerKind::Vector;
    Document document; document.load(project);
    BrushSettings brush; brush.size=28; brush.pressureSize=false; brush.spacing=.35;
    brush.strokeOpacity=.7;
    if (rich) {
        brush.tipMask=pattern(64); brush.noise=true; brush.wetEdges=true;
        brush.angleDynamics.jitter=.3; brush.roundness=.75;
        brush.texture.enabled=true; brush.texture.pattern=pattern(16); brush.texture.depth=.6;
        brush.dual.enabled=true; brush.dual.tipMask=pattern(24); brush.dual.size=16;
    }
    constexpr int strokes=48, previews=7;
    for (int i=0; i<strokes; ++i) {
        brush.color=QColor::fromHsv((i*29)%360,180,200);
        const QPointF start(70+(i%6)*290,70+(i/6)*120);
        if (!document.beginStroke(start,1,brush,false)) throw std::runtime_error("Cannot begin benchmark stroke");
        for (int p=1; p<=16; ++p) document.continueStroke(start+QPointF(p*13,std::sin(p*.4)*24),1);
        document.endStroke();
    }
    const auto original=document.project().shots[0].layers[0].objects.first();
    ObjectTools tools(&document);
    if (!tools.beginEdit(original.id)) throw std::runtime_error("Cannot begin benchmark path edit");
    QVector<double> times;
    for (int i=0; i<previews; ++i) {
        auto object=original; object.transform.translate(2+i*2,1+i);
        QElapsedTimer timer; timer.start();
        if (!tools.previewEdit(object)) throw std::runtime_error("Cannot preview benchmark path edit");
        times.append(timer.nsecsElapsed()/1000000.);
    }
    const double first=times.takeFirst(); std::sort(times.begin(),times.end()); tools.cancelEdit();
    QJsonObject result{{"profile",rich?"sampled_texture_dual":"round"},{"resolution","1920x1080"},
        {"strokes",strokes},{"edited_stroke",0},{"previews",previews},{"first_ms",first},
        {"warm_median_ms",times[times.size()/2]},{"warm_min_ms",times.first()},{"warm_max_ms",times.last()}};
    std::cout << QJsonDocument(result).toJson(QJsonDocument::Compact).constData() << std::endl;
}
}
int main(int argc,char **argv) {
    QGuiApplication application(argc,argv);
    try { run(false); run(true); }
    catch (const std::exception &error) { std::cerr << error.what() << std::endl; return 1; }
    return 0;
}
