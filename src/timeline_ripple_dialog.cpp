#include "timeline_ripple_dialog.h"
#include "timeline_controller.h"
#include "document.h"
#include "icons.h"
#include "value_slider.h"
#include <QDialogButtonBox>
#include <QLabel>
#include <QPushButton>
#include <QScopedValueRollback>
#include <QTabBar>
#include <QVBoxLayout>
#include <algorithm>

namespace toon {
TimelineRippleDialog::TimelineRippleDialog(TimelineController *controller,QWidget *parent)
    : QDialog(parent),m_controller(controller),m_duration(controller->frameCount()) {
    setObjectName("timelineRippleDialog");setWindowTitle("Ripple time");setMinimumWidth(240);resize(320,390);
    auto *layout=new QVBoxLayout(this);
    m_kind=new QTabBar;m_kind->setObjectName("rippleKind");m_kind->setExpanding(true);m_kind->setDrawBase(false);
    for(const auto &entry:QVector<QPair<QString,QString>>{{"plus","Insert"},{"trash","Delete"},{"move","Move"}}) {
        // These destructive timing modes need explicit names; text-only tabs
        // keep all three directly available even in the 240-pixel layout.
        const int index=m_kind->addTab(entry.second);
        m_kind->setTabToolTip(index,entry.second+" time across all tracks");m_kind->setAccessibleTabName(index,entry.second+" time");
    }
    layout->addWidget(m_kind);
    m_scope=new QLabel;m_scope->setObjectName("rippleScope");m_scope->setWordWrap(true);
    m_scope->setMinimumHeight(fontMetrics().height()*3);layout->addWidget(m_scope);
    const int first=controller->playbackRangeEnabled()?controller->playbackFirstFrame():controller->currentFrame();
    const int count=controller->playbackRangeEnabled()?controller->playbackLastFrame()-first+1:1;
    layout->addWidget(new QLabel("Start frame"));
    m_start=new ValueSlider("Start frame",1,std::max(1,m_duration+1),first+1);m_start->setObjectName("rippleStart");layout->addWidget(m_start);
    layout->addWidget(new QLabel("Frames"));
    m_count=new ValueSlider("Number of frames",1,1000000,count);m_count->setSliderRange(1,std::min(1000000,std::max(120,m_duration)));m_count->setObjectName("rippleCount");layout->addWidget(m_count);
    auto *destinationLabel=new QLabel("Move before frame");destinationLabel->setObjectName("rippleDestinationLabel");layout->addWidget(destinationLabel);
    m_destination=new ValueSlider("Destination before frame, using current timeline numbers",1,std::max(1,m_duration+1),m_duration+1);m_destination->setObjectName("rippleDestination");layout->addWidget(m_destination);
    m_result=new QLabel;m_result->setObjectName("rippleResult");m_result->setWordWrap(true);m_result->setMinimumHeight(fontMetrics().height()*3);layout->addWidget(m_result);
    auto *buttons=new QDialogButtonBox(QDialogButtonBox::Apply|QDialogButtonBox::Cancel);
    m_apply=buttons->button(QDialogButtonBox::Apply);m_apply->setObjectName("applyTimelineRipple");m_apply->setAutoDefault(false);layout->addWidget(buttons);
    connect(m_kind,&QTabBar::currentChanged,this,&TimelineRippleDialog::refresh);
    for(auto *value:{m_start,m_count,m_destination})connect(value,&ValueSlider::valueChanged,this,&TimelineRippleDialog::refresh);
    connect(m_apply,&QPushButton::clicked,this,&TimelineRippleDialog::apply);
    connect(buttons,&QDialogButtonBox::rejected,this,&QDialog::reject);
    refresh();
}
TimelineRippleEdit TimelineRippleDialog::edit() const {
    return {static_cast<TimelineRippleKind>(m_kind->currentIndex()),qRound(m_start->value())-1,qRound(m_count->value()),qRound(m_destination->value())-1};
}
void TimelineRippleDialog::refresh() {
    if(m_refreshing)return;const QScopedValueRollback<bool> guard(m_refreshing,true);
    const auto request=edit();const bool insert=request.kind==TimelineRippleKind::Insert,move=request.kind==TimelineRippleKind::Move;
    m_destination->setEnabled(move);
    findChild<QLabel *>("rippleDestinationLabel")->setEnabled(move);
    m_scope->setText(insert?"All tracks · insert blank time, splitting drawings and audio at the cut.":move?"All tracks · move this time range before the destination frame.":"All tracks · delete this time range and close the gap.");
    // Cheap range feedback while dragging. Complete content/lock validation runs
    // once on Apply, keeping large shots responsive in this dialog.
    QString error;
    if(!m_controller->document()->isOpen()||m_controller->frameCount()!=m_duration)error="The shot changed. Close and reopen Ripple time.";
    else if(!insert&&request.startFrame+request.durationFrames>m_duration)error="The range extends past the end of the shot.";
    else if(insert&&m_duration+request.durationFrames>1000000)error="The shot cannot exceed 1,000,000 frames.";
    else if(!insert&&!move&&request.durationFrames>=m_duration)error="Keep at least one frame in the shot.";
    else if(move&&request.destinationFrame>request.startFrame&&request.destinationFrame<request.startFrame+request.durationFrames)error="Choose a destination outside the moved range.";
    const int duration=m_duration+(insert?request.durationFrames:move?0:-request.durationFrames);
    m_result->setText(error.isEmpty()?QString("Shot length: %1 → %2 frames").arg(m_duration).arg(duration):error);
    m_result->setAccessibleName(m_result->text());m_result->setToolTip(m_result->text());m_apply->setEnabled(error.isEmpty());
}
void TimelineRippleDialog::apply() {
    for(auto *value:{m_start,m_count,m_destination})value->commitEditing();
    refresh();if(!m_apply->isEnabled())return;
    QString failure;
    const auto connection=connect(m_controller->document(),&Document::warning,this,[&failure](const QString &message){failure=message;});
    const bool applied=m_controller->rippleTimeline(edit());disconnect(connection);
    if(applied){accept();return;}
    m_result->setText(failure.isEmpty()?"This timing edit could not be applied.":failure);
    m_result->setAccessibleName(m_result->text());m_result->setToolTip(m_result->text());
}
}
