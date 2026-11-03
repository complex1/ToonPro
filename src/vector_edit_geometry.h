#pragma once
#include "editable_object.h"

namespace toon {
// Geometry-only helpers. All inputs and output remain in object-local coordinates.
// A local brush region is supplied in artwork coordinates.
EditableObject simplifiedVectorPath(const EditableObject &object, qreal tolerance,
                                    const QPainterPath &region = {});
// Correction brushes affect the shared source path when they touch any of its
// captured symmetry copies. All distances and regions are in artwork pixels.
bool vectorCorrectionIntersects(const EditableObject &object, const QPainterPath &region);
qreal vectorNodeWidthFactor(const EditableObject &object, int index);
QPointF vectorNodeNormal(const EditableObject &object, int index);
QPointF vectorWidthHandle(const EditableObject &object, int index);
int insertVectorNode(EditableObject &object, QPointF artworkPoint, qreal tolerance);
void resampleVectorWidthNodes(EditableObject &object, qreal artworkSpacing, int limit = 4096);
bool brushVectorWidths(EditableObject &object, QPointF from, QPointF to, qreal radius,
                       qreal strength, bool thicken);
QVector<QPointF> vectorTransformHandles(const EditableObject &object);
QTransform vectorHandleTransform(const EditableObject &object, int handle, QPointF start,
                                 QPointF current, bool constrain);
}
