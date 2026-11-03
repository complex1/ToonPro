#include "cel_shading.h"
#include <QBitArray>
#include <QPainter>
#include <QPen>
#include <algorithm>
#include <cmath>
#include <new>

namespace toon {
CelShadingResult renderCelShading(const QImage &input, const QVector<CelBoundary> &boundaries,
                                 const QVector<CelShadeFill> &fills, CelBoundaryFinish finish,
                                 const std::shared_ptr<std::atomic_bool> &cancelled) {
    CelShadingResult result;
    auto stopped=[&]{return cancelled && cancelled->load(std::memory_order_relaxed);};
    if(input.isNull() || input.width()<3 || input.height()<3 || qint64(input.width())*input.height()>16000000
       || boundaries.size()>256 || fills.size()>64) {
        result.error="Cel shading supports canvases up to 16 million pixels, 256 boundaries and 64 regions per preview.";return result;
    }
    try {
        const auto outline=input.convertToFormat(QImage::Format_ARGB32_Premultiplied);
        const int width=outline.width(),height=outline.height(),pixels=width*height;
        QImage guides(outline.size(),QImage::Format_ARGB32_Premultiplied);guides.fill(Qt::transparent);
        QImage thickGuides(outline.size(),QImage::Format_ARGB32_Premultiplied);thickGuides.fill(Qt::transparent);
        int guideRadius=1;
        {
            QPainter painter(&guides);painter.setRenderHint(QPainter::Antialiasing,false);
            QPainter thickPainter(&thickGuides);thickPainter.setRenderHint(QPainter::Antialiasing,true);
            int elementCount=0;
            for(const auto &boundary:boundaries) {
                if(stopped()){result.cancelled=true;return result;}
                elementCount+=boundary.path.elementCount();
                if(boundary.path.elementCount()>32768 || elementCount>262144 || !std::isfinite(boundary.width)) {result.error="The boundary is too complex.";return result;}
                for(int i=0;i<boundary.path.elementCount();++i){const auto element=boundary.path.elementAt(i);if(!std::isfinite(element.x)||!std::isfinite(element.y)||std::abs(element.x)>1000000||std::abs(element.y)>1000000){result.error="The boundary contains unsupported coordinates.";return result;}}
                // Segment at a one-pixel centerline. Display width never creates
                // an unpainted band between two neighboring shade regions.
                painter.setPen(QPen(Qt::white,1,Qt::SolidLine,Qt::RoundCap,Qt::RoundJoin));painter.drawPath(boundary.path);
                thickPainter.setPen(QPen(Qt::white,std::clamp(boundary.width,1.,12.),Qt::SolidLine,Qt::RoundCap,Qt::RoundJoin));thickPainter.drawPath(boundary.path);
                guideRadius=std::max(guideRadius,int(std::ceil(std::clamp(boundary.width,1.,12.)/2))+1);
            }
        }
        QBitArray blocked(pixels),guidePixels(pixels);
        for(int y=0;y<height;++y) {
            if((y&63)==0 && stopped()){result.cancelled=true;return result;}
            const auto *ink=reinterpret_cast<const QRgb*>(outline.constScanLine(y));
            const auto *guide=reinterpret_cast<const QRgb*>(guides.constScanLine(y));
            for(int x=0;x<width;++x){const int p=y*width+x;guidePixels.setBit(p,qAlpha(guide[x])>0);blocked.setBit(p,qAlpha(ink[x])>=32||guidePixels.testBit(p));}
        }
        result.shadow=QImage(outline.size(),QImage::Format_ARGB32_Premultiplied);result.shadow.fill(Qt::transparent);
        result.highlight=QImage(outline.size(),QImage::Format_ARGB32_Premultiplied);result.highlight.fill(Qt::transparent);
        for(int fillIndex=0;fillIndex<fills.size();++fillIndex) {
            if(stopped()){result.cancelled=true;return result;}
            const auto &fill=fills[fillIndex];const auto seed=fill.seed;
            if(!fill.color.isValid() || fill.color.alpha()==0 || seed.x()<0||seed.y()<0||seed.x()>=width||seed.y()>=height
               ||blocked.testBit(seed.y()*width+seed.x())){result.rejectedFills.append(fillIndex);continue;}
            QRect regionBounds(seed,QSize(1,1));
            QBitArray region(pixels);QVector<int> queue;queue.reserve(std::min(pixels,65536));
            queue.append(seed.y()*width+seed.x());region.setBit(queue.front());bool open=false;
            for(qsizetype head=0;head<queue.size();++head) {
                if((head&16383)==0 && stopped()){result.cancelled=true;return result;}
                const int p=queue[head],x=p%width,y=p/width;
                if(x==0||y==0||x==width-1||y==height-1){open=true;break;}
                regionBounds=regionBounds.united(QRect(x,y,1,1));
                for(const int next:{p-1,p+1,p-width,p+width})if(!blocked.testBit(next)&&!region.testBit(next)){region.setBit(next);queue.append(next);}
            }
            if(open){result.rejectedFills.append(fillIndex);continue;}
            // Cover the centerline and the translucent outline fringe. The new
            // paint sits BELOW the source ink, so AA edges meet the fill cleanly.
            QImage mask(outline.size(),QImage::Format_ARGB32_Premultiplied);mask.fill(Qt::transparent);
            int covered=0;
            for(const int p:queue){if((++covered&16383)==0&&stopped()){result.cancelled=true;return result;}const int x=p%width,y=p/width;
                for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx)
                    if(x+dx>=0&&y+dy>=0&&x+dx<width&&y+dy<height
                       && (region.testBit((y+dy)*width+x+dx)||blocked.testBit((y+dy)*width+x+dx)))
                        reinterpret_cast<QRgb*>(mask.scanLine(y+dy))[x+dx]=qRgba(255,255,255,255);
            }
            if(finish==CelBoundaryFinish::RegionColor) {
                // Test only the actual guide pixels, once per region, rather
                // than allocating and scanning a full image for every path.
                const auto scan=regionBounds.adjusted(-guideRadius,-guideRadius,guideRadius,guideRadius).intersected(outline.rect());
                for(int y=scan.top();y<=scan.bottom();++y){
                    if((y&31)==0&&stopped()){result.cancelled=true;return result;}
                    const auto *stroke=reinterpret_cast<const QRgb*>(thickGuides.constScanLine(y));
                    auto *row=reinterpret_cast<QRgb*>(mask.scanLine(y));
                    for(int x=scan.left();x<=scan.right();++x)if(qAlpha(stroke[x])&&!qAlpha(row[x])) {
                        bool touches=false;
                        for(int dy=-guideRadius;dy<=guideRadius&&!touches;++dy)for(int dx=-guideRadius;dx<=guideRadius&&!touches;++dx)
                            if(x+dx>=0&&y+dy>=0&&x+dx<width&&y+dy<height&&region.testBit((y+dy)*width+x+dx)) {
                                // A wide guide meeting the ink must not paint
                                // the other side of the character silhouette.
                                bool crossedInk=false;const int steps=std::max(std::abs(dx),std::abs(dy));
                                for(int step=0;step<=steps;++step){const int sx=x+(steps?qRound(qreal(dx)*step/steps):0),sy=y+(steps?qRound(qreal(dy)*step/steps):0);
                                    if(qAlpha(reinterpret_cast<const QRgb*>(outline.constScanLine(sy))[sx])>=32){crossedInk=true;break;}}
                                if(!crossedInk)touches=true;
                            }
                        if(touches)row[x]=stroke[x];
                    }
                }
            }
            {QPainter painter(&mask);painter.setCompositionMode(QPainter::CompositionMode_SourceIn);painter.fillRect(mask.rect(),fill.color);}
            auto &target=fill.kind==CelShadeKind::Shadow?result.shadow:result.highlight;
            QPainter painter(&target);painter.drawImage(0,0,mask);
        }
    } catch(const std::bad_alloc &) { result={};result.error="Not enough memory to preview cel shading. Try fewer regions."; }
    return result;
}
} // namespace toon
