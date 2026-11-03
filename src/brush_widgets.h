#pragma once
#include "brush_settings.h"
#include <QWidget>

namespace toon {
// Spatial controls edit the same brush fields as the precise numeric controls.
// The preview is local to this widget; it never renders or mutates document pixels.
class BrushVisualWidget : public QWidget {
    Q_OBJECT
public:
    enum class Kind { Tip, Spacing, Scatter, Pose, Symmetry, Texture };
    explicit BrushVisualWidget(Kind kind, QWidget *parent = nullptr);
    void setBrush(const BrushSettings &brush);
    const BrushSettings &brush() const { return m_brush; }
    void setArtworkSize(QSizeF size);
    QSize sizeHint() const override;
Q_SIGNALS:
    void brushEdited(const toon::BrushSettings &brush);
protected:
    bool event(QEvent *) override;
    void paintEvent(QPaintEvent *) override;
    void resizeEvent(QResizeEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
    void keyPressEvent(QKeyEvent *) override;
private:
    QRectF field() const;
    QPointF anglePoint(qreal angle, qreal radius) const;
    void editAt(QPointF point);
    void commit();
    Kind m_kind;
    BrushSettings m_brush;
    QSizeF m_artwork{1920, 1080};
    bool m_dragging = false, m_angleDrag = false;
    QPointF m_start;
    qreal m_startScale = 1;
    QImage m_preview;
};
} // namespace toon
