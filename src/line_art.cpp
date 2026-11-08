#include "line_art.h"
#include "line_art_edges.h"
#include <QPainter>
#include <QColorSpace>
#include <algorithm>
#include <cmath>
#include <vector>

namespace toon {
namespace {
bool stopped(const LineArtCancellation &c) { return c && c->load(std::memory_order_relaxed); }
QImage transparent(QSize size) { QImage image(size,QImage::Format_ARGB32_Premultiplied); image.setColorSpace(QColorSpace::SRgb); image.fill(Qt::transparent); return image; }
QRgb colored(QColor color,int coverage) { color.setAlpha(qRound(color.alpha()*coverage/255.));return qPremultiply(color.rgba()); }
}
QString validateLineArtSettings(const LineArtSettings &s) {
    const auto valid=[](double value,double lo,double hi){return std::isfinite(value)&&value>=lo&&value<=hi;};
    if(!valid(s.textureSuppression,0,100)||!valid(s.detail,0,100)||!valid(s.width,.5,12)||!valid(s.smoothing,0,4)||s.fragments<0||s.fragments>4096
        ||s.darkCutoff<1||s.lightCutoff>254||s.darkCutoff>=s.lightCutoff||!valid(s.toneSpacing,2,64)||!valid(s.toneAngle,0,180)
        ||!s.ink.isValid()||!s.shade.isValid()||!s.deepShade.isValid()
        ||int(s.shading)<0||int(s.shading)>int(LineArtShading::Screentone)) return "Conversion settings are outside the supported range.";
    return {};
}
LineArtSettings backgroundLineArtSettings(LineArtSettings settings) {
    settings.detail=60;settings.smoothing=2.5;settings.textureSuppression=70;
    settings.fragments=12;settings.width=1.5;return settings;
}
LineArtResult convertLineArt(const LineArtInput &input,const LineArtSettings &settings,const LineArtCancellation &c,int previewSide) {
    LineArtResult result;
    try {
        result.error=validateLineArtSettings(settings);if(!result.error.isEmpty())return result;
        if(stopped(c)){result.cancelled=true;return result;}
        if(input.size.isEmpty()||qint64(input.size.width())*input.size.height()>maxLineArtPixels||input.size.width()>16384||input.size.height()>16384) {
            result.error="Conversion supports canvases up to 9 megapixels and 16,384 pixels per side.";return result;
        }
        QSize size=input.size;
        if(previewSide>0) size.scale(previewSide,previewSide,Qt::KeepAspectRatio);
        if(size.width()>input.size.width())size=input.size;
        const double scale=double(size.width())/input.size.width();
        if(input.image.isNull()||input.image.size()!=input.size){result.error="The source image is unavailable or has changed size.";return result;}
        result.source=input.image.scaled(size,Qt::IgnoreAspectRatio,Qt::SmoothTransformation).convertToFormat(QImage::Format_ARGB32_Premultiplied);
        auto mask=extractLineArtInk(result.source,settings,scale,c);
        if(stopped(c)){result.cancelled=true;return result;}
        result.lines=transparent(size);
        if(settings.shading!=LineArtShading::None)result.shade=transparent(size);
        if(settings.shading==LineArtShading::Cel)result.deepShade=transparent(size);
        if(settings.base)result.base=transparent(size);
        const double angle=settings.toneAngle*M_PI/180.,cs=std::cos(angle),sn=std::sin(angle);
        for(int y=0;y<size.height()&&!stopped(c);++y) {
            const auto *row=reinterpret_cast<const QRgb *>(result.source.constScanLine(y));
            auto *lines=reinterpret_cast<QRgb *>(result.lines.scanLine(y));
            for(int x=0;x<size.width();++x) {
                const auto pixel=qUnpremultiply(row[x]);const int alpha=qAlpha(pixel);if(!alpha)continue;
                lines[x]=colored(settings.ink,mask.constScanLine(y)[x]);
                if(settings.base)reinterpret_cast<QRgb *>(result.base.scanLine(y))[x]=qRgb(255,255,255);
                const int l=qRound(qRed(pixel)*.2126+qGreen(pixel)*.7152+qBlue(pixel)*.0722);
                if(settings.shading==LineArtShading::Cel) {
                    if(l<settings.darkCutoff)reinterpret_cast<QRgb *>(result.deepShade.scanLine(y))[x]=colored(settings.deepShade,255);
                    else if(l<settings.lightCutoff)reinterpret_cast<QRgb *>(result.shade.scanLine(y))[x]=colored(settings.shade,255);
                } else if(settings.shading==LineArtShading::Grayscale) {
                    if(l<254)reinterpret_cast<QRgb *>(result.shade.scanLine(y))[x]=colored(QColor(l,l,l),255);
                } else if(settings.shading==LineArtShading::Screentone) {
                    const double density=l<settings.darkCutoff?.8:l<settings.lightCutoff?.4:0;
                    if(density>0){
                    const double u=(x*cs+y*sn)/scale/settings.toneSpacing,v=(-x*sn+y*cs)/scale/settings.toneSpacing;
                    const double radius=std::sqrt(density/M_PI);
                    const double distance=std::hypot(u-std::floor(u)-.5,v-std::floor(v)-.5);
                    // Below display resolution, show average coverage instead of false moire.
                    const double cover=settings.toneSpacing*scale<3?density:qBound(0.,(radius-distance)*settings.toneSpacing*scale+.5,1.);
                    reinterpret_cast<QRgb *>(result.shade.scanLine(y))[x]=colored(settings.ink,qRound(255*cover));
                    }
                }
                // Distribute source coverage over the stack once. Multiplying
                // every layer independently would darken transparent edges and
                // increase their composite alpha whenever layers overlap.
                if(alpha<255){
                    double originalTransmission=1,newTransmission=1;
                    for(auto *image:{&result.lines,&result.deepShade,&result.shade,&result.base})if(!image->isNull()){
                        auto &out=reinterpret_cast<QRgb *>(image->scanLine(y))[x];const double oldAlpha=qAlpha(out)/255.;
                        const double newAlpha=newTransmission>1e-8?alpha/255.*oldAlpha*originalTransmission/newTransmission:0;
                        const auto color=qUnpremultiply(out);const int a=qBound(0,qRound(newAlpha*255),255);
                        out=qPremultiply(qRgba(qRed(color),qGreen(color),qBlue(color),a));
                        originalTransmission*=1-oldAlpha;newTransmission*=1-a/255.;
                    }
                }
            }
        }
        if(stopped(c)){result.cancelled=true;return result;}
        result.composite=transparent(size);
        {QPainter painter(&result.composite);for(const auto *image:{&result.base,&result.shade,&result.deepShade,&result.lines})if(!image->isNull())painter.drawImage(0,0,*image);}
    } catch(const std::bad_alloc &) {result={};result.error="Not enough memory for this conversion. Use a smaller canvas.";}
    catch(const std::exception &e) {result={};result.error=QString("Conversion failed: %1").arg(e.what());}
    return result;
}
}
