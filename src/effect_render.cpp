#include "effect_render.h"
#include "advanced_transform.h"
#include "layer_outline.h"
#include "scene3d_cache.h"
#include "reference3d_render.h"
#include <QCache>
#include <QCryptographicHash>
#include <QDataStream>
#include <QHash>
#include <QIODevice>
#include <QJsonDocument>
#include <QScopeGuard>
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>
#include <cstring>
#include <limits>

namespace toon {
namespace {
thread_local QString renderError;
thread_local const std::atomic_bool *renderCancellation = nullptr;
constexpr qint64 maxRegionPixels = 8 * 1024 * 1024;
constexpr qint64 maxWorkingBytes = 256 * 1024 * 1024;
QCache<QByteArray,QImage> &effectCache() { static thread_local QCache<QByteArray,QImage> cache(64*1024*1024); return cache; }
bool supported(const RasterLayer &layer) { return !layer.annotation && !layer.scene3d && !layer.reference3d; }
bool colorActive(const LayerEffects &fx) {
    return fx.enabled && fx.colorEnabled && (fx.exposure!=0 || fx.contrast!=0 || fx.saturation!=0 || fx.hue!=0);
}
bool maskActive(const std::optional<LayerMask> &mask) {
    return mask && mask->enabled && mask->opacity>0
        && (!mask->tiles.isEmpty() || (mask->inverted ? mask->defaultValue!=0 : mask->defaultValue!=255));
}
bool affected(const RasterLayer &layer) {
    if(!supported(layer)||!layer.visible||layer.opacity<=0)return false;
    if(layer.adjustment)return colorActive(layer.effects);
    const auto &fx=layer.effects;
    const bool active=fx.enabled&&((fx.blurEnabled&&fx.blurRadius>0)
        ||(fx.shadowEnabled&&fx.shadowOpacity>0)||(fx.glowEnabled&&fx.glowOpacity>0)||(fx.outlineEnabled&&fx.outlineWidth>0)||colorActive(fx));
    return active || maskActive(layer.mask);
}
int radius(qreal value) { return qBound(0,int(std::ceil(value)),128); }
int blurRadius(const LayerEffects &fx) { return fx.enabled && fx.blurEnabled ? radius(fx.blurRadius) : 0; }
QRectF expandedOutput(QRectF bounds,const RasterLayer &layer) {
    if(bounds.isEmpty() || !supported(layer) || !layer.effects.enabled)return bounds;
    const auto &fx=layer.effects; const int blur=blurRadius(fx)+(fx.outlineEnabled?radius(fx.outlineWidth):0);
    bounds.adjust(-blur,-blur,blur,blur);
    auto result=bounds;
    if(fx.glowEnabled && fx.glowOpacity>0) {
        const int r=radius(fx.glowRadius); result|=bounds.adjusted(-r,-r,r,r);
    }
    if(fx.shadowEnabled && fx.shadowOpacity>0) {
        const int r=radius(fx.shadowRadius);result|=bounds.translated(fx.shadowOffset).adjusted(-r,-r,r,r);
    }
    return result;
}
QRectF requiredInput(QRectF output,const RasterLayer &layer) {
    if(output.isEmpty() || !supported(layer) || !layer.effects.enabled)return output;
    const auto &fx=layer.effects;auto result=output;
    if(fx.glowEnabled && fx.glowOpacity>0) {
        const int r=radius(fx.glowRadius);result|=output.adjusted(-r,-r,r,r);
    }
    if(fx.shadowEnabled && fx.shadowOpacity>0) {
        const int r=radius(fx.shadowRadius);
        result|=output.translated(-fx.shadowOffset).adjusted(-r-1,-r-1,r+1,r+1);
    }
    const int blur=blurRadius(fx)+(fx.outlineEnabled&&fx.outlineWidth>0?radius(fx.outlineWidth)+1:0);return result.adjusted(-blur,-blur,blur,blur);
}
bool bounded(const QRectF &rect) {
    return std::isfinite(rect.left()) && std::isfinite(rect.top()) && std::isfinite(rect.right())
        && std::isfinite(rect.bottom()) && std::abs(rect.left())<10000000 && std::abs(rect.top())<10000000
        && std::abs(rect.right())<10000000 && std::abs(rect.bottom())<10000000;
}
int maskValue(const LayerMask &mask,int x,int y) {
    const int tx=int(std::floor(qreal(x)/tileSize)),ty=int(std::floor(qreal(y)/tileSize));
    const auto it=mask.tiles.constFind({tx,ty});
    return it==mask.tiles.cend()?mask.defaultValue:it->constScanLine(y-ty*tileSize)[x-tx*tileSize];
}
qreal sampledMask(const LayerMask &mask,QPointF point,bool smooth) {
    // Mask resources are bounded to tile coordinates ±128. Tiny animated
    // scales can inverse-map a visible pixel far beyond the integer range.
    if(!std::isfinite(point.x())||!std::isfinite(point.y())||point.x()<-32770||point.y()<-32770
        ||point.x()>33026||point.y()>33026)return mask.defaultValue;
    if(!smooth)return maskValue(mask,int(std::floor(point.x())),int(std::floor(point.y())));
    point-=QPointF(.5,.5);
    const int x=int(std::floor(point.x())),y=int(std::floor(point.y()));
    const qreal fx=point.x()-x,fy=point.y()-y;
    return maskValue(mask,x,y)*(1-fx)*(1-fy)+maskValue(mask,x+1,y)*fx*(1-fy)
        +maskValue(mask,x,y+1)*(1-fx)*fy+maskValue(mask,x+1,y+1)*fx*fy;
}
QRgb scalePixel(QRgb p,int value) {
    return qRgba((qRed(p)*value+127)/255,(qGreen(p)*value+127)/255,
                 (qBlue(p)*value+127)/255,(qAlpha(p)*value+127)/255);
}
void applyIdentityMask(QImage &image,QPoint origin,const std::optional<LayerMask> &mask) {
    if(!mask || !mask->enabled || (mask->defaultValue==255&&mask->tiles.isEmpty()))return;
    auto *pixels=image.bits();const auto stride=image.bytesPerLine();
    for(int y=0;y<image.height();++y) {if((y&31)==0&&effectRenderCancelled()){return;}
        auto *row=reinterpret_cast<QRgb*>(pixels+y*stride);
        const int py=y+origin.y(),ty=int(std::floor(qreal(py)/tileSize));
        for(int x=0;x<image.width();) {
            const int px=x+origin.x(),tx=int(std::floor(qreal(px)/tileSize));
            const int end=qMin(image.width(),x+(tx+1)*tileSize-px);
            const auto tile=mask->tiles.constFind({tx,ty});
            if(tile!=mask->tiles.cend()) {
                const auto *coverage=tile->constScanLine(py-ty*tileSize)+(px-tx*tileSize);
                for(int i=x;i<end;++i)row[i]=scalePixel(row[i],coverage[i-x]);
            }else if(mask->defaultValue==0)std::memset(row+x,0,size_t(end-x)*sizeof(QRgb));
            else if(mask->defaultValue!=255)for(int i=x;i<end;++i)row[i]=scalePixel(row[i],mask->defaultValue);
            x=end;
        }
    }
}
// Exposure works in linear light; the remaining artist controls operate on
// unassociated sRGB. Alpha is never changed by color correction.
void correctColor(QImage &image,const LayerEffects &fx) {
    if(!colorActive(fx) || image.isNull())return;
    std::array<qreal,256> exposure;
    const qreal multiplier=std::exp2(fx.exposure);
    for(int i=0;i<256;++i) {
        const qreal s=i/255.;const qreal linear=s<=.04045?s/12.92:std::pow((s+.055)/1.055,2.4);
        const qreal v=qBound(0.,linear*multiplier,1.);
        exposure[i]=v<=.0031308?12.92*v:1.055*std::pow(v,1./2.4)-.055;
    }
    const qreal contrast=fx.contrast>=0?1+fx.contrast/10.:1+fx.contrast/100.;
    const qreal saturation=1+fx.saturation/100.;
    const bool independent=fx.hue==0&&fx.saturation==0;
    std::array<uchar,256> channelLut{};std::array<bool,256> channelSafe{};
    if(independent) {
        // With no hue/saturation edit, channels are independent. Retain Qt's
        // float -> 16-bit -> RGB8 quantization rather than rounding to 8 bits
        // ourselves. The legacy gray+(channel-gray) can shift a value by a few
        // double ulps; guard both sides and fall back at ambiguous boundaries.
        // All terms are clamped to [0,1], so 16 eps exceeds that arithmetic's
        // rounding bound, including exposure/contrast expression contraction.
        constexpr qreal margin=16*std::numeric_limits<qreal>::epsilon();
        for(int i=0;i<256;++i) {
            const qreal value=qBound(0.,(exposure[i]-.5)*contrast+.5,1.);
            const float low=float(qBound(0.,value-margin,1.)),high=float(qBound(0.,value+margin,1.));
            const int below=QColor::fromRgbF(low,low,low).red(),above=QColor::fromRgbF(high,high,high).red();
            channelLut[i]=uchar(below);channelSafe[i]=below==above;
        }
    }
    for(int y=0;y<image.height();++y) {if((y&31)==0&&effectRenderCancelled()){return;}
        auto *row=reinterpret_cast<QRgb*>(image.scanLine(y));
        for(int x=0;x<image.width();++x) {
            if(!qAlpha(row[x]))continue;
            const auto p=qUnpremultiply(row[x]);
            if(independent&&channelSafe[qRed(p)]&&channelSafe[qGreen(p)]&&channelSafe[qBlue(p)]) {
                row[x]=qPremultiply(qRgba(channelLut[qRed(p)],channelLut[qGreen(p)],channelLut[qBlue(p)],qAlpha(p)));continue;
            }
            qreal r=qBound(0.,(exposure[qRed(p)]-.5)*contrast+.5,1.);
            qreal g=qBound(0.,(exposure[qGreen(p)]-.5)*contrast+.5,1.);
            qreal b=qBound(0.,(exposure[qBlue(p)]-.5)*contrast+.5,1.);
            const qreal gray=.2126*r+.7152*g+.0722*b;
            r=qBound(0.,gray+(r-gray)*saturation,1.);g=qBound(0.,gray+(g-gray)*saturation,1.);b=qBound(0.,gray+(b-gray)*saturation,1.);
            QColor color=QColor::fromRgbF(r,g,b);
            if(fx.hue!=0) {
                float h,s,v;color.getHsvF(&h,&s,&v);
                if(h>=0){h=std::fmod(h+fx.hue/360.+1.,1.);color=QColor::fromHsvF(h,s,v);}
            }
            // A hue adjustment leaves QColor in HSV form. Convert it once,
            // rather than repeating HSV-to-RGB for each channel accessor.
            row[x]=qPremultiply((color.rgb()&0x00ffffffU)|(quint32(qAlpha(p))<<24));
        }
    }
}
// Three finite box passes approximate a Gaussian, with total support exactly
// ceil(radius). Sliding sums make cost independent of radius and stitch across
// tiles because callers supply the complete support halo before cropping.
template<typename Pixel,int Channels>
bool boxPass(QImage &image,int r,bool horizontal) {
    if(!r)return true;
    QImage output(image.size(),image.format());if(output.isNull())return false;
    const int width=image.width(),height=image.height(),divisor=2*r+1;
    const auto *input=image.constBits();const auto inStride=image.bytesPerLine();
    auto *destination=output.bits();const auto outStride=output.bytesPerLine();
    using Sums=std::array<int,Channels>;
    const auto add=[](Sums &sum,Pixel pixel,int sign) {
        if constexpr(Channels==1)sum[0]+=sign*pixel;
        else {sum[0]+=sign*qRed(pixel);sum[1]+=sign*qGreen(pixel);sum[2]+=sign*qBlue(pixel);sum[3]+=sign*qAlpha(pixel);}
    };
    // Each of the three passes has r<=43, hence divisor<=87 and a
    // rounded byte sum<=22228. ceil(2^24/divisor) is exact over this range:
    // its maximum quotient error is <22228/2^24, smaller than 1/87.
    // The unsigned product fits 32 bits, enabling vector multiply/shift
    // instead of four slow integer divisions for every output pixel.
    const quint32 reciprocal=((1U<<24)+quint32(divisor)-1)/quint32(divisor);
    const auto divide=[divisor,reciprocal](int sum){return ((quint32(sum)+quint32(divisor/2))*reciprocal)>>24;};
    const auto average=[divide](const Sums &sum)->Pixel {
        if constexpr(Channels==1)return Pixel(divide(sum[0]));
        else return qRgba(divide(sum[0]),divide(sum[1]),divide(sum[2]),divide(sum[3]));
    };
    if(horizontal) {
        for(int y=0;y<height;++y) {
            if((y&31)==0&&effectRenderCancelled())return false;
            const auto *source=reinterpret_cast<const Pixel*>(input+y*inStride);
            auto *target=reinterpret_cast<Pixel*>(destination+y*outStride);Sums sum{};
            for(int x=0;x<=r&&x<width;++x)add(sum,source[x],1);
            for(int x=0;x<width;++x) {
                target[x]=average(sum);
                if(x>=r)add(sum,source[x-r],-1);
                if(x+r+1<width)add(sum,source[x+r+1],1);
            }
        }
    }else {
        // Walk rows, keeping one running sum per column. Both source reads and
        // destination writes remain contiguous; never detach/scan an image for
        // each pixel or stride through a whole column's cache lines.
        std::vector<Sums> sums(size_t(width),Sums{});
        for(int y=0;y<=r&&y<height;++y) {
            const auto *source=reinterpret_cast<const Pixel*>(input+y*inStride);
            for(int x=0;x<width;++x)add(sums[x],source[x],1);
        }
        for(int y=0;y<height;++y) {
            if((y&31)==0&&effectRenderCancelled())return false;
            auto *target=reinterpret_cast<Pixel*>(destination+y*outStride);
            const auto *leaving=y>=r?reinterpret_cast<const Pixel*>(input+(y-r)*inStride):nullptr;
            const auto *entering=y+r+1<height?reinterpret_cast<const Pixel*>(input+(y+r+1)*inStride):nullptr;
            for(int x=0;x<width;++x) {
                target[x]=average(sums[x]);
                if(leaving)add(sums[x],leaving[x],-1);
                if(entering)add(sums[x],entering[x],1);
            }
        }
    }
    image=std::move(output);return true;
}
bool blur(QImage &image,int r) {
    const bool alpha=image.format()==QImage::Format_Grayscale8;
    for(int pass=0;pass<3;++pass) {
        const int part=r/3+(pass<r%3?1:0);
        if(alpha) {if(!boxPass<uchar,1>(image,part,true)||!boxPass<uchar,1>(image,part,false))return false;}
        else if(!boxPass<QRgb,4>(image,part,true)||!boxPass<QRgb,4>(image,part,false))return false;
    }
    return true;
}
QCache<QByteArray,QImage> &maskFeatherCache() {
    static thread_local QCache<QByteArray,QImage> cache(32*1024*1024);return cache;
}
// Feather is sampled in mask/source space. Each tile includes the full filter
// support before cropping, so neighboring tiles join without seams. Only a
// bounded tile cache is retained, even when a tiny scale samples far away.
class MaskSampler {
public:
    explicit MaskSampler(const LayerMask &source):mask(source),r(radius(source.feather)) {
        inverse=layerMaskTransform(mask).inverted(&invertible);
        if(r && !mask.tiles.isEmpty()) {
            QByteArray bytes;QDataStream stream(&bytes,QIODevice::Append);
            stream<<mask.defaultValue<<r<<quint32(mask.tiles.size());
            for(auto it=mask.tiles.cbegin();it!=mask.tiles.cend();++it)stream<<it.key()<<it->cacheKey();
            signature=QCryptographicHash::hash(bytes,QCryptographicHash::Sha256);
        }
    }
    qreal coverage(QPointF layerPoint,bool smooth) {
        if(!mask.enabled || mask.opacity<=0)return 255;
        qreal value=mask.defaultValue;
        if(invertible && !mask.tiles.isEmpty() && !r && inverse.isIdentity())value=sampledMask(mask,layerPoint,smooth);
        else if(invertible && !mask.tiles.isEmpty()) {
            const auto p=inverse.map(layerPoint);
            if(std::isfinite(p.x())&&std::isfinite(p.y())&&p.x()>-33000&&p.y()>-33000&&p.x()<33300&&p.y()<33300) {
                if(!smooth)value=sample(int(std::floor(p.x())),int(std::floor(p.y())));
                else {
                    const auto centered=p-QPointF(.5,.5);
                    const int x=int(std::floor(centered.x())),y=int(std::floor(centered.y()));
                    const qreal fx=centered.x()-x,fy=centered.y()-y;
                    value=sample(x,y)*(1-fx)*(1-fy)+sample(x+1,y)*fx*(1-fy)
                        +sample(x,y+1)*(1-fx)*fy+sample(x+1,y+1)*fx*fy;
                }
            }
        }
        if(mask.inverted)value=255-value;
        return 255-mask.opacity*(255-value);
    }
private:
    qreal sample(int x,int y) {
        if(!r)return maskValue(mask,x,y);
        const TileKey key{int(std::floor(x/qreal(tileSize))),int(std::floor(y/qreal(tileSize)))};
        if(!lastValid || key!=lastKey) {
            QByteArray cacheKey=signature;QDataStream stream(&cacheKey,QIODevice::Append);stream<<key;
            if(const auto *cached=maskFeatherCache().object(cacheKey))lastImage=*cached;
            else {
                const QRect area(key.first*tileSize-r,key.second*tileSize-r,tileSize+2*r,tileSize+2*r);
                const int x0=int(std::floor(area.left()/qreal(tileSize))),x1=int(std::floor(area.right()/qreal(tileSize)));
                const int y0=int(std::floor(area.top()/qreal(tileSize))),y1=int(std::floor(area.bottom()/qreal(tileSize)));
                bool hasPixels=false;
                for(int ty=y0;ty<=y1&&!hasPixels;++ty)for(int tx=x0;tx<=x1;++tx)
                    if(mask.tiles.contains({tx,ty})){hasPixels=true;break;}
                // A very small scale can visit many distant source tiles in a
                // single row. Uniform outside coverage needs no image or blur.
                if(!hasPixels){lastImage={};lastKey=key;lastValid=true;return mask.defaultValue;}
                QImage image(area.size(),QImage::Format_Grayscale8);
                if(image.isNull()){renderError="There is not enough memory to feather the layer mask.";return mask.defaultValue;}
                image.fill(mask.defaultValue);
                for(int ty=y0;ty<=y1;++ty)for(int tx=x0;tx<=x1;++tx) {
                    if(effectRenderCancelled())return mask.defaultValue;
                    const auto tile=mask.tiles.constFind({tx,ty});if(tile==mask.tiles.cend())continue;
                    const QRect bounds(tx*tileSize,ty*tileSize,tileSize,tileSize),copy=area.intersected(bounds);
                    for(int py=copy.top();py<=copy.bottom();++py)
                        std::memcpy(image.scanLine(py-area.top())+copy.left()-area.left(),
                            tile->constScanLine(py-bounds.top())+copy.left()-bounds.left(),size_t(copy.width()));
                }
                if(!blur(image,r)){if(!effectRenderCancelled())renderError="There is not enough memory to feather the layer mask.";return mask.defaultValue;}
                lastImage=image.copy(r,r,tileSize,tileSize);
                if(lastImage.isNull()){renderError="There is not enough memory to crop the layer mask.";return mask.defaultValue;}
                maskFeatherCache().insert(cacheKey,new QImage(lastImage),int(lastImage.sizeInBytes()+cacheKey.size()+128));
            }
            lastKey=key;lastValid=true;
        }
        return lastImage.isNull()?mask.defaultValue:lastImage.constScanLine(y-key.second*tileSize)[x-key.first*tileSize];
    }
    const LayerMask &mask;int r;QTransform inverse;bool invertible=false,lastValid=false;
    QByteArray signature;TileKey lastKey;QImage lastImage;
};
void applyMask(QImage &image,QPoint origin,const std::optional<LayerMask> &mask,MaskSampler *sampler,bool smooth) {
    if(!maskActive(mask))return;
    // Retain the original exact byte path for ordinary untransformed masks.
    if(layerMaskTransform(*mask).isIdentity() && mask->opacity==1 && !mask->inverted && mask->feather==0) {
        applyIdentityMask(image,origin,mask);return;
    }
    if(!sampler)return;
    for(int y=0;y<image.height();++y) {
        if((y&31)==0&&effectRenderCancelled())return;
        auto *row=reinterpret_cast<QRgb*>(image.scanLine(y));
        for(int x=0;x<image.width();++x)if(qAlpha(row[x]))
            row[x]=scalePixel(row[x],qBound(0,qRound(sampler->coverage(QPointF(x+origin.x()+.5,y+origin.y()+.5),smooth)),255));
        if(!renderError.isEmpty())return;
    }
}
QImage alphaChannel(const QImage &source) {
    QImage result(source.size(),QImage::Format_Grayscale8);if(result.isNull())return {};
    for(int y=0;y<source.height();++y) {if((y&31)==0&&effectRenderCancelled()){return {};}
        const auto *input=reinterpret_cast<const QRgb*>(source.constScanLine(y));auto *output=result.scanLine(y);
        for(int x=0;x<source.width();++x)output[x]=uchar(qAlpha(input[x]));
    }
    return result;
}
QImage coloredAlpha(const QImage &source,QPoint sourceOrigin,QRect output,QPointF offset,QColor color,qreal opacity) {
    QImage result(output.size(),QImage::Format_ARGB32_Premultiplied);if(result.isNull())return {};
    const QPointF delta=QPointF(output.topLeft()-sourceOrigin)-offset;
    const int dx=int(std::floor(delta.x())),dy=int(std::floor(delta.y()));
    const qreal fx=delta.x()-dx,fy=delta.y()-dy;
    const int red=color.red(),green=color.green(),blue=color.blue(),width=source.width();
    const auto *pixels=source.constBits();const auto stride=source.bytesPerLine();
    const auto alpha=[width](const uchar *row,int x){return !row||x<0||x>=width?0:row[x];};
    for(int y=0;y<result.height();++y) {if((y&31)==0&&effectRenderCancelled()){return {};}
        auto *dst=reinterpret_cast<QRgb*>(result.scanLine(y));const int sy=y+dy;
        const auto *row0=sy>=0&&sy<source.height()?pixels+sy*stride:nullptr;
        const auto *row1=sy+1>=0&&sy+1<source.height()?pixels+(sy+1)*stride:nullptr;
        for(int x=0;x<result.width();++x) {
            const qreal coverage=alpha(row0,x+dx)*(1-fx)*(1-fy)+alpha(row0,x+dx+1)*fx*(1-fy)
                +alpha(row1,x+dx)*(1-fx)*fy+alpha(row1,x+dx+1)*fx*fy;
            dst[x]=qPremultiply(qRgba(red,green,blue,qRound(coverage*opacity)));
        }
    }
    return result;
}
void clipped(QImage &base,const QImage &overlay,LayerBlendMode mode,qreal opacity) {
    if(base.isNull()||overlay.isNull())return;
    if(mode==LayerBlendMode::Normal) {
        QPainter p(&base);p.setCompositionMode(QPainter::CompositionMode_SourceAtop);p.setOpacity(opacity);p.drawImage(QPoint(),overlay);return;
    }
    const auto original=base;
    for(int y=0;y<base.height();++y) {if((y&31)==0&&effectRenderCancelled()){return;}auto *row=reinterpret_cast<QRgb*>(base.scanLine(y));for(int x=0;x<base.width();++x)row[x]=qUnpremultiply(row[x])|0xff000000;}
    {QPainter p(&base);p.setCompositionMode(layerCompositionMode(mode));p.setOpacity(opacity);p.drawImage(QPoint(),overlay);}
    for(int y=0;y<base.height();++y) {if((y&31)==0&&effectRenderCancelled()){return;}auto *row=reinterpret_cast<QRgb*>(base.scanLine(y));const auto *old=reinterpret_cast<const QRgb*>(original.constScanLine(y));
        for(int x=0;x<base.width();++x)row[x]=qPremultiply((row[x]&0xffffff)|(old[x]&0xff000000));}
}
QRectF tileBounds(const RasterLayer &layer) {
    QRectF result;for(auto it=layer.tiles.cbegin();it!=layer.tiles.cend();++it)
        result|=QRectF(it.key().first*tileSize,it.key().second*tileSize,it->width(),it->height());
    return result;
}
}

EffectRenderCancellationScope::EffectRenderCancellationScope(const std::atomic_bool *cancelled)
    : m_previous(renderCancellation) { renderCancellation = cancelled; }
EffectRenderCancellationScope::~EffectRenderCancellationScope() { renderCancellation = m_previous; }
bool effectRenderCancelled() { return renderCancellation && renderCancellation->load(std::memory_order_relaxed); }
bool layerStackHasEffects(const QVector<RasterLayer> &layers) {
    return std::any_of(layers.cbegin(),layers.cend(),affected);
}
QString effectRenderError(){return renderError;}
void clearEffectRenderError(){renderError.clear();}
void reportLayerRenderError(const QString &error){if(!error.isEmpty()&&renderError.isEmpty())renderError=error;}
QImage layerMaskCoveragePreview(const LayerMask &mask,QRectF layerBounds,QSize size,bool smooth) {
    if(size.isEmpty() || size.width()>1024 || size.height()>1024 || layerBounds.isEmpty() || !bounded(layerBounds))return {};
    // Thumbnail failure must not erase a canvas error or inherit a stale one.
    const auto previousError=renderError;renderError.clear();
    const auto restoreError=qScopeGuard([&]{renderError=previousError;});
    QImage image(size,QImage::Format_Grayscale8);if(image.isNull())return {};
    MaskSampler sampler(mask);
    for(int y=0;y<size.height();++y) {
        if((y&31)==0&&effectRenderCancelled())return {};
        auto *row=image.scanLine(y);
        for(int x=0;x<size.width();++x)row[x]=uchar(qBound(0,qRound(sampler.coverage({
            layerBounds.x()+(x+.5)*layerBounds.width()/size.width(),
            layerBounds.y()+(y+.5)*layerBounds.height()/size.height()},smooth)),255));
        if(!renderError.isEmpty())return {};
    }
    return image;
}
bool checkEffectClipping(const QVector<RasterLayer> &layers) {
    QHash<QString,bool> adjustmentBase;
    for(const auto &layer:layers) {
        if(layer.clipped&&(layer.adjustment||adjustmentBase.value(layer.parentId))) {
            renderError="Adjustment layers cannot be clipping masks or clipping bases. Choose an artwork layer as the base.";
            return false;
        }
        if(!layer.clipped)adjustmentBase.insert(layer.parentId,layer.adjustment);
    }
    return true;
}
QRectF effectSourceBounds(const QVector<RasterLayer> &layers,const QString &layerId,QRectF output) {
    QHash<QString,const RasterLayer*> lookup;for(const auto &layer:layers)lookup.insert(layer.id,&layer);
    QVector<const RasterLayer*> ancestry;QString id=layerId;
    while(!id.isEmpty()&&ancestry.size()<10) {const auto *layer=lookup.value(id);if(!layer)break;ancestry.prepend(layer);id=layer->parentId;}
    for(const auto *layer:ancestry) {
        bool ok=false;const auto inverse=layer->renderTransform.inverted(&ok);if(!ok)return {};
        output=inverse.mapRect(output).adjusted(-2,-2,2,2);output=requiredInput(output,*layer);
    }
    return output;
}

class EffectCompositor::Impl {
public:
    Impl(const QVector<RasterLayer> &layers,bool annotations,const RasterLayerPreview *preview,bool smooth,QSize sceneSize)
        :layers(layers),annotations(annotations),preview(preview),smooth(smooth),sceneSize(sceneSize) {
        QByteArray bytes("effects-v1");QDataStream data(&bytes,QIODevice::Append);data<<annotations<<smooth<<sceneSize;
        for(int i=0;i<layers.size();++i) {
            const auto &layer=layers[i];children[layer.parentId].append(i);
            if(layer.scene3d&&layer.scene3d->visible&&!sceneSize.isEmpty())sceneImages.insert(layer.id,scene3DLayerPreview(layer.id,*layer.scene3d,sceneSize));
            if(layer.reference3d&&layer.visible&&!sceneSize.isEmpty()){QString error;sceneImages.insert(layer.id,referenceLayerPreview(layer.id,*layer.reference3d,sceneSize,&error));reportLayerRenderError(error);}
            data<<layer.id<<layer.parentId<<layer.group<<layer.visible<<layer.annotation<<layer.clipped<<layer.adjustment
                <<layer.opacity<<qint32(layer.blendMode)<<layer.renderTransform<<sceneImages.value(layer.id).cacheKey()
                <<double(layer.scene3d?layer.scene3d->opacity:1)<<QJsonDocument(layerEffectsToJson(layer.effects)).toJson(QJsonDocument::Compact);
            data<<quint32(layer.tiles.size());
            for(auto it=layer.tiles.cbegin();it!=layer.tiles.cend();++it)data<<it.key()<<it->cacheKey();
            data<<bool(layer.mask);
            if(layer.mask){data<<QJsonDocument(layerMaskToJson(*layer.mask)).toJson(QJsonDocument::Compact);for(auto it=layer.mask->tiles.cbegin();it!=layer.mask->tiles.cend();++it)data<<it.key()<<it->cacheKey();}
        }
        // Transform previews change every gesture step and use bounded temporary
        // regions; do not retain their transient images in the persistent cache.
        signature=QCryptographicHash::hash(bytes,QCryptographicHash::Sha256);
        groupTiles=!preview&&std::all_of(layers.cbegin(),layers.cend(),[](const auto &layer){return layer.renderTransform.isIdentity()&&!layer.scene3d&&!layer.reference3d;});
    }
    bool shown(const RasterLayer &layer)const{return layer.visible&&(annotations||!layer.annotation)&&layer.opacity>0;}
    void fail(const QString &message)const{if(!effectRenderCancelled()&&renderError.isEmpty())renderError=message;}
    MaskSampler *maskSampler(const RasterLayer &layer) {
        if(!layer.mask)return nullptr;
        auto &sampler=maskSamplers[layer.id];if(!sampler)sampler=std::make_shared<MaskSampler>(*layer.mask);
        return sampler.get();
    }
    QImage blank(QRect area)const {
        if(effectRenderCancelled())return {};
        if(area.isEmpty())return {};
        if(!bounded(area)||qint64(area.width())*area.height()>maxRegionPixels){fail("The effects region exceeds the 8-million-pixel working limit. Reduce the effect size or extreme layer scale.");return {};}
        QImage image(area.size(),QImage::Format_ARGB32_Premultiplied);
        if(image.isNull())fail("There is not enough memory to render the layer effects.");else image.fill(Qt::transparent);
        return image;
    }
    const RasterLayerPreview *candidate(const QString &id)const {
        if(!preview)return nullptr;if(preview->layerId==id)return preview;
        for(const auto &other:preview->additional)if(other.layerId==id)return &other;return nullptr;
    }
    QRectF rawBounds(int index,int depth)const {
        const auto &layer=layers[index];
        if(layer.group)return bounds(layer.id,depth+1);
        if(layer.scene3d||layer.reference3d)return sceneImages.value(layer.id).isNull()?QRectF():QRectF(QPointF(),sceneSize);
        if(const auto *moving=candidate(layer.id)) {
            auto result=tileBounds(moving->stationary);
            if(moving->warpNodes.isEmpty())result|=moving->transform.mapRect(tileBounds(moving->moving));
            else {for(const auto &p:moving->warpNodes)result|=QRectF(p,QSizeF(.001,.001));}
            return result;
        }
        return tileBounds(layer);
    }
    QRectF bounds(const QString &parent={},int depth=0)const {
        if(depth>8)return {};QRectF result;
        for(int i:children.value(parent)) {const auto &layer=layers[i];if(!shown(layer)||layer.adjustment||layer.clipped)continue;
            result|=layer.renderTransform.mapRect(expandedOutput(rawBounds(i,depth),layer));}
        return result;
    }
    QImage raw(int index,QRect area,int depth) {
        if(effectRenderCancelled())return {};
        const auto &layer=layers[index];if(layer.group)return composite(layer.id,area,depth+1);
        auto image=blank(area);if(image.isNull())return {};
        QPainter painter(&image);painter.translate(-area.topLeft());
        if(layer.scene3d||layer.reference3d) {
            painter.setOpacity(layer.scene3d?layer.scene3d->opacity:1);painter.setRenderHint(QPainter::SmoothPixmapTransform,smooth);
            painter.drawImage(QRectF(QPointF(),sceneSize),sceneImages.value(layer.id));
        }else if(const auto *moving=candidate(layer.id)) {
            paintRasterLayerPixels(painter,moving->stationary,smooth);
            paintAdvancedMovingLayer(painter,*moving,smooth);
        }else paintRasterLayerPixels(painter,layer,false);
        return image;
    }
    QImage node(int index,QRect area,int depth) {
        if(effectRenderCancelled())return {};
        const auto &layer=layers[index];bool invertible=false;const auto inverse=layer.renderTransform.inverted(&invertible);
        if(!invertible)return {};
        const auto content=expandedOutput(rawBounds(index,depth),layer);
        auto localF=inverse.mapRect(QRectF(area)).adjusted(-2,-2,2,2).intersected(content.adjusted(-2,-2,2,2));
        if(localF.isEmpty())return {};
        if(!bounded(localF)){fail("The transformed effects bounds are outside the supported range.");return {};}
        const auto local=localF.toAlignedRect();
        auto workF=requiredInput(local,layer);
        if(!bounded(workF)){fail("The effects bounds are outside the supported range.");return {};}
        const auto work=workF.toAlignedRect();const qint64 reserve=qint64(work.width())*work.height()*4*6;
        if(qint64(work.width())*work.height()>maxRegionPixels || working+reserve>maxWorkingBytes) {
            fail("Layer effects exceed the 256 MiB working-memory limit. Reduce the effect size or extreme layer scale.");return {};
        }
        working+=reserve;struct Release{qint64 &bytes;qint64 amount;~Release(){bytes-=amount;}} release{working,reserve};
        auto source=raw(index,work,depth);if(source.isNull()||!renderError.isEmpty()||effectRenderCancelled())return {};
        if(supported(layer)) {
            applyMask(source,work.topLeft(),layer.mask,maskSampler(layer),smooth);
            if(!renderError.isEmpty())return {};
            correctColor(source,layer.effects);
            if(effectRenderCancelled())return {};
            if(!blur(source,blurRadius(layer.effects))){fail("There is not enough memory to blur the layer.");return {};}
            if(layer.effects.enabled&&layer.effects.outlineEnabled&&layer.effects.outlineWidth>0
                &&!applyLayerOutline(source,layer.effects.outlineWidth,layer.effects.outlineColor,[]{return effectRenderCancelled();})){
                if(!effectRenderCancelled())fail("The outline exceeds the supported working-memory limits.");return {};
            }
        }
        QImage processed;
        const auto &fx=layer.effects;
        if(supported(layer)&&fx.enabled&&((fx.shadowEnabled&&fx.shadowOpacity>0)||(fx.glowEnabled&&fx.glowOpacity>0))) {
            processed=blank(local);if(processed.isNull())return {};
            QPainter painter(&processed);painter.translate(-local.topLeft());painter.setRenderHint(QPainter::SmoothPixmapTransform,true);
            // Shadow/glow use coverage only. Extract it once and keep the same
            // byte rounding through all six passes; RGB blur cannot affect it.
            const auto coverage=alphaChannel(source);if(coverage.isNull()){fail("There is not enough memory to render the shadow or glow.");return {};}
            QImage alpha;int alphaRadius=-1;
            const auto effect=[&](bool enabled,qreal opacity,int r,QColor color,QPointF offset) {
                if(!enabled||opacity<=0)return true;
                if(alphaRadius!=r){alpha=coverage;if(!blur(alpha,r))return false;alphaRadius=r;}
                // Qt rounds translation-only image draws. Sample the shifted
                // coverage explicitly so subpixel shadow offsets stay smooth.
                auto tint=coloredAlpha(alpha,work.topLeft(),local,offset,color,opacity);if(tint.isNull())return false;
                painter.drawImage(local.topLeft(),tint);return true;
            };
            if(!effect(fx.shadowEnabled,fx.shadowOpacity,radius(fx.shadowRadius),fx.shadowColor,fx.shadowOffset)
               ||!effect(fx.glowEnabled,fx.glowOpacity,radius(fx.glowRadius),fx.glowColor,{})) {
                fail("There is not enough memory to render the shadow or glow.");return {};
            }
            painter.drawImage(work.topLeft(),source);painter.end();
        }else processed=source.copy(QRect(local.topLeft()-work.topLeft(),local.size()));
        if(processed.isNull()){fail("There is not enough memory to render the layer effects.");return {};}
        auto result=blank(area);if(result.isNull())return {};
        QPainter painter(&result);painter.translate(-area.topLeft());painter.setTransform(layer.renderTransform,true);
        painter.setRenderHint(QPainter::SmoothPixmapTransform,smooth);painter.drawImage(local.topLeft(),processed);return result;
    }
    void adjustment(QImage &backdrop,const RasterLayer &layer,QRect area) {
        if(backdrop.isNull()||!colorActive(layer.effects))return;
        auto corrected=backdrop;correctColor(corrected,layer.effects);
        if(layer.blendMode!=LayerBlendMode::Normal) {
            // Blend corrected unassociated colors over opaque backdrop colors,
            // then interpolate using the original coverage exactly once.
            auto opaque=backdrop;
            for(int y=0;y<opaque.height();++y) {if((y&31)==0&&effectRenderCancelled()){return;}auto *dst=reinterpret_cast<QRgb*>(opaque.scanLine(y));auto *src=reinterpret_cast<QRgb*>(corrected.scanLine(y));
                for(int x=0;x<opaque.width();++x){dst[x]=qUnpremultiply(dst[x])|0xff000000;src[x]=qUnpremultiply(src[x])|0xff000000;}}
            {QPainter p(&opaque);p.setCompositionMode(layerCompositionMode(layer.blendMode));p.drawImage(QPoint(),corrected);}corrected=std::move(opaque);
        }
        bool ok=false;const auto inverse=layer.renderTransform.inverted(&ok);if(!ok)return;
        auto *sampler=maskSampler(layer);
        for(int y=0;y<backdrop.height();++y) {if((y&31)==0&&effectRenderCancelled()){return;}
            auto *dst=reinterpret_cast<QRgb*>(backdrop.scanLine(y));const auto *src=reinterpret_cast<const QRgb*>(corrected.constScanLine(y));
            for(int x=0;x<backdrop.width();++x) {
                const int alpha=qAlpha(dst[x]);if(!alpha)continue;
                qreal amount=layer.opacity;
                if(sampler&&layer.mask->enabled){const auto p=inverse.map(QPointF(x+area.x()+.5,y+area.y()+.5));
                    amount*=sampler->coverage(p,smooth)/255.;}
                if(amount<=0)continue;
                const auto old=qUnpremultiply(dst[x]),next=qUnpremultiply(src[x]);
                dst[x]=qPremultiply(qRgba(qRound(qRed(old)+(qRed(next)-qRed(old))*amount),qRound(qGreen(old)+(qGreen(next)-qGreen(old))*amount),
                                        qRound(qBlue(old)+(qBlue(next)-qBlue(old))*amount),alpha));
            }
        }
    }
    QImage composite(const QString &parent,QRect area,int depth=0) {
        if(effectRenderCancelled())return {};
        if(depth>8){fail("The effects folder nesting exceeds the supported limit.");return {};}
        QImage result;const auto siblings=children.value(parent);
        for(int at=0;at<siblings.size();) {
            if(effectRenderCancelled())return {};
            const int index=siblings[at];const auto &base=layers[index];
            if(base.adjustment&&!base.clipped) {if(shown(base))adjustment(result,base,area);if(!renderError.isEmpty())return {};++at;continue;}
            if(base.clipped){++at;continue;}int end=at+1;while(end<siblings.size()&&layers[siblings[end]].clipped)++end;
            if(shown(base)) {
                auto image=node(index,area,depth);if(!renderError.isEmpty())return {};
                if(!image.isNull()) {
                    for(int i=at+1;i<end;++i) {
                        const auto &overlay=layers[siblings[i]];if(!shown(overlay))continue;
                        if(overlay.adjustment)adjustment(image,overlay,area);
                        else clipped(image,node(siblings[i],area,depth),overlay.blendMode,overlay.opacity);
                        if(!renderError.isEmpty())return {};
                    }
                    if(result.isNull())result=blank(area);if(result.isNull())return {};
                    QPainter painter(&result);painter.setCompositionMode(layerCompositionMode(base.blendMode));painter.setOpacity(base.opacity);painter.drawImage(QPoint(),image);
                }
            }
            at=end;
        }
        return result;
    }
    void prepareVisibleTiles(const QSet<TileKey> &keys) {
        for(auto &groups:groupedTiles)groups.clear();if(!groupTiles)return;
        for(int index=0;index<2;++index) {
            const int side=index==0?4:2;QHash<TileKey,int> counts;
            for(const auto &key:keys)++counts[{int(std::floor(key.first/qreal(side)))*side,int(std::floor(key.second/qreal(side)))*side}];
            // Never spend a whole block's work on a partially visible block.
            // Zoomed-in views keep the original 256px tile behavior.
            for(auto it=counts.cbegin();it!=counts.cend();++it)if(it.value()==side*side)groupedTiles[index].insert(it.key());
        }
    }
    QByteArray tileKey(TileKey key,int side)const {
        QByteArray cacheKey=signature;QDataStream data(&cacheKey,QIODevice::Append);data<<side<<key;return cacheKey;
    }
    QImage tile(TileKey key) {
        if(effectRenderCancelled())return {};
        const auto croppedTile=[this](const QImage &image,const QRect &crop) {
            auto result=image.copy(crop);
            // Empty composites are legitimately transparent. A failed copy of
            // rendered content must instead surface an error to canvas/export.
            if(!image.isNull()&&result.isNull())fail("There is not enough memory to crop the layer effects.");
            return result;
        };
        for(int index=0;index<2;++index) {
            const int side=index==0?4:2;
            const TileKey group{int(std::floor(key.first/qreal(side)))*side,int(std::floor(key.second/qreal(side)))*side};
            if(!groupedTiles[index].contains(group)||!renderError.isEmpty())continue;
            const auto cacheKey=tileKey(group,side);if(failedGroups.contains(cacheKey))continue;
            const QRect crop((key.first-group.first)*tileSize,(key.second-group.second)*tileSize,tileSize,tileSize);
            if(const auto *cached=effectCache().object(cacheKey))return croppedTile(*cached,crop);
            // Shared 1024/512px regions pay for overlapping blur/light halos
            // once. Public tiles, source resolution and the 64MiB cache cap
            // remain unchanged. Only fully visible blocks are grouped.
            const auto image=composite({},QRect(group.first*tileSize,group.second*tileSize,side*tileSize,side*tileSize));
            if(effectRenderCancelled())return {};
            if(renderError.isEmpty()) {
                effectCache().insert(cacheKey,new QImage(image),int(image.sizeInBytes()+cacheKey.size()+128));return croppedTile(image,crop);
            }
            // Retain the original working limits. A document that fits smaller
            // tiles must still render: retry at 512, then 256px. Remember failed
            // size/origin pairs so neighboring tiles do not repeat that work.
            failedGroups.insert(cacheKey);renderError.clear();
        }
        const auto cacheKey=tileKey(key,1);
        if(!preview)if(const auto *cached=effectCache().object(cacheKey))return *cached;
        const auto image=composite({},QRect(key.first*tileSize,key.second*tileSize,tileSize,tileSize));
        if(effectRenderCancelled())return {};
        if(!preview&&renderError.isEmpty())effectCache().insert(cacheKey,new QImage(image),int(image.sizeInBytes()+cacheKey.size()+128));
        return image;
    }
    const QVector<RasterLayer> &layers;bool annotations;const RasterLayerPreview *preview;bool smooth;QSize sceneSize;
    QHash<QString,QVector<int>> children;QHash<QString,QImage> sceneImages;QByteArray signature;qint64 working=0;
    QHash<QString,std::shared_ptr<MaskSampler>> maskSamplers;
    bool groupTiles=false;std::array<QSet<TileKey>,2> groupedTiles;QSet<QByteArray> failedGroups;
};
EffectCompositor::EffectCompositor(const QVector<RasterLayer> &layers,bool annotations,const RasterLayerPreview *preview,bool smooth,QSize sceneSize)
    :m_impl(std::make_unique<Impl>(layers,annotations,preview,smooth,sceneSize)){}
EffectCompositor::~EffectCompositor()=default;
QRectF EffectCompositor::bounds()const{return m_impl->bounds();}
QSet<TileKey> EffectCompositor::visibleTiles(QRectF artworkClip)const {
    const auto visible=artworkClip.intersected(bounds().adjusted(-2,-2,2,2));QSet<TileKey> keys;
    if(visible.isEmpty())return keys;
    if(!bounded(visible)){m_impl->fail("The visible effects bounds are outside the supported range.");return keys;}
    const int left=int(std::floor(visible.left()/tileSize)),top=int(std::floor(visible.top()/tileSize));
    const int right=int(std::floor(visible.right()/tileSize)),bottom=int(std::floor(visible.bottom()/tileSize));
    if(qint64(right-left+1)*(bottom-top+1)>4096) {
        m_impl->fail("The visible effects region exceeds the 4096-tile render limit. Zoom in or reduce widely separated artwork.");return keys;
    }
    for(int y=top;y<=bottom;++y)for(int x=left;x<=right;++x)keys.insert({x,y});m_impl->prepareVisibleTiles(keys);return keys;
}
QImage EffectCompositor::tile(const TileKey &key){return m_impl->tile(key);}
QRectF compositedLayerBounds(const QVector<RasterLayer> &layers,bool annotations,QSize sceneSize) {
    EffectCompositor compositor(layers,annotations,nullptr,false,sceneSize);return compositor.bounds();
}
} // namespace toon
