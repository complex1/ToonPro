#include "audio_envelope_widget.h"
#include "ui_theme.h"
#include <QFocusEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <algorithm>
#include <cmath>
namespace toon {
AudioEnvelopeWidget::AudioEnvelopeWidget(QWidget *parent):QWidget(parent){
    setObjectName("audioEnvelope");setMinimumSize(120,150);setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Fixed);setFixedHeight(170);setFocusPolicy(Qt::StrongFocus);
    setAccessibleName("Audio volume and fades");setToolTip("Drag top handles for fades and diamonds for volume. Double-click to add a volume key. Arrow keys adjust the selected handle; Delete removes a key. I / O select fades. Escape cancels.");
}
void AudioEnvelopeWidget::setClip(const AudioClip &clip,qint64 playhead,double fps){
    if(m_editing)return;const auto selected=selectedKeySample();const bool same=m_valid&&m_clip.id==clip.id;m_clip=clip;m_playhead=playhead;m_fps=fps;m_valid=true;
    if(!same)m_handle=None;
    else if(m_handle>=0){m_handle=None;if(selected&&*selected>=clip.sourceOffsetSamples&&*selected<clip.sourceOffsetSamples+clip.sampleCount)for(int i=0;i<m_clip.gainKeys.size();++i)if(m_clip.gainKeys[i].sourceSample==*selected){m_handle=i;break;}}
    update();
}
void AudioEnvelopeWidget::clear(){cancelEditing();m_valid=false;m_handle=None;update();}
QRectF AudioEnvelopeWidget::plot()const{return QRectF(12,24,qMax(1,width()-24),qMax(1,height()-46));}
qreal AudioEnvelopeWidget::xAt(qint64 sample)const{return plot().left()+qreal(sample-m_clip.sourceOffsetSamples)/qMax(qint64(1),m_clip.sampleCount-1)*plot().width();}
qint64 AudioEnvelopeWidget::sampleAt(qreal x)const{return m_clip.sourceOffsetSamples+qRound64(std::clamp((x-plot().left())/plot().width(),0.0,1.0)*qMax(qint64(0),m_clip.sampleCount-1));}
double AudioEnvelopeWidget::gainAt(qreal y)const{return std::clamp((plot().bottom()-y)/plot().height()*2.0,0.0,2.0);}
QPointF AudioEnvelopeWidget::keyPoint(int i)const{return {xAt(m_clip.gainKeys[i].sourceSample),plot().bottom()-m_clip.gainKeys[i].gain/2*plot().height()};}
QPointF AudioEnvelopeWidget::fadePoint(bool in)const{
    const auto start=m_clip.fadeInStartSample<0?m_clip.sourceOffsetSamples:m_clip.fadeInStartSample;
    const auto end=m_clip.fadeOutEndSample<0?m_clip.sourceOffsetSamples+m_clip.sampleCount:m_clip.fadeOutEndSample;
    const auto at=in?start+qMax(qint64(0),m_clip.fadeInSamples-1):end-qMax(qint64(1),m_clip.fadeOutSamples);
    return {std::clamp(xAt(at),plot().left(),plot().right()),13};
}
std::optional<qint64> AudioEnvelopeWidget::selectedKeySample()const{if(m_valid&&m_handle>=0&&m_handle<m_clip.gainKeys.size())return m_clip.gainKeys[m_handle].sourceSample;return std::nullopt;}
void AudioEnvelopeWidget::selectKey(qint64 sample){m_handle=None;for(int i=0;i<m_clip.gainKeys.size();++i)if(m_clip.gainKeys[i].sourceSample==sample){m_handle=i;break;}update();emit keySelectionChanged();}
void AudioEnvelopeWidget::paintEvent(QPaintEvent *){
    QPainter p(this);p.setRenderHint(QPainter::Antialiasing);p.fillRect(rect(),QColor(ui::base));p.setPen(QPen(QColor(hasFocus()?ui::focus:ui::border),1));p.drawRoundedRect(rect().adjusted(1,1,-1,-1),4,4);
    const auto area=plot();p.setPen(QPen(QColor(ui::border),1));for(int i=0;i<=4;++i)p.drawLine(QPointF(area.left(),area.top()+area.height()*i/4),QPointF(area.right(),area.top()+area.height()*i/4));
    if(!m_valid){p.setPen(QColor(ui::muted));p.drawText(area,Qt::AlignCenter,"Select an audio clip");return;}
    if(m_clip.asset){p.setPen(QPen(QColor(ui::disabled),1));for(int x=int(area.left());x<=area.right();x+=2){const auto first=sampleAt(x),last=sampleAt(x+2);const auto peak=audioPeak(*m_clip.asset,first,qMax(qint64(1),last-first));const qreal mid=area.center().y(),scale=area.height()*.4;p.drawLine(QPointF(x,mid-peak.maximum*scale),QPointF(x,mid-peak.minimum*scale));}}
    QPainterPath envelope;for(int x=0;x<=qRound(area.width());++x){const auto sample=sampleAt(area.left()+x);const auto gain=audioClipGainAtSourceSample(m_clip,sample);const QPointF point(area.left()+x,area.bottom()-gain/2*area.height());if(x==0)envelope.moveTo(point);else envelope.lineTo(point);}
    p.setPen(QPen(QColor(m_clip.muted?ui::muted:ui::accent),2));p.drawPath(envelope);
    if(m_playhead>=m_clip.sourceOffsetSamples&&m_playhead<m_clip.sourceOffsetSamples+m_clip.sampleCount){p.setPen(QPen(QColor(ui::focus),1,Qt::DashLine));p.drawLine(QPointF(xAt(m_playhead),area.top()),QPointF(xAt(m_playhead),area.bottom()));}
    for(int i=0;i<m_clip.gainKeys.size();++i){const auto point=keyPoint(i);if(!area.adjusted(-1,-1,1,1).contains(point))continue;p.setPen(QPen(QColor(m_handle==i?ui::focus:ui::text),1));p.setBrush(QColor(m_clip.gainAnimated?ui::accent:ui::disabled));QPolygonF diamond{point+QPointF(0,-5),point+QPointF(5,0),point+QPointF(0,5),point+QPointF(-5,0)};p.drawPolygon(diamond);}
    for(bool in:{true,false}){const auto point=fadePoint(in);p.setPen(QPen(QColor(m_handle==(in?FadeIn:FadeOut)?ui::focus:ui::text),1));p.setBrush(QColor(ui::raised));QPolygonF triangle{point+QPointF(-6,-4),point+QPointF(6,-4),point+QPointF(0,5)};p.drawPolygon(triangle);}
    p.setPen(QColor(ui::muted));p.drawText(QRectF(area.left(),height()-20,area.width()/2,18),Qt::AlignLeft,"0");p.drawText(QRectF(area.center().x(),height()-20,area.width()/2,18),Qt::AlignRight,QString::number(qreal(m_clip.sampleCount)*m_fps/audioSampleRate,'f',1)+" fr");
}
bool AudioEnvelopeWidget::beginEdit(){if(!m_valid||!isEnabled())return false;if(m_editing)return true;if(m_guard&&!m_guard())return false;m_before=m_clip;m_editing=true;return true;}
void AudioEnvelopeWidget::publish(){update();emit clipEdited(m_clip);}
void AudioEnvelopeWidget::changeHandle(QPointF point){
    if(!m_editing)return;const auto sample=sampleAt(point.x());
    if(m_handle==FadeIn){m_clip.fadeInStartSample=-1;m_clip.fadeInSamples=std::clamp(sample-m_clip.sourceOffsetSamples+1,qint64(0),m_clip.sampleCount);if(point.x()<=plot().left())m_clip.fadeInSamples=0;}
    else if(m_handle==FadeOut){m_clip.fadeOutEndSample=-1;m_clip.fadeOutSamples=std::clamp(m_clip.sourceOffsetSamples+m_clip.sampleCount-sample,qint64(0),m_clip.sampleCount);if(point.x()>=plot().right())m_clip.fadeOutSamples=0;}
    else if(m_handle>=0&&m_handle<m_clip.gainKeys.size()){
        const auto minimum=m_handle?std::max(m_clip.sourceOffsetSamples,m_clip.gainKeys[m_handle-1].sourceSample+1):m_clip.sourceOffsetSamples;
        const auto maximum=m_handle+1<m_clip.gainKeys.size()?std::min(m_clip.sourceOffsetSamples+m_clip.sampleCount-1,m_clip.gainKeys[m_handle+1].sourceSample-1):m_clip.sourceOffsetSamples+m_clip.sampleCount-1;
        auto &key=m_clip.gainKeys[m_handle];key.sourceSample=std::clamp(sample,minimum,maximum);key.gain=gainAt(point.y());m_clip.gainAnimated=true;
    }publish();
}
void AudioEnvelopeWidget::mousePressEvent(QMouseEvent *event){
    if(event->button()!=Qt::LeftButton||!m_valid){QWidget::mousePressEvent(event);return;}setFocus(Qt::MouseFocusReason);m_handle=None;double nearest=12;
    for(int i=0;i<m_clip.gainKeys.size();++i){const auto distance=QLineF(event->position(),keyPoint(i)).length();if(distance<nearest){nearest=distance;m_handle=i;}}
    for(bool in:{true,false}){const auto distance=QLineF(event->position(),fadePoint(in)).length();if(distance<nearest){nearest=distance;m_handle=in?FadeIn:FadeOut;}}
    emit keySelectionChanged();if(m_handle!=None)beginEdit();update();event->accept();
}
void AudioEnvelopeWidget::mouseMoveEvent(QMouseEvent *event){if(m_editing&&(event->buttons()&Qt::LeftButton)){changeHandle(event->position());event->accept();}else QWidget::mouseMoveEvent(event);}
void AudioEnvelopeWidget::mouseReleaseEvent(QMouseEvent *event){if(event->button()==Qt::LeftButton&&m_editing){finishEditing();event->accept();}else QWidget::mouseReleaseEvent(event);}
void AudioEnvelopeWidget::mouseDoubleClickEvent(QMouseEvent *event){
    if(event->button()!=Qt::LeftButton||!m_valid||!plot().contains(event->position())||m_clip.gainKeys.size()>=maxAudioGainKeys){QWidget::mouseDoubleClickEvent(event);return;}
    if(!beginEdit())return;const auto sample=sampleAt(event->position().x());auto it=std::lower_bound(m_clip.gainKeys.begin(),m_clip.gainKeys.end(),sample,[](const auto &key,qint64 at){return key.sourceSample<at;});
    m_handle=int(it-m_clip.gainKeys.begin());if(it!=m_clip.gainKeys.end()&&it->sourceSample==sample)it->gain=gainAt(event->position().y());else m_clip.gainKeys.insert(m_handle,{sample,gainAt(event->position().y())});m_clip.gainAnimated=true;publish();emit keySelectionChanged();finishEditing();event->accept();
}
void AudioEnvelopeWidget::finishEditing(){if(!m_editing)return;m_editing=false;emit editingFinished();}
void AudioEnvelopeWidget::cancelEditing(){if(!m_editing)return;m_clip=m_before;m_editing=false;update();emit editingCancelled();}
void AudioEnvelopeWidget::focusOutEvent(QFocusEvent *event){finishEditing();QWidget::focusOutEvent(event);}
void AudioEnvelopeWidget::keyPressEvent(QKeyEvent *event){
    if(event->key()==Qt::Key_Escape&&m_editing){cancelEditing();event->accept();return;}
    if(!m_valid){QWidget::keyPressEvent(event);return;}
    if(event->key()==Qt::Key_I||event->key()==Qt::Key_O){m_handle=event->key()==Qt::Key_I?FadeIn:FadeOut;emit keySelectionChanged();update();event->accept();return;}
    if((event->key()==Qt::Key_Delete||event->key()==Qt::Key_Backspace)&&m_handle>=0&&m_handle<m_clip.gainKeys.size()){
        if(beginEdit()){
            const double removedGain=m_clip.gainKeys[m_handle].gain;m_clip.gainKeys.removeAt(m_handle);
            if(m_clip.gainKeys.isEmpty()){if(m_clip.gainAnimated)m_clip.gain=removedGain;m_clip.gainAnimated=false;}
            m_handle=None;publish();finishEditing();emit keySelectionChanged();
        }event->accept();return;
    }
    if(m_handle==None||event->key()<Qt::Key_Left||event->key()>Qt::Key_Down){QWidget::keyPressEvent(event);return;}
    if(!beginEdit())return;const bool shift=event->modifiers()&Qt::ShiftModifier;const qint64 step=qMax(qint64(1),audioSampleFrameAtTimelineFrame(shift?10:1,m_fps));
    if(m_handle<0){auto &samples=m_handle==FadeIn?m_clip.fadeInSamples:m_clip.fadeOutSamples;const int direction=event->key()==Qt::Key_Left?-1:event->key()==Qt::Key_Right?1:0;samples=std::clamp(samples+direction*step,qint64(0),m_clip.sampleCount);if(m_handle==FadeIn)m_clip.fadeInStartSample=-1;else m_clip.fadeOutEndSample=-1;}
    else {auto &key=m_clip.gainKeys[m_handle];const auto low=m_handle?std::max(m_clip.sourceOffsetSamples,m_clip.gainKeys[m_handle-1].sourceSample+1):m_clip.sourceOffsetSamples,high=m_handle+1<m_clip.gainKeys.size()?std::min(m_clip.sourceOffsetSamples+m_clip.sampleCount-1,m_clip.gainKeys[m_handle+1].sourceSample-1):m_clip.sourceOffsetSamples+m_clip.sampleCount-1;
        if(event->key()==Qt::Key_Left||event->key()==Qt::Key_Right)key.sourceSample=std::clamp(key.sourceSample+(event->key()==Qt::Key_Left?-step:step),low,high);else key.gain=std::clamp(key.gain+(event->key()==Qt::Key_Up?1:-1)*(shift?.01:.05),0.0,2.0);m_clip.gainAnimated=true;}
    publish();finishEditing();event->accept();
}
}
