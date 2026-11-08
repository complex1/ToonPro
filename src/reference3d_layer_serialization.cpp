#include "reference3d_layer_serialization.h"
#include <QJsonDocument>
#include <QMutex>
#include <QMutexLocker>
#include <QtEndian>
#include <cmath>
#include <algorithm>
#include <stdexcept>

namespace toon {
namespace {
void require(bool valid,const char *message){if(!valid)throw QString::fromUtf8(message);}
QJsonArray vector(QVector3D v){return {v.x(),v.y(),v.z()};}
QJsonArray rotation(QQuaternion q){return {q.scalar(),q.x(),q.y(),q.z()};}
QJsonArray color(QColor c){return {c.redF(),c.greenF(),c.blueF(),c.alphaF()};}
float number(const QJsonValue &v){require(v.isDouble()&&std::isfinite(v.toDouble()),"Invalid numeric reference-layer value.");const auto n=float(v.toDouble());require(std::isfinite(n),"Reference-layer number is out of range.");return n;}
QVector3D readVector(const QJsonValue &v){const auto a=v.toArray();require(v.isArray()&&a.size()==3,"Invalid reference-layer vector.");return {number(a[0]),number(a[1]),number(a[2])};}
QQuaternion readRotation(const QJsonValue &v){const auto a=v.toArray();require(v.isArray()&&a.size()==4,"Invalid reference-layer rotation.");return {number(a[0]),number(a[1]),number(a[2]),number(a[3])};}
QColor readColor(const QJsonValue &v){const auto a=v.toArray();require(v.isArray()&&a.size()==4,"Invalid reference-layer light color.");return QColor::fromRgbF(number(a[0]),number(a[1]),number(a[2]),number(a[3]));}
bool boolean(const QJsonValue &v){require(v.isBool(),"Invalid reference-layer switch.");return v.toBool();}
int nodeKey(const QString &key){bool ok=false;const int n=key.toInt(&ok);require(ok&&n>=0&&n<maxReferenceNodes&&QString::number(n)==key,"Invalid reference-layer node index.");return n;}
template<class T,class Encode> QJsonObject map(const QHash<int,T>&values,Encode encode){QJsonObject result;for(auto it=values.cbegin();it!=values.cend();++it)result.insert(QString::number(it.key()),encode(it.value()));return result;}
QJsonArray weights(const QVector<float>&values){QJsonArray out;for(float value:values)out.append(value);return out;}
bool fail(QString *error,const QString &message){if(error)*error=message;return false;}
}
QJsonObject referenceLayerSceneToJson(const ReferenceScene &scene){
    QJsonArray instances;
    for(const auto &i:scene.instances){QJsonObject expressions;for(auto e=i.expressionWeights.cbegin();e!=i.expressionWeights.cend();++e)expressions.insert(e.key(),e.value());
        instances.append(QJsonObject{{"id",i.id},{"name",i.name},{"assetId",i.asset?i.asset->id:QString()},
            {"translation",vector(i.translation)},{"rotation",rotation(i.rotation)},{"scale",vector(i.scale)},
            {"visible",i.visible},{"locked",i.locked},{"poseRotations",map(i.poseRotations,rotation)},
            {"poseTranslations",map(i.poseTranslations,vector)},{"poseScales",map(i.poseScales,vector)},
            {"morphOverrides",map(i.morphOverrides,weights)},{"expressions",expressions}});}
    const auto &c=scene.camera;const auto &l=scene.light;
    return {{"version",1},{"name",scene.name},{"instances",instances},{"textured",scene.textured},
        {"camera",QJsonObject{{"target",vector(c.target)},{"yaw",c.yaw},{"pitch",c.pitch},{"distance",c.distance},
            {"fieldOfView",c.fieldOfView},{"orthographic",c.orthographic},{"orthoScale",c.orthoScale}}},
        {"light",QJsonObject{{"direction",vector(l.direction)},{"color",color(l.color)},
            {"intensity",l.intensity},{"ambientIntensity",l.ambientIntensity},{"shadows",l.shadows}}}};
}
std::optional<ReferenceScene> referenceLayerSceneFromJson(const QJsonObject &json,const QHash<QString,ReferenceAssetPtr>&assets,QString *error){
    try{require(json.value("version").toInt(-1)==1,"Unsupported reference-layer scene version.");
        require(json.value("instances").isArray()&&json.value("instances").toArray().size()<=maxReferenceInstances,"Invalid reference-layer model collection.");
        ReferenceScene scene;scene.name=json.value("name").toString();scene.textured=boolean(json.value("textured"));
        for(const auto &value:json.value("instances").toArray()){
            require(value.isObject(),"Invalid reference-layer model.");const auto o=value.toObject();ReferenceInstance i;
            i.id=o.value("id").toString();i.name=o.value("name").toString();i.asset=assets.value(o.value("assetId").toString());require(bool(i.asset),"A reference layer requires a missing model resource.");
            i.translation=readVector(o.value("translation"));i.rotation=readRotation(o.value("rotation"));i.scale=readVector(o.value("scale"));i.visible=boolean(o.value("visible"));i.locked=boolean(o.value("locked"));
            for(const auto &name:{"poseRotations","poseTranslations","poseScales","morphOverrides","expressions"})require(o.value(name).isObject(),"Invalid reference-layer pose mapping.");
            const auto rotations=o.value("poseRotations").toObject();for(auto p=rotations.begin();p!=rotations.end();++p)i.poseRotations.insert(nodeKey(p.key()),readRotation(p.value()));
            const auto translations=o.value("poseTranslations").toObject();for(auto p=translations.begin();p!=translations.end();++p)i.poseTranslations.insert(nodeKey(p.key()),readVector(p.value()));
            const auto scales=o.value("poseScales").toObject();for(auto p=scales.begin();p!=scales.end();++p)i.poseScales.insert(nodeKey(p.key()),readVector(p.value()));
            const auto morphs=o.value("morphOverrides").toObject();for(auto p=morphs.begin();p!=morphs.end();++p){require(p.value().isArray()&&p.value().toArray().size()<=maxReferenceMorphTargets,"Invalid reference-layer morph weights.");QVector<float> values;for(const auto &weight:p.value().toArray())values.append(number(weight));i.morphOverrides.insert(nodeKey(p.key()),values);}
            const auto expressions=o.value("expressions").toObject();for(auto p=expressions.begin();p!=expressions.end();++p)i.expressionWeights.insert(p.key(),number(p.value()));
            scene.instances.append(std::move(i));
        }
        const auto c=json.value("camera").toObject();require(json.value("camera").isObject(),"Missing reference-layer camera.");
        scene.camera.target=readVector(c.value("target"));scene.camera.yaw=number(c.value("yaw"));scene.camera.pitch=number(c.value("pitch"));scene.camera.distance=number(c.value("distance"));scene.camera.fieldOfView=number(c.value("fieldOfView"));scene.camera.orthographic=boolean(c.value("orthographic"));scene.camera.orthoScale=number(c.value("orthoScale"));
        const auto l=json.value("light").toObject();require(json.value("light").isObject(),"Missing reference-layer light.");
        scene.light.direction=readVector(l.value("direction"));scene.light.color=readColor(l.value("color"));scene.light.intensity=number(l.value("intensity"));scene.light.ambientIntensity=number(l.value("ambientIntensity"));scene.light.shadows=boolean(l.value("shadows"));
        const auto problem=validateReferenceScene(scene);if(!problem.isEmpty())throw problem;if(error)error->clear();return scene;
    }catch(const QString &message){if(error)*error=message;return {};}
}
QJsonArray referenceLayerAssetsToJson(const QHash<QString,ReferenceAssetPtr>&assets){
    static QMutex mutex;static QHash<QString,QJsonObject> cache;static qint64 cacheBytes=0;
    QMutexLocker lock(&mutex);QJsonArray result;auto ids=assets.keys();std::sort(ids.begin(),ids.end());
    for(const auto &id:ids){auto descriptor=cache.value(id);if(descriptor.isEmpty()){
        QString error;const auto bytes=serializeReferenceAsset(*assets.value(id),&error);if(bytes.isEmpty())return {};
        const auto compressed=qCompress(bytes,6);descriptor={{"id",id},{"bytes",bytes.size()},{"encoding","qcompress-base64"},{"data",QString::fromLatin1(compressed.toBase64())}};
        const qint64 encodedBytes=((compressed.size()+2)/3)*4;if(cacheBytes+encodedBytes>64LL*1024*1024){cache.clear();cacheBytes=0;}cache.insert(id,descriptor);cacheBytes+=encodedBytes;
    }result.append(descriptor);}return result;
}
bool referenceLayerAssetsFromJson(const QJsonArray&json,QHash<QString,ReferenceAssetPtr>*assets,QString*error){
    try{require(assets&&json.size()<=64,"A project supports at most 64 reference model resources.");QHash<QString,ReferenceAssetPtr> result;qint64 total=0;
        for(const auto &value:json){require(value.isObject(),"Invalid reference model resource.");const auto o=value.toObject();const auto id=o.value("id").toString();require(!id.isEmpty()&&!result.contains(id)&&o.value("encoding")=="qcompress-base64"&&o.value("data").isString(),"Invalid or duplicate reference model resource.");
            const auto bytes=o.value("bytes").toDouble(-1);require(std::isfinite(bytes)&&bytes>0&&bytes<=maxReferenceAssetBytes&&std::floor(bytes)==bytes,"Invalid reference model resource size.");total+=qint64(bytes);require(total<=256LL*1024*1024,"Reference models exceed the 256 MiB source-data budget.");
            const auto encoded=o.value("data").toString().toLatin1();require(encoded.size()<=maxReferenceAssetBytes*2,"Reference model resource is oversized.");const auto decoded=QByteArray::fromBase64Encoding(encoded,QByteArray::AbortOnBase64DecodingErrors);require(bool(decoded)&&decoded.decoded.size()>=4,"Damaged reference model encoding.");
            require(qFromBigEndian<quint32>(decoded.decoded.constData())==quint32(bytes),"Reference model compressed size does not match.");const auto canonical=qUncompress(decoded.decoded);require(canonical.size()==qint64(bytes),"Damaged compressed reference model.");QString problem;const auto asset=deserializeReferenceAsset(canonical,&problem);if(!asset)throw problem;require(asset->id==id,"Reference model content does not match its identifier.");result.insert(id,asset);
        }*assets=std::move(result);if(error)error->clear();return true;
    }catch(const QString&message){return fail(error,message);}
}
bool referenceLayerScenesEqual(const ReferenceScene&a,const ReferenceScene&b){return referenceLayerSceneToJson(a)==referenceLayerSceneToJson(b);}
} // namespace toon
