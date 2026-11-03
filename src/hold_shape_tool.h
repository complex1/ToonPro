#pragma once
#include "document.h"
#include "shape_recognition.h"
#include "stroke_filter.h"
#include <QObject>
#include <QTimer>

namespace toon {
class Canvas;
class ShapeChoiceBar;
class HoldShapeTool final : public QObject {
    Q_OBJECT
public:
    explicit HoldShapeTool(Canvas *canvas);
    void begin(QPointF point, BrushInput input, bool eligible);
    // Returns true when the fitted shape owns this contact packet.
    bool move(QPointF point, BrushInput input);
    bool release();
    void reset();
    bool pending() const { return m_pending; }
    bool fitted() const { return m_fitted; }
private:
    void detect();
    void positionBar();
    void choose(int index);
    void restoreOriginal();
    Canvas *m_canvas;
    ShapeChoiceBar *m_bar;
    QTimer m_timer;
    QVector<StrokeInputSample> m_samples;
    QVector<RecognizedShape> m_candidates;
    QPointF m_holdAnchor;
    qreal m_travel = 0;
    StrokeFilter m_originalFilter;
    BrushInput m_originalInput, m_originalPreviousInput, m_originalLastPaintInput;
    QPointF m_originalPaintedTip;
    bool m_tracking = false, m_fitted = false, m_pending = false;
};
}
