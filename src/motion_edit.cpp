#include "motion_edit.h"
#include <algorithm>
#include <array>
#include <QSet>
namespace toon {
namespace {
PropertyChannel *channel(LayerAnimation &a, KeyframeProperty p) {
    for (auto &c : a.channels) if(c.property == p) return &c;
    return nullptr;
}
ScalarKeyframe *key(PropertyChannel &c,int frame) {
    for(auto &k:c.keys) if(k.frame==frame) return &k;
    return nullptr;
}
QPointF mix(QPointF a,QPointF b,qreal t){return a+(b-a)*t;}
using Curve=std::array<QPointF,4>;
std::pair<Curve,Curve> split(const Curve &c,qreal t) {
    const auto a=mix(c[0],c[1],t),b=mix(c[1],c[2],t),d=mix(c[2],c[3],t);
    const auto e=mix(a,b,t),f=mix(b,d,t),g=mix(e,f,t);
    return {{c[0],a,e,g},{g,f,d,c[3]}};
}

}
void pairMotionChannels(LayerAnimation &animation) {
    if(positionChannelsPaired(animation)) return;
    const auto original=animation;
    QSet<int> frames;
    for(const auto &c:original.channels) if(c.property==KeyframeProperty::PositionX || c.property==KeyframeProperty::PositionY)
        for(const auto &k:c.keys) frames.insert(k.frame);
    if(frames.isEmpty()) frames.insert(0);
    auto times=frames.values();std::sort(times.begin(),times.end());
    PropertyChannel x{KeyframeProperty::PositionX,{}},y{KeyframeProperty::PositionY,{}};
    auto copy=original;
    auto *oldX=channel(copy,KeyframeProperty::PositionX),*oldY=channel(copy,KeyframeProperty::PositionY);
    for(int frame:times){
        ScalarKeyframe kx{frame,evaluateProperty(original,KeyframeProperty::PositionX,frame)},ky{frame,evaluateProperty(original,KeyframeProperty::PositionY,frame)};
        const auto *a=oldX?key(*oldX,frame):nullptr,*b=oldY?key(*oldY,frame):nullptr;
        if(a) kx=*a;
        if(b) ky=*b;
        const auto *timing=a?a:b;
        if(timing){kx.interpolation=ky.interpolation=timing->interpolation;kx.easing=ky.easing=timing->easing;}
        ky.spatial.reset();x.keys.append(kx);y.keys.append(ky);
    }
    animation.channels.removeIf([](const auto &c){return c.property==KeyframeProperty::PositionX || c.property==KeyframeProperty::PositionY;});
    animation.channels.append(x);animation.channels.append(y);
}
void insertMotionKey(LayerAnimation &animation,int frame) {
    pairMotionChannels(animation);
    auto *x=channel(animation,KeyframeProperty::PositionX),*y=channel(animation,KeyframeProperty::PositionY);
    if(key(*x,frame))return;
    const auto position=evaluateAnimatedPosition(animation,frame);
    const auto at=std::lower_bound(x->keys.cbegin(),x->keys.cend(),frame,[](const auto &k,int f){return k.frame<f;});
    const int i=int(at-x->keys.cbegin());
    ScalarKeyframe kx{frame,position.x()},ky{frame,position.y()};
    if(i>0 && i<x->keys.size()){
        auto &a=x->keys[i-1];auto &b=x->keys[i];
        const qreal t=qreal(frame-a.frame)/(b.frame-a.frame);
        const qreal progress=keyframeProgress(t,a.interpolation,a.easing);
        kx.interpolation=ky.interpolation=a.interpolation;
        const auto parts=splitKeyframeEasing(t,a.interpolation,a.easing);
        a.easing=y->keys[i-1].easing=parts.first;
        kx.easing=ky.easing=parts.second;
        if(a.spatial && a.spatial->curved){
            const QPointF start(a.value,y->keys[i-1].value),end(b.value,y->keys[i].value);
            const Curve spatial{start,start+a.spatial->outgoing,end+(b.spatial?b.spatial->incoming:QPointF()),end};
            const auto [left,right]=split(spatial,progress);
            a.spatial->outgoing=left[1]-start;
            if(!b.spatial)b.spatial=SpatialTangents{};
            b.spatial->incoming=right[2]-end;
            kx.spatial=SpatialTangents{left[2]-position,right[1]-position,true};
        }
    }
    x->keys.insert(i,kx);y->keys.insert(i,ky);
}
} // namespace toon
