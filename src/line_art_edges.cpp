#include "line_art_edges.h"
#include <QPainter>
#include <QPainterPath>
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <vector>

namespace toon {
namespace {
using Field = std::vector<float>;
bool stopped(const LineArtCancellation &c) { return c && c->load(std::memory_order_relaxed); }
float at(const Field &f, int w, int h, int x, int y) {
    return f[size_t(qBound(0,y,h-1))*w+qBound(0,x,w-1)];
}
float sample(const Field &f, int w, int h, float x, float y) {
    x=qBound(0.f,x,float(w-1)); y=qBound(0.f,y,float(h-1));
    const int ix=int(x),iy=int(y); const float dx=x-ix,dy=y-iy;
    return (at(f,w,h,ix,iy)*(1-dx)+at(f,w,h,ix+1,iy)*dx)*(1-dy)
        +(at(f,w,h,ix,iy+1)*(1-dx)+at(f,w,h,ix+1,iy+1)*dx)*dy;
}
Field gaussian(const Field &input,int w,int h,double sigma,const LineArtCancellation &cancel) {
    const int radius=qBound(1,int(std::ceil(sigma*2.5)),10);
    Field kernel(2*radius+1),temp(input.size()),out(input.size()); double sum=0;
    for(int i=-radius;i<=radius;++i){kernel[i+radius]=std::exp(-i*i/(2*sigma*sigma));sum+=kernel[i+radius];}
    for(auto &weight:kernel)weight/=sum;
    for(int y=0;y<h&&!stopped(cancel);++y)for(int x=0;x<w;++x){
        float value=0;for(int k=-radius;k<=radius;++k)value+=at(input,w,h,x+k,y)*kernel[k+radius];temp[size_t(y)*w+x]=value;
    }
    for(int y=0;y<h&&!stopped(cancel);++y)for(int x=0;x<w;++x){
        float value=0;for(int k=-radius;k<=radius;++k)value+=at(temp,w,h,x,y+k)*kernel[k+radius];out[size_t(y)*w+x]=value;
    }
    return out;
}
// Separable bilateral passes remove small tonal variations while retaining
// stronger boundaries. Range weights are tabled; no per-sample exponentials.
Field suppressTexture(Field input,int w,int h,double amount,double scale,const LineArtCancellation &cancel) {
    if(amount<=0)return input;
    const double spatial=qMax(.6,(.9+amount*.023)*scale),range=2+amount*.18;
    const int radius=qBound(1,int(std::ceil(spatial*2)),8);
    std::array<float,256> tone{};Field spatialWeights(2*radius+1),output(input.size());
    for(int i=0;i<256;++i)tone[i]=std::exp(-i*i/(2*range*range));
    for(int i=-radius;i<=radius;++i)spatialWeights[i+radius]=std::exp(-i*i/(2*spatial*spatial));
    for(int pass=0;pass<(amount>=35?4:2)&&!stopped(cancel);++pass){
        const bool horizontal=pass%2==0;
        for(int y=0;y<h&&!stopped(cancel);++y)for(int x=0;x<w;++x){
            const float center=input[size_t(y)*w+x];float total=0,weight=0;
            for(int k=-radius;k<=radius;++k){
                const float value=at(input,w,h,x+(horizontal?k:0),y+(horizontal?0:k));
                const float a=spatialWeights[k+radius]*tone[qBound(0,qRound(std::abs(value-center)),255)];
                total+=value*a;weight+=a;
            }
            output[size_t(y)*w+x]=total/qMax(weight,1e-8f);
        }
        input.swap(output);
    }
    return input;
}
struct Flow { Field nx,ny,coherence; };
Flow edgeFlow(const Field &values,int w,int h,double scale,const LineArtCancellation &cancel) {
    Flow flow{Field(values.size()),Field(values.size()),Field(values.size())};
    for(int y=0;y<h&&!stopped(cancel);++y)for(int x=0;x<w;++x){
        const float gx=(at(values,w,h,x+1,y-1)+2*at(values,w,h,x+1,y)+at(values,w,h,x+1,y+1)
            -at(values,w,h,x-1,y-1)-2*at(values,w,h,x-1,y)-at(values,w,h,x-1,y+1))*.125f;
        const float gy=(at(values,w,h,x-1,y+1)+2*at(values,w,h,x,y+1)+at(values,w,h,x+1,y+1)
            -at(values,w,h,x-1,y-1)-2*at(values,w,h,x,y-1)-at(values,w,h,x+1,y-1))*.125f;
        const size_t i=size_t(y)*w+x;flow.nx[i]=gx*gx;flow.ny[i]=gx*gy;flow.coherence[i]=gy*gy;
    }
    const double sigma=qMax(.65,1.2*scale);
    flow.nx=gaussian(flow.nx,w,h,sigma,cancel);flow.ny=gaussian(flow.ny,w,h,sigma,cancel);flow.coherence=gaussian(flow.coherence,w,h,sigma,cancel);
    for(size_t i=0;i<values.size()&&!stopped(cancel);++i){
        const float xx=flow.nx[i],xy=flow.ny[i],yy=flow.coherence[i],trace=xx+yy;
        const float angle=.5f*std::atan2(2*xy,xx-yy);
        flow.nx[i]=std::cos(angle);flow.ny[i]=std::sin(angle);
        flow.coherence[i]=trace<.025f?-1:std::hypot(xx-yy,2*xy)/trace;
    }
    return flow;
}
Field coherentResponse(const Field &values,const Flow &flow,int w,int h,double smooth,double scale,const LineArtCancellation &cancel) {
    const double small=qMax(.45,(.65+.12*smooth)*scale),large=small*1.8;
    const int radius=qBound(2,int(std::ceil(large*2.5)),10);
    Field weights(2*radius+1);double sumSmall=0,sumLarge=0;
    for(int k=-radius;k<=radius;++k){sumSmall+=std::exp(-k*k/(2*small*small));sumLarge+=std::exp(-k*k/(2*large*large));}
    for(int k=-radius;k<=radius;++k)weights[k+radius]=std::exp(-k*k/(2*large*large))/sumLarge-std::exp(-k*k/(2*small*small))/sumSmall;
    Field response(values.size());
    const auto cross=[&](int x,int y,float nx,float ny){float value=0;for(int k=-radius;k<=radius;++k)value+=sample(values,w,h,x+k*nx,y+k*ny)*weights[k+radius];return value;};
    for(int y=0;y<h&&!stopped(cancel);++y)for(int x=0;x<w;++x){
        const size_t i=size_t(y)*w+x;if(flow.coherence[i]<0)continue;
        const float nx=flow.nx[i],ny=flow.ny[i];float value=cross(x,y,nx,ny);
        if(flow.coherence[i]<.2f)value=(value+cross(x,y,-ny,nx))*.5f;
        response[i]=value;
    }
    if(smooth<=0||stopped(cancel))return response;
    Field coherent(response.size());const int length=qBound(1,qRound((1+smooth*.8)*scale),6);
    for(int y=0;y<h&&!stopped(cancel);++y)for(int x=0;x<w;++x){
        const size_t i=size_t(y)*w+x;
        if(flow.coherence[i]<.2f){coherent[i]=response[i];continue;}
        float total=response[i],weight=1;
        for(int sign:{-1,1}){
            float px=x,py=y,tx=-flow.ny[i]*sign,ty=flow.nx[i]*sign;
            for(int k=1;k<=length;++k){
                const int xx=qBound(0,qRound(px),w-1),yy=qBound(0,qRound(py),h-1);const size_t next=size_t(yy)*w+xx;
                if(flow.coherence[next]<.15f)break;
                float vx=-flow.ny[next],vy=flow.nx[next];if(vx*tx+vy*ty<0){vx=-vx;vy=-vy;}
                px+=vx;py+=vy;tx=vx;ty=vy;if(px<0||py<0||px>w-1||py>h-1)break;
                const float a=std::exp(-k*k/(2.f*length*length));total+=sample(response,w,h,px,py)*a;weight+=a;
            }
        }
        coherent[i]=total/weight;
    }
    return coherent;
}
using Pixels=std::vector<uchar>;
Pixels connectedInk(const Field &response,int w,int h,double detail,int fragments,const LineArtCancellation &cancel) {
    const float high=float(.8+18*std::pow(1-detail/100.,2)),low=high*.45f;
    Pixels state(response.size());std::vector<int> queue;queue.reserve(65536);
    for(size_t i=0;i<response.size();++i){if(response[i]>=high){state[i]=2;queue.push_back(int(i));}else if(response[i]>=low)state[i]=1;}
    for(size_t p=0;p<queue.size()&&!stopped(cancel);++p){
        const int x=queue[p]%w,y=queue[p]/w;
        for(int yy=qMax(0,y-1);yy<=qMin(h-1,y+1);++yy)for(int xx=qMax(0,x-1);xx<=qMin(w-1,x+1);++xx){const int i=yy*w+xx;if(state[i]==1){state[i]=2;queue.push_back(i);}}
    }
    for(auto &v:state)v=v==2;
    if(fragments<=1)return state;
    Pixels visited(state.size());
    for(int seed=0;seed<int(state.size())&&!stopped(cancel);++seed){
        if(!state[seed]||visited[seed])continue;queue.clear();queue.push_back(seed);visited[seed]=1;
        for(size_t p=0;p<queue.size();++p){
            if((p&4095)==0&&stopped(cancel))return state;
            const int x=queue[p]%w,y=queue[p]/w;
            for(int yy=qMax(0,y-1);yy<=qMin(h-1,y+1);++yy)for(int xx=qMax(0,x-1);xx<=qMin(w-1,x+1);++xx){const int i=yy*w+xx;if(state[i]&&!visited[i]){visited[i]=1;queue.push_back(i);}}
        }
        if(int(queue.size())<fragments)for(int i:queue)state[i]=0;
    }
    return state;
}
void thin(Pixels &pixels,int w,int h,const LineArtCancellation &cancel) {
    const auto get=[&](int x,int y){return x>=0&&y>=0&&x<w&&y<h&&pixels[size_t(y)*w+x];};
    std::vector<int> remove;remove.reserve(65536);
    for(int iteration=0;iteration<10&&!stopped(cancel);++iteration){
        bool changed=false;
        for(int phase=0;phase<2;++phase){
            remove.clear();
            for(int y=0;y<h&&!stopped(cancel);++y)for(int x=0;x<w;++x){
                if(!get(x,y))continue;
                const std::array<int,8> p{{get(x,y-1),get(x+1,y-1),get(x+1,y),get(x+1,y+1),get(x,y+1),get(x-1,y+1),get(x-1,y),get(x-1,y-1)}};
                int count=0,transitions=0;for(int k=0;k<8;++k){count+=p[k];transitions+=!p[k]&&p[(k+1)%8];}
                if(count<2||count>6||transitions!=1)continue;
                if(phase==0?(p[0]*p[2]*p[4]||p[2]*p[4]*p[6]):(p[0]*p[2]*p[6]||p[0]*p[4]*p[6]))continue;
                remove.push_back(y*w+x);
            }
            for(int i:remove){
                // Parallel thinning can erase a tiny 2 × 2 mark completely.
                // Retain endpoints as this phase is applied so small artwork
                // survives as a short stroke instead of disappearing.
                const int x=i%w,y=i/w;int remaining=0;
                for(int yy=y-1;yy<=y+1;++yy)for(int xx=x-1;xx<=x+1;++xx)
                    if(xx!=x||yy!=y)remaining+=get(xx,yy);
                if(remaining>1){pixels[i]=0;changed=true;}
            }
        }
        if(!changed)break;
    }
}
void pruneWeakIslands(Pixels &pixels,const Field &response,int w,int h,double detail,double denoise,double scale,const LineArtCancellation &cancel) {
    if(denoise<=0)return;
    const int length=qMax(1,qRound((2+denoise/12)*scale));
    const float peakLimit=float((.8+18*std::pow(1-detail/100.,2))*(2.3+denoise*.008));
    Pixels visited(pixels.size());std::vector<int> region;
    for(int seed=0;seed<int(pixels.size())&&!stopped(cancel);++seed){
        if(!pixels[seed]||visited[seed])continue;region.clear();region.push_back(seed);visited[seed]=1;float peak=response[seed];
        for(size_t p=0;p<region.size();++p){
            if((p&4095)==0&&stopped(cancel))return;
            const int x=region[p]%w,y=region[p]/w;peak=qMax(peak,response[region[p]]);
            for(int yy=qMax(0,y-1);yy<=qMin(h-1,y+1);++yy)for(int xx=qMax(0,x-1);xx<=qMin(w-1,x+1);++xx){const int i=yy*w+xx;if(pixels[i]&&!visited[i]){visited[i]=1;region.push_back(i);}}
        }
        // Keep long faint contours and strong tiny details; discard short,
        // low-contrast islands left by grain rather than all small artwork.
        if(int(region.size())<=length&&peak<peakLimit)for(int i:region)pixels[i]=0;
    }
}
std::vector<QPointF> simplify(const std::vector<QPointF> &points,double tolerance) {
    if(points.size()<3||tolerance<=0)return points;
    std::vector<uchar> keep(points.size());keep.front()=keep.back()=1;
    std::vector<std::pair<int,int>> stack{{0,int(points.size())-1}};
    while(!stack.empty()){
        const auto [first,last]=stack.back();stack.pop_back();const auto a=points[first],delta=points[last]-a;
        const double squared=delta.x()*delta.x()+delta.y()*delta.y();double best=tolerance*tolerance;int index=-1;
        for(int i=first+1;i<last;++i){const auto d=points[i]-a;const double t=squared>1e-12?qBound(0.,(d.x()*delta.x()+d.y()*delta.y())/squared,1.):0;const auto gap=d-delta*t;const double distance=gap.x()*gap.x()+gap.y()*gap.y();if(distance>best){best=distance;index=i;}}
        if(index>=0){keep[index]=1;stack.push_back({first,index});stack.push_back({index,last});}
    }
    std::vector<QPointF> out;for(size_t i=0;i<points.size();++i)if(keep[i])out.push_back(points[i]);return out;
}
void appendStroke(QPainterPath &path,const std::vector<QPointF> &raw,double smooth,double scale) {
    const auto points=simplify(raw,(.18+.18*smooth)*scale);if(points.empty())return;
    path.moveTo(points.front());
    if(points.size()==1){path.lineTo(points.front()+QPointF(.001,0));return;}
    for(size_t i=1;i+1<points.size();++i){
        const auto in=points[i]-points[i-1],out=points[i+1]-points[i];const double a=std::hypot(in.x(),in.y()),b=std::hypot(out.x(),out.y());
        if(a>1e-6&&b>1e-6&&smooth>0&&(in.x()*out.x()+in.y()*out.y())/(a*b)>.5){
            const double radius=qMin(qMin(a,b)*.3,smooth*.35*scale);
            path.lineTo(points[i]-in*(radius/a));path.quadTo(points[i],points[i]+out*(radius/b));
        }else path.lineTo(points[i]);
    }
    path.lineTo(points.back());
}
QImage drawInk(const Pixels &pixels,const Field &response,const Flow &flow,int w,int h,const LineArtSettings &settings,double scale,const LineArtCancellation &cancel) {
    QImage painted(w,h,QImage::Format_ARGB32_Premultiplied);painted.fill(Qt::transparent);
    QPainter painter(&painted);painter.setRenderHint(QPainter::Antialiasing);painter.setPen(QPen(Qt::white,settings.width*scale,Qt::SolidLine,Qt::RoundCap,Qt::RoundJoin));
    const std::array<QPoint,8> directions{{{0,-1},{1,-1},{1,0},{1,1},{0,1},{-1,1},{-1,0},{-1,-1}}};
    const auto get=[&](int x,int y){return x>=0&&y>=0&&x<w&&y<h&&pixels[size_t(y)*w+x];};
    const auto neighbors=[&](int i){uchar bits=0;const int x=i%w,y=i/w;for(int d=0;d<8;++d){const auto v=directions[d];if(!get(x+v.x(),y+v.y()))continue;if(v.x()&&v.y()&&(get(x+v.x(),y)||get(x,y+v.y())))continue;bits|=uchar(1<<d);}return bits;};
    const auto position=[&](int i){const float x=i%w,y=i/w,nx=flow.nx[i],ny=flow.ny[i];const float a=sample(response,w,h,x-nx,y-ny),b=response[i],d=sample(response,w,h,x+nx,y+ny),denominator=a-2*b+d;const float offset=denominator<-.01f?qBound(-.45f,.5f*(a-d)/denominator,.45f):0;return QPointF(x+.5+nx*offset,y+.5+ny*offset);};
    Pixels visited(pixels.size());QPainterPath batch;int batchPoints=0;
    const auto flush=[&]{painter.drawPath(batch);batch=QPainterPath();batchPoints=0;};
    const auto walk=[&](int start,int direction){
        std::vector<QPointF> points;points.reserve(4097);points.push_back(position(start));int current=start,d=direction;
        for(;;){
            visited[current]|=uchar(1<<d);const auto v=directions[d];const int next=current+v.y()*w+v.x();visited[next]|=uchar(1<<((d+4)%8));points.push_back(position(next));
            const uchar options=neighbors(next),remaining=options&~visited[next];
            const bool endpoint=std::popcount(unsigned(options))!=2||remaining==0;
            if(endpoint||points.size()>=4096||stopped(cancel)){
                appendStroke(batch,points,settings.smoothing,scale);batchPoints+=int(points.size());
                if(batchPoints>=32768)flush();points.clear();points.push_back(position(next));
            }
            if(endpoint||stopped(cancel))break;
            current=next;d=std::countr_zero(unsigned(remaining));
        }
    };
    for(int pass=0;pass<2&&!stopped(cancel);++pass)for(int i=0;i<int(pixels.size())&&!stopped(cancel);++i){
        if(!pixels[i])continue;const uchar options=neighbors(i);const int degree=std::popcount(unsigned(options));
        if(pass==0&&degree==2)continue;
        if(degree==0&&!visited[i]){appendStroke(batch,{position(i)},settings.smoothing,scale);visited[i]=255;++batchPoints;}
        for(int d=0;d<8;++d)if((options&(1<<d))&&!(visited[i]&(1<<d)))walk(i,d);
    }
    flush();painter.end();QImage mask(w,h,QImage::Format_Grayscale8);mask.fill(0);
    for(int y=0;y<h&&!stopped(cancel);++y){const auto *row=reinterpret_cast<const QRgb *>(painted.constScanLine(y));auto *out=mask.scanLine(y);for(int x=0;x<w;++x)out[x]=qAlpha(row[x]);}
    return mask;
}
}
QImage extractLineArtInk(const QImage &source,const LineArtSettings &settings,double scale,const LineArtCancellation &cancel) {
    const int w=source.width(),h=source.height();Field values(size_t(w)*h);
    for(int y=0;y<h&&!stopped(cancel);++y){const auto *row=reinterpret_cast<const QRgb *>(source.constScanLine(y));for(int x=0;x<w;++x){const auto pixel=qUnpremultiply(row[x]);const float alpha=qAlpha(pixel)/255.f;values[size_t(y)*w+x]=(qRed(pixel)*.2126f+qGreen(pixel)*.7152f+qBlue(pixel)*.0722f)*alpha+255*(1-alpha);}}
    values=suppressTexture(std::move(values),w,h,settings.textureSuppression,scale,cancel);
    if(stopped(cancel))return {};
    const auto flow=edgeFlow(values,w,h,scale,cancel);
    if(stopped(cancel))return {};
    const auto response=coherentResponse(values,flow,w,h,settings.smoothing,scale,cancel);
    if(stopped(cancel))return {};
    auto pixels=connectedInk(response,w,h,settings.detail,qMax(1,qRound(settings.fragments*scale*scale)),cancel);
    thin(pixels,w,h,cancel);
    pruneWeakIslands(pixels,response,w,h,settings.detail,settings.textureSuppression,scale,cancel);
    if(stopped(cancel))return {};
    return drawInk(pixels,response,flow,w,h,settings,scale,cancel);
}
}
