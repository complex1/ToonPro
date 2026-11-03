#pragma once
#include "camera_animation.h"
#include <QObject>
#include <QPolygonF>
class QPainter;
namespace toon {
class Canvas;
// Layout-space camera framing. Navigation stays entirely on Canvas.
class CameraTool final : public QObject {
    Q_OBJECT
public:
    explicit CameraTool(Canvas *canvas);
    bool enabled() const { return m_enabled; }
    bool dragging() const { return m_drag != 0; }
    void setEnabled(bool enabled);
    void finish();
    void cancel();
    QPolygonF box() const;
    QPointF rotationHandle() const;
    void paint(QPainter &painter) const;
signals:
    void changed();
protected:
    bool eventFilter(QObject *, QEvent *) override;
private:
    bool press(QPointF position);
    void move(QPointF position, bool fine);
    Canvas *m_canvas;
    bool m_enabled = false, m_tablet = false;
    int m_drag = 0, m_frame = -1;
    Camera2D m_before;
    QPointF m_start, m_center;
    qreal m_startRadius = 0, m_lastAngle = 0, m_rotationDelta = 0;
};
} // namespace toon
