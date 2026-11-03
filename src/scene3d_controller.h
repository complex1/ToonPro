#pragma once
#include "scene3d.h"
#include "project.h"
#include "skin3d_view.h"
#include "pose3d.h"
#include <QObject>
#include <QPointF>
#include <optional>
#include <functional>
#include <atomic>
class QTimer;

namespace toon {
class Canvas;
class Scene3DRenderService;
class Scene3DController : public QObject {
    Q_OBJECT
public:
    enum class Manipulation { Move, Rotate, Scale, Orbit, Pan, Dolly };
    explicit Scene3DController(Canvas *canvas);
    ~Scene3DController() override;
    const Scene3D *scene() const;
    QString layerId() const { return m_layerId; }
    QString selectedObjectId() const { return m_selected; }
    const ModelInstance3D *selectedObject() const;
    bool canEdit() const;
    bool addModel(Model3DPtr model, const QString &name = {});
    bool createScene();
    void selectObject(const QString &id);
    void removeSelected();
    void duplicateSelected();
    void updateObject(const ModelInstance3D &object);
    void updateCamera(const Camera3D &camera);
    void setTextured(bool value);
    void fitScene();
    void fitSelected();
    void resetSelected();
    void selectTool(bool camera);
    void selectRigTool();
    const Rig3D *selectedRig() const;
    const RigJoint3D *selectedJoint() const;
    QString selectedJointId() const { return m_selectedJoint; }
    bool canEditRig() const;
    void selectJoint(const QString &id);
    bool createRig(bool humanoid);
    QString addJoint(bool child = true);
    void updateJoint(const RigJoint3D &joint);
    void removeSelectedJoint();
    void mirrorSelectedBranch();
    bool mirrorEdits() const { return m_mirrorEdits; }
    void setMirrorEdits(bool enabled);
    bool rigVisible() const { return m_rigVisible; }
    bool changingRestSkeleton() const { return m_changingRestSkeleton; }
    void setRigVisible(bool visible);
    enum class RigView { Front, Side, Top };
    void setRigView(RigView view);
    enum class RigEditMode { Rest, Weights, Pose };
    enum class PoseMode { FK, IK };
    enum class IKHandle { Target, Pole };
    bool canPose() const;
    const Pose3D *selectedPose() const;
    PoseMode poseMode() const { return m_poseMode; }
    void setPoseMode(PoseMode mode);
    int poseRotationAxis() const { return m_poseRotationAxis; }
    void setPoseRotationAxis(int axis);
    IKHandle ikHandle() const { return m_ikHandle; }
    void setIKHandle(IKHandle handle);
    QVector3D poseRotation() const;
    bool canRotateSelectedPoseJoint() const;
    void updatePoseRotation(QVector3D rotation);
    QVector3D poseRootTranslation() const;
    void updatePoseRootTranslation(QVector3D translation);
    bool canUseSelectedIK() const;
    std::optional<IKTarget3D> selectedIKTarget() const;
    void enableSelectedIK(bool enabled);
    void updateIKTarget(QVector3D target);
    void updateIKPole(QVector3D pole);
    void setIKPinned(bool pinned);
    JointLimit3D selectedPoseLimit() const;
    void updatePoseLimit(const JointLimit3D &limit);
    void mirrorPose();
    void resetSelectedPose();
    void resetAllPose();
    QString poseStatus() const;
    enum class WeightPaintMode { Add, Subtract, Smooth };
    RigEditMode rigEditMode() const { return m_rigEditMode; }
    void setRigEditMode(RigEditMode mode);
    const Skin3D *selectedSkin() const;
    void bindSkin();
    void unbindSkin();
    bool skinBindingBusy() const { return m_skinBindingBusy; }
    bool canPaintWeights() const;
    WeightPaintMode weightPaintMode() const { return m_weightPaintMode; }
    void setWeightPaintMode(WeightPaintMode mode);
    qreal weightBrushRadius() const { return m_weightBrushRadius; }
    qreal weightBrushStrength() const { return m_weightBrushStrength; }
    void setWeightBrushRadius(qreal radius);
    void setWeightBrushStrength(qreal strength);
    QVector3D skinPreviewRotation() const { return m_skinPreviewRotation; }
    void setSkinPreviewRotation(QVector3D rotation);
    bool weightProjectionBusy() const { return m_projectionBusy; }
    QByteArray previewBaseKey() const { return m_renderBaseKey; }
    QByteArray previewDisplayKey() const { return m_requestedKey; }
    Manipulation manipulation() const { return m_manipulation; }
    void setManipulation(Manipulation mode);
    bool beginEdit(const QString &label);
    bool beginCameraEdit(const QString &label = "Adjust 3D camera");
    void endEdit();
    void cancelEdit();
    void reset();
    bool editing() const { return m_before.has_value() || m_cameraEditing; }
    bool pointerPress(QPointF artwork, Qt::KeyboardModifiers modifiers);
    void pointerMove(QPointF artwork, Qt::KeyboardModifiers modifiers);
    void pointerRelease();
    void paintHandles(QPainter &painter) const;
    bool rendering() const { return m_rendering; }
    quint64 renderCount() const;
signals:
    // Direct UI listeners resolve unsubmitted editor text before history/save.
    void editResolutionRequested(bool commit);
    void changed();
    void warning(const QString &message);
    void sceneAdded();
    void previewChanged();
private:
    void refresh();
    bool mutate(const QString &label, const std::function<void(Scene3D &)> &edit);
    void requestRenders();
    bool rigPointerPress(QPointF artwork);
    void rigPointerMove(QPointF artwork, Qt::KeyboardModifiers modifiers);
    void paintRig(QPainter &painter) const;
    bool mutateRig(const QString &label, const Rig3D &rig);
    bool mutatePose(const QString &label, const Pose3D &pose);
    bool posePointerPress(QPointF artwork);
    void posePointerMove(QPointF artwork, Qt::KeyboardModifiers modifiers);
    void paintPose(QPainter &painter) const;
    bool weightPointerPress(QPointF artwork);
    void weightPointerMove(QPointF artwork);
    void paintWeightDab(QPointF widgetPoint);
    void finishWeightStroke(bool commit);
    void prepareWeightProjection();
    void invalidateSkinView();
    Scene3DRenderOptions skinRenderOptions() const;
    void resetSkinEditing();
    int sceneLayerIndex() const;
    Canvas *m_canvas;
    Scene3DRenderService *m_renderer;
    QString m_layerId, m_selected, m_editLabel;
    QString m_selectedJoint;
    bool m_mirrorEdits = true, m_rigVisible = true;
    bool m_changingRestSkeleton = false;
    bool m_resolvingEditors = false;
    RigEditMode m_rigEditMode = RigEditMode::Rest;
    PoseMode m_poseMode = PoseMode::FK;
    IKHandle m_ikHandle = IKHandle::Target;
    int m_poseRotationAxis = 2;
    enum class PoseDrag { None, Rotation, Root, Target, Pole };
    PoseDrag m_poseDrag = PoseDrag::None;
    QPointF m_posePivot;
    qreal m_poseStartAngle = 0;
    QVector3D m_poseDragOrigin;
    WeightPaintMode m_weightPaintMode = WeightPaintMode::Add;
    qreal m_weightBrushRadius = 40, m_weightBrushStrength = .25;
    QVector3D m_skinPreviewRotation;
    bool m_skinBindingBusy = false, m_projectionBusy = false, m_weightStroke = false;
    quint64 m_bindingGeneration = 0, m_projectionGeneration = 0;
    std::shared_ptr<std::atomic_bool> m_bindingCancel;
    std::optional<Skin3D> m_paintedSkin;
    // Retain the COW buffers so pointer identity is stable until the next render.
    std::optional<Skin3D> m_previewSkin;
    QByteArray m_skinViewId;
    quint64 m_skinViewRevision = 0;
    QHash<quint64, float> m_weightDabStrength;
    QString m_weightStrokeError;
    std::shared_ptr<SkinPaintProjection> m_weightProjection;
    QByteArray m_projectionKey, m_renderBaseKey;
    QPointF m_weightLastPoint;
    QTimer *m_skinRenderTimer = nullptr;
    Manipulation m_manipulation = Manipulation::Move;
    std::optional<Project> m_before;
    // Rendering, picking and all rig/skin helpers share this evaluated snapshot.
    // Scene mutations still start from Document's saved source scene.
    std::optional<Scene3D> m_evaluatedScene;
    bool m_cameraEditing = false;
    QPointF m_dragStart;
    Scene3D m_dragScene;
    bool m_dragging = false, m_rendering = false, m_mutating = false;
    quint64 m_request = 0;
    QByteArray m_requestedKey;
    QImage m_lastImage;
    std::optional<Camera3D> m_lastImageCamera;
    Camera3D m_requestedCamera;
    std::optional<Scene3D> m_observedScene;
    QSize m_observedFrame;
    bool m_hadProject = false, m_wasEditable = false;
};
} // namespace toon
