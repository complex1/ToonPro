#include "reference3d_review.h"
#include "reference3d.h"
#include "reference3d_render.h"
#include "reference3d_import.h"
#include "reference3d_builtin.h"
#include "reference3d_catalog.h"
#include "reference3d_library.h"
#include "reference3d_pose.h"
#include "reference3d_pose_library.h"
#include "reference3d_pets.h"
#include "scene3d.h"
#include "icons.h"
#include "property_row.h"
#include "ui_theme.h"
#include "value_slider.h"
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QCloseEvent>
#include <QDockWidget>
#include <QDir>
#include <QElapsedTimer>
#include <QFileDialog>
#include <QFileInfo>
#include <QHeaderView>
#include <QInputDialog>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QMouseEvent>
#include <QMutex>
#include <QMutexLocker>
#include <QPainter>
#include <QPainterPath>
#include <QPointer>
#include <QProgressDialog>
#include <QSaveFile>
#include <QScrollArea>
#include <QScrollBar>
#include <QShowEvent>
#include <QStackedLayout>
#include <QSet>
#include <QShortcut>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStatusBar>
#include <QStandardPaths>
#include <QTabWidget>
#include <QTabBar>
#include <QThread>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QTreeWidget>
#include <QUndoCommand>
#include <QUndoStack>
#include <QUuid>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <functional>
#include <limits>
#include <exception>
#include <optional>

namespace toon {
namespace {
constexpr float pi = 3.14159265358979323846f;
QToolButton *actionButton(const char *id, const char *icon, const QString &name, QWidget *parent = nullptr) {
    auto *button = new QToolButton(parent);
    button->setObjectName(id); button->setIcon(appIcon(icon)); button->setIconSize({16,16});
    button->setFixedSize(28,28); button->setToolTip(name); button->setAccessibleName(name);
    button->setProperty("compactDrawingAction",true); return button;
}
Camera3D legacyCamera(const ReferenceCamera &source) {
    Camera3D result;
    result.target=source.target; result.yaw=source.yaw; result.pitch=source.pitch;
    result.distance=source.distance; result.fieldOfView=source.fieldOfView;
    result.orthographic=source.orthographic; result.orthoScale=source.orthoScale; return result;
}
QMatrix4x4 viewProjection(const ReferenceCamera &camera, QSize size) {
    const auto old=legacyCamera(camera);
    return cameraProjectionMatrix(old,float(qMax(1,size.width()))/qMax(1,size.height()))*cameraViewMatrix(old);
}
struct ScreenPoint { float x=0,y=0,z=0,w=0; bool valid=false; };
ScreenPoint projected(const QMatrix4x4 &matrix,QVector3D point,QSize size) {
    const auto clip=matrix*QVector4D(point,1);
    if(clip.w()<=.00001f) return {};
    const auto p=clip.toVector3DAffine();
    return {(p.x()+1)*.5f*size.width(),(1-p.y())*.5f*size.height(),p.z(),clip.w(),p.z()>=-1&&p.z()<=1};
}
using JointHandle=ReferenceJointHandle;
using ViewportFrame=ReferenceRenderFrame;
using ReviewRenderCache=ReferenceRenderCache;
QString previewBudgetError(const ReferenceScene &scene){return referenceScenePreviewBudgetError(scene);}
ViewportFrame renderReview(const ReferenceScene &scene,QSize size,const std::shared_ptr<std::atomic_bool> &cancel,ReviewRenderCache &cache,bool ground=true){
    ReferenceRenderOptions options;options.background=QColor(ui::canvas);options.grid=ground;options.gridColor=QColor(ui::border);
    return renderReferenceScene(scene,size,cancel,cache,options);
}

class ReferenceViewport;
struct ReviewCompletionGate { QMutex mutex; ReferenceViewport *receiver=nullptr; };
class ReferenceViewport final : public QWidget {
public:
    explicit ReferenceViewport(QWidget *parent=nullptr):QWidget(parent) {
        setObjectName("reference3DViewport"); setAccessibleName("3D reference canvas"); setFocusPolicy(Qt::StrongFocus);
        setMinimumSize(160,160); setMouseTracking(true);
        setToolTip("Click a model or joint to select it. Drag a selected joint ring to pose. Right-drag or Alt-drag to orbit; middle-drag to pan; scroll to zoom. Escape cancels a pose drag.");
        m_timer.setSingleShot(true);m_timer.setInterval(0);connect(&m_timer,&QTimer::timeout,this,[this]{dispatch();});
        m_idle.setSingleShot(true);m_idle.setInterval(120);connect(&m_idle,&QTimer::timeout,this,[this]{
            if(m_drag==None&&m_navigation){m_navigation=false;finishBoundary();return;}
            if(!m_stats.finalQuality||m_presented!=m_generation){m_refine=true;requestFrame();}
        });
        m_gate=std::make_shared<ReviewCompletionGate>();m_gate->receiver=this;
        m_cache=std::make_shared<ReviewRenderCache>();m_thread=new QThread;m_worker=new QObject;m_worker->moveToThread(m_thread);
        connect(m_thread,&QThread::finished,m_worker,&QObject::deleteLater);connect(m_thread,&QThread::finished,m_thread,&QObject::deleteLater);m_thread->start();
    }
    ~ReferenceViewport() override {
        // The worker may post a completion while its window is closing. Gate
        // that nonblocking invoke against destruction without joining the GUI.
        {QMutexLocker lock(&m_gate->mutex);m_gate->receiver=nullptr;}
        if(m_cancel)m_cancel->store(true);m_thread->quit();
    }
    void suppressPreview(){m_suppressed=true;m_timer.stop();m_idle.stop();m_pending=false;++m_epoch;if(m_cancel)m_cancel->store(true);}
    void setScene(const ReferenceScene &scene,bool shapeUpdate=false) {
        if(m_suppressed){m_scene=scene;return;}
        bool structure=scene.instances.size()!=m_scene.instances.size();
        if(!structure)for(int i=0;i<scene.instances.size();++i){const auto &next=scene.instances[i],&old=m_scene.instances[i];
            if(next.id!=old.id){structure=true;break;}
            if(next.asset!=old.asset&&!(shapeUpdate&&sameLayout(old,next))){structure=true;break;}
        }
        if(structure){++m_epoch;if(m_cancel)m_cancel->store(true);m_frame={};m_error.clear();}
        m_scene=scene;++m_generation;m_stats.requestedGeneration=m_generation;
        if(!interactive()){++m_epoch;if(m_cancel)m_cancel->store(true);m_refine=true;}
        else m_refine=false;
        requestFrame();if(interactive())m_idle.start();else m_idle.stop();update();
    }
    void setInteractionActive(bool active){if(m_editing==active)return;m_editing=active;if(active)m_refine=false;else finishBoundary();}
    void selection(int object,int node,bool pose,int axis,bool ik=false) { m_object=object;m_node=node;m_pose=pose;m_axis=axis;m_ik=ik;update(); }
    QImage image() const { return m_frame.image; }
    bool ready() const { return !m_running&&!m_pending&&!m_timer.isActive()&&m_presented==m_generation&&m_stats.finalQuality; }
    QString error()const{return m_error;}
    Reference3DPreviewStats stats()const{return m_stats;}
    std::function<void(int)> objectPicked;
    std::function<void(int)> jointPicked;
    std::function<void()> gestureStarted;
    std::function<void(float)> rotationDragged;
    std::function<void(QVector3D)> placementDragged;
    std::function<void(QVector3D,bool)> limbDragged;
    std::function<void(bool)> gestureFinished;
    std::function<void(ReferenceCamera)> cameraChanged;
    std::function<void(const QString&)> renderFailed;
    std::function<void(quint64,bool)> framePublished;
protected:
    void paintEvent(QPaintEvent *) override {
        QPainter painter(this);painter.fillRect(rect(),QColor(ui::canvas));painter.setRenderHint(QPainter::Antialiasing);
        if(!m_frame.image.isNull()) painter.drawImage(rect(),m_frame.image);
        if(!m_error.isEmpty()){painter.setPen(QColor(ui::text));painter.drawText(rect().adjusted(24,24,-24,-24),Qt::AlignCenter|Qt::TextWordWrap,m_error);return;}
        if(m_scene.instances.isEmpty()) { painter.setPen(QColor(ui::muted)); painter.drawText(rect().adjusted(24,24,-24,-24),Qt::AlignCenter,"Place a reference or import a character");return; }
        if(!m_pose) return;
        const auto matrix=viewProjection(m_frame.camera,size());
        for(const auto &joint:m_frame.joints) {
            if(joint.object!=m_object)continue;
            const auto point=projected(matrix,joint.world,size());if(!point.valid)continue;
            for(const auto &parent:m_frame.joints)if(parent.object==joint.object&&parent.node==joint.parent) {
                const auto end=projected(matrix,parent.world,size());if(end.valid){painter.setPen(QPen(QColor(ui::muted),1));painter.drawLine(QPointF(point.x,point.y),QPointF(end.x,end.y));}break;
            }
            const bool selected=joint.node==m_node;
            painter.setPen(QPen(QColor(ui::canvas),2));painter.setBrush(QColor(selected?ui::focus:ui::muted));painter.drawEllipse(QPointF(point.x,point.y),selected?6:4,selected?6:4);
            if(selected) {
                static const QColor colors[]{QColor(233,125,125),QColor(142,202,147),QColor(140,173,232)};
                painter.setBrush(Qt::NoBrush);painter.setPen(QPen(colors[m_axis],3));
                if(m_ik)painter.drawRect(QRectF(point.x-10,point.y-10,20,20));else painter.drawEllipse(QPointF(point.x,point.y),46,46);
                painter.setPen(QColor(ui::text));painter.drawText(QPointF(point.x+10,point.y-10),joint.name);
            }
        }
    }
    void resizeEvent(QResizeEvent *event) override { QWidget::resizeEvent(event);++m_generation;m_stats.requestedGeneration=m_generation;finishBoundary(); }
    void mousePressEvent(QMouseEvent *event) override {
        setFocus();m_last=event->position();m_start=m_last;m_camera=m_scene.camera;
        if(event->button()==Qt::RightButton||(event->modifiers()&Qt::AltModifier)){m_drag=Orbit;m_navigation=true;m_refine=false;event->accept();return;}
        if(event->button()==Qt::MiddleButton){m_drag=Pan;m_navigation=true;m_refine=false;event->accept();return;}
        if(event->button()!=Qt::LeftButton)return;
        // Hit buffers and handles must describe the displayed current state.
        // While a replacement/final frame is pending, navigation stays usable.
        if(m_presented!=m_generation||m_frame.ids.isNull()){event->accept();return;}
        if(m_pose) {
            const auto matrix=viewProjection(m_frame.camera,size());float closest=14*14;int node=-1;
            for(const auto &joint:m_frame.joints)if(joint.object==m_object) {
                const auto p=projected(matrix,joint.world,size());if(!p.valid)continue;
                const float distance=std::pow(p.x-m_last.x(),2)+std::pow(p.y-m_last.y(),2);
                if(distance<closest){closest=distance;node=joint.node;}
                if(!m_ik&&joint.node==m_node&&std::abs(std::hypot(p.x-m_last.x(),p.y-m_last.y())-46)<10){node=m_node;closest=0;}
            }
            if(node>=0){if(jointPicked)jointPicked(node);m_drag=m_ik?Limb:Rotate;if(gestureStarted)gestureStarted();event->accept();return;}
        }
        int object=-1;
        if(!m_frame.ids.isNull()&&width()>0&&height()>0) {
            const int x=qBound(0,int(event->position().x()*m_frame.ids.width()/width()),m_frame.ids.width()-1);
            const int y=qBound(0,int(event->position().y()*m_frame.ids.height()/height()),m_frame.ids.height()-1);
            const int oldIndex=int(reinterpret_cast<const quint32*>(m_frame.ids.constScanLine(y))[x])-1;
            if(oldIndex>=0&&oldIndex<m_frame.objectIds.size())for(int i=0;i<m_scene.instances.size();++i)if(m_scene.instances[i].id==m_frame.objectIds[oldIndex]){object=i;break;}
        }
        if(objectPicked)objectPicked(object);
        if(object>=0&&!m_pose){m_drag=Move;if(gestureStarted)gestureStarted();}else{m_drag=Orbit;m_navigation=true;m_refine=false;}
        event->accept();
    }
    void mouseMoveEvent(QMouseEvent *event) override {
        if(m_drag==None)return;
        const QPointF delta=event->position()-m_last;m_last=event->position();
        if(m_drag==Rotate){if(rotationDragged)rotationDragged(float(delta.x()-delta.y())*.65f);}
        else if(m_drag==Move||m_drag==Limb) {
            const auto camera=legacyCamera(m_scene.camera);const auto inverse=cameraViewMatrix(camera).inverted();
            const float span=m_scene.camera.orthographic?m_scene.camera.orthoScale:2*m_scene.camera.distance*std::tan(m_scene.camera.fieldOfView*pi/360);
            const QVector3D world=inverse.mapVector(QVector3D(delta.x(),-delta.y(),0))*span/qMax(1,height());
            if(m_drag==Limb){if(limbDragged)limbDragged(world,event->modifiers()&Qt::ShiftModifier);}else if(placementDragged)placementDragged(world);
        } else {
            auto camera=m_scene.camera;
            if(m_drag==Orbit){camera.yaw-=delta.x()*.45f;camera.pitch=qBound(-85.f,camera.pitch+float(delta.y())*.45f,85.f);}
            else {const auto inverse=cameraViewMatrix(legacyCamera(camera)).inverted();const float span=camera.orthographic?camera.orthoScale:2*camera.distance*std::tan(camera.fieldOfView*pi/360);camera.target-=inverse.mapVector(QVector3D(delta.x(),-delta.y(),0))*span/qMax(1,height());}
            if(cameraChanged)cameraChanged(camera);
        }
        event->accept();
    }
    void mouseReleaseEvent(QMouseEvent *event) override {
        if(m_drag==None)return;
        const bool edit=m_drag==Move||m_drag==Rotate||m_drag==Limb;m_drag=None;
        if(edit){if(gestureFinished)gestureFinished(false);}else{m_navigation=false;finishBoundary();}
        event->accept();
    }
    void wheelEvent(QWheelEvent *event) override {
        if(m_drag!=None)return;m_navigation=true;m_refine=false;auto camera=m_scene.camera;
        const float multiplier=std::exp(-event->angleDelta().y()/900.f);
        if(camera.orthographic)camera.orthoScale=qBound(.01f,camera.orthoScale*multiplier,10000.f);
        else camera.distance=qBound(.02f,camera.distance*multiplier,10000.f);
        if(cameraChanged)cameraChanged(camera);event->accept();
    }
    void keyPressEvent(QKeyEvent *event) override {
        if(event->key()==Qt::Key_Escape&&m_drag!=None) {
            if((m_drag==Move||m_drag==Rotate||m_drag==Limb)&&gestureFinished)gestureFinished(true);
            else if(cameraChanged)cameraChanged(m_camera);
            m_drag=None;m_navigation=false;finishBoundary();event->accept();return;
        }
        QWidget::keyPressEvent(event);
    }
private:
    bool m_suppressed=false;
    bool interactive()const{return m_editing||m_navigation;}
    static bool sameLayout(const ReferenceInstance &old,const ReferenceInstance &next){
        if(!old.asset||!next.asset||old.asset->metadata.value("generator")!="toonpro-reference-starter-v1"
            ||next.asset->metadata.value("generator")!="toonpro-reference-starter-v1"
            ||old.asset->metadata.value("starterKind")!=next.asset->metadata.value("starterKind")
            ||old.asset->nodes.size()!=next.asset->nodes.size()||old.asset->skins.size()!=next.asset->skins.size())return false;
        for(int i=0;i<old.asset->nodes.size();++i){const auto &a=old.asset->nodes[i],&b=next.asset->nodes[i];if(a.name!=b.name||a.parentIndex!=b.parentIndex||a.meshIndex!=b.meshIndex||a.skinIndex!=b.skinIndex)return false;}
        for(int i=0;i<old.asset->skins.size();++i)if(old.asset->skins[i].joints!=next.asset->skins[i].joints)return false;
        return true;
    }
    void requestFrame(){
        if(m_suppressed)return;
        m_pending=true;++m_stats.requestedFrames;
        // A throttle starts once; input never postpones an already queued frame.
        if(!m_running&&!m_timer.isActive())m_timer.start();
    }
    void finishBoundary(){
        ++m_epoch;if(m_cancel)m_cancel->store(true);m_refine=true;m_idle.stop();requestFrame();
    }
    void dispatch() {
        if(m_running||!m_pending)return;
        m_running=true;m_pending=false;++m_stats.startedFrames;
        const auto generation=m_generation,epoch=m_epoch;const auto scene=m_scene;const bool full=!interactive()||m_refine;
        const QSize limit=full?QSize(1280,960):QSize(640,480);
        const QSize pixels=(width()>limit.width()||height()>limit.height()?size().scaled(limit,Qt::KeepAspectRatio):size()).expandedTo({1,1});
        auto cancel=std::make_shared<std::atomic_bool>(false);m_cancel=cancel;QPointer<ReferenceViewport> guard(this);
        const auto cache=m_cache;
        const auto gate=m_gate;
        QMetaObject::invokeMethod(m_worker,[guard,scene,pixels,cancel,generation,epoch,full,cache,gate]{
            ViewportFrame frame;QElapsedTimer elapsed;elapsed.start();
            try{if(!cancel->load())frame=renderReview(scene,pixels,cancel,*cache);}
            catch(const std::bad_alloc&){frame.error=QStringLiteral("Not enough memory to prepare the reference preview. Hide or remove some models; the source scene is preserved.");}
            catch(const std::exception&){frame.error=QStringLiteral("The reference preview could not be prepared. The source scene is preserved.");}
            catch(...){frame.error=QStringLiteral("The reference preview failed. The source scene is preserved.");}
            const double milliseconds=elapsed.nsecsElapsed()/1000000.;const auto evaluations=cache->evaluations,hits=cache->hits;
            QMutexLocker completionLock(&gate->mutex);
            if(gate->receiver)QMetaObject::invokeMethod(gate->receiver,[guard,frame=std::move(frame),generation,epoch,full,milliseconds,pixels,evaluations,hits]()mutable{
                if(!guard)return;guard->m_running=false;++guard->m_stats.completedFrames;guard->m_stats.geometryEvaluations=evaluations;guard->m_stats.cacheHits=hits;
                const bool current=generation==guard->m_generation;
                const bool live=guard->interactive()&&!guard->m_refine&&!full;
                // Intermediate frames may advance a live gesture, never a new
                // scene/topology or a completed/cancelled editing boundary.
                const bool publish=epoch==guard->m_epoch&&generation>=guard->m_presented&&(current||live);
                if(publish&&(!frame.error.isEmpty()||!frame.image.isNull())){
                    guard->m_error=frame.error;guard->m_frame=frame.error.isEmpty()?std::move(frame):ViewportFrame{};guard->m_presented=generation;
                    ++guard->m_stats.publishedFrames;guard->m_stats.publishedGeneration=generation;guard->m_stats.lastFrameMilliseconds=milliseconds;guard->m_stats.lastFrameSize=pixels;guard->m_stats.finalQuality=full;
                    if(!guard->m_error.isEmpty()&&guard->renderFailed)guard->renderFailed(guard->m_error);
                    if(guard->framePublished)guard->framePublished(generation,full);guard->update();
                }
                if(guard->m_pending&&!guard->m_timer.isActive())guard->m_timer.start();
            },Qt::QueuedConnection);
        },Qt::QueuedConnection);
    }
    ReferenceScene m_scene;ViewportFrame m_frame;QTimer m_timer,m_idle;bool m_running=false,m_pending=false,m_pose=false,m_ik=false,m_editing=false,m_navigation=false,m_refine=true;
    quint64 m_generation=0,m_presented=0,m_epoch=0;std::shared_ptr<std::atomic_bool> m_cancel;
    std::shared_ptr<ReviewRenderCache>m_cache;std::shared_ptr<ReviewCompletionGate>m_gate;QThread *m_thread=nullptr;QObject *m_worker=nullptr;Reference3DPreviewStats m_stats;
    int m_object=-1,m_node=-1,m_axis=2;QPointF m_last,m_start;ReferenceCamera m_camera;QString m_error;
    enum Drag{None,Orbit,Pan,Move,Rotate,Limb}m_drag=None;
};

class SceneChange final:public QUndoCommand {
public:
    SceneChange(QString label,ReferenceScene before,ReferenceScene after,std::function<void(const ReferenceScene&)> apply)
        :QUndoCommand(std::move(label)),m_before(std::move(before)),m_after(std::move(after)),m_apply(std::move(apply)){}
    void undo()override{m_apply(m_before);}void redo()override{if(m_first)m_first=false;else m_apply(m_after);}
private:
    ReferenceScene m_before,m_after;std::function<void(const ReferenceScene&)>m_apply;bool m_first=true;
};
class ReferencePropertyTabs final:public QTabWidget {
public:
    explicit ReferencePropertyTabs(int namesWidth=400,QWidget *parent=nullptr):QTabWidget(parent),m_namesWidth(namesWidth){}
protected:
    void resizeEvent(QResizeEvent *event)override{
        QTabWidget::resizeEvent(event);
        for(int i=0;i<count();++i){const auto title=tabBar()->tabData(i).toString();if(!title.isEmpty())setTabText(i,width()<m_namesWidth?QString():title);}
    }
private:int m_namesWidth;
};
class ReferenceGallery final:public QListWidget {
public:
    ReferenceGallery(){setViewMode(QListView::IconMode);setResizeMode(QListView::Adjust);setMovement(QListView::Static);setIconSize({80,86});setGridSize({100,116});setSpacing(3);setWordWrap(true);setUniformItemSizes(true);setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);connect(verticalScrollBar(),&QScrollBar::valueChanged,this,[this]{requestVisible();});}
    std::function<void()> visibleItemsChanged;
    void requestVisible(){if(isVisible()&&visibleItemsChanged)visibleItemsChanged();}
protected:
    void showEvent(QShowEvent *event)override{QListWidget::showEvent(event);QTimer::singleShot(0,this,[this]{requestVisible();});}
    void resizeEvent(QResizeEvent *event)override{QListWidget::resizeEvent(event);QTimer::singleShot(0,this,[this]{requestVisible();});}
};
class ReferenceModelCategories final:public QTabBar {
public:
    ReferenceModelCategories(){setProperty("compactDrawingTabs",true);setExpanding(true);setUsesScrollButtons(false);setIconSize({16,16});}
    QSize minimumSizeHint()const override{return {count()*28,qMax(28,fontMetrics().height()+8)};}
    QSize sizeHint()const override{return {qMax(220,count()*28),minimumSizeHint().height()};}
protected:
    QSize tabSizeHint(int tab)const override{auto size=QTabBar::tabSizeHint(tab);if(m_compact&&!tabIcon(tab).isNull())size.setWidth(32);size.setHeight(qMax(28,size.height()));return size;}
    QSize minimumTabSizeHint(int tab)const override{return tabSizeHint(tab);}
    void resizeEvent(QResizeEvent *event)override{adaptLabels();QTabBar::resizeEvent(event);}
    void changeEvent(QEvent *event)override{if(event->type()==QEvent::FontChange||event->type()==QEvent::StyleChange)adaptLabels();QTabBar::changeEvent(event);}
private:
    void adaptLabels(){
        int labelsWidth=0;for(int tab=0;tab<count();++tab)labelsWidth+=fontMetrics().horizontalAdvance(tabData(tab).toString())+(tabIcon(tab).isNull()?24:44);
        m_compact=width()<labelsWidth;
        // Decide intrinsic tab widths before Qt lays out the selected tab. An
        // old text width must not scroll the final icon outside a narrow bar.
        for(int tab=0;tab<count();++tab){const auto title=tabData(tab).toString();if(!title.isEmpty())setTabText(tab,!m_compact||tabIcon(tab).isNull()?title:QString());}
    }
    bool m_compact=true;
};
class ReferenceGroupLayout final:public QStackedLayout {
public:
    explicit ReferenceGroupLayout(QWidget *parent):QStackedLayout(parent){setSizeConstraint(QLayout::SetNoConstraint);}
    QSize minimumSize()const override{return currentWidget()?currentWidget()->minimumSizeHint().expandedTo(currentWidget()->minimumSize()):QSize();}
    QSize sizeHint()const override{return currentWidget()?currentWidget()->sizeHint():QSize();}
};
class ReferenceShapePages final:public QWidget {
public:
    ReferenceShapePages():m_layout(new ReferenceGroupLayout(this)){}
    void addPage(QWidget *page){m_layout->addWidget(page);}
    void setPage(int index){m_layout->setCurrentIndex(index);updateGeometry();}
private:ReferenceGroupLayout *m_layout;
};
class ReferenceBodyPad final:public QWidget {
public:
    ReferenceBodyPad(){setObjectName("referenceBodyBuildPad");setAccessibleName("Body build and definition");setToolTip("Drag horizontally for lean or full build and vertically for soft or defined anatomy. Arrow keys adjust either axis; Escape cancels the drag.");setFocusPolicy(Qt::StrongFocus);setMinimumHeight(142);}
    void setValue(float build,float definition){m_build=std::clamp(build,0.f,1.f);m_definition=std::clamp(definition,0.f,1.f);update();}
    void finishInteraction(bool cancel=false){if(!m_drag)return;m_drag=false;if(finished)finished(cancel);}
    std::function<void()>started;std::function<void(float,float)>changed;std::function<void(bool)>finished;
protected:
    bool event(QEvent *event)override{if(m_drag&&(event->type()==QEvent::UngrabMouse||event->type()==QEvent::WindowDeactivate||event->type()==QEvent::Hide))finishInteraction(true);return QWidget::event(event);}
    void paintEvent(QPaintEvent*)override{
        QPainter p(this);p.setRenderHint(QPainter::Antialiasing);const QRectF area=rect().adjusted(30,12,-12,-26);p.fillRect(area,QColor(ui::base));p.setPen(QPen(QColor(ui::border),1));p.drawRect(area);
        p.drawLine(QPointF(area.center().x(),area.top()),QPointF(area.center().x(),area.bottom()));p.drawLine(QPointF(area.left(),area.center().y()),QPointF(area.right(),area.center().y()));
        const qreal cx=area.center().x(),cy=area.center().y(),shoulder=15+12*m_build,hip=11+10*m_build;
        QPainterPath body;body.moveTo(cx-shoulder,cy-22);body.quadTo(cx,cy-30,cx+shoulder,cy-22);body.lineTo(cx+hip*.64,cy+5);body.lineTo(cx+hip,cy+27);body.quadTo(cx,cy+35,cx-hip,cy+27);body.lineTo(cx-hip*.64,cy+5);body.closeSubpath();
        p.setPen(QPen(QColor(ui::muted),1.5));p.setBrush(QColor(ui::raised));p.drawPath(body);p.drawEllipse(QPointF(cx,cy-37),7,9);
        if(m_definition>.25f){p.drawLine(QPointF(cx,cy-17),QPointF(cx,cy+14));p.drawLine(QPointF(cx-8,cy-6),QPointF(cx+8,cy-6));if(m_definition>.65f)p.drawLine(QPointF(cx-7,cy+5),QPointF(cx+7,cy+5));}
        const QPointF handle(area.left()+m_build*area.width(),area.bottom()-m_definition*area.height());p.setBrush(QColor(ui::focus));p.setPen(QPen(QColor(ui::canvas),2));p.drawEllipse(handle,6,6);
        p.setPen(QColor(ui::muted));p.drawText(QRectF(area.left(),area.bottom()+4,area.width(),20),Qt::AlignLeft,"Lean");p.drawText(QRectF(area.left(),area.bottom()+4,area.width(),20),Qt::AlignRight,"Full");
        p.save();p.translate(17,area.center().y());p.rotate(-90);p.drawText(QRectF(-area.height()/2,-12,area.height(),20),Qt::AlignCenter,"Soft   —   Defined");p.restore();
    }
    void mousePressEvent(QMouseEvent *event)override{if(event->button()!=Qt::LeftButton||!isEnabled())return;setFocus();m_drag=true;if(started)started();move(event->position());event->accept();}
    void mouseMoveEvent(QMouseEvent *event)override{if(m_drag){move(event->position());event->accept();}}
    void mouseReleaseEvent(QMouseEvent *event)override{if(!m_drag)return;finishInteraction();event->accept();}
    void keyPressEvent(QKeyEvent *event)override{
        if(event->key()==Qt::Key_Escape&&m_drag){finishInteraction(true);event->accept();return;}
        float x=m_build,y=m_definition;const float step=event->modifiers()&Qt::ShiftModifier?.1f:.02f;
        if(event->key()==Qt::Key_Left)x-=step;else if(event->key()==Qt::Key_Right)x+=step;else if(event->key()==Qt::Key_Up)y+=step;else if(event->key()==Qt::Key_Down)y-=step;else{QWidget::keyPressEvent(event);return;}
        if(started)started();if(changed)changed(std::clamp(x,0.f,1.f),std::clamp(y,0.f,1.f));if(finished)finished(false);event->accept();
    }
private:
    void move(QPointF point){const QRectF area=rect().adjusted(30,12,-12,-26);if(changed)changed(std::clamp(float((point.x()-area.left())/area.width()),0.f,1.f),std::clamp(float((area.bottom()-point.y())/area.height()),0.f,1.f));}
    float m_build=.5f,m_definition=.35f;bool m_drag=false;
};
class ReferenceThumbnailService;
struct ThumbnailCompletionGate{QMutex mutex;ReferenceThumbnailService *receiver=nullptr;};
struct ThumbnailWorkerState{ReviewRenderCache cache;ReferenceAssetPtr figure,hand;QHash<QString,ReferenceAssetPtr> pets;ThumbnailWorkerState(){cache.cacheLimit=8LL*1024*1024;}};
class ReferenceThumbnailService final:public QObject {
public:
    explicit ReferenceThumbnailService(QObject *parent):QObject(parent){m_gate=std::make_shared<ThumbnailCompletionGate>();m_gate->receiver=this;m_state=std::make_shared<ThumbnailWorkerState>();m_cancel=std::make_shared<std::atomic_bool>(false);m_thread=new QThread;m_worker=new QObject;m_worker->moveToThread(m_thread);connect(m_thread,&QThread::finished,m_worker,&QObject::deleteLater);connect(m_thread,&QThread::finished,m_thread,&QObject::deleteLater);m_thread->start(QThread::LowPriority);}
    ~ReferenceThumbnailService()override{{QMutexLocker lock(&m_gate->mutex);m_gate->receiver=nullptr;}m_cancel->store(true);m_thread->quit();}
    QString key(const QString &id,bool pose,const ReferenceAssetPtr &source={})const{
        QString result=(pose?"pose:":"model:")+id;if(!source)return result;
        auto entry=m_sourceIds.find(source.get());if(entry==m_sourceIds.end()||entry->asset.expired()){
            if(m_sourceIds.size()>256){for(auto old=m_sourceIds.begin();old!=m_sourceIds.end();){if(old->asset.expired())old=m_sourceIds.erase(old);else ++old;}}
            entry=m_sourceIds.insert(source.get(),{source,++m_sourceSerial});
        }
        return result+"@"+source->id+"/"+QString::number(entry->serial);
    }
    QImage image(const QString &id,bool pose,const ReferenceAssetPtr &source={})const{return m_images.value(key(id,pose,source));}
    ReferenceAssetPtr asset(const QString &id)const{return m_assets.value(id);}
    void request(const QString &id,bool pose,const ReferenceAssetPtr &source={}){const auto name=key(id,pose,source);if(m_images.contains(name)){m_imageOrder.removeAll(name);m_imageOrder.append(name);return;}if(m_failed.contains(name)||m_active==name||m_queued.contains(name))return;m_queue.append({id,pose,source});m_queued.insert(name);if(!m_running)QTimer::singleShot(0,this,[this]{dispatch();});}
    void setPoseSource(const ReferenceAssetPtr &source){for(int job=m_queue.size()-1;job>=0;--job)if(m_queue[job].pose&&m_queue[job].source!=source){m_queued.remove(key(m_queue[job].id,true,m_queue[job].source));m_queue.removeAt(job);}}
    void prioritizeModel(const QString &id){const auto name=key(id,false);m_failed.remove(name);for(int job=m_queue.size()-1;job>=0;--job)if(m_queue[job].id==id&&!m_queue[job].pose)m_queue.removeAt(job);if(m_active!=name){m_queue.prepend({id,false,{}});m_queued.insert(name);}if(!m_running)QTimer::singleShot(0,this,[this]{dispatch();});}
    std::function<void(const QString&,bool,const QImage&,const QString&,ReferenceAssetPtr)>ready;
private:
    struct Job{QString id;bool pose=false;ReferenceAssetPtr source;};
    struct SourceIdentity{std::weak_ptr<const ReferenceAsset> asset;quint64 serial;};
    void dispatch(){
        if(m_running||m_queue.isEmpty()||m_cancel->load())return;const auto job=m_queue.takeFirst();const auto jobKey=key(job.id,job.pose,job.source);m_active=jobKey;m_queued.remove(m_active);m_running=true;
        const auto gate=m_gate;const auto state=m_state;const auto cancel=m_cancel;
        QMetaObject::invokeMethod(m_worker,[gate,state,cancel,job,jobKey]{
            QImage image;ReferenceAssetPtr asset;QString error;
            try{
                ReferenceInstance instance;instance.id="thumbnail";instance.name="Preset preview";
                if(job.pose&&job.source){instance.asset=job.source;if(!applyReferencePosePreset(job.id,instance,&error))instance.asset.reset();}
                else if(job.pose&&job.id.startsWith("pet/")){const auto model=job.id.section('/',0,1);auto &base=state->pets[model];if(!base)base=createReferenceCatalogAsset(model,&error);instance.asset=base;if(instance.asset&&!applyReferencePetPosePreset(job.id,instance,&error))instance.asset.reset();}
                else if(job.pose){bool hands=false;for(const auto &preset:referencePosePresets())if(preset.id==job.id){hands=preset.handsOnly;break;}
                    auto &base=hands?state->hand:state->figure;if(!base)base=createReferenceStarter(hands?ReferenceStarterKind::Hand:ReferenceStarterKind::Figure,&error);instance.asset=base;
                    if(instance.asset&&!applyReferencePosePreset(job.id,instance,&error))instance.asset.reset();
                }else{asset=createReferenceCatalogAsset(job.id,&error);instance.asset=asset;}
                if(instance.asset&&!cancel->load()){
                    instance.name=instance.asset->name;ReferenceScene scene;scene.instances.append(instance);state->cache.retainScene(scene);const auto geometry=state->cache.geometry(instance,&error);
                    if(geometry&&geometry->geometry.bounds.valid){auto bounds=geometry->geometry.bounds;
                        if(job.source&&job.id.startsWith("hands/")){ReferenceBounds hand;const auto side=job.source->humanoidBones.contains("leftHand")?QString("left"):QString("right");for(auto bone=job.source->humanoidBones.begin();bone!=job.source->humanoidBones.end();++bone)if(bone.key().startsWith(side)&&(bone.key()==side+"Hand"||bone.key().contains("Thumb")||bone.key().contains("Index")||bone.key().contains("Middle")||bone.key().contains("Ring")||bone.key().contains("Little")))hand.include(geometry->geometry.nodeGlobals[bone.value()].map(QVector3D()));if(hand.valid){const float padding=qMax(.01f,(hand.maximum-hand.minimum).length()*.16f);bounds.minimum=hand.minimum-QVector3D(padding,padding,padding);bounds.maximum=hand.maximum+QVector3D(padding,padding,padding);}}
                        const float radius=qMax(.001f,(bounds.maximum-bounds.minimum).length()*.5f);scene.camera.target=bounds.center();scene.camera.yaw=22;scene.camera.pitch=12;scene.camera.distance=radius*3;scene.camera.orthographic=true;scene.camera.orthoScale=radius*2.2f;scene.light.ambientIntensity=.35f;scene.light.intensity=.9f;image=renderReview(scene,{176,176},cancel,state->cache,false).image;}
                }
            }catch(const std::exception&){error="The preset thumbnail could not be prepared.";}catch(...){error="The preset thumbnail could not be prepared.";}
            QMutexLocker lock(&gate->mutex);if(gate->receiver)QMetaObject::invokeMethod(gate->receiver,[gate,job,jobKey,image,asset,error]{
                auto *self=gate->receiver;if(!self)return;self->m_running=false;self->m_active.clear();if(image.isNull()){if(self->m_failed.size()>256)self->m_failed.clear();self->m_failed.insert(jobKey);}else{self->m_images.insert(jobKey,image);self->m_imageOrder.removeAll(jobKey);self->m_imageOrder.append(jobKey);while(self->m_imageOrder.size()>128)self->m_images.remove(self->m_imageOrder.takeFirst());}
                if(asset){self->m_assets.insert(job.id,asset);self->m_assetOrder.removeAll(job.id);self->m_assetOrder.append(job.id);while(self->m_assetOrder.size()>4)self->m_assets.remove(self->m_assetOrder.takeFirst());}
                if(self->ready)self->ready(job.id,job.pose,image,error,job.source);self->dispatch();
            },Qt::QueuedConnection);
        },Qt::QueuedConnection);
    }
    std::shared_ptr<ThumbnailCompletionGate>m_gate;std::shared_ptr<ThumbnailWorkerState>m_state;std::shared_ptr<std::atomic_bool>m_cancel;
    QThread *m_thread=nullptr;QObject *m_worker=nullptr;QVector<Job>m_queue;QSet<QString>m_queued,m_failed;QHash<QString,QImage>m_images;QHash<QString,ReferenceAssetPtr>m_assets;QStringList m_assetOrder,m_imageOrder;QString m_active;bool m_running=false;
    mutable QHash<const ReferenceAsset*,SourceIdentity>m_sourceIds;mutable quint64 m_sourceSerial=0;
};
QVector<float> weightsFor(const ReferenceInstance &instance,int node) {
    if(!instance.asset||node<0||node>=instance.asset->nodes.size())return {};
    const auto &source=instance.asset->nodes[node];
    if(source.meshIndex<0||source.meshIndex>=instance.asset->meshes.size())return {};
    if(instance.morphOverrides.contains(node))return instance.morphOverrides.value(node);
    if(!source.morphWeights.isEmpty())return source.morphWeights;
    const auto &mesh=instance.asset->meshes[source.meshIndex];
    auto weights=mesh.defaultMorphWeights;
    const int count=mesh.primitives.isEmpty()?0:mesh.primitives.first().morphTargets.size();weights.resize(count);return weights;
}
} // namespace
QString reference3DPreviewBudgetError(const ReferenceScene &scene){return previewBudgetError(scene);}

struct Reference3DReviewWindow::State {
    Reference3DReviewWindow *window;
    ReferenceScene scene;QUndoStack *undo;ReferenceViewport *viewport;
    QPointer<QUndoStack> externalUndo;bool canvasPanel=false;QSize canvasFrameSize;
    QUndoStack *activeUndo()const{return externalUndo?externalUndo.data():undo;}
    QListWidget *objects,*starters,*materials;QTreeWidget *joints;QLineEdit *name,*materialSearch;
    ReferenceThumbnailService *thumbnails;ReferenceGallery *modelGallery,*poseGallery;QTabWidget *browserTabs;
    QTabBar *modelKinds,*poseKinds,*poseCategories,*poseHands;QLineEdit *modelSearch,*poseSearch;QToolButton *placeModel,*applyPose,*mirrorPose;QLabel *poseTarget;
    QVector<ReferenceCatalogEntry>modelCatalog;QVector<ReferencePosePreset>poseCatalog;ReferenceAssetPtr petPoseAsset,poseThumbnailSource;
    QString pendingCatalogId;QPointer<QProgressDialog> catalogProgress;
    ReferenceMaterialLibrary *library;QComboBox *materialKind;QToolButton *materialFavorite;
    QTabWidget *tabs;QWidget *posePage,*shapePage,*objectPage;
    QVBoxLayout *morphLayout,*faceLayout;QLabel *capabilities,*poseStatus;QToolButton *visible,*locked,*axisButtons[3],*importNotes;
    ValueSlider *position[3],*rotation[3],*scale[3],*poseRotation[3],*fov;
    QTabBar *poseModes;QWidget *fkGroup,*ikGroup,*handGroup;ValueSlider *ikTargetFields[3],*ikPoleFields[3];
    QTabBar *handSides,*handFingers;ValueSlider *handCurl,*handSplay;QLabel *handStatus;QToolButton *resetPose,*resetHands;
    QString handEditFinger;ReferenceHandTarget handEditSide=ReferenceHandTarget::Both;bool handEditing=false;
    QVector3D ikTarget,ikPole;QString ikObject;int ikNode=-1;
    QCheckBox *orthographic;QToolButton *undoButton,*redoButton,*duplicate,*remove;
    QVector<ValueSlider*>morphFields;QVector<QPair<int,int>>morphBindings;
    QVector<ValueSlider*>bodyFields;QVector<QString>bodyBindings;
    QVector<ValueSlider*>expressionFields;QStringList expressionBindings;QString controlsKey;
    struct FamilyControls{ReferenceStarterKind family;QCheckBox *limit;QVector<ValueSlider*>fields;QStringList ids;};QVector<FamilyControls>familyControls;
    ReferenceBodyPad *buildPad=nullptr;QTabBar *handedness=nullptr;float buildMinimum=.65f,buildMaximum=1.5f,definitionMinimum=0,definitionMaximum=1;
    std::optional<ReferenceScene> gesture;QString gestureLabel;bool gestureChanged=false,gesturePreserveCamera=true,refreshing=false;
    int object=-1,node=-1,axis=2;QString path;std::shared_ptr<std::atomic_bool>importCancel;bool importing=false;

    ReferenceInstance *selected(){return object>=0&&object<scene.instances.size()?&scene.instances[object]:nullptr;}
    const ReferenceInstance *selected()const{return object>=0&&object<scene.instances.size()?&scene.instances[object]:nullptr;}
    QString selectedControlsKey()const{
        const auto *instance=selected();
        return instance&&instance->asset?instance->id+"/"+(instance->asset->metadata.value("generator").toString()=="toonpro-reference-starter-v1"?QString::number(instance->asset->metadata.value("starterKind").toInt()):instance->asset->id):QString("none");
    }
    std::optional<std::array<int,3>>limb()const{
        const auto *instance=selected();if(!instance||!instance->asset)return {};
        const auto &bones=instance->asset->humanoidBones;
        for(const auto &side:QStringList{"left","right"})for(bool leg:{false,true}){
            const int end=bones.value(side+(leg?"Foot":"Hand"),-1);if(end!=node)continue;
            const int upper=bones.value(side+(leg?"UpperLeg":"UpperArm"),-1),middle=bones.value(side+(leg?"LowerLeg":"LowerArm"),-1);
            if(upper>=0&&middle>=0&&end>=0)return std::array<int,3>{upper,middle,end};
        }
        return {};
    }
    void synchronizeIK(){
        const auto *instance=selected();if(!instance||!limb())return;
        if(ikObject==instance->id&&ikNode==node)return;
        const auto globals=evaluateReferenceNodeGlobals(*instance);if(!globals)return;
        const auto chain=*limb();ikTarget=(*globals)[chain[2]].map(QVector3D());ikPole=(*globals)[chain[1]].map(QVector3D());ikObject=instance->id;ikNode=node;
    }
    void status(const QString &message){window->statusBar()->showMessage(message);emit window->statusChanged(message);}
    void requestVisibleThumbnails(ReferenceGallery *gallery,bool pose){
        if(!gallery->isVisible())return;const QRect visible=gallery->viewport()->rect().adjusted(0,-120,0,120);
        for(int i=0;i<gallery->count();++i)if(gallery->visualItemRect(gallery->item(i)).intersects(visible))thumbnails->request(gallery->item(i)->data(Qt::UserRole).toString(),pose,pose?poseThumbnailSource:ReferenceAssetPtr());
    }
    void updateThumbnail(const QString &id,bool pose,const QImage &image,const QString &error,const ReferenceAssetPtr &source){
        if(pose&&source!=poseThumbnailSource)return;
        auto *gallery=pose?poseGallery:modelGallery;
        for(int i=0;i<gallery->count();++i){auto *item=gallery->item(i);if(item->data(Qt::UserRole).toString()!=id)continue;if(!image.isNull())item->setIcon(QIcon(QPixmap::fromImage(image)));else if(!error.isEmpty())item->setToolTip(item->toolTip()+"\n"+error);}
        if(!pose&&!image.isNull())for(auto *button:window->findChildren<QToolButton*>())if(button->property("referencePresetThumbnail").toString()==id)button->setIcon(QIcon(QPixmap::fromImage(image)));
        if(!pose&&pendingCatalogId==id){const auto asset=thumbnails->asset(id);cancelCatalogPlacement();if(!asset){status(error.isEmpty()?"The catalog model could not be prepared.":error);return;}QString placementError;if(!placeFinalized(asset,&placementError))status(placementError);else status("Reference model placed");}
    }
    void cancelCatalogPlacement(){pendingCatalogId.clear();if(catalogProgress){catalogProgress->blockSignals(true);catalogProgress->close();catalogProgress->deleteLater();catalogProgress.clear();}placeModel->setEnabled(modelGallery->count()>0);}
    void prepareCatalogPlacement(const QString &id){
        if(importing){status("Wait for the character import to finish");return;}resolveEditors();cancelCatalogPlacement();
        if(const auto asset=thumbnails->asset(id)){QString error;if(!placeFinalized(asset,&error))status(error);return;}
        pendingCatalogId=id;placeModel->setEnabled(false);catalogProgress=new QProgressDialog("Preparing reference model…","Cancel",0,0,window);catalogProgress->setWindowModality(Qt::WindowModal);catalogProgress->setMinimumDuration(0);catalogProgress->setObjectName("referenceCatalogProgress");QObject::connect(catalogProgress,&QProgressDialog::canceled,window,[this]{cancelCatalogPlacement();status("Model placement cancelled");});catalogProgress->show();thumbnails->prioritizeModel(id);
    }
    void refreshModels(){
        const QString selectedId=modelGallery->currentItem()?modelGallery->currentItem()->data(Qt::UserRole).toString():QString();const auto query=modelSearch->text().trimmed();const auto kind=ReferenceCatalogKind(modelKinds->currentIndex());
        QSignalBlocker block(modelGallery);modelGallery->clear();
        for(const auto &entry:modelCatalog){if(entry.kind!=kind||(!query.isEmpty()&&!entry.name.contains(query,Qt::CaseInsensitive)&&!entry.description.contains(query,Qt::CaseInsensitive)&&!entry.id.contains(query,Qt::CaseInsensitive)))continue;
            const auto image=thumbnails->image(entry.id,false);const QString icon=kind==ReferenceCatalogKind::Body?"toon-character":kind==ReferenceCatalogKind::Head?"circle":kind==ReferenceCatalogKind::Hand?"hand":kind==ReferenceCatalogKind::Anime?"sparkles":"toon-pet";auto *item=new QListWidgetItem(image.isNull()?appIcon(icon):QIcon(QPixmap::fromImage(image)),entry.name,modelGallery);
            item->setData(Qt::UserRole,entry.id);item->setToolTip(entry.name+"\n"+entry.description+(entry.sourceUrl.isEmpty()?QString():"\n"+entry.sourceUrl)+"\nDouble-click to place");item->setData(Qt::AccessibleTextRole,entry.name);if(entry.id==selectedId)modelGallery->setCurrentItem(item);
        }
        if(modelGallery->count()&&!modelGallery->currentItem())modelGallery->setCurrentRow(0);placeModel->setEnabled(modelGallery->count()>0&&pendingCatalogId.isEmpty());QTimer::singleShot(0,window,[this]{requestVisibleThumbnails(modelGallery,false);});
    }
    QVector<ReferencePosePreset> availablePoses()const{
        QVector<ReferencePosePreset> result;
        if(poseKinds->currentIndex()==2){if(const auto *instance=selected())for(const auto &pose:referencePetPosePresets(*instance)){auto category=pose.kind;if(!category.isEmpty())category[0]=category[0].toUpper();result.append({pose.id,pose.name,category,false});}}
        else for(const auto &pose:poseCatalog)if(pose.handsOnly==(poseKinds->currentIndex()==1))result.append(pose);
        return result;
    }
    void refreshPoseCategories(){
        const QString selected=poseCategories->currentIndex()>=0?poseCategories->tabData(poseCategories->currentIndex()).toString():QString();QSignalBlocker block(poseCategories);while(poseCategories->count())poseCategories->removeTab(0);poseCategories->addTab("All");poseCategories->setTabData(0,QString());
        poseCategories->setTabToolTip(0,"All available poses");poseCategories->setAccessibleTabName(0,"All available poses");QStringList categories;for(const auto &preset:availablePoses())if(!categories.contains(preset.category))categories.append(preset.category);
        for(const auto &category:categories){const int tab=poseCategories->addTab(category);poseCategories->setTabData(tab,category);poseCategories->setTabToolTip(tab,category+" poses");poseCategories->setAccessibleTabName(tab,category+" poses");if(category==selected)poseCategories->setCurrentIndex(tab);}
        refreshPoses();
    }
    void refreshPoses(){
        const QString selectedId=poseGallery->currentItem()?poseGallery->currentItem()->data(Qt::UserRole).toString():QString();const bool hands=poseKinds->currentIndex()==1,pets=poseKinds->currentIndex()==2;const auto category=poseCategories->tabData(poseCategories->currentIndex()).toString(),query=poseSearch->text().trimmed();
        QSignalBlocker block(poseGallery);poseGallery->clear();
        for(const auto &preset:availablePoses()){if((!category.isEmpty()&&preset.category!=category)||(!query.isEmpty()&&!preset.name.contains(query,Qt::CaseInsensitive)&&!preset.category.contains(query,Qt::CaseInsensitive)&&!preset.id.contains(query,Qt::CaseInsensitive)))continue;
            const auto image=thumbnails->image(preset.id,true,poseThumbnailSource);auto *item=new QListWidgetItem(image.isNull()?appIcon(pets?"toon-pet":hands?"hand":"toon-symbol-pose"):QIcon(QPixmap::fromImage(image)),preset.name,poseGallery);item->setData(Qt::UserRole,preset.id);item->setToolTip(preset.name+" · "+preset.category+"\nDouble-click or press Enter to apply");item->setData(Qt::AccessibleTextRole,preset.name);if(preset.id==selectedId)poseGallery->setCurrentItem(item);
        }
        if(poseGallery->count()&&!poseGallery->currentItem())poseGallery->setCurrentRow(0);poseHands->setVisible(hands);mirrorPose->setVisible(!pets);refreshPoseActions();QTimer::singleShot(0,window,[this]{requestVisibleThumbnails(poseGallery,true);});
    }
    void refreshPoseActions(){
        const auto *instance=selected();const bool hands=poseKinds->currentIndex()==1,pets=poseKinds->currentIndex()==2;bool supported=false;
        if(instance&&instance->asset){const auto &bones=instance->asset->humanoidBones;const bool left=bones.contains("leftHand"),right=bones.contains("rightHand");supported=pets?!referencePetPosePresets(*instance).isEmpty():hands?(left||right):bones.contains("hips");QSignalBlocker block(poseHands);poseHands->setTabEnabled(0,left||right);poseHands->setTabEnabled(1,left);poseHands->setTabEnabled(2,right);if(!poseHands->isTabEnabled(poseHands->currentIndex()))poseHands->setCurrentIndex(0);}
        applyPose->setEnabled(instance&&!instance->locked&&supported&&poseGallery->currentItem());mirrorPose->setEnabled(!pets&&(!hands||poseHands->currentIndex()!=0));if(pets||(hands&&poseHands->currentIndex()==0)){QSignalBlocker block(mirrorPose);mirrorPose->setChecked(false);}
        poseTarget->setText(!instance?(pets?"Select a dog, cat or bird":hands?"Select a model with hands":"Select a figure"):instance->locked?"Unlock the selected model":!supported?(pets?"This model has no native pet poses":hands?"This model has no mapped hands":"This model has no mapped body"):instance->name);
        poseTarget->setToolTip(poseTarget->text());
    }
    void resolveEditors(){
        if(buildPad)buildPad->finishInteraction();
        const auto fields=window->findChildren<ValueSlider*>();for(auto *field:fields)if(field->editingActive())field->commitEditing();end();
        auto *instance=selected();const auto draft=name->text().trimmed();
        if(instance&&!instance->locked&&name->isModified()&&!draft.isEmpty()&&draft!=instance->name)edit("Rename reference",[&]{instance->name=draft;return true;});
        name->setModified(false);
    }
    bool confirmReplacement(){
        resolveEditors();if(!window->isWindowModified())return true;
        const auto choice=QMessageBox::question(window,"Unsaved reference scene","Save this reference scene before continuing?",QMessageBox::Save|QMessageBox::Discard|QMessageBox::Cancel,QMessageBox::Save);
        if(choice!=QMessageBox::Save)return choice==QMessageBox::Discard;
        auto destination=path;if(destination.isEmpty())destination=QFileDialog::getSaveFileName(window,"Save reference scene",{},"Reference scenes (*.toon3d)");
        QString error;if(destination.isEmpty()||!window->saveScene(destination,&error)){if(!error.isEmpty())status(error);return false;}return true;
    }
    void refreshMaterials(){
        const QString selectedId=materials->currentItem()?materials->currentItem()->data(Qt::UserRole).toString():QString();
        const QString query=materialSearch->text().trimmed();const int kind=materialKind->currentData().toInt();
        QSignalBlocker block(materials);materials->clear();
        for(const auto &entry:library->entries()){
            if(kind>=0&&int(entry.kind)!=kind)continue;
            if(materialFavorite->isChecked()&&!entry.favorite)continue;
            if(!query.isEmpty()&&!entry.name.contains(query,Qt::CaseInsensitive)&&!entry.tags.join(' ').contains(query,Qt::CaseInsensitive))continue;
            auto *item=new QListWidgetItem(entry.thumbnail.isNull()?appIcon("box"):QIcon(QPixmap::fromImage(entry.thumbnail)),entry.name,materials);
            item->setData(Qt::UserRole,entry.id);item->setToolTip(entry.name+" · "+referenceLibraryKindName(entry.kind));item->setData(Qt::AccessibleTextRole,item->toolTip());item->setSizeHint({0,36});if(entry.id==selectedId)materials->setCurrentItem(item);
        }
        if(materials->count()&&!materials->currentItem())materials->setCurrentRow(0);
    }
    void begin(const QString &label,bool preserveCamera=true){
        if(gesture)return;gesture=scene;
        // selected() may already have returned a pointer into the live QVector.
        // Detach the snapshot, not the live vector, before such a pointer writes.
        // Otherwise Qt's implicit sharing would let the edit mutate Undo's origin.
        gesture->instances.detach();gestureLabel=label;gestureChanged=false;gesturePreserveCamera=preserveCamera;viewport->setInteractionActive(true);emit window->editBegan();
    }
    void end(bool cancel=false){
        if(!gesture)return;
        auto before=std::move(*gesture);gesture.reset();viewport->setInteractionActive(false);
        if(cancel){scene=std::move(before);refresh();emit window->editEnded(true);return;}
        if(gestureChanged){undo->push(new SceneChange(gestureLabel,std::move(before),scene,[this,preserveCamera=gesturePreserveCamera](const ReferenceScene &value){const auto camera=scene.camera;scene=value;if(preserveCamera)scene.camera=camera;refresh();window->setWindowModified(!undo->isClean());emit window->sceneChanged();}));window->setWindowModified(true);emit window->sceneChanged();}
        refresh();emit window->editEnded(false);
    }
    void changed(){gestureChanged=true;viewport->setScene(scene,gestureLabel=="Change reference proportions");refreshValues();emit window->editPreviewed();}
    template<class Edit> bool edit(const QString &label,Edit callback,bool preserveCamera=true){
        const bool own=!gesture;if(own)begin(label,preserveCamera);
        const bool did=callback();if(did)changed();if(own)end();return did;
    }
    void connectScalar(ValueSlider *field,const QString &label,std::function<void(double)>change){
        QObject::connect(field,&ValueSlider::editingStarted,window,[this,label]{begin(label);});
        QObject::connect(field,&ValueSlider::valueChanged,window,[this,change](double value){if(!refreshing)change(value);});
        QObject::connect(field,&ValueSlider::editingFinished,window,[this]{end();});
        QObject::connect(field,&ValueSlider::editingCancelled,window,[this]{end(true);});
    }
    ValueSlider *row(QVBoxLayout *layout,const QString &label,const QString &id,double low,double high,int decimals=2){
        auto *value=new ValueSlider(label,low,high,0,decimals);value->setObjectName(id);value->setAccessibleName(label);value->setToolTip(label);
        layout->addWidget(new PropertyRow(label,value));return value;
    }
    void addFamilyMixer(QVBoxLayout *layout,ReferenceStarterKind family){
        const auto *instance=selected();if(!instance)return;const auto values=referenceShapeFamilyMix(*instance,family);if(values.isEmpty())return;
        const QString title=family==ReferenceStarterKind::Head?"Head mix":"Hand mix",prefix=family==ReferenceStarterKind::Head?"referenceHeadMix":"referenceHandMix";
        auto *header=new QHBoxLayout;header->addWidget(new QLabel(title),1);auto *limit=new QCheckBox("Limit");limit->setObjectName(prefix+"Limit");limit->setToolTip("Normalize combined preset weights above 100%; lower totals retain the base form");limit->setAccessibleName("Limit "+title.toLower());limit->setChecked(referenceShapeFamilyMixLimited(*instance,family));header->addWidget(limit);layout->addLayout(header);
        FamilyControls controls;controls.family=family;controls.limit=limit;
        for(const auto &value:values){
            auto *field=new ValueSlider(value.name,0,100,value.weight*100,1);const QString safe=QString(value.id).replace('/','_');field->setObjectName(prefix+"_"+safe);field->setProperty("referenceMixId",value.id);field->setProperty("referenceMixFamily",int(family));field->setAccessibleName(value.name+" mix percentage");field->setToolTip(value.name+" mix weight (%)");auto *property=new PropertyRow(value.name,field);
            const QString preset=value.id.contains('/')?value.id:(family==ReferenceStarterKind::Head?"head/":"hand/")+value.id;
            auto *pure=actionButton("referenceMixPreset",family==ReferenceStarterKind::Head?"circle":"hand","Use only "+value.name+" in the "+title.toLower());pure->setProperty("referencePresetThumbnail",preset);const auto image=thumbnails->image(preset,false);if(!image.isNull())pure->setIcon(QIcon(QPixmap::fromImage(image)));property->setAccessory(pure);layout->addWidget(property);controls.fields.append(field);controls.ids.append(value.id);
            connectScalar(field,"Change reference proportions",[this,family,id=value.id](double amount){auto *target=selected();if(!target||target->locked)return;QHash<QString,float> weights;for(const auto &entry:referenceShapeFamilyMix(*target,family))weights[entry.id]=entry.weight;if(qFuzzyCompare(weights.value(id),float(amount/100)))return;weights[id]=float(amount/100);edit("Change reference proportions",[&]{QString error;if(!setReferenceShapeFamilyMix(*target,family,weights,referenceShapeFamilyMixLimited(*target,family),&error)){status(error);return false;}return true;});});
            QObject::connect(pure,&QToolButton::clicked,window,[this,family,id=value.id]{resolveEditors();auto *target=selected();if(!target||target->locked)return;QHash<QString,float> weights;for(const auto &entry:referenceShapeFamilyMix(*target,family))weights[entry.id]=entry.id==id?1.f:0.f;edit("Change reference proportions",[&]{QString error;if(!setReferenceShapeFamilyMix(*target,family,weights,referenceShapeFamilyMixLimited(*target,family),&error)){status(error);return false;}return true;});});
        }
        QObject::connect(limit,&QCheckBox::toggled,window,[this,family](bool enabled){if(refreshing)return;resolveEditors();auto *target=selected();if(!target||target->locked)return;QHash<QString,float> weights;for(const auto &entry:referenceShapeFamilyMix(*target,family))weights[entry.id]=entry.weight;edit("Change reference proportions",[&]{QString error;if(!setReferenceShapeFamilyMix(*target,family,weights,enabled,&error)){status(error);return false;}return true;});});
        familyControls.append(std::move(controls));
    }
    void refreshHandValues(bool editable){
        const auto *instance=selected();const bool left=instance&&instance->asset->humanoidBones.contains("leftHand"),right=instance&&instance->asset->humanoidBones.contains("rightHand");
        {QSignalBlocker block(handSides);handSides->setTabEnabled(0,left||right);handSides->setTabEnabled(1,left);handSides->setTabEnabled(2,right);if(!handSides->isTabEnabled(handSides->currentIndex()))handSides->setCurrentIndex(0);}
        QString error;const auto controls=instance?referenceHandControls(*instance,ReferenceHandTarget(handSides->currentIndex()),&error):QVector<ReferenceHandControl>();
        handCurl->setEnabled(editable&&!controls.isEmpty());handSplay->setEnabled(editable&&!controls.isEmpty());resetHands->setEnabled(editable&&!controls.isEmpty());float curl=0,splay=0;bool approximate=false;int count=0;const auto id=handFingers->tabData(handFingers->currentIndex()).toString();
        for(const auto &control:controls)if(id=="all"||id==control.id){curl+=control.curl;splay+=control.splay;approximate|=control.approximate;++count;}
        if(count){curl/=count;splay/=count;if(id=="all")for(const auto &control:controls)if(std::abs(control.curl-curl)>.005f||std::abs(control.splay-splay)>.005f)approximate=true;}
        if(!handCurl->editingActive())handCurl->setValueSilently(curl*100);if(!handSplay->editingActive())handSplay->setValueSilently(splay*100);
        handStatus->setText(instance&&instance->locked?"Unlock the selected model":controls.isEmpty()?"No complete mapped hand":approximate?"Custom or mixed finger pose":"Finger curl and spread");handStatus->setToolTip(error.isEmpty()?"Values are fitted from the actual finger joints. Editing replaces the selected fingers while retaining the wrist and body pose.":error);
    }
    void changeHandValue(bool curl,double amount){
        auto *instance=selected();if(!instance||instance->locked)return;const auto side=handEditing?handEditSide:ReferenceHandTarget(handSides->currentIndex());const auto id=handEditing?handEditFinger:handFingers->tabData(handFingers->currentIndex()).toString();QString error;
        auto work=*instance;QVector<ReferenceHandTarget> sides;if(side==ReferenceHandTarget::Both){if(instance->asset->humanoidBones.contains("leftHand"))sides.append(ReferenceHandTarget::Left);if(instance->asset->humanoidBones.contains("rightHand"))sides.append(ReferenceHandTarget::Right);}else sides.append(side);
        if(sides.isEmpty())return;
        // Both-hand values are a display average. Preserve the unedited channel
        // independently on each hand, then publish both changes atomically.
        for(const auto target:sides){const auto controls=referenceHandControls(work,target,&error);if(controls.isEmpty()){status(error);return;}QHash<QString,QVector2D> values;for(const auto &control:controls)if(id=="all"||id==control.id)values.insert(control.id,{curl?float(amount/100):control.curl,curl?control.splay:float(amount/100)});if(!setReferenceHandControls(work,values,target,&error)){status(error);return;}}
        edit("Pose fingers",[&]{if(instance->poseRotations==work.poseRotations&&instance->poseTranslations==work.poseTranslations&&instance->poseScales==work.poseScales)return false;*instance=std::move(work);return true;});
    }
    void requestStyleThumbnails(){for(auto *button:window->findChildren<QToolButton*>()){const auto id=button->property("referencePresetThumbnail").toString();if(!id.isEmpty()&&button->isVisible())thumbnails->request(id,false);}}
    void refreshValues(){
        if(refreshing)return;
        // Placement/removal can select a different rig before edit() commits.
        // Rebuild its controls before reading bindings from the preceding model.
        // Authored proportion drags retain their stable family key.
        if(selectedControlsKey()!=controlsKey){refresh();return;}
        refreshing=true;const auto *instance=selected();const bool editable=instance&&!instance->locked;
        const QVector3D euler=instance?instance->rotation.toEulerAngles():QVector3D();
        for(int a=0;a<3;++a){position[a]->setEnabled(editable);rotation[a]->setEnabled(editable);scale[a]->setEnabled(editable);
            if(!position[a]->editingActive())position[a]->setValueSilently(instance?instance->translation[a]:0);
            if(!rotation[a]->editingActive())rotation[a]->setValueSilently(euler[a]);
            if(!scale[a]->editingActive())scale[a]->setValueSilently(instance?instance->scale[a]:1);
        }
        const bool joint=editable&&node>=0&&node<instance->asset->nodes.size();
        const bool ikMode=poseModes->currentIndex()==1,handMode=poseModes->currentIndex()==2;fkGroup->setVisible(!ikMode&&!handMode);ikGroup->setVisible(ikMode);handGroup->setVisible(handMode);joints->setVisible(!handMode);resetPose->setVisible(!handMode);
        QVector3D angles;
        if(joint){const auto &rest=instance->asset->nodes[node].rotation;angles=(rest.conjugated()*instance->poseRotations.value(node,rest)).toEulerAngles();}
        for(int a=0;a<3;++a){poseRotation[a]->setEnabled(joint);if(!poseRotation[a]->editingActive())poseRotation[a]->setValueSilently(angles[a]);}
        poseStatus->setText(joint?instance->asset->nodes[node].name:instance&&instance->locked?"Unlock the model to pose":"Select a joint on the model");
        if(ikMode){synchronizeIK();const bool available=editable&&limb().has_value();poseStatus->setText(available?instance->asset->nodes[node].name+" · limb IK":"Select a hand or foot for limb IK");for(int a=0;a<3;++a){ikTargetFields[a]->setEnabled(available);ikPoleFields[a]->setEnabled(available);if(!ikTargetFields[a]->editingActive())ikTargetFields[a]->setValueSilently(ikTarget[a]);if(!ikPoleFields[a]->editingActive())ikPoleFields[a]->setValueSilently(ikPole[a]);}}
        if(handMode&&tabs->currentWidget()==posePage){refreshHandValues(editable);poseStatus->setText(instance?instance->name:"Select a model with hands");}
        name->setEnabled(editable);if(!name->hasFocus()&&!name->isModified())name->setText(instance?instance->name:QString());
        {QSignalBlocker a(visible),b(locked);visible->setChecked(instance&&instance->visible);locked->setChecked(instance&&instance->locked);}
        visible->setEnabled(instance);locked->setEnabled(instance);duplicate->setEnabled(instance);remove->setEnabled(editable);
        for(int i=0;i<morphFields.size();++i){auto *field=morphFields[i];field->setEnabled(editable);if(instance&&!field->editingActive()){
            const auto [n,t]=morphBindings[i];const auto weights=weightsFor(*instance,n);field->setValueSilently(t<weights.size()?weights[t]:0);
        }}
        if(instance){const auto parameters=referenceBodyShapeParameters(*instance);for(int f=0;f<bodyFields.size();++f){auto *field=bodyFields[f];field->setEnabled(editable);for(const auto &parameter:parameters)if(parameter.id==bodyBindings[f]&&!field->editingActive()){field->setValueSilently(parameter.value);break;}}}
        for(int i=0;i<expressionFields.size();++i){auto *field=expressionFields[i];field->setEnabled(editable);if(instance&&!field->editingActive())field->setValueSilently(instance->expressionWeights.value(expressionBindings[i],0));}
        for(auto &mixer:familyControls){mixer.limit->setEnabled(editable);if(instance){QSignalBlocker block(mixer.limit);mixer.limit->setChecked(referenceShapeFamilyMixLimited(*instance,mixer.family));const auto values=referenceShapeFamilyMix(*instance,mixer.family);for(int i=0;i<mixer.fields.size();++i){mixer.fields[i]->setEnabled(editable);if(!mixer.fields[i]->editingActive())for(const auto &value:values)if(value.id==mixer.ids[i])mixer.fields[i]->setValueSilently(value.weight*100);}}}
        if(buildPad){buildPad->setEnabled(editable);if(instance){float build=1,definition=.35f;for(const auto &parameter:referenceBodyShapeParameters(*instance)){if(parameter.id=="build")build=parameter.value;else if(parameter.id=="definition")definition=parameter.value;}buildPad->setValue((build-buildMinimum)/(buildMaximum-buildMinimum),(definition-definitionMinimum)/(definitionMaximum-definitionMinimum));}}
        if(handedness){handedness->setEnabled(editable);if(instance){QSignalBlocker block(handedness);for(const auto &parameter:referenceBodyShapeParameters(*instance))if(parameter.id=="handedness")handedness->setCurrentIndex(parameter.value>=.5f?1:0);}}
        const auto poseAsset=instance?instance->asset:ReferenceAssetPtr();const auto thumbnailSource=poseAsset&&poseAsset->metadata.value("referenceCatalogId").toString().startsWith("anime/")?poseAsset:ReferenceAssetPtr();if(thumbnailSource!=poseThumbnailSource){poseThumbnailSource=thumbnailSource;thumbnails->setPoseSource(thumbnailSource);if(poseKinds->currentIndex()!=2)refreshPoses();}
        if(poseKinds->currentIndex()==2&&poseAsset!=petPoseAsset){petPoseAsset=poseAsset;refreshPoseCategories();}else refreshPoseActions();
        if(!fov->editingActive())fov->setValueSilently(scene.camera.fieldOfView);
        {QSignalBlocker block(orthographic);orthographic->setChecked(scene.camera.orthographic);}
        viewport->selection(object,node,tabs->currentWidget()==posePage,axis,ikMode);
        refreshing=false;
    }
    void refresh(){
        refreshing=true;
        ikNode=-1;
        {QSignalBlocker block(objects);objects->clear();for(const auto &instance:scene.instances){auto *item=new QListWidgetItem(appIcon(instance.visible?"box":"eye-off"),instance.name,objects);item->setToolTip(instance.name);item->setData(Qt::AccessibleTextRole,instance.name);}objects->setCurrentRow(object);}
        const auto *instance=selected();
        {QSignalBlocker block(joints);joints->clear();
            if(instance&&instance->asset){QSet<int>jointSet;for(const auto &skin:instance->asset->skins)for(int n:skin.joints)jointSet.insert(n);for(auto it=instance->asset->humanoidBones.begin();it!=instance->asset->humanoidBones.end();++it)jointSet.insert(it.value());
                QHash<int,QTreeWidgetItem*>items;
                for(int n=0;n<instance->asset->nodes.size();++n)if(jointSet.contains(n)){auto *item=new QTreeWidgetItem;item->setText(0,instance->asset->nodes[n].name);item->setData(0,Qt::UserRole,n);item->setToolTip(0,item->text(0));items.insert(n,item);}
                for(auto it=items.begin();it!=items.end();++it){const int parent=instance->asset->nodes[it.key()].parentIndex;if(items.contains(parent))items[parent]->addChild(it.value());else joints->addTopLevelItem(it.value());if(it.key()==node)joints->setCurrentItem(it.value());}
                joints->expandAll();
            }
        }
        const QString nextControls=selectedControlsKey();
        if(nextControls!=controlsKey){
        controlsKey=nextControls;
        while(auto *item=morphLayout->takeAt(0)){if(auto *widget=item->widget()){for(auto *child:widget->findChildren<QWidget*>())child->setObjectName({});widget->setObjectName({});widget->hide();widget->deleteLater();}delete item;}morphFields.clear();morphBindings.clear();bodyFields.clear();bodyBindings.clear();expressionFields.clear();expressionBindings.clear();familyControls.clear();buildPad=nullptr;handedness=nullptr;
        while(auto *item=faceLayout->takeAt(0)){if(auto *widget=item->widget()){for(auto *child:widget->findChildren<QWidget*>())child->setObjectName({});widget->setObjectName({});widget->hide();widget->deleteLater();}delete item;}
        if(instance&&instance->asset){
            const auto &asset=*instance->asset;int count=0;
            const auto parameters=referenceBodyShapeParameters(*instance);
            auto *groups=new QTabBar;groups->setObjectName("referenceShapeGroups");groups->setAccessibleName("Shape regions and mixers");groups->setExpanding(false);groups->setUsesScrollButtons(true);morphLayout->addWidget(groups);auto *pages=new ReferenceShapePages;morphLayout->addWidget(pages);
            QHash<QString,QVBoxLayout*>layouts;const auto group=[&](const QString &name){if(layouts.contains(name))return layouts.value(name);auto *page=new QWidget;auto *layout=new QVBoxLayout(page);layout->setContentsMargins(0,4,0,0);layout->setSpacing(4);const int index=groups->addTab(name);groups->setTabToolTip(index,name+" shape controls");groups->setAccessibleTabName(index,name+" shape controls");pages->addPage(page);layouts.insert(name,layout);return layout;};
            bool hasBuild=false,hasDefinition=false;for(const auto &parameter:parameters){if(parameter.id=="build"){hasBuild=true;buildMinimum=parameter.minimum;buildMaximum=parameter.maximum;}if(parameter.id=="definition"){hasDefinition=true;definitionMinimum=parameter.minimum;definitionMaximum=parameter.maximum;}}
            if(hasBuild&&hasDefinition){buildPad=new ReferenceBodyPad;group("Body")->addWidget(buildPad);buildPad->started=[this]{begin("Change reference proportions");};buildPad->finished=[this](bool cancel){end(cancel);};buildPad->changed=[this](float x,float y){auto *target=selected();if(!target||target->locked)return;const QHash<QString,float> values{{"build",buildMinimum+x*(buildMaximum-buildMinimum)},{"definition",definitionMinimum+y*(definitionMaximum-definitionMinimum)}};edit("Change reference proportions",[&]{QString error;if(!setReferenceBodyShapeValues(*target,values,&error)){status(error);return false;}return true;});};}
            for(const auto family:{ReferenceStarterKind::Head,ReferenceStarterKind::Hand})if(!referenceShapeFamilyMix(*instance,family).isEmpty())addFamilyMixer(group(family==ReferenceStarterKind::Head?"Head mix":"Hand mix"),family);
            for(const auto &parameter:parameters){if(parameter.id=="handedness"){
                    handedness=new QTabBar;handedness->setObjectName("referenceHandedness");handedness->setAccessibleName("Hand model side");handedness->addTab("Left");handedness->addTab("Right");for(int side=0;side<2;++side){handedness->setTabToolTip(side,side?"Use a right hand while preserving the finger pose":"Use a left hand while preserving the finger pose");handedness->setAccessibleTabName(side,side?"Right hand model":"Left hand model");}handedness->setCurrentIndex(parameter.value>=.5f?1:0);group("Hand")->addWidget(handedness);QObject::connect(handedness,&QTabBar::currentChanged,window,[this](int side){if(refreshing)return;resolveEditors();auto *target=selected();if(!target||target->locked)return;for(const auto &value:referenceBodyShapeParameters(*target))if(value.id=="handedness"&&int(value.value)==side)return;edit("Change reference proportions",[&]{QString error;if(!setReferenceBodyShape(*target,"handedness",float(side),&error)){status(error);return false;}return true;});});continue;
                }auto *field=row(group(parameter.group.isEmpty()?"Shape":parameter.group),parameter.name,"referenceShape_"+parameter.id,parameter.minimum,parameter.maximum,parameter.decimals);field->setValueSilently(parameter.value);bodyFields.append(field);bodyBindings.append(parameter.id);
                connectScalar(field,"Change reference proportions",[this,key=parameter.id](double value){auto *current=selected();if(!current||current->locked)return;edit("Change reference proportions",[&]{QString error;if(!setReferenceBodyShape(*current,key,float(value),&error)){status(error);return false;}return true;});});
            }
            for(int n=0;n<asset.nodes.size();++n){const auto &source=asset.nodes[n];if(source.meshIndex<0)continue;const auto &mesh=asset.meshes[source.meshIndex];if(mesh.primitives.isEmpty())continue;
                const auto &targets=mesh.primitives.first().morphTargets;
                for(int t=0;t<targets.size();++t){bool expressionTarget=false;for(const auto &expression:asset.expressions)for(const auto &binding:expression.bindings)if(binding.nodeIndex==n&&binding.morphIndex==t)expressionTarget=true;if(expressionTarget)continue;
                    QString label=targets[t].name.isEmpty()?QString("Shape %1").arg(t+1):targets[t].name;
                    auto *field=row(group("Targets"),label,QString("referenceMorph_%1_%2").arg(n).arg(t),-1,2,3);morphFields.append(field);morphBindings.append({n,t});++count;
                    connectScalar(field,"Change shape",[this,n,t](double value){auto *current=selected();if(!current||current->locked)return;edit("Change shape",[&]{auto weights=weightsFor(*current,n);if(t>=weights.size()||qFuzzyCompare(weights[t],float(value)))return false;weights[t]=float(value);current->morphOverrides[n]=weights;return true;});});
                }
            }
            for(auto *layout:layouts)layout->addStretch();groups->setVisible(groups->count()>1);QObject::connect(groups,&QTabBar::currentChanged,window,[this,pages](int index){resolveEditors();pages->setPage(index);QTimer::singleShot(0,window,[this]{requestStyleThumbnails();});});
            if(!count&&parameters.isEmpty()){auto *label=new QLabel("This model has no authored shape controls");label->setWordWrap(true);morphLayout->addWidget(label);}
            const bool poseable=!asset.skins.isEmpty()||!asset.humanoidBones.isEmpty();
            const int shapeControls=parameters.size()+count;
            QString summary=poseable?QString("Poseable"):QString("Object");
            if(!asset.expressions.isEmpty())summary+=QString(" · %1 expressions").arg(asset.expressions.size());
            else if(shapeControls)summary+=QString(" · %1 shape controls").arg(shapeControls);
            capabilities->setText(summary);
            QStringList notes;for(const auto &value:asset.metadata.value("importWarnings").toArray())notes.append(value.toString());
            importNotes->setVisible(!notes.isEmpty());importNotes->setToolTip(notes.isEmpty()?"Imported model capabilities":notes.join('\n'));
            capabilities->setToolTip(QString("%1 meshes · %2 skins · %3 shape controls").arg(asset.meshes.size()).arg(asset.skins.size()).arg(shapeControls)+(notes.isEmpty()?QString():"\n"+notes.join('\n')));
            for(const auto &expression:asset.expressions){auto *field=row(faceLayout,expression.name,QString("referenceExpression_%1").arg(expression.name),0,1,3);
                expressionFields.append(field);expressionBindings.append(expression.name);field->setValueSilently(instance->expressionWeights.value(expression.name,0));
                connectScalar(field,"Change expression",[this,expression](double amount){auto *current=selected();if(!current||current->locked)return;edit("Change expression",[&]{QString error;if(!setReferenceExpression(*current,expression.name,float(amount),&error)){status(error);return false;}return true;});});
            }if(asset.expressions.isEmpty()){auto *label=new QLabel("This character has no named expression presets");label->setWordWrap(true);faceLayout->addWidget(label);}
        }else{capabilities->setText("Select a reference model");capabilities->setToolTip({});importNotes->hide();}
        morphLayout->addStretch();faceLayout->addStretch();}
        refreshing=false;refreshValues();viewport->setScene(scene);QTimer::singleShot(0,window,[this]{requestStyleThumbnails();});
    }
    void fit(){
        ReferenceBounds bounds;for(const auto &instance:scene.instances){if(!instance.visible)continue;const auto e=evaluateReferenceInstance(instance);if(e&&e->bounds.valid){bounds.include(e->bounds.minimum);bounds.include(e->bounds.maximum);}}
        if(bounds.valid){const float radius=qMax(.01f,(bounds.maximum-bounds.minimum).length()*.5f);const float aspect=canvasPanel?float(qMax(1,canvasFrameSize.width()))/qMax(1,canvasFrameSize.height()):float(qMax(1,viewport->width()))/qMax(1,viewport->height());const float vertical=scene.camera.fieldOfView*pi/360;const float limitingAngle=aspect<1?std::atan(std::tan(vertical)*aspect):vertical;scene.camera.target=bounds.center();scene.camera.distance=radius/std::sin(limitingAngle)*1.2f;scene.camera.orthoScale=radius*2.5f/qMin(1.f,aspect);viewport->setScene(scene);refreshValues();}
    }
    bool placeFinalized(ReferenceAssetPtr asset,QString *error){
        // Only the importer and the authored starter factory call this path.
        // Their immutable assets were validated and content-addressed already;
        // hashing/PNG encoding them a second time would block the GUI thread.
        if(error)error->clear();
        if(!asset){if(error)*error="The reference asset could not be prepared";return false;}
        if(scene.instances.size()>=maxReferenceInstances){if(error)*error="The reference scene is full";return false;}
        resolveEditors();ReferenceInstance instance;instance.asset=std::move(asset);instance.id=QUuid::createUuid().toString(QUuid::WithoutBraces);instance.name=instance.asset->name;
        if(!scene.instances.isEmpty())instance.translation.setX(scene.instances.size()*.6f);
        const bool added=edit("Place reference",[&]{scene.instances.append(instance);object=scene.instances.size()-1;node=-1;if(canvasPanel)fit();return true;});if(added&&!canvasPanel)fit();return added;
    }
    void importAsync(const QString &file){
        if(importing)return;importing=true;importCancel=std::make_shared<std::atomic_bool>(false);
        auto *progress=new QProgressDialog("Importing character…","Cancel",0,0,window);progress->setWindowModality(Qt::WindowModal);progress->setMinimumDuration(0);progress->show();
        const auto cancel=importCancel;QPointer<Reference3DReviewWindow> guard(window);QPointer<QProgressDialog> dialog(progress);
        QObject::connect(progress,&QProgressDialog::canceled,window,[cancel]{cancel->store(true);});
        auto *thread=QThread::create([guard,dialog,cancel,file]{
            const auto result=importReferenceAsset(file,{cancel});
            if(guard)QMetaObject::invokeMethod(guard,[guard,dialog,cancel,result]{
                if(dialog){dialog->blockSignals(true);dialog->close();dialog->deleteLater();}if(!guard)return;auto &s=*guard->m_state;s.importing=false;
                if(cancel->load()){s.status("Import cancelled");return;}
                if(!result){s.status(result.error);return;}QString error;if(!s.placeFinalized(result.asset,&error)){s.status(error);return;}
                s.status(result.warnings.isEmpty()?"Character imported":result.warnings.join(" · "));
            },Qt::QueuedConnection);
        });QObject::connect(thread,&QThread::finished,thread,&QObject::deleteLater);thread->start();
    }
};

Reference3DReviewWindow::Reference3DReviewWindow(QWidget *parent,QString materialDirectory):QMainWindow(parent),m_state(std::make_unique<State>()) {
    auto &s=*m_state;s.window=this;s.undo=new QUndoStack(this);setObjectName("reference3DReviewWindow");setWindowTitle("3D reference[*]");resize(1180,760);
    s.viewport=new ReferenceViewport;setCentralWidget(s.viewport);statusBar()->setSizeGripEnabled(true);s.thumbnails=new ReferenceThumbnailService(this);s.modelCatalog=referenceModelCatalog();s.poseCatalog=referencePosePresets();
    auto *toolbar=addToolBar("Reference scene");toolbar->setMovable(false);toolbar->setIconSize({16,16});
    const auto addActionButton=[&](const char *id,const char *icon,const QString &text,std::function<void()>callback){auto *button=actionButton(id,icon,text);toolbar->addWidget(button);connect(button,&QToolButton::clicked,this,std::move(callback));return button;};
    addActionButton("referenceImport","folder-open","Import GLB, glTF or VRM character…",[&s,this]{const auto file=QFileDialog::getOpenFileName(this,"Import reference character",{},"3D characters (*.glb *.gltf *.vrm)");if(!file.isEmpty())s.importAsync(file);});
    addActionButton("referenceOpen","file","Open reference scene…",[this]{const auto file=QFileDialog::getOpenFileName(this,"Open reference scene",{},"Reference scenes (*.toon3d)");if(!file.isEmpty()&&m_state->confirmReplacement()){QString error;if(!openScene(file,&error))m_state->status(error);}});
    addActionButton("referenceSave","save","Save self-contained reference scene…",[this]{auto file=m_state->path;if(file.isEmpty())file=QFileDialog::getSaveFileName(this,"Save reference scene",{},"Reference scenes (*.toon3d)");if(!file.isEmpty()){QString error;if(!saveScene(file,&error))m_state->status(error);}});
    toolbar->addSeparator();s.undoButton=addActionButton("referenceUndo","undo-2","Undo (Ctrl/Cmd+Z)",[&s]{s.end();s.activeUndo()->undo();});s.redoButton=addActionButton("referenceRedo","redo-2","Redo (Ctrl/Cmd+Shift+Z)",[&s]{s.end();s.activeUndo()->redo();});
    toolbar->addSeparator();addActionButton("referenceFit","scan","Frame all reference models",[&s]{if(s.canvasPanel)s.edit("Frame 3D references",[&s]{s.fit();return true;},false);else s.fit();});
    auto *notice=new QLabel("  Base-color preview");notice->setObjectName("referencePreviewMode");notice->setAccessibleName("Base-color preview");
    notice->setToolTip("Preview shows deformed geometry, base colors, vertex colors and base-color textures with simple lighting. Normal maps, metallic/roughness, occlusion, emissive and avatar shader data are retained in the scene but are not reproduced by this review renderer.");
    notice->setStyleSheet(QString("color:%1").arg(ui::muted));toolbar->addWidget(notice);
    connect(s.undo,&QUndoStack::canUndoChanged,s.undoButton,&QWidget::setEnabled);connect(s.undo,&QUndoStack::canRedoChanged,s.redoButton,&QWidget::setEnabled);connect(s.undo,&QUndoStack::cleanChanged,this,[this](bool clean){if(!m_state->canvasPanel)setWindowModified(!clean);});s.undoButton->setEnabled(false);s.redoButton->setEnabled(false);
    auto *undoShortcut=new QShortcut(QKeySequence::Undo,this);connect(undoShortcut,&QShortcut::activated,this,[&s]{s.end();s.activeUndo()->undo();});auto *redoShortcut=new QShortcut(QKeySequence::Redo,this);connect(redoShortcut,&QShortcut::activated,this,[&s]{s.end();s.activeUndo()->redo();});
    auto *browser=new QDockWidget("References",this);browser->setObjectName("referenceBrowserDock");browser->setAllowedAreas(Qt::LeftDockWidgetArea|Qt::RightDockWidgetArea);
    auto *browserBody=new QWidget;auto *browserLayout=new QVBoxLayout(browserBody);browserLayout->setContentsMargins(4,4,4,4);browserLayout->setSpacing(4);
    auto *browserTabs=new ReferencePropertyTabs(280);s.browserTabs=browserTabs;browserTabs->setObjectName("referenceLibraryTabs");browserTabs->setDocumentMode(true);browserTabs->setIconSize({16,16});browserLayout->addWidget(browserTabs,3);
    auto *modelsPage=new QWidget;auto *modelsLayout=new QVBoxLayout(modelsPage);modelsLayout->setContentsMargins(0,0,0,0);modelsLayout->setSpacing(4);
    s.modelKinds=new ReferenceModelCategories;s.modelKinds->setObjectName("referenceModelKinds");s.modelKinds->setAccessibleName("Model category");
    for(const auto &entry:QList<QPair<QString,QString>>{{"toon-character","Body"},{"circle","Head"},{"hand","Hand"},{"sparkles","Anime"},{"toon-pet","Pets"}}){const int tab=s.modelKinds->addTab(appIcon(entry.first),entry.second);s.modelKinds->setTabData(tab,entry.second);s.modelKinds->setTabToolTip(tab,entry.second+" reference models");s.modelKinds->setAccessibleTabName(tab,entry.second+" reference models");}modelsLayout->addWidget(s.modelKinds);
    s.modelSearch=new QLineEdit;s.modelSearch->setObjectName("referenceModelSearch");s.modelSearch->setPlaceholderText("Find a model");s.modelSearch->setAccessibleName("Search built-in reference models");s.modelSearch->setClearButtonEnabled(true);modelsLayout->addWidget(s.modelSearch);
    s.modelGallery=new ReferenceGallery;s.starters=s.modelGallery;s.modelGallery->setObjectName("referenceStarters");s.modelGallery->setAccessibleName("Built-in reference models");modelsLayout->addWidget(s.modelGallery,1);
    s.placeModel=actionButton("referencePlaceStarter","plus","Place selected reference model");s.placeModel->setText("Place model");s.placeModel->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);s.placeModel->setFixedWidth(120);modelsLayout->addWidget(s.placeModel);browserTabs->addTab(modelsPage,appIcon("box"),"Models");
    const auto placeStarter=[this,&s]{auto *item=s.modelGallery->currentItem();if(!item)return;this->placeCatalogModelAsync(item->data(Qt::UserRole).toString());};
    connect(s.placeModel,&QToolButton::clicked,this,placeStarter);connect(s.modelGallery,&QListWidget::itemActivated,this,[placeStarter]{placeStarter();});connect(s.modelKinds,&QTabBar::currentChanged,this,[&s]{s.refreshModels();});connect(s.modelSearch,&QLineEdit::textChanged,this,[&s]{s.refreshModels();});s.modelGallery->visibleItemsChanged=[&s]{s.requestVisibleThumbnails(s.modelGallery,false);};
    auto *posesPage=new QWidget;auto *posesLayout=new QVBoxLayout(posesPage);posesLayout->setContentsMargins(0,0,0,0);posesLayout->setSpacing(4);
    s.poseKinds=new QTabBar;s.poseKinds->setObjectName("referencePoseKinds");s.poseKinds->setAccessibleName("Pose category");s.poseKinds->addTab(appIcon("toon-symbol-pose"),"Body");s.poseKinds->addTab(appIcon("hand"),"Hands");s.poseKinds->addTab(appIcon("toon-pet"),"Pets");for(int tab=0;tab<3;++tab){s.poseKinds->setTabToolTip(tab,s.poseKinds->tabText(tab)+" pose presets");s.poseKinds->setAccessibleTabName(tab,s.poseKinds->tabText(tab)+" pose presets");}posesLayout->addWidget(s.poseKinds);
    s.poseCategories=new QTabBar;s.poseCategories->setObjectName("referencePoseCategories");s.poseCategories->setAccessibleName("Filter pose collection");s.poseCategories->setExpanding(false);s.poseCategories->setUsesScrollButtons(true);posesLayout->addWidget(s.poseCategories);
    s.poseSearch=new QLineEdit;s.poseSearch->setObjectName("referencePoseSearch");s.poseSearch->setPlaceholderText("Find a pose");s.poseSearch->setAccessibleName("Search built-in pose presets");s.poseSearch->setClearButtonEnabled(true);posesLayout->addWidget(s.poseSearch);
    s.poseGallery=new ReferenceGallery;s.poseGallery->setObjectName("referencePosePresets");s.poseGallery->setAccessibleName("Built-in pose presets");posesLayout->addWidget(s.poseGallery,1);
    s.poseHands=new QTabBar;s.poseHands->setObjectName("referencePoseHandTarget");s.poseHands->setAccessibleName("Hands receiving the pose");for(const auto &side:QStringList{"Both","Left","Right"}){const int tab=s.poseHands->addTab(side);s.poseHands->setTabToolTip(tab,"Apply to "+side.toLower()+" hand"+(tab==0?"s":QString()));s.poseHands->setAccessibleTabName(tab,side+" hand target");}posesLayout->addWidget(s.poseHands);
    s.poseTarget=new QLabel;s.poseTarget->setObjectName("referencePoseTarget");s.poseTarget->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Fixed);s.poseTarget->setFixedHeight(qMax(22,s.poseTarget->fontMetrics().height()+4));posesLayout->addWidget(s.poseTarget);
    auto *poseActions=new QHBoxLayout;s.applyPose=actionButton("referenceApplyPose","clipboard-paste","Apply selected pose to the selected reference");s.applyPose->setText("Apply pose");s.applyPose->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);s.applyPose->setFixedWidth(110);poseActions->addWidget(s.applyPose);
    s.mirrorPose=actionButton("referenceMirrorPose","arrow-left-right","Mirror the selected body pose or switch the selected hand target");s.mirrorPose->setCheckable(true);s.mirrorPose->setText("Mirror");s.mirrorPose->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);s.mirrorPose->setFixedWidth(90);poseActions->addWidget(s.mirrorPose);poseActions->addStretch();posesLayout->addLayout(poseActions);browserTabs->addTab(posesPage,appIcon("toon-symbol-pose"),"Poses");
    const auto usePose=[this,&s]{auto *item=s.poseGallery->currentItem();if(!item)return;QString error;if(!applyPosePreset(item->data(Qt::UserRole).toString(),ReferenceHandTarget(s.poseHands->currentIndex()),s.mirrorPose->isChecked(),&error))s.status(error);};
    connect(s.applyPose,&QToolButton::clicked,this,usePose);connect(s.poseGallery,&QListWidget::itemActivated,this,[usePose]{usePose();});connect(s.poseGallery,&QListWidget::currentItemChanged,this,[&s]{s.refreshPoseActions();});connect(s.poseHands,&QTabBar::currentChanged,this,[&s]{s.refreshPoseActions();});connect(s.poseKinds,&QTabBar::currentChanged,this,[&s]{s.refreshPoseCategories();});connect(s.poseCategories,&QTabBar::currentChanged,this,[&s]{s.refreshPoses();});connect(s.poseSearch,&QLineEdit::textChanged,this,[&s]{s.refreshPoses();});s.poseGallery->visibleItemsChanged=[&s]{s.requestVisibleThumbnails(s.poseGallery,true);};
    auto *materialPage=new QWidget;auto *materialLayout=new QVBoxLayout(materialPage);materialLayout->setContentsMargins(0,0,0,0);materialLayout->setSpacing(4);
    s.materialKind=new QComboBox;s.materialKind->setObjectName("referenceMaterialKind");s.materialKind->setAccessibleName("Filter material kind");s.materialKind->addItem("All kinds",-1);
    for(int kind=0;kind<=int(ReferenceLibraryKind::Camera);++kind)s.materialKind->addItem(referenceLibraryKindName(ReferenceLibraryKind(kind)),kind);
    materialLayout->addWidget(s.materialKind);auto *searchRow=new QHBoxLayout;s.materialSearch=new QLineEdit;s.materialSearch->setObjectName("referenceMaterialSearch");s.materialSearch->setPlaceholderText("Search materials");s.materialSearch->setAccessibleName("Search reference materials");s.materialSearch->setClearButtonEnabled(true);searchRow->addWidget(s.materialSearch);
    s.materialFavorite=actionButton("referenceMaterialFavorites","pin","Show favorite materials");s.materialFavorite->setCheckable(true);searchRow->addWidget(s.materialFavorite);materialLayout->addLayout(searchRow);
    s.materials=new QListWidget;s.materials->setObjectName("referenceMaterials");s.materials->setAccessibleName("Saved reference materials");s.materials->setIconSize({40,40});materialLayout->addWidget(s.materials,1);browserTabs->addTab(materialPage,appIcon("layers"),"Materials");
    auto *materialActions=new QHBoxLayout;auto *useMaterial=actionButton("referenceApplyMaterial","clipboard-paste","Apply selected material");auto *registerMaterial=actionButton("referenceRegisterMaterial","save","Save reference as reusable material…");auto *favoriteMaterial=actionButton("referenceFavoriteMaterial","pin","Toggle selected material favorite");auto *removeMaterial=actionButton("referenceRemoveMaterial","trash","Remove selected library material; placed references are kept");auto *reloadMaterials=actionButton("referenceReloadMaterials","rotate-ccw","Reload material library");
    for(auto *button:{useMaterial,registerMaterial,favoriteMaterial,removeMaterial,reloadMaterials})materialActions->addWidget(button);materialActions->addStretch();materialLayout->addLayout(materialActions);
    if(materialDirectory.isEmpty())materialDirectory=QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)).filePath("reference3d-materials");s.library=new ReferenceMaterialLibrary(materialDirectory,this);
    connect(s.library,&ReferenceMaterialLibrary::changed,this,[&s]{s.refreshMaterials();});connect(s.library,&ReferenceMaterialLibrary::warning,this,[&s](const QString &message){s.status(message);});
    QString libraryError;if(!s.library->load(&libraryError))s.status(libraryError);
    connect(reloadMaterials,&QToolButton::clicked,this,[&s]{QString error;if(!s.library->load(&error))s.status(error);else s.status("Material library reloaded");});
    connect(s.materialSearch,&QLineEdit::textChanged,this,[&s]{s.refreshMaterials();});connect(s.materialKind,&QComboBox::currentIndexChanged,this,[&s]{s.refreshMaterials();});connect(s.materialFavorite,&QToolButton::toggled,this,[&s]{s.refreshMaterials();});
    const auto applySelectedMaterial=[this,&s]{auto *item=s.materials->currentItem();if(!item)return;QString error;if(!applyMaterial(item->data(Qt::UserRole).toString(),&error))s.status(error);};connect(useMaterial,&QToolButton::clicked,this,applySelectedMaterial);connect(s.materials,&QListWidget::itemDoubleClicked,this,[applySelectedMaterial]{applySelectedMaterial();});
    connect(registerMaterial,&QToolButton::clicked,this,[this,&s]{QStringList choices;for(int kind=0;kind<=int(ReferenceLibraryKind::Camera);++kind)choices.append(referenceLibraryKindName(ReferenceLibraryKind(kind)));bool ok=false;const auto choice=QInputDialog::getItem(this,"Register reference material","Material kind",choices,0,false,&ok);if(!ok)return;const auto label=QInputDialog::getText(this,"Register reference material","Name",QLineEdit::Normal,s.selected()?s.selected()->name:"Reference scene",&ok);if(!ok||label.trimmed().isEmpty())return;QString error;if(saveMaterial(label,ReferenceLibraryKind(choices.indexOf(choice)),&error).isEmpty())s.status(error);});
    connect(favoriteMaterial,&QToolButton::clicked,this,[&s]{auto *item=s.materials->currentItem();if(!item)return;const auto *entry=s.library->entry(item->data(Qt::UserRole).toString());if(!entry)return;QString error;if(!s.library->setFavorite(entry->id,!entry->favorite,&error))s.status(error);});
    connect(removeMaterial,&QToolButton::clicked,this,[&s]{auto *item=s.materials->currentItem();if(!item)return;QString error;if(!s.library->remove(item->data(Qt::UserRole).toString(),&error))s.status(error);});
    for(int tab=0;tab<browserTabs->count();++tab){browserTabs->setTabToolTip(tab,browserTabs->tabText(tab));browserTabs->tabBar()->setAccessibleTabName(tab,browserTabs->tabText(tab));browserTabs->tabBar()->setTabData(tab,browserTabs->tabText(tab));}
    auto *sceneLabel=new QLabel("Scene");browserLayout->addWidget(sceneLabel);s.objects=new QListWidget;s.objects->setObjectName("referenceSceneObjects");s.objects->setAccessibleName("Models in the reference scene");s.objects->setMinimumHeight(64);s.objects->setMaximumHeight(180);browserLayout->addWidget(s.objects,1);
    auto *objectActions=new QHBoxLayout;s.duplicate=actionButton("referenceDuplicate","copy","Duplicate selected reference");s.remove=actionButton("referenceRemove","trash","Remove selected reference");s.visible=actionButton("referenceVisible","eye","Show selected reference");s.visible->setCheckable(true);s.locked=actionButton("referenceLocked","lock-keyhole","Lock selected reference");s.locked->setCheckable(true);
    for(auto *button:{s.duplicate,s.remove,s.visible,s.locked})objectActions->addWidget(button);objectActions->addStretch();browserLayout->addLayout(objectActions);browser->setWidget(browserBody);addDockWidget(Qt::LeftDockWidgetArea,browser);browser->setMinimumWidth(220);
    auto *properties=new QDockWidget("Reference properties",this);properties->setObjectName("referencePropertiesDock");properties->setMinimumWidth(250);
    auto *body=new QWidget;auto *layout=new QVBoxLayout(body);layout->setContentsMargins(4,4,4,4);layout->setSpacing(4);
    auto *capabilityRow=new QHBoxLayout;s.capabilities=new QLabel;s.capabilities->setObjectName("referenceCapabilities");s.capabilities->setWordWrap(true);capabilityRow->addWidget(s.capabilities,1);s.importNotes=actionButton("referenceImportNotes","triangle-alert","Review imported model limitations");capabilityRow->addWidget(s.importNotes);layout->addLayout(capabilityRow);
    connect(s.importNotes,&QToolButton::clicked,this,[this,&s]{const auto *instance=s.selected();if(!instance)return;QStringList notes;for(const auto &value:instance->asset->metadata.value("importWarnings").toArray())notes.append(value.toString());QMessageBox::information(this,"Imported model capabilities",notes.join("\n\n"));});
    s.tabs=new ReferencePropertyTabs;s.tabs->setObjectName("referencePropertyTabs");s.tabs->setDocumentMode(true);s.tabs->setIconSize({16,16});layout->addWidget(s.tabs,1);
    const auto page=[&](const QString &title,const QString &icon,QWidget **content){auto *scroll=new QScrollArea;scroll->setFrameShape(QFrame::NoFrame);scroll->setWidgetResizable(true);scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);*content=new QWidget;auto *v=new QVBoxLayout(*content);v->setContentsMargins(4,4,4,4);v->setSpacing(4);scroll->setWidget(*content);s.tabs->addTab(scroll,appIcon(icon),title);return v;};
    QWidget *objectContent,*poseContent,*shapeContent,*faceContent,*cameraContent;
    auto *objectLayout=page("Object","move",&objectContent);s.objectPage=s.tabs->widget(0);
    s.name=new QLineEdit;s.name->setObjectName("referenceObjectName");s.name->setAccessibleName("Reference name");objectLayout->addWidget(new PropertyRow("Name",s.name));
    for(int a=0;a<3;++a){const QString axis=QString("XYZ")[a];s.position[a]=s.row(objectLayout,"Position "+axis,"referencePosition"+axis,-10000,10000,3);s.position[a]->setSliderRange(-5,5);
        s.rotation[a]=s.row(objectLayout,"Rotation "+axis,"referenceRotation"+axis,-180,180,2);s.scale[a]=s.row(objectLayout,"Scale "+axis,"referenceScale"+axis,.01,100,3);s.scale[a]->setSliderRange(.1,3);
        s.connectScalar(s.position[a],"Move reference",[&s,a](double value){auto *i=s.selected();if(!i||i->locked)return;s.edit("Move reference",[&]{if(i->translation[a]==float(value))return false;i->translation[a]=value;return true;});});
        s.connectScalar(s.rotation[a],"Rotate reference",[&s,a](double value){auto *i=s.selected();if(!i||i->locked)return;s.edit("Rotate reference",[&]{auto e=i->rotation.toEulerAngles();if(qFuzzyCompare(e[a],float(value)))return false;e[a]=value;i->rotation=QQuaternion::fromEulerAngles(e);return true;});});
        s.connectScalar(s.scale[a],"Scale reference",[&s,a](double value){auto *i=s.selected();if(!i||i->locked)return;s.edit("Scale reference",[&]{if(i->scale[a]==float(value))return false;i->scale[a]=value;return true;});});
    }objectLayout->addStretch();
    auto *poseLayout=page("Pose","toon-character",&poseContent);s.posePage=s.tabs->widget(1);
    s.joints=new QTreeWidget;s.joints->setObjectName("referenceJoints");s.joints->setAccessibleName("Pose joints");s.joints->setHeaderHidden(true);s.joints->setIndentation(10);s.joints->setMinimumHeight(120);s.joints->setMaximumHeight(250);poseLayout->addWidget(s.joints);
    s.poseStatus=new QLabel;s.poseStatus->setObjectName("referencePoseStatus");s.poseStatus->setWordWrap(true);poseLayout->addWidget(s.poseStatus);
    s.poseModes=new QTabBar;s.poseModes->setObjectName("referencePoseModes");s.poseModes->setAccessibleName("Joint posing method");s.poseModes->addTab("FK");s.poseModes->addTab("IK");s.poseModes->addTab(appIcon("hand"),"Hands");s.poseModes->setTabToolTip(0,"Rotate individual joints with local axes");s.poseModes->setTabToolTip(1,"Two-bone arm and leg IK; drag a hand or foot target; Shift-drag to change its pole");s.poseModes->setTabToolTip(2,"Adjust all fingers or one finger with curl and spread controls");s.poseModes->setAccessibleTabName(0,"Joint rotations");s.poseModes->setAccessibleTabName(1,"Two-bone limb IK");s.poseModes->setAccessibleTabName(2,"Finger curl and spread");poseLayout->addWidget(s.poseModes);
    s.fkGroup=new QWidget;auto *fkLayout=new QVBoxLayout(s.fkGroup);fkLayout->setContentsMargins(0,0,0,0);fkLayout->setSpacing(4);poseLayout->addWidget(s.fkGroup);
    auto *axes=new QHBoxLayout;for(int a=0;a<3;++a){auto *button=actionButton(a==0?"referencePoseAxisX":a==1?"referencePoseAxisY":"referencePoseAxisZ","rotate-ccw",QString("Rotate around local ")+QString("XYZ")[a]);button->setText(QString("XYZ")[a]);button->setToolButtonStyle(Qt::ToolButtonTextOnly);button->setCheckable(true);s.axisButtons[a]=button;axes->addWidget(button);connect(button,&QToolButton::clicked,this,[&s,a]{s.axis=a;for(int b=0;b<3;++b)s.axisButtons[b]->setChecked(a==b);s.refreshValues();});}s.axisButtons[2]->setChecked(true);axes->addStretch();fkLayout->addLayout(axes);
    for(int a=0;a<3;++a){const QString axis=QString("XYZ")[a];s.poseRotation[a]=s.row(fkLayout,"Joint "+axis,"referenceJointRotation"+axis,-180,180,2);s.connectScalar(s.poseRotation[a],"Pose joint",[this,&s,a](double value){auto *i=s.selected();if(!i||s.node<0)return;const auto rest=i->asset->nodes[s.node].rotation;auto degrees=(rest.conjugated()*i->poseRotations.value(s.node,rest)).toEulerAngles();degrees[a]=value;setJointRotation(degrees);});}
    s.ikGroup=new QWidget;auto *ikLayout=new QVBoxLayout(s.ikGroup);ikLayout->setContentsMargins(0,0,0,0);ikLayout->setSpacing(4);poseLayout->addWidget(s.ikGroup);
    for(int a=0;a<3;++a){const QString axis=QString("XYZ")[a];s.ikTargetFields[a]=s.row(ikLayout,"Target "+axis,"referenceIKTarget"+axis,-10000,10000,3);s.ikPoleFields[a]=s.row(ikLayout,"Pole "+axis,"referenceIKPole"+axis,-10000,10000,3);s.ikTargetFields[a]->setSliderRange(-3,3);s.ikPoleFields[a]->setSliderRange(-3,3);
        s.connectScalar(s.ikTargetFields[a],"Pose limb target",[this,&s,a](double value){auto target=s.ikTarget;target[a]=value;QString error;if(!setIKTarget(target,s.ikPole,&error))s.status(error);});
        s.connectScalar(s.ikPoleFields[a],"Pose limb pole",[this,&s,a](double value){auto pole=s.ikPole;pole[a]=value;QString error;if(!setIKTarget(s.ikTarget,pole,&error))s.status(error);});
    }
    s.handGroup=new QWidget;auto *handLayout=new QVBoxLayout(s.handGroup);handLayout->setContentsMargins(0,0,0,0);handLayout->setSpacing(4);poseLayout->addWidget(s.handGroup);
    s.handSides=new QTabBar;s.handSides->setObjectName("referenceHandControlSides");s.handSides->setAccessibleName("Hands being adjusted");for(const auto &side:QStringList{"Both","Left","Right"}){const int index=s.handSides->addTab(side);s.handSides->setTabToolTip(index,"Adjust "+side.toLower()+" hand"+(index==0?"s":QString()));s.handSides->setAccessibleTabName(index,side+" hand controls");}handLayout->addWidget(s.handSides);
    s.handFingers=new QTabBar;s.handFingers->setObjectName("referenceHandControlFingers");s.handFingers->setAccessibleName("Fingers being adjusted");s.handFingers->setExpanding(false);s.handFingers->setUsesScrollButtons(true);for(const auto &id:QStringList{"all","thumb","index","middle","ring","little"}){const auto name=id=="all"?QString("All"):id.left(1).toUpper()+id.mid(1);const int index=s.handFingers->addTab(name);s.handFingers->setTabData(index,id);s.handFingers->setTabToolTip(index,id=="all"?"Adjust all five fingers together":"Adjust the "+id+" finger");s.handFingers->setAccessibleTabName(index,id=="all"?"All fingers":name+" finger");}handLayout->addWidget(s.handFingers);
    s.handStatus=new QLabel;s.handStatus->setObjectName("referenceHandControlStatus");s.handStatus->setWordWrap(true);handLayout->addWidget(s.handStatus);s.handCurl=s.row(handLayout,"Curl","referenceHandCurl",0,100,1);s.handSplay=s.row(handLayout,"Spread","referenceHandSplay",-100,100,1);s.handCurl->setAccessibleName("Selected fingers curl percentage");s.handSplay->setAccessibleName("Selected fingers spread percentage");
    for(auto *field:{s.handCurl,s.handSplay}){connect(field,&ValueSlider::editingStarted,this,[&s]{s.handEditFinger=s.handFingers->tabData(s.handFingers->currentIndex()).toString();s.handEditSide=ReferenceHandTarget(s.handSides->currentIndex());s.handEditing=true;});s.connectScalar(field,"Pose fingers",[&s,field](double value){s.changeHandValue(field==s.handCurl,value);});connect(field,&ValueSlider::editingFinished,this,[&s]{s.handEditing=false;});connect(field,&ValueSlider::editingCancelled,this,[&s]{s.handEditing=false;});}
    connect(s.handSides,&QTabBar::currentChanged,this,[&s]{s.resolveEditors();s.refreshValues();});connect(s.handFingers,&QTabBar::currentChanged,this,[&s]{s.resolveEditors();s.refreshValues();});
    s.resetHands=actionButton("referenceResetFingers","rotate-ccw","Reset selected fingers to open without changing the wrist or body");handLayout->addWidget(s.resetHands);connect(s.resetHands,&QToolButton::clicked,this,[&s]{s.resolveEditors();auto *instance=s.selected();if(!instance||instance->locked)return;const auto id=s.handFingers->tabData(s.handFingers->currentIndex()).toString();QString error;QHash<QString,QVector2D> values;for(const auto &control:referenceHandControls(*instance,ReferenceHandTarget(s.handSides->currentIndex()),&error))if(id=="all"||id==control.id)values.insert(control.id,{});s.edit("Reset fingers",[&]{if(!setReferenceHandControls(*instance,values,ReferenceHandTarget(s.handSides->currentIndex()),&error)){s.status(error);return false;}return true;});});
    connect(s.poseModes,&QTabBar::currentChanged,this,[&s]{s.resolveEditors();s.refreshValues();});
    auto *reset=actionButton("referenceResetPose","rotate-ccw","Reset all joint rotations and translations");s.resetPose=reset;poseLayout->addWidget(reset);connect(reset,&QToolButton::clicked,this,[&s]{auto *i=s.selected();if(!i||i->locked)return;s.edit("Reset pose",[&]{if(i->poseRotations.isEmpty()&&i->poseTranslations.isEmpty())return false;i->poseRotations.clear();i->poseTranslations.clear();return true;});});poseLayout->addStretch();
    auto *shapeLayout=page("Shape","sliders-horizontal",&shapeContent);s.shapePage=s.tabs->widget(2);s.morphLayout=shapeLayout;
    s.faceLayout=page("Face","toon-expression",&faceContent);
    auto *cameraLayout=page("Camera","toon-perspective-grid",&cameraContent);s.orthographic=new QCheckBox("Parallel projection");s.orthographic->setObjectName("referenceOrthographic");cameraLayout->addWidget(s.orthographic);s.fov=s.row(cameraLayout,"Field of view","referenceCameraFOV",5,120,1);cameraLayout->addStretch();
    s.connectScalar(s.fov,"Camera perspective",[&s](double value){if(s.canvasPanel)s.edit("Camera perspective",[&s,value]{s.scene.camera.fieldOfView=value;return true;},false);else{s.scene.camera.fieldOfView=value;s.viewport->setScene(s.scene);}});
    connect(s.orthographic,&QCheckBox::toggled,this,[&s](bool on){if(s.refreshing)return;if(s.canvasPanel)s.edit("Camera projection",[&s,on]{s.scene.camera.orthographic=on;return true;},false);else{s.scene.camera.orthographic=on;s.viewport->setScene(s.scene);}});
    for(int tab=0;tab<s.tabs->count();++tab){s.tabs->setTabToolTip(tab,s.tabs->tabText(tab));s.tabs->tabBar()->setAccessibleTabName(tab,s.tabs->tabText(tab));s.tabs->tabBar()->setTabData(tab,s.tabs->tabText(tab));}
    properties->setWidget(body);addDockWidget(Qt::RightDockWidgetArea,properties);
    connect(s.objects,&QListWidget::currentRowChanged,this,&Reference3DReviewWindow::selectObject);
    connect(s.joints,&QTreeWidget::currentItemChanged,this,[this](QTreeWidgetItem *item){if(item)selectJoint(item->data(0,Qt::UserRole).toInt());});
    connect(s.tabs,&QTabWidget::currentChanged,this,[&s]{s.end();s.refreshValues();s.requestStyleThumbnails();});
    connect(s.name,&QLineEdit::editingFinished,this,[&s]{auto *i=s.selected();if(!i||i->locked)return;const auto name=s.name->text().trimmed();if(name.isEmpty()||name==i->name)return;s.edit("Rename reference",[&]{i->name=name;return true;});s.name->setModified(false);});
    connect(s.visible,&QToolButton::toggled,this,[&s](bool on){if(s.refreshing)return;auto *i=s.selected();if(!i)return;s.edit("Reference visibility",[&]{if(i->visible==on)return false;i->visible=on;return true;});});
    connect(s.locked,&QToolButton::toggled,this,[&s](bool on){if(s.refreshing)return;auto *i=s.selected();if(!i)return;s.edit("Reference lock",[&]{if(i->locked==on)return false;i->locked=on;return true;});});
    connect(s.duplicate,&QToolButton::clicked,this,[&s]{auto *i=s.selected();if(!i||s.scene.instances.size()>=maxReferenceInstances)return;auto copy=*i;copy.id=QUuid::createUuid().toString(QUuid::WithoutBraces);copy.name+=" copy";copy.translation.setX(copy.translation.x()+.5f);s.edit("Duplicate reference",[&]{s.scene.instances.append(copy);s.object=s.scene.instances.size()-1;s.node=-1;return true;});});
    connect(s.remove,&QToolButton::clicked,this,[&s]{auto *i=s.selected();if(!i||i->locked)return;s.edit("Remove reference",[&]{s.scene.instances.removeAt(s.object);s.object=qMin(s.object,int(s.scene.instances.size())-1);s.node=-1;return true;});});
    s.viewport->objectPicked=[this](int index){selectObject(index);};s.viewport->jointPicked=[this](int index){selectJoint(index);};s.viewport->gestureStarted=[&s]{s.begin("Manipulate reference");};s.viewport->gestureFinished=[&s](bool cancel){s.end(cancel);};
    s.viewport->rotationDragged=[&s](float delta){auto *i=s.selected();if(!i||i->locked||s.node<0)return;const auto rest=i->asset->nodes[s.node].rotation;const auto current=i->poseRotations.value(s.node,rest);QVector3D axis;axis[s.axis]=1;s.edit("Pose joint",[&]{i->poseRotations[s.node]=current*QQuaternion::fromAxisAndAngle(axis,delta);return true;});};
    s.viewport->placementDragged=[&s](QVector3D delta){auto *i=s.selected();if(!i||i->locked||delta.isNull())return;s.edit("Move reference",[&]{i->translation+=delta;return true;});};
    s.viewport->limbDragged=[this,&s](QVector3D delta,bool pole){s.synchronizeIK();QString error;const auto target=pole?s.ikTarget:s.ikTarget+delta;const auto bend=pole?s.ikPole+delta:s.ikPole;if(!setIKTarget(target,bend,&error))s.status(error);};
    s.viewport->cameraChanged=[&s](ReferenceCamera camera){s.scene.camera=camera;s.viewport->setScene(s.scene);s.refreshValues();};
    s.viewport->renderFailed=[&s](const QString &error){s.status(error);};
    s.viewport->framePublished=[this](quint64 generation,bool full){emit previewFramePublished(generation,full);};
    s.thumbnails->ready=[&s](const QString &id,bool pose,const QImage &image,const QString &error,ReferenceAssetPtr source){s.updateThumbnail(id,pose,image,error,source);};s.refreshModels();s.refreshPoseCategories();
    s.refresh();s.status(libraryError.isEmpty()?"Drawing references · Built-in models and poses · Base-color preview":libraryError);
}
Reference3DReviewWindow::~Reference3DReviewWindow(){
    if(m_state->importCancel)m_state->importCancel->store(true);
    m_state->cancelCatalogPlacement();
    // Child widgets outlive the Pimpl until QObject's base destructor runs.
    // Finish their edit callbacks while State still exists, then disconnect.
    for(auto *field:findChildren<ValueSlider*>())if(field->editingActive())field->cancelEditing();
    for(auto *child:findChildren<QObject*>())QObject::disconnect(child,nullptr,this,nullptr);
}
void Reference3DReviewWindow::setCanvasPanelMode(QUndoStack *documentUndo,QSize frameSize){
    if(m_state->canvasPanel&&m_state->externalUndo==documentUndo){m_state->canvasFrameSize=frameSize;return;}
    auto &s=*m_state;s.canvasPanel=true;s.canvasFrameSize=frameSize;s.externalUndo=documentUndo;
    setWindowFlags(Qt::Widget);s.viewport->suppressPreview();
    for(auto *shortcut:findChildren<QShortcut*>())shortcut->setEnabled(false);
    if(centralWidget()){auto *view=takeCentralWidget();view->hide();view->setParent(this);}
    auto *browser=findChild<QDockWidget*>("referenceBrowserDock");auto *properties=findChild<QDockWidget*>("referencePropertiesDock");
    if(browser&&properties){
        for(auto *dock:{browser,properties}){dock->setFeatures(QDockWidget::NoDockWidgetFeatures);dock->setAllowedAreas(Qt::LeftDockWidgetArea);auto *title=new QWidget(dock);title->setFixedHeight(0);dock->setTitleBarWidget(title);}
        browser->setWindowTitle("Library");properties->setWindowTitle("Controls");removeDockWidget(properties);addDockWidget(Qt::LeftDockWidgetArea,properties);tabifyDockWidget(browser,properties);browser->raise();
    }
    statusBar()->hide();setMinimumSize(280,240);resize(340,700);
    QObject::disconnect(s.undo,nullptr,s.undoButton,nullptr);QObject::disconnect(s.undo,nullptr,s.redoButton,nullptr);
    if(documentUndo){connect(documentUndo,&QUndoStack::canUndoChanged,s.undoButton,&QWidget::setEnabled);connect(documentUndo,&QUndoStack::canRedoChanged,s.redoButton,&QWidget::setEnabled);s.undoButton->setEnabled(documentUndo->canUndo());s.redoButton->setEnabled(documentUndo->canRedo());}
}
void Reference3DReviewWindow::syncCanvasScene(const ReferenceScene &scene,int object,int joint){
    auto &s=*m_state;s.cancelCatalogPlacement();s.scene=scene;
    s.object=scene.instances.isEmpty()?-1:qBound(0,object<0?s.object:object,int(scene.instances.size())-1);s.node=joint;
    s.name->setModified(false);s.undo->clear();s.refresh();setWindowModified(false);
    if(s.externalUndo){s.undoButton->setEnabled(s.externalUndo->canUndo());s.redoButton->setEnabled(s.externalUndo->canRedo());}
}
void Reference3DReviewWindow::cancelPendingEdits(){for(auto *field:findChildren<ValueSlider*>())if(field->editingActive())field->cancelEditing();m_state->end(true);}
void Reference3DReviewWindow::finishPendingEdits(){m_state->resolveEditors();m_state->end();}
int Reference3DReviewWindow::selectedObject()const{return m_state->object;}
int Reference3DReviewWindow::selectedJoint()const{return m_state->node;}
const ReferenceScene &Reference3DReviewWindow::scene()const{return m_state->scene;}
bool Reference3DReviewWindow::setScene(const ReferenceScene &scene,QString *error){
    const auto validation=validateReferenceScene(scene);if(!validation.isEmpty()){if(error)*error=validation;return false;}
    auto &s=*m_state;if(s.canvasPanel){s.resolveEditors();return s.edit("Replace 3D reference scene",[&s,&scene]{s.scene=scene;s.object=scene.instances.isEmpty()?-1:0;s.node=-1;return true;},false);}s.cancelCatalogPlacement();s.end();s.scene=scene;s.object=scene.instances.isEmpty()?-1:0;s.node=-1;s.name->setModified(false);s.undo->clear();s.refresh();setWindowModified(false);emit sceneChanged();return true;
}
bool Reference3DReviewWindow::addAsset(const ReferenceAsset &asset,QString *error){
    auto ptr=finalizeReferenceAsset(asset,error);if(!ptr)return false;return m_state->placeFinalized(std::move(ptr),error);
}
bool Reference3DReviewWindow::placeStarter(const QString &id,QString *error){return placeCatalogModel(id,error);}
bool Reference3DReviewWindow::placeCatalogModel(const QString &id,QString *error){m_state->cancelCatalogPlacement();auto asset=m_state->thumbnails->asset(id);if(!asset)asset=createReferenceCatalogAsset(id,error);if(!asset)return false;return m_state->placeFinalized(std::move(asset),error);}
void Reference3DReviewWindow::placeCatalogModelAsync(const QString &id){m_state->prepareCatalogPlacement(id);}
bool Reference3DReviewWindow::catalogPlacementInProgress()const{return !m_state->pendingCatalogId.isEmpty();}
bool Reference3DReviewWindow::applyStarterShape(const QString &id,QString *error){auto &s=*m_state;s.resolveEditors();auto *instance=s.selected();if(!instance||instance->locked){if(error)*error="Select an unlocked model to change its shape";return false;}bool applied=false;s.edit("Change reference proportions",[&]{applied=applyReferenceShapePreset(*instance,id,error);return applied;});return applied;}
bool Reference3DReviewWindow::applyPosePreset(const QString &id,QString *error){return applyPosePreset(id,ReferenceHandTarget::Both,false,error);}
bool Reference3DReviewWindow::applyPosePreset(const QString &id,ReferenceHandTarget hands,bool mirror,QString *error){auto &s=*m_state;s.resolveEditors();auto *instance=s.selected();if(!instance||instance->locked){if(error)*error="Select an unlocked model to apply a pose";return false;}const bool pet=id.startsWith("pet/");if(pet&&mirror){if(error)*error="Pet study poses have no mirrored variant";return false;}bool applied=false;s.edit("Apply pose preset",[&]{applied=pet?applyReferencePetPosePreset(id,*instance,error):applyReferencePosePreset(id,*instance,error,hands,mirror);return applied;});if(applied)s.status("Pose applied");return applied;}
bool Reference3DReviewWindow::importFile(const QString &path,QString *error){const auto result=importReferenceAsset(path);if(!result){if(error)*error=result.error;return false;}const bool ok=m_state->placeFinalized(result.asset,error);if(ok&&!result.warnings.isEmpty())m_state->status(result.warnings.join(" · "));return ok;}
void Reference3DReviewWindow::importFileAsync(const QString &path){m_state->importAsync(path);}
bool Reference3DReviewWindow::importInProgress()const{return m_state->importing;}
bool Reference3DReviewWindow::saveScene(const QString &path,QString *error){auto &s=*m_state;s.resolveEditors();if(!saveReferenceScene(s.scene,path,error))return false;s.path=path;s.undo->setClean();setWindowModified(false);s.status("Saved self-contained reference scene");return true;}
bool Reference3DReviewWindow::openScene(const QString &path,QString *error){const auto scene=loadReferenceScene(path,error);if(!scene)return false;if(!setScene(*scene,error))return false;auto &s=*m_state;s.path=path;if(const auto *instance=s.selected()){const auto &metadata=instance->asset->metadata;const auto id=metadata.value("referenceCatalogId").toString();int category=-1;for(const auto &entry:s.modelCatalog)if(entry.id==id){category=int(entry.kind);break;}if(category<0&&!metadata.value("referencePetKind").toString().isEmpty())category=int(ReferenceCatalogKind::Pet);if(category>=0){s.modelSearch->clear();s.modelKinds->setCurrentIndex(category);for(int row=0;row<s.modelGallery->count();++row)if(s.modelGallery->item(row)->data(Qt::UserRole).toString()==id){s.modelGallery->setCurrentRow(row);break;}}}s.status("Reference scene opened");return true;}
void Reference3DReviewWindow::selectObject(int index){auto &s=*m_state;if(s.refreshing)return;s.resolveEditors();index=index>=0&&index<s.scene.instances.size()?index:-1;if(s.object==index)return;s.name->clearFocus();s.object=index;s.node=-1;s.refresh();emit selectionChanged(s.object,s.node);}
void Reference3DReviewWindow::selectJoint(int node){auto &s=*m_state;if(s.refreshing)return;s.resolveEditors();const auto *i=s.selected();if(!i||node<0||node>=i->asset->nodes.size())return;s.node=node;s.refreshValues();emit selectionChanged(s.object,s.node);}
bool Reference3DReviewWindow::setJointRotation(const QVector3D &degrees){auto &s=*m_state;auto *i=s.selected();if(!i||i->locked||s.node<0||!std::isfinite(degrees.x())||!std::isfinite(degrees.y())||!std::isfinite(degrees.z()))return false;return s.edit("Pose joint",[&]{const auto rest=i->asset->nodes[s.node].rotation;const auto value=rest*QQuaternion::fromEulerAngles(degrees);if(i->poseRotations.value(s.node,rest)==value)return false;i->poseRotations[s.node]=value;return true;});}
bool Reference3DReviewWindow::setIKTarget(const QVector3D &target,const QVector3D &pole,QString *error){
    auto &s=*m_state;auto *instance=s.selected();const auto chain=s.limb();if(!instance||instance->locked||!chain){if(error)*error="Select an unlocked mapped hand or foot for limb IK";return false;}
    bool solved=false;s.edit("Pose limb",[&]{solved=solveReferenceLimbIK(*instance,(*chain)[0],(*chain)[1],(*chain)[2],target,pole,error);if(solved){s.ikTarget=target;s.ikPole=pole;s.ikObject=instance->id;s.ikNode=s.node;}return solved;});return solved;
}
bool Reference3DReviewWindow::setMorphWeight(int mesh,int target,float weight){auto &s=*m_state;auto *i=s.selected();if(!i||i->locked||mesh<0||mesh>=i->asset->meshes.size()||!std::isfinite(weight)||weight< -1||weight>2)return false;const auto &source=i->asset->meshes[mesh];if(source.primitives.isEmpty()||target<0||target>=source.primitives.first().morphTargets.size())return false;
    return s.edit("Change shape",[&]{bool changed=false;for(int n=0;n<i->asset->nodes.size();++n)if(i->asset->nodes[n].meshIndex==mesh){auto values=weightsFor(*i,n);if(values[target]==weight)continue;values[target]=weight;i->morphOverrides[n]=values;changed=true;}return changed;});}
QString Reference3DReviewWindow::saveMaterial(const QString &name,ReferenceLibraryKind kind,QString *error){
    auto &s=*m_state;s.resolveEditors();ReferenceScene payload=s.scene;
    if(kind!=ReferenceLibraryKind::Scene&&kind!=ReferenceLibraryKind::Camera){const auto *instance=s.selected();if(!instance){if(error)*error="Select a reference to save this material kind";return {};}payload.instances={*instance};}
    const QString id=s.library->addScene(payload,name,kind,error,s.viewport->image());if(!id.isEmpty())s.status("Material saved: "+name);return id;
}
bool Reference3DReviewWindow::applyMaterial(const QString &id,QString *error){
    auto &s=*m_state;s.resolveEditors();const auto *entry=s.library->entry(id);if(!entry){if(error)*error="Choose an existing material";return false;}
    const auto kind=entry->kind;
    if(kind==ReferenceLibraryKind::Scene||kind==ReferenceLibraryKind::Character||kind==ReferenceLibraryKind::Camera){
        const auto payload=s.library->materialScene(id,error);if(!payload)return false;
        if(kind==ReferenceLibraryKind::Camera){s.scene.camera=payload->camera;s.viewport->setScene(s.scene);s.refreshValues();return true;}
        if(kind==ReferenceLibraryKind::Character){if(payload->instances.isEmpty()){if(error)*error="The material contains no character";return false;}if(s.scene.instances.size()>=maxReferenceInstances){if(error)*error="The reference scene is full";return false;}
            auto copy=payload->instances.first();copy.id=QUuid::createUuid().toString(QUuid::WithoutBraces);copy.translation.setX(copy.translation.x()+s.scene.instances.size()*.6f);
            return s.edit("Place character material",[&]{s.scene.instances.append(copy);s.object=s.scene.instances.size()-1;s.node=-1;return true;});
        }
        return s.edit("Apply scene material",[&]{s.scene=*payload;s.object=s.scene.instances.isEmpty()?-1:0;s.node=-1;return true;},false);
    }
    auto *instance=s.selected();if(!instance||instance->locked){if(error)*error="Select an unlocked reference to apply this material";return false;}
    bool applied=false;s.edit("Apply reference material",[&]{applied=s.library->applyToInstance(id,*instance,error);return applied;});return applied;
}
ReferenceMaterialLibrary *Reference3DReviewWindow::materialLibrary()const{return m_state->library;}
QUndoStack *Reference3DReviewWindow::undoStack()const{return m_state->activeUndo();}
QWidget *Reference3DReviewWindow::viewportWidget()const{return m_state->viewport;}
QImage Reference3DReviewWindow::viewportImage()const{return m_state->viewport->image();}
bool Reference3DReviewWindow::viewportReady()const{return m_state->viewport->ready();}
QString Reference3DReviewWindow::viewportError()const{return m_state->viewport->error();}
Reference3DPreviewStats Reference3DReviewWindow::previewStats()const{return m_state->viewport->stats();}
void Reference3DReviewWindow::requestPresetThumbnail(const QString &id,bool pose,bool selectedModel){m_state->thumbnails->request(id,pose,pose&&selectedModel?m_state->poseThumbnailSource:ReferenceAssetPtr());}
QImage Reference3DReviewWindow::presetThumbnail(const QString &id,bool pose,bool selectedModel)const{return m_state->thumbnails->image(id,pose,pose&&selectedModel?m_state->poseThumbnailSource:ReferenceAssetPtr());}
void Reference3DReviewWindow::closeEvent(QCloseEvent *event){m_state->cancelCatalogPlacement();if(m_state->canvasPanel){finishPendingEdits();event->accept();return;}if(!m_state->confirmReplacement()){event->ignore();return;}QMainWindow::closeEvent(event);}
} // namespace toon
