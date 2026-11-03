#pragma once
#include "scene3d.h"
#include "skin3d_view.h"
#include <QObject>
#include <QSize>
#include <memory>

namespace toon {
// A GUI-thread service. GPU state lives exclusively on its dedicated worker.
// Requests are snapshots: obsolete queued work is coalesced, completed stale frames
// are never published, and an unchanged scene/size does not schedule more work.
class Scene3DRenderService final : public QObject {
    Q_OBJECT
public:
    explicit Scene3DRenderService(QObject *parent = nullptr);
    ~Scene3DRenderService() override;
    quint64 request(const Scene3D &scene, QSize pixelSize, const Scene3DRenderOptions &options = {});
    void clear();
    quint64 latestRequest() const;
    quint64 renderCount() const;
    bool busy() const;
signals:
    void frameReady(quint64 requestId, const QImage &image);
    void failed(quint64 requestId, const QString &message);
private:
    struct State;
    std::unique_ptr<State> m_state;
    void dispatch();
};
} // namespace toon
