#include "specialized_catalog_brushes.h"
#include "brush_engine.h"
#include <QPainter>
#include <QPainterPath>
#include <QRadialGradient>
#include <cmath>
namespace toon {
namespace {
struct Entry { const char *id, *name, *family; };
constexpr Entry entries[]{
    {"clustered-aerosol", "Clustered Aerosol", "Soft paint and spray"},
    {"ground-vine", "Ground Vine", "Grass and ground cover"},
    {"bamboo-trunks", "Bamboo Trunks", "Trees and foliage"},
    {"hanging-ivy", "Hanging Ivy", "Trees and foliage"},
    {"smoke-trail", "Smoke Trail", "Clouds and atmosphere"},
    {"window-row", "Window Row", "Architecture and repeating details"},
    {"apartment-window-grid", "Apartment Window Grid", "Architecture and repeating details"},
    {"balcony-row", "Balcony Row", "Architecture and repeating details"},
    {"fence-posts", "Fence Posts", "Architecture and repeating details"},
    {"roof-tile-border", "Roof Tile Border", "Architecture and repeating details"},
    {"brick-perspective-wall", "Brick Perspective Wall", "Architecture and repeating details"},
    {"stone-perspective-path", "Stone Perspective Path", "Architecture and repeating details"},
    {"shopfront-modules", "Shopfront Modules", "Architecture and repeating details"},
    {"utility-insulators", "Utility Insulators", "Architecture and repeating details"},
    {"railings-and-bars", "Railings and Bars", "Architecture and repeating details"},
    {"cable-bundle", "Cable Bundle", "Architecture and repeating details"},
    {"roofline-modules", "Roofline Modules", "Architecture and repeating details"},
    {"shore-foam-edge", "Shore Foam Edge", "Water and reflections"},
    {"stream-highlight", "Stream Highlight", "Water and reflections"},
    {"fine-dot-tone", "Fine Dot Tone", "Manga tones and hatching"},
    {"coarse-dot-tone", "Coarse Dot Tone", "Manga tones and hatching"},
    {"line-tone", "Line Tone", "Manga tones and hatching"},
    {"cross-tone", "Cross Tone", "Manga tones and hatching"},
    {"diamond-tone", "Diamond Tone", "Manga tones and hatching"},
    {"noise-tone", "Noise Tone", "Manga tones and hatching"},
    {"tone-fade", "Tone Fade", "Manga tones and hatching"},
    {"curved-hatch-band", "Curved Hatch Band", "Manga tones and hatching"},
    {"rope-border", "Rope Border", "Manga effects and decoration"},
    {"chain-border", "Chain Border", "Manga effects and decoration"},
    {"skyline-builder", "Skyline Builder", "Dynamic subject systems"},
    {"house-row-builder", "House Row Builder", "Dynamic subject systems"},
    {"seasonal-foliage-tint", "Seasonal Foliage Tint", "Dynamic subject systems"},
    {"ribbon-ornament", "Ribbon Ornament", "Dynamic subject systems"},
    {"speed-responding-ink", "Speed Responding Ink", "Dynamic subject systems"},
};
constexpr qreal pi=3.14159265358979323846;
QImage mask(const QImage &image) {
    QImage result(image.size(),QImage::Format_Grayscale8);
    for(int y=0;y<image.height();++y)for(int x=0;x<image.width();++x)result.scanLine(y)[x]=qAlpha(image.pixel(x,y));
    return result;
}
QPainterPath leaf(QPointF root,QPointF tip,qreal width) {
    const QPointF d=tip-root;const qreal length=std::hypot(d.x(),d.y());
    const QPointF n(-d.y()/qMax(1.,length)*width,d.x()/qMax(1.,length)*width);
    QPainterPath p(root);p.cubicTo(root+d*.25+n,root+d*.7+n,tip);p.cubicTo(root+d*.72-n,root+d*.25-n,root);return p;
}
// Each strip has authored joining pixels at both ends, on transparent paper.
QImage strip(const QString &id,int variant,bool corrected) {
    QImage image(256,128,QImage::Format_ARGB32_Premultiplied);image.fill(Qt::transparent);
    QPainter p(&image);p.setRenderHint(QPainter::Antialiasing);p.setPen(QPen(Qt::white,3,Qt::SolidLine,Qt::RoundCap,Qt::RoundJoin));p.setBrush(Qt::NoBrush);
    const auto line=[&](qreal x1,qreal y1,qreal x2,qreal y2){p.drawLine(QPointF(x1,y1),QPointF(x2,y2));};
    if(id=="rope-border") {
        for(int side:{-1,1}) {QPainterPath path;for(int x=-2;x<=258;++x){const qreal y=64+side*23+std::sin(x*pi/64)*6;if(x==-2)path.moveTo(x,y);else path.lineTo(x,y);}p.drawPath(path);}
        for(int x=-32;x<288;x+=32){QPainterPath twist({qreal(x),40});twist.cubicTo(x+25,50,x+5,78,x+32,88);p.drawPath(twist);}
    } else if(id=="chain-border") {
        p.setPen(QPen(Qt::white,5));for(int x=-32;x<288;x+=64){p.drawEllipse(QRectF(x,46+(variant%2)*7,76,36-(variant%2)*14));}
    } else if(id=="cable-bundle") {
        p.setPen(QPen(Qt::white,3));for(int y:{35,48,64,80,93})line(-2,y,258,y);
    } else if(id=="ground-vine"||id=="hanging-ivy") {
        QPainterPath stem({-2,64});stem.cubicTo(70,64,55,48+variant*5,128,64);stem.cubicTo(185,80-variant*5,195,64,258,64);p.drawPath(stem);
        p.setBrush(Qt::white);for(int x=24;x<256;x+=40){const int side=(x/40)%2?1:-1;const QPointF root(x,64),tip(x+24+variant*2,64+side*((id=="ground-vine"?18:32)+variant*3));p.drawPath(leaf(root,tip,8+variant));}
    } else if(id=="smoke-trail") {
        p.setPen(Qt::NoPen);
        for(int x=-64;x<320;x+=32){const qreal y=64+std::sin(x*pi/128)*(8+variant*3);QRadialGradient g(QPointF(x,y),46);g.setColorAt(0,QColor(255,255,255,100));g.setColorAt(.5,QColor(255,255,255,55));g.setColorAt(1,Qt::transparent);p.setBrush(g);p.drawEllipse(QPointF(x,y),60,46);}
    } else if(id=="shore-foam-edge") {
        for(int x=-32;x<288;x+=32){QPainterPath edge({qreal(x),68});edge.cubicTo(x+8,35-variant*3,x+24,35-variant*3,x+32,68);p.drawPath(edge);}
        for(int x=12;x<250;x+=28)p.drawEllipse(QPointF(x,83+(x%3)*5),3+variant,2);
    } else if(id=="stream-highlight") {
        for(int j=0;j<4;++j){QPainterPath path({-2.,40.+j*15});path.cubicTo(65,24+j*15+variant*3,190,56+j*15-variant*3,258,40+j*15);p.setPen(QPen(Qt::white,j==1?4:1.5));p.drawPath(path);}
    } else if(id=="curved-hatch-band") {
        p.setPen(QPen(Qt::white,1.5));for(int x=-64;x<320;x+=16)line(x,99,x+60,29);
    } else if(id=="ribbon-ornament") {
        line(-2,37,258,37);line(-2,91,258,91);for(int x=32;x<256;x+=64){QPainterPath diamond;diamond.moveTo(x,46);diamond.lineTo(x+22,64);diamond.lineTo(x,82);diamond.lineTo(x-22,64);diamond.closeSubpath();p.drawPath(diamond);p.drawEllipse(QPointF(x,64),4+variant*2,4+variant*2);}
    } else if(id=="bamboo-trunks") {
        line(-2,40,258,40);line(-2,88,258,88);for(int x=0;x<=256;x+=128){p.drawArc(QRectF(x-6,37,12,54),-90*16,180*16);line(x+5,42,x+5,86);}
    } else if(id=="window-row") {
        line(-2,104,258,104);for(int x=12;x<256;x+=64){p.drawRect(QRectF(x,23,40,70));line(x+20,23,x+20,93);line(x,53,x+40,53);}
    } else if(id=="balcony-row") {
        line(-2,102,258,102);for(qreal x=12;x<256;x+=corrected?256./3:84.){p.drawRect(QRectF(x,26+variant*4,56,52-variant*4));p.drawRect(QRectF(x-5,62,66,34));for(int k=0;k<6;++k)line(x+k*10,62,x+k*10,96);}
    } else if(id=="fence-posts") {
        for(int y:{53,86}){line(-2,y,258,y);line(-2,y+7,258,y+7);}p.setBrush(Qt::white);for(int x=0;x<(corrected?257:256);x+=64){QPainterPath post({qreal(x-4),112});post.lineTo(x-4,31);post.lineTo(x,22);post.lineTo(x+4,31);post.lineTo(x+4,112);post.closeSubpath();p.drawPath(post);}
    } else if(id=="roof-tile-border") {
        for(int x=-32;x<288;x+=64){QPainterPath tile({qreal(x),42});tile.lineTo(x+8,82);tile.quadTo(x+32,100+variant*3,x+56,82);tile.lineTo(x+64,42);p.drawPath(tile);}line(-2,42,258,42);
    } else if(id=="utility-insulators") {
        line(-2,64,258,64);for(int x=32;x<256;x+=64)for(int k=0;k<4;++k)p.drawEllipse(QRectF(x-12+k,42+k*8,24-k*2,7));
    } else if(id=="railings-and-bars") {
        line(-2,33,258,33);line(-2,99,258,99);for(int x=0;x<(corrected?257:256);x+=32){line(x,33,x,99);p.drawEllipse(QRectF(x+5,51,22,30));}
    } else if(id=="roofline-modules") {
        line(-2,108,258,108);for(int x=0;x<256;x+=128){QPainterPath roof({qreal(x),82});roof.lineTo(x+64,25+variant*11);roof.lineTo(x+128,82);roof.lineTo(x+119,82);roof.lineTo(x+119,108);roof.moveTo(x+9,108);roof.lineTo(x+9,82);roof.lineTo(x,82);p.drawPath(roof);line(x+85,36,x+85,16);line(x+85,16,x+98,16);line(x+98,16,x+98,51);}
    } else if(id=="shopfront-modules") {
        line(-2,112,258,112);for(int x=0;x<256;x+=128){p.drawRect(QRectF(x+8,28,112,82));p.drawRect(QRectF(x+14,36,100,20));for(int k=0;k<5;++k){p.drawRect(QRectF(x+14+k*20,57,20,9+variant*2));}p.drawRect(QRectF(x+16,74,58,28));p.drawRect(QRectF(x+82,71,28,39));}
    } else if(id=="skyline-builder") {
        line(-2,112,258,112);for(int x=0;x<256;x+=64){const int h=44+(x/64*17+variant*19)%58;p.drawRect(QRectF(x,112-h,64,h));for(int yy=120-h;yy<105;yy+=15)for(int xx=x+10;xx<x+59;xx+=15)p.drawRect(QRectF(xx,yy,6,7));}
    } else if(id=="house-row-builder") {
        line(-2,112,258,112);for(int x=0;x<256;x+=128){QPainterPath house({qreal(x),70});house.lineTo(x+64,20+variant*8);house.lineTo(x+128,70);house.lineTo(x+119,70);house.lineTo(x+119,112);house.lineTo(x+9,112);house.lineTo(x+9,70);house.closeSubpath();p.drawPath(house);p.drawRect(QRectF(x+24,76,25,20));p.drawRect(QRectF(x+76,74,24,38));}
    }
    p.end();return mask(image);
}
QImage surfaceTile(const QString &id,bool corrected) {
    QImage image(256,256,QImage::Format_ARGB32_Premultiplied);image.fill(Qt::transparent);QPainter p(&image);p.setRenderHint(QPainter::Antialiasing);p.setPen(QPen(QColor("#283440"),2));p.setBrush(Qt::NoBrush);
    if(id=="apartment-window-grid") {
        for(int y=16;y<256;y+=64)for(int x=20;x<256;x+=64){p.setBrush(QColor("#7196ac"));p.drawRect(QRectF(x,y,30,40));p.setBrush(Qt::NoBrush);p.drawLine(x+15,y,x+15,y+40);p.drawLine(x,y+20,x+30,y+20);p.drawLine(x-3,y+44,x+33,y+44);}
    } else if(id=="brick-perspective-wall") {
        for(int y=0;y<=256;y+=32){p.drawLine(0,y,256,y);for(int x=(y/32%2)*32;x<=256;x+=64)p.drawLine(x,y,x,y+32);}
    } else {
        for(int y=corrected?-64:0;y<(corrected?257:256);y+=64)for(int x=corrected?-64:0;x<(corrected?257:256);x+=64){const int dx=((y/64+2)%2)*32;QPainterPath path({qreal(x+dx-4),qreal(y)});path.lineTo(x+dx+58,y+4);path.lineTo(x+dx+64,y+55);path.lineTo(x+dx+3,y+64);path.closeSubpath();p.drawPath(path);}
    }p.end();return image;
}
QImage paintedLeaf(int variant) {
    QImage image(128,128,QImage::Format_ARGB32_Premultiplied);image.fill(Qt::transparent);QPainter p(&image);p.setRenderHint(QPainter::Antialiasing);
    QLinearGradient g(24,80,95,45);g.setColorAt(0,QColor("#315743"));g.setColorAt(.45,QColor("#77a65b"));g.setColorAt(1,QColor("#adc476"));
    p.setPen(Qt::NoPen);p.setBrush(g);p.drawPath(leaf({20,106},{105.-variant*5,18.+variant*6},29+variant*3));
    p.setPen(QPen(QColor("#c4d99a"),1.5));p.drawLine(QPointF(20,106),QPointF(105-variant*5,18+variant*6));return image;
}
BrushPreset make(const Entry &e,bool corrected) {
    BrushPreset preset;preset.id="builtin/catalog/"+QString::fromLatin1(e.id);preset.name=e.name;preset.builtIn=true;
    preset.source="Original Toon Pro procedural brush artwork · specialized catalog";preset.smoothing=8;
    const QString id=e.id;auto &b=preset.brush;b.pressureSize=false;b.size=64;b.spacing=.08;
    if(id=="clustered-aerosol") {
        b.size=4;b.hardness=.6;b.spacing=.8;b.count=12;b.opacity=.32;b.clusterSpray.enabled=true;b.clusterSpray.radius=5;b.clusterSpray.spread=.8;b.clusterSpray.radialBias=.35;b.sizeDynamics.jitter=.6;
    } else if(id=="speed-responding-ink") {
        b.size=16;b.spacing=.02;b.pressureSize=true;b.speedDynamics.enabled=true;b.speedDynamics.referenceSpeed=900;b.speedDynamics.sizeAmount=-.65;b.speedDynamics.opacityAmount=-.15;
    } else if(id=="seasonal-foliage-tint") {
        QVector<QImage> images;for(int i=0;i<4;++i)images.append(paintedLeaf(i));setBrushTipImages(b,images);b.size=64;b.spacing=.7;b.scatter=.2;b.sizeDynamics.jitter=.25;b.angleDynamics.jitter=.12;b.colorDynamics.tintAmount=.8;b.colorDynamics.applyToColorTip=true;b.colorDynamics.enabled=true;b.colorDynamics.hue=.04;b.colorDynamics.saturation=.12;
    } else if(QString::fromLatin1(e.family)=="Manga tones and hatching" && id!="curved-hatch-band") {
        b.size=120;b.hardness=1;b.surface.angle=45;b.surface.frequency=10;b.surface.density=.3;
        b.surface.kind=id=="line-tone"?BrushSurfaceKind::Lines:id=="cross-tone"?BrushSurfaceKind::Crosshatch:id=="diamond-tone"?BrushSurfaceKind::Diamond:id=="noise-tone"?BrushSurfaceKind::Noise:id=="tone-fade"?BrushSurfaceKind::FadeDots:BrushSurfaceKind::Dots;
        if(id=="coarse-dot-tone"){b.surface.frequency=5;b.surface.density=.42;}
        if(id=="line-tone")b.surface.angle=30;
        if(id=="cross-tone"){b.surface.frequency=7;b.surface.density=.25;}
        if(id=="diamond-tone")b.surface.frequency=8;
        if(id=="noise-tone")b.surface.frequency=40;
        if(id=="tone-fade"){b.surface.density=.65;b.surface.densityEnd=.05;b.surface.fadeLength=512;b.surface.angle=0;}
    } else if(id=="apartment-window-grid"||id.contains("perspective")) {
        b.size=180;b.surface.kind=BrushSurfaceKind::Image;b.surface.image=surfaceTile(id,corrected);b.surface.tileSize={256,256};
        if(id.contains("perspective")){b.surface.perspective=true;b.surface.quad={QPointF(0,0),QPointF(512,100),QPointF(512,412),QPointF(0,512)};}
    } else {
        b.connected.mode=(QString::fromLatin1(e.family)=="Architecture and repeating details"||id=="bamboo-trunks"||id.contains("builder"))?BrushConnectedMode::Repeat:BrushConnectedMode::Ribbon;
        b.connected.repeatLength=b.connected.mode==BrushConnectedMode::Repeat?2:1;
        b.size=id=="rope-border"?28:id=="chain-border"?32:id=="cable-bundle"?24:id=="smoke-trail"?110:id.contains("builder")?120:64;
        const bool varied=id=="ground-vine"||id=="hanging-ivy"||id=="smoke-trail"||id=="balcony-row"||id=="roof-tile-border"||id=="roofline-modules"||id=="skyline-builder"||id=="house-row-builder"||id=="ribbon-ornament"||id=="shore-foam-edge"||id=="stream-highlight";
        if(varied){QVector<QImage> images;for(int i=0;i<3;++i)images.append(strip(id,i,corrected));setBrushTipImages(b,images);b.tipOrder=BrushTipOrder::Ordered;}
        else b.tipMask=strip(id,0,corrected);
        if(id=="smoke-trail")b.opacity=.7;
    }
    return preset;
}
const Entry *entry(const QString &id) {for(const auto &e:entries)if(id=="builtin/catalog/"+QString::fromLatin1(e.id))return &e;return nullptr;}
}
QVector<BrushPreset> specializedCatalogBrushPresets(bool corrected){
    const auto build=[](bool fixes){QVector<BrushPreset> result;for(const auto &e:entries)result.append(make(e,fixes));return result;};
    if(!corrected){static const auto previous=build(false);return previous;}
    static const auto current=build(true);return current;
}
QString specializedCatalogBrushCategory(const QString &id){const auto *e=entry(id);return e?QString::fromLatin1(e->family):QString();}
QStringList specializedCatalogBrushTags(const QString &id){const auto *e=entry(id);if(!e)return {};const auto all=specializedCatalogBrushPresets();const auto &b=all[e-entries].brush;QStringList tags{"Standard","Default","Anime",QString::fromLatin1(e->family)};if(b.connected.mode!=BrushConnectedMode::Off)tags.append({"Connected","Repeat"});if(b.surface.kind!=BrushSurfaceKind::Off)tags.append({"Pattern","Tone"});if(b.surface.perspective)tags.append("Perspective");if(b.clusterSpray.enabled||b.speedDynamics.enabled||b.colorDynamics.tintAmount>0)tags.append("Dynamic");return tags;}
}
