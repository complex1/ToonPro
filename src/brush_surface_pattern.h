#pragma once
#include "brush_surface_settings.h"
#include <QColor>
#include <QByteArray>
#include <QTransform>
#include <QVector>

namespace toon {
// Construct once per dab/preview, then sample bounded work per artwork pixel.
class BrushSurfaceSampler {
public:
    explicit BrushSurfaceSampler(const BrushSurfacePattern &pattern,
                                 QColor foreground = Qt::black);
    bool isValid() const { return m_valid; }
    bool enabled() const { return m_pattern.kind != BrushSurfaceKind::Off; }
    bool colored() const { return m_pattern.kind == BrushSurfaceKind::Image; }
    const QByteArray &cacheIdentity() const { return m_cacheIdentity; }
    // Premultiplied color. Tone alpha is coverage and RGB follows foreground.
    QRgb sample(QPointF artworkPixelCenter) const;
    qreal coverage(QPointF artworkPixelCenter) const;
private:
    bool map(QPointF point, QPointF &mapped) const;
    qreal tone(QPointF point) const;
    QRgb image(QPointF point, int level = 0) const;
    BrushSurfacePattern m_pattern;
    QByteArray m_cacheIdentity;
    QTransform m_inverse;
    QColor m_foreground;
    qreal m_cos = 1, m_sin = 0, m_radius = 0, m_lineWidth = 0;
    std::array<qreal, 256> m_fadeRadii{};
    QVector<QImage> m_imageLevels;
    bool m_valid = false;
};
// The image must be ARGB32_Premultiplied. Its origin is in artwork pixels.
// For Image mode the resulting image is colored even if its input was a mask.
void applyBrushSurface(QImage &image, QPoint origin, const BrushSurfaceSampler &surface,
                       bool sampledAlreadyColored = false, QImage *coverage = nullptr);
} // namespace toon
