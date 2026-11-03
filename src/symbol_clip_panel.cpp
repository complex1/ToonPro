#include "symbol_clip_panel.h"
#include "document.h"
#include "icons.h"
#include "property_row.h"
#include "symbol_clips.h"
#include "symbol_poses.h"
#include "symbol_frame_picker_dialog.h"
#include "symbols.h"
#include "timeline_controller.h"
#include "value_slider.h"
#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QScopedValueRollback>
#include <QTabBar>
#include <QToolButton>
#include <QVBoxLayout>
#include <algorithm>

namespace toon {
namespace {
class ClipPlaybackTabs final : public QTabBar {
public:
    using QTabBar::QTabBar;
    QSize minimumSizeHint() const override { auto size = QTabBar::minimumSizeHint(); size.setWidth(count() * 28); return size; }
protected:
    QSize tabSizeHint(int index) const override {
        auto size = QTabBar::tabSizeHint(index); if (tabText(index).isEmpty()) size.setWidth(28); return size;
    }
    QSize minimumTabSizeHint(int index) const override { return tabSizeHint(index); }
    void resizeEvent(QResizeEvent *event) override { QTabBar::resizeEvent(event); updateLabels(); }
    void changeEvent(QEvent *event) override { QTabBar::changeEvent(event); if (event->type() == QEvent::FontChange || event->type() == QEvent::StyleChange) updateLabels(); }
    void tabInserted(int index) override { QTabBar::tabInserted(index); updateLabels(); }
private:
    void updateLabels() {
        if (m_updating) return;
        const QStringList labels{"Loop", "Once", "Still", "Ping-pong", "Poses"};
        int fullWidth = 0;
        for (int i = 0; i < count(); ++i) fullWidth += QTabBar::tabSizeHint(i).width()
            + fontMetrics().horizontalAdvance(labels.value(i)) - fontMetrics().horizontalAdvance(tabText(i));
        const QScopedValueRollback<bool> guard(m_updating, true);
        for (int i = 0; i < count(); ++i) setTabText(i, width() >= fullWidth ? labels.value(i) : QString());
    }
    bool m_updating = false;
};
QToolButton *button(QWidget *parent, const char *id, const char *icon, const QString &help) {
    auto *result = new QToolButton(parent); result->setObjectName(id);
    result->setIcon(appIcon(icon)); result->setIconSize({16,16}); result->setFixedSize(28,28);
    result->setAutoRaise(true); result->setToolTip(help); result->setAccessibleName(help); return result;
}
}
SymbolClipPanel::SymbolClipPanel(TimelineController *controller, QWidget *parent)
    : QWidget(parent), m_controller(controller) {
    setObjectName("symbolClipProperties"); setMinimumWidth(0);
    auto *layout = new QVBoxLayout(this); layout->setContentsMargins(6,5,6,5); layout->setSpacing(4);
    m_controls = new QWidget; auto *controls = new QVBoxLayout(m_controls);
    controls->setContentsMargins(0,0,0,0); controls->setSpacing(4);
    auto *header = new QHBoxLayout; header->setSpacing(2);
    m_name = new QLabel; m_name->setObjectName("symbolClipName"); m_name->setTextFormat(Qt::PlainText);
    m_name->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    m_pick = button(this, "symbolClipPickFrame", "image", "Choose a symbol frame visually");
    m_posePick = button(this,"symbolPosePick","toon-symbol-pose","Choose a timed pose at the playhead");
    header->addWidget(m_name,1); header->addWidget(m_pick); header->addWidget(m_posePick); controls->addLayout(header);
    m_playback = new ClipPlaybackTabs; m_playback->setProperty("compactDrawingTabs",true); m_playback->setObjectName("symbolClipPlayback");
    m_playback->setDrawBase(false); m_playback->setExpanding(true); m_playback->setIconSize({16,16}); m_playback->setUsesScrollButtons(false);
    const QStringList names{"Loop", "Once", "Still", "Ping-pong", "Poses"};
    const QStringList icons{"repeat", "play", "image", "arrow-left-right", "toon-symbol-pose"};
    const QStringList help{"Repeat the symbol", "Play once, then hold the final frame", "Hold the chosen first frame", "Play forwards and backwards repeatedly", "Hold timed poses for manual lip-sync"};
    for (int i = 0; i < names.size(); ++i) { m_playback->addTab(appIcon(icons[i]), names[i]); m_playback->setTabToolTip(i,help[i]); m_playback->setAccessibleTabName(i,names[i]); }
    controls->addWidget(m_playback);
    m_start = new ValueSlider("Symbol clip start frame",1,1000001,1); m_start->setObjectName("symbolClipStart");
    m_duration = new ValueSlider("Symbol clip duration in frames",0,1000000,1); m_duration->setObjectName("symbolClipDuration");
    m_first = new ValueSlider("First symbol frame",1,1000000,1); m_first->setObjectName("symbolClipFirstFrame");
    m_speed = new ValueSlider("Symbol playback speed in percent",1,10000,100); m_speed->setObjectName("symbolClipSpeed"); m_speed->setSliderRange(1,400);
    m_slip = new ValueSlider("Slip symbol content in parent frames",-1000000,1000000,0); m_slip->setObjectName("symbolClipSlip"); m_slip->setSliderRange(-120,120);
    auto *durationRow = new PropertyRow("Hold",m_duration);
    m_throughEnd = button(this,"symbolClipThroughEnd","skip-forward","Continue through the shot end");
    m_throughEnd->setCheckable(true); durationRow->setAccessory(m_throughEnd);
    controls->addWidget(new PropertyRow("Start",m_start)); controls->addWidget(durationRow);
    controls->addWidget(new PropertyRow("First frame",m_first)); controls->addWidget(new PropertyRow("Speed",m_speed));
    controls->addWidget(new PropertyRow("Slip",m_slip));
    m_poseControls = new QWidget; auto *poses = new QVBoxLayout(m_poseControls); poses->setContentsMargins(0,2,0,0); poses->setSpacing(4);
    auto *poseActions = new QHBoxLayout; poseActions->setSpacing(2);
    m_posePrevious = button(this,"symbolPosePrevious","chevron-left","Previous pose key");
    m_poseDelete = button(this,"symbolPoseDelete","trash","Delete the pose key at the playhead");
    m_poseNext = button(this,"symbolPoseNext","chevron-right","Next pose key");
    m_poseStatus = new QLabel; m_poseStatus->setObjectName("symbolPoseStatus"); m_poseStatus->setTextFormat(Qt::PlainText);
    m_poseStatus->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Preferred);
    poseActions->addWidget(m_posePrevious); poseActions->addWidget(m_poseStatus,1); poseActions->addWidget(m_poseDelete); poseActions->addWidget(m_poseNext); poses->addLayout(poseActions);
    m_poseFrame = new ValueSlider("Held Symbol pose frame",1,1000000,1); m_poseFrame->setObjectName("symbolPoseFrame");
    m_poseTime = new ValueSlider("Pose key parent frame",1,1000000,1); m_poseTime->setObjectName("symbolPoseTime");
    poses->addWidget(new PropertyRow("Pose",m_poseFrame)); poses->addWidget(new PropertyRow("Key frame",m_poseTime));
    controls->addWidget(m_poseControls); layout->addWidget(m_controls);
    m_status = new QLabel; m_status->setObjectName("symbolClipStatus"); m_status->setTextFormat(Qt::PlainText); m_status->setWordWrap(true);
    layout->addWidget(m_status); layout->addStretch();
    const QList<ValueSlider *> fields{m_start,m_duration,m_first,m_speed,m_slip,m_poseFrame,m_poseTime};
    const QList<int> kinds{Start,Duration,First,Speed,Slip,PoseFrame,PoseTime};
    for (int i = 0; i < fields.size(); ++i) {
        connect(fields[i],&ValueSlider::editingStarted,this,[this,kind=kinds[i]] { beginEdit(kind); });
        connect(fields[i],&ValueSlider::editingFinished,this,[this,kind=kinds[i]] { if (!m_refreshing && !m_resolving) { beginEdit(kind); commitEditors(); } });
        connect(fields[i],&ValueSlider::editingCancelled,this,&SymbolClipPanel::cancelEditors);
    }
    connect(m_playback,&QTabBar::currentChanged,this,[this] { if (!m_refreshing && !m_resolving) { beginEdit(Playback); commitEditors(); } });
    connect(m_throughEnd,&QToolButton::clicked,this,[this] { if (!m_refreshing) { beginEdit(Duration); commitEditors(); } });
    connect(m_pick,&QToolButton::clicked,this,[this] { const auto id = selectedId(); openSymbolFramePicker(m_controller,id,this); });
    connect(m_posePick,&QToolButton::clicked,this,[this] { openSymbolFramePicker(m_controller,selectedId(),this,SymbolFramePickerMode::TimedPose); });
    connect(m_posePrevious,&QToolButton::clicked,this,[this] { m_controller->stepSymbolPose(-1); });
    connect(m_poseNext,&QToolButton::clicked,this,[this] { m_controller->stepSymbolPose(1); });
    connect(m_poseDelete,&QToolButton::clicked,this,[this] { m_controller->removeSelectedSymbolPose(m_controller->currentFrame()); });
    connect(controller,&TimelineController::changed,this,&SymbolClipPanel::refresh);
    connect(controller,&TimelineController::editResolutionRequested,this,&SymbolClipPanel::commitEditors);
    connect(controller,&TimelineController::editCancellationRequested,this,&SymbolClipPanel::cancelEditors);
    connect(controller->document(),&Document::loaded,this,&SymbolClipPanel::cancelEditors);
    connect(controller->document(),&Document::shotChanged,this,&SymbolClipPanel::cancelEditors);
    refresh();
}
QString SymbolClipPanel::selectedId() const {
    const auto pose = m_controller->selectedSymbolPoseLayerId();
    return pose.isEmpty() ? m_controller->selectedSymbolClipId() : pose;
}
const SymbolInstance *SymbolClipPanel::selectedInstance() const {
    if (!m_controller->document()->isOpen()) return nullptr;
    const auto id = selectedId();
    for (const auto &layer : m_controller->document()->activeShot().layers)
        if (layer.id == id && layer.kind == LayerKind::Symbol && layer.symbol) return &*layer.symbol;
    return nullptr;
}
void SymbolClipPanel::refresh() {
    if (m_refreshing || m_resolving) return;
    const auto id = selectedId();
    const auto shotId = m_controller->document()->isOpen() ? m_controller->document()->activeShot().id : QString();
    const auto *source = selectedInstance();
    if (m_dirty && (id != m_id || shotId != m_shotId || !source || !m_snapshot || *source != *m_snapshot
        || ((m_dirty & (PoseFrame|PoseTime)) && m_controller->currentFrame() != m_editPoseFrame))) { cancelEditors(); return; }
    const QScopedValueRollback<bool> guard(m_refreshing,true);
    const auto *instance = selectedInstance(); m_controls->setVisible(instance);
    if (!instance) { m_status->setText("Select a symbol clip."); return; }
    const auto &shot = m_controller->document()->activeShot();
    const auto *definition = findSymbol(m_controller->document()->project(),instance->symbolId);
    const QString error = symbolClipEditError(shot,id);
    const bool editable = error.isEmpty() && definition && !m_controller->playing()
        && !m_controller->document()->editingGestureActive() && !m_controller->document()->layerPropertyEditActive();
    m_controls->setEnabled(editable);
    QString status = error;
    if (status.isEmpty() && instance->durationFrames == 0) status = "Empty clip · increase Hold or continue through the shot end.";
    if (status.isEmpty() && (instance->startFrame > shot.durationFrames
        || (instance->durationFrames >= 0 && qint64(instance->startFrame) + instance->durationFrames > shot.durationFrames)))
        status = "Clip extends outside this shot · adjust Start or Hold.";
    if (status.isEmpty() && symbolClipIntervals(shot,id).isEmpty()) status = "This symbol has no exposed frames here.";
    m_status->setText(status);
    m_name->setText(definition ? definition->name : QString("Missing symbol")); m_name->setToolTip(m_name->text());
    const bool poses = instance->playback == SymbolPlayback::Poses;
    m_poseControls->setVisible(poses);
    const int parentFrame = m_controller->currentFrame();
    const bool insideClip = parentFrame >= instance->startFrame && (instance->durationFrames < 0 || parentFrame < instance->startFrame + instance->durationFrames);
    const auto *poseKey = symbolPoseKeyAtParent(*instance,parentFrame);
    const bool canSetPose = insideClip || poseKey;
    m_posePick->setEnabled(editable && canSetPose);
    m_posePick->setToolTip(canSetPose ? "Choose a timed pose at the playhead" : "Move the playhead inside this Symbol clip to add a pose");
    m_poseDelete->setEnabled(editable && poseKey);
    bool previous = false, next = false;
    for (const auto &key : instance->poseKeys) { const int frame = symbolPoseParentFrame(*instance,key.frame); previous |= frame >= 0 && frame < parentFrame; next |= frame > parentFrame && frame < m_controller->frameCount(); }
    m_posePrevious->setEnabled(editable && previous); m_poseNext->setEnabled(editable && next);
    m_poseStatus->setText(poseKey ? QString("Pose key · %1").arg(parentFrame + 1) : "Held pose"); m_poseStatus->setToolTip(m_poseStatus->text());
    m_poseFrame->setEnabled(editable && canSetPose); m_poseTime->setEnabled(editable && poseKey);
    if (m_dirty) return;
    m_poseTargetFrame = parentFrame;
    m_id = id; m_shotId = shotId; m_snapshot = *instance;
    const int maxStart = instance->durationFrames < 0 ? shot.durationFrames : shot.durationFrames - instance->durationFrames;
    m_start->setRange(1,std::max({1,maxStart + 1,instance->startFrame + 1})); m_start->setSliderRange(1,std::max(1,std::min(240,maxStart + 1)));
    m_duration->setRange(0,std::max({0,shot.durationFrames - instance->startFrame,instance->durationFrames}));
    m_duration->setSliderRange(0,std::max(0,std::min(240,shot.durationFrames - instance->startFrame)));
    const int sourceDuration = definition ? std::max(1,definition->content.durationFrames) : 1;
    m_first->setRange(1,sourceDuration); m_first->setSliderRange(1,std::min(240,sourceDuration));
    m_start->setValueSilently(instance->startFrame + 1);
    m_duration->setValueSilently(instance->durationFrames < 0 ? std::max(0,shot.durationFrames - instance->startFrame) : instance->durationFrames);
    m_first->setValueSilently(instance->firstFrame + 1); m_speed->setValueSilently(instance->speed * 100);
    m_slip->setValueSilently(instance->playbackOffset); m_playback->setCurrentIndex(int(instance->playback));
    m_throughEnd->setChecked(instance->durationFrames < 0); m_duration->setEnabled(instance->durationFrames >= 0);
    const bool moving = instance->playback != SymbolPlayback::SingleFrame; m_speed->setEnabled(moving && !poses); m_slip->setEnabled(moving);
    m_poseFrame->setRange(1,sourceDuration); m_poseFrame->setSliderRange(1,std::min(240,sourceDuration));
    m_poseFrame->setValueSilently(evaluatedSymbolPose(*instance,parentFrame)+1);
    m_poseTime->setRange(1,shot.durationFrames); m_poseTime->setSliderRange(1,std::min(240,shot.durationFrames)); m_poseTime->setValueSilently(parentFrame+1);
}
void SymbolClipPanel::beginEdit(int field) {
    if (m_refreshing || m_resolving) return;
    if (!m_dirty) { const auto *instance = selectedInstance(); if (!instance) return; m_snapshot = *instance; m_editPoseFrame = m_controller->currentFrame(); m_id = selectedId(); m_shotId = m_controller->document()->activeShot().id; }
    m_dirty |= field; m_controller->setPendingEdits(this,true);
}
void SymbolClipPanel::commitEditors() {
    if (m_refreshing || m_resolving || !m_dirty || !m_snapshot) return;
    bool applied = false;
    {
        const QScopedValueRollback<bool> guard(m_resolving,true);
        for (auto *field : {m_start,m_duration,m_first,m_speed,m_slip,m_poseFrame,m_poseTime}) field->commitEditing();
        const auto *current = selectedInstance();
        if (!current || m_id != selectedId()
            || m_shotId != m_controller->document()->activeShot().id || *current != *m_snapshot) {
            m_dirty = 0; m_controller->setPendingEdits(this,false);
        } else {
            auto instance = *m_snapshot;
            if (m_dirty & Start) instance.startFrame = qRound(m_start->value()) - 1;
            if (m_dirty & Duration) instance.durationFrames = m_throughEnd->isChecked() ? -1 : qRound(m_duration->value());
            if (m_dirty & Slip) instance.playbackOffset = qRound(m_slip->value());
            if (m_dirty & First) { instance.firstFrame = qRound(m_first->value()) - 1; if (instance.playback != SymbolPlayback::Poses) instance.playbackOffset = 0; }
            if (m_dirty & Speed) instance.speed = m_speed->value() / 100.;
            if (m_dirty & Playback) instance.playback = SymbolPlayback(m_playback->currentIndex());
            if (m_dirty & PoseFrame) applied = m_controller->setSelectedSymbolPose(m_editPoseFrame,qRound(m_poseFrame->value())-1);
            else if (m_dirty & PoseTime) applied = m_controller->moveSelectedSymbolPose(m_editPoseFrame,qRound(m_poseTime->value())-1);
            else applied = instance == *current || m_controller->setSelectedSymbolPlayback(instance);
            if (applied) m_dirty = 0;
            m_controller->setPendingEdits(this,m_dirty != 0);
        }
    }
    refresh();
}
void SymbolClipPanel::cancelEditors() {
    if (m_refreshing || m_resolving) return;
    {
        const QScopedValueRollback<bool> guard(m_resolving,true);
        for (auto *field : {m_start,m_duration,m_first,m_speed,m_slip,m_poseFrame,m_poseTime}) field->cancelEditing();
        m_dirty = 0; m_snapshot.reset(); m_controller->setPendingEdits(this,false);
    }
    refresh();
}
} // namespace toon
