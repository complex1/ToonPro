#include "animation_render.h"
#include "animation.h"
#include "keyframe_render.h"
#include "sheet.h"
#include "symbols.h"
#include <QDir>
#include <QFileInfo>
#include <algorithm>

namespace toon {
namespace {
const AnimationTrack *findTrack(const Shot &shot, const QString &id) {
    for (const auto &track : shot.animationTracks) if (track.id == id) return &track;
    return nullptr;
}
}
QVector<OnionSkinSample> onionSkinSamples(const Shot &shot, int frame, const QString &trackId,
                                        int direction, int count) {
    const auto *track = findTrack(shot, trackId);
    if (!track || (direction != -1 && direction != 1) || count <= 0) return {};
    count = qMin(count, maxOnionDrawings);
    QHash<QString, const RasterLayer *> layerById;
    for (const auto &layer : shot.layers) layerById.insert(layer.id, &layer);
    const auto visibleGroup = [&layerById](QString id) {
        for (int depth = 0; !id.isEmpty() && depth < 9; ++depth) {
            const auto *layer = layerById.value(id, nullptr);
            if (!layer || !layer->visible) return false;
            id = layer->parentId;
        }
        return id.isEmpty();
    };
    if (!visibleGroup(track->rootGroupLayerId)) return {};
    const auto *current = drawingAtFrame(*track, frame);
    QSet<QString> seen;
    if (current) seen.insert(current->id);
    QVector<const CelExposure *> candidates;
    for (const auto &exposure : track->exposures) {
        if ((direction < 0 && exposure.startFrame + exposure.durationFrames <= frame)
            || (direction > 0 && exposure.startFrame > frame)) candidates.append(&exposure);
    }
    std::sort(candidates.begin(), candidates.end(), [direction](auto *a, auto *b) {
        return direction < 0 ? a->startFrame > b->startFrame : a->startFrame < b->startFrame;
    });
    QVector<OnionSkinSample> samples;
    for (const auto *exposure : candidates) {
        if (seen.contains(exposure->drawingId)) continue;
        const auto drawing = std::find_if(track->drawings.cbegin(), track->drawings.cend(),
            [exposure](const auto &candidate) { return candidate.id == exposure->drawingId; });
        if (drawing == track->drawings.cend() || !visibleGroup(drawing->groupLayerId)) continue;
        seen.insert(exposure->drawingId);
        samples.append({drawing->id, direction < 0
            ? exposure->startFrame + exposure->durationFrames - 1 : exposure->startFrame});
        if (samples.size() == count) break;
    }
    return samples;
}
QVector<RasterLayer> onionSkinLayersForSample(const Shot &shot, int currentFrame, const QString &trackId,
                                            const OnionSkinSample &sample, QRectF bounds) {
    const auto *track = findTrack(shot, trackId);
    if (!track || sample.frame < 0 || sample.frame >= shot.durationFrames) return {};
    const auto *drawing = drawingAtFrame(*track, sample.frame);
    if (!drawing || drawing->id != sample.drawingId) return {};
    const auto owners = animationOwners(shot);
    // A nested track needs its ordinary ancestors for the same group effects,
    // masks and world transforms as the current drawing; omit their siblings.
    QSet<QString> ancestors;
    QString ancestor=track->rootGroupLayerId;
    for(int depth=0;!ancestor.isEmpty() && depth<=8;++depth) {
        if(ancestors.contains(ancestor))return {};
        ancestors.insert(ancestor);QString parent;
        for(const auto &layer:shot.layers)if(layer.id==ancestor){parent=layer.parentId;break;}
        ancestor=parent;
    }
    QVector<RasterLayer> layers;
    for (const auto &layer : shot.layers) {
        if (ancestors.contains(layer.id) || owners.value(layer.id).drawingId == drawing->id) layers.append(layer);
    }
    // Neighbor drawings and their layer/path poses share the current camera.
    // Keep the source crop in this view's world coordinates: a current pan can
    // reveal vector geometry outside the neighbor's own camera framing.
    auto evaluatedShot = shot;
    evaluatedShot.camera2D = evaluateShotCamera(shot, currentFrame);
    evaluatedShot.camera2DKeys.clear();
    if (!applyLayerAnimations(evaluatedShot, sample.frame, layers, bounds)) return {};
    return layers;
}
QVector<RasterLayer> onionSkinLayers(const Shot &shot, int frame, const QString &trackId, int direction, QRectF bounds) {
    const auto samples = onionSkinSamples(shot, frame, trackId, direction, 1);
    return samples.isEmpty() ? QVector<RasterLayer>{}
        : onionSkinLayersForSample(shot, frame, trackId, samples.first(), bounds);
}
QVector<RasterLayer> onionSkinLayersForSample(const Project &project,const Shot &shot,int currentFrame,
                                            const QString &trackId,const OnionSkinSample &sample,QRectF bounds) {
    auto layers=onionSkinLayersForSample(shot,currentFrame,trackId,sample,bounds);
    if(!resolveSymbolLayers(project,shot,sample.frame,layers,bounds))return {};
    return layers;
}
QString animationDisplayKey(const Shot &shot, int frame, const QString &onionTrackId,
                            const OnionSkinSettings &settings) {
    QString key;
    if (!shot.layerAnimations.isEmpty() || !shot.camera2DKeys.isEmpty() || !shot.sceneCameraAnimations.isEmpty()
        || std::any_of(shot.layers.cbegin(),shot.layers.cend(),[](const auto &layer){return layer.symbol.has_value();}))
        key = QString::number(frame) + ':';
    for (const auto &track : shot.animationTracks) {
        const auto *drawing = drawingAtFrame(track, frame);
        key += track.id + ':' + (drawing ? drawing->id : QString()) + ';';
        if (track.id == onionTrackId) for (int direction : {-1, 1}) {
            const int count = direction < 0 ? settings.previousCount : settings.nextCount;
            for (const auto &sample : onionSkinSamples(shot, frame, track.id, direction, count))
                key += sample.drawingId + ',';
            key += ';';
        }
    }
    return key;
}
bool exportPngSequence(const Project &project, int shotIndex, int first, int last, const QString &directory,
                       const std::function<bool(int,int)> &progress, QString *error) {
    auto fail = [error](const QString &message) { if (error) *error = message; return false; };
    if (error) error->clear();
    if (shotIndex < 0 || shotIndex >= project.shots.size() || first < 0 || last < first
        || last >= project.shots[shotIndex].durationFrames) return fail("Choose a valid frame range to export.");
    const auto validation = project.validate();
    if (!validation.isEmpty()) return fail(validation);
    QDir dir(directory);
    if (directory.isEmpty() || (dir.exists() && !dir.entryList(QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot).isEmpty()))
        return fail("Choose an empty folder for the PNG sequence. Existing files were preserved.");
    if (!dir.exists() && !QDir().mkpath(directory)) return fail("Could not create the PNG sequence folder.");
    const int count = last - first + 1;
    const int digits = qMax(4, QString::number(last + 1).size());
    for (int frame = first; frame <= last; ++frame) {
        if (progress && !progress(frame - first, count)) return fail("Export cancelled. Completed frames remain in the export folder.");
        const auto path = dir.filePath(QString("frame-%1.png").arg(frame + 1, digits, 10, QLatin1Char('0')));
        if (QFileInfo::exists(path)) return fail("An output frame already exists. Export stopped without replacing it.");
        QString detail;
        const auto image = renderMovieFrame(project, frame, shotIndex);
        if (image.isNull() && !keyframeRenderError().isEmpty())
            return fail("Could not export frame " + QString::number(frame + 1) + ": " + keyframeRenderError());
        if (!savePng(image, path, &detail)) return fail("Could not export frame " + QString::number(frame + 1) + ": " + detail);
    }
    if (progress) progress(count, count);
    return true;
}
} // namespace toon
