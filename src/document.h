#pragma once
#include "project.h"
#include "brush_engine.h"
#include "connected_brush.h"
#include "raster_edit.h"
#include "timeline_edit.h"
#include "cel_edit.h"
#include "timeline_ripple.h"
#include <QObject>
#include <QUndoStack>
#include <QStringList>

namespace toon {
struct LineArtResult;
struct InbetweenPlan;
struct StrokeInputSample { QPointF point; BrushInput input; };
class Document : public QObject {
    Q_OBJECT
public:
    bool bakePuppetWarp();
    explicit Document(QObject *parent = nullptr);
    void load(const Project &project);
    bool isOpen() const { return m_open; }
    quint64 contentRevision() const { return m_contentRevision; }
    bool addPerspectiveGuides(const QVector<DrawingGuide> &, const QString &shotId,
                              quint64 expectedRevision, int frame, const QString &sourceId = {});
    bool createLineArtLayers(const QString &sourceId, quint64 expectedRevision, int frame,
                             const LineArtResult &, bool hideSource);
    const Project &project() const { return m_project; }
    // Saved drawing aids are independent of artwork Undo and never alter pixels.
    bool setDrawingAids(const DrawingAids &aids, QString *error = nullptr);
    bool drawingAidsDirty() const { return m_drawingAidsDirty; }
    bool loading() const { return m_loading; }
    void markDrawingAidsSaved();
    QUndoStack *history() { return &m_history; }
    int activeShotIndex() const { return m_activeShot; }
    QString activeShotId() const { return m_activeShotId; }
    const Shot &activeShot() const { return m_project.shots.at(m_activeShot); }
    void setSingleShotEditing(bool enabled) { m_singleShotEditing = enabled; }
    bool singleShotEditing() const { return m_singleShotEditing; }
    bool selectShot(const QString &id);
    QString createShot(const QString &name = {}, const QString &scene = {});
    QString duplicateShot(const QString &id);
    bool renameShot(const QString &id, const QString &number, const QString &name);
    bool setShotScene(const QString &id, const QString &scene);
    bool setShotProduction(const QString &id, const QString &episode, const QString &stage, const QString &notes);
    bool moveShot(const QString &id, int destination);
    bool deleteShot(const QString &id);
    int activeLayer() const { return m_active; }
    int currentFrame() const { return m_currentFrame; }
    bool setCurrentFrame(int frame);
    QString activeAnimationTrackId() const { return m_activeAnimationTrackId; }
    bool selectAnimationTrack(const QString &id);
    bool addAnimationTrack(const QString &name, DrawingTemplate type = DrawingTemplate::Anime);
    bool addAnimationTrack(const QString &name, const QVector<DrawingTemplateSlot> &templateSlots);
    bool rippleTimeline(const TimelineRippleEdit &edit, bool *changed = nullptr);
    bool addStaticTrack(const QString &name);
    bool createAnimationDrawing(bool duplicate = false, int durationFrames = 1);
    bool insertInbetweens(const InbetweenPlan &, const QVector<QImage> &, quint64 expectedRevision);
    bool removeAnimationExposure();
    bool moveAnimationExposures(const QVector<TimelineCelRef> &refs, int deltaFrames);
    bool removeAnimationExposures(const QVector<TimelineCelRef> &refs);
    bool setAnimationExposureHolds(const QVector<TimelineCelRef> &refs, int hold);
    bool reverseAnimationExposures(const QVector<TimelineCelRef> &refs);
    bool repeatAnimationExposures(const QVector<TimelineCelRef> &refs, int repeats = 1);
    bool pasteAnimationExposures(const TimelineCelClipboard &clipboard, int startFrame,
                                QVector<TimelineCelRef> *inserted = nullptr);
    bool exposeAnimationDrawing(const QString &trackId, const QString &drawingId, int startFrame,
                                int hold, TimelineCelRef *inserted = nullptr);
    bool deleteUnusedAnimationDrawing(const QString &trackId, const QString &drawingId);
    bool removeAnimationTrack();
    bool setAnimationExposureTiming(int startFrame, int durationFrames);
    bool setAnimationExposureTimingById(const QString &trackId, const QString &exposureId, int startFrame, int durationFrames);
    QString addAudioClip(AudioAssetPtr asset, int startFrame, const QString &name = {});
    bool removeAudioClip(const QString &id);
    bool updateAudioClip(const AudioClip &clip);
    bool crossfadeAudioClips(const QString &firstId, const QString &secondId,
                            AudioFadeCurve curve = AudioFadeCurve::EqualPower);
    bool renameAnimationDrawing(const QString &name);
    bool convertLayerToAnimation(const QString &name = {});
    bool keyframeSourceEditing() const { return m_keyframeSourceEditing; }
    void setKeyframeSourceEditing(bool enabled);
    bool enableKeyframeChannel(const QString &layerId, KeyframeProperty property, bool enabled = true);
    bool setKeyframeValue(const QString &layerId, KeyframeProperty property, qreal value);
    bool removeKeyframe(const QString &layerId, KeyframeProperty property, int frame);
    bool moveKeyframe(const QString &layerId, KeyframeProperty property, int fromFrame, int toFrame);
    bool setKeyframeInterpolation(const QString &layerId, KeyframeProperty property, int frame, KeyframeInterpolation interpolation);
    bool setKeyframePivot(const QString &layerId, QPointF pivot);
    bool enablePathAnimation(const QString &layerId, const QString &objectId, bool enabled = true);
    bool setPathKeyframe(const QString &layerId, const QString &objectId, const EditableObject &pose);
    bool removePathKeyframe(const QString &layerId, const QString &objectId, int frame);
    bool movePathKeyframe(const QString &layerId, const QString &objectId, int fromFrame, int toFrame);
    bool setPathKeyframeInterpolation(const QString &layerId, const QString &objectId, int frame, KeyframeInterpolation interpolation);
    bool beginKeyframeEdit(const QString &layerId, const QString &label = "Edit animation property");
    bool previewKeyframeValue(KeyframeProperty property, qreal value);
    bool previewKeyframePosition(QPointF position, int frame = -1);
    bool previewKeyframeEasing(KeyframeProperty property, int frame, const std::optional<CubicEasing> &easing, const QString &objectId = {});
    bool setMotionPathCurved(const QString &layerId, bool curved, int frame = -1);
    bool setOrientAlongPath(const QString &layerId, bool enabled);
    bool previewKeyframeMotionHandle(int frame, bool incoming, const QPointF &offset);
    bool previewKeyframePivot(QPointF pivot);
    void commitKeyframeEdit();
    void cancelKeyframeEdit();
    bool keyframeEditActive() const { return m_beforeKeyframeEdit.has_value(); }
    bool setShotCamera(const Camera2D &camera);
    bool enableShotCameraAnimation(bool enabled = true);
    bool addShotCameraKeyframe();
    bool removeShotCameraKeyframe(int frame);
    bool moveShotCameraKeyframe(int fromFrame, int toFrame);
    bool setShotCameraKeyframeInterpolation(int frame, KeyframeInterpolation interpolation);
    bool setSceneCamera(const QString &layerId, const Camera3D &camera);
    bool enableSceneCameraAnimation(const QString &layerId, bool enabled = true);
    bool addSceneCameraKeyframe(const QString &layerId);
    bool removeSceneCameraKeyframe(const QString &layerId, int frame);
    bool moveSceneCameraKeyframe(const QString &layerId, int fromFrame, int toFrame);
    bool setSceneCameraKeyframeInterpolation(const QString &layerId, int frame, KeyframeInterpolation interpolation);
    bool beginCameraEdit(const QString &sceneLayerId = {}, const QString &label = "Adjust camera");
    bool previewShotCamera(const Camera2D &camera);
    bool previewSceneCamera(const Camera3D &camera);
    void commitCameraEdit();
    void cancelCameraEdit();
    bool cameraEditActive() const { return m_beforeCameraEdit.has_value(); }
    QString effectsEditReason(const QString &layerId = {}) const;
    bool beginEffectsEdit(const QString &layerId = {}, const QString &label = "Edit layer effects", bool keyframeProperties = false);
    bool previewLayerEffects(const LayerEffects &effects);
    bool previewLayerMask(const LayerMask &mask);
    void commitEffectsEdit();
    void cancelEffectsEdit();
    bool effectsEditActive() const { return m_beforeEffectsEdit.has_value(); }
    bool setLayerEffects(const QString &layerId, const LayerEffects &effects, bool keyframeProperties = false);
    bool addAdjustmentLayer();
    bool addLayerMask(bool fromSelection = false);
    bool removeLayerMask();
    bool setLayerMaskEnabled(bool enabled, bool keyframeProperties = false);
    bool invertLayerMask(bool keyframeProperties = false);
    bool fillLayerMask(bool reveal);
    bool moveTimelineKeys(const QVector<TimelineKeyRef> &keys, int deltaFrames);
    bool removeTimelineKeys(const QVector<TimelineKeyRef> &keys);
    bool copyTimelineKeys(const QVector<TimelineKeyRef> &keys, TimelineKeyClipboard *out);
    bool pasteTimelineKeys(const TimelineKeyClipboard &clipboard, int atFrame);
    void selectLayer(int index);
    void addLayer(bool annotation = false);
    QString addReference3DLayer(const ReferenceScene &scene, const QString &name = {}, QString *error = nullptr);
    bool setReference3DScene(const QString &layerId, const ReferenceScene &scene, QString *error = nullptr);
    bool beginReference3DEdit(const QString &layerId, QString *error = nullptr);
    bool previewReference3DScene(const ReferenceScene &scene, QString *error = nullptr);
    void commitReference3DEdit();
    void cancelReference3DEdit();
    bool reference3DEditActive() const { return m_beforeReference3DEdit.has_value(); }
    void addGroup();
    void setLayerParent(const QString &parentId);
    void setLayerClipped(bool clipped);
    void setLayerAlphaLocked(bool locked);
    void deleteLayer();
    void moveLayer(int direction);
    void changeLayer(const QString &name, bool visible, bool locked, bool annotation, qreal opacity);
    void deleteLayers(const QStringList &ids);
    void duplicateLayers(const QStringList &ids);
    void groupLayers(const QStringList &ids);
    // Sibling storage is bottom-to-top. Insert immediately before beforeId;
    // an empty beforeId inserts at the top. Descendants move with their group.
    void moveLayers(const QStringList &ids, const QString &parentId, const QString &beforeId = {});
    void setLayersVisible(const QStringList &ids, bool visible);
    void setLayersLocked(const QStringList &ids, bool locked);
    bool setFillReferences(const QStringList &ids, bool reference);
    void setLayersRowColor(const QStringList &ids, const QColor &color);
    void setLayerBlendMode(LayerBlendMode mode);
    void setLayerPositionLocked(bool locked);
    bool mergeDown();
    bool flattenImage();
    // Preview changes redraw the canvas without adding history entries or
    // changing layer selection. Commit records the entire gesture once.
    bool beginLayerPropertyEdit(const QString &label = "Layer opacity");
    void previewLayerOpacity(qreal opacity);
    void previewLayersVisible(const QStringList &ids, bool visible);
    void commitLayerPropertyEdit();
    void cancelLayerPropertyEdit();
    bool layerPropertyEditActive() const { return m_beforeLayerPropertyEdit.has_value(); }
    bool addReference(const QString &name, const QImage &image, QString *error = nullptr);
    void removeReference(const QString &id);
    QString addCharacter(const QString &name, QString *error = nullptr);
    bool renameCharacter(const QString &id, const QString &name, QString *error = nullptr);
    bool removeCharacter(const QString &id, QString *error = nullptr);
    bool addCharacterSheets(const QString &characterId, const QVector<CharacterModelSheet> &sheets, QString *error = nullptr);
    bool renameCharacterSheet(const QString &characterId, const QString &sheetId, const QString &name, QString *error = nullptr);
    bool setCharacterSheetCategory(const QString &characterId, const QString &sheetId, ModelSheetCategory category, QString *error = nullptr);
    bool removeCharacterSheet(const QString &characterId, const QString &sheetId, QString *error = nullptr);
    QString addCharacterColor(const QString &characterId, const QString &name, CharacterColorCategory category, const QColor &color, QString *error = nullptr);
    bool updateCharacterColor(const QString &characterId, const QString &colorId, const QString &name, CharacterColorCategory category, const QColor &color, QString *error = nullptr, const QString &variantId = {});
    bool removeCharacterColor(const QString &characterId, const QString &colorId, QString *error = nullptr);
    QString addCharacterColorVariant(const QString &characterId, const QString &name, const QString &sourceVariantId = {}, QString *error = nullptr);
    bool renameCharacterColorVariant(const QString &characterId, const QString &variantId, const QString &name, QString *error = nullptr);
    bool removeCharacterColorVariant(const QString &characterId, const QString &variantId, QString *error = nullptr);
    bool addStandardCharacterColorVariants(const QString &characterId, QString *error = nullptr);
    void replaceProject(const Project &project, const QString &label, const QJsonObject &action = {});
    bool insertMotionStudyProject(const Project &project, const QString &label);
    bool replaceSymbolProject(const Project &project, const QString &label);
    const RasterSelection &rasterSelection() const { return m_selection; }
    void setRasterSelection(const QPainterPath &path);
    void combineRasterSelection(const QPainterPath &path, SelectionCombine mode);
    void modifyRasterSelection(qreal signedPixels);
    bool fillRasterSelection(const QColor &color);
    bool clearSelectedPixels();
    void selectAllPixels();
    void invertRasterSelection();
    void clearRasterSelection();
    QRectF drawingBounds() const;
    // Place one frozen brush impression, independently of the existing pixels.
    // Uses the ordinary transform preview/Apply/Cancel interface and one Undo.
    bool beginStampPlacement(QPointF point, BrushInput input, BrushSettings settings);
    bool stampPlacementActive() const { return m_stampPlacement.has_value(); }
    bool beginTransform();
    bool previewTransform(const TransformParameters &parameters);
    bool applyTransform();
    void cancelTransform();
    bool transformActive() const { return m_transformPreview.has_value(); }
    bool editingGestureActive() const { return strokeActive() || transformActive() || m_externalEditActive || keyframeEditActive() || cameraEditActive() || effectsEditActive() || reference3DEditActive(); }
    bool externalEditActive() const { return m_externalEditActive; }
    QRectF transformBounds() const { return m_transformBounds; }
    QTransform transformMatrix() const { return rasterTransform(m_transformBounds, m_transformParameters); }
    TransformParameters transformParameters() const { return m_transformParameters; }
    const RasterLayerPreview *transformPreview() const { return m_transformPreview ? &*m_transformPreview : nullptr; }
    // Explicit full-resolution snapshot. Interactive views use transformPreview()
    // instead, so pointer movement never resamples document pixels.
    const Project &displayProject() const;
    bool beginStroke(QPointF point, qreal pressure, BrushSettings settings, bool eraser);
    bool beginStroke(QPointF point, BrushInput input, BrushSettings settings, bool eraser);
    void continueStroke(QPointF point, qreal pressure);
    void continueStroke(QPointF point, BrushInput input);
    // Replace the live gesture with a line from its original contact point.
    // Keeps its seed, brush, selection and single undo snapshot.
    void previewStrokeLine(QPointF endpoint, BrushInput input);
    void preserveStrokeShapeOriginal();
    bool restoreStrokeShapeOriginal();
    bool previewStrokePath(const QPainterPath &path, const QVector<StrokeInputSample> &profile);
    void airbrushTick();
    void setBrushRandomSeed(quint32 seed) { m_randomSeed = seed; }
    void endStroke();
    void cancelStroke();
    bool strokeActive() const { return m_beforeStroke.has_value(); }
    bool committing() const { return m_committing; }
    quint64 brushDabIndex() const { return m_dabIndex ? m_dabIndex - 1 : 0; }
    quint64 brushCursorIndex() const;
    quint32 brushStrokeSeed() const { return m_strokeSeed; }
    qreal brushDirection() const { return m_direction; }
    qreal brushInitialDirection() const { return m_initialDirection; }
signals:
    // Semantic recording observes completed commits only, never live pen samples.
    void historyNavigationStarting();
    void operationCommitted(const Project &before, const Project &after, const QString &label,
                            const QString &beforeLayer, const QString &afterLayer, const QJsonObject &action);
    void effectsEditResolutionRequested();
    void loaded();
    void drawingAidsChanged();
    void drawingAidsCleanChanged(bool clean);
    void shotChanged();
    // Document/UI refreshes belong to completed brush gestures. Only the canvas
    // consumes live samples, without rebuilding panels or preview thumbnails.
    void changed();
    void strokeUpdated();
    void frameChanged();
    void animationSelectionChanged();
    void selectionChanged();
    void rasterSelectionChanged();
    // Flush a view's coalesced transform pointer update before synchronous Apply.
    void transformApplyRequested();
    void transformChanged();
    void warning(const QString &message);
private:
    quint64 m_contentRevision = 0;
    friend class ProjectCommand;
    friend class AutoActions;
    friend class RasterTools;
    friend class ObjectTools;
    friend class AdvancedTransform;
    friend class ClipboardTools;
    friend class Scene3DController;
    void apply(const Project &project, const QString &selectedId = {}, const QString &shotId = {}, bool preserveDrawingAids = true);
    DrawingAids m_savedDrawingAids;
    bool m_drawingAidsDirty = false;
    bool m_loading = false;
    void rememberShotView();
    bool shotOperationReady();
    bool characterOperationReady(QString *error) const;
    bool commitCharacterChange(const Project &project, const QString &label, QString *error);
    bool commitCelChange(Project project, const QString &label, const QString &selectedId = {});
    bool commit(const Project &before, const Project &after, const QString &label, const QString &selectedId = {},
                std::optional<RasterSelection> beforeSelection = {}, std::optional<RasterSelection> afterSelection = {},
                const QString &beforeShotId = {}, const QString &afterShotId = {}, const QJsonObject &action = {});
    bool dab(QPointF point, BrushInput input);
    bool rasterizeTransform(QString *error) const;
    bool renderStampPlacement(const QTransform &transform, bool clipped, RasterLayer *layer,
                              EditableObject *object, QString *error) const;
    bool previewStampPlacement(const TransformParameters &parameters, const QTransform &transform);
    bool rasterizeStampPlacement(QString *error) const;
    bool applyStampPlacement();
    void remapAnimationSelection(const QString &preferredLayerId = {}, const QString &templateSlotId = {});
    Project m_project;
    bool m_open = false;
    bool m_committing = false;
    bool m_externalEditActive = false;
    std::optional<Project> m_beforeReference3DEdit;
    QString m_reference3DEditLayerId, m_reference3DEditShotId;
    int m_reference3DEditFrame = -1;
    struct ShotView { QString layerId, animationTrackId; int frame = 0; };
    QHash<QString, ShotView> m_shotViews;
    QString m_activeShotId;
    int m_activeShot = 0;
    int m_active = 0;
    int m_currentFrame = 0;
    QString m_activeAnimationTrackId;
    bool m_animationMutation = false;
    bool m_keyframeSourceEditing = false;
    bool m_singleShotEditing = false;
    QUndoStack m_history;
    qint64 m_historyBytes = 0;
    std::optional<Project> m_beforeKeyframeEdit;
    QString m_keyframeEditLayerId, m_keyframeEditLabel;
    std::optional<Project> m_beforeCameraEdit;
    QString m_cameraEditLayerId, m_cameraEditLabel;
    std::optional<Project> m_beforeEffectsEdit;
    QString m_effectsLayerId, m_effectsLabel, m_effectsShotId;
    int m_effectsFrame = -1;
    bool m_effectsKeyframeProperties = false;
    std::optional<Project> m_beforeStroke;
    struct StrokeShapeOriginal {
        QMap<TileKey, QImage> tiles;
        RasterLayer paint;
        std::optional<EditableObject> vector;
        QPointF lastPoint;
        BrushInput lastInput;
        qreal distanceToDab = 0, direction = 0, initialDirection = 0;
        quint64 dabIndex = 0;
        bool hasDirection = false;
        ConnectedBrushState connected;
    };
    std::optional<StrokeShapeOriginal> m_strokeShapeOriginal;
    StrokeShapeOriginal currentStrokeShapeState() const;
    void applyStrokeShapeState(StrokeShapeOriginal state);
    bool resetStrokePaint(QPointF point, BrushInput resolvedInput);
    bool m_previewingStrokeShape = false, m_strokeShapeFailed = false;
    std::optional<Project> m_beforeLayerPropertyEdit;
    QString m_layerPropertyLabel;
    RasterSelection m_selection;
    RasterSelectionCache m_strokeSelectionCache;
    struct StampPlacement {
        EditableObject source;
        RasterSelection selection;
        bool vectorLayer = false;
        int otherTiles = 0;
    };
    std::optional<StampPlacement> m_stampPlacement;
    std::optional<RasterLayerPreview> m_transformPreview;
    mutable std::optional<Project> m_transformRasterized;
    QVector<QRect> m_transformRows;
    QRectF m_transformBounds;
    TransformParameters m_transformParameters;
    BrushSettings m_brush;
    RasterLayer m_strokePaint;
    ConnectedBrushState m_connectedStroke;
    int m_strokeOtherTiles = 0;
    QRectF m_strokeDrawingBounds;
    std::optional<EditableObject> m_vectorStroke;
    quint32 m_randomSeed = 1;
    quint32 m_strokeSeed = 1;
    quint64 m_dabIndex = 0;
    qreal m_direction = 0, m_initialDirection = 0;
    bool m_hasDirection = false;
    BrushInput m_initialInput;
    bool m_eraser = false;
    QPointF m_lastPoint;
    QPointF m_strokeOrigin;
    BrushInput m_lastInput;
    qreal m_distanceToDab = 0;
};
} // namespace toon
