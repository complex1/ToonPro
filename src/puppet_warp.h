#pragma once
#include <QPointF>
#include <QRectF>
#include <QString>
#include <QVector>
#include <array>

namespace toon {
using WarpTriangle = std::array<int,3>;
struct PuppetPin {
    QPointF source, target;
    bool operator==(const PuppetPin &) const = default;
};
struct PuppetMesh {
    QVector<QPointF> source, target;
    QVector<WarpTriangle> triangles;
    QVector<int> pinVertices;
};
constexpr int minPuppetDensity=4, maxPuppetDensity=32, maxPuppetPins=64;
// Rigid moving least squares: pins interpolate exactly and unchanged pins hold
// their positions. A single pin translates the artwork.
QPointF deformPuppetPoint(QPointF point,const QVector<PuppetPin> &pins);
// Density is the number of cells on the longer side. Pins are inserted as
// vertices, including on shared edges, so raster interpolation honors them.
bool buildPuppetMesh(QRectF bounds,int density,const QVector<PuppetPin> &pins,
                     PuppetMesh *result,QString *error=nullptr);
bool inversePuppetPoint(const PuppetMesh &mesh,QPointF target,QPointF *source);
bool puppetMeshIdentity(const QVector<PuppetPin> &pins);
}
