#pragma once

#include "video_export_settings.h"
#include <QObject>
#include <QPointer>
#include <atomic>
#include <memory>

class QThread;

namespace toon {
class VideoExportWorker;
enum class VideoExportOutcome { Completed, Cancelled, Failed, Skipped };
struct VideoExportResult {
    VideoExportOutcome outcome = VideoExportOutcome::Failed;
    QString path;
    QString error;
    int framesEncoded = 0;
};

// Export works on an immutable snapshot; it never moves the editor playhead.
class VideoExport final : public QObject {
    Q_OBJECT
public:
    explicit VideoExport(QObject *parent = nullptr);
    ~VideoExport() override;
    bool start(Project snapshot, VideoExportSettings settings, const QString &destination);
    bool busy() const { return m_busy; }
    void cancel();
    static QString availabilityError(bool withAudio = true);
signals:
    void progress(int completedFrames, int totalFrames, const QString &phase);
    void finished(const toon::VideoExportResult &result);
private:
    QPointer<QThread> m_thread;
    QPointer<VideoExportWorker> m_worker;
    std::shared_ptr<std::atomic_bool> m_cancelled;
    bool m_busy = false;
};
} // namespace toon
Q_DECLARE_METATYPE(toon::VideoExportResult)
