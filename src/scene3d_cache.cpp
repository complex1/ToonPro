#include "scene3d_cache.h"
#include "pose3d.h"
#include <QCache>
#include <QCryptographicHash>
#include <QJsonDocument>
#include <QDataStream>
#include <QIODevice>
#include <algorithm>
#include <limits>
namespace toon {
namespace {
struct Preview { QImage image; bool complete; };
struct SkinIdentity { Skin3D snapshot; quint64 token; };
thread_local QCache<QByteArray,Preview> previews(96*1024*1024);
thread_local QCache<QByteArray,SkinIdentity> skinIdentities(64*1024*1024);
thread_local quint64 nextSkinIdentity = 0;
thread_local QByteArray activeBase, activeDisplay;
thread_local const QHash<QString,QImage> *activeLayerPreviews = nullptr;
}
Scene3DLayerPreviewScope::Scene3DLayerPreviewScope(const QHash<QString,QImage> &prepared)
    : m_previous(activeLayerPreviews) { activeLayerPreviews = &prepared; }
Scene3DLayerPreviewScope::~Scene3DLayerPreviewScope() { activeLayerPreviews = m_previous; }
QImage scene3DLayerPreview(const QString &layerId, const Scene3D &scene, QSize size, bool completedOnly) {
    if (activeLayerPreviews) return activeLayerPreviews->value(layerId);
    return scene3DPreview(scene, size, completedOnly);
}
quint64 skin3DRenderKey(const Skin3D &skin) {
    QByteArray storage; QDataStream stream(&storage,QIODevice::WriteOnly);
    stream<<skin.jointIds<<quint32(skin.meshes.size());
    for(const auto &mesh:skin.meshes) stream<<quint64(quintptr(mesh.constData()))<<quint64(mesh.size());
    if(const auto *entry=skinIdentities.object(storage))return entry->token;
    const auto token=++nextSkinIdentity;
    qint64 retainedBytes=skin3DByteSize(skin)+sizeof(SkinIdentity)+storage.size()
        +skin.meshes.size()*sizeof(QVector<SkinVertex3D>)+skin.jointIds.size()*sizeof(QString);
    for(const auto &id:skin.jointIds)retainedBytes+=id.size()*sizeof(QChar);
    const auto bytes=std::clamp<qint64>(retainedBytes,1,std::numeric_limits<int>::max());
    skinIdentities.insert(storage,new SkinIdentity{skin,token},int(bytes));
    return token;
}
Scene3DPreviewScope::Scene3DPreviewScope(QByteArray baseKey, QByteArray displayKey)
    : m_previousBase(activeBase), m_previousDisplay(activeDisplay) {
    activeBase = std::move(baseKey); activeDisplay = std::move(displayKey);
}
Scene3DPreviewScope::~Scene3DPreviewScope() { activeBase = m_previousBase; activeDisplay = m_previousDisplay; }
QByteArray scene3DCacheKey(const Scene3D &scene,QSize size) {
    // Rest metadata changes pixels only when it participates in a saved pose.
    QByteArray data=QJsonDocument(scene3DToJson(scene, false)).toJson(QJsonDocument::Compact);
    QDataStream stream(&data,QIODevice::Append); stream<<size;
    for(const auto &object:scene.objects)if(object.skin&&!pose3DIsIdentity(object.pose)) {
        stream<<object.id<<skin3DRenderKey(*object.skin);
        for(const auto &joint:object.rig.joints)stream<<joint.id<<joint.parentId<<joint.position;
    }
    return QCryptographicHash::hash(data,QCryptographicHash::Sha256);
}
QImage scene3DPreview(const Scene3D &scene,QSize size,bool completedOnly) {
    const auto key=scene3DCacheKey(scene,size);
    if(!completedOnly && key==activeBase && !activeDisplay.isEmpty() && activeDisplay!=key) {
        const auto image=cachedScene3DPreview(activeDisplay);
        if(!image.isNull())return image;
    }
    return cachedScene3DPreview(key,completedOnly);
}
QImage cachedScene3DPreview(const QByteArray &key,bool completedOnly) {
    const auto *preview=previews.object(key); return preview && (!completedOnly || preview->complete) ? preview->image : QImage();
}
void clearPendingScene3DPreviews() {
    for(const auto &key:previews.keys())if(const auto *item=previews.object(key);item&&!item->complete)previews.remove(key);
}
void cacheScene3DPreview(const QByteArray &key,const QImage &image,bool complete) {
    if(!complete)if(const auto *old=previews.object(key);old && old->complete)return;
    if(!image.isNull()) previews.insert(key,new Preview{image,complete},int(image.sizeInBytes()));
}
}
