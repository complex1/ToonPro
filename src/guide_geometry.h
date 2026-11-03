#pragma once
#include "drawing_guide.h"
#include <QLineF>
#include <QPainterPath>
#include <QTransform>
namespace toon {
// Geometry is artwork-space and bounded by the caller's visible artwork rectangle.
bool validGuideGeometry(const DrawingGuide &guide);
QPainterPath guidePath(const DrawingGuide &guide, const QRectF &visibleBounds, qreal detailTolerance = .5);
QVector<QLineF> guideSnapLines(const DrawingGuide &guide, QPointF strokeStart);
QPointF projectOntoGuideLine(QPointF point, const QLineF &line);
qreal guideLineDistance(QPointF point, const QLineF &line);

struct GuideConstraint {
    enum class Kind { Line, Ellipse };
    Kind kind = Kind::Line;
    QLineF line;
    QPointF center;
    // Orthogonal artwork-space vectors: their lengths are the ellipse radii.
    QPointF axisX, axisY;
};
QVector<GuideConstraint> guideSnapConstraints(const DrawingGuide &guide, QPointF strokeStart);
QPointF projectOntoGuideConstraint(QPointF point, const GuideConstraint &constraint);
qreal guideConstraintDistance(QPointF point, const GuideConstraint &constraint);
QPainterPath guideConstraintPath(const GuideConstraint &constraint, const QRectF &visibleBounds,
                                qreal detailTolerance = .5);
// Shortest ellipse arc, excluding from and including the projected endpoint.
// The maximum is 512 samples, independent of document size or pointer speed.
QVector<QPointF> guideConstraintSamples(const GuideConstraint &constraint, QPointF from,
                                      QPointF to, qreal maxError = .5);
} // namespace toon
