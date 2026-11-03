#pragma once
#include "canvas.h"
#include "raster_tools.h"
#include "object_tools.h"
#include "advanced_transform.h"
class QPlainTextEdit;

namespace toon {
class DrawingToolController : public QObject {
    Q_OBJECT
public:
    explicit DrawingToolController(Canvas *canvas);
    enum class PathEditMode { Transform, Nodes, Width };
    PathEditMode pathEditMode = PathEditMode::Transform;
    qreal simplifyTolerance = 2, widthBrushRadius = 30, widthBrushStrength = .2;
    bool widthThicken = true;
    qreal objectOpacity = 1;
    void setPathEditMode(PathEditMode mode);
    bool simplifySelected();
    bool simplifyLayer();
    bool resetWidths();
    bool beginStyleEdit();
    void previewObjectStyle();
    void finishStyleEdit();
    void cancelStyleEdit();
    RasterFillSettings fillSettings;
    RetouchSettings retouchSettings;
    QGradientStops gradientStops{{0,Qt::black},{1,Qt::white}};
    bool radialGradient=false, shapeFill=false, shapeStroke=true, closedPath=false, eraseSegments=false;
    QColor fillColor=Qt::white;
    qreal strokeWidth=3;
    EditableObject textStyle;
    bool warpMode=false, selectionBoundary=false;
    AdvancedTransform *transform() { return &m_transform; }
    bool beginAdvanced(const QStringList &ids={},bool selectionOnly=false);
    bool handlesTool() const;
    bool active() const;
    bool dragging() const { return m_drag; }
    bool waitingForArcBend() const { return m_arcPending; }
    bool press(QPointF view,Qt::KeyboardModifiers modifiers,qreal pressure=1);
    bool move(QPointF view,Qt::KeyboardModifiers modifiers,qreal pressure=1);
    bool release(QPointF view,Qt::KeyboardModifiers modifiers,qreal pressure=1);
    bool key(int key,Qt::KeyboardModifiers modifiers);
    void finishPath(bool close=false);
    void finishGesture();
    void interruptGesture();
    void cancel();
    void paint(QPainter &painter) const;
    QPainterPath cursorPath() const;
    std::optional<QPainterPath> selectionPreview() const;
    QString selectedObjectId() const { return m_objectId; }
    std::optional<EditableObject> selectedObject() const;
    bool selectedPathAnimated() const;
    bool selectAnimatedPath(const QString &id);
    void applyObjectStyle();
    void notifySettings();
    bool beginTextEdit(bool inlineEditor = true);
    bool previewTextStyle();
    void finishTextEdit(bool cancel = false);
    bool textEditing() const { return m_textEdit; }
signals:
    void changed();
protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
private:
    Document *doc() const { return m_canvas->document(); }
    QPainterPath gesturePath() const;
    void commitPath(const QPainterPath &path,const QVector<BezierNode> &nodes={},bool closed=false);
    void selectObject(const QString &id);
    EditableObject styledObject(EditableObject object) const;
    void applyVectorBrush(QPointF point, qreal pressure);
    QPointF rotationHandle(const EditableObject &object) const;
    QVector<int> visibleNodeIndices(const EditableObject &object) const;
    QTransform objectLayerTransform() const;
    QTransform objectViewTransform() const;
    QVector<EditableObject> editableObjectsAtFrame() const;
    void synchronizeObjectStyle();
    void positionTextEditor();
    Canvas *m_canvas;
    RasterTools m_raster;
    ObjectTools m_objects;
    AdvancedTransform m_transform;
    QPointF m_start,m_current;
    QVector<QPointF> m_points,m_transformPoints;
    QVector<BezierNode> m_nodes;
    QPainterPath m_eraseSweep;
    QString m_objectId;
    QString m_contextLayerId;
    EditableObject m_editObject;
    bool m_drag=false;
    bool m_arcPending=false;
    QPointF m_arcStart,m_arcEnd;
    bool m_styleEdit=false;
    QPointer<QPlainTextEdit> m_textEditor;
    bool m_textEdit=false, m_textResolving=false, m_openTextOnRelease=false, m_focusingTextEditor=false;
    QVector<EditableObject> m_editObjects;
    QPointF m_lastBrushPoint;
    QPainterPath m_simplifyRegion;
    int m_handle=-1,m_node=-1,m_control=0,m_contextNodeCount=0;
    SelectionCombine m_combine=SelectionCombine::Replace;
};
}
