#include "reference3d_builtin.h"
#include <QCryptographicHash>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QResource>
#include <QtMath>
#include <algorithm>
#include <array>
#include <cmath>
#include <functional>

// This global-namespace symbol forces the resource out of a static toon_core.
static void initializeReferenceAnatomyResource() { Q_INIT_RESOURCE(reference3d_anatomy); }
namespace toon {
namespace {
constexpr float pi=3.14159265358979323846f;
bool fail(QString *error,const QString &message) { if(error)*error=message;return false; }
struct Setting { QString id,name;float min=.5f,max=1.5f,initial=1;int decimals=2;QString group; };
QVector<Setting> headSettings() {
    QVector<Setting> s{
        {"headWidth","Width",.7f,1.35f,1,2,"Head"},{"headLength","Length",.75f,1.35f,1,2,"Head"},{"headDepth","Depth",.75f,1.35f,1,2,"Head"},
        {"chinWidth","Width",.5f,1.6f,1,2,"Chin"},{"chinLength","Length",.65f,1.35f,1,2,"Chin"},{"chinProjection","Projection",.65f,1.35f,1,2,"Chin"},
        {"eyeSpacing","Spacing",.65f,1.35f,1,2,"Eyes"},{"eyeSize","Size",.6f,1.5f,1,2,"Eyes"},
        {"browHeight","Height",.65f,1.35f,1,2,"Brows"},{"browAngle","Angle",.65f,1.35f,1,2,"Brows"},
        {"noseSize","Size",.5f,1.5f,1,2,"Nose"},{"noseWidth","Width",.6f,1.4f,1,2,"Nose"},{"noseProjection","Projection",.6f,1.4f,1,2,"Nose"},
        {"mouthWidth","Width",.6f,1.5f,1,2,"Mouth"},{"lipFullness","Lip fullness",.6f,1.4f,1,2,"Mouth"},
        {"earSize","Size",.6f,1.5f,1,2,"Ears"},{"earProjection","Projection",.6f,1.4f,1,2,"Ears"},
        {"neckWidth","Width",.65f,1.4f,1,2,"Neck"},{"neckLength","Length",.6f,1.5f,1,2,"Neck"}};
    for(const auto &side:QStringList{"left","right"}) {
        const auto label=side=="left"?QString("Left "):QString("Right ");
        s.append({side+"EyeSize",label+"size",.7f,1.3f,1,2,"Eyes"});
        s.append({side+"EyeHeight",label+"height",.75f,1.25f,1,2,"Eyes"});
        s.append({side+"BrowHeight",label+"height",.75f,1.25f,1,2,"Brows"});
        s.append({side+"EarSize",label+"size",.7f,1.3f,1,2,"Ears"});
    } return s;
}
QVector<Setting> handSettings() {
    QVector<Setting> s{
        {"palmWidth","Width",.65f,1.5f,1,2,"Hand"},{"palmLength","Length",.7f,1.4f,1,2,"Hand"},
        {"palmThickness","Thickness",.65f,1.5f,1,2,"Hand"},{"fingerLength","Length",.65f,1.4f,1,2,"Fingers"},
        {"fingerThickness","Thickness",.65f,1.5f,1,2,"Fingers"},{"fingerSpread","Rest spread",.65f,1.4f,1,2,"Fingers"},
        {"nailLength","Length",.4f,1.6f,1,2,"Nails"},{"nailWidth","Width",.5f,1.5f,1,2,"Nails"},
        {"nailShape","Round to square",0,1,.35f,2,"Nails"},{"wristSize","Size",.65f,1.4f,1,2,"Wrist"},
        {"wristLength","Visible length",0,1,1,2,"Wrist"},{"veins","Vein relief",0,1,0,2,"Details"},
        {"knuckleDefinition","Knuckle relief",0,1,.35f,2,"Details"}};
    for(const auto &finger:QStringList{"thumb","index","middle","ring","little"}) {
        auto label=finger;label[0]=label[0].toUpper();
        s.append({finger+"Length",label+" length",.75f,1.3f,1,2,"Fingers"});
        s.append({finger+"Thickness",label+" thickness",.75f,1.3f,1,2,"Fingers"});
    } return s;
}
const QVector<Setting> &settings(ReferenceStarterKind kind) {
    static const auto head=headSettings();static const auto hand=[] {auto s=handSettings();s.append({"handedness","Left / right",0,1,0,0,"Hand"});return s;}();
    static const auto figure=[] {
        QVector<Setting> s{{"height","Height (m)",1.2f,2.2f,1.75f,2,"Body"},{"build","Build",.65f,1.5f,1,2,"Body"},
            {"definition","Definition",0,1,.35f,2,"Body"},{"headRatio","Head count",5,9,7.5f,2,"Body"},
            {"shoulders","Shoulders",.65f,1.4f,1,2,"Torso"},{"waist","Waist",.65f,1.4f,1,2,"Torso"},
            {"hips","Hips",.7f,1.4f,1,2,"Torso"},{"chestDepth","Chest depth",.7f,1.4f,1,2,"Torso"},{"chestFullness","Chest fullness",0,1,.35f,2,"Torso"},
            {"armLength","Arm length",.7f,1.3f,1,2,"Arms"},{"legLength","Leg length",.78f,1.22f,1,2,"Legs"},
            {"handSize","Hand size",.65f,1.4f,1,2,"Hand"},{"footSize","Foot size",.65f,1.4f,1,2,"Legs"}};
        s+=headSettings();s+=handSettings();return s;
    }();return kind==ReferenceStarterKind::Figure?figure:kind==ReferenceStarterKind::Head?head:hand;
}
float number(const QJsonObject &v,const QString &key,float fallback=1) {return float(v.value(key).toDouble(fallback));}
QJsonObject defaults(ReferenceStarterKind kind) {QJsonObject r;for(const auto&s:settings(kind))r[s.id]=s.initial;return r;}
struct Sample {QVector<QPair<int,float>> sources;};
struct Surface {QVector<Sample> samples;QVector<quint32> indices;QVector<int> capVertices;};
struct Delta {int index;QVector3D value;};
struct Anatomy {
    QVector<QVector3D> positions;QVector<QVector<QPair<int,float>>> weights;QVector<QString> names;QVector<int> parents;
    QVector<QVector<int>> anchors;QHash<QString,QVector<Delta>> targets;std::array<Surface,3> surfaces;
    std::array<QVector<int>,2> eyes;QString error;
};
QVector3D vector(const QJsonValue &v) {const auto a=v.toArray();return {float(a[0].toDouble()),float(a[1].toDouble()),float(a[2].toDouble())};}
const Anatomy &anatomy() {
    static const Anatomy data=[] {
        Anatomy a;initializeReferenceAnatomyResource();
        QFile f(":/reference3d/resources/reference3d/anatomy-v1.bin");if(!f.open(QIODevice::ReadOnly)){f.setFileName(":/reference3d/anatomy-v1.bin");if(!f.open(QIODevice::ReadOnly)){a.error="The bundled anatomical resource is missing.";return a;}}
        const auto packed=f.readAll();
        if(packed.size()<44||packed.size()>16*1024*1024||packed.left(8)!="TOONANA1") {a.error="The bundled anatomical resource is missing or invalid.";return a;}
        const auto *size=reinterpret_cast<const uchar*>(packed.constData()+40);
        const quint32 expected=(quint32(size[0])<<24)|(quint32(size[1])<<16)|(quint32(size[2])<<8)|size[3];
        if(expected>48*1024*1024){a.error="The anatomical resource exceeds its decoded budget.";return a;}
        const auto raw=qUncompress(packed.mid(40));
        if(raw.size()!=expected||QCryptographicHash::hash(raw,QCryptographicHash::Sha256)!=packed.mid(8,32)){a.error="The anatomical resource checksum is invalid.";return a;}
        const auto doc=QJsonDocument::fromJson(raw);const auto root=doc.object();
        if(root.value("version").toInt()!=1){a.error="The anatomical resource version is unsupported.";return a;}
        for(const auto &v:root.value("positions").toArray())a.positions.append(vector(v));
        for(const auto &v:root.value("names").toArray())a.names.append(v.toString());
        for(const auto &v:root.value("parents").toArray())a.parents.append(v.toInt());
        for(const auto &v:root.value("anchors").toArray()){QVector<int> ids;for(const auto&i:v.toArray())ids.append(i.toInt());a.anchors.append(ids);}
        for(const auto &v:root.value("weights").toArray()){QVector<QPair<int,float>> w;for(const auto&i:v.toArray()){const auto p=i.toArray();w.append({p[0].toInt(),float(p[1].toDouble())});}a.weights.append(w);}
        const auto targets=root.value("targets").toObject();
        for(auto i=targets.begin();i!=targets.end();++i){QVector<Delta> d;for(const auto &v:i.value().toArray()){const auto p=v.toArray();d.append({p[0].toInt(),{float(p[1].toDouble()),float(p[2].toDouble()),float(p[3].toDouble())}});}a.targets.insert(i.key(),std::move(d));}
        for(int k=0;k<3;++k){const auto s=root.value(QStringList{"body","head","hand"}[k]).toObject();
            for(const auto&v:s.value("samples").toArray()){Sample p;for(const auto &c:v.toArray()){const auto x=c.toArray();p.sources.append({x[0].toInt(),float(x[1].toDouble())});}a.surfaces[k].samples.append(std::move(p));}
            for(const auto &v:s.value("triangles").toArray())for(const auto&i:v.toArray())a.surfaces[k].indices.append(quint32(i.toInt()));
            for(const auto&v:s.value("capVertices").toArray())a.surfaces[k].capVertices.append(v.toInt());}
        const auto eyes=root.value("eyes").toObject();for(int i=0;i<2;++i)for(const auto&v:eyes.value(i==0?"l":"r").toArray())a.eyes[i].append(v.toInt());
        if(a.positions.size()!=19158||a.names.size()!=52||a.weights.size()!=a.positions.size())a.error="The bundled anatomical layout is invalid.";
        return a;
    }();return data;
}
QVector<QVector3D> joints(const Anatomy&a,const QVector<QVector3D>&p){QVector<QVector3D> r(a.anchors.size());for(int i=0;i<r.size();++i){for(int v:a.anchors[i])r[i]+=p[v];if(!a.anchors[i].isEmpty())r[i]/=float(a.anchors[i].size());}return r;}
struct HandAxes {QVector3D forward,across,volar;};
HandAxes handAxes(const Anatomy &a,const QVector<QVector3D>&points,const QString&side) {
    const auto origin=points[a.names.indexOf(side+"Hand")];
    const auto forward=(points[a.names.indexOf(side+"MiddleProximal")]-origin).normalized();
    auto across=points[a.names.indexOf(side+"LittleProximal")]-points[a.names.indexOf(side+"IndexProximal")];
    across=(across-forward*QVector3D::dotProduct(across,forward)).normalized();
    return {forward,across,QVector3D::crossProduct(across,forward).normalized()*(side=="left"?1.f:-1.f)};
}
float influence(const Anatomy&a,int v,int first,int last){float r=0;for(const auto&w:a.weights[v])if(w.first>=first&&w.first<=last)r+=w.second;return std::clamp(r,0.f,1.f);}
void addTarget(const Anatomy&a,QVector<QVector3D>&p,const QString &id,float weight,int half=0) {
    if(std::abs(weight)<1e-7f)return;const auto it=a.targets.constFind(id);if(it==a.targets.cend())return;
    for(const auto&d:*it)if(!half||a.positions[d.index].x()*half>=0)p[d.index]+=d.value*weight;
}
void signedTarget(const Anatomy&a,QVector<QVector3D>&p,const QString &id,float value,int half=0){if(value) addTarget(a,p,id+(value>0?"-incr":"-decr"),std::min(std::abs(value),1.35f),half);}
void directedTarget(const Anatomy&a,QVector<QVector3D>&p,const QString&id,float value,const QString&positive,const QString&negative,int half=0){if(value)addTarget(a,p,id+(value>0?positive:negative),std::min(std::abs(value),1.35f),half);}
QString familyKey(ReferenceStarterKind family){return family==ReferenceStarterKind::Head?"headMix":"handMix";}
QStringList familyIds(ReferenceStarterKind f){return f==ReferenceStarterKind::Head?QStringList{"balanced","oval","round","long","angular","heart","square","soft","stylized"}:QStringList{"balanced","slender","broad","long","compact","strong","soft"};}
QHash<QString,float> mixWeights(const QJsonObject&shape,ReferenceStarterKind family){QHash<QString,float> r;const auto o=shape.value(familyKey(family)).toObject();float sum=0;for(const auto&id:familyIds(family)){r[id]=number(o,id,0);sum+=r[id];}if(shape.value(familyKey(family)+"Limit").toBool(true)&&sum>1)for(auto&i:r)i/=sum;return r;}
QVector<QVector3D> shapedPositions(const Anatomy&a,ReferenceStarterKind kind,const QJsonObject&shape) {
    auto p=a.positions;auto values=shape;
    const auto hand=mixWeights(shape,ReferenceStarterKind::Hand);
    const QHash<QString,QHash<QString,float>> recipes{
        {"slender",{{"palmWidth",-.12f},{"fingerThickness",-.16f},{"fingerLength",.08f}}},
        {"broad",{{"palmWidth",.23f},{"palmThickness",.14f},{"fingerThickness",.08f}}},
        {"long",{{"palmLength",.10f},{"fingerLength",.23f}}},
        {"compact",{{"palmLength",-.12f},{"fingerLength",-.18f},{"palmWidth",.06f}}},
        {"strong",{{"palmThickness",.20f},{"fingerThickness",.20f},{"knuckleDefinition",.35f}}},
        {"soft",{{"palmWidth",.08f},{"palmThickness",.13f},{"knuckleDefinition",-.25f}}}};
    for(auto it=recipes.begin();it!=recipes.end();++it)for(auto v=it->begin();v!=it->end();++v)values[v.key()]=std::clamp(number(values,v.key())+v.value()*hand.value(it.key()),.3f,2.f);
    if(kind!=ReferenceStarterKind::Hand) {
        const auto hm=mixWeights(shape,ReferenceStarterKind::Head);
        for(const auto&pair:QVector<QPair<QString,QString>>{{"oval","head-oval"},{"round","head-round"},{"long","head-rectangular"},{"angular","head-diamond"},{"heart","head-invertedtriangular"},{"square","head-square"}})addTarget(a,p,"head/"+pair.second,hm.value(pair.first)*.8f);
        signedTarget(a,p,"head/head-fat",hm.value("soft")*.65f-hm.value("angular")*.25f);
        signedTarget(a,p,"chin/chin-width",-hm.value("stylized")*.35f);signedTarget(a,p,"eyes/l-eye-scale",hm.value("stylized")*.35f);signedTarget(a,p,"eyes/r-eye-scale",hm.value("stylized")*.35f);
        for(const auto&pair:QVector<QPair<QString,QString>>{{"headWidth","head/head-scale-horiz"},{"headLength","head/head-scale-vert"},{"headDepth","head/head-scale-depth"},{"chinWidth","chin/chin-width"},{"chinLength","chin/chin-height"},{"chinProjection","chin/chin-prominent"},{"noseWidth","nose/nose-scale-horiz"},{"noseProjection","nose/nose-scale-depth"},{"mouthWidth","mouth/mouth-scale-horiz"},{"lipFullness","mouth/mouth-upperlip-volume"},{"neckWidth","neck/neck-scale-horiz"},{"neckLength","neck/neck-scale-vert"}})signedTarget(a,p,pair.second,(number(values,pair.first)-1)*1.8f);
        signedTarget(a,p,"mouth/mouth-lowerlip-volume",(number(values,"lipFullness")-1)*1.8f);
        for(const auto&axis:QStringList{"horiz","vert","depth"})signedTarget(a,p,"nose/nose-scale-"+axis,(number(values,"noseSize")-1)*1.5f);
        for(int side=0;side<2;++side){const QString s=side?"r":"l",longSide=side?"right":"left";const int half=side?-1:1;
            signedTarget(a,p,"eyes/"+s+"-eye-scale",(number(values,"eyeSize")*number(values,longSide+"EyeSize")-1)*1.5f);
            directedTarget(a,p,"eyes/"+s+"-eye-trans",(number(values,"eyeSpacing")-1)*1.8f,"-out","-in");
            directedTarget(a,p,"eyes/"+s+"-eye-trans",(number(values,longSide+"EyeHeight")-1)*1.6f,"-up","-down");
            directedTarget(a,p,"eyebrows/eyebrows-trans",(number(values,"browHeight")*number(values,longSide+"BrowHeight")-1)*1.5f,"-up","-down",half);
            directedTarget(a,p,"eyebrows/eyebrows-angle",(number(values,"browAngle")-1)*1.5f,"-up","-down",half);
            signedTarget(a,p,"ears/"+s+"-ear-scale",(number(values,"earSize")*number(values,longSide+"EarSize")-1)*1.6f);
            signedTarget(a,p,"ears/"+s+"-ear-wing",(number(values,"earProjection")-1)*1.5f);
        }
    }
    if(kind==ReferenceStarterKind::Figure){
        const float build=number(values,"build")-1,definition=number(values,"definition",.35f)-.35f;
        signedTarget(a,p,"torso/torso-scale-horiz",build*1.3f);signedTarget(a,p,"torso/torso-scale-depth",build);
        signedTarget(a,p,"hip/hip-scale-horiz",build*.8f+(number(values,"hips")-1)*1.8f);
        signedTarget(a,p,"torso/measure-waist-circ",(number(values,"waist")-1)*1.8f);
        signedTarget(a,p,"torso/measure-shoulder-dist",(number(values,"shoulders")-1)*1.8f);
        signedTarget(a,p,"torso/torso-scale-depth",(number(values,"chestDepth")-1)*1.8f);
        // A regional contour recipe removes/adds front chest volume without changing the ribcage width.
        const float fullness=number(values,"chestFullness",.35f);
        for(auto&v:p){
            const float cx=std::abs(v.x()),dy=(v.y()-1.235f)/.16f;
            const float regional=std::exp(-std::pow((cx-.080f)/.085f,4.f)-dy*dy*dy*dy);
            const float front=std::clamp((v.z()-.025f)/.055f,0.f,1.f);
            const float ribcage=.118f+.04f*(v.y()-1.2f);
            const float contour=fullness<.35f ? -(1-fullness/.35f)*std::max(0.f,v.z()-ribcage) : (fullness-.35f)*.085f;
            v.setZ(v.z()+contour*regional*front);
        }
        signedTarget(a,p,"torso/torso-muscle-pectoral",definition*1.3f);signedTarget(a,p,"torso/torso-muscle-dorsi",definition);
        for(const auto&s:QStringList{"l","r"}){
            for(const auto&part:QStringList{"upperarm","lowerarm"}){signedTarget(a,p,"arms/"+s+"-"+part+"-fat",build);signedTarget(a,p,"arms/"+s+"-"+part+"-muscle",definition*1.5f);signedTarget(a,p,"arms/"+s+"-"+part+"-scale-vert",(number(values,"armLength")-1)*1.5f);}
            for(const auto&part:QStringList{"upperleg","lowerleg"}){signedTarget(a,p,"legs/"+s+"-"+part+"-fat",build);signedTarget(a,p,"legs/"+s+"-"+part+"-muscle",definition*1.3f);signedTarget(a,p,"legs/"+s+"-"+part+"-scale-vert",(number(values,"legLength")-1)*1.7f);}
            signedTarget(a,p,"feet/"+s+"-foot-scale",(number(values,"footSize")-1)*1.5f);
        }
    }
    if(kind!=ReferenceStarterKind::Head){
        for(const auto&s:QStringList{"l","r"}){
            signedTarget(a,p,"hands/"+s+"-hand-scale",(number(values,"handSize")-1)*1.6f);
            signedTarget(a,p,"hands/"+s+"-hand-fingers-length",(number(values,"fingerLength")-1)*1.7f);
            signedTarget(a,p,"hands/"+s+"-hand-fingers-diameter",(number(values,"fingerThickness")-1)*1.7f);
            signedTarget(a,p,"hands/"+s+"-hand-fingers-distance",(number(values,"fingerSpread")-1)*1.5f);
        }
        signedTarget(a,p,"hands/measure-wrist-circ",(number(values,"wristSize")-1)*1.6f);
        const auto js=joints(a,p);
        for(const auto&side:QStringList{"left","right"}){
            const int h=a.names.indexOf(side+"Hand"),mid=a.names.indexOf(side+"MiddleProximal");
            const auto origin=js[h];const auto axes=handAxes(a,js,side);const auto forward=axes.forward,volar=axes.volar,lateral=axes.across;Q_UNUSED(mid);
            for(int v=0;v<p.size();++v){const float w=influence(a,v,h,h+15);if(w<=0)continue;auto d=p[v]-origin;
                p[v]+=w*(lateral*QVector3D::dotProduct(d,lateral)*(number(values,"palmWidth")-1)+forward*QVector3D::dotProduct(d,forward)*(number(values,"palmLength")-1)*.7f+volar*QVector3D::dotProduct(d,volar)*(number(values,"palmThickness")-1));
                const float along=QVector3D::dotProduct(d,forward);if(along<0)p[v]+=forward*(-along*(1-number(values,"wristLength"))*.95f*w);
                const float vein=number(values,"veins",0);const float across=QVector3D::dotProduct(d,lateral);
                if(vein>0&&along>0&&along<.07f&&QVector3D::dotProduct(d,volar)<0){const float line=across-.012f*std::sin(along*42);p[v]-=volar*(vein*.0012f*std::exp(-line*line/.000006f)*std::sin(pi*along/.07f)*w);}
            }
            const auto current=joints(a,p);
            for(const auto&finger:QStringList{"Thumb","Index","Middle","Ring","Little"}){
                const QString small=finger.toLower();const int first=a.names.indexOf(side+finger+(finger=="Thumb"?"Metacarpal":"Proximal"));
                const auto start=current[first],axis=(current[first+2]-start).normalized();
                for(int v=0;v<p.size();++v){const float w=influence(a,v,first,first+2);if(w<=0)continue;const auto d=p[v]-start;const auto longitudinal=axis*QVector3D::dotProduct(d,axis),radial=d-longitudinal;
                    p[v]+=w*(longitudinal*(number(values,small+"Length")-1)+radial*(number(values,small+"Thickness")-1));
                    const float knuckle=number(values,"knuckleDefinition",.35f)-.35f;
                    const float dist=std::min((p[v]-current[first+1]).lengthSquared(),(p[v]-current[first+2]).lengthSquared());
                    p[v]+=radial*(knuckle*.35f*std::exp(-dist/.00005f)*w);
                }
            }
        }
    }
    if(kind==ReferenceStarterKind::Figure){
        // Alter head count continuously, then restore the exact requested stature.
        const auto js=joints(a,p);float top=-1e6f,bottom=1e6f;for(const auto&s:a.surfaces[0].samples){const auto v=p[s.sources[0].first];top=std::max(top,v.y());bottom=std::min(bottom,v.y());}
        const float chin=js[5].y()-.014f;const float h=top-chin;const float desired=number(values,"headRatio",7.5f);
        const float factor=std::clamp((top-bottom-h)/(std::max(.02f,h)*(desired-1)),.55f,1.75f);
        for(auto&v:p){
            const float t=std::clamp((v.y()-(chin-.045f))/.065f,0.f,1.f);
            const float blend=t*t*(3-2*t),radial=1+(factor-1)*blend;
            v.setX(v.x()*radial);v.setZ(js[5].z()+(v.z()-js[5].z())*radial);
            if(v.y()>chin)v.setY(chin+(v.y()-chin)*factor);
        }
        top=-1e6f;bottom=1e6f;for(const auto&s:a.surfaces[0].samples){top=std::max(top,p[s.sources[0].first].y());bottom=std::min(bottom,p[s.sources[0].first].y());}
        const float scale=number(values,"height",1.75f)/(top-bottom);for(auto&v:p){v.setY(v.y()-bottom);v*=scale;}
    }
    return p;
}
QVector<QVector3D> normals(const ReferencePrimitive&p,const QVector<QVector3D>&offset={}){
    QVector<QVector3D> n(p.vertices.size());for(int i=0;i<p.indices.size();i+=3){const int a=int(p.indices[i]),b=int(p.indices[i+1]),c=int(p.indices[i+2]);const auto pos=[&](int v){return p.vertices[v].position+(offset.isEmpty()?QVector3D():offset[v]);};const auto cross=QVector3D::crossProduct(pos(b)-pos(a),pos(c)-pos(a));n[a]+=cross;n[b]+=cross;n[c]+=cross;}
    // QVector3D::normalized treats lengths below Qt's fuzzy epsilon as zero.
    // Anatomical triangles in metre units need area-weighted normals below that threshold.
    for(auto&v:n){const float length=std::sqrt(v.lengthSquared());v=length>1e-12f?v/length:QVector3D(0,0,1);}return n;
}
void assignWeights(ReferenceVertex &v,const QHash<int,float>&weights){QVector<QPair<int,float>> sorted;for(auto i=weights.begin();i!=weights.end();++i)if(i.value()>0)sorted.append({i.key(),i.value()});std::sort(sorted.begin(),sorted.end(),[](const auto&a,const auto&b){return a.second==b.second?a.first<b.first:a.second>b.second;});float total=0;for(int i=0;i<std::min(4,int(sorted.size()));++i)total+=sorted[i].second;v.weights={0,0,0,0};for(int i=0;i<std::min(4,int(sorted.size()));++i){v.joints[i]=quint16(sorted[i].first);v.weights[i]=sorted[i].second/total;}}
// A nail guide is sampled from the actual continuous skin, including its
// blend weights. A rigid distal-bone card can detach where that skin bends.
QVector3D closestTriangleCoordinates(const QVector3D&p,const QVector3D&a,const QVector3D&b,const QVector3D&c) {
    const auto ab=b-a,ac=c-a,ap=p-a;const float d1=QVector3D::dotProduct(ab,ap),d2=QVector3D::dotProduct(ac,ap);
    if(d1<=0&&d2<=0)return {1,0,0};const auto bp=p-b;const float d3=QVector3D::dotProduct(ab,bp),d4=QVector3D::dotProduct(ac,bp);
    if(d3>=0&&d4<=d3)return {0,1,0};const float vc=d1*d4-d3*d2;
    if(vc<=0&&d1>=0&&d3<=0){const float v=d1/(d1-d3);return {1-v,v,0};}
    const auto cp=p-c;const float d5=QVector3D::dotProduct(ab,cp),d6=QVector3D::dotProduct(ac,cp);
    if(d6>=0&&d5<=d6)return {0,0,1};const float vb=d5*d2-d1*d6;
    if(vb<=0&&d2>=0&&d6<=0){const float w=d2/(d2-d6);return {1-w,0,w};}
    const float va=d3*d6-d5*d4;
    if(va<=0&&(d4-d3)>=0&&(d5-d6)>=0){const float w=(d4-d3)/((d4-d3)+(d5-d6));return {0,1-w,w};}
    const float inverse=1/(va+vb+vc),v=vb*inverse,w=vc*inverse;return {1-v-w,v,w};
}
ReferenceVertex fittedNailVertex(const ReferencePrimitive&skin,const QVector<int>&triangles,const QVector3D&requested,const QVector3D&dorsal) {
    int selected=-1;QVector3D barycentric;float closest=1e6f;const auto origin=requested+dorsal*.05f,direction=-dorsal;
    for(int triangle:triangles){
        const auto&a=skin.vertices[int(skin.indices[triangle])],&b=skin.vertices[int(skin.indices[triangle+1])],&c=skin.vertices[int(skin.indices[triangle+2])];
        if(QVector3D::dotProduct(a.normal+b.normal+c.normal,dorsal)<=.05f)continue;
        const auto ab=b.position-a.position,ac=c.position-a.position,h=QVector3D::crossProduct(direction,ac);const float determinant=QVector3D::dotProduct(ab,h);
        if(std::abs(determinant)<1e-12f)continue;const float inverse=1/determinant;const auto s=origin-a.position;
        const float u=inverse*QVector3D::dotProduct(s,h);if(u<-.00001f||u>1.00001f)continue;
        const auto q=QVector3D::crossProduct(s,ab);const float v=inverse*QVector3D::dotProduct(direction,q);
        if(v<-.00001f||u+v>1.00001f)continue;const float distance=inverse*QVector3D::dotProduct(ac,q);
        if(distance>=0&&distance<closest){closest=distance;selected=triangle;barycentric={1-u-v,u,v};}
    }
    if(selected<0){
        for(int triangle:triangles){const auto&a=skin.vertices[int(skin.indices[triangle])],&b=skin.vertices[int(skin.indices[triangle+1])],&c=skin.vertices[int(skin.indices[triangle+2])];
            if(QVector3D::dotProduct(a.normal+b.normal+c.normal,dorsal)<=.05f)continue;
            const auto bary=closestTriangleCoordinates(requested,a.position,b.position,c.position);
            const auto point=a.position*bary.x()+b.position*bary.y()+c.position*bary.z();const float distance=(point-requested).lengthSquared();
            if(distance<closest){closest=distance;selected=triangle;barycentric=bary;}
        }
    }
    ReferenceVertex result;
    if(selected<0)return result; // Candidate selection below guarantees a dorsal triangle.
    QHash<int,float> weights;QVector3D normal;result.position={};result.uv={};
    for(int k=0;k<3;++k){const auto&v=skin.vertices[int(skin.indices[selected+k])];const float amount=std::max(0.f,barycentric[k]);result.position+=v.position*amount;normal+=v.normal*amount;result.uv+=v.uv*amount;for(int j=0;j<4;++j)weights[v.joints[j]]+=v.weights[j]*amount;}
    result.normal=normal.normalized();result.position+=result.normal*.00012f;assignWeights(result,weights);
    const auto tangent=QVector3D::crossProduct(std::abs(result.normal.y())<.95f?QVector3D(0,1,0):QVector3D(1,0,0),result.normal).normalized();result.tangent=QVector4D(tangent,1);return result;
}
struct Coordinates {
    QVector3D origin,x{1,0,0},y{0,1,0},z{0,0,1};bool mirrored=false;
    QVector3D point(const QVector3D&p)const {const auto d=p-origin;auto r=QVector3D(QVector3D::dotProduct(d,x),QVector3D::dotProduct(d,y),QVector3D::dotProduct(d,z));if(mirrored)r.setX(-r.x());return r;}
    QVector3D direction(const QVector3D&p)const {auto r=QVector3D(QVector3D::dotProduct(p,x),QVector3D::dotProduct(p,y),QVector3D::dotProduct(p,z));if(mirrored)r.setX(-r.x());return r;}
};
void sphere(ReferencePrimitive&p,QVector3D center,QVector3D axis,QVector3D side,QVector3D front,float length,float width,float depth,int joint){
    const int start=p.vertices.size();constexpr int rings=8,segs=12;
    for(int r=0;r<=rings;++r)for(int s=0;s<=segs;++s){const float t=pi*r/rings,f=2*pi*s/segs;ReferenceVertex v;v.position=center+axis*(std::cos(t)*length)+side*(std::sin(t)*std::cos(f)*width)+front*(std::sin(t)*std::sin(f)*depth);v.normal=(axis*(std::cos(t)/length)+side*(std::sin(t)*std::cos(f)/width)+front*(std::sin(t)*std::sin(f)/depth)).normalized();v.joints[0]=quint16(joint);v.weights[0]=1;v.uv={float(s)/segs,float(r)/rings};p.vertices.append(v);}
    for(int r=0;r<rings;++r)for(int s=0;s<segs;++s){const quint32 a=quint32(start+r*(segs+1)+s),b=a+segs+1;p.indices+={a,a+1,b,a+1,b+1,b};}
}
ReferenceAssetPtr build(ReferenceStarterKind kind,const QJsonObject&shape,QString*error){
    const auto&a=anatomy();if(!a.error.isEmpty()){fail(error,a.error);return {};}
    auto positions=shapedPositions(a,kind,shape);const auto fullJoints=joints(a,positions);Coordinates frame;
    QVector<int> sourceNodes;QVector<int> parents;QVector<QString> semantics;
    if(kind==ReferenceStarterKind::Figure){for(int i=0;i<a.names.size();++i){sourceNodes.append(i);parents.append(a.parents[i]);semantics.append(i?a.names[i]:QString());}}
    else if(kind==ReferenceStarterKind::Head){sourceNodes={0,4,5};parents={-1,0,1};semantics={"","neck","head"};
        float bottom=1e6f;for(const auto&sample:a.surfaces[1].samples){QVector3D p;for(const auto&part:sample.sources)p+=positions[part.first]*part.second;bottom=std::min(bottom,p.y());}
        frame.origin={0,bottom-.018f,0};}
    else {const int hand=a.names.indexOf("leftHand");sourceNodes={0,hand};parents={-1,0};semantics={"","leftHand"};
        for(int i=1;i<=15;++i){sourceNodes.append(hand+i);parents.append(a.parents[hand+i]-hand+1);semantics.append(a.names[hand+i]);}
        frame.origin=fullJoints[hand]-(fullJoints[a.names.indexOf("leftMiddleProximal")]-fullJoints[hand]).normalized()*.047f;
        const auto axes=handAxes(a,fullJoints,"left");frame.y=axes.forward;frame.x=axes.across;frame.z=axes.volar;
        frame.mirrored=number(shape,"handedness",0)>=.5f;if(frame.mirrored)for(auto&s:semantics)s.replace("left","right");}
    QHash<int,int> remap;for(int i=0;i<sourceNodes.size();++i)remap.insert(sourceNodes[i],i);
    ReferenceAsset asset;asset.name=kind==ReferenceStarterKind::Figure?"Drawing figure":kind==ReferenceStarterKind::Head?"Head study":"Hand study";
    QVector<QVector3D> world;
    for(int i=0;i<sourceNodes.size();++i){const auto pos=i?frame.point(fullJoints[sourceNodes[i]]):QVector3D();ReferenceNode n;n.name=i?semantics[i]:"Root";n.parentIndex=parents[i];n.translation=parents[i]<0?pos:pos-world[parents[i]];world.append(pos);asset.nodes.append(n);if(i)asset.humanoidBones.insert(semantics[i],i);}
    ReferenceMaterial clay;clay.name="Neutral anatomical plaster";clay.baseColor=QColor(190,185,176);clay.roughness=.86f;clay.doubleSided=true;
    ReferenceMaterial eye=clay;eye.name="Eyes and nail planes";eye.baseColor=QColor(214,211,203);ReferenceMaterial pupil=clay;pupil.name="Iris guides";pupil.baseColor=QColor(76,72,67);asset.materials={clay,eye,pupil};
    ReferenceMesh mesh;mesh.name="Continuous CC0 anatomical surface";ReferencePrimitive surface;surface.materialIndex=0;const auto&s=a.surfaces[int(kind)];surface.indices=s.indices;
    for(const auto&sample:s.samples){ReferenceVertex v;QHash<int,float> weights;QVector3D p;
        for(const auto&part:sample.sources){p+=positions[part.first]*part.second;for(const auto&w:a.weights[part.first]){int joint=remap.value(w.first,-1);if(joint<0)joint=kind==ReferenceStarterKind::Head?1:0;weights[joint]+=w.second*part.second;}}
        v.position=frame.point(p);assignWeights(v,weights);surface.vertices.append(v);}
    if(!s.capVertices.isEmpty()){float cutY=0;for(int i:s.capVertices)cutY+=surface.vertices[i].position.y();cutY/=float(s.capVertices.size());for(int i:s.capVertices)surface.vertices[i].position.setY(cutY);}
    if(frame.mirrored)for(int i=0;i<surface.indices.size();i+=3)std::swap(surface.indices[i+1],surface.indices[i+2]);
    const auto ns=normals(surface);for(int i=0;i<ns.size();++i){surface.vertices[i].normal=ns[i];const auto t=QVector3D::crossProduct(std::abs(ns[i].y())<.95f?QVector3D(0,1,0):QVector3D(1,0,0),ns[i]).normalized();surface.vertices[i].tangent=QVector4D(t,1);}
    if(kind!=ReferenceStarterKind::Hand){
        for(int expression=0;expression<3;++expression){auto deformed=positions;
            if(expression==0)addTarget(a,deformed,"mouth/mouth-angles-up",.85f);
            if(expression==1){signedTarget(a,deformed,"eyes/l-eye-height1",-1.1f);signedTarget(a,deformed,"eyes/r-eye-height1",-1.1f);signedTarget(a,deformed,"eyes/l-eye-height2",-1);signedTarget(a,deformed,"eyes/r-eye-height2",-1);}
            if(expression==2)addTarget(a,deformed,"chin/chin-jaw-drop-incr",.85f);
            ReferenceMorphTarget target;target.name=QStringList{"Smile","Blink","Jaw open"}[expression];
            for(int i=0;i<s.samples.size();++i){QVector3D p;for(const auto&part:s.samples[i].sources)p+=deformed[part.first]*part.second;target.positionDeltas.append(frame.point(p)-surface.vertices[i].position);}
            for(int v:s.capVertices)target.positionDeltas[v].setY(0);
            const auto n=normals(surface,target.positionDeltas);for(int i=0;i<n.size();++i)target.normalDeltas.append(n[i]-ns[i]);surface.morphTargets.append(std::move(target));}
        mesh.defaultMorphWeights={0,0,0};
    }
    mesh.primitives.append(std::move(surface));ReferencePrimitive details;details.materialIndex=1;ReferencePrimitive pupils;pupils.materialIndex=2;
    if(kind!=ReferenceStarterKind::Hand){const int bone=asset.humanoidBones.value("head");
        for(int side=0;side<2;++side){QVector3D center;for(int i:a.eyes[side])center+=positions[i];center/=float(a.eyes[side].size());center=frame.point(center);
            QVector3D baseMin(1e6f,1e6f,1e6f),baseMax(-1e6f,-1e6f,-1e6f),currentMin=baseMin,currentMax=baseMax;
            for(int i:a.eyes[side])for(int axis=0;axis<3;++axis){baseMin[axis]=std::min(baseMin[axis],a.positions[i][axis]);baseMax[axis]=std::max(baseMax[axis],a.positions[i][axis]);currentMin[axis]=std::min(currentMin[axis],positions[i][axis]);currentMax[axis]=std::max(currentMax[axis],positions[i][axis]);}
            const auto originalExtent=baseMax-baseMin,currentExtent=currentMax-currentMin;
            const float r=.0118f*number(shape,"eyeSize")*number(shape,side?"rightEyeSize":"leftEyeSize");
            const QVector3D radii(r*currentExtent.x()/originalExtent.x(),r*currentExtent.y()/originalExtent.y(),r*currentExtent.z()/originalExtent.z());
            sphere(details,center,{0,1,0},{1,0,0},{0,0,1},radii.y(),radii.x(),radii.z(),bone);
            sphere(pupils,center+QVector3D(0,0,radii.z()*.91f),{0,1,0},{1,0,0},{0,0,1},radii.y()*.33f,radii.x()*.33f,radii.z()*.12f,bone);}
    }
    if(kind!=ReferenceStarterKind::Head){
        for(const auto&side:QStringList{"left","right"})for(const auto&finger:QStringList{"Thumb","Index","Middle","Ring","Little"}){
            const auto sem=side+finger+"Distal";if(!asset.humanoidBones.contains(sem))continue;const int bone=asset.humanoidBones[sem];const int source=sourceNodes[bone];
            const auto previous=world[parents[bone]],start=world[bone],axis=(start-previous).normalized();
            const auto nativeAxes=handAxes(a,fullJoints,kind==ReferenceStarterKind::Hand?QString("left"):side);
            const auto desiredDorsal=-frame.direction(nativeAxes.volar);const auto dorsal=(desiredDorsal-axis*QVector3D::dotProduct(desiredDorsal,axis)).normalized();
            const auto lateral=QVector3D::crossProduct(axis,dorsal).normalized()*(frame.mirrored?-1.f:1.f);
            float extent=.012f,thickness=0,halfWidth=.004f;QVector<QVector3D> local;
            for(const auto&v:mesh.primitives[0].vertices){float weight=0;for(int i=0;i<4;++i)if(v.joints[i]==bone)weight+=v.weights[i];if(weight<.4f)continue;const auto d=v.position-start;extent=std::max(extent,QVector3D::dotProduct(d,axis));local.append(d);}
            for(const auto&d:local)if(std::abs(QVector3D::dotProduct(d,axis)-extent*.55f)<extent*.22f){thickness=std::max(thickness,QVector3D::dotProduct(d,dorsal));halfWidth=std::max(halfWidth,std::abs(QVector3D::dotProduct(d,lateral)));}
            Q_UNUSED(source);const float width=halfWidth*.56f*number(shape,"nailWidth");const float length=extent*.24f*number(shape,"nailLength");
            QVector<int> candidates;const auto&skin=mesh.primitives[0];
            for(int t=0;t<skin.indices.size();t+=3){float weight=0;for(int corner=0;corner<3;++corner){const auto&v=skin.vertices[int(skin.indices[t+corner])];for(int j=0;j<4;++j)if(v.joints[j]==bone)weight=std::max(weight,v.weights[j]);}if(weight>.15f)candidates.append(t);}
            const float squared=number(shape,"nailShape",.35f);const auto center=start+axis*(extent*.55f)+dorsal*thickness;const int base=details.vertices.size();constexpr int rows=6,cols=8;
            for(int r=0;r<=rows;++r)for(int c=0;c<=cols;++c){const float u=(float(c)/cols*2-1),v=(float(r)/rows*2-1);const float edge=(.18f+.82f*std::sqrt(std::max(0.f,1-v*v)))*(1-squared)+squared;
                details.vertices.append(fittedNailVertex(skin,candidates,center+axis*(v*length)+lateral*(u*width*edge),dorsal));}
            for(int r=0;r<rows;++r)for(int c=0;c<cols;++c){const quint32 x=quint32(base+r*(cols+1)+c),y=x+cols+1;const auto cross=QVector3D::crossProduct(details.vertices[int(x+1)].position-details.vertices[int(x)].position,details.vertices[int(y)].position-details.vertices[int(x)].position);if(QVector3D::dotProduct(cross,dorsal)>0)details.indices+={x,x+1,y,x+1,y+1,y};else details.indices+={x,y,x+1,x+1,y,y+1};}
        }
    }
    for(auto *p:{&details,&pupils})if(!p->vertices.isEmpty()){for(int i=0;i<mesh.defaultMorphWeights.size();++i){ReferenceMorphTarget m;m.name=QStringList{"Smile","Blink","Jaw open"}[i];m.positionDeltas.fill({},p->vertices.size());p->morphTargets.append(std::move(m));}mesh.primitives.append(std::move(*p));}
    asset.meshes.append(std::move(mesh));ReferenceSkin skin;skin.name="Anatomical semantic skeleton";skin.skeletonRoot=0;
    for(int i=0;i<world.size();++i){skin.joints.append(i);QMatrix4x4 inverse;inverse.translate(-world[i]);skin.inverseBindMatrices.append(inverse);}asset.skins.append(skin);
    ReferenceNode node;node.name="Anatomical study surface";node.meshIndex=0;node.skinIndex=0;const int meshNode=asset.nodes.size();asset.nodes.append(node);
    for(int i=0;i<asset.meshes[0].defaultMorphWeights.size();++i){ReferenceExpression e;e.name=QStringList{"Smile","Blink","Jaw open"}[i];e.bindings.append({meshNode,i,1});asset.expressions.append(e);}
    QJsonObject frames;
    for(const auto&side:QStringList{"left","right"})if(asset.humanoidBones.contains(side+"Hand")){
        const auto nativeAxes=handAxes(a,fullJoints,kind==ReferenceStarterKind::Hand?QString("left"):side);
        const auto volar=frame.direction(nativeAxes.volar).normalized();frames[side]=QJsonObject{{"volar",QJsonArray{volar.x(),volar.y(),volar.z()}}};
    }
    asset.metadata={{"generator","toonpro-reference-starter-v1"},{"geometryRevision",2},{"starterKind",int(kind)},{"shape",shape},{"handFrames",frames},
        {"quality","Anatomical drawing studies; artist acceptance pending. Original bounded shape recipes, no anatomical accuracy certification."},
        {"provenance",QJsonObject{{"source","https://github.com/makehumancommunity/mpfb2"},{"revision","6ff1eb358f6c1959ced77e79e6dd2406d9a71f49"},{"license","CC0-1.0"},{"adaptation","Continuous base, sparse regional targets, four-influence skin, capped study extractions; Toon Pro shape mixtures and eye/nail guides."}}}};
    return finalizeReferenceAsset(std::move(asset),error);
}
std::optional<ReferenceStarterKind> starterKind(const ReferenceAsset &asset){if(asset.metadata.value("generator")!="toonpro-reference-starter-v1")return {};const int k=asset.metadata.value("starterKind").toInt(-1);if(k<0||k>2)return {};return ReferenceStarterKind(k);}
bool replaceShape(ReferenceInstance &target,ReferenceAssetPtr replacement,QString*error){
    if(!replacement||!target.asset)return fail(error,"A valid shape and target are required.");auto invalid=validateReferenceInstance(target);if(!invalid.isEmpty())return fail(error,invalid);
    auto next=target;const auto previous=next.asset;next.asset=std::move(replacement);
    if(previous->nodes.size()!=next.asset->nodes.size())return fail(error,"The shape has an incompatible skeleton.");
    const bool mirror=starterKind(*previous)==ReferenceStarterKind::Hand&&number(previous->metadata.value("shape").toObject(),"handedness",0)!=number(next.asset->metadata.value("shape").toObject(),"handedness",0);
    for(auto i=next.poseRotations.begin();i!=next.poseRotations.end();++i){if(mirror)i.value()=QQuaternion(i.value().scalar(),i.value().x(),-i.value().y(),-i.value().z());else if(previous->nodes[i.key()].rotation!=next.asset->nodes[i.key()].rotation)i.value()=(next.asset->nodes[i.key()].rotation*previous->nodes[i.key()].rotation.conjugated()*i.value()).normalized();}
    for(auto i=next.poseTranslations.begin();i!=next.poseTranslations.end();++i){auto delta=i.value()-previous->nodes[i.key()].translation;if(mirror)delta.setX(-delta.x());i.value()=next.asset->nodes[i.key()].translation+delta;}
    invalid=validateReferenceInstance(next);if(!invalid.isEmpty())return fail(error,invalid);target=std::move(next);return true;
}
bool supportsFamily(ReferenceStarterKind kind,ReferenceStarterKind family){return family!=ReferenceStarterKind::Figure&&(kind==ReferenceStarterKind::Figure||kind==family);}
}
ReferenceAssetPtr createReferenceStarter(ReferenceStarterKind kind,QString*error){if(int(kind)<0||int(kind)>2){fail(error,"Choose a supported reference starter kind.");return {};}return build(kind,defaults(kind),error);}
QVector<ReferenceStarterPreset> referenceStarterPresets(){QVector<ReferenceStarterPreset> r;for(const auto&kind:QVector<ReferenceStarterKind>{ReferenceStarterKind::Figure,ReferenceStarterKind::Head,ReferenceStarterKind::Hand}){
    const auto prefix=kind==ReferenceStarterKind::Figure?QString("figure"):kind==ReferenceStarterKind::Head?QString("head"):QString("hand");const auto ids=kind==ReferenceStarterKind::Figure?QStringList{"balanced","slender","athletic","rounded"}:familyIds(kind);
    for(const auto&id:ids){auto label=id;label[0]=label[0].toUpper();r.append({prefix+"/"+id,label+(kind==ReferenceStarterKind::Figure?" figure":kind==ReferenceStarterKind::Head?" head":" hand"),"CC0 anatomical base with a Toon Pro proportion recipe",kind});}}return r;}
ReferenceAssetPtr createReferenceStarter(const QString&id,QString*error){const auto presets=referenceStarterPresets();const auto p=std::find_if(presets.begin(),presets.end(),[&](const auto&p){return p.id==id;});if(p==presets.end()){fail(error,"Choose an available study preset.");return {};}
    ReferenceInstance i;i.id="starter";i.asset=createReferenceStarter(p->kind,error);if(!i.asset)return {};i.name=i.asset->name;if(!applyReferenceShapePreset(i,id,error))return {};return i.asset;}
QVector<ReferenceStarter> referenceStarters(){static const QVector<ReferenceStarter> r{{"figure","Drawing figure",ReferenceStarterKind::Figure,createReferenceStarter(ReferenceStarterKind::Figure)},{"head","Head study",ReferenceStarterKind::Head,createReferenceStarter(ReferenceStarterKind::Head)},{"hand","Hand study",ReferenceStarterKind::Hand,createReferenceStarter(ReferenceStarterKind::Hand)}};return r;}
QVector<ReferenceShapeParameter> referenceBodyShapeParameters(const ReferenceInstance&i){QVector<ReferenceShapeParameter> r;if(!i.asset)return r;const auto kind=starterKind(*i.asset);if(!kind)return r;const auto values=i.asset->metadata.value("shape").toObject();for(const auto&s:settings(*kind))r.append({s.id,s.name,s.min,s.max,number(values,s.id,s.initial),s.decimals,s.group});return r;}
bool setReferenceBodyShapeValues(ReferenceInstance&i,const QHash<QString,float>&changes,QString*error){if(!i.asset)return fail(error,"Choose a customizable anatomical study.");const auto k=starterKind(*i.asset);if(!k)return fail(error,"This imported character has no authored shape recipe.");auto values=defaults(*k);const auto old=i.asset->metadata.value("shape").toObject();for(auto j=old.begin();j!=old.end();++j)values[j.key()]=j.value();const auto&options=settings(*k);
    for(auto v=changes.begin();v!=changes.end();++v){const auto s=std::find_if(options.begin(),options.end(),[&](const auto&s){return s.id==v.key();});if(s==options.end()||!std::isfinite(v.value())||v.value()<s->min||v.value()>s->max||(s->decimals==0&&v.value()!=std::round(v.value())))return fail(error,"The shape value is unsupported or outside its range.");values[v.key()]=v.value();}
    if(changes.isEmpty())return true;return replaceShape(i,build(*k,values,error),error);}
bool setReferenceBodyShape(ReferenceInstance&i,const QString&id,float value,QString*error){return setReferenceBodyShapeValues(i,{{id,value}},error);}
QVector<ReferenceShapeFamilyWeight> referenceShapeFamilyMix(const ReferenceInstance&i,ReferenceStarterKind f){QVector<ReferenceShapeFamilyWeight> r;if(!i.asset)return r;const auto k=starterKind(*i.asset);if(!k||!supportsFamily(*k,f))return r;const auto shape=i.asset->metadata.value("shape").toObject();const auto stored=shape.value(familyKey(f)).toObject();for(const auto&id:familyIds(f)){auto label=id;label[0]=label[0].toUpper();r.append({id,label,number(stored,id,0)});}return r;}
bool referenceShapeFamilyMixLimited(const ReferenceInstance&i,ReferenceStarterKind f){return !i.asset||i.asset->metadata.value("shape").toObject().value(familyKey(f)+"Limit").toBool(true);}
bool setReferenceShapeFamilyMix(ReferenceInstance&i,ReferenceStarterKind f,const QHash<QString,float>&weights,bool limit,QString*error){if(!i.asset)return fail(error,"Choose an anatomical study.");const auto k=starterKind(*i.asset);if(!k||!supportsFamily(*k,f))return fail(error,"That study does not contain this shape family.");const auto ids=familyIds(f);QJsonObject mix;for(const auto&id:ids)mix[id]=0;for(auto v=weights.begin();v!=weights.end();++v){if(!ids.contains(v.key())||!std::isfinite(v.value())||v.value()<0||v.value()>1)return fail(error,"Mixture weights must be between zero and one.");mix[v.key()]=v.value();}auto shape=i.asset->metadata.value("shape").toObject();shape[familyKey(f)]=mix;shape[familyKey(f)+"Limit"]=limit;return replaceShape(i,build(*k,shape,error),error);}
bool applyReferenceShapePreset(ReferenceInstance&i,const QString&id,QString*error){if(!i.asset)return fail(error,"Choose an anatomical study.");const auto k=starterKind(*i.asset);const auto presets=referenceStarterPresets();const auto p=std::find_if(presets.begin(),presets.end(),[&](const auto&p){return p.id==id;});if(!k||p==presets.end()||(*k!=p->kind&&!supportsFamily(*k,p->kind)))return fail(error,"The preset belongs to a different shape family.");const auto variant=id.section('/',1);
    if(p->kind!=ReferenceStarterKind::Figure)return setReferenceShapeFamilyMix(i,p->kind,{{variant,1}},true,error);
    QHash<QString,float> values{{"build",1},{"definition",.35f},{"waist",1},{"shoulders",1},{"hips",1},{"chestFullness",.35f}};
    if(variant=="slender")values={{"build",.78f},{"definition",.25f},{"waist",.88f},{"shoulders",.95f},{"hips",.95f},{"chestFullness",0}};
    if(variant=="athletic")values={{"build",1.08f},{"definition",.9f},{"waist",.9f},{"shoulders",1.12f},{"hips",1},{"chestFullness",0}};
    if(variant=="rounded")values={{"build",1.3f},{"definition",.12f},{"waist",1.15f},{"shoulders",1},{"hips",1.14f},{"chestFullness",.8f}};
    return setReferenceBodyShapeValues(i,values,error);}
bool applyReferenceBuiltinShape(const ReferenceInstance&source,ReferenceInstance&target,QString*error){if(!source.asset||!target.asset)return fail(error,"Choose a source shape and target study.");const auto a=starterKind(*source.asset),b=starterKind(*target.asset);if(!a||!b||*a!=*b)return fail(error,"This shape material belongs to a different study family.");return replaceShape(target,source.asset,error);}
} // namespace toon
