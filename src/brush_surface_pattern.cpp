#include "brush_surface_pattern.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QCache>
#include <QDataStream>
#include <QIODevice>
#include <QLineF>
#include <QPolygonF>
#include <QSet>
#include <algorithm>
#include <cmath>
#include <numbers>

namespace toon {
namespace {
constexpr qreal pi = std::numbers::pi_v<qreal>;
bool fail(QString *error, const QString &message) { if (error) *error = message; return false; }
bool within(qreal n, qreal lo, qreal hi) { return std::isfinite(n) && n >= lo && n <= hi; }
qreal cross(QPointF a, QPointF b) { return a.x() * b.y() - a.y() * b.x(); }
QPolygonF polygon(const std::array<QPointF, 4> &points) {
    QPolygonF result; for (auto point : points) result.append(point); return result;
}
bool planeTransform(const BrushSurfacePattern &p, QTransform &inverse) {
    const auto target = polygon(p.quad);
    const QPolygonF source{{0,0}, {p.planeSize.width(),0},
                          {p.planeSize.width(),p.planeSize.height()}, {0,p.planeSize.height()}};
    if (!QTransform::quadToQuad(target, source, inverse)) return false;
    qreal smallest = 1e100, largest = 0; int sign = 0;
    for (auto point : p.quad) {
        const qreal denominator = inverse.m13()*point.x()+inverse.m23()*point.y()+inverse.m33();
        if (!std::isfinite(denominator) || std::abs(denominator) < 1e-8) return false;
        const int next = denominator > 0 ? 1 : -1;
        if (sign && next != sign) return false;
        sign = next; smallest = std::min(smallest, std::abs(denominator));
        largest = std::max(largest, std::abs(denominator));
    }
    return largest / smallest <= 1000;
}
qreal dotArea(qreal r) {
    if (r <= .5) return pi*r*r;
    return pi*r*r - 4*(r*r*std::acos(.5/r)-.5*std::sqrt(r*r-.25));
}
qreal radiusForDensity(qreal density) {
    if(density<=0)return 0;if(density>=1)return std::sqrt(.5);
    qreal low=0,high=std::sqrt(.5);
    for(int i=0;i<28;++i){const qreal mid=(low+high)/2;if(dotArea(mid)<density)low=mid;else high=mid;}
    return (low+high)/2;
}
qreal fraction(qreal v) { return v - std::floor(v); }
qreal stripeCoverage(qreal center, qreal footprint, qreal width) {
    if(footprint<1e-10)return std::abs(fraction(center+.5)-.5)<=width*.5?1:0;
    auto integral=[width](qreal point){const qreal cell=std::floor(point);return cell*width+std::clamp(point-cell,0.,width);};
    return std::clamp((integral(center+footprint*.5+width*.5)-integral(center-footprint*.5+width*.5))/footprint,0.,1.);
}
int wrap(int n, int maximum) { n %= maximum; return n < 0 ? n+maximum : n; }
QRgb scalePixel(QRgb pixel, int coverage) {
    return qRgba((qRed(pixel)*coverage+127)/255, (qGreen(pixel)*coverage+127)/255,
                 (qBlue(pixel)*coverage+127)/255, (qAlpha(pixel)*coverage+127)/255);
}
}

bool validateBrushSurface(const BrushSurfacePattern &p, QString *error) {
    if (int(p.kind)<0 || int(p.kind)>int(BrushSurfaceKind::FadeDots)
        || !within(p.densityEnd,0,1) || !within(p.fadeLength,1,8192)
        || !within(p.frequency,.1,50) || !within(p.density,0,1) || !within(p.angle,-360,360)
        || !within(p.phase.x(),-32768,32768) || !within(p.phase.y(),-32768,32768)
        || !within(p.tileSize.width(),1,8192) || !within(p.tileSize.height(),1,8192)
        || !within(p.planeSize.width(),8,8192) || !within(p.planeSize.height(),8,8192))
        return fail(error,"Surface pattern settings exceed their supported range.");
    if (!p.image.isNull() && (p.image.format()!=QImage::Format_ARGB32_Premultiplied
        || p.image.width()>2048 || p.image.height()>2048))
        return fail(error,"Surface tiles must be premultiplied color images no larger than 2048 × 2048.");
    if(!p.image.isNull()){
        static thread_local QCache<qint64,int> validity(256);
        auto *valid=validity.object(p.image.cacheKey());
        if(!valid){bool ok=true;for(int y=0;y<p.image.height()&&ok;++y){const auto *row=reinterpret_cast<const QRgb *>(p.image.constScanLine(y));
            for(int x=0;x<p.image.width();++x)if(qRed(row[x])>qAlpha(row[x])||qGreen(row[x])>qAlpha(row[x])||qBlue(row[x])>qAlpha(row[x])){ok=false;break;}}
            valid=new int(ok);validity.insert(p.image.cacheKey(),valid);
        }
        if(!*valid)return fail(error,"Surface tile contains invalid premultiplied colors.");
    }
    for (auto point:p.quad)
        if (!within(point.x(),-32768,32768) || !within(point.y(),-32768,32768))
            return fail(error,"Surface plane corners exceed the artwork range.");
    qreal area=0;
    for (int i=0;i<4;++i) {
        const auto a=p.quad[i], b=p.quad[(i+1)%4], c=p.quad[(i+2)%4];
        if (QLineF(a,b).length()<8 || cross(b-a,c-b)<=1)
            return fail(error,"Surface plane corners must form a convex clockwise screen-space quadrilateral with edges at least 8 pixels long.");
        area += cross(a,b);
    }
    QTransform inverse;
    if (area<128 || !planeTransform(p,inverse))
        return fail(error,"The perspective plane is too narrow or approaches a vanishing point.");
    return true;
}

QJsonObject brushSurfaceToJson(const BrushSurfacePattern &p) {
    QJsonArray quad; for (auto point:p.quad) quad.append(QJsonArray{point.x(),point.y()});
    return {{"kind",int(p.kind)}, {"frequency",p.frequency}, {"density",p.density},
            {"angle",p.angle}, {"densityEnd",p.densityEnd}, {"fadeLength",p.fadeLength}, {"seed",qint64(p.seed)}, {"phase",QJsonArray{p.phase.x(),p.phase.y()}},
            {"tileSize",QJsonArray{p.tileSize.width(),p.tileSize.height()}},
            {"perspective",p.perspective}, {"quad",quad},
            {"planeSize",QJsonArray{p.planeSize.width(),p.planeSize.height()}}};
}
bool brushSurfaceFromJson(const QJsonObject &json, BrushSurfacePattern &pattern, QString *error) {
    const QSet<QString> keys{"kind","frequency","density","angle","phase","tileSize","perspective","quad","planeSize","densityEnd","fadeLength","seed"};
    if (json.size()!=keys.size()) return fail(error,"Surface pattern metadata is incomplete.");
    for(auto it=json.begin();it!=json.end();++it)
        if(!keys.contains(it.key()))return fail(error,"Unknown surface pattern setting.");
    BrushSurfacePattern next = pattern;
    auto numeric=[](QJsonValue v, qreal &out){if(!v.isDouble()||!std::isfinite(v.toDouble()))return false;out=v.toDouble();return true;};
    auto pair=[&](QJsonValue v,QPointF &out){if(!v.isArray())return false;const auto a=v.toArray();qreal x,y;
        if(a.size()!=2||!numeric(a[0],x)||!numeric(a[1],y))return false;out={x,y};return true;};
    qreal kind,seed;QPointF tile,plane;
    if(!numeric(json["kind"],kind)||kind!=std::floor(kind)||kind<0||kind>7
        ||!numeric(json["densityEnd"],next.densityEnd)||!numeric(json["fadeLength"],next.fadeLength)
        ||!numeric(json["seed"],seed)||seed<0||seed>4294967295.||seed!=std::floor(seed)
        ||!numeric(json["frequency"],next.frequency)||!numeric(json["density"],next.density)
        ||!numeric(json["angle"],next.angle)||!pair(json["phase"],next.phase)
        ||!pair(json["tileSize"],tile)||!pair(json["planeSize"],plane)
        ||!json["perspective"].isBool()||!json["quad"].isArray()||json["quad"].toArray().size()!=4)
        return fail(error,"Surface pattern metadata contains invalid values.");
    const auto quad=json["quad"].toArray();
    for(int i=0;i<4;++i)if(!pair(quad[i],next.quad[i]))return fail(error,"Invalid surface plane corner.");
    next.kind=BrushSurfaceKind(int(kind));next.seed=quint32(seed);next.tileSize={tile.x(),tile.y()};next.planeSize={plane.x(),plane.y()};
    next.perspective=json["perspective"].toBool();
    if(!validateBrushSurface(next,error))return false;
    pattern=std::move(next);return true;
}

BrushSurfaceSampler::BrushSurfaceSampler(const BrushSurfacePattern &pattern,QColor foreground)
    : m_pattern(pattern),m_foreground(foreground) {
    m_valid=validateBrushSurface(pattern);
    if(!m_valid)return;
    m_cacheIdentity=QJsonDocument(brushSurfaceToJson(pattern)).toJson(QJsonDocument::Compact);
    QDataStream identity(&m_cacheIdentity,QIODevice::Append);identity<<pattern.image.cacheKey()<<quint32(foreground.rgba());
    if(pattern.perspective&&!planeTransform(pattern,m_inverse)){m_valid=false;return;}
    if(pattern.kind==BrushSurfaceKind::Image&&!pattern.image.isNull()){
        // The source is already owned by the brush. Cache only newly allocated
        // smaller levels, so a 2048-pixel source still fits the 8 MiB budget.
        static thread_local QCache<qint64,QVector<QImage>> levels(8*1024*1024);
        auto *cached=levels.object(pattern.image.cacheKey());
        if(!cached){cached=new QVector<QImage>;auto previous=pattern.image;int bytes=0;
            while(previous.width()>1||previous.height()>1){previous=previous.scaled(std::max(1,previous.width()/2),std::max(1,previous.height()/2),Qt::IgnoreAspectRatio,Qt::SmoothTransformation);bytes+=int(previous.sizeInBytes());cached->append(previous);}
            levels.insert(pattern.image.cacheKey(),cached,std::max(1,bytes));
        }
        m_imageLevels.append(pattern.image);m_imageLevels.append(*cached);
    }
    const qreal angle=pattern.angle*pi/180.;m_cos=std::cos(angle);m_sin=std::sin(angle);
    m_radius=radiusForDensity(pattern.density);
    if(pattern.kind==BrushSurfaceKind::FadeDots)for(int i=0;i<256;++i)m_fadeRadii[i]=radiusForDensity(i/255.);
    m_lineWidth=pattern.kind==BrushSurfaceKind::Crosshatch ? 1-std::sqrt(1-pattern.density) : pattern.density;
}
bool BrushSurfaceSampler::map(QPointF point,QPointF &mapped) const {
    if(!m_valid||!std::isfinite(point.x())||!std::isfinite(point.y()))return false;
    if(m_pattern.perspective){
        for(int i=0;i<4;++i)
            if(cross(m_pattern.quad[(i+1)%4]-m_pattern.quad[i],point-m_pattern.quad[i]) < -1e-7)return false;
        const qreal w=m_inverse.m13()*point.x()+m_inverse.m23()*point.y()+m_inverse.m33();
        if(!std::isfinite(w)||std::abs(w)<1e-8)return false;
        point={(m_inverse.m11()*point.x()+m_inverse.m21()*point.y()+m_inverse.m31())/w,
               (m_inverse.m12()*point.x()+m_inverse.m22()*point.y()+m_inverse.m32())/w};
    }
    point-=m_pattern.phase;
    mapped={m_cos*point.x()+m_sin*point.y(),-m_sin*point.x()+m_cos*point.y()};
    return within(mapped.x(),-1e8,1e8)&&within(mapped.y(),-1e8,1e8);
}
qreal BrushSurfaceSampler::tone(QPointF point) const {
    qreal density=m_pattern.density;
    if(m_pattern.kind==BrushSurfaceKind::FadeDots)
        density += (m_pattern.densityEnd-density)*std::clamp(point.y()/m_pattern.fadeLength,0.,1.);
    if(density<=0)return 0;if(density>=1)return 1;
    const qreal unit=m_pattern.frequency/100;
    const qreal x=std::abs(fraction(point.x()*unit+.5)-.5),y=std::abs(fraction(point.y()*unit+.5)-.5);
    if(m_pattern.kind==BrushSurfaceKind::Dots)return x*x+y*y<=m_radius*m_radius?1:0;
    if(m_pattern.kind==BrushSurfaceKind::FadeDots){const qreal d=density*255;const int i=std::min(254,int(d));
        const qreal radius=m_fadeRadii[i]+(m_fadeRadii[i+1]-m_fadeRadii[i])*(d-i);
        return x*x+y*y<=radius*radius?1:0;}
    if(m_pattern.kind==BrushSurfaceKind::Diamond){const qreal radius=density<=.5?std::sqrt(density/2):1-std::sqrt((1-density)/2);return x+y<=radius?1:0;}
    if(m_pattern.kind==BrushSurfaceKind::Noise){
        quint64 n=quint64(qint64(std::floor(point.x()*unit)))*0x9e3779b97f4a7c15ULL
            ^quint64(qint64(std::floor(point.y()*unit)))*0xbf58476d1ce4e5b9ULL^m_pattern.seed;
        n^=n>>30;n*=0xbf58476d1ce4e5b9ULL;n^=n>>27;n*=0x94d049bb133111ebULL;n^=n>>31;
        return (n>>11)*(1./9007199254740992.)<density?1:0;
    }
    if(m_pattern.kind==BrushSurfaceKind::Crosshatch)return x<=m_lineWidth*.5||y<=m_lineWidth*.5?1:0;
    return y<=m_lineWidth*.5?1:0;
}
QRgb BrushSurfaceSampler::image(QPointF point,int level) const {
    if(m_imageLevels.isEmpty())return 0;const auto &tile=m_imageLevels[std::clamp(level,0,int(m_imageLevels.size())-1)];
    const qreal x=fraction(point.x()/m_pattern.tileSize.width())*tile.width()-.5;
    const qreal y=fraction(point.y()/m_pattern.tileSize.height())*tile.height()-.5;
    const int ix=int(std::floor(x)),iy=int(std::floor(y));const qreal fx=x-ix,fy=y-iy;
    const QRgb pixels[]{tile.pixel(wrap(ix,tile.width()),wrap(iy,tile.height())),
        tile.pixel(wrap(ix+1,tile.width()),wrap(iy,tile.height())),
        tile.pixel(wrap(ix,tile.width()),wrap(iy+1,tile.height())),
        tile.pixel(wrap(ix+1,tile.width()),wrap(iy+1,tile.height()))};
    const qreal weights[]{(1-fx)*(1-fy),fx*(1-fy),(1-fx)*fy,fx*fy};
    qreal r=0,g=0,b=0,a=0;for(int i=0;i<4;++i){r+=qRed(pixels[i])*weights[i];g+=qGreen(pixels[i])*weights[i];b+=qBlue(pixels[i])*weights[i];a+=qAlpha(pixels[i])*weights[i];}
    return qRgba(qRound(r),qRound(g),qRound(b),qRound(a));
}
QRgb BrushSurfaceSampler::sample(QPointF center) const {
    if(!m_valid)return 0;
    if(m_pattern.kind==BrushSurfaceKind::Off)return qPremultiply(m_foreground.rgba());
    QPointF mapped;
    if(!map(center,mapped))return 0;
    // A pixel-area estimate only near pattern edges. Solid interiors need one
    // sample; the homography Jacobian bounds work without re-solving a plane
    // for each subpixel. Tiny periods converge to average density.
    QPointF dx(m_cos,-m_sin),dy(m_sin,m_cos);
    if(m_pattern.perspective){
        const qreal w=m_inverse.m13()*center.x()+m_inverse.m23()*center.y()+m_inverse.m33();
        const auto source=m_inverse.map(center);
        const QPointF sx((m_inverse.m11()-source.x()*m_inverse.m13())/w,(m_inverse.m12()-source.y()*m_inverse.m13())/w);
        const QPointF sy((m_inverse.m21()-source.x()*m_inverse.m23())/w,(m_inverse.m22()-source.y()*m_inverse.m23())/w);
        dx={m_cos*sx.x()+m_sin*sx.y(),-m_sin*sx.x()+m_cos*sx.y()};
        dy={m_cos*sy.x()+m_sin*sy.y(),-m_sin*sy.x()+m_cos*sy.y()};
    }
    if(m_pattern.kind==BrushSurfaceKind::Image){
        const qreal sx=m_pattern.image.width()/m_pattern.tileSize.width(),sy=m_pattern.image.height()/m_pattern.tileSize.height();
        const qreal footprint=std::max(std::hypot(dx.x()*sx,dx.y()*sy),std::hypot(dy.x()*sx,dy.y()*sy));
        const qreal level=footprint>1?std::log2(footprint):0;const int low=int(std::floor(level));const qreal mix=level-low;
        const auto a=image(mapped,low);if(mix<1e-8)return a;const auto b=image(mapped,low+1);
        return qRgba(qRound(qRed(a)+(qRed(b)-qRed(a))*mix),qRound(qGreen(a)+(qGreen(b)-qGreen(a))*mix),
                     qRound(qBlue(a)+(qBlue(b)-qBlue(a))*mix),qRound(qAlpha(a)+(qAlpha(b)-qAlpha(a))*mix));
    }
    const qreal unit=m_pattern.frequency/100;
    const qreal nx=std::hypot(dx.x(),dx.y()),ny=std::hypot(dy.x(),dy.y());
    const qreal cycles=std::max(nx,ny)*unit;
    qreal density=m_pattern.density;
    if(m_pattern.kind==BrushSurfaceKind::FadeDots)density+=(m_pattern.densityEnd-density)*std::clamp(mapped.y()/m_pattern.fadeLength,0.,1.);
    qreal alpha=0;
    if(cycles>=1)alpha=density;
    else if(density<=0)alpha=0;
    else if(density>=1)alpha=1;
    else if(m_pattern.kind==BrushSurfaceKind::Lines||m_pattern.kind==BrushSurfaceKind::Crosshatch){
        const qreal stripeY=stripeCoverage(mapped.y()*unit,(std::abs(dx.y())+std::abs(dy.y()))*unit,m_lineWidth);
        if(m_pattern.kind==BrushSurfaceKind::Lines)alpha=stripeY;
        else {const qreal stripeX=stripeCoverage(mapped.x()*unit,(std::abs(dx.x())+std::abs(dy.x()))*unit,m_lineWidth);alpha=1-(1-stripeX)*(1-stripeY);}
    }
    else {
        const qreal x=std::abs(fraction(mapped.x()*unit+.5)-.5),y=std::abs(fraction(mapped.y()*unit+.5)-.5);
        const qreal spanX=(std::abs(dx.x())+std::abs(dy.x()))*.5*unit;
        const qreal spanY=(std::abs(dx.y())+std::abs(dy.y()))*.5*unit;
        bool interior=false;
        switch(m_pattern.kind){
        case BrushSurfaceKind::Dots:interior=std::abs(std::hypot(x,y)-m_radius)>(nx+ny)*.5*unit;break;
        case BrushSurfaceKind::Lines:interior=std::abs(y-m_lineWidth*.5)>spanY;break;
        case BrushSurfaceKind::Crosshatch:interior=(x<m_lineWidth*.5-spanX)||(y<m_lineWidth*.5-spanY)
            ||(x>m_lineWidth*.5+spanX&&y>m_lineWidth*.5+spanY);break;
        case BrushSurfaceKind::Diamond:{const qreal radius=density<=.5?std::sqrt(density/2):1-std::sqrt((1-density)/2);interior=std::abs(x+y-radius)>spanX+spanY;break;}
        case BrushSurfaceKind::Noise:{const qreal fx=fraction(mapped.x()*unit),fy=fraction(mapped.y()*unit);interior=std::min(fx,1-fx)>spanX&&std::min(fy,1-fy)>spanY;break;}
        default:break;
        }
        if(interior)alpha=tone(mapped);
        else {
            for(int y=0;y<4;++y)for(int x=0;x<4;++x)
                alpha+=tone(mapped+dx*((x+.5)/4-.5)+dy*((y+.5)/4-.5));
            alpha/=16;
        }
    }
    return qPremultiply(qRgba(m_foreground.red(),m_foreground.green(),m_foreground.blue(),qRound(alpha*255)));
}
qreal BrushSurfaceSampler::coverage(QPointF point) const {return qAlpha(sample(point))/255.;}
void applyBrushSurface(QImage &image,QPoint origin,const BrushSurfaceSampler &surface,bool sampledAlreadyColored,QImage *coverage) {
    if(!surface.enabled())return;
    if(image.format()!=QImage::Format_ARGB32_Premultiplied)return;
    if(coverage){*coverage=QImage(image.size(),QImage::Format_Grayscale8);coverage->fill(0);}
    // Surface pixels depend only on immutable settings and artwork coordinates.
    // Cache small world tiles across overlapping dabs, with a strict 16 MiB cap.
    static thread_local QCache<QByteArray,QImage> cache(16*1024*1024);
    constexpr int side=64;
    const int left=int(std::floor(qreal(origin.x())/side)),right=int(std::floor(qreal(origin.x()+image.width()-1)/side));
    const int top=int(std::floor(qreal(origin.y())/side)),bottom=int(std::floor(qreal(origin.y()+image.height()-1)/side));
    for(int ty=top;ty<=bottom;++ty)for(int tx=left;tx<=right;++tx){
        const QRect part=QRect(tx*side-origin.x(),ty*side-origin.y(),side,side).intersected(image.rect());
        bool occupied=false;
        for(int y=part.top();y<=part.bottom()&&!occupied;++y){const auto *row=reinterpret_cast<const QRgb *>(image.constScanLine(y));for(int x=part.left();x<=part.right();++x)if(qAlpha(row[x])){occupied=true;break;}}
        if(!occupied)continue;
        QByteArray key=surface.cacheIdentity();QDataStream stream(&key,QIODevice::Append);stream<<qint32(tx)<<qint32(ty);
        auto *tile=cache.object(key);
        if(!tile){tile=new QImage(side,side,QImage::Format_ARGB32_Premultiplied);
            for(int y=0;y<side;++y){auto *row=reinterpret_cast<QRgb *>(tile->scanLine(y));for(int x=0;x<side;++x)row[x]=surface.sample(QPointF(tx*side+x+.5,ty*side+y+.5));}
            cache.insert(key,tile,side*side*4);
        }
        for(int y=part.top();y<=part.bottom();++y){auto *row=reinterpret_cast<QRgb *>(image.scanLine(y));auto *mask=coverage?coverage->scanLine(y):nullptr;
            const auto *pattern=reinterpret_cast<const QRgb *>(tile->constScanLine(origin.y()+y-ty*side));
            for(int x=part.left();x<=part.right();++x){const int alpha=qAlpha(row[x]);if(!alpha)continue;
                const auto s=pattern[origin.x()+x-tx*side];if(mask)mask[x]=qAlpha(s);
                if(surface.colored())row[x]=scalePixel(s,alpha);
                else if(sampledAlreadyColored)row[x]=scalePixel(row[x],qAlpha(s));
                else {const int a=(alpha*qAlpha(s)+127)/255;row[x]=qRgba(a,a,a,a);}
            }
        }
    }
}
} // namespace toon
