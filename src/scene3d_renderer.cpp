#include "scene3d_renderer.h"
#include "scene3d_cache.h"
#include "pose3d.h"
#include <QFile>
#include <QCryptographicHash>
#include <QDataStream>
#include <QJsonDocument>
#include <QPointer>
#include <QSet>
#include <QThread>
#include <algorithm>
#include <array>
#include <atomic>
#include <stdexcept>
#include <cmath>
#include <optional>
#include <vector>

#ifdef TOON_HAS_FILAMENT
#include <filament/Camera.h>
#include <filament/Engine.h>
#include <filament/IndexBuffer.h>
#include <filament/IndirectLight.h>
#include <filament/LightManager.h>
#include <filament/Material.h>
#include <filament/MaterialInstance.h>
#include <filament/RenderableManager.h>
#include <filament/Renderer.h>
#include <filament/Scene.h>
#include <filament/SwapChain.h>
#include <filament/Texture.h>
#include <filament/TextureSampler.h>
#include <filament/TransformManager.h>
#include <filament/VertexBuffer.h>
#include <filament/View.h>
#include <filament/Viewport.h>
#include <backend/BufferDescriptor.h>
#include <backend/PixelBufferDescriptor.h>
#include <math/mat3.h>
#include <math/mat4.h>
#include <utils/EntityManager.h>
#endif

#ifdef TOON_HAS_FILAMENT
static void initializeSceneResources() { Q_INIT_RESOURCE(scene3d); }
#endif

namespace toon {
namespace {
constexpr int maxRenderSide = 4096;
struct RenderRequest {
    quint64 id = 0;
    Scene3D scene;
    QSize size;
    Scene3DRenderOptions options;
    QByteArray cacheKey;
};
bool skinPreview(const Scene3DRenderOptions &options) {
    return !options.skinObjectId.isEmpty() && (options.restPose || options.weights || !options.skinRotation.isNull());
}
bool weightPreview(const Scene3DRenderOptions &options) { return options.weights || !options.skinRotation.isNull(); }
bool showsPose(const ModelInstance3D &object, const Scene3DRenderOptions &options) {
    return object.skin && !pose3DIsIdentity(object.pose) && !(skinPreview(options) && object.id == options.skinObjectId);
}
void appendBindingKey(QDataStream &stream, const ModelInstance3D &object, quint64 revision = 0) {
    for (const auto &joint : object.rig.joints) stream << joint.id << joint.parentId << joint.position;
    stream << bool(revision) << (revision ? revision : object.skin ? skin3DRenderKey(*object.skin) : quint64(0));
}
QByteArray previewModelKey(const ModelInstance3D &object, const Scene3DRenderOptions &options) {
    QByteArray data;
    QDataStream stream(&data, QIODevice::WriteOnly);
    stream << object.modelId << options.skinJointId << options.skinRotation << options.weights;
    appendBindingKey(stream, object, options.skinRevision);
    return QCryptographicHash::hash(data, QCryptographicHash::Sha256);
}
QByteArray posedModelKey(const ModelInstance3D &object) {
    QByteArray data; QDataStream stream(&data,QIODevice::WriteOnly);
    stream<<object.modelId<<pose3DCacheKey(object.pose); appendBindingKey(stream,object);
    return QCryptographicHash::hash(data,QCryptographicHash::Sha256);
}
QByteArray requestKey(const Scene3D &scene, QSize size, const Scene3DRenderOptions &options) {
    // Binding identities retain COW snapshots instead of scanning weight arrays.
    // Saved poses affect ordinary references; rest and heat displays are scoped
    // temporary overrides and must never deduplicate against the posed artwork.
    QByteArray data = scene3DCacheKey(scene,size);
    QDataStream stream(&data, QIODevice::Append);
    if (skinPreview(options)) {
        stream << options.skinObjectId << options.skinJointId << options.skinRotation << options.weights << options.restPose;
        if(weightPreview(options))for(const auto &object:scene.objects)if(object.id==options.skinObjectId)stream<<previewModelKey(object,options);
    }
    return QCryptographicHash::hash(data, QCryptographicHash::Sha256);
}

#ifdef TOON_HAS_FILAMENT
using namespace filament;
math::float3 f3(const QVector3D &v) { return {v.x(), v.y(), v.z()}; }
math::mat4f fmat(const QMatrix4x4 &matrix) {
    math::mat4f result;
    for (int column = 0; column != 4; ++column)
        for (int row = 0; row != 4; ++row) result[column][row] = matrix(row, column);
    return result;
}
struct GpuMesh {
    VertexBuffer *vertices = nullptr;
    IndexBuffer *indices = nullptr;
    Texture *texture = nullptr;
    MaterialInstance *material = nullptr;
    Box bounds;
    QMatrix4x4 transform;
};
struct GpuModel {
    Model3DPtr source;
    bool weightPreview = false;
    std::vector<GpuMesh> meshes;
};
struct GpuInstance {
    QString modelId;
    std::vector<utils::Entity> entities;
};
struct Vertex {
    math::float3 position;
    math::float4 tangent;
    math::float2 uv;
};

class ReferenceRenderer {
public:
    ~ReferenceRenderer() { shutdown(); }
    QImage render(const Scene3D &source, QSize size, const Scene3DRenderOptions &options) {
        if (!source.visible || source.objects.isEmpty()) {
            QImage empty(size, QImage::Format_RGBA8888_Premultiplied);
            empty.fill(Qt::transparent);
            return empty;
        }
        QString validation = validateScene3D(source);
        if (!validation.isEmpty()) throw std::runtime_error(validation.toStdString());
        initialize();
        synchronize(source, options);
        if (m_size != size) {
            if (m_swapChain) m_engine->destroy(m_swapChain);
            m_swapChain = m_engine->createSwapChain(uint32_t(size.width()), uint32_t(size.height()),
                SwapChain::CONFIG_TRANSPARENT | SwapChain::CONFIG_READABLE);
            m_size = size;
        }
        m_view->setViewport({0, 0, uint32_t(size.width()), uint32_t(size.height())});
        const auto eye = cameraPosition(source.camera);
        m_camera->lookAt(f3(eye), f3(source.camera.target), {0, 1, 0});
        const double aspect = double(size.width()) / size.height();
        // A scale-relative clip range preserves tiny/large imported models.
        const double radius = std::max(0.01f, m_renderBounds.radius());
        const double distance = source.camera.distance;
        const double near = std::max(0.001, std::min(distance * 0.01, radius * 0.01));
        const double far = std::max(100.0, distance + radius * 8.0);
        if (source.camera.orthographic) {
            const double half = source.camera.orthoScale * 0.5;
            m_camera->setProjection(Camera::Projection::ORTHO, -half * aspect, half * aspect,
                                    -half, half, near, far);
        } else {
            m_camera->setProjection(source.camera.fieldOfView, aspect, near, far, Camera::Fov::VERTICAL);
        }
        QImage result(size, QImage::Format_RGBA8888_Premultiplied);
        result.fill(Qt::transparent);
        std::atomic_bool copied = false;
        if (!m_renderer->beginFrame(m_swapChain)) {
            m_engine->flushAndWait();
            if (!m_renderer->beginFrame(m_swapChain))
                throw std::runtime_error("The 3D renderer could not begin a frame.");
        }
        m_renderer->render(m_view);
        m_renderer->readPixels(0, 0, uint32_t(size.width()), uint32_t(size.height()),
            backend::PixelBufferDescriptor(result.bits(), size_t(result.sizeInBytes()),
                backend::PixelDataFormat::RGBA, backend::PixelDataType::UBYTE,
                [](void *, size_t, void *user) { static_cast<std::atomic_bool *>(user)->store(true); }, &copied));
        m_renderer->endFrame();
        // Completion drains the readback callback before result or its guard leaves scope.
        // This never runs on the Qt UI thread and is requested only on scene changes.
        m_engine->flushAndWait();
        if (!copied.load()) throw std::runtime_error("The GPU did not return a 3D reference image.");
        // Metal readback rows already match QImage top-to-bottom order.
        return result;
    }
private:
    Engine *m_engine = nullptr;
    Renderer *m_renderer = nullptr;
    Scene *m_scene = nullptr;
    View *m_view = nullptr;
    Camera *m_camera = nullptr;
    SwapChain *m_swapChain = nullptr;
    Material *m_material = nullptr;
    IndirectLight *m_ambient = nullptr;
    utils::Entity m_cameraEntity, m_light;
    QHash<QString, std::shared_ptr<GpuModel>> m_models;
    QHash<QString, GpuInstance> m_instances;
    QSize m_size;
    Bounds3D m_renderBounds;

    void initialize() {
        if (m_engine) return;
        initializeSceneResources();
        QFile package(QStringLiteral(":/scene3d/reference.filamat"));
        if (!package.open(QIODevice::ReadOnly)) throw std::runtime_error("The 3D material resource is unavailable.");
        const QByteArray data = package.readAll();
#ifdef Q_OS_MACOS
        m_engine = Engine::create(Engine::Backend::METAL);
#else
        m_engine = Engine::create();
#endif
        if (!m_engine) throw std::runtime_error("The graphics device does not support the 3D renderer.");
        m_renderer = m_engine->createRenderer();
        m_scene = m_engine->createScene();
        m_view = m_engine->createView();
        m_cameraEntity = utils::EntityManager::get().create();
        m_camera = m_engine->createCamera(m_cameraEntity);
        m_camera->setExposure(16.0f, 1.0f / 125.0f, 100.0f);
        m_material = Material::Builder().package(data.constData(), size_t(data.size())).build(*m_engine);
        if (!m_material) throw std::runtime_error("The 3D material is incompatible with this renderer.");
        m_view->setScene(m_scene);
        m_view->setCamera(m_camera);
        m_view->setBlendMode(View::BlendMode::TRANSLUCENT);
        m_view->setDithering(View::Dithering::NONE);
        m_view->setAntiAliasing(View::AntiAliasing::FXAA);
        m_view->setMultiSampleAntiAliasingOptions({.enabled = true, .sampleCount = 4});
        m_view->setShadowingEnabled(false);
        m_renderer->setClearOptions({.clearColor = {0, 0, 0, 0}, .clear = true, .discard = true});
        const math::float3 irradiance{0.8f};
        m_ambient = IndirectLight::Builder().irradiance(1, &irradiance).intensity(22000.0f).build(*m_engine);
        m_scene->setIndirectLight(m_ambient);
        m_light = utils::EntityManager::get().create();
        LightManager::Builder(LightManager::Type::DIRECTIONAL)
            .color({1.0f, 0.97f, 0.92f}).intensity(85000.0f)
            .direction({-0.6f, -1.0f, -0.8f}).castShadows(false).build(*m_engine, m_light);
        m_scene->addEntity(m_light);
    }
    void uploadMesh(const ModelMesh3D &source, GpuMesh &mesh) {
        mesh.transform = source.transform;
        auto *vertices = new std::vector<Vertex>;
        vertices->reserve(size_t(source.positions.size()));
        Bounds3D bounds;
        for (qsizetype i = 0; i < source.positions.size(); ++i) {
            auto normal = source.normals.value(i, {0, 1, 0}).normalized();
            if (normal.lengthSquared() < 0.1f) normal = {0, 1, 0};
            const QVector3D up = std::abs(normal.y()) > 0.95f ? QVector3D(1, 0, 0) : QVector3D(0, 1, 0);
            const QVector3D tangent = QVector3D::crossProduct(up, normal).normalized();
            const QVector3D bitangent = QVector3D::crossProduct(normal, tangent);
            auto rotation = math::mat3f::packTangentFrame({f3(tangent), f3(bitangent), f3(normal)});
            const auto uv = source.texCoords.value(i);
            vertices->push_back({f3(source.positions[i]), {rotation.x, rotation.y, rotation.z, rotation.w}, {uv.x(), uv.y()}});
            bounds.include(source.positions[i]);
        }
        mesh.bounds = Box(f3(bounds.center()), f3((bounds.maximum - bounds.minimum) * 0.5f));
        mesh.vertices = VertexBuffer::Builder().vertexCount(uint32_t(vertices->size())).bufferCount(1)
            .attribute(VertexAttribute::POSITION, 0, VertexBuffer::AttributeType::FLOAT3, offsetof(Vertex, position), sizeof(Vertex))
            .attribute(VertexAttribute::TANGENTS, 0, VertexBuffer::AttributeType::FLOAT4, offsetof(Vertex, tangent), sizeof(Vertex))
            .attribute(VertexAttribute::UV0, 0, VertexBuffer::AttributeType::FLOAT2, offsetof(Vertex, uv), sizeof(Vertex))
            .build(*m_engine);
        mesh.vertices->setBufferAt(*m_engine, 0, backend::BufferDescriptor(vertices->data(), vertices->size() * sizeof(Vertex),
            [](void *, size_t, void *user) { delete static_cast<std::vector<Vertex> *>(user); }, vertices));
        auto *indices = new QVector<quint32>(source.indices);
        mesh.indices = IndexBuffer::Builder().indexCount(uint32_t(indices->size())).bufferType(IndexBuffer::IndexType::UINT).build(*m_engine);
        mesh.indices->setBuffer(*m_engine, backend::BufferDescriptor(indices->constData(), size_t(indices->size()) * sizeof(quint32),
            [](void *, size_t, void *user) { delete static_cast<QVector<quint32> *>(user); }, indices));
        auto *image = new QImage(source.baseColorTexture.convertToFormat(QImage::Format_RGBA8888).flipped(Qt::Vertical));
        if (image->isNull()) { *image = QImage(1, 1, QImage::Format_RGBA8888); image->fill(Qt::white); }
        mesh.texture = Texture::Builder().width(uint32_t(image->width())).height(uint32_t(image->height()))
            .levels(0xff).usage(Texture::Usage::DEFAULT | Texture::Usage::GEN_MIPMAPPABLE).sampler(Texture::Sampler::SAMPLER_2D).format(Texture::InternalFormat::SRGB8_A8).build(*m_engine);
        mesh.texture->setImage(*m_engine, 0, backend::PixelBufferDescriptor(image->constBits(), size_t(image->sizeInBytes()),
            backend::PixelDataFormat::RGBA, backend::PixelDataType::UBYTE,
            [](void *, size_t, void *user) { delete static_cast<QImage *>(user); }, image));
        mesh.texture->generateMipmaps(*m_engine);
        mesh.material = m_material->createInstance();
        const QColor color = source.baseColor;
        mesh.material->setParameter("baseColor", RgbaType::sRGB,
            math::float4{color.redF(), color.greenF(), color.blueF(), color.alphaF()});
        mesh.material->setParameter("baseTexture", mesh.texture,
            TextureSampler(TextureSampler::MinFilter::LINEAR_MIPMAP_LINEAR, TextureSampler::MagFilter::LINEAR,
                           TextureSampler::WrapMode::REPEAT));
        mesh.material->setParameter("useTexture", source.baseColorTexture.isNull() ? 0.0f : 1.0f);
        mesh.material->setParameter("useAlpha", source.alphaCutoff >= 0 ? 1.0f : 0.0f);
        mesh.material->setMaskThreshold(source.alphaCutoff >= 0 ? source.alphaCutoff : 0.5f);
        mesh.material->setDoubleSided(source.doubleSided);
        mesh.material->setCullingMode(source.doubleSided ? MaterialInstance::CullingMode::NONE : MaterialInstance::CullingMode::BACK);
    }
    void destroyInstance(GpuInstance &instance) {
        for (auto entity : instance.entities) {
            m_scene->remove(entity);
            m_engine->destroy(entity);
            utils::EntityManager::get().destroy(entity);
        }
    }
    void destroyModel(GpuModel &model) {
        for (auto &mesh : model.meshes) {
            if (mesh.material) m_engine->destroy(mesh.material);
            if (mesh.texture) m_engine->destroy(mesh.texture);
            if (mesh.vertices) m_engine->destroy(mesh.vertices);
            if (mesh.indices) m_engine->destroy(mesh.indices);
        }
    }
    Model3DPtr previewModel(const Model3DPtr &source, const ModelInstance3D &object, const Scene3DRenderOptions &options) {
        if (!object.skin) throw std::runtime_error("Bind the model to its skeleton before previewing weights.");
        const auto error = validateSkin3D(*object.skin, object.rig, source.get());
        if (!error.isEmpty()) throw std::runtime_error(error.toStdString());
        const int joint = object.skin->jointIds.indexOf(options.skinJointId);
        if (joint < 0) throw std::runtime_error("Select a bound joint to preview its weights.");
        auto base = source;
        if (!options.skinRotation.isNull()) {
            QString error; base = deformModel3D(*source, object.rig, *object.skin, options.skinJointId, options.skinRotation, &error);
            if (!base) throw std::runtime_error(error.toStdString());
        }
        if (!options.weights) return base;
        auto result = std::make_shared<Model3D>(*base);
        QImage palette(256, 1, QImage::Format_RGBA8888);
        for (int x = 0; x < 256; ++x) palette.setPixelColor(x, 0, skinWeightColor(float(x) / 255));
        for (int m = 0; m < result->meshes.size(); ++m) {
            auto &mesh = result->meshes[m]; mesh.baseColor = Qt::white; mesh.baseColorTexture = palette; mesh.alphaCutoff = -1;
            mesh.texCoords.resize(mesh.positions.size());
            for (int v = 0; v < mesh.positions.size(); ++v) {
                const float weight = skinJointWeight(object.skin->meshes[m][v], quint16(joint));
                // Keep endpoint samples at texel centers to avoid repeat-wrap
                // mixing the red and blue ends of the palette.
                mesh.texCoords[v] = {(.5f + weight * 255.f) / 256.f, .5f};
            }
        }
        return result;
    }
    void synchronize(const Scene3D &source, const Scene3DRenderOptions &options) {
        QSet<QString> wantedObjects, wantedModels;
        QHash<QString, QString> modelKeys;
        bool selectedFound = !skinPreview(options);
        for (const auto &object : source.objects) {
            wantedObjects.insert(object.id);
            QString key = object.modelId;
            if(skinPreview(options)&&object.id==options.skinObjectId) {
                selectedFound=true;
                if(weightPreview(options))key="skin/"+QString::fromLatin1(previewModelKey(object,options).toHex());
            } else if(showsPose(object,options))key="pose/"+QString::fromLatin1(posedModelKey(object).toHex());
            modelKeys.insert(object.id, key); wantedModels.insert(key);
        }
        if (!selectedFound) throw std::runtime_error("Select a model to preview its weights.");
        for (auto it = m_instances.begin(); it != m_instances.end();) {
            if (!wantedObjects.contains(it.key()) || modelKeys.value(it.key()) != it->modelId) {
                destroyInstance(it.value());
                it = m_instances.erase(it);
            } else ++it;
        }
        for (auto it = m_models.begin(); it != m_models.end();) {
            if (!wantedModels.contains(it.key())) {
                destroyModel(*it.value());
                it = m_models.erase(it);
            } else ++it;
        }
        for (const auto &object : source.objects) {
            const auto modelKey = modelKeys.value(object.id);
            if (!m_models.contains(modelKey)) {
                auto model = std::make_shared<GpuModel>();
                model->source = source.models.value(object.modelId);
                if (skinPreview(options) && object.id == options.skinObjectId && weightPreview(options)) {
                    model->source = previewModel(model->source, object, options);
                    model->weightPreview = options.weights;
                } else if(showsPose(object,options)) {
                    QString error;
                    model->source=deformPosedModel3D(*model->source,object.rig,*object.skin,object.pose,&error);
                    if(!model->source)throw std::runtime_error(error.toStdString());
                }
                const QString validation = validateModel3D(*model->source);
                if (!validation.isEmpty()) throw std::runtime_error(validation.toStdString());
                m_models.insert(modelKey, model);
                for (const auto &mesh : model->source->meshes) {
                    model->meshes.emplace_back();
                    uploadMesh(mesh, model->meshes.back());
                }
            }
            const auto &model = *m_models[modelKey];
            if (!m_instances.contains(object.id)) {
                GpuInstance instance;
                instance.modelId = modelKey;
                m_instances.insert(object.id, instance);
                for (const auto &mesh : model.meshes) {
                    const auto entity = utils::EntityManager::get().create();
                    m_instances[object.id].entities.push_back(entity);
                    RenderableManager::Builder(1).boundingBox(mesh.bounds).material(0, mesh.material)
                        .geometry(0, RenderableManager::PrimitiveType::TRIANGLES, mesh.vertices, mesh.indices)
                        .castShadows(false).receiveShadows(false).build(*m_engine, entity);
                }
            }
            const auto &instance = m_instances[object.id];
            auto &transforms = m_engine->getTransformManager();
            for (size_t i = 0; i < instance.entities.size(); ++i) {
                const auto entity = instance.entities[i];
                transforms.setTransform(transforms.getInstance(entity), fmat(instanceMatrix(object) * model.meshes[i].transform));
                if (object.visible) m_scene->addEntity(entity); else m_scene->remove(entity);
                model.meshes[i].material->setParameter("useTexture", (source.textured || model.weightPreview) && !model.source->meshes[qsizetype(i)].baseColorTexture.isNull() ? 1.0f : 0.0f);
            }
        }
        m_renderBounds={};
        for(const auto &object:source.objects)if(object.visible) {
            const auto &bounds=m_models.value(modelKeys.value(object.id))->source->bounds;
            if(!bounds.valid)continue;
            const auto transform=instanceMatrix(object);
            for(int i=0;i<8;++i)m_renderBounds.include(transform.map(QVector3D(i&1?bounds.maximum.x():bounds.minimum.x(),
                i&2?bounds.maximum.y():bounds.minimum.y(),i&4?bounds.maximum.z():bounds.minimum.z())));
        }
    }
    void shutdown() {
        if (!m_engine) return;
        for (auto &instance : m_instances) destroyInstance(instance);
        m_instances.clear();
        for (auto &model : m_models) destroyModel(*model);
        m_models.clear();
        m_engine->destroy(m_light);
        utils::EntityManager::get().destroy(m_light);
        if (m_view) m_engine->destroy(m_view);
        if (m_scene) m_engine->destroy(m_scene);
        if (m_material) m_engine->destroy(m_material);
        if (m_ambient) m_engine->destroy(m_ambient);
        if (m_renderer) m_engine->destroy(m_renderer);
        if (m_swapChain) m_engine->destroy(m_swapChain);
        if (m_camera) m_engine->destroyCameraComponent(m_cameraEntity);
        utils::EntityManager::get().destroy(m_cameraEntity);
        Engine::destroy(&m_engine);
    }
};
#endif

class RenderWorker final : public QObject {
    Q_OBJECT
public:
    void render(const RenderRequest &request) {
        QImage result;
        QString error;
        try {
#ifdef TOON_HAS_FILAMENT
            if (!m_renderer) m_renderer = std::make_unique<ReferenceRenderer>();
            result = m_renderer->render(request.scene, request.size, request.options);
#else
            Q_UNUSED(request)
            error = tr("3D rendering is unavailable in this build. Install the supported Filament SDK and rebuild.");
#endif
        } catch (const std::exception &exception) {
            error = tr("Could not render the 3D reference: %1").arg(QString::fromUtf8(exception.what()));
#ifdef TOON_HAS_FILAMENT
            m_renderer.reset();
#endif
        } catch (...) {
            error = tr("Could not render the 3D reference.");
#ifdef TOON_HAS_FILAMENT
            m_renderer.reset();
#endif
        }
        emit completed(request.id, result, error);
    }
    void shutdown() {
#ifdef TOON_HAS_FILAMENT
        m_renderer.reset();
#endif
    }
signals:
    void completed(quint64 id, const QImage &image, const QString &error);
private:
#ifdef TOON_HAS_FILAMENT
    std::unique_ptr<ReferenceRenderer> m_renderer;
#endif
};
} // namespace

struct Scene3DRenderService::State {
    QThread thread;
    RenderWorker *worker = nullptr;
    quint64 latest = 0, completed = 0;
    bool running = false;
    std::optional<RenderRequest> pending;
    std::optional<RenderRequest> previous;
};
Scene3DRenderService::Scene3DRenderService(QObject *parent) : QObject(parent), m_state(std::make_unique<State>()) {}
Scene3DRenderService::~Scene3DRenderService() {
    if (m_state->worker) {
        auto *worker = m_state->worker;
        QMetaObject::invokeMethod(worker, [worker] { worker->shutdown(); }, Qt::BlockingQueuedConnection);
        m_state->thread.quit();
        m_state->thread.wait();
    }
}
quint64 Scene3DRenderService::request(const Scene3D &scene, QSize size, const Scene3DRenderOptions &options) {
    Q_ASSERT(thread() == QThread::currentThread());
    if (size.width() <= 0 || size.height() <= 0) { clear(); return m_state->latest; }
    if (size.width() > maxRenderSide || size.height() > maxRenderSide)
        size.scale(maxRenderSide, maxRenderSide, Qt::KeepAspectRatio);
    const auto key = requestKey(scene, size, options);
    if (m_state->previous && m_state->previous->cacheKey == key)
        return m_state->previous->id;
    RenderRequest next{++m_state->latest, scene, size, options, key};
    m_state->previous = next;
    m_state->pending = std::move(next);
    // Defer dispatch one event-loop turn, coalescing a burst of synchronous edits.
    QMetaObject::invokeMethod(this, &Scene3DRenderService::dispatch, Qt::QueuedConnection);
    return m_state->latest;
}
void Scene3DRenderService::clear() {
    ++m_state->latest;
    m_state->previous.reset();
    m_state->pending.reset();
    if (m_state->worker) {
        auto *worker = m_state->worker;
        QMetaObject::invokeMethod(worker, [worker] { worker->shutdown(); }, Qt::QueuedConnection);
    }
}
quint64 Scene3DRenderService::latestRequest() const { return m_state->latest; }
quint64 Scene3DRenderService::renderCount() const { return m_state->completed; }
bool Scene3DRenderService::busy() const { return m_state->running || m_state->pending.has_value(); }
void Scene3DRenderService::dispatch() {
    if (m_state->running || !m_state->pending) return;
    if (!m_state->worker) {
        auto *worker = new RenderWorker;
        m_state->worker = worker;
        worker->moveToThread(&m_state->thread);
        connect(&m_state->thread, &QThread::finished, worker, &QObject::deleteLater);
        connect(worker, &RenderWorker::completed, this, [this](quint64 id, const QImage &image, const QString &error) {
            m_state->running = false;
            ++m_state->completed;
            if (id == m_state->latest) {
                if (error.isEmpty()) emit frameReady(id, image);
                else { m_state->previous.reset(); emit failed(id, error); }
            }
            dispatch();
        });
        m_state->thread.setObjectName(QStringLiteral("ToonPro3DRenderer"));
        m_state->thread.start();
    }
    auto next = std::move(*m_state->pending);
    m_state->pending.reset();
    m_state->running = true;
    auto *worker = m_state->worker;
    QMetaObject::invokeMethod(worker, [worker, next = std::move(next)] { worker->render(next); }, Qt::QueuedConnection);
}
} // namespace toon
#include "scene3d_renderer.moc"
