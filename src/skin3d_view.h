#pragma once
#include "scene3d.h"
#include <QColor>
#include <QPointF>
#include <QSize>
#include <QTransform>
#include <memory>

namespace toon {
// Transient workspace feedback, never saved with models or exported artwork.
struct Scene3DRenderOptions {
    QString skinObjectId, skinJointId;
    QVector3D skinRotation;
    bool weights = false;
    // Optional monotonic revision scoped to one render service. Callers must
    // advance it when skin storage changes; zero uses retained storage identity.
    quint64 skinRevision = 0;
    bool restPose = false;
    bool operator==(const Scene3DRenderOptions &) const = default;
};
QColor skinWeightColor(float weight);

struct SkinPaintHit {
    int mesh = 0, vertex = 0;
    float falloff = 0;
};
// Build after changing geometry, camera, object placement or canvas view. Brush
// queries use widget coordinates and a spatial index of front-visible vertices.
class SkinPaintProjection {
public:
    SkinPaintProjection();
    ~SkinPaintProjection();
    SkinPaintProjection(const SkinPaintProjection &) = default;
    SkinPaintProjection &operator=(const SkinPaintProjection &) = default;
    SkinPaintProjection(SkinPaintProjection &&) noexcept;
    SkinPaintProjection &operator=(SkinPaintProjection &&) noexcept;
    bool build(const Scene3D &scene, const QString &objectId, QSize frame,
               const QTransform &artworkToWidget, QString *error = nullptr);
    QVector<SkinPaintHit> verticesInBrush(QPointF center, qreal radius) const;
    int visibleVertexCount() const;
    QSize depthSize() const;
private:
    struct Data;
    std::shared_ptr<Data> m_data;
};
} // namespace toon
