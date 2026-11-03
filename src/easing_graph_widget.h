#pragma once
#include <QPointF>
#include <QWidget>

namespace toon {
// Normalized time/value graph for one outgoing scalar keyframe segment.
// The owning panel decides whether a gesture is editable and groups its previews
// into one document command; the graph does not own animation or history.
class EasingGraphWidget final : public QWidget {
    Q_OBJECT
public:
    explicit EasingGraphWidget(QWidget *parent = nullptr);
    QPointF firstControl() const { return m_first; }
    QPointF secondControl() const { return m_second; }
    bool editing() const { return m_editing; }
    int selectedHandle() const { return m_selected; }
    QPointF handlePosition(int handle) const;
    QRectF graphRect() const;
    void setCurve(QPointF first, QPointF second);
    void setHold(bool hold);
    void setSegment(int firstFrame, int lastFrame, qreal firstValue, qreal lastValue, const QString &unit);
public slots:
    void finishEditing();
    void cancelEditing();
signals:
    void editingStarted();
    void curveChanged(QPointF first, QPointF second);
    void editingFinished();
    void editingCancelled();
protected:
    bool event(QEvent *event) override;
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void focusOutEvent(QFocusEvent *event) override;
private:
    QPointF toScreen(QPointF point) const;
    QPointF fromScreen(QPointF point) const;
    void beginEditing();
    void moveHandle(QPointF value);
    QPointF m_first{1. / 3, 1. / 3}, m_second{2. / 3, 2. / 3};
    QPointF m_originalFirst, m_originalSecond, m_pressPosition, m_pressControl;
    qreal m_plotLow = -.2, m_plotHigh = 1.2;
    int m_selected = 0;
    bool m_editing = false, m_dragging = false, m_hold = false, m_originalHold = false;
    QString m_startLabel, m_endLabel;
};
} // namespace toon
