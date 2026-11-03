#pragma once
#include <QColor>
#include <QImage>
#include <QPainterPath>
#include <QVector>
#include <atomic>
#include <memory>

namespace toon {
enum class CelShadeKind { Shadow, Highlight };
enum class CelBoundaryFinish { Hidden, RegionColor };
struct CelBoundary {
    QPainterPath path;
    CelShadeKind kind = CelShadeKind::Shadow;
    QColor guideColor = QColor("#5cb8ed");
    qreal width = 2;
};
struct CelShadeFill {
    QPoint seed;
    CelShadeKind kind = CelShadeKind::Shadow;
    QColor color = QColor("#77597e");
};
struct CelShadingResult {
    QImage shadow, highlight;
    QVector<int> rejectedFills;
    QString error;
    bool cancelled = false;
};
// Full-resolution, immutable worker input. Outlines must have transparent areas;
// guide colors are never written into output. The frame edge is deliberately an
// open boundary: a fill reaching it requires an explicit closing guide.
CelShadingResult renderCelShading(const QImage &outline, const QVector<CelBoundary> &boundaries,
                                 const QVector<CelShadeFill> &fills, CelBoundaryFinish finish,
                                 const std::shared_ptr<std::atomic_bool> &cancelled = {});
} // namespace toon
