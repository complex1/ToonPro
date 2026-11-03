#pragma once
#include <QImage>
#include <QMap>
#include <QPainter>
#include <QRectF>
#include <QString>
#include <optional>
#include "brush_settings.h"
#include "editable_object.h"
#include "scene3d.h"
#include "layer_effects.h"
#include "symbol_types.h"

namespace toon {
constexpr int tileSize = 256;
constexpr int maxProjectTiles = 1024;
using TileKey = QPair<int, int>;
enum class LayerBlendMode {
    Normal, Multiply, Screen, Overlay, Darken, Lighten, ColorDodge, ColorBurn,
    HardLight, SoftLight, Difference, Exclusion
};
QString layerBlendModeName(LayerBlendMode mode);
QString layerBlendModeKey(LayerBlendMode mode);
std::optional<LayerBlendMode> layerBlendModeFromKey(const QString &key);
QPainter::CompositionMode layerCompositionMode(LayerBlendMode mode);
struct RasterLayer {
    QString id;
    QString name = "Drawing 1";
    LayerKind kind = LayerKind::Raster;
    QVector<EditableObject> objects;
    std::optional<Scene3D> scene3d;
    std::optional<SymbolInstance> symbol;
    bool visible = true;
    bool locked = false;
    bool annotation = false;
    // Siblings are ordered bottom-to-top by their position in the shot's flat list.
    QString parentId;
    bool group = false;
    bool clipped = false;
    bool alphaLocked = false;
    bool positionLocked = false;
    LayerBlendMode blendMode = LayerBlendMode::Normal;
    QColor rowColor;
    qreal opacity = 1;
    QMap<TileKey, QImage> tiles;
    LayerEffects effects;
    std::optional<LayerMask> mask;
    bool adjustment = false;
    // Evaluated animation only; never persisted in canonical artwork.
    QTransform renderTransform;
    static RasterLayer create(const QString &name = "Drawing 1");
};
struct RasterLayerPreview {
    QString layerId;
    RasterLayer stationary;
    RasterLayer moving;
    QTransform transform;
    QVector<RasterLayerPreview> additional;
    QRectF warpBounds;
    QVector<QPointF> warpNodes; // Nine row-major control points; empty for a matrix transform.
    // Set only by document transform preparation: the unsplit document pixels
    // are exact while this preview's transform is unchanged. Custom previews
    // remain opt-out, since they may carry content different from the document.
    bool sourceUnchangedAtIdentity = false;
};
// Group ancestry is bounded by project validation (at most eight group levels).
bool layerIsDescendant(const QVector<RasterLayer> &layers, int index, const QString &ancestorId);
bool layerCanPaint(const QVector<RasterLayer> &layers, int index);
bool layerCanEdit(const QVector<RasterLayer> &layers, int index);
bool layerPositionLocked(const QVector<RasterLayer> &layers, int index);
bool layerIsAnnotation(const QVector<RasterLayer> &layers, int index);
// Transparent, content-fitted preview of this layer's own pixels. No other layers,
// display opacity or blend mode are baked into the thumbnail.
QImage renderLayerThumbnail(const RasterLayer &layer, QRectF artworkBounds, QSize size);
// Prepare once per stroke. Pressure is applied later when drawing this cached stamp.
QImage prepareBrushStamp(const BrushSettings &brush);
void paintLayers(QPainter &painter, const QVector<RasterLayer> &layers, bool annotations,
                 const RasterLayerPreview *preview = nullptr, bool smoothDisplay = false, QSize sceneFrameSize = {});
// Draw a layer's raw pixels without its opacity/blend settings. Display smoothing
// samples neighboring tiles together; it never changes stored pixels.
void paintRasterLayerPixels(QPainter &painter, const RasterLayer &layer, bool smoothDisplay = false);
// Dabs are clipped in output-relative coordinates, including negative overscan.
bool paintDab(RasterLayer &layer, QPointF point, qreal diameter, QColor color,
              qreal opacity, bool eraser, QRectF bounds, int availableTiles,
              const QImage &stamp = {}, QSizeF tipSize = {});
} // namespace toon
