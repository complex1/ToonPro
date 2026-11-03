#pragma once
#include "raster.h"
#include <QTransform>
#include <QPainterPath>
#include <QVector>

namespace toon {
class RasterSelectionCache;
// All angles are degrees in artwork coordinates; tilt is in degrees from upright.
BrushInput resolvedBrushInput(const BrushSettings &brush, BrushInput input);
qreal brushRandom(quint32 seed, quint64 dab, quint32 channel);
int brushTipFrameIndex(const BrushSettings &brush, const BrushInput &input, qreal direction,
                       quint64 dab, quint32 seed);
QImage brushTipFrameImage(const BrushSettings &brush, int frame);
bool setBrushTipImages(BrushSettings &brush, const QVector<QImage> &images,
                       const QVector<QPointF> &anchors = {}, QString *error = nullptr);
qreal brushControlValue(BrushControl control, const BrushInput &input, quint64 dab,
                        int fade, qreal direction = 0, qreal initialDirection = 0);
qreal brushResponseValue(const BrushDynamic &dynamic, qreal input);
qreal brushDynamicValue(const BrushDynamic &dynamic, const BrushInput &input,
                        quint64 dab, quint32 seed, quint32 channel,
                        qreal direction = 0, qreal initialDirection = 0);
// Resolve per-stroke color once before count/scatter copies derive their seeds.
// Colored tips retain an owned RGBA palette; per-tip dynamics remain unresolved.
void resolveBrushStrokeColor(BrushSettings &brush, const BrushInput &input, quint32 seed);
QImage brushColorTipDynamics(const QImage &source, const BrushSettings &settings, quint64 repeatIndex, quint32 seed);
QImage brushTipMask(const BrushSettings &brush, const BrushInput &input,
                   qreal direction = 0, qreal initialDirection = 0,
                   quint64 dabIndex = 0, quint32 seed = 1, QPointF position = {});
struct BrushDab {
    QImage mask;
    // Transformed premultiplied stamp colors, with the same coverage as the silhouette.
    QImage colorTip;
    // Continuous texture is sampled after transforming the untextured silhouette.
    QImage untexturedMask;
    BrushTexture canvasTexture;
    BrushSurfacePattern surface;
    qreal canvasTextureDepth = 0;
    QColor color;
    qreal flow = 1;
    qreal opacity = 1;
    QPointF offset;
};
// Shared by painting and alpha-aware vector picking. The pattern is already
// Grayscale8; sample is a pixel center in artwork coordinates.
qreal brushCanvasTextureCoverage(qreal coverage, const BrushTexture &texture,
                                 const QImage &pattern, qreal depth, QPointF sample);
int brushDabCount(const BrushSettings &brush, const BrushInput &input, quint64 dabIndex, quint32 seed);
enum class BrushDabPurpose { Preview, Painting };
// Painting/picking sample continuous texture after the tip is transformed into
// artwork coordinates. They need its untextured silhouette, not a second local
// texture preview. Preview remains the default for mask/cursor consumers.
BrushDab evaluateBrushDab(const BrushSettings &brush, const BrushInput &input, QPointF position,
                         qreal direction, qreal initialDirection, quint64 dabIndex, quint32 seed,
                         const QTransform &textureTransform = {}, BrushDabPurpose purpose = BrushDabPurpose::Preview,
                         int scatterCopy = -1, quint32 scatterSeed = 0);
struct BrushSymmetryInstance { QPointF point; QTransform transform; };
QPainterPath brushSymmetryGuide(const BrushSymmetry &symmetry, QSizeF artwork);
QVector<BrushSymmetryInstance> brushSymmetryInstances(const BrushSymmetry &symmetry,
                                                     QPointF point, QSizeF artwork);
// Accumulation remains separate from the original layer to cap one stroke's opacity.
// Only the changed pixel rectangle is recomposited, never the complete layer.
bool applyBrushDab(RasterLayer &layer, RasterLayer &accumulation, const RasterLayer &original,
                   const BrushDab &dab, QPointF point, const QTransform &tipTransform,
                   BrushBlend blend, bool eraser, QRectF bounds, int availableTiles,
                   quint32 seed, const QPainterPath *selection = nullptr,
                   RasterSelectionCache *selectionCache = nullptr);
// Composite a completed, backdrop-independent stroke once per pixel. Paint
// tiles contain the final premultiplied 8-bit value of the 16-bit accumulator.
bool compositeBrushAccumulation(RasterLayer &layer, const QMap<TileKey, QImage> &paint,
                                BrushBlend blend, bool alphaLocked, int availableTiles,
                                quint32 seed);
QImage brushStrokePreview(const BrushSettings &brush, QSize size = QSize(200, 60),
                          QColor background = QColor(39, 43, 51));
} // namespace toon
