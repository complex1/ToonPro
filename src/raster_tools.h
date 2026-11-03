#pragma once
#include "document.h"
#include <QGradient>

namespace toon {
enum class RasterSampleSource { ActiveLayer, AllLayers, ReferenceLayer };
struct RasterFillSettings {
    RasterSampleSource source = RasterSampleSource::AllLayers;
    QString referenceLayerId;
    int tolerance = 24;
    int gapClosing = 0;
};
enum class RetouchMode { Smudge, Mixer, Blur, Sharpen, Dodge, Burn };
struct RetouchSettings {
    RetouchMode mode = RetouchMode::Smudge;
    qreal size = 48;
    qreal strength = .5;
    QColor color = Qt::black;
};
class RasterTools {
public:
    explicit RasterTools(Document *document) : m_document(document) {}
    static QColor sample(Document *, QPointF, RasterSampleSource = RasterSampleSource::AllLayers,
                         const QString &layerId = {}, const Shot *displayShot = nullptr);
    static bool bucket(Document *, QPointF, const QColor &, const RasterFillSettings & = {});
    static bool selectColor(Document *, QPointF, const RasterFillSettings &, bool contiguous, SelectionCombine = SelectionCombine::Replace);
    static bool gradient(Document *, QPointF start, QPointF end, const QGradientStops &, bool radial = false);
    static bool drawPath(Document *, const QPainterPath &, const QColor &fill, const QColor &stroke, qreal width);
    static bool featherSelection(Document *, qreal radius);
    static bool saveSelection(Document *, const QString &name);
    static bool loadSelection(Document *, const QString &id);
    static bool deleteSelection(Document *, const QString &id);
    bool beginRetouch(QPointF, qreal pressure, const RetouchSettings &);
    void continueRetouch(QPointF, qreal pressure);
    void endRetouch();
    void cancelRetouch();
    bool retouchActive() const { return m_retouching; }
private:
    bool retouchDab(QPointF, qreal pressure, QPointF movement);
    Document *m_document;
    RetouchSettings m_settings;
    RasterSelectionCache m_selectionCache;
    QPointF m_lastPoint, m_lastDabPoint;
    qreal m_distanceToDab = 1;
    qreal m_lastPressure = 1;
    bool m_retouching = false;
    bool m_changed = false;
};
} // namespace toon
