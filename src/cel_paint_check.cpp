#include "cel_paint_check.h"
#include "document.h"
#include "keyframe_render.h"
#include <QHash>
#include <QPainter>
#include <QSet>
#include <algorithm>
#include <cmath>
#include <limits>
#include <new>

namespace toon {
namespace {
constexpr qint64 maximumPixels = 32LL * 1024 * 1024;
constexpr qint64 maximumDrawingPixels = 16LL * 1024 * 1024;
constexpr int maximumIssues = 1024;
constexpr int maximumRepairPixels = 2 * 1024 * 1024;
bool cancelled(const CelPaintCancellation &flag) { return flag && flag->load(std::memory_order_relaxed); }
int colorDistance(QRgb a, QRgb b) {
    return qMax(qMax(std::abs(qRed(a)-qRed(b)), std::abs(qGreen(a)-qGreen(b))), std::abs(qBlue(a)-qBlue(b)));
}
int nearest(QRgb pixel, const QVector<QColor> &palette, int *distance = nullptr) {
    int result=-1, best=256;
    for (int i=0;i<palette.size();++i) {
        const int value=colorDistance(pixel,palette[i].rgba());
        if (value<best) { best=value; result=i; if (!value) break; }
    }
    if (distance) *distance=best;
    return result;
}
bool mixture(QRgb pixel, QRgb first, QRgb second, int tolerance) {
    const double dx=qRed(second)-qRed(first), dy=qGreen(second)-qGreen(first), dz=qBlue(second)-qBlue(first);
    const double length=dx*dx+dy*dy+dz*dz;
    if (length<1) return false;
    const double t=((qRed(pixel)-qRed(first))*dx+(qGreen(pixel)-qGreen(first))*dy+(qBlue(pixel)-qBlue(first))*dz)/length;
    if (t<=0 || t>=1) return false;
    return std::abs(qRed(pixel)-(qRed(first)+t*dx))<=tolerance
        && std::abs(qGreen(pixel)-(qGreen(first)+t*dy))<=tolerance
        && std::abs(qBlue(pixel)-(qBlue(first)+t*dz))<=tolerance;
}
bool sameTiles(const RasterLayer &a, const RasterLayer &b) {
    if (a.tiles.size()!=b.tiles.size()) return false;
    auto left=a.tiles.cbegin(),right=b.tiles.cbegin();
    for (;left!=a.tiles.cend();++left,++right)
        if (left.key()!=right.key() || left.value().cacheKey()!=right.value().cacheKey()) return false;
    return true;
}
void putPixel(RasterLayer &layer,QPoint point,QRgb pixel) {
    const TileKey key{int(std::floor(point.x()/qreal(tileSize))),int(std::floor(point.y()/qreal(tileSize)))};
    auto found=layer.tiles.find(key);
    if (found==layer.tiles.end()) {
        QImage image(tileSize,tileSize,QImage::Format_ARGB32_Premultiplied); image.fill(Qt::transparent);
        found=layer.tiles.insert(key,image);
    }
    auto &image=found.value();
    reinterpret_cast<QRgb*>(image.scanLine(point.y()-key.second*tileSize))[point.x()-key.first*tileSize]=qPremultiply(pixel);
}
}
QString celPaintIssueName(CelPaintIssueKind kind) {
    switch(kind) {
    case CelPaintIssueKind::Hole:return "Unpainted hole";
    case CelPaintIssueKind::Leak:return "Possible fill leak";
    case CelPaintIssueKind::Stray:return "Stray pixels";
    case CelPaintIssueKind::OutsidePalette:return "Outside palette";
    }
    return {};
}
CelPaintCheckResult inspectCelPaint(QVector<CelPaintCheckTarget> targets,const CelPaintCheckSettings &settings,
                                   const CelPaintCancellation &cancel) {
    CelPaintCheckResult result; result.settings=settings; result.targets=std::move(targets);
    qint64 pixels=0;
    if (result.targets.isEmpty() || result.targets.size()>64) { result.error="Choose between 1 and 64 raster drawings."; return result; }
    if (settings.tolerance<0 || settings.tolerance>64 || settings.maximumHolePixels<1 || settings.maximumHolePixels>4096
        || settings.maximumStrayPixels<1 || settings.maximumStrayPixels>256 || settings.approvedColors.size()>256) {
        result.error="Invalid paint check settings."; return result;
    }
    QSet<QString> ids;
    for (const auto &target:result.targets) {
        const auto area=qint64(target.bounds.width())*target.bounds.height(); pixels+=area;
        const auto id=target.shotId+"/"+target.layerId;
        if (target.bounds.isEmpty() || area>maximumDrawingPixels || pixels>maximumPixels || ids.contains(id)
            || target.original.kind!=LayerKind::Raster || target.original.group || target.original.scene3d || target.original.adjustment) {
            result.error="Choose unique raster drawings, up to 16 million pixels each and 32 million pixels per check."; return result;
        }
        ids.insert(id);
    }
    for (const auto &color:settings.approvedColors) if (!color.isValid()) { result.error="The approved palette contains an invalid color."; return result; }
    int retainedPixels=0;
    try {
        for (int targetIndex=0;targetIndex<result.targets.size();++targetIndex) {
            auto &target=result.targets[targetIndex];
            target.image=QImage(target.bounds.size(),QImage::Format_ARGB32);
            if (target.image.isNull()) {result.error="Not enough memory to inspect these drawings.";return result;}
            target.image.fill(Qt::transparent);
            {QPainter painter(&target.image);painter.translate(-target.bounds.topLeft());paintRasterLayerPixels(painter,target.original);}
            const int width=target.image.width(),height=target.image.height(),count=width*height;
            const auto at=[&](int index){return reinterpret_cast<const QRgb*>(target.image.constScanLine(index/width))[index%width];};
            QByteArray visited(count,0); QVector<int> queue;
            const auto collect=[&](int seed,bool diagonal,const auto &eligible) {
                queue.clear(); queue.append(seed); visited[seed]=1;
                for (qsizetype head=0;head<queue.size();++head) {
                    if ((head&4095)==0 && cancelled(cancel)) return false;
                    const int value=queue[head],x=value%width,y=value/width;
                    for (int dy=-1;dy<=1;++dy) for (int dx=-1;dx<=1;++dx) {
                        if ((!dx&&!dy) || (!diagonal&&dx&&dy) || x+dx<0 || x+dx>=width || y+dy<0 || y+dy>=height) continue;
                        const int next=value+dy*width+dx;
                        if (!visited[next] && eligible(next)) {visited[next]=1;queue.append(next);}
                    }
                }
                return true;
            };
            const auto record=[&](CelPaintIssueKind kind,bool allowRepair=true) {
                if (result.issues.size()>=maximumIssues) {result.truncated=true;return;}
                CelPaintIssue issue;issue.kind=kind;issue.target=targetIndex;issue.pixelCount=int(queue.size());
                int left=width,top=height,right=0,bottom=0;
                for (int index:queue) {left=qMin(left,index%width);right=qMax(right,index%width);top=qMin(top,index/width);bottom=qMax(bottom,index/width);}
                issue.bounds=QRect(QPoint(left,top),QPoint(right,bottom)).translated(target.bounds.topLeft());
                issue.repairable=allowRepair && retainedPixels+queue.size()<=maximumRepairPixels;
                if (issue.repairable) {issue.pixels=queue;retainedPixels+=queue.size();}
                else if(allowRepair)result.truncated=true;
                result.issues.append(std::move(issue));
            };
            // Transparent regions connected to the frame edge are intentional
            // exterior. Tiny enclosed regions are suspects; never auto-selected.
            for (int index=0;index<count;++index) {
                if ((index&4095)==0 && cancelled(cancel)) {result.cancelled=true;return result;}
                if (visited[index] || qAlpha(at(index))>8) continue;
                if (!collect(index,false,[&](int value){return qAlpha(at(value))<=8;})) {result.cancelled=true;return result;}
                bool edge=false; for (int value:queue) if (value%width==0 || value%width==width-1 || value/width==0 || value/width==height-1) {edge=true;break;}
                if (!edge) record(CelPaintIssueKind::Hole,queue.size()<=settings.maximumHolePixels);
            }
            visited.fill(0);
            // Eight-connected islands preserve diagonal ink strokes.
            for (int index=0;index<count;++index) {
                if ((index&4095)==0 && cancelled(cancel)) {result.cancelled=true;return result;}
                if (visited[index] || qAlpha(at(index))<=8) continue;
                if (!collect(index,true,[&](int value){return qAlpha(at(value))>8;})) {result.cancelled=true;return result;}
                if (queue.size()<=settings.maximumStrayPixels) record(CelPaintIssueKind::Stray);
            }
            visited.fill(0);
            // A flat region reaching the frame can be a leak or an intentional
            // background/crop. Keep it explicitly labelled and unchecked.
            for (int index=0;index<count;++index) {
                if ((index&4095)==0 && cancelled(cancel)) {result.cancelled=true;return result;}
                const auto seed=at(index);
                if (visited[index] || qAlpha(seed)<250 || qMax(qMax(qRed(seed),qGreen(seed)),qBlue(seed))<40) continue;
                if (!collect(index,false,[&](int value){const auto pixel=at(value);return qAlpha(pixel)>=250 && colorDistance(pixel,seed)<=settings.tolerance;})) {result.cancelled=true;return result;}
                if (queue.size()<=qMax(16,settings.maximumStrayPixels)) continue;
                bool edge=false;for (int value:queue) if (value%width==0 || value%width==width-1 || value/width==0 || value/width==height-1) {edge=true;break;}
                if(edge)record(CelPaintIssueKind::Leak);
            }
            if (settings.approvedColors.isEmpty()) continue;
            QByteArray outside(count,0); QHash<QRgb,QPair<int,int>> matches;
            const auto match=[&](QRgb pixel) {
                auto found=matches.constFind(pixel); if(found!=matches.cend())return found.value();
                int distance=0; const int color=nearest(pixel,settings.approvedColors,&distance);
                const QPair<int,int> value{color,distance}; if(matches.size()<65536)matches.insert(pixel,value);return value;
            };
            for(int index=0;index<count;++index) {
                if((index&4095)==0 && cancelled(cancel)){result.cancelled=true;return result;}
                const auto pixel=at(index); if(qAlpha(pixel)<250 || match(pixel).second<=settings.tolerance)continue;
                // Fully opaque antialias blends between approved adjacent inks
                // are not new flat colors. Require two local approved colors.
                QSet<int> adjacent; const int x=index%width,y=index/width;
                for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx) {
                    if(x+dx<0||x+dx>=width||y+dy<0||y+dy>=height)continue;
                    const auto neighbor=at(index+dy*width+dx); if(qAlpha(neighbor)<250)continue;
                    const auto found=match(neighbor);if(found.second<=settings.tolerance)adjacent.insert(found.first);
                }
                bool blend=false;
                for(auto a=adjacent.cbegin();a!=adjacent.cend()&&!blend;++a) for(auto b=adjacent.cbegin();b!=a;++b)
                    if(mixture(pixel,settings.approvedColors[*a].rgba(),settings.approvedColors[*b].rgba(),qMax(3,settings.tolerance))){blend=true;break;}
                if(!blend)outside[index]=1;
            }
            visited.fill(0);
            for(int index=0;index<count;++index) {
                if((index&4095)==0 && cancelled(cancel)){result.cancelled=true;return result;}
                if(visited[index]||!outside[index])continue;
                if(!collect(index,true,[&](int value){return outside[value]!=0;})){result.cancelled=true;return result;}
                record(CelPaintIssueKind::OutsidePalette);
            }
        }
    } catch(const std::bad_alloc &) {result.error="Not enough memory to inspect these drawings.";}
    return result;
}
CelPaintRepairResult prepareCelPaintRepairs(const CelPaintCheckResult &inspection,const QVector<int> &indices,
                                          QColor holeColor,const CelPaintCancellation &cancel) {
    CelPaintRepairResult result;
    if(!inspection.error.isEmpty()||inspection.cancelled||!holeColor.isValid()){result.error="Run a valid paint check and choose a repair color first.";return result;}
    QHash<int,int> output;QSet<int> seen;
    try {
        for(int issueIndex:indices) {
            if(cancelled(cancel)){result.cancelled=true;return result;}
            if(issueIndex<0||issueIndex>=inspection.issues.size()||seen.contains(issueIndex))continue;
            seen.insert(issueIndex);const auto &issue=inspection.issues[issueIndex];
            if(!issue.repairable||issue.pixels.isEmpty()){result.error="This large region needs manual inspection; deselect it before previewing repairs.";return result;}
            if(!output.contains(issue.target)) {
                const auto &source=inspection.targets.at(issue.target);
                output.insert(issue.target,result.drawings.size());
                result.drawings.append({source,source.original,source.image});
            }
            auto &drawing=result.drawings[output[issue.target]];const int width=drawing.preview.width();
            for(int index:issue.pixels) {
                if((index&4095)==0&&cancelled(cancel)){result.cancelled=true;return result;}
                const QPoint local(index%width,index/width);QRgb replacement=0;
                if(issue.kind==CelPaintIssueKind::Hole)replacement=holeColor.rgba();
                else if(issue.kind==CelPaintIssueKind::OutsidePalette) {
                    const int selected=nearest(drawing.source.image.pixel(local),inspection.settings.approvedColors);
                    if(selected<0){result.error="Choose an approved Character Color Model palette.";return result;}
                    auto color=inspection.settings.approvedColors[selected];color.setAlpha(qAlpha(drawing.source.image.pixel(local)));replacement=color.rgba();
                }
                drawing.preview.setPixel(local,replacement);putPixel(drawing.replacement,local+drawing.source.bounds.topLeft(),replacement);
            }
        }
    }catch(const std::bad_alloc &){result.error="Not enough memory to preview these repairs.";}
    if(result.drawings.isEmpty()&&result.error.isEmpty())result.error="Check at least one problem to preview a repair.";
    return result;
}
bool applyCelPaintRepairs(Document *document,const CelPaintRepairResult &repairs,QString *error) {
    const auto fail=[&](const QString &message){if(error)*error=message;return false;};
    if(error)error->clear();
    if(!document||!document->isOpen()||document->editingGestureActive()||document->layerPropertyEditActive())return fail("Finish the current drawing or property edit first.");
    if(!repairs.error.isEmpty()||repairs.cancelled||repairs.drawings.isEmpty())return fail("Preview valid repairs first.");
    auto project=document->project();QSet<QString> seen;
    for(const auto &repair:repairs.drawings) {
        const QString id=repair.source.shotId+"/"+repair.source.layerId;
        if(seen.contains(id))return fail("A repair target was duplicated.");
        seen.insert(id);
        if(repair.source.bounds!=QRect(0,0,project.width,project.height))
            return fail("The artwork frame changed. Run the paint check again.");
        auto shot=std::find_if(project.shots.begin(),project.shots.end(),[&](const Shot &value){return value.id==repair.source.shotId;});
        if(shot==project.shots.end())return fail("The source shot changed. Run the paint check again.");
        int index=-1;for(int i=0;i<shot->layers.size();++i)if(shot->layers[i].id==repair.source.layerId){index=i;break;}
        if(index<0||!sameTiles(shot->layers[index],repair.source.original))return fail("The source drawing changed. Run the paint check again.");
        const auto &layer=shot->layers[index];
        if(!layerCanEdit(shot->layers,index)||layer.kind!=LayerKind::Raster||layer.group||layer.scene3d||layer.adjustment||layer.alphaLocked)
            return fail("Unlock the selected raster drawings, including transparent-pixel locks, before repairing them.");
        for (const auto &animation : shot->layerAnimations)
            if (animation.layerId == layer.id || layerIsDescendant(shot->layers,index,animation.layerId))
                return fail("Paint checks repair source pixels. Remove property animation from the target or its parent before applying repairs.");
        shot->layers[index].tiles=repair.replacement.tiles;
    }
    const auto validation=project.validate();if(!validation.isEmpty())return fail(validation);
    bool committed=false;
    const auto connection=QObject::connect(document,&Document::operationCommitted,document,[&]{committed=true;});
    document->replaceProject(project,"Repair cel paint");
    QObject::disconnect(connection);
    if(!committed)return fail("The repairs could not be applied; the artwork is unchanged.");
    return true;
}
}
