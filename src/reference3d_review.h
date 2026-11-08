#pragma once

#include <QImage>
#include <QMainWindow>
#include <QSize>
#include <QVector3D>
#include <memory>

class QUndoStack;

namespace toon {
struct ReferenceAsset;
struct ReferenceScene;
class ReferenceMaterialLibrary;
enum class ReferenceLibraryKind;
enum class ReferenceHandTarget;
// Source assets stay intact when a scene exceeds the bounded review renderer.
QString reference3DPreviewBudgetError(const ReferenceScene &scene);
struct Reference3DPreviewStats {
    quint64 requestedFrames=0,startedFrames=0,completedFrames=0,publishedFrames=0;
    quint64 geometryEvaluations=0,cacheHits=0,requestedGeneration=0,publishedGeneration=0;
    double lastFrameMilliseconds=0;
    QSize lastFrameSize;
    bool finalQuality=false;
};

// An explicitly experimental, independent review scene. It never mutates the
// drawing document and does not expose the retired skeleton/weight editor.
class Reference3DReviewWindow final : public QMainWindow {
    Q_OBJECT
public:
    explicit Reference3DReviewWindow(QWidget *parent = nullptr, QString materialDirectory = {});
    ~Reference3DReviewWindow() override;
    const ReferenceScene &scene() const;
    bool setScene(const ReferenceScene &scene, QString *error = nullptr);
    bool addAsset(const ReferenceAsset &asset, QString *error = nullptr);
    bool placeStarter(const QString &presetId, QString *error = nullptr);
    bool placeCatalogModel(const QString &catalogId, QString *error = nullptr);
    void placeCatalogModelAsync(const QString &catalogId);
    bool catalogPlacementInProgress() const;
    bool applyStarterShape(const QString &presetId, QString *error = nullptr);
    bool applyPosePreset(const QString &presetId, QString *error = nullptr);
    bool applyPosePreset(const QString &presetId, ReferenceHandTarget hand, bool mirror, QString *error = nullptr);
    bool importFile(const QString &path, QString *error = nullptr);
    void importFileAsync(const QString &path);
    bool importInProgress() const;
    bool saveScene(const QString &path, QString *error = nullptr);
    bool openScene(const QString &path, QString *error = nullptr);
    void selectObject(int index);
    void selectJoint(int node);
    bool setJointRotation(const QVector3D &degrees);
    bool setIKTarget(const QVector3D &worldTarget, const QVector3D &worldPole, QString *error = nullptr);
    bool setMorphWeight(int mesh, int target, float weight);
    QString saveMaterial(const QString &name, ReferenceLibraryKind kind, QString *error = nullptr);
    bool applyMaterial(const QString &id, QString *error = nullptr);
    ReferenceMaterialLibrary *materialLibrary() const;
    QUndoStack *undoStack() const;
    QWidget *viewportWidget() const;
    QImage viewportImage() const;
    bool viewportReady() const;
    QString viewportError() const;
    Reference3DPreviewStats previewStats() const;
    // Reuse the approved controls against a native drawing-layer canvas.
    void setCanvasPanelMode(QUndoStack *documentUndo, QSize frameSize);
    void syncCanvasScene(const ReferenceScene &, int object = -1, int joint = -1);
    void finishPendingEdits();
    void cancelPendingEdits();
    int selectedObject() const;
    int selectedJoint() const;
    void requestPresetThumbnail(const QString &presetId, bool pose = false, bool selectedModel = false);
    QImage presetThumbnail(const QString &presetId, bool pose = false, bool selectedModel = false) const;
signals:
    void sceneChanged();
    void statusChanged(const QString &message);
    void previewFramePublished(quint64 generation, bool finalQuality);
    void editBegan();
    void editPreviewed();
    void editEnded(bool cancelled);
    void selectionChanged(int object, int joint);
protected:
    void closeEvent(QCloseEvent *event) override;
private:
    struct State;
    std::unique_ptr<State> m_state;
};
} // namespace toon
