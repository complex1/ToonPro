#include "video_export_queue.h"
#include <QDir>
#include <QFileInfo>
#include <QSet>
#include <QTimer>

namespace toon {
QString videoExportShotFileName(const Project &project, int shotIndex) {
    if (shotIndex < 0 || shotIndex >= project.shots.size()) return {};
    const auto &shot = project.shots[shotIndex];
    QString clean;
    for (const auto character : shot.number + "-" + shot.name) {
        if (clean.size() >= 80) break;
        if (character.isLetterOrNumber() || character == '-' || character == '_') clean += character;
        else if (!clean.endsWith('-')) clean += '-';
    }
    while (clean.endsWith('-')) clean.chop(1);
    if (clean.isEmpty()) clean = "shot";
    // Sequence position guarantees distinct names even with duplicate titles,
    // case-insensitive filesystems, path separators and reserved device names.
    return QString("%1-%2.mp4").arg(shotIndex + 1, 4, 10, QChar('0')).arg(clean);
}
VideoExportQueue::VideoExportQueue(QObject *parent) : QObject(parent), m_exporter(new VideoExport(this)) {
    qRegisterMetaType<VideoExportQueueReport>();
    connect(m_exporter, &VideoExport::progress, this, [this](int completed, int total, const QString &phase) {
        emit progress(m_index, m_items.size(), completed, total, phase);
    });
    connect(m_exporter, &VideoExport::finished, this, [this](const VideoExportResult &result) {
        if (!m_busy) return;
        m_report.results.append(result);
        if (result.outcome == VideoExportOutcome::Completed) ++m_report.completedJobs;
        emit itemFinished(m_index, result);
        ++m_index;
        if (result.outcome == VideoExportOutcome::Failed || result.outcome == VideoExportOutcome::Cancelled) {
            m_report.outcome = result.outcome; m_report.error = result.error; finish(); return;
        }
        QTimer::singleShot(0, this, &VideoExportQueue::next);
    });
}
bool VideoExportQueue::start(Project snapshot, QVector<VideoExportQueueItem> items, QString *error) {
    if (error) error->clear();
    const auto fail = [error](const QString &message) { if (error) *error = message; return false; };
    if (m_busy) return fail("A shot export queue is already running.");
    if (items.isEmpty() || items.size() > 10000) return fail("Choose between 1 and 10,000 shot exports.");
    if (const auto invalid = snapshot.validate(); !invalid.isEmpty()) return fail(invalid);
    QSet<QString> destinations;
    for (auto &item : items) {
        if (item.settings.scope != VideoExportScope::Shot) return fail("A shot queue contains individual shot exports.");
        const auto invalid = validateVideoExportSettings(snapshot, item.settings, false);
        if (!invalid.isEmpty()) return fail(invalid);
        if (item.destination.trimmed().isEmpty()) return fail("Choose a destination for every shot.");
        item.destination = QFileInfo(item.destination).absoluteFilePath();
        const QFileInfo info(item.destination);
        if (info.isDir() || !info.dir().exists()) return fail("Choose an existing output folder and a file name for every shot.");
        const QString identity = QDir::cleanPath(info.dir().canonicalPath() + '/' + info.fileName()).toCaseFolded();
        if (destinations.contains(identity)) return fail("Each queued shot needs a different output file.");
        destinations.insert(identity);
        item.settings.overwriteApproved = item.overwriteApproved;
    }
    m_project = std::move(snapshot); m_items = std::move(items); m_report = {};
    m_index = 0; m_cancelled = false; m_busy = true;
    QTimer::singleShot(0, this, &VideoExportQueue::next);
    return true;
}
void VideoExportQueue::next() {
    if (!m_busy) return;
    if (m_cancelled) { m_report.outcome = VideoExportOutcome::Cancelled; finish(); return; }
    if (m_index >= m_items.size()) { finish(); return; }
    const auto &item = m_items[m_index];
    const QFileInfo info(item.destination);
    if (!item.overwriteApproved && (info.exists() || info.isSymLink())) {
        const VideoExportResult result{VideoExportOutcome::Skipped, item.destination,
            "Existing file skipped; replacement was not approved.", 0};
        m_report.results.append(result); ++m_report.skippedJobs;
        emit itemFinished(m_index, result); ++m_index;
        QTimer::singleShot(0, this, &VideoExportQueue::next); return;
    }
    emit itemStarted(m_index, m_items.size(), item.label.isEmpty() ? info.fileName() : item.label);
    if (m_cancelled) { m_report.outcome = VideoExportOutcome::Cancelled; finish(); return; }
    if (!m_exporter->start(m_project, item.settings, item.destination)) {
        m_report.outcome = VideoExportOutcome::Failed; m_report.error = "Could not start the next shot export."; finish();
    }
}
void VideoExportQueue::cancel() {
    if (!m_busy) return;
    m_cancelled = true;
    if (m_exporter->busy()) m_exporter->cancel();
    else QTimer::singleShot(0, this, &VideoExportQueue::next);
}
void VideoExportQueue::finish() {
    if (!m_busy) return;
    m_busy = false;
    const auto result = m_report;
    emit finished(result);
}
} // namespace toon
