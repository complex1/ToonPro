#include "advanced_transform.h"
#include "keyframe_render.h"
#include "animation.h"
#include "editable_object.h"
#include "selection_geometry.h"
#include <QSet>
#include <array>
#include <cmath>
#include <new>

namespace toon {
namespace {
struct Triangle { QPolygonF source, destination; QTransform transform, inverse; };
QPolygonF rectangleQuad(QRectF r) { return {r.topLeft(), r.topRight(), r.bottomRight(), r.bottomLeft()}; }
bool finite(QPointF p) { return std::isfinite(p.x()) && std::isfinite(p.y()) && std::abs(p.x()) <= 32768 && std::abs(p.y()) <= 32768; }
qreal cross(QPointF a, QPointF b) { return a.x() * b.y() - a.y() * b.x(); }
QVector<QPointF> grid(QRectF r) {
    QVector<QPointF> result;
    for (int y = 0; y < 3; ++y) for (int x = 0; x < 3; ++x)
        result.append(r.topLeft() + QPointF(r.width() * x / 2, r.height() * y / 2));
    return result;
}
bool convex(const QPolygonF &quad) {
    if (quad.size() != 4) return false;
    qreal sign = 0;
    for (int i = 0; i < 4; ++i) {
        if (!finite(quad[i])) return false;
        const qreal value = cross(quad[(i + 1) % 4] - quad[i], quad[(i + 2) % 4] - quad[(i + 1) % 4]);
        if (std::abs(value) < .0001 || (sign && sign * value < 0)) return false;
        sign = value;
    }
    return true;
}
QVector<Triangle> triangles(QRectF bounds, const QVector<QPointF> &nodes) {
    if (nodes.size() != 9) return {};
    const auto source = grid(bounds); QVector<Triangle> result;
    for (int y = 0; y < 2; ++y) for (int x = 0; x < 2; ++x) {
        const int a = y * 3 + x, b = a + 1, c = a + 4, d = a + 3;
        for (const auto &indices : {std::array<int, 3>{a, b, c}, std::array<int, 3>{a, c, d}}) {
            Triangle triangle;
            for (int i : indices) { triangle.source.append(source[i]); triangle.destination.append(nodes[i]); }
            // A triangle is mapped using its parallelogram, so the matrix is affine.
            const auto parallelogram = [](const QPolygonF &p) {
                return QPolygonF{p[0], p[1], p[1] + p[2] - p[0], p[2]};
            };
            if (!QTransform::quadToQuad(parallelogram(triangle.source), parallelogram(triangle.destination), triangle.transform)) return {};
            bool invertible; triangle.inverse = triangle.transform.inverted(&invertible);
            if (!invertible) return {};
            result.append(triangle);
        }
    }
    return result;
}
bool inTriangle(QPointF p, const QPolygonF &t) {
    const qreal a = cross(t[1] - t[0], p - t[0]), b = cross(t[2] - t[1], p - t[1]), c = cross(t[0] - t[2], p - t[2]);
    return (a >= -1e-8 && b >= -1e-8 && c >= -1e-8) || (a <= 1e-8 && b <= 1e-8 && c <= 1e-8);
}
QPainterPath warpedPath(const QPainterPath &path, QRectF bounds, const QVector<QPointF> &nodes) {
    QPainterPath result;
    for (const auto &triangle : triangles(bounds, nodes)) {
        QPainterPath clip; clip.addPolygon(triangle.source); clip.closeSubpath();
        result = result.united(triangle.transform.map(path.intersected(clip)));
    }
    return result;
}
QRectF layerBounds(const Project &project, int shotIndex, int index) {
    const auto &shot = project.shots[shotIndex]; const auto &sheet = shot.sheet;
    const qreal extra = sheet.overscan;
    QRectF bounds(-extra, -extra, project.width + 2 * extra, project.height + 2 * extra);
    if (layerIsAnnotation(shot.layers, index)) bounds.adjust(-sheet.left, -sheet.top, sheet.right, sheet.bottom);
    return bounds;
}
bool nonempty(const QImage &image) {
    for (int y = 0; y < image.height(); ++y) {
        const auto row = reinterpret_cast<const QRgb *>(image.constScanLine(y));
        for (int x = 0; x < image.width(); ++x) if (qAlpha(row[x])) return true;
    }
    return false;
}
QRgb multiply(QRgb color, int coverage) {
    return qRgba((qRed(color) * coverage + 127) / 255, (qGreen(color) * coverage + 127) / 255,
                 (qBlue(color) * coverage + 127) / 255, (qAlpha(color) * coverage + 127) / 255);
}
QRgb over(QRgb source, QRgb target) {
    const int inv = 255 - qAlpha(source);
    return qRgba(qRed(source) + (qRed(target) * inv + 127) / 255, qGreen(source) + (qGreen(target) * inv + 127) / 255,
                 qBlue(source) + (qBlue(target) * inv + 127) / 255, qAlpha(source) + (qAlpha(target) * inv + 127) / 255);
}
bool split(const RasterLayer &layer, const RasterSelection &selection, RasterLayerPreview *preview) {
    preview->layerId = layer.id; preview->stationary = layer; preview->moving = layer;
    preview->sourceUnchangedAtIdentity = true;
    preview->moving.tiles.clear();
    RasterSelectionCache cache(selection.active ? &selection.path : nullptr, selection.featherRadius);
    for (auto it = layer.tiles.cbegin(); it != layer.tiles.cend(); ++it) {
        const auto coverage = cache.tile(it.key());
        if (coverage.coverage == RasterSelectionTile::Coverage::Invalid) return false;
        if (coverage.coverage == RasterSelectionTile::Coverage::Empty) continue;
        if (coverage.coverage == RasterSelectionTile::Coverage::Full) {
            preview->moving.tiles.insert(it.key(), it.value()); preview->stationary.tiles.remove(it.key()); continue;
        }
        auto moving = it->copy(), stationary = it->copy();
        if (moving.isNull() || stationary.isNull()) return false;
        for (int y = 0; y < tileSize; ++y) {
            auto move = reinterpret_cast<QRgb *>(moving.scanLine(y)), stay = reinterpret_cast<QRgb *>(stationary.scanLine(y));
            const auto mask = coverage.mask.constScanLine(y);
            for (int x = 0; x < tileSize; ++x) { move[x] = multiply(move[x], mask[x]); stay[x] = multiply(stay[x], 255 - mask[x]); }
        }
        if (nonempty(moving)) preview->moving.tiles.insert(it.key(), moving);
        if (nonempty(stationary)) preview->stationary.tiles.insert(it.key(), stationary); else preview->stationary.tiles.remove(it.key());
    }
    return true;
}
int coordinate(int value) { return int(std::floor(value / qreal(tileSize))); }
QRgb sourcePixel(const RasterLayer &layer, int x, int y) {
    const TileKey key(coordinate(x), coordinate(y)); const auto found = layer.tiles.constFind(key);
    return found == layer.tiles.cend() ? 0 : reinterpret_cast<const QRgb *>(found->constScanLine(y - key.second * tileSize))[x - key.first * tileSize];
}
QRgb sample(const RasterLayer &layer, QPointF p) {
    const qreal x = p.x() - .5, y = p.y() - .5;
    if (!std::isfinite(x) || !std::isfinite(y) || std::abs(x) > 32769 || std::abs(y) > 32769) return 0;
    const int left = int(std::floor(x)), top = int(std::floor(y)); const qreal fx = x - left, fy = y - top;
    const std::array<QRgb, 4> colors{sourcePixel(layer, left, top), sourcePixel(layer, left + 1, top),
                                    sourcePixel(layer, left, top + 1), sourcePixel(layer, left + 1, top + 1)};
    const std::array<qreal, 4> weights{(1-fx)*(1-fy), fx*(1-fy), (1-fx)*fy, fx*fy};
    qreal r=0,g=0,b=0,a=0;
    for (int i=0; i<4; ++i) { r+=qRed(colors[i])*weights[i]; g+=qGreen(colors[i])*weights[i]; b+=qBlue(colors[i])*weights[i]; a+=qAlpha(colors[i])*weights[i]; }
    return qRgba(qRound(r),qRound(g),qRound(b),qRound(a));
}
bool rasterize(const RasterLayerPreview &preview, QRectF sourceBounds, QRectF extent, int allowance, RasterLayer *result, QString *error) {
    const auto fail = [&](const QString &message) { *error = message; return false; };
    auto candidate = preview.stationary;
    const bool warp = !preview.warpNodes.isEmpty();
    const auto pieces = warp ? triangles(preview.warpBounds, preview.warpNodes) : QVector<Triangle>();
    const auto inverse = preview.transform.inverted();
    const QRect output = (warp ? QPolygonF(preview.warpNodes).boundingRect() : preview.transform.mapRect(sourceBounds)).toAlignedRect().intersected(extent.toAlignedRect());
    if (output.isEmpty()) { *result = candidate; return true; }
    const int left = coordinate(output.left()), right = coordinate(output.right()), top = coordinate(output.top()), bottom = coordinate(output.bottom());
    if (qint64(right - left + 1) * (bottom - top + 1) > maxProjectTiles)
        return fail("The transform spans more than 1024 tiles. Move the handles closer together.");
    for (int ty=top; ty<=bottom; ++ty) for (int tx=left; tx<=right; ++tx) {
        const TileKey key(tx,ty); const QPoint origin(tx*tileSize,ty*tileSize);
        const auto part = output.intersected(QRect(origin,QSize(tileSize,tileSize)));
        QImage tile = candidate.tiles.value(key);
        bool changed = false;
        for (int y=part.top(); y<=part.bottom(); ++y) for (int x=part.left(); x<=part.right(); ++x) {
            const QPointF point(x+.5,y+.5); if (!extent.contains(point)) continue;
            QPointF source;
            if (warp) {
                bool found = false;
                for (const auto &piece : pieces) if (inTriangle(point,piece.destination)) { source = piece.inverse.map(point); found = true; break; }
                if (!found) continue;
            } else source = inverse.map(point);
            if (!sourceBounds.adjusted(-1,-1,1,1).contains(source)) continue;
            const auto color = sample(preview.moving,source); if (!qAlpha(color)) continue;
            if (tile.isNull()) { tile=QImage(tileSize,tileSize,QImage::Format_ARGB32_Premultiplied); if(tile.isNull())return fail("There is not enough memory to transform the artwork.");tile.fill(Qt::transparent); }
            auto row=reinterpret_cast<QRgb *>(tile.scanLine(y-origin.y())); row[x-origin.x()]=over(color,row[x-origin.x()]); changed=true;
        }
        if (changed) {
            if (!candidate.tiles.contains(key) && candidate.tiles.size()>=allowance) return fail("The transform exceeds the 1024-tile project limit. No artwork was changed.");
            candidate.tiles.insert(key,tile);
        }
    }
    if (candidate.tiles.size()>allowance) return fail("The transform exceeds the project tile limit.");
    *result=std::move(candidate);return true;
}
}
void paintAdvancedMovingLayer(QPainter &painter, const RasterLayerPreview &preview, bool smoothDisplay) {
    const auto draw = [&](const QTransform &matrix) {
        painter.save(); painter.setTransform(matrix,true);
        paintRasterLayerPixels(painter, preview.moving, smoothDisplay);
        painter.restore();
    };
    if (preview.warpNodes.isEmpty()) { draw(preview.transform); return; }
    for (const auto &triangle:triangles(preview.warpBounds,preview.warpNodes)) {
        painter.save();QPainterPath clip;clip.addPolygon(triangle.destination);clip.closeSubpath();
        painter.setClipPath(clip,Qt::IntersectClip); draw(triangle.transform); painter.restore();
    }
}
AdvancedTransform::AdvancedTransform(QObject *parent):QObject(parent) {}
AdvancedTransform::~AdvancedTransform() { cancel(); }
bool AdvancedTransform::fail(const QString &message) const { if (m_document) emit m_document->warning(message); return false; }
bool AdvancedTransform::begin(Document *document,const QStringList &layerIds,bool selectionOnly) {
    if (active()) cancel();
    if (!document || !document->isOpen() || document->editingGestureActive() || document->layerPropertyEditActive()) return false;
    m_document=document;m_before=document->m_project;m_selection=document->m_selection;m_selectionOnly=selectionOnly;
    m_indices.clear();m_preview={};m_bounds={};m_matrix={};m_nodes.clear();
    const auto reject=[&](const QString &message){ fail(message);cancel();return false; };
    const auto &layers=m_before.shots[m_document->activeShotIndex()].layers;
    const auto exposed = activeLayerIds(m_before.shots[m_document->activeShotIndex()], document->currentFrame());
    if (selectionOnly) {
        if (!m_selection.active || m_selection.path.isEmpty()) return reject("Select an area before transforming its boundary.");
        m_bounds=m_selection.path.boundingRect();
    } else {
        auto ids=layerIds;if(ids.isEmpty())ids.append(layers[document->m_active].id);
        for(int i=0;i<layers.size();++i){bool selected=ids.contains(layers[i].id);for(const auto&id:ids)selected|=layerIsDescendant(layers,i,id);if(!selected)continue;
            const auto reason=keyframeRasterEditReason(document,layers[i].id);if(!reason.isEmpty())return reject(reason);
            if(const auto*a=layerAnimation(m_before.shots[m_document->activeShotIndex()],layers[i].id);a&&!a->paths.isEmpty())return reject("Use Path Edit to animate this path, or disable its path animation before transforming source artwork.");}
        for (const auto &id:ids) {
            bool exists=false;for(const auto &layer:layers)exists|=layer.id==id;
            if(!exists)return reject("A selected layer no longer exists.");
        }
        for (int i=0;i<layers.size();++i) {
            if (layers[i].group || !exposed.contains(layers[i].id)) continue;
            bool wanted=ids.contains(layers[i].id);
            for(const auto &id:ids)wanted|=layerIsDescendant(layers,i,id);
            if(!wanted)continue;
            if (layers[i].kind == LayerKind::Symbol)
                return reject("Use the Symbols panel to transform the linked instance, or break it apart first.");
            if (layers[i].kind == LayerKind::Scene3D)
                return reject("Use the 3D Scene panel to transform 3D objects. Exclude the 3D reference layer from this selection.");
            if(!layerCanEdit(layers,i) || layerPositionLocked(layers,i) || layers[i].alphaLocked)
                return reject("Unlock and show every selected layer and folder, including position and alpha locks, before transforming.");
            if (layerHasEditableObjects(layers[i]) && m_selection.active)
                return reject("Clear the pixel selection or rasterize the vector/text layer before transforming selected pixels.");
            RasterLayerPreview prepared;
            if(!split(layers[i],m_selection,&prepared))return reject("There is not enough memory to prepare the transform.");
            const auto area=selectedPixelBounds(prepared.moving);
            if(area.isEmpty())continue;
            m_indices.append(i);m_bounds|=area;
            if(m_indices.size()==1)m_preview=std::move(prepared);else m_preview.additional.append(std::move(prepared));
        }
        if(m_indices.isEmpty())return reject("The selected layers contain no editable pixels.");
        // A mesh must also cover empty portions of the selection boundary; they
        // should move with the selected artwork instead of disappearing.
        if (m_selection.active) m_bounds |= m_selection.path.boundingRect();
    }
    if(m_bounds.width()<=0 || m_bounds.height()<=0)return reject("The transform area is empty.");
    document->m_externalEditActive=true;
    m_documentConnection=connect(document,&Document::changed,this,[this]{if(m_document && !m_document->committing())cancel();});
    emit changed();return true;
}
QPolygonF AdvancedTransform::quad() const { return m_nodes.isEmpty()?m_matrix.map(rectangleQuad(m_bounds)):QPolygonF{m_nodes[0],m_nodes[2],m_nodes[8],m_nodes[6]}; }
QVector<QPointF> AdvancedTransform::controlPoints() const { if(!m_nodes.isEmpty())return m_nodes;auto points=grid(m_bounds);for(auto &p:points)p=m_matrix.map(p);return points; }
const RasterLayerPreview *AdvancedTransform::preview() const { return active() && !m_selectionOnly?&m_preview:nullptr; }
QPainterPath AdvancedTransform::selectionPath() const {
    if(!active() || !m_selection.active)return {};
    return m_nodes.isEmpty()?m_matrix.map(m_selection.path):warpedPath(m_selection.path,m_bounds,m_nodes);
}
bool AdvancedTransform::validatePlacement(const QTransform &matrix,const QVector<QPointF> &nodes) const {
    const QRectF target=nodes.isEmpty()?matrix.mapRect(m_bounds):QPolygonF(nodes).boundingRect();
    if(!finite(target.topLeft()) || !finite(target.bottomRight()) || target.isEmpty())return fail("Use a finite, nonempty transform within the drawing coordinates.");
    // Preview retains all source content, even outside each layer's drawing
    // extent. Raster output and selection boundaries are cropped only on Apply.
    return true;
}
void AdvancedTransform::refreshPreview() {
    const auto update=[&](RasterLayerPreview &preview){preview.transform=m_matrix;preview.warpBounds=m_bounds;preview.warpNodes=m_nodes;};
    update(m_preview);for(auto &preview:m_preview.additional)update(preview);emit changed();
}
bool AdvancedTransform::setMatrix(const QTransform &matrix) {
    if(!active())return false;
    for(qreal value:{matrix.m11(),matrix.m12(),matrix.m13(),matrix.m21(),matrix.m22(),matrix.m23(),matrix.m31(),matrix.m32(),matrix.m33()})
        if(!std::isfinite(value))return fail("The transform contains an invalid value.");
    if(!matrix.isInvertible())return fail("The transform must have a nonzero area.");
    qreal sign=0;
    for(const auto &point:rectangleQuad(m_bounds)) {
        const qreal w=matrix.m13()*point.x()+matrix.m23()*point.y()+matrix.m33();
        if(std::abs(w)<1e-8 || (sign && w*sign<0))return fail("Perspective cannot cross the vanishing horizon inside the artwork.");sign=w;
    }
    if(!convex(matrix.map(rectangleQuad(m_bounds))) || !validatePlacement(matrix,{}))return false;
    m_matrix=matrix;m_nodes.clear();refreshPreview();return true;
}
bool AdvancedTransform::setQuad(const QPolygonF &corners) {
    if(!active())return false;
    if(!convex(corners))return fail("Keep perspective corners in a convex, uncrossed shape.");
    QTransform matrix;
    if(!QTransform::quadToQuad(rectangleQuad(m_bounds),corners,matrix))return fail("These perspective corners cannot form a transform.");
    return setMatrix(matrix);
}
bool AdvancedTransform::setWarp(const QVector<QPointF> &nodes) {
    if(!active())return false;
    if(nodes.size()!=9)return fail("Warp requires nine mesh handles.");
    // Warp currently clips and joins the selection against eight mesh triangles.
    // Keep detailed color selections out of that unbounded polygon operation;
    // matrix transforms remain available and this rejection changes no state.
    if(m_selection.active && m_selection.path.elementCount()>4096)
        return fail("This selection is too detailed for mesh warp. Use Perspective Transform or deselect it first.");
    for(const auto &point:nodes)if(!finite(point))return fail("The warp contains invalid coordinates.");
    for(int index:m_indices)if(layerHasEditableObjects(m_before.shots[m_document->activeShotIndex()].layers[index]))return fail("Rasterize vector/text layers before using mesh warp. Matrix transforms keep them editable.");
    const auto pieces=triangles(m_bounds,nodes);if(pieces.size()!=8)return fail("Warp cells must have a nonzero area.");
    qreal sign=0;
    for(const auto &piece:pieces){const qreal area=cross(piece.destination[1]-piece.destination[0],piece.destination[2]-piece.destination[0]);
        if(std::abs(area)<.01 || (sign && area*sign<0))return fail("Keep warp cells from folding over one another.");sign=area;}
    // A convex outer boundary plus consistently oriented cells makes this mesh injective.
    QPolygonF boundary{nodes[0],nodes[2],nodes[8],nodes[6]};
    if(!convex(boundary))return fail("Keep the outer warp corners convex.");
    const QVector<int> outline{0,1,2,5,8,7,6,3};
    for (int i=0;i<8;++i) for (int j=i+2;j<8;++j) {
        if (i==0 && j==7) continue;
        const QLineF a(nodes[outline[i]],nodes[outline[(i+1)%8]]);
        const QLineF b(nodes[outline[j]],nodes[outline[(j+1)%8]]);
        if (a.intersects(b)==QLineF::BoundedIntersection) return fail("Keep the outer warp edges from crossing.");
    }
    if(!validatePlacement({},nodes))return false;
    m_matrix={};m_nodes=nodes;refreshPreview();return true;
}
bool AdvancedTransform::flip(bool horizontal) {
    if(!active())return false;
    const auto center=QPolygonF(controlPoints()).boundingRect().center();QTransform flip;
    flip.translate(center.x(),center.y());flip.scale(horizontal?-1:1,horizontal?1:-1);flip.translate(-center.x(),-center.y());
    if(!m_nodes.isEmpty()){auto nodes=m_nodes;for(auto &node:nodes)node=flip.map(node);return setWarp(nodes);}
    return setMatrix(m_matrix*flip);
}
bool AdvancedTransform::skew(qreal horizontal,qreal vertical) {
    if(!active() || !std::isfinite(horizontal) || !std::isfinite(vertical) || std::abs(horizontal)>4 || std::abs(vertical)>4)return fail("Use skew amounts between -4 and 4.");
    const auto center=m_bounds.center();QTransform skew;skew.translate(center.x(),center.y());skew.shear(horizontal,vertical);skew.translate(-center.x(),-center.y());
    return setMatrix(skew);
}
bool AdvancedTransform::apply() {
    if(!active())return false;
    if(m_matrix.isIdentity() && (m_nodes.isEmpty() || m_nodes==grid(m_bounds))){cancel();return true;}
    auto after=m_before;auto selected=m_selection;
    try {
        if(selected.active) {
            auto path=selectionPath();const auto extent=m_document->drawingBounds();
            if(!path.isEmpty() && !extent.contains(path.controlPointRect())) {
                QPainterPath clip;clip.addRect(extent);
                if(path.elementCount()>4096) {
                    QPainterPath clipped;QString error;
                    if(!selectionBooleanPath(path,clip,SelectionPathOperation::Intersection,extent,&clipped,&error))return fail(error);
                    path=std::move(clipped);
                } else path=path.intersected(clip);
            }
            selected.path=std::move(path);
            if(selected.path.isEmpty())selected={};
        }
        if(!m_selectionOnly) {
            int used=projectTileCount(after);
            for(int at=0;at<m_indices.size();++at) {
                const int index=m_indices[at];auto &layer=after.shots[m_document->activeShotIndex()].layers[index];const int previous=int(layer.tiles.size());
                const auto &preview=at==0?m_preview:m_preview.additional[at-1];QString error;
                if(layerHasEditableObjects(layer)) {
                    if(!transformEditableLayer(layer,m_matrix,layerBounds(after,m_document->activeShotIndex(),index),maxProjectTiles-used+previous,&error))return fail(error);
                } else {
                    RasterLayer transformed;
                    if(!rasterize(preview,selectedPixelBounds(preview.moving),layerBounds(after,m_document->activeShotIndex(),index),maxProjectTiles-used+previous,&transformed,&error))return fail(error);
                    layer=std::move(transformed);
                }
                used+=layer.tiles.size()-previous;
            }
        }
        const auto validation=after.validate();if(!validation.isEmpty())return fail(validation);
        auto *document=m_document.data();const auto before=m_before;const auto previousSelection=m_selection;const auto selectionOnly=m_selectionOnly;
        cancel();return document->commit(before,after,selectionOnly?"Transform selection boundary":"Transform layers",{},previousSelection,selected);
    } catch(const std::bad_alloc &) { return fail("There is not enough memory to apply the transform. The original artwork is unchanged."); }
}
void AdvancedTransform::cancel() {
    if(!active())return;
    disconnect(m_documentConnection);m_document->m_externalEditActive=false;m_document.clear();m_before={};m_selection={};m_indices.clear();m_preview={};m_bounds={};m_nodes.clear();m_matrix={};emit changed();
}
} // namespace toon
