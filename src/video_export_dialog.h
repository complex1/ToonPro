#pragma once

#include "project.h"
#include "video_export_settings.h"
#include "video_export_queue.h"
#include <QDialog>

class QCheckBox;
class QCloseEvent;
class QLabel;
class QLineEdit;
class QListWidget;
class QProgressBar;
class QPushButton;
class QTabBar;
class QToolButton;

namespace toon {
class ValueSlider;
class VideoExport;
struct VideoExportResult;

class VideoExportDialog final : public QDialog {
    Q_OBJECT
public:
    explicit VideoExportDialog(const Project &project, int playbackFirst, int playbackLast,
                               bool rangeEnabled, QWidget *parent = nullptr);
    VideoExportDialog(const Project &project, int activeShotIndex, int playbackFirst, int playbackLast,
                      bool rangeEnabled, QWidget *parent = nullptr);
    VideoExportSettings settings() const;
    QVector<VideoExportQueueItem> queueItems() const;
    void setQueuedShotIds(const QStringList &ids);
    QString destination() const;
    void setDestination(const QString &path);
signals:
    void exported(const QString &path);
public slots:
    void reject() override;
protected:
    void closeEvent(QCloseEvent *event) override;
private:
    struct SavedControls {
        VideoExportSettings settings;
        int rangeMode = 0, customFirst = 1, customLast = 1;
        bool aspect = true;
    };
    bool busy() const;
    void saveControls();
    void restoreControls(const SavedControls &state);
    void changeScope(int index);
    void changeQueueShot(int row);
    void startQueue();
    void finishQueue(const VideoExportQueueReport &report);
    void refresh();
    void setStatus(const QString &message, bool error = false);
    void changeRangeMode(int index);
    void normalizeDimensions(bool widthDriven);
    void updateCompanionDimension(bool widthDriven);
    void browse();
    void startExport();
    void finishExport(const VideoExportResult &result);
    void requestClose();
    Project m_project;
    VideoExport *m_exporter;
    VideoExportQueue *m_queue;
    QTabBar *m_scope;
    QListWidget *m_queueShots;
    QCheckBox *m_reviewExisting;
    QLabel *m_destinationLabel;
    QVector<SavedControls> m_queueStates;
    SavedControls m_shotState, m_movieState;
    QStringList m_scopeDestinations{QString{}, QString{}, QString{}};
    QVector<int> m_runningQueueShots;
    int m_activeShot = 0, m_queueShot = 0, m_previousScope = 0;
    QWidget *m_controls;
    QTabBar *m_range, *m_quality;
    ValueSlider *m_first, *m_last, *m_width, *m_height;
    QToolButton *m_aspect, *m_usePlayback;
    QCheckBox *m_audio;
    QLineEdit *m_destination;
    QLabel *m_summary, *m_status;
    QProgressBar *m_progress;
    QPushButton *m_export, *m_cancel;
    int m_frameCount = 1, m_customFirst = 1, m_customLast = 1;
    int m_playbackFirst = 0, m_playbackLast = 0, m_previousRange = 0;
    bool m_hasPlaybackRange = false, m_hasAudio = false;
    bool m_changing = false, m_widthDriven = true, m_closing = false;
    double m_aspectRatio = 1;
    QString m_projectError, m_silentAvailability, m_audioAvailability, m_failure;
};
} // namespace toon
