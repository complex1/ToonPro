#include "editable_object.h"
#include "raster.h"
#include "vector_brush.h"
#include "vector_brush_storage.h"
#include <QFont>
#include <QCache>
#include <QGlyphRun>
#include <QJsonObject>
#include <QPainter>
#include <QPainterPathStroker>
#include <QSet>
#include <QTextLayout>
#include <QUuid>
#include <cmath>
#include <algorithm>

namespace toon {
namespace {
struct ObjectPrefix {
    QVector<EditableObject> objects;
    QMap<TileKey,QImage> tiles;
    QRectF bounds;
};
// Offline export and interactive editing must never mutate one shared cache.
thread_local QCache<QString,ObjectPrefix> prefixCache(64*1024*1024);
bool finitePoint(QPointF p) { return std::isfinite(p.x()) && std::isfinite(p.y()) && qAbs(p.x()) <= 32768 && qAbs(p.y()) <= 32768; }
QJsonArray pointJson(QPointF p) { return {p.x(), p.y()}; }
bool readPoint(const QJsonValue &value, QPointF *point) {
    const auto a = value.toArray();
    if (a.size() != 2 || !a[0].isDouble() || !a[1].isDouble()) return false;
    *point = QPointF(a[0].toDouble(), a[1].toDouble()); return finitePoint(*point);
}
bool readNumber(const QJsonObject &o, const char *key, qreal *number) {
    if (!o.value(QLatin1String(key)).isDouble()) return false;
    *number = o.value(QLatin1String(key)).toDouble(); return std::isfinite(*number);
}
QPainterPath localPath(const EditableObject &o) {
    QPainterPath path;
    if (o.kind == ObjectKind::Path) {
        if (o.nodes.isEmpty()) return path;
        path.moveTo(o.nodes.first().anchor);
        for (int i = 1; i < o.nodes.size(); ++i)
            path.cubicTo(o.nodes[i - 1].outControl, o.nodes[i].inControl, o.nodes[i].anchor);
        if (o.closed && o.nodes.size() > 1) {
            path.cubicTo(o.nodes.last().outControl, o.nodes.first().inControl, o.nodes.first().anchor);
            path.closeSubpath();
        }
        return path;
    }
    QFont font(o.fontFamily);
    font.setPixelSize(qMax(1, qRound(o.fontSize)));
    font.setBold(o.bold); font.setItalic(o.italic);
    qreal y = 0;
    const auto paragraphs = o.text.split('\n', Qt::KeepEmptyParts);
    for (const auto &paragraph : paragraphs) {
        QTextLayout layout(paragraph, font);
        QTextOption option;
        option.setWrapMode(o.textWidth > 0 ? QTextOption::WrapAtWordBoundaryOrAnywhere : QTextOption::NoWrap);
        option.setAlignment(o.alignment);
        layout.setTextOption(option);
        layout.beginLayout();
        int lines = 0;
        while (true) {
            auto line = layout.createLine();
            if (!line.isValid()) break;
            line.setLineWidth(o.textWidth > 0 ? o.textWidth : 32768);
            // No-wrap alignment is relative to the supplied text anchor.
            qreal x = 0;
            if (o.textWidth == 0) {
                if (o.alignment == Qt::AlignHCenter) x = -line.naturalTextWidth() / 2;
                else if (o.alignment == Qt::AlignRight) x = -line.naturalTextWidth();
                line.setLineWidth(line.naturalTextWidth());
            }
            line.setPosition(QPointF(x, y));
            y += qMax(1.0, line.height() * o.lineSpacing); ++lines;
        }
        layout.endLayout();
        for (const auto &run : layout.glyphRuns()) {
            const auto glyphs = run.glyphIndexes();
            const auto positions = run.positions();
            const auto rawFont = run.rawFont();
            for (int i = 0; i < glyphs.size(); ++i) {
                auto glyph = rawFont.pathForGlyph(glyphs[i]);
                glyph.translate(o.position + positions[i]);
                path.addPath(glyph);
            }
        }
        if (!lines) y += o.fontSize * o.lineSpacing;
    }
    path.setFillRule(Qt::WindingFill);
    return path;
}
QPainterPath paintedPath(const EditableObject &o) {
    const auto local = localPath(o);
    QPainterPath area;
    if (o.fill.isValid() && o.fill.alpha() > 0) area = local;
    if (o.stroke.isValid() && o.stroke.alpha() > 0 && o.strokeWidth > 0) {
        auto copy=o; copy.transform={};
        area.addPath(editableStrokeOutline(copy));
    }
    return o.transform.map(area);
}
bool hasPixels(const QImage &image) {
    for (int y = 0; y < image.height(); ++y) {
        const auto *row = reinterpret_cast<const QRgb *>(image.constScanLine(y));
        for (int x = 0; x < image.width(); ++x) if (qAlpha(row[x])) return true;
    }
    return false;
}
}
EditableObject EditableObject::create(ObjectKind kind) {
    EditableObject result; result.kind = kind;
    result.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    if (kind == ObjectKind::Text) { result.fill = Qt::black; result.stroke = QColor(); }
    return result;
}
QString layerKindKey(LayerKind kind) {
    switch (kind) { case LayerKind::Raster: return "raster"; case LayerKind::Vector: return "vector"; case LayerKind::Text: return "text"; case LayerKind::Scene3D: return "scene3d"; case LayerKind::Symbol: return "symbol"; case LayerKind::Reference3D: return "reference3d"; }
    return {};
}
std::optional<LayerKind> layerKindFromKey(const QString &key) {
    if (key == "raster") return LayerKind::Raster;
    if (key == "vector") return LayerKind::Vector;
    if (key == "text") return LayerKind::Text;
    if (key == "scene3d") return LayerKind::Scene3D;
    if (key == "symbol") return LayerKind::Symbol;
    if (key == "reference3d") return LayerKind::Reference3D;
    return {};
}
QString validateEditableObject(const EditableObject &o) {
    if (QUuid(o.id).isNull()) return "Invalid object identifier.";
    if (o.kind != ObjectKind::Path && o.kind != ObjectKind::Text) return "Unknown object type.";
    if (!std::isfinite(o.opacity) || o.opacity<0 || o.opacity>1) return "Object opacity must be between 0 and 1.";
    if (!o.widthFactors.isEmpty() && (o.kind!=ObjectKind::Path || o.widthFactors.size()!=o.nodes.size())) return "Width factors must match the path nodes.";
    for (qreal factor:o.widthFactors) if(!std::isfinite(factor)||factor<0||factor>16) return "Path width factors must be between 0 and 16.";
    if(o.brushStroke) {
        if(o.kind!=ObjectKind::Path) return "Only paths can contain editable brush strokes.";
        QString error; if(!validateVectorBrushStroke(*o.brushStroke,&error)) return error;
    }
    if (!std::isfinite(o.strokeWidth) || o.strokeWidth < 0 || o.strokeWidth > 2048) return "Stroke width must be between 0 and 2048 pixels.";
    const qreal matrix[] = {o.transform.m11(),o.transform.m12(),o.transform.m13(),o.transform.m21(),o.transform.m22(),o.transform.m23(),o.transform.m31(),o.transform.m32(),o.transform.m33()};
    for (qreal value : matrix) if (!std::isfinite(value) || qAbs(value) > 1000000000) return "Invalid object transform.";
    if (!o.transform.isInvertible()) return "Object transform must remain invertible.";
    if (o.kind == ObjectKind::Path) {
        if (o.nodes.size() < 2 || o.nodes.size() > 16384) return "Paths require between 2 and 16,384 nodes.";
        for (const auto &node : o.nodes) if (!finitePoint(node.anchor) || !finitePoint(node.inControl) || !finitePoint(node.outControl)) return "Path coordinates exceed the artwork range.";
    } else {
        if (o.text.isEmpty() || o.text.size() > 16384 || o.fontFamily.trimmed().isEmpty() || o.fontFamily.size() > 200
            || !std::isfinite(o.fontSize) || o.fontSize < 1 || o.fontSize > 2048 || !finitePoint(o.position)
            || !std::isfinite(o.lineSpacing) || o.lineSpacing < .5 || o.lineSpacing > 5
            || !std::isfinite(o.textWidth) || o.textWidth < 0 || o.textWidth > 16384
            || (o.alignment != Qt::AlignLeft && o.alignment != Qt::AlignHCenter && o.alignment != Qt::AlignRight))
            return "Invalid text, font or paragraph settings.";
    }
    const auto bounds = editableObjectBounds(o);
    if (!finitePoint(bounds.topLeft()) || !finitePoint(bounds.bottomRight())) return "Transformed object exceeds the supported artwork range.";
    return {};
}
QPainterPath editableObjectPath(const EditableObject &o) { return o.transform.map(localPath(o)); }
QRectF editableObjectBounds(const EditableObject &o) { return o.brushStroke ? vectorBrushBounds(o) : paintedPath(o).boundingRect(); }
QJsonArray editableObjectsToJson(const QVector<EditableObject> &objects) {
    QJsonArray array;
    for (const auto &o : objects) {
        QJsonArray nodes;
        for (const auto &node : o.nodes) nodes.append(QJsonArray{pointJson(node.anchor), pointJson(node.inControl), pointJson(node.outControl)});
        QJsonArray transform{o.transform.m11(),o.transform.m12(),o.transform.m13(),o.transform.m21(),o.transform.m22(),o.transform.m23(),o.transform.m31(),o.transform.m32(),o.transform.m33()};
        QJsonArray widths; for(qreal factor:o.widthFactors)widths.append(factor);
        QJsonObject json{{"id",o.id},{"kind",o.kind == ObjectKind::Path ? "path" : "text"},{"nodes",nodes},{"closed",o.closed},
            {"stroke",o.stroke.isValid() ? o.stroke.name(QColor::HexArgb) : QString()},{"fill",o.fill.isValid() ? o.fill.name(QColor::HexArgb) : QString()},
            {"strokeWidth",o.strokeWidth},{"opacity",o.opacity},{"widthFactors",widths},{"text",o.text},{"fontFamily",o.fontFamily},{"fontSize",o.fontSize},{"bold",o.bold},{"italic",o.italic},
            {"alignment",int(o.alignment)},{"lineSpacing",o.lineSpacing},{"textWidth",o.textWidth},{"position",pointJson(o.position)},{"transform",transform}};
        if(o.brushStroke)json["brushStroke"]=vectorBrushStrokeToJson(*o.brushStroke);
        array.append(json);
    }
    return array;
}
bool editableObjectsFromJson(const QJsonArray &json, QVector<EditableObject> *objects, QString *error) {
    auto fail = [error](const QString &message) { if (error) *error = message; return false; };
    if (json.size() > 1024) return fail("A layer supports up to 1024 editable objects.");
    QVector<EditableObject> result;
    for (const auto &value : json) {
        if (!value.isObject()) return fail("Invalid editable object.");
        const auto j = value.toObject(); EditableObject o;
        if (!j.value("id").isString() || !j.value("kind").isString() || !j.value("nodes").isArray()
            || !j.value("closed").isBool() || !j.value("stroke").isString() || !j.value("fill").isString()
            || !j.value("text").isString() || !j.value("fontFamily").isString() || !j.value("bold").isBool()
            || !j.value("italic").isBool() || !j.value("alignment").isDouble()) return fail("Invalid editable object settings.");
        o.id = j.value("id").toString();
        if (j.value("kind") == "path") o.kind = ObjectKind::Path;
        else if (j.value("kind") == "text") o.kind = ObjectKind::Text;
        else return fail("Unsupported editable object type.");
        const auto nodes = j.value("nodes").toArray();
        if (nodes.size() > 16384) return fail("Path has too many nodes.");
        for (const auto &value : nodes) {
            const auto n = value.toArray(); BezierNode node;
            if (n.size() != 3 || !readPoint(n[0], &node.anchor) || !readPoint(n[1], &node.inControl) || !readPoint(n[2], &node.outControl)) return fail("Invalid path node.");
            o.nodes.append(node);
        }
        if(j.contains("opacity")&&!readNumber(j,"opacity",&o.opacity))return fail("Invalid object opacity.");
        if(j.contains("widthFactors")){
            if(!j["widthFactors"].isArray())return fail("Invalid path width factors.");
            for(const auto &factor:j["widthFactors"].toArray()){
                if(!factor.isDouble())return fail("Invalid path width factor.");
                o.widthFactors.append(factor.toDouble());
            }
        }
        if(j.contains("brushStroke")){
            if(!j["brushStroke"].isObject())return fail("Invalid brush stroke metadata.");
            VectorBrushStroke stroke; if(!vectorBrushStrokeFromJson(j["brushStroke"].toObject(),&stroke,error))return false;
            o.brushStroke=std::move(stroke);
        }
        o.closed = j.value("closed").toBool();
        for (const auto &key : {"stroke", "fill"}) {
            const auto text = j.value(QLatin1String(key)).toString(); QColor color;
            if (!text.isEmpty()) {
                color = QColor(text);
                if (text.size() != 9 || !text.startsWith('#') || !color.isValid() || color.name(QColor::HexArgb) != text.toLower()) return fail("Invalid object color.");
            }
            if (QString::fromLatin1(key) == "stroke") o.stroke = color; else o.fill = color;
        }
        if (!readNumber(j,"strokeWidth",&o.strokeWidth) || !readNumber(j,"fontSize",&o.fontSize)
            || !readNumber(j,"lineSpacing",&o.lineSpacing) || !readNumber(j,"textWidth",&o.textWidth)
            || !readPoint(j.value("position"),&o.position)) return fail("Invalid object dimensions.");
        o.text = j.value("text").toString(); o.fontFamily = j.value("fontFamily").toString();
        o.bold = j.value("bold").toBool(); o.italic = j.value("italic").toBool();
        o.alignment = Qt::Alignment(j.value("alignment").toInt(-1));
        const auto matrix = j.value("transform").toArray();
        if (matrix.size() != 9) return fail("Invalid object transform.");
        for (const auto &v : matrix) if (!v.isDouble() || !std::isfinite(v.toDouble())) return fail("Invalid object transform.");
        o.transform = QTransform(matrix[0].toDouble(),matrix[1].toDouble(),matrix[2].toDouble(),matrix[3].toDouble(),matrix[4].toDouble(),matrix[5].toDouble(),matrix[6].toDouble(),matrix[7].toDouble(),matrix[8].toDouble());
        const auto validation = validateEditableObject(o); if (!validation.isEmpty()) return fail(validation);
        result.append(o);
    }
    *objects = result; return true;
}
bool layerHasEditableObjects(const RasterLayer &layer) { return layer.kind == LayerKind::Vector || layer.kind == LayerKind::Text; }
void transformLayerObjects(RasterLayer &layer, const QTransform &transform) {
    for (auto &object : layer.objects) object.transform *= transform;
}
bool rebuildObjectLayer(RasterLayer &layer, QRectF bounds, int maxTiles, QString *error) {
    auto fail = [error](const QString &message) { if (error) *error = message; return false; };
    if (!layerHasEditableObjects(layer) || layer.group) return fail("Choose a vector or text layer.");
    if (maxTiles < 0) return fail("Project exceeds the 1024-tile memory limit.");
    auto result=layer; result.tiles.clear();
    qsizetype start=0;
    const auto prefix=prefixCache.object(layer.id);
    if(prefix&&prefix->bounds==bounds&&prefix->objects.size()<layer.objects.size()
        &&std::equal(prefix->objects.cbegin(),prefix->objects.cend(),layer.objects.cbegin())){
        result.tiles=prefix->tiles; start=prefix->objects.size();
        if(result.tiles.size()>maxTiles)return fail("Project exceeds the tile memory limit.");
    }
    // Stroke blend modes and alpha locks depend on earlier objects in this
    // layer, so replay in object order into an otherwise ordinary raster layer.
    for(qsizetype objectIndex=start;objectIndex<layer.objects.size();++objectIndex){
        if(objectIndex==layer.objects.size()-1){
            qint64 bytes=4096;for(const auto &tile:result.tiles)bytes+=tile.sizeInBytes();
            for(qsizetype cached=0;cached<objectIndex;++cached){
                const auto &source=layer.objects[cached];
                bytes+=sizeof(EditableObject)+source.nodes.size()*sizeof(BezierNode)+source.widthFactors.size()*sizeof(qreal)
                    +source.text.size()*sizeof(QChar)+source.fontFamily.size()*sizeof(QChar);
                if(source.brushStroke){
                    const auto &brush=*source.brushStroke;
                    bytes+=sizeof(VectorBrushStroke)+brush.samples.size()*sizeof(VectorBrushSample)
                        +brush.selection.elementCount()*sizeof(QPainterPath::Element)+brush.sourceGeometry.size()
                        +brush.settings.tipMask.sizeInBytes()+brush.settings.colorTip.sizeInBytes()+brush.settings.texture.pattern.sizeInBytes()+brush.settings.dual.tipMask.sizeInBytes();
                }
            }
            // Retain only the last prefix per layer, bounded across documents.
            // Editing the newest stroke then replays that stroke alone.
            if(bytes<=prefixCache.maxCost())prefixCache.insert(layer.id,new ObjectPrefix{layer.objects.mid(0,objectIndex),result.tiles,bounds},qMax(1,int(bytes)));
        }
        const auto &object=layer.objects[objectIndex];
        const auto validation=validateEditableObject(object); if(!validation.isEmpty())return fail(validation);
        if(object.brushStroke){
            if(!replayVectorBrushStroke(result,object,bounds,maxTiles,error))return false;
            continue;
        }
        const auto area=editableObjectBounds(object).adjusted(-1,-1,1,1).intersected(bounds);
        if(area.isEmpty()||object.opacity<=0)continue;
        const int left=int(std::floor(area.left()/tileSize)),right=int(std::floor(area.right()/tileSize));
        const int top=int(std::floor(area.top()/tileSize)),bottom=int(std::floor(area.bottom()/tileSize));
        if(qint64(right-left+1)*(bottom-top+1)>maxProjectTiles)return fail("Object exceeds the tile memory limit.");
        const auto path=localPath(object);
        const bool variable=std::any_of(object.widthFactors.cbegin(),object.widthFactors.cend(),[](qreal factor){return factor!=1;});
        const auto outline=variable?editableStrokeOutline(object):QPainterPath();
        for(int y=top;y<=bottom;++y)for(int x=left;x<=right;++x){
            const TileKey key{x,y}; auto tile=result.tiles.value(key);
            if(tile.isNull()){
                tile=QImage(tileSize,tileSize,QImage::Format_ARGB32_Premultiplied);
                if(tile.isNull())return fail("Could not allocate object artwork.");
                tile.fill(Qt::transparent);
            }
            QPainter painter(&tile); painter.setRenderHint(QPainter::Antialiasing);
            painter.translate(-x*tileSize,-y*tileSize); painter.setClipRect(bounds); painter.setOpacity(object.opacity);
            painter.save(); painter.setTransform(object.transform,true);
            QPen pen(object.stroke,object.strokeWidth,Qt::SolidLine,Qt::RoundCap,Qt::RoundJoin);
            painter.setPen(!variable&&object.stroke.isValid()&&object.strokeWidth>0?pen:QPen(Qt::NoPen));
            painter.setBrush(object.fill.isValid()?QBrush(object.fill):QBrush(Qt::NoBrush));
            painter.drawPath(path); painter.restore();
            if(variable&&object.stroke.isValid())painter.fillPath(outline,object.stroke);
            painter.end();
            if(hasPixels(tile)){
                if(!result.tiles.contains(key)&&result.tiles.size()>=maxTiles)return fail("Project exceeds the 1024-tile memory limit.");
                result.tiles.insert(key,tile);
            }
        }
    }
    layer.tiles=result.tiles; return true;
}
void clearObjectLayerCaches() {
    prefixCache.clear();
    clearVectorBrushReplayCache();
}
bool transformEditableLayer(RasterLayer &layer, const QTransform &transform, QRectF bounds, int maxTiles, QString *error) {
    auto copy = layer; transformLayerObjects(copy,transform);
    if (!rebuildObjectLayer(copy,bounds,maxTiles,error)) return false;
    layer = copy; return true;
}
} // namespace toon
