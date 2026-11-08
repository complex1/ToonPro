#pragma once
#include "reference3d.h"
#include <atomic>

namespace toon {
struct ReferenceJointHandle { int object=-1,node=-1,parent=-1;QVector3D world;QString name; };
struct ReferenceRenderFrame {QImage image,ids;QVector<ReferenceJointHandle> joints;QStringList objectIds;ReferenceCamera camera;QString error;};
struct ReferenceRenderOptions {QColor background{Qt::transparent};bool grid=false;QColor gridColor{64,64,64};};
struct ReferenceCachedDeformation {ReferenceInstance key;ReferenceEvaluation geometry;qint64 bytes=0;};
// A cache belongs to one render worker. It never shares mutable evaluator data.
struct ReferenceRenderCache {
    QVector<std::shared_ptr<ReferenceCachedDeformation>> entries;
    quint64 evaluations=0,hits=0;qint64 bytes=0,cacheLimit=128LL*1024*1024;
    void retainScene(const ReferenceScene &scene);
    std::shared_ptr<ReferenceCachedDeformation> geometry(const ReferenceInstance &instance,QString *error=nullptr);
};
QMatrix4x4 referenceViewProjection(const ReferenceCamera &camera,QSize size);
QString referenceScenePreviewBudgetError(const ReferenceScene &scene);
ReferenceRenderFrame renderReferenceScene(const ReferenceScene &scene,QSize size,const std::shared_ptr<std::atomic_bool> &cancel,
    ReferenceRenderCache &cache,const ReferenceRenderOptions &options={});
// Exact canonical-source layer render. Large artwork is sampled with the same
// aspect at a bounded 16-megapixel resolution and composited in artwork space.
QImage renderReferenceLayer(const ReferenceScene &scene,QSize size,QString *error=nullptr);
// A canvas can supply background-rendered frames for its own paint operation.
// Worker/export renders outside this scope always use canonical scene content.
class ReferenceLayerPreviewScope {
public:
    explicit ReferenceLayerPreviewScope(const QHash<QString,QImage> &prepared);
    ~ReferenceLayerPreviewScope();
    ReferenceLayerPreviewScope(const ReferenceLayerPreviewScope&)=delete;
    ReferenceLayerPreviewScope&operator=(const ReferenceLayerPreviewScope&)=delete;
private:const QHash<QString,QImage> *m_previous;
};
QImage referenceLayerPreview(const QString &layerId,const ReferenceScene &scene,QSize size,QString *error=nullptr);
} // namespace toon
