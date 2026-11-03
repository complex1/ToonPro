#include "audio_panel.h"
#include "audio_envelope_widget.h"
#include "timeline_controller.h"
#include "document.h"
#include "icons.h"
#include "property_row.h"
#include "ui_metrics.h"
#include "ui_theme.h"
#include "value_slider.h"
#include <QAction>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPainter>
#include <QScopedValueRollback>
#include <QScrollArea>
#include <QTabWidget>
#include <QTabBar>
#include <QToolButton>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>
namespace toon {
namespace {
enum Field {Name=1,Start=2,Source=4,Duration=8,Volume=16,FadeIn=32,FadeOut=64,Envelope=128};
const AudioClip *selectedAudio(TimelineController *controller){
    if(!controller||!controller->document()->isOpen())return nullptr;
    for(const auto &clip:controller->document()->activeShot().audioClips)if(clip.id==controller->selectedAudioClipId())return &clip;
    return nullptr;
}
class StatusLabel final:public QLabel {
public:using QLabel::QLabel;
protected:void paintEvent(QPaintEvent *)override{QPainter p(this);p.setPen(palette().color(QPalette::WindowText));p.drawText(rect(),Qt::AlignVCenter|Qt::AlignLeft,fontMetrics().elidedText(text(),Qt::ElideRight,width()));}
};
void setRange(ValueSlider *field,double minimum,double maximum){
    if(field->property("audioMinimum").isValid()&&field->property("audioMinimum").toDouble()==minimum&&field->property("audioMaximum").toDouble()==maximum)return;
    field->setProperty("audioMinimum",minimum);field->setProperty("audioMaximum",maximum);field->setRange(minimum,maximum);field->setSliderRange(minimum,std::min(maximum,minimum+240));
}
QToolButton *tool(const QString &name,const QString &icon,const QString &help){
    auto *button=new QToolButton;button->setObjectName(name);
    if(icon=="diamond"){QPixmap image(40,40);image.setDevicePixelRatio(2);image.fill(Qt::transparent);QPainter painter(&image);painter.setRenderHint(QPainter::Antialiasing);painter.setPen(QPen(QColor(ui::text),1.8));painter.drawPolygon(QPolygonF{{10,3},{17,10},{10,17},{3,10}});button->setIcon(QIcon(image));}else button->setIcon(appIcon(icon));
    button->setIconSize({ui::metrics::actionIcon,ui::metrics::actionIcon});button->setFixedSize(ui::metrics::actionTarget,ui::metrics::actionTarget);button->setToolTip(help);button->setAccessibleName(help);button->setProperty("compactDrawingAction",true);button->setFocusPolicy(Qt::StrongFocus);return button;
}
bool visibleSample(const AudioClip &clip,qint64 sample){return sample>=clip.sourceOffsetSamples&&sample<clip.sourceOffsetSamples+clip.sampleCount;}
double rawGain(AudioClip clip,qint64 sample){clip.fadeInSamples=clip.fadeOutSamples=0;clip.muted=false;return audioClipGainAtSourceSample(clip,std::clamp(sample,clip.sourceOffsetSamples,clip.sourceOffsetSamples+clip.sampleCount-1));}
void setKey(AudioClip &clip,qint64 sample,double gain){
    auto it=std::lower_bound(clip.gainKeys.begin(),clip.gainKeys.end(),sample,[](const auto &key,qint64 at){return key.sourceSample<at;});
    if(it!=clip.gainKeys.end()&&it->sourceSample==sample)it->gain=gain;
    else if(clip.gainKeys.size()<maxAudioGainKeys)clip.gainKeys.insert(int(it-clip.gainKeys.begin()),{sample,gain});
}
bool overlaps(const AudioClip &a,const AudioClip &b,double fps){const auto a0=audioSampleFrameAtTimelineFrame(a.startFrame,fps),b0=audioSampleFrameAtTimelineFrame(b.startFrame,fps),a1=a0+a.sampleCount,b1=b0+b.sampleCount;return a.id!=b.id&&((a0<b0&&b0<a1&&a1<=b1)||(b0<a0&&a0<b1&&b1<=a1));}
}
AudioPanel::AudioPanel(TimelineController *controller,QWidget *parent):QWidget(parent),m_controller(controller){
    setObjectName("audioProperties");setMinimumWidth(0);auto *outer=new QVBoxLayout(this);outer->setContentsMargins(0,0,0,0);
    auto *scroll=new QScrollArea;scroll->setObjectName("audioPropertiesScroll");scroll->setWidgetResizable(true);scroll->setFrameShape(QFrame::NoFrame);scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);outer->addWidget(scroll);
    auto *content=new QWidget;auto *layout=new QVBoxLayout(content);layout->setContentsMargins(4,4,4,4);layout->setSpacing(5);scroll->setWidget(content);
    auto *header=new QHBoxLayout;header->setSpacing(3);m_name=new QLineEdit;m_name->setObjectName("audioClipName");m_name->setPlaceholderText("Audio clip name");m_name->setAccessibleName("Audio clip name");m_name->setMaxLength(200);m_name->installEventFilter(this);header->addWidget(m_name,1);
    m_mute=tool("audioClipMute","volume-2","Mute this audio clip");m_mute->setCheckable(true);header->addWidget(m_mute);layout->addLayout(header);
    auto *volumeActions=new QWidget;auto *volumeHeader=new QHBoxLayout(volumeActions);volumeHeader->setContentsMargins(0,0,0,0);volumeHeader->setSpacing(3);
    m_automate=tool("audioVolumeAnimated","diamond","Animate volume");m_automate->setCheckable(true);m_addKey=tool("audioVolumeAddKey","plus","Add volume key at playhead");m_deleteKey=tool("audioVolumeDeleteKey","trash","Delete selected volume key or key at playhead");
    for(auto *button:{m_automate,m_addKey,m_deleteKey})volumeHeader->addWidget(button);
    m_volume=new ValueSlider("Audio volume",0,200,100,1,"%");m_volume->setObjectName("audioClipVolume");auto *volumeRow=new PropertyRow("Volume",m_volume);volumeRow->setObjectName("audioClipVolumeRow");volumeRow->setAccessory(volumeActions);layout->addWidget(volumeRow);
    m_tabs=new QTabWidget;m_tabs->setObjectName("audioFinishingTabs");m_tabs->setDocumentMode(true);layout->addWidget(m_tabs);
    m_tabs->setIconSize({ui::metrics::tabIcon,ui::metrics::tabIcon});m_tabs->tabBar()->setProperty("compactDrawingTabs",true);m_tabs->tabBar()->setFocusPolicy(Qt::StrongFocus);m_tabs->tabBar()->setAccessibleName("Audio settings");
    auto *clipPage=new QWidget;auto *clipLayout=new QVBoxLayout(clipPage);clipLayout->setContentsMargins(0,6,0,0);clipLayout->setSpacing(5);
    auto *mixPage=new QWidget;auto *mixLayout=new QVBoxLayout(mixPage);mixLayout->setContentsMargins(0,6,0,0);mixLayout->setSpacing(5);
    m_tabs->addTab(clipPage,appIcon("music"),"Clip");m_tabs->setTabToolTip(0,"Clip placement and source trim");m_tabs->addTab(mixPage,appIcon("volume-2"),"Mix");m_tabs->setTabToolTip(1,"Volume keys, fades and crossfades");m_tabs->tabBar()->setAccessibleTabName(0,"Audio clip timing");m_tabs->tabBar()->setAccessibleTabName(1,"Audio mix and fades");
    m_start=new ValueSlider("Audio start frame",1,1,1);m_start->setObjectName("audioClipStart");
    m_sourceIn=new ValueSlider("Audio source in, frame offset from zero",0,1,0);m_sourceIn->setObjectName("audioClipSourceIn");m_sourceIn->setToolTip("Source offset in frames, starting at zero. Trimming keeps the original audio available.");
    m_duration=new ValueSlider("Audio duration in frames",1,1,1);m_duration->setObjectName("audioClipDuration");m_duration->setToolTip("Duration in frames. The last partial frame is preserved until you change this value.");
    const QList<ValueSlider *> timing{m_start,m_sourceIn,m_duration};const QStringList labels{"Start","Source in","Duration"};for(int i=0;i<timing.size();++i){auto *row=new PropertyRow(labels[i],timing[i]);row->setObjectName(timing[i]->objectName()+"Row");clipLayout->addWidget(row);}clipLayout->addStretch();
    m_envelope=new AudioEnvelopeWidget;mixLayout->addWidget(m_envelope);m_envelope->setEditGuard([this]{return beginEdit(Envelope);});
    auto *curveRow=new QHBoxLayout;curveRow->setSpacing(3);curveRow->addWidget(new QLabel("Fades"));curveRow->addStretch();
    m_linear=tool("audioFadeLinear","minus","Linear fades");m_equalPower=tool("audioFadeEqualPower","spline","Equal-power fades");m_linear->setCheckable(true);m_equalPower->setCheckable(true);
    m_crossfade=tool("audioCrossfade","blend","Crossfade with an overlapping clip");m_crossfadeMenu=new QMenu(m_crossfade);m_crossfade->setMenu(m_crossfadeMenu);m_crossfade->setPopupMode(QToolButton::InstantPopup);
    for(auto *button:{m_linear,m_equalPower,m_crossfade})curveRow->addWidget(button);mixLayout->addLayout(curveRow);
    m_fadeIn=new ValueSlider("Audio fade in, frames",0,1,0,2,"fr");m_fadeIn->setObjectName("audioClipFadeIn");m_fadeOut=new ValueSlider("Audio fade out, frames",0,1,0,2,"fr");m_fadeOut->setObjectName("audioClipFadeOut");
    mixLayout->addWidget(new PropertyRow("Fade in",m_fadeIn));mixLayout->addWidget(new PropertyRow("Fade out",m_fadeOut));mixLayout->addStretch();
    const QList<ValueSlider *> fields{m_start,m_sourceIn,m_duration,m_volume,m_fadeIn,m_fadeOut};const QList<int> kinds{Start,Source,Duration,Volume,FadeIn,FadeOut};
    for(int i=0;i<fields.size();++i){connect(fields[i],&ValueSlider::editingStarted,this,[this,kind=kinds[i]]{beginEdit(kind);});connect(fields[i],&ValueSlider::editingFinished,this,&AudioPanel::commitEditors);connect(fields[i],&ValueSlider::editingCancelled,this,&AudioPanel::cancelEditors);connect(fields[i],&ValueSlider::valueChanged,this,[this]{if(!m_refreshing)updatePreview();});}
    m_status=new StatusLabel;m_status->setObjectName("audioClipStatus");m_status->setMinimumHeight(24);m_status->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Fixed);layout->addWidget(m_status);layout->addStretch();
    connect(m_name,&QLineEdit::textEdited,this,[this]{beginEdit(Name);});connect(m_name,&QLineEdit::editingFinished,this,&AudioPanel::commitEditors);
    connect(m_mute,&QToolButton::clicked,this,[this]{if(m_refreshing)return;commitEditors();if(const auto *current=selectedAudio(m_controller)){auto clip=*current;clip.muted=!clip.muted;m_controller->updateAudioClip(clip);}refresh();});
    connect(m_automate,&QToolButton::clicked,this,&AudioPanel::setAutomation);connect(m_addKey,&QToolButton::clicked,this,&AudioPanel::addGainKey);connect(m_deleteKey,&QToolButton::clicked,this,&AudioPanel::deleteGainKey);
    connect(m_envelope,&AudioEnvelopeWidget::clipEdited,this,[this](const AudioClip &clip){if(!m_refreshing&&m_dirty){m_envelopeDraft=clip;updatePreview();}});
    connect(m_envelope,&AudioEnvelopeWidget::editingFinished,this,&AudioPanel::commitEditors);connect(m_envelope,&AudioEnvelopeWidget::editingCancelled,this,&AudioPanel::cancelEditors);connect(m_envelope,&AudioEnvelopeWidget::keySelectionChanged,this,[this]{if(!m_dirty)refresh();});
    for(const auto &[button,curve]:{std::pair{m_linear,AudioFadeCurve::Linear},std::pair{m_equalPower,AudioFadeCurve::EqualPower}})connect(button,&QToolButton::clicked,this,[this,curve]{commitEditors();if(const auto *current=selectedAudio(m_controller)){auto clip=*current;clip.fadeInCurve=clip.fadeOutCurve=curve;m_controller->updateAudioClip(clip);}refresh();});
    connect(m_crossfadeMenu,&QMenu::aboutToShow,this,&AudioPanel::rebuildCrossfadeMenu);
    connect(m_tabs,&QTabWidget::currentChanged,this,[this]{commitEditors();});
    connect(controller,&TimelineController::changed,this,&AudioPanel::refresh);connect(controller,&TimelineController::editResolutionRequested,this,&AudioPanel::commitEditors);connect(controller,&TimelineController::editCancellationRequested,this,&AudioPanel::cancelEditors);refresh();
}
AudioPanel::~AudioPanel(){if(m_controller){disconnect(m_controller,nullptr,this,nullptr);for(auto *ancestor=parentWidget();ancestor;ancestor=ancestor->parentWidget())if(ancestor->objectName()=="framePropertiesContent"){disconnect(m_controller,nullptr,ancestor,nullptr);break;}m_controller->setPendingEdits(this,false);}}
qint64 AudioPanel::playheadSample(const AudioClip &clip)const{const auto fps=m_controller->document()->project().fps;return clip.sourceOffsetSamples+audioSampleFrameAtTimelineFrame(m_controller->currentFrame(),fps)-audioSampleFrameAtTimelineFrame(clip.startFrame,fps);}
void AudioPanel::refresh(){
    if(m_refreshing||!m_controller)return;const QScopedValueRollback<bool> guard(m_refreshing,true);const auto *current=selectedAudio(m_controller);const QString id=current?current->id:QString();
    const bool different=id!=m_id;const bool external=m_dirty&&m_snapshot&&current&&*current!=*m_snapshot&&!m_resolving;
    if(different||external){m_name->setModified(false);for(auto *field:{m_start,m_sourceIn,m_duration,m_volume,m_fadeIn,m_fadeOut})field->cancelEditing();m_envelope->cancelEditing();m_envelopeDraft.reset();m_dirty=0;m_controller->setPendingEdits(this,false);}
    m_id=id;if(!m_dirty)m_snapshot=current?std::optional<AudioClip>(*current):std::nullopt;
    const bool editable=current&&!m_controller->playing();m_name->setEnabled(editable);m_mute->setEnabled(editable);for(auto *field:{m_start,m_sourceIn,m_duration,m_volume,m_fadeIn,m_fadeOut})field->setEnabled(editable);
    const double fps=m_controller->document()->isOpen()?m_controller->document()->project().fps:24;
    const qint64 samples=current&&current->asset?current->asset->sampleFrameCount():1;
    const int sourceMaximum=std::max(0,int(std::floor(qreal(samples-1)*fps/audioSampleRate))),durationMaximum=std::max(1,int(std::ceil(qreal(samples-(current?current->sourceOffsetSamples:0))*fps/audioSampleRate)));
    setRange(m_start,1,std::max(1,m_controller->frameCount()));setRange(m_sourceIn,0,sourceMaximum);setRange(m_duration,1,durationMaximum);
    const double fadeMaximum=current?qreal(current->sampleCount)*fps/audioSampleRate:1;setRange(m_fadeIn,0,fadeMaximum);setRange(m_fadeOut,0,fadeMaximum);
    if(!(m_dirty&Name))m_name->setText(current?current->name:QString());m_name->setToolTip(current?current->name:QString());
    if(!(m_dirty&Start))m_start->setValueSilently(current?current->startFrame+1:1);
    if(!(m_dirty&Source))m_sourceIn->setValueSilently(current?qRound64(qreal(current->sourceOffsetSamples)*fps/audioSampleRate):0);
    if(!(m_dirty&Duration))m_duration->setValueSilently(current?audioClipDurationFrames(*current,fps):1);
    if(!(m_dirty&Volume))m_volume->setValueSilently(current?rawGain(*current,playheadSample(*current))*100:100);
    if(!(m_dirty&FadeIn))m_fadeIn->setValueSilently(current?qreal(current->fadeInSamples)*fps/audioSampleRate:0);
    if(!(m_dirty&FadeOut))m_fadeOut->setValueSilently(current?qreal(current->fadeOutSamples)*fps/audioSampleRate:0);
    m_mute->setChecked(current&&current->muted);m_mute->setIcon(appIcon(current&&current->muted?"volume-x":"volume-2"));m_mute->setToolTip(current&&current->muted?"Unmute this audio clip":"Mute this audio clip");m_mute->setAccessibleName(m_mute->toolTip());
    m_automate->setEnabled(editable);m_automate->setChecked(current&&current->gainAnimated);
    const auto at=current?playheadSample(*current):-1;const bool inside=current&&visibleSample(*current,at);bool hasKey=false;if(current)for(const auto &key:current->gainKeys)if(key.sourceSample==at)hasKey=true;
    m_volume->setEnabled(editable&&(!current->gainAnimated||(inside&&(hasKey||current->gainKeys.size()<maxAudioGainKeys))));m_addKey->setEnabled(editable&&inside&&(hasKey||current->gainKeys.size()<maxAudioGainKeys));m_deleteKey->setEnabled(editable&&(hasKey||(!different&&m_envelope->selectedKeySample().has_value())));
    bool overlap=false;if(current)for(const auto &other:m_controller->document()->activeShot().audioClips)if(overlaps(*current,other,fps))overlap=true;
    m_crossfade->setEnabled(editable&&overlap);for(auto *button:{m_linear,m_equalPower})button->setEnabled(editable);m_linear->setChecked(current&&current->fadeInCurve==AudioFadeCurve::Linear&&current->fadeOutCurve==AudioFadeCurve::Linear);m_equalPower->setChecked(current&&current->fadeInCurve==AudioFadeCurve::EqualPower&&current->fadeOutCurve==AudioFadeCurve::EqualPower);
    m_envelope->setEnabled(editable);if(current)m_envelope->setClip(m_dirty?draftClip():*current,at,fps);else m_envelope->clear();
    const QString status=!current?"Select an audio clip":m_controller->playing()?"Playback":current->gainAnimated&&!inside?"Playhead is outside this clip":QString();m_status->setText(status);m_status->setToolTip(status);m_status->setAccessibleName(status);
}
bool AudioPanel::beginEdit(int field){
    if(m_refreshing||m_resolving||!selectedAudio(m_controller))return false;
    if(!m_dirty){const QScopedValueRollback<bool> guard(m_resolving,true);const QScopedValueRollback<bool> update(m_refreshing,true);if(!m_controller->prepareTimelineEdit())return false;const auto *current=selectedAudio(m_controller);if(!current)return false;m_snapshot=*current;m_envelopeDraft.reset();}
    if(field==Volume)m_volumeSample=playheadSample(*m_snapshot);m_dirty|=field;m_controller->setPendingEdits(this,true);return true;
}
AudioClip AudioPanel::draftClip()const{
    auto clip=m_envelopeDraft?*m_envelopeDraft:*m_snapshot;const double fps=m_controller->document()->project().fps;
    if(m_dirty&Name)clip.name=m_name->text().trimmed();if(m_dirty&Start)clip.startFrame=qRound(m_start->value())-1;
    if((m_dirty&Source)&&qRound(m_sourceIn->value())!=qRound64(qreal(m_snapshot->sourceOffsetSamples)*fps/audioSampleRate))clip.sourceOffsetSamples=std::clamp(audioSampleFrameAtTimelineFrame(m_sourceIn->value(),fps),qint64(0),clip.asset->sampleFrameCount()-1);
    clip.sampleCount=std::min(clip.sampleCount,clip.asset->sampleFrameCount()-clip.sourceOffsetSamples);
    if((m_dirty&Duration)&&qRound(m_duration->value())!=audioClipDurationFrames(*m_snapshot,fps))clip.sampleCount=std::clamp(audioSampleFrameAtTimelineFrame(m_duration->value(),fps),qint64(1),clip.asset->sampleFrameCount()-clip.sourceOffsetSamples);
    if(m_dirty&Volume){if(clip.gainAnimated&&visibleSample(clip,m_volumeSample))setKey(clip,m_volumeSample,m_volume->value()/100.0);else if(!clip.gainAnimated)clip.gain=m_volume->value()/100.0;}
    if(m_dirty&FadeIn){clip.fadeInSamples=std::clamp(audioSampleFrameAtTimelineFrame(m_fadeIn->value(),fps),qint64(0),clip.sampleCount);clip.fadeInStartSample=-1;}
    if(m_dirty&FadeOut){clip.fadeOutSamples=std::clamp(audioSampleFrameAtTimelineFrame(m_fadeOut->value(),fps),qint64(0),clip.sampleCount);clip.fadeOutEndSample=-1;}
    return clip;
}
void AudioPanel::updatePreview(){if(m_refreshing||!m_dirty||!m_snapshot)return;const auto clip=draftClip();m_envelope->setClip(clip,playheadSample(clip),m_controller->document()->project().fps);if(m_dirty&Envelope){const QScopedValueRollback<bool> guard(m_refreshing,true);m_fadeIn->setValueSilently(qreal(clip.fadeInSamples)*m_controller->document()->project().fps/audioSampleRate);m_fadeOut->setValueSilently(qreal(clip.fadeOutSamples)*m_controller->document()->project().fps/audioSampleRate);}}
void AudioPanel::commitEditors(){
    if(m_refreshing||m_resolving||!m_dirty)return;const QScopedValueRollback<bool> guard(m_resolving,true);
    {const QScopedValueRollback<bool> update(m_refreshing,true);for(auto *field:{m_start,m_sourceIn,m_duration,m_volume,m_fadeIn,m_fadeOut})field->commitEditing();m_envelope->finishEditing();}
    const auto *current=selectedAudio(m_controller);if(!current||!m_snapshot||current->id!=m_id){m_dirty=0;m_envelopeDraft.reset();refresh();m_controller->setPendingEdits(this,false);return;}
    const auto clip=draftClip();const auto selectedKey=m_envelope->selectedKeySample();m_dirty=0;m_envelopeDraft.reset();m_controller->setPendingEdits(this,false);if(clip!=*current)m_controller->updateAudioClip(clip);refresh();
    // Resolving the pending gesture emits a controller refresh before the new
    // document value arrives. Restore the edited key by sample, not its old
    // index, after that intermediate refresh has finished.
    if(selectedKey)m_envelope->selectKey(*selectedKey);
}
void AudioPanel::cancelEditors(){
    if(m_refreshing||m_resolving)return;const QScopedValueRollback<bool> guard(m_resolving,true);{const QScopedValueRollback<bool> update(m_refreshing,true);for(auto *field:{m_start,m_sourceIn,m_duration,m_volume,m_fadeIn,m_fadeOut})field->cancelEditing();m_envelope->cancelEditing();}
    m_dirty=0;m_envelopeDraft.reset();m_controller->setPendingEdits(this,false);refresh();
}
void AudioPanel::setAutomation(bool enabled){commitEditors();if(const auto *current=selectedAudio(m_controller)){auto clip=*current;clip.gainAnimated=enabled;if(enabled&&clip.gainKeys.isEmpty())setKey(clip,std::clamp(playheadSample(clip),clip.sourceOffsetSamples,clip.sourceOffsetSamples+clip.sampleCount-1),clip.gain);m_controller->updateAudioClip(clip);}refresh();}
void AudioPanel::addGainKey(){commitEditors();if(const auto *current=selectedAudio(m_controller)){auto clip=*current;const auto at=playheadSample(clip);if(visibleSample(clip,at)){const auto gain=rawGain(clip,at);clip.gainAnimated=true;setKey(clip,at,gain);m_controller->updateAudioClip(clip);}}refresh();}
void AudioPanel::deleteGainKey(){const auto selected=m_envelope->selectedKeySample();commitEditors();if(const auto *current=selectedAudio(m_controller)){auto clip=*current;const auto at=selected.value_or(playheadSample(clip));const double gain=rawGain(clip,at);clip.gainKeys.erase(std::remove_if(clip.gainKeys.begin(),clip.gainKeys.end(),[at](const auto &key){return key.sourceSample==at;}),clip.gainKeys.end());if(clip.gainKeys.isEmpty()){if(clip.gainAnimated)clip.gain=gain;clip.gainAnimated=false;}m_controller->updateAudioClip(clip);}refresh();}
void AudioPanel::rebuildCrossfadeMenu(){
    commitEditors();m_crossfadeMenu->clear();const auto *current=selectedAudio(m_controller);if(!current)return;const auto id=current->id;const double fps=m_controller->document()->project().fps;
    for(const auto &other:m_controller->document()->activeShot().audioClips)if(overlaps(*current,other,fps)){
        auto *menu=m_crossfadeMenu->addMenu(other.name);menu->setObjectName("audioCrossfadePartner_"+other.id);
        const QString first=current->startFrame<other.startFrame?id:other.id,second=first==id?other.id:id;
        for(const auto &[label,curve]:{std::pair{QString("Linear"),AudioFadeCurve::Linear},std::pair{QString("Equal power"),AudioFadeCurve::EqualPower}}){auto *action=menu->addAction(label);action->setObjectName(curve==AudioFadeCurve::Linear?"audioCrossfadeLinear":"audioCrossfadeEqualPower");connect(action,&QAction::triggered,this,[this,first,second,curve]{m_controller->crossfadeAudioClips(first,second,curve);refresh();});}
    }
}
bool AudioPanel::eventFilter(QObject *watched,QEvent *event){
    if(watched==m_name&&(event->type()==QEvent::ShortcutOverride||event->type()==QEvent::KeyPress)){const auto *key=static_cast<QKeyEvent *>(event);if(key->key()==Qt::Key_Escape&&m_dirty){if(event->type()==QEvent::KeyPress)cancelEditors();event->accept();return true;}}
    return QWidget::eventFilter(watched,event);
}
}
