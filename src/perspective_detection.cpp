#include "perspective_detection.h"
#include "guide_geometry.h"
#include <QPainter>
#include <QSet>
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <vector>

namespace toon {
namespace {
constexpr double pi = 3.14159265358979323846;
constexpr int thetaBins = 720;
constexpr int maxSegments = 160;
constexpr int maxCandidates = 6;
constexpr int maxAnalysisPixels = 2 * 1024 * 1024;
constexpr int maxEdges = 160000;
constexpr double angularTolerance = 1.6 * pi / 180;
bool finite(QPointF p) { return std::isfinite(p.x()) && std::isfinite(p.y()); }
double dot(QPointF a, QPointF b) { return a.x()*b.x()+a.y()*b.y(); }
double cross(QPointF a, QPointF b) { return a.x()*b.y()-a.y()*b.x(); }
double length(QPointF a) { return std::hypot(a.x(),a.y()); }
QPointF unit(QPointF p) { const double n=length(p);return n>1e-12?p/n:QPointF(); }
bool stopped(const PerspectiveCancellation &cancel) { return cancel && cancel(); }
PerspectiveDetectionResult cancelled(PerspectiveDetectionResult result) {
    result.cancelled=true;result.segments.clear();result.candidates.clear();result.message="Cancelled";return result;
}
struct Line {
    QPointF midpoint, direction, normal;
    double c=0, length=0, weight=0;
    int source=0;
};
double angularError(const Line &line, const PerspectiveCandidate &candidate) {
    const auto direction=candidate.parallel?candidate.direction:unit(candidate.point-line.midpoint);
    if(length(direction)<.5)return pi;
    return std::asin(std::clamp(std::abs(cross(line.direction,direction)),0.,1.));
}
bool independentGeometry(const QLineF &line) {
    return finite(line.p1())&&finite(line.p2())&&line.length()>1e-5
        && std::abs(line.x1())<=1000000&&std::abs(line.y1())<=1000000
        && std::abs(line.x2())<=1000000&&std::abs(line.y2())<=1000000;
}
bool sameSegment(const QLineF &a,const QLineF &b,double tolerance) {
    const auto d=unit(a.p2()-a.p1()),other=unit(b.p2()-b.p1()),normal=QPointF(-d.y(),d.x());
    if(std::abs(cross(d,other))>std::sin(1.3*pi/180))return false;
    if(std::abs(dot(normal,b.center()-a.center()))>tolerance)return false;
    const double b0=dot(d,b.p1()-a.p1()),b1=dot(d,b.p2()-a.p1());
    const double gap=std::max(0.,std::max(std::min(b0,b1),0.)-std::min(std::max(b0,b1),a.length()));
    return gap<std::max(8.,std::min(a.length(),b.length())*.12);
}
bool sameSupportingLine(const QLineF &a,const QLineF &b,double tolerance) {
    const auto direction=unit(a.p2()-a.p1()),other=unit(b.p2()-b.p1());
    return std::abs(cross(direction,other))<=std::sin(3*pi/180)
        &&std::abs(cross(direction,b.center()-a.center()))<=tolerance;
}
// Merge duplicate image contours before voting: two sides of one ink stroke
// must not count as independent evidence for a vanishing point.
void mergeSegment(QVector<PerspectiveSegment> &segments,PerspectiveSegment next,double tolerance) {
    for(auto &existing:segments)if(sameSegment(existing.line,next.line,tolerance)) {
        const auto d=unit(existing.line.p2()-existing.line.p1());
        const auto origin=(existing.line.center()*existing.line.length()+next.line.center()*next.line.length())/(existing.line.length()+next.line.length());
        const std::array<double,4> p{dot(d,existing.line.p1()-origin),dot(d,existing.line.p2()-origin),dot(d,next.line.p1()-origin),dot(d,next.line.p2()-origin)};
        const auto [lo,hi]=std::minmax_element(p.begin(),p.end());existing.line={origin+d*(*lo),origin+d*(*hi)};existing.weight=std::max(existing.weight,next.weight);return;
    }
    segments.append(next);
}
struct Evaluation {
    PerspectiveCandidate candidate;
    double score=0,totalWeight=0;
    int occupied=0;
    bool valid=false;
};
Evaluation evaluate(PerspectiveCandidate candidate,const QVector<Line> &lines,const QVector<int> &available,const QRectF &bounds) {
    Evaluation e;e.candidate=candidate;e.candidate.supportingSegments.clear();
    const double diagonal=std::hypot(bounds.width(),bounds.height());
    double error=0,total=0,minX=1e20,maxX=-1e20,minY=1e20,maxY=-1e20;
    QSet<int> bins;
    for(int i:available) {
        const auto &line=lines[i];const double angle=angularError(line,candidate);
        if(angle>angularTolerance)continue;
        const double weight=line.weight*(1-angle*angle/(angularTolerance*angularTolerance));
        e.score+=weight;total+=line.weight;error+=angle*line.weight;
        e.candidate.supportingSegments.append(line.source);
        minX=std::min(minX,line.midpoint.x());maxX=std::max(maxX,line.midpoint.x());minY=std::min(minY,line.midpoint.y());maxY=std::max(maxY,line.midpoint.y());
        const int x=std::clamp(int((line.midpoint.x()-bounds.x())*4/std::max(1.,bounds.width())),0,3),y=std::clamp(int((line.midpoint.y()-bounds.y())*4/std::max(1.,bounds.height())),0,3);bins.insert(y*4+x);
    }
    e.totalWeight=total;e.occupied=bins.size();
    e.valid=e.candidate.supportingSegments.size()>=4 && total>=diagonal*.55 && bins.size()>=3
        && std::hypot(maxX-minX,maxY-minY)>=diagonal*.22;
    e.candidate.score=e.score;e.candidate.meanAngularError=total>0?error/total*180/pi:180;
    e.candidate.strong=e.valid&&e.candidate.supportingSegments.size()>=6&&total>=diagonal&&bins.size()>=4&&e.candidate.meanAngularError<.7;
    return e;
}
PerspectiveCandidate refine(PerspectiveCandidate candidate,const QVector<Line> &lines,const QVector<int> &available) {
    for(int iteration=0;iteration<4;++iteration) {
        double a=0,b=0,d=0,bx=0,by=0,xx=0,xy=0;
        for(int i:available) {
            const auto &line=lines[i];const double error=angularError(line,candidate);if(error>angularTolerance)continue;
            double w=line.weight;
            if(candidate.parallel) {
                xx+=w*(line.direction.x()*line.direction.x()-line.direction.y()*line.direction.y());xy+=w*2*line.direction.x()*line.direction.y();
            }else {
                const double distance=length(candidate.point-line.midpoint);
                w/=std::max(distance*distance,line.length*line.length*.1);
                a+=w*line.normal.x()*line.normal.x();b+=w*line.normal.x()*line.normal.y();d+=w*line.normal.y()*line.normal.y();bx-=w*line.normal.x()*line.c;by-=w*line.normal.y()*line.c;
            }
        }
        if(candidate.parallel) {const double angle=.5*std::atan2(xy,xx);candidate.direction={std::cos(angle),std::sin(angle)};}
        else {const double det=a*d-b*b;if(det<1e-10*(a+d)*(a+d))break;const QPointF point((d*bx-b*by)/det,(a*by-b*bx)/det);if(!finite(point))break;candidate.point=point;}
    }
    return candidate;
}
void resultMessage(PerspectiveDetectionResult &r,PerspectiveMode mode) {
    int finiteCount=0;bool strong=false;for(const auto &c:r.candidates){finiteCount+=!c.parallel;strong|=c.strong;}
    if(r.candidates.isEmpty())r.message="Not enough consistent straight edges. Try a smaller region or draw two reference lines.";
    else if(mode==PerspectiveMode::TwoPoint&&finiteCount<2)r.message="Two finite vanishing points were not found. Review a one-point or parallel guide, or draw reference lines.";
    else if(mode==PerspectiveMode::OnePoint&&!finiteCount)r.message="Parallel directions found. Draw converging reference lines to add a vanishing point.";
    else r.message=strong?"Strong straight-edge match. Review the proposed directions before adding guides.":"Review suggested. Adjust the proposed directions or draw reference lines.";
}
}

PerspectiveDetectionResult fitPerspectiveSegments(const QVector<PerspectiveSegment> &input,QSize sourceSize,
                                                  const PerspectiveDetectionSettings &settings,const PerspectiveCancellation &cancel) {
    PerspectiveDetectionResult result;result.sourceSize=sourceSize;
    if(sourceSize.isEmpty()||sourceSize.width()>32768||sourceSize.height()>32768||input.size()>maxSegments) {result.error="Perspective analysis exceeds the supported image or segment limits.";return result;}
    if(stopped(cancel))return cancelled(result);
    result.segments=input;
    QVector<Line> lines;lines.reserve(input.size());
    for(int i=0;i<input.size();++i) {
        const auto &s=input[i];if(!s.enabled)continue;
        if(!independentGeometry(s.line)||!std::isfinite(s.weight)||s.weight<=0||s.weight>1000){result.error="Invalid perspective line geometry.";result.segments.clear();return result;}
        int duplicate=-1;for(int k=0;k<lines.size();++k)if(sameSupportingLine(input[lines[k].source].line,s.line,4)){duplicate=k;break;}
        if(duplicate>=0&&lines[duplicate].length>=s.line.length())continue;
        const auto direction=unit(s.line.p2()-s.line.p1()),normal=QPointF(-direction.y(),direction.x());
        const Line line{s.line.center(),direction,normal,-dot(normal,s.line.center()),s.line.length(),s.line.length()*std::clamp(double(s.weight),.05,2.),i};
        if(duplicate>=0)lines[duplicate]=line;else lines.append(line);
    }
    QVector<int> available;for(int i=0;i<lines.size();++i)available.append(i);
    const QRectF analysisBounds=settings.region.isEmpty()?QRectF(QPointF(),sourceSize):QRectF(settings.region.intersected(QRect(QPoint(),sourceSize)));
    if(analysisBounds.isEmpty()){result.error="Choose a region inside the source image.";return result;}
    const double diagonal=std::hypot(analysisBounds.width(),analysisBounds.height());
    for(int family=0;family<maxCandidates&&available.size()>=4;++family) {
        Evaluation best;
        for(int a=0;a<available.size();++a) {
            if(stopped(cancel))return cancelled(result);
            const auto &first=lines[available[a]];
            PerspectiveCandidate parallel;parallel.parallel=true;parallel.direction=first.direction;
            auto e=evaluate(parallel,lines,available,analysisBounds);if(e.valid&&e.score>best.score)best=e;
            for(int b=a+1;b<available.size();++b) {
                const auto &second=lines[available[b]];const double denominator=cross(first.direction,second.direction);
                if(std::abs(denominator)<std::sin(1.8*pi/180))continue;
                const auto point=first.midpoint+first.direction*(cross(second.midpoint-first.midpoint,second.direction)/denominator);
                if(!finite(point)||length(point-analysisBounds.center())>diagonal*20)continue;
                PerspectiveCandidate candidate;candidate.point=point;
                e=evaluate(candidate,lines,available,analysisBounds);if(e.valid&&e.score>best.score*1.015)best=e;
            }
        }
        if(!best.valid)break;
        auto improved=evaluate(refine(best.candidate,lines,available),lines,available,analysisBounds);
        if(improved.valid&&improved.score>=best.score*.95)best=improved;
        double remainingWeight=0;for(int i:available)remainingWeight+=lines[i].weight;
        if(best.score<remainingWeight*.12)break;
        result.candidates.append(best.candidate);
        const QSet<int> used(best.candidate.supportingSegments.begin(),best.candidate.supportingSegments.end());
        available.erase(std::remove_if(available.begin(),available.end(),[&](int i){return used.contains(lines[i].source);}),available.end());
    }
    std::stable_sort(result.candidates.begin(),result.candidates.end(),[](const auto &a,const auto &b){return a.score>b.score;});
    resultMessage(result,settings.mode);return result;
}

PerspectiveDetectionResult detectPerspective(const QImage &source,const PerspectiveDetectionSettings &settings,
                                              const PerspectiveCancellation &cancel) {
    PerspectiveDetectionResult result;result.sourceSize=source.size();
    if(source.isNull()||source.width()>32768||source.height()>32768||qint64(source.width())*source.height()>128LL*1024*1024
        ||!std::isfinite(settings.sensitivity)||settings.sensitivity<0||settings.sensitivity>1){result.error="Choose a supported image for perspective detection.";return result;}
    if(stopped(cancel))return cancelled(result);
    const auto region=settings.region.isEmpty()?source.rect():settings.region.intersected(source.rect());
    if(region.width()<24||region.height()<24){result.message="Choose an image region at least 24 pixels wide and high.";return result;}
    const double scale=std::min({1.,2048./std::max(region.width(),region.height()),std::sqrt(double(maxAnalysisPixels)/(qint64(region.width())*region.height()))});
    const int w=std::max(1,qRound(region.width()*scale)),h=std::max(1,qRound(region.height()*scale));
    try {
        QImage image(w,h,QImage::Format_ARGB32_Premultiplied);if(image.isNull()){result.error="Not enough memory for perspective analysis.";return result;}
        image.fill(Qt::white);{QPainter p(&image);p.setRenderHint(QPainter::SmoothPixmapTransform);p.drawImage(image.rect(),source,region);}
        const int n=w*h;
        std::vector<uchar> gray(n),horizontal(n),blurred(n);std::vector<float> magnitude(n);std::vector<ushort> orientation(n);
        for(int y=0;y<h;++y){if(stopped(cancel))return cancelled(result);const auto *row=reinterpret_cast<const QRgb*>(image.constScanLine(y));for(int x=0;x<w;++x)gray[y*w+x]=uchar((qRed(row[x])*77+qGreen(row[x])*150+qBlue(row[x])*29+128)>>8);}
        for(int y=0;y<h;++y)for(int x=1;x<w-1;++x)horizontal[y*w+x]=uchar((gray[y*w+x-1]+2*gray[y*w+x]+gray[y*w+x+1]+2)/4);
        for(int y=1;y<h-1;++y)for(int x=1;x<w-1;++x)blurred[y*w+x]=uchar((horizontal[(y-1)*w+x]+2*horizontal[y*w+x]+horizontal[(y+1)*w+x]+2)/4);
        std::array<int,1024> histogram{};int nonzero=0;
        for(int y=2;y<h-2;++y) {
            if(stopped(cancel))return cancelled(result);
            for(int x=2;x<w-2;++x) {
                const int i=y*w+x,gx=-blurred[i-w-1]+blurred[i-w+1]-2*blurred[i-1]+2*blurred[i+1]-blurred[i+w-1]+blurred[i+w+1],gy=-blurred[i-w-1]-2*blurred[i-w]-blurred[i-w+1]+blurred[i+w-1]+2*blurred[i+w]+blurred[i+w+1];
                const double m=std::hypot(gx,gy);magnitude[i]=float(m);if(m>0){++histogram[std::clamp(int(m),0,1023)];++nonzero;}
                double theta=std::atan2(gy,gx);if(theta<0)theta+=pi;if(theta>=pi)theta-=pi;orientation[i]=ushort(qRound(theta/pi*thetaBins)%thetaBins);
            }
        }
        int percentile=0,count=0;for(;percentile<1023;++percentile){count+=histogram[percentile];if(count>=nonzero*.7)break;}
        const double threshold=std::max(24.+(1-settings.sensitivity)*32,percentile*.5);
        struct Edge { float x,y,strength;int theta; };std::vector<Edge> edges;edges.reserve(std::min(n/10,maxEdges));
        for(int y=3;y<h-3;++y) {
            if(stopped(cancel))return cancelled(result);
            for(int x=3;x<w-3;++x) {
                const int i=y*w+x;const double m=magnitude[i];if(m<threshold)continue;
                const int angle=((int(orientation[i])*4+thetaBins/2)/thetaBins)%4;int delta=1;if(angle==1)delta=w+1;else if(angle==2)delta=w;else if(angle==3)delta=w-1;
                if(m<magnitude[i-delta]||m<magnitude[i+delta])continue;
                edges.push_back({float(x),float(y),float(std::min(3.,m/100.)),int(orientation[i])});
            }
        }
        if(edges.size()>size_t(maxEdges)){const size_t stride=(edges.size()+maxEdges-1)/maxEdges;size_t out=0;for(size_t i=0;i<edges.size();i+=stride)edges[out++]=edges[i];edges.resize(out);}
        const double cx=(w-1)*.5,cy=(h-1)*.5;const int rhoHalf=int(std::ceil(std::hypot(w,h)*.5))+3,rhoBins=2*rhoHalf+1;
        std::vector<float> votes(size_t(thetaBins)*rhoBins,0);std::array<double,thetaBins> cosine{},sine{};
        for(int theta=0;theta<thetaBins;++theta){cosine[theta]=std::cos(theta*pi/thetaBins);sine[theta]=std::sin(theta*pi/thetaBins);}
        for(size_t i=0;i<edges.size();++i) {
            if((i&1023)==0&&stopped(cancel))return cancelled(result);const auto &edge=edges[i];
            for(int offset=-6;offset<=6;++offset){const int theta=(edge.theta+offset+thetaBins)%thetaBins,rho=qRound((edge.x-cx)*cosine[theta]+(edge.y-cy)*sine[theta])+rhoHalf;if(rho>=0&&rho<rhoBins)votes[size_t(theta)*rhoBins+rho]+=edge.strength;}
        }
        struct Peak{int theta,rho;float score;};std::vector<Peak> peaks;
        const double minimumLength=std::max(24.,std::min(w,h)*.065),minimumVote=std::max(16.,minimumLength*.7);
        for(int theta=0;theta<thetaBins;++theta)for(int rho=1;rho<rhoBins-1;++rho) {
            const float score=votes[size_t(theta)*rhoBins+rho];if(score<minimumVote)continue;
            bool maximum=true;for(int dt=-1;dt<=1&&maximum;++dt)for(int dr=-1;dr<=1;++dr){if(!dt&&!dr)continue;const int t=(theta+dt+thetaBins)%thetaBins;const int r=(theta+dt<0||theta+dt>=thetaBins)?2*rhoHalf-(rho+dr):rho+dr;if(r>=0&&r<rhoBins&&votes[size_t(t)*rhoBins+r]>score){maximum=false;break;}}
            if(maximum)peaks.push_back({theta,rho,score});
        }
        std::sort(peaks.begin(),peaks.end(),[](const auto &a,const auto &b){if(a.score!=b.score)return a.score>b.score;if(a.theta!=b.theta)return a.theta<b.theta;return a.rho<b.rho;});
        if(peaks.size()>180)peaks.resize(180);
        QVector<PerspectiveSegment> segments;
        for(const auto &peak:peaks) {
            if(stopped(cancel))return cancelled(result);
            const QPointF normal(cosine[peak.theta],sine[peak.theta]),direction(-normal.y(),normal.x());
            struct Along{double position;QPointF point;double strength;};std::vector<Along> aligned;
            for(const auto &edge:edges) {
                int angle=std::abs(edge.theta-peak.theta);angle=std::min(angle,thetaBins-angle);if(angle>32)continue;
                const QPointF point(edge.x-cx,edge.y-cy);if(std::abs(dot(normal,point)-(peak.rho-rhoHalf))>1.8)continue;
                aligned.push_back({dot(direction,point),point,edge.strength});
            }
            std::sort(aligned.begin(),aligned.end(),[](const auto&a,const auto&b){return a.position<b.position;});
            for(size_t begin=0;begin<aligned.size();) {
                size_t end=begin+1;while(end<aligned.size()&&aligned[end].position-aligned[end-1].position<=std::max(4.,minimumLength*.1))++end;
                if(end-begin>=size_t(minimumLength*.4)&&aligned[end-1].position-aligned[begin].position>=minimumLength) {
                    QPointF mean;double strength=0;for(size_t i=begin;i<end;++i){mean+=aligned[i].point;strength+=aligned[i].strength;}mean/=double(end-begin);
                    double xx=0,xy=0,yy=0;for(size_t i=begin;i<end;++i){const auto p=aligned[i].point-mean;xx+=p.x()*p.x();xy+=p.x()*p.y();yy+=p.y()*p.y();}
                    const double angle=.5*std::atan2(2*xy,xx-yy);const QPointF fitted(std::cos(angle),std::sin(angle));double low=1e20,high=-1e20;
                    for(size_t i=begin;i<end;++i){const double value=dot(fitted,aligned[i].point-mean);low=std::min(low,value);high=std::max(high,value);}
                    const QPointF origin(cx,cy);const auto map=[&](QPointF p){return region.topLeft()+QPointF((p.x()+.5)*region.width()/w,(p.y()+.5)*region.height()/h);};
                    PerspectiveSegment segment{{map(origin+mean+fitted*low),map(origin+mean+fitted*high)},std::clamp(strength/(end-begin)/3.,.1,1.),true};
                    mergeSegment(segments,segment,3.5/scale);
                }
                begin=end;
            }
        }
        std::sort(segments.begin(),segments.end(),[](const auto&a,const auto&b){return a.line.length()*a.weight>b.line.length()*b.weight;});
        if(segments.size()>maxSegments)segments.resize(maxSegments);
        return fitPerspectiveSegments(segments,source.size(),settings,cancel);
    }catch(const std::bad_alloc &){result.error="Not enough memory for perspective analysis.";return result;}
}

std::optional<PerspectiveCandidate> perspectiveFromLines(const QLineF &a,const QLineF &b) {
    if(!independentGeometry(a)||!independentGeometry(b))return {};
    const auto da=unit(a.p2()-a.p1()),db=unit(b.p2()-b.p1());const double denominator=cross(da,db);
    PerspectiveCandidate result;result.supportingSegments={0,1};
    if(std::abs(denominator)<1e-6) {
        if(std::abs(cross(da,b.center()-a.center()))<1e-4)return {};
        result.parallel=true;result.direction=da;return result;
    }
    result.point=a.p1()+da*(cross(b.p1()-a.p1(),db)/denominator);
    if(!finite(result.point)||std::abs(result.point.x())>1000000||std::abs(result.point.y())>1000000)return {};
    return result;
}
QVector<DrawingGuide> createPerspectiveGuides(const QVector<PerspectiveCandidate> &candidates,const QVector<int> &chosen,
                                            QPointF anchor,const QRectF &bounds,PerspectiveMode mode) {
    if(!finite(anchor)||bounds.isEmpty())return {};
    QVector<PerspectiveCandidate> selected;QSet<int> visited;
    for(int i:chosen)if(i>=0&&i<candidates.size()&&!visited.contains(i)){visited.insert(i);selected.append(candidates[i]);}
    QVector<DrawingGuide> guides;QVector<PerspectiveCandidate> finiteCandidates;
    for(const auto &c:selected){
        if(c.parallel){if(!finite(c.direction)||length(c.direction)<1e-5)return {};}
        else {if(!finite(c.point)||std::abs(c.point.x())>1000000||std::abs(c.point.y())>1000000)return {};finiteCandidates.append(c);}
    }
    if(finiteCandidates.size()>(mode==PerspectiveMode::OnePoint?1:2))return {};
    if(mode==PerspectiveMode::TwoPoint&&finiteCandidates.size()<2)return {};
    if(mode==PerspectiveMode::OnePoint&&finiteCandidates.isEmpty())return {};
    if(finiteCandidates.size()>=2&&mode!=PerspectiveMode::OnePoint) {
        const auto first=finiteCandidates[0].point,second=finiteCandidates[1].point;const auto axis=unit(second-first);if(length(axis)<.5)return {};
        if(std::abs(cross(axis,anchor-first))<std::max(1.,bounds.height()*.01))anchor+=QPointF(-axis.y(),axis.x())*std::max(16.,bounds.height()*.2);
        DrawingGuide g;g.name="Detected two-point perspective";g.type=GuideType::TwoPoint;g.points={first,second,anchor};g.vertical=false;g.area=bounds;guides.append(g);
    }else if(!finiteCandidates.isEmpty()) {
        DrawingGuide g;g.name="Detected vanishing point";g.type=GuideType::VanishingPoint;g.points={finiteCandidates[0].point};g.area=bounds;guides.append(g);
    }
    for(const auto &c:selected)if(c.parallel&&finite(c.direction)&&length(c.direction)>1e-5) {
        DrawingGuide g;g.name="Detected parallel direction";g.type=GuideType::ParallelRuler;g.points={anchor,anchor+unit(c.direction)*std::max(16.,std::min(bounds.width(),bounds.height())*.4)};g.area=bounds;guides.append(g);
    }
    for(const auto &g:guides)if(!validGuideGeometry(g))return {};
    return guides;
}
}
