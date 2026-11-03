#pragma once

#include "animation.h"
#include "property_animation.h"
#include "timeline_edit.h"
#include "cel_edit.h"
#include "timeline_ripple.h"
#include "onion_settings.h"
#include "audio.h"
#include "camera_animation.h"
#include "symbol_clips.h"
#include "timeline_cues.h"
#include "symbol_poses.h"
#include <QObject>
#include <QElapsedTimer>
#include <QTimer>
#include <functional>

namespace toon {
class Canvas;
class Document;
class AudioImport;
class AudioPlayback;
class AudioScrub;
class MovieReviewController;

// Workspace time is separate from the saved drawings and exposures. All UI
// transitions pass through here to resolve pending drawing/transform gestures.
class TimelineController : public QObject {
    Q_OBJECT
public:
    explicit TimelineController(Canvas *canvas, QObject *parent = nullptr);
    ~TimelineController() override;
    Canvas *canvas() const { return m_canvas; }
    Document *document() const;
    MovieReviewController *movieReview() const { return m_movieReview; }
    int currentFrame() const;
    int frameCount() const;
    QString selectedTrackId() const;
    bool playing() const { return m_playing; }
    int playbackPreviewDivisor() const;
    void setPlaybackPreviewDivisor(int divisor);
    bool looping() const { return m_looping; }
    bool onionSkin() const { return m_onion; }
    bool setFrame(int frame);
    bool beginScrub();
    bool scrubToFrame(int frame);
    void endScrub();
    bool audioScrubbing() const { return m_audioScrubbing; }
    void setAudioScrubbing(bool enabled);
    bool selectTrack(const QString &id);
    bool selectTimelineLayer(QString layerId);
    bool setTimelineLayerVisible(QString layerId, bool visible);
    bool setTimelineLayerLocked(QString layerId, bool locked);
    QSet<QString> timelineSoloLayers() const;
    bool toggleTimelineLayerSolo(QString layerId);
    bool clearTimelineSolo();
    QString timelineLayerMoveError(QString layerId, QString parentId, QString beforeId) const;
    bool moveTimelineLayer(QString layerId, QString parentId, QString beforeId);
    bool addTimelineFolder(QString parentId, QString name);
    bool timelineFolderCollapsed(const QString &layerId) const;
    bool setTimelineFolderCollapsed(QString layerId, bool collapsed);
    bool addTrack(bool animated, DrawingTemplate drawingTemplate, const QString &name);
    bool addTrack(bool animated, const QVector<DrawingTemplateSlot> &templateSlots, const QString &name);
    bool rippleTimeline(const TimelineRippleEdit &edit);
    OnionSkinSettings onionSkinSettings() const;
    void setOnionSkinSettings(const OnionSkinSettings &settings);
    void rememberOnionSkinSettings();
    bool newDrawing(bool duplicate = false, int durationFrames = 1);
    bool removeExposure();
    bool removeTrack();
    bool setExposureTiming(int startFrame, int durationFrames);
    bool setExposureTimingById(const QString &trackId, const QString &exposureId, int startFrame, int durationFrames);
    QVector<TimelineCelRef> selectedCels() const { return m_selectedCels; }
    void setSelectedCels(const QVector<TimelineCelRef> &cels);
    void clearCelSelection();
    bool moveSelectedCels(int deltaFrames);
    bool deleteSelectedCels();
    bool copySelectedCels();
    bool pasteCels();
    bool canPasteCels() const;
    bool setSelectedCelHolds(int durationFrames);
    bool reverseSelectedCels();
    bool repeatSelectedCels();
    bool exposeExistingDrawing(const QString &drawingId, int durationFrames = 1);
    bool removeUnusedDrawing(const QString &drawingId);
    QVector<TimelineKeyRef> selectedKeys() const { return m_selectedKeys; }
    void setSelectedKeys(const QVector<TimelineKeyRef> &keys);
    void clearKeySelection();
    bool moveSelectedKeys(int deltaFrames);
    bool deleteSelectedKeys();
    bool copySelectedKeys();
    bool pasteKeys();
    bool canPasteKeys() const;
    bool playbackRangeEnabled() const { return m_rangeEnabled; }
    int playbackFirstFrame() const;
    int playbackLastFrame() const;
    bool setPlaybackRange(int firstFrame, int lastFrame);
    void setPlaybackRangeEnabled(bool enabled);
    void resetPlaybackRange();
    bool prepareTimelineEdit();
    bool renameDrawing(const QString &name);
    QString selectedAudioClipId() const { return m_selectedAudio; }
    bool selectAudioClip(const QString &id);
    void importAudio();
    bool importAudioFile(const QString &path);
    bool audioImportBusy() const;
    QString audioImportStatus() const { return m_audioImportStatus; }
    void cancelAudioImport();
    bool updateAudioClip(const AudioClip &clip);
    bool removeAudioClip();
    bool crossfadeAudioClips(const QString &outgoing, const QString &incoming, AudioFadeCurve curve);
    bool convertSelectedLayer();
    QString selectedPropertyLayerId() const;
    bool selectPropertyLayer(const QString &id);
    QString selectedSymbolClipId() const;
    bool selectSymbolClip(const QString &id);
    void clearSymbolClipSelection();
    bool editSelectedSymbolClip(SymbolClipOperation operation, int value);
    bool setSelectedSymbolClipTiming(int start, int duration, int offset);
    bool setSelectedSymbolPlayback(const SymbolInstance &instance);
    QString selectedSymbolPoseLayerId() const;
    bool selectSymbolPoseLayer(const QString &id);
    void clearSymbolPoseSelection();
    bool setSelectedSymbolPose(int parentFrame, int poseFrame);
    bool removeSelectedSymbolPose(int parentFrame);
    bool moveSelectedSymbolPose(int fromParent, int toParent);
    bool stepSymbolPose(int direction);
    QString selectedCueId() const;
    bool selectCue(QString id, bool seek = true);
    void clearCueSelection();
    QString addCue(TimelineCueKind kind);
    bool updateSelectedCue(const TimelineCue &cue);
    bool setSelectedCueTiming(int start, int duration);
    bool removeSelectedCue();
    bool stepCue(int direction);
    bool setPropertyEnabled(KeyframeProperty property, bool enabled);
    bool beginPropertyEdit(KeyframeProperty property);
    bool previewPropertyValue(KeyframeProperty property, qreal value);
    bool beginEasingEdit(KeyframeProperty property, const QString &objectId = {});
    bool previewEasing(const CubicEasing &easing);
    bool resetEasing(KeyframeProperty property, const QString &objectId = {});
    bool setMotionPathCurved(bool curved, int frame = -1);
    bool setOrientAlongPath(bool enabled);
    void finishPropertyEdit();
    void cancelPropertyEdit();
    bool setPropertyKey(KeyframeProperty property);
    bool removePropertyKey(KeyframeProperty property, int frame);
    bool movePropertyKey(KeyframeProperty property, int from, int to);
    bool setPropertyInterpolation(KeyframeProperty property, int frame, KeyframeInterpolation interpolation);
    bool setPathEnabled(const QString &objectId, bool enabled);
    bool setPathKey(const QString &objectId);
    bool removePathKey(const QString &objectId, int frame);
    bool movePathKey(const QString &objectId, int from, int to);
    bool setPathInterpolation(const QString &objectId, int frame, KeyframeInterpolation interpolation);
    bool sourceEditing() const;
    void setSourceEditing(bool enabled);
    bool animatedTransformEditing() const;
    void editAnimatedTransform();
    bool motionPathEditing() const;
    bool pivotEditing() const;
    void editMotionPath();
    void editAnimationPivot();
    QPointF animationPivot() const;
    bool beginPivotEdit();
    bool previewPivot(QPointF pivot);
    bool resetAnimationPivot();
    bool cameraSelected() const { return m_cameraSelected; }
    QString selectedCameraLayerId() const { return m_cameraLayerId; }
    bool selectCamera(const QString &layerId = {});
    bool setCameraEnabled(bool enabled);
    bool beginCameraEdit();
    bool previewShotCamera(const Camera2D &camera);
    bool previewSceneCamera(const Camera3D &camera);
    void finishCameraEdit();
    void cancelCameraEdit();
    bool setCameraKey();
    bool removeCameraKey(int frame);
    bool moveCameraKey(int from, int to);
    bool setCameraInterpolation(int frame, KeyframeInterpolation interpolation);
    bool cameraFramingEditing() const;
    void editCameraFraming();
    bool cameraView() const;
    void setCameraView(bool enabled);
    bool hasPendingEdits() const { return m_pendingEdits; }
    // Workspace previews can veto navigation when their Apply/Discard dialog is cancelled.
    void setTransitionGuard(std::function<bool()> guard) { m_transitionGuard = std::move(guard); }
    void setPendingEdits(bool pending);
    void setPendingEdits(QObject *source, bool pending);
    void cancelEditing();
    // Flush local timing fields before saving, export or leaving a shot.
    void finishEditing();
public slots:
    void stepFrame(int direction);
    void stepDrawing(int direction);
    void setPlaying(bool playing);
    void setLooping(bool enabled);
    void setOnionSkin(bool enabled);
signals:
    void drawingPropertiesRequested();
    void animationPropertiesRequested();
    void audioPropertiesRequested();
    void cameraPropertiesRequested();
    void symbolPropertiesRequested();
    void cuePropertiesRequested();
    void propertyChannelRequested(KeyframeProperty property);
    void pathChannelRequested(const QString &objectId);
    void changed();
    void audioScrubbingChanged(bool enabled);
    void onionSkinSettingsCommitted(const OnionSkinSettings &settings);
    void editResolutionRequested();
    void editCancellationRequested();
private:
    bool prepareTransition();
    void tick();
    void finishPendingAudioImport();
    void updateCanvasState();
    void syncWorkspaceState(bool reset = false);
    void syncTimelineOrganization();
    void pruneCelSelection();
    Canvas *m_canvas;
    MovieReviewController *m_movieReview = nullptr;
    QTimer m_timer;
    QElapsedTimer m_clock;
    int m_playStart = 0;
    bool m_playing = false, m_looping = true, m_onion = false;
    bool m_resolving = false, m_inTick = false, m_pendingEdits = false;
    QSet<QObject *> m_pendingSources;
    std::function<bool()> m_transitionGuard;
    QVector<TimelineKeyRef> m_selectedKeys;
    TimelineKeyClipboard m_keyClipboard;
    QVector<TimelineCelRef> m_selectedCels;
    TimelineCelClipboard m_celClipboard;
    QString m_shotId, m_propertyLayerId;
    QString m_timelineLayerSelection;
    QHash<QString, QSet<QString>> m_collapsedTimelineFolders;
    quint64 m_timelineGeneration = 0;
    QString m_selectedSymbolClip;
    QString m_selectedSymbolPoseLayer;
    QString m_selectedCue;
    bool m_symbolSelectionChanging = false, m_symbolInferenceSuppressed = false;
    KeyframeProperty m_easingProperty = KeyframeProperty::PositionX;
    QString m_easingObjectId;
    int m_easingFrame = 0;
    bool m_cameraSelected = false;
    QString m_cameraLayerId;
    QString m_selectedAudio, m_audioImportStatus, m_importShot;
    int m_importFrame = 0;
    AudioImport *m_audioImport;
    AudioPlayback *m_audioPlayback;
    AudioScrub *m_audioScrub;
    bool m_audioScrubbing = false, m_scrubbing = false, m_inScrub = false;
    QTimer m_audioCompletionTimer;
    AudioAssetPtr m_pendingAudio;
    bool m_rangeEnabled = false;
    int m_rangeFirst = 0, m_rangeLast = -1;
};
} // namespace toon
