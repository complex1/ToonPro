#pragma once
#include "scene3d.h"
#include <QHash>
namespace toon {
// Immutable GUI-prepared images keyed by layer identity, scoped to one worker
// render. Empty/missing entries remain empty, never using another job's cache.
class Scene3DLayerPreviewScope {
public:
    explicit Scene3DLayerPreviewScope(const QHash<QString,QImage> &prepared);
    ~Scene3DLayerPreviewScope();
    Scene3DLayerPreviewScope(const Scene3DLayerPreviewScope &) = delete;
    Scene3DLayerPreviewScope &operator=(const Scene3DLayerPreviewScope &) = delete;
private:
    const QHash<QString,QImage> *m_previous;
};
QImage scene3DLayerPreview(const QString &layerId, const Scene3D &scene,
                           QSize frameSize, bool completedOnly = false);
// A cheap, process-local identity for immutable/COW skin snapshots. Retained
// snapshots and bounded monotonic tokens prevent stale hits after allocation reuse.
quint64 skin3DRenderKey(const Skin3D &skin);
// Derived display images are bounded and never stored in project history.
QByteArray scene3DCacheKey(const Scene3D &scene, QSize frameSize);
QImage scene3DPreview(const Scene3D &scene, QSize frameSize, bool completedOnly = false);
void clearPendingScene3DPreviews();
void cacheScene3DPreview(const QByteArray &key, const QImage &image, bool complete = true);
QImage cachedScene3DPreview(const QByteArray &key, bool completedOnly = false);
// Scope transient heat/bend display to the owning canvas, never other canvases or exports.
class Scene3DPreviewScope {
public:
    Scene3DPreviewScope(QByteArray baseKey, QByteArray displayKey);
    ~Scene3DPreviewScope();
    Scene3DPreviewScope(const Scene3DPreviewScope &) = delete;
    Scene3DPreviewScope &operator=(const Scene3DPreviewScope &) = delete;
private:
    QByteArray m_previousBase, m_previousDisplay;
};
}
