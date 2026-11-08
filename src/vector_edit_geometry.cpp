#include "vector_edit_geometry.h"
#include "brush_engine.h"
#include <QLineF>
#include <QDebug>
#include <algorithm>
#include <cmath>
#include <functional>

namespace toon {
namespace {
qreal distanceToSegment(QPointF p,QPointF a,QPointF b) {
    const auto d=b-a; const qreal length=d.x()*d.x()+d.y()*d.y();
    const qreal t=length>1e-12?qBound(0.,QPointF::dotProduct(p-a,d)/length,1.):0;
    return QLineF(p,a+d*t).length();
}
QPointF cubic(const BezierNode &a,const BezierNode &b,qreal t) {
    const qreal u=1-t;return a.anchor*(u*u*u)+a.outControl*(3*u*u*t)+b.inControl*(3*u*t*t)+b.anchor*(t*t*t);
}
void split(EditableObject &o,int index,qreal t) {
    const int next=(index+1)%o.nodes.size(); auto &a=o.nodes[index];auto &b=o.nodes[next];
    const auto p01=a.anchor*(1-t)+a.outControl*t,p12=a.outControl*(1-t)+b.inControl*t,p23=b.inControl*(1-t)+b.anchor*t;
    const auto p012=p01*(1-t)+p12*t,p123=p12*(1-t)+p23*t,center=p012*(1-t)+p123*t;
    const qreal factor=vectorNodeWidthFactor(o,index)*(1-t)+vectorNodeWidthFactor(o,next)*t;
    if(o.widthFactors.isEmpty())o.widthFactors=QVector<qreal>(o.nodes.size(),1);
    a.outControl=p01;b.inControl=p23;o.nodes.insert(index+1,{center,p012,p123});o.widthFactors.insert(index+1,factor);
}
QRectF localBounds(const EditableObject &object) {
    auto o=object;o.transform={}; auto bounds=editableObjectBounds(o);
    if(bounds.width()<1)bounds.adjust(-.5,0,.5,0);
    if(bounds.height()<1)bounds.adjust(0,-.5,0,.5);
    return bounds;
}
QVector<QPointF> correctionPoints(const EditableObject &o,QPointF point) {
    if(!o.brushStroke)return {o.transform.map(point)};
    const auto &stroke=*o.brushStroke;QVector<QPointF> result;
    for(const auto &instance:brushSymmetryInstances(stroke.settings.symmetry,point,stroke.canvasSize))
        result.append(o.transform.map(instance.point));
    return result;
}
bool curvedSymmetry(const EditableObject &o) {
    if(!o.brushStroke)return false;
    const auto mode=o.brushStroke->settings.symmetry.mode;
    return mode==SymmetryMode::Wave||mode==SymmetryMode::Circle||mode==SymmetryMode::Spiral;
}
QVector<QPainterPath> correctionPaths(const EditableObject &o,const QPainterPath &local) {
    if(!o.brushStroke)return {o.transform.map(local)};
    QVector<QPainterPath> result;const auto &stroke=*o.brushStroke;
    if(!curvedSymmetry(o)) {
        // The stamp transform omits the symmetry center's translation. Recover
        // that translation at the origin before applying the object transform.
        for(const auto &instance:brushSymmetryInstances(stroke.settings.symmetry,{},stroke.canvasSize))
            result.append((instance.transform*QTransform::fromTranslate(instance.point.x(),instance.point.y())*o.transform).map(local));
        return result;
    }
    // Curved guides map each centerline position independently. Keep the same
    // mapping as brush replay, rather than reflecting the entire path about one
    // tangent. Subdivide straight sections too, since their copies can curve.
    for(const auto &polygon:local.toSubpathPolygons()) {
        if(polygon.isEmpty())continue;
        auto append=[&](QPointF point,bool start) {
            const auto copies=correctionPoints(o,point);
            while(result.size()<copies.size())result.append(QPainterPath());
            for(int i=0;i<copies.size();++i)if(start)result[i].moveTo(copies[i]);else result[i].lineTo(copies[i]);
        };
        append(polygon.first(),true);
        for(int i=1;i<polygon.size();++i) {
            const int steps=qBound(1,int(std::ceil(QLineF(polygon[i-1],polygon[i]).length()/4)),4096);
            for(int step=1;step<=steps;++step)append(polygon[i-1]+(polygon[i]-polygon[i-1])*(qreal(step)/steps),false);
        }
    }
    return result;
}
}
qreal vectorNodeWidthFactor(const EditableObject &o,int index) {
    return index>=0&&index<o.widthFactors.size()?o.widthFactors[index]:1.;
}
QPointF vectorNodeNormal(const EditableObject &o,int index) {
    if(index<0||index>=o.nodes.size())return {0,-1};
    const auto &node=o.nodes[index];auto tangent=node.outControl-node.inControl;
    if(QLineF({},tangent).length()<1e-6) {
        const auto previous=index>0?o.nodes[index-1].anchor:o.closed?o.nodes.last().anchor:node.anchor;
        const auto next=index+1<o.nodes.size()?o.nodes[index+1].anchor:o.closed?o.nodes.first().anchor:node.anchor;
        tangent=next-previous;
    }
    const qreal length=QLineF({},tangent).length();return length>1e-6?QPointF(tangent.y(),-tangent.x())/length:QPointF(0,-1);
}
QPointF vectorWidthHandle(const EditableObject &o,int index) {
    return o.transform.map(o.nodes[index].anchor+vectorNodeNormal(o,index)*(o.strokeWidth*vectorNodeWidthFactor(o,index)/2));
}
EditableObject simplifiedVectorPath(const EditableObject &o,qreal tolerance,const QPainterPath &region) {
    if(o.kind!=ObjectKind::Path||o.nodes.size()<3||!std::isfinite(tolerance)||tolerance<=0)return o;
    const int n=o.nodes.size();QVector<bool> keep(n,false);keep[0]=keep[n-1]=true;
    QVector<qreal> distances(n,0);for(int i=1;i<n;++i)distances[i]=distances[i-1]+QLineF(o.transform.map(o.nodes[i-1].anchor),o.transform.map(o.nodes[i].anchor)).length();
    struct Copies { QVector<QPointF> anchor,in,out; };
    QVector<Copies> copies;copies.reserve(n);
    for(const auto &node:o.nodes)copies.append({correctionPoints(o,node.anchor),{}, {}});
    QVector<bool> affected(n,true);
    if(!region.isEmpty())for(int i=0;i<n;++i)affected[i]=std::any_of(copies[i].anchor.cbegin(),copies[i].anchor.cend(),[&](QPointF point){return region.contains(point);});
    if(!std::any_of(affected.cbegin()+1,affected.cend()-1,[](bool value){return value;}))return o;
    for(int i=0;i<n;++i) {
        copies[i].in=o.nodes[i].inControl==o.nodes[i].anchor?copies[i].anchor:correctionPoints(o,o.nodes[i].inControl);
        copies[i].out=o.nodes[i].outControl==o.nodes[i].anchor?copies[i].anchor:correctionPoints(o,o.nodes[i].outControl);
    }
    QVector<QPair<int,int>> ranges;int protectedIndex=0;
    if(!region.isEmpty())for(int i=1;i<n-1;++i)if(!affected[i]){keep[i]=true;ranges.append({protectedIndex,i});protectedIndex=i;}
    ranges.append({protectedIndex,n-1});int budget=2000000;
    while(!ranges.isEmpty()) {
        const auto range=ranges.takeLast();const int first=range.first,last=range.second;if(last-first<2)continue;
        qreal error=0;int furthest=-1;
        for(int i=first+1;i<last;++i) {
            if(--budget<=0)return o;
            qreal value=0;
            for(int copy=0;copy<copies[i].anchor.size();++copy) {
                const auto a=copies[first].anchor[copy],b=copies[last].anchor[copy];
                value=qMax(value,distanceToSegment(copies[i].anchor[copy],a,b));
                value=qMax(value,distanceToSegment(copies[i].in[copy],a,b));
                value=qMax(value,distanceToSegment(copies[i].out[copy],a,b));
                if(i==first+1)value=qMax(value,distanceToSegment(copies[first].out[copy],a,b));
                if(i==last-1)value=qMax(value,distanceToSegment(copies[last].in[copy],a,b));
            }
            if(!affected[i])value=1e20;
            const qreal ratio=distances[last]>distances[first]?(distances[i]-distances[first])/(distances[last]-distances[first]):qreal(i-first)/(last-first);
            const qreal expected=vectorNodeWidthFactor(o,first)*(1-ratio)+vectorNodeWidthFactor(o,last)*ratio;
            value=qMax(value,qAbs(expected-vectorNodeWidthFactor(o,i))*qMax(1.,o.strokeWidth));
            if(value>error){error=value;furthest=i;}
        }
        if(error>tolerance&&furthest>=0){keep[furthest]=true;ranges.append({first,furthest});ranges.append({furthest,last});}
    }
    auto result=o;result.nodes.clear();result.widthFactors.clear();int previous=-1;
    for(int i=0;i<n;++i)if(keep[i]) {
        auto node=o.nodes[i];
        if(previous>=0&&i>previous+1){result.nodes.last().outControl=result.nodes.last().anchor;node.inControl=node.anchor;}
        result.nodes.append(node);if(!o.widthFactors.isEmpty())result.widthFactors.append(vectorNodeWidthFactor(o,i));previous=i;
    }
    // Closed paths retain at least three anchors and their closing cubic.
    if(result.nodes.size()<(o.closed?3:2)||result.nodes.size()>=n)return o;
    return result;
}
bool vectorCorrectionIntersects(const EditableObject &o,const QPainterPath &region) {
    if(o.kind!=ObjectKind::Path||o.nodes.isEmpty()||region.isEmpty())return false;
    for(const auto point:correctionPoints(o,o.nodes.first().anchor))if(region.contains(point))return true;
    auto local=o;local.transform={};
    for(const auto &path:correctionPaths(o,editableObjectPath(local)))
        if(path.intersects(region))return true;
    return false;
}
int insertVectorNode(EditableObject &o,QPointF point,qreal tolerance) {
    if(o.kind!=ObjectKind::Path||o.nodes.size()>=16384)return -1;
    int segment=-1;qreal parameter=0,best=tolerance;const int count=o.nodes.size()-(o.closed?0:1);
    for(int i=0;i<count;++i) {
        const auto &a=o.nodes[i],&b=o.nodes[(i+1)%o.nodes.size()];
        auto previous=o.transform.map(a.anchor);
        for(int j=1;j<=32;++j) {
            const auto next=o.transform.map(cubic(a,b,qreal(j)/32));const auto delta=next-previous;
            const qreal denominator=QPointF::dotProduct(delta,delta);
            const qreal fraction=denominator>1e-12?qBound(0.,QPointF::dotProduct(point-previous,delta)/denominator,1.):0;
            const qreal distance=QLineF(point,previous+delta*fraction).length();
            if(distance<best){best=distance;segment=i;parameter=(j-1+fraction)/32;}previous=next;
        }
    }
    if(segment<0)return -1;
    if(parameter<.001)return segment;if(parameter>.999)return (segment+1)%o.nodes.size();
    split(o,segment,parameter);return segment+1;
}
void resampleVectorWidthNodes(EditableObject &o,qreal spacing,int limit) {
    if(o.kind!=ObjectKind::Path||spacing<=0)return;
    for(int i=0;i<o.nodes.size()-(o.closed?0:1)&&o.nodes.size()<limit;++i) {
        const auto a=o.nodes[i],b=o.nodes[(i+1)%o.nodes.size()];
        QPainterPath path(a.anchor);path.cubicTo(a.outControl,b.inControl,b.anchor);
        qreal length=0;for(const auto &copy:correctionPaths(o,path))length=qMax(length,copy.length());
        const int available=limit-int(o.nodes.size())+1;
        const int divisions=length>=spacing*available?available:qMax(1,int(std::ceil(length/spacing-1e-6)));
        for(int part=1;part<divisions;++part){split(o,i,1./(divisions-part+1));++i;}
    }
}
bool brushVectorWidths(EditableObject &o,QPointF from,QPointF to,qreal radius,qreal strength,bool thicken) {
    if(o.kind!=ObjectKind::Path||radius<=0||strength<=0)return false;
    bool changed=false;auto widths=o.widthFactors.isEmpty()?QVector<qreal>(o.nodes.size(),1):o.widthFactors;
    for(int i=0;i<o.nodes.size();++i) {
        qreal distance=radius;
        for(const auto point:correctionPoints(o,o.nodes[i].anchor))distance=qMin(distance,distanceToSegment(point,from,to));
        if(distance>=radius)continue;
        const qreal weight=std::pow(1-distance/radius,2);
        const qreal value=qBound(.02,widths[i]*std::exp((thicken?1:-1)*qBound(0.,strength,1.)*weight),16.);
        changed|=qAbs(value-widths[i])>1e-9;widths[i]=value;
    }
    if(changed)o.widthFactors=widths;return changed;
}
std::optional<EditableObject> nudgedVectorPath(const EditableObject &source,QPointF center,
        QPointF displacement,qreal radius,qreal strength,const QTransform &layerTransform,QString *error) {
    const auto fail=[&](const QString &reason)->std::optional<EditableObject>{if(error)*error=reason;return {};};
    const auto finite=[](QPointF p){return std::isfinite(p.x())&&std::isfinite(p.y());};
    if(!finite(center)||!finite(displacement)||!std::isfinite(radius)||radius<=0||!std::isfinite(strength)||strength<0||strength>1)
        return fail("The nudge brush has invalid settings.");
    if(source.kind!=ObjectKind::Path||source.nodes.size()<2||!strength||QLineF({},displacement).length()<1e-9)return source;
    auto result=source;auto world=source;world.transform*=layerTransform;
    QPainterPath region;region.addEllipse(center,radius,radius);
    if(!vectorCorrectionIntersects(world,region))return source;
    if(!world.transform.isInvertible())return fail("Restore a nonzero layer and path scale before nudging strokes.");
    // Split exact cubics only near the brush. Without this, a long two-node
    // stroke has no local anchors and dragging its middle moves the whole line.
    const qreal spacing=qMax(.25,radius/6);bool exhausted=false;
    std::function<int(int,int)> refine=[&](int index,int depth){
        const auto &a=result.nodes[index],&b=result.nodes[(index+1)%result.nodes.size()];
        QPainterPath segment(a.anchor);segment.cubicTo(a.outControl,b.inControl,b.anchor);
        auto mapped=result;mapped.transform=world.transform;bool touches=false;qreal length=0;
        for(const auto &copy:correctionPaths(mapped,segment)){touches|=copy.intersects(region)||region.contains(copy.pointAtPercent(0));length=qMax(length,copy.length());}
        if(!touches||length<=spacing)return index+1;
        if(depth>=18||result.nodes.size()>=16384){exhausted=true;return index+1;}
        split(result,index,.5);const int right=refine(index,depth+1);return exhausted?right:refine(right,depth+1);
    };
    for(int index=0;index<result.nodes.size()-(result.closed?0:1)&&!exhausted;)index=refine(index,0);
    if(exhausted)return fail("This nudge needs too many path nodes. Use a larger radius or simplify the stroke first.");
    const auto mapCopy=[&](QPointF point,int copy){
        if(!source.brushStroke)return world.transform.map(point);
        const auto &stroke=*source.brushStroke;const auto instances=brushSymmetryInstances(stroke.settings.symmetry,point,stroke.canvasSize);
        return copy<instances.size()?world.transform.map(instances[copy].point):world.transform.map(point);
    };
    bool singular=false;
    const auto pushed=[&](QPointF point){
        const auto copies=correctionPoints(world,point);int nearest=-1;qreal distance=radius;
        for(int copy=0;copy<copies.size();++copy){const auto d=QLineF(center,copies[copy]).length();if(d<distance){distance=d;nearest=copy;}}
        if(nearest<0)return point;
        const qreal falloff=std::pow(1-std::pow(distance/radius,2),2);
        const auto target=copies[nearest]+displacement*(strength*falloff);
        // A single inverse tangent is sufficient for affine symmetry, but a
        // curved mirror can turn substantially during a push. Solve its actual
        // captured mapping, with bounded Newton steps and residual line search.
        // This keeps the touched copy under the artist's drag, not merely close
        // to the first tangent's extrapolation.
        constexpr qreal epsilon=.01;auto guess=point;qreal trustRadius=0;
        for(int iteration=0;iteration<12;++iteration){
            const auto residual=target-mapCopy(guess,nearest);const qreal error=QPointF::dotProduct(residual,residual);
            if(error<1e-10)return guess;
            const auto dx=(mapCopy(guess+QPointF(epsilon,0),nearest)-mapCopy(guess-QPointF(epsilon,0),nearest))/(2*epsilon);
            const auto dy=(mapCopy(guess+QPointF(0,epsilon),nearest)-mapCopy(guess-QPointF(0,epsilon),nearest))/(2*epsilon);
            QTransform jacobian(dx.x(),dx.y(),dy.x(),dy.y(),0,0);bool invertible=false;const auto inverse=jacobian.inverted(&invertible);
            if(!invertible)break;
            const auto step=inverse.map(residual);if(!iteration)trustRadius=qMax(.01,2*QLineF({},step).length());bool accepted=false;
            for(qreal fraction=1;fraction>=1./128;fraction*=.5){const auto candidate=guess+step*fraction;if(!finite(candidate)||QLineF(point,candidate).length()>trustRadius)continue;const auto next=target-mapCopy(candidate,nearest);if(QPointF::dotProduct(next,next)<error){guess=candidate;accepted=true;break;}}
            if(!accepted)break;
        }
        const auto remaining=target-mapCopy(guess,nearest);
        if(QPointF::dotProduct(remaining,remaining)<1e-8)return guess;
        if(qEnvironmentVariableIsSet("TOON_NUDGE_DIAGNOSTICS")){
            const auto dx=(mapCopy(guess+QPointF(epsilon,0),nearest)-mapCopy(guess-QPointF(epsilon,0),nearest))/(2*epsilon);
            const auto dy=(mapCopy(guess+QPointF(0,epsilon),nearest)-mapCopy(guess-QPointF(0,epsilon),nearest))/(2*epsilon);
            qInfo()<<"Nudge copy inverse refused"<<"copy"<<nearest<<"source"<<point<<"target"<<target<<"candidate"<<guess<<"remaining"<<remaining<<"Jacobian"<<QTransform(dx.x(),dx.y(),dy.x(),dy.y(),0,0);
        }
        singular=true;return point;
    };
    for(auto &node:result.nodes){node.anchor=pushed(node.anchor);node.inControl=pushed(node.inControl);node.outControl=pushed(node.outControl);}
    if(singular)return fail("This curved symmetry copy cannot follow the push reliably here. Try a shorter drag or a different part of the stroke.");
    if(source.widthFactors.isEmpty())result.widthFactors.clear();
    if(const auto reason=validateEditableObject(result);!reason.isEmpty())return fail(reason);
    return result;
}
QVector<QPointF> vectorTransformHandles(const EditableObject &o) {
    const auto r=localBounds(o);return {o.transform.map(r.topLeft()),o.transform.map(QPointF(r.center().x(),r.top())),o.transform.map(r.topRight()),o.transform.map(QPointF(r.right(),r.center().y())),o.transform.map(r.bottomRight()),o.transform.map(QPointF(r.center().x(),r.bottom())),o.transform.map(r.bottomLeft()),o.transform.map(QPointF(r.left(),r.center().y()))};
}
QTransform vectorHandleTransform(const EditableObject &o,int handle,QPointF start,QPointF current,bool constrain) {
    if(handle==8) {
        const auto center=o.transform.map(localBounds(o).center());const auto a=start-center,b=current-center;
        qreal angle=(std::atan2(b.y(),b.x())-std::atan2(a.y(),a.x()))*180/M_PI;if(constrain)angle=std::round(angle/15)*15;
        QTransform delta;delta.translate(center.x(),center.y());delta.rotate(angle);delta.translate(-center.x(),-center.y());return o.transform*delta;
    }
    if(handle<0||handle>7)return o.transform*QTransform::fromTranslate(current.x()-start.x(),current.y()-start.y());
    const auto r=localBounds(o);const auto points=QVector<QPointF>{r.topLeft(),{r.center().x(),r.top()},r.topRight(),{r.right(),r.center().y()},r.bottomRight(),{r.center().x(),r.bottom()},r.bottomLeft(),{r.left(),r.center().y()}};
    const auto anchor=points[(handle+4)%8];const auto inverse=o.transform.inverted();
    const auto original=points[handle]-anchor,currentLocal=original+inverse.map(current)-inverse.map(start);
    qreal sx=qAbs(original.x())>1e-9?currentLocal.x()/original.x():1,sy=qAbs(original.y())>1e-9?currentLocal.y()/original.y():1;
    if(constrain&&handle%2==0){const qreal scale=qAbs(sx)>qAbs(sy)?sx:sy;sx=sy=scale;}
    if(qAbs(sx)<.01)sx=std::copysign(.01,sx);if(qAbs(sy)<.01)sy=std::copysign(.01,sy);
    QTransform delta;delta.translate(anchor.x(),anchor.y());delta.scale(sx,sy);delta.translate(-anchor.x(),-anchor.y());return delta*o.transform;
}
}
