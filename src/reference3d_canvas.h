#pragma once
#include "reference3d.h"
#include "reference3d_pose_library.h"
#include <QObject>
#include <QTransform>
#include <QHash>
#include <memory>
#include <functional>

class QPainter;
class QWidget;
class QMouseEvent;
class QWheelEvent;
class QKeyEvent;
class QTabletEvent;
namespace toon {
class Document;
enum class Reference3DCanvasMode { Translate, Rotate, Scale, Pose, Camera };
// Canvas editing for a native reference layer. Artwork layers are never handled.
class Reference3DCanvasController final : public QObject {
    Q_OBJECT
public:
    explicit Reference3DCanvasController(Document *, QWidget *canvas);
    ~Reference3DCanvasController() override;
    bool available() const;
    bool editable() const;
    bool interactionActive() const;
    QString layerId() const;
    const ReferenceScene *scene() const;
    Reference3DCanvasMode mode() const;
    void setMode(Reference3DCanvasMode);
    int axis() const;
    void setAxis(int); // -1: free translation/uniform scale; 0/1/2: X/Y/Z.
    int selectedObject() const;
    int selectedJoint() const;
    void selectObject(int);
    void selectJoint(int);
    bool setJointRotation(const QVector3D &degrees, QString *error = nullptr);
    bool applyPosePreset(const QString &, QString *error = nullptr,
                         ReferenceHandTarget hands = ReferenceHandTarget::Both, bool mirror = false);
    bool setScene(const ReferenceScene &, QString *error = nullptr);
    void cancelInteraction();
    void finishInteraction(bool cancelled = false);
    void setLayerVisibilityFilter(std::function<bool(const QString &)>);
    void prepareLayerPreviews(QSize frameSize);
    QHash<QString,QImage> preparedLayerPreviews() const;
    bool previewReady() const;
    QString previewError() const;
    void paintOverlay(QPainter &, const QTransform &artworkToViewport, QSize frameSize);
    bool mousePress(QMouseEvent *, const QTransform &artworkToViewport, QSize frameSize);
    bool mouseMove(QMouseEvent *, const QTransform &artworkToViewport, QSize frameSize);
    bool mouseRelease(QMouseEvent *, const QTransform &artworkToViewport, QSize frameSize);
    bool wheel(QWheelEvent *, const QTransform &artworkToViewport, QSize frameSize);
    bool keyPress(QKeyEvent *);
    bool tablet(QTabletEvent *, const QTransform &artworkToViewport, QSize frameSize);
signals:
    void selectionChanged(int object, int joint);
    void modeChanged();
    void sceneEdited();
    void interactionChanged(bool active);
    void warning(const QString &message);
    void previewChanged();
private:
    struct State;
    std::unique_ptr<State> m_state;
};
} // namespace toon
