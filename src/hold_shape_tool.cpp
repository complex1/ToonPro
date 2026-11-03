#include "hold_shape_tool.h"
#include "canvas.h"
#include "shape_choice_bar.h"
#include <QLineF>
#include <cmath>

namespace toon {
HoldShapeTool::HoldShapeTool(Canvas *canvas) : QObject(canvas), m_canvas(canvas), m_bar(new ShapeChoiceBar(canvas)) {
    m_bar->hide();
    m_timer.setSingleShot(true); m_timer.setInterval(700); m_timer.setTimerType(Qt::PreciseTimer);
    connect(&m_timer, &QTimer::timeout, this, &HoldShapeTool::detect);
    connect(canvas, &Canvas::viewChanged, this, &HoldShapeTool::positionBar);
    connect(m_bar, &ShapeChoiceBar::choiceActivated, this, &HoldShapeTool::choose);
    connect(m_bar, &ShapeChoiceBar::cancelled, this, [this] {
        reset(); m_canvas->document()->cancelStroke(); m_canvas->setFocus();
    });
}
void HoldShapeTool::reset() {
    m_timer.stop(); m_bar->hide();
    m_tracking = m_fitted = m_pending = false;
    m_samples.clear(); m_candidates.clear(); m_travel = 0;
}
void HoldShapeTool::begin(QPointF point, BrushInput input, bool eligible) {
    reset();
    if (!eligible || !m_canvas->holdShapeEnabled()) return;
    m_tracking = true; m_holdAnchor = point; m_samples.append({point, input});
}
bool HoldShapeTool::move(QPointF point, BrushInput input) {
    if (!m_tracking || !m_canvas->document()->strokeActive()) return false;
    if (!std::isfinite(point.x()) || !std::isfinite(point.y()) || !std::isfinite(input.pressure)) return m_fitted;
    const qreal zoom = m_canvas->artworkZoom();
    const bool moved = QLineF(m_holdAnchor, point).length() * zoom > 3;
    if (m_fitted) {
        if (!moved) return true;
        // Moving again resumes the exact freehand engine state, including
        // pressure/spacing/random sequence. A subsequent pause can fit again.
        restoreOriginal();
        m_fitted = false; m_candidates.clear();
    }
    if (m_samples.isEmpty()) return false;
    const qreal distance = QLineF(m_samples.last().point, point).length();
    if (distance > 50000) { reset(); return false; }
    if (distance * zoom >= .35) {
        m_travel += distance;
        // Bound input retained for fitting, without dropping live paint events.
        if (m_samples.size() >= 4096) {
            QVector<StrokeInputSample> reduced; reduced.reserve(2049);
            for (int i = 0; i < m_samples.size(); i += 2) reduced.append(m_samples[i]);
            reduced.append(m_samples.last()); m_samples = std::move(reduced);
        }
        m_samples.append({point, input});
    } else if (m_samples.size() > 1) m_samples.last().input = input;
    if (moved) {
        m_holdAnchor = point;
        if (m_travel * zoom >= 12) m_timer.start();
    }
    return false;
}
void HoldShapeTool::detect() {
    if (!m_tracking || m_fitted || !m_canvas->document()->strokeActive() || m_samples.size() < 2) return;
    QVector<QPointF> points; points.reserve(m_samples.size());
    for (const auto &sample : m_samples) points.append(sample.point);
    auto candidates = recognizeStrokeShapes(points);
    if (candidates.isEmpty()) return;
    auto *doc = m_canvas->document();
    m_originalFilter = m_canvas->m_strokeFilter;
    m_originalInput = m_canvas->m_input; m_originalPreviousInput = m_canvas->m_previousInput;
    m_originalLastPaintInput = m_canvas->m_lastPaintInput; m_originalPaintedTip = m_canvas->m_paintedTip;
    doc->preserveStrokeShapeOriginal();
    if (!doc->previewStrokePath(candidates.first().path, m_samples)) {
        restoreOriginal(); return;
    }
    m_candidates = std::move(candidates); m_fitted = true;
    m_holdAnchor = m_samples.last().point;
}
bool HoldShapeTool::release() {
    m_timer.stop(); m_tracking = false;
    if (!m_fitted || !m_canvas->document()->strokeActive()) return false;
    m_pending = true;
    QPainterPath original(m_samples.first().point);
    for (int i = 1; i < m_samples.size(); ++i) original.lineTo(m_samples[i].point);
    m_bar->setChoices(m_candidates, original);
    positionBar(); m_bar->show(); m_bar->raise();
    return true;
}
void HoldShapeTool::positionBar() {
    if (!m_pending) return;
    m_bar->setAvailableWidth(qMax(80, m_canvas->width() - 24));
    // Keep the choice strip in the canvas, away from the current pen position.
    m_bar->move((m_canvas->width() - m_bar->width()) / 2, qMax(8, m_canvas->height() - m_bar->height() - 14));
    m_bar->raise();
}
void HoldShapeTool::choose(int index) {
    if (!m_pending || !m_canvas->document()->strokeActive()) return;
    if (index < 0) { restoreOriginal(); m_fitted = false; }
    else if (index < m_candidates.size()) {
        if (!m_canvas->document()->previewStrokePath(m_candidates[index].path, m_samples)) return;
    } else return;
    m_canvas->finishStroke(); m_canvas->setFocus();
}
void HoldShapeTool::restoreOriginal() {
    if (!m_canvas->document()->restoreStrokeShapeOriginal()) return;
    m_canvas->m_strokeFilter = m_originalFilter;
    m_canvas->m_input = m_originalInput; m_canvas->m_previousInput = m_originalPreviousInput;
    m_canvas->m_lastPaintInput = m_originalLastPaintInput; m_canvas->m_paintedTip = m_originalPaintedTip;
}
}
