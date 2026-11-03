#include "timeline_controller.h"
#include "canvas.h"
#include "camera_tool.h"
#include "document.h"
#include "timeline_organization.h"
#include <QScopedValueRollback>
#include <algorithm>

namespace toon {
namespace {
const RasterLayer *findLayer(const Shot &shot, const QString &id) {
    for (const auto &layer : shot.layers) if (layer.id == id) return &layer;
    return nullptr;
}
bool drawingContainer(const Shot &shot, const QString &id) {
    for (const auto &track : shot.animationTracks)
        for (const auto &drawing : track.drawings) if (drawing.groupLayerId == id) return true;
    return false;
}
QStringList hierarchy(const Shot &shot) {
    QStringList result;
    for (const auto &layer : shot.layers) result.append(layer.id + ':' + layer.parentId);
    return result;
}
}

void TimelineController::syncTimelineOrganization() {
    if (!document()->isOpen()) { m_timelineLayerSelection.clear(); return; }
    const auto &shot = document()->activeShot();
    if (m_timelineLayerSelection != selectedPropertyLayerId()) m_timelineLayerSelection.clear();
    auto found = m_collapsedTimelineFolders.find(shot.id);
    if (found != m_collapsedTimelineFolders.end()) {
        for (auto it = found->begin(); it != found->end();) {
            const auto *layer = findLayer(shot,*it);
            if (!layer || !layer->group) it = found->erase(it); else ++it;
        }
    }
    // Removed shots must not leave an unbounded workspace-only state cache.
    for (auto it = m_collapsedTimelineFolders.begin(); it != m_collapsedTimelineFolders.end();) {
        if (std::none_of(document()->project().shots.cbegin(),document()->project().shots.cend(),
            [&](const auto &candidate) { return candidate.id == it.key(); })) it = m_collapsedTimelineFolders.erase(it);
        else ++it;
    }
}

bool TimelineController::selectTimelineLayer(QString id) {
    if (!document()->isOpen()) return false;
    const auto shotId = document()->activeShotId(); const auto generation = m_timelineGeneration;
    const auto *initial = findLayer(document()->activeShot(),id);
    if (!initial || drawingContainer(document()->activeShot(),id)) return false;
    if (initial->symbol) return selectSymbolClip(id);
    for (const auto &track : document()->activeShot().animationTracks)
        if (track.rootGroupLayerId == id) { const auto trackId = track.id; return selectTrack(trackId); }
    if (!prepareTransition() || !document()->isOpen() || document()->activeShotId() != shotId
        || generation != m_timelineGeneration) return false;
    const auto &shot = document()->activeShot(); const auto *layer = findLayer(shot,id);
    if (!layer) return false;
    if (layer->symbol || !animationOwner(shot,id).trackId.isEmpty()) return false;
    if (!layerPresentAtFrame(shot,id,currentFrame())) return false;
    const QScopedValueRollback<bool> guard(m_symbolSelectionChanging,true);
    m_selectedCue.clear(); m_selectedSymbolClip.clear(); m_selectedSymbolPoseLayer.clear();
    m_symbolInferenceSuppressed = true; m_selectedKeys.clear(); m_selectedCels.clear(); m_selectedAudio.clear();
    m_cameraSelected = false; m_cameraLayerId.clear(); m_canvas->cameraTool()->setEnabled(false);
    const int index = int(layer - shot.layers.constData());
    document()->selectLayer(index);
    if (selectedPropertyLayerId() != id) return false;
    m_timelineLayerSelection = id;
    emit changed(); emit animationPropertiesRequested(); return true;
}

bool TimelineController::setTimelineLayerVisible(QString id, bool visible) {
    if (!document()->isOpen()) return false;
    const auto *initial = findLayer(document()->activeShot(),id);
    if (!initial || drawingContainer(document()->activeShot(),id)) return false;
    const auto shotId = document()->activeShotId(); const auto generation = m_timelineGeneration;
    const bool previous = initial->visible; const auto parent = initial->parentId;
    if (!prepareTransition() || !document()->isOpen() || document()->activeShotId() != shotId
        || generation != m_timelineGeneration) return false;
    const auto *resolved = findLayer(document()->activeShot(),id);
    if (!resolved || resolved->visible != previous || resolved->parentId != parent) return false;
    document()->setLayersVisible({id},visible);
    const auto *result = findLayer(document()->activeShot(),id);
    return result && result->visible == visible;
}

bool TimelineController::setTimelineLayerLocked(QString id, bool locked) {
    if (!document()->isOpen()) return false;
    const auto *initial = findLayer(document()->activeShot(),id);
    if (!initial || drawingContainer(document()->activeShot(),id)) return false;
    const auto shotId = document()->activeShotId(); const auto generation = m_timelineGeneration;
    const bool previous = initial->locked; const auto parent = initial->parentId;
    if (!prepareTransition() || !document()->isOpen() || document()->activeShotId() != shotId
        || generation != m_timelineGeneration) return false;
    const auto *resolved = findLayer(document()->activeShot(),id);
    if (!resolved || resolved->locked != previous || resolved->parentId != parent) return false;
    document()->setLayersLocked({id},locked);
    const auto *result = findLayer(document()->activeShot(),id);
    return result && result->locked == locked;
}

QSet<QString> TimelineController::timelineSoloLayers() const { return m_canvas->timelineSoloLayers(); }
bool TimelineController::toggleTimelineLayerSolo(QString id) {
    if (!document()->isOpen() || !findLayer(document()->activeShot(),id)
        || drawingContainer(document()->activeShot(),id)) return false;
    const auto shotId = document()->activeShotId(); const auto generation = m_timelineGeneration;
    auto desired = timelineSoloLayers(); const auto before = desired;
    if (desired.contains(id)) desired.remove(id); else desired.insert(id);
    // Solo is a view operation and can change during playback. Resolve an
    // actual unfinished edit before changing the artwork the artist can see.
    if ((hasPendingEdits() || document()->editingGestureActive() || document()->layerPropertyEditActive())
        && !prepareTransition()) return false;
    if (!document()->isOpen() || document()->activeShotId() != shotId || generation != m_timelineGeneration
        || timelineSoloLayers() != before || !findLayer(document()->activeShot(),id)) return false;
    return m_canvas->setTimelineSoloLayers(desired);
}
bool TimelineController::clearTimelineSolo() {
    if (timelineSoloLayers().isEmpty()) return true;
    const auto generation = m_timelineGeneration;
    if ((hasPendingEdits() || document()->editingGestureActive() || document()->layerPropertyEditActive())
        && !prepareTransition()) return false;
    return generation == m_timelineGeneration && m_canvas->setTimelineSoloLayers({});
}

QString TimelineController::timelineLayerMoveError(QString id, QString parentId, QString beforeId) const {
    return document()->isOpen() ? timelineOrganizationEditError(document()->activeShot(),id,parentId,beforeId)
        : QString("Open a project before organizing tracks.");
}
bool TimelineController::moveTimelineLayer(QString id, QString parentId, QString beforeId) {
    if (!document()->isOpen()) return false;
    const auto shotId = document()->activeShotId(); const auto generation = m_timelineGeneration;
    const auto structure = hierarchy(document()->activeShot());
    if (!prepareTransition() || !document()->isOpen() || document()->activeShotId() != shotId
        || generation != m_timelineGeneration || hierarchy(document()->activeShot()) != structure) return false;
    QString error; const bool result = toon::moveTimelineLayer(document(),id,parentId,beforeId,&error);
    return result;
}
bool TimelineController::addTimelineFolder(QString parentId, QString name) {
    if (!document()->isOpen()) return false;
    const auto shotId = document()->activeShotId(); const auto generation = m_timelineGeneration;
    if (!prepareTransition() || !document()->isOpen() || document()->activeShotId() != shotId
        || generation != m_timelineGeneration) return false;
    QString created,error; const bool result = toon::addTimelineFolder(document(),parentId,name,&created,&error);
    if (result && !created.isEmpty()) {
        m_collapsedTimelineFolders[shotId].remove(parentId);
        selectTimelineLayer(created);
    }
    return result;
}

bool TimelineController::timelineFolderCollapsed(const QString &id) const {
    return document()->isOpen() && m_collapsedTimelineFolders.value(document()->activeShotId()).contains(id);
}
bool TimelineController::setTimelineFolderCollapsed(QString id, bool collapsed) {
    if (!document()->isOpen() || document()->editingGestureActive() || document()->layerPropertyEditActive()) return false;
    const auto *layer = findLayer(document()->activeShot(),id);
    if (!layer || !layer->group || drawingContainer(document()->activeShot(),id)) return false;
    auto &folders = m_collapsedTimelineFolders[document()->activeShotId()];
    if (folders.contains(id) == collapsed) return true;
    if (collapsed) folders.insert(id); else folders.remove(id);
    emit changed(); return true;
}
} // namespace toon
