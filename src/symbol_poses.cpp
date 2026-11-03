#include "symbol_poses.h"
#include "document.h"
#include "symbol_clips.h"
#include "symbols.h"
#include <QHash>
#include <QJsonObject>
#include <algorithm>
#include <cmath>
#include <limits>

namespace toon {
namespace {
bool fail(QString *error, const QString &message) { if (error) *error = message; return false; }
bool report(Document *document, QString *error, const QString &message) {
    if (error) *error = message; else if (document) emit document->warning(message); return false;
}
int indexOf(const Shot &shot, const QString &id) {
    for (int i = 0; i < shot.layers.size(); ++i) if (shot.layers[i].id == id) return i;
    return -1;
}
bool ready(Document *document, const QString &layerId, int frame, QString *error) {
    if (error) error->clear();
    if (!document || !document->isOpen()) return report(document,error,"Open a project first.");
    if (document->editingGestureActive() || document->layerPropertyEditActive())
        return report(document,error,"Finish the current edit before changing Symbol poses.");
    const auto reason = symbolPoseEditError(document->activeShot(),layerId);
    if (!reason.isEmpty()) return report(document,error,reason);
    if (frame < 0 || frame >= document->activeShot().durationFrames)
        return report(document,error,"Choose a pose-key frame inside the shot.");
    return true;
}
bool commit(Document *document, const QString &layerId, const SymbolInstance &instance, const QString &label, QString *error) {
    const int index = indexOf(document->activeShot(),layerId);
    if (*document->activeShot().layers[index].symbol == instance) return true;
    auto project = document->project(); project.shots[document->activeShotIndex()].layers[index].symbol = instance;
    const auto invalid = project.validate(); if (!invalid.isEmpty()) return report(document,error,invalid);
    QString detail; const auto connection = QObject::connect(document,&Document::warning,document,[&](const QString &message) { detail = message; });
    const bool okay = document->replaceSymbolProject(project,label); QObject::disconnect(connection);
    return okay || report(document,error,detail.isEmpty() ? QString("Could not change the Symbol pose.") : detail);
}
}
int symbolPoseClockFrame(const SymbolInstance &instance, int parentFrame) {
    return int(qBound<qint64>(qint64(std::numeric_limits<int>::min()),qint64(parentFrame)-instance.startFrame+instance.playbackOffset,qint64(std::numeric_limits<int>::max())));
}
int symbolPoseParentFrame(const SymbolInstance &instance, int clockFrame) {
    return int(qBound<qint64>(qint64(std::numeric_limits<int>::min()),qint64(clockFrame)+instance.startFrame-instance.playbackOffset,qint64(std::numeric_limits<int>::max())));
}
const SymbolPoseKey *symbolPoseKeyAtParent(const SymbolInstance &instance, int parentFrame) {
    const int clock = symbolPoseClockFrame(instance,parentFrame);
    const auto at = std::lower_bound(instance.poseKeys.cbegin(),instance.poseKeys.cend(),clock,[](const auto &key,int frame) { return key.frame < frame; });
    return at != instance.poseKeys.cend() && at->frame == clock ? &*at : nullptr;
}
int evaluatedSymbolPose(const SymbolInstance &instance, int parentFrame) {
    const int clock = symbolPoseClockFrame(instance,parentFrame);
    const auto after = std::upper_bound(instance.poseKeys.cbegin(),instance.poseKeys.cend(),clock,[](int frame,const auto &key) { return frame < key.frame; });
    return after == instance.poseKeys.cbegin() ? instance.firstFrame : std::prev(after)->poseFrame;
}
QString validateSymbolPoses(const SymbolInstance &instance, int sourceDuration) {
    if (instance.poseKeys.size() > maxSymbolPoseKeys) return "A Symbol instance supports up to 4096 pose keys.";
    int previous = minSymbolPoseClock - 1;
    for (const auto &key : instance.poseKeys) {
        if (key.frame < minSymbolPoseClock || key.frame > maxSymbolPoseClock || key.frame <= previous)
            return "Symbol pose keys need unique, ordered clock frames within ±2,000,000.";
        if (key.poseFrame < 0 || key.poseFrame >= sourceDuration)
            return "A Symbol pose key refers to a frame outside its source Symbol.";
        previous = key.frame;
    }
    return {};
}
QJsonArray symbolPoseKeysToJson(const QVector<SymbolPoseKey> &keys) {
    QJsonArray result; for (const auto &key : keys) result.append(QJsonObject{{"frame",key.frame},{"poseFrame",key.poseFrame}}); return result;
}
QString validateProjectSymbolPoses(const Project &project) {
    int count = 0;
    for (const auto *shot : projectArtworkShots(project)) for (const auto &layer : shot->layers)
        if (layer.symbol && (layer.symbol->playback == SymbolPlayback::Poses || !layer.symbol->poseKeys.isEmpty())) {
            const auto *source = findSymbol(project,layer.symbol->symbolId);
            if (!source) return "A Symbol pose refers to a missing source Symbol.";
            const auto error = validateSymbolPoses(*layer.symbol,source->content.durationFrames);
            if (!error.isEmpty()) return error;
            count += layer.symbol->poseKeys.size();
            if (count > maxProjectSymbolPoseKeys) return "A project supports up to 65536 Symbol pose keys.";
        }
    return {};
}
bool symbolPoseKeysFromJson(const QJsonArray &array, QVector<SymbolPoseKey> *keys, QString *error) {
    if (error) error->clear();
    if (!keys || array.size() > maxSymbolPoseKeys) return fail(error,"A Symbol instance supports up to 4096 pose keys.");
    QVector<SymbolPoseKey> parsed; int previous = minSymbolPoseClock - 1;
    const auto integer = [](const QJsonValue &value, int lo, int hi, int *out) {
        const double number = value.toDouble();
        if (!value.isDouble() || !std::isfinite(number) || number != std::floor(number) || number < lo || number > hi) return false;
        *out = int(number); return true;
    };
    for (const auto &value : array) {
        const auto object = value.toObject(); SymbolPoseKey key;
        if (!value.isObject() || object.size() != 2 || !integer(object.value("frame"),minSymbolPoseClock,maxSymbolPoseClock,&key.frame)
            || !integer(object.value("poseFrame"),0,999999,&key.poseFrame) || key.frame <= previous)
            return fail(error,"Invalid, duplicate or unordered Symbol pose-key data.");
        previous = key.frame; parsed.append(key);
    }
    *keys = std::move(parsed); return true;
}
qint64 symbolPoseHistoryCost(const Shot &before, const Shot &after) {
    QHash<QString,QVector<SymbolPoseKey>> old;
    for (const auto &layer : before.layers) if (layer.symbol) old.insert(layer.id,layer.symbol->poseKeys);
    qint64 bytes = 0;
    for (const auto &layer : after.layers) if (layer.symbol) {
        const auto previous = old.take(layer.id);
        if (previous != layer.symbol->poseKeys) bytes += (previous.size()+layer.symbol->poseKeys.size())*sizeof(SymbolPoseKey);
    }
    for (const auto &keys : old) bytes += keys.size()*sizeof(SymbolPoseKey);
    return bytes;
}
QString symbolPoseEditError(const Shot &shot, const QString &layerId) { return symbolClipEditError(shot,layerId); }
bool setSymbolPose(Document *document,const QString &layerId,int parentFrame,int poseFrame,QString *error) {
    if (!ready(document,layerId,parentFrame,error)) return false;
    auto instance = *document->activeShot().layers[indexOf(document->activeShot(),layerId)].symbol;
    const auto *definition = findSymbol(document->project(),instance.symbolId);
    if (!definition || poseFrame < 0 || poseFrame >= definition->content.durationFrames)
        return report(document,error,"Choose a pose inside the source Symbol.");
    const int clock = symbolPoseClockFrame(instance,parentFrame);
    if (clock < minSymbolPoseClock || clock > maxSymbolPoseClock) return report(document,error,"The pose-key clock exceeds its supported range.");
    auto at = std::lower_bound(instance.poseKeys.begin(),instance.poseKeys.end(),clock,[](const auto &key,int frame) { return key.frame < frame; });
    if (at != instance.poseKeys.end() && at->frame == clock) at->poseFrame = poseFrame;
    else instance.poseKeys.insert(at,{clock,poseFrame});
    instance.playback = SymbolPlayback::Poses;
    return commit(document,layerId,instance,"Set Symbol pose",error);
}
bool removeSymbolPose(Document *document,const QString &layerId,int parentFrame,QString *error) {
    if (!ready(document,layerId,parentFrame,error)) return false;
    auto instance = *document->activeShot().layers[indexOf(document->activeShot(),layerId)].symbol;
    const int clock = symbolPoseClockFrame(instance,parentFrame);
    if (!instance.poseKeys.removeIf([&](const auto &key) { return key.frame == clock; }))
        return report(document,error,"There is no Symbol pose key at this frame.");
    return commit(document,layerId,instance,"Delete Symbol pose",error);
}
} // namespace toon
