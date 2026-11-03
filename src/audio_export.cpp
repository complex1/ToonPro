#include "audio_export.h"
#include "audio.h"
#include "video_export_audio.h"
#include "video_export_settings.h"
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QTemporaryFile>
#include <QThread>
#include <QtEndian>
#include <array>
#include <cstring>
#include <filesystem>
#include <limits>

namespace toon {
namespace {
constexpr qint64 maximumWaveSamples = (qint64(std::numeric_limits<quint32>::max()) - 36) / audioBytesPerSampleFrame;
VideoExportSettings streamSettings(const AudioExportSettings &settings) {
    VideoExportSettings value;
    value.scope = settings.scope == AudioExportScope::Shot ? VideoExportScope::Shot : VideoExportScope::Movie;
    value.shotIndex = settings.shotIndex; value.firstFrame = settings.firstFrame; value.lastFrame = settings.lastFrame;
    value.size = QSize(64, 64); value.includeAudio = true; return value;
}
std::array<char, 44> waveHeader(qint64 samples) {
    std::array<char, 44> header{};
    std::memcpy(header.data(), "RIFF", 4); std::memcpy(header.data() + 8, "WAVEfmt ", 8);
    std::memcpy(header.data() + 36, "data", 4);
    qToLittleEndian<quint32>(quint32(samples * audioBytesPerSampleFrame + 36), header.data() + 4);
    qToLittleEndian<quint32>(16, header.data() + 16);
    qToLittleEndian<quint16>(1, header.data() + 20);
    qToLittleEndian<quint16>(audioChannels, header.data() + 22);
    qToLittleEndian<quint32>(audioSampleRate, header.data() + 24);
    qToLittleEndian<quint32>(audioSampleRate * audioBytesPerSampleFrame, header.data() + 28);
    qToLittleEndian<quint16>(audioBytesPerSampleFrame, header.data() + 32);
    qToLittleEndian<quint16>(16, header.data() + 34);
    qToLittleEndian<quint32>(quint32(samples * audioBytesPerSampleFrame), header.data() + 40);
    return header;
}
std::filesystem::path nativePath(const QString &path) {
#ifdef Q_OS_WIN
    return std::filesystem::path(path.toStdWString());
#else
    return std::filesystem::path(QFile::encodeName(path).toStdString());
#endif
}
}
int audioExportDurationFrames(const Project &project, AudioExportScope scope, int shotIndex) {
    if (scope != AudioExportScope::Shot && scope != AudioExportScope::Movie) return 0;
    return videoExportDurationFrames(project, scope == AudioExportScope::Shot ? VideoExportScope::Shot : VideoExportScope::Movie, shotIndex);
}
AudioExportSettings audioExportDefaults(const Project &project, int shotIndex) {
    AudioExportSettings settings; settings.shotIndex = shotIndex;
    settings.lastFrame = qMax(1, audioExportDurationFrames(project, settings.scope, shotIndex)) - 1; return settings;
}
AudioExportSettings audioExportMovieDefaults(const Project &project) {
    AudioExportSettings settings; settings.scope = AudioExportScope::Movie;
    settings.lastFrame = qMax(1, audioExportDurationFrames(project, settings.scope)) - 1; return settings;
}
qint64 audioExportSampleCount(const Project &project, const AudioExportSettings &settings) {
    if (settings.scope != AudioExportScope::Shot && settings.scope != AudioExportScope::Movie) return 0;
    return videoExportSampleCount(project, streamSettings(settings));
}
QString validateAudioExportSettings(const Project &project, const AudioExportSettings &settings, bool validateProject) {
    if (settings.scope != AudioExportScope::Shot && settings.scope != AudioExportScope::Movie)
        return "Choose a supported audio export scope.";
    if (const auto error = validateVideoExportSettings(project, streamSettings(settings), validateProject); !error.isEmpty()) return error;
    const auto samples = audioExportSampleCount(project, settings);
    if (samples <= 0) return "Choose a nonempty audio export range.";
    if (samples > maximumWaveSamples) return "This range exceeds the WAV file size limit (4 GB). Export a shorter range.";
    return {};
}

class AudioExportWorker final : public QObject {
    Q_OBJECT
public:
    AudioExportWorker(Project project, AudioExportSettings settings, QString path,
                      std::shared_ptr<std::atomic_bool> cancelled)
        : m_project(std::move(project)), m_settings(settings), m_path(std::move(path)), m_cancelled(std::move(cancelled)) {}
    void begin() {
        auto result = run();
        if (result.outcome == AudioExportOutcome::Cancelled) result.error.clear();
        emit finished(result);
    }
signals:
    void progress(qint64 completedSamples, qint64 totalSamples);
    void finished(const toon::AudioExportResult &result);
private:
    bool cancelled() const { return m_cancelled->load(std::memory_order_relaxed); }
    AudioExportResult run() {
        AudioExportResult result; result.path = m_path;
        auto fail = [&result](const QString &error) { result.error = error; return result; };
        auto cancel = [&result] { result.outcome = AudioExportOutcome::Cancelled; return result; };
        if (cancelled()) return cancel();
        if (m_path.trimmed().isEmpty()) return fail("Choose a destination for the WAV file.");
        m_path = QFileInfo(m_path).absoluteFilePath(); result.path = m_path;
        if (const auto error = validateAudioExportSettings(m_project, m_settings); !error.isEmpty()) return fail(error);
        if (cancelled()) return cancel();
        const QFileInfo target(m_path);
        if (target.isDir()) return fail("Choose a file, not a folder, for the WAV destination.");
        if (!m_settings.overwriteApproved && (target.exists() || target.isSymLink()))
            return fail("The destination already exists. Replacement was not approved.");
        std::unique_ptr<QTemporaryFile> staging;
        QString outputPath = m_path;
        if (!m_settings.overwriteApproved) {
            staging = std::make_unique<QTemporaryFile>(target.absoluteDir().filePath(".toon-audio-XXXXXX.wav"));
            if (!staging->open()) return fail("Could not create the audio export: " + staging->errorString());
            outputPath = staging->fileName(); staging->close();
        }
        QSaveFile output(outputPath); output.setDirectWriteFallback(false);
        if (!output.open(QIODevice::WriteOnly)) return fail("Could not create the audio export: " + output.errorString());
        const auto total = audioExportSampleCount(m_project, m_settings);
        const auto header = waveHeader(total);
        if (output.write(header.data(), header.size()) != qint64(header.size()))
            return fail("Could not write the WAV header: " + output.errorString());
        VideoExportAudioStream stream(m_project, streamSettings(m_settings));
        if (!stream.valid()) return fail("Could not prepare the timeline audio mix.");
        std::array<char, 32768> buffer{};
        QElapsedTimer progressTimer; progressTimer.start(); emit progress(0, total);
        while (result.sampleFrames < total) {
            if (cancelled()) return cancel();
            const auto needed = qMin<qint64>(buffer.size(), (total - result.sampleFrames) * audioBytesPerSampleFrame);
            const auto count = stream.read(buffer.data(), needed);
            if (count != needed) return fail("The audio mixer ended before the selected range was complete.");
#if Q_BYTE_ORDER == Q_BIG_ENDIAN
            for (qint64 i = 0; i < count; i += sizeof(qint16)) {
                qint16 sample; std::memcpy(&sample, buffer.data() + i, sizeof(sample));
                qToLittleEndian<qint16>(sample, buffer.data() + i);
            }
#endif
            if (output.write(buffer.data(), count) != count) return fail("Could not write the audio export: " + output.errorString());
            result.sampleFrames += count / audioBytesPerSampleFrame;
            if (progressTimer.elapsed() >= 40) { emit progress(result.sampleFrames, total); progressTimer.restart(); }
        }
        if (cancelled()) return cancel();
        if (!output.commit()) return fail("Could not finish the WAV file: " + output.errorString());
        if (staging) {
            if (cancelled()) return cancel();
            // A same-folder hard link publishes without replacement atomically,
            // including if another program creates the target during export.
            std::error_code error;
            std::filesystem::create_hard_link(nativePath(staging->fileName()), nativePath(m_path), error);
            if (error) return fail("Could not publish the WAV without replacing an existing file: " + QString::fromStdString(error.message()));
        }
        result.outcome = AudioExportOutcome::Completed; emit progress(total, total); return result;
    }
    Project m_project;
    AudioExportSettings m_settings;
    QString m_path;
    std::shared_ptr<std::atomic_bool> m_cancelled;
};

AudioExport::AudioExport(QObject *parent) : QObject(parent) { qRegisterMetaType<AudioExportResult>(); }
AudioExport::~AudioExport() {
    cancel();
    if (m_thread && m_thread->isRunning()) m_thread->wait(5000);
    // On a stalled filesystem the independent worker retains its snapshot and
    // atomic file, then self-cleans; never terminate it underneath an open file.
}
bool AudioExport::start(Project snapshot, AudioExportSettings settings, const QString &destination) {
    if (m_busy) return false;
    m_cancelled = std::make_shared<std::atomic_bool>(false);
    auto *thread = new QThread;
    auto *worker = new AudioExportWorker(std::move(snapshot), settings, destination, m_cancelled);
    m_thread = thread; m_busy = true; thread->setObjectName("Audio export"); worker->moveToThread(thread);
    connect(thread, &QThread::started, worker, &AudioExportWorker::begin);
    connect(worker, &AudioExportWorker::progress, this, &AudioExport::progress, Qt::DirectConnection);
    connect(worker, &AudioExportWorker::finished, this, [this, thread](const AudioExportResult &result) {
        if (m_thread == thread) { m_busy = false; m_thread = nullptr; }
        emit finished(result);
    });
    connect(worker, &AudioExportWorker::finished, thread, &QThread::quit, Qt::DirectConnection);
    connect(thread, &QThread::finished, worker, &QObject::deleteLater);
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start(); return true;
}
void AudioExport::cancel() { if (m_cancelled) m_cancelled->store(true, std::memory_order_relaxed); }
} // namespace toon
#include "audio_export.moc"
