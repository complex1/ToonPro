#include "skin3d_view.h"
#include "pose3d.h"
#include <QHash>
#include <QSet>
#include <QVector4D>
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace toon {
namespace {
constexpr int maxDepthSide = 1024;
constexpr qreal binSide = 32;
struct ProjectedVertex { QPointF widget, depthPoint; float metric; int mesh, vertex; };
struct PaintVertex { QPointF widget; int mesh, vertex; };
struct DepthPixel {
    float value = -std::numeric_limits<float>::infinity(), dx = 0, dy = 0;
    bool selected = false;
};
struct ClipVertex { QVector4D clip; QVector2D uv; float cameraDepth; };
struct ScreenVertex { QPointF point; QVector2D uv; float metric, inverseDepth; };
quint64 binKey(int x, int y) { return (quint64(quint32(x)) << 32) | quint32(y); }
qreal edge(QPointF a, QPointF b, QPointF p) {
    return (b.x() - a.x()) * (p.y() - a.y()) - (b.y() - a.y()) * (p.x() - a.x());
}
float planeDistance(const QVector4D &p, int plane) {
    switch (plane) {
    case 0: return p.w() + p.x(); case 1: return p.w() - p.x();
    case 2: return p.w() + p.y(); case 3: return p.w() - p.y();
    case 4: return p.w() + p.z(); default: return p.w() - p.z();
    }
}
int clipTriangle(std::array<ClipVertex, 12> &vertices) {
    unsigned outside = 0, common = 63;
    for (int i = 0; i < 3; ++i) {
        unsigned flags = 0;
        for (int plane = 0; plane < 6; ++plane) if (planeDistance(vertices[i].clip, plane) < 0) flags |= 1u << plane;
        outside |= flags; common &= flags;
    }
    if (common) return 0;
    if (!outside) return 3;
    int count = 3;
    for (int plane = 0; plane < 6 && count; ++plane) {
        if (!(outside & (1u << plane))) continue;
        std::array<ClipVertex, 12> result; int size = 0;
        auto previous = vertices[count - 1]; float previousDistance = planeDistance(previous.clip, plane);
        for (int i = 0; i < count; ++i) {
            const auto current = vertices[i]; const float currentDistance = planeDistance(current.clip, plane);
            if ((previousDistance >= 0) != (currentDistance >= 0)) {
                const float t = previousDistance / (previousDistance - currentDistance);
                result[size++] = {previous.clip + t * (current.clip - previous.clip),
                    previous.uv + t * (current.uv - previous.uv),
                    previous.cameraDepth + t * (current.cameraDepth - previous.cameraDepth)};
            }
            if (currentDistance >= 0) result[size++] = current;
            previous = current; previousDistance = currentDistance;
        }
        vertices = result; count = size;
    }
    return count;
}
ScreenVertex screenVertex(const ClipVertex &v, QSize size, bool orthographic) {
    const auto p = v.clip.toVector3DAffine();
    const float inverseDepth = 1.f / std::max(v.cameraDepth, .000001f);
    return {{(p.x() + 1) * size.width() / 2., (1 - p.y()) * size.height() / 2.}, v.uv,
            orthographic ? -v.cameraDepth : inverseDepth, inverseDepth};
}
bool opaqueAt(const ModelMesh3D &mesh, QVector2D uv) {
    if (mesh.alphaCutoff < 0 || mesh.baseColorTexture.isNull()) return true;
    const auto &image = mesh.baseColorTexture;
    const float u = uv.x() - std::floor(uv.x()), v = uv.y() - std::floor(uv.y());
    return image.pixelColor(std::min(image.width() - 1, int(u * image.width())),
        std::min(image.height() - 1, int(v * image.height()))).alphaF() * mesh.baseColor.alphaF() >= mesh.alphaCutoff;
}
void rasterize(const ScreenVertex &a, const ScreenVertex &b, const ScreenVertex &c,
               const ModelMesh3D &mesh, bool orthographic, bool selected, bool textured,
               QSize size, QVector<DepthPixel> &buffer) {
    const qreal area = edge(a.point, b.point, c.point);
    if (std::abs(area) < .000001 || (!mesh.doubleSided && area >= 0)) return;
    const int left = std::max(0, int(std::floor(std::min({a.point.x(), b.point.x(), c.point.x()}))));
    const int right = std::min(size.width() - 1, int(std::floor(std::max({a.point.x(), b.point.x(), c.point.x()}))));
    const int top = std::max(0, int(std::floor(std::min({a.point.y(), b.point.y(), c.point.y()}))));
    const int bottom = std::min(size.height() - 1, int(std::floor(std::max({a.point.y(), b.point.y(), c.point.y()}))));
    const float dx = float(((b.metric - a.metric) * (c.point.y() - a.point.y())
        - (c.metric - a.metric) * (b.point.y() - a.point.y())) / area);
    const float dy = float(((b.point.x() - a.point.x()) * (c.metric - a.metric)
        - (c.point.x() - a.point.x()) * (b.metric - a.metric)) / area);
    for (int y = top; y <= bottom; ++y) for (int x = left; x <= right; ++x) {
        const QPointF point(x + .5, y + .5);
        const float wa = float(edge(b.point, c.point, point) / area), wb = float(edge(c.point, a.point, point) / area);
        const float wc = 1 - wa - wb;
        if (wa < -.00001f || wb < -.00001f || wc < -.00001f) continue;
        const float metric = wa * a.metric + wb * b.metric + wc * c.metric;
        auto &pixel = buffer[y * size.width() + x]; if (metric <= pixel.value) continue;
        // The selected model's heatmap is opaque, including original material
        // holes. Other models retain their ordinary cutout coverage as blockers.
        if (!selected && textured && mesh.alphaCutoff >= 0 && !mesh.baseColorTexture.isNull()) {
            const auto uv = orthographic ? wa * a.uv + wb * b.uv + wc * c.uv
                : (wa * a.inverseDepth * a.uv + wb * b.inverseDepth * b.uv + wc * c.inverseDepth * c.uv)
                    / (wa * a.inverseDepth + wb * b.inverseDepth + wc * c.inverseDepth);
            if (!opaqueAt(mesh, uv)) continue;
        }
        pixel = {metric, dx, dy, selected};
    }
}
bool vertexVisible(const ProjectedVertex &vertex, QSize size, const QVector<DepthPixel> &buffer) {
    const int x = std::clamp(int(vertex.depthPoint.x()), 0, size.width() - 1);
    const int y = std::clamp(int(vertex.depthPoint.y()), 0, size.height() - 1);
    // Evaluate the winning triangle's depth plane at the exact vertex rather
    // than using a broad depth tolerance that would paint through thin meshes.
    const auto visibleAt = [&](int px, int py) {
        const auto &pixel = buffer[py * size.width() + px];
        const float surface = pixel.value + pixel.dx * float(vertex.depthPoint.x() - px - .5)
            + pixel.dy * float(vertex.depthPoint.y() - py - .5);
        const float tolerance = std::max(.0000001f, std::abs(vertex.metric) * .00001f);
        return pixel.selected && std::abs(vertex.metric - surface) <= tolerance;
    };
    if (std::isfinite(buffer[y * size.width() + x].value)) return visibleAt(x, y);
    // A vertex on the silhouette can fall just outside its triangle's sampled
    // pixels. Nearby covered pixels recover those edges without through-paint.
    for (int py = std::max(0, y - 1); py <= std::min(size.height() - 1, y + 1); ++py)
        for (int px = std::max(0, x - 1); px <= std::min(size.width() - 1, x + 1); ++px)
            if (std::isfinite(buffer[py * size.width() + px].value) && visibleAt(px, py)) return true;
    return false;
}
}

struct SkinPaintProjection::Data {
    QSize size;
    QVector<PaintVertex> visible;
    QHash<quint64, QVector<int>> bins;
};
SkinPaintProjection::SkinPaintProjection() = default;
SkinPaintProjection::~SkinPaintProjection() = default;
SkinPaintProjection::SkinPaintProjection(SkinPaintProjection &&) noexcept = default;
SkinPaintProjection &SkinPaintProjection::operator=(SkinPaintProjection &&) noexcept = default;

QColor skinWeightColor(float weight) {
    if (!std::isfinite(weight)) weight = 0;
    static const std::array<QColor, 4> stops{QColor("#2563eb"), QColor("#00cfff"), QColor("#ffdb00"), QColor("#ff3030")};
    const float position = std::clamp(weight, 0.f, 1.f) * 3;
    const int segment = std::min(2, int(position)); const float mix = position - segment;
    const auto &a = stops[segment], &b = stops[segment + 1];
    return QColor::fromRgbF(a.redF() + mix * (b.redF() - a.redF()), a.greenF() + mix * (b.greenF() - a.greenF()),
                           a.blueF() + mix * (b.blueF() - a.blueF()));
}
bool SkinPaintProjection::build(const Scene3D &scene, const QString &objectId, QSize frame,
                               const QTransform &artworkToWidget, QString *error) {
    m_data.reset(); if (error) error->clear();
    const auto fail = [&](const QString &message) { if (error) *error = message; return false; };
    if (frame.isEmpty() || !artworkToWidget.isInvertible()) return fail("The painting view is invalid.");
    const auto selected = std::find_if(scene.objects.cbegin(), scene.objects.cend(), [&](const auto &o) { return o.id == objectId; });
    if (!scene.visible || selected == scene.objects.cend() || !selected->visible || !scene.models.value(selected->modelId))
        return fail("Select a visible model to paint its weights.");
    const auto validation = validateScene3D(scene); if (!validation.isEmpty()) return fail(validation);
    QSet<QString> validatedModels;
    for (const auto &object : scene.objects) if (object.visible && !validatedModels.contains(object.modelId)) {
        const auto modelError = validateModel3D(*scene.models.value(object.modelId));
        if (!modelError.isEmpty()) return fail(modelError);
        validatedModels.insert(object.modelId);
    }
    // Weight painting displays the selected model at rest. Other objects retain
    // their saved poses and must occlude the same surfaces as the GPU reference.
    QHash<QString,Model3DPtr> displayedModels;
    Bounds3D displayedBounds;
    for(const auto &object:scene.objects)if(object.visible) {
        auto model=scene.models.value(object.modelId);
        if(object.id!=objectId&&object.skin&&!pose3DIsIdentity(object.pose)) {
            QString deformationError;
            model=deformPosedModel3D(*model,object.rig,*object.skin,object.pose,&deformationError);
            if(!model)return fail(deformationError);
        }
        displayedModels.insert(object.id,model);
        const auto transform=instanceMatrix(object); const auto &bounds=model->bounds;
        if(bounds.valid)for(int i=0;i<8;++i)displayedBounds.include(transform.map(QVector3D(
            i&1?bounds.maximum.x():bounds.minimum.x(),i&2?bounds.maximum.y():bounds.minimum.y(),
            i&4?bounds.maximum.z():bounds.minimum.z())));
    }
    auto result = std::make_shared<Data>();
    result->size = frame; if (frame.width() > maxDepthSide || frame.height() > maxDepthSide)
        result->size.scale(maxDepthSide, maxDepthSide, Qt::KeepAspectRatio);
    QVector<DepthPixel> depth(result->size.width() * result->size.height());
    QVector<ProjectedVertex> candidates;
    const auto cameraView = cameraViewMatrix(scene.camera);
    // Match ReferenceRenderer's scale-relative clipping. The general canvas
    // projection has a wider range and could expose paintable vertices on a
    // surface which the rendered reference has clipped away.
    const auto &camera = scene.camera;
    const float radius = std::max(.01f, displayedBounds.radius());
    const float near = std::max(.001f, std::min(camera.distance * .01f, radius * .01f));
    const float far = std::max(100.f, camera.distance + radius * 8.f);
    const float aspect = float(frame.width()) / frame.height();
    QMatrix4x4 projection;
    if (camera.orthographic) {
        const float half = camera.orthoScale * .5f;
        projection.ortho(-half * aspect, half * aspect, -half, half, near, far);
    } else projection.perspective(camera.fieldOfView, aspect, near, far);
    for (const auto &object : scene.objects) {
        if (!object.visible) continue;
        const auto model = displayedModels.value(object.id); if (!model) continue;
        for (int meshIndex = 0; meshIndex < model->meshes.size(); ++meshIndex) {
            const auto &mesh = model->meshes[meshIndex];
            if (object.id != objectId && mesh.alphaCutoff >= 0 && mesh.baseColor.alphaF() < mesh.alphaCutoff) continue;
            const auto modelView = cameraView * instanceMatrix(object) * mesh.transform;
            const auto matrix = projection * modelView;
            QVector<ClipVertex> vertices; vertices.reserve(mesh.positions.size());
            for (int index = 0; index < mesh.positions.size(); ++index) {
                const auto point = mesh.positions[index];
                ClipVertex vertex{matrix * QVector4D(point, 1), mesh.texCoords.value(index), -modelView.map(point).z()};
                vertices.append(vertex);
                if (object.id != objectId || vertex.cameraDepth <= 0 || vertex.clip.w() <= 0) continue;
                bool inside = true; for (int plane = 0; plane < 6; ++plane) inside &= planeDistance(vertex.clip, plane) >= 0;
                if (!inside) continue;
                const auto projected = screenVertex(vertex, result->size, scene.camera.orthographic);
                const auto ndc = vertex.clip.toVector3DAffine();
                const QPointF artwork((ndc.x() + 1) * frame.width() / 2., (1 - ndc.y()) * frame.height() / 2.);
                const auto widget = artworkToWidget.map(artwork);
                if (std::isfinite(widget.x()) && std::isfinite(widget.y()) && std::abs(widget.x()) < 1e9 && std::abs(widget.y()) < 1e9)
                    candidates.append({widget, projected.point, projected.metric, meshIndex, index});
            }
            for (int index = 0; index + 2 < mesh.indices.size(); index += 3) {
                const auto a = mesh.indices[index], b = mesh.indices[index + 1], c = mesh.indices[index + 2];
                if (a >= quint32(vertices.size()) || b >= quint32(vertices.size()) || c >= quint32(vertices.size()))
                    return fail("A model contains an invalid triangle index.");
                std::array<ClipVertex, 12> triangle; triangle[0] = vertices[a]; triangle[1] = vertices[b]; triangle[2] = vertices[c];
                const int count = clipTriangle(triangle); if (count < 3) continue;
                const auto first = screenVertex(triangle[0], result->size, scene.camera.orthographic);
                for (int i = 1; i + 1 < count; ++i)
                    rasterize(first, screenVertex(triangle[i], result->size, scene.camera.orthographic),
                        screenVertex(triangle[i + 1], result->size, scene.camera.orthographic), mesh, scene.camera.orthographic,
                        object.id == objectId, scene.textured, result->size, depth);
            }
        }
    }
    for (const auto &vertex : candidates) if (vertexVisible(vertex, result->size, depth)) {
        const int index = result->visible.size(); result->visible.append({vertex.widget, vertex.mesh, vertex.vertex});
        result->bins[binKey(int(std::floor(vertex.widget.x() / binSide)), int(std::floor(vertex.widget.y() / binSide)))].append(index);
    }
    m_data = std::move(result); return true;
}
QVector<SkinPaintHit> SkinPaintProjection::verticesInBrush(QPointF center, qreal radius) const {
    QVector<SkinPaintHit> result;
    if (!m_data || !std::isfinite(radius) || radius <= 0 || radius > 1000000 || !std::isfinite(center.x()) || !std::isfinite(center.y())
        || std::abs(center.x()) >= 1e9 || std::abs(center.y()) >= 1e9) return result;
    const int left = int(std::floor((center.x() - radius) / binSide)), right = int(std::floor((center.x() + radius) / binSide));
    const int top = int(std::floor((center.y() - radius) / binSide)), bottom = int(std::floor((center.y() + radius) / binSide));
    const auto collect = [&](const QVector<int> &indices) {
        for (const auto index : indices) {
            const auto &vertex = m_data->visible[index]; const auto delta = vertex.widget - center;
            const auto distance = std::hypot(delta.x(), delta.y()); if (distance >= radius) continue;
            const float linear = float(1 - distance / radius);
            result.append({vertex.mesh, vertex.vertex, linear * linear * (3 - 2 * linear)});
        }
    };
    if (qint64(right - left + 1) * (bottom - top + 1) > m_data->bins.size() * 4) {
        for (auto bin = m_data->bins.cbegin(); bin != m_data->bins.cend(); ++bin) {
            const int x = qint32(bin.key() >> 32), y = qint32(bin.key() & 0xffffffff);
            if (x >= left && x <= right && y >= top && y <= bottom) collect(bin.value());
        }
    } else for (int y = top; y <= bottom; ++y) for (int x = left; x <= right; ++x) {
        const auto bin = m_data->bins.constFind(binKey(x, y)); if (bin != m_data->bins.cend()) collect(bin.value());
    }
    return result;
}
int SkinPaintProjection::visibleVertexCount() const { return m_data ? m_data->visible.size() : 0; }
QSize SkinPaintProjection::depthSize() const { return m_data ? m_data->size : QSize(); }
} // namespace toon
