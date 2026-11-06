#include "video_export_dialog.h"
#include "icons.h"
#include "ui_theme.h"
#include "value_slider.h"
#include "video_export.h"
#include <QCheckBox>
#include <QCloseEvent>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QLocale>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QResizeEvent>
#include <QSignalBlocker>
#include <QTabBar>
#include <QToolButton>
#include <QVBoxLayout>
#include <cmath>

namespace toon {
namespace {
class ExportStatus final : public QLabel {
public:
    explicit ExportStatus(QWidget *parent = nullptr) : QLabel(parent) {
        setTextFormat(Qt::PlainText); setMinimumWidth(0);
        setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
        setFixedHeight(fontMetrics().height() + 10);
    }
    void message(const QString &value) {
        m_message = value; setToolTip(value); setAccessibleName(value); updateText();
    }
protected:
    void resizeEvent(QResizeEvent *event) override { QLabel::resizeEvent(event); updateText(); }
private:
    void updateText() { setText(fontMetrics().elidedText(m_message, Qt::ElideRight, qMax(0, width()))); }
    QString m_message;
};
int evenPixels(double value) { return qBound(16, qRound(value / 2) * 2, 4096); }
QTabBar *choices(const char *id, const QStringList &labels, QWidget *parent) {
    auto *tabs = new QTabBar(parent); tabs->setObjectName(id);
    tabs->setExpanding(true); tabs->setDrawBase(false); tabs->setUsesScrollButtons(true);
    for (const auto &label : labels) {
        const int tab = tabs->addTab(label);
        tabs->setAccessibleTabName(tab, label); tabs->setTabToolTip(tab, label);
    }
    return tabs;
}
QToolButton *iconButton(const char *id, const QString &icon, const QString &help, QWidget *parent) {
    auto *button = new QToolButton(parent); button->setObjectName(id); button->setIcon(appIcon(icon));
    button->setIconSize({20, 20}); button->setMinimumSize(30, 30);
    button->setToolTip(help); button->setAccessibleName(help); return button;
}
}

VideoExportDialog::VideoExportDialog(const Project &project, int playbackFirst, int playbackLast,
    bool rangeEnabled, QWidget *parent)
    : VideoExportDialog(project, 0, playbackFirst, playbackLast, rangeEnabled, parent) {}

VideoExportDialog::VideoExportDialog(const Project &project, int activeShotIndex, int playbackFirst, int playbackLast,
    bool rangeEnabled, QWidget *parent) : QDialog(parent), m_project(project), m_exporter(new VideoExport(this)),
    m_queue(new VideoExportQueue(this)), m_activeShot(qBound(0, activeShotIndex, qMax(0, int(project.shots.size()) - 1))),
    m_queueShot(m_activeShot) {
    setObjectName("videoExportDialog"); setWindowTitle("Export video"); setWindowModality(Qt::WindowModal);
    setMinimumWidth(360); resize(500, 560);
    m_exporter->setObjectName("videoExporter");
    const auto defaults = videoExportDefaults(project, m_activeShot);
    m_frameCount = qMax(1, videoExportDurationFrames(project, VideoExportScope::Shot, m_activeShot));
    m_shotState = {defaults, 0, 1, m_frameCount, true};
    const auto movieDefaults = videoExportMovieDefaults(project);
    m_movieState = {movieDefaults, 0, 1, movieDefaults.lastFrame + 1, true};
    for (int i = 0; i < project.shots.size(); ++i) {
        const auto shotDefaults = videoExportDefaults(project, i);
        m_queueStates.append({shotDefaults, 0, 1, shotDefaults.lastFrame + 1, true});
    }
    m_customLast = m_frameCount;
    m_playbackFirst = qBound(0, playbackFirst, m_frameCount - 1);
    m_playbackLast = qBound(m_playbackFirst, playbackLast, m_frameCount - 1);
    m_hasPlaybackRange = rangeEnabled && playbackFirst >= 0 && playbackLast >= playbackFirst && playbackLast < m_frameCount;
    m_aspectRatio = project.width > 0 && project.height > 0 ? qBound(1.0 / 256, double(project.width) / project.height, 256.0) : 1;
    m_projectError = project.validate();
    m_hasAudio = defaults.includeAudio;
    m_silentAvailability = VideoExport::availabilityError(false);
    m_audioAvailability = VideoExport::availabilityError(true);

    auto *layout = new QVBoxLayout(this); layout->setContentsMargins(20, 16, 20, 16); layout->setSpacing(10);
    m_controls = new QWidget; m_controls->setObjectName("videoExportControls");
    auto *controls = new QVBoxLayout(m_controls); controls->setContentsMargins(0, 0, 0, 0); controls->setSpacing(10);
    layout->addWidget(m_controls, 1);

    m_scope = choices("videoExportScope", {"Current shot", "Movie", "Shot queue"}, this);
    m_scope->setAccessibleName("Export scope"); controls->addWidget(m_scope);
    m_queueShots = new QListWidget(this); m_queueShots->setObjectName("videoExportQueueShots");
    m_queueShots->setAccessibleName("Shots to export; select a row to edit its settings");
    m_queueShots->setMinimumHeight(90); m_queueShots->setMaximumHeight(140);
    for (int i = 0; i < project.shots.size(); ++i) {
        const auto &shot = project.shots[i];
        auto *row = new QListWidgetItem(shot.number + " · " + shot.name, m_queueShots);
        row->setData(Qt::UserRole, shot.id); row->setFlags(row->flags() | Qt::ItemIsUserCheckable);
        row->setCheckState(Qt::Checked);
        row->setToolTip("Select this row to edit its range, size, quality and audio settings");
    }
    m_queueShots->setCurrentRow(m_queueShot); m_queueShots->hide(); controls->addWidget(m_queueShots);
    m_reviewExisting = new QCheckBox("Review existing files before replacement", this);
    m_reviewExisting->setObjectName("videoExportReviewExisting");
    m_reviewExisting->setToolTip("Existing files are skipped unless you review and approve replacement individually.");
    m_reviewExisting->hide(); controls->addWidget(m_reviewExisting);
    auto *format = new QLabel("MP4 · H.264"); format->setObjectName("videoExportFormat"); controls->addWidget(format);
    auto *rangeRow = new QHBoxLayout;
    m_range = choices("videoExportRange", {"Full shot", "Custom range"}, this);
    m_range->setAccessibleName("Export frame range"); rangeRow->addWidget(m_range, 1);
    m_usePlayback = iconButton("videoExportUsePlayback", "scan", "Use the current playback range", this);
    m_usePlayback->setEnabled(m_hasPlaybackRange); rangeRow->addWidget(m_usePlayback); controls->addLayout(rangeRow);

    auto *form = new QFormLayout; form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    form->setRowWrapPolicy(QFormLayout::WrapLongRows); form->setVerticalSpacing(10); form->setHorizontalSpacing(10);
    controls->addLayout(form);
    auto number = [this, form](const QString &label, const char *id, int minimum, int maximum, int initial, const QString &unit) {
        auto *control = new ValueSlider(label, minimum, maximum, initial, 0, unit, this);
        control->setObjectName(id); form->addRow(label, control); return control;
    };
    m_first = number("Start · frame", "videoExportFirst", 1, m_frameCount, 1, "frame");
    m_last = number("End · frame", "videoExportLast", 1, m_frameCount, m_frameCount, "frame");
    m_first->setSliderRange(1, qMin(m_frameCount, 3600)); m_last->setSliderRange(1, qMin(m_frameCount, 3600));
    m_first->setToolTip("First included frame, numbered from 1"); m_last->setToolTip("Last included frame, numbered from 1");
    m_first->setEnabled(false); m_last->setEnabled(false);

    auto *widthRow = new QWidget; auto *widthLayout = new QHBoxLayout(widthRow); widthLayout->setContentsMargins(0, 0, 0, 0);
    m_width = new ValueSlider("Video width", 16, 4096, defaults.size.width(), 0, "px", this); m_width->setObjectName("videoExportWidth");
    m_width->setSingleStep(2); m_width->setSliderRange(16, 3840); widthLayout->addWidget(m_width, 1);
    m_aspect = iconButton("videoExportAspect", "lock-keyhole", "Keep the movie aspect ratio", this);
    m_aspect->setCheckable(true); m_aspect->setChecked(true); widthLayout->addWidget(m_aspect); form->addRow("Width · px", widthRow);
    m_height = number("Height · px", "videoExportHeight", 16, 4096, defaults.size.height(), "px");
    m_height->setSingleStep(2); m_height->setSliderRange(16, 2160);
    m_width->setToolTip("Output width in even pixels, from 16 to 4096"); m_height->setToolTip("Output height in even pixels, from 16 to 4096");

    m_quality = choices("videoExportQuality", {"Draft", "Standard", "High"}, this);
    m_quality->setAccessibleName("Video quality"); m_quality->setCurrentIndex(int(defaults.quality));
    m_quality->setTabToolTip(0, "Draft: smaller file, lower image quality");
    m_quality->setTabToolTip(1, "Standard: balanced size and quality");
    m_quality->setTabToolTip(2, "High: higher image quality and larger file");
    form->addRow("Quality", m_quality);
    m_audio = new QCheckBox("Include audio", this); m_audio->setObjectName("videoExportAudio");
    m_audio->setChecked(m_hasAudio && defaults.includeAudio); m_audio->setEnabled(m_hasAudio);
    m_audio->setToolTip(m_hasAudio ? "Include unmuted timeline audio with its current timing and volume" : "There are no unmuted audio clips in this shot");
    form->addRow(m_audio);
    m_summary = new QLabel; m_summary->setObjectName("videoExportSummary"); m_summary->setTextFormat(Qt::PlainText);
    m_summary->setStyleSheet(QString("color: %1;").arg(ui::muted)); m_summary->setWordWrap(true);
    controls->addWidget(m_summary); controls->addStretch();

    m_destinationLabel = new QLabel("Save video to"); auto *fileLabel = m_destinationLabel; controls->addWidget(fileLabel);
    auto *destinationRow = new QHBoxLayout;
    m_destination = new QLineEdit(this); m_destination->setObjectName("videoExportDestination");
    m_destination->setAccessibleName("Video destination"); m_destination->setPlaceholderText("Choose an MP4 file");
    m_destination->setMaxLength(4096); fileLabel->setBuddy(m_destination); destinationRow->addWidget(m_destination, 1);
    auto *browseButton = iconButton("videoExportBrowse", "folder-open", "Choose video destination", this);
    destinationRow->addWidget(browseButton); controls->addLayout(destinationRow);

    m_status = new ExportStatus(this); m_status->setObjectName("videoExportStatus"); layout->addWidget(m_status);
    m_progress = new QProgressBar(this); m_progress->setObjectName("videoExportProgress");
    m_progress->setRange(0, 100); m_progress->setValue(0); m_progress->setTextVisible(false); m_progress->setFixedHeight(6);
    layout->addWidget(m_progress);
    auto *buttons = new QDialogButtonBox(this);
    m_export = buttons->addButton("Export", QDialogButtonBox::AcceptRole); m_export->setObjectName("videoExportStart");
    m_export->setIcon(appIcon("video")); m_export->setDefault(true);
    m_cancel = buttons->addButton(QDialogButtonBox::Cancel); m_cancel->setObjectName("videoExportCancel"); m_cancel->setAutoDefault(false);
    layout->addWidget(buttons);

    connect(m_scope, &QTabBar::currentChanged, this, &VideoExportDialog::changeScope);
    connect(m_queueShots, &QListWidget::currentRowChanged, this, &VideoExportDialog::changeQueueShot);
    connect(m_queueShots, &QListWidget::itemChanged, this, [this] { if (!m_changing) { m_failure.clear(); refresh(); } });
    connect(m_queue, &VideoExportQueue::itemStarted, this, [this](int index, int total, const QString &label) {
        if (!m_closing) setStatus(QString("%1 / %2 · %3").arg(index + 1).arg(total).arg(label));
    });
    connect(m_queue, &VideoExportQueue::progress, this, [this](int completedJobs, int totalJobs, int frames, int total, const QString &phase) {
        m_progress->setRange(0, totalJobs * 1000);
        m_progress->setValue(completedJobs * 1000 + (total > 0 ? int(qint64(frames) * 1000 / total) : 0));
        if (!m_closing) setStatus(QString("Shot %1 / %2 · %3 · %4 / %5 frames").arg(completedJobs + 1).arg(totalJobs).arg(phase).arg(frames).arg(total));
    });
    connect(m_queue, &VideoExportQueue::itemFinished, this, [this](int index, const VideoExportResult &result) {
        m_progress->setRange(0, m_runningQueueShots.size() * 1000);
        m_progress->setValue((index + 1) * 1000);
        if (index < m_runningQueueShots.size()) {
            const int shotIndex = m_runningQueueShots[index]; const auto &shot = m_project.shots[shotIndex];
            const QString status = result.outcome == VideoExportOutcome::Completed ? "Done" : result.outcome == VideoExportOutcome::Skipped ? "Skipped" : result.outcome == VideoExportOutcome::Cancelled ? "Cancelled" : "Failed";
            m_queueShots->item(shotIndex)->setText(shot.number + " · " + shot.name + " · " + status);
            m_queueShots->item(shotIndex)->setToolTip(result.error.isEmpty() ? result.path : result.error);
        }
        if (result.outcome == VideoExportOutcome::Completed) emit exported(result.path);
    });
    connect(m_queue, &VideoExportQueue::finished, this, &VideoExportDialog::finishQueue);
    connect(m_range, &QTabBar::currentChanged, this, &VideoExportDialog::changeRangeMode);
    connect(m_usePlayback, &QToolButton::clicked, this, [this] {
        m_range->setCurrentIndex(1);
        m_first->setValueSilently(m_playbackFirst + 1); m_last->setValueSilently(m_playbackLast + 1);
        m_customFirst = m_playbackFirst + 1; m_customLast = m_playbackLast + 1; m_failure.clear(); refresh();
    });
    for (auto *control : {m_first, m_last}) connect(control, &ValueSlider::valueChanged, this, [this] {
        if (m_changing) return;
        if (m_range->currentIndex() == 1) { m_customFirst = qRound(m_first->value()); m_customLast = qRound(m_last->value()); }
        m_failure.clear(); refresh();
    });
    connect(m_width, &ValueSlider::valueChanged, this, [this] { updateCompanionDimension(true); });
    connect(m_height, &ValueSlider::valueChanged, this, [this] { updateCompanionDimension(false); });
    connect(m_width, &ValueSlider::editingFinished, this, [this] { normalizeDimensions(true); });
    connect(m_height, &ValueSlider::editingFinished, this, [this] { normalizeDimensions(false); });
    connect(m_aspect, &QToolButton::toggled, this, [this](bool locked) {
        m_aspect->setIcon(appIcon(locked ? "lock-keyhole" : "lock-keyhole-open"));
        m_aspect->setToolTip(locked ? "Keep the movie aspect ratio" : "Free output size; artwork fits inside with a white background");
        m_aspect->setAccessibleName(m_aspect->toolTip()); normalizeDimensions(m_widthDriven);
    });
    connect(m_quality, &QTabBar::currentChanged, this, [this] { m_failure.clear(); refresh(); });
    connect(m_audio, &QCheckBox::toggled, this, [this] { m_failure.clear(); refresh(); });
    connect(m_destination, &QLineEdit::textChanged, this, [this] { m_failure.clear(); refresh(); });
    connect(browseButton, &QToolButton::clicked, this, &VideoExportDialog::browse);
    connect(m_export, &QPushButton::clicked, this, &VideoExportDialog::startExport);
    connect(m_cancel, &QPushButton::clicked, this, &VideoExportDialog::reject);
    connect(m_exporter, &VideoExport::progress, this, [this](int completed, int total, const QString &phase) {
        m_progress->setRange(0, qMax(0, total)); m_progress->setValue(completed);
        if (!m_closing) setStatus(total > 0 ? QString("%1 · %2 / %3 frames").arg(phase).arg(completed).arg(total) : phase);
    });
    connect(m_exporter, &VideoExport::finished, this, &VideoExportDialog::finishExport);
    normalizeDimensions(true); refresh();
}

VideoExportSettings VideoExportDialog::settings() const {
    VideoExportSettings value;
    value.shotIndex = m_previousScope == 2 ? m_queueShot : m_activeShot;
    value.scope = m_previousScope == 1 ? VideoExportScope::Movie : VideoExportScope::Shot;
    value.firstFrame = m_range->currentIndex() == 0 ? 0 : qRound(m_first->value()) - 1;
    value.lastFrame = m_range->currentIndex() == 0 ? m_frameCount - 1 : qRound(m_last->value()) - 1;
    value.size = {qRound(m_width->value()), qRound(m_height->value())};
    value.quality = static_cast<VideoExportQuality>(m_quality->currentIndex());
    value.includeAudio = m_hasAudio && m_audio->isChecked(); return value;
}
bool VideoExportDialog::busy() const { return m_exporter->busy() || m_queue->busy(); }
void VideoExportDialog::saveControls() {
    SavedControls state{settings(), m_range->currentIndex(), m_customFirst, m_customLast, m_aspect->isChecked()};
    if (m_previousScope == 0) m_shotState = state;
    else if (m_previousScope == 1) m_movieState = state;
    else if (m_queueShot >= 0 && m_queueShot < m_queueStates.size()) m_queueStates[m_queueShot] = state;
}
void VideoExportDialog::restoreControls(const SavedControls &state) {
    m_changing = true;
    m_frameCount = qMax(1, videoExportDurationFrames(m_project, state.settings.scope, state.settings.shotIndex));
    m_hasAudio = state.settings.scope == VideoExportScope::Movie
        ? videoExportMovieDefaults(m_project).includeAudio : videoExportDefaults(m_project, state.settings.shotIndex).includeAudio;
    const QSignalBlocker rangeBlock(m_range), audioBlock(m_audio), qualityBlock(m_quality), aspectBlock(m_aspect);
    m_range->setTabText(0, state.settings.scope == VideoExportScope::Movie ? "Full movie" : "Full shot");
    m_range->setCurrentIndex(state.rangeMode); m_previousRange = state.rangeMode;
    m_customFirst = state.customFirst; m_customLast = state.customLast;
    m_first->setRange(1, m_frameCount); m_last->setRange(1, m_frameCount);
    m_first->setSliderRange(1, qMin(m_frameCount, 3600)); m_last->setSliderRange(1, qMin(m_frameCount, 3600));
    m_first->setValueSilently(state.rangeMode ? state.customFirst : 1);
    m_last->setValueSilently(state.rangeMode ? state.customLast : m_frameCount);
    m_first->setEnabled(state.rangeMode == 1); m_last->setEnabled(state.rangeMode == 1);
    m_width->setValueSilently(state.settings.size.width()); m_height->setValueSilently(state.settings.size.height());
    m_quality->setCurrentIndex(int(state.settings.quality));
    m_audio->setChecked(m_hasAudio && state.settings.includeAudio); m_audio->setEnabled(m_hasAudio);
    m_audio->setToolTip(m_hasAudio ? "Include unmuted timeline audio with its current timing and volume" : "There are no unmuted audio clips in this range");
    m_aspect->setChecked(state.aspect); m_aspect->setIcon(appIcon(state.aspect ? "lock-keyhole" : "lock-keyhole-open"));
    m_usePlayback->setEnabled(m_hasPlaybackRange && m_previousScope != 1 && (m_previousScope != 2 || m_queueShot == m_activeShot));
    m_changing = false; m_failure.clear(); refresh();
}
void VideoExportDialog::setSingleShotOnly(){m_scope->setCurrentIndex(0);m_singleShotOnly=true;m_scope->setTabEnabled(1,false);m_scope->setTabEnabled(2,false);m_scope->hide();}
void VideoExportDialog::changeScope(int index) {
    if(m_singleShotOnly && index!=0){const QSignalBlocker block(m_scope);m_scope->setCurrentIndex(0);return;}
    if (m_changing || busy()) return;
    for (auto *control : {m_first, m_last, m_width, m_height}) control->commitEditing();
    saveControls(); m_scopeDestinations[m_previousScope] = m_destination->text();
    m_previousScope = index;
    m_queueShots->setVisible(index == 2); m_reviewExisting->setVisible(index == 2);
    m_destinationLabel->setText(index == 2 ? "Export shots to folder" : "Save video to");
    m_destination->setPlaceholderText(index == 2 ? "Choose an output folder" : "Choose an MP4 file");
    m_destination->setAccessibleName(index == 2 ? "Shot export folder" : "Video destination");
    { const QSignalBlocker block(m_destination); m_destination->setText(m_scopeDestinations[index]); }
    if (index == 2 && !m_queueStates.isEmpty()) restoreControls(m_queueStates[qBound(0, m_queueShot, int(m_queueStates.size()) - 1)]);
    else restoreControls(index == 1 ? m_movieState : m_shotState);
}
void VideoExportDialog::changeQueueShot(int row) {
    if (m_changing || busy() || m_previousScope != 2 || row < 0 || row >= m_queueStates.size()) return;
    for (auto *control : {m_first, m_last, m_width, m_height}) control->commitEditing();
    saveControls(); m_queueShot = row; restoreControls(m_queueStates[row]);
}
QVector<VideoExportQueueItem> VideoExportDialog::queueItems() const {
    QVector<VideoExportQueueItem> result;
    for (int i = 0; i < m_queueShots->count(); ++i) {
        if (m_queueShots->item(i)->checkState() != Qt::Checked) continue;
        auto config = m_previousScope == 2 && m_queueShot == i ? settings() : m_queueStates[i].settings;
        config.shotIndex = i; config.scope = VideoExportScope::Shot; config.overwriteApproved = false;
        result.append({config, QDir(destination()).filePath(videoExportShotFileName(m_project, i)), false,
            m_project.shots[i].number + " · " + m_project.shots[i].name});
    }
    return result;
}
void VideoExportDialog::setQueuedShotIds(const QStringList &ids) {
    if (busy()) return;
    m_scope->setCurrentIndex(2);
    m_changing = true; int first = -1;
    for (int i = 0; i < m_queueShots->count(); ++i) {
        const bool checked = ids.contains(m_project.shots[i].id);
        m_queueShots->item(i)->setCheckState(checked ? Qt::Checked : Qt::Unchecked);
        if (checked && first < 0) first = i;
    }
    m_changing = false;
    if (first >= 0) m_queueShots->setCurrentRow(first);
    refresh();
}
void VideoExportDialog::startQueue() {
    saveControls(); auto items = queueItems();
    if (items.isEmpty()) return;
    if (m_reviewExisting->isChecked()) for (auto &item : items) {
        const QFileInfo target(item.destination);
        if (!target.exists() && !target.isSymLink()) continue;
        QMessageBox confirmation(QMessageBox::Question, "Existing shot export",
            QString("Replace “%1”? Skip keeps the existing file.").arg(target.fileName()),
            QMessageBox::Yes | QMessageBox::Ignore | QMessageBox::Cancel, this);
        confirmation.setObjectName("videoExportQueueOverwrite"); confirmation.setTextFormat(Qt::PlainText);
        confirmation.button(QMessageBox::Yes)->setText("Replace"); confirmation.button(QMessageBox::Ignore)->setText("Skip");
        confirmation.setDefaultButton(QMessageBox::Ignore);
        const auto choice = confirmation.exec();
        if (choice == QMessageBox::Cancel) return;
        item.overwriteApproved = choice == QMessageBox::Yes;
    }
    m_runningQueueShots.clear();
    for (const auto &item : items) m_runningQueueShots.append(item.settings.shotIndex);
    QString error;
    if (!m_queue->start(m_project, items, &error)) { setStatus(error, true); return; }
    m_failure.clear(); m_controls->setEnabled(false); m_export->setEnabled(false); m_cancel->setEnabled(true);
    m_progress->setRange(0, items.size() * 1000); m_progress->setValue(0); setStatus("Preparing shot queue…");
}
void VideoExportDialog::finishQueue(const VideoExportQueueReport &report) {
    if (m_closing) { QDialog::reject(); return; }
    if (report.outcome == VideoExportOutcome::Completed && report.skippedJobs == 0) { QDialog::accept(); return; }
    m_controls->setEnabled(true); m_cancel->setEnabled(true);
    m_first->setEnabled(m_range->currentIndex() == 1); m_last->setEnabled(m_range->currentIndex() == 1);
    m_audio->setEnabled(m_hasAudio);
    m_usePlayback->setEnabled(m_hasPlaybackRange && m_queueShot == m_activeShot);
    m_failure = report.outcome == VideoExportOutcome::Failed ? report.error
        : report.outcome == VideoExportOutcome::Cancelled ? QString("Queue cancelled · %1 files exported").arg(report.completedJobs)
        : QString("Queue finished · %1 exported · %2 existing files skipped").arg(report.completedJobs).arg(report.skippedJobs);
    refresh();
}
QString VideoExportDialog::destination() const {
    QString path = m_destination->text().trimmed();
    if (m_previousScope != 2 && !path.isEmpty() && !path.endsWith(".mp4", Qt::CaseInsensitive)) path += ".mp4";
    return path.isEmpty() ? QString() : QFileInfo(path).absoluteFilePath();
}
void VideoExportDialog::setDestination(const QString &path) { m_destination->setText(path); }
void VideoExportDialog::setStatus(const QString &message, bool error) {
    m_status->setStyleSheet(QString("color: %1;").arg(error ? ui::text : ui::muted));
    static_cast<ExportStatus *>(m_status)->message(message);
}
void VideoExportDialog::refresh() {
    if (busy() || m_changing) return;
    const auto value = settings();
    const int count = qMax(0, value.lastFrame - value.firstFrame + 1);
    const QString fps = QLocale().toString(m_project.fps, 'g', 6);
    QString summary = QString("%1 frames · %2 s · %3 FPS\n%4 × %5 px · opaque white background")
        .arg(count).arg(m_project.fps > 0 ? count / m_project.fps : 0, 0, 'f', 2).arg(fps).arg(value.size.width()).arg(value.size.height());
    if (m_previousScope == 2) summary.prepend(QString("%1 shots queued · settings for %2\n").arg(queueItems().size())
        .arg(m_queueShot >= 0 && m_queueShot < m_project.shots.size() ? m_project.shots[m_queueShot].number : QString()));
    m_summary->setText(summary);
    QString error = m_projectError;
    if (error.isEmpty() && (value.firstFrame < 0 || value.lastFrame < value.firstFrame || value.lastFrame >= m_frameCount))
        error = "Choose an end frame at or after the start frame.";
    if (error.isEmpty() && (value.size.width() < 16 || value.size.height() < 16 || value.size.width() > 4096
        || value.size.height() > 4096 || value.size.width() % 2 || value.size.height() % 2))
        error = "Output width and height must be even numbers from 16 to 4096.";
    const auto availability = value.includeAudio ? m_audioAvailability : m_silentAvailability;
    if (error.isEmpty()) error = availability;
    if (error.isEmpty() && destination().isEmpty()) error = "Choose a destination for the video.";
    if (m_previousScope == 2) {
        if (error.isEmpty() && queueItems().isEmpty()) error = "Choose at least one shot for the queue.";
        if (error.isEmpty() && !QFileInfo(destination()).isDir()) error = "Choose an existing folder for the shot exports.";
        if (error.isEmpty()) for (const auto &item : queueItems()) {
            if (const auto invalid = validateVideoExportSettings(m_project, item.settings, false); !invalid.isEmpty()) { error = invalid; break; }
            if (item.settings.includeAudio && !m_audioAvailability.isEmpty()) { error = m_audioAvailability; break; }
        }
    } else if (error.isEmpty() && (QFileInfo(destination()).isDir() || QFileInfo(m_destination->text().trimmed()).isDir()))
        error = "Choose a file name, not a folder.";
    m_export->setEnabled(error.isEmpty());
    setStatus(!error.isEmpty() ? error : m_failure, !error.isEmpty() || !m_failure.isEmpty());
}
void VideoExportDialog::changeRangeMode(int index) {
    m_first->commitEditing(); m_last->commitEditing();
    if (m_previousRange == 1) { m_customFirst = qRound(m_first->value()); m_customLast = qRound(m_last->value()); }
    m_changing = true;
    m_first->setValueSilently(index == 0 ? 1 : m_customFirst); m_last->setValueSilently(index == 0 ? m_frameCount : m_customLast);
    m_first->setEnabled(index == 1); m_last->setEnabled(index == 1);
    m_previousRange = index; m_changing = false; m_failure.clear(); refresh();
}
void VideoExportDialog::updateCompanionDimension(bool widthDriven) {
    if (m_changing) return;
    m_widthDriven = widthDriven;
    if (m_aspect->isChecked()) {
        const double width = widthDriven ? m_width->value() : m_height->value() * m_aspectRatio;
        const double boundedWidth = qBound(qMax(16.0, 16.0 * m_aspectRatio), width, qMin(4096.0, 4096.0 * m_aspectRatio));
        if (widthDriven) m_height->setValueSilently(evenPixels(boundedWidth / m_aspectRatio));
        else m_width->setValueSilently(evenPixels(boundedWidth));
    }
    m_failure.clear(); refresh();
}
void VideoExportDialog::normalizeDimensions(bool widthDriven) {
    if (m_changing) return;
    m_changing = true; m_widthDriven = widthDriven;
    double width = m_width->value(), height = m_height->value();
    if (m_aspect->isChecked()) {
        width = widthDriven ? width : height * m_aspectRatio;
        width = qBound(qMax(16.0, 16.0 * m_aspectRatio), width, qMin(4096.0, 4096.0 * m_aspectRatio));
        height = width / m_aspectRatio;
    }
    m_width->setValueSilently(evenPixels(width)); m_height->setValueSilently(evenPixels(height));
    m_changing = false; m_failure.clear(); refresh();
}
void VideoExportDialog::browse() {
    if (m_previousScope == 2) {
        const auto path = QFileDialog::getExistingDirectory(this, "Export shots to folder", destination());
        if (!path.isEmpty()) setDestination(path);
        return;
    }
    // macOS native save dialogs ignore DontConfirmOverwrite. Defer the one
    // overwrite decision until Export, after the final .mp4 path is normalized.
    const auto path = QFileDialog::getSaveFileName(this, "Save video", destination().isEmpty() ? "movie.mp4" : destination(),
        "MP4 video (*.mp4)", nullptr, QFileDialog::DontUseNativeDialog | QFileDialog::DontConfirmOverwrite);
    if (!path.isEmpty()) setDestination(path);
}
void VideoExportDialog::startExport() {
    if (busy()) return;
    for (auto *control : {m_first, m_last, m_width, m_height}) control->commitEditing();
    normalizeDimensions(m_widthDriven); refresh();
    if (!m_export->isEnabled()) return;
    if (m_previousScope == 2) { startQueue(); return; }
    if (const auto error = validateVideoExportSettings(m_project, settings()); !error.isEmpty()) { setStatus(error, true); return; }
    const auto path = destination();
    const QFileInfo target(path);
    if (target.exists() || target.isSymLink()) {
        QMessageBox confirmation(QMessageBox::Question, "Replace video?", QString("Replace “%1”?").arg(QFileInfo(path).fileName()),
            QMessageBox::Yes | QMessageBox::Cancel, this);
        confirmation.setObjectName("videoExportOverwrite"); confirmation.setTextFormat(Qt::PlainText);
        confirmation.setDefaultButton(QMessageBox::Cancel);
        if (confirmation.exec() != QMessageBox::Yes) return;
    }
    auto exportSettings = settings(); exportSettings.overwriteApproved = target.exists() || target.isSymLink();
    m_destination->setText(path); m_failure.clear();
    if (!m_exporter->start(m_project, exportSettings, path)) { setStatus("A video export is already running.", true); return; }
    m_controls->setEnabled(false); m_export->setEnabled(false); m_cancel->setEnabled(true);
    m_progress->setRange(0, 0); setStatus("Preparing video…");
}
void VideoExportDialog::finishExport(const VideoExportResult &result) {
    if (result.outcome == VideoExportOutcome::Completed) {
        emit exported(result.path); QDialog::accept(); return;
    }
    if (m_closing) { QDialog::reject(); return; }
    m_controls->setEnabled(true); m_first->setEnabled(m_range->currentIndex() == 1); m_last->setEnabled(m_range->currentIndex() == 1);
    m_usePlayback->setEnabled(m_hasPlaybackRange); m_audio->setEnabled(m_hasAudio); m_cancel->setEnabled(true);
    m_progress->setRange(0, 100); m_progress->setValue(0);
    m_failure = result.outcome == VideoExportOutcome::Cancelled ? "Video export cancelled." : result.error;
    refresh();
}
void VideoExportDialog::requestClose() {
    if (!busy()) { QDialog::reject(); return; }
    if (m_closing) return;
    m_closing = true; m_cancel->setEnabled(false); setStatus("Cancelling export…"); m_exporter->cancel(); m_queue->cancel();
}
void VideoExportDialog::reject() { requestClose(); }
void VideoExportDialog::closeEvent(QCloseEvent *event) {
    if (busy()) { event->ignore(); requestClose(); }
    else QDialog::closeEvent(event);
}
} // namespace toon
