#include "audio.h"
#include "project.h"
#include <QCryptographicHash>
#include <QHash>
#include <QJsonObject>
#include <QSet>
#include <QUuid>
#include <QtEndian>
#include <algorithm>
#include <cmath>
#include <limits>

namespace toon {
namespace {
bool validResourceId(const QString &id) {
    if (id.size() != 64) return false;
    for (const auto ch : id) if (!((ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f'))) return false;
    return true;
}
bool validName(const QString &name) { return !name.trimmed().isEmpty() && name.size() <= 200; }
bool validFadeCurve(AudioFadeCurve curve) {
    return curve == AudioFadeCurve::Linear || curve == AudioFadeCurve::EqualPower;
}
QString fadeCurveKey(AudioFadeCurve curve) {
    return curve == AudioFadeCurve::EqualPower ? QStringLiteral("equalPower") : QStringLiteral("linear");
}
bool fadeCurveFromJson(const QJsonValue &value, AudioFadeCurve *curve) {
    if (value == "linear") { *curve = AudioFadeCurve::Linear; return true; }
    if (value == "equalPower") { *curve = AudioFadeCurve::EqualPower; return true; }
    return false;
}
double fadeLevel(qint64 position, qint64 length, AudioFadeCurve curve) {
    if (position < 0) return 0;
    if (length <= 1) return position == 0 ? 0 : 1;
    if (position >= length - 1) return 1;
    const double linear = double(position) / double(length - 1);
    return curve == AudioFadeCurve::EqualPower ? std::sin(linear * 1.57079632679489661923) : linear;
}
double fadeMultiplier(const AudioClip &clip, qint64 source) {
    double level = 1;
    if (clip.fadeInSamples) {
        const auto start = clip.fadeInStartSample < 0 ? clip.sourceOffsetSamples : clip.fadeInStartSample;
        level *= fadeLevel(source - start, clip.fadeInSamples, clip.fadeInCurve);
    }
    if (clip.fadeOutSamples) {
        const auto end = clip.fadeOutEndSample < 0 ? clip.sourceOffsetSamples + clip.sampleCount : clip.fadeOutEndSample;
        level *= fadeLevel(end - 1 - source, clip.fadeOutSamples, clip.fadeOutCurve);
    }
    return level;
}
double keyGain(const AudioClip &clip, qint64 source, qsizetype next) {
    if (!clip.gainAnimated) return clip.gain;
    if (next == 0) return clip.gainKeys.first().gain;
    if (next >= clip.gainKeys.size()) return clip.gainKeys.last().gain;
    const auto &left = clip.gainKeys[next - 1], &right = clip.gainKeys[next];
    const double fraction = double(source - left.sourceSample) / double(right.sourceSample - left.sourceSample);
    return left.gain + (right.gain - left.gain) * fraction;
}
bool playableSamples(const AudioClip &clip) {
    return clip.asset && !clip.muted && std::isfinite(clip.gain) && clip.gain >= 0 && clip.gain <= 2
        && clip.sourceOffsetSamples >= 0 && clip.sourceOffsetSamples < clip.asset->sampleFrameCount()
        && clip.sampleCount > 0 && clip.sampleCount <= clip.asset->sampleFrameCount() - clip.sourceOffsetSamples;
}
bool integer(const QJsonValue &value, qint64 minimum, qint64 maximum, qint64 *out) {
    const double n = value.toDouble(-1);
    if (!value.isDouble() || !std::isfinite(n) || n < minimum || n > maximum || std::floor(n) != n) return false;
    *out = qint64(n); return true;
}
AudioPeak samplePeak(const QByteArray &pcm, qint64 first, qint64 count) {
    AudioPeak peak;
    const auto *bytes = reinterpret_cast<const uchar *>(pcm.constData()) + first * audioBytesPerSampleFrame;
    for (qint64 i = 0; i < count * audioChannels; ++i) {
        const float value = qFromLittleEndian<qint16>(bytes + i * 2) / 32768.f;
        peak.minimum = std::min(peak.minimum, value); peak.maximum = std::max(peak.maximum, value);
    }
    return peak;
}
QString validAsset(const AudioAssetPtr &asset, bool requirePcm) {
    if (!asset || !validResourceId(asset->id) || !validName(asset->name)) return "Invalid audio asset metadata.";
    if (asset->sampleFrameCount() < 1 || asset->sampleFrameCount() > maxAudioAssetBytes / audioBytesPerSampleFrame)
        return "An audio asset must contain samples and stay within the 128 MiB decoded limit.";
    if ((requirePcm || !asset->pcm.isEmpty()) && asset->pcm.size() != asset->sampleFrameCount() * audioBytesPerSampleFrame)
        return "Audio sample data has not been loaded or does not match its metadata.";
    return {};
}
}
AudioAsset::AudioAsset(QString assetId, QString assetName, QByteArray data, qint64 frames, QVector<AudioPeak> peaks)
    : id(std::move(assetId)), name(std::move(assetName)), pcm(std::move(data)), m_sampleFrames(frames), m_peaks(std::move(peaks)) {}
AudioAssetPtr makeAudioAsset(QString name, QByteArray pcm, QString *error) {
    auto fail = [error](const QString &message) -> AudioAssetPtr { if (error) *error = message; return {}; };
    name = name.trimmed();
    if (!validName(name)) return fail("Enter an audio name between 1 and 200 characters.");
    if (pcm.isEmpty() || pcm.size() % audioBytesPerSampleFrame || pcm.size() > maxAudioAssetBytes)
        return fail("Audio must contain complete stereo sample frames within the 128 MiB decoded limit.");
    // A caller may pass fromRawData(); the immutable asset must own its bytes.
    pcm.detach();
    const qint64 frames = pcm.size() / audioBytesPerSampleFrame;
    QVector<AudioPeak> peaks; peaks.reserve((frames + audioPeakBlockSamples - 1) / audioPeakBlockSamples);
    for (qint64 at = 0; at < frames; at += audioPeakBlockSamples)
        peaks.append(samplePeak(pcm, at, std::min(qint64(audioPeakBlockSamples), frames - at)));
    const auto id = QString::fromLatin1(QCryptographicHash::hash(pcm, QCryptographicHash::Sha256).toHex());
    return AudioAssetPtr(new AudioAsset(id, std::move(name), std::move(pcm), frames, std::move(peaks)));
}
bool AudioClip::operator==(const AudioClip &other) const {
    return id == other.id && name == other.name && bool(asset) == bool(other.asset)
        && (!asset || asset->id == other.asset->id) && startFrame == other.startFrame
        && sourceOffsetSamples == other.sourceOffsetSamples && sampleCount == other.sampleCount
        && gain == other.gain && muted == other.muted
        && fadeInSamples == other.fadeInSamples && fadeOutSamples == other.fadeOutSamples
        && fadeInCurve == other.fadeInCurve && fadeOutCurve == other.fadeOutCurve
        && gainAnimated == other.gainAnimated && gainKeys == other.gainKeys
        && fadeInStartSample == other.fadeInStartSample && fadeOutEndSample == other.fadeOutEndSample;
}
bool audioClipHasFinishing(const AudioClip &clip) {
    return clip.fadeInSamples || clip.fadeOutSamples || clip.fadeInCurve != AudioFadeCurve::Linear
        || clip.fadeOutCurve != AudioFadeCurve::Linear || clip.gainAnimated || !clip.gainKeys.isEmpty()
        || clip.fadeInStartSample != -1 || clip.fadeOutEndSample != -1;
}
QString validateAudioClipFinishing(const AudioClip &clip) {
    if (!clip.asset) return "Audio finishing requires a valid source.";
    const auto frames = clip.asset->sampleFrameCount();
    if (!validFadeCurve(clip.fadeInCurve) || !validFadeCurve(clip.fadeOutCurve)) return "Unsupported audio fade curve.";
    if (clip.fadeInStartSample < -1 || clip.fadeInStartSample >= frames
        || clip.fadeOutEndSample < -1 || clip.fadeOutEndSample == 0 || clip.fadeOutEndSample > frames)
        return "Audio fade anchors must stay inside the source samples.";
    const auto inCapacity = clip.fadeInStartSample < 0 ? clip.sampleCount : frames - clip.fadeInStartSample;
    const auto outCapacity = clip.fadeOutEndSample < 0 ? clip.sampleCount : clip.fadeOutEndSample;
    if (clip.fadeInSamples < 0 || clip.fadeInSamples > inCapacity || clip.fadeOutSamples < 0 || clip.fadeOutSamples > outCapacity)
        return "Audio fades must stay inside the source or visible clip edges.";
    if (clip.gainKeys.size() > maxAudioGainKeys || (clip.gainAnimated && clip.gainKeys.isEmpty()))
        return "Audio volume animation needs 1 to 256 keys.";
    qint64 previous = -1;
    for (const auto &key : clip.gainKeys) {
        if (key.sourceSample < 0 || key.sourceSample >= frames || key.sourceSample <= previous
            || !std::isfinite(key.gain) || key.gain < 0 || key.gain > 2)
            return "Audio volume keys must be ordered source samples with volume from 0 to 200 percent.";
        previous = key.sourceSample;
    }
    return {};
}
void materializeAudioFadeAnchors(AudioClip &clip) {
    if (clip.fadeInSamples > 0 && clip.fadeInStartSample == -1) clip.fadeInStartSample = clip.sourceOffsetSamples;
    if (clip.fadeOutSamples > 0 && clip.fadeOutEndSample == -1) clip.fadeOutEndSample = clip.sourceOffsetSamples + clip.sampleCount;
}
double audioClipGainAtSourceSample(const AudioClip &clip, qint64 source) {
    if (!playableSamples(clip) || source < clip.sourceOffsetSamples || source - clip.sourceOffsetSamples >= clip.sampleCount
        || !validateAudioClipFinishing(clip).isEmpty()) return 0;
    const auto next = std::lower_bound(clip.gainKeys.cbegin(), clip.gainKeys.cend(), source,
        [](const AudioGainKey &key, qint64 value) { return key.sourceSample < value; }) - clip.gainKeys.cbegin();
    return keyGain(clip, source, next) * fadeMultiplier(clip, source);
}
void mixAudioClipSamples(const AudioClip &clip, qint64 source, qint64 count, double *output) {
    if (!output || count <= 0 || source < 0 || !playableSamples(clip) || !validateAudioClipFinishing(clip).isEmpty()
        || clip.asset->pcm.size() != clip.asset->sampleFrameCount() * audioBytesPerSampleFrame) return;
    if (source < clip.sourceOffsetSamples) {
        const auto skip = clip.sourceOffsetSamples - source;
        if (skip >= count) return;
        count -= skip; output += skip * audioChannels; source += skip;
    }
    if (source - clip.sourceOffsetSamples >= clip.sampleCount) return;
    count = std::min(count, clip.sampleCount - (source - clip.sourceOffsetSamples));
    const auto *pcm = reinterpret_cast<const uchar *>(clip.asset->pcm.constData()) + source * audioBytesPerSampleFrame;
    if (!clip.gainAnimated && !clip.fadeInSamples && !clip.fadeOutSamples) {
        if (clip.gain == 0) return;
        for (qint64 i = 0; i < count * audioChannels; ++i)
            output[i] += double(qFromLittleEndian<qint16>(pcm + i * sizeof(qint16))) * clip.gain;
        return;
    }
    auto next = std::lower_bound(clip.gainKeys.cbegin(), clip.gainKeys.cend(), source,
        [](const AudioGainKey &key, qint64 value) { return key.sourceSample < value; }) - clip.gainKeys.cbegin();
    for (qint64 i = 0; i < count; ++i) {
        const qint64 at = source + i;
        while (next < clip.gainKeys.size() && clip.gainKeys[next].sourceSample < at) ++next;
        const double gain = keyGain(clip, at, next) * fadeMultiplier(clip, at);
        output[i * 2] += double(qFromLittleEndian<qint16>(pcm + i * audioBytesPerSampleFrame)) * gain;
        output[i * 2 + 1] += double(qFromLittleEndian<qint16>(pcm + i * audioBytesPerSampleFrame + 2)) * gain;
    }
}
AudioPeak audioPeak(const AudioAsset &asset, qint64 firstSample, qint64 sampleCount) {
    AudioPeak result;
    if (firstSample < 0 || sampleCount < 1 || firstSample >= asset.sampleFrameCount() || asset.pcm.isEmpty()) return result;
    const qint64 end = firstSample + std::min(sampleCount, asset.sampleFrameCount() - firstSample);
    auto include = [&](AudioPeak peak) { result.minimum = std::min(result.minimum, peak.minimum); result.maximum = std::max(result.maximum, peak.maximum); };
    qint64 at = firstSample;
    if (at % audioPeakBlockSamples) {
        const qint64 count = std::min(end - at, audioPeakBlockSamples - at % audioPeakBlockSamples);
        include(samplePeak(asset.pcm, at, count)); at += count;
    }
    while (at + audioPeakBlockSamples <= end) {
        include(asset.m_peaks[at / audioPeakBlockSamples]); at += audioPeakBlockSamples;
    }
    if (at < end) include(samplePeak(asset.pcm, at, end - at));
    return result;
}
qint64 audioSampleFrameAtTimelineFrame(qreal frame, double fps) {
    if (!std::isfinite(frame) || frame < 0 || !std::isfinite(fps) || fps <= 0) return 0;
    const long double sample = static_cast<long double>(frame) * audioSampleRate / fps;
    if (sample >= static_cast<long double>(std::numeric_limits<qint64>::max())) return std::numeric_limits<qint64>::max();
    return qint64(std::floor(sample + .5L));
}
int audioClipDurationFrames(const AudioClip &clip, double fps) {
    if (clip.sampleCount < 1 || !std::isfinite(fps) || fps <= 0) return 0;
    const long double frames = std::ceil(static_cast<long double>(clip.sampleCount) * fps / audioSampleRate);
    return int(std::min(frames, static_cast<long double>(std::numeric_limits<int>::max())));
}
QString validateAudioClips(const Shot &shot, bool requirePcm) {
    if (shot.audioClips.size() > maxAudioClipsPerShot) return "A shot supports up to 64 audio clips.";
    QSet<QUuid> ids;
    for (const auto &clip : shot.audioClips) {
        const QUuid id(clip.id);
        if (id.isNull() || ids.contains(id) || !validName(clip.name)) return "Invalid or duplicate audio clip identity or name.";
        ids.insert(id);
        const auto error = validAsset(clip.asset, requirePcm); if (!error.isEmpty()) return error;
        if (clip.startFrame < 0 || clip.startFrame >= shot.durationFrames) return "An audio clip must start inside the shot.";
        if (clip.sourceOffsetSamples < 0 || clip.sourceOffsetSamples >= clip.asset->sampleFrameCount()
            || clip.sampleCount < 1 || clip.sampleCount > clip.asset->sampleFrameCount() - clip.sourceOffsetSamples)
            return "Audio trimming must stay inside the source samples and retain at least one sample frame.";
        if (!std::isfinite(clip.gain) || clip.gain < 0 || clip.gain > 2) return "Audio volume must be between 0 and 200 percent.";
        const auto finishingError = validateAudioClipFinishing(clip); if (!finishingError.isEmpty()) return finishingError;
    }
    return {};
}
QMap<QString, AudioAssetPtr> audioResources(const Project &project) {
    QMap<QString, AudioAssetPtr> resources;
    for (const auto &shot : project.shots) for (const auto &clip : shot.audioClips)
        if (clip.asset && !resources.contains(clip.asset->id)) resources.insert(clip.asset->id, clip.asset);
    return resources;
}
QString validateProjectAudio(const Project &project, bool requirePcm) {
    QSet<QUuid> clipIds;
    for (const auto &shot : project.shots) {
        const auto error = validateAudioClips(shot, requirePcm); if (!error.isEmpty()) return error;
        for (const auto &clip : shot.audioClips) {
            if (clipIds.contains(QUuid(clip.id))) return "Audio clip identifiers must be unique across the project.";
            clipIds.insert(QUuid(clip.id));
        }
    }
    qint64 bytes = 0;
    for (const auto &asset : audioResources(project)) {
        bytes += asset->sampleFrameCount() * audioBytesPerSampleFrame;
        if (bytes > maxProjectAudioBytes) return "Project audio exceeds the 256 MiB decoded limit.";
    }
    return {};
}
QJsonArray audioResourcesToJson(const Project &project) {
    QJsonArray result;
    for (const auto &asset : audioResources(project)) result.append(QJsonObject{{"id", asset->id}, {"name", asset->name},
        {"sampleFrames", double(asset->sampleFrameCount())}, {"sampleRate", audioSampleRate}, {"channels", audioChannels}, {"encoding", "s16le"}});
    return result;
}
bool audioResourcesFromJson(const QJsonArray &json, QMap<QString, AudioAssetPtr> *assets, QString *error) {
    auto fail = [error](const QString &message) { if (error) *error = message; return false; };
    if (!assets) return false;
    QMap<QString, AudioAssetPtr> result; qint64 bytes = 0;
    for (const auto &value : json) {
        const auto descriptor = value.toObject();
        const auto id = descriptor.value("id").toString(), name = descriptor.value("name").toString(); qint64 frames = 0;
        if (!value.isObject() || descriptor.size() != 6 || !validResourceId(id) || result.contains(id) || !validName(name) || name != name.trimmed()
            || descriptor.value("sampleRate") != audioSampleRate || descriptor.value("channels") != audioChannels || descriptor.value("encoding") != "s16le"
            || !integer(descriptor.value("sampleFrames"), 1, maxAudioAssetBytes / audioBytesPerSampleFrame, &frames))
            return fail("Invalid or duplicate audio resource descriptor.");
        bytes += frames * audioBytesPerSampleFrame;
        if (bytes > maxProjectAudioBytes) return fail("Project audio exceeds the 256 MiB decoded limit.");
        result.insert(id, AudioAssetPtr(new AudioAsset(id, name, {}, frames, {})));
    }
    *assets = std::move(result); return true;
}
QJsonArray audioClipsToJson(const QVector<AudioClip> &clips) {
    QJsonArray result;
    for (const auto &clip : clips) {
        QJsonObject object{{"id", clip.id}, {"name", clip.name}, {"assetId", clip.asset ? clip.asset->id : QString()},
        {"startFrame", clip.startFrame}, {"sourceOffsetSamples", double(clip.sourceOffsetSamples)}, {"sampleCount", double(clip.sampleCount)},
        {"gain", clip.gain}, {"muted", clip.muted}};
        if (audioClipHasFinishing(clip)) {
            QJsonArray keys;
            for (const auto &key : clip.gainKeys) keys.append(QJsonObject{{"sourceSample", double(key.sourceSample)}, {"gain", key.gain}});
            object.insert("finishing", QJsonObject{{"fadeInSamples", double(clip.fadeInSamples)}, {"fadeOutSamples", double(clip.fadeOutSamples)},
                {"fadeInCurve", fadeCurveKey(clip.fadeInCurve)}, {"fadeOutCurve", fadeCurveKey(clip.fadeOutCurve)},
                {"fadeInStartSample", double(clip.fadeInStartSample)}, {"fadeOutEndSample", double(clip.fadeOutEndSample)},
                {"gainAnimated", clip.gainAnimated}, {"gainKeys", keys}});
        }
        result.append(object);
    }
    return result;
}
bool audioClipsFromJson(const QJsonArray &json, const QMap<QString, AudioAssetPtr> &assets, QVector<AudioClip> *clips, QString *error) {
    auto fail = [error](const QString &message) { if (error) *error = message; return false; };
    if (!clips || json.size() > maxAudioClipsPerShot) return fail("A shot supports up to 64 audio clips.");
    QVector<AudioClip> result;
    for (const auto &value : json) {
        const auto object = value.toObject(); AudioClip clip; qint64 start = 0;
        clip.id = object.value("id").toString(); clip.name = object.value("name").toString(); clip.asset = assets.value(object.value("assetId").toString());
        if (!value.isObject() || object.size() != (object.contains("finishing") ? 9 : 8) || !clip.asset || !object.value("gain").isDouble() || !object.value("muted").isBool()
            || !integer(object.value("startFrame"), 0, 999999, &start)
            || !integer(object.value("sourceOffsetSamples"), 0, maxAudioAssetBytes / audioBytesPerSampleFrame, &clip.sourceOffsetSamples)
            || !integer(object.value("sampleCount"), 1, maxAudioAssetBytes / audioBytesPerSampleFrame, &clip.sampleCount))
            return fail("Invalid audio clip metadata.");
        clip.startFrame = int(start); clip.gain = object.value("gain").toDouble(); clip.muted = object.value("muted").toBool();
        if (object.contains("finishing")) {
            const auto finishing = object.value("finishing").toObject();
            const auto limit = clip.asset->sampleFrameCount();
            if (!object.value("finishing").isObject() || finishing.size() != 8
                || !integer(finishing.value("fadeInSamples"), 0, limit, &clip.fadeInSamples)
                || !integer(finishing.value("fadeOutSamples"), 0, limit, &clip.fadeOutSamples)
                || !integer(finishing.value("fadeInStartSample"), -1, limit - 1, &clip.fadeInStartSample)
                || !integer(finishing.value("fadeOutEndSample"), -1, limit, &clip.fadeOutEndSample)
                || !fadeCurveFromJson(finishing.value("fadeInCurve"), &clip.fadeInCurve)
                || !fadeCurveFromJson(finishing.value("fadeOutCurve"), &clip.fadeOutCurve)
                || !finishing.value("gainAnimated").isBool() || !finishing.value("gainKeys").isArray()
                || finishing.value("gainKeys").toArray().size() > maxAudioGainKeys)
                return fail("Invalid audio finishing metadata.");
            clip.gainAnimated = finishing.value("gainAnimated").toBool();
            for (const auto &keyValue : finishing.value("gainKeys").toArray()) {
                const auto keyObject = keyValue.toObject(); AudioGainKey key;
                if (!keyValue.isObject() || keyObject.size() != 2 || !keyObject.value("gain").isDouble()
                    || !integer(keyObject.value("sourceSample"), 0, limit - 1, &key.sourceSample))
                    return fail("Invalid audio volume key metadata.");
                key.gain = keyObject.value("gain").toDouble(); clip.gainKeys.append(key);
            }
            const auto validation = validateAudioClipFinishing(clip);
            if (!validation.isEmpty()) return fail(validation);
        }
        result.append(std::move(clip));
    }
    *clips = std::move(result); return true;
}
qint64 audioHistoryCost(const Project &before, const Project &after) {
    const auto previous = audioResources(before), current = audioResources(after); qint64 cost = 0;
    for (auto it = previous.cbegin(); it != previous.cend(); ++it) if (!current.contains(it.key())) cost += it.value()->pcm.size();
    for (auto it = current.cbegin(); it != current.cend(); ++it) if (!previous.contains(it.key())) cost += it.value()->pcm.size();
    QHash<QString, const Shot *> previousShots, currentShots;
    for (const auto &shot : before.shots) previousShots.insert(shot.id, &shot);
    for (const auto &shot : after.shots) currentShots.insert(shot.id, &shot);
    const auto metadataCost = [](const Shot &shot) {
        qint64 bytes = 0;
        for (const auto &clip : shot.audioClips) bytes += 256 + clip.gainKeys.size() * qint64(sizeof(AudioGainKey));
        return bytes;
    };
    for (const auto &shot : before.shots) {
        const Shot *next = currentShots.value(shot.id);
        if (!next || next->audioClips != shot.audioClips) cost += metadataCost(shot);
    }
    for (const auto &shot : after.shots) {
        const Shot *previousShot = previousShots.value(shot.id);
        if (!previousShot || previousShot->audioClips != shot.audioClips) cost += metadataCost(shot);
    }
    return cost;
}
} // namespace toon
