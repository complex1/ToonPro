#pragma once
#include "sheet.h"
#include "document.h"
#include "stroke_filter.h"
#include "advanced_brush_dynamics_settings.h"
#include "drawing_guide.h"
#include "onion_settings.h"
#include <QWidget>
#include <QPainterPath>
#include <QRegion>
#include <QSet>
#include <QTimer>
#include <functional>

namespace toon {
class CanvasSelectionOverlay;
class GuideTool;
class SelectionContextBar;
class DrawingToolController;
class Scene3DController;
class KeyframeTransformTool;
class CameraTool;
class MaskTool;
class HoldShapeTool;
class EffectsPreviewRenderer;
struct EffectsPreviewSnapshot;
class Canvas : public QWidget {
    Q_OBJECT
public:
    explicit Canvas(QWidget *parent = nullptr);
    ~Canvas() override;
    void setProject(const Project &project, int shot = 0);
    enum class Tool { Brush, Eraser, Hand, RectangleSelect, LassoSelect, Transform, Guides,
        Eyedropper, Bucket, Gradient, Line, Rectangle, Ellipse, Polygon, Curve,
        EllipseSelect, PolygonSelect, MagicWand, ColorRange, Retouch, Text,
        VectorPen, VectorNode, VectorEraser, AdvancedTransform, VectorSimplify, VectorWidth, SceneObject, SceneCamera, SceneRig, Arc };
    DrawingToolController *drawingTools() const { return m_drawingTools; }
    Scene3DController *scene3D() const { return m_scene3D; }
    KeyframeTransformTool *keyframeTransformTool() const { return m_keyframeTransformTool; }
    CameraTool *cameraTool() const { return m_cameraTool; }
    MaskTool *maskTool() const { return m_maskTool; }
    bool cameraView() const { return m_cameraView; }
    void setCameraView(bool enabled);
    QTransform cameraTransform() const;
    qreal artworkZoom() const;
    qreal artworkRotation() const;
    Shot displayShot() const;
    QSet<QString> timelineSoloLayers() const { return m_timelineSoloLayers; }
    bool setTimelineSoloLayers(const QSet<QString> &layerIds);
    bool timelineSoloIncludesLayer(const QString &layerId) const;
    Document *document() const { return m_document; }
    bool hasProject() const { return m_document->isOpen(); }
    const Project &project() const { return m_document->project(); }
    const Shot &shot() const { return m_document->activeShot(); }
    BrushSettings brush;
    void setBrushSize(qreal size);
    void setBrushFlow(qreal flow);
    void setBrushSpacing(qreal spacing);
    void setBrushColor(const QColor &color);
    void setBrushSettings(const BrushSettings &settings);
    void applyBrushSettings(const BrushSettings &settings, qreal smoothing);
    void setPressureSize(bool enabled);
    void setStabilization(qreal amount);
    qreal stabilization() const { return m_stabilization; }
    void finishStroke();
    bool holdShapeEnabled() const { return m_holdShapeEnabled; }
    void setHoldShapeEnabled(bool enabled);
    bool holdShapePending() const;
    bool stampPlacementEnabled() const { return m_stampPlacementEnabled; }
    bool stampPlacementAvailable() const;
    void setStampPlacementEnabled(bool enabled);
    QRectF brushCursorRect() const;
    QPainterPath brushCursorPath() const;
    Tool tool() const { return m_tool; }
    Tool pointerTool() const { return m_tool == Tool::Brush && m_pointerEraser ? Tool::Eraser : m_tool; }
    void setTool(Tool tool);
    // Resolve a pending pixel/layer/selection transform before changing context.
    // False keeps the current tool and preview intact. Identity previews close silently.
    bool resolvePendingTransform();
    void setToolRouter(std::function<bool(Tool)> router) { m_toolRouter = std::move(router); }
    bool toolAvailable() const { return m_toolAvailable; }
    // Context restrictions are distinct from a missing tool-catalog placement.
    QString toolBlockReason() const;
    QString interactionHint() const;
    void setToolAvailable(bool available);
    void notifyToolStateApplied() { emit toolStateApplied(); }
    void notifyToolSettingsEdited() { emit toolSettingsEdited(); }
    GuideType guideCreationType() const;
    void setGuideCreationType(GuideType type);
    void beginGuideCreation(GuideType type);
    void cancelGuideCreation();
    bool guideCreating() const;
    QString selectedGuideId() const;
    void selectGuide(const QString &id);
    QVector<DrawingGuide> drawingGuides() const;
    GuideSettings guideSettings() const;
    void addDefaultGuide(GuideType type);
    void updateGuide(const DrawingGuide &guide);
    void removeSelectedGuide();
    void duplicateSelectedGuide();
    void setGuideSettings(const GuideSettings &settings);
    void replaceGuideSet(const QVector<DrawingGuide> &guides, const GuideSettings &settings, bool append = false);
    SelectionCombine selectionCombine() const { return m_selectionCombine; }
    void setSelectionCombine(SelectionCombine mode);
    QRectF selectionViewportBounds() const;
    bool selectionGestureActive() const;
    bool selectionFocusOverlay() const { return m_selectionFocusOverlay; }
    void setSelectionFocusOverlay(bool enabled);
    qreal selectionShadeOpacity() const { return m_selectionShadeOpacity; }
    void setSelectionShadeOpacity(qreal value);
    bool selectionAnimated() const { return m_selectionAnimated; }
    void setSelectionAnimated(bool value);
    bool selectionBarVisible() const { return m_selectionBarVisible; }
    void setSelectionBarVisible(bool visible);
    // Rendering diagnostic: overlay/cursor animation must reuse the viewport cache.
    quint64 artworkRenderCount() const { return m_artworkRenderCount; }
    quint64 artworkPaintedPixelCount() const { return m_artworkPaintedPixelCount; }
    quint64 strokeTileScanCount() const { return m_strokeTileScanCount; }
    quint64 brushOutlineBuildCount() const { return m_brushOutlineBuildCount; }
    quint64 brushCursorRasterBuildCount() const { return m_brushCursorRasterBuildCount; }
    qint64 brushCursorRasterBytes() const { return m_brushCursorRaster.sizeInBytes(); }
    bool effectsPreviewPending() const { return m_effectsPreviewPending; }
    quint64 guideCacheRenderCount() const;
    QPointF artworkPoint(QPointF position) const;
    QRectF paperRect() const;
    QTransform viewTransform() const;
    QPolygonF visiblePolygon() const;
    qreal zoom() const { return m_zoom; }
    qreal rotation() const { return m_rotation; }
    QPointF center() const { return m_center; }
    bool canvasSmoothing() const { return m_canvasSmoothing; }
    bool animationPlayback() const { return m_animationPlayback; }
    int playbackPreviewDivisor() const { return m_playbackPreviewDivisor; }
    void setPlaybackPreviewDivisor(int divisor);
    QString playbackPreviewFallbackReason() const { return m_playbackPreviewFallbackReason; }
    bool onionSkin() const { return m_onionSkin; }
    const OnionSkinSettings &onionSkinSettings() const { return m_onionSettings; }
    void setOnionSkinSettings(const OnionSkinSettings &settings);
    void setAnimationPlayback(bool playing);
    void setOnionSkin(bool enabled);
    void prepareTimelineTransition();
public slots:
    void fitSheet();
    void setZoom(qreal zoom);
    void setRotation(qreal degrees);
    void centerOn(QPointF point);
    void setCanvasSmoothing(bool enabled);
    void refreshLayerSelectionContext();
signals:
    void guidesChanged();
    void guideSelectionChanged();
    void guideCreationChanged();
    void viewChanged();
    void canvasSmoothingChanged(bool enabled);
    void holdShapeEnabledChanged(bool enabled);
    void stampPlacementEnabledChanged(bool enabled);
    void onionSkinSettingsChanged();
    void documentChanged();
    void frameViewChanged();
    void timelineSoloChanged();
    void animationPlaybackChanged(bool playing);
    void playbackPreviewChanged();
    void playbackPreviewStatusChanged();
    void cameraViewChanged();
    void toolChanged();
    void toolStateApplied();
    void toolSettingsEdited();
    void toolAvailabilityChanged();
    void interactionHintChanged(const QString &message);
    void actionBlocked(const QString &message);
    void brushChanged();
    void effectsPreviewPendingChanged(bool pending);
    void selectionUiChanged();
    void selectionCombineChanged();
    void selectionBarVisibilityChanged(bool visible);
protected:
    bool event(QEvent *) override;
    bool eventFilter(QObject *, QEvent *) override;
    void paintEvent(QPaintEvent *) override;
    void resizeEvent(QResizeEvent *) override;
    void enterEvent(QEnterEvent *) override;
    void leaveEvent(QEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
    void wheelEvent(QWheelEvent *) override;
    void tabletEvent(QTabletEvent *) override;
    void keyPressEvent(QKeyEvent *) override;
    void keyReleaseEvent(QKeyEvent *) override;
    void focusOutEvent(QFocusEvent *) override;
    void showEvent(QShowEvent *) override;
    void hideEvent(QHideEvent *) override;
private:
    friend class CanvasSelectionOverlay;
    friend class GuideTool;
    friend class DrawingToolController;
    friend class Scene3DController;
    friend class KeyframeTransformTool;
    friend class CameraTool;
    friend class MaskTool;
    friend class HoldShapeTool;
    friend class Navigator;
    void zoomAt(qreal zoom, QPointF position);
    void trackPointer(QPointF position, qreal pressure = 1);
    void refreshPointerCursor();
    void syncTabletCursor(QPointF position);
    void handOffTabletCursor(QWidget *receiver, QPointF position);
    void finishInterruptedTabletGesture();
    QRectF brushCursorDamageRect() const;
    bool paintDetailedBrushCursor(QPainter &painter, const QPainterPath &path);
    void clearBrushCursorRaster();
    QString pointerBlockReason() const;
    bool rejectBlockedAction();
    bool prepareToolTransform(const QStringList &layerIds = {});
    QStringList resolvedTransformLayerIds(const QStringList &requested = {}) const;
    BrushInput timedBrushInput(QPointF point, BrushInput input, ulong timestamp, bool beginning = false);
    void beginPaintStroke(QPointF point, const BrushInput &input, bool eraser, Qt::KeyboardModifiers modifiers);
    void continuePaintStroke(QPointF point, const BrushInput &input);
    void flushStraightStroke();
    void resetStrokeConstraint();
    void endPaintContact();
    void flushStampTransform();
    bool prepareStampPress(QPointF position, Qt::KeyboardModifiers modifiers);
    void applyStrokeSamples(const QVector<StrokeSample> &samples);
    void beginSelection(QPointF position, Qt::KeyboardModifiers modifiers);
    void updateSelection(QPointF position);
    void finishSelection(QPointF position);
    void cancelOverlayDrag();
    void beginTransformDrag(QPointF position);
    void updateTransformDrag(QPointF position, Qt::KeyboardModifiers modifiers);
    QTransform artworkViewTransform() const;
    QPolygonF transformBox() const;
    QPointF rotationHandle() const;
    void paintTransformHandles(QPainter &painter) const;
    QPainterPath viewportSelectionPath() const;
    void updateSelectionUi();
    void invalidateArtworkCache();
    void invalidateStrokeArtworkCache();
    void scheduleStrokeArtworkUpdate();
    void ensureArtworkCache();
    void paintOnionSkin(QPainter &painter);
    void synchronizeTimelineSolo(bool reset = false);
    void refreshTimelineSoloPreview() const;
    bool needsAsyncEffectsPreview() const;
    void invalidateEffectsPreviewContext();
    void setEffectsPreviewPending(bool pending);
    void setPlaybackPreviewFallbackReason(const QString &reason);
    EffectsPreviewSnapshot effectsPreviewSnapshot(const QWidget *target, const QTransform &view, bool miniature,
        QImage &background, QByteArray &backgroundKey, std::optional<SheetGeometry> &backgroundGeometry) const;
    enum class TransformDrag { None, Move, Scale, Rotate };
    Document *m_document;
    GuideTool *m_guides;
    DrawingToolController *m_drawingTools;
    Scene3DController *m_scene3D;
    KeyframeTransformTool *m_keyframeTransformTool;
    CameraTool *m_cameraTool;
    MaskTool *m_maskTool;
    HoldShapeTool *m_holdShape;
    bool m_holdShapeEnabled = true;
    Tool m_tool = Tool::Brush;
    std::function<bool(Tool)> m_toolRouter;
    bool m_toolAvailable = true;
    bool m_space = false;
    bool m_tablet = false;
    bool m_tabletCursorOwned = false;
    qreal m_zoom = .4;
    qreal m_rotation = 0;
    QPointF m_center;
    QPointF m_last;
    bool m_dragging = false;
    bool m_fit = true;
    bool m_canvasSmoothing = true;
    bool m_animationPlayback = false, m_onionSkin = false;
    QSet<QString> m_timelineSoloLayers;
    mutable QSet<QString> m_timelineSoloKeptLayers;
    mutable QVector<RasterLayer> m_timelineSoloSource, m_timelineSoloPreview;
    int m_playbackPreviewDivisor = 1;
    QString m_playbackPreviewFallbackReason;
    OnionSkinSettings m_onionSettings;
    bool m_cameraView = true;
    QString m_animationDisplayKey;
    QString m_animationRenderError;
    QImage m_onionCache;
    bool m_selecting = false;
    SelectionCombine m_selectionCombine = SelectionCombine::Replace;
    SelectionCombine m_gestureCombine = SelectionCombine::Replace;
    bool m_selectionFocusOverlay = true;
    qreal m_selectionShadeOpacity = 1;
    bool m_selectionAnimated = true;
    bool m_selectionBarVisible = true;
    CanvasSelectionOverlay *m_selectionOverlay;
    SelectionContextBar *m_selectionBar;
    QImage m_artworkCache;
    EffectsPreviewRenderer *m_effectsPreview = nullptr;
    quint64 m_effectsPreviewContext = 1, m_effectsPreviewRevision = 0, m_effectsPreviewPublishedRevision = 0;
    bool m_effectsPreviewPending = false, m_effectsPreviewActive = false;
    bool m_effectsEditWasActive = false, m_effectsStrokeWasActive = false;
    QSize m_effectsPreviewPixelSize;
    qreal m_effectsPreviewRatio = 0;
    int m_effectsPreviewDotsX = 0, m_effectsPreviewDotsY = 0;
    QImage m_effectsPreviewBackground;
    QByteArray m_effectsPreviewBackgroundKey;
    std::optional<SheetGeometry> m_effectsPreviewBackgroundGeometry;
    BrushSymmetry m_cachedSymmetry;
    bool m_artworkCacheDirty = true;
    bool m_strokeArtworkUpdatePending = false;
    QRegion m_artworkDirtyRegion;
    QString m_strokeLayerId;
    QMap<TileKey, qint64> m_strokeTileRevisions;
    quint64 m_artworkRenderCount = 0;
    quint64 m_artworkPaintedPixelCount = 0;
    quint64 m_strokeTileScanCount = 0;
    QPointF m_selectionOrigin;
    QPainterPath m_selectionPreview;
    QVector<QPointF> m_lassoPoints;
    TransformDrag m_transformDrag = TransformDrag::None;
    int m_resizeCorner = 0;
    QPointF m_transformStart;
    TransformParameters m_transformStartParameters;
    QPointF m_pointerPosition;
    qreal m_pointerPressure = 1;
    bool m_pointerInside = false;
    bool m_pointerEraser = false;
    Qt::KeyboardModifiers m_pointerModifiers = Qt::NoModifier;
    QString m_interactionHint;
    QString m_transformFailure;
    QStringList m_transformFailureLayerIds;
    Tool m_transformFailureTool = Tool::Transform;
    bool m_transformFailureBoundary = false, m_transformFailureWarp = false;
    qreal m_stabilization = 0;
    StrokeFilter m_strokeFilter;
    BrushSpeedTracker m_brushSpeedTracker;
    enum class StrokeConstraint { None, AxisPending, Horizontal, Vertical, Straight };
    StrokeConstraint m_strokeConstraint = StrokeConstraint::None;
    QPointF m_strokeOrigin;
    QTimer m_straightStrokeTimer;
    bool m_straightStrokeDirty = false;
    bool m_stampPlacementEnabled = false, m_stampContact = false;
    QTimer m_stampTransformTimer;
    std::optional<TransformParameters> m_pendingStampTransform;
    BrushInput m_input;
    BrushInput m_previousInput;
    BrushInput m_lastPaintInput;
    QPointF m_paintedTip;
    BrushSettings m_strokeBrush;
    QTimer m_airbrushTimer;
    qreal m_pointerDirection = 0;
    qreal m_initialDirection = 0;
    bool m_hasDirection = false;
    mutable BrushSettings m_outlineSettings;
    mutable BrushInput m_outlineInput;
    mutable qreal m_outlineDirection = 0;
    mutable qreal m_outlineInitialDirection = 0;
    mutable quint64 m_outlineDabIndex = 0;
    mutable quint32 m_outlineSeed = 1;
    mutable QPointF m_outlinePosition;
    mutable bool m_outlineValid = false;
    mutable quint64 m_brushOutlineBuildCount = 0;
    mutable QPainterPath m_tipOutline;
    QPainterPath m_brushCursorRasterPath;
    QImage m_brushCursorRaster;
    qreal m_brushCursorRasterRatio = 0;
    quint64 m_brushCursorRasterBuildCount = 0;
};

class Navigator : public QWidget {
    Q_OBJECT
public:
    explicit Navigator(Canvas *canvas, QWidget *parent = nullptr);
    QTransform miniatureTransform() const;
protected:
    void paintEvent(QPaintEvent *) override;
    bool event(QEvent *event) override;
    void resizeEvent(QResizeEvent *) override;
    void showEvent(QShowEvent *) override;
    void hideEvent(QHideEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
private:
    void invalidatePreview(bool contextChanged = false);
    Canvas *m_canvas;
    EffectsPreviewRenderer *m_effectsPreview = nullptr;
    QImage m_preview;
    QImage m_previewBackground;
    QByteArray m_previewBackgroundKey;
    std::optional<SheetGeometry> m_previewBackgroundGeometry;
    quint64 m_previewContext = 1, m_previewRevision = 0, m_publishedRevision = 0;
    bool m_previewDirty = true, m_previewActive = false, m_effectsEditWasActive = false;
    bool m_strokePreviewQueued = false, m_strokePreviewActive = false;
    bool m_dragging = false;
    QPointF m_offset;
};
} // namespace toon
