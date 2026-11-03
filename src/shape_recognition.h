#pragma once

#include <QPainterPath>
#include <QString>
#include <QVector>

namespace toon {

enum class RecognizedShapeKind { Line, Arc, Circle, Ellipse, Rectangle, Triangle };

struct RecognizedShape {
    RecognizedShapeKind kind;
    QString name;
    QPainterPath path;
    // Dimensionless fit error; lower values are better. Candidates are sorted.
    qreal score = 0;
};

// Fits a small set of clean, plausible shapes to one continuous stroke. Input
// density and drawing speed do not affect the fit. Invalid/degenerate strokes
// and scribbles return no candidates. Geometry is returned in artwork units.
QVector<RecognizedShape> recognizeStrokeShapes(const QVector<QPointF> &points);

} // namespace toon
