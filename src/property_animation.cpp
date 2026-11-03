#include "property_animation.h"
#include "project.h"
#include "raster_edit.h"
#include "symbols.h"
#include "motion_edit.h"
#include <QJsonObject>
#include <QSet>
#include <algorithm>
#include <cmath>
#include <limits>

namespace toon {
namespace {
const RasterLayer *findLayer(const Shot &s,const QString &id) { for(const auto &l:s.layers) if(l.id==id)return &l;return nullptr; }
const EditableObject *findObject(const RasterLayer *l,const QString &id) { if(l)for(const auto&o:l->objects)if(o.id==id)return &o;return nullptr; }
bool finitePoint(QPointF p,qreal limit=32768) { return std::isfinite(p.x())&&std::isfinite(p.y())&&qAbs(p.x())<=limit&&qAbs(p.y())<=limit; }
bool validInterpolation(KeyframeInterpolation i) { return !keyframeInterpolationKey(i).isEmpty(); }
qreal mix(qreal a,qreal b,qreal t){return a+(b-a)*t;}
QPointF mix(QPointF a,QPointF b,qreal t){return a+(b-a)*t;}
QColor mix(QColor a,QColor b,qreal t) {
    if(!a.isValid()||!b.isValid())return t<1?a:b;
    return QColor::fromRgbF(mix(a.redF(),b.redF(),t),mix(a.greenF(),b.greenF(),t),mix(a.blueF(),b.blueF(),t),mix(a.alphaF(),b.alphaF(),t));
}
struct AffineParts { qreal angle, scaleX, scaleY, shear, x, y; };
AffineParts decompose(const QTransform &m) {
    const qreal sx=std::hypot(m.m11(),m.m12());
    return {std::atan2(m.m12(),m.m11()),sx,m.determinant()/sx,
        (m.m11()*m.m21()+m.m12()*m.m22())/sx,m.dx(),m.dy()};
}
QTransform mixTransform(const QTransform&a,const QTransform&b,qreal t) {
    const auto x=decompose(a),y=decompose(b);
    constexpr qreal pi=3.14159265358979323846;
    qreal delta=std::remainder(y.angle-x.angle,2*pi);
    const qreal angle=x.angle+delta*t,c=std::cos(angle),s=std::sin(angle);
    const qreal sx=mix(x.scaleX,y.scaleX,t),sy=mix(x.scaleY,y.scaleY,t),shear=mix(x.shear,y.shear,t);
    return QTransform(c*sx,s*sx,c*shear-s*sy,s*shear+c*sy,mix(x.x,y.x,t),mix(x.y,y.y,t));
}
template<class Key> std::pair<const Key*,const Key*> bracket(const QVector<Key>&keys,qreal frame){
    if(keys.isEmpty())return {};
    auto next=std::upper_bound(keys.cbegin(),keys.cend(),frame,[](qreal f,const Key&k){return f<k.frame;});
    if(next==keys.cbegin())return {&keys.first(),&keys.first()};
    if(next==keys.cend())return {&keys.last(),&keys.last()};
    return {&*std::prev(next),&*next};
}
const PropertyChannel *channel(const LayerAnimation &animation, KeyframeProperty property) {
    for (const auto &value : animation.channels) if (value.property == property) return &value;
    return nullptr;
}
qreal scalarValue(const PropertyChannel *channel, KeyframeProperty property, qreal frame) {
    if (!channel || channel->keys.isEmpty()) return keyframeDefaultValue(property);
    const auto [a, b] = bracket(channel->keys, frame);
    return a == b || keyframePropertyIsDiscrete(property) ? a->value : mix(a->value, b->value,
        keyframeProgress((frame - a->frame) / (b->frame - a->frame), a->interpolation, a->easing));
}
QPointF cubicPoint(QPointF a, QPointF b, QPointF c, QPointF d, qreal t) {
    const qreal u = 1 - t;
    return a * (u * u * u) + b * (3 * u * u * t) + c * (3 * u * t * t) + d * (t * t * t);
}
QPointF cubicDerivative(QPointF a, QPointF b, QPointF c, QPointF d, qreal t) {
    const qreal u = 1 - t;
    return (b - a) * (3 * u * u) + (c - b) * (6 * u * t) + (d - c) * (3 * t * t);
}
bool nonzero(QPointF value) { return std::hypot(value.x(), value.y()) > 1e-9; }
qreal scalarDerivative(const PropertyChannel *values, qreal frame) {
    if (!values || values->keys.size() < 2) return 0;
    const auto &keys = values->keys;
    if (frame < keys.first().frame || frame > keys.last().frame) return 0;
    frame = qBound(qreal(keys.first().frame), frame, qreal(keys.last().frame));
    auto next = std::upper_bound(keys.cbegin(), keys.cend(), frame, [](qreal at, const auto &key) { return at < key.frame; });
    if (next == keys.cend()) --next;
    const auto &a = *(next - 1), &b = *next;
    if (a.interpolation == KeyframeInterpolation::Hold && !a.easing) return 0;
    const qreal t = (frame - a.frame) / (b.frame - a.frame);
    const qreal from = qMax(0.0, t - .00001), to = qMin(1.0, t + .00001);
    return (b.value - a.value) * (keyframeProgress(to, a.interpolation, a.easing)
        - keyframeProgress(from, a.interpolation, a.easing)) / ((to - from) * (b.frame - a.frame));
}
QString validatePose(const PathPose &p,const EditableObject &source) {
    if(p.nodes.size()!=source.nodes.size()||p.closed!=source.closed)return "Path morphs must retain their node count, order and open/closed state.";
    if(p.stroke.isValid()!=source.stroke.isValid()||p.fill.isValid()!=source.fill.isValid())return "Path morphs must retain their stroke and fill types.";
    auto object=source; object.brushStroke.reset(); applyPathPose(object,p);
    if(!p.transform.isAffine())return "Path morphs support affine object transforms.";
    return validateEditableObject(object);
}
QJsonArray point(QPointF p){return {p.x(),p.y()};}
bool readPoint(const QJsonValue &v,QPointF *p){const auto a=v.toArray();if(a.size()!=2||!a[0].isDouble()||!a[1].isDouble())return false;*p={a[0].toDouble(),a[1].toDouble()};return finitePoint(*p);}
bool fields(const QJsonObject&o,std::initializer_list<const char*>names){if(o.size()!=qsizetype(names.size()))return false;for(auto n:names)if(!o.contains(QLatin1String(n)))return false;return true;}
bool integer(const QJsonValue&v,int *n){const auto f=v.toDouble(-1);if(!v.isDouble()||!std::isfinite(f)||f<0||f>1000000||std::floor(f)!=f)return false;*n=int(f);return true;}
bool number(const QJsonValue&v,qreal *n){if(!v.isDouble())return false;*n=v.toDouble();return std::isfinite(*n);}
std::optional<KeyframeInterpolation> interpolationFromJson(const QJsonValue&v){for(int i=0;i<5;++i)if(v==keyframeInterpolationKey(KeyframeInterpolation(i)))return KeyframeInterpolation(i);return {};}
bool optionalFields(const QJsonObject &object, std::initializer_list<const char *> required,
                    std::initializer_list<const char *> optional) {
    QSet<QString> allowed;
    for (auto name : required) { if (!object.contains(QLatin1String(name))) return false; allowed.insert(QLatin1String(name)); }
    for (auto name : optional) allowed.insert(QLatin1String(name));
    for (auto it = object.constBegin(); it != object.constEnd(); ++it) if (!allowed.contains(it.key())) return false;
    return true;
}
QJsonObject easingJson(const CubicEasing &easing) { return {{"first", point(easing.first)}, {"second", point(easing.second)}}; }
bool readEasing(const QJsonValue &value, std::optional<CubicEasing> *out) {
    if (value.isUndefined()) return true;
    if (!value.isObject()) return false;
    const auto object = value.toObject(); CubicEasing easing;
    if (!fields(object, {"first", "second"}) || !readPoint(object["first"], &easing.first)
        || !readPoint(object["second"], &easing.second) || !validCubicEasing(easing)) return false;
    *out = easing; return true;
}
QJsonObject spatialJson(const SpatialTangents &spatial) {
    return {{"incoming", point(spatial.incoming)}, {"outgoing", point(spatial.outgoing)}, {"curved", spatial.curved}};
}
bool readSpatial(const QJsonValue &value, std::optional<SpatialTangents> *out) {
    if (value.isUndefined()) return true;
    if (!value.isObject()) return false;
    const auto object = value.toObject(); SpatialTangents spatial;
    if (!fields(object, {"incoming", "outgoing", "curved"}) || !readPoint(object["incoming"], &spatial.incoming)
        || !readPoint(object["outgoing"], &spatial.outgoing) || !object["curved"].isBool()) return false;
    spatial.curved = object["curved"].toBool(); *out = spatial; return true;
}
QString validateMotion(const LayerAnimation &animation) {
    for (const auto &values : animation.channels) for (const auto &key : values.keys) {
        if (keyframePropertyIsDiscrete(values.property) && (key.interpolation!=KeyframeInterpolation::Hold || key.easing || key.spatial))
            return "Enable and inversion keys require Hold interpolation without easing or motion handles.";
        if (key.easing && !validCubicEasing(*key.easing)) return "Easing handles must stay inside the graph and in time order.";
        if (key.spatial && (values.property != KeyframeProperty::PositionX
            || !finitePoint(key.spatial->incoming) || !finitePoint(key.spatial->outgoing)))
            return "Motion handles must belong to Position X and remain inside the supported coordinate range.";
    }
    for (const auto &path : animation.paths) for (const auto &key : path.keys)
        if (key.easing && !validCubicEasing(*key.easing)) return "Path easing handles must stay inside the graph and in time order.";
    if (hasSpatialMotion(animation)) {
        if (!positionChannelsPaired(animation)) return "Curved motion needs matching Position X/Y key times and easing.";
        const auto *x = channel(animation, KeyframeProperty::PositionX), *y = channel(animation, KeyframeProperty::PositionY);
        for (int i = 0; i < x->keys.size(); ++i) if (x->keys[i].spatial) {
            const QPointF position{x->keys[i].value, y->keys[i].value};
            if (!finitePoint(position + x->keys[i].spatial->incoming) || !finitePoint(position + x->keys[i].spatial->outgoing))
                return "Motion handles must stay inside the supported artwork coordinate range.";
        }
    }
    return {};
}
QJsonObject poseJson(const PathPose &p){QJsonArray nodes,widths;for(const auto&n:p.nodes)nodes.append(QJsonArray{point(n.anchor),point(n.inControl),point(n.outControl)});for(auto w:p.widthFactors)widths.append(w);return {{"nodes",nodes},{"widthFactors",widths},{"closed",p.closed},{"transform",QJsonArray{p.transform.m11(),p.transform.m12(),p.transform.m21(),p.transform.m22(),p.transform.dx(),p.transform.dy()}},{"stroke",p.stroke.isValid()?p.stroke.name(QColor::HexArgb):QString()},{"fill",p.fill.isValid()?p.fill.name(QColor::HexArgb):QString()},{"strokeWidth",p.strokeWidth},{"opacity",p.opacity}};}
bool readPose(const QJsonObject&o,PathPose*p){
    if(!fields(o,{"nodes","widthFactors","closed","transform","stroke","fill","strokeWidth","opacity"})||!o["nodes"].isArray()||!o["widthFactors"].isArray()||!o["closed"].isBool()||!o["stroke"].isString()||!o["fill"].isString()||!number(o["strokeWidth"],&p->strokeWidth)||!number(o["opacity"],&p->opacity))return false;
    const auto nodes=o["nodes"].toArray();if(nodes.size()<2||nodes.size()>16384)return false;
    for(const auto&v:nodes){auto a=v.toArray();BezierNode n;if(a.size()!=3||!readPoint(a[0],&n.anchor)||!readPoint(a[1],&n.inControl)||!readPoint(a[2],&n.outControl))return false;p->nodes.append(n);}
    const auto widths=o["widthFactors"].toArray();if(!widths.isEmpty()&&widths.size()!=nodes.size())return false;
    for(const auto&v:widths){qreal w;if(!number(v,&w)||w<0||w>16)return false;p->widthFactors.append(w);}
    p->closed=o["closed"].toBool();const auto t=o["transform"].toArray();if(t.size()!=6)return false;qreal m[6];for(int i=0;i<6;++i)if(!number(t[i],&m[i])||qAbs(m[i])>1000000000)return false;p->transform=QTransform(m[0],m[1],m[2],m[3],m[4],m[5]);
    const auto stroke=o["stroke"].toString(),fill=o["fill"].toString();p->stroke=QColor(stroke);p->fill=QColor(fill);if((!stroke.isEmpty()&&!p->stroke.isValid())||(!fill.isEmpty()&&!p->fill.isValid()))return false;
    return true;
}
}
namespace {
struct PropertyInfo { const char *key; const char *name; qreal low, high, initial; };
constexpr PropertyInfo propertyInfo[]{
    {"positionX","Position X",-32768,32768,0}, {"positionY","Position Y",-32768,32768,0},
    {"scaleX","Scale X",-100,100,1}, {"scaleY","Scale Y",-100,100,1},
    {"rotation","Rotation",-36000,36000,0}, {"opacity","Opacity",0,1,1},
    {"effectsEnabled","Effects enabled",0,1,1}, {"blurEnabled","Blur enabled",0,1,0},
    {"blurRadius","Blur radius",0,128,4}, {"shadowEnabled","Shadow enabled",0,1,0},
    {"shadowRadius","Shadow blur",0,128,8}, {"shadowOffsetX","Shadow X",-512,512,8},
    {"shadowOffsetY","Shadow Y",-512,512,8}, {"shadowOpacity","Shadow opacity",0,1,.5},
    {"glowEnabled","Glow enabled",0,1,0}, {"glowRadius","Glow radius",0,128,8},
    {"glowOpacity","Glow opacity",0,1,.5}, {"colorEnabled","Color enabled",0,1,0},
    {"exposure","Exposure",-4,4,0}, {"contrast","Contrast",-100,100,0},
    {"saturation","Saturation",-100,100,0}, {"hue","Hue",-180,180,0},
    {"maskPositionX","Mask X",-32768,32768,0}, {"maskPositionY","Mask Y",-32768,32768,0},
    {"maskScaleX","Mask scale X",-100,100,1}, {"maskScaleY","Mask scale Y",-100,100,1},
    {"maskRotation","Mask rotation",-36000,36000,0}, {"maskOpacity","Mask opacity",0,1,1},
    {"maskFeather","Mask feather",0,128,0}, {"maskEnabled","Mask enabled",0,1,1},
    {"maskInverted","Mask inverted",0,1,0}
};
static_assert(sizeof(propertyInfo)/sizeof(propertyInfo[0]) == keyframePropertyCount);
const PropertyInfo *info(KeyframeProperty property) {
    const int index=int(property); return index>=0 && index<keyframePropertyCount ? &propertyInfo[index] : nullptr;
}
}
QString keyframePropertyKey(KeyframeProperty property){const auto *value=info(property);return value?QLatin1String(value->key):QString();}
QString keyframePropertyName(KeyframeProperty property){const auto *value=info(property);return value?QLatin1String(value->name):QString();}
bool keyframePropertyIsEffect(KeyframeProperty property){return property>=KeyframeProperty::EffectsEnabled && property<=KeyframeProperty::Hue;}
bool keyframePropertyIsMask(KeyframeProperty property){return property>=KeyframeProperty::MaskPositionX && property<=KeyframeProperty::MaskInverted;}
bool keyframePropertyIsDiscrete(KeyframeProperty property){
    switch(property){case KeyframeProperty::EffectsEnabled:case KeyframeProperty::BlurEnabled:case KeyframeProperty::ShadowEnabled:
    case KeyframeProperty::GlowEnabled:case KeyframeProperty::ColorEnabled:case KeyframeProperty::MaskEnabled:case KeyframeProperty::MaskInverted:return true;
    default:return false;}
}
bool keyframePropertyIsPercent(KeyframeProperty property){
    switch(property){case KeyframeProperty::ScaleX:case KeyframeProperty::ScaleY:case KeyframeProperty::Opacity:
    case KeyframeProperty::ShadowOpacity:case KeyframeProperty::GlowOpacity:case KeyframeProperty::MaskScaleX:
    case KeyframeProperty::MaskScaleY:case KeyframeProperty::MaskOpacity:return true;default:return false;}
}
bool keyframePropertyNeedsPositionUnlock(KeyframeProperty property){
    return (property>=KeyframeProperty::PositionX && property<=KeyframeProperty::Rotation)
        || (property>=KeyframeProperty::MaskPositionX && property<=KeyframeProperty::MaskRotation);
}
qreal keyframePropertyMinimum(KeyframeProperty property){const auto *value=info(property);return value?value->low:0;}
qreal keyframePropertyMaximum(KeyframeProperty property){const auto *value=info(property);return value?value->high:0;}
qreal keyframeSourceValue(const RasterLayer &layer, KeyframeProperty property){
    const auto &effects=layer.effects; const LayerMask empty; const auto &mask=layer.mask?*layer.mask:empty;
    switch(property){
    case KeyframeProperty::EffectsEnabled:return effects.enabled;
    case KeyframeProperty::BlurEnabled:return effects.blurEnabled;
    case KeyframeProperty::BlurRadius:return effects.blurRadius;
    case KeyframeProperty::ShadowEnabled:return effects.shadowEnabled;
    case KeyframeProperty::ShadowRadius:return effects.shadowRadius;
    case KeyframeProperty::ShadowOffsetX:return effects.shadowOffset.x();
    case KeyframeProperty::ShadowOffsetY:return effects.shadowOffset.y();
    case KeyframeProperty::ShadowOpacity:return effects.shadowOpacity;
    case KeyframeProperty::GlowEnabled:return effects.glowEnabled;
    case KeyframeProperty::GlowRadius:return effects.glowRadius;
    case KeyframeProperty::GlowOpacity:return effects.glowOpacity;
    case KeyframeProperty::ColorEnabled:return effects.colorEnabled;
    case KeyframeProperty::Exposure:return effects.exposure;
    case KeyframeProperty::Contrast:return effects.contrast;
    case KeyframeProperty::Saturation:return effects.saturation;
    case KeyframeProperty::Hue:return effects.hue;
    case KeyframeProperty::MaskPositionX:return mask.position.x();
    case KeyframeProperty::MaskPositionY:return mask.position.y();
    case KeyframeProperty::MaskScaleX:return mask.scale.x();
    case KeyframeProperty::MaskScaleY:return mask.scale.y();
    case KeyframeProperty::MaskRotation:return mask.rotation;
    case KeyframeProperty::MaskOpacity:return mask.opacity;
    case KeyframeProperty::MaskFeather:return mask.feather;
    case KeyframeProperty::MaskEnabled:return mask.enabled;
    case KeyframeProperty::MaskInverted:return mask.inverted;
    default:return keyframeDefaultValue(property);
    }
}
bool setKeyframeSourceValue(RasterLayer &layer,KeyframeProperty property,qreal value){
    if(!validKeyframeValue(property,value) || (!keyframePropertyIsEffect(property) && !keyframePropertyIsMask(property))
        || (keyframePropertyIsMask(property) && !layer.mask))return false;
    auto &effects=layer.effects;
    switch(property){
    case KeyframeProperty::EffectsEnabled:effects.enabled=value!=0;break;
    case KeyframeProperty::BlurEnabled:effects.blurEnabled=value!=0;break;
    case KeyframeProperty::BlurRadius:effects.blurRadius=value;break;
    case KeyframeProperty::ShadowEnabled:effects.shadowEnabled=value!=0;break;
    case KeyframeProperty::ShadowRadius:effects.shadowRadius=value;break;
    case KeyframeProperty::ShadowOffsetX:effects.shadowOffset.setX(value);break;
    case KeyframeProperty::ShadowOffsetY:effects.shadowOffset.setY(value);break;
    case KeyframeProperty::ShadowOpacity:effects.shadowOpacity=value;break;
    case KeyframeProperty::GlowEnabled:effects.glowEnabled=value!=0;break;
    case KeyframeProperty::GlowRadius:effects.glowRadius=value;break;
    case KeyframeProperty::GlowOpacity:effects.glowOpacity=value;break;
    case KeyframeProperty::ColorEnabled:effects.colorEnabled=value!=0;break;
    case KeyframeProperty::Exposure:effects.exposure=value;break;
    case KeyframeProperty::Contrast:effects.contrast=value;break;
    case KeyframeProperty::Saturation:effects.saturation=value;break;
    case KeyframeProperty::Hue:effects.hue=value;break;
    case KeyframeProperty::MaskPositionX:layer.mask->position.setX(value);break;
    case KeyframeProperty::MaskPositionY:layer.mask->position.setY(value);break;
    case KeyframeProperty::MaskScaleX:layer.mask->scale.setX(value);break;
    case KeyframeProperty::MaskScaleY:layer.mask->scale.setY(value);break;
    case KeyframeProperty::MaskRotation:layer.mask->rotation=value;break;
    case KeyframeProperty::MaskOpacity:layer.mask->opacity=value;break;
    case KeyframeProperty::MaskFeather:layer.mask->feather=value;break;
    case KeyframeProperty::MaskEnabled:layer.mask->enabled=value!=0;break;
    case KeyframeProperty::MaskInverted:layer.mask->inverted=value!=0;break;
    default:return false;
    }
    return true;
}
QString keyframeInterpolationKey(KeyframeInterpolation i){switch(i){case KeyframeInterpolation::Hold:return "hold";case KeyframeInterpolation::Linear:return "linear";case KeyframeInterpolation::EaseIn:return "easeIn";case KeyframeInterpolation::EaseOut:return "easeOut";case KeyframeInterpolation::EaseInOut:return "easeInOut";}return {};}
QString keyframeInterpolationName(KeyframeInterpolation i){switch(i){case KeyframeInterpolation::Hold:return "Hold";case KeyframeInterpolation::Linear:return "Linear";case KeyframeInterpolation::EaseIn:return "Ease in";case KeyframeInterpolation::EaseOut:return "Ease out";case KeyframeInterpolation::EaseInOut:return "Ease in/out";}return {};}
qreal keyframeDefaultValue(KeyframeProperty property){const auto *value=info(property);return value?value->initial:0;}
bool validKeyframeValue(KeyframeProperty property,qreal value){
    const auto *entry=info(property);return entry && std::isfinite(value) && value>=entry->low && value<=entry->high
        && (!keyframePropertyIsDiscrete(property) || value==0 || value==1);
}
qreal keyframeProgress(qreal t,KeyframeInterpolation i){t=qBound(0.0,t,1.0);switch(i){case KeyframeInterpolation::Hold:return t<1?0:1;case KeyframeInterpolation::Linear:return t;case KeyframeInterpolation::EaseIn:return t*t;case KeyframeInterpolation::EaseOut:return 1-(1-t)*(1-t);case KeyframeInterpolation::EaseInOut:return t*t*(3-2*t);}return t;}
const LayerAnimation *layerAnimation(const Shot&s,const QString&id){for(const auto&a:s.layerAnimations)if(a.layerId==id)return &a;return nullptr;}
LayerAnimation *layerAnimation(Shot&s,const QString&id){for(auto&a:s.layerAnimations)if(a.layerId==id)return &a;return nullptr;}
const PropertyChannel *propertyChannel(const Shot&s,const QString&id,KeyframeProperty p){if(const auto*a=layerAnimation(s,id))for(const auto&c:a->channels)if(c.property==p)return &c;return nullptr;}
const PathAnimation *pathAnimation(const Shot&s,const QString&id,const QString&object){if(const auto*a=layerAnimation(s,id))for(const auto&p:a->paths)if(p.objectId==object)return &p;return nullptr;}
bool validCubicEasing(const CubicEasing &easing) {
    const auto unit = [](QPointF p) { return std::isfinite(p.x()) && std::isfinite(p.y())
        && p.x() >= 0 && p.x() <= 1 && p.y() >= 0 && p.y() <= 1; };
    return unit(easing.first) && unit(easing.second) && easing.first.x() <= easing.second.x();
}
CubicEasing easingForInterpolation(KeyframeInterpolation interpolation) {
    // Exact cubic representations of the established polynomial presets.
    switch (interpolation) {
    case KeyframeInterpolation::EaseIn: return {{1.0 / 3, 0}, {2.0 / 3, 1.0 / 3}};
    case KeyframeInterpolation::EaseOut: return {{1.0 / 3, 2.0 / 3}, {2.0 / 3, 1}};
    case KeyframeInterpolation::EaseInOut: return {{1.0 / 3, 0}, {2.0 / 3, 1}};
    default: return {};
    }
}
qreal keyframeProgress(qreal t, KeyframeInterpolation interpolation, const std::optional<CubicEasing> &easing) {
    if (!easing || !validCubicEasing(*easing)) return keyframeProgress(t, interpolation);
    if (t <= 0) return 0;
    if (t >= 1) return 1;
    if (easing->first.x() == easing->first.y() && easing->second.x() == easing->second.y()) return t;
    if (easing->first.x() == 1.0 / 3 && easing->second.x() == 2.0 / 3)
        return cubicPoint({}, easing->first, easing->second, {1, 1}, t).y();
    // Invert monotone cubic time, including vertical/flat endpoint tangents.
    // Bisection has a fixed budget and cannot escape the unit interval.
    qreal low = 0, high = 1;
    for (int step = 0; step < 48; ++step) {
        const qreal mid = (low + high) / 2;
        if (cubicPoint({}, easing->first, easing->second, {1, 1}, mid).x() < t) low = mid;
        else high = mid;
    }
    return qBound(0.0, cubicPoint({}, easing->first, easing->second, {1, 1}, (low + high) / 2).y(), 1.0);
}
std::pair<std::optional<CubicEasing>, std::optional<CubicEasing>> splitKeyframeEasing(
    qreal t, KeyframeInterpolation interpolation, const std::optional<CubicEasing> &easing) {
    if (!easing && (interpolation == KeyframeInterpolation::Hold || interpolation == KeyframeInterpolation::Linear)) return {};
    if (!std::isfinite(t) || t <= 0 || t >= 1) return {easing, easing};
    const auto curve = easing.value_or(easingForInterpolation(interpolation));
    qreal low = 0, high = 1;
    for (int step = 0; step < 48; ++step) {
        const qreal mid = (low + high) / 2;
        if (cubicPoint({}, curve.first, curve.second, {1, 1}, mid).x() < t) low = mid;
        else high = mid;
    }
    const qreal parameter = (low + high) / 2;
    const QPointF a = mix(QPointF(), curve.first, parameter), b = mix(curve.first, curve.second, parameter),
        c = mix(curve.second, QPointF(1, 1), parameter), d = mix(a, b, parameter), e = mix(b, c, parameter), midpoint = mix(d, e, parameter);
    const auto normalize = [](QPointF origin, QPointF first, QPointF second, QPointF end) {
        const QPointF extent = end - origin;
        if (extent.x() <= 0 || extent.y() <= 0) return CubicEasing{};
        const auto point = [&](QPointF p) { return QPointF(qBound(0.0, (p.x() - origin.x()) / extent.x(), 1.0),
            qBound(0.0, (p.y() - origin.y()) / extent.y(), 1.0)); };
        auto one = point(first), two = point(second); one.setX(qMin(one.x(), two.x()));
        return CubicEasing{one, two};
    };
    return {normalize({}, a, d, midpoint), normalize(midpoint, e, c, {1, 1})};
}
bool positionChannelsPaired(const LayerAnimation &animation) {
    const auto *x = channel(animation, KeyframeProperty::PositionX), *y = channel(animation, KeyframeProperty::PositionY);
    if (!x || !y || x->keys.isEmpty() || x->keys.size() != y->keys.size()) return false;
    for (int i = 0; i < x->keys.size(); ++i)
        if (x->keys[i].frame != y->keys[i].frame || x->keys[i].interpolation != y->keys[i].interpolation
            || x->keys[i].easing != y->keys[i].easing) return false;
    return true;
}
bool hasSpatialMotion(const LayerAnimation &animation) {
    if (const auto *x = channel(animation, KeyframeProperty::PositionX))
        for (const auto &key : x->keys) if (key.spatial) return true;
    return false;
}
QPointF evaluateAnimatedPosition(const LayerAnimation &animation, qreal frame) {
    const auto *x = channel(animation, KeyframeProperty::PositionX), *y = channel(animation, KeyframeProperty::PositionY);
    const QPointF ordinary{scalarValue(x, KeyframeProperty::PositionX, frame), scalarValue(y, KeyframeProperty::PositionY, frame)};
    if (!x || !y || x->keys.isEmpty() || x->keys.size() != y->keys.size()) return ordinary;
    const auto [a, b] = bracket(x->keys, frame);
    if (a == b || !a->spatial || !a->spatial->curved) return ordinary;
    const auto index = qsizetype(a - x->keys.constData());
    if (y->keys[index].frame != a->frame || y->keys[index + 1].frame != b->frame) return ordinary;
    const QPointF start{a->value, y->keys[index].value}, end{b->value, y->keys[index + 1].value};
    const auto t = keyframeProgress((frame - a->frame) / (b->frame - a->frame), a->interpolation, a->easing);
    return cubicPoint(start, start + a->spatial->outgoing,
        end + (b->spatial ? b->spatial->incoming : QPointF()), end, t);
}
static QPointF directionAt(const LayerAnimation &animation, qreal frame) {
    const auto *x = channel(animation, KeyframeProperty::PositionX), *y = channel(animation, KeyframeProperty::PositionY);
    if (x && y && x->keys.size() >= 2 && x->keys.size() == y->keys.size()) {
        const qreal bounded = qBound(qreal(x->keys.first().frame), frame, qreal(x->keys.last().frame));
        auto next = std::upper_bound(x->keys.cbegin(), x->keys.cend(), bounded, [](qreal at, const auto &key) { return at < key.frame; });
        if (next == x->keys.cend()) --next;
        const auto &a = *(next - 1), &b = *next;
        const qsizetype index = (next - 1) - x->keys.cbegin();
        if (a.spatial && a.spatial->curved && y->keys[index].frame == a.frame && y->keys[index + 1].frame == b.frame) {
            const QPointF start{a.value, y->keys[index].value}, end{b.value, y->keys[index + 1].value};
            const QPointF first = start + a.spatial->outgoing, second = end + (b.spatial ? b.spatial->incoming : QPointF());
            const qreal t = keyframeProgress((bounded - a.frame) / (b.frame - a.frame), a.interpolation, a.easing);
            auto direction = cubicDerivative(start, first, second, end, t);
            if (!nonzero(direction)) direction = cubicDerivative(start, first, second, end, t < 1 ? qMin(1.0, t + .00001) : qMax(0.0, t - .00001));
            if (!nonzero(direction)) direction = end - start;
            if (nonzero(direction)) return direction;
        }
    }
    const QPointF direction{scalarDerivative(x, frame), scalarDerivative(y, frame)};
    if (nonzero(direction)) return direction;
    // A Hold or flat temporal tangent retains the segment's travel direction.
    const auto chord = [frame](const PropertyChannel *values) -> qreal {
        if (!values || values->keys.size() < 2) return 0;
        const auto &keys = values->keys;
        if (frame < keys.first().frame || frame > keys.last().frame) return 0;
        const qreal bounded = qBound(qreal(keys.first().frame), frame, qreal(keys.last().frame));
        auto next = std::upper_bound(keys.cbegin(), keys.cend(), bounded, [](qreal at, const auto &key) { return at < key.frame; });
        if (next == keys.cend()) --next;
        const auto &a = *(next - 1), &b = *next;
        return (b.value - a.value) / (b.frame - a.frame);
    };
    return {chord(x), chord(y)};
}
QPointF animatedPositionDirection(const LayerAnimation &animation, qreal frame) {
    const auto direction = directionAt(animation, frame);
    if (nonzero(direction)) return direction;
    // A stationary segment inherits the nearest preceding moving segment.
    // Before motion begins, use the next moving segment instead. Searching
    // only exact key intervals is bounded by the existing key budget.
    QVector<int> frames;
    for (const auto &values : animation.channels)
        if (values.property == KeyframeProperty::PositionX || values.property == KeyframeProperty::PositionY)
            for (const auto &key : values.keys) frames.append(key.frame);
    std::sort(frames.begin(), frames.end());
    frames.erase(std::unique(frames.begin(), frames.end()), frames.end());
    for (int i = frames.size() - 2; i >= 0; --i) {
        if (frames[i] >= frame) continue;
        const auto previous = directionAt(animation, std::nextafter(qreal(frames[i + 1]), qreal(frames[i])));
        if (nonzero(previous)) return previous;
    }
    for (int i = 0; i + 1 < frames.size(); ++i) {
        if (frames[i + 1] <= frame) continue;
        const auto next = directionAt(animation, qreal(frames[i]));
        if (nonzero(next)) return next;
    }
    return {};
}
qreal evaluateProperty(const LayerAnimation &animation, KeyframeProperty property, qreal frame) {
    if (property == KeyframeProperty::PositionX || property == KeyframeProperty::PositionY) {
        const auto position = evaluateAnimatedPosition(animation, frame);
        return property == KeyframeProperty::PositionX ? position.x() : position.y();
    }
    return scalarValue(channel(animation, property), property, frame);
}
qreal evaluateProperty(const Shot &shot, const QString &id, KeyframeProperty property, qreal frame) {
    const auto *animation = layerAnimation(shot, id);
    if (animation && channel(*animation,property)) return evaluateProperty(*animation,property,frame);
    const auto *layer=findLayer(shot,id);
    return layer ? keyframeSourceValue(*layer,property) : keyframeDefaultValue(property);
}
QTransform evaluateLayerTransform(const LayerAnimation &animation, qreal frame) {
    const auto position = evaluateAnimatedPosition(animation, frame);
    qreal rotation = evaluateProperty(animation, KeyframeProperty::Rotation, frame);
    if (animation.orientAlongPath) {
        const auto direction = animatedPositionDirection(animation, frame);
        if (nonzero(direction)) rotation += std::atan2(direction.y(), direction.x()) * 180 / 3.14159265358979323846;
    }
    QTransform transform;
    transform.translate(position.x() + animation.pivot.x(), position.y() + animation.pivot.y());
    transform.rotate(rotation);
    transform.scale(evaluateProperty(animation, KeyframeProperty::ScaleX, frame), evaluateProperty(animation, KeyframeProperty::ScaleY, frame));
    transform.translate(-animation.pivot.x(), -animation.pivot.y());
    return transform;
}
QTransform evaluateLayerTransform(const Shot &shot, const QString &id, qreal frame) {
    const auto *animation = layerAnimation(shot, id);
    return animation ? evaluateLayerTransform(*animation, frame) : QTransform();
}
LayerEffects evaluateAnimatedLayerEffects(const Shot &shot,const QString &id,qreal frame,const LayerEffects &base){
    auto result=base;const auto *animation=layerAnimation(shot,id);if(!animation)return result;
    for(const auto &entry:animation->channels){if(!keyframePropertyIsEffect(entry.property))continue;
        const auto value=evaluateProperty(*animation,entry.property,frame);
        switch(entry.property){
        case KeyframeProperty::EffectsEnabled:result.enabled=value!=0;break;
        case KeyframeProperty::BlurEnabled:result.blurEnabled=value!=0;break;
        case KeyframeProperty::BlurRadius:result.blurRadius=value;break;
        case KeyframeProperty::ShadowEnabled:result.shadowEnabled=value!=0;break;
        case KeyframeProperty::ShadowRadius:result.shadowRadius=value;break;
        case KeyframeProperty::ShadowOffsetX:result.shadowOffset.setX(value);break;
        case KeyframeProperty::ShadowOffsetY:result.shadowOffset.setY(value);break;
        case KeyframeProperty::ShadowOpacity:result.shadowOpacity=value;break;
        case KeyframeProperty::GlowEnabled:result.glowEnabled=value!=0;break;
        case KeyframeProperty::GlowRadius:result.glowRadius=value;break;
        case KeyframeProperty::GlowOpacity:result.glowOpacity=value;break;
        case KeyframeProperty::ColorEnabled:result.colorEnabled=value!=0;break;
        case KeyframeProperty::Exposure:result.exposure=value;break;
        case KeyframeProperty::Contrast:result.contrast=value;break;
        case KeyframeProperty::Saturation:result.saturation=value;break;
        case KeyframeProperty::Hue:result.hue=value;break;
        default:break;
        }
    }return result;
}
LayerMask evaluateAnimatedLayerMask(const Shot &shot,const QString &id,qreal frame,const LayerMask &base){
    auto result=base;const auto *animation=layerAnimation(shot,id);if(!animation)return result;
    for(const auto &entry:animation->channels){if(!keyframePropertyIsMask(entry.property))continue;
        const auto value=evaluateProperty(*animation,entry.property,frame);
        switch(entry.property){
        case KeyframeProperty::MaskPositionX:result.position.setX(value);break;
        case KeyframeProperty::MaskPositionY:result.position.setY(value);break;
        case KeyframeProperty::MaskScaleX:result.scale.setX(value);break;
        case KeyframeProperty::MaskScaleY:result.scale.setY(value);break;
        case KeyframeProperty::MaskRotation:result.rotation=value;break;
        case KeyframeProperty::MaskOpacity:result.opacity=value;break;
        case KeyframeProperty::MaskFeather:result.feather=value;break;
        case KeyframeProperty::MaskEnabled:result.enabled=value!=0;break;
        case KeyframeProperty::MaskInverted:result.inverted=value!=0;break;
        default:break;
        }
    }return result;
}
QPointF defaultAnimationPivot(const Shot&s,const QString&id,QSizeF fallback){
    QRectF bounds;
    for(int i=0;i<s.layers.size();++i){const auto&layer=s.layers[i];if(layer.id!=id&&!layerIsDescendant(s.layers,i,id))continue;
        if(!layer.objects.isEmpty()){for(const auto&o:layer.objects)bounds=bounds.united(editableObjectBounds(o));}
        else bounds=bounds.united(selectedPixelBounds(layer));
    }
    return bounds.isEmpty()?(fallback.isValid()?QPointF(fallback.width()/2,fallback.height()/2):QPointF()):bounds.center();
}
QPointF defaultAnimationPivot(const Project &project, const Shot &shot, const QString &id, int frame) {
    const QSizeF fallback(project.width, project.height);
    bool containsSymbol = false;
    for (int i = 0; i < shot.layers.size(); ++i)
        if (shot.layers[i].symbol && (shot.layers[i].id == id || layerIsDescendant(shot.layers, i, id))) {
            containsSymbol = true;
            break;
        }
    // Retain the established source-artwork convention for ordinary layers.
    if (!containsSymbol) return defaultAnimationPivot(shot, id, fallback);

    QRectF bounds;
    const auto active = activeLayerIds(shot, frame);
    for (int i = 0; i < shot.layers.size(); ++i) {
        const auto &layer = shot.layers[i];
        if (layer.group || !active.contains(layer.id)
            || (layer.id != id && !layerIsDescendant(shot.layers, i, id))) continue;
        QRectF area;
        if (layer.symbol) {
            if (const auto *definition = findSymbol(project, layer.symbol->symbolId)) {
                const int local = symbolLocalFrame(*layer.symbol, std::max(frame, layer.symbol->startFrame),
                                                  definition->content.durationFrames);
                if (local >= 0) area = symbolArtworkBounds(project, layer.symbol->symbolId, local);
            }
        } else if (!layer.objects.isEmpty()) {
            for (const auto &object : layer.objects)
                area |= editableObjectBounds(evaluateAnimatedObject(shot, layer.id, object, frame));
        } else area = selectedPixelBounds(layer);
        if (area.isEmpty()) continue;

        // Compose only descendants: the selected layer's own transform and
        // ancestors must not move its registration point. No inverse is needed
        // when a user has temporarily set the selected layer's scale to zero.
        QTransform relative;
        const auto *child = &layer;
        for (int depth = 0; child && child->id != id && depth <= 8; ++depth) {
            relative *= evaluateLayerTransform(shot, child->id, frame);
            child = findLayer(shot, child->parentId);
        }
        bounds |= relative.mapRect(area);
    }
    return bounds.isEmpty() ? QPointF(project.width / 2.0, project.height / 2.0) : bounds.center();
}
QTransform worldLayerTransform(const Shot&s,const QString&id,qreal f){QTransform t;QString current=id;for(int depth=0;depth<=8&&!current.isEmpty();++depth){t=t*evaluateLayerTransform(s,current,f);const auto*l=findLayer(s,current);current=l?l->parentId:QString();}return t;}
PathPose pathPose(const EditableObject&o){return {o.nodes,o.widthFactors,o.closed,o.transform,o.stroke,o.fill,o.strokeWidth,o.opacity};}
void applyPathPose(EditableObject&o,const PathPose&p){o.nodes=p.nodes;o.widthFactors=p.widthFactors;o.closed=p.closed;o.transform=p.transform;o.stroke=p.stroke;o.fill=p.fill;o.strokeWidth=p.strokeWidth;o.opacity=p.opacity;}
EditableObject evaluateAnimatedObject(const Shot&s,const QString&layerId,const EditableObject&source,qreal frame){
    const auto*channel=pathAnimation(s,layerId,source.id);if(!channel||channel->keys.isEmpty())return source;auto[a,b]=bracket(channel->keys,frame);auto result=source;if(a==b){applyPathPose(result,a->pose);return result;}
    const qreal t=keyframeProgress((frame-a->frame)/(b->frame-a->frame),a->interpolation,a->easing);if(t==0){applyPathPose(result,a->pose);return result;}auto p=a->pose;const auto&q=b->pose;
    for(int i=0;i<p.nodes.size();++i){p.nodes[i].anchor=mix(p.nodes[i].anchor,q.nodes[i].anchor,t);p.nodes[i].inControl=mix(p.nodes[i].inControl,q.nodes[i].inControl,t);p.nodes[i].outControl=mix(p.nodes[i].outControl,q.nodes[i].outControl,t);}
    if(!p.widthFactors.isEmpty()||!q.widthFactors.isEmpty()){p.widthFactors.resize(p.nodes.size());for(int i=0;i<p.nodes.size();++i)p.widthFactors[i]=mix(a->pose.widthFactors.value(i,1),q.widthFactors.value(i,1),t);}
    p.transform=mixTransform(a->pose.transform,q.transform,t);
    p.stroke=mix(p.stroke,q.stroke,t);p.fill=mix(p.fill,q.fill,t);p.strokeWidth=mix(p.strokeWidth,q.strokeWidth,t);p.opacity=mix(p.opacity,q.opacity,t);applyPathPose(result,p);return result;
}
QString propertyAnimationUnavailableReason(const Shot&s,const QString&id){const auto*l=findLayer(s,id);if(!l)return "Select a layer to animate.";int index=int(l-s.layers.constData());if(layerIsAnnotation(s.layers,index))return "Production notes cannot be animated.";if(l->kind==LayerKind::Scene3D)return "3D animation is not available yet.";if(l->group)for(int i=0;i<s.layers.size();++i)if(layerIsDescendant(s.layers,i,id)&&(s.layers[i].kind==LayerKind::Scene3D||layerIsAnnotation(s.layers,i)))return "Groups containing 3D references or production notes cannot be animated.";return {};}
QString keyframePropertyUnavailableReason(const Shot &shot,const QString &id,KeyframeProperty property){
    const auto reason=propertyAnimationUnavailableReason(shot,id);if(!reason.isEmpty())return reason;
    if(keyframePropertyKey(property).isEmpty())return "This animation property is unavailable.";
    const auto *layer=findLayer(shot,id);
    if(keyframePropertyIsMask(property) && (!layer || !layer->mask))return "Add a mask to this layer before animating its settings.";
    if(layer && layer->adjustment && property>=KeyframeProperty::BlurEnabled && property<=KeyframeProperty::GlowOpacity)
        return "Adjustment layers support color effects and masks; animate blur, shadow or glow on an artwork layer.";
    return {};
}
QString validatePropertyAnimation(const Shot&s){
    if(s.layerAnimations.size()>maxPropertyAnimatedLayers)return "A shot supports up to 1024 property-animated layers.";
    QSet<QString> layers;int keys=0; qint64 nodes=0;
    for(const auto&a:s.layerAnimations){if(layers.contains(a.layerId)||!finitePoint(a.pivot))return "Invalid or duplicate animated layer.";layers.insert(a.layerId);const auto reason=propertyAnimationUnavailableReason(s,a.layerId);if(!reason.isEmpty())return reason;if(a.channels.isEmpty()&&a.paths.isEmpty())return "Animated layers must contain a property or path channel.";QSet<int> properties;
        const auto motionError = validateMotion(a); if (!motionError.isEmpty()) return motionError;
        for(const auto&c:a.channels){if(keyframePropertyKey(c.property).isEmpty()||properties.contains(int(c.property))||c.keys.isEmpty())return "Invalid or duplicate animation property.";properties.insert(int(c.property));const auto propertyReason=keyframePropertyUnavailableReason(s,a.layerId,c.property);if(!propertyReason.isEmpty())return propertyReason;int last=-1;for(const auto&k:c.keys){if(++keys>maxPropertyKeyframes||k.frame<=last||k.frame>=s.durationFrames||!validKeyframeValue(c.property,k.value)||!validInterpolation(k.interpolation))return "Property keys must be unique, ordered, finite and inside the shot.";last=k.frame;}}
        QSet<QString> objects;const auto*l=findLayer(s,a.layerId);
        for(const auto&p:a.paths){const auto*source=findObject(l,p.objectId);if(!source||l->kind!=LayerKind::Vector||source->kind!=ObjectKind::Path||objects.contains(p.objectId)||p.keys.isEmpty())return "Invalid or duplicate animated path.";objects.insert(p.objectId);int last=-1;const PathKeyframe *previous=nullptr;for(const auto&k:p.keys){nodes+=k.pose.nodes.size();if(++keys>maxPropertyKeyframes||nodes>maxPathKeyframeNodes||k.frame<=last||k.frame>=s.durationFrames||!validInterpolation(k.interpolation))return "Path keys exceed the shot or animation limits.";last=k.frame;const auto error=validatePose(k.pose,*source);if(!error.isEmpty())return error;if(previous&&(previous->interpolation!=KeyframeInterpolation::Hold||previous->easing)&&previous->pose.transform.determinant()*k.pose.transform.determinant()<0)return "A reflected path pose needs Hold interpolation to avoid collapsing through zero scale.";previous=&k;}}
    }return {};
}
QString validatePropertyAnimationMutation(const Shot&before,const Shot&after){
    const auto validation=validatePropertyAnimation(after);if(!validation.isEmpty())return validation;
    for(const auto&a:before.layerAnimations)for(const auto&p:a.paths){if(!pathAnimation(after,a.layerId,p.objectId))continue;const auto*old=findObject(findLayer(before,a.layerId),p.objectId);const auto*now=findObject(findLayer(after,a.layerId),p.objectId);if(old&&now&&*old!=*now)return "Edit an animated path at the current frame, or disable its path animation before changing the source.";}
    return {};
}
void prunePropertyAnimation(Shot&s){for(auto&a:s.layerAnimations){const auto*l=findLayer(s,a.layerId);if(l&&!l->mask)a.channels.removeIf([](const auto &channel){return keyframePropertyIsMask(channel.property);});a.paths.erase(std::remove_if(a.paths.begin(),a.paths.end(),[&](const auto&p){return !findObject(l,p.objectId);}),a.paths.end());}s.layerAnimations.erase(std::remove_if(s.layerAnimations.begin(),s.layerAnimations.end(),[&](const auto&a){return !findLayer(s,a.layerId)||(a.channels.isEmpty()&&a.paths.isEmpty());}),s.layerAnimations.end());}
bool writePropertyKeyframe(const Project &project,Shot &shot,const QString &id,
                           KeyframeProperty property,int frame,qreal value,QString *error){
    if(error)error->clear();
    const auto fail=[&](const QString &message){if(error)*error=message;return false;};
    const auto reason=keyframePropertyUnavailableReason(shot,id,property);if(!reason.isEmpty())return fail(reason);
    if(frame<0 || frame>=shot.durationFrames)return fail("The keyframe is outside the shot.");
    if(!validKeyframeValue(property,value))return fail("This animation value is outside its supported range.");
    const auto original=shot.layerAnimations;
    const auto base=keyframeSourceValue(*findLayer(shot,id),property);
    auto *animation=layerAnimation(shot,id);
    if(!animation){shot.layerAnimations.append({id,defaultAnimationPivot(project,shot,id,frame),{}, {}});animation=&shot.layerAnimations.last();}
    if(hasSpatialMotion(*animation) && (property==KeyframeProperty::PositionX || property==KeyframeProperty::PositionY))insertMotionKey(*animation,frame);
    PropertyChannel *values=nullptr;for(auto &entry:animation->channels)if(entry.property==property){values=&entry;break;}
    const bool discrete=keyframePropertyIsDiscrete(property);
    if(!values){animation->channels.append({property,{{0,base,discrete?KeyframeInterpolation::Hold:KeyframeInterpolation::Linear}}});values=&animation->channels.last();}
    auto at=std::lower_bound(values->keys.begin(),values->keys.end(),frame,[](const auto &key,int at){return key.frame<at;});
    if(at!=values->keys.end() && at->frame==frame)at->value=value;
    else{
        ScalarKeyframe key{frame,value,discrete?KeyframeInterpolation::Hold:KeyframeInterpolation::Linear};
        if(!discrete && at!=values->keys.begin() && at!=values->keys.end()){
            auto &previous=*(at-1);key.interpolation=previous.interpolation;
            const auto parts=splitKeyframeEasing(qreal(frame-previous.frame)/(at->frame-previous.frame),previous.interpolation,previous.easing);
            previous.easing=parts.first;key.easing=parts.second;
        }
        values->keys.insert(at,key);
    }
    const auto problem=validatePropertyAnimation(shot);
    if(!problem.isEmpty()){shot.layerAnimations=original;return fail(problem);}
    return true;
}
bool writePathKeyframe(Shot &shot, const QString &layerId, const QString &objectId, int frame,
                       const EditableObject &pose, QString *error) {
    const auto fail = [&](const QString &message) { if (error) *error = message; return false; };
    auto *animation = layerAnimation(shot, layerId);
    const auto *source = findObject(findLayer(shot, layerId), objectId);
    if (!animation || !source || pose.id != objectId || pose.kind != ObjectKind::Path || pose.brushStroke != source->brushStroke)
        return fail("An animated path must retain its original brush source.");
    if (frame < 0 || frame >= shot.durationFrames) return fail("The keyframe is outside the shot.");
    const auto validation = validatePose(pathPose(pose), *source); if (!validation.isEmpty()) return fail(validation);
    for (auto &path : animation->paths) if (path.objectId == objectId) {
        const auto before = path.keys;
        auto at = std::lower_bound(path.keys.begin(), path.keys.end(), frame, [](const auto &key, int value) { return key.frame < value; });
        if (at != path.keys.end() && at->frame == frame) at->pose = pathPose(pose);
        else {
            PathKeyframe key{frame, pathPose(pose), KeyframeInterpolation::Linear};
            if (at != path.keys.begin() && at != path.keys.end()) {
                auto &previous = *(at - 1);
                key.interpolation = previous.interpolation;
                const auto parts = splitKeyframeEasing(qreal(frame - previous.frame) / (at->frame - previous.frame), previous.interpolation, previous.easing);
                previous.easing = parts.first; key.easing = parts.second;
            }
            path.keys.insert(at, key);
        }
        const auto problem = validatePropertyAnimation(shot);
        if (!problem.isEmpty()) { path.keys = before; return fail(problem); }
        return true;
    }
    return fail("Enable path animation before adding a pose key.");
}
QJsonArray layerAnimationsToJson(const QVector<LayerAnimation> &animations) {
    QJsonArray result;
    for (const auto &animation : animations) {
        QJsonArray channels, paths;
        for (const auto &channel : animation.channels) {
            QJsonArray keys;
            for (const auto &key : channel.keys) {
                QJsonObject object{{"frame", key.frame}, {"value", key.value}, {"interpolation", keyframeInterpolationKey(key.interpolation)}};
                if (key.easing) object.insert("easing", easingJson(*key.easing));
                if (key.spatial) object.insert("spatial", spatialJson(*key.spatial));
                keys.append(object);
            }
            channels.append(QJsonObject{{"property", keyframePropertyKey(channel.property)}, {"keys", keys}});
        }
        for (const auto &path : animation.paths) {
            QJsonArray keys;
            for (const auto &key : path.keys) {
                QJsonObject object{{"frame", key.frame}, {"pose", poseJson(key.pose)}, {"interpolation", keyframeInterpolationKey(key.interpolation)}};
                if (key.easing) object.insert("easing", easingJson(*key.easing));
                keys.append(object);
            }
            paths.append(QJsonObject{{"objectId", path.objectId}, {"keys", keys}});
        }
        QJsonObject object{{"layerId", animation.layerId}, {"pivot", point(animation.pivot)}, {"channels", channels}, {"paths", paths}};
        if (animation.orientAlongPath) object.insert("orientAlongPath", true);
        result.append(object);
    }
    return result;
}
bool layerAnimationsFromJson(const QJsonArray &json, QVector<LayerAnimation> *animations, QString *error) {
    const auto fail = [&](QString message) { if (error) *error = message; return false; };
    if (!animations || json.size() > maxPropertyAnimatedLayers) return fail("Invalid property animation list.");
    QVector<LayerAnimation> result; int count = 0; qint64 nodes = 0;
    for (const auto &value : json) {
        const auto object = value.toObject();
        if (!optionalFields(object, {"layerId", "pivot", "channels", "paths"}, {"orientAlongPath"})
            || !object["layerId"].isString() || !object["channels"].isArray() || !object["paths"].isArray()
            || (object.contains("orientAlongPath") && !object["orientAlongPath"].isBool())) return fail("Invalid animated layer metadata.");
        LayerAnimation animation; animation.layerId = object["layerId"].toString();
        animation.orientAlongPath = object["orientAlongPath"].toBool(false);
        if (!readPoint(object["pivot"], &animation.pivot) || object["channels"].toArray().size() > keyframePropertyCount
            || object["paths"].toArray().size() > 1024) return fail("Invalid animated layer channels.");
        for (const auto &entry : object["channels"].toArray()) {
            const auto channelJson = entry.toObject();
            if (!fields(channelJson, {"property", "keys"}) || !channelJson["keys"].isArray()) return fail("Invalid property channel.");
            PropertyChannel channel; bool found = false;
            for (int i = 0; i < keyframePropertyCount; ++i) if (channelJson["property"] == keyframePropertyKey(KeyframeProperty(i))) {
                channel.property = KeyframeProperty(i); found = true;
            }
            if (!found) return fail("Unknown animation property.");
            for (const auto &entry : channelJson["keys"].toArray()) {
                const auto keyJson = entry.toObject(); ScalarKeyframe key;
                const auto interpolation = interpolationFromJson(keyJson["interpolation"]);
                if (++count > maxPropertyKeyframes || !optionalFields(keyJson, {"frame", "value", "interpolation"}, {"easing", "spatial"})
                    || !integer(keyJson["frame"], &key.frame) || !number(keyJson["value"], &key.value) || !interpolation
                    || !readEasing(keyJson["easing"], &key.easing) || !readSpatial(keyJson["spatial"], &key.spatial))
                    return fail("Invalid property keyframe.");
                key.interpolation = *interpolation; channel.keys.append(key);
            }
            animation.channels.append(channel);
        }
        for (const auto &entry : object["paths"].toArray()) {
            const auto pathJson = entry.toObject();
            if (!fields(pathJson, {"objectId", "keys"}) || !pathJson["objectId"].isString() || !pathJson["keys"].isArray()) return fail("Invalid path channel.");
            PathAnimation path; path.objectId = pathJson["objectId"].toString();
            for (const auto &entry : pathJson["keys"].toArray()) {
                const auto keyJson = entry.toObject(); PathKeyframe key;
                const auto interpolation = interpolationFromJson(keyJson["interpolation"]);
                if (++count > maxPropertyKeyframes || !optionalFields(keyJson, {"frame", "pose", "interpolation"}, {"easing"})
                    || !integer(keyJson["frame"], &key.frame) || !keyJson["pose"].isObject() || !interpolation
                    || !readPose(keyJson["pose"].toObject(), &key.pose) || !readEasing(keyJson["easing"], &key.easing))
                    return fail("Invalid path pose keyframe.");
                nodes += key.pose.nodes.size();
                if (nodes > maxPathKeyframeNodes) return fail("Path keys exceed the geometry memory limit.");
                key.interpolation = *interpolation; path.keys.append(key);
            }
            animation.paths.append(path);
        }
        const auto motionError = validateMotion(animation); if (!motionError.isEmpty()) return fail(motionError);
        result.append(animation);
    }
    *animations = std::move(result); return true;
}
qint64 propertyAnimationHistoryCost(const Shot&before,const Shot&after){if(before.layerAnimations==after.layerAnimations)return 0;auto bytes=[](const Shot&s){qint64 n=0;for(const auto&a:s.layerAnimations){n+=256;for(const auto&c:a.channels)n+=c.keys.size()*sizeof(ScalarKeyframe);for(const auto&p:a.paths)for(const auto&k:p.keys)n+=sizeof(PathKeyframe)+k.pose.nodes.size()*sizeof(BezierNode)+k.pose.widthFactors.size()*sizeof(qreal);}return n;};return bytes(before)+bytes(after);}
} // namespace toon
