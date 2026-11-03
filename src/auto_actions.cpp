#include "auto_actions.h"
#include "raster_tools.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>
#include <QUuid>
#include <cmath>

namespace toon {
namespace {
constexpr qint64 maxBytes = 4 * 1024 * 1024;
bool keys(const QJsonObject &o, const QStringList &allowed) {
    for(auto it=o.begin();it!=o.end();++it)if(!allowed.contains(it.key()))return false;
    return true;
}
bool integer(const QJsonValue &v, int low, int high) {
    return v.isDouble() && std::isfinite(v.toDouble()) && v.toDouble()==std::floor(v.toDouble()) && v.toDouble()>=low && v.toDouble()<=high;
}
bool real(const QJsonValue &v,qreal low,qreal high) {
    return v.isDouble()&&std::isfinite(v.toDouble())&&v.toDouble()>=low&&v.toDouble()<=high;
}
bool validColor(const QJsonValue &v) {
    if(!v.isString())return false;const auto text=v.toString();
    if(text.size()!=9||text[0]!='#')return false;
    for(const auto ch:text.mid(1))if(!QStringLiteral("0123456789abcdefABCDEF").contains(ch))return false;
    return QColor(text).isValid();
}
bool pixelOperation(const QString &operation) {
    return operation=="fill"||operation=="bucket"||operation=="transform";
}
bool sameSelection(const RasterSelection &a,const RasterSelection &b) {
    return a.active==b.active&&a.path==b.path&&a.featherRadius==b.featherRadius;
}
bool sameProject(const Project &a,const Project &b) {
    if(a.toJson()!=b.toJson())return false;
    for(int s=0;s<a.shots.size();++s)for(int i=0;i<a.shots[s].layers.size();++i) {
        const auto &x=a.shots[s].layers[i],&y=b.shots[s].layers[i];
        if(x.tiles!=y.tiles||x.mask!=y.mask)return false;
    }
    return true;
}
QJsonObject properties(const RasterLayer &l) {
    return {{"name",l.name},{"visible",l.visible},{"locked",l.locked},{"annotation",l.annotation},
        {"opacity",l.opacity},{"blend",layerBlendModeKey(l.blendMode)},{"alphaLocked",l.alphaLocked},
        {"positionLocked",l.positionLocked},{"clipped",l.clipped},{"effects",layerEffectsToJson(l.effects)}};
}
bool validStep(const AutoActionStep &s) {
    if(s.label.trimmed().isEmpty() || s.label.size()>160)return false;
    const auto &p=s.parameters;
    if(s.operation=="fill")return p.size()==1&&validColor(p["color"]);
    if(s.operation=="transform")return p.size()==5&&real(p["translationX"],-32768,32768)&&real(p["translationY"],-32768,32768)
        &&real(p["scaleX"],.01,16)&&real(p["scaleY"],.01,16)&&real(p["rotation"],-180,180);
    if(s.operation=="bucket") {
        if(!keys(p,{"x","y","color","source","tolerance","gapClosing","referenceOffset"})
            ||!real(p["x"],-32768,32768)||!real(p["y"],-32768,32768)||!validColor(p["color"])
            ||!integer(p["source"],0,2)||!integer(p["tolerance"],0,255)||!integer(p["gapClosing"],0,20))return false;
        return p["source"].toInt()==2 ? p.size()==7&&integer(p["referenceOffset"],1-maxAnimationLayers,maxAnimationLayers-1) : p.size()==6&&!p.contains("referenceOffset");
    }
    if(s.operation=="selectRelative")return p.size()==1 && integer(p["offset"],-1024,1024) && p["offset"].toInt()!=0;
    if(s.operation=="newLayer")return p.size()==1 && p["annotation"].isBool();
    if(s.operation=="group"||s.operation=="duplicate"||s.operation=="delete")return p.isEmpty();
    if(s.operation!="properties"||p.isEmpty()||!keys(p,{"name","visible","locked","annotation","opacity","blend","alphaLocked","positionLocked","clipped","effects"}))return false;
    for(auto it=p.begin();it!=p.end();++it) {
        if(it.key()=="name") {if(!it->isString()||it->toString().trimmed().isEmpty()||it->toString().size()>100)return false;}
        else if(it.key()=="opacity") {if(!it->isDouble()||!std::isfinite(it->toDouble())||it->toDouble()<0||it->toDouble()>1)return false;}
        else if(it.key()=="blend") {if(!it->isString()||!layerBlendModeFromKey(it->toString()))return false;}
        else if(it.key()=="effects") {LayerEffects e;QString error;if(!it->isObject()||!layerEffectsFromJson(it->toObject(),&e,&error))return false;}
        else if(!it->isBool())return false;
    }
    return true;
}
const RasterLayer *findLayer(const Project &p,const QString &id,int *shot=nullptr) {
    for(int i=0;i<p.shots.size();++i)for(const auto &l:p.shots[i].layers)if(l.id==id){if(shot)*shot=i;return &l;}
    return nullptr;
}
bool applyStep(Document &d,const AutoActionStep &s,QString *error) {
    const auto p=s.parameters;
    const auto active=[&]()->const RasterLayer& {return d.activeShot().layers[d.activeLayer()];};
    const int count=d.activeShot().layers.size(); const auto originalId=active().id;
    if(s.operation=="fill") {
        const bool whole=!d.rasterSelection().active;
        if(whole)d.selectAllPixels();
        const bool ok=d.fillRasterSelection(QColor(p["color"].toString()));
        if(whole)d.clearRasterSelection();
        return ok;
    }
    if(s.operation=="bucket") {
        RasterFillSettings settings;settings.source=RasterSampleSource(p["source"].toInt());
        settings.tolerance=p["tolerance"].toInt();settings.gapClosing=p["gapClosing"].toInt();
        if(settings.source==RasterSampleSource::ReferenceLayer) {
            const int target=d.activeLayer()+p["referenceOffset"].toInt();
            if(target<0||target>=count){*error="The recorded reference layer is outside this layer stack.";return false;}
            settings.referenceLayerId=d.activeShot().layers[target].id;
        }
        return RasterTools::bucket(&d,{p["x"].toDouble(),p["y"].toDouble()},QColor(p["color"].toString()),settings);
    }
    if(s.operation=="transform") {
        TransformParameters transform;transform.translation={p["translationX"].toDouble(),p["translationY"].toDouble()};
        transform.scaleX=p["scaleX"].toDouble();transform.scaleY=p["scaleY"].toDouble();transform.rotation=p["rotation"].toDouble();
        if(!d.beginTransform())return false;
        if(!d.previewTransform(transform)){d.cancelTransform();return false;}
        return d.applyTransform();
    }
    if(s.operation=="selectRelative") {
        const int destination=d.activeLayer()+p["offset"].toInt();
        if(destination<0||destination>=count){*error="The relative layer selection is outside this layer stack.";return false;}
        d.selectLayer(destination);return d.activeLayer()==destination;
    }
    if(s.operation=="newLayer") {d.addLayer(p["annotation"].toBool());return d.activeShot().layers.size()==count+1;}
    if(s.operation=="group") {d.addGroup();return d.activeShot().layers.size()==count+1&&active().group;}
    if(s.operation=="duplicate") {
        if(active().group){*error="This action duplicates one drawing layer; select a drawing layer.";return false;}
        d.duplicateLayers({originalId});return d.activeShot().layers.size()==count+1;
    }
    if(s.operation=="delete") {
        if(active().group){*error="This action deletes one drawing layer; select a drawing layer.";return false;}
        d.deleteLayer();return d.activeShot().layers.size()==count-1;
    }
    if(s.operation!="properties")return false;
    // Explicit unlocking precedes edits. Every other field preserves the target's
    // current value unless that field was actually changed while recording.
    if(p.contains("locked")&&!p["locked"].toBool())d.setLayersLocked({originalId},false);
    if(p.contains("name")||p.contains("visible")||p.contains("annotation")||p.contains("opacity")) {
        const auto l=active();
        d.changeLayer(p.contains("name")?p["name"].toString():l.name,
            p.contains("visible")?p["visible"].toBool():l.visible,l.locked,
            p.contains("annotation")?p["annotation"].toBool():l.annotation,
            p.contains("opacity")?p["opacity"].toDouble():l.opacity);
    }
    if(p.contains("blend"))d.setLayerBlendMode(*layerBlendModeFromKey(p["blend"].toString()));
    if(p.contains("alphaLocked"))d.setLayerAlphaLocked(p["alphaLocked"].toBool());
    if(p.contains("positionLocked"))d.setLayerPositionLocked(p["positionLocked"].toBool());
    if(p.contains("clipped"))d.setLayerClipped(p["clipped"].toBool());
    if(p.contains("effects")){LayerEffects e;if(!layerEffectsFromJson(p["effects"].toObject(),&e,error))return false;d.setLayerEffects(originalId,e);}
    if(p.contains("locked")&&p["locked"].toBool())d.setLayersLocked({originalId},true);
    const auto result=properties(active());
    for(auto it=p.begin();it!=p.end();++it)if(result[it.key()]!=it.value())return false;
    return true;
}
}
AutoActions::AutoActions(Document *document,QString path,QObject *parent):QObject(parent),m_document(document),m_path(std::move(path)) {
    if(m_path.isEmpty())m_path=QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)+"/auto-actions.json";
    if(!document)return;
    connect(document,&Document::historyNavigationStarting,this,[this]{if(recording())halt("Recording stopped by history navigation. Recorded steps were kept.");});
    connect(document,&Document::operationCommitted,this,&AutoActions::committed);
    connect(document,&Document::operationCommitted,this,&AutoActions::stateChanged);
    connect(document,&Document::loaded,this,[this]{if(recording())halt("Recording stopped because the document changed.");emit stateChanged();});
    connect(document,&Document::shotChanged,this,[this]{if(recording()&&!m_document->committing())halt("Recording stopped because the shot changed.");emit stateChanged();});
    connect(document,&Document::frameChanged,this,[this]{if(recording()&&!m_document->committing())halt("Recording stopped because the frame changed.");emit stateChanged();});
    connect(document,&Document::changed,this,&AutoActions::stateChanged);
    connect(document,&Document::transformChanged,this,&AutoActions::stateChanged);
    connect(document,&Document::rasterSelectionChanged,this,[this]{
        if(!recording()||m_document->committing())return;
        const auto &selection=m_document->rasterSelection();
        if(selection.active!=m_recordSelection.active||selection.path!=m_recordSelection.path||selection.featherRadius!=m_recordSelection.featherRadius)
            halt("Recording stopped: pixel selection changes are not supported. Earlier steps were kept.");
    });
    connect(document,&Document::selectionChanged,this,&AutoActions::stateChanged);
    connect(document,&Document::selectionChanged,this,[this]{
        if(!recording()||m_document->committing())return;
        if(m_recordFrame!=m_document->currentFrame()||m_recordShot!=m_document->activeShotId()){halt("Recording stopped because the shot or frame changed.");return;}
        const auto &layers=m_document->activeShot().layers;int previous=-1;
        for(int i=0;i<layers.size();++i)if(layers[i].id==m_recordLayer)previous=i;
        const int current=m_document->activeLayer();
        if(previous<0){halt("Recording stopped because its layer is no longer available.");return;}
        if(previous!=current)append({"selectRelative",QString("Select layer %1%2").arg(current>previous?"+":"").arg(current-previous),{{"offset",current-previous}},true});
        m_recordLayer=layers[current].id;
    });
    connect(document->history(),&QUndoStack::indexChanged,this,[this]{if(recording()&&!m_document->committing())halt("Recording stopped by history navigation. Recorded steps were kept.");emit stateChanged();});
    connect(document,&QObject::destroyed,this,[this]{m_recording.clear();emit stateChanged();});
}
const AutoAction *AutoActions::action(const QString &id) const {for(const auto &a:m_actions)if(a.id==id)return &a;return nullptr;}
AutoAction *AutoActions::mutableAction(const QString &id){for(auto &a:m_actions)if(a.id==id)return &a;return nullptr;}
bool AutoActions::fail(const QString &message,QString *error){if(error)*error=message;emit status(message);return false;}
QString AutoActions::availabilityReason() const {
    if(!m_document||!m_document->isOpen())return "Open a project first.";
    if(m_playing)return "An action is already running.";
    if(m_document->committing()||m_document->editingGestureActive()||m_document->layerPropertyEditActive())return "Finish the current drawing or property edit first.";
    return {};
}
bool AutoActions::edited(){emit changed();QString error;return save(&error);}
QString AutoActions::create(const QString &name){
    if(recording()||playing()||m_loadFailed){fail("Stop recording before changing actions, and resolve any library load error.");return {};}
    if(m_actions.size()>=maxActions||name.trimmed().isEmpty()||name.size()>100){fail("Use a name up to 100 characters; the library holds 128 actions.");return {};}
    const auto id=QUuid::createUuid().toString(QUuid::WithoutBraces);m_actions.append({id,name.trimmed(),{}});edited();return id;
}
bool AutoActions::rename(const QString &id,const QString &name){auto *a=mutableAction(id);if(!a||recording()||playing()||m_loadFailed)return false;if(name.trimmed().isEmpty()||name.size()>100)return fail("Use an action name up to 100 characters.");a->name=name.trimmed();return edited();}
QString AutoActions::duplicate(const QString &id){const auto *a=action(id);if(!a||recording()||playing()||m_loadFailed)return {};if(m_actions.size()>=maxActions){fail("The library already contains 128 actions.");return {};}auto copy=*a;copy.id=QUuid::createUuid().toString(QUuid::WithoutBraces);copy.name=(copy.name+" copy").left(100);m_actions.append(copy);edited();return copy.id;}
bool AutoActions::remove(const QString &id){if(recording()||playing()||m_loadFailed)return false;for(int i=0;i<m_actions.size();++i)if(m_actions[i].id==id){m_actions.removeAt(i);return edited();}return false;}
bool AutoActions::setStepEnabled(const QString &id,int index,bool enabled){auto *a=mutableAction(id);if(!a||recording()||playing()||m_loadFailed||index<0||index>=a->steps.size())return false;a->steps[index].enabled=enabled;return edited();}
bool AutoActions::removeStep(const QString &id,int index){auto *a=mutableAction(id);if(!a||recording()||playing()||m_loadFailed||index<0||index>=a->steps.size())return false;a->steps.removeAt(index);return edited();}
bool AutoActions::moveStep(const QString &id,int from,int to){auto *a=mutableAction(id);if(!a||recording()||playing()||m_loadFailed||from<0||to<0||from>=a->steps.size()||to>=a->steps.size())return false;a->steps.move(from,to);return edited();}
bool AutoActions::startRecording(const QString &id){
    const auto reason=availabilityReason();if(!reason.isEmpty())return fail(reason);
    auto *a=mutableAction(id);if(!a||recording()||m_loadFailed)return fail("Select an action and resolve any library load error first.");
    if(a->steps.size()>=maxSteps)return fail("This action already has 256 steps.");
    if(!save())return false;
    m_recordFrame=m_document->currentFrame();m_recordShot=m_document->activeShotId();m_recordSelection=m_document->rasterSelection();
    m_recording=id;m_recordLayer=m_document->activeShot().layers[m_document->activeLayer()].id;
    emit stateChanged();emit status("Recording layer, fill and transform operations. Unsupported edits stop recording.");return true;
}
void AutoActions::stopRecording(){if(!recording())return;m_recording.clear();m_recordLayer.clear();const bool saved=edited();emit stateChanged();if(saved)emit status("Recording stopped.");}
void AutoActions::halt(const QString &reason){stopRecording();emit status(reason+(m_saveError.isEmpty()?QString():" "+m_saveError));}
void AutoActions::append(AutoActionStep step){auto *a=mutableAction(m_recording);if(!a)return;if(a->steps.size()>=maxSteps){halt("Recording stopped at the 256-step limit.");return;}a->steps.append(std::move(step));if(!edited()){m_recording.clear();m_recordLayer.clear();emit stateChanged();}}
void AutoActions::committed(const Project &before,const Project &after,const QString &label,const QString &beforeLayer,const QString &afterLayer,const QJsonObject &action){
    if(!recording()||playing())return;
    int si=-1,ti=-1;const auto *old=findLayer(before,beforeLayer,&si);const auto *now=findLayer(after,afterLayer,&ti);
    const auto unsupported=[&]{halt("Recording stopped: “"+label+"” is not supported. Earlier steps were kept.");};
    if(!old||!now||si!=ti||before.shots[si].id!=after.shots[ti].id){unsupported();return;}
    const auto &a=before.shots[si].layers;const auto &b=after.shots[ti].layers;
    AutoActionStep step;step.label=label;
    if(!action.isEmpty()) {
        if(!keys(action,{"operation","parameters"})||!action["operation"].isString()||!action["parameters"].isObject()
            ||beforeLayer!=afterLayer||a.size()!=b.size()||!pixelOperation(action["operation"].toString())){unsupported();return;}
        step.operation=action["operation"].toString();step.parameters=action["parameters"].toObject();
        if(!validStep(step)){unsupported();return;}
        append(std::move(step));m_recordLayer=afterLayer;m_recordSelection=m_document->rasterSelection();return;
    }
    if((label=="Add drawing layer"||label=="Add notes layer")&&b.size()==a.size()+1&&!now->group){step.operation="newLayer";step.parameters={{"annotation",now->annotation}};}
    else if(label=="Group layers"&&b.size()==a.size()+1&&now->group){
        int moved=0;bool activeMoved=false;for(const auto &l:b)if(l.parentId==now->id){++moved;activeMoved|=l.id==beforeLayer;}
        if(moved!=1||!activeMoved){unsupported();return;}step.operation="group";
    }
    else if(label=="Duplicate layers"&&b.size()==a.size()+1&&!old->group&&now->name==(old->name+" copy").left(100)&&old->tiles==now->tiles&&old->kind==now->kind){
        int source=-1,copy=-1;for(int i=0;i<b.size();++i){if(b[i].id==beforeLayer)source=i;if(b[i].id==afterLayer)copy=i;}
        if(copy!=source+1||source<0||now->parentId!=old->parentId){unsupported();return;}
        step.operation="duplicate";
    }
    else if(label=="Delete layers"&&b.size()==a.size()-1&&!old->group&&!findLayer(after,beforeLayer)){step.operation="delete";}
    else if(a.size()==b.size()&&beforeLayer==afterLayer){
        // Reconstruct the before-project with only the supported active-layer
        // fields changed. Any other metadata or pixel change is unsupported.
        Project projected=before;auto &l=projected.shots[si].layers;
        int index=-1;for(int i=0;i<l.size();++i)if(l[i].id==beforeLayer)index=i;
        if(index<0){unsupported();return;}
        const auto prev=properties(*old),next=properties(*now);
        for(auto it=next.begin();it!=next.end();++it)if(prev[it.key()]!=it.value())step.parameters[it.key()]=it.value();
        auto &p=l[index];p.name=now->name;p.visible=now->visible;p.locked=now->locked;p.annotation=now->annotation;p.opacity=now->opacity;
        p.blendMode=now->blendMode;p.alphaLocked=now->alphaLocked;p.positionLocked=now->positionLocked;p.clipped=now->clipped;p.effects=now->effects;
        bool samePixels=true;for(int j=0;j<a.size();++j)if(a[j].tiles!=b[j].tiles||a[j].mask!=b[j].mask)samePixels=false;
        if(step.parameters.isEmpty()||!samePixels||projected.toJson()!=after.toJson()){unsupported();return;}
        step.operation="properties";
    }else {unsupported();return;}
    if(!validStep(step)){unsupported();return;}
    append(std::move(step));m_recordLayer=afterLayer;
}
bool AutoActions::play(const QString &id){
    const auto reason=availabilityReason();if(!reason.isEmpty())return fail(reason);
    const auto *selected=action(id);if(!selected||recording())return fail("Stop recording and select an action to play.");
    const auto a=*selected;int enabled=0;for(const auto &s:a.steps)enabled+=s.enabled;if(!enabled)return fail("Enable at least one recorded step.");
    m_playing=true;emit stateChanged();
    Document scratch;scratch.load(m_document->project());scratch.selectShot(m_document->activeShotId());scratch.setCurrentFrame(m_document->currentFrame());scratch.selectLayer(m_document->activeLayer());
    scratch.setKeyframeSourceEditing(m_document->keyframeSourceEditing());
    const auto beforeSelection=m_document->m_selection;
    scratch.m_selection=beforeSelection;
    QString error;connect(&scratch,&Document::warning,this,[&error](const QString &message){error=message;});
    bool ok=true;int number=0;
    for(const auto &s:a.steps){++number;if(!s.enabled)continue;if(!validStep(s)||!applyStep(scratch,s,&error)||!error.isEmpty()){ok=false;if(error.isEmpty())error="The target layer cannot perform this operation (check layer type, locks and available layers).";error=QString("Step %1 (%2): %3").arg(number).arg(s.label,error);break;}}
    if(ok){
        const auto &after=scratch.project();
        // Selection-only actions still need to apply their final target, but do
        // not create an empty project command or disturb the clean marker.
        if(sameProject(after,m_document->project())&&sameSelection(beforeSelection,scratch.m_selection))m_document->selectLayer(scratch.activeLayer());
        else ok=m_document->commit(m_document->project(),after,"Action: "+a.name,scratch.activeShot().layers[scratch.activeLayer()].id,beforeSelection,scratch.m_selection);
    }
    m_playing=false;emit stateChanged();
    if(!ok)return fail(error.isEmpty()?"The action could not be applied.":error);
    emit status("Played “"+a.name+"”.");return true;
}
bool AutoActions::save(QString *error){
    const auto unsaved=[&](const QString &message){m_saveError="Actions are unsaved. "+message;return fail(m_saveError,error);};
    if(m_loadFailed)return unsaved("The action library could not be loaded; it has not been overwritten.");
    QJsonArray actions;
    for(const auto &a:m_actions){QJsonArray steps;for(const auto &s:a.steps)steps.append(QJsonObject{{"operation",s.operation},{"label",s.label},{"parameters",s.parameters},{"enabled",s.enabled}});actions.append(QJsonObject{{"id",a.id},{"name",a.name},{"steps",steps}});}
    bool extended=false;for(const auto &a:m_actions)for(const auto &s:a.steps)extended|=pixelOperation(s.operation);
    const auto bytes=QJsonDocument(QJsonObject{{"version",extended?2:1},{"actions",actions}}).toJson();
    if(bytes.size()>maxBytes)return unsaved("The action library exceeds its 4 MB limit.");
    if(!QDir().mkpath(QFileInfo(m_path).absolutePath()))return unsaved("Cannot create the action-library folder.");
    QSaveFile file(m_path);if(!file.open(QIODevice::WriteOnly)||file.write(bytes)!=bytes.size()||!file.commit())return unsaved("Could not save: "+file.errorString());
    m_saveError.clear();return true;
}
bool AutoActions::load(QString *error){
    if(recording()||playing())return fail("Stop recording or playback before loading actions.",error);
    QFile file(m_path);if(!file.exists()){m_actions.clear();m_loadFailed=false;emit changed();return true;}
    const auto invalid=[&](const QString &why){m_loadFailed=true;return fail("Could not load actions: "+why+" Existing actions were kept.",error);};
    if(!file.open(QIODevice::ReadOnly)||file.size()>maxBytes)return invalid("file is unreadable or exceeds 4 MB.");
    QJsonParseError parse;const auto json=QJsonDocument::fromJson(file.readAll(),&parse);if(parse.error!=QJsonParseError::NoError||!json.isObject())return invalid("invalid JSON.");
    const auto root=json.object();if(!keys(root,{"version","actions"})||!integer(root["version"],1,2)||!root["actions"].isArray())return invalid("unsupported format.");
    const auto list=root["actions"].toArray();if(list.size()>maxActions)return invalid("too many actions.");
    QVector<AutoAction> decoded;QSet<QString> ids;
    for(const auto &value:list){if(!value.isObject())return invalid("invalid action.");const auto o=value.toObject();
        if(!keys(o,{"id","name","steps"})||!o["id"].isString()||QUuid(o["id"].toString()).isNull()||ids.contains(o["id"].toString())||!o["name"].isString()||o["name"].toString().trimmed().isEmpty()||o["name"].toString().size()>100||!o["steps"].isArray()||o["steps"].toArray().size()>maxSteps)return invalid("invalid action fields.");
        AutoAction a{o["id"].toString(),o["name"].toString(),{}};ids.insert(a.id);
        for(const auto &v:o["steps"].toArray()){if(!v.isObject())return invalid("invalid step.");const auto s=v.toObject();
            if(!keys(s,{"operation","label","parameters","enabled"})||!s["operation"].isString()||!s["label"].isString()||!s["parameters"].isObject()||!s["enabled"].isBool())return invalid("invalid step fields.");
            AutoActionStep step{s["operation"].toString(),s["label"].toString(),s["parameters"].toObject(),s["enabled"].toBool()};if(!validStep(step)||(root["version"].toInt()==1&&pixelOperation(step.operation)))return invalid("unsupported or invalid step parameters.");a.steps.append(step);
        }decoded.append(a);
    }
    m_actions=std::move(decoded);m_loadFailed=false;m_saveError.clear();emit changed();return true;
}
}
