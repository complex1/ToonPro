#pragma once
#include "drawing_guide.h"
#include <QImage>
#include <QLineF>
#include <QRect>
#include <functional>
#include <optional>

namespace toon {
enum class PerspectiveMode { Auto, OnePoint, TwoPoint };
struct PerspectiveSegment {
    QLineF line;
    qreal weight = 1;
    bool enabled = true;
};
struct PerspectiveCandidate {
    bool parallel = false;
    QPointF point;
    QPointF direction;
    QVector<int> supportingSegments;
    qreal score = 0;
    qreal meanAngularError = 0; // Degrees, not a calibrated probability.
    bool strong = false;
};
struct PerspectiveDetectionSettings {
    PerspectiveMode mode = PerspectiveMode::Auto;
    QRect region; // Empty means the whole source image.
    qreal sensitivity = .5;
};
struct PerspectiveDetectionResult {
    QSize sourceSize;
    QVector<PerspectiveSegment> segments;
    QVector<PerspectiveCandidate> candidates;
    QString message;
    QString error;
    bool cancelled = false;
};
using PerspectiveCancellation = std::function<bool()>;

// Results use original input-image coordinates, including a selected region's
// offset. All work is local and bounded; no input pixels are modified.
PerspectiveDetectionResult detectPerspective(const QImage &, const PerspectiveDetectionSettings & = {},
                                              const PerspectiveCancellation & = {});
PerspectiveDetectionResult fitPerspectiveSegments(const QVector<PerspectiveSegment> &, QSize sourceSize,
                                                  const PerspectiveDetectionSettings & = {},
                                                  const PerspectiveCancellation & = {});
std::optional<PerspectiveCandidate> perspectiveFromLines(const QLineF &, const QLineF &);

// Returns ordinary, uncommitted guide geometry. The document command assigns
// fresh IDs and performs the final artwork-coordinate mapping/validation.
QVector<DrawingGuide> createPerspectiveGuides(const QVector<PerspectiveCandidate> &, const QVector<int> &chosen,
                                            QPointF anchor, const QRectF &imageBounds,
                                            PerspectiveMode = PerspectiveMode::Auto);
}
