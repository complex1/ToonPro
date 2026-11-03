#include "stroke_filter.h"
#include <QLineF>
#include <cmath>

namespace toon {
static bool validSample(QPointF point, qreal pressure) {
    return std::isfinite(point.x()) && std::isfinite(point.y()) && std::isfinite(pressure);
}
StrokeSample StrokeFilter::begin(QPointF point, qreal pressure, qreal strength) {
    reset();
    if (!validSample(point, pressure)) return {};
    m_active = true;
    m_length = std::isfinite(strength) ? qBound(0.0, strength, 100.0) * .24 : 0;
    m_raw = m_filtered = m_tip = {point, qBound(0.0, pressure, 1.0)};
    return m_tip;
}
void StrokeFilter::reset() {
    m_active = false;
    m_untilSample = 1;
}
void StrokeFilter::integrate(const StrokeSample &next, qreal distance) {
    // Exact solution to dy/ds = (x(s) - y) / length for a linear input
    // segment. Splitting that segment into more tablet events gives the same
    // result, including linearly interpolated pressure.
    const qreal decay = std::exp(-distance / m_length);
    const qreal response = -std::expm1(-distance / m_length);
    const qreal ramp = distance - m_length * response;
    m_filtered.point = m_filtered.point * decay + m_raw.point * response
        + (next.point - m_raw.point) * (ramp / distance);
    m_filtered.pressure = m_filtered.pressure * decay + m_raw.pressure * response
        + (next.pressure - m_raw.pressure) * (ramp / distance);
    m_raw = next;
}
QVector<StrokeSample> StrokeFilter::advance(QPointF point, qreal pressure) {
    QVector<StrokeSample> samples;
    if (!m_active || !validSample(point, pressure)) return samples;
    const StrokeSample next{point, qBound(0.0, pressure, 1.0)};
    const qreal distance = QLineF(m_raw.point, point).length();
    // Never interpolate a corrupted, implausibly distant input event. Canvas
    // cancels the gesture rather than letting it connect through the artwork.
    if (distance > 50000) { reset(); return samples; }
    if (m_length == 0) {
        m_raw = m_filtered = m_tip = next;
        samples.append(next);
        return samples;
    }
    if (distance < 1e-9) {
        m_raw.pressure = m_filtered.pressure = m_tip.pressure = next.pressure;
        return samples;
    }
    const StrokeSample start = m_raw;
    qreal travelled = 0;
    // Emit at fixed one-pixel intervals of raw path length. This also prevents
    // event frequency from changing the polygon rendered by the raster engine.
    while (distance - travelled >= m_untilSample - 1e-9 && distance - travelled > 1e-9) {
        const qreal step = qMin(m_untilSample, distance - travelled);
        travelled += step;
        const qreal fraction = travelled / distance;
        integrate({start.point + (next.point - start.point) * fraction,
            start.pressure + (next.pressure - start.pressure) * fraction}, step);
        m_tip = m_filtered;
        samples.append(m_tip);
        m_untilSample = 1;
    }
    const qreal remainder = distance - travelled;
    if (remainder > 1e-9) {
        integrate(next, remainder);
        m_untilSample -= remainder;
    }
    return samples;
}
QVector<StrokeSample> StrokeFilter::finish(QPointF point, qreal pressure) {
    auto samples = advance(point, pressure);
    if (!m_active) return samples;
    // The lag is bounded by the filter length plus one sampling pixel. Finish it directly,
    // within this gesture, so lifting never leaves a missing end or a timer tail.
    if (m_length > 0 && QLineF(m_tip.point, m_raw.point).length() > 1e-9) {
        m_tip = m_raw;
        samples.append(m_tip);
    }
    reset();
    return samples;
}
} // namespace toon
