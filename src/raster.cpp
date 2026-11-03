#include "scene3d_cache.h"
#include "raster.h"
#include "advanced_transform.h"
#include "effect_render.h"
#include <QCache>
#include <QDataStream>
#include <QHash>
#include <QIODevice>
#include <QPaintEngine>
#include <QPainterPath>
#include <QSet>
#include <QUuid>
#include <algorithm>
#include <array>
#include <cmath>

namespace toon {
namespace {
struct BlendEntry { const char *key; const char *name; QPainter::CompositionMode composition; };
constexpr BlendEntry blendEntries[] = {
    {"normal", "Normal", QPainter::CompositionMode_SourceOver},
    {"multiply", "Multiply", QPainter::CompositionMode_Multiply},
    {"screen", "Screen", QPainter::CompositionMode_Screen},
    {"overlay", "Overlay", QPainter::CompositionMode_Overlay},
    {"darken", "Darken", QPainter::CompositionMode_Darken},
    {"lighten", "Lighten", QPainter::CompositionMode_Lighten},
    {"color-dodge", "Color dodge", QPainter::CompositionMode_ColorDodge},
    {"color-burn", "Color burn", QPainter::CompositionMode_ColorBurn},
    {"hard-light", "Hard light", QPainter::CompositionMode_HardLight},
    {"soft-light", "Soft light", QPainter::CompositionMode_SoftLight},
    {"difference", "Difference", QPainter::CompositionMode_Difference},
    {"exclusion", "Exclusion", QPainter::CompositionMode_Exclusion}
};
bool validBlendMode(LayerBlendMode mode) { return int(mode) >= 0 && int(mode) < int(std::size(blendEntries)); }
}
QString layerBlendModeName(LayerBlendMode mode) {
    return validBlendMode(mode) ? QString::fromLatin1(blendEntries[int(mode)].name) : QString();
}
QString layerBlendModeKey(LayerBlendMode mode) {
    return validBlendMode(mode) ? QString::fromLatin1(blendEntries[int(mode)].key) : QString();
}
std::optional<LayerBlendMode> layerBlendModeFromKey(const QString &key) {
    for (int i = 0; i < int(std::size(blendEntries)); ++i)
        if (key == QLatin1String(blendEntries[i].key)) return LayerBlendMode(i);
    return {};
}
QPainter::CompositionMode layerCompositionMode(LayerBlendMode mode) {
    return validBlendMode(mode) ? blendEntries[int(mode)].composition : QPainter::CompositionMode_SourceOver;
}
RasterLayer RasterLayer::create(const QString &name) {
    RasterLayer layer;
    layer.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    layer.name = name;
    return layer;
}
QImage prepareBrushStamp(const BrushSettings &brush) {
    if (!brush.colorTip.isNull()) {
        if (!std::isfinite(brush.size)) return {};
        const int side = int(std::ceil(qBound(1.0,brush.size,256.0)));
        const qreal factor=qreal(side)/qMax(brush.colorTip.width(),brush.colorTip.height());
        const QSize pixels(qMax(1,qRound(brush.colorTip.width()*factor)),qMax(1,qRound(brush.colorTip.height()*factor)));
        return brush.colorTip.scaled(pixels,Qt::IgnoreAspectRatio,Qt::SmoothTransformation);
    }
    if (brush.tipMask.isNull()) return {};
    if (!std::isfinite(brush.size)) return {};
    const int side = int(std::ceil(qBound(1.0, brush.size, 256.0)));
    const qreal factor = qreal(side) / qMax(brush.tipMask.width(), brush.tipMask.height());
    const QSize pixels(qMax(1, qRound(brush.tipMask.width() * factor)),
                       qMax(1, qRound(brush.tipMask.height() * factor)));
    const auto mask = brush.tipMask.scaled(pixels, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
        .convertToFormat(QImage::Format_Grayscale8);
    QImage stamp(mask.size(), QImage::Format_ARGB32_Premultiplied);
    if (stamp.isNull()) return {};
    for (int y = 0; y < mask.height(); ++y) {
        const auto *source = mask.constScanLine(y);
        auto *target = reinterpret_cast<QRgb *>(stamp.scanLine(y));
        for (int x = 0; x < mask.width(); ++x)
            target[x] = qPremultiply(qRgba(brush.color.red(), brush.color.green(), brush.color.blue(), source[x]));
    }
    return stamp;
}
namespace {
QRectF deviceViewport(const QPainter &painter) {
    const auto *device = painter.device();
    const qreal ratio = device->devicePixelRatioF();
    // Qt 6 combinedTransform already includes the device pixel ratio. Widget
    // dimensions are logical; image and pixmap dimensions are physical.
    const qreal multiplier = device->devType() == QInternal::Widget ? ratio : 1;
    return QRectF(0, 0, device->width() * multiplier, device->height() * multiplier);
}
QRectF deviceClip(const QPainter &painter) {
    QRectF bounds = deviceViewport(painter);
    if (painter.hasClipping()) bounds &= painter.combinedTransform().mapRect(painter.clipBoundingRect());
    if (painter.paintEngine() && !painter.paintEngine()->systemClip().isEmpty()) {
        const auto physicalToViewport = painter.deviceTransform().inverted() * painter.combinedTransform();
        bounds &= physicalToViewport.mapRect(painter.paintEngine()->systemClip().boundingRect());
    }
    if (bounds.isEmpty()) return {};
    return bounds.toAlignedRect();
}
QCache<QByteArray, QImage> &compositeCache() {
    // Shared by canvas and navigator, bounded independently of the document's undo budget.
    thread_local QCache<QByteArray, QImage> cache(96 * 1024 * 1024);
    return cache;
}
// Each filtered tile carries its neighbors' edge pixels. Drawing the entire
// padded image, clipped to the tile's core, lets Qt filter across tile boundaries
// instead of clamping each independent source rectangle. Core clips have hard,
// shared edges: antialiasing adjacent image quads would introduce alpha seams.
template<typename TileProvider>
void paintSmoothTiles(QPainter &painter, const QSet<TileKey> &occupied, TileProvider provide,
                      bool preciseTranslation = false) {
    const auto clip = deviceClip(painter);
    if (clip.isEmpty() || occupied.isEmpty()) return;
    const auto transform = painter.combinedTransform();
    const qreal fractionX=transform.dx()-std::floor(transform.dx());
    const qreal fractionY=transform.dy()-std::floor(transform.dy());
    const bool filterTranslation=preciseTranslation&&transform.type()<=QTransform::TxTranslate
        && (fractionX!=0||fractionY!=0);
    const int phaseX=qBound(0,qRound(fractionX*256),256),phaseY=qBound(0,qRound(fractionY*256),256);
    QSet<TileKey> keys;
    for (const auto &key : occupied)
        for (int y = -1; y <= 1; ++y) for (int x = -1; x <= 1; ++x)
            keys.insert({key.first + x, key.second + y});
    // Keep only a small neighborhood locally. Holding every visible composite
    // would pin images evicted from the shared cache when zoomed far out.
    QCache<TileKey, QImage> sourceTiles(32);
    const auto source = [&](const TileKey &key) {
        if (const auto *found = sourceTiles.object(key)) return *found;
        const auto image = occupied.contains(key) ? provide(key) : QImage();
        sourceTiles.insert(key, new QImage(image));
        return image;
    };
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, false);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    auto ordered = keys.values();
    std::sort(ordered.begin(), ordered.end(), [](const TileKey &a, const TileKey &b) {
        return a.second == b.second ? a.first < b.first : a.second < b.second;
    });
    for (const auto &key : ordered) {
        const QRectF core(key.first * tileSize, key.second * tileSize, tileSize, tileSize);
        if (!transform.mapRect(core).intersects(clip)) continue;
        QByteArray signature("display-gutter-v1");
        QDataStream stream(&signature, QIODevice::Append);
        std::array<QImage, 9> neighbors;
        bool any = false;
        int index = 0;
        for (int y = -1; y <= 1; ++y) for (int x = -1; x <= 1; ++x) {
            auto &tile = neighbors[index++];
            tile = source({key.first + x, key.second + y});
            stream << tile.cacheKey();
            any |= !tile.isNull();
        }
        if (!any) continue;
        auto &cache = compositeCache();
        QImage padded;
        if (const auto *cached = cache.object(signature)) padded = *cached;
        else {
            padded = QImage(tileSize + 2, tileSize + 2, QImage::Format_ARGB32_Premultiplied);
            if (padded.isNull()) continue;
            padded.fill(Qt::transparent);
            {
                QPainter stitch(&padded);
                stitch.setCompositionMode(QPainter::CompositionMode_Source);
                index = 0;
                for (int y = -1; y <= 1; ++y) for (int x = -1; x <= 1; ++x) {
                    const auto &tile = neighbors[index++];
                    if (!tile.isNull()) stitch.drawImage(QPoint(1 + x * tileSize, 1 + y * tileSize), tile);
                }
            }
            cache.insert(signature, new QImage(padded), int(padded.sizeInBytes() + signature.size() + 128));
        }
        if(filterTranslation) {
            // Qt's translation-only drawImage fast path rounds fractional
            // positions even with SmoothPixmapTransform. Filter the core in
            // premultiplied color, then place it at an integer device offset.
            // Neighbor gutters cover tile seams and the final coverage pixel.
            QByteArray filteredSignature=signature;
            QDataStream filteredStream(&filteredSignature,QIODevice::Append);
            filteredStream<<QByteArray("fractional-translation-v1")<<quint16(phaseX)<<quint16(phaseY);
            QImage filtered;
            if(const auto *cached=cache.object(filteredSignature))filtered=*cached;
            else {
                filtered=QImage(tileSize,tileSize,QImage::Format_ARGB32_Premultiplied);
                if(filtered.isNull())continue;
                const quint32 weights[]={quint32((256-phaseX)*(256-phaseY)),quint32(phaseX*(256-phaseY)),
                                         quint32((256-phaseX)*phaseY),quint32(phaseX*phaseY)};
                for(int y=0;y<tileSize;++y) {
                    const auto *row=reinterpret_cast<const QRgb*>(padded.constScanLine(y+1));
                    const auto *above=reinterpret_cast<const QRgb*>(padded.constScanLine(y));
                    auto *target=reinterpret_cast<QRgb*>(filtered.scanLine(y));
                    for(int x=0;x<tileSize;++x) {
                        const QRgb pixels[]={row[x+1],row[x],above[x+1],above[x]};
                        const auto component=[&](int shift) {
                            quint32 sum=32768;
                            for(int i=0;i<4;++i)sum+=((pixels[i]>>shift)&255)*weights[i];
                            return int(sum>>16);
                        };
                        target[x]=qRgba(component(16),component(8),component(0),component(24));
                    }
                }
                cache.insert(filteredSignature,new QImage(filtered),int(filtered.sizeInBytes()+filteredSignature.size()+128));
            }
            painter.save();
            painter.translate(-fractionX,-fractionY);
            painter.drawImage(core.topLeft(),filtered);
            painter.restore();
            continue;
        }
        painter.save();
        painter.setClipRect(core, Qt::IntersectClip);
        painter.drawImage(core.topLeft() - QPointF(1, 1), padded);
        painter.restore();
    }
    painter.restore();
}
// Clipping changes color without adding coverage. Blending over an opaque copy
// first gives Qt the unassociated base color; restore its original alpha after
// the blend so even fractional antialiased edges keep exactly their coverage.
void paintClippedOverlay(QImage &base, const QImage &overlay, LayerBlendMode mode, qreal opacity) {
    if (mode == LayerBlendMode::Normal) {
        QPainter painter(&base);
        painter.setCompositionMode(QPainter::CompositionMode_SourceAtop);
        painter.setOpacity(opacity); painter.drawImage(QPoint(), overlay);
        return;
    }
    const auto coverage = base;
    for (int y = 0; y < base.height(); ++y) {
        auto *row = reinterpret_cast<QRgb *>(base.scanLine(y));
        for (int x = 0; x < base.width(); ++x) row[x] = qUnpremultiply(row[x]) | 0xff000000;
    }
    {
        QPainter painter(&base);
        painter.setCompositionMode(layerCompositionMode(mode));
        painter.setOpacity(opacity); painter.drawImage(QPoint(), overlay);
    }
    for (int y = 0; y < base.height(); ++y) {
        auto *row = reinterpret_cast<QRgb *>(base.scanLine(y));
        const auto *original = reinterpret_cast<const QRgb *>(coverage.constScanLine(y));
        for (int x = 0; x < base.width(); ++x)
            row[x] = qPremultiply((row[x] & 0x00ffffff) | (original[x] & 0xff000000));
    }
}
// Compose one sparse tile at a time. Groups and clipping runs are isolated so
// their opacity never compounds where children overlap.
class LayerCompositor {
public:
    LayerCompositor(const QVector<RasterLayer> &layers, bool annotations, QSize sceneFrameSize = {})
        : m_layers(layers), m_annotations(annotations), m_sceneFrameSize(sceneFrameSize) {
        QDataStream signature(&m_signature, QIODevice::WriteOnly);
        signature << annotations << quint32(layers.size());
        for (int i = 0; i < layers.size(); ++i) {
            const auto &layer = layers[i];
            m_children[layer.parentId].append(i);
            if(layer.scene3d && layer.scene3d->visible && !sceneFrameSize.isEmpty())
                m_sceneImages.insert(layer.id,scene3DLayerPreview(layer.id,*layer.scene3d,sceneFrameSize));
            signature << layer.id << layer.parentId << layer.group << layer.visible << layer.annotation
                      << layer.clipped << double(layer.opacity) << qint32(layer.blendMode)
                      << m_sceneImages.value(layer.id).cacheKey() << sceneFrameSize
                      << double(layer.scene3d ? layer.scene3d->opacity : 1);
        }
    }
    QImage cachedTile(const TileKey &key) const {
        auto signature = m_signature;
        QDataStream stream(&signature, QIODevice::Append);
        stream << key.first << key.second;
        for (const auto &layer : m_layers) {
            const auto tile = layer.tiles.constFind(key);
            stream << (tile == layer.tiles.cend() ? qint64(0) : tile->cacheKey());
        }
        auto &cache = compositeCache();
        if (const auto *cached = cache.object(signature)) return *cached;
        const auto image = tile({}, key);
        cache.insert(signature, new QImage(image), int(image.sizeInBytes() + signature.size() + 128));
        return image;
    }
    QImage tile(const QString &parent, const TileKey &key, int depth = 0) const {
        if (depth > 8) return {};
        QImage result;
        const auto siblings = m_children.value(parent);
        for (int at = 0; at < siblings.size();) {
            const auto &base = m_layers[siblings[at]];
            if (base.clipped) { ++at; continue; }
            int end = at + 1;
            while (end < siblings.size() && m_layers[siblings[end]].clipped) ++end;
            if (shown(base) && base.opacity > 0) {
                auto image = base.group ? tile(base.id, key, depth + 1) : sourceTile(base, key);
                if (!image.isNull()) {
                    for (int clipped = at + 1; clipped < end; ++clipped) {
                        const auto &layer = m_layers[siblings[clipped]];
                        const auto overlay = layer.tiles.constFind(key);
                        if (!shown(layer) || layer.opacity <= 0 || overlay == layer.tiles.cend()) continue;
                        paintClippedOverlay(image, overlay.value(), layer.blendMode, layer.opacity);
                    }
                    if (result.isNull() && base.opacity == 1) result = image;
                    else {
                        if (result.isNull()) {
                        result = QImage(tileSize, tileSize, QImage::Format_ARGB32_Premultiplied);
                        result.fill(Qt::transparent);
                        }
                        QPainter painter(&result);
                        painter.setCompositionMode(layerCompositionMode(base.blendMode));
                        painter.setOpacity(base.opacity);
                        painter.drawImage(QPoint(), image);
                    }
                }
            }
            at = end;
        }
        return result;
    }
private:
    QImage sourceTile(const RasterLayer &layer,const TileKey &key) const {
        if(!layer.scene3d)return layer.tiles.value(key);
        const auto source=m_sceneImages.value(layer.id);if(source.isNull())return {};
        const QRect area(key.first*tileSize,key.second*tileSize,tileSize,tileSize);
        if(!area.intersects(QRect(QPoint(),m_sceneFrameSize)))return {};
        QByteArray signature("scene-source-tile-v1");QDataStream data(&signature,QIODevice::Append);
        data<<source.cacheKey()<<m_sceneFrameSize<<key.first<<key.second<<double(layer.scene3d->opacity);
        auto &cache=compositeCache();if(auto *found=cache.object(signature))return *found;
        QImage tile(tileSize,tileSize,QImage::Format_ARGB32_Premultiplied);tile.fill(Qt::transparent);
        {QPainter painter(&tile);painter.translate(-area.topLeft());painter.setOpacity(layer.scene3d->opacity);
         painter.setRenderHint(QPainter::SmoothPixmapTransform,true);
         painter.drawImage(QRectF(QPointF(),QSizeF(m_sceneFrameSize)),source);}
        cache.insert(signature,new QImage(tile),int(tile.sizeInBytes()+signature.size()+128));return tile;
    }
    bool shown(const RasterLayer &layer) const {
        return layer.visible && (m_annotations || !layer.annotation);
    }
    const QVector<RasterLayer> &m_layers;
    bool m_annotations;
    QHash<QString, QVector<int>> m_children;
    QByteArray m_signature;
    QSize m_sceneFrameSize;
    QHash<QString,QImage> m_sceneImages;
};

// A transform preview is composited at the size of the visible device region.
// No full-resolution transformed tiles are generated during a pointer gesture.
class PreviewCompositor {
public:
    PreviewCompositor(const QVector<RasterLayer> &layers, bool annotations, const RasterLayerPreview &preview,
                      const QPainter &source, QRectF viewport, bool smoothDisplay, QSize sceneFrameSize = {}, bool fullResolution = false)
        : m_layers(layers), m_annotations(annotations), m_preview(preview), m_viewport(viewport),
          m_artworkToDevice(source.combinedTransform()), m_world(source.worldTransform()),
          m_window(source.window()), m_painterViewport(source.viewport()), m_viewEnabled(source.viewTransformEnabled()),
          m_clip(source.clipPath()), m_hasClip(source.hasClipping()), m_smoothDisplay(smoothDisplay),
          m_preciseTranslation(fullResolution&&preview.layerId.isEmpty()&&preview.additional.isEmpty()), m_sceneFrameSize(sceneFrameSize) {
        for (int i = 0; i < layers.size(); ++i) m_children[layers[i].parentId].append(i);
        const qreal pixels = viewport.width() * viewport.height();
        const qreal ratio = fullResolution ? 1.0 : qMin(1.0, std::sqrt(4000000.0 / qMax(1.0, pixels)));
        m_size = QSize(qMax(1, int(std::ceil(viewport.width() * ratio))),
                       qMax(1, int(std::ceil(viewport.height() * ratio))));
        m_deviceRatio = source.device()->devicePixelRatioF() * ratio;
    }
    QImage composite(const QString &parent = {}, int depth = 0, QTransform ancestor = {}) const {
        if (depth > 8) return {};
        QImage result;
        const auto siblings = m_children.value(parent);
        for (int at = 0; at < siblings.size();) {
            const auto &base = m_layers[siblings[at]];
            if (base.clipped) { ++at; continue; }
            int end = at + 1;
            while (end < siblings.size() && m_layers[siblings[end]].clipped) ++end;
            if (shown(base) && base.opacity > 0) {
                const auto transform = base.renderTransform * ancestor;
                auto image = base.group ? composite(base.id, depth + 1, transform) : raster(base, transform);
                if (!image.isNull()) {
                    for (int index = at + 1; index < end; ++index) {
                        const auto &layer = m_layers[siblings[index]];
                        if (!shown(layer) || layer.opacity <= 0) continue;
                        const auto overlay = raster(layer, layer.renderTransform * ancestor);
                        if (overlay.isNull()) continue;
                        paintClippedOverlay(image, overlay, layer.blendMode, layer.opacity);
                    }
                    if (result.isNull() && base.opacity == 1) result = image;
                    else {
                        if (result.isNull()) result = blank();
                        QPainter painter(&result); painter.setCompositionMode(layerCompositionMode(base.blendMode));
                        painter.setOpacity(base.opacity); painter.drawImage(QPoint(), image);
                    }
                }
            }
            at = end;
        }
        return result;
    }
private:
    bool shown(const RasterLayer &layer) const { return layer.visible && (m_annotations || !layer.annotation); }
    QImage blank() const {
        QImage image(m_size, QImage::Format_ARGB32_Premultiplied);
        image.setDevicePixelRatio(m_deviceRatio); image.fill(Qt::transparent); return image;
    }
    void drawTiles(QImage &image, const RasterLayer &layer, const QTransform &transform) const {
        const auto artworkToDevice = transform * m_artworkToDevice;
        QVector<TileKey> visible;
        for (auto tile = layer.tiles.cbegin(); tile != layer.tiles.cend(); ++tile) {
            const QRectF area(tile.key().first * tileSize, tile.key().second * tileSize, tileSize, tileSize);
            const auto sampledArea = m_smoothDisplay ? area.adjusted(-1, -1, 1, 1) : area;
            if (artworkToDevice.mapRect(sampledArea).intersects(m_viewport)) visible.append(tile.key());
        }
        const auto sceneImage = layer.scene3d && layer.scene3d->visible && !m_sceneFrameSize.isEmpty()
            ? scene3DLayerPreview(layer.id,*layer.scene3d,m_sceneFrameSize) : QImage();
        if (visible.isEmpty() && sceneImage.isNull()) return;
        if (image.isNull()) image = blank();
        QPainter painter(&image);
        // Let QPainter apply the device ratio in its normal stage. Folding it
        // into the world matrix changes fixed-point rounding at rotated edges.
        if (m_viewEnabled) { painter.setWindow(m_window); painter.setViewport(m_painterViewport); }
        painter.setTransform(m_world);
        if (m_hasClip) painter.setClipPath(m_clip);
        painter.setTransform(transform, true);
        if (!sceneImage.isNull()) {
            painter.setOpacity(layer.scene3d->opacity);
            painter.setRenderHint(QPainter::SmoothPixmapTransform,m_smoothDisplay);
            painter.drawImage(QRectF(QPointF(),QSizeF(m_sceneFrameSize)),sceneImage);
        } else if(m_smoothDisplay&&m_preciseTranslation) {
            QSet<TileKey> keys;
            for(auto tile=layer.tiles.cbegin();tile!=layer.tiles.cend();++tile)keys.insert(tile.key());
            paintSmoothTiles(painter,keys,[&](const TileKey &key){return layer.tiles.value(key);},true);
        } else paintRasterLayerPixels(painter, layer, m_smoothDisplay);
    }
    QImage raster(const RasterLayer &layer, const QTransform &transform) const {
        QImage image;
        const RasterLayerPreview *preview = layer.id == m_preview.layerId ? &m_preview : nullptr;
        if (!preview) for (const auto &extra : m_preview.additional)
            if (extra.layerId == layer.id) { preview = &extra; break; }
        if (preview) {
            drawTiles(image, preview->stationary, transform);
            if (preview->warpNodes.isEmpty()) drawTiles(image, preview->moving, preview->transform * transform);
            else {
                if (image.isNull()) image = blank();
                QPainter painter(&image);
                if (m_viewEnabled) { painter.setWindow(m_window); painter.setViewport(m_painterViewport); }
                painter.setTransform(m_world);
                if (m_hasClip) painter.setClipPath(m_clip);
                painter.setRenderHint(QPainter::Antialiasing, false);
                painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
                paintAdvancedMovingLayer(painter, *preview, m_smoothDisplay);
            }
        } else drawTiles(image, layer, transform);
        return image;
    }
    const QVector<RasterLayer> &m_layers;
    bool m_annotations;
    const RasterLayerPreview &m_preview;
    QRectF m_viewport;
    QSize m_size;
    QTransform m_artworkToDevice, m_world;
    QRect m_window, m_painterViewport;
    bool m_viewEnabled;
    QPainterPath m_clip;
    bool m_hasClip;
    bool m_smoothDisplay;
    bool m_preciseTranslation;
    QSize m_sceneFrameSize;
    qreal m_deviceRatio;
    QHash<QString, QVector<int>> m_children;
};
}
QImage renderLayerThumbnail(const RasterLayer &layer, QRectF artworkBounds, QSize size) {
    if (layer.group || size.isEmpty() || size.width() > 512 || size.height() > 512 || artworkBounds.isEmpty()
        || !std::isfinite(artworkBounds.x()) || !std::isfinite(artworkBounds.y())
        || !std::isfinite(artworkBounds.width()) || !std::isfinite(artworkBounds.height())) return {};
    thread_local QCache<QByteArray, QImage> thumbnails(8 * 1024 * 1024);
    thread_local QCache<qint64, QRect> tileExtents(2048);
    QByteArray key; QDataStream stream(&key, QIODevice::WriteOnly);
    stream << artworkBounds << size;
    for (auto tile = layer.tiles.cbegin(); tile != layer.tiles.cend(); ++tile)
        stream << tile.key().first << tile.key().second << tile->cacheKey();
    if (const auto *cached = thumbnails.object(key)) return *cached;
    QRectF bounds;
    for (auto tile = layer.tiles.cbegin(); tile != layer.tiles.cend(); ++tile) {
        const QPoint origin(tile.key().first * tileSize, tile.key().second * tileSize);
        if (!QRectF(origin, tile->size()).intersects(artworkBounds)) continue;
        const auto imageKey = tile->cacheKey();
        const QRect *cached = tileExtents.object(imageKey);
        QRect extent;
        if (cached) extent = *cached;
        else {
            int left = tile->width(), top = tile->height(), right = -1, bottom = -1;
            for (int y = 0; y < tile->height(); ++y) {
                const auto *row = reinterpret_cast<const QRgb *>(tile->constScanLine(y));
                for (int x = 0; x < tile->width(); ++x) if (qAlpha(row[x])) {
                    left = qMin(left, x); right = qMax(right, x); top = qMin(top, y); bottom = y;
                }
            }
            if (right >= left) extent = QRect(QPoint(left, top), QPoint(right, bottom));
            tileExtents.insert(imageKey, new QRect(extent));
        }
        if (!extent.isEmpty()) bounds |= QRectF(extent.translated(origin)).intersected(artworkBounds);
    }
    QImage result(size, QImage::Format_ARGB32_Premultiplied); result.fill(Qt::transparent);
    if (!bounds.isEmpty()) {
        const qreal scale = qMin(size.width() / bounds.width(), size.height() / bounds.height());
        QPainter painter(&result); painter.setRenderHint(QPainter::SmoothPixmapTransform);
        painter.translate((size.width() - bounds.width() * scale) / 2, (size.height() - bounds.height() * scale) / 2);
        painter.scale(scale, scale); painter.translate(-bounds.topLeft()); painter.setClipRect(artworkBounds);
        for (auto tile = layer.tiles.cbegin(); tile != layer.tiles.cend(); ++tile) {
            const QPoint origin(tile.key().first * tileSize, tile.key().second * tileSize);
            if (QRectF(origin, tile->size()).intersects(bounds)) painter.drawImage(origin, tile.value());
        }
    }
    thumbnails.insert(key, new QImage(result), int(result.sizeInBytes() + key.size() + 128));
    return result;
}
void paintRasterLayerPixels(QPainter &painter, const RasterLayer &layer, bool smoothDisplay) {
    if (smoothDisplay) {
        QSet<TileKey> keys;
        for (auto tile = layer.tiles.cbegin(); tile != layer.tiles.cend(); ++tile) keys.insert(tile.key());
        paintSmoothTiles(painter, keys, [&](const TileKey &key) { return layer.tiles.value(key); });
        return;
    }
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, false);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
    const auto clip = deviceClip(painter);
    for (auto tile = layer.tiles.cbegin(); tile != layer.tiles.cend(); ++tile) {
        const QRectF area(tile.key().first * tileSize, tile.key().second * tileSize, tileSize, tileSize);
        if (painter.combinedTransform().mapRect(area).intersects(clip)) painter.drawImage(area.topLeft(), tile.value());
    }
    painter.restore();
}
void paintLayers(QPainter &p, const QVector<RasterLayer> &layers, bool annotations,
                 const RasterLayerPreview *preview, bool smoothDisplay, QSize sceneFrameSize) {
    clearEffectRenderError();
    if (!checkEffectClipping(layers)) return;
    const bool animatedTransform = std::any_of(layers.cbegin(), layers.cend(), [](const auto &layer) { return !layer.renderTransform.isIdentity(); });
    if (preview) {
        // At identity, use the original unsplit coverage. Re-compositing the two
        // halves of a feathered selection would otherwise reduce its alpha,
        // and filtering separate layers would change overlapping edge coverage.
        const auto unchanged = [](const RasterLayerPreview &candidate) {
            if (!candidate.sourceUnchangedAtIdentity || !candidate.transform.isIdentity()) return false;
            if (candidate.warpNodes.isEmpty()) return true;
            if (candidate.warpNodes.size() != 9) return false;
            for (int y = 0; y < 3; ++y) for (int x = 0; x < 3; ++x)
                if (candidate.warpNodes[y * 3 + x] != candidate.warpBounds.topLeft()
                    + QPointF(candidate.warpBounds.width() * x / 2, candidate.warpBounds.height() * y / 2)) return false;
            return true;
        };
        if (unchanged(*preview) && std::all_of(preview->additional.cbegin(), preview->additional.cend(), unchanged))
            preview = nullptr;
    }
    p.save();
    // Adjacent tile rectangles must share exact rasterized edges at fractional zoom.
    // Antialiasing the image quads independently introduces translucent seams.
    p.setRenderHint(QPainter::Antialiasing, false);
    p.setRenderHint(QPainter::SmoothPixmapTransform, false);
    const auto clip = deviceClip(p);
    if (clip.isEmpty()) { p.restore(); return; }
    if (layerStackHasEffects(layers)) {
        EffectCompositor compositor(layers, annotations, preview, smoothDisplay, sceneFrameSize);
        bool invertible = false;
        const auto inverse = p.combinedTransform().inverted(&invertible);
        const auto keys = compositor.visibleTiles(invertible ? inverse.mapRect(clip).adjusted(-2, -2, 2, 2) : QRectF());
        if (!keys.isEmpty()) {
            // The compositor retains only cached tiles and the current filter
            // halo. It never allocates the complete full-resolution canvas.
            const auto provide = [&](const TileKey &key) {
                return effectRenderError().isEmpty() ? compositor.tile(key) : QImage();
            };
            if (smoothDisplay) paintSmoothTiles(p, keys, provide, true);
            else for (const auto &key : keys) {
                const auto tile = provide(key);
                if (!tile.isNull()) p.drawImage(QPointF(key.first * tileSize, key.second * tileSize), tile);
            }
        }
        p.restore(); return;
    }
    if (preview || animatedTransform) {
        // Keep the buffer anchored to the device origin. Rebasing a rotated
        // sampler to a cropped clip origin changes Qt's fixed-point edge rounding.
        const auto viewport = deviceViewport(p);
        const RasterLayerPreview emptyPreview;
        const PreviewCompositor compositor(layers, annotations, preview ? *preview : emptyPreview, p, viewport, smoothDisplay, sceneFrameSize, !preview);
        const auto image = compositor.composite();
        if (!image.isNull()) {
            // Draw in physical viewport pixels; only a QWidget's backing-store
            // offset remains after neutralizing the combined mapping.
            p.setWorldTransform(p.combinedTransform().inverted(), true);
            p.setRenderHint(QPainter::SmoothPixmapTransform, smoothDisplay);
            p.drawImage(viewport, image);
        }
        p.restore(); return;
    }
    QSet<TileKey> keys;
    for (const auto &layer : layers) {
        if (layer.group || !layer.visible || (!annotations && layer.annotation)) continue;
        for (auto tile = layer.tiles.cbegin(); tile != layer.tiles.cend(); ++tile) keys.insert(tile.key());
        if(layer.scene3d && layer.scene3d->visible && !sceneFrameSize.isEmpty())
            for(int y=0;y<(sceneFrameSize.height()+tileSize-1)/tileSize;++y)
                for(int x=0;x<(sceneFrameSize.width()+tileSize-1)/tileSize;++x)keys.insert({x,y});
    }
    const LayerCompositor compositor(layers, annotations, sceneFrameSize);
    if (smoothDisplay) {
        paintSmoothTiles(p, keys, [&](const TileKey &key) { return compositor.cachedTile(key); });
        p.restore(); return;
    }
    for (const auto &key : keys) {
        const QRectF area(key.first * tileSize, key.second * tileSize, tileSize, tileSize);
        if (!p.combinedTransform().mapRect(area).intersects(clip)) continue;
        const auto tile = compositor.cachedTile(key);
        if (!tile.isNull()) p.drawImage(QPointF(key.first * tileSize, key.second * tileSize), tile);
    }
    p.restore();
}
bool paintDab(RasterLayer &layer, QPointF point, qreal diameter, QColor color,
              qreal opacity, bool eraser, QRectF bounds, int availableTiles, const QImage &stamp, QSizeF tipSize) {
    if (layer.group || (layer.alphaLocked && eraser)) return true;
    if (!std::isfinite(point.x()) || !std::isfinite(point.y()) || !std::isfinite(diameter)
        || !std::isfinite(opacity) || diameter <= 0 || opacity <= 0) return true;
    QSizeF size(diameter, diameter);
    if (!stamp.isNull()) {
        if (tipSize.isEmpty()) tipSize = stamp.size();
        size = tipSize.scaled(size, Qt::KeepAspectRatio);
    }
    const QRectF dab(point - QPointF(size.width() / 2, size.height() / 2), size);
    const auto area = dab.adjusted(-1, -1, 1, 1).intersected(bounds);
    if (area.isEmpty()) return true;
    QImage sampledDab;
    QPointF sampledOrigin;
    if (!stamp.isNull()) {
        // Resample once in local coordinates. Resampling independently in each tile
        // gives Qt's fixed-point image sampler a different rounding phase at edges.
        const QPointF anchor(std::floor(point.x()), std::floor(point.y()));
        const QPointF phase = point - anchor;
        const QRectF localDab(phase - QPointF(size.width() / 2, size.height() / 2), size);
        const QRect rasterBounds = localDab.toAlignedRect().adjusted(-1, -1, 1, 1);
        sampledDab = QImage(rasterBounds.size(), QImage::Format_ARGB32_Premultiplied);
        if (sampledDab.isNull()) return false;
        sampledDab.fill(Qt::transparent);
        QPainter sampler(&sampledDab);
        sampler.setRenderHint(QPainter::Antialiasing);
        sampler.setRenderHint(QPainter::SmoothPixmapTransform);
        sampler.drawImage(localDab.translated(-rasterBounds.topLeft()), stamp);
        sampledOrigin = anchor + rasterBounds.topLeft();
    }
    const int left = int(std::floor(area.left() / tileSize));
    const int right = int(std::floor(area.right() / tileSize));
    const int top = int(std::floor(area.top() / tileSize));
    const int bottom = int(std::floor(area.bottom() / tileSize));
    for (int y = top; y <= bottom; ++y) for (int x = left; x <= right; ++x) {
        const TileKey key{x, y};
        if (!layer.tiles.contains(key)) {
            if (eraser || layer.alphaLocked) continue;
            if (availableTiles-- <= 0) return false;
            QImage tile(tileSize, tileSize, QImage::Format_ARGB32_Premultiplied);
            if (tile.isNull()) return false;
            tile.fill(Qt::transparent);
            layer.tiles.insert(key, tile);
        }
        QPainter painter(&layer.tiles[key]);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.translate(-x * tileSize, -y * tileSize);
        painter.setClipRect(bounds);
        painter.setCompositionMode(eraser ? QPainter::CompositionMode_DestinationOut
            : layer.alphaLocked ? QPainter::CompositionMode_SourceAtop : QPainter::CompositionMode_SourceOver);
        if (stamp.isNull()) {
            color.setAlphaF(qBound(0.0, opacity, 1.0));
            painter.setPen(Qt::NoPen);
            painter.setBrush(color);
            painter.drawEllipse(dab);
        } else {
            painter.setOpacity(qBound(0.0, opacity, 1.0));
            painter.drawImage(sampledOrigin, sampledDab);
        }
    }
    return true;
}
} // namespace toon
