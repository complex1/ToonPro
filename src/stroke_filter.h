#pragma once
#include <QPointF>
#include <QVector>

namespace toon {
struct StrokeSample {
    QPointF point;
    qreal pressure = 1;
};

// A causal low-pass filter parameterized by travelled artwork distance, not
// elapsed time or event count. Increasing strength rounds corners and introduces
// up to 25 artwork pixels of trailing distance, including sampling. No timer
// continues the stroke.
class StrokeFilter {
public:
    StrokeSample begin(QPointF point, qreal pressure, qreal strength);
    QVector<StrokeSample> advance(QPointF point, qreal pressure);
    QVector<StrokeSample> finish(QPointF point, qreal pressure);
    void reset();
    bool active() const { return m_active; }
    StrokeSample tip() const { return m_tip; }
    StrokeSample raw() const { return m_raw; }
private:
    void integrate(const StrokeSample &next, qreal distance);
    bool m_active = false;
    qreal m_length = 0;
    qreal m_untilSample = 1;
    StrokeSample m_raw;
    StrokeSample m_filtered;
    StrokeSample m_tip;
};
} // namespace toon
