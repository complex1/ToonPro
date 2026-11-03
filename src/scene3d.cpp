#include "scene3d.h"
#include "scene_model_io.h"
#include <QJsonArray>
#include <QSet>
#include <QtMath>
#include <algorithm>
#include <cmath>

namespace toon {
namespace {
bool finite(float v, float limit = 1000000) { return std::isfinite(v) && std::abs(v) <= limit; }
bool finite(const QVector3D &v) { return finite(v.x()) && finite(v.y()) && finite(v.z()); }
QJsonArray jsonVector(const QVector3D &v) { return {v.x(), v.y(), v.z()}; }
bool readVector(const QJsonValue &value, QVector3D &v) {
    const auto a = value.toArray();
    if (!value.isArray() || a.size() != 3) return false;
    for (const auto &x : a) if (!x.isDouble() || !finite(float(x.toDouble()))) return false;
    v = {float(a[0].toDouble()), float(a[1].toDouble()), float(a[2].toDouble())}; return true;
}
bool number(const QJsonObject &o, const char *key, float &v) {
    const auto x = o.value(QLatin1String(key)); if (!x.isDouble() || !finite(float(x.toDouble()))) return false;
    v = float(x.toDouble()); return true;
}
bool boolean(const QJsonObject &o, const char *key, bool &v) {
    const auto x = o.value(QLatin1String(key)); if (!x.isBool()) return false; v = x.toBool(); return true;
}
QJsonObject jsonRig(const Rig3D &rig) {
    QJsonArray joints;
    for (const auto &joint : rig.joints) joints.append(QJsonObject{{"id", joint.id}, {"name", joint.name},
        {"parentId", joint.parentId}, {"mirrorId", joint.mirrorId}, {"position", jsonVector(joint.position)}});
    return {{"mirrorPlaneX", rig.mirrorPlaneX}, {"joints", joints}};
}
bool readRig(const QJsonValue &value, Rig3D &rig) {
    if (!value.isObject()) return false;
    const auto json = value.toObject();
    if (!number(json, "mirrorPlaneX", rig.mirrorPlaneX) || !json.value("joints").isArray()
        || json.value("joints").toArray().size() > maxRig3DJoints) return false;
    for (const auto &entry : json.value("joints").toArray()) {
        if (!entry.isObject()) return false;
        const auto item = entry.toObject();
        for (const char *key : {"id", "name", "parentId", "mirrorId"}) if (!item.value(QLatin1String(key)).isString()) return false;
        RigJoint3D joint{item.value("id").toString(), item.value("name").toString(),
            item.value("parentId").toString(), item.value("mirrorId").toString(), {}};
        if (!readVector(item.value("position"), joint.position)) return false;
        rig.joints.append(joint);
    }
    return true;
}
void includeTransformed(Bounds3D &b, const Bounds3D &source, const QMatrix4x4 &matrix) {
    if (!source.valid) return;
    for (int mask = 0; mask < 8; ++mask) b.include(matrix.map({mask & 1 ? source.maximum.x() : source.minimum.x(),
        mask & 2 ? source.maximum.y() : source.minimum.y(), mask & 4 ? source.maximum.z() : source.minimum.z()}));
}
ModelMesh3D cubeMesh(const QVector3D &center, const QVector3D &size, const QColor &color) {
    ModelMesh3D m; m.baseColor = color; m.transform.translate(center); m.transform.scale(size);
    const QVector3D faces[6][4] = {
        {{-.5f,-.5f,.5f},{.5f,-.5f,.5f},{.5f,.5f,.5f},{-.5f,.5f,.5f}},
        {{.5f,-.5f,-.5f},{-.5f,-.5f,-.5f},{-.5f,.5f,-.5f},{.5f,.5f,-.5f}},
        {{.5f,-.5f,.5f},{.5f,-.5f,-.5f},{.5f,.5f,-.5f},{.5f,.5f,.5f}},
        {{-.5f,-.5f,-.5f},{-.5f,-.5f,.5f},{-.5f,.5f,.5f},{-.5f,.5f,-.5f}},
        {{-.5f,.5f,.5f},{.5f,.5f,.5f},{.5f,.5f,-.5f},{-.5f,.5f,-.5f}},
        {{-.5f,-.5f,-.5f},{.5f,-.5f,-.5f},{.5f,-.5f,.5f},{-.5f,-.5f,.5f}}};
    for (const auto &face : faces) {
        const quint32 i = quint32(m.positions.size()); const auto n = QVector3D::normal(face[1] - face[0], face[2] - face[0]);
        for (int k = 0; k < 4; ++k) { m.positions << face[k]; m.normals << n; m.texCoords << QVector2D(k == 1 || k == 2, k < 2); }
        m.indices << i << i+1 << i+2 << i << i+2 << i+3;
    } return m;
}
ModelMesh3D sphereMesh(const QVector3D &center, const QVector3D &size, const QColor &color) {
    ModelMesh3D m; m.baseColor = color; m.transform.translate(center); m.transform.scale(size);
    constexpr int rings = 16, slices = 24;
    for (int y=0; y<=rings; ++y) for (int x=0; x<=slices; ++x) {
        const float a = float(M_PI) * y / rings, b = float(2*M_PI) * x / slices;
        QVector3D n(std::sin(a)*std::cos(b), std::cos(a), std::sin(a)*std::sin(b));
        m.positions << n; m.normals << n; m.texCoords << QVector2D(float(x)/slices, float(y)/rings);
    }
    for (int y=0; y<rings; ++y) for (int x=0; x<slices; ++x) {
        quint32 a = quint32(y*(slices+1)+x), b=a+slices+1;
        m.indices << a << a+1 << b << a+1 << b+1 << b;
    }
    return m;
}
}
void Bounds3D::include(const QVector3D &p) {
    if (!valid) { minimum = maximum = p; valid = true; return; }
    minimum = {std::min(minimum.x(),p.x()),std::min(minimum.y(),p.y()),std::min(minimum.z(),p.z())};
    maximum = {std::max(maximum.x(),p.x()),std::max(maximum.y(),p.y()),std::max(maximum.z(),p.z())};
}
float Bounds3D::radius() const { return valid ? std::max(.001f,(maximum-minimum).length()*.5f) : 1.f; }
bool Scene3D::operator==(const Scene3D &o) const {
    if (objects != o.objects || camera != o.camera || visible != o.visible || opacity != o.opacity || textured != o.textured || models.size() != o.models.size()) return false;
    for (auto it=models.cbegin(); it!=models.cend(); ++it) if (!o.models.contains(it.key()) || bool(it.value()) != bool(o.models.value(it.key()))) return false;
    return true; // Content-addressed, immutable assets compare by key, not allocation address.
}
QMatrix4x4 instanceMatrix(const ModelInstance3D &o) {
    QMatrix4x4 m; m.translate(o.position); m.rotate(o.rotation.z(),0,0,1); m.rotate(o.rotation.y(),0,1,0); m.rotate(o.rotation.x(),1,0,0); m.scale(o.scale); return m;
}
QVector3D cameraPosition(const Camera3D &c) {
    float yaw=qDegreesToRadians(c.yaw), pitch=qDegreesToRadians(c.pitch);
    return c.target + c.distance*QVector3D(std::cos(pitch)*std::sin(yaw),std::sin(pitch),std::cos(pitch)*std::cos(yaw));
}
QMatrix4x4 cameraViewMatrix(const Camera3D &c) { QMatrix4x4 m; m.lookAt(cameraPosition(c),c.target,{0,1,0}); return m; }
QMatrix4x4 cameraProjectionMatrix(const Camera3D &c,float aspect) {
    QMatrix4x4 m; aspect=std::max(.01f,aspect); const float near=std::max(.001f,c.distance*.0001f), far=std::max(1000.f,c.distance*10);
    if (c.orthographic) m.ortho(-c.orthoScale*aspect*.5f,c.orthoScale*aspect*.5f,-c.orthoScale*.5f,c.orthoScale*.5f,near,far);
    else m.perspective(c.fieldOfView,aspect,near,far); return m;
}
Bounds3D sceneBounds(const Scene3D &scene,bool visibleOnly) {
    Bounds3D b;
    for(const auto &o:scene.objects) {
        const auto model=scene.models.value(o.modelId);
        if(model && (!visibleOnly || o.visible)) {
            const auto bounds=o.skin && !pose3DIsIdentity(o.pose)?posedModelBounds3D(*model,o.rig,o.pose):model->bounds;
            includeTransformed(b,bounds,instanceMatrix(o));
        }
    }
    return b;
}
Camera3D fitCamera3D(const Camera3D &camera,const Bounds3D &b,float aspect) {
    auto c=camera; if(!b.valid) return c; c.target=b.center(); float radius=b.radius();
    float vertical=qDegreesToRadians(c.fieldOfView)*.5f, horizontal=std::atan(std::tan(vertical)*std::max(.01f,aspect));
    c.distance=std::clamp(radius/std::sin(std::min(vertical,horizontal))*1.15f,.01f,100000.f);
    c.orthoScale=std::clamp(radius*2.3f/std::min(1.f,std::max(.01f,aspect)),.001f,100000.f); return c;
}
QString validateModel3D(const Model3D &m) {
    if(m.name.isEmpty() || m.name.size()>256 || m.meshes.isEmpty() || m.meshes.size()>maxModel3DMeshes) return "Invalid model name or mesh count.";
    qint64 vertices=0,triangles=0,pixels=0; QSet<qint64> textures;
    for(const auto &mesh:m.meshes) {
        vertices+=mesh.positions.size(); triangles+=mesh.indices.size()/3;
        if(vertices>maxModel3DVertices || triangles>maxModel3DTriangles) return "Model exceeds the one-million vertex/triangle limit.";
        if(mesh.positions.isEmpty() || mesh.indices.isEmpty() || mesh.indices.size()%3 || mesh.name.size()>256 || mesh.normals.size()!=mesh.positions.size() || (!mesh.texCoords.isEmpty() && mesh.texCoords.size()!=mesh.positions.size())) return "Invalid model mesh arrays.";
        for(const auto &p:mesh.positions) if(!finite(p)) return "Model contains invalid coordinates.";
        for(const auto &n:mesh.normals) if(!finite(n)) return "Model contains invalid normals.";
        for(const auto &uv:mesh.texCoords) if(!finite(uv.x()) || !finite(uv.y())) return "Model contains invalid texture coordinates.";
        for(auto i:mesh.indices) if(i>=quint32(mesh.positions.size())) return "Model contains an out-of-range triangle index.";
        for(int i=0;i<16;++i) if(!finite(mesh.transform.constData()[i])) return "Model contains an invalid transform.";
        if(!mesh.transform.isAffine() || !mesh.transform.inverted().isAffine() || std::abs(mesh.transform.determinant())<1e-12) return "Model has a singular or non-affine transform.";
        if(!mesh.baseColor.isValid() || !std::isfinite(mesh.alphaCutoff) || (mesh.alphaCutoff != -1 && (mesh.alphaCutoff < 0 || mesh.alphaCutoff > 1))) return "Model has an invalid material color or alpha cutoff.";
        if(!mesh.baseColorTexture.isNull()) {
            if(mesh.baseColorTexture.width()>4096 || mesh.baseColorTexture.height()>4096) return "Model texture exceeds 4096 pixels per side.";
            if(!textures.contains(mesh.baseColorTexture.cacheKey())) { textures.insert(mesh.baseColorTexture.cacheKey()); pixels+=qint64(mesh.baseColorTexture.width())*mesh.baseColorTexture.height(); }
            if(pixels>maxModel3DTexturePixels) return "Model exceeds the 16-megapixel texture budget.";
        }
        for(const auto &p:mesh.positions) if(!finite(mesh.transform.map(p))) return "Transformed model bounds are too large.";
    } return {};
}
QString validateScene3D(const Scene3D &s) {
    if(s.objects.size()>maxScene3DObjects || s.models.size()>maxScene3DModels) return "3D scene exceeds its object or model limit.";
    if(!finite(s.opacity) || s.opacity<0 || s.opacity>1) return "Invalid 3D scene opacity.";
    const auto &c=s.camera;
    if(!finite(c.target) || !finite(c.yaw) || !finite(c.pitch) || std::abs(c.pitch)>89.9f || !finite(c.distance) || c.distance<.001f || c.distance>100000 || !finite(c.fieldOfView) || c.fieldOfView<5 || c.fieldOfView>150 || !finite(c.orthoScale) || c.orthoScale<.001f || c.orthoScale>100000) return "Invalid 3D camera.";
    for(auto it=s.models.cbegin();it!=s.models.cend();++it) if(!it.value() || it.key()!=it.value()->id || it.key().size()!=64) return "Invalid 3D model reference.";
    QSet<QString> ids; qint64 skinBytes = 0;
    for(const auto &o:s.objects) {
        if(o.id.isEmpty() || o.id.size()>128 || ids.contains(o.id) || o.name.isEmpty() || o.name.size()>256 || !s.models.contains(o.modelId)) return "Invalid 3D object or missing model.";
        ids.insert(o.id);
        if(!finite(o.position) || !finite(o.rotation) || !finite(o.scale) || std::abs(o.scale.x())<.0001f || std::abs(o.scale.y())<.0001f || std::abs(o.scale.z())<.0001f || std::abs(o.scale.x())>10000 || std::abs(o.scale.y())>10000 || std::abs(o.scale.z())>10000) return "Invalid 3D object transform.";
        const auto rigError = validateRig3D(o.rig);
        if (!rigError.isEmpty()) return rigError;
        const auto poseError = validatePose3D(o.pose, o.rig);
        if (!poseError.isEmpty()) return poseError;
        if (!o.skin && o.pose != Pose3D{}) return "Bind the model before storing a pose or joint limits.";
        if (o.skin) {
            skinBytes += skin3DByteSize(*o.skin);
            if (skinBytes > maxProjectSkin3DBytes) return "Scene skin weights exceed the 48 MiB decoded limit.";
            const auto model = s.models.value(o.modelId);
            const auto skinError = validateSkin3D(*o.skin, o.rig, model && !model->meshes.isEmpty() ? model.get() : nullptr, true);
            if (!skinError.isEmpty()) return skinError;
        }
    } return {};
}
QJsonObject scene3DToJson(const Scene3D &s, bool includeRig) {
    QJsonArray objects,models; auto keys=s.models.keys(); std::sort(keys.begin(),keys.end()); for(const auto &id:keys) models.append(id);
    for(const auto &o:s.objects) {
        QJsonObject object{{"id",o.id},{"name",o.name},{"modelId",o.modelId},{"position",jsonVector(o.position)},{"rotation",jsonVector(o.rotation)},{"scale",jsonVector(o.scale)},{"visible",o.visible},{"locked",o.locked}};
        object.insert("pose", pose3DToJson(o.pose));
        if (includeRig) {
            object.insert("rig", jsonRig(o.rig));
            if (o.skin) object.insert("skin", skin3DToJson(*o.skin));
        }
        objects.append(object);
    }
    const auto &c=s.camera;
    return {{"version",4},{"models",models},{"objects",objects},{"visible",s.visible},{"opacity",s.opacity},{"textured",s.textured},{"camera",QJsonObject{{"target",jsonVector(c.target)},{"yaw",c.yaw},{"pitch",c.pitch},{"distance",c.distance},{"fieldOfView",c.fieldOfView},{"orthographic",c.orthographic},{"orthoScale",c.orthoScale},{"locked",c.locked}}}};
}
std::optional<Scene3D> scene3DFromJson(const QJsonObject &j,const QHash<QString,Model3DPtr> &models,QString *error) {
    auto fail=[&](const QString &message)->std::optional<Scene3D>{if(error)*error=message;return std::nullopt;};
    const auto version = j.value("version");
    if((version != 1 && version != 2 && version != 3 && version != 4) || !j.value("models").isArray() || !j.value("objects").isArray() || !j.value("camera").isObject()) return fail("Unsupported or malformed 3D scene.");
    Scene3D s; if(j.value("models").toArray().size()>maxScene3DModels || j.value("objects").toArray().size()>maxScene3DObjects) return fail("3D scene exceeds its object or model limit.");
    for(const auto &v:j.value("models").toArray()) { QString id=v.toString(); if(!models.contains(id) || s.models.contains(id)) return fail("3D scene references a missing or duplicate model."); s.models.insert(id,models.value(id)); }
    auto c=j.value("camera").toObject();
    if(!boolean(j,"visible",s.visible)||!number(j,"opacity",s.opacity)||!boolean(j,"textured",s.textured)||!readVector(c.value("target"),s.camera.target)||!number(c,"yaw",s.camera.yaw)||!number(c,"pitch",s.camera.pitch)||!number(c,"distance",s.camera.distance)||!number(c,"fieldOfView",s.camera.fieldOfView)||!boolean(c,"orthographic",s.camera.orthographic)||!number(c,"orthoScale",s.camera.orthoScale)||!boolean(c,"locked",s.camera.locked)) return fail("Malformed 3D camera or display settings.");
    qint64 skinBytes = 0;
    for(const auto &v:j.value("objects").toArray()) { auto j=v.toObject(); ModelInstance3D o; o.id=j.value("id").toString();o.name=j.value("name").toString();o.modelId=j.value("modelId").toString();
        if(!readVector(j.value("position"),o.position)||!readVector(j.value("rotation"),o.rotation)||!readVector(j.value("scale"),o.scale)||!boolean(j,"visible",o.visible)||!boolean(j,"locked",o.locked))return fail("Malformed 3D object.");
        if (version == 1 && j.contains("rig")) return fail("Rig metadata requires 3D scene version 2.");
        if ((version == 2 || version == 3 || version == 4) && !readRig(j.value("rig"), o.rig)) return fail("Malformed 3D rig metadata.");
        if (version != 4 && j.contains("pose")) return fail("Pose metadata requires 3D scene version 4.");
        if (version == 4) {
            if (!j.value("pose").isObject()) return fail("Malformed pose metadata.");
            QString poseError; const auto pose = pose3DFromJson(j.value("pose").toObject(), o.rig, &poseError);
            if (!pose) return fail(poseError);
            o.pose = *pose;
        }
        if (j.contains("skin")) {
            if (version != 3 && version != 4) return fail("Skin metadata requires 3D scene version 3.");
            if (!j.value("skin").isObject()) return fail("Malformed skin metadata.");
            for (const auto &count : j.value("skin").toObject().value("meshVertexCounts").toArray()) {
                const double n = count.toDouble(-1);
                if (!count.isDouble() || !std::isfinite(n) || n < 1 || n > maxModel3DVertices || std::floor(n) != n)
                    return fail("Invalid skin vertex count.");
                skinBytes += qint64(n) * 24;
                if (skinBytes > maxProjectSkin3DBytes) return fail("Scene skin weights exceed the 48 MiB decoded limit.");
            }
            QString skinError; o.skin = skin3DFromJson(j.value("skin").toObject(), o.rig, &skinError);
            if (!o.skin) return fail(skinError);
        }
        s.objects<<o; }
    const auto validation=validateScene3D(s); if(!validation.isEmpty())return fail(validation); if(error)error->clear();return s;
}
QVector<Model3DPtr> builtInModels3D() {
    static const QVector<Model3DPtr> models=[] {
        QVector<Model3DPtr> result; auto add=[&](QString name,QVector<ModelMesh3D> meshes){Model3D m;m.name=name;m.meshes=std::move(meshes);result<<finalizeModel3D(std::move(m));};
        add("Cube",{cubeMesh({0,.5f,0},{1,1,1},QColor(180,193,210))});
        add("Sphere",{sphereMesh({0,.5f,0},{.5f,.5f,.5f},QColor(190,195,205))});
        ModelMesh3D plane;plane.name="Ground";plane.positions={{-2,0,2},{2,0,2},{2,0,-2},{-2,0,-2}};plane.normals=QVector<QVector3D>(4,{0,1,0});plane.texCoords={{0,1},{1,1},{1,0},{0,0}};plane.indices={0,1,2,0,2,3};plane.doubleSided=true;plane.baseColor=QColor(145,150,160);add("Ground plane",{plane});
        QVector<ModelMesh3D> mannequin;const QColor body(204,182,155),joint(154,139,124);
        mannequin<<sphereMesh({0,1.25f,0},{.25f,.32f,.13f},body)<<sphereMesh({0,.95f,0},{.21f,.16f,.13f},body)<<sphereMesh({0,1.75f,0},{.14f,.18f,.13f},body)<<sphereMesh({0,1.53f,0},{.065f,.09f,.065f},joint);
        for(float side:{-1.f,1.f}) {
            mannequin<<sphereMesh({side*.14f,.65f,0},{.085f,.28f,.095f},body)<<sphereMesh({side*.14f,.24f,0},{.065f,.19f,.075f},body)<<sphereMesh({side*.14f,.43f,0},{.075f,.075f,.075f},joint)<<cubeMesh({side*.14f,.045f,.06f},{.15f,.09f,.26f},body);
            mannequin<<sphereMesh({side*.31f,1.4f,0},{.095f,.095f,.095f},joint)<<sphereMesh({side*.43f,1.2f,0},{.08f,.22f,.08f},body)<<sphereMesh({side*.51f,1.f,0},{.07f,.07f,.07f},joint)<<sphereMesh({side*.57f,.83f,0},{.06f,.18f,.06f},body)<<sphereMesh({side*.61f,.62f,0},{.065f,.1f,.035f},body);
        } add("Mannequin (static)",mannequin);
        QVector<ModelMesh3D> house{cubeMesh({0,1,0},{2,2,1.7f},QColor(198,185,164)),cubeMesh({0,2.15f,0},{2.3f,.3f,2},QColor(91,107,126)),cubeMesh({0,.6f,.86f},{.45f,1.2f,.04f},QColor(98,79,66))};
        for(float side:{-.6f,.6f}) house<<cubeMesh({side,1.35f,.86f},{.55f,.55f,.04f},QColor(110,161,180));
        add("Simple building",house);return result;
    }(); return models;
}
} // namespace toon
