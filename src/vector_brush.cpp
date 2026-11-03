#include "vector_brush.h"
#include "brush_engine.h"
#include "connected_brush.h"
#include "brush_surface_pattern.h"
#include "raster_edit.h"
#include <QCache>
#include <QCryptographicHash>
#include <QDataStream>
#include <QIODevice>
#include <QLineF>
#include <QPainter>
#include <QPainterPathStroker>
#include <algorithm>
#include <cmath>

namespace toon {
namespace {
constexpr qreal pi = 3.14159265358979323846;
struct CenterPoint { QPointF point; qreal length = 0; qreal width = 1; };
class Centerline {
public:
    QVector<CenterPoint> points;
    explicit Centerline(const EditableObject &o) {
        if (o.nodes.isEmpty()) return;
        points.reserve(o.nodes.size()*2);
        const auto width = [&o](int i) { return o.widthFactors.size() == o.nodes.size() ? o.widthFactors[i] : 1.0; };
        points.append({o.nodes.first().anchor,0,width(0)});
        const int count = o.nodes.size() - 1 + (o.closed ? 1 : 0);
        for (int segment = 0; segment < count; ++segment) {
            const int a = segment % o.nodes.size(), b = (segment + 1) % o.nodes.size();
            if(o.nodes[a].outControl==o.nodes[a].anchor&&o.nodes[b].inControl==o.nodes[b].anchor){
                points.append({o.nodes[b].anchor,points.last().length+QLineF(o.nodes[a].anchor,o.nodes[b].anchor).length(),width(b)});
                continue;
            }
            QPainterPath path(o.nodes[a].anchor);
            path.cubicTo(o.nodes[a].outControl,o.nodes[b].inControl,o.nodes[b].anchor);
            const auto polygons = path.toSubpathPolygons();
            QVector<QPointF> flattened;
            for (const auto &polygon : polygons) for (const auto &point : polygon) flattened.append(point);
            if (flattened.isEmpty()) flattened = {o.nodes[a].anchor,o.nodes[b].anchor};
            qreal segmentLength = 0;
            for (int i=1; i<flattened.size(); ++i) segmentLength += QLineF(flattened[i-1],flattened[i]).length();
            qreal along = 0;
            for (int i=1; i<flattened.size(); ++i) {
                const qreal distance = QLineF(flattened[i-1],flattened[i]).length(); along += distance;
                const qreal fraction = segmentLength > 1e-9 ? along / segmentLength : 1;
                points.append({flattened[i],points.last().length+distance,width(a)+(width(b)-width(a))*fraction});
            }
        }
        if (points.size() == 1) points.append(points.first());
    }
    CenterPoint at(qreal position, qreal *direction = nullptr) const {
        if (points.isEmpty()) return {};
        const qreal length = qBound(0.0,position,1.0)*points.last().length;
        auto right = std::lower_bound(points.cbegin(),points.cend(),length,
                                      [](const CenterPoint &p,qreal target) { return p.length < target; });
        int i = qBound(1,int(right-points.cbegin()),int(points.size())-1);
        const auto &a=points[i-1], &b=points[i];
        const qreal fraction = b.length>a.length ? (length-a.length)/(b.length-a.length) : 0;
        const auto delta = b.point-a.point;
        if (direction) *direction=std::atan2(delta.y(),delta.x())*180/pi;
        return {a.point+delta*fraction,length,a.width+(b.width-a.width)*fraction};
    }
};
QTransform stampTransform(const QTransform &objectTransform, QPointF center, qreal scale) {
    // QTransform uses row-vector composition. Keep translation in the stamp's
    // center and only the local deformation in the rasterizer's tip transform.
    if (objectTransform.isIdentity()) return scale == 1 ? QTransform() : QTransform::fromScale(scale,scale);
    const auto mapped=objectTransform.map(center);
    return QTransform::fromScale(scale,scale) * QTransform::fromTranslate(center.x(),center.y())
        * objectTransform * QTransform::fromTranslate(-mapped.x(),-mapped.y());
}
QImage hitAlphaImage(const QImage &mask) {
    QImage image(mask.size(),QImage::Format_ARGB32_Premultiplied);
    bool painted=false;
    for(int y=0;y<mask.height();++y){
        const auto *source=mask.constScanLine(y); auto *target=reinterpret_cast<QRgb *>(image.scanLine(y));
        for(int x=0;x<mask.width();++x){const int alpha=source[x];painted|=alpha!=0;target[x]=qRgba(alpha,alpha,alpha,alpha);}
    }
    return painted?image:QImage();
}
// Work in destination pixels, just as applyBrushDab does. Inverse sampling a
// source mask alone misses thin transformed marks and samples canvas textures
// in the wrong place. Ordinary stamps keep the renderer's exact raster origin;
// very large transformed stamps are clipped to a bounded query tile instead.
class BrushAlphaQuery {
public:
    BrushAlphaQuery(const QPainterPath &region,const EditableObject &object,QRectF tile,bool firstOnly,QCache<quint64,QImage> &stamps)
        : m_region(region),m_bounds(region.boundingRect().intersected(tile)),m_firstOnly(firstOnly),m_stamps(stamps) {
        const auto &stroke=*object.brushStroke;
        m_seed=stroke.seed;m_blend=stroke.settings.blendMode;
        if(stroke.selectionActive){
            const auto selected=object.transform.map(stroke.selection);
            m_selection.reset(&selected,stroke.selectionFeather);
            const qreal feather=qBound(0,qRound(stroke.selectionFeather),100);
            m_bounds=m_bounds.intersected(selected.boundingRect().adjusted(-feather-1,-feather-1,feather+1,feather+1));
        }
        if(stroke.settings.texture.enabled&&!stroke.settings.texture.eachTip)
            m_pattern=stroke.settings.texture.pattern.convertToFormat(QImage::Format_Grayscale8);
        if (stroke.settings.surface.kind != BrushSurfaceKind::Off)
            m_surface.emplace(stroke.settings.surface, object.stroke);
    }
    QRectF bounds() const { return m_bounds; }
    bool intersects(const BrushDab &dab,const QImage &alpha,QPointF center,const QTransform &transform,quint64 stampKey) {
        if(alpha.isNull())return false;
        const QRectF stamp(-alpha.width()/2.0,-alpha.height()/2.0,alpha.width(),alpha.height());
        const QRectF footprint=transform.mapRect(stamp).translated(center).adjusted(-1,-1,1,1);
        const auto overlap=footprint.intersected(m_bounds);
        if(overlap.isEmpty()||!std::isfinite(overlap.x())||!std::isfinite(overlap.y())
            ||!std::isfinite(overlap.width())||!std::isfinite(overlap.height()))return false;
        const QRect area=overlap.toAlignedRect();
        const int left=int(std::floor(qreal(area.left())/tileSize)),right=int(std::floor(qreal(area.right())/tileSize));
        const int top=int(std::floor(qreal(area.top())/tileSize)),bottom=int(std::floor(qreal(area.bottom())/tileSize));
        bool hit=false;
        for(int ty=top;ty<=bottom;++ty)for(int tx=left;tx<=right;++tx){
            const TileKey key{tx,ty};const QPoint origin(tx*tileSize,ty*tileSize);
            const QRect tile(origin,QSize(tileSize,tileSize)),part=area.intersected(tile);
            const auto selected=m_selection.tile(key);
            if(selected.coverage==RasterSelectionTile::Coverage::Empty||selected.coverage==RasterSelectionTile::Coverage::Invalid)continue;
            auto *query=queryMask(key);
            if(query->image.isNull())continue;
            QRect sampledArea=part;
            const QPointF anchor(std::floor(center.x()),std::floor(center.y())),phase=center-anchor;
            const QRectF originalFootprint=transform.mapRect(stamp).translated(phase);
            const qreal width=std::ceil(originalFootprint.width())+4,height=std::ceil(originalFootprint.height())+4;
            const bool exact=width>0&&height>0&&width<=32768&&height<=32768&&width*height<=4*1024*1024;
            if(exact)
                sampledArea=originalFootprint.toAlignedRect().adjusted(-1,-1,1,1).translated(anchor.toPoint());
            QImage sampled;
            if(exact)if(const auto *cached=m_stamps.object(stampKey))sampled=*cached;
            if(sampled.isNull()){
                sampled=QImage(sampledArea.size(),QImage::Format_ARGB32_Premultiplied);sampled.fill(Qt::transparent);
                QPainter painter(&sampled);painter.setRenderHint(QPainter::SmoothPixmapTransform);
                painter.translate(center-sampledArea.topLeft());painter.setTransform(transform,true);
                painter.drawImage(stamp.topLeft(),alpha);painter.end();
                if(exact)m_stamps.insert(stampKey,new QImage(sampled),int(sampled.sizeInBytes()));
            }
            for(int y=part.top();y<=part.bottom();++y){
                const auto *pixels=reinterpret_cast<const QRgb *>(sampled.constScanLine(y-sampledArea.top()));
                const auto *queryRow=query->image.constScanLine(y-query->origin.y());
                const auto *surfaceRow=query->surface.isNull()?nullptr:query->surface.constScanLine(y-query->origin.y());
                auto *accumulated=reinterpret_cast<quint16 *>(query->accumulated.scanLine(y-query->origin.y()));
                const auto *selectedRow=selected.mask.isNull()?nullptr:selected.mask.constScanLine(y-origin.y());
                for(int x=part.left();x<=part.right();++x){
                    const int at=x-query->origin.x();
                    if(!queryRow[at]||(selectedRow&&!selectedRow[x-origin.x()]))continue;
                    qreal coverage=qAlpha(pixels[x-sampledArea.left()])/255.0;
                    if (!dab.colorTip.isNull() && coverage <= 0) continue;
                    if(!dab.untexturedMask.isNull())coverage=brushCanvasTextureCoverage(coverage,dab.canvasTexture,
                        m_pattern,dab.canvasTextureDepth,QPointF(x+.5,y+.5));
                    // The paint engine quantizes texture, keeps per-stroke
                    // accumulation in 16 bits, then composites an 8-bit pixel.
                    const qreal surfaceAlpha=surfaceRow?surfaceRow[at]/255.:1.;
                    if (surfaceAlpha<=0) continue;
                    const int masked=qRound(qRound(coverage*255)*surfaceAlpha);
                    const qreal flow=qMin(1.,masked/255./qMax(surfaceAlpha,1./255.))*qBound(0.,dab.flow,1.);
                    if(flow<=0)continue;
                    const qreal previous=accumulated[at]/65535.0;
                    const qreal cap=qBound(0.,dab.opacity,1.)*(selectedRow?selectedRow[x-origin.x()]/255.0:1.)*surfaceAlpha;
                    const qreal added=cap*flow,retained=previous>cap?1-added/previous:1-flow;
                    accumulated[at]=quint16(qBound(0,qRound((previous*retained+added)*65535),65535));
                    const int outputAlpha=qAlpha(QRgba64::fromRgba64(0,0,0,accumulated[at]).toArgb32());
                    if(!outputAlpha)continue;
                    if(m_blend==BrushBlend::Dissolve&&brushRandom(m_seed,(quint64(quint32(x))<<32)|quint32(y),200)>=outputAlpha/255.0)continue;
                    hit=true;if(m_firstOnly)return true;
                }
            }
        }
        return hit;
    }
private:
    struct QueryMask { QImage image,accumulated,surface; QPoint origin; };
    QueryMask *queryMask(TileKey key) {
        if(auto *cached=m_queries.object(key))return cached;
        const QRect area=QRect(key.first*tileSize,key.second*tileSize,tileSize,tileSize).intersected(m_bounds.toAlignedRect());
        QImage mask(area.size(),QImage::Format_Grayscale8);mask.fill(0);
        {
            QPainter painter(&mask);painter.setRenderHint(QPainter::Antialiasing);
            painter.translate(-area.topLeft());
            painter.setPen(Qt::NoPen);painter.setBrush(Qt::white);painter.drawPath(m_region);
        }
        bool any=false;
        for(int y=0;y<mask.height()&&!any;++y)for(int x=0;x<mask.width();++x)if(mask.constScanLine(y)[x]){any=true;break;}
        if(!any)mask={};
        QImage accumulated(any?area.size():QSize(),QImage::Format_Grayscale16);accumulated.fill(0);
        QImage surface;
        if (any && m_surface) {
            surface=QImage(area.size(),QImage::Format_Grayscale8);surface.fill(0);
            for (int y=0;y<area.height();++y) {
                const auto *query=mask.constScanLine(y);auto *row=surface.scanLine(y);
                for (int x=0;x<area.width();++x) if (query[x])
                    row[x]=uchar(qRound(m_surface->coverage(QPointF(area.left()+x+.5,area.top()+y+.5))*255));
            }
        }
        auto *result=new QueryMask{mask,accumulated,surface,area.topLeft()};
        m_queries.insert(key,result,any?int(mask.sizeInBytes()+accumulated.sizeInBytes()+surface.sizeInBytes()):1);
        return result;
    }
    QPainterPath m_region;
    QRectF m_bounds;
    QImage m_pattern;
    std::optional<BrushSurfaceSampler> m_surface;
    bool m_firstOnly=false;
    quint32 m_seed=1;
    BrushBlend m_blend=BrushBlend::Normal;
    QCache<quint64,QImage> &m_stamps;
    RasterSelectionCache m_selection;
    QCache<TileKey,QueryMask> m_queries{1024*1024};
};
}
QByteArray editableGeometrySignature(const EditableObject &object) {
    QByteArray bytes;
    QDataStream stream(&bytes,QIODevice::WriteOnly); stream.setVersion(QDataStream::Qt_6_0);
    stream << object.closed << quint32(object.nodes.size());
    for (const auto &node : object.nodes) stream << node.anchor << node.inControl << node.outControl;
    return QCryptographicHash::hash(bytes,QCryptographicHash::Sha256);
}
qreal vectorBrushWidthAt(const EditableObject &object,qreal position) {
    if (object.widthFactors.isEmpty()) return 1;
    return Centerline(object).at(position).width;
}
QRectF vectorBrushBounds(const EditableObject &object) {
    if (!object.brushStroke) return {};
    const auto &stroke=*object.brushStroke;
    const bool original=editableGeometrySignature(object)==stroke.sourceGeometry;
    Centerline centerline(original?EditableObject{}:object);
    bool first=true; QRectF area;
    auto include=[&](QPointF point) {
        for (const auto &instance : brushSymmetryInstances(stroke.settings.symmetry,point,stroke.canvasSize)) {
            if (first) { area=QRectF(instance.point,QSizeF()); first=false; }
            else { area.setLeft(qMin(area.left(),instance.point.x())); area.setRight(qMax(area.right(),instance.point.x()));
                   area.setTop(qMin(area.top(),instance.point.y())); area.setBottom(qMax(area.bottom(),instance.point.y())); }
        }
    };
    if (original) for (const auto &sample : stroke.samples) include(sample.point);
    else for (const auto &point : centerline.points) include(point.point);
    if (first) return {};
    qreal width=1; for (qreal factor : object.widthFactors) width=qMax(width,factor);
    const auto &settings = stroke.settings;
    const qreal speed = settings.speedDynamics.enabled ? qMax(1., 1 + settings.speedDynamics.sizeAmount) : 1;
    const qreal scatter = settings.connected.mode != BrushConnectedMode::Off ? 0
        : settings.clusterSpray.enabled ? settings.clusterSpray.radius + settings.clusterSpray.spread : settings.scatter * 1.5;
    const qreal radius=qMax(.5,object.strokeWidth*width)*(speed+scatter)+2;
    return object.transform.mapRect(area.adjusted(-radius,-radius,radius,radius));
}
QVector<qreal> vectorBrushIntersectionPositions(const EditableObject &object,const QPainterPath &artworkRegion,bool firstOnly) {
    QVector<qreal> result;
    if(!object.brushStroke||artworkRegion.isEmpty()||object.opacity<=0||object.strokeWidth<=0
        ||!object.stroke.isValid()||!object.stroke.alpha())return {};
    const auto bounds=vectorBrushBounds(object).intersected(artworkRegion.boundingRect());
    if(bounds.isEmpty())return {};
    const auto &stroke=*object.brushStroke;
    if(stroke.selectionActive&&stroke.selection.isEmpty())return {};
    const bool originalGeometry=editableGeometrySignature(object)==stroke.sourceGeometry;
    const Centerline centerline(originalGeometry&&object.widthFactors.isEmpty()?EditableObject{}:object);
    if (stroke.settings.connected.mode != BrushConnectedMode::Off) {
        ConnectedBrushState connected;
        const auto selection = object.transform.map(stroke.selection);
        RasterSelectionCache selected(stroke.selectionActive ? &selection : nullptr, stroke.selectionFeather);
        const RasterLayer original;
        for (const auto &sample : stroke.samples) {
            const auto mapped = centerline.at(sample.position);
            auto settings = stroke.settings; settings.color = object.stroke;
            settings.size = object.strokeWidth * mapped.width; settings.strokeOpacity *= object.opacity;
            RasterLayer pixels, paint;
            if (!appendConnectedBrush(pixels, paint, original, connected, settings,
                originalGeometry ? sample.point : mapped.point, sample.input, sample.dabIndex, stroke.seed,
                stroke.canvasSize, bounds, maxProjectTiles, false,
                stroke.selectionActive ? &selection : nullptr, &selected, object.transform)) return result;
            bool hit = false;
            for (auto it = pixels.tiles.cbegin(); it != pixels.tiles.cend() && !hit; ++it) {
                const QPoint origin(it.key().first * tileSize, it.key().second * tileSize);
                for (int y = 0; y < tileSize && !hit; ++y) {
                    const auto *row = reinterpret_cast<const QRgb *>(it->constScanLine(y));
                    for (int x = 0; x < tileSize; ++x) if (qAlpha(row[x]) && artworkRegion.contains(QPointF(origin.x() + x + .5, origin.y() + y + .5))) {
                        hit = true; break;
                    }
                }
            }
            if (hit) { result.append(sample.position); if (firstOnly) return result; }
        }
        return result;
    }
    qreal initial=0;centerline.at(0,&initial);
    QVector<bool> hitSamples(stroke.samples.size(),false);
    QCache<quint64,QImage> stamps{16*1024*1024};
    const QRect pixels=bounds.toAlignedRect();
    const int left=int(std::floor(qreal(pixels.left())/tileSize)),right=int(std::floor(qreal(pixels.right())/tileSize));
    const int top=int(std::floor(qreal(pixels.top())/tileSize)),bottom=int(std::floor(qreal(pixels.bottom())/tileSize));
    for(int ty=top;ty<=bottom;++ty)for(int tx=left;tx<=right;++tx){
    const QRectF tile(tx*tileSize,ty*tileSize,tileSize,tileSize);
    if(!artworkRegion.intersects(tile))continue;
    BrushAlphaQuery query(artworkRegion,object,tile,firstOnly,stamps);
    if(query.bounds().isEmpty())continue;
    const QRectF regionBounds=query.bounds();
    for(qsizetype index=0;index<stroke.samples.size();++index){
        const auto &sample=stroke.samples[index];
        qreal direction=sample.direction;
        const auto mapped=centerline.at(sample.position,originalGeometry?nullptr:&direction);
        const auto point=originalGeometry?sample.point:mapped.point;
        auto settings=stroke.settings;settings.color=object.stroke;settings.strokeOpacity*=object.opacity;
        const qreal size=object.strokeWidth*mapped.width;
        if(size<=0)continue;
        settings.size=qBound(1.0,size,usesAdvancedBrushTips(stroke.settings)?maxBrushSize:256.0);
        const qreal scale=size>(usesAdvancedBrushTips(stroke.settings)?maxBrushSize:256.0)?size/(usesAdvancedBrushTips(stroke.settings)?maxBrushSize:256.0):(size<1?size:1);
        const int copies=brushDabCount(settings,sample.input,sample.dabIndex,stroke.seed);
        bool hit=false;
        for(int copy=0;copy<copies&&(!hit||!firstOnly);++copy){
            const quint32 seed=stroke.seed^(quint32(copy)*0x9e3779b9U);
            const auto input=resolvedBrushInput(settings,sample.input);
            const qreal scatter=settings.scatter*settings.size*brushControlValue(settings.scatterControl,input,sample.dabIndex,settings.scatterFade);
            const QPointF localOffset(settings.scatterBothAxes?(brushRandom(seed,sample.dabIndex,30)*2-1)*scatter:0,
                                      (brushRandom(seed,sample.dabIndex,31)*2-1)*scatter);
            const qreal angle=direction*pi/180;
            const QPointF offset=settings.clusterSpray.enabled
                ? brushClusterOffset(settings.clusterSpray,settings.size,direction,sample.dabIndex,stroke.seed,copy)
                : QPointF(localOffset.x()*std::cos(angle)-localOffset.y()*std::sin(angle),
                                 localOffset.x()*std::sin(angle)+localOffset.y()*std::cos(angle));
            std::optional<BrushDab> evaluated;
            QImage alpha;
            int instanceIndex=0;
            for(const auto &instance:brushSymmetryInstances(settings.symmetry,point+offset*scale,stroke.canvasSize)){
                const quint64 stampKey=(quint64(index)<<8)|(quint64(copy)<<4)|quint64(instanceIndex++);
                const auto transform=instance.transform*stampTransform(object.transform,instance.point,scale);
                const auto center=object.transform.map(instance.point);
                const qreal extent=settings.size*(settings.speedDynamics.enabled?2:1)+2;
                const QRectF rough=transform.mapRect(QRectF(-extent,-extent,2*extent,2*extent)).translated(center).adjusted(-1,-1,1,1);
                if(!rough.intersects(regionBounds))continue;
                if(!evaluated){
                    evaluated=evaluateBrushDab(settings,sample.input,point,direction,
                        originalGeometry||index==0?sample.initialDirection:initial,sample.dabIndex,seed,{},BrushDabPurpose::Painting,copy,stroke.seed);
                    const auto &dab=*evaluated;
                    if(dab.flow<=0||dab.opacity<=0||dab.mask.isNull())break;
                    alpha=hitAlphaImage(dab.untexturedMask.isNull()?dab.mask:dab.untexturedMask);
                }
                if(query.intersects(*evaluated,alpha,center,transform,stampKey)){hit=true;if(firstOnly)break;}
            }
        }
        if(hit){hitSamples[index]=true;if(firstOnly)return {sample.position};}
    }
    }
    for(qsizetype index=0;index<stroke.samples.size();++index)if(hitSamples[index])result.append(stroke.samples[index].position);
    return result;
}
bool vectorBrushIntersects(const EditableObject &object,const QPainterPath &artworkRegion) {
    return !vectorBrushIntersectionPositions(object,artworkRegion,true).isEmpty();
}
QPainterPath editableStrokeOutline(const EditableObject &object) {
    QPainterPath result;
    if (object.nodes.isEmpty() || object.strokeWidth<=0) return result;
    const bool variable=std::any_of(object.widthFactors.cbegin(),object.widthFactors.cend(),[](qreal value){return value!=1;});
    if (!variable) {
        auto copy=object; copy.transform={};
        QPainterPathStroker stroker; stroker.setWidth(object.strokeWidth); stroker.setCapStyle(Qt::RoundCap); stroker.setJoinStyle(Qt::RoundJoin);
        return object.transform.map(stroker.createStroke(editableObjectPath(copy)));
    }
    const Centerline centerline(object); result.setFillRule(Qt::WindingFill);
    for (int i=0;i<centerline.points.size();++i) {
        const auto &p=centerline.points[i]; const qreal r=object.strokeWidth*p.width/2;
        if (r>0) result.addEllipse(p.point,r,r);
        if (!i) continue;
        const auto &a=centerline.points[i-1]; const auto delta=p.point-a.point; const qreal distance=std::hypot(delta.x(),delta.y());
        if (distance<1e-9) continue;
        const QPointF normal(-delta.y()/distance,delta.x()/distance); const qreal ar=object.strokeWidth*a.width/2;
        QPolygonF polygon{a.point-normal*ar,p.point-normal*r,p.point+normal*r,a.point+normal*ar};
        result.addPolygon(polygon); result.closeSubpath();
    }
    return object.transform.map(result);
}
namespace {
struct StrokePaint {
    EditableObject source;
    QRectF bounds;
    QMap<TileKey,QImage> tiles;
};
// The cache contains paint before blending with the layer, never a snapshot of
// the backdrop. Earlier-stroke edits therefore reuse later strokes exactly.
// Export workers replay the same immutable sources independently of the editor.
thread_local QCache<QString,StrokePaint> strokePaintCache(64*1024*1024);
constexpr int maxCachedStrokeTiles=32;
qint64 strokeSourceBytes(const EditableObject &object) {
    const auto &stroke=*object.brushStroke;
    return 4096+sizeof(EditableObject)+sizeof(VectorBrushStroke)
        +object.nodes.size()*sizeof(BezierNode)+object.widthFactors.size()*sizeof(qreal)
        +(object.text.size()+object.fontFamily.size()+stroke.settings.texture.name.size()
            +stroke.settings.dual.name.size())*sizeof(QChar)
        +stroke.samples.size()*sizeof(VectorBrushSample)+stroke.sourceGeometry.size()
        +stroke.selection.elementCount()*sizeof(QPainterPath::Element)
        +stroke.settings.tipMask.sizeInBytes()+stroke.settings.colorTip.sizeInBytes()+stroke.settings.texture.pattern.sizeInBytes()
        +stroke.settings.dual.tipMask.sizeInBytes()+stroke.settings.tipFrames.size()*sizeof(BrushTipFrame);
}
QMap<TileKey,QImage> completedStrokePaint(const RasterLayer &accumulation,bool *ok) {
    QMap<TileKey,QImage> paint;*ok=false;
    for(auto it=accumulation.tiles.cbegin();it!=accumulation.tiles.cend();++it){
        if(it->size()!=QSize(tileSize,tileSize)||it->format()!=QImage::Format_RGBA64_Premultiplied)return {};
        QImage tile(tileSize,tileSize,QImage::Format_ARGB32_Premultiplied);
        if(tile.isNull())return {};
        for(int y=0;y<tileSize;++y){
            const auto *source=reinterpret_cast<const QRgba64 *>(it->constScanLine(y));
            auto *destination=reinterpret_cast<QRgb *>(tile.scanLine(y));
            for(int x=0;x<tileSize;++x)destination[x]=source[x].toArgb32();
        }
        paint.insert(it.key(),std::move(tile));
    }
    *ok=true;return paint;
}
void retainStrokePaint(const EditableObject &object,QRectF bounds,QMap<TileKey,QImage> paint) {
    qint64 bytes=strokeSourceBytes(object);
    for(const auto &tile:paint)bytes+=tile.sizeInBytes();
    if(bytes<=strokePaintCache.maxCost())
        strokePaintCache.insert(object.id,new StrokePaint{object,bounds,std::move(paint)},qMax(1,int(bytes)));
}
bool replayStrokeDabs(RasterLayer &layer,const EditableObject &object,QRectF bounds,int maxTiles,
                     QString *error,RasterLayer *completedPaint=nullptr) {
    auto fail=[error](const QString &message){if(error)*error=message;return false;};
    if (!object.brushStroke) return fail("The path has no brush stroke source.");
    if (!object.stroke.isValid() || object.stroke.alpha()==0 || object.strokeWidth<=0 || object.opacity<=0) return true;
    const auto &stroke=*object.brushStroke;
    const bool originalGeometry=editableGeometrySignature(object)==stroke.sourceGeometry;
    const Centerline centerline(originalGeometry&&object.widthFactors.isEmpty()?EditableObject{}:object);
    const auto original=layer; RasterLayer accumulation;
    const bool previousAlphaLock=layer.alphaLocked; layer.alphaLocked=stroke.alphaLocked;
    const auto selection=object.transform.map(stroke.selection);
    RasterSelectionCache selected(stroke.selectionActive?&selection:nullptr,stroke.selectionFeather);
    ConnectedBrushState connected;
    qreal newInitial=0; centerline.at(0,&newInitial);
    for (qsizetype index=0;index<stroke.samples.size();++index) {
        const auto &sample=stroke.samples[index];
        qreal direction=sample.direction, initial=sample.initialDirection;
        const auto mapped=centerline.at(sample.position,originalGeometry?nullptr:&direction);
        const auto point=originalGeometry?sample.point:mapped.point;
        if(!originalGeometry&&index>0)initial=newInitial;
        auto settings=stroke.settings; settings.color=object.stroke;
        settings.strokeOpacity*=object.opacity;
        const qreal size=object.strokeWidth*mapped.width;
        if(size<=0)continue;
        if (settings.connected.mode != BrushConnectedMode::Off) {
            settings.size = size;
            if (!appendConnectedBrush(layer, accumulation, original, connected, settings, point,
                sample.input, sample.dabIndex, stroke.seed, stroke.canvasSize, bounds, maxTiles, false,
                stroke.selectionActive ? &selection : nullptr, &selected, object.transform)) {
                layer.alphaLocked = previousAlphaLock;
                return fail("The edited connected stroke exceeds the artwork memory limit.");
            }
            continue;
        }
        const qreal scale=size>(usesAdvancedBrushTips(stroke.settings)?maxBrushSize:256.0)?size/(usesAdvancedBrushTips(stroke.settings)?maxBrushSize:256.0):(size<1?size:1);
        settings.size=qBound(1.0,size,usesAdvancedBrushTips(stroke.settings)?maxBrushSize:256.0);
        const int copies=brushDabCount(settings,sample.input,sample.dabIndex,stroke.seed);
        for(int copy=0;copy<copies;++copy){
            const quint32 seed=stroke.seed^(quint32(copy)*0x9e3779b9U);
            const auto dab=evaluateBrushDab(settings,sample.input,point,direction,initial,sample.dabIndex,seed,{},BrushDabPurpose::Painting,copy,stroke.seed);
            for(const auto &instance:brushSymmetryInstances(settings.symmetry,point+dab.offset*scale,stroke.canvasSize)){
                const auto transform=instance.transform*stampTransform(object.transform,instance.point,scale);
                const auto &silhouette=dab.untexturedMask.isNull()?dab.mask:dab.untexturedMask;
                const auto footprint=transform.mapRect(QRectF(-silhouette.width()/2.0,-silhouette.height()/2.0,
                                                              silhouette.width(),silhouette.height()));
                // A large object transform must not allocate an unbounded
                // temporary stamp before the tile budget can reject the edit.
                const qreal width=std::ceil(footprint.width())+4,height=std::ceil(footprint.height())+4;
                if(!std::isfinite(width)||!std::isfinite(height)||width<0||height<0
                    ||width>32768||height>32768||width*height>qreal(maxProjectTiles)*tileSize*tileSize){
                    layer.alphaLocked=previousAlphaLock;
                    return fail("The transformed brush stamp exceeds the artwork memory limit.");
                }
                if(!applyBrushDab(layer,accumulation,original,dab,object.transform.map(instance.point),transform,
                    settings.blendMode,false,bounds,maxTiles-layer.tiles.size(),stroke.seed,
                    stroke.selectionActive?&selection:nullptr,&selected)){
                    layer.alphaLocked=previousAlphaLock;
                    return fail("The edited stroke exceeds the tile memory limit.");
                }
            }
        }
    }
    layer.alphaLocked=previousAlphaLock;
    if(completedPaint)*completedPaint=std::move(accumulation);
    return true;
}
}
void clearVectorBrushReplayCache() { strokePaintCache.clear(); }
void rememberVectorBrushPaint(const EditableObject &object,QRectF bounds,const RasterLayer &accumulation) {
    if(!object.brushStroke||object.brushStroke->alphaLocked||accumulation.tiles.size()>maxCachedStrokeTiles
        ||strokeSourceBytes(object)>strokePaintCache.maxCost()/2||!bounds.isValid()
        ||!std::isfinite(bounds.left())||!std::isfinite(bounds.top())
        ||!std::isfinite(bounds.right())||!std::isfinite(bounds.bottom()))return;
    bool ok=false;auto paint=completedStrokePaint(accumulation,&ok);
    if(ok)retainStrokePaint(object,bounds,std::move(paint));
}
bool replayVectorBrushStroke(RasterLayer &layer,const EditableObject &object,QRectF bounds,int maxTiles,QString *error) {
    if(!object.brushStroke){if(error)*error="The path has no brush stroke source.";return false;}
    if(!object.stroke.isValid()||object.stroke.alpha()==0||object.strokeWidth<=0||object.opacity<=0)return true;
    const auto &stroke=*object.brushStroke;
    const auto apply=[&](const QMap<TileKey,QImage> &paint){
        if(compositeBrushAccumulation(layer,paint,stroke.settings.blendMode,stroke.alphaLocked,
            maxTiles-layer.tiles.size(),stroke.seed))return true;
        if(error)*error="The edited stroke exceeds the tile memory limit.";
        return false;
    };
    if(const auto cached=strokePaintCache.object(object.id);
        cached&&cached->bounds==bounds&&cached->source==object)return apply(cached->tiles);
    const qint64 sourceBytes=strokeSourceBytes(object);
    const auto area=vectorBrushBounds(object).intersected(bounds);
    const qreal columns=std::floor(area.right()/tileSize)-std::floor(area.left()/tileSize)+1;
    const qreal rows=std::floor(area.bottom()/tileSize)-std::floor(area.top()/tileSize)+1;
    // Oversized strokes retain the streaming replay path. The per-entry tile
    // budget bounds temporary accumulation as well as permanent cache memory.
    if(sourceBytes>strokePaintCache.maxCost()/2||(!area.isEmpty()&&columns*rows>maxCachedStrokeTiles))
        return replayStrokeDabs(layer,object,bounds,maxTiles,error);
    RasterLayer scratch,accumulation;
    auto independent=object;independent.brushStroke->alphaLocked=false;
    QString cacheError;
    if(!replayStrokeDabs(scratch,independent,bounds,maxCachedStrokeTiles,&cacheError,&accumulation))
        return replayStrokeDabs(layer,object,bounds,maxTiles,error);
    bool ok=false;auto paint=completedStrokePaint(accumulation,&ok);
    if(!ok)return replayStrokeDabs(layer,object,bounds,maxTiles,error);
    if(!apply(paint))return false;
    retainStrokePaint(object,bounds,std::move(paint));
    return true;
}
void sliceVectorBrushStroke(EditableObject &fragment,qreal from,qreal to,qreal sourceLength) {
    if(!fragment.brushStroke)return;
    auto &stroke=*fragment.brushStroke; QVector<VectorBrushSample> samples;
    from=qBound(0.0,from,1.0); to=qBound(0.0,to,1.0);
    const bool wrapped=to<from;
    const qreal length=wrapped?1-from+to:to-from;
    const bool connected = stroke.settings.connected.mode != BrushConnectedMode::Off;
    const auto endpoint = [&](qreal position) {
        VectorBrushSample result;
        if (stroke.samples.isEmpty()) return result;
        const auto right = std::lower_bound(stroke.samples.cbegin(), stroke.samples.cend(), position,
            [](const VectorBrushSample &sample, qreal value) { return sample.position < value; });
        if (right == stroke.samples.cbegin()) return *right;
        if (right == stroke.samples.cend()) return stroke.samples.last();
        const auto &b = *right, &a = *(right - 1);
        const qreal t = b.position > a.position ? (position - a.position) / (b.position - a.position) : 1;
        if (t >= 1) return b;
        const auto mix = [t](qreal first, qreal last) { return first + (last - first) * t; };
        result = a; result.point = a.point + (b.point - a.point) * t;
        result.input.pressure = mix(a.input.pressure, b.input.pressure);
        result.input.tiltX = mix(a.input.tiltX, b.input.tiltX); result.input.tiltY = mix(a.input.tiltY, b.input.tiltY);
        result.input.rotation = std::remainder(a.input.rotation + std::remainder(b.input.rotation - a.input.rotation, 360.) * t, 360.);
        result.input.wheel = mix(a.input.wheel, b.input.wheel); result.input.speed = mix(a.input.speed, b.input.speed);
        result.direction = a.direction + std::remainder(b.direction - a.direction, 360.) * t;
        result.position = position; return result;
    };
    const auto first = connected ? endpoint(from) : VectorBrushSample{};
    const auto last = connected ? endpoint(to) : VectorBrushSample{};
    for(auto sample:stroke.samples) if(wrapped?(sample.position>=from||sample.position<=to):(sample.position>=from&&sample.position<=to)){
        const qreal along=sample.position>=from?sample.position-from:1-from+sample.position;
        sample.position=length>0?along/length:0;samples.append(sample);
    }
    if(wrapped)std::stable_sort(samples.begin(),samples.end(),[](const auto&a,const auto&b){return a.position<b.position;});
    if(samples.isEmpty()&&!stroke.samples.isEmpty()&&!connected){
        auto nearest=std::min_element(stroke.samples.cbegin(),stroke.samples.cend(),[from,to](const auto&a,const auto&b){return std::abs(a.position-(from+to)/2)<std::abs(b.position-(from+to)/2);});
        auto sample=*nearest;sample.position=.5;samples.append(sample);
    }
    if (connected && !stroke.samples.isEmpty()) {
        // Keep both clipped boundaries even when the retained span falls
        // between two original input packets; one point cannot paint a strip.
        samples.removeIf([](const VectorBrushSample &s) { return s.position <= 1e-12 || s.position >= 1 - 1e-12; });
        auto a = first, b = last; a.position = 0; b.position = 1;
        samples.prepend(a); samples.append(b);
        for (qsizetype i = 0; i < samples.size(); ++i) samples[i].dabIndex = quint64(i);
        if (sourceLength < 0) {
            sourceLength = 0;
            for (int i = 1; i < stroke.samples.size(); ++i)
                sourceLength += QLineF(stroke.samples[i - 1].point, stroke.samples[i].point).length();
        }
        stroke.settings.connected.phaseOffset += from * sourceLength;
    }
    stroke.samples=samples;
    // A fixed invalidated signature keeps the decoder's SHA256 shape strict and
    // forces source positions onto the fragment's new centerline after reopening.
    stroke.sourceGeometry=QByteArray(32,'\0');
}
} // namespace toon
