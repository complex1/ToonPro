#include "movie_review_controller.h"
#include "movie_review_renderer.h"
#include "timeline_controller.h"
#include "audio_playback.h"
#include "canvas.h"
#include "document.h"
#include "ui_theme.h"
#include <QKeyEvent>
#include <QPainter>
#include <QResizeEvent>
#include <QTabletEvent>
#include <cmath>

namespace toon {
namespace {
class ReviewViewport final : public QWidget {
public:
    ReviewViewport(MovieReviewController *controller, QWidget *parent)
        : QWidget(parent), m_controller(controller) {
        setObjectName("movieReviewViewport");
        setAccessibleName("Scene and movie preview");
        setAccessibleDescription("Space plays or pauses. Arrow keys step frames. Enter edits this shot. Escape returns to the original drawing.");
        setFocusPolicy(Qt::StrongFocus); setCursor(Qt::ArrowCursor);
        setAttribute(Qt::WA_OpaquePaintEvent); setAttribute(Qt::WA_TabletTracking);
        connect(controller,&MovieReviewController::previewChanged,this,[this](const QImage &image){m_image=image;update();});
        hide();
    }
protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);p.fillRect(rect(),QColor(ui::canvas));
        if (m_image.isNull()) {
            p.setPen(QColor(ui::muted));p.drawText(rect(),Qt::AlignCenter,"Preparing preview…");return;
        }
        const QSizeF size=QSizeF(m_image.size()).scaled(QSizeF(width(),height()),Qt::KeepAspectRatio);
        const QRectF target((width()-size.width())/2,(height()-size.height())/2,size.width(),size.height());
        p.fillRect(target,Qt::white);p.setRenderHint(QPainter::SmoothPixmapTransform);p.drawImage(target,m_image);
    }
    void keyPressEvent(QKeyEvent *event) override {
        if((event->modifiers()&~Qt::KeypadModifier)!=Qt::NoModifier){QWidget::keyPressEvent(event);return;}
        switch(event->key()) {
        case Qt::Key_Space: if(!event->isAutoRepeat())m_controller->setPlaying(!m_controller->playing());break;
        case Qt::Key_Left: m_controller->setFrame(qMax(0,m_controller->currentFrame()-1));break;
        case Qt::Key_Right: m_controller->setFrame(qMin(m_controller->frameCount()-1,m_controller->currentFrame()+1));break;
        case Qt::Key_Home: m_controller->setFrame(0);break;
        case Qt::Key_End: m_controller->setFrame(m_controller->frameCount()-1);break;
        case Qt::Key_Return: case Qt::Key_Enter: m_controller->editCurrentShot();break;
        case Qt::Key_Escape: m_controller->setMode(ReviewMode::Shot);break;
        default: QWidget::keyPressEvent(event);return;
        }
        event->accept();
    }
    void mousePressEvent(QMouseEvent *event) override {setFocus(Qt::MouseFocusReason);event->accept();}
    void mouseMoveEvent(QMouseEvent *event) override {event->accept();}
    void mouseReleaseEvent(QMouseEvent *event) override {event->accept();}
    void tabletEvent(QTabletEvent *event) override {if(event->type()==QEvent::TabletPress)setFocus(Qt::MouseFocusReason);event->accept();}
    void wheelEvent(QWheelEvent *event) override {event->accept();}
private:
    MovieReviewController *m_controller;
    QImage m_image;
};
}
MovieReviewController::MovieReviewController(TimelineController *timeline)
    : QObject(timeline),m_timeline(timeline),m_renderer(new MovieReviewRenderer(this)),m_audio(new AudioPlayback(this)) {
    setObjectName("movieReviewController");
    m_viewport=new ReviewViewport(this,timeline->canvas());
    timeline->canvas()->installEventFilter(this);
    m_timer.setTimerType(Qt::PreciseTimer);m_timer.setInterval(8);
    connect(&m_timer,&QTimer::timeout,this,&MovieReviewController::tick);
    connect(m_audio,&AudioPlayback::warning,timeline->document(),&Document::warning);
    connect(m_renderer,&MovieReviewRenderer::failed,this,[this](const QString &error){
        if(m_mode==ReviewMode::Shot)return;
        // A failed frame must not leave audio running behind a frozen image.
        m_playing=false;m_timer.stop();m_audio->stop();emit changed();emit m_timeline->document()->warning(error);
    });
    connect(m_renderer,&MovieReviewRenderer::frameReady,this,[this](int shot,int frame,const QImage &image){
        const auto here=location();
        if(m_mode!=ReviewMode::Shot&&shot==here.shotIndex&&(m_playing||frame==here.frame)){
            m_waitingForFrame=false;emit previewChanged(image);
        }
    });
    connect(m_renderer,&MovieReviewRenderer::thumbnailReady,this,&MovieReviewController::thumbnailReady);
    connect(m_renderer,&MovieReviewRenderer::thumbnailReady,this,[this](int shot,const QImage &image){
        if(m_mode!=ReviewMode::Shot&&m_waitingForFrame&&shot==location().shotIndex)emit previewChanged(image);
    });
    const auto invalidate=[this]{invalidateReview();};
    connect(timeline->document(),&Document::changed,this,invalidate);
    connect(timeline->document(),&Document::loaded,this,invalidate);
    connect(timeline->document(),&Document::shotChanged,this,invalidate);
    connect(timeline->document(),&Document::frameChanged,this,invalidate);
    connect(timeline->canvas(),&Canvas::toolChanged,this,invalidate);
    connect(timeline->canvas(),&Canvas::playbackPreviewChanged,this,[this]{if(m_mode!=ReviewMode::Shot){requestPreview();emit changed();}});
}
MovieReviewController::~MovieReviewController(){m_timer.stop();m_audio->stop();delete m_viewport.data();}
void MovieReviewController::invalidateReview(){if(m_mode!=ReviewMode::Shot)setMode(ReviewMode::Shot);}
bool MovieReviewController::setMode(ReviewMode mode) {
    if(mode==m_mode)return true;
    if(mode!=ReviewMode::Shot&&mode!=ReviewMode::Scene&&mode!=ReviewMode::Movie)return false;
    auto *document=m_timeline->document();
    if(mode!=ReviewMode::Shot){
        if(m_timeline->property("symbolEditor").toBool()||!document->isOpen()||document->strokeActive())return false;
        if(!m_timeline->prepareTimelineEdit())return false;
        m_project=document->project();
        m_segments=moviePlaybackSegments(m_project,mode==ReviewMode::Scene?MoviePlaybackScope::Scene:MoviePlaybackScope::Movie,document->activeShot().scene);
        m_frameCount=moviePlaybackDuration(m_segments);
        if(!frameCount()){emit document->warning("There are no playable shots in this review range.");m_segments.clear();return false;}
        m_frame=0;
        for(const auto &segment:m_segments)if(segment.shotIndex==document->activeShotIndex()){
            m_frame=segment.firstFrame+qBound(0,document->currentFrame(),segment.frameCount-1);break;
        }
        m_renderer->setProject(m_project);
    }
    m_playing=false;m_timer.stop();m_audio->stop();m_mode=mode;m_prefetchShot=-1;
    if(mode==ReviewMode::Shot){
        m_renderer->clear();m_project=Project{};m_segments.clear();m_frame=0;m_frameCount=0;
        m_waitingForFrame=false;emit previewChanged({});
        if(m_viewport)m_viewport->hide();
        if(m_timeline->canvas()->window()->isActiveWindow())m_timeline->canvas()->setFocus(Qt::OtherFocusReason);
    }else if(m_viewport){
        m_waitingForFrame=true;emit previewChanged({});m_viewport->setGeometry(m_timeline->canvas()->rect());m_viewport->show();m_viewport->raise();
        if(m_viewport->window()->isActiveWindow())m_viewport->setFocus(Qt::OtherFocusReason);
        requestPreview();
    }
    emit sequenceChanged();emit changed();return true;
}
bool MovieReviewController::setFrame(int frame){
    if(m_mode==ReviewMode::Shot||frame<0||frame>=frameCount())return false;
    setPlaying(false);showFrame(frame);return true;
}
void MovieReviewController::showFrame(int frame){
    const auto previous=location();m_frame=frame;
    if(previous.shotIndex!=location().shotIndex){m_waitingForFrame=true;emit previewChanged(m_renderer->cachedThumbnail(location().shotIndex));}
    requestPreview();emit changed();
}
void MovieReviewController::setPlaying(bool playing){
    if(m_mode==ReviewMode::Shot||m_playing==playing||!frameCount())return;
    if(playing){
        if(m_frame==frameCount()-1){m_frame=0;m_waitingForFrame=true;emit previewChanged(m_renderer->cachedThumbnail(location().shotIndex));}
        m_start=m_frame;m_audio->start(m_project,m_segments,m_start,0,frameCount()-1,m_looping);
        if(!m_audio->active())return;
        m_playing=true;m_timer.start();
    }else{m_playing=false;m_timer.stop();m_audio->stop();}
    requestPreview();emit changed();
}
void MovieReviewController::setLooping(bool looping){
    if(m_looping==looping)return;m_looping=looping;
    if(m_playing){m_start=m_frame;m_audio->start(m_project,m_segments,m_start,0,frameCount()-1,m_looping);}
    emit changed();
}
void MovieReviewController::tick(){
    if(!m_playing||!frameCount())return;
    const double fps=m_project.fps;
    const auto end=audioSampleFrameAtTimelineFrame(frameCount(),fps);
    qint64 sample=audioSampleFrameAtTimelineFrame(m_start,fps)+qint64(std::llround(static_cast<long double>(m_audio->elapsedUSecs())*audioSampleRate/1000000));
    if(!m_looping&&sample>=end){setPlaying(false);showFrame(frameCount()-1);return;}
    sample%=end;
    int frame=qBound(0,int(std::floor((sample+.5)*fps/audioSampleRate)),frameCount()-1);
    while(frame>0&&audioSampleFrameAtTimelineFrame(frame,fps)>sample)--frame;
    while(frame<frameCount()-1&&audioSampleFrameAtTimelineFrame(frame+1,fps)<=sample)++frame;
    if(frame!=m_frame)showFrame(frame);
}
bool MovieReviewController::editCurrentShot(){
    const auto here=location();if(m_mode==ReviewMode::Shot||here.shotIndex<0)return false;
    const QString id=m_project.shots[here.shotIndex].id;
    if(!m_timeline->prepareTimelineEdit())return false;
    setMode(ReviewMode::Shot);
    if(!m_timeline->document()->selectShot(id))return false;
    return m_timeline->setFrame(here.frame);
}
int MovieReviewController::previewDivisor()const{return m_timeline->playbackPreviewDivisor();}
void MovieReviewController::setPreviewDivisor(int divisor){m_timeline->setPlaybackPreviewDivisor(divisor);}
void MovieReviewController::requestThumbnail(int shot){if(m_mode!=ReviewMode::Shot)m_renderer->requestThumbnail(shot);}
void MovieReviewController::requestPreview(){
    const auto here=location();if(m_mode==ReviewMode::Shot||here.shotIndex<0||!m_viewport)return;
    const auto pixels=m_viewport->size()*m_viewport->devicePixelRatioF();
    m_renderer->requestFrame(here.shotIndex,here.frame,previewDivisor(),m_playing,pixels);
    if(m_prefetchShot==here.shotIndex)return;
    m_prefetchShot=here.shotIndex;
    if(m_waitingForFrame)m_renderer->requestThumbnail(here.shotIndex,true);
    // Warm the next cut independently of the current expensive frame. Its
    // correct-shot thumbnail is an interim preview until motion is ready.
    for(int i=0;i<m_segments.size();++i)if(m_segments[i].shotIndex==here.shotIndex){
        if(i+1<m_segments.size())m_renderer->requestThumbnail(m_segments[i+1].shotIndex,true);
        else if(m_looping&&!m_segments.isEmpty())m_renderer->requestThumbnail(m_segments[0].shotIndex,true);
        break;
    }
}
bool MovieReviewController::eventFilter(QObject *watched,QEvent *event){
    if(watched==m_timeline->canvas()&&m_mode!=ReviewMode::Shot){
        if(event->type()==QEvent::Resize||event->type()==QEvent::Show||event->type()==QEvent::DevicePixelRatioChange){
            if(m_viewport){m_viewport->setGeometry(m_timeline->canvas()->rect());m_viewport->raise();requestPreview();}
        }else if(event->type()==QEvent::Hide)setPlaying(false);
    }
    return QObject::eventFilter(watched,event);
}
} // namespace toon
