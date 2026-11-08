#include "puppet_warp.h"
#include <QLineF>
#include <QMap>
#include <algorithm>
#include <cmath>
#include <limits>

namespace toon {
namespace {
qreal cross(QPointF a,QPointF b){return a.x()*b.y()-a.y()*b.x();}
qreal squared(QPointF p){return p.x()*p.x()+p.y()*p.y();}
bool finite(QPointF p){return std::isfinite(p.x())&&std::isfinite(p.y())&&std::abs(p.x())<=32768&&std::abs(p.y())<=32768;}
bool barycentric(QPointF p,QPointF a,QPointF b,QPointF c,QPointF *uv){
    const auto v=b-a,w=c-a,d=p-a;const auto area=cross(v,w);
    if(std::abs(area)<1e-12)return false;
    *uv={cross(d,w)/area,cross(v,d)/area};
    return uv->x()>=-1e-8&&uv->y()>=-1e-8&&uv->x()+uv->y()<=1+1e-8;
}
}
bool puppetMeshIdentity(const QVector<PuppetPin> &pins){
    return std::all_of(pins.cbegin(),pins.cend(),[](const auto &pin){return pin.source==pin.target;});
}
QPointF deformPuppetPoint(QPointF point,const QVector<PuppetPin> &pins){
    if(pins.isEmpty())return point;
    if(pins.size()==1)return point+pins[0].target-pins[0].source;
    qreal closest=std::numeric_limits<qreal>::max();
    for(const auto &pin:pins){const auto distance=squared(pin.source-point);if(distance<1e-16)return pin.target;closest=qMin(closest,distance);}
    // Normalized inverse-square weights avoid extreme values near a pin.
    qreal total=0;QPointF sourceCenter,targetCenter;
    for(const auto &pin:pins){const qreal weight=closest/squared(pin.source-point);total+=weight;sourceCenter+=pin.source*weight;targetCenter+=pin.target*weight;}
    sourceCenter/=total;targetCenter/=total;
    qreal cosine=0,sine=0;
    for(const auto &pin:pins){
        const qreal weight=closest/squared(pin.source-point);const auto p=pin.source-sourceCenter,q=pin.target-targetCenter;
        cosine+=weight*(p.x()*q.x()+p.y()*q.y());sine+=weight*cross(p,q);
    }
    const auto delta=point-sourceCenter;const qreal norm=std::hypot(cosine,sine);
    if(norm<1e-14)return delta+targetCenter;
    return targetCenter+QPointF((cosine*delta.x()-sine*delta.y())/norm,(sine*delta.x()+cosine*delta.y())/norm);
}
bool buildPuppetMesh(QRectF bounds,int density,const QVector<PuppetPin> &pins,PuppetMesh *result,QString *error){
    const auto fail=[&](const QString &message){if(error)*error=message;return false;};
    if(!result||!finite(bounds.topLeft())||!finite(bounds.bottomRight())||bounds.isEmpty())return fail("The puppet mesh needs a finite artwork area.");
    if(density<minPuppetDensity||density>maxPuppetDensity)return fail("Use a mesh density between 4 and 32.");
    if(pins.size()>maxPuppetPins)return fail("A puppet mesh supports up to 64 pins.");
    for(int i=0;i<pins.size();++i){
        if(!finite(pins[i].source)||!finite(pins[i].target)||!bounds.contains(pins[i].source))return fail("Place pins inside the mesh and drawing coordinates.");
        for(int j=0;j<i;++j)if(squared(pins[i].source-pins[j].source)<1e-8)return fail("Place each pin at a distinct point.");
    }
    PuppetMesh mesh;const qreal longest=qMax(bounds.width(),bounds.height());
    const int columns=qMax(1,qRound(density*bounds.width()/longest)),rows=qMax(1,qRound(density*bounds.height()/longest));
    for(int y=0;y<=rows;++y)for(int x=0;x<=columns;++x)mesh.source.append(bounds.topLeft()+QPointF(bounds.width()*x/columns,bounds.height()*y/rows));
    for(int y=0;y<rows;++y)for(int x=0;x<columns;++x){
        const int a=y*(columns+1)+x,b=a+1,d=a+columns+1,c=d+1;
        if((x+y)%2){mesh.triangles.append({a,b,d});mesh.triangles.append({b,c,d});}
        else{mesh.triangles.append({a,b,c});mesh.triangles.append({a,c,d});}
    }
    for(const auto &pin:pins){
        int vertex=-1;
        for(int i=0;i<mesh.source.size();++i)if(squared(mesh.source[i]-pin.source)<1e-16){vertex=i;break;}
        if(vertex<0){
            vertex=mesh.source.size();mesh.source.append(pin.source);QVector<WarpTriangle> split;
            for(const auto &triangle:mesh.triangles){
                QPointF uv;
                if(!barycentric(pin.source,mesh.source[triangle[0]],mesh.source[triangle[1]],mesh.source[triangle[2]],&uv)){split.append(triangle);continue;}
                // Split every incident triangle when the pin lies on an edge;
                // no hanging vertex or gap is left on its neighboring cell.
                for(int edge=0;edge<3;++edge){
                    const int a=triangle[edge],b=triangle[(edge+1)%3];
                    if(cross(mesh.source[b]-mesh.source[a],pin.source-mesh.source[a])>1e-10)split.append({a,b,vertex});
                }
            }
            mesh.triangles=std::move(split);
        }
        mesh.pinVertices.append(vertex);
    }
    const bool identity=puppetMeshIdentity(pins);mesh.target.reserve(mesh.source.size());
    for(const auto &point:mesh.source)mesh.target.append(identity?point:deformPuppetPoint(point,pins));
    QMap<QPair<int,int>,int> edges;
    for(const auto &triangle:mesh.triangles){
        const auto a=mesh.target[triangle[0]],b=mesh.target[triangle[1]],c=mesh.target[triangle[2]];
        if(!finite(a)||!finite(b)||!finite(c))return fail("Keep the mesh inside the drawing coordinates.");
        const auto sourceArea=cross(mesh.source[triangle[1]]-mesh.source[triangle[0]],mesh.source[triangle[2]]-mesh.source[triangle[0]]);
        if(cross(b-a,c-a)<=qMax(1e-10,sourceArea*1e-5))return fail("Move the pin closer: this position would fold or collapse the mesh.");
        for(int i=0;i<3;++i){int x=triangle[i],y=triangle[(i+1)%3];if(x>y)std::swap(x,y);++edges[{x,y}];}
    }
    QVector<QPair<int,int>> boundary;for(auto edge=edges.cbegin();edge!=edges.cend();++edge)if(edge.value()==1)boundary.append(edge.key());
    for(int i=0;i<boundary.size();++i)for(int j=i+1;j<boundary.size();++j){
        const auto a=boundary[i],b=boundary[j];if(a.first==b.first||a.first==b.second||a.second==b.first||a.second==b.second)continue;
        if(QLineF(mesh.target[a.first],mesh.target[a.second]).intersects(QLineF(mesh.target[b.first],mesh.target[b.second]))==QLineF::BoundedIntersection)
            return fail("Move the pin closer: the outer mesh edges would cross.");
    }
    *result=std::move(mesh);if(error)error->clear();return true;
}
bool inversePuppetPoint(const PuppetMesh &mesh,QPointF target,QPointF *source){
    if(!source||!finite(target))return false;
    for(const auto &triangle:mesh.triangles){
        const int a=triangle[0],b=triangle[1],c=triangle[2];QPointF uv;
        if(barycentric(target,mesh.target[a],mesh.target[b],mesh.target[c],&uv)){
            *source=mesh.source[a]+(mesh.source[b]-mesh.source[a])*uv.x()+(mesh.source[c]-mesh.source[a])*uv.y();return true;
        }
    }
    return false;
}
}
