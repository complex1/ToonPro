#pragma once

#include "audio_export.h"
#include <QDialog>

class QCloseEvent;
class QLabel;
class QLineEdit;
class QProgressBar;
class QPushButton;
class QTabBar;
class QToolButton;
namespace toon {
class ValueSlider;
class AudioExportDialog final : public QDialog {
    Q_OBJECT
public:
    AudioExportDialog(const Project &project, int activeShotIndex, int playbackFirst,
                      int playbackLast, bool rangeEnabled, QWidget *parent = nullptr);
    AudioExportSettings settings() const;
    QString destination() const;
    void setDestination(const QString &path);
signals:
    void exported(const QString &path);
public slots:
    void reject() override;
protected:
    void closeEvent(QCloseEvent *event) override;
private:
    struct RangeState { int mode = 0, first = 1, last = 1; };
    void saveRange();
    void restoreRange();
    void changeScope(int index);
    void changeRange(int index);
    void refresh();
    void setStatus(const QString &message, bool error = false);
    void browse();
    void startExport();
    void finishExport(const AudioExportResult &result);
    void requestClose();
    Project m_project;
    AudioExport *m_exporter;
    QWidget *m_controls;
    QTabBar *m_scope, *m_range;
    ValueSlider *m_first, *m_last;
    QToolButton *m_usePlayback;
    QLineEdit *m_destination;
    QLabel *m_summary, *m_status;
    QProgressBar *m_progress;
    QPushButton *m_export, *m_cancel;
    RangeState m_ranges[2];
    int m_activeShot = 0, m_previousScope = 0, m_previousRange = 0, m_frameCount = 1;
    int m_playbackFirst = 0, m_playbackLast = 0;
    bool m_hasPlaybackRange = false, m_changing = false, m_closing = false;
    QString m_projectError, m_failure;
};
} // namespace toon
