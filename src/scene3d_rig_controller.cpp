#include "scene3d_controller.h"
#include "canvas.h"
#include "ui_theme.h"
#include <QPainter>
#include <QSet>
#include <QScopedValueRollback>
#include <QUuid>
#include <algorithm>
#include <cmath>

namespace toon {
namespace {
QString newJointId() { return QUuid::createUuid().toString(QUuid::WithoutBraces); }
QString jointName(const Rig3D &rig, const QString &base) {
    QSet<QString> names; for (const auto &joint : rig.joints) names.insert(joint.name);
    if (!names.contains(base)) return base;
    for (int i = 2; ; ++i) if (!names.contains(base + QString::number(i))) return base + QString::number(i);
}
QMatrix4x4 rigProjection(const Scene3D &scene, const ModelInstance3D &object, QSize frame) {
    return cameraProjectionMatrix(scene.camera, float(frame.width()) / frame.height())
        * cameraViewMatrix(scene.camera) * instanceMatrix(object);
}
std::optional<QPointF> projectJoint(const QMatrix4x4 &matrix, QVector3D position, QSize frame) {
    auto p = matrix * QVector4D(position, 1);
    if (p.w() <= .00001f) return {};
    p /= p.w();
    if (p.z() < -1 || p.z() > 1) return {};
    return QPointF((p.x() + 1) * frame.width() / 2., (1 - p.y()) * frame.height() / 2.);
}
qreal distanceToLine(QPointF point, QPointF a, QPointF b) {
    const auto delta = b - a;
    const qreal squared = QPointF::dotProduct(delta, delta);
    if (squared < .001) return QLineF(point, a).length();
    const auto t = std::clamp(QPointF::dotProduct(point - a, delta) / squared, 0., 1.);
    return QLineF(point, a + t * delta).length();
}
}

const Rig3D *Scene3DController::selectedRig() const {
    const auto *object = selectedObject(); return object ? &object->rig : nullptr;
}
const RigJoint3D *Scene3DController::selectedJoint() const {
    const auto *rig = selectedRig(); return rig ? rigJoint(*rig, m_selectedJoint) : nullptr;
}
bool Scene3DController::canEditRig() const {
    const auto *object = selectedObject();
    return canEdit() && scene()->visible && object && object->visible && !object->locked && !object->skin && !m_skinBindingBusy;
}
bool Scene3DController::mutateRig(const QString &label, const Rig3D &rig) {
    if (!canEditRig()) return false;
    const auto id = m_selected;
    const QScopedValueRollback<bool> guard(m_changingRestSkeleton, true);
    return mutate(label, [&](Scene3D &s) { for (auto &object : s.objects) if (object.id == id) object.rig = rig; });
}
void Scene3DController::selectRigTool() {
    m_canvas->setTool(Canvas::Tool::SceneRig);
    if (m_canvas->tool() != Canvas::Tool::SceneRig) return;
    if (sceneLayerIndex() >= 0) m_canvas->document()->selectLayer(sceneLayerIndex());
    if (!selectedObject() && scene()) for (const auto &object : scene()->objects)
        if (object.visible) { selectObject(object.id); break; }
    m_skinPreviewRotation={}; invalidateSkinView(); emit changed(); m_canvas->update();
}
void Scene3DController::selectJoint(const QString &id) {
    if (id == m_selectedJoint) return;
    endEdit(); m_selectedJoint = id;
    if (!selectedJoint()) m_selectedJoint.clear();
    m_skinPreviewRotation={}; invalidateSkinView(); emit changed(); m_canvas->update();
}
bool Scene3DController::createRig(bool humanoid) {
    if (!canEditRig()) { emit warning("Select a visible, unlocked model to create its skeleton."); return false; }
    if (!selectedRig()->joints.isEmpty()) { emit warning("This model already has a skeleton. Edit its existing joints."); return false; }
    const auto model = scene()->models.value(selectedObject()->modelId);
    if (!model) return false;
    Rig3D rig;
    if (humanoid) rig = createHumanoidRig3D(model->bounds);
    else { rig.mirrorPlaneX = model->bounds.center().x(); rig.joints.append({newJointId(), "Root", {}, {}, model->bounds.center()}); }
    if (rig.joints.isEmpty()) { emit warning("The model needs a non-zero height for a humanoid template. Create a root skeleton instead."); return false; }
    const auto validation = validateRig3D(rig);
    if (!validation.isEmpty()) { emit warning(validation); return false; }
    if (!mutateRig(humanoid ? "Add humanoid skeleton" : "Create skeleton", rig)) return false;
    m_selectedJoint = rig.joints.isEmpty() ? QString() : rig.joints.first().id;
    m_rigVisible = true; selectRigTool(); emit changed(); m_canvas->update(); return true;
}
QString Scene3DController::addJoint(bool child) {
    if (!canEditRig()) return {};
    endEdit();
    auto rig = *selectedRig();
    if (rig.joints.size() >= maxRig3DJoints) { emit warning("This skeleton has reached its 256-joint limit."); return {}; }
    if (child && !selectedJoint()) { emit warning("Select a parent joint first."); return {}; }
    const auto model = scene()->models.value(selectedObject()->modelId); if (!model) return {};
    RigJoint3D joint; joint.id = newJointId(); joint.name = jointName(rig, child ? "Joint" : "Root");
    joint.position = model->bounds.center();
    if (child) {
        joint.parentId = m_selectedJoint;
        joint.position = selectedJoint()->position + QVector3D(0, std::max(.01f, model->bounds.radius() * .15f), 0);
    }
    if (rig.joints.isEmpty()) rig.mirrorPlaneX = model->bounds.center().x();
    rig.joints.append(joint);
    if (!mutateRig("Add skeleton joint", rig)) return {};
    m_selectedJoint = joint.id; m_rigVisible = true; emit changed(); m_canvas->update(); return joint.id;
}
void Scene3DController::updateJoint(const RigJoint3D &value) {
    if (!canEditRig() || !selectedJoint() || value.id != m_selectedJoint) return;
    auto rig = *selectedRig(); const auto old = *selectedJoint();
    QString error;
    if (value.position != old.position && !moveRigJoint3D(rig, value.id, value.position, m_mirrorEdits, &error)) {
        emit warning(error); emit changed(); return;
    }
    for (auto &joint : rig.joints) if (joint.id == value.id) {
        joint.name = value.name.trimmed(); joint.parentId = value.parentId;
        // Pair links are structural and are managed by mirroring/removal, not a numeric edit.
        joint.mirrorId = old.mirrorId;
    }
    error = validateRig3D(rig);
    if (!error.isEmpty()) { emit warning(error); emit changed(); return; }
    mutateRig("Edit rest skeleton", rig);
}
void Scene3DController::removeSelectedJoint() {
    if (!canEditRig() || !selectedJoint()) return;
    endEdit();
    const auto parent = selectedJoint()->parentId;
    auto rig = *selectedRig(); const auto removed = rigDescendants(rig, m_selectedJoint);
    rig.joints.removeIf([&](const auto &joint) { return removed.contains(joint.id); });
    for (auto &joint : rig.joints) if (removed.contains(joint.mirrorId)) joint.mirrorId.clear();
    if (mutateRig("Delete skeleton branch", rig)) {
        m_selectedJoint = parent; emit changed(); m_canvas->update();
    }
}
void Scene3DController::mirrorSelectedBranch() {
    if (!canEditRig() || !selectedJoint()) return;
    endEdit();
    auto rig = *selectedRig(); QString error;
    if (!mirrorRigBranch3D(rig, m_selectedJoint, &error)) { emit warning(error); return; }
    mutateRig("Mirror skeleton branch", rig);
}
void Scene3DController::setMirrorEdits(bool enabled) {
    if (m_mirrorEdits == enabled) return; m_mirrorEdits = enabled; emit changed();
}
void Scene3DController::setRigVisible(bool visible) {
    if (m_rigVisible == visible) return; m_rigVisible = visible; emit changed(); m_canvas->update();
}
void Scene3DController::setRigView(RigView view) {
    if (!canEdit() || !selectedObject() || !scene() || scene()->camera.locked) return;
    auto s = *scene(); s.objects = {*selectedObject()};
    auto camera = s.camera; camera.orthographic = true;
    camera.yaw = view == RigView::Side ? 90 : 0; camera.pitch = view == RigView::Top ? 89.9f : 0;
    camera = fitCamera3D(camera, sceneBounds(s), float(m_canvas->project().width) / m_canvas->project().height);
    updateCamera(camera);
}
bool Scene3DController::rigPointerPress(QPointF artwork) {
    if (!canEditRig() || !m_rigVisible) return true;
    const auto *object = selectedObject(); const auto &rig = object->rig;
    const QSize frame(m_canvas->project().width, m_canvas->project().height);
    const auto matrix = rigProjection(*scene(), *object, frame);
    const auto view = m_canvas->artworkViewTransform(); const auto pointer = view.map(artwork);
    QString hit; qreal distance = 10;
    for (const auto &joint : rig.joints) if (auto p = projectJoint(matrix, joint.position, frame)) {
        const qreal candidate = QLineF(pointer, view.map(*p)).length();
        if (candidate < distance) { distance = candidate; hit = joint.id; }
    }
    if (hit.isEmpty()) for (const auto &joint : rig.joints) if (const auto *parent = rigJoint(rig, joint.parentId)) {
        const auto a = projectJoint(matrix, joint.position, frame), b = projectJoint(matrix, parent->position, frame);
        if (a && b) { const auto candidate = distanceToLine(pointer, view.map(*a), view.map(*b));
            if (candidate < distance) { distance = candidate; hit = joint.id; }
        }
    }
    selectJoint(hit);
    if (!hit.isEmpty() && beginEdit("Move skeleton joint")) { m_dragStart = artwork; m_dragScene = *scene(); m_dragging = true; }
    return true;
}
void Scene3DController::rigPointerMove(QPointF artwork, Qt::KeyboardModifiers modifiers) {
    if (!m_dragging || !editing() || !canEditRig() || !selectedJoint()) return;
    const ModelInstance3D *object = nullptr;
    for (const auto &o : m_dragScene.objects) if (o.id == m_selected) { object = &o; break; }
    if (!object) return;
    const auto *joint = rigJoint(object->rig, m_selectedJoint); if (!joint) return;
    const QSize frame(m_canvas->project().width, m_canvas->project().height);
    const auto modelView = cameraViewMatrix(m_dragScene.camera) * instanceMatrix(*object);
    bool invertible = false; const auto inverse = modelView.inverted(&invertible); if (!invertible) return;
    const auto cameraPosition = modelView.map(joint->position);
    if (cameraPosition.z() >= -.00001f) return;
    const auto projection = cameraProjectionMatrix(m_dragScene.camera, float(frame.width()) / frame.height());
    const auto delta = artwork - m_dragStart;
    // Move on the original joint's camera-facing plane. Inverting the full
    // projection and subtracting unprojected positions loses precision when
    // the near plane is tiny compared with the joint's depth.
    const float depth = m_dragScene.camera.orthographic ? 1.f : -cameraPosition.z();
    const QVector3D cameraDelta(float(delta.x() * 2 / frame.width()) * depth / projection(0, 0),
        float(-delta.y() * 2 / frame.height()) * depth / projection(1, 1), 0);
    auto value = *selectedJoint();
    value.position = joint->position + inverse.mapVector(cameraDelta) * (modifiers & Qt::ShiftModifier ? .1f : 1.f);
    updateJoint(value);
}
void Scene3DController::paintRig(QPainter &painter) const {
    if(m_rigEditMode==RigEditMode::Weights && !m_skinPreviewRotation.isNull())return;
    const auto *object = selectedObject();
    if (!m_rigVisible || !object || !object->visible || !scene() || !scene()->visible) return;
    const int layer = sceneLayerIndex();
    if (layer < 0) return;
    const auto &layers = m_canvas->shot().layers;
    int at = layer;
    for (int depth = 0; at >= 0 && depth <= layers.size(); ++depth) {
        if (!layers[at].visible) return;
        const auto parent = layers[at].parentId; if (parent.isEmpty()) break;
        at = -1; for (int i = 0; i < layers.size(); ++i) if (layers[i].id == parent) { at = i; break; }
        if (at < 0 || depth == layers.size()) return;
    }
    const QSize frame(m_canvas->project().width, m_canvas->project().height);
    const auto matrix = rigProjection(*scene(), *object, frame);
    const auto view = m_canvas->artworkViewTransform();
    QHash<QString, QPointF> points;
    const auto pose=m_rigEditMode==RigEditMode::Pose?evaluatePose3D(object->rig,object->pose):std::nullopt;
    for (int i=0;i<object->rig.joints.size();++i){const auto &joint=object->rig.joints[i];
        if (auto point = projectJoint(matrix, pose?pose->positions[i]:joint.position, frame)) points.insert(joint.id, view.map(*point));}
    painter.save(); painter.setRenderHint(QPainter::Antialiasing); painter.setBrush(Qt::NoBrush);
    for (const auto &joint : object->rig.joints) if (points.contains(joint.id) && points.contains(joint.parentId)) {
        const QLineF line(points.value(joint.parentId), points.value(joint.id));
        painter.setPen(QPen(QColor(15, 18, 22, 225), 5, Qt::SolidLine, Qt::RoundCap)); painter.drawLine(line);
        painter.setPen(QPen(QColor(joint.id == m_selectedJoint ? ui::accent : ui::text), 2, Qt::SolidLine, Qt::RoundCap)); painter.drawLine(line);
    }
    for (const auto &joint : object->rig.joints) if (points.contains(joint.id)) {
        const auto point = points.value(joint.id);
        if (joint.id == m_selectedJoint) continue;
        painter.setPen(QPen(QColor(ui::canvas), 3)); painter.setBrush(QColor(ui::text));
        painter.drawEllipse(point, 4, 4);
    }
    // Keep the selected marker and its name above every other joint and bone.
    if (const auto *joint = selectedJoint(); joint && points.contains(joint->id)) {
        const auto point = points.value(joint->id);
        painter.setPen(QPen(QColor(ui::canvas), 3)); painter.setBrush(QColor(ui::accent));
        painter.drawEllipse(point, 6, 6);
        const auto name = painter.fontMetrics().elidedText(joint->name, Qt::ElideRight, 160);
        const auto box = painter.fontMetrics().boundingRect(name).adjusted(-5, -3, 5, 3);
        const QRectF label(point + QPointF(11, -box.height() / 2.), box.size());
        painter.fillRect(label, QColor(ui::panel)); painter.setPen(QColor(ui::text)); painter.drawText(label, Qt::AlignCenter, name);
    }
    painter.restore();
}
} // namespace toon
