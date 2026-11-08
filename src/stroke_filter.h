#pragma once
#include <QPointF>
#include <QVector>

namespace toon {
struct StrokeSample {
    QPointF point;
    qreal pressure = 1;
};
enum class StrokeFilterMode { Smooth, Stabilizer };
struct StrokeFilterSettings {
    StrokeFilterMode mode = StrokeFilterMode::Smooth;
    qreal strength = 0;
    // Artwork units per logical screen pixel, snapshotted at contact.
    qreal pixelScale = 1;
    qreal deadZone = 0; // Logical screen pixels.
    bool finishAtEndpoint = true;
};

// Causal, distance-domain smoothing. The settings overload adds a strong pulled
// string, a radial dead zone and an ordered, bounded completion tail. Settings
// and view scale are latched at begin; no timer continues the stroke.
class StrokeFilter {
public:
    StrokeSample begin(QPointF point, qreal pressure, qreal strength);
    StrokeSample begin(QPointF point, qreal pressure, const StrokeFilterSettings &settings);
    QVector<StrokeSample> advance(QPointF point, qreal pressure);
    QVector<StrokeSample> finish(QPointF point, qreal pressure);
    void reset();
    bool active() const { return m_active; }
    bool failed() const { return m_failed; }
    StrokeSample tip() const { return m_tip; }
    StrokeSample raw() const { return m_raw; }
private:
    void integrate(const StrokeSample &next, qreal distance);
    void fail();
    void processConfigured(const StrokeSample &next, QVector<StrokeSample> &output, bool forceEndpoint = false);
    void appendConfigured(const StrokeSample &next, QVector<StrokeSample> &output);
    StrokeSample historyAt(qreal distance) const;
    StrokeSample weightedHistory(qreal distance, qreal radius) const;
    void completeTail(QVector<StrokeSample> &output);
    struct HistorySample { StrokeSample sample; qreal distance = 0; };
    bool m_active = false;
    bool m_failed = false;
    bool m_legacy = true;
    StrokeFilterSettings m_settings;
    qreal m_length = 0;
    qreal m_spacing = 1;
    qreal m_untilSample = 1;
    qreal m_progress = 0, m_filteredProgress = 0;
    StrokeSample m_pathRaw;
    QVector<HistorySample> m_history;
    StrokeSample m_raw;
    StrokeSample m_filtered;
    StrokeSample m_tip;
};
} // namespace toon
