#pragma once
#include <QObject>
#include <QPointF>
#include <QPolygonF>
#include <QTransform>
#include <QHash>
#include <QMap>
#include <QPainterPath>
#include <array>
class QPainter;
namespace toon {
class Canvas;
class KeyframeTransformTool final : public QObject {
    Q_OBJECT
public:
    enum class Mode { Transform, MotionPath, Pivot };
    explicit KeyframeTransformTool(Canvas *canvas);
    bool enabled() const { return m_enabled; }
    void setEnabled(bool enabled);
    Mode mode() const { return m_mode; }
    void setMode(Mode mode);
    bool dragging() const { return m_drag != 0; }
    void finish();
    void cancel();
    QString unavailableReason() const;
    void paint(QPainter &painter) const;
    QPolygonF box() const;
    QPointF rotationHandle() const;
    QPointF pivotHandle() const;
    struct MotionControlHandle {
        int frame = 0;
        bool incoming = false;
        QPointF anchor, position;
    };
    // Viewport coordinates keep tangent hit targets independent of zoom.
    QVector<MotionControlHandle> motionControlHandles() const;
    int selectedMotionFrame() const;
signals:
    void changed();
protected:
    bool eventFilter(QObject *, QEvent *) override;
private:
    bool press(QPointF point, Qt::KeyboardModifiers modifiers);
    void move(QPointF point, bool proportional);
    void refreshMotionGeometry() const;
    QString gestureLabel() const;
    QRectF contentBounds() const;
    Canvas *m_canvas;
    Mode m_mode = Mode::Transform;
    bool m_enabled = false, m_tablet = false, m_moved = false;
    int m_drag = 0;
    int m_dragFrame = -1, m_canvasFrame = -1, m_selectedMotionFrame = -1;
    QString m_layerId;
    QPointF m_start, m_pivot, m_tangentStart;
    qreal m_lastAngle = 0, m_rotationDelta = 0, m_transformRotation = 0;
    QTransform m_parentInverse, m_sourceInverse;
    std::array<qreal,6> m_values{};
    struct RasterBounds { QMap<QPair<int,int>, qint64> revisions; QRectF bounds; };
    mutable QHash<QString, RasterBounds> m_rasterBounds;
    struct MotionMarker { int frame; QPointF position; };
    mutable QVector<MotionMarker> m_motionMarkers;
    mutable QPainterPath m_motionPath;
    mutable bool m_motionDirty = true;
};
}
