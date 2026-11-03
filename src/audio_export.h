#pragma once

#include "project.h"
#include <QObject>
#include <QPointer>
#include <atomic>
#include <memory>

class QThread;
namespace toon {
enum class AudioExportScope { Shot, Movie };
struct AudioExportSettings {
    AudioExportScope scope = AudioExportScope::Shot;
    int shotIndex = 0, firstFrame = 0, lastFrame = 0;
    bool overwriteApproved = false;
};
AudioExportSettings audioExportDefaults(const Project &project, int shotIndex = 0);
AudioExportSettings audioExportMovieDefaults(const Project &project);
int audioExportDurationFrames(const Project &project, AudioExportScope scope, int shotIndex = 0);
qint64 audioExportSampleCount(const Project &project, const AudioExportSettings &settings);
QString validateAudioExportSettings(const Project &project, const AudioExportSettings &settings,
                                   bool validateProject = true);

enum class AudioExportOutcome { Completed, Cancelled, Failed };
struct AudioExportResult {
    AudioExportOutcome outcome = AudioExportOutcome::Failed;
    QString path, error;
    qint64 sampleFrames = 0;
};
class AudioExportWorker;
// Writes a bounded-memory WAV from an immutable project snapshot. Export never
// changes editor time or document history and publishes only a complete file.
class AudioExport final : public QObject {
    Q_OBJECT
public:
    explicit AudioExport(QObject *parent = nullptr);
    ~AudioExport() override;
    bool start(Project snapshot, AudioExportSettings settings, const QString &destination);
    bool busy() const { return m_busy; }
    void cancel();
signals:
    void progress(qint64 completedSamples, qint64 totalSamples);
    void finished(const toon::AudioExportResult &result);
private:
    QPointer<QThread> m_thread;
    std::shared_ptr<std::atomic_bool> m_cancelled;
    bool m_busy = false;
};
} // namespace toon
Q_DECLARE_METATYPE(toon::AudioExportResult)
