#include "feature_flags.h"
#include "reference3d_canvas.h"
#include "document.h"
#include "property_animation.h"
#include "reference3d_layer_serialization.h"
#include "reference3d_pets.h"
#include "reference3d_render.h"
#include "scene3d.h"
#include "ui_theme.h"
#include <QKeyEvent>
#include <QMouseEvent>
#include <QMutex>
#include <QMutexLocker>
#include <QPainter>
#include <QPointer>
#include <QTabletEvent>
#include <QThread>
#include <QTimer>
#include <QWheelEvent>
#include <QWidget>
#include <algorithm>
#include <cmath>
#include <limits>

namespace toon {
namespace {
constexpr float pi=3.14159265358979323846f;
Camera3D camera3D(const ReferenceCamera &c){Camera3D r;r.target=c.target;r.yaw=c.yaw;r.pitch=c.pitch;r.distance=c.distance;r.fieldOfView=c.fieldOfView;r.orthographic=c.orthographic;r.orthoScale=c.orthoScale;return r;}
bool visibleLayer(const Shot &shot,int index,int frame){
    QSet<QString> seen;
    while(index>=0&&index<shot.layers.size()){
        const auto &l=shot.layers[index];if(!l.visible||l.opacity<=0||seen.contains(l.id)||!layerPresentAtFrame(shot,l.id,frame))return false;
        if(l.parentId.isEmpty())return true;seen.insert(l.id);index=-1;
        for(int i=0;i<shot.layers.size();++i)if(shot.layers[i].id==l.parentId){index=i;break;}
    }return false;
}
bool sameScene(const ReferenceScene&a,const ReferenceScene&b){
    if(a.instances.size()!=b.instances.size())return false;
    for(int i=0;i<a.instances.size();++i)if(a.instances[i].asset!=b.instances[i].asset)return false;
    return referenceLayerScenesEqual(a,b);
}
bool sameRig(const ReferenceAssetPtr&a,const ReferenceAssetPtr&b){
    if(a==b)return true;if(!a||!b||a->nodes.size()!=b->nodes.size()||a->skins.size()!=b->skins.size())return false;
    for(int n=0;n<a->nodes.size();++n)if(a->nodes[n].name!=b->nodes[n].name||a->nodes[n].parentIndex!=b->nodes[n].parentIndex)return false;
    for(int s=0;s<a->skins.size();++s)if(a->skins[s].joints!=b->skins[s].joints)return false;return true;
}
QPointF projection(const QMatrix4x4 &matrix,const QVector3D &point,QSize frame,bool *valid=nullptr){
    const auto p=matrix*QVector4D(point,1);const bool okay=p.w()>.00001f&&p.z()/p.w()>=-1&&p.z()/p.w()<=1;
    if(valid)*valid=okay;if(!okay)return {};
    return {(p.x()/p.w()+1)*.5*frame.width(),(1-p.y()/p.w())*.5*frame.height()};
}
struct PreviewRequest{QString id;ReferenceScene scene;QSize size;bool full=true;};
bool sameRequests(const QVector<PreviewRequest>&a,const QVector<PreviewRequest>&b){
    if(a.size()!=b.size())return false;for(int i=0;i<a.size();++i)if(a[i].id!=b[i].id||a[i].size!=b[i].size||a[i].full!=b[i].full||!sameScene(a[i].scene,b[i].scene))return false;return true;
}
struct PreviewGate{QMutex mutex;Reference3DCanvasController *receiver=nullptr;};
struct PreviewResult{PreviewRequest request;QImage image;QString error;};
}
struct Reference3DCanvasController::State{
    Reference3DCanvasController *owner;QPointer<Document> document;QPointer<QWidget> canvas;
    Reference3DCanvasMode mode=Reference3DCanvasMode::Translate;int axis=-1,object=-1,joint=-1;QString layer,shot;
    QString objectId;ReferenceAssetPtr selectedAsset;
    std::function<bool(const QString&)> visibilityFilter;
    enum Drag{None,Move,Rotate,Scale,Pose,Orbit,Pan,Zoom}drag=None;
    QPointF start,last;ReferenceScene original;QString editLayer;bool syncing=false;
    QTimer timer,idle;QThread *thread=nullptr;QObject *worker=nullptr;bool running=false,hasPending=false;
    QSize frameSize;quint64 generation=0,epoch=0;QVector<PreviewRequest> targets,pending;
    QHash<QString,QImage> images;QHash<QString,PreviewRequest> completed;QString error;
    std::shared_ptr<PreviewGate> gate;std::shared_ptr<ReferenceRenderCache> cache;std::shared_ptr<std::atomic_bool> cancel;
    ReferenceRenderCache overlayCache;
    const RasterLayer *selectedLayer()const{
        if(!document||!document->isOpen())return nullptr;const auto &layers=document->activeShot().layers;const int at=document->activeLayer();
        return at>=0&&at<layers.size()&&layers[at].kind==LayerKind::Reference3D&&layers[at].reference3d?&layers[at]:nullptr;
    }
    const ReferenceScene *scene()const{const auto *l=selectedLayer();return l?&*l->reference3d:nullptr;}
    bool included(const QString &id)const{return !visibilityFilter||visibilityFilter(id);}
    bool interactive()const{return drag!=None||(document&&document->reference3DEditActive());}
    bool available()const{return features::experimental3D&&selectedLayer()&&included(selectedLayer()->id)&&visibleLayer(document->activeShot(),document->activeLayer(),document->currentFrame());}
    bool editable()const{return available()&&layerCanEdit(document->activeShot().layers,document->activeLayer())&&!layerPositionLocked(document->activeShot().layers,document->activeLayer());}
    void rememberSelection(){const auto *s=scene();if(s&&object>=0&&object<s->instances.size()){objectId=s->instances[object].id;selectedAsset=s->instances[object].asset;}else{objectId.clear();selectedAsset.reset();}}
    void notifySelection(){rememberSelection();emit owner->selectionChanged(object,joint);if(canvas)canvas->update();}
    void sync(){
        if(syncing)return;syncing=true;const auto *l=selectedLayer();const QString next=l?l->id:QString();const auto nextShot=document&&document->isOpen()?document->activeShotId():QString();
        if(drag!=None&&(next!=editLayer||nextShot!=shot||!editable()||!document->reference3DEditActive()))finish(true);
        const int oldObject=object,oldJoint=joint;const bool changed=next!=layer||nextShot!=shot;layer=next;shot=nextShot;
        const auto *s=scene();if(changed){object=s&&!s->instances.isEmpty()?0:-1;joint=-1;}
        else if(s&&!objectId.isEmpty()){int found=-1;for(int i=0;i<s->instances.size();++i)if(s->instances[i].id==objectId){found=i;break;}if(found>=0)object=found;else{object=s->instances.isEmpty()?-1:0;joint=-1;}}
        if(!s||object>=s->instances.size()){object=s&&!s->instances.isEmpty()?0:-1;joint=-1;}
        if(s&&object>=0&&!sameRig(selectedAsset,s->instances[object].asset))joint=-1;
        if(s&&object>=0&&(joint>=s->instances[object].asset->nodes.size()))joint=-1;
        rememberSelection();
        syncing=false;if(changed||object!=oldObject||joint!=oldJoint)notifySelection();
        if(document&&document->isOpen()){if(frameSize.isEmpty())frameSize={document->project().width,document->project().height};prepare(frameSize);}
        if(s||changed)emit owner->sceneEdited();
    }
    bool begin(Drag kind){
        if(!editable()||!scene()||document->editingGestureActive()||document->layerPropertyEditActive())return false;
        if(kind!=Orbit&&kind!=Pan&&kind!=Zoom&&(object<0||object>=scene()->instances.size()||scene()->instances[object].locked))return false;
        QString message;if(!document->beginReference3DEdit(layer,&message)){emit owner->warning(message);return false;}
        original=*scene();editLayer=layer;drag=kind;idle.stop();emit owner->interactionChanged(true);prepare(frameSize);return true;
    }
    void finish(bool abort){
        if(drag==None)return;drag=None;idle.stop();
        if(document&&document->reference3DEditActive()){if(abort)document->cancelReference3DEdit();else document->commitReference3DEdit();}
        editLayer.clear();++epoch;if(cancel)cancel->store(true);targets.clear();prepare(frameSize);emit owner->interactionChanged(false);
    }
    bool preview(const ReferenceScene &s){QString message;if(!document||!document->previewReference3DScene(s,&message)){if(!message.isEmpty())emit owner->warning(message);finish(true);return false;}return true;}
    QTransform layerTransform()const{return document&&selectedLayer()&&!document->keyframeSourceEditing()?worldLayerTransform(document->activeShot(),selectedLayer()->id,document->currentFrame()):QTransform();}
    QPointF local(QPointF point,const QTransform &art,bool *okay=nullptr)const{
        bool a=false,b=false;const auto view=art.inverted(&a),layer=layerTransform().inverted(&b);if(okay)*okay=a&&b;return a&&b?layer.map(view.map(point)):QPointF();
    }
    QPointF screen(QPointF point,const QTransform &art)const{return art.map(layerTransform().map(point));}
    QPointF worldPoint(const QVector3D &point,const QTransform &art,QSize size,bool *valid=nullptr)const{return screen(projection(referenceViewProjection(scene()->camera,size),point,size,valid),art);}
    QVector<ReferenceJointHandle> joints()const{
        QVector<ReferenceJointHandle> result;const auto *s=scene();if(!s||object<0||object>=s->instances.size())return result;
        const auto &i=s->instances[object];if(!i.visible)return result;const auto globals=evaluateReferenceNodeGlobals(i);if(!globals)return result;
        QSet<int> nodes;for(const auto &skin:i.asset->skins)for(int n:skin.joints)nodes.insert(n);for(auto it=i.asset->humanoidBones.cbegin();it!=i.asset->humanoidBones.cend();++it)nodes.insert(it.value());
        for(int n:nodes)if(n>=0&&n<i.asset->nodes.size())result.append({object,n,i.asset->nodes[n].parentIndex,(*globals)[n].map(QVector3D()),i.asset->nodes[n].name});return result;
    }
    ReferenceBounds objectBounds(){
        ReferenceBounds result;const auto *s=scene();if(!s||object<0||object>=s->instances.size())return result;
        const auto &i=s->instances[object];const auto geometry=overlayCache.geometry(i);if(!geometry||!geometry->geometry.bounds.valid)return result;
        const auto &b=geometry->geometry.bounds;const auto placement=referencePlacementMatrix(i);
        for(int x=0;x<2;++x)for(int y=0;y<2;++y)for(int z=0;z<2;++z)result.include(placement.map({x?b.maximum.x():b.minimum.x(),y?b.maximum.y():b.minimum.y(),z?b.maximum.z():b.minimum.z()}));return result;
    }
    int pickObject(QPointF point,const QTransform &art,QSize size){
        const auto *s=scene();if(!s)return -1;bool okay;const auto p=local(point,art,&okay);if(!okay||!QRectF(QPointF(),size).contains(p))return -1;
        bool invertible;const auto inverse=referenceViewProjection(s->camera,size).inverted(&invertible);if(!invertible)return -1;
        const float x=float(2*p.x()/size.width()-1),y=float(1-2*p.y()/size.height());const auto near=(inverse*QVector4D(x,y,-1,1)).toVector3DAffine(),far=(inverse*QVector4D(x,y,1,1)).toVector3DAffine();const auto direction=(far-near).normalized();
        float nearest=std::numeric_limits<float>::max();int found=-1;
        for(int o=0;o<s->instances.size();++o){const auto &i=s->instances[o];if(!i.visible)continue;const auto geometry=evaluateReferenceInstance(i);if(!geometry)continue;
            for(const auto &primitive:geometry->primitives)for(int t=0;t+2<primitive.indices.size();t+=3){
                const auto &a=primitive.vertices[primitive.indices[t]],&b=primitive.vertices[primitive.indices[t+1]],&c=primitive.vertices[primitive.indices[t+2]];
                const auto ab=b.position-a.position,ac=c.position-a.position,h=QVector3D::crossProduct(direction,ac);const float determinant=QVector3D::dotProduct(ab,h);if(std::abs(determinant)<1e-10f)continue;
                const float inv=1/determinant;const auto relative=near-a.position;const float u=inv*QVector3D::dotProduct(relative,h);if(u<0||u>1)continue;
                const auto q=QVector3D::crossProduct(relative,ab);const float v=inv*QVector3D::dotProduct(direction,q);if(v<0||u+v>1)continue;const float distance=inv*QVector3D::dotProduct(ac,q);if(distance<0||distance>=nearest)continue;
                if(primitive.materialIndex>=0&&primitive.materialIndex<i.asset->materials.size()){
                    const auto &material=i.asset->materials[primitive.materialIndex];float alpha=material.baseColor.alphaF()*((1-u-v)*a.color.w()+u*b.color.w()+v*c.color.w());
                    if(s->textured&&!material.baseColorTexture.isNull()){const auto uv=(1-u-v)*a.uv+u*b.uv+v*c.uv;const auto &texture=material.baseColorTexture;alpha*=qAlpha(texture.pixel(qMin(texture.width()-1,int((uv.x()-std::floor(uv.x()))*texture.width())),qMin(texture.height()-1,int((uv.y()-std::floor(uv.y()))*texture.height()))))/255.f;}
                    if((material.alphaMode==ReferenceAlphaMode::Mask&&alpha<material.alphaCutoff)||(material.alphaMode==ReferenceAlphaMode::Blend&&alpha<.01f))continue;
                }
                nearest=distance;found=o;
            }
        }return found;
    }
    bool press(QPointF point,Qt::MouseButton button,Qt::KeyboardModifiers modifiers,const QTransform &art,QSize size){
        if(!available()||button==Qt::NoButton)return false;owner->prepareLayerPreviews(size);if(canvas)canvas->setFocus();
        if(drag!=None)finish(false);start=last=point;
        if(!editable()){emit owner->warning("Show and unlock the 3D reference layer before posing.");return true;}
        if(button==Qt::RightButton||modifiers.testFlag(Qt::AltModifier))return begin(Orbit);
        if(button==Qt::MiddleButton)return begin(Pan);if(button!=Qt::LeftButton)return false;
        if(mode==Reference3DCanvasMode::Camera)return begin(Orbit);
        if(mode==Reference3DCanvasMode::Pose){
            int picked=-1;double closest=14*14;for(const auto &h:joints()){bool visible;const auto p=worldPoint(h.world,art,size,&visible);if(!visible)continue;const double d=QLineF(p,point).length();if(d*d<closest){closest=d*d;picked=h.node;}if(h.node==joint&&std::abs(d-46)<10){picked=joint;closest=0;}}
            if(picked>=0){joint=picked;notifySelection();return begin(Pose);}
        }
        if(object>=0&&(mode==Reference3DCanvasMode::Rotate||mode==Reference3DCanvasMode::Scale)){
            const auto bounds=objectBounds();if(bounds.valid){const auto center=worldPoint(bounds.center(),art,size);if(mode==Reference3DCanvasMode::Rotate&&std::abs(QLineF(center,point).length()-46)<10)return begin(Rotate);
                if(mode==Reference3DCanvasMode::Scale){QPolygonF hull;for(int x=0;x<2;++x)for(int y=0;y<2;++y)for(int z=0;z<2;++z){bool valid;const auto p=worldPoint({x?bounds.maximum.x():bounds.minimum.x(),y?bounds.maximum.y():bounds.minimum.y(),z?bounds.maximum.z():bounds.minimum.z()},art,size,&valid);if(valid)hull.append(p);}const auto r=hull.boundingRect();for(const auto &p:{r.topLeft(),r.topRight(),r.bottomLeft(),r.bottomRight()})if(QLineF(p,point).length()<12)return begin(Scale);}
            }
        }
        const int picked=pickObject(point,art,size);if(picked!=object){object=picked;joint=-1;notifySelection();}
        if(object<0)return true;if(scene()->instances[object].locked){emit owner->warning("Unlock the selected model before posing.");return true;}
        if(mode==Reference3DCanvasMode::Pose)return true;
        return begin(mode==Reference3DCanvasMode::Rotate?Rotate:mode==Reference3DCanvasMode::Scale?Scale:Move);
    }
    bool move(QPointF point,const QTransform &art,QSize size){
        if(drag==None)return available();if(!editable()||!scene()||layer!=editLayer){finish(true);return true;}
        const auto previous=local(last,art),current=local(point,art);const QPointF delta=current-previous;last=point;auto next=*scene();
        if(drag==Rotate||drag==Pose){const float degrees=float(point.x()-start.x()-point.y()+start.y())*.65f;const int selectedAxis=axis<0?2:axis;QVector3D rotationAxis;rotationAxis[selectedAxis]=1;
            if(object<0||object>=next.instances.size()){finish(true);return true;}auto &i=next.instances[object];const auto &base=original.instances[object];
            if(drag==Rotate)i.rotation=base.rotation*QQuaternion::fromAxisAndAngle(rotationAxis,degrees);
            else if(joint>=0&&joint<i.asset->nodes.size())i.poseRotations[joint]=base.poseRotations.value(joint,i.asset->nodes[joint].rotation)*QQuaternion::fromAxisAndAngle(rotationAxis,degrees);
        }else if(drag==Scale){
            const float factor=std::exp(float((point.x()-start.x())-(point.y()-start.y()))*.007f);auto &i=next.instances[object];const auto base=original.instances[object].scale;
            for(int a=0;a<3;++a)if(axis<0||axis==a)i.scale[a]=qBound(.001f,base[a]*factor,1000.f);
        }else if(drag==Orbit){next.camera.yaw-=float(point.x()-start.x())*.45f;next.camera.pitch=qBound(-85.f,next.camera.pitch+float(point.y()-start.y())*.45f,85.f);start=point;}
        else{
            const float span=next.camera.orthographic?next.camera.orthoScale:2*next.camera.distance*std::tan(next.camera.fieldOfView*pi/360);
            const auto world=cameraViewMatrix(camera3D(next.camera)).inverted().mapVector(QVector3D(delta.x(),-delta.y(),0))*span/qMax(1,size.height());
            if(drag==Pan)next.camera.target-=world;else if(drag==Move){auto movement=world;if(axis>=0)for(int a=0;a<3;++a)if(a!=axis)movement[a]=0;next.instances[object].translation+=movement;}
        }preview(next);return true;
    }
    void prepare(QSize size){
        if(!document||!document->isOpen()||size.isEmpty())return;frameSize=size;QVector<PreviewRequest> next;QSet<QString> keep;const bool live=interactive();
        const QString edited=editLayer.isEmpty()?layer:editLayer;
        const auto &layers=document->activeShot().layers;int visibleReferences=0;for(int i=0;i<layers.size();++i)if(layers[i].reference3d&&included(layers[i].id)&&visibleLayer(document->activeShot(),i,document->currentFrame()))++visibleReferences;
        // Keep every visible layer available while bounding the total display
        // cache to 64 MiB; exports use the independent canonical renderer.
        const qint64 allowedPixels=16LL*1024*1024/qMax(1,visibleReferences);
        for(int i=0;i<layers.size();++i)if(layers[i].reference3d&&included(layers[i].id)&&visibleLayer(document->activeShot(),i,document->currentFrame())){
            const bool full=!live||layers[i].id!=edited;const auto limit=full?QSize(1280,960):QSize(640,480);
            auto pixels=(size.width()>limit.width()||size.height()>limit.height()?size.scaled(limit,Qt::KeepAspectRatio):size).expandedTo({1,1});
            const qint64 requestedPixels=qint64(pixels.width())*pixels.height();if(requestedPixels>allowedPixels){const double scale=std::sqrt(double(allowedPixels)/requestedPixels);pixels={qMax(1,int(pixels.width()*scale)),qMax(1,int(pixels.height()*scale))};}
            next.append({layers[i].id,*layers[i].reference3d,pixels,full});keep.insert(layers[i].id);if(!images.contains(layers[i].id))images.insert(layers[i].id,{});
        }
        for(auto it=images.begin();it!=images.end();)if(!keep.contains(it.key())){completed.remove(it.key());it=images.erase(it);}else ++it;
        if(sameRequests(next,targets))return;
        bool structure=next.size()!=targets.size();if(!structure)for(int i=0;i<next.size();++i)if(next[i].id!=targets[i].id){structure=true;break;}
        if(structure||!live){++epoch;if(cancel)cancel->store(true);}targets=next;++generation;pending.clear();
        for(const auto &r:next){const auto old=completed.constFind(r.id);if(old==completed.cend()||old->size!=r.size||old->full!=r.full||!sameScene(old->scene,r.scene))pending.append(r);}
        hasPending=!pending.isEmpty();if(hasPending&&!running&&!timer.isActive())timer.start();
    }
    void dispatch(){
        if(running||!hasPending)return;running=true;hasPending=false;const auto requests=std::move(pending);const auto jobGeneration=generation,jobEpoch=epoch;const bool full=!interactive();
        cancel=std::make_shared<std::atomic_bool>(false);const auto stopped=cancel;const auto sharedCache=cache;const auto sharedGate=gate;
        QMetaObject::invokeMethod(worker,[requests,stopped,sharedCache,sharedGate,jobGeneration,jobEpoch,full]{
            QVector<PreviewResult> results;for(const auto &request:requests){if(stopped->load())break;PreviewResult result;result.request=request;
                try{auto frame=renderReferenceScene(request.scene,request.size,stopped,*sharedCache);result.image=std::move(frame.image);result.error=frame.error;}catch(...){result.error="The 3D reference preview could not be prepared.";}results.append(std::move(result));}
            QMutexLocker lock(&sharedGate->mutex);if(sharedGate->receiver)QMetaObject::invokeMethod(sharedGate->receiver,[sharedGate,results=std::move(results),jobGeneration,jobEpoch,full]()mutable{
                auto *receiver=sharedGate->receiver;if(!receiver)return;auto &s=*receiver->m_state;s.running=false;const bool current=jobGeneration==s.generation;const bool live=s.interactive()&&!full;
                if(jobEpoch==s.epoch&&(current||live)){s.error.clear();for(auto &result:results)if(s.images.contains(result.request.id)){s.images[result.request.id]=std::move(result.image);s.completed[result.request.id]=result.request;if(!result.error.isEmpty())s.error=result.error;}if(!s.error.isEmpty())emit receiver->warning(s.error);emit receiver->previewChanged();if(s.canvas)s.canvas->update();}
                if(s.hasPending&&!s.timer.isActive())s.timer.start();
            },Qt::QueuedConnection);
        },Qt::QueuedConnection);
    }
};

Reference3DCanvasController::Reference3DCanvasController(Document *document,QWidget *canvas):QObject(canvas),m_state(std::make_unique<State>()){
    auto &s=*m_state;s.owner=this;s.document=document;s.canvas=canvas;s.gate=std::make_shared<PreviewGate>();s.gate->receiver=this;s.cache=std::make_shared<ReferenceRenderCache>();
    s.overlayCache.cacheLimit=16LL*1024*1024;s.thread=new QThread;s.worker=new QObject;s.worker->moveToThread(s.thread);connect(s.thread,&QThread::finished,s.worker,&QObject::deleteLater);connect(s.thread,&QThread::finished,s.thread,&QObject::deleteLater);s.thread->start();
    s.timer.setSingleShot(true);s.timer.setInterval(0);connect(&s.timer,&QTimer::timeout,this,[this]{m_state->dispatch();});s.idle.setSingleShot(true);s.idle.setInterval(120);connect(&s.idle,&QTimer::timeout,this,[this]{m_state->finish(false);});
    if(document){for(auto signal:{&Document::changed,&Document::selectionChanged,&Document::loaded,&Document::shotChanged,&Document::frameChanged})connect(document,signal,this,[this]{m_state->sync();});connect(document,&Document::historyNavigationStarting,this,&Reference3DCanvasController::cancelInteraction);}
    s.sync();
}
Reference3DCanvasController::~Reference3DCanvasController(){auto &s=*m_state;{QMutexLocker lock(&s.gate->mutex);s.gate->receiver=nullptr;}if(s.cancel)s.cancel->store(true);s.thread->quit();if(s.document){QObject::disconnect(s.document,nullptr,this,nullptr);if(s.drag!=State::None&&s.document->reference3DEditActive())s.document->cancelReference3DEdit();}}
bool Reference3DCanvasController::available()const{return m_state->available();}
bool Reference3DCanvasController::editable()const{return m_state->editable();}
bool Reference3DCanvasController::interactionActive()const{return m_state->drag!=State::None;}
QString Reference3DCanvasController::layerId()const{const auto *l=m_state->selectedLayer();return l?l->id:QString();}
const ReferenceScene *Reference3DCanvasController::scene()const{return m_state->scene();}
Reference3DCanvasMode Reference3DCanvasController::mode()const{return m_state->mode;}
void Reference3DCanvasController::setMode(Reference3DCanvasMode mode){if(m_state->mode==mode)return;cancelInteraction();m_state->mode=mode;emit modeChanged();if(m_state->canvas)m_state->canvas->update();}
int Reference3DCanvasController::axis()const{return m_state->axis;}
void Reference3DCanvasController::setAxis(int axis){if(axis< -1||axis>2||axis==m_state->axis)return;cancelInteraction();m_state->axis=axis;emit modeChanged();if(m_state->canvas)m_state->canvas->update();}
int Reference3DCanvasController::selectedObject()const{return m_state->object;}
int Reference3DCanvasController::selectedJoint()const{return m_state->joint;}
void Reference3DCanvasController::selectObject(int object){auto &s=*m_state;if(!s.scene()||object<0||object>=s.scene()->instances.size())object=-1;if(object==s.object)return;cancelInteraction();s.object=object;s.joint=-1;s.notifySelection();}
void Reference3DCanvasController::selectJoint(int joint){auto &s=*m_state;if(!s.scene()||s.object<0||joint<0||joint>=s.scene()->instances[s.object].asset->nodes.size())joint=-1;if(joint==s.joint)return;cancelInteraction();s.joint=joint;s.notifySelection();}
bool Reference3DCanvasController::setScene(const ReferenceScene &scene,QString *error){return m_state->document&&m_state->editable()&&m_state->document->setReference3DScene(layerId(),scene,error);}
bool Reference3DCanvasController::setJointRotation(const QVector3D &degrees,QString *error){auto &s=*m_state;if(!s.editable()||!s.scene()||s.object<0||s.joint<0)return false;auto next=*s.scene();auto &i=next.instances[s.object];if(i.locked)return false;i.poseRotations[s.joint]=i.asset->nodes[s.joint].rotation*QQuaternion::fromEulerAngles(degrees);return setScene(next,error);}
bool Reference3DCanvasController::applyPosePreset(const QString &id,QString *error,ReferenceHandTarget hands,bool mirror){auto &s=*m_state;if(!s.editable()||!s.scene()||s.object<0)return false;auto next=*s.scene();const bool okay=id.startsWith("pet/")?!mirror&&applyReferencePetPosePreset(id,next.instances[s.object],error):applyReferencePosePreset(id,next.instances[s.object],error,hands,mirror);return okay&&setScene(next,error);}
void Reference3DCanvasController::cancelInteraction(){m_state->finish(true);}
void Reference3DCanvasController::finishInteraction(bool cancelled){m_state->finish(cancelled);}
void Reference3DCanvasController::setLayerVisibilityFilter(std::function<bool(const QString &)> filter){auto &s=*m_state;s.visibilityFilter=std::move(filter);if(interactionActive()&&!available())cancelInteraction();s.prepare(s.frameSize);if(s.canvas)s.canvas->update();}
void Reference3DCanvasController::prepareLayerPreviews(QSize frameSize){m_state->prepare(frameSize);}
QHash<QString,QImage> Reference3DCanvasController::preparedLayerPreviews()const{return m_state->images;}
bool Reference3DCanvasController::previewReady()const{return !m_state->running&&!m_state->hasPending&&!m_state->timer.isActive();}
QString Reference3DCanvasController::previewError()const{return m_state->error;}
void Reference3DCanvasController::paintOverlay(QPainter &p,const QTransform &art,QSize frame){
    auto &s=*m_state;if(!s.available()||!s.scene()||s.object<0)return;const auto &instance=s.scene()->instances[s.object];if(!instance.visible)return;
    p.save();p.setRenderHint(QPainter::Antialiasing);const bool unlocked=s.editable()&&!instance.locked;
    if(s.mode==Reference3DCanvasMode::Pose){const auto handles=s.joints();for(const auto &h:handles){bool valid;const auto a=s.worldPoint(h.world,art,frame,&valid);if(!valid)continue;for(const auto &parent:handles)if(parent.node==h.parent){bool okay;const auto b=s.worldPoint(parent.world,art,frame,&okay);if(okay){p.setPen(QPen(QColor(ui::muted),1));p.drawLine(a,b);}break;}
            const bool chosen=h.node==s.joint;p.setPen(QPen(QColor(ui::canvas),2));p.setBrush(QColor(chosen&&unlocked?ui::focus:ui::muted));p.drawEllipse(a,chosen?6:4,chosen?6:4);if(chosen){static const QColor colors[]{QColor(233,125,125),QColor(142,202,147),QColor(140,173,232)};p.setBrush(Qt::NoBrush);p.setPen(QPen(unlocked?colors[s.axis<0?2:s.axis]:QColor(ui::muted),3));p.drawEllipse(a,46,46);p.setPen(QColor(ui::text));p.drawText(a+QPointF(12,-12),h.name);}}
    }else if(s.mode!=Reference3DCanvasMode::Camera){
        const auto b=s.objectBounds();if(b.valid){QPolygonF hull;for(int x=0;x<2;++x)for(int y=0;y<2;++y)for(int z=0;z<2;++z){bool valid;const auto point=s.worldPoint({x?b.maximum.x():b.minimum.x(),y?b.maximum.y():b.minimum.y(),z?b.maximum.z():b.minimum.z()},art,frame,&valid);if(valid)hull.append(point);}const auto rect=hull.boundingRect();p.setPen(QPen(QColor(unlocked?ui::focus:ui::muted),1,Qt::DashLine));p.setBrush(Qt::NoBrush);p.drawRect(rect);
            bool centerValid;const auto center=s.worldPoint(b.center(),art,frame,&centerValid);if(centerValid){if(s.mode==Reference3DCanvasMode::Rotate)p.drawEllipse(center,46,46);else if(s.mode==Reference3DCanvasMode::Scale){p.setBrush(QColor(ui::focus));for(const auto &point:{rect.topLeft(),rect.topRight(),rect.bottomLeft(),rect.bottomRight()})p.drawRect(QRectF(point-QPointF(4,4),QSizeF(8,8)));}else{const float span=(b.maximum-b.minimum).length()*.18f;static const QColor colors[]{QColor(233,125,125),QColor(142,202,147),QColor(140,173,232)};for(int a=0;a<3;++a){QVector3D v;v[a]=span;const auto end=s.worldPoint(b.center()+v,art,frame);p.setPen(QPen(colors[a],s.axis==a?3:2));p.drawLine(center,end);p.drawText(end+QPointF(4,-4),QString("XYZ")[a]);}}}}
    }p.restore();
}
bool Reference3DCanvasController::mousePress(QMouseEvent *event,const QTransform &art,QSize size){const bool used=m_state->press(event->position(),event->button(),event->modifiers(),art,size);if(used)event->accept();return used;}
bool Reference3DCanvasController::mouseMove(QMouseEvent *event,const QTransform &art,QSize size){const bool used=m_state->move(event->position(),art,size);if(used)event->accept();return used;}
bool Reference3DCanvasController::mouseRelease(QMouseEvent *event,const QTransform &,QSize){if(!available()&&!interactionActive())return false;m_state->finish(false);event->accept();return true;}
bool Reference3DCanvasController::wheel(QWheelEvent *event,const QTransform &,QSize size){auto &s=*m_state;if(!s.available())return false;if(!s.editable())return true;if(s.drag!=State::Zoom){s.finish(false);if(!s.begin(State::Zoom))return true;}auto next=*s.scene();const float factor=std::exp(-event->angleDelta().y()/900.f);if(next.camera.orthographic)next.camera.orthoScale=qBound(.01f,next.camera.orthoScale*factor,10000.f);else next.camera.distance=qBound(.02f,next.camera.distance*factor,10000.f);s.preview(next);s.prepare(size);s.idle.start();event->accept();return true;}
bool Reference3DCanvasController::keyPress(QKeyEvent *event){if(event->key()==Qt::Key_Escape&&interactionActive()){cancelInteraction();event->accept();return true;}if(!available()||(event->modifiers()&(Qt::ControlModifier|Qt::MetaModifier|Qt::AltModifier)))return false;const int axis=event->key()==Qt::Key_X?0:event->key()==Qt::Key_Y?1:event->key()==Qt::Key_Z?2:-1;if(axis>=0){setAxis(axis);event->accept();return true;}return false;}
bool Reference3DCanvasController::tablet(QTabletEvent *event,const QTransform &art,QSize size){bool used=false;if(event->type()==QEvent::TabletPress)used=m_state->press(event->position(),event->button()==Qt::NoButton?Qt::LeftButton:event->button(),event->modifiers(),art,size);else if(event->type()==QEvent::TabletMove)used=m_state->move(event->position(),art,size);else if(event->type()==QEvent::TabletRelease){used=available()||interactionActive();m_state->finish(false);}if(used)event->accept();return used;}
} // namespace toon
