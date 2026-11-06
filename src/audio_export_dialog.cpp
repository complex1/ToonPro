#include "audio_export_dialog.h"
#include "icons.h"
#include "ui_theme.h"
#include "value_slider.h"
#include <QCloseEvent>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QResizeEvent>
#include <QSignalBlocker>
#include <QTabBar>
#include <QToolButton>
#include <QVBoxLayout>

namespace toon {
namespace {
class AudioExportStatus final : public QLabel {
public:
    explicit AudioExportStatus(QWidget *parent) : QLabel(parent) {
        setTextFormat(Qt::PlainText); setMinimumWidth(0);
        setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed); setFixedHeight(fontMetrics().height() + 10);
    }
    void message(const QString &text) { m_text = text; setToolTip(text); setAccessibleName(text); updateText(); }
protected:
    void resizeEvent(QResizeEvent *event) override { QLabel::resizeEvent(event); updateText(); }
private:
    void updateText() { setText(fontMetrics().elidedText(m_text, Qt::ElideRight, qMax(0, width()))); }
    QString m_text;
};
QTabBar *choices(const char *id, const QStringList &labels, QWidget *parent) {
    auto *tabs = new QTabBar(parent); tabs->setObjectName(id); tabs->setExpanding(true); tabs->setDrawBase(false);
    for (const auto &label : labels) {
        const int tab = tabs->addTab(label); tabs->setAccessibleTabName(tab, label); tabs->setTabToolTip(tab, label);
    }
    return tabs;
}
QToolButton *iconButton(const char *id, const QString &icon, const QString &help, QWidget *parent) {
    auto *button = new QToolButton(parent); button->setObjectName(id); button->setIcon(appIcon(icon));
    button->setIconSize({20, 20}); button->setMinimumSize(30, 30);
    button->setToolTip(help); button->setAccessibleName(help); return button;
}
}
AudioExportDialog::AudioExportDialog(const Project &project, int activeShotIndex, int playbackFirst,
                                   int playbackLast, bool rangeEnabled, QWidget *parent)
    : QDialog(parent), m_project(project), m_exporter(new AudioExport(this)),
      m_activeShot(qBound(0, activeShotIndex, qMax(0, int(project.shots.size()) - 1))) {
    setObjectName("audioExportDialog"); setWindowTitle("Export audio"); setWindowModality(Qt::WindowModal);
    setMinimumWidth(360); resize(480, 400); m_exporter->setObjectName("audioExporter");
    m_ranges[0].last = qMax(1, audioExportDurationFrames(project, AudioExportScope::Shot, m_activeShot));
    m_ranges[1].last = qMax(1, audioExportDurationFrames(project, AudioExportScope::Movie));
    m_frameCount = m_ranges[0].last;
    m_playbackFirst = qBound(0, playbackFirst, m_frameCount - 1);
    m_playbackLast = qBound(m_playbackFirst, playbackLast, m_frameCount - 1);
    m_hasPlaybackRange = rangeEnabled && playbackFirst >= 0 && playbackLast >= playbackFirst && playbackLast < m_frameCount;
    m_projectError = project.validate();
    auto *layout = new QVBoxLayout(this); layout->setContentsMargins(20, 16, 20, 16); layout->setSpacing(10);
    m_controls = new QWidget(this); m_controls->setObjectName("audioExportControls");
    auto *controls = new QVBoxLayout(m_controls); controls->setContentsMargins(0, 0, 0, 0); controls->setSpacing(10);
    layout->addWidget(m_controls, 1);
    m_scope = choices("audioExportScope", {"Current shot", "Movie"}, this);
    m_scope->setAccessibleName("Audio export scope"); controls->addWidget(m_scope);
    auto *format = new QLabel("WAV · 48 kHz · Stereo · 16-bit", this); controls->addWidget(format);
    auto *rangeRow = new QHBoxLayout;
    m_range = choices("audioExportRange", {"Full shot", "Custom range"}, this);
    m_range->setAccessibleName("Audio export frame range"); rangeRow->addWidget(m_range, 1);
    m_usePlayback = iconButton("audioExportUsePlayback", "scan", "Use the current playback range", this);
    rangeRow->addWidget(m_usePlayback); controls->addLayout(rangeRow);
    auto *form = new QFormLayout; form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    form->setRowWrapPolicy(QFormLayout::WrapLongRows); form->setVerticalSpacing(10); controls->addLayout(form);
    m_first = new ValueSlider("Start frame", 1, m_frameCount, 1, 0, "frame", this); m_first->setObjectName("audioExportFirst");
    m_last = new ValueSlider("End frame", 1, m_frameCount, m_frameCount, 0, "frame", this); m_last->setObjectName("audioExportLast");
    m_first->setToolTip("First included frame, numbered from 1"); m_last->setToolTip("Last included frame, numbered from 1");
    form->addRow("Start · frame", m_first); form->addRow("End · frame", m_last);
    m_summary = new QLabel(this); m_summary->setObjectName("audioExportSummary"); m_summary->setTextFormat(Qt::PlainText);
    m_summary->setWordWrap(true); m_summary->setMinimumWidth(0); m_summary->setMinimumHeight(fontMetrics().height() * 2 + 8);
    controls->addWidget(m_summary);
    controls->addWidget(new QLabel("Save audio to", this));
    auto *destinationRow = new QHBoxLayout;
    m_destination = new QLineEdit(this); m_destination->setObjectName("audioExportDestination");
    m_destination->setAccessibleName("Audio destination"); m_destination->setPlaceholderText("Choose a WAV file");
    destinationRow->addWidget(m_destination, 1);
    auto *browseButton = iconButton("audioExportBrowse", "folder-open", "Choose a WAV destination", this);
    destinationRow->addWidget(browseButton); controls->addLayout(destinationRow); controls->addStretch();
    m_status = new AudioExportStatus(this); m_status->setObjectName("audioExportStatus"); layout->addWidget(m_status);
    m_progress = new QProgressBar(this); m_progress->setObjectName("audioExportProgress");
    m_progress->setRange(0, 1000); m_progress->setValue(0); m_progress->setAccessibleName("Audio export progress"); layout->addWidget(m_progress);
    auto *buttons = new QDialogButtonBox(this);
    m_export = buttons->addButton("Export", QDialogButtonBox::AcceptRole); m_export->setObjectName("audioExportStart");
    m_cancel = buttons->addButton(QDialogButtonBox::Cancel); m_cancel->setObjectName("audioExportCancel");
    for (auto *button : {m_export, m_cancel}) { button->setAutoDefault(false); button->setDefault(false); }
    layout->addWidget(buttons);
    connect(m_scope, &QTabBar::currentChanged, this, &AudioExportDialog::changeScope);
    connect(m_range, &QTabBar::currentChanged, this, &AudioExportDialog::changeRange);
    connect(m_usePlayback, &QToolButton::clicked, this, [this] {
        m_range->setCurrentIndex(1); m_first->setValueSilently(m_playbackFirst + 1); m_last->setValueSilently(m_playbackLast + 1);
        saveRange(); m_failure.clear(); refresh();
    });
    for (auto *control : {m_first, m_last}) connect(control, &ValueSlider::valueChanged, this, [this] {
        if (m_changing) return;
        saveRange(); m_failure.clear(); refresh();
    });
    connect(m_destination, &QLineEdit::textChanged, this, [this] { m_failure.clear(); refresh(); });
    connect(browseButton, &QToolButton::clicked, this, &AudioExportDialog::browse);
    connect(m_export, &QPushButton::clicked, this, &AudioExportDialog::startExport);
    connect(m_cancel, &QPushButton::clicked, this, &AudioExportDialog::reject);
    connect(m_exporter, &AudioExport::progress, this, [this](qint64 completed, qint64 total) {
        m_progress->setValue(total > 0 ? int(completed * 1000 / total) : 0);
        if (!m_closing) setStatus(QString("Mixing audio · %1%").arg(total > 0 ? completed * 100 / total : 0));
    });
    connect(m_exporter, &AudioExport::finished, this, &AudioExportDialog::finishExport);
    restoreRange();
}
AudioExportSettings AudioExportDialog::settings() const {
    AudioExportSettings result;
    result.scope = m_previousScope == 0 ? AudioExportScope::Shot : AudioExportScope::Movie;
    result.shotIndex = m_activeShot;
    result.firstFrame = m_range->currentIndex() ? qRound(m_first->value()) - 1 : 0;
    result.lastFrame = m_range->currentIndex() ? qRound(m_last->value()) - 1 : m_frameCount - 1;
    return result;
}
QString AudioExportDialog::destination() const {
    auto path = m_destination->text().trimmed();
    if (!path.isEmpty() && !path.endsWith(".wav", Qt::CaseInsensitive)) path += ".wav";
    return path.isEmpty() ? QString{} : QFileInfo(path).absoluteFilePath();
}
void AudioExportDialog::setDestination(const QString &path) { m_destination->setText(path); }
void AudioExportDialog::saveRange() {
    auto &state = m_ranges[m_previousScope]; state.mode = m_previousRange;
    if (m_previousRange) { state.first = qRound(m_first->value()); state.last = qRound(m_last->value()); }
}
void AudioExportDialog::restoreRange() {
    m_changing = true; const auto state = m_ranges[m_previousScope];
    m_frameCount = qMax(1, audioExportDurationFrames(m_project, m_previousScope ? AudioExportScope::Movie : AudioExportScope::Shot, m_activeShot));
    m_previousRange = state.mode; const QSignalBlocker block(m_range);
    m_range->setTabText(0, m_previousScope ? "Full movie" : "Full shot"); m_range->setCurrentIndex(state.mode);
    for (auto *control : {m_first, m_last}) {
        control->setRange(1, m_frameCount); control->setSliderRange(1, qMin(m_frameCount, 3600)); control->setEnabled(state.mode == 1);
    }
    m_first->setValueSilently(state.mode ? state.first : 1); m_last->setValueSilently(state.mode ? state.last : m_frameCount);
    m_usePlayback->setEnabled(m_hasPlaybackRange && m_previousScope == 0);
    m_changing = false; m_failure.clear(); refresh();
}
void AudioExportDialog::setSingleShotOnly(){m_scope->setCurrentIndex(0);m_singleShotOnly=true;m_scope->setTabEnabled(1,false);m_scope->hide();}
void AudioExportDialog::changeScope(int index) {
    if(m_singleShotOnly && index!=0){const QSignalBlocker block(m_scope);m_scope->setCurrentIndex(0);return;}
    if (m_changing || m_exporter->busy()) return;
    for (auto *control : {m_first, m_last}) control->commitEditing();
    saveRange(); m_previousScope = index; restoreRange();
}
void AudioExportDialog::changeRange(int index) {
    if (m_changing || m_exporter->busy()) return;
    for (auto *control : {m_first, m_last}) control->commitEditing();
    saveRange(); m_ranges[m_previousScope].mode = index; restoreRange();
}
void AudioExportDialog::setStatus(const QString &message, bool error) {
    m_status->setStyleSheet(QString("color: %1;").arg(error ? ui::text : ui::muted));
    static_cast<AudioExportStatus *>(m_status)->message(message);
}
void AudioExportDialog::refresh() {
    if (m_changing || m_exporter->busy()) return;
    const auto value = settings(); const auto samples = audioExportSampleCount(m_project, value);
    m_summary->setText(QString("%1 frames · %2 s · %3 FPS\nTimeline volume, fades and mute are included.")
        .arg(qMax(0, value.lastFrame - value.firstFrame + 1)).arg(double(samples) / audioSampleRate, 0, 'f', 2)
        .arg(QLocale().toString(m_project.fps, 'g', 6)));
    QString error = m_projectError;
    if (error.isEmpty()) error = validateAudioExportSettings(m_project, value, false);
    m_export->setEnabled(error.isEmpty() && !destination().isEmpty());
    setStatus(!error.isEmpty() ? error : !m_failure.isEmpty() ? m_failure : destination().isEmpty() ? "Choose where to save the WAV file." : "Ready to export.",
              !error.isEmpty() || !m_failure.isEmpty());
}
void AudioExportDialog::browse() {
    QFileDialog chooser(this, "Save audio", destination(), "WAV audio (*.wav)");
    chooser.setAcceptMode(QFileDialog::AcceptSave); chooser.setDefaultSuffix("wav");
    chooser.setOption(QFileDialog::DontUseNativeDialog); chooser.setOption(QFileDialog::DontConfirmOverwrite);
    if (chooser.exec() == QDialog::Accepted && !chooser.selectedFiles().isEmpty()) setDestination(chooser.selectedFiles().first());
}
void AudioExportDialog::startExport() {
    if (m_exporter->busy()) return;
    for (auto *control : {m_first, m_last}) control->commitEditing();
    refresh(); if (!m_export->isEnabled()) return;
    const auto path = destination(); const QFileInfo target(path);
    bool overwriteApproved = false;
    if (target.exists() || target.isSymLink()) {
        QMessageBox confirmation(QMessageBox::Question, "Replace audio?", QString("Replace “%1”?").arg(target.fileName()),
                                 QMessageBox::Yes | QMessageBox::Cancel, this);
        confirmation.setObjectName("audioExportOverwrite"); confirmation.setTextFormat(Qt::PlainText);
        confirmation.setDefaultButton(QMessageBox::Cancel);
        if (confirmation.exec() != QMessageBox::Yes) return;
        overwriteApproved = true;
    }
    auto value = settings(); value.overwriteApproved = overwriteApproved; m_destination->setText(path);
    if (!m_exporter->start(m_project, value, path)) return;
    m_failure.clear(); m_controls->setEnabled(false); m_export->setEnabled(false);
    m_progress->setValue(0); setStatus("Preparing audio…");
}
void AudioExportDialog::finishExport(const AudioExportResult &result) {
    if (result.outcome == AudioExportOutcome::Completed) { emit exported(result.path); QDialog::accept(); return; }
    if (m_closing) { QDialog::reject(); return; }
    m_controls->setEnabled(true); m_cancel->setEnabled(true);
    m_first->setEnabled(m_previousRange == 1); m_last->setEnabled(m_previousRange == 1);
    m_usePlayback->setEnabled(m_hasPlaybackRange && m_previousScope == 0); m_progress->setValue(0);
    m_failure = result.outcome == AudioExportOutcome::Cancelled ? "Audio export cancelled." : result.error; refresh();
}
void AudioExportDialog::requestClose() {
    if (!m_exporter->busy()) { QDialog::reject(); return; }
    if (m_closing) return;
    m_closing = true; m_cancel->setEnabled(false); setStatus("Cancelling audio export…"); m_exporter->cancel();
}
void AudioExportDialog::reject() { requestClose(); }
void AudioExportDialog::closeEvent(QCloseEvent *event) {
    if (m_exporter->busy()) { event->ignore(); requestClose(); }
    else QDialog::closeEvent(event);
}
} // namespace toon
