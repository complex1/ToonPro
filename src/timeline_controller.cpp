#include "timeline_controller.h"
#include "canvas.h"
#include "document.h"
#include "drawing_tool_controller.h"
#include "scene3d_controller.h"
#include "keyframe_transform_tool.h"
#include "camera_tool.h"
#include "mask_tool.h"
#include "motion_controls.h"
#include "audio_import.h"
#include "audio_playback.h"
#include "audio_scrub.h"
#include "movie_review_controller.h"
#include <QFileDialog>
#include <QFileInfo>
#include <QScopedValueRollback>
#include <algorithm>
#include <cmath>
#include <limits>

namespace toon {
namespace {
bool keyExists(const Shot &shot, const TimelineKeyRef &key) {
    if (key.isSymbolPose()) {
        for (const auto &layer : shot.layers) if (layer.id == key.layerId && layer.symbol)
            return layer.symbol->playback == SymbolPlayback::Poses && key.frame >= 0 && key.frame < shot.durationFrames && symbolPoseKeyAtParent(*layer.symbol,key.frame);
        return false;
    }
    if (key.kind == TimelineKeyKind::Camera2D)
        return std::any_of(shot.camera2DKeys.cbegin(), shot.camera2DKeys.cend(), [&](const auto &value) { return value.frame == key.frame; });
    if (key.kind == TimelineKeyKind::Camera3D) {
        const auto *animation = sceneCameraAnimation(shot, key.layerId);
        return animation && std::any_of(animation->keys.cbegin(), animation->keys.cend(), [&](const auto &value) { return value.frame == key.frame; });
    }
    if (key.isPath()) {
        const auto *path = pathAnimation(shot, key.layerId, key.objectId);
        return path && std::any_of(path->keys.cbegin(), path->keys.cend(), [&](const auto &value) { return value.frame == key.frame; });
    }
    const auto *channel = propertyChannel(shot, key.layerId, key.property);
    return channel && std::any_of(channel->keys.cbegin(), channel->keys.cend(), [&](const auto &value) { return value.frame == key.frame; });
}
}
TimelineController::TimelineController(Canvas *canvas, QObject *parent)
    : QObject(parent), m_canvas(canvas) {
    setObjectName("timelineController");
    connect(canvas,&Canvas::playbackPreviewChanged,this,&TimelineController::changed);
    connect(canvas,&Canvas::playbackPreviewStatusChanged,this,&TimelineController::changed);
    m_audioImport = new AudioImport(this);
    m_audioPlayback = new AudioPlayback(this);
    m_audioScrub = new AudioScrub(this);
    connect(m_audioScrub, &AudioScrub::warning, document(), &Document::warning);
    connect(m_audioPlayback, &AudioPlayback::warning, document(), &Document::warning);
    connect(m_audioImport, &AudioImport::progress, this, [this](int percent) {
        m_audioImportStatus = percent >= 0 ? QString("Importing audio · %1%").arg(percent) : QString("Importing audio…");
        emit changed();
    });
    connect(m_audioImport, &AudioImport::busyChanged, this, [this] {
        if (!audioImportBusy()) m_audioImportStatus.clear();
        emit changed();
    });
    connect(m_audioImport, &AudioImport::failed, this, [this](const QString &reason) {
        endScrub(); cancelAudioImport(); emit document()->warning(reason);
    });
    m_audioCompletionTimer.setInterval(50);
    connect(&m_audioCompletionTimer, &QTimer::timeout, this, &TimelineController::finishPendingAudioImport);
    connect(m_audioImport, &AudioImport::completed, this, [this](AudioAssetPtr asset) {
        if (!document()->isOpen() || document()->activeShot().id != m_importShot) { cancelAudioImport(); return; }
        m_pendingAudio = std::move(asset);
        m_audioImportStatus = "Audio ready · waiting for the current interaction";
        m_audioCompletionTimer.start(); emit changed();
    });
    m_timer.setTimerType(Qt::PreciseTimer);
    m_timer.setInterval(8);
    connect(&m_timer, &QTimer::timeout, this, &TimelineController::tick);
    connect(document(), &Document::changed, this, [this] {
        // A document edit invalidates playback's time base. A brush dab should
        // not rebuild the timeline controls or interrupt the artist's input.
        endScrub();
        if (m_playing) setPlaying(false);
        if (!document()->strokeActive()) { syncWorkspaceState(); emit changed(); }
    });
    connect(document(), &Document::frameChanged, this, [this] {
        if (!m_inScrub) endScrub();
        if (m_playing && !m_inTick) setPlaying(false);
        syncWorkspaceState(); emit changed();
    });
    connect(document(), &Document::selectionChanged, this, [this] {
        // Frame navigation refreshes m_propertyLayerId before the ensuing
        // selection notification. A direct Layers choice starts a new domain.
        if (!m_symbolSelectionChanging && selectedPropertyLayerId() != m_propertyLayerId) {
            m_selectedSymbolClip.clear(); m_symbolInferenceSuppressed = false;
            m_selectedCue.clear(); m_selectedSymbolPoseLayer.clear();
        }
        if (!m_symbolSelectionChanging && m_selectedSymbolClip.isEmpty()
            && m_selectedCue.isEmpty() && m_selectedSymbolPoseLayer.isEmpty() && m_selectedCels.isEmpty() && m_selectedKeys.isEmpty() && m_selectedAudio.isEmpty() && !m_cameraSelected)
            m_symbolInferenceSuppressed = false;
        syncWorkspaceState(); updateCanvasState(); emit changed();
    });
    connect(document(), &Document::animationSelectionChanged, this, [this] {
        syncWorkspaceState(); updateCanvasState(); emit changed();
    });
    connect(document(), &Document::loaded, this, [this] {
        endScrub(); cancelAudioImport();
        m_collapsedTimelineFolders.clear();
        setPlaying(false); cancelEditing(); syncWorkspaceState(true); emit changed();
    });
    connect(document(), &Document::shotChanged, this, [this] {
        endScrub(); cancelAudioImport(); setPlaying(false); syncWorkspaceState(true); emit changed();
    });
    connect(m_canvas->drawingTools(), &DrawingToolController::changed, this, &TimelineController::changed);
    connect(m_canvas->keyframeTransformTool(), &KeyframeTransformTool::changed, this, &TimelineController::changed);
    connect(m_canvas->cameraTool(), &CameraTool::changed, this, &TimelineController::changed);
    connect(m_canvas, &Canvas::cameraViewChanged, this, &TimelineController::changed);
    connect(m_canvas, &Canvas::timelineSoloChanged, this, &TimelineController::changed);
    connect(m_canvas, &Canvas::toolChanged, this, [this] {
        endScrub();
        if (document()->cameraEditActive() || document()->keyframeEditActive() || m_pendingEdits) finishEditing();
    });
    syncWorkspaceState(true);
    // A controller attached to an already-open document inherits the Layers
    // selection. Later project/shot reloads still clear timeline selection.
    if (document()->isOpen()) m_symbolInferenceSuppressed = false;
    m_movieReview = new MovieReviewController(this);
}
TimelineController::~TimelineController() {
    // Disconnect the importer before child destruction can refresh panels.
    disconnect(m_audioImport, nullptr, this, nullptr);
    m_audioCompletionTimer.stop(); m_pendingAudio.reset();
    m_audioImport->cancel(); m_audioPlayback->stop(); m_audioScrub->end();
}
Document *TimelineController::document() const { return m_canvas->document(); }
int TimelineController::currentFrame() const { return document()->currentFrame(); }
int TimelineController::frameCount() const {
    return document()->isOpen() ? document()->activeShot().durationFrames : 0;
}
QString TimelineController::selectedTrackId() const {
    if (!m_selectedAudio.isEmpty()) return m_selectedAudio;
    if (!document()->isOpen()) return {};
    const auto trackId = document()->activeAnimationTrackId();
    if (!trackId.isEmpty()) return trackId;
    if (!m_timelineLayerSelection.isEmpty() && m_timelineLayerSelection == selectedPropertyLayerId())
        return m_timelineLayerSelection;
    const auto &layers = document()->activeShot().layers;
    int index = document()->activeLayer();
    if (index < 0 || index >= layers.size()) return {};
    for (int depth = 0; depth <= 8; ++depth) {
        if (layers[index].parentId.isEmpty()) return layers[index].id;
        int parent = -1;
        for (int i = 0; i < layers.size(); ++i)
            if (layers[i].id == layers[index].parentId) { parent = i; break; }
        if (parent < 0) return {};
        index = parent;
    }
    return {};
}
void TimelineController::finishEditing() {
    if (m_resolving) return;
    QScopedValueRollback<bool> guard(m_resolving, true);
    emit editResolutionRequested();
    m_canvas->drawingTools()->finishTextEdit();
    m_canvas->maskTool()->finish(); document()->commitEffectsEdit();
    m_canvas->keyframeTransformTool()->finish();
    document()->commitKeyframeEdit();
    m_canvas->scene3D()->endEdit();
    m_canvas->cameraTool()->finish(); document()->commitCameraEdit();
    m_pendingSources.clear(); setPendingEdits(false);
}
void TimelineController::setPendingEdits(bool pending) {
    setPendingEdits(this, pending);
}
void TimelineController::setPendingEdits(QObject *source, bool pending) {
    if(!source)source=this;
    if(pending)m_pendingSources.insert(source);else m_pendingSources.remove(source);
    const bool any=!m_pendingSources.isEmpty();
    if(m_pendingEdits==any)return;
    m_pendingEdits=any;emit changed();
}
void TimelineController::cancelEditing() {
    if (m_resolving) return;
    QScopedValueRollback<bool> guard(m_resolving, true);
    m_canvas->drawingTools()->finishTextEdit(true);
    emit editCancellationRequested(); m_canvas->maskTool()->cancel(); document()->cancelEffectsEdit(); document()->cancelKeyframeEdit();
    m_canvas->keyframeTransformTool()->cancel();
    m_canvas->cameraTool()->cancel(); m_canvas->scene3D()->cancelEdit(); document()->cancelCameraEdit();
    m_pendingSources.clear();setPendingEdits(false);
}
bool TimelineController::prepareTransition() {
    endScrub();
    setPlaying(false);
    if (m_transitionGuard && !m_transitionGuard()) return false;
    m_canvas->finishStroke();
    document()->commitLayerPropertyEdit();
    m_canvas->scene3D()->endEdit();
    m_canvas->drawingTools()->finishGesture();
    m_canvas->keyframeTransformTool()->finish();
    m_canvas->cameraTool()->finish();
    if (!m_canvas->resolvePendingTransform()) return false;
    if (document()->editingGestureActive()&&!document()->keyframeEditActive()&&!document()->cameraEditActive()&&!document()->effectsEditActive()) return false;
    // Resolve canvas decisions once before draft timing fields can call back
    // into this controller. Cancelling leaves both contexts unchanged.
    finishEditing();
    // Incomplete polygons and selection drags must not survive a frame switch.
    m_canvas->prepareTimelineTransition();
    if(m_movieReview)m_movieReview->setMode(ReviewMode::Shot);
    return true;
}
bool TimelineController::setFrame(int frame) {
    if (!document()->isOpen() || frame < 0 || frame >= frameCount()) return false;
    if (frame == currentFrame()) return true;
    return prepareTransition() && document()->setCurrentFrame(frame);
}
bool TimelineController::beginScrub() {
    if (m_scrubbing) return true;
    if (!document()->isOpen() || !prepareTransition()) return false;
    m_scrubbing = true;
    if (m_audioScrubbing) m_audioScrub->begin(document()->activeShot(), document()->project().fps);
    return true;
}
bool TimelineController::scrubToFrame(int frame) {
    if (frame < 0 || frame >= frameCount() || !beginScrub()) return false;
    QScopedValueRollback<bool> guard(m_inScrub, true);
    if (frame != currentFrame() && !document()->setCurrentFrame(frame)) { endScrub(); return false; }
    if (m_audioScrubbing) m_audioScrub->seek(frame);
    return true;
}
void TimelineController::endScrub() {
    m_scrubbing = false;
    m_audioScrub->end();
}
void TimelineController::setAudioScrubbing(bool enabled) {
    if (m_audioScrubbing == enabled) return;
    endScrub(); m_audioScrubbing = enabled;
    emit audioScrubbingChanged(enabled); emit changed();
}
bool TimelineController::selectTrack(const QString &sourceId) {
    const QString id = sourceId;
    if (!document()->isOpen()) return false;
    const auto shotId = document()->activeShotId(); const auto generation = m_timelineGeneration;
    if (!prepareTransition() || !document()->isOpen() || document()->activeShotId() != shotId
        || generation != m_timelineGeneration) return false;
    const QScopedValueRollback<bool> selectionGuard(m_symbolSelectionChanging, true);
    for (const auto &clip : document()->activeShot().audioClips) if (clip.id == id) return selectAudioClip(id);
    const bool hadAudioSelection = !m_selectedAudio.isEmpty() || m_cameraSelected;
    m_cameraSelected = false; m_canvas->cameraTool()->setEnabled(false);
    const auto &shot = document()->activeShot();
    for (const auto &track : shot.animationTracks) if (track.id == id) {
        m_timelineLayerSelection.clear();
        clearCueSelection();
        clearSymbolPoseSelection();
        clearSymbolClipSelection();
        m_selectedAudio.clear();
        const bool selected = document()->selectAnimationTrack(id);
        if (hadAudioSelection) emit changed();
        return selected;
    }
    for (int i = 0; i < shot.layers.size(); ++i) if (shot.layers[i].id == id && shot.layers[i].parentId.isEmpty()) {
        m_timelineLayerSelection.clear();
        clearCueSelection();
        clearSymbolPoseSelection();
        clearSymbolClipSelection();
        // Select a drawable child for a static folder, if it contains one.
        int target = i;
        if (shot.layers[i].group) for (int j = int(shot.layers.size()) - 1; j >= 0; --j)
            if (!shot.layers[j].group && layerIsDescendant(shot.layers, j, id)
                && animationOwner(shot,shot.layers[j].id).trackId.isEmpty()
                && layerPresentAtFrame(shot,shot.layers[j].id,currentFrame())) { target = j; break; }
        m_selectedAudio.clear(); document()->selectLayer(target);
        if (hadAudioSelection) emit changed();
        return true;
    }
    return false;
}
bool TimelineController::addTrack(bool animated, DrawingTemplate drawingTemplate, const QString &name) {
    return addTrack(animated, drawingTemplateSlots(drawingTemplate), name);
}
bool TimelineController::addTrack(bool animated, const QVector<DrawingTemplateSlot> &templateSlots, const QString &name) {
    if (!prepareTransition()) return false;
    const bool added = animated ? document()->addAnimationTrack(name, templateSlots) : document()->addStaticTrack(name);
    if (added) {
        m_selectedAudio.clear(); m_cameraSelected = false; m_cameraLayerId.clear(); m_selectedKeys.clear();
        m_selectedCels.clear(); m_selectedCue.clear(); m_selectedSymbolPoseLayer.clear();
        m_canvas->cameraTool()->setEnabled(false);
        emit changed();
    }
    return added;
}
bool TimelineController::rippleTimeline(const TimelineRippleEdit &edit) {
    if (!document()->isOpen() || !prepareTransition()) return false;
    const int oldDuration = frameCount();
    const int first = playbackFirstFrame(), last = playbackLastFrame();
    const bool ranged = m_rangeEnabled;
    bool didChange = false;
    if (!document()->rippleTimeline(edit, &didChange)) return false;
    if (!didChange) return true;
    m_selectedKeys.clear();
    // The workspace range follows its original endpoints; a moved interval may
    // reverse them, so normalize after mapping rather than changing shot data.
    if (ranged) {
        const int a = timelineRippleFrame(first, oldDuration, edit);
        const int b = timelineRippleFrame(last, oldDuration, edit);
        setPlaybackRange(std::min(a,b), std::max(a,b));
    }
    emit changed();
    return true;
}
OnionSkinSettings TimelineController::onionSkinSettings() const { return m_canvas->onionSkinSettings(); }
void TimelineController::setOnionSkinSettings(const OnionSkinSettings &settings) { m_canvas->setOnionSkinSettings(settings); }
void TimelineController::rememberOnionSkinSettings() { emit onionSkinSettingsCommitted(onionSkinSettings()); }
bool TimelineController::newDrawing(bool duplicate, int durationFrames) {
    if (!prepareTransition() || !document()->createAnimationDrawing(duplicate, durationFrames)) return false;
    for (const auto &track : document()->activeShot().animationTracks)
        if (track.id == document()->activeAnimationTrackId())
            if (const auto *cel = exposureAtFrame(track, currentFrame())) {
                setSelectedCels({{track.id, cel->id}}); return true;
            }
    clearCelSelection(); return true;
}
bool TimelineController::removeExposure() {
    if (!m_selectedCels.isEmpty()) return deleteSelectedCels();
    return prepareTransition() && document()->removeAnimationExposure();
}
bool TimelineController::removeTrack() {
    if (!document()->isOpen() || !prepareTransition()) return false;
    if (!m_selectedAudio.isEmpty()) return removeAudioClip();
    if (!document()->activeAnimationTrackId().isEmpty()) return document()->removeAnimationTrack();
    const auto id = selectedTrackId();
    if (id.isEmpty()) return false;
    document()->deleteLayers({id});
    for (const auto &layer : document()->activeShot().layers) if (layer.id == id) return false;
    return true;
}
bool TimelineController::setExposureTiming(int startFrame, int durationFrames) {
    return prepareTransition() && document()->setAnimationExposureTiming(startFrame, durationFrames);
}
bool TimelineController::setExposureTimingById(const QString &trackId, const QString &exposureId, int startFrame, int durationFrames) {
    return prepareTransition() && document()->setAnimationExposureTimingById(trackId, exposureId, startFrame, durationFrames);
}
bool TimelineController::prepareTimelineEdit() { return prepareTransition(); }
void TimelineController::syncWorkspaceState(bool reset) {
    const auto shotId = document()->isOpen() ? document()->activeShot().id : QString();
    const auto layerId = selectedPropertyLayerId();
    if (reset || shotId != m_shotId) {
        ++m_timelineGeneration;
        m_timelineLayerSelection.clear();
        m_shotId = shotId; m_rangeEnabled = false; m_rangeFirst = 0; m_rangeLast = -1;
        m_selectedKeys.clear(); m_keyClipboard = {};
        m_selectedCels.clear(); m_celClipboard = {};
        m_selectedAudio.clear(); m_cameraSelected = false; m_cameraLayerId.clear();
        m_selectedSymbolClip.clear(); m_symbolInferenceSuppressed = true;
        m_selectedCue.clear(); m_selectedSymbolPoseLayer.clear();
    }
    if (layerId != m_propertyLayerId && !m_cameraSelected && m_selectedSymbolPoseLayer.isEmpty()) m_selectedKeys.clear();
    m_propertyLayerId = layerId;
    if (frameCount() > 0) {
        m_rangeFirst = qBound(0, m_rangeFirst, frameCount() - 1);
        if (m_rangeLast >= 0) m_rangeLast = qBound(m_rangeFirst, m_rangeLast, frameCount() - 1);
        const auto &shot = document()->activeShot();
        if (!m_selectedAudio.isEmpty() && std::none_of(shot.audioClips.cbegin(), shot.audioClips.cend(), [this](const auto &clip) { return clip.id == m_selectedAudio; })) m_selectedAudio.clear();
        if (m_cameraSelected && !m_cameraLayerId.isEmpty()
            && std::none_of(shot.layers.cbegin(), shot.layers.cend(), [&](const auto &layer) { return layer.id == m_cameraLayerId && layer.scene3d; })) {
            m_cameraSelected = false; m_cameraLayerId.clear();
        }
        m_selectedKeys.removeIf([&](const auto &key) {
            const bool target = key.isSymbolPose() ? key.layerId == m_selectedSymbolPoseLayer
                : key.isCamera() ? m_cameraSelected && key.layerId == m_cameraLayerId
                : !m_cameraSelected && m_selectedSymbolPoseLayer.isEmpty() && key.layerId == layerId;
            return !target || !keyExists(shot, key);
        });
    } else m_selectedKeys.clear();
    if (!m_selectedSymbolPoseLayer.isEmpty() && (frameCount() <= 0
        || std::none_of(document()->activeShot().layers.cbegin(),document()->activeShot().layers.cend(),
            [this](const auto &layer) { return layer.id == m_selectedSymbolPoseLayer && layer.symbol && layer.symbol->playback == SymbolPlayback::Poses; }))) {
        m_selectedSymbolPoseLayer.clear();
        m_selectedKeys.removeIf([](const auto &key) { return key.isSymbolPose(); });
    }
    pruneCelSelection();
    if (!m_selectedCue.isEmpty() && (frameCount() <= 0 || !timelineCue(document()->activeShot(), m_selectedCue)))
        m_selectedCue.clear();
    if (!m_selectedSymbolClip.isEmpty() && (frameCount() <= 0
        || std::none_of(document()->activeShot().layers.cbegin(), document()->activeShot().layers.cend(),
            [this](const auto &layer) { return layer.id == m_selectedSymbolClip && layer.kind == LayerKind::Symbol && layer.symbol; })))
        m_selectedSymbolClip.clear();
    syncTimelineOrganization();
}
void TimelineController::setSelectedKeys(const QVector<TimelineKeyRef> &keys) {
    if (keys != m_selectedKeys && !m_resolving && (m_pendingEdits || document()->editingGestureActive()
        || document()->layerPropertyEditActive()) && !prepareTransition()) return;
    if (!keys.isEmpty() && m_playing) setPlaying(false);
    QVector<TimelineKeyRef> selected;
    if (document()->isOpen()) {
        const auto &shot = document()->activeShot();
        const auto layerId = selectedPropertyLayerId();
        const bool poses = !keys.isEmpty() && keys.first().isSymbolPose();
        const QString poseLayer = !m_selectedSymbolPoseLayer.isEmpty() ? m_selectedSymbolPoseLayer : layerId;
        for (const auto &key : keys) {
            const bool target = poses ? key.isSymbolPose() && key.layerId == poseLayer
                : !key.isSymbolPose() && (key.isCamera() ? m_cameraSelected && key.layerId == m_cameraLayerId : !m_cameraSelected && key.layerId == layerId);
            if (target && keyExists(shot,key) && !selected.contains(key)) selected.append(key);
        }
        const auto explicitKeys=selected;
        for(const auto &key:explicitKeys) if(!key.isCamera() && !key.isPath() && !key.isSymbolPose()
            && (key.property==KeyframeProperty::PositionX || key.property==KeyframeProperty::PositionY)) {
            const auto *animation=layerAnimation(shot,key.layerId);
            if(animation && hasSpatialMotion(*animation)) {
                auto paired=key;paired.property=key.property==KeyframeProperty::PositionX?KeyframeProperty::PositionY:KeyframeProperty::PositionX;
                if(keyExists(shot,paired) && !selected.contains(paired))selected.append(paired);
            }
        }
    }
    const bool clearedCels = !selected.isEmpty() && !m_selectedCels.isEmpty();
    if (!selected.isEmpty()) {
        if (selected.first().isSymbolPose()) {
            m_selectedSymbolPoseLayer = selected.first().layerId;
            m_selectedAudio.clear(); m_cameraSelected = false; m_cameraLayerId.clear();
            m_canvas->cameraTool()->setEnabled(false);
        } else m_selectedSymbolPoseLayer.clear();
        clearCueSelection(); clearSymbolClipSelection();
    }
    if (clearedCels) m_selectedCels.clear();
    if (selected == m_selectedKeys) { if (clearedCels) emit changed(); return; }
    m_selectedKeys = selected; emit changed();
}
void TimelineController::clearKeySelection() { setSelectedKeys({}); }
bool TimelineController::moveSelectedKeys(int deltaFrames) {
    if (!prepareTransition()) return false;
    auto keys = m_selectedKeys;
    if (!document()->moveTimelineKeys(keys, deltaFrames)) return false;
    for (auto &key : keys) key.frame += deltaFrames;
    setSelectedKeys(keys); return true;
}
bool TimelineController::deleteSelectedKeys() {
    if (!prepareTransition()) return false;
    const auto keys = m_selectedKeys;
    if (!document()->removeTimelineKeys(keys)) return false;
    clearKeySelection(); return true;
}
bool TimelineController::copySelectedKeys() {
    if (!prepareTransition()) return false;
    TimelineKeyClipboard clipboard;
    if (!document()->copyTimelineKeys(m_selectedKeys, &clipboard)) return false;
    m_keyClipboard = std::move(clipboard); emit changed(); return true;
}
bool TimelineController::canPasteKeys() const {
    if (!document()->isOpen() || m_keyClipboard.keys.isEmpty() || m_keyClipboard.shotId != document()->activeShot().id) return false;
    const auto &shot = document()->activeShot();
    const auto &layers = shot.layers;
    for (const auto &value : m_keyClipboard.keys) {
        const auto &key=value.source;
        if (key.kind == TimelineKeyKind::Camera2D) continue;
        const auto layer = std::find_if(layers.cbegin(), layers.cend(), [&](const auto &item) { return item.id == key.layerId; });
        if (layer == layers.cend()) return false;
        if (key.kind == TimelineKeyKind::Camera3D) {
            if (!sceneCameraEditError(shot,key.layerId).isEmpty()) return false;
        } else if (key.isSymbolPose()) {
            if (!layer->symbol || !value.symbolPose || layer->symbol->symbolId != value.symbolDefinitionId
                || !symbolPoseEditError(shot,key.layerId).isEmpty()) return false;
        } else {
            if (key.kind != TimelineKeyKind::Layer) return false;
            const auto reason=key.isPath()?propertyAnimationUnavailableReason(shot,key.layerId)
                :keyframePropertyUnavailableReason(shot,key.layerId,key.property);
            if (!reason.isEmpty()) return false;
            if (key.isPath() && std::none_of(layer->objects.cbegin(), layer->objects.cend(), [&](const auto &object) { return object.id == key.objectId; })) return false;
            if ((key.isPath() || keyframePropertyNeedsPositionUnlock(key.property))
                && layerPositionLocked(layers,int(layer-layers.cbegin()))) return false;
            auto ancestor=layer;QSet<QString> visited;
            while (ancestor!=layers.cend()) {
                if (ancestor->locked || visited.contains(ancestor->id)) return false;
                visited.insert(ancestor->id);
                if (ancestor->parentId.isEmpty()) break;
                const auto parentId=ancestor->parentId;
                ancestor=std::find_if(layers.cbegin(),layers.cend(),[&](const auto &item){return item.id==parentId;});
                if (ancestor==layers.cend()) return false;
            }
        }
    }
    return true;
}
bool TimelineController::pasteKeys() {
    if (!prepareTransition() || !canPasteKeys()) return false;
    const int frame = currentFrame();
    if (!document()->pasteTimelineKeys(m_keyClipboard, frame)) return false;
    int first = std::numeric_limits<int>::max();
    for (const auto &value : m_keyClipboard.keys) first = qMin(first, value.source.frame);
    QVector<TimelineKeyRef> keys;
    for (const auto &value : m_keyClipboard.keys) { auto key = value.source; key.frame += frame - first; keys.append(key); }
    if (!keys.isEmpty() && keys.first().isSymbolPose()) selectSymbolPoseLayer(keys.first().layerId);
    setSelectedKeys(keys); return true;
}
int TimelineController::playbackFirstFrame() const { return m_rangeEnabled ? m_rangeFirst : 0; }
int TimelineController::playbackLastFrame() const { return m_rangeEnabled && m_rangeLast >= 0 ? m_rangeLast : qMax(0, frameCount() - 1); }
bool TimelineController::setPlaybackRange(int firstFrame, int lastFrame) {
    if (firstFrame < 0 || lastFrame < firstFrame || lastFrame >= frameCount()) return false;
    setPlaying(false);
    if (m_rangeEnabled && m_rangeFirst == firstFrame && m_rangeLast == lastFrame) return true;
    m_rangeEnabled = true; m_rangeFirst = firstFrame; m_rangeLast = lastFrame; emit changed(); return true;
}
void TimelineController::setPlaybackRangeEnabled(bool enabled) {
    if (m_rangeEnabled == enabled || !frameCount()) return;
    setPlaying(false); m_rangeEnabled = enabled; emit changed();
}
void TimelineController::resetPlaybackRange() {
    setPlaying(false); m_rangeEnabled = false; m_rangeFirst = 0; m_rangeLast = -1; emit changed();
}
bool TimelineController::renameDrawing(const QString &name) {
    return prepareTransition() && document()->renameAnimationDrawing(name);
}
bool TimelineController::convertSelectedLayer() {
    if (!document()->isOpen() || !prepareTransition()) return false;
    const auto id = selectedTrackId();
    const int original = document()->activeLayer();
    const auto &layers = document()->activeShot().layers;
    for (int i = 0; i < layers.size(); ++i) if (layers[i].id == id) { document()->selectLayer(i); break; }
    if (document()->convertLayerToAnimation()) return true;
    document()->selectLayer(original); return false;
}
void TimelineController::stepFrame(int direction) {
    if (frameCount()) setFrame(qBound(0, currentFrame() + direction, frameCount() - 1));
}
void TimelineController::stepDrawing(int direction) {
    if (!document()->isOpen() || direction == 0) return;
    for (const auto &track : document()->activeShot().animationTracks) {
        if (track.id != document()->activeAnimationTrackId()) continue;
        int target = direction < 0 ? -1 : frameCount();
        for (const auto &exposure : track.exposures) {
            if (direction < 0 && exposure.startFrame < currentFrame()) target = qMax(target, exposure.startFrame);
            if (direction > 0 && exposure.startFrame > currentFrame()) target = qMin(target, exposure.startFrame);
        }
        if (target >= 0 && target < frameCount()) setFrame(target);
        return;
    }
}
int TimelineController::playbackPreviewDivisor() const { return m_canvas->playbackPreviewDivisor(); }
void TimelineController::setPlaybackPreviewDivisor(int divisor) { m_canvas->setPlaybackPreviewDivisor(divisor); }
void TimelineController::setPlaying(bool playing) {
    if(!playing&&m_movieReview)m_movieReview->setPlaying(false);
    endScrub();
    if (m_playing == playing) return;
    if (playing) {
        if (!document()->isOpen() || !prepareTransition()) return;
        m_canvas->keyframeTransformTool()->setEnabled(false);
        m_canvas->maskTool()->setEnabled(false);
        m_canvas->cameraTool()->setEnabled(false); m_canvas->setCameraView(true);
        document()->setKeyframeSourceEditing(false);
        if (currentFrame() < playbackFirstFrame() || currentFrame() >= playbackLastFrame()) document()->setCurrentFrame(playbackFirstFrame());
        m_playStart = currentFrame(); m_clock.start();
        m_audioPlayback->start(document()->activeShot(), document()->project().fps, m_playStart,
                               playbackFirstFrame(), playbackLastFrame(), m_looping);
        m_playing = true; m_timer.start();
    } else { m_playing = false; m_timer.stop(); m_audioPlayback->stop(); }
    updateCanvasState(); emit changed();
}
void TimelineController::setLooping(bool enabled) {
    if (m_looping == enabled) return;
    m_looping = enabled;
    if (m_playing) {
        m_playStart = currentFrame(); m_clock.restart();
        m_audioPlayback->start(document()->activeShot(), document()->project().fps, m_playStart,
                               playbackFirstFrame(), playbackLastFrame(), m_looping);
    }
    emit changed();
}
void TimelineController::setOnionSkin(bool enabled) {
    if (m_onion == enabled) return;
    m_onion = enabled; updateCanvasState(); emit changed();
}
void TimelineController::updateCanvasState() {
    m_canvas->setAnimationPlayback(m_playing);
    m_canvas->setOnionSkin(m_onion && !m_playing);
}
void TimelineController::tick() {
    if (!m_playing || !document()->isOpen() || !frameCount()) { setPlaying(false); return; }
    const double fps = document()->project().fps;
    const int first = playbackFirstFrame(), last = playbackLastFrame();
    const auto firstSample = audioSampleFrameAtTimelineFrame(first, fps);
    const auto endSample = audioSampleFrameAtTimelineFrame(qreal(last) + 1, fps);
    const auto elapsedSamples = qint64(std::llround(static_cast<long double>(m_audioPlayback->elapsedUSecs()) * audioSampleRate / 1000000));
    qint64 sample = audioSampleFrameAtTimelineFrame(m_playStart, fps) + elapsedSamples;
    if (!m_looping && sample >= endSample) {
        QScopedValueRollback<bool> guard(m_inTick, true);
        document()->setCurrentFrame(last); setPlaying(false); return;
    }
    // Use the mixer's rounded sample boundaries for every loop. Repeated
    // fractional-FPS loops would drift if wrapped separately in frame time.
    sample = firstSample + (sample - firstSample) % (endSample - firstSample);
    int frame = qBound(first, int(std::floor((sample + .5) * fps / audioSampleRate)), last);
    while (frame > first && audioSampleFrameAtTimelineFrame(frame, fps) > sample) --frame;
    while (frame < last && audioSampleFrameAtTimelineFrame(frame + 1, fps) <= sample) ++frame;
    if (frame != currentFrame()) {
        QScopedValueRollback<bool> guard(m_inTick, true);
        if (!document()->setCurrentFrame(int(frame))) setPlaying(false);
    }
}
} // namespace toon

namespace toon {
QString TimelineController::selectedPropertyLayerId() const {
    if (!document()->isOpen()) return {};
    const auto &layers = document()->activeShot().layers;
    const int index = document()->activeLayer();
    return index >= 0 && index < layers.size() ? layers[index].id : QString();
}
bool TimelineController::selectPropertyLayer(const QString &id) {
    if(!prepareTransition()||!document()->isOpen())return false;
    const QScopedValueRollback<bool> selectionGuard(m_symbolSelectionChanging, true);
    const bool hadAudioSelection = !m_selectedAudio.isEmpty() || m_cameraSelected;
    m_cameraSelected = false; m_canvas->cameraTool()->setEnabled(false);
    const auto &shot=document()->activeShot();
    for(int i=0;i<shot.layers.size();++i)if(shot.layers[i].id==id){
        if(!layerPresentAtFrame(shot,id,currentFrame())){document()->selectLayer(i);return false;}
        clearCueSelection();
        clearSymbolPoseSelection();
        clearSymbolClipSelection();
        clearCelSelection();
        m_selectedAudio.clear();document()->selectLayer(i);
        if(hadAudioSelection)emit changed();
        return true;
    }
    return false;
}
bool TimelineController::setPropertyEnabled(KeyframeProperty p,bool enabled){return prepareTransition()&&document()->enableKeyframeChannel(selectedPropertyLayerId(),p,enabled);}
bool TimelineController::beginPropertyEdit(KeyframeProperty){
    if(!prepareTransition())return false;document()->setKeyframeSourceEditing(false);
    return document()->beginKeyframeEdit(selectedPropertyLayerId());
}
bool TimelineController::beginEasingEdit(KeyframeProperty property,const QString &objectId) {
    if(!prepareTransition())return false;
    document()->setKeyframeSourceEditing(false);
    m_easingProperty=property;m_easingObjectId=objectId;m_easingFrame=currentFrame();
    return document()->beginKeyframeEdit(selectedPropertyLayerId(),"Shape keyframe easing");
}
bool TimelineController::previewEasing(const CubicEasing &easing) {
    return document()->previewKeyframeEasing(m_easingProperty,m_easingFrame,easing,m_easingObjectId);
}
bool TimelineController::resetEasing(KeyframeProperty property,const QString &objectId) {
    if(!beginEasingEdit(property,objectId))return false;
    const bool result=document()->previewKeyframeEasing(property,m_easingFrame,std::nullopt,objectId);
    if(result)finishPropertyEdit();else cancelPropertyEdit();return result;
}
bool TimelineController::setMotionPathCurved(bool curved,int frame) {
    if(frame==-1)frame=motionPathEditing()?m_canvas->keyframeTransformTool()->selectedMotionFrame():currentFrame();
    return prepareTransition()&&document()->setMotionPathCurved(selectedPropertyLayerId(),curved,frame);
}
bool TimelineController::setOrientAlongPath(bool enabled) {
    return prepareTransition()&&document()->setOrientAlongPath(selectedPropertyLayerId(),enabled);
}
bool TimelineController::previewPropertyValue(KeyframeProperty p,qreal value){return document()->previewKeyframeValue(p,value);}
void TimelineController::finishPropertyEdit(){document()->commitKeyframeEdit();}
void TimelineController::cancelPropertyEdit(){document()->cancelKeyframeEdit();}
bool TimelineController::setPropertyKey(KeyframeProperty p){
    if(!prepareTransition()||!document()->isOpen())return false;
    return document()->setKeyframeValue(selectedPropertyLayerId(),p,evaluateProperty(document()->activeShot(),selectedPropertyLayerId(),p,currentFrame()));
}
bool TimelineController::removePropertyKey(KeyframeProperty p,int frame){return prepareTransition()&&document()->removeKeyframe(selectedPropertyLayerId(),p,frame);}
bool TimelineController::movePropertyKey(KeyframeProperty p,int from,int to){return prepareTransition()&&document()->moveKeyframe(selectedPropertyLayerId(),p,from,to);}
bool TimelineController::setPropertyInterpolation(KeyframeProperty p,int frame,KeyframeInterpolation mode){return prepareTransition()&&document()->setKeyframeInterpolation(selectedPropertyLayerId(),p,frame,mode);}
bool TimelineController::setPathEnabled(const QString &id,bool enabled){return prepareTransition()&&document()->enablePathAnimation(selectedPropertyLayerId(),id,enabled);}
bool TimelineController::setPathKey(const QString &id){
    if(!prepareTransition()||!document()->isOpen())return false;
    const auto &shot=document()->activeShot();const auto &layer=shot.layers[document()->activeLayer()];
    for(const auto &object:layer.objects)if(object.id==id)return document()->setPathKeyframe(layer.id,id,evaluateAnimatedObject(shot,layer.id,object,currentFrame()));return false;
}
bool TimelineController::removePathKey(const QString &id,int frame){return prepareTransition()&&document()->removePathKeyframe(selectedPropertyLayerId(),id,frame);}
bool TimelineController::movePathKey(const QString &id,int from,int to){return prepareTransition()&&document()->movePathKeyframe(selectedPropertyLayerId(),id,from,to);}
bool TimelineController::setPathInterpolation(const QString &id,int frame,KeyframeInterpolation mode){return prepareTransition()&&document()->setPathKeyframeInterpolation(selectedPropertyLayerId(),id,frame,mode);}
bool TimelineController::sourceEditing()const{return document()->keyframeSourceEditing();}
void TimelineController::setSourceEditing(bool enabled){
    if(!prepareTransition())return;
    m_canvas->maskTool()->setEnabled(false);
    m_canvas->keyframeTransformTool()->setEnabled(false);
    if(enabled)m_canvas->cameraTool()->setEnabled(false);
    document()->setKeyframeSourceEditing(enabled);emit changed();
}
bool TimelineController::animatedTransformEditing()const{
    const auto *tool=m_canvas->keyframeTransformTool();
    return tool->enabled()&&tool->mode()==KeyframeTransformTool::Mode::Transform;
}
void TimelineController::editAnimatedTransform(){
    const bool enabled=!animatedTransformEditing();if(!prepareTransition())return;
    if(enabled){m_canvas->cameraTool()->setEnabled(false);document()->setKeyframeSourceEditing(false);}
    auto *tool=m_canvas->keyframeTransformTool();tool->setMode(KeyframeTransformTool::Mode::Transform);tool->setEnabled(enabled);emit changed();
}
bool TimelineController::motionPathEditing()const{
    const auto *tool=m_canvas->keyframeTransformTool();
    return tool->enabled()&&tool->mode()==KeyframeTransformTool::Mode::MotionPath;
}
bool TimelineController::pivotEditing()const{
    const auto *tool=m_canvas->keyframeTransformTool();
    return tool->enabled()&&tool->mode()==KeyframeTransformTool::Mode::Pivot;
}
void TimelineController::editMotionPath(){
    const bool enabled=!motionPathEditing();if(!prepareTransition())return;
    if(enabled){m_canvas->cameraTool()->setEnabled(false);document()->setKeyframeSourceEditing(false);}
    auto *tool=m_canvas->keyframeTransformTool();tool->setMode(KeyframeTransformTool::Mode::MotionPath);tool->setEnabled(enabled);emit changed();
}
void TimelineController::editAnimationPivot(){
    const bool enabled=!pivotEditing();if(!prepareTransition())return;
    if(enabled){m_canvas->cameraTool()->setEnabled(false);document()->setKeyframeSourceEditing(false);}
    auto *tool=m_canvas->keyframeTransformTool();tool->setMode(KeyframeTransformTool::Mode::Pivot);tool->setEnabled(enabled);emit changed();
}
QPointF TimelineController::animationPivot()const{
    if(!document()->isOpen())return {};
    const auto &project=document()->project();
    return effectiveAnimationPivot(project,project.shots[document()->activeShotIndex()],selectedPropertyLayerId(),document()->currentFrame());
}
bool TimelineController::beginPivotEdit(){
    if(!prepareTransition()||!document()->isOpen())return false;
    document()->setKeyframeSourceEditing(false);
    return document()->beginKeyframeEdit(selectedPropertyLayerId(),"Move animation pivot");
}
bool TimelineController::previewPivot(QPointF pivot){return document()->previewKeyframePivot(pivot);}
bool TimelineController::resetAnimationPivot(){
    if(!prepareTransition()||!document()->isOpen())return false;
    const auto &project=document()->project();const auto id=selectedPropertyLayerId();
    const auto pivot=defaultAnimationPivot(project,project.shots[document()->activeShotIndex()],id,document()->currentFrame());
    document()->setKeyframeSourceEditing(false);
    if(!document()->beginKeyframeEdit(id,"Center animation pivot"))return false;
    if(!document()->previewKeyframePivot(pivot)){document()->cancelKeyframeEdit();return false;}
    document()->commitKeyframeEdit();return true;
}
}
