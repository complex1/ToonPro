#include "document.h"
#include "inbetween.h"
#include <QUuid>
#include <algorithm>

namespace toon {
bool Document::insertInbetweens(const InbetweenPlan &plan, const QVector<QImage> &images, quint64 expectedRevision) {
    const auto fail = [this](const QString &reason) { emit warning(reason); return false; };
    if (!isOpen() || loading() || editingGestureActive() || layerPropertyEditActive())
        return fail("Finish the current edit before inserting in-betweens.");
    if (contentRevision() != expectedRevision || activeShotId() != plan.shotId)
        return fail("The document changed. Generate a new preview before inserting.");
    QString error;
    const auto checked = planInbetweens(project(), plan.shotId, {plan.first, plan.last}, plan.step, plan.easing, &error, plan.output);
    if (!error.isEmpty() || checked != plan) return fail(error.isEmpty() ? "Generate a new preview for these poses." : error);
    if (images.size() != plan.samples.size() || images.isEmpty()) return fail("Generate all in-between drawings before inserting.");
    for (const auto &image : images) if (image.isNull() || image.size() != plan.size
        || (plan.output == InbetweenOutput::WhiteBackground && !inbetweenImageOpaque(image)))
        return fail("The generated drawings have an invalid size or transparency.");
    auto after = project(); auto &shot = after.shots[activeShotIndex()];
    auto track = std::find_if(shot.animationTracks.begin(), shot.animationTracks.end(),
        [&](const auto &entry) { return entry.id == plan.first.trackId; });
    for (auto &cel : track->exposures) if (cel.id == plan.first.exposureId)
        cel.durationFrames = plan.step;
    const auto id = [] { return QUuid::createUuid().toString(QUuid::WithoutBraces); };
    QString selected;
    for (int i = 0; i < images.size(); ++i) {
        auto group = RasterLayer::create(QString("In-between %1").arg(plan.samples[i].frame + 1));
        group.group = true; group.parentId = track->rootGroupLayerId;
        auto layer = RasterLayer::create("Generated drawing"); layer.parentId = group.id;
        const auto image = images[i].convertToFormat(QImage::Format_ARGB32_Premultiplied);
        for (int y = 0; y < image.height(); y += tileSize) for (int x = 0; x < image.width(); x += tileSize) {
            QImage tile(tileSize, tileSize, QImage::Format_ARGB32_Premultiplied); tile.fill(Qt::transparent);
            { QPainter painter(&tile); painter.drawImage(QPoint(), image, QRect(x, y, tileSize, tileSize)); }
            layer.tiles.insert({x / tileSize, y / tileSize}, tile);
        }
        AnimationDrawing drawing{id(), group.id, {}};
        // Reuse the first raster template role when available. A flattened
        // generated cel intentionally has one paint layer, not fake source roles.
        for (const auto &slot : track->drawingTemplate) if (slot.kind == LayerKind::Raster) {
            drawing.templateLayers.insert(slot.id, layer.id); break;
        }
        track->drawings.append(drawing);
        track->exposures.append({id(), drawing.id, plan.samples[i].frame, plan.samples[i].hold});
        if (selected.isEmpty()) selected = layer.id;
        shot.layers.append(group); shot.layers.append(layer);
    }
    std::sort(track->exposures.begin(), track->exposures.end(), [](const auto &a, const auto &b) { return a.startFrame < b.startFrame; });
    const auto validation = after.validate();
    if (!validation.isEmpty()) return fail(validation);
    return commitCelChange(std::move(after), "Generate in-between drawings", selected);
}
} // namespace toon
