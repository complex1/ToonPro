#include "symbol_clips.h"
#include "document.h"
#include "project.h"
#include <QSet>
#include <algorithm>

namespace toon {
namespace {
constexpr int frameLimit = 1000000;
int layerIndex(const Shot &shot, const QString &id) {
    for (int i = 0; i < shot.layers.size(); ++i) if (shot.layers[i].id == id) return i;
    return -1;
}
bool fail(QString *error, const QString &message) {
    if (error) *error = message;
    return false;
}
bool report(Document *document, QString *error, const QString &message) {
    if (error) *error = message;
    else if (document) emit document->warning(message);
    return false;
}
bool validTiming(const SymbolInstance &instance, int duration) {
    return duration >= 1 && duration <= frameLimit
        && instance.startFrame >= 0 && instance.startFrame <= duration
        && instance.durationFrames >= -1 && instance.durationFrames <= frameLimit
        && instance.playbackOffset >= -frameLimit && instance.playbackOffset <= frameLimit
        && (instance.durationFrames < 0 || qint64(instance.startFrame) + instance.durationFrames <= duration);
}
bool ready(Document *document, const QString &layerId, QString *error) {
    if (error) error->clear();
    if (!document || !document->isOpen()) return report(document, error, "Open a project first.");
    if (document->editingGestureActive() || document->layerPropertyEditActive())
        return report(document, error, "Finish the current edit before changing the Symbol clip.");
    const auto message = symbolClipEditError(document->activeShot(), layerId);
    return message.isEmpty() || report(document, error, message);
}
bool commit(Document *document, const QString &layerId, const SymbolInstance &instance,
            const QString &label, QString *error) {
    auto after = document->project();
    auto &shot = after.shots[document->activeShotIndex()];
    const int index = layerIndex(shot, layerId);
    if (*shot.layers[index].symbol == instance) return true;
    shot.layers[index].symbol = instance;
    const auto invalid = after.validate();
    if (!invalid.isEmpty()) return report(document, error, invalid);
    QString detail;
    const auto connection = QObject::connect(document, &Document::warning, document,
        [&](const QString &message) { detail = message; });
    const bool okay = document->replaceSymbolProject(after, label);
    QObject::disconnect(connection);
    return okay || report(document, error, detail.isEmpty() ? QString("Could not change the Symbol clip.") : detail);
}
} // namespace

int symbolClipEnd(const SymbolInstance &instance, int parentDuration) {
    const int limit = qMax(0, parentDuration);
    return instance.durationFrames < 0 ? limit
        : int(qBound<qint64>(qint64(0), qint64(instance.startFrame) + instance.durationFrames, qint64(limit)));
}

bool previewSymbolClipEdit(const SymbolInstance &instance, int parentDuration,
                           SymbolClipOperation operation, int value, SymbolInstance *out, QString *error) {
    if (error) error->clear();
    if (!out || !validTiming(instance, parentDuration))
        return fail(error, "The Symbol clip must fit inside the shot before editing its timing.");
    SymbolInstance result = instance;
    const qint64 start = instance.startFrame, end = symbolClipEnd(instance, parentDuration);
    if (((operation == SymbolClipOperation::Move || operation == SymbolClipOperation::Slip) && value == 0)
        || (operation == SymbolClipOperation::TrimStart && value == start)
        || (operation == SymbolClipOperation::TrimEnd && value == end)) {
        *out = instance;
        return true;
    }
    qint64 nextStart = start, nextEnd = end, offset = instance.playbackOffset;
    switch (operation) {
    case SymbolClipOperation::Move:
        nextStart += value; nextEnd += value;
        break;
    case SymbolClipOperation::TrimStart:
        nextStart = value; offset += nextStart - start;
        break;
    case SymbolClipOperation::TrimEnd:
        nextEnd = value;
        break;
    case SymbolClipOperation::Slip:
        offset += value;
        break;
    default: return fail(error, "This Symbol clip operation is unsupported.");
    }
    if (nextStart < 0 || nextEnd < nextStart || nextEnd > parentDuration)
        return fail(error, "Keep the Symbol clip inside the shot and its end after its start.");
    if (offset < -frameLimit || offset > frameLimit)
        return fail(error, "The Symbol playback offset must stay within 1,000,000 frames.");
    result.startFrame = int(nextStart);
    result.playbackOffset = int(offset);
    if (operation != SymbolClipOperation::Slip) result.durationFrames = int(nextEnd - nextStart);
    *out = result;
    return true;
}

QVector<QPair<int, int>> symbolClipIntervals(const Shot &shot, const QString &layerId) {
    QVector<QPair<int, int>> intervals;
    const int index = layerIndex(shot, layerId);
    if (index < 0 || !shot.layers[index].symbol) return intervals;
    const auto &instance = *shot.layers[index].symbol;
    const int first = qMax(0, instance.startFrame), end = symbolClipEnd(instance, shot.durationFrames);
    if (end <= first) return intervals;
    const auto owner = animationOwner(shot, layerId);
    if (owner.drawingId.isEmpty()) intervals.append({first, end});
    else for (const auto &track : shot.animationTracks) if (track.id == owner.trackId)
        for (const auto &exposure : track.exposures) if (exposure.drawingId == owner.drawingId) {
            const int a = qMax(first, exposure.startFrame), b = qMin(end, exposure.startFrame + exposure.durationFrames);
            if (b > a) intervals.append({a, b});
        }
    std::sort(intervals.begin(), intervals.end());
    QVector<QPair<int, int>> merged;
    for (const auto &range : intervals) {
        if (!merged.isEmpty() && range.first <= merged.last().second)
            merged.last().second = qMax(merged.last().second, range.second);
        else merged.append(range);
    }
    return merged;
}

QString symbolClipEditError(const Shot &shot, const QString &layerId) {
    int index = layerIndex(shot, layerId);
    if (index < 0 || !shot.layers[index].symbol) return "Select a Symbol clip first.";
    QSet<QString> visited;
    while (index >= 0) {
        const auto &layer = shot.layers[index];
        if (visited.contains(layer.id)) return "This Symbol has invalid folder ancestry.";
        visited.insert(layer.id);
        if (layer.locked || layer.positionLocked) return "Unlock the Symbol and its folders before changing clip timing.";
        if (layer.parentId.isEmpty()) return {};
        index = layerIndex(shot, layer.parentId);
        if (index < 0) return "This Symbol's folder no longer exists.";
    }
    return {};
}

bool editSymbolClip(Document *document, const QString &layerId, SymbolClipOperation operation, int value, QString *error) {
    if (!ready(document, layerId, error)) return false;
    const auto &shot = document->activeShot();
    SymbolInstance instance;
    QString detail;
    if (!previewSymbolClipEdit(*shot.layers[layerIndex(shot, layerId)].symbol,
                              shot.durationFrames, operation, value, &instance, &detail))
        return report(document, error, detail);
    const QString label = operation == SymbolClipOperation::Move ? "Move Symbol clip"
        : operation == SymbolClipOperation::Slip ? "Slip Symbol clip" : "Trim Symbol clip";
    return commit(document, layerId, instance, label, error);
}

bool setSymbolClipTiming(Document *document, const QString &layerId, int start, int duration, int offset, QString *error) {
    if (!ready(document, layerId, error)) return false;
    const auto &shot = document->activeShot();
    auto instance = *shot.layers[layerIndex(shot, layerId)].symbol;
    instance.startFrame = start; instance.durationFrames = duration; instance.playbackOffset = offset;
    if (!validTiming(instance, shot.durationFrames))
        return report(document, error, "Keep the Symbol clip inside the shot and its offset within 1,000,000 frames.");
    return commit(document, layerId, instance, "Change Symbol clip timing", error);
}
} // namespace toon
