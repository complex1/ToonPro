#pragma once
#include "video_export.h"

namespace toon {
struct VideoExportQueueItem {
    VideoExportSettings settings;
    QString destination;
    bool overwriteApproved = false;
    QString label;
};
struct VideoExportQueueReport {
    VideoExportOutcome outcome = VideoExportOutcome::Completed;
    QVector<VideoExportResult> results;
    int completedJobs = 0, skippedJobs = 0;
    QString error;
};
QString videoExportShotFileName(const Project &project, int shotIndex);

// Sequential jobs share one immutable snapshot and one encoder. Existing outputs
// are skipped unless that individual job explicitly approves replacement.
// Failure/cancellation stops the queue; already completed files remain intact.
class VideoExportQueue final : public QObject {
    Q_OBJECT
public:
    explicit VideoExportQueue(QObject *parent = nullptr);
    bool start(Project snapshot, QVector<VideoExportQueueItem> items, QString *error = nullptr);
    bool busy() const { return m_busy; }
    void cancel();
    const VideoExportQueueReport &report() const { return m_report; }
signals:
    void itemStarted(int index, int total, const QString &label);
    void progress(int completedJobs, int totalJobs, int completedFrames, int totalFrames, const QString &phase);
    void itemFinished(int index, const toon::VideoExportResult &result);
    void finished(const toon::VideoExportQueueReport &report);
private:
    void next();
    void finish();
    Project m_project;
    QVector<VideoExportQueueItem> m_items;
    VideoExport *m_exporter;
    VideoExportQueueReport m_report;
    int m_index = 0;
    bool m_busy = false, m_cancelled = false;
};
} // namespace toon
Q_DECLARE_METATYPE(toon::VideoExportQueueReport)
