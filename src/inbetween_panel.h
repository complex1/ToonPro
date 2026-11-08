#pragma once
#include "rife_job.h"
#include <QFutureWatcher>
#include <QPointer>
#include <QWidget>
#include <optional>

class QComboBox;
class QLabel;
class QListWidget;
class QPushButton;
class QProgressBar;
class QTabBar;
class QTimer;
class QSettings;
class QToolButton;
namespace toon {
class TimelineController;
class EasingGraphWidget;
class ReferenceView;
class ValueSlider;
class InbetweenPanel final : public QWidget {
    Q_OBJECT
public:
    explicit InbetweenPanel(TimelineController *, QWidget *parent = nullptr, QSettings *settings = nullptr);
    ~InbetweenPanel() override;
    bool busy() const { return m_busy; }
    bool hasPreview() const { return m_result.has_value(); }
public slots:
    void useSelectedCels();
    void generate();
    void cancel();
    void insert();
private:
    TimelineCelRef reference(QComboBox *) const;
    void selectReference(QComboBox *, const TimelineCelRef &);
    InbetweenPlan currentPlan(QString *error) const;
    void refresh();
    void settingsChanged();
    void invalidate(const QString &reason);
    void updateButtons();
    void finishGeneration();
    void showPreview(int index);
    void setPlaying(bool);
    TimelineController *m_controller;
    QPointer<QSettings> m_settings;
    QComboBox *m_first, *m_last;
    QLabel *m_firstImage, *m_lastImage, *m_summary, *m_status;
    QTabBar *m_frequency, *m_presets, *m_output;
    EasingGraphWidget *m_graph;
    ReferenceView *m_view;
    QListWidget *m_strip;
    ValueSlider *m_scrub;
    QPushButton *m_generate, *m_cancel, *m_insert;
    QToolButton *m_play;
    QProgressBar *m_progress;
    QTimer *m_progressTimer, *m_playTimer;
    QFutureWatcher<InbetweenResult> m_watcher;
    std::shared_ptr<RifeJobState> m_job;
    std::optional<InbetweenResult> m_result;
    QVector<TimelineCelRef> m_lastSelection;
    QString m_shotId;
    QString m_thumbnailKey;
    quint64 m_seenRevision = 0, m_jobRevision = 0;
    bool m_refreshing = false, m_busy = false, m_discardJob = false;
    CubicEasing m_curve;
};
} // namespace toon
