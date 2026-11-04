#include "video_export.h"
#include "audio_mix.h"
#include "video_export_audio.h"
#include "mp4_timing.h"
#include <QAudioBufferInput>
#include <QAudioFormat>
#include <QBuffer>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QMediaCaptureSession>
#include <QMediaFormat>
#include <QMediaRecorder>
#include <QSaveFile>
#include <QThread>
#include <QTimer>
#include <QTemporaryDir>
#include <filesystem>
#include <QVideoFrameInput>
#include <QVideoFrame>
#include <QVideoFrameFormat>
#include <algorithm>
#include <cmath>

namespace toon {
namespace {
QMediaFormat exportFormat(bool withAudio) {
    QMediaFormat format(QMediaFormat::MPEG4);
    format.setVideoCodec(QMediaFormat::VideoCodec::H264);
    format.setAudioCodec(withAudio ? QMediaFormat::AudioCodec::AAC : QMediaFormat::AudioCodec::Unspecified);
    return format;
}
QString codecError(bool withAudio) {
    const auto backend = qEnvironmentVariable("QT_MEDIA_BACKEND").trimmed();
    if (!backend.isEmpty() && backend != "ffmpeg")
        return "Video export requires Qt's FFmpeg multimedia backend. Restart with QT_MEDIA_BACKEND=ffmpeg.";
    if (!exportFormat(withAudio).isSupported(QMediaFormat::Encode))
        return withAudio ? "MP4 export needs an H.264 video encoder and an AAC audio encoder. They are not available in this multimedia installation."
                         : "An H.264 video encoder for MP4 is not available in this multimedia installation.";
    return {};
}
QAudioFormat exportAudioFormat() {
    QAudioFormat format;
    format.setSampleRate(audioSampleRate);
    format.setChannelConfig(QAudioFormat::ChannelConfigStereo);
    format.setSampleFormat(QAudioFormat::Int16);
    return format;
}
constexpr qint64 streamLeadUSecs = 250000;
constexpr int audioChunkSamples = 2048;
constexpr int minimumAacSamples = 2 * 1024;
constexpr int audioPrerollSamples = 2 * 1024;
constexpr int stallTimeoutMSecs = 60000;
constexpr qint64 shortClipBufferLimit = 80 * 1024 * 1024;

// Qt 6.10 stores the requested encoder FPS in an int internally. A fractional
// single frame therefore gets the wrong inferred duration. Buffer only this
// bounded case; audio metadata is finalized on disk for movies of any length.
class ShortClipBuffer final : public QBuffer {
public:
    bool failed() const { return m_failed; }
    bool seek(qint64 position) override {
        if (position < 0 || position > shortClipBufferLimit) return reject();
        return QBuffer::seek(position);
    }
protected:
    qint64 writeData(const char *data, qint64 length) override {
        if (m_failed || length < 0 || length > shortClipBufferLimit
            || pos() > shortClipBufferLimit - length) {
            reject(); return -1;
        }
        return QBuffer::writeData(data, length);
    }
private:
    bool reject() {
        m_failed = true;
        setErrorString("The short video exceeded its 80 MiB encoding limit.");
        return false;
    }
    bool m_failed = false;
};
}

class VideoExportWorker final : public QObject {
    Q_OBJECT
public:
    VideoExportWorker(Project project, VideoExportSettings settings, QString path,
                      std::shared_ptr<std::atomic_bool> cancelled)
        : m_project(std::move(project)), m_settings(settings), m_path(std::move(path)),
          m_cancelled(std::move(cancelled)) {}

public slots:
    void begin() {
        if (cancelRequested()) { finishWithoutRecorder(VideoExportOutcome::Cancelled); return; }
        const auto validation = validateVideoExportSettings(m_project, m_settings);
        if (!validation.isEmpty()) { finishWithoutRecorder(VideoExportOutcome::Failed, validation); return; }
        const auto unavailable = codecError(m_settings.includeAudio);
        if (!unavailable.isEmpty()) { finishWithoutRecorder(VideoExportOutcome::Failed, unavailable); return; }
        if (m_path.trimmed().isEmpty() || QFileInfo(m_path).isDir()) {
            finishWithoutRecorder(VideoExportOutcome::Failed, "Choose a file path for the MP4 export."); return;
        }
        if (!m_settings.overwriteApproved && (QFileInfo::exists(m_path) || QFileInfo(m_path).isSymLink())) {
            finishWithoutRecorder(VideoExportOutcome::Failed, "The destination already exists and replacement was not approved."); return;
        }
        m_segments = videoExportSegments(m_project, m_settings);
        m_totalFrames = m_settings.lastFrame - m_settings.firstFrame + 1;
        m_totalSamples = videoExportSampleCount(m_project, m_settings);
        // Keep the encoded file private until its audio timing is finalized.
        // Do not retain a QTemporaryFile handle across QSaveFile::commit:
        // close() does not release that handle on Windows.
        m_stagingDirectory = std::make_unique<QTemporaryDir>(QFileInfo(m_path).absoluteDir().filePath(".toon-export-XXXXXX"));
        if (!m_stagingDirectory->isValid()) {
            finishWithoutRecorder(VideoExportOutcome::Failed, "Could not prepare an atomic export in the destination folder."); return;
        }
        m_output = std::make_unique<QSaveFile>(m_stagingDirectory->filePath("video.mp4"));
        // Never fall back to overwriting the final file when an atomic temp cannot be created.
        m_output->setDirectWriteFallback(false);
        if (!m_output->open(QIODevice::WriteOnly)) {
            finishWithoutRecorder(VideoExportOutcome::Failed, "Could not open the export destination: " + m_output->errorString()); return;
        }
        if (m_totalFrames == 1 && m_project.fps != std::floor(m_project.fps)) {
            m_memoryOutput = std::make_unique<ShortClipBuffer>();
            if (!m_memoryOutput->open(QIODevice::ReadWrite)) {
                finishWithoutRecorder(VideoExportOutcome::Failed, "Could not prepare the short video buffer."); return;
            }
        }
        m_session = std::make_unique<QMediaCaptureSession>();
        m_recorder = std::make_unique<QMediaRecorder>();
        QVideoFrameFormat videoFormat(m_settings.size, QVideoFrameFormat::Format_RGBA8888);
        videoFormat.setStreamFrameRate(m_project.fps);
        m_videoInput = std::make_unique<QVideoFrameInput>(videoFormat);
        m_session->setRecorder(m_recorder.get());
        m_session->setVideoFrameInput(m_videoInput.get());
        if (m_settings.includeAudio) {
            m_audioFormat = exportAudioFormat();
            m_audioInput = std::make_unique<QAudioBufferInput>(m_audioFormat);
            m_session->setAudioBufferInput(m_audioInput.get());
            m_mix = std::make_unique<VideoExportAudioStream>(m_project, m_settings);
            if (!m_mix->valid() || m_totalSamples <= 0) {
                finishWithoutRecorder(VideoExportOutcome::Failed, "Could not prepare the audio mix for this frame range."); return;
            }
            connect(m_audioInput.get(), &QAudioBufferInput::readyToSendAudioBuffer, this, [this] {
                m_audioReady = true; schedulePump();
            });
        } else m_audioEos = true;
        // Availability depends on the recorder belonging to a capture session
        // with an input; an unattached recorder reports unavailable.
        if (!m_recorder->isAvailable()) {
            finishWithoutRecorder(VideoExportOutcome::Failed, "The multimedia recording service is unavailable."); return;
        }
        connect(m_videoInput.get(), &QVideoFrameInput::readyToSendVideoFrame, this, [this] {
            m_videoReady = true; schedulePump();
        });
        connect(m_recorder.get(), &QMediaRecorder::durationChanged, this, [this](qint64) { m_activity.restart(); });
        connect(m_recorder.get(), &QMediaRecorder::errorOccurred, this,
            [this](QMediaRecorder::Error, const QString &error) {
                if (!m_stopping) requestStop(VideoExportOutcome::Failed, "Video encoding failed: " + error);
            });
        connect(m_recorder.get(), &QMediaRecorder::recorderStateChanged, this,
            [this](QMediaRecorder::RecorderState state) {
                if (state == QMediaRecorder::RecordingState) {
                    m_started = true;
                    const auto actual = m_recorder->mediaFormat();
                    if (actual.fileFormat() != QMediaFormat::MPEG4 || actual.videoCodec() != QMediaFormat::VideoCodec::H264
                        || (m_settings.includeAudio && actual.audioCodec() != QMediaFormat::AudioCodec::AAC)) {
                        requestStop(VideoExportOutcome::Failed, "The encoder substituted a different format. MP4 H.264 export was not completed."); return;
                    }
                    schedulePump();
                } else if (state == QMediaRecorder::StoppedState) {
                    if (!m_stopping && m_videoEos && m_audioEos && m_sentFrames == m_totalFrames) {
                        m_outcome = VideoExportOutcome::Completed;
                    } else if (!m_stopping) {
                        m_outcome = VideoExportOutcome::Failed;
                        m_error = "The encoder stopped before the complete frame range was written.";
                    }
                    scheduleCompletion();
                }
            });
        m_recorder->setMediaFormat(exportFormat(m_settings.includeAudio));
        m_recorder->setVideoResolution(m_settings.size);
        m_recorder->setVideoFrameRate(m_project.fps);
        m_recorder->setEncodingMode(QMediaRecorder::AverageBitRateEncoding);
        const qreal bitsPerPixel = m_settings.quality == VideoExportQuality::Draft ? .06
                                 : m_settings.quality == VideoExportQuality::Standard ? .12 : .24;
        const qreal requestedRate = m_settings.size.width() * qreal(m_settings.size.height()) * m_project.fps * bitsPerPixel;
        m_recorder->setVideoBitRate(int(qBound(250000.0, requestedRate, 80000000.0)));
        if (m_settings.includeAudio) {
            m_recorder->setAudioSampleRate(audioSampleRate);
            m_recorder->setAudioChannelCount(audioChannels);
            m_recorder->setAudioBitRate(m_settings.quality == VideoExportQuality::Draft ? 128000
                : m_settings.quality == VideoExportQuality::Standard ? 192000 : 256000);
        }
        m_recorder->setOutputDevice(m_memoryOutput ? static_cast<QIODevice *>(m_memoryOutput.get()) : m_output.get());
        m_recorder->setAutoStop(true);
        m_activity.start();
        m_stallTimer = new QTimer(this);
        m_stallTimer->setInterval(1000);
        connect(m_stallTimer, &QTimer::timeout, this, [this] {
            if (cancelRequested()) { requestStop(VideoExportOutcome::Cancelled); return; }
            if (!m_stopping && m_activity.elapsed() > stallTimeoutMSecs)
                requestStop(VideoExportOutcome::Failed, "The video encoder stopped responding. No output file was replaced.");
        });
        m_stallTimer->start();
        emit progress(0, m_totalFrames, "Preparing encoder");
        if (cancelRequested()) { requestStop(VideoExportOutcome::Cancelled); return; }
        m_recorder->record();
        // Some backend startup errors do not produce a state transition.
        if (m_recorder->error() != QMediaRecorder::NoError && !m_stopping)
            requestStop(VideoExportOutcome::Failed, "Could not start video encoding: " + m_recorder->errorString());
        if (m_stopping && m_recorder->recorderState() != QMediaRecorder::StoppedState)
            m_recorder->stop();
    }

    void cancel() {
        m_cancelled->store(true, std::memory_order_relaxed);
        if (!m_done) requestStop(VideoExportOutcome::Cancelled);
    }

signals:
    void progress(int completedFrames, int totalFrames, const QString &phase);
    void finished(const toon::VideoExportResult &result);

private:
    bool cancelRequested() const {
        return m_cancelled->load(std::memory_order_relaxed) || QThread::currentThread()->isInterruptionRequested();
    }
    void schedulePump() {
        if (m_pumpQueued || m_done || m_stopping) return;
        m_pumpQueued = true;
        QTimer::singleShot(0, this, [this] { m_pumpQueued = false; pump(); });
    }
    void pump() {
        if (m_done || m_stopping) return;
        if (cancelRequested()) { requestStop(VideoExportOutcome::Cancelled); return; }
        if (!m_started || m_recorder->recorderState() != QMediaRecorder::RecordingState) return;
        const qint64 videoTime = videoExportFrameTimeUSecs(m_sentFrames, m_project.fps);
        const qint64 audioTime = (m_audioPrerollSent ? audioPrerollSamples + m_sentSamples : 0) * 1000000 / audioSampleRate;
        const bool videoAllowed = m_videoReady && !m_videoEos
            && (m_audioEos || videoTime <= audioTime + streamLeadUSecs);
        const bool audioAllowed = m_audioReady && !m_audioEos
            && (m_videoEos || audioTime <= videoTime + streamLeadUSecs);
        // One bounded unit per event turn keeps cancellation responsive. Prefer the
        // stream with the earlier timestamp, preventing a long muxer-side backlog.
        if (videoAllowed && (!audioAllowed || videoTime <= audioTime)) sendVideo();
        else if (audioAllowed) sendAudio();
        else return;
        if (m_done || m_stopping) return;
        if (m_videoEos && m_audioEos) {
            emit progress(m_sentFrames, m_totalFrames, "Finalizing MP4");
            return; // autoStop drains all accepted data before StoppedState.
        }
        schedulePump();
    }
    void sendVideo() {
        if (m_sentFrames == m_totalFrames) {
            if (m_videoInput->sendVideoFrame({})) { m_videoEos = true; m_activity.restart(); }
            else m_videoReady = false;
            return;
        }
        if (!m_pendingVideo.isValid()) {
            QString error;
            const int frame = m_settings.firstFrame + m_sentFrames;
            while (m_segmentIndex + 1 < m_segments.size() && frame >= m_segments[m_segmentIndex + 1].firstFrame) ++m_segmentIndex;
            const auto &segment = m_segments[m_segmentIndex];
            auto local = m_settings; local.scope = VideoExportScope::Shot; local.shotIndex = segment.shotIndex;
            local.firstFrame = 0; local.lastFrame = segment.frameCount - 1;
            const auto image = renderVideoExportFrame(m_project, local, frame - segment.firstFrame, &error);
            if (cancelRequested()) { requestStop(VideoExportOutcome::Cancelled); return; }
            if (image.isNull()) {
                requestStop(VideoExportOutcome::Failed, error.isEmpty() ? "Could not render an export frame." : error); return;
            }
            m_pendingVideo = QVideoFrame(image);
            m_pendingVideo.setStartTime(videoExportFrameTimeUSecs(m_sentFrames, m_project.fps));
            m_pendingVideo.setEndTime(videoExportFrameTimeUSecs(m_sentFrames + 1, m_project.fps));
            m_pendingVideo.setStreamFrameRate(m_project.fps);
            if (!m_pendingVideo.isValid()) { requestStop(VideoExportOutcome::Failed, "The rendered frame could not be passed to the video encoder."); return; }
        }
        if (!m_videoInput->sendVideoFrame(m_pendingVideo)) {
            m_videoReady = false; return; // Retain exactly this frame for the next readiness signal.
        }
        m_pendingVideo = {};
        ++m_sentFrames;
        m_activity.restart();
        emit progress(m_sentFrames, m_totalFrames, "Encoding video");
    }
    void sendAudio() {
        if (!m_audioPrerollSent) {
            // Native Windows AAC suppresses its first few hundred input samples.
            // Warm up with explicit silence instead of guessing a backend delay.
            // Remove this exact preroll in the final MP4 audio edit below.
            if (!m_pendingAudio.isValid())
                m_pendingAudio = QAudioBuffer(QByteArray(audioPrerollSamples * audioBytesPerSampleFrame, '\0'), m_audioFormat, 0);
            if (!m_audioInput->sendAudioBuffer(m_pendingAudio)) { m_audioReady = false; return; }
            m_audioPrerollSent = true; m_pendingAudio = {}; m_activity.restart(); return;
        }
        if (m_sentSamples == m_totalSamples) {
            if (m_audioInput->sendAudioBuffer({})) { m_audioEos = true; m_activity.restart(); }
            else m_audioReady = false;
            return;
        }
        if (!m_pendingAudio.isValid()) {
            const auto count = std::min<qint64>(audioChunkSamples, m_totalSamples - m_sentSamples);
            // AudioMixStream intentionally produces silence forever for live playback;
            // this explicit cap gives offline export exactly the selected sample range.
            auto bytes = m_mix->read(count * audioBytesPerSampleFrame);
            if (bytes.size() != count * audioBytesPerSampleFrame) {
                requestStop(VideoExportOutcome::Failed, "The audio mix ended unexpectedly."); return;
            }
            // A short input also gets bounded silent look-ahead. Never read
            // audio outside the selected range. The presentation edit excludes
            // this tail and the explicit preroll, preserving backend priming.
            if (m_totalSamples < minimumAacSamples) {
                bytes.append(QByteArray((minimumAacSamples - count) * audioBytesPerSampleFrame, '\0'));
            }
            m_pendingAudio = QAudioBuffer(bytes, m_audioFormat, (audioPrerollSamples + m_sentSamples) * 1000000 / audioSampleRate);
            if (!m_pendingAudio.isValid()) { requestStop(VideoExportOutcome::Failed, "Could not prepare audio for the encoder."); return; }
        }
        if (!m_audioInput->sendAudioBuffer(m_pendingAudio)) { m_audioReady = false; return; }
        // Count selected source samples, excluding any encoder-only silence.
        m_sentSamples += std::min<qint64>(m_pendingAudio.frameCount(), m_totalSamples - m_sentSamples);
        m_pendingAudio = {};
        m_activity.restart();
    }
    void requestStop(VideoExportOutcome outcome, const QString &error = {}) {
        if (m_done || m_stopping) return;
        m_stopping = true; m_outcome = outcome; m_error = error;
        m_pendingVideo = {}; m_pendingAudio = {};
        emit progress(m_sentFrames, m_totalFrames, outcome == VideoExportOutcome::Cancelled ? "Cancelling export" : "Stopping export");
        if (m_recorder && m_recorder->recorderState() != QMediaRecorder::StoppedState)
            m_recorder->stop();
        else scheduleCompletion();
    }
    void scheduleCompletion() {
        if (m_completionQueued || m_done) return;
        m_completionQueued = true;
        // Do not destroy a recorder or its output from inside one of its callbacks.
        QTimer::singleShot(0, this, [this] { complete(); });
    }
    void finishWithoutRecorder(VideoExportOutcome outcome, const QString &error = {}) {
        m_outcome = outcome; m_error = error; m_stopping = true; scheduleCompletion();
    }
    void complete() {
        if (m_done) return;
        if (m_recorder && m_recorder->recorderState() != QMediaRecorder::StoppedState) {
            m_completionQueued = false; return;
        }
        m_done = true;
        if (m_stallTimer) m_stallTimer->stop();
        if (cancelRequested()) m_outcome = VideoExportOutcome::Cancelled;
        if (m_outcome == VideoExportOutcome::Completed) {
            const auto encodedSize = m_memoryOutput ? m_memoryOutput->size() : m_output ? m_output->size() : 0;
            if (!m_videoEos || !m_audioEos || m_sentFrames != m_totalFrames
                || (m_settings.includeAudio && m_sentSamples != m_totalSamples)
                || !m_output || encodedSize <= 0) {
                m_outcome = VideoExportOutcome::Failed;
                m_error = "The encoder did not produce a complete MP4 file.";
            } else if (m_memoryOutput && !writeFinalizedShortClip()) {
                // The helper sets a failure/cancellation result and never commits.
            }
            // Copying a bounded file can still take time on a slow destination.
            // Observe cancellation once more immediately before atomic publication.
            if (cancelRequested()) m_outcome = VideoExportOutcome::Cancelled;
            if (m_outcome == VideoExportOutcome::Completed && !m_settings.overwriteApproved
                && (QFileInfo::exists(m_path) || QFileInfo(m_path).isSymLink())) {
                m_outcome = VideoExportOutcome::Failed;
                m_error = "The destination appeared during export. Replacement was not approved.";
            }
            if (m_outcome == VideoExportOutcome::Completed && !m_output->commit()) {
                m_outcome = VideoExportOutcome::Failed;
                m_error = "Could not finish the exported file: " + m_output->errorString();
            }
            if (m_outcome == VideoExportOutcome::Completed && m_settings.includeAudio) {
                QFile staging(m_stagingDirectory->filePath("video.mp4")); QString error;
                if (!staging.open(QIODevice::ReadWrite)
                    || !finalizeMp4AudioPresentation(&staging, m_totalSamples, audioSampleRate, audioPrerollSamples, &error)
                    || !staging.flush()) {
                    m_outcome = VideoExportOutcome::Failed;
                    m_error = "Could not finalize audio timing: " + (error.isEmpty() ? staging.errorString() : error);
                }
                // Release the native file handle before publishing on Windows.
                staging.close();
            }
            if (m_outcome == VideoExportOutcome::Completed && m_stagingDirectory && cancelRequested())
                m_outcome = VideoExportOutcome::Cancelled;
            if (m_outcome == VideoExportOutcome::Completed && m_stagingDirectory) {
                // The private file is complete and every handle is closed. Its
                // directory is inside the destination folder, so publication is
                // on one filesystem. Rename replaces atomically only with prior
                // approval; otherwise a hard link refuses every existing target,
                // including a dangling symlink, without a check/write race.
                const auto nativePath = [](const QString &path) {
#ifdef Q_OS_WIN
                    return std::filesystem::path(path.toStdWString());
#else
                    return std::filesystem::path(QFile::encodeName(path).toStdString());
#endif
                };
                std::error_code error;
                if (m_settings.overwriteApproved)
                    std::filesystem::rename(nativePath(m_stagingDirectory->filePath("video.mp4")), nativePath(m_path), error);
                else
                    std::filesystem::create_hard_link(nativePath(m_stagingDirectory->filePath("video.mp4")), nativePath(m_path), error);
                if (error) {
                    m_outcome = VideoExportOutcome::Failed;
                    m_error = (m_settings.overwriteApproved ? "Could not replace the exported video: "
                        : "Could not publish the video without replacing an existing file: ") + QString::fromStdString(error.message());
                }
            }
        }
        if (m_outcome != VideoExportOutcome::Completed && m_output && m_output->isOpen()) m_output->cancelWriting();
        // All encoder threads have finished writing before the atomic file is closed.
        m_recorder.reset(); m_audioInput.reset(); m_videoInput.reset(); m_session.reset();
        m_memoryOutput.reset(); m_output.reset(); m_stagingDirectory.reset(); m_mix.reset();
        if (m_outcome == VideoExportOutcome::Cancelled) m_error.clear();
        emit finished({m_outcome, m_path, m_error, m_sentFrames});
    }

    bool writeFinalizedShortClip() {
        if (m_memoryOutput->failed()) {
            m_outcome = VideoExportOutcome::Failed; m_error = m_memoryOutput->errorString(); return false;
        }
        QString error;
        if (m_totalFrames == 1 && m_project.fps != std::floor(m_project.fps)
            && !correctSingleFrameMp4Timing(&m_memoryOutput->buffer(), m_project.fps, &error)) {
            m_outcome = VideoExportOutcome::Failed;
            m_error = "Could not finalize single-frame video timing: " + error;
            return false;
        }
        const auto &bytes = m_memoryOutput->buffer();
        constexpr qsizetype copyChunk = 1024 * 1024;
        qsizetype copied = 0;
        while (copied < bytes.size()) {
            if (cancelRequested()) { m_outcome = VideoExportOutcome::Cancelled; return false; }
            const auto count = qMin(copyChunk, bytes.size() - copied);
            const auto written = m_output->write(bytes.constData() + copied, count);
            if (written != count) {
                m_outcome = VideoExportOutcome::Failed;
                m_error = "Could not write the corrected video: " + m_output->errorString();
                return false;
            }
            copied += count;
        }
        return true;
    }

    const Project m_project;
    const VideoExportSettings m_settings;
    const QString m_path;
    std::shared_ptr<std::atomic_bool> m_cancelled;
    std::unique_ptr<QTemporaryDir> m_stagingDirectory;
    std::unique_ptr<QSaveFile> m_output;
    std::unique_ptr<ShortClipBuffer> m_memoryOutput;
    std::unique_ptr<QMediaCaptureSession> m_session;
    std::unique_ptr<QMediaRecorder> m_recorder;
    std::unique_ptr<QVideoFrameInput> m_videoInput;
    std::unique_ptr<QAudioBufferInput> m_audioInput;
    std::unique_ptr<VideoExportAudioStream> m_mix;
    QVector<VideoExportSegment> m_segments;
    int m_segmentIndex = 0;
    QAudioFormat m_audioFormat;
    QVideoFrame m_pendingVideo;
    QAudioBuffer m_pendingAudio;
    QTimer *m_stallTimer = nullptr;
    QElapsedTimer m_activity;
    int m_totalFrames = 0, m_sentFrames = 0;
    qint64 m_totalSamples = 0, m_sentSamples = 0;
    bool m_started = false, m_done = false, m_stopping = false;
    bool m_videoReady = false, m_audioReady = false, m_videoEos = false, m_audioEos = false;
    bool m_audioPrerollSent = false;
    bool m_pumpQueued = false, m_completionQueued = false;
    VideoExportOutcome m_outcome = VideoExportOutcome::Failed;
    QString m_error;
};

VideoExport::VideoExport(QObject *parent) : QObject(parent) { qRegisterMetaType<VideoExportResult>(); }
VideoExport::~VideoExport() {
    cancel();
    if (m_thread && m_thread->isRunning()) {
        m_thread->requestInterruption();
        // A platform encoder may block while finishing. Never terminate its thread
        // or destroy its open atomic file underneath it; detached jobs self-clean.
        m_thread->wait(5000);
    }
}
QString VideoExport::availabilityError(bool withAudio) { return codecError(withAudio); }
bool VideoExport::start(Project snapshot, VideoExportSettings settings, const QString &destination) {
    if (m_busy) return false;
    m_cancelled = std::make_shared<std::atomic_bool>(false);
    auto *thread = new QThread;
    auto *worker = new VideoExportWorker(std::move(snapshot), settings, destination, m_cancelled);
    m_thread = thread; m_worker = worker; m_busy = true;
    thread->setObjectName("Video export"); worker->moveToThread(thread);
    connect(thread, &QThread::started, worker, &VideoExportWorker::begin);
    // Preserve worker ordering for synchronous observers. Auto-connected UI
    // receivers still queue delivery to their own thread.
    connect(worker, &VideoExportWorker::progress, this, &VideoExport::progress, Qt::DirectConnection);
    connect(worker, &VideoExportWorker::finished, this, [this, thread](const VideoExportResult &result) {
        if (m_thread == thread) { m_busy = false; m_worker = nullptr; m_thread = nullptr; }
        emit finished(result);
    });
    connect(worker, &VideoExportWorker::finished, thread, &QThread::quit, Qt::DirectConnection);
    connect(thread, &QThread::finished, worker, &QObject::deleteLater);
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
    return true;
}
void VideoExport::cancel() {
    if (!m_busy) return;
    if (m_cancelled) m_cancelled->store(true, std::memory_order_relaxed);
    if (m_worker) QMetaObject::invokeMethod(m_worker.data(), &VideoExportWorker::cancel, Qt::QueuedConnection);
}
} // namespace toon
#include "video_export.moc"
