#pragma once

#include <QByteArray>
#include <QJsonArray>
#include <QMap>
#include <QString>
#include <QVector>
#include <memory>

namespace toon {
struct Project;
struct Shot;
inline constexpr int audioSampleRate = 48000;
inline constexpr int audioChannels = 2;
inline constexpr int audioBytesPerSampleFrame = 4;
inline constexpr int audioPeakBlockSamples = 256;
inline constexpr qint64 maxAudioAssetBytes = 128LL * 1024 * 1024;
inline constexpr qint64 maxProjectAudioBytes = 256LL * 1024 * 1024;
inline constexpr int maxAudioClipsPerShot = 64;
inline constexpr int maxAudioGainKeys = 256;
enum class AudioFadeCurve { Linear, EqualPower };
struct AudioGainKey {
    qint64 sourceSample = 0;
    double gain = 1;
    bool operator==(const AudioGainKey &) const = default;
};
struct AudioPeak { float minimum = 0, maximum = 0; };
class AudioAsset;
using AudioAssetPtr = std::shared_ptr<const AudioAsset>;
class AudioAsset final {
public:
    const QString id, name;
    const QByteArray pcm; // Interleaved 48000 Hz stereo signed 16-bit little-endian.
    qint64 sampleFrameCount() const { return m_sampleFrames; }
private:
    AudioAsset(QString id, QString name, QByteArray pcm, qint64 frames, QVector<AudioPeak> peaks);
    const qint64 m_sampleFrames;
    const QVector<AudioPeak> m_peaks;
    friend AudioAssetPtr makeAudioAsset(QString, QByteArray, QString *);
    friend AudioPeak audioPeak(const AudioAsset &, qint64, qint64);
    friend bool audioResourcesFromJson(const QJsonArray &, QMap<QString, AudioAssetPtr> *, QString *);
};
struct AudioClip {
    QString id, name;
    AudioAssetPtr asset;
    int startFrame = 0;
    qint64 sourceOffsetSamples = 0, sampleCount = 0;
    double gain = 1;
    bool muted = false;
    qint64 fadeInSamples = 0, fadeOutSamples = 0;
    AudioFadeCurve fadeInCurve = AudioFadeCurve::Linear, fadeOutCurve = AudioFadeCurve::Linear;
    bool gainAnimated = false;
    QVector<AudioGainKey> gainKeys;
    // -1 follows the current visible edge until a trim materializes the source anchor.
    qint64 fadeInStartSample = -1, fadeOutEndSample = -1;
    bool operator==(const AudioClip &other) const;
};
bool audioClipHasFinishing(const AudioClip &clip);
QString validateAudioClipFinishing(const AudioClip &clip);
void materializeAudioFadeAnchors(AudioClip &clip);
double audioClipGainAtSourceSample(const AudioClip &clip, qint64 sourceSample);
// Add unclipped signed-16 amplitudes to an existing interleaved stereo mix.
void mixAudioClipSamples(const AudioClip &clip, qint64 firstSourceSample,
                         qint64 sampleCount, double *interleavedStereo);
AudioAssetPtr makeAudioAsset(QString name, QByteArray pcm, QString *error = nullptr);
AudioPeak audioPeak(const AudioAsset &asset, qint64 firstSample, qint64 sampleCount);
qint64 audioSampleFrameAtTimelineFrame(qreal frame, double fps);
int audioClipDurationFrames(const AudioClip &clip, double fps);
QString validateAudioClips(const Shot &shot, bool requirePcm = true);
QString validateProjectAudio(const Project &project, bool requirePcm = true);
QMap<QString, AudioAssetPtr> audioResources(const Project &project);
QJsonArray audioResourcesToJson(const Project &project);
bool audioResourcesFromJson(const QJsonArray &json, QMap<QString, AudioAssetPtr> *assets, QString *error);
QJsonArray audioClipsToJson(const QVector<AudioClip> &clips);
bool audioClipsFromJson(const QJsonArray &json, const QMap<QString, AudioAssetPtr> &assets,
                       QVector<AudioClip> *clips, QString *error);
qint64 audioHistoryCost(const Project &before, const Project &after);
} // namespace toon
