#pragma once

#include "property_animation.h"

namespace toon {
struct MotionPathSample {
    qreal frame = 0;
    QPointF position;
    bool moveTo = false;
};
struct MotionPathGeometry {
    QVector<MotionPathSample> samples;
    QPainterPath path;
};
QPointF effectiveAnimationPivot(const Shot &shot, const QString &layerId, QSizeF fallbackSize = {});
QPointF effectiveAnimationPivot(const Project &project, const Shot &shot, const QString &layerId, int frame = 0);
QPointF animationPivotWorld(const Shot &shot, const QString &layerId, qreal frame, QSizeF fallbackSize = {});
QPointF animationPivotWorld(const Project &project, const Shot &shot, const QString &layerId, int frame);
// Exact editable position-key markers remain separate from the bounded curve.
QVector<int> motionPositionKeyFrames(const Shot &shot, const QString &layerId);
// Exact position markers, evaluated with one shared ancestry/pivot lookup.
QVector<MotionPathSample> motionPositionKeySamples(const Shot &shot, const QString &layerId, QSizeF fallbackSize = {});
MotionPathGeometry buildMotionPath(const Shot &shot, const QString &layerId,
                                  QSizeF fallbackSize = {}, int maximumSamples = 2048);
} // namespace toon
