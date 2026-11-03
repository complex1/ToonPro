#include "scene_model_io.h"
#include <QBuffer>
#include <QCryptographicHash>
#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QQuaternion>
#include <QSet>
#include <QUrl>
#include <QtEndian>
#include <cmath>
#include <cstring>
#include <functional>
#include <new>

namespace toon {
namespace {
constexpr auto magic = "TOON3D01";
[[noreturn]] void bad(const QString &message) { throw message; }
void ensure(bool condition,const QString &message) { if(!condition)bad(message); }
void setup(QDataStream &s) { s.setVersion(QDataStream::Qt_6_0);s.setByteOrder(QDataStream::LittleEndian);s.setFloatingPointPrecision(QDataStream::SinglePrecision); }
QByteArray readFile(const QString &path,qint64 maxBytes=maxModel3DBytes) {
    QFile f(path);ensure(f.open(QIODevice::ReadOnly),"Cannot open model resource: "+QFileInfo(path).fileName());ensure(f.size()>0 && f.size()<=maxBytes,"Model resource exceeds its size limit or is empty.");auto b=f.readAll();ensure(b.size()==f.size(),"Could not read the complete model resource.");return b;
}
QImage decodeTexture(const QByteArray &bytes,qint64 &pixels) {
    QBuffer buffer;buffer.setData(bytes);buffer.open(QIODevice::ReadOnly);QImageReader reader(&buffer);
    const auto format=reader.format().toLower();ensure(format=="png" || format=="jpeg" || format=="jpg","Only PNG and JPEG model textures are supported.");
    const auto size=reader.size();ensure(size.isValid() && size.width()<=4096 && size.height()<=4096,"Model texture exceeds 4096 pixels per side.");
    pixels+=qint64(size.width())*size.height();ensure(pixels<=maxModel3DTexturePixels,"Model exceeds its 16-megapixel texture budget.");
    QImage image=reader.read();ensure(!image.isNull(),"Could not decode a model texture.");image=image.convertToFormat(QImage::Format_RGBA8888);image.setDevicePixelRatio(1);for(const auto &key:image.textKeys())image.setText(key,{});return image;
}
void writeString(QDataStream &s,const QString &text) { auto bytes=text.toUtf8();s<<quint32(bytes.size());s.writeRawData(bytes.constData(),int(bytes.size())); }
QString readString(QDataStream &s) { quint32 n=0;s>>n;ensure(n<=1024 && n<=s.device()->bytesAvailable(),"Invalid model name.");QByteArray b(n,Qt::Uninitialized);ensure(s.readRawData(b.data(),int(n))==int(n),"Truncated model name.");QString v=QString::fromUtf8(b);ensure(v.toUtf8()==b,"Invalid model name encoding.");return v; }
void writeVector(QDataStream &s,const QVector3D &p){s<<p.x()<<p.y()<<p.z();}
QVector3D readVector(QDataStream &s){float x=0,y=0,z=0;s>>x>>y>>z;return {x,y,z};}
QByteArray payloadFor(const Model3D &model) {
    auto validation=validateModel3D(model);ensure(validation.isEmpty(),validation);
    QByteArray bytes;QDataStream s(&bytes,QIODevice::WriteOnly);setup(s);writeString(s,model.name);
    QVector<QByteArray> images;QHash<qint64,int> textureIndex;
    for(const auto &mesh:model.meshes)if(!mesh.baseColorTexture.isNull() && !textureIndex.contains(mesh.baseColorTexture.cacheKey())) {
        QByteArray png;QBuffer b(&png);b.open(QIODevice::WriteOnly);ensure(mesh.baseColorTexture.save(&b,"PNG"),"Could not encode a model texture.");textureIndex.insert(mesh.baseColorTexture.cacheKey(),images.size());images<<png;
    }
    s<<quint32(images.size());for(const auto &image:images){s<<quint32(image.size());s.writeRawData(image.constData(),int(image.size()));}
    s<<quint32(model.meshes.size());
    for(const auto &mesh:model.meshes){writeString(s,mesh.name);for(int i=0;i<16;++i)s<<mesh.transform.constData()[i];s<<quint32(mesh.baseColor.rgba())<<quint8(mesh.doubleSided)<<mesh.alphaCutoff<<qint32(mesh.baseColorTexture.isNull()?-1:textureIndex.value(mesh.baseColorTexture.cacheKey()));
        s<<quint32(mesh.positions.size())<<quint32(mesh.indices.size())<<quint8(!mesh.texCoords.isEmpty());
        for(const auto &p:mesh.positions)writeVector(s,p);for(const auto &n:mesh.normals)writeVector(s,n);for(const auto &uv:mesh.texCoords)s<<uv.x()<<uv.y();for(auto i:mesh.indices)s<<i;
        ensure(bytes.size()<=maxModel3DBytes,"Model data exceeds 128 MiB.");
    }ensure(s.status()==QDataStream::Ok,"Could not encode model data.");return bytes;
}
int integer(const QJsonValue &v,int min,int max,const QString &what) {
    const double d=v.toDouble(std::numeric_limits<double>::quiet_NaN());ensure(v.isDouble()&&std::isfinite(d)&&d==std::floor(d)&&d>=min&&d<=max,"Invalid "+what+".");return int(d);
}
int integer(const QJsonObject &o,const char *key,int min,int max,int fallback=-1){return o.contains(QLatin1String(key))?integer(o.value(QLatin1String(key)),min,max,QLatin1String(key)):fallback;}
QJsonObject indexed(const QJsonArray &a,int i,const QString &what){ensure(i>=0&&i<a.size()&&a[i].isObject(),"Invalid "+what+" reference.");return a[i].toObject();}
float real(const QJsonValue &v,float limit=1000000) {auto d=v.toDouble(std::numeric_limits<double>::quiet_NaN());ensure(v.isDouble()&&std::isfinite(d)&&std::abs(d)<=limit,"Invalid numeric model value.");return float(d);}
struct Accessor {const char *data=nullptr;int count=0,components=0,componentType=0,componentBytes=0,stride=0;bool normalized=false;};
class GltfReader {
public:
    QJsonObject json;QVector<QByteArray> buffers;QString basePath;QByteArray binary; qint64 resourceBytes=0,texturePixels=0,vertexCount=0,triangleCount=0;
    QHash<int,QImage> imageCache;
    QByteArray uri(const QString &text) {
        ensure(!text.isEmpty(),"Missing model resource URI.");QByteArray bytes;
        if(text.startsWith("data:")) {const int comma=text.indexOf(',');ensure(comma>0&&text.left(comma).endsWith(";base64"),"Only base64 data resources are supported.");auto r=QByteArray::fromBase64Encoding(text.mid(comma+1).toLatin1(),QByteArray::AbortOnBase64DecodingErrors);ensure(bool(r),"Invalid base64 model resource.");bytes=r.decoded;}
        else {QUrl u(text);ensure(u.isRelative()&&u.scheme().isEmpty()&&!u.hasQuery()&&!u.hasFragment(),"Model resources must be local relative files.");auto relative=QUrl::fromPercentEncoding(text.toUtf8());ensure(!QDir::isAbsolutePath(relative)&&!relative.contains('\\'),"Invalid model resource path.");const auto root=QFileInfo(basePath).canonicalFilePath();const auto path=QFileInfo(QDir(basePath).filePath(relative)).canonicalFilePath();ensure(!path.isEmpty()&&path.startsWith(root+QDir::separator()),"Model resource must remain inside its source folder.");bytes=readFile(path);}
        resourceBytes+=bytes.size();ensure(resourceBytes<=maxModel3DBytes,"Model and external resources exceed 128 MiB.");return bytes;
    }
    void loadBuffers(){auto a=json.value("buffers").toArray();ensure(a.size()<=256,"Too many model buffers.");for(int i=0;i<a.size();++i){auto o=indexed(a,i,"buffer");int size=integer(o,"byteLength",1,int(maxModel3DBytes));ensure(size>0,"Missing buffer size.");QByteArray b;
        if(o.contains("uri"))b=uri(o.value("uri").toString());else{ensure(i==0&&!binary.isEmpty(),"Missing binary model buffer.");b=binary;}
        ensure(b.size()>=size&&b.size()-size<=3,"Buffer length does not match its declaration.");b.resize(size);buffers<<b;}}
    QByteArray viewBytes(int index){auto v=indexed(json.value("bufferViews").toArray(),index,"buffer view");auto bi=integer(v,"buffer",0,buffers.size()-1);ensure(bi>=0,"Missing buffer reference.");int offset=integer(v,"byteOffset",0,int(maxModel3DBytes),0),length=integer(v,"byteLength",1,int(maxModel3DBytes));ensure(length>0&&qint64(offset)+length<=buffers[bi].size(),"Buffer view exceeds its buffer.");return buffers[bi].mid(offset,length);}
    Accessor accessor(int index,int components,bool indices=false){auto a=indexed(json.value("accessors").toArray(),index,"accessor");ensure(!a.contains("sparse"),"Sparse glTF accessors are not supported yet. Export a dense mesh.");ensure(a.value("type").toString()==(components==1?"SCALAR":components==2?"VEC2":"VEC3"),"Unexpected accessor shape.");
        Accessor result;result.count=integer(a,"count",1,indices?maxModel3DTriangles*3:maxModel3DVertices);ensure(result.count>0,"Missing accessor count.");result.components=components;result.componentType=integer(a,"componentType",0,65535);result.normalized=a.value("normalized").toBool(false);
        switch(result.componentType){case 5120:case 5121:result.componentBytes=1;break;case 5122:case 5123:result.componentBytes=2;break;case 5125:case 5126:result.componentBytes=4;break;default:bad("Unsupported accessor component type.");}
        if(indices)ensure((result.componentType==5121||result.componentType==5123||result.componentType==5125)&&!result.normalized,"Invalid triangle index format.");
        else if(components==3)ensure(result.componentType==5126&&!result.normalized,"Positions and normals must use float components.");
        else ensure(result.componentType==5126||((result.componentType==5121||result.componentType==5123)&&result.normalized),"Unsupported texture coordinate format.");
        auto v=indexed(json.value("bufferViews").toArray(),integer(a,"bufferView",0,100000),"buffer view");int bi=integer(v,"buffer",0,buffers.size()-1);ensure(bi>=0,"Missing buffer.");int vo=integer(v,"byteOffset",0,int(maxModel3DBytes),0),vl=integer(v,"byteLength",1,int(maxModel3DBytes)),ao=integer(a,"byteOffset",0,int(maxModel3DBytes),0);
        const int packed=components*result.componentBytes;result.stride=integer(v,"byteStride",4,252,packed);ensure(result.stride>=packed&&result.stride%result.componentBytes==0&&ao%result.componentBytes==0&&vo%result.componentBytes==0,"Invalid accessor alignment or stride.");
        qint64 end=qint64(ao)+qint64(result.count-1)*result.stride+packed;ensure(vl>0&&qint64(vo)+vl<=buffers[bi].size()&&end<=vl,"Accessor exceeds its buffer view.");result.data=buffers[bi].constData()+vo+ao;return result;
    }
    double component(const Accessor &a,int row,int column=0){const auto p=reinterpret_cast<const uchar*>(a.data+qint64(row)*a.stride+column*a.componentBytes);switch(a.componentType){case 5121:return a.normalized?double(*p)/255:*p;case 5123:{auto n=qFromLittleEndian<quint16>(p);return a.normalized?double(n)/65535:n;}case 5125:return qFromLittleEndian<quint32>(p);case 5126:{auto n=qFromLittleEndian<quint32>(p);float f;std::memcpy(&f,&n,4);ensure(std::isfinite(f)&&std::abs(f)<=1000000,"Nonfinite or excessive model coordinate.");return f;}default:bad("Unsupported component type.");}}
    QImage image(int index){if(imageCache.contains(index))return imageCache.value(index);auto o=indexed(json.value("images").toArray(),index,"image");QByteArray b;if(o.contains("uri"))b=uri(o.value("uri").toString());else b=viewBytes(integer(o,"bufferView",0,100000));QImage img=decodeTexture(b,texturePixels);imageCache.insert(index,img);return img;}
    void material(ModelMesh3D &mesh,int index){mesh.baseColor=Qt::white;if(index<0)return;auto m=indexed(json.value("materials").toArray(),index,"material");ensure(m.value("extensions").toObject().isEmpty(),"Material extensions are not supported in this first 3D version.");
        const auto alpha=m.value("alphaMode").toString("OPAQUE");ensure(alpha=="OPAQUE"||alpha=="MASK","Transparent BLEND materials are not supported yet. Use opaque or cutout materials.");mesh.alphaCutoff=alpha=="MASK"?(m.contains("alphaCutoff")?real(m.value("alphaCutoff"),1):.5f):-1;
        ensure(mesh.alphaCutoff>=-1,"Invalid alpha cutoff.");mesh.doubleSided=m.value("doubleSided").toBool(false);auto pbr=m.value("pbrMetallicRoughness").toObject();
        if(pbr.contains("baseColorFactor")){auto a=pbr.value("baseColorFactor").toArray();ensure(a.size()==4,"Invalid material base color.");float values[4];for(int i=0;i<4;++i){values[i]=real(a[i],1);ensure(values[i]>=0,"Invalid material base color.");}mesh.baseColor=QColor::fromRgbF(values[0],values[1],values[2],values[3]);}
        if(pbr.contains("baseColorTexture")){auto tex=pbr.value("baseColorTexture").toObject();ensure(tex.value("extensions").toObject().isEmpty(),"Texture transforms are not supported yet.");ensure(integer(tex,"texCoord",0,10,0)==0,"Only the first texture coordinate set is supported.");auto t=indexed(json.value("textures").toArray(),integer(tex,"index",0,100000),"texture");ensure(t.value("extensions").toObject().isEmpty(),"Compressed texture extensions are not supported.");mesh.baseColorTexture=image(integer(t,"source",0,100000));ensure(!mesh.texCoords.isEmpty(),"Textured mesh is missing texture coordinates.");
            if(t.contains("sampler")){auto sampler=indexed(json.value("samplers").toArray(),integer(t,"sampler",0,100000),"sampler");ensure(integer(sampler,"wrapS",0,65535,10497)==10497&&integer(sampler,"wrapT",0,65535,10497)==10497,"Only repeating model textures are supported in this version.");}}
    }
    QVector<ModelMesh3D> meshes(int index,const QMatrix4x4 &transform){auto mesh=indexed(json.value("meshes").toArray(),index,"mesh");ensure(!mesh.contains("weights"),"Morph-target models are not supported yet.");auto primitives=mesh.value("primitives").toArray();ensure(!primitives.isEmpty()&&primitives.size()<=maxModel3DMeshes,"Invalid mesh primitive count.");QVector<ModelMesh3D> result;
        for(const auto &v:primitives){ensure(v.isObject(),"Malformed mesh primitive.");auto p=v.toObject();ensure(integer(p,"mode",0,6,4)==4,"Only triangular meshes are supported. Triangulate the model before importing.");ensure(p.value("extensions").toObject().isEmpty(),"Compressed or extended mesh primitives are not supported yet.");ensure(!p.contains("targets"),"Morph targets are not supported yet.");auto a=p.value("attributes").toObject();for(auto it=a.begin();it!=a.end();++it)ensure(!it.key().startsWith("JOINTS_")&&!it.key().startsWith("WEIGHTS_"),"Skinned models need the upcoming rig support. Import a static mesh for now.");
            ModelMesh3D m;m.name=mesh.value("name").toString().left(256);m.transform=transform;auto positions=accessor(integer(a,"POSITION",0,100000),3);vertexCount+=positions.count;ensure(vertexCount<=maxModel3DVertices,"Model exceeds one million vertices.");m.positions.reserve(positions.count);for(int i=0;i<positions.count;++i)m.positions<<QVector3D(float(component(positions,i,0)),float(component(positions,i,1)),float(component(positions,i,2)));
            if(a.contains("NORMAL")){auto n=accessor(integer(a,"NORMAL",0,100000),3);ensure(n.count==positions.count,"Normal count does not match positions.");for(int i=0;i<n.count;++i)m.normals<<QVector3D(float(component(n,i,0)),float(component(n,i,1)),float(component(n,i,2))).normalized();}
            if(a.contains("TEXCOORD_0")){auto t=accessor(integer(a,"TEXCOORD_0",0,100000),2);ensure(t.count==positions.count,"Texture coordinate count does not match positions.");for(int i=0;i<t.count;++i)m.texCoords<<QVector2D(float(component(t,i,0)),float(component(t,i,1)));}
            if(p.contains("indices")){auto indices=accessor(integer(p,"indices",0,100000),1,true);ensure(indices.count%3==0,"Triangle index count must be divisible by three.");m.indices.reserve(indices.count);for(int i=0;i<indices.count;++i){auto idx=quint32(component(indices,i));ensure(idx<quint32(positions.count),"Triangle index is outside the vertex array.");m.indices<<idx;}}
            else{ensure(positions.count%3==0,"Unindexed triangle vertex count must be divisible by three.");for(int i=0;i<positions.count;++i)m.indices<<quint32(i);}
            triangleCount+=m.indices.size()/3;ensure(triangleCount<=maxModel3DTriangles,"Model exceeds one million triangles.");
            if(m.normals.isEmpty()){m.normals=QVector<QVector3D>(m.positions.size());for(int i=0;i<m.indices.size();i+=3){auto x=m.indices[i],y=m.indices[i+1],z=m.indices[i+2];auto n=QVector3D::crossProduct(m.positions[y]-m.positions[x],m.positions[z]-m.positions[x]);m.normals[x]+=n;m.normals[y]+=n;m.normals[z]+=n;}for(auto &n:m.normals)n.normalize();}
            material(m,integer(p,"material",0,100000));result<<m;
        }return result;
    }
    QMatrix4x4 nodeTransform(const QJsonObject &o){QMatrix4x4 m;if(o.contains("matrix")){ensure(!o.contains("translation")&&!o.contains("rotation")&&!o.contains("scale"),"Node cannot combine matrix and TRS transforms.");auto a=o.value("matrix").toArray();ensure(a.size()==16,"Invalid node matrix.");float values[16];for(int i=0;i<16;++i)values[i]=real(a[i]);m=QMatrix4x4(values).transposed();}
        else{auto vec=[&](const char *key,QVector3D defaultValue){if(!o.contains(QLatin1String(key)))return defaultValue;auto a=o.value(QLatin1String(key)).toArray();ensure(a.size()==3,"Invalid node transform.");return QVector3D(real(a[0]),real(a[1]),real(a[2]));};m.translate(vec("translation",{}));if(o.contains("rotation")){auto a=o.value("rotation").toArray();ensure(a.size()==4,"Invalid node quaternion.");QQuaternion q(real(a[3],1),real(a[0],1),real(a[1],1),real(a[2],1));ensure(q.lengthSquared()>.00001f,"Invalid zero rotation quaternion.");m.rotate(q.normalized());}m.scale(vec("scale",{1,1,1}));}return m;}
    Model3D read(const QString &name){ensure(json.value("asset").toObject().value("version").toString()=="2.0","Only glTF 2.0 models are supported.");ensure(json.value("extensionsRequired").toArray().isEmpty(),"This model requires unsupported glTF extensions.");ensure(json.value("skins").toArray().isEmpty(),"Rigged models need the upcoming rig support. Import a static mesh for now.");ensure(json.value("animations").toArray().isEmpty(),"Animated models are not supported yet. Export a static model.");loadBuffers();Model3D model;model.name=name.left(256);
        auto nodes=json.value("nodes").toArray(),scenes=json.value("scenes").toArray();ensure(nodes.size()<=4096&&scenes.size()<=128,"Model scene hierarchy is too large.");QJsonArray roots;
        if(!scenes.isEmpty()){auto scene=indexed(scenes,integer(json,"scene",0,scenes.size()-1,0),"scene");roots=scene.value("nodes").toArray();}
        else{QSet<int> children;for(int i=0;i<nodes.size();++i)for(const auto &c:indexed(nodes,i,"node").value("children").toArray())children.insert(integer(c,0,nodes.size()-1,"child"));for(int i=0;i<nodes.size();++i)if(!children.contains(i))roots.append(i);}
        QSet<int> visited;std::function<void(int,QMatrix4x4,int)> visit=[&](int index,QMatrix4x4 parent,int depth){ensure(depth<128&&!visited.contains(index),"Model has a cyclic or shared node hierarchy.");visited.insert(index);auto n=indexed(nodes,index,"node");ensure(!n.contains("skin"),"Rigged models are not supported yet.");ensure(n.value("extensions").toObject().isEmpty(),"Extended scene nodes are not supported yet.");auto transform=parent*nodeTransform(n);if(n.contains("mesh")){auto parts=meshes(integer(n,"mesh",0,100000),transform);ensure(model.meshes.size()+parts.size()<=maxModel3DMeshes,"Model exceeds 256 mesh parts.");model.meshes+=parts;}for(const auto &child:n.value("children").toArray())visit(integer(child,0,nodes.size()-1,"child"),transform,depth+1);};
        for(const auto &root:roots)visit(integer(root,0,nodes.size()-1,"scene root"),QMatrix4x4(),0);
        ensure(!model.meshes.isEmpty(),"The model's selected scene has no triangle meshes.");return model;
    }
};
} // namespace
QByteArray serializeModel3D(const Model3D &model,QString *error){try{auto payload=payloadFor(model);QByteArray bytes(magic,8);bytes+=QCryptographicHash::hash(payload,QCryptographicHash::Sha256);bytes+=payload;ensure(bytes.size()<=maxModel3DBytes,"Model exceeds 128 MiB.");if(error)error->clear();return bytes;}catch(const QString &message){if(error)*error=message;return {};}catch(const std::bad_alloc &){if(error)*error="Not enough memory for this 3D model. Try a smaller model or texture.";return {};}}
Model3DPtr finalizeModel3D(Model3D model,QString *error){try{QString validation=validateModel3D(model);if(!validation.isEmpty()){if(error)*error=validation;return {};}
    model.bounds={};for(const auto &mesh:model.meshes)for(const auto &p:mesh.positions)model.bounds.include(mesh.transform.map(p));auto bytes=serializeModel3D(model,error);if(bytes.isEmpty())return {};model.id=QString::fromLatin1(bytes.mid(8,32).toHex());if(error)error->clear();return std::make_shared<const Model3D>(std::move(model));}catch(const std::bad_alloc &){if(error)*error="Not enough memory for this 3D model.";return {};}}
Model3DPtr deserializeModel3D(const QByteArray &bytes,QString *error){try{ensure(bytes.size()>40&&bytes.size()<=maxModel3DBytes&&bytes.startsWith(QByteArray(magic,8)),"Unsupported or invalid model data.");auto payload=bytes.mid(40);ensure(QCryptographicHash::hash(payload,QCryptographicHash::Sha256)==bytes.mid(8,32),"Model checksum does not match.");QDataStream s(payload);setup(s);Model3D model;model.name=readString(s);quint32 count=0;s>>count;ensure(count<=maxModel3DMeshes,"Too many model textures.");QVector<QImage> images;qint64 pixels=0;
    for(quint32 i=0;i<count;++i){quint32 size=0;s>>size;ensure(size>0&&size<=quint32(s.device()->bytesAvailable())&&size<=maxModel3DBytes,"Invalid model texture size.");QByteArray png(size,Qt::Uninitialized);ensure(s.readRawData(png.data(),int(size))==int(size),"Truncated model texture.");images<<decodeTexture(png,pixels);}
    s>>count;ensure(count>0&&count<=maxModel3DMeshes,"Invalid model mesh count.");qint64 vertices=0,triangles=0;
    for(quint32 k=0;k<count;++k){ModelMesh3D m;m.name=readString(s);float matrix[16];for(float &v:matrix)s>>v;m.transform=QMatrix4x4(matrix).transposed();quint32 rgba=0;quint8 sided=0,uv=0;qint32 texture=-1;s>>rgba>>sided>>m.alphaCutoff>>texture;ensure(sided<=1&&texture>=-1&&texture<images.size(),"Invalid model material.");m.baseColor=QColor::fromRgba(rgba);m.doubleSided=sided;if(texture>=0)m.baseColorTexture=images[texture];quint32 vc=0,ic=0;s>>vc>>ic>>uv;vertices+=vc;triangles+=ic/3;
        ensure(vc>0&&ic>0&&ic%3==0&&uv<=1&&vertices<=maxModel3DVertices&&triangles<=maxModel3DTriangles,"Invalid model geometry size.");ensure(qint64(vc)*(24+(uv?8:0))+qint64(ic)*4<=s.device()->bytesAvailable(),"Truncated model geometry.");m.positions.reserve(vc);m.normals.reserve(vc);for(quint32 i=0;i<vc;++i)m.positions<<readVector(s);for(quint32 i=0;i<vc;++i)m.normals<<readVector(s);if(uv)for(quint32 i=0;i<vc;++i){float x=0,y=0;s>>x>>y;m.texCoords<<QVector2D(x,y);}m.indices.reserve(ic);for(quint32 i=0;i<ic;++i){quint32 v=0;s>>v;m.indices<<v;}model.meshes<<std::move(m);
    }ensure(s.status()==QDataStream::Ok&&s.atEnd(),"Invalid trailing or truncated model data.");auto validation=validateModel3D(model);ensure(validation.isEmpty(),validation);model.bounds={};for(const auto &m:model.meshes)for(const auto &p:m.positions)model.bounds.include(m.transform.map(p));model.id=QString::fromLatin1(bytes.mid(8,32).toHex());if(error)error->clear();return std::make_shared<const Model3D>(std::move(model));}catch(const QString &message){if(error)*error=message;return {};}catch(const std::bad_alloc &){if(error)*error="Not enough memory for this 3D model. Try a smaller model or texture.";return {};}}
Model3DPtr importModel3D(const QString &filePath,QString *error){try{const auto ext=QFileInfo(filePath).suffix().toLower();ensure(ext=="gltf"||ext=="glb","Choose a GLB or glTF 2.0 model.");auto bytes=readFile(filePath);QByteArray jsonBytes;GltfReader reader;reader.basePath=QFileInfo(filePath).absolutePath();reader.resourceBytes=bytes.size();
    if(ext=="glb"){ensure(bytes.size()>=20,"Truncated GLB header.");auto u32=[&](int at){return qFromLittleEndian<quint32>(reinterpret_cast<const uchar*>(bytes.constData()+at));};ensure(u32(0)==0x46546c67&&u32(4)==2&&u32(8)==quint32(bytes.size()),"Invalid GLB header or version.");int cursor=12,chunk=0;while(cursor<bytes.size()){ensure(cursor+8<=bytes.size(),"Truncated GLB chunk.");quint32 size=u32(cursor),type=u32(cursor+4);cursor+=8;ensure(size%4==0&&qint64(cursor)+size<=bytes.size(),"Invalid GLB chunk size.");if(chunk==0){ensure(type==0x4e4f534a,"GLB must begin with JSON.");jsonBytes=bytes.mid(cursor,size);}else if(type==0x004e4942){ensure(reader.binary.isEmpty(),"GLB contains multiple binary chunks.");reader.binary=bytes.mid(cursor,size);}else bad("Unsupported GLB chunk.");cursor+=int(size);++chunk;}}
    else jsonBytes=bytes;ensure(jsonBytes.size()<=16*1024*1024,"Model JSON exceeds 16 MiB.");QJsonParseError parse;auto doc=QJsonDocument::fromJson(jsonBytes,&parse);ensure(parse.error==QJsonParseError::NoError&&doc.isObject(),"Invalid model JSON: "+parse.errorString());reader.json=doc.object();auto model=reader.read(QFileInfo(filePath).completeBaseName());return finalizeModel3D(std::move(model),error);
    }catch(const QString &message){if(error)*error=message;return {};}catch(const std::bad_alloc &){if(error)*error="Not enough memory for this 3D model. Try a smaller model or texture.";return {};}}
} // namespace toon
