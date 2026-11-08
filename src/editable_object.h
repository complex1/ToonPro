#pragma once
#include "brush_settings.h"
#include <QByteArray>
#include <QColor>
#include <QJsonArray>
#include <QPainterPath>
#include <QTransform>
#include <QSizeF>
#include <QVector>
#include <optional>

namespace toon {
enum class LayerKind { Raster, Vector, Text, Scene3D, Symbol, Reference3D };
enum class ObjectKind { Path, Text };
struct BezierNode {
    QPointF anchor;
    QPointF inControl;
    QPointF outControl;
    static BezierNode corner(QPointF p) { return {p, p, p}; }
    bool operator==(const BezierNode &) const = default;
};
// Source dabs retain the brush engine's resolved tablet input and random sequence.
// position is normalized distance along the original centerline, independent of
// node count, so simplifying or moving handles does not discard brush character.
struct VectorBrushSample {
    quint64 dabIndex = 0;
    QPointF point;
    BrushInput input;
    qreal direction = 0;
    qreal initialDirection = 0;
    qreal position = 0;
    bool operator==(const VectorBrushSample &) const = default;
};
struct VectorBrushStroke {
    BrushSettings settings;
    quint32 seed = 1;
    QVector<VectorBrushSample> samples;
    QByteArray sourceGeometry;
    QSizeF canvasSize;
    QPainterPath selection;
    bool selectionActive = false;
    qreal selectionFeather = 0;
    bool alphaLocked = false;
    bool operator==(const VectorBrushStroke &) const = default;
};
struct EditableObject {
    QString id;
    ObjectKind kind = ObjectKind::Path;
    QVector<BezierNode> nodes;
    bool closed = false;
    QColor stroke = Qt::black;
    QColor fill; // Invalid color means no fill/stroke.
    qreal strokeWidth = 3;
    qreal opacity = 1;
    QVector<qreal> widthFactors; // One multiplier per node; empty means uniform.
    std::optional<VectorBrushStroke> brushStroke;
    QString text;
    QString fontFamily = "Sans Serif";
    qreal fontSize = 32; // Artwork pixels, independent of screen DPI.
    bool bold = false;
    bool italic = false;
    Qt::Alignment alignment = Qt::AlignLeft;
    qreal lineSpacing = 1.2;
    qreal textWidth = 0; // Zero means no automatic wrapping.
    QPointF position;
    QTransform transform;
    static EditableObject create(ObjectKind kind = ObjectKind::Path);
    bool operator==(const EditableObject &) const = default;
};
struct RasterLayer;
QString layerKindKey(LayerKind kind);
std::optional<LayerKind> layerKindFromKey(const QString &key);
QString validateEditableObject(const EditableObject &object);
QPainterPath editableObjectPath(const EditableObject &object); // In artwork coordinates.
QRectF editableObjectBounds(const EditableObject &object);
QJsonArray editableObjectsToJson(const QVector<EditableObject> &objects);
bool editableObjectsFromJson(const QJsonArray &json, QVector<EditableObject> *objects, QString *error);
bool layerHasEditableObjects(const RasterLayer &layer);
void transformLayerObjects(RasterLayer &layer, const QTransform &transform);
// Tiles are derived from source objects. These functions replace them atomically.
bool rebuildObjectLayer(RasterLayer &layer, QRectF bounds, int maxTiles, QString *error = nullptr);
// Discard bounded derived render caches, e.g. before a cold-render comparison.
void clearObjectLayerCaches();
bool transformEditableLayer(RasterLayer &layer, const QTransform &transform, QRectF bounds,
                            int maxTiles, QString *error = nullptr);
} // namespace toon
