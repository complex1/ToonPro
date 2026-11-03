#include "document.h"
#include <QScopedValueRollback>
#include <QSet>
#include <QUuid>
#include <algorithm>
#include <new>

namespace toon {
namespace {
QString freshId() { return QUuid::createUuid().toString(QUuid::WithoutBraces); }
int shotIndex(const Project &project, const QString &id) {
    for (int i = 0; i < project.shots.size(); ++i) if (project.shots[i].id == id) return i;
    return -1;
}
QString nextNumber(const Project &project) {
    QSet<QString> used;
    for (const auto &shot : project.shots) used.insert(shot.number.toCaseFolded());
    for (int n = 1; n <= maxProjectShots + 1; ++n) {
        const auto number = QString("C%1").arg(n, 3, 10, QLatin1Char('0'));
        if (!used.contains(number.toCaseFolded())) return number;
    }
    return {};
}
void remapScene(Scene3D &scene) {
    for (auto &object : scene.objects) {
        object.id = freshId();
        QHash<QString, QString> joints;
        for (const auto &joint : object.rig.joints) joints.insert(joint.id, freshId());
        const auto mapped = [&](const QString &id) { return id.isEmpty() ? QString{} : joints.value(id, id); };
        for (auto &joint : object.rig.joints) {
            joint.id = mapped(joint.id); joint.parentId = mapped(joint.parentId); joint.mirrorId = mapped(joint.mirrorId);
        }
        if (object.skin) for (auto &id : object.skin->jointIds) id = mapped(id);
        for (auto &pose : object.pose.joints) pose.jointId = mapped(pose.jointId);
        for (auto &limit : object.pose.limits) limit.jointId = mapped(limit.jointId);
        for (auto &target : object.pose.targets) target.jointId = mapped(target.jointId);
    }
}
Shot duplicate(const Shot &source) {
    Shot shot = source; shot.id = freshId();
    QHash<QString, QString> ids;
    const auto own = [&](const QString &id) { ids.insert(id, freshId()); };
    for (const auto &layer : source.layers) {
        own(layer.id); for (const auto &object : layer.objects) own(object.id);
    }
    for (const auto &track : source.animationTracks) {
        own(track.id);
        for (const auto &drawing : track.drawings) own(drawing.id);
        for (const auto &exposure : track.exposures) own(exposure.id);
    }
    const auto mapped = [&](const QString &id) { return id.isEmpty() ? QString{} : ids.value(id, id); };
    for (auto &layer : shot.layers) {
        layer.id = mapped(layer.id); layer.parentId = mapped(layer.parentId);
        for (auto &object : layer.objects) object.id = mapped(object.id);
        if (layer.scene3d) remapScene(*layer.scene3d);
    }
    for (auto &track : shot.animationTracks) {
        track.id = mapped(track.id); track.rootGroupLayerId = mapped(track.rootGroupLayerId);
        for (auto &drawing : track.drawings) {
            drawing.id = mapped(drawing.id); drawing.groupLayerId = mapped(drawing.groupLayerId);
            for (auto it = drawing.templateLayers.begin(); it != drawing.templateLayers.end(); ++it) it.value() = mapped(it.value());
        }
        for (auto &exposure : track.exposures) { exposure.id = mapped(exposure.id); exposure.drawingId = mapped(exposure.drawingId); }
    }
    for (auto &animation : shot.layerAnimations) {
        animation.layerId = mapped(animation.layerId);
        for (auto &path : animation.paths) path.objectId = mapped(path.objectId);
    }
    for (auto &animation : shot.sceneCameraAnimations) animation.layerId = mapped(animation.layerId);
    for (auto &reference : shot.references) reference.id = freshId();
    for (auto &guide : shot.guides) guide.id = freshId();
    for (auto &selection : shot.savedSelections) selection.id = freshId();
    for (auto &audio : shot.audioClips) audio.id = freshId();
    for (auto &cue : shot.timelineCues) cue.id = freshId();
    return shot;
}
}
void Document::rememberShotView() {
    if (!m_open || m_activeShotId.isEmpty() || m_activeShot < 0 || m_activeShot >= m_project.shots.size()) return;
    const auto &shot = m_project.shots[m_activeShot];
    if (shot.id != m_activeShotId || shot.layers.isEmpty()) return;
    m_shotViews.insert(shot.id, {shot.layers[qBound(0, m_active, int(shot.layers.size()) - 1)].id,
                               m_activeAnimationTrackId, m_currentFrame});
}
bool Document::shotOperationReady() {
    if (!m_open) return false;
    if (editingGestureActive() || layerPropertyEditActive()) {
        emit warning("Finish the current edit before changing shots."); return false;
    }
    return true;
}
bool Document::selectShot(const QString &id) {
    if (!m_open || shotIndex(m_project, id) < 0) return false;
    if (id == m_activeShotId) return true;
    if (!shotOperationReady()) return false;
    apply(m_project, {}, id);
    return true;
}
QString Document::createShot(const QString &name, const QString &scene) {
    if (!shotOperationReady()) return {};
    if (m_project.shots.size() >= maxProjectShots) { emit warning("The movie has reached its 10,000-shot limit."); return {}; }
    try {
        auto after = m_project;
        auto shot = Project::create().shots.first();
        shot.number = nextNumber(after);
        shot.name = name.isEmpty() ? QString("Shot %1").arg(after.shots.size() + 1) : name.trimmed();
        shot.scene = scene.isEmpty() ? activeShot().scene : scene.trimmed();
        shot.sheet = activeShot().sheet; shot.durationFrames = activeShot().durationFrames;
        const auto id = shot.id; const auto selected = shot.layers.first().id;
        after.shots.insert(activeShotIndex() + 1, std::move(shot));
        const auto error = after.validate(); if (!error.isEmpty()) { emit warning(error); return {}; }
        if (!commit(m_project, after, "Create shot", selected, {}, {}, {}, id)) return {};
        return id;
    } catch (const std::bad_alloc &) { emit warning("There is not enough memory to create a shot."); return {}; }
}
QString Document::duplicateShot(const QString &id) {
    if (!shotOperationReady()) return {};
    const int index = shotIndex(m_project, id); if (index < 0) return {};
    if (m_project.shots.size() >= maxProjectShots) { emit warning("The movie has reached its 10,000-shot limit."); return {}; }
    try {
        auto after = m_project; auto shot = duplicate(after.shots[index]);
        shot.number = nextNumber(after); shot.name = (shot.name + " copy").left(200);
        const auto result = shot.id; const auto selected = shot.layers.first().id;
        after.shots.insert(index + 1, std::move(shot));
        const auto error = after.validate(); if (!error.isEmpty()) { emit warning(error); return {}; }
        if (!commit(m_project, after, "Duplicate shot", selected, {}, {}, {}, result)) return {};
        return result;
    } catch (const std::bad_alloc &) { emit warning("There is not enough memory to duplicate this shot."); return {}; }
}
bool Document::renameShot(const QString &id, const QString &number, const QString &name) {
    if (!shotOperationReady()) return false;
    const int index = shotIndex(m_project, id); if (index < 0) return false;
    auto after = m_project; auto &shot = after.shots[index];
    const auto cleanNumber = number.trimmed(), cleanName = name.trimmed();
    if (shot.number == cleanNumber && shot.name == cleanName) return true;
    shot.number = cleanNumber; shot.name = cleanName;
    const auto error = after.validate(); if (!error.isEmpty()) { emit warning(error); return false; }
    return commit(m_project, after, "Rename shot", {}, {}, {}, id, id);
}
bool Document::setShotScene(const QString &id, const QString &scene) {
    if (!shotOperationReady()) return false;
    const int index = shotIndex(m_project, id); if (index < 0) return false;
    auto after = m_project; const auto clean = scene.trimmed();
    if (after.shots[index].scene == clean) return true;
    after.shots[index].scene = clean;
    const auto error = after.validate(); if (!error.isEmpty()) { emit warning(error); return false; }
    return commit(m_project, after, "Move shot to scene", {}, {}, {}, id, id);
}
bool Document::moveShot(const QString &id, int destination) {
    if (!shotOperationReady()) return false;
    const int index = shotIndex(m_project, id);
    if (index < 0 || destination < 0 || destination >= m_project.shots.size()) return false;
    if (index == destination) return true;
    auto after = m_project; after.shots.move(index, destination);
    return commit(m_project, after, "Reorder shot");
}
bool Document::deleteShot(const QString &id) {
    if (!shotOperationReady()) return false;
    const int index = shotIndex(m_project, id); if (index < 0) return false;
    if (m_project.shots.size() == 1) { emit warning("Keep at least one shot in the movie."); return false; }
    auto after = m_project; after.shots.removeAt(index);
    const auto targetId = id == m_activeShotId ? after.shots[qMin(index, int(after.shots.size()) - 1)].id : m_activeShotId;
    const auto error = after.validate(); if (!error.isEmpty()) { emit warning(error); return false; }
    QScopedValueRollback mutation(m_animationMutation, true);
    return commit(m_project, after, "Delete shot", {}, {}, {}, id, targetId);
}
} // namespace toon
