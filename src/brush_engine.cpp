#include "brush_engine.h"
#include "brush_surface_pattern.h"
#include "raster_edit.h"
#include "connected_brush.h"
#include <QPainter>
#include <QLineF>
#include <QVector3D>
#include <QCache>
#include <algorithm>
#include <array>
#include <cmath>

namespace toon {
namespace {
constexpr qreal pi = 3.14159265358979323846;
qreal unit(qreal v) { return qBound(0.0, v, 1.0); }
qreal radians(qreal v) { return v * pi / 180; }
qreal angleOf(QPointF p) { return std::atan2(p.y(), p.x()) * 180 / pi; }
QPointF rotate(QPointF p, qreal degrees) {
    const auto a = radians(degrees); return {p.x() * std::cos(a) - p.y() * std::sin(a), p.x() * std::sin(a) + p.y() * std::cos(a)};
}
QImage alphaImage(const QImage &mask) {
    QImage out(mask.size(), QImage::Format_ARGB32_Premultiplied);
    for (int y = 0; y < out.height(); ++y) {
        const auto *src = mask.constScanLine(y); auto *dst = reinterpret_cast<QRgb *>(out.scanLine(y));
        for (int x = 0; x < out.width(); ++x) dst[x] = qRgba(src[x], src[x], src[x], src[x]);
    }
    return out;
}
QImage grayAlpha(const QImage &image) {
    QImage out(image.size(), QImage::Format_Grayscale8);
    for (int y = 0; y < out.height(); ++y) {
        const auto *src = reinterpret_cast<const QRgb *>(image.constScanLine(y)); auto *dst = out.scanLine(y);
        for (int x = 0; x < out.width(); ++x) dst[x] = qAlpha(src[x]);
    }
    return out;
}
qreal maskBlend(qreal primary, qreal secondary, MaskBlend mode) {
    switch (mode) {
    case MaskBlend::Multiply: return primary * secondary;
    case MaskBlend::Darken: return qMin(primary, secondary);
    case MaskBlend::Lighten: return qMax(primary, secondary);
    case MaskBlend::Subtract: return qMax(0.0, primary - secondary);
    case MaskBlend::ColorBurn: return secondary <= 0 ? 0 : 1 - qMin(1.0, (1 - primary) / secondary);
    case MaskBlend::LinearHeight: return unit(primary + secondary - 1);
    case MaskBlend::Add: return qMin(1.0, primary + secondary);
    case MaskBlend::Difference: return std::abs(primary - secondary);
    }
    return primary;
}
struct ShapeKey {
    qint64 tip;
    qreal diameter, roundness, angle, hardness;
    bool flipX, flipY;
    bool operator==(const ShapeKey &) const = default;
    friend size_t qHash(const ShapeKey &key,size_t seed=0) {
        return qHashMulti(seed,key.tip,key.diameter,key.roundness,key.angle,key.hardness,key.flipX,key.flipY);
    }
};
struct ShapedTip { QImage mask, color; };
// Blur premultiplied colors together with alpha so transparent pixels never introduce dark fringes.
QImage softenedColorTip(const QImage &source, int radius) {
    QImage horizontal(source.size(), QImage::Format_ARGB32_Premultiplied);
    QImage result(source.size(), QImage::Format_ARGB32_Premultiplied);
    const int divisor = 2 * radius + 1;
    for (int pass = 0; pass < 2; ++pass) {
        const QImage &input = pass ? horizontal : source;
        QImage &output = pass ? result : horizontal;
        const int lines = pass ? input.width() : input.height();
        const int length = pass ? input.height() : input.width();
        for (int line = 0; line < lines; ++line) {
            int channels[4] = {};
            auto adjust = [&](int at, int sign) {
                if (at < 0 || at >= length) return;
                const QRgb pixel = input.pixel(pass ? line : at, pass ? at : line);
                channels[0] += sign * qRed(pixel); channels[1] += sign * qGreen(pixel);
                channels[2] += sign * qBlue(pixel); channels[3] += sign * qAlpha(pixel);
            };
            for (int at = 0; at <= radius; ++at) adjust(at, 1);
            for (int at = 0; at < length; ++at) {
                output.setPixel(pass ? line : at, pass ? at : line,
                    qRgba(channels[0]/divisor, channels[1]/divisor, channels[2]/divisor, channels[3]/divisor));
                adjust(at - radius, -1); adjust(at + radius + 1, 1);
            }
        }
    }
    return result;
}
QImage shapedMask(const BrushSettings &brush, const BrushInput &in, qreal direction, qreal initial,
                  quint64 index, quint32 seed, QImage *color = nullptr, QPointF *tipOffset = nullptr) {
    auto b = brush;
    QPointF anchor(.5, .5);
    if (!b.tipFrames.isEmpty()) {
        const int frame = brushTipFrameIndex(b, resolvedBrushInput(b, in), direction, index, seed);
        anchor = b.tipFrames[frame].anchor;
        const auto image = brushTipFrameImage(b, frame);
        if (b.colorTip.isNull()) b.tipMask = image; else b.colorTip = image;
    }
    const auto input = resolvedBrushInput(b, in);
    auto sizeDynamics = b.sizeDynamics;
    if (b.pressureSize && sizeDynamics.control == BrushControl::Off) sizeDynamics.control = BrushControl::Pressure;
    qreal sizeFactor = brushDynamicValue(sizeDynamics, input, index, seed, 1, direction, initial);
    if (b.sizeDynamics.control == BrushControl::Tilt)
        sizeFactor = qMax(b.sizeDynamics.minimum, unit(sizeFactor * b.tiltScale));
    const qreal diameter = qBound(.5, qBound(1.0, b.size, maxBrushSize) * sizeFactor
        * brushSpeedFactor(b.speedDynamics, input.speed, b.speedDynamics.sizeAmount), maxBrushSize);
    qreal roundness = qMax(.01, b.roundness * brushDynamicValue(b.roundnessDynamics, input, index, seed, 2));
    roundness = qMax(b.roundnessDynamics.minimum, roundness);
    // Existing brushes already follow barrel rotation implicitly. An explicit
    // Rotation controller replaces that term, rather than applying it twice.
    qreal angle = b.angle + (b.angleDynamics.control == BrushControl::Rotation ? 0 : input.rotation);
    angle += (brushRandom(seed, b.angleDynamics.jitterPerStroke ? 0 : index, 3) * 2 - 1) * unit(b.angleDynamics.jitter) * 180;
    qreal controlledAngle = 0;
    switch (b.angleDynamics.control) {
    case BrushControl::Off: break;
    case BrushControl::Direction: controlledAngle = direction; break;
    case BrushControl::InitialDirection: controlledAngle = initial; break;
    case BrushControl::Tilt: controlledAngle = angleOf({input.tiltX, input.tiltY}); break;
    case BrushControl::Rotation: controlledAngle = input.rotation; break;
    default: controlledAngle = 360 * brushControlValue(b.angleDynamics.control, input, index, b.angleDynamics.fade); break;
    }
    if (b.angleDynamics.control != BrushControl::Off) {
        // Legacy angle minimum was inert. Only a new response curve opts into
        // remapping, so older captures keep their exact rendered orientation.
        if (b.angleDynamics.response != BrushDynamic{}.response) {
            qreal normalized = controlledAngle / 360;
            // Angular inputs wrap; scalar pressure/fade inputs retain their 1 endpoint.
            if (b.angleDynamics.control == BrushControl::Direction || b.angleDynamics.control == BrushControl::InitialDirection ||
                b.angleDynamics.control == BrushControl::Tilt || b.angleDynamics.control == BrushControl::Rotation)
                normalized -= std::floor(normalized);
            controlledAngle = 360 * qMax(unit(b.angleDynamics.minimum), brushResponseValue(b.angleDynamics, normalized));
        }
        angle += controlledAngle;
    }
    if (b.projection) {
        const qreal tilt = qMin(89.0, std::hypot(input.tiltX, input.tiltY));
        roundness *= qMax(.02, std::cos(radians(tilt)));
        angle += angleOf({input.tiltX, input.tiltY});
    }
    // Key the resolved geometry, not the input sample/index: pressure, tilt and
    // jitter may change the geometry, while scattering and flow do not.
    static thread_local QCache<ShapeKey,ShapedTip> shapes(8192);
    const QImage &sourceTip = b.colorTip.isNull() ? b.tipMask : b.colorTip;
    const ShapeKey shapeKey{sourceTip.cacheKey(),diameter,roundness,angle,b.hardness,b.flipX,b.flipY};
    QSizeF size(diameter, diameter);
    if (!sourceTip.isNull()) size = QSizeF(sourceTip.size()).scaled(size, Qt::KeepAspectRatio);
    size.setHeight(qMax(.05, size.height() * roundness));
    QTransform transform; transform.rotate(angle);
    transform.scale(b.flipX ? -1 : 1, b.flipY ? -1 : 1);
    if (tipOffset) *tipOffset = transform.map(QPointF((.5 - anchor.x()) * size.width(), (.5 - anchor.y()) * size.height()));
    if (const auto *cached=shapes.object(shapeKey)) { if (color) *color = cached->color; return cached->mask; }
    const QRectF tipRect(-size.width()/2, -size.height()/2, size.width(), size.height());
    const QRectF rotated = transform.mapRect(tipRect);
    // Symmetric dimensions keep the geometric origin at the exact image center.
    const QSize pixels(qMax(2, int(std::ceil(rotated.width())) + 2), qMax(2, int(std::ceil(rotated.height())) + 2));
    QImage rendered(pixels, QImage::Format_ARGB32_Premultiplied); rendered.fill(Qt::transparent);
    QPainter painter(&rendered); painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    painter.translate(pixels.width()/2.0, pixels.height()/2.0); painter.setTransform(transform, true);
    if (!b.colorTip.isNull()) {
        const int target = qMax(1, int(std::ceil(diameter)));
        const QSize sampleSize = QSizeF(b.colorTip.size()).scaled(QSizeF(target,target),Qt::KeepAspectRatio).toSize().expandedTo({1,1});
        static thread_local QCache<QString,QImage> samples(8192);
        const QString key = QString::number(b.colorTip.cacheKey())+":"+QString::number(sampleSize.width())+":"+QString::number(sampleSize.height());
        QImage sampled;
        if (const auto *cached = samples.object(key)) sampled = *cached;
        else {
            // Pressure can visit many sizes in one stroke. Downsample the imported
            // source only once to the largest paintable size, then resample that
            // small immutable image rather than repeatedly scanning a 2048px stamp.
            QImage base = b.colorTip;
            const int baseSide = b.size > 256 ? int(maxBrushSize) : 256;
            if (qMax(base.width(),base.height()) > baseSide) {
                static thread_local QCache<QString,QImage> bases(8192);
                const auto baseKey = QString::number(b.colorTip.cacheKey()) + ":" + QString::number(baseSide);
                if (const auto *cached=bases.object(baseKey)) base=*cached;
                else {
                    const QSize baseSize=QSizeF(base.size()).scaled(QSizeF(baseSide,baseSide),Qt::KeepAspectRatio).toSize().expandedTo({1,1});
                    base=base.scaled(baseSize,Qt::IgnoreAspectRatio,Qt::SmoothTransformation);
                    bases.insert(baseKey,new QImage(base),qMax(1,int((base.sizeInBytes()+1023)/1024)));
                }
            }
            sampled = base.size() == sampleSize ? base : base.scaled(sampleSize,Qt::IgnoreAspectRatio,Qt::SmoothTransformation);
            samples.insert(key,new QImage(sampled),qMax(1,int((sampled.sizeInBytes()+1023)/1024)));
        }
        if (b.hardness < .999) sampled = softenedColorTip(sampled, qMax(1,int((1-unit(b.hardness))*diameter*.12)));
        painter.drawImage(tipRect, sampled);
    } else if (!b.tipMask.isNull()) {
        // Never convert a full imported 2048px tip to ARGB on every small dab.
        const int target = qMax(1, int(std::ceil(diameter)));
        const QSize sampleSize = QSizeF(b.tipMask.size()).scaled(QSizeF(target,target),Qt::KeepAspectRatio).toSize().expandedTo({1,1});
        // Cache immutable sampled inputs by image identity and raster dimensions. Dynamic
        // pressure can reuse previously seen sizes; the cache is bounded to 8 MiB.
        static thread_local QCache<QString,QImage> samples(8192);
        const QString key=QString::number(b.tipMask.cacheKey())+":"+QString::number(sampleSize.width())+":"+QString::number(sampleSize.height());
        QImage mask;
        if(const auto *cached=samples.object(key)) mask=*cached;
        else {
            mask=b.tipMask.size()==sampleSize?b.tipMask.convertToFormat(QImage::Format_Grayscale8)
                :b.tipMask.scaled(sampleSize,Qt::IgnoreAspectRatio,Qt::SmoothTransformation).convertToFormat(QImage::Format_Grayscale8);
            samples.insert(key,new QImage(mask),qMax(1,int(mask.sizeInBytes()/1024)));
        }
        // Sampled-tip hardness softens the existing coverage; it never substitutes a circle.
        if (b.hardness < .999) {
            const int radius = qMax(1, int((1 - unit(b.hardness)) * diameter * .12));
            QImage temp(mask.size(), QImage::Format_Grayscale8);
            for (int y = 0; y < mask.height(); ++y) {
                const auto *src = mask.constScanLine(y); auto *dst = temp.scanLine(y); int sum = 0;
                for (int i = -radius; i <= radius; ++i) if (i >= 0 && i < mask.width()) sum += src[i];
                for (int x = 0; x < mask.width(); ++x) {
                    dst[x] = sum / (radius*2+1);
                    if (x-radius >= 0) sum -= src[x-radius];
                    if (x+radius+1 < mask.width()) sum += src[x+radius+1];
                }
            }
            for (int x = 0; x < mask.width(); ++x) {
                int sum = 0; for (int i = -radius; i <= radius; ++i) if (i >= 0 && i < mask.height()) sum += temp.constScanLine(i)[x];
                for (int y = 0; y < mask.height(); ++y) {
                    mask.scanLine(y)[x] = sum / (radius*2+1);
                    if (y-radius >= 0) sum -= temp.constScanLine(y-radius)[x];
                    if (y+radius+1 < mask.height()) sum += temp.constScanLine(y+radius+1)[x];
                }
            }
        }
        painter.drawImage(tipRect, alphaImage(mask));
    } else if (b.hardness >= .999) {
        painter.setPen(Qt::NoPen); painter.setBrush(Qt::white); painter.drawEllipse(tipRect);
    } else {
        QRadialGradient gradient(0,0,.5);
        gradient.setColorAt(0, Qt::white);
        gradient.setColorAt(qMin(.999, unit(b.hardness)), Qt::white);
        gradient.setColorAt(1, Qt::transparent);
        painter.scale(size.width(), size.height()); painter.setPen(Qt::NoPen);
        painter.setBrush(gradient); painter.drawEllipse(QRectF(-.5,-.5,1,1));
    }
    painter.end();
    auto result=grayAlpha(rendered);
    const QImage stampColor = b.colorTip.isNull() ? QImage() : rendered;
    if (color) *color = stampColor;
    shapes.insert(shapeKey,new ShapedTip{result,stampColor},qMax(1,int((result.sizeInBytes()+stampColor.sizeInBytes()+1023)/1024)));
    return result;
}
void applyMaskEffects(QImage &mask, const BrushSettings &b, const BrushInput &input,
                      QPointF position, qreal direction, quint64 index, quint32 seed, const QTransform &textureTransform = {}) {
    const bool hasTexture=b.texture.enabled&&!b.texture.pattern.isNull();
    if (!b.dual.enabled&&!hasTexture&&!b.wetEdges&&!b.noise) return;
    QImage secondary;
    if (b.dual.enabled) {
        secondary = QImage(mask.size(), QImage::Format_ARGB32_Premultiplied); secondary.fill(Qt::transparent);
        BrushSettings dual; dual.tipMask = b.dual.tipMask; dual.size = b.dual.size;
        dual.pressureSize = false; dual.angle = b.angle; dual.roundness = b.roundness;
        dual.flipX = b.flipX; dual.flipY = b.flipY;
        const auto tip = alphaImage(shapedMask(dual, {}, 0, 0, 0, 1));
        QPainter p(&secondary); p.setRenderHint(QPainter::SmoothPixmapTransform);
        // Secondary stamps are phased along the stroke direction by their own spacing.
        const qreal spacing = qMax(.25, b.dual.size * b.dual.spacing);
        const qreal phase = std::fmod(index * qMax(.25, b.size*b.spacing), spacing) - spacing*.5;
        for (int i=0; i<qBound(1,b.dual.count,16); ++i) {
            const qreal scatter = b.dual.scatter * b.dual.size;
            QPointF offset(phase + (brushRandom(seed,index,50+i*2)*2-1)*scatter,
                           (brushRandom(seed,index,51+i*2)*2-1)*scatter);
            offset = rotate(offset,direction);
            p.drawImage(QPointF(mask.width()/2.0-tip.width()/2.0, mask.height()/2.0-tip.height()/2.0)+offset,tip);
        }
        p.end(); secondary = grayAlpha(secondary);
    }
    QImage pattern;
    if (hasTexture) pattern=b.texture.pattern.convertToFormat(QImage::Format_Grayscale8);
    const qreal textureScale=qMax(.1,b.texture.scale);
    const qreal textureContrast=std::pow(4.0,b.texture.contrast);
    const QPointF textureOffset(brushRandom(seed,index,81)*pattern.width()*textureScale,
                                brushRandom(seed,index,82)*pattern.height()*textureScale);
    const qreal depth = b.texture.depth * brushDynamicValue(b.texture.dynamics, input,index,seed,7);
    const QImage edgeMask = b.wetEdges ? mask.copy() : QImage();
    for (int y=0;y<mask.height();++y) {
        auto *row=mask.scanLine(y);
        for (int x=0;x<mask.width();++x) {
            const qreal original=row[x]/255.0; if (original<=0) continue;
            qreal coverage=original;
            if (!secondary.isNull()) coverage=maskBlend(original,secondary.constScanLine(y)[x]/255.0,b.dual.mode);
            if (!pattern.isNull()) {
                const QPointF sample=b.texture.eachTip
                    ? textureOffset+QPointF(x,y)
                    : position+textureTransform.map(QPointF(x+.5-mask.width()/2.0,y+.5-mask.height()/2.0));
                auto wrap=[](int value,int period){int r=value%period;return r<0?r+period:r;};
                const int px=wrap(int(std::floor(sample.x()/textureScale)),pattern.width());
                const int py=wrap(int(std::floor(sample.y()/textureScale)),pattern.height());
                qreal texture=pattern.constScanLine(py)[px]/255.0;
                if(b.texture.invert) texture=1-texture;
                texture=unit((texture-.5)*textureContrast+.5+b.texture.brightness);
                const qreal result=maskBlend(coverage,texture,b.texture.mode);
                coverage=coverage*(1-depth)+result*depth;
            }
            if (b.wetEdges) {
                // Coverage-gradient rim: soften the body and retain/increase edge density.
                qreal nearby=1;
                for(const auto d: {QPoint(-2,0),QPoint(2,0),QPoint(0,-2),QPoint(0,2)}) {
                    const int xx=x+d.x(), yy=y+d.y();
                    const qreal value=xx<0||yy<0||xx>=mask.width()||yy>=mask.height()?0:edgeMask.constScanLine(yy)[xx]/255.0;
                    nearby=qMin(nearby,value);
                }
                coverage*=qBound(.35,.35+2.5*(original-nearby),1.0);
            }
            if (b.noise) coverage*=.25+.75*brushRandom(seed,index,quint32(1000+y*mask.width()+x));
            row[x]=qRound(unit(coverage)*255);
        }
    }
}
qreal canvasTextureCoverage(qreal coverage, const BrushTexture &texture, const QImage &pattern, qreal depth, QPointF sample,
                            qreal contrast) {
    if(coverage<=0||pattern.isNull())return coverage;
    const qreal scale=qMax(.1,texture.scale);
    auto wrap=[](int value,int period){int r=value%period;return r<0?r+period:r;};
    const int x=wrap(int(std::floor(sample.x()/scale)),pattern.width());
    const int y=wrap(int(std::floor(sample.y()/scale)),pattern.height());
    qreal value=pattern.constScanLine(y)[x]/255.0;
    if(texture.invert)value=1-value;
    value=unit((value-.5)*contrast+.5+texture.brightness);
    return unit(coverage*(1-depth)+maskBlend(coverage,value,texture.mode)*depth);
}
void canvasTexturePreview(QImage &mask, const BrushTexture &texture, qreal depth, QPointF position, const QTransform &transform, bool preserveEmpty = false) {
    const auto pattern=texture.pattern.convertToFormat(QImage::Format_Grayscale8);
    const qreal contrast=std::pow(4.0,texture.contrast);
    for(int y=0;y<mask.height();++y){auto *row=mask.scanLine(y);for(int x=0;x<mask.width();++x){
        if (preserveEmpty && !row[x]) continue;
        const QPointF sample=position+transform.map(QPointF(x+.5-mask.width()/2.0,y+.5-mask.height()/2.0));
        row[x]=qRound(canvasTextureCoverage(row[x]/255.0,texture,pattern,depth,sample,contrast)*255);
    }}
}
QColor dabColor(const BrushSettings &b,const BrushInput &input,quint64 index,quint32 seed) {
    if(!b.colorDynamics.enabled) return b.color;
    const auto &d=b.colorDynamics; if(!d.perTip) index=0;
    const qreal mix=unit(d.foregroundBackground)* (d.control==BrushControl::Off ? brushRandom(seed,index,101) : brushControlValue(d.control,input,index,d.fade));
    QColor color=QColor::fromRgbF(b.color.redF()*(1-mix)+b.backgroundColor.redF()*mix,
        b.color.greenF()*(1-mix)+b.backgroundColor.greenF()*mix,b.color.blueF()*(1-mix)+b.backgroundColor.blueF()*mix);
    float h,s,v; color.getHsvF(&h,&s,&v); if(h<0)h=0;
    h=std::fmod(h+(brushRandom(seed,index,102)*2-1)*d.hue+2,1.0);
    s=unit(s+(brushRandom(seed,index,103)*2-1)*d.saturation);
    s=d.purity>=0 ? s+(1-s)*d.purity : s*(1+d.purity);
    v=unit(v+(brushRandom(seed,index,104)*2-1)*d.brightness);
    return QColor::fromRgba(QColor::fromHsvF(h,unit(s),v).rgba());
}
struct ColorTipKey {
    qint64 image;
    qreal hue, saturation, brightness, purity, tint;
    QRgb foreground;
    bool operator==(const ColorTipKey &) const = default;
    friend size_t qHash(const ColorTipKey &key,size_t seed=0) {
        return qHashMulti(seed,key.image,key.hue,key.saturation,key.brightness,key.purity,key.tint,key.foreground);
    }
};
QImage dynamicColorTip(const QImage &source,const BrushColorDynamics &d,quint64 index,quint32 seed,QColor foreground) {
    // The explicit opt-in leaves previously saved colored brushes unchanged,
    // including those whose formerly ignored color dynamics were enabled.
    const bool vary = d.enabled && d.applyToColorTip;
    if (source.isNull() || (!vary && d.tintAmount == 0) ||
        (d.hue == 0 && d.saturation == 0 && d.brightness == 0 && d.purity == 0 && d.tintAmount == 0)) return source;
    if (!d.perTip) index=0;
    const ColorTipKey key{source.cacheKey(),vary?(brushRandom(seed,index,102)*2-1)*d.hue:0,
        vary?(brushRandom(seed,index,103)*2-1)*d.saturation:0,
        vary?(brushRandom(seed,index,104)*2-1)*d.brightness:0,vary?d.purity:0,
        unit(d.tintAmount),foreground.rgba()};
    static thread_local QCache<ColorTipKey,QImage> colors(8192);
    if (const auto *cached=colors.object(key)) return *cached;
    QImage result(source.size(),QImage::Format_ARGB32_Premultiplied);
    // Shift the source palette, not the foreground color. Work on straight RGB
    // and restore the original alpha before premultiplication; translucent
    // antialiased edges and transparent holes retain exactly their coverage.
    for (int y=0;y<source.height();++y) {
        const auto *src=reinterpret_cast<const QRgb *>(source.constScanLine(y));
        auto *dst=reinterpret_cast<QRgb *>(result.scanLine(y));
        for (int x=0;x<source.width();++x) {
            const int alpha=qAlpha(src[x]);
            if (!alpha) { dst[x]=0; continue; }
            const auto rgb=qUnpremultiply(src[x]);
            const qreal red=qRed(rgb)/255.,green=qGreen(rgb)/255.,blue=qBlue(rgb)/255.;
            const qreal maximum=qMax(red,qMax(green,blue)),minimum=qMin(red,qMin(green,blue));
            const qreal chroma=maximum-minimum;
            qreal hue=0;
            if (chroma>0) {
                if (maximum==red) hue=(green-blue)/chroma;
                else if (maximum==green) hue=2+(blue-red)/chroma;
                else hue=4+(red-green)/chroma;
                hue/=6;
            }
            qreal sourceSaturation = maximum>0?chroma/maximum:0;
            qreal sourceValue = maximum;
            if (key.tint > 0) {
                const qreal targetHue = qMax(0., foreground.hsvHueF());
                // Shortest hue arc avoids a red-to-yellow tint traveling through
                // blue. Value modulation keeps source light/dark shading intact.
                hue += std::remainder(targetHue - hue, 1.) * key.tint;
                sourceSaturation += (foreground.hsvSaturationF() - sourceSaturation) * key.tint;
                sourceValue *= 1 + (foreground.valueF() - 1) * key.tint;
            }
            hue+=key.hue;hue-=std::floor(hue);
            qreal saturation=unit(sourceSaturation+key.saturation);
            saturation=unit(key.purity>=0?saturation+(1-saturation)*key.purity:saturation*(1+key.purity));
            const qreal value=unit(sourceValue+key.brightness);
            const qreal h=hue*6,c=value*saturation,m=value-c;
            const qreal second=c*(1-std::abs(std::fmod(h,2.)-1));
            qreal r=0,g=0,b=0;
            switch (qMin(5,int(h))) {
            case 0:r=c;g=second;break;
            case 1:r=second;g=c;break;
            case 2:g=c;b=second;break;
            case 3:g=second;b=c;break;
            case 4:r=second;b=c;break;
            default:r=c;b=second;break;
            }
            dst[x]=qPremultiply(qRgba(qRound((r+m)*255),qRound((g+m)*255),qRound((b+m)*255),alpha));
        }
    }
    colors.insert(key,new QImage(result),qMax(1,int((result.sizeInBytes()+1023)/1024)));
    return result;
}
using RGB=std::array<qreal,3>;
qreal lum(const RGB &c){return .3*c[0]+.59*c[1]+.11*c[2];}
qreal sat(const RGB &c){return *std::max_element(c.begin(),c.end())-*std::min_element(c.begin(),c.end());}
RGB setLum(RGB c,qreal l){
    const qreal delta=l-lum(c); for(auto &v:c)v+=delta;
    const qreal n=*std::min_element(c.begin(),c.end()), x=*std::max_element(c.begin(),c.end());
    if(n<0)for(auto &v:c)v=l+(v-l)*l/(l-n);
    if(x>1)for(auto &v:c)v=l+(v-l)*(1-l)/(x-l);
    return c;
}
RGB setSat(RGB c,qreal s){
    const auto [lo,hi]=std::minmax_element(c.begin(),c.end()); const qreal low=*lo,range=*hi-low;
    for(auto &v:c)v=range>0?(v-low)*s/range:0; return c;
}
RGB blendColor(RGB d,RGB s,BrushBlend mode){
    if(mode==BrushBlend::Hue)return setLum(setSat(s,sat(d)),lum(d));
    if(mode==BrushBlend::Saturation)return setLum(setSat(d,sat(s)),lum(d));
    if(mode==BrushBlend::Color)return setLum(s,lum(d));
    if(mode==BrushBlend::Luminosity)return setLum(d,lum(s));
    RGB out;
    for(int i=0;i<3;++i){ const qreal a=d[i],b=s[i]; qreal r=b;
        switch(mode){
        case BrushBlend::Multiply:r=a*b;break;
        case BrushBlend::Screen:r=1-(1-a)*(1-b);break;
        case BrushBlend::Overlay:r=a<=.5?2*a*b:1-2*(1-a)*(1-b);break;
        case BrushBlend::Darken:r=qMin(a,b);break;
        case BrushBlend::Lighten:r=qMax(a,b);break;
        case BrushBlend::ColorDodge:r=b>=1?1:qMin(1.0,a/(1-b));break;
        case BrushBlend::ColorBurn:r=b<=0?0:1-qMin(1.0,(1-a)/b);break;
        case BrushBlend::LinearDodge:r=qMin(1.0,a+b);break;
        case BrushBlend::LinearBurn:r=qMax(0.0,a+b-1);break;
        case BrushBlend::Difference:r=std::abs(a-b);break;
        case BrushBlend::Exclusion:r=a+b-2*a*b;break;
        case BrushBlend::Subtract:r=qMax(0.0,a-b);break;
        case BrushBlend::Divide:r=b<=0?1:qMin(1.0,a/b);break;
        default:break;
        }out[i]=unit(r);
    }return out;
}
QRgb composite(QRgb source,QRgb backdrop,BrushBlend mode,bool eraser,bool alphaLocked,int x,int y,quint32 seed){
    qreal sa=qAlpha(source)/255.0,da=qAlpha(backdrop)/255.0;
    if(sa==0)return backdrop;
    if(eraser)return qRgba(qRound(qRed(backdrop)*(1-sa)),qRound(qGreen(backdrop)*(1-sa)),qRound(qBlue(backdrop)*(1-sa)),qRound(qAlpha(backdrop)*(1-sa)));
    if(mode==BrushBlend::Normal&&!alphaLocked){
        // Both inputs are premultiplied bytes. Preserve the generic formula's
        // nearest-byte rounding without unpremultiplying/reblending every
        // covered pixel. The odd divisor has no half-byte rounding ties.
        const int remaining=255-qAlpha(source);
        const auto over=[remaining](int sourceChannel,int backdropChannel){
            return (sourceChannel*255+backdropChannel*remaining+127)/255;
        };
        return qRgba(over(qRed(source),qRed(backdrop)),over(qGreen(source),qGreen(backdrop)),
                     over(qBlue(source),qBlue(backdrop)),over(qAlpha(source),qAlpha(backdrop)));
    }
    RGB s={qRed(source)/255.0/sa,qGreen(source)/255.0/sa,qBlue(source)/255.0/sa};
    if(mode==BrushBlend::Dissolve){
        const quint64 key=(quint64(quint32(x))<<32)|quint32(y);
        if(brushRandom(seed,key,200)>=sa)return backdrop;sa=1;
    }
    RGB d=da==0?RGB{0,0,0}:RGB{qRed(backdrop)/255.0/da,qGreen(backdrop)/255.0/da,qBlue(backdrop)/255.0/da};
    const auto blend=blendColor(d,s,mode);
    if(alphaLocked){
        RGB out;for(int i=0;i<3;++i)out[i]=((1-sa)*d[i]+sa*blend[i])*da;
        // Preserve the original byte exactly, including partially covered edges.
        return qRgba(qRound(unit(out[0])*255),qRound(unit(out[1])*255),qRound(unit(out[2])*255),qAlpha(backdrop));
    }
    const qreal alpha=sa+da-sa*da;
    RGB out;for(int i=0;i<3;++i)out[i]=(1-sa)*d[i]*da+(1-da)*s[i]*sa+sa*da*blend[i];
    return qRgba(qRound(unit(out[0])*255),qRound(unit(out[1])*255),qRound(unit(out[2])*255),qRound(alpha*255));
}
}

qreal brushCanvasTextureCoverage(qreal coverage,const BrushTexture &texture,
                                 const QImage &pattern,qreal depth,QPointF sample){
    return canvasTextureCoverage(coverage,texture,pattern,depth,sample,std::pow(4.0,texture.contrast));
}
QImage brushColorTipDynamics(const QImage &source,const BrushSettings &settings,quint64 index,quint32 seed) {
    return dynamicColorTip(source,settings.colorDynamics,index,seed,settings.color);
}
void resolveBrushStrokeColor(BrushSettings &b,const BrushInput &input,quint32 seed) {
    if (!b.colorDynamics.enabled || b.colorDynamics.perTip) return;
    if (!b.colorTip.isNull() && b.colorDynamics.applyToColorTip) {
        b.colorTip=dynamicColorTip(b.colorTip,b.colorDynamics,0,seed,b.color);
        b.colorDynamics.tintAmount=0;
    } else
        b.color=dabColor(b,resolvedBrushInput(b,input),0,seed);
    b.colorDynamics.enabled=false;
}
qreal brushRandom(quint32 seed,quint64 dab,quint32 channel){
    quint64 value=dab^(quint64(seed)<<32)^(quint64(channel)*0x9e3779b97f4a7c15ULL);
    value=(value^(value>>30))*0xbf58476d1ce4e5b9ULL;
    value=(value^(value>>27))*0x94d049bb133111ebULL;value^=value>>31;
    return (value>>11)*(1.0/9007199254740992.0);
}
int brushTipFrameIndex(const BrushSettings &b, const BrushInput &input, qreal direction, quint64 dab, quint32 seed) {
    const int count = b.tipFrames.size();
    if (!count) return -1;
    if (b.tipOrder == BrushTipOrder::Ordered) return int(dab % count);
    if (b.tipOrder == BrushTipOrder::Pressure) return qMin(count - 1, int(unit(input.pressure) * count));
    if (b.tipOrder == BrushTipOrder::Direction) {
        const qreal phase = direction / 360. - std::floor(direction / 360.);
        return qMin(count - 1, int(phase * count));
    }
    if (b.tipOrder == BrushTipOrder::Shuffle) {
        if (count < 3) return int((dab + (brushRandom(seed, 0, 907) >= .5 ? 1 : 0)) % count);
        const quint64 block = dab / count;
        // Every bag contains every tip exactly once. Swapping the first two
        // entries leaves its last entry untouched, so the preceding bag's last
        // tip can be recovered independently without replaying earlier dabs.
        auto bag = [&](quint64 cycle) {
            std::array<int, 16> order{};
            for (int i = 0; i < count; ++i) order[i] = i;
            std::sort(order.begin(), order.begin() + count, [&](int a, int c) {
                const auto x = brushRandom(seed, cycle, 910 + a), y = brushRandom(seed, cycle, 910 + c);
                return x == y ? a < c : x < y;
            });
            return order;
        };
        auto order = bag(block);
        if (block && order[0] == bag(block - 1)[count - 1]) std::swap(order[0], order[1]);
        return order[dab % count];
    }
    qreal total = 0;
    for (const auto &frame : b.tipFrames) total += frame.weight;
    qreal pick = brushRandom(seed, dab, 906) * total;
    for (int i = 0; i < count; ++i) { pick -= b.tipFrames[i].weight; if (pick < 0) return i; }
    return count - 1;
}
QImage brushTipFrameImage(const BrushSettings &b, int frame) {
    const auto &atlas = b.colorTip.isNull() ? b.tipMask : b.colorTip;
    if (frame < 0 || frame >= b.tipFrames.size() || atlas.isNull()) return atlas;
    const auto r = b.tipFrames[frame].rect;
    static thread_local QCache<QString, QImage> frames(16384);
    const auto key = QString::number(atlas.cacheKey()) + ":" + QString::number(r.x()) + ":" + QString::number(r.y())
        + ":" + QString::number(r.width()) + ":" + QString::number(r.height());
    if (const auto *cached = frames.object(key)) return *cached;
    const auto image = atlas.copy(r);
    frames.insert(key, new QImage(image), qMax(1, int((image.sizeInBytes() + 1023) / 1024)));
    return image;
}
bool setBrushTipImages(BrushSettings &brush, const QVector<QImage> &images, const QVector<QPointF> &anchors, QString *error) {
    auto fail = [&](const QString &message) { if (error) *error = message; return false; };
    if (images.isEmpty() || images.size() > 16 || (!anchors.isEmpty() && anchors.size() != images.size()))
        return fail("Choose between 1 and 16 tip images with matching anchors.");
    const bool colored = images.first().format() == QImage::Format_ARGB32_Premultiplied;
    const int columns = int(std::ceil(std::sqrt(double(images.size()))));
    int width = 0, height = 0;
    for (const auto &image : images) {
        if (image.isNull() || image.width() > 512 || image.height() > 512 ||
            image.format() != (colored ? QImage::Format_ARGB32_Premultiplied : QImage::Format_Grayscale8))
            return fail("Choose matching monochrome or color tips up to 512 × 512.");
        bool visible = false;
        for (int y = 0; y < image.height() && !visible; ++y)
            for (int x = 0; x < image.width(); ++x)
                if (colored ? qAlpha(image.pixel(x,y)) > 0 : image.constScanLine(y)[x] > 0) { visible = true; break; }
        if (!visible) return fail("Each tip image needs visible pixels.");
        width = qMax(width, image.width()); height = qMax(height, image.height());
    }
    QImage atlas(columns * width, ((images.size() + columns - 1) / columns) * height, QImage::Format_ARGB32_Premultiplied);
    if (atlas.isNull()) return fail("Not enough memory for these brush tips.");
    atlas.fill(colored ? Qt::transparent : Qt::black);
    QPainter painter(&atlas); QVector<BrushTipFrame> frames;
    for (int i = 0; i < images.size(); ++i) {
        const QPoint origin((i % columns) * width, (i / columns) * height);
        auto source = images[i]; source.setDevicePixelRatio(1);
        painter.drawImage(origin, source);
        frames.append({QRect(origin, images[i].size()), anchors.isEmpty() ? QPointF(.5,.5) : anchors[i], 1});
    }
    painter.end(); auto candidate = brush;
    candidate.tipFrames = frames;
    if (colored) { candidate.colorTip = atlas; candidate.tipMask = {}; }
    else { candidate.tipMask = atlas.convertToFormat(QImage::Format_Grayscale8); candidate.colorTip = {}; }
    if (!validateBrushSettings(candidate, error)) return false;
    brush = std::move(candidate); return true;
}
BrushInput resolvedBrushInput(const BrushSettings &b,BrushInput input){
    auto finite=[](qreal value,qreal fallback){return std::isfinite(value)?value:fallback;};
    input.pressure=unit(finite(b.pose.overridePressure?b.pose.pressure:input.pressure,1));
    input.tiltX=qBound(-90.0,finite(b.pose.overrideTiltX?b.pose.tiltX:input.tiltX,0),90.0);
    input.tiltY=qBound(-90.0,finite(b.pose.overrideTiltY?b.pose.tiltY:input.tiltY,0),90.0);
    input.rotation=finite(b.pose.overrideRotation?b.pose.rotation:input.rotation,0);
    input.wheel=unit(finite(input.wheel,1));
    input.speed=qBound(0.,finite(input.speed,0),1000000.);return input;
}
qreal brushControlValue(BrushControl control,const BrushInput &input,quint64 dab,int fade,qreal direction,qreal initial){
    switch(control){
    case BrushControl::Off:return 1;
    case BrushControl::Pressure:return unit(input.pressure);
    case BrushControl::Tilt:return unit(std::hypot(input.tiltX,input.tiltY)/90);
    case BrushControl::Wheel:return unit(input.wheel);
    case BrushControl::Fade:return qMax(0.0,1-qreal(dab)/qMax(1,fade));
    case BrushControl::InitialDirection:return std::fmod(initial+720,360.0)/360;
    case BrushControl::Direction:return std::fmod(direction+720,360.0)/360;
    case BrushControl::Rotation:return input.rotation/360-std::floor(input.rotation/360);
    }return 1;
}
qreal brushResponseValue(const BrushDynamic &d,qreal input){
    input=unit(input);
    if(d.response==BrushDynamic{}.response)return input;
    const qreal position=input*4;
    const int segment=qMin(3,int(position));
    return d.response[segment]+(d.response[segment+1]-d.response[segment])*(position-segment);
}
qreal brushDynamicValue(const BrushDynamic &d,const BrushInput &input,quint64 dab,quint32 seed,quint32 channel,qreal direction,qreal initial){
    const qreal control=d.control==BrushControl::Off?1:brushResponseValue(d,brushControlValue(d.control,input,dab,d.fade,direction,initial));
    return qBound(unit(d.minimum),control*(1-unit(d.jitter)*brushRandom(seed,d.jitterPerStroke?0:dab,channel)),1.0);
}
QImage brushTipMask(const BrushSettings &b,const BrushInput &in,qreal direction,qreal initial,quint64 index,quint32 seed,QPointF position){
    auto settings=b;settings.scatter=0;
    // Cursor outlines only need coverage; palette shifts never change alpha.
    settings.colorDynamics.applyToColorTip=false;
    settings.colorDynamics.tintAmount=0;settings.clusterSpray.enabled=false;
    return evaluateBrushDab(settings,in,position,direction,initial,index,seed).mask;
}
int brushDabCount(const BrushSettings &b,const BrushInput &in,quint64 index,quint32 seed){
    const auto input=resolvedBrushInput(b,in);
    const qreal factor=brushControlValue(b.countControl,input,index,b.countFade)*(1-unit(b.countJitter)*brushRandom(seed,index,20));
    return qBound(1,qRound(qBound(1,b.count,16)*factor),16);
}
BrushDab evaluateBrushDab(const BrushSettings &b,const BrushInput &in,QPointF position,qreal direction,qreal initial,quint64 index,quint32 seed,const QTransform &textureTransform,BrushDabPurpose purpose,int scatterCopy,quint32 scatterSeed){
    const auto input=resolvedBrushInput(b,in);BrushDab dab;dab.surface=b.surface;
    const qreal scatter=b.scatter*b.size*brushControlValue(b.scatterControl,input,index,b.scatterFade);
    dab.offset=rotate({b.scatterBothAxes?(brushRandom(seed,index,30)*2-1)*scatter:0,
                      (brushRandom(seed,index,31)*2-1)*scatter},direction);
    if (b.clusterSpray.enabled) dab.offset = brushClusterOffset(b.clusterSpray,b.size,direction,index,scatterCopy>=0?scatterSeed:seed,qMax(0,scatterCopy));
    QPointF anchorOffset;
    dab.mask=shapedMask(b,input,direction,initial,index,seed,&dab.colorTip,&anchorOffset);
    dab.colorTip=dynamicColorTip(dab.colorTip,b.colorDynamics,index,seed,b.color);
    dab.offset += anchorOffset;
    auto effects=b;
    const bool canvasTexture=b.texture.enabled&&!b.texture.eachTip&&!b.texture.pattern.isNull();
    if(canvasTexture)effects.texture.enabled=false;
    applyMaskEffects(dab.mask,effects,input,position+dab.offset,direction,index,seed,textureTransform);
    if (!dab.colorTip.isNull() && (effects.dual.enabled || (effects.texture.enabled && !effects.texture.pattern.isNull()) || effects.wetEdges || effects.noise)) {
        for (int y = 0; y < dab.mask.height(); ++y) {
            auto *mask = dab.mask.scanLine(y);
            auto *pixels = reinterpret_cast<QRgb *>(dab.colorTip.scanLine(y));
            for (int x = 0; x < dab.mask.width(); ++x) {
                // Additive dual-tip effects cannot invent color beyond the source silhouette.
                if (!qAlpha(pixels[x])) { mask[x] = 0; continue; }
                const auto rgb = qUnpremultiply(pixels[x]);
                pixels[x] = qPremultiply(qRgba(qRed(rgb),qGreen(rgb),qBlue(rgb),mask[x]));
            }
        }
    }
    if(canvasTexture){
        dab.untexturedMask=dab.mask;dab.canvasTexture=b.texture;
        dab.canvasTextureDepth=b.texture.depth*brushDynamicValue(b.texture.dynamics,input,index,seed,7);
        if(purpose==BrushDabPurpose::Preview)
            canvasTexturePreview(dab.mask,b.texture,dab.canvasTextureDepth,position+dab.offset,textureTransform,!dab.colorTip.isNull());
    }
    dab.color=dabColor(b,input,index,seed);
    dab.flow=unit(b.opacity)*brushDynamicValue(b.flowDynamics,input,index,seed,32);
    dab.opacity=unit(unit(b.strokeOpacity)*brushDynamicValue(b.opacityDynamics,input,index,seed,33)
        * brushSpeedFactor(b.speedDynamics,input.speed,b.speedDynamics.opacityAmount));
    return dab;
}
QPainterPath brushSymmetryGuide(const BrushSymmetry &s, QSizeF artwork) {
    QPainterPath path;
    const qreal extent = std::hypot(artwork.width(),artwork.height()) * 2;
    auto line = [&](QPointF a,QPointF b){path.moveTo(a);path.lineTo(b);};
    switch(s.mode){
    case SymmetryMode::Vertical: line({0,-extent},{0,extent}); break;
    case SymmetryMode::Horizontal: line({-extent,0},{extent,0}); break;
    case SymmetryMode::DualAxis: line({0,-extent},{0,extent});line({-extent,0},{extent,0});break;
    case SymmetryMode::Diagonal: line({-extent,-extent},{extent,extent});break;
    case SymmetryMode::Wave: {
        const qreal step=qMax(1.0,qMax(1.0,s.wavelength)/96);
        path.moveTo(-extent,s.amplitude*std::sin(-extent*2*pi/qMax(1.0,s.wavelength)));
        for(qreal x=-extent+step;x<=extent;x+=step)path.lineTo(x,s.amplitude*std::sin(x*2*pi/qMax(1.0,s.wavelength)));
        break;
    }
    case SymmetryMode::Circle: path.addEllipse(QPointF(),s.radius,s.radius);break;
    case SymmetryMode::Spiral:
        for(int i=0;i<=1024;++i){const qreal t=i*8*pi/1024,r=qMax(1.0,s.radius)+qMax(1.0,s.distance)*t/(2*pi);const QPointF p(r*std::cos(t),r*std::sin(t));if(i==0)path.moveTo(p);else path.lineTo(p);}break;
    case SymmetryMode::ParallelLines:
        line({0,-extent},{0,extent});for(int i=1;i<qBound(2,s.count,12);++i){const qreal x=(i%2?1:-1)*((i+1)/2)*s.distance;line({x,-extent},{x,extent});}break;
    case SymmetryMode::Radial:
        for(int i=0;i<qBound(2,s.count,12);++i)line({},rotate({extent,0},360.0*i/qBound(2,s.count,12)));break;
    case SymmetryMode::Off:break;
    }
    QTransform transform;transform.translate(s.center.x()*artwork.width(),s.center.y()*artwork.height());transform.rotate(s.angle);return transform.map(path);
}
QVector<BrushSymmetryInstance> brushSymmetryInstances(const BrushSymmetry &s,QPointF point,QSizeF artwork){
    QVector<BrushSymmetryInstance> out{{point,{}}};if(s.mode==SymmetryMode::Off)return out;
    const QPointF center(s.center.x()*artwork.width(),s.center.y()*artwork.height());
    const QPointF local=rotate(point-center,-s.angle);
    auto append=[&](QPointF q,QTransform localTransform){
        const auto target=center+rotate(q,s.angle);
        QTransform t;t.rotate(-s.angle);t*=localTransform;QTransform r;r.rotate(s.angle);t*=r;
        if(QLineF(target,point).length()<.0001&&localTransform.isIdentity())return;
        out.append({target,t});
    };
    auto mirror=[&](QPointF origin,qreal tangent){
        auto delta=rotate(local-origin,-tangent);delta.setY(-delta.y());
        const qreal a = radians(2*tangent);
        QTransform t(std::cos(a),std::sin(a),std::sin(a),-std::cos(a),0,0);
        append(origin+rotate(delta,tangent),t);
    };
    switch(s.mode){
    case SymmetryMode::Vertical:{QTransform t;t.scale(-1,1);append({-local.x(),local.y()},t);break;}
    case SymmetryMode::Horizontal:{QTransform t;t.scale(1,-1);append({local.x(),-local.y()},t);break;}
    case SymmetryMode::DualAxis:{QTransform x;x.scale(-1,1);append({-local.x(),local.y()},x);QTransform y;y.scale(1,-1);append({local.x(),-local.y()},y);QTransform both;both.scale(-1,-1);append(-local,both);break;}
    case SymmetryMode::Diagonal:mirror({},45);break;
    case SymmetryMode::Wave:{
        const qreal wavelength=qMax(1.0,s.wavelength), k=2*pi/wavelength;
        // Coarse nearest-point search then Newton refinement, bounded to nearby periods.
        qreal bestX=local.x(),best=1e100;
        for(int i=-32;i<=32;++i){const qreal x=local.x()+i*wavelength/32;const qreal y=s.amplitude*std::sin(k*x);const qreal d=std::hypot(x-local.x(),y-local.y());if(d<best){best=d;bestX=x;}}
        for(int i=0;i<8;++i){const qreal y=s.amplitude*std::sin(k*bestX),dy=s.amplitude*k*std::cos(k*bestX),ddy=-k*k*y;
            const qreal denominator=1+dy*dy+(y-local.y())*ddy;if(std::abs(denominator)<1e-8)break;
            bestX-=qBound(-wavelength/8,((bestX-local.x())+(y-local.y())*dy)/denominator,wavelength/8);}
        const QPointF p(bestX,s.amplitude*std::sin(k*bestX));mirror(p,std::atan(s.amplitude*k*std::cos(k*bestX))*180/pi);break;}
    case SymmetryMode::Circle:{
        qreal a=angleOf(local);const QPointF nearest=rotate({qMax(1.0,s.radius),0},a);mirror(nearest,a+90);break;}
    case SymmetryMode::Spiral:{
        const qreal pitch=qMax(1.0,s.distance)/(2*pi), start=qMax(1.0,s.radius);
        qreal best=1e100,bestT=0;
        // Finite four-turn Archimedean guide; reflect at the nearest curve tangent.
        for(int i=0;i<=512;++i){const qreal t=i*8*pi/512,r=start+pitch*t;const QPointF p(r*std::cos(t),r*std::sin(t));const qreal d=QLineF(local,p).length();if(d<best){best=d;bestT=t;}}
        for(int i=0;i<8;++i){const qreal t=bestT,r=start+pitch*t;const QPointF p(r*std::cos(t),r*std::sin(t));
            const QPointF derivative(pitch*std::cos(t)-r*std::sin(t),pitch*std::sin(t)+r*std::cos(t));
            const QPointF second(-2*pitch*std::sin(t)-r*std::cos(t),2*pitch*std::cos(t)-r*std::sin(t));
            const QPointF delta=p-local;const qreal denominator=QPointF::dotProduct(derivative,derivative)+QPointF::dotProduct(delta,second);
            if(std::abs(denominator)<1e-8)break;bestT=qBound(0.0,bestT-qBound(-.1,QPointF::dotProduct(delta,derivative)/denominator,.1),8*pi);}
        const qreal r=start+pitch*bestT;const QPointF p(r*std::cos(bestT),r*std::sin(bestT));
        mirror(p,angleOf({pitch*std::cos(bestT)-r*std::sin(bestT),pitch*std::sin(bestT)+r*std::cos(bestT)}));break;}
    case SymmetryMode::ParallelLines:
        for(int i=1;i<qBound(2,s.count,12);++i)append(local+QPointF((i%2?1:-1)*((i+1)/2)*s.distance,0),{});break;
    case SymmetryMode::Radial:
        for(int i=1;i<qBound(2,s.count,12);++i){const qreal a=360.0*i/qBound(2,s.count,12);QTransform t;t.rotate(a);append(rotate(local,a),t);}break;
    case SymmetryMode::Off:break;
    }return out;
}
bool compositeBrushAccumulation(RasterLayer &layer,const QMap<TileKey,QImage> &paint,
                                BrushBlend blend,bool alphaLocked,int availableTiles,quint32 seed){
    if(layer.group)return true;
    if(!alphaLocked){
        int required=0;
        for(auto it=paint.cbegin();it!=paint.cend();++it)if(!layer.tiles.contains(it.key()))++required;
        if(required>availableTiles)return false;
    }
    auto result=layer.tiles;
    for(auto it=paint.cbegin();it!=paint.cend();++it){
        const auto key=it.key();
        if(alphaLocked&&!result.contains(key))continue;
        auto tile=result.value(key);
        if(tile.isNull()){
            tile=QImage(tileSize,tileSize,QImage::Format_ARGB32_Premultiplied);
            if(tile.isNull())return false;
            tile.fill(Qt::transparent);
        }
        for(int y=0;y<tileSize;++y){
            const auto *source=reinterpret_cast<const QRgb *>(it->constScanLine(y));
            auto *destination=reinterpret_cast<QRgb *>(tile.scanLine(y));
            for(int x=0;x<tileSize;++x){
                if(!qAlpha(source[x])||(alphaLocked&&!qAlpha(destination[x])))continue;
                destination[x]=composite(source[x],destination[x],blend,false,alphaLocked,
                    key.first*tileSize+x,key.second*tileSize+y,seed);
            }
        }
        result.insert(key,tile);
    }
    layer.tiles=std::move(result);return true;
}
bool applyBrushDab(RasterLayer &layer,RasterLayer &accumulation,const RasterLayer &original,
                   const BrushDab &dab,QPointF point,const QTransform &tipTransform,BrushBlend blend,
                   bool eraser,QRectF bounds,int availableTiles,quint32 seed,const QPainterPath *selection,
                   RasterSelectionCache *selectionCache){
    if(layer.group||(layer.alphaLocked&&eraser)||dab.mask.isNull()||dab.flow<=0||dab.opacity<=0
        ||(selection&&selection->isEmpty()))return true;
    const QImage &silhouette=dab.untexturedMask.isNull()?dab.mask:dab.untexturedMask;
    const QPointF anchor(std::floor(point.x()),std::floor(point.y())),phase=point-anchor;
    QTransform transform=tipTransform;
    const QRectF rect(-silhouette.width()/2.0,-silhouette.height()/2.0,silhouette.width(),silhouette.height());
    const QRect region=transform.mapRect(rect).translated(phase).toAlignedRect().adjusted(-1,-1,1,1);
    QImage sampled(region.size(),QImage::Format_ARGB32_Premultiplied);if(sampled.isNull())return false;
    sampled.fill(Qt::transparent);QPainter painter(&sampled);painter.setRenderHint(QPainter::SmoothPixmapTransform);
    painter.translate(phase-region.topLeft());painter.setTransform(transform,true);painter.drawImage(rect.topLeft(),dab.colorTip.isNull()?alphaImage(silhouette):dab.colorTip);painter.end();
    const QPoint origin=anchor.toPoint()+region.topLeft();
    if(!dab.untexturedMask.isNull()){
        const auto pattern=dab.canvasTexture.pattern.convertToFormat(QImage::Format_Grayscale8);
        const qreal contrast=std::pow(4.0,dab.canvasTexture.contrast);
        for(int y=0;y<sampled.height();++y){auto *row=reinterpret_cast<QRgb *>(sampled.scanLine(y));
            for(int x=0;x<sampled.width();++x){if(!dab.colorTip.isNull()&&!qAlpha(row[x]))continue;const int coverage=qRound(canvasTextureCoverage(qAlpha(row[x])/255.0,dab.canvasTexture,pattern,dab.canvasTextureDepth,QPointF(origin.x()+x+.5,origin.y()+y+.5),contrast)*255);
                if (dab.colorTip.isNull()) row[x]=qRgba(coverage,coverage,coverage,coverage);
                else {
                    const auto rgb=qUnpremultiply(row[x]);
                    row[x]=qPremultiply(qRgba(qRed(rgb),qGreen(rgb),qBlue(rgb),coverage));
                }
            }
        }
    }
    QImage surfaceCoverage;
    if (dab.surface.kind != BrushSurfaceKind::Off)
        applyBrushSurface(sampled, origin, BrushSurfaceSampler(dab.surface, dab.color), !dab.colorTip.isNull(), &surfaceCoverage);
    const QRect area=QRect(origin,sampled.size()).intersected(bounds.toAlignedRect());if(area.isEmpty())return true;
    const qreal dabFlow=unit(dab.flow),dabOpacity=unit(dab.opacity);
    const bool colored=!dab.colorTip.isNull() || dab.surface.kind == BrushSurfaceKind::Image;
    const QRgb foreground=dab.color.rgb();
    RasterSelectionCache fallbackSelection;
    if(!selectionCache){fallbackSelection.reset(selection);selectionCache=&fallbackSelection;}
    const int left=int(std::floor(qreal(area.left())/tileSize)),right=int(std::floor(qreal(area.right())/tileSize));
    const int top=int(std::floor(qreal(area.top())/tileSize)),bottom=int(std::floor(qreal(area.bottom())/tileSize));
    for(int ty=top;ty<=bottom;++ty)for(int tx=left;tx<=right;++tx){
        const TileKey key{tx,ty};const QRect tileRect(tx*tileSize,ty*tileSize,tileSize,tileSize),part=area.intersected(tileRect);
        const auto coverage=selectionCache->tile(key);
        if(coverage.coverage==RasterSelectionTile::Coverage::Empty)continue;
        if(coverage.coverage==RasterSelectionTile::Coverage::Invalid)return false;
        const auto &selectedMask=coverage.mask;
        const auto old=original.tiles.constFind(key);
        if((eraser||layer.alphaLocked)&&old==original.tiles.cend())continue;
        bool any=false;for(int y=part.top();y<=part.bottom()&&!any;++y){const auto *row=reinterpret_cast<const QRgb*>(sampled.constScanLine(y-origin.y()));
            const QRgb *base=old==original.tiles.cend()?nullptr:reinterpret_cast<const QRgb*>(old->constScanLine(y-ty*tileSize));
            const auto selectedRow=selectedMask.isNull()?nullptr:selectedMask.constScanLine(y-ty*tileSize);
            for(int x=part.left();x<=part.right();++x)if(qAlpha(row[x-origin.x()])>0&&bounds.contains(QPointF(x+.5,y+.5))
                &&(!selectedRow||selectedRow[x-tx*tileSize])
                &&(!layer.alphaLocked||(base&&qAlpha(base[x-tx*tileSize])>0))){any=true;break;}}
        if(!any)continue;
        if(!layer.tiles.contains(key)){
            if(availableTiles--<=0)return false;QImage tile(tileSize,tileSize,QImage::Format_ARGB32_Premultiplied);
            if(tile.isNull())return false;tile.fill(Qt::transparent);layer.tiles.insert(key,tile);
        }
        if(!accumulation.tiles.contains(key)){
            QImage tile(tileSize,tileSize,QImage::Format_RGBA64_Premultiplied);if(tile.isNull())return false;tile.fill(Qt::transparent);accumulation.tiles.insert(key,tile);
        }
        auto &target=layer.tiles[key];auto &paint=accumulation.tiles[key];
        for(int y=part.top();y<=part.bottom();++y){
            const auto *src=reinterpret_cast<const QRgb*>(sampled.constScanLine(y-origin.y()));
            auto *acc=reinterpret_cast<QRgba64*>(paint.scanLine(y-ty*tileSize));auto *dst=reinterpret_cast<QRgb*>(target.scanLine(y-ty*tileSize));
            const QRgb *base=old==original.tiles.cend()?nullptr:reinterpret_cast<const QRgb*>(old->constScanLine(y-ty*tileSize));
            const auto selectedRow=selectedMask.isNull()?nullptr:selectedMask.constScanLine(y-ty*tileSize);
            for(int x=part.left();x<=part.right();++x){
                if(!bounds.contains(QPointF(x+.5,y+.5)))continue;
                if(selectedRow&&!selectedRow[x-tx*tileSize])continue;
                const qreal surfaceAlpha = surfaceCoverage.isNull() ? 1.0 : surfaceCoverage.constScanLine(y-origin.y())[x-origin.x()]/255.0;
                const qreal flow=qMin(1.0,qAlpha(src[x-origin.x()])/255.0/qMax(surfaceAlpha,1.0/255.0))*dabFlow;if(flow<=0||surfaceAlpha<=0)continue;
                const int at=x-tx*tileSize;
                if(layer.alphaLocked&&(!base||qAlpha(base[at])==0))continue;
                const qreal previous=acc[at].alpha()/65535.0;
                // Preserve old coverage when pressure drops, while low-opacity color
                // still mixes only at its actual strength. 16-bit working paint avoids
                // low-flow buildup stalling well below the selected opacity.
                const qreal cap=dabOpacity*(selectedRow ? selectedRow[at]/255.0 : 1.0)*surfaceAlpha, added=cap*flow;
                const qreal retained=previous>cap?1-added/previous:1-flow;
                const qreal alpha=previous*retained+added;
                const auto channel=[&](quint16 oldChannel,int color){return quint16(qBound(0,qRound(oldChannel*retained+color*257*added),65535));};
                const QRgb sourceColor = colored ? qUnpremultiply(src[x-origin.x()]) : foreground;
                acc[at]=QRgba64::fromRgba64(channel(acc[at].red(),qRed(sourceColor)),channel(acc[at].green(),qGreen(sourceColor)),channel(acc[at].blue(),qBlue(sourceColor)),quint16(qRound(alpha*65535)));
                dst[at]=composite(acc[at].toArgb32(),base?base[at]:0,blend,eraser,layer.alphaLocked,x,y,seed);
            }
        }
    }
    return true;
}
QImage brushStrokePreview(const BrushSettings &brush,QSize size,QColor background){
    size=size.boundedTo({2048,512}).expandedTo({1,1});
    QImage result(size,QImage::Format_ARGB32_Premultiplied);result.fill(background);
    BrushSettings b=brush;
    const quint32 seed=brush.lockRandomSeed?brush.randomSeed:1;
    b.size=qBound(3.0,b.size,32.0);
    b.dual.size=qBound(1.0,b.dual.size*b.size/qMax(1.0,brush.size),64.0);
    if (b.surface.kind != BrushSurfaceKind::Off) {
        if (b.surface.perspective) {
            // Show the complete authored plane instead of a narrow stroke that
            // may miss its artwork coordinates entirely. A uniform fit retains
            // the quad's geometry and the source-plane/tile relationship.
            QRectF source;
            for (const auto &corner : b.surface.quad) source |= QRectF(corner,QSizeF(.001,.001));
            const qreal insetX=qMin(12.,size.width()*.08),insetY=qMin(8.,size.height()*.12);
            const QRectF target(insetX,insetY,qMax(1.,size.width()-2*insetX),qMax(1.,size.height()-2*insetY));
            if (source.width()>0 && source.height()>0) {
                const qreal scale=qMin(target.width()/source.width(),target.height()/source.height());
                for (auto &corner : b.surface.quad) corner=target.center()+(corner-source.center())*scale;
                const BrushSurfaceSampler surface(b.surface,b.color);
                if (surface.isValid()) {
                    QImage swatch(size,QImage::Format_ARGB32_Premultiplied);swatch.fill(Qt::transparent);
                    for (int y=0;y<size.height();++y) {
                        auto *row=reinterpret_cast<QRgb *>(swatch.scanLine(y));
                        for (int x=0;x<size.width();++x) row[x]=surface.sample({x+.5,y+.5});
                    }
                    QPainter painter(&result);painter.setOpacity(unit(b.strokeOpacity)*unit(b.opacity));painter.drawImage(0,0,swatch);
                    return result;
                }
            }
        } else {
            // Preview brush width is capped, so surface periods and phase must
            // use the same scale. Otherwise a tiny preview shows isolated window
            // fragments even though the painted brush contains complete repeats.
            const qreal scale=b.size/qMax(1.,brush.size);
            b.surface.tileSize={qBound(1.,b.surface.tileSize.width()*scale,8192.),qBound(1.,b.surface.tileSize.height()*scale,8192.)};
            b.surface.phase={qBound(-32768.,b.surface.phase.x()*scale,32768.),qBound(-32768.,b.surface.phase.y()*scale,32768.)};
            b.surface.frequency=qBound(.1,b.surface.frequency/scale,50.);
            b.surface.fadeLength=qBound(1.,b.surface.fadeLength*scale,8192.);
        }
    }
    RasterLayer output,accumulation,original;
    for(int y=0;y<size.height();y+=tileSize)for(int x=0;x<size.width();x+=tileSize){
        QImage tile(tileSize,tileSize,QImage::Format_ARGB32_Premultiplied);tile.fill(background);original.tiles[{x/tileSize,y/tileSize}]=tile;
    }
    output.tiles=original.tiles;
    const QRectF bounds(QPointF(),size);QPointF last;int index=0;
    ConnectedBrushState connected;
    BrushInput initial;initial.pressure=.3;
    // UI previews need only their small paintable source. Resolve per-stroke
    // color after reduction, avoiding a full atlas HSV/tint scan per thumbnail.
    if (!b.colorTip.isNull() && qMax(b.colorTip.width(),b.colorTip.height()) > 128) {
        const auto reduced=[](const QImage &image) {
            static thread_local QCache<qint64,QImage> previews(8192);
            if (const auto *cached=previews.object(image.cacheKey())) return *cached;
            auto result=image.scaled({128,128},Qt::KeepAspectRatio,Qt::SmoothTransformation);
            previews.insert(image.cacheKey(),new QImage(result),qMax(1,int((result.sizeInBytes()+1023)/1024)));
            return result;
        };
        if (b.tipFrames.isEmpty()) b.colorTip=reduced(b.colorTip);
        else {
            QVector<QImage> images; QVector<QPointF> anchors;
            for (int i=0;i<b.tipFrames.size();++i) {
                images.append(reduced(brushTipFrameImage(b,i)));
                anchors.append(b.tipFrames[i].anchor);
            }
            setBrushTipImages(b,images,anchors);
        }
    }
    resolveBrushStrokeColor(b,initial,seed);
    // Count can multiply the work; bound representative previews to 768 stamps.
    const qreal step=b.connected.mode!=BrushConnectedMode::Off?2.
        :qMax(qMax(.75,b.size*b.spacing),(size.width()-40)*qBound(1,b.count,16)/768.0);
    for(qreal x=20;x<size.width()-20;x+=step){
        const qreal t=(x-20)/qMax(1,size.width()-40);
        const QPointF p(x,size.height()*.5+std::sin(t*pi*2)*size.height()*.16);
        BrushInput in;in.pressure=.3+.7*std::sin(t*pi);
        if (b.speedDynamics.enabled) in.speed=b.speedDynamics.referenceSpeed*t;
        const qreal direction=index==0?0:angleOf(p-last);
        if (b.connected.mode != BrushConnectedMode::Off) {
            appendConnectedBrush(output,accumulation,original,connected,b,p,in,index,seed,QSizeF(size),bounds,16);
            ++index;last=p;continue;
        }
        for(int i=0;i<brushDabCount(b,in,index,seed);++i){
            const auto dab=evaluateBrushDab(b,in,p,direction,0,index,seed^(quint32(i)*0x9e3779b9U),{},BrushDabPurpose::Painting,i,seed);
            applyBrushDab(output,accumulation,original,dab,p+dab.offset,{},b.blendMode,false,bounds,16,seed);
        }
        ++index;last=p;
    }
    QPainter p(&result);paintLayers(p,{output},true);return result;
}
} // namespace toon
