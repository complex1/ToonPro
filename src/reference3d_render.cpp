#include "reference3d_render.h"
#include "scene3d.h"
#include <QCache>
#include <QDataStream>
#include <QIODevice>
#include <QPainter>
#include <QSet>
#include <algorithm>
#include <cmath>
#include <limits>

namespace toon {
QMatrix4x4 referenceViewProjection(const ReferenceCamera &source,QSize size){
    Camera3D camera;camera.target=source.target;camera.yaw=source.yaw;camera.pitch=source.pitch;camera.distance=source.distance;
    camera.fieldOfView=source.fieldOfView;camera.orthographic=source.orthographic;camera.orthoScale=source.orthoScale;
    return cameraProjectionMatrix(camera,float(qMax(1,size.width()))/qMax(1,size.height()))*cameraViewMatrix(camera);
}
namespace {
struct ScreenPoint { float x=0,y=0,z=0,w=0; bool valid=false; };
ScreenPoint projected(const QMatrix4x4 &matrix,QVector3D point,QSize size) {
    const auto clip=matrix*QVector4D(point,1);
    if(clip.w()<=.00001f) return {};
    const auto p=clip.toVector3DAffine();
    return {(p.x()+1)*.5f*size.width(),(1-p.y())*.5f*size.height(),p.z(),clip.w(),p.z()>=-1&&p.z()<=1};
}
struct Triangle {
    ScreenPoint p[3]; QVector2D uv[3]; float light[3]{};
    QVector4D color[3];
    const ReferenceMaterial *material=nullptr; int object=0,order=0; float depth=0; bool blend=false,linearColor=false;
};
// glTF factors and vertex colors are linear; its base-color images are sRGB.
// Keep authored QColor assets in their existing screen-color convention.
// Tables avoid adding three pow() calls to every imported-model pixel.
struct PreviewColorTransfer {
    std::array<float,256> linear{};
    std::array<quint8,8193> screen{};
    PreviewColorTransfer(){
        for(int i=0;i<256;++i){const float c=i/255.f;linear[i]=c<=.04045f?c/12.92f:std::pow((c+.055f)/1.055f,2.4f);}
        for(int i=0;i<=8192;++i){const float c=i/8192.f;screen[i]=quint8(std::lround(255*(c<=.0031308f?12.92f*c:1.055f*std::pow(c,1/2.4f)-.055f)));}
    }
    int byte(float value)const{return screen[qBound(0,int(std::lround(value*8192)),8192)];}
};
bool sameDeformation(const ReferenceInstance &a,const ReferenceInstance &b){
    return a.asset&&a.asset==b.asset
        &&a.poseRotations==b.poseRotations&&a.poseTranslations==b.poseTranslations&&a.poseScales==b.poseScales
        &&a.morphOverrides==b.morphOverrides&&a.expressionWeights==b.expressionWeights;
}


} // namespace
void ReferenceRenderCache::retainScene(const ReferenceScene &scene){
        for(int i=entries.size()-1;i>=0;--i){bool used=false;for(const auto &instance:scene.instances)if(instance.visible&&sameDeformation(instance,entries[i]->key)){used=true;break;}
            if(!used){bytes-=entries[i]->bytes;entries.removeAt(i);}
        }
    }
std::shared_ptr<ReferenceCachedDeformation> ReferenceRenderCache::geometry(const ReferenceInstance &instance,QString *error){
        for(int i=0;i<entries.size();++i)if(sameDeformation(instance,entries[i]->key)){
            ++hits;auto result=entries.takeAt(i);entries.append(result);return result;
        }
        auto local=instance;local.translation={};local.rotation=QQuaternion();local.scale={1,1,1};
        auto evaluated=evaluateReferenceInstance(local,error);++evaluations;if(!evaluated)return {};
        auto result=std::make_shared<ReferenceCachedDeformation>();result->key=std::move(local);result->geometry=std::move(*evaluated);
        result->bytes=qint64(result->geometry.nodeGlobals.size())*sizeof(QMatrix4x4);
        for(const auto &primitive:result->geometry.primitives)result->bytes+=qint64(primitive.vertices.size())*sizeof(ReferenceVertex);
        while(!entries.isEmpty()&&bytes+result->bytes>cacheLimit){bytes-=entries.first()->bytes;entries.removeFirst();}
        if(result->bytes<=cacheLimit){entries.append(result);bytes+=result->bytes;}
        return result;
    }
QString referenceScenePreviewBudgetError(const ReferenceScene &scene){
    constexpr qint64 budget=192LL*1024*1024;
    // Display frames are bounded by their controller; direct image allocation
    // has a separate 16-megapixel limit below.
    qint64 bytes=16LL*1024*1024;
    for(const auto &instance:scene.instances){
        if(!instance.visible)continue;
        if(!instance.asset)return "A visible reference is missing its source asset.";
        const auto &asset=*instance.asset;
        bytes+=qint64(asset.nodes.size())*sizeof(QMatrix4x4);
        for(const auto &node:asset.nodes){
            if(node.meshIndex<0)continue;
            if(node.meshIndex>=asset.meshes.size())return "A visible reference has an invalid mesh node.";
            for(const auto &primitive:asset.meshes[node.meshIndex].primitives){
                bytes+=qint64(primitive.vertices.size())*sizeof(ReferenceVertex);
                // QVector growth may retain almost twice its logical size.
                bytes+=qint64(primitive.indices.size()/3)*sizeof(Triangle)*2;
                if(bytes>budget)return "This scene exceeds the 192 MiB review-preview geometry budget. Hide or remove some models; the saved source scene is preserved.";
            }
        }
    }
    return {};
}
float edge(const ScreenPoint &a,const ScreenPoint &b,float x,float y) { return (x-a.x)*(b.y-a.y)-(y-a.y)*(b.x-a.x); }

// A bounded, background reference preview. The canonical evaluator supplies the
// same deformed world triangles used for picking and model export. This renderer
// deliberately exposes no unsupported PBR or simulation controls.
ReferenceRenderFrame renderReferenceScene(const ReferenceScene &scene,QSize size,const std::shared_ptr<std::atomic_bool> &cancel,ReferenceRenderCache &cache,const ReferenceRenderOptions &options) {
    static const PreviewColorTransfer transfer;
    ReferenceRenderFrame frame; frame.camera=scene.camera;
    for(const auto &instance:scene.instances)frame.objectIds.append(instance.id);
    frame.error=referenceScenePreviewBudgetError(scene);if(!frame.error.isEmpty())return frame;
    cache.retainScene(scene);
    if(size.isEmpty()||size.width()>8192||size.height()>8192||qint64(size.width())*size.height()>16LL*1024*1024){frame.error="The 3D reference render exceeds the 16 megapixel image budget.";return frame;}
    frame.image=QImage(size,QImage::Format_ARGB32_Premultiplied); frame.image.fill(options.background);
    frame.ids=QImage(size,QImage::Format_ARGB32); frame.ids.fill(0);
    if(frame.image.isNull()||frame.ids.isNull()){frame.error="Not enough memory to prepare the reference preview.";return frame;}
    QVector<float> depth(size.width()*size.height(),std::numeric_limits<float>::infinity());
    const auto matrix=referenceViewProjection(scene.camera,size);
    if(options.grid){
        QPainter p(&frame.image); p.setRenderHint(QPainter::Antialiasing);
        p.setPen(QPen(options.gridColor,1));
        const float span=qMax(2.f,scene.camera.distance*1.5f);
        const float step=std::pow(10.f,std::floor(std::log10(span)))/5;
        for(int i=-20;i<=20;++i) {
            const float a=i*step;
            for(int axis=0;axis<2;++axis) {
                const auto one=projected(matrix,axis?QVector3D(-20*step,0,a):QVector3D(a,0,-20*step),size);
                const auto two=projected(matrix,axis?QVector3D(20*step,0,a):QVector3D(a,0,20*step),size);
                if(one.valid&&two.valid) p.drawLine(QPointF(one.x,one.y),QPointF(two.x,two.y));
            }
        }
    }
    QVector<Triangle> opaqueTriangles,transparentTriangles;int triangleOrder=0;
    for(int object=0;object<scene.instances.size();++object) {
        if((cancel&&cancel->load())) return {};
        const auto &instance=scene.instances[object];
        if(!instance.visible||!instance.asset) continue;
        const bool linearColor=instance.asset->metadata.value("sourceGltf").isObject();
        QString error;const auto evaluated=cache.geometry(instance,&error);
        if(!evaluated) { frame.error=error; return frame; }
        const auto &geometry=evaluated->geometry;
        const auto placement=referencePlacementMatrix(instance);const auto normalMatrix=placement.normalMatrix();
        const auto projectedPlacement=matrix*placement;
        QSet<int> joints;
        for(const auto &skin:instance.asset->skins) for(int joint:skin.joints) joints.insert(joint);
        for(auto it=instance.asset->humanoidBones.begin();it!=instance.asset->humanoidBones.end();++it) joints.insert(it.value());
        for(int node:joints) if(node>=0&&node<geometry.nodeGlobals.size())
            frame.joints.append({object,node,instance.asset->nodes[node].parentIndex,
                                 placement.map(geometry.nodeGlobals[node].map(QVector3D())),instance.asset->nodes[node].name});
        const auto direction=-scene.light.direction.normalized();
        for(const auto &primitive:geometry.primitives) {
            const ReferenceMaterial *material=primitive.materialIndex>=0&&primitive.materialIndex<instance.asset->materials.size()
                ? &instance.asset->materials[primitive.materialIndex] : nullptr;
            for(int k=0;k+2<primitive.indices.size();k+=3) {
                Triangle triangle; triangle.material=material; triangle.object=object;
                triangle.linearColor=linearColor;
                triangle.blend=material&&material->alphaMode==ReferenceAlphaMode::Blend;
                bool valid=true;
                for(int v=0;v<3;++v) {
                    const auto &vertex=primitive.vertices[primitive.indices[k+v]];
                    triangle.p[v]=projected(projectedPlacement,vertex.position,size);
                    if(!triangle.p[v].valid) { valid=false; break; }
                    triangle.uv[v]=vertex.uv;
                    triangle.color[v]=vertex.color;
                    const auto n=vertex.normal;const QVector3D normal(normalMatrix(0,0)*n.x()+normalMatrix(0,1)*n.y()+normalMatrix(0,2)*n.z(),normalMatrix(1,0)*n.x()+normalMatrix(1,1)*n.y()+normalMatrix(1,2)*n.z(),normalMatrix(2,0)*n.x()+normalMatrix(2,1)*n.y()+normalMatrix(2,2)*n.z());
                    triangle.light[v]=std::clamp(scene.light.ambientIntensity+scene.light.intensity*qMax(0.f,QVector3D::dotProduct(normal.normalized(),direction)),.08f,1.65f);
                }
                if(!valid) continue;
                triangle.depth=(triangle.p[0].z+triangle.p[1].z+triangle.p[2].z)/3;
                triangle.order=triangleOrder++;
                if(triangle.blend)transparentTriangles.append(triangle);else opaqueTriangles.append(triangle);
            }
        }
    }
    // Keep opaque source order for coplanar depth ties. Transparent ties use
    // their source sequence as well, without a second temporary triangle array.
    std::sort(transparentTriangles.begin(),transparentTriangles.end(),[](const Triangle &a,const Triangle &b){return a.depth==b.depth?a.order<b.order:a.depth>b.depth;});
    int count=0;
    for(const auto *pass:{&opaqueTriangles,&transparentTriangles})for(const auto &triangle:*pass) {
        if((++count&127)==0&&(cancel&&cancel->load())) return {};
        const auto *p=triangle.p; const float area=edge(p[0],p[1],p[2].x,p[2].y);
        if(std::abs(area)<.0001f) continue;
        // Shared triangle edges have one owner. Inclusive edges on both sides
        // double alpha coverage and create seams in transparent reference layers.
        const auto topLeft=[&](const ScreenPoint &a,const ScreenPoint &b){const float dx=(b.x-a.x)*(area>0?1.f:-1.f),dy=(b.y-a.y)*(area>0?1.f:-1.f);return dy>0||(dy==0&&dx<0);};
        const bool edgeA=topLeft(p[1],p[2]),edgeB=topLeft(p[2],p[0]),edgeC=topLeft(p[0],p[1]);
        const int left=qMax(0,int(std::floor(std::min({p[0].x,p[1].x,p[2].x}))));
        const int top=qMax(0,int(std::floor(std::min({p[0].y,p[1].y,p[2].y}))));
        const int right=qMin(size.width()-1,int(std::ceil(std::max({p[0].x,p[1].x,p[2].x}))));
        const int bottom=qMin(size.height()-1,int(std::ceil(std::max({p[0].y,p[1].y,p[2].y}))));
        const QColor base=triangle.material?triangle.material->baseColor:QColor(190,190,190);
        const float baseR=base.redF(),baseG=base.greenF(),baseB=base.blueF(),baseA=base.alphaF();
        const QImage *texture=scene.textured&&triangle.material&&!triangle.material->baseColorTexture.isNull()?&triangle.material->baseColorTexture:nullptr;
        const bool premultiplied=texture&&texture->pixelFormat().premultiplied()==QPixelFormat::Premultiplied;
        const float inverseArea=1.f/area,stepA=(p[2].y-p[1].y)*inverseArea,stepB=(p[0].y-p[2].y)*inverseArea;
        const auto unit=[](float value){return std::clamp(value,0.f,1.f);};
        for(int y=top;y<=bottom;++y) {
            auto *pixels=reinterpret_cast<QRgb*>(frame.image.scanLine(y)); auto *ids=reinterpret_cast<quint32*>(frame.ids.scanLine(y));
            float a=edge(p[1],p[2],left+.5f,y+.5f)*inverseArea,b=edge(p[2],p[0],left+.5f,y+.5f)*inverseArea;
            for(int x=left;x<=right;++x,a+=stepA,b+=stepB) {
                const float c=1-a-b;
                constexpr float edgeTolerance=1e-6f;
                if(a< -edgeTolerance||b< -edgeTolerance||c< -edgeTolerance
                    ||(std::abs(a)<=edgeTolerance&&!edgeA)||(std::abs(b)<=edgeTolerance&&!edgeB)||(std::abs(c)<=edgeTolerance&&!edgeC))continue;
                const int index=y*size.width()+x; const float z=a*p[0].z+b*p[1].z+c*p[2].z;
                if(z>=depth[index]) continue;
                const float reciprocal=a/p[0].w+b/p[1].w+c/p[2].w;
                if(reciprocal<=0) continue;
                const float aa=a/p[0].w/reciprocal,bb=b/p[1].w/reciprocal,cc=c/p[2].w/reciprocal;
                const float light=aa*triangle.light[0]+bb*triangle.light[1]+cc*triangle.light[2];
                const auto vertexColor=aa*triangle.color[0]+bb*triangle.color[1]+cc*triangle.color[2];
                float red=unit(baseR*vertexColor.x()),green=unit(baseG*vertexColor.y()),blue=unit(baseB*vertexColor.z()),alpha=unit(baseA*vertexColor.w());
                if(texture) {
                    const auto uv=aa*triangle.uv[0]+bb*triangle.uv[1]+cc*triangle.uv[2];
                    const float u=uv.x()-std::floor(uv.x()),v=uv.y()-std::floor(uv.y());
                    QRgb tex=texture->pixel(qMin(texture->width()-1,int(u*texture->width())),qMin(texture->height()-1,int(v*texture->height())));if(premultiplied)tex=qUnpremultiply(tex);
                    constexpr float reciprocalByte=1.f/255.f;
                    if(triangle.linearColor){red*=transfer.linear[qRed(tex)];green*=transfer.linear[qGreen(tex)];blue*=transfer.linear[qBlue(tex)];}
                    else{red*=qRed(tex)*reciprocalByte;green*=qGreen(tex)*reciprocalByte;blue*=qBlue(tex)*reciprocalByte;}
                    alpha*=qAlpha(tex)*reciprocalByte;
                }
                if(triangle.material&&triangle.material->alphaMode==ReferenceAlphaMode::Mask) {
                    if(alpha<triangle.material->alphaCutoff) continue; alpha=1;
                } else if(!triangle.blend) alpha=1;
                if(alpha<.01f) continue;
                const QRgb previous=pixels[x];
                if(qAlpha(previous)==255){
                    const auto channel=[&](float value,int old){return triangle.linearColor
                        ?transfer.byte(value*light*alpha+transfer.linear[old]*(1-alpha))
                        :qBound(0,int(value*255*light*alpha+old*(1-alpha)),255);};
                    pixels[x]=qRgb(channel(red,qRed(previous)),channel(green,qGreen(previous)),channel(blue,qBlue(previous)));
                }else{
                    const float destinationAlpha=qAlpha(previous)/255.f;
                    const float outputAlpha=alpha+destinationAlpha*(1-alpha);
                    const QRgb straight=qUnpremultiply(previous);
                    const auto channel=[&](float value,int old){const float destination=triangle.linearColor?transfer.linear[old]:old/255.f;
                        const float combined=(value*light*alpha+destination*destinationAlpha*(1-alpha))/outputAlpha;
                        return triangle.linearColor?transfer.byte(combined):qBound(0,int(combined*255),255);};
                    pixels[x]=qPremultiply(qRgba(channel(red,qRed(straight)),channel(green,qGreen(straight)),channel(blue,qBlue(straight)),qBound(0,int(std::lround(outputAlpha*255)),255)));
                }
                ids[x]=quint32(triangle.object+1);
                if(!triangle.blend) depth[index]=z;
            }
        }
    }
    return frame;
}

namespace {
thread_local const QHash<QString,QImage> *activeReferenceLayerPreviews=nullptr;
struct AssetIdentity {std::weak_ptr<const ReferenceAsset> owner;quint64 token=0;};
quint64 assetIdentity(const ReferenceAssetPtr &asset){
    if(!asset)return 0;
    thread_local QHash<const ReferenceAsset*,AssetIdentity> identities;
    thread_local quint64 serial=0;
    auto at=identities.find(asset.get());
    if(at==identities.end()||at->owner.expired()||at->owner.owner_before(asset)||asset.owner_before(at->owner)){
        if(identities.size()>512){for(auto old=identities.begin();old!=identities.end();){if(old->owner.expired())old=identities.erase(old);else ++old;}}
        at=identities.insert(asset.get(),{asset,++serial});
    }
    return at->token;
}
QByteArray layerRenderKey(const ReferenceScene &scene,QSize size){
    QByteArray key("reference-layer-v1");QDataStream data(&key,QIODevice::Append);
    const auto &c=scene.camera;const auto &l=scene.light;
    data<<size<<c.target<<c.yaw<<c.pitch<<c.distance<<c.fieldOfView<<c.orthographic<<c.orthoScale
        <<l.direction<<l.color<<l.intensity<<l.ambientIntensity<<l.shadows<<scene.textured<<quint32(scene.instances.size());
    for(const auto &instance:scene.instances){
        data<<assetIdentity(instance.asset)<<(instance.asset?instance.asset->id:QString())<<instance.visible
            <<instance.translation<<instance.rotation<<instance.scale<<instance.poseRotations<<instance.poseTranslations
            <<instance.poseScales<<instance.morphOverrides<<instance.expressionWeights;
    }
    return key;
}
}
ReferenceLayerPreviewScope::ReferenceLayerPreviewScope(const QHash<QString,QImage> &prepared):m_previous(activeReferenceLayerPreviews){activeReferenceLayerPreviews=&prepared;}
ReferenceLayerPreviewScope::~ReferenceLayerPreviewScope(){activeReferenceLayerPreviews=m_previous;}
QImage renderReferenceLayer(const ReferenceScene &scene,QSize size,QString *error){
    if(size.isEmpty()){if(error)*error="A reference layer needs a valid artwork frame size.";return {};}
    if(size.width()>4096||size.height()>4096)size.scale({4096,4096},Qt::KeepAspectRatio);
    size=size.expandedTo({1,1});
    const auto key=layerRenderKey(scene,size);
    thread_local QCache<QByteArray,QImage> images(64*1024*1024);
    if(const auto *image=images.object(key)){if(error)error->clear();return *image;}
    thread_local ReferenceRenderCache cache;
    try{
        const auto frame=renderReferenceScene(scene,size,{},cache);
        if(error)*error=frame.error;
        if(!frame.error.isEmpty()||frame.image.isNull())return {};
        images.insert(key,new QImage(frame.image),int(frame.image.sizeInBytes()+key.size()+128));return frame.image;
    }catch(const std::bad_alloc &){if(error)*error="Not enough memory to render the 3D reference layer.";return {};}
    catch(const std::exception &){if(error)*error="The 3D reference layer could not be rendered.";return {};}
}
QImage referenceLayerPreview(const QString &layerId,const ReferenceScene &scene,QSize size,QString *error){
    if(activeReferenceLayerPreviews){if(error)error->clear();return activeReferenceLayerPreviews->value(layerId);}
    return renderReferenceLayer(scene,size,error);
}

} // namespace toon
