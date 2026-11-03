#include "connected_brush.h"
#include <QCache>
#include <QLineF>
#include <QPolygonF>
#include <cmath>
#include <limits>

namespace toon {
namespace {
constexpr qreal pi = 3.14159265358979323846;
QPointF normal(qreal degrees) {
    const qreal a = degrees * pi / 180;
    return {-std::sin(a), std::cos(a)};
}
QImage sourceImage(const BrushSettings &brush, int frame = -1) {
    const auto source = !brush.colorTip.isNull() ? brush.colorTip : brush.tipMask;
    if (source.isNull()) {
        QImage solid(1, 1, QImage::Format_ARGB32_Premultiplied);
        solid.fill(Qt::white); return solid;
    }
    thread_local QCache<QString, QImage> cache(8 * 1024 * 1024);
    const auto key = QString::number(source.cacheKey()) + ':' + QString::number(frame)
        + (brush.colorTip.isNull() ? ":mask" : ":color");
    if (const auto found = cache.object(key)) return *found;
    QImage image;
    const auto pixels = frame >= 0 ? source.copy(brush.tipFrames[frame].rect) : source;
    if (!brush.colorTip.isNull()) image = pixels.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    else image = pixels.convertToFormat(QImage::Format_Grayscale8);
    if (image.sizeInBytes() <= cache.maxCost()) cache.insert(key, new QImage(image), int(image.sizeInBytes()));
    return image;
}
QImage minifiedSource(const QImage &image, qreal period, qreal width, const QTransform &transform) {
    // Integrate fine linework before inverse sampling. Merely sampling the
    // original pixels skips subpixel cables/fences as the curve changes angle.
    const qreal scale = qMax(std::hypot(transform.m11(), transform.m12()), std::hypot(transform.m21(), transform.m22()));
    const QSize size(qMax(1, int(std::ceil(qMin(qreal(image.width()), period * scale)))),
                     qMax(1, int(std::ceil(qMin(qreal(image.height()), width * scale)))));
    if (size == image.size()) return image;
    thread_local QCache<QString, QImage> cache(8 * 1024 * 1024);
    const auto key = QString::number(image.cacheKey()) + ':' + QString::number(size.width()) + ':' + QString::number(size.height());
    if (const auto found = cache.object(key)) return *found;
    auto reduced = image.scaled(size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    if (reduced.sizeInBytes() <= cache.maxCost()) cache.insert(key, new QImage(reduced), qMax(1, int(reduced.sizeInBytes())));
    return reduced;
}
QRgb sample(const QImage &image, qreal u, qreal v, bool alternate) {
    const qreal repeat = std::floor(u);
    u -= repeat;
    if (alternate && (qint64(repeat) & 1)) u = 1 - u;
    const qreal x = u * image.width() - .5;
    const qreal y = qBound(0., v * image.height() - .5, qreal(image.height() - 1));
    const int left = int(std::floor(x)), top = int(std::floor(y));
    auto wrap = [&](int value) { return alternate ? qBound(0, value, image.width()-1) : (value % image.width() + image.width()) % image.width(); };
    const qreal fx = x - left, fy = y - top;
    const auto at = [&](int x, int y) {
        if (image.format() == QImage::Format_Grayscale8) {
            const int alpha = image.constScanLine(y)[wrap(x)]; return qRgba(alpha, alpha, alpha, alpha);
        }
        return reinterpret_cast<const QRgb *>(image.constScanLine(y))[wrap(x)];
    };
    const int bottom = qMin(top + 1, image.height() - 1);
    const QRgb p[4] = {at(left, top), at(left + 1, top), at(left, bottom), at(left + 1, bottom)};
    auto mix = [&](auto channel) {
        return qRound((channel(p[0]) * (1 - fx) + channel(p[1]) * fx) * (1 - fy)
                    + (channel(p[2]) * (1 - fx) + channel(p[3]) * fx) * fy);
    };
    return qRgba(mix([](QRgb p) { return qRed(p); }), mix([](QRgb p) { return qGreen(p); }),
                 mix([](QRgb p) { return qBlue(p); }), mix([](QRgb p) { return qAlpha(p); }));
}
qreal widthAt(const BrushSettings &brush, const BrushInput &input, quint64 index, quint32 seed, qreal direction) {
    auto dynamics = brush.sizeDynamics;
    if (brush.pressureSize && dynamics.control == BrushControl::Off) dynamics.control = BrushControl::Pressure;
    return qMax(.5, brush.size * brushDynamicValue(dynamics, input, index, seed, 1, direction, direction)
        * brushSpeedFactor(brush.speedDynamics, input.speed, brush.speedDynamics.sizeAmount));
}
}
bool appendConnectedBrush(RasterLayer &layer, RasterLayer &accumulation, const RasterLayer &original,
                          ConnectedBrushState &state, const BrushSettings &brush,
                          QPointF point, BrushInput input, quint64, quint32 seed,
                          QSizeF canvasSize, QRectF bounds, int maxTiles, bool eraser,
                          const QPainterPath *selection, RasterSelectionCache *selectionCache,
                          const QTransform &objectTransform, int rasterBandHeight) {
    input = resolvedBrushInput(brush, input);
    const QPointF delta = point - state.point;
    const qreal distance = std::hypot(delta.x(), delta.y());
    // Bound geometry steps, without changing repeat phase or discarding pen
    // samples. Slow subpixel input follows the same distance-based turn filter.
    if (state.hasPoint && distance > 2.000001) {
        const QPointF start = state.point;
        const auto first = state.input;
        const int steps = int(std::ceil(distance / 2.));
        for (int step = 1; step <= steps; ++step) {
            const qreal t = qreal(step) / steps;
            auto mixed = input;
            mixed.pressure = first.pressure + (input.pressure - first.pressure) * t;
            mixed.speed = first.speed + (input.speed - first.speed) * t;
            mixed.tiltX = first.tiltX + (input.tiltX - first.tiltX) * t;
            mixed.tiltY = first.tiltY + (input.tiltY - first.tiltY) * t;
            mixed.wheel = first.wheel + (input.wheel - first.wheel) * t;
            mixed.rotation = first.rotation + (input.rotation - first.rotation) * t;
            if (!appendConnectedBrush(layer, accumulation, original, state, brush,
                start + delta * t, mixed, 0, seed, canvasSize, bounds, maxTiles,
                eraser, selection, selectionCache, objectTransform, rasterBandHeight)) return false;
        }
        return true;
    }
    const qreal direction = distance > 1e-8 ? std::atan2(delta.y(), delta.x()) * 180 / pi : state.direction;
    const auto sourceSize = brush.tipFrames.isEmpty()
        ? (!brush.colorTip.isNull() ? brush.colorTip.size() : brush.tipMask.size()) : brush.tipFrames.first().rect.size();
    const qreal aspect = brush.connected.mode == BrushConnectedMode::Ribbon && !sourceSize.isEmpty()
        ? qreal(sourceSize.width()) / sourceSize.height() : 1;
    const qreal period = qMax(.1, brush.size * brush.connected.repeatLength * aspect);
    const quint64 repeatIndex = quint64(qMax(0., std::floor((state.hasPoint ? state.distance + distance : brush.connected.phaseOffset) / period)));
    const qreal width = widthAt(brush, input, repeatIndex, seed, direction);
    if (!state.hasPoint) { state = {point, input, width, direction, brush.connected.phaseOffset, true, false}; return true; }
    if (distance < 1e-8) return true;
    QVector<QImage> sources;
    if (brush.tipFrames.isEmpty()) sources.append(sourceImage(brush));
    else for (int frame = 0; frame < brush.tipFrames.size(); ++frame) sources.append(sourceImage(brush, frame));
    if (sources.isEmpty() || sources.first().isNull()) return false;
    for (auto &source : sources) source = minifiedSource(source, period, qMax(state.width, width), objectTransform);
    QCache<quint64, int> chosenFrames(4096);
    QCache<quint64, QImage> coloredRepeats(16 * 1024 * 1024);
    QCache<quint64, QColor> foregroundRepeats(4096);
    // Width dynamics bend the edges continuously; they do not reset the repeat phase.
    qreal heading = direction;
    if (state.hasDirection) {
        const qreal turn = std::remainder(direction - state.direction, 360.);
        if (std::abs(turn) < 78.) {
            const qreal radius = qMax(2., qMax(state.width, width) * .75);
            const qreal filtered = turn * -std::expm1(-distance / radius);
            const qreal limit = distance / radius * 180 / pi;
            heading = state.direction + qBound(-limit, filtered, limit);
        }
    }
    auto startNormal = normal(state.hasDirection ? state.direction : heading);
    const auto endNormal = normal(heading);
    if (QPointF::dotProduct(startNormal, normal(direction)) < .2) startNormal = endNormal;
    const QPolygonF local{state.point - startNormal * (state.width / 2), point - endNormal * (width / 2),
                         point + endNormal * (width / 2), state.point + startNormal * (state.width / 2)};
    struct Patch { QPolygonF points; bool segment = true; qreal outerV = 1; };
    QVector<Patch> patches;
    // When a corner resets the cross-section, bridge its outer wedge at the
    // existing repeat phase. Never fill transparent motif pixels with a solid cap.
    const auto previousNormal = normal(state.direction);
    if (state.hasDirection && QLineF(previousNormal,startNormal).length()>1e-6) {
        const qreal turn = std::remainder(heading-state.direction,360.);
        const qreal side = turn<0 ? 1. : -1.;
        const auto oldEdge=state.point+previousNormal*(side*state.width/2);
        const auto newEdge=state.point+startNormal*(side*state.width/2);
        const qreal v=side>0 ? 1. : 0.;
        patches.append({{state.point,oldEdge,newEdge,state.point},false,v});
    }
    patches.append({local,true});
    const QPointF center = (state.point + point) / 2;
    // Reuse foreground/flow/opacity controllers without evaluating a large stamp.
    auto modulation = brush;
    modulation.size = 1; modulation.tipMask = {}; modulation.colorTip = {}; modulation.tipFrames.clear();
    modulation.dual.enabled = false; modulation.texture.enabled = false;
    modulation.noise = false; modulation.wetEdges = false; modulation.scatter = 0;
    auto style = evaluateBrushDab(modulation, input, point, direction, direction, repeatIndex, seed, {}, BrushDabPurpose::Painting);
    style.surface = brush.surface;
    const auto instances = brushSymmetryInstances(brush.symmetry, center, canvasSize);
    for (const auto &instance : instances) for (const auto &patch : patches) {
        QPolygonF polygon;
        for (const auto &p : patch.points) polygon.append(objectTransform.map(instance.point + instance.transform.map(p - center)));
        // Affine triangles avoid projective poles and ill-conditioned mappings
        // when pen samples are much closer than the brush width.
        const auto inverseTriangle = [&](int a, int b, int c, bool &ok) {
            const auto u=polygon[b]-polygon[a], v=polygon[c]-polygon[a];
            return QTransform(u.x(),u.y(),v.x(),v.y(),polygon[a].x(),polygon[a].y()).inverted(&ok);
        };
        bool validA=false,validB=false;
        const auto inverseA=inverseTriangle(0,1,2,validA), inverseB=inverseTriangle(0,2,3,validB);
        if(!validA&&!validB) continue;
        const auto footprint = polygon.boundingRect().adjusted(-1, -1, 1, 1).intersected(bounds);
        if (footprint.isEmpty()) continue;
        if (!std::isfinite(footprint.x()) || !std::isfinite(footprint.y())
            || !std::isfinite(footprint.width()) || !std::isfinite(footprint.height())) return false;
        const auto area = footprint.toAlignedRect();
        const int left = int(std::floor(qreal(area.left()) / tileSize)), right = int(std::floor(qreal(area.right()) / tileSize));
        const int top = int(std::floor(qreal(area.top()) / tileSize)), bottom = int(std::floor(qreal(area.bottom()) / tileSize));
        if (qint64(right - left + 1) * (bottom - top + 1) > maxProjectTiles) return false;
        for (int ty = top; ty <= bottom; ++ty) for (int tx = left; tx <= right; ++tx) {
            const auto tilePart = area.intersected(QRect(tx * tileSize, ty * tileSize, tileSize, tileSize));
            if (!polygon.intersects(QPolygonF(QRectF(tilePart)))) continue;
            // A wide diagonal strip occupies only a narrow fraction of its tile
            // bounds. Tight horizontal bands avoid sampling and compositing the
            // empty square around every two-pixel geometry step.
            for (int bandY=tilePart.top(); bandY<=tilePart.bottom(); bandY+=qBound(1,rasterBandHeight,tileSize)) {
            const int bandBottom=qMin(bandY+qBound(1,rasterBandHeight,tileSize),tilePart.bottom()+1);
            qreal minX=std::numeric_limits<qreal>::infinity(), maxX=-minX;
            const auto includeX=[&](qreal x) { minX=qMin(minX,x);maxX=qMax(maxX,x); };
            // Include the shared diagonal: at a sharp turn the triangle
            // union can extend beyond a concave/self-crossing quad outline.
            for(int edge=0;edge<5;++edge) {
                const auto a=polygon[edge==4?0:edge], b=polygon[edge==4?2:(edge+1)%4];
                if(a.y()>=bandY&&a.y()<=bandBottom)includeX(a.x());
                if(a.y()==b.y())continue;
                for(qreal y:{qreal(bandY),qreal(bandBottom)}) {
                    const qreal t=(y-a.y())/(b.y()-a.y());
                    if(t>=0&&t<=1)includeX(a.x()+(b.x()-a.x())*t);
                }
            }
            if(!std::isfinite(minX)||!std::isfinite(maxX))continue;
            const int bandLeft=rasterBandHeight>=tileSize?tilePart.left():qMax(tilePart.left(),int(std::floor(minX))-1);
            const int bandRight=rasterBandHeight>=tileSize?tilePart.right():qMin(tilePart.right(),int(std::ceil(maxX))+1);
            if(bandRight<bandLeft)continue;
            const QRect part(bandLeft,bandY,bandRight-bandLeft+1,bandBottom-bandY);
            QImage mask(part.size(), QImage::Format_Grayscale8); mask.fill(0);
            QImage colors;
            if (!brush.colorTip.isNull() || brush.colorDynamics.enabled) {
                colors = QImage(part.size(), QImage::Format_ARGB32_Premultiplied); colors.fill(0);
            }
            bool painted = false;
            for (int y = 0; y < part.height(); ++y) {
                auto *coverage = mask.scanLine(y);
                auto *rgba = colors.isNull() ? nullptr : reinterpret_cast<QRgb *>(colors.scanLine(y));
                for (int x = 0; x < part.width(); ++x) {
                    const QPointF pixelPoint(part.left()+x+.5,part.top()+y+.5);
                    QPointF source; bool inside=false;
                    const auto inTriangle=[](QPointF p) { return p.x()>=-1e-7 && p.y()>=-1e-7 && p.x()+p.y()<=1+1e-7; };
                    if(validA) {
                        const auto p=inverseA.map(pixelPoint);
                        if(inTriangle(p) && (patch.segment || p.x()>1e-7)) {
                            source=patch.segment ? QPointF(state.distance+(p.x()+p.y())*distance,p.y())
                                : QPointF(state.distance,.5+(patch.outerV-.5)*(p.x()+p.y()));
                            inside=true;
                        }
                    }
                    if(!inside&&validB) {
                        const auto p=inverseB.map(pixelPoint);
                        if(inTriangle(p)) { source={state.distance+p.x()*distance,p.x()+p.y()};inside=true; }
                    }
                    if(!inside) continue;
                    // Half-open longitudinal intervals give a shared edge to
                    // exactly one segment, even for subpixel pen packets.
                    if(patch.segment && source.x()>=state.distance+distance-1e-7)continue;
                    const qreal u = source.x() / period;
                    const quint64 repeat = quint64(qMax(0., std::floor(u)));
                    int frame = 0;
                    if (sources.size() > 1) {
                        const auto *found = chosenFrames.object(repeat);
                        if (!found) {
                            frame = brushTipFrameIndex(brush, input, direction, repeat, seed);
                            chosenFrames.insert(repeat, new int(frame));
                        } else frame = *found;
                    }
                    const QImage *chosen = &sources[frame];
                    if (!brush.colorTip.isNull()) {
                        const auto *found = coloredRepeats.object(repeat);
                        if (!found) {
                            auto evaluated = brushColorTipDynamics(sources[frame], brush, repeat, seed);
                            // A valid primary atlas is at most 2048² RGBA bytes.
                            if (evaluated.sizeInBytes() > coloredRepeats.maxCost()) return false;
                            coloredRepeats.insert(repeat, new QImage(evaluated), qMax(1, int(evaluated.sizeInBytes())));
                            found = coloredRepeats.object(repeat);
                        }
                        chosen = found;
                    }
                    const auto pixel = sample(*chosen, u, source.y(), brush.connected.flipAlternate);
                    coverage[x] = uchar(qAlpha(pixel)); painted |= coverage[x] != 0;
                    if (rgba) {
                        if (!brush.colorTip.isNull()) rgba[x] = pixel;
                        else {
                            const auto *color = foregroundRepeats.object(repeat);
                            if (!color) {
                                const auto evaluated = evaluateBrushDab(modulation, input, point, direction, direction,
                                    repeat, seed, {}, BrushDabPurpose::Painting).color;
                                foregroundRepeats.insert(repeat, new QColor(evaluated)); color = foregroundRepeats.object(repeat);
                            }
                            rgba[x] = qPremultiply(qRgba(color->red(), color->green(), color->blue(), qAlpha(pixel)));
                        }
                    }
                }
            }
            if (!painted) continue;
            auto dab = style; dab.mask = std::move(mask); dab.colorTip = std::move(colors); dab.untexturedMask = {};
            dab.offset = {};
            if (brush.texture.enabled && !brush.texture.pattern.isNull()) {
                dab.untexturedMask = dab.mask; dab.canvasTexture = brush.texture;
                dab.canvasTextureDepth = brush.texture.depth * brushDynamicValue(brush.texture.dynamics, input, repeatIndex, seed, 7);
            }
            const QPointF at(part.left() + part.width() / 2., part.top() + part.height() / 2.);
            if (!applyBrushDab(layer, accumulation, original, dab, at, {}, brush.blendMode, eraser,
                               bounds, maxTiles - layer.tiles.size(), seed, selection, selectionCache)) return false;
            }
        }
    }
    state.point = point; state.input = input; state.width = width; state.direction = heading;
    state.distance += distance; state.hasDirection = true;
    return true;
}
} // namespace toon
