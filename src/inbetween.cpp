#include "inbetween.h"
#include "effect_render.h"
#include <QPainter>
#include <algorithm>
#include <cmath>

namespace toon {
namespace {
const Shot *findShot(const Project &project, const QString &id) {
    for (const auto &shot : project.shots) if (shot.id == id) return &shot;
    return nullptr;
}
bool sourceLayer(const QVector<RasterLayer> &layers, int index, const QString &root) {
    return layers[index].id == root || layerIsDescendant(layers, index, root);
}
QString sourceError(const Shot &shot, const AnimationTrack &track, const CelExposure &cel) {
    const auto *drawing = celDrawing(track, cel.drawingId);
    if (!drawing) return "A selected drawing is unavailable.";
    const auto editError = celDrawingEditError(shot, track.id, cel.drawingId);
    if (!editError.isEmpty()) return editError;
    int rootIndex = -1;
    for (int i = 0; i < shot.layers.size(); ++i) if (shot.layers[i].id == track.rootGroupLayerId) rootIndex = i;
    if (rootIndex < 0) return "The animation track is unavailable.";
    for (int i = 0; i < shot.layers.size(); ++i) {
        const auto &layer = shot.layers[i];
        const bool ancestor = layer.id == track.rootGroupLayerId
            || layerIsDescendant(shot.layers, rootIndex, layer.id);
        if (!ancestor && !sourceLayer(shot.layers, i, drawing->groupLayerId)) continue;
        if (layer.kind == LayerKind::Scene3D || layer.kind == LayerKind::Symbol || layer.annotation)
            return "Choose drawing cels without 3D, Symbols or Notes layers.";
        for (const auto &animation : shot.layerAnimations) if (animation.layerId == layer.id)
            return "Choose drawing cels without animated layer properties.";
        // A root/ancestor effect would otherwise be applied twice to the baked
        // result, or would make the two detached source groups incomparable.
        if (ancestor && (!layer.visible || layer.clipped || layer.mask || layer.adjustment
            || hasLayerEffects(layer.effects) || layer.opacity != 1 || layer.blendMode != LayerBlendMode::Normal))
            return "Use a visible, normal animation track without masks, effects or opacity changes.";
    }
    return {};
}
}

InbetweenPlan planInbetweens(const Project &project, const QString &shotId,
        const QVector<TimelineCelRef> &refs, int step, const CubicEasing &curve, QString *error, InbetweenOutput output) {
    if (error) error->clear();
    const auto fail = [&](const QString &reason) { if (error) *error = reason; return InbetweenPlan{}; };
    const auto *shot = findShot(project, shotId);
    if (!shot || refs.size() != 2) return fail("Select two drawing cels on the same animation track.");
    if (refs[0].trackId != refs[1].trackId || refs[0] == refs[1])
        return fail("Select two different drawing cels on the same animation track.");
    if ((step != 1 && step != 2) || !validCubicEasing(curve)) return fail("Choose a valid drawing frequency and easing curve.");
    if (output != InbetweenOutput::WhiteBackground && output != InbetweenOutput::TransparentInk)
        return fail("Choose a valid output background.");
    const auto *a = celExposure(*shot, refs[0]), *b = celExposure(*shot, refs[1]);
    const auto *track = celTrack(*shot, refs[0].trackId);
    if (!a || !b || !track) return fail("The selected poses are no longer available.");
    auto first = refs[0], last = refs[1];
    if (a->startFrame > b->startFrame) { std::swap(a, b); std::swap(first, last); }
    if (a->drawingId == b->drawingId) return fail("Select two distinct pose drawings.");
    if (b->startFrame - a->startFrame <= step) return fail("Leave room between the poses for an in-between drawing.");
    for (const auto &ref : {first, last}) {
        const auto reason = celEditError(*shot, ref);
        if (!reason.isEmpty()) return fail(reason);
    }
    for (const auto *cel : {a, b}) {
        const auto reason = sourceError(*shot, *track, *cel);
        if (!reason.isEmpty()) return fail(reason);
    }
    for (const auto &cel : track->exposures) {
        if (cel.id == a->id || cel.id == b->id) continue;
        if (cel.startFrame < b->startFrame && cel.startFrame + cel.durationFrames > a->startFrame)
            return fail("The interval contains another drawing. Choose consecutive poses.");
    }
    InbetweenPlan plan{shotId, first, last, {project.width, project.height}, a->startFrame, b->startFrame, step, curve, {}};
    plan.output = output;
    if (project.width <= 0 || project.height <= 0 || qint64(project.width) * project.height > 16000000)
        return fail("In-betweens support canvases up to 16 megapixels.");
    const qint64 count = (qint64(plan.lastFrame) - plan.firstFrame - 1) / step;
    qint64 drawings = 0, exposures = 0;
    for (const auto &entry : shot->animationTracks) { drawings += entry.drawings.size(); exposures += entry.exposures.size(); }
    if (count > maxInbetweenDrawings || drawings + count > maxAnimationDrawings
        || shot->layers.size() + count * 2 > maxAnimationLayers
        || exposures + count > maxAnimationExposures)
        return fail("This interval exceeds the drawing or timeline limits. Use fewer in-betweens.");
    const qint64 tiles = qint64((project.width + tileSize - 1) / tileSize) * ((project.height + tileSize - 1) / tileSize);
    qint64 used = 0;
    for (const auto *artwork : projectArtworkShots(project)) for (const auto &layer : artwork->layers) used += layer.tiles.size();
    if (used + tiles * count > maxProjectTiles)
        return fail("The generated drawings would exceed the project pixel budget. Use fewer drawings or a smaller canvas.");
    // Leave headroom for the batch metadata within the existing Undo budget.
    if (tiles * count * tileSize * tileSize * 4 > 60LL * 1024 * 1024)
        return fail("This batch is too large for one undoable edit. Use fewer drawings or a smaller canvas.");
    for (int frame = plan.firstFrame + step; frame < plan.lastFrame; frame += step) {
        const qreal progress = qreal(frame - plan.firstFrame) / (plan.lastFrame - plan.firstFrame);
        plan.samples.append({frame, std::min(step, plan.lastFrame - frame),
            keyframeProgress(progress, KeyframeInterpolation::Linear, curve)});
    }
    return plan;
}

bool inbetweenImageOpaque(const QImage &image) {
    if (image.isNull()) return false;
    if (!image.hasAlphaChannel()) return true;
    const auto pixels = image.convertToFormat(QImage::Format_ARGB32);
    for (int y = 0; y < pixels.height(); ++y) {
        const auto *row = reinterpret_cast<const QRgb *>(pixels.constScanLine(y));
        for (int x = 0; x < pixels.width(); ++x) if (qAlpha(row[x]) != 255) return false;
    }
    return true;
}

QImage renderInbetweenSource(const Project &project, const InbetweenPlan &plan, bool last, QString *error) {
    if (error) error->clear();
    const auto fail = [&](const QString &reason) { if (error) *error = reason; return QImage{}; };
    QString reason;
    const auto checked = planInbetweens(project, plan.shotId, {plan.first, plan.last}, plan.step, plan.easing, &reason, plan.output);
    if (!reason.isEmpty() || checked != plan) return fail(reason.isEmpty() ? "The source poses changed." : reason);
    const auto &shot = *findShot(project, plan.shotId);
    const auto &ref = last ? plan.last : plan.first;
    const auto &track = *celTrack(shot, ref.trackId);
    const auto &drawing = *celDrawing(track, celExposure(shot, ref)->drawingId);
    QVector<RasterLayer> layers;
    for (int i = 0; i < shot.layers.size(); ++i) if (sourceLayer(shot.layers, i, drawing.groupLayerId)) {
        auto layer = shot.layers[i];
        if (layer.id == drawing.groupLayerId) layer.parentId.clear();
        layers.append(std::move(layer));
    }
    QImage image(plan.size, QImage::Format_ARGB32_Premultiplied);
    if (image.isNull()) return fail("Not enough memory to render the source poses.");
    image.fill(Qt::transparent);
    clearEffectRenderError();
    { QPainter painter(&image); paintLayers(painter, layers, false); }
    const auto renderError = effectRenderError();
    if (!renderError.isEmpty()) return fail(renderError);
    return image;
}

QImage inbetweenWhiteMatte(const QImage &source) {
    if (source.isNull()) return {};
    QImage image(source.size(), QImage::Format_RGB32); image.fill(Qt::white);
    { QPainter painter(&image); painter.drawImage(QPoint(), source); }
    return image;
}
QColor inbetweenInkColor(const QImage &first, const QImage &last) {
    QColor ink(Qt::black); int best = 0;
    for (const auto *source : {&first, &last}) {
        const auto image=source->convertToFormat(QImage::Format_ARGB32);
        for (int y=0;y<image.height();++y) {
            const auto *row=reinterpret_cast<const QRgb *>(image.constScanLine(y));
            for (int x=0;x<image.width();++x) {
                const auto pixel=row[x];
                const int score=qAlpha(pixel)*(255-std::min({qRed(pixel),qGreen(pixel),qBlue(pixel)}));
                if (score>best) { best=score; ink=QColor::fromRgb(qRed(pixel),qGreen(pixel),qBlue(pixel)); }
            }
        }
    }
    return ink;
}
QImage inbetweenTransparentInk(const QImage &whiteMatted, QColor ink) {
    if (whiteMatted.isNull() || !ink.isValid()) return {};
    const auto input=whiteMatted.convertToFormat(QImage::Format_RGB32);
    QImage result(input.size(),QImage::Format_ARGB32_Premultiplied);
    if (result.isNull()) return {};
    const double dr=255-ink.red(), dg=255-ink.green(), db=255-ink.blue();
    const double length=dr*dr+dg*dg+db*db;
    for (int y=0;y<input.height();++y) {
        const auto *row=reinterpret_cast<const QRgb *>(input.constScanLine(y));
        auto *dest=reinterpret_cast<QRgb *>(result.scanLine(y));
        for (int x=0;x<input.width();++x) {
            const auto pixel=row[x];
            const int alpha=length>0?qBound(0,qRound(255*(dr*(255-qRed(pixel))+dg*(255-qGreen(pixel))+db*(255-qBlue(pixel)))/length),255):0;
            dest[x]=qPremultiply(qRgba(ink.red(),ink.green(),ink.blue(),alpha));
        }
    }
    return result;
}
} // namespace toon
