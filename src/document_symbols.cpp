#include "symbols.h"
#include "symbol_clips.h"
#include "document.h"
#include <QUuid>
#include <QSet>
#include <algorithm>
#include <cmath>

namespace toon {
namespace {
QString freshId() { return QUuid::createUuid().toString(QUuid::WithoutBraces); }
bool fail(Document *document, QString *error, const QString &message) {
    if (error) *error = message; else if (document) emit document->warning(message);
    return false;
}
bool ready(Document *document, QString *error) {
    if (error) error->clear();
    if (!document || !document->isOpen()) return fail(document,error,"Open a project first.");
    if (document->editingGestureActive() || document->layerPropertyEditActive())
        return fail(document,error,"Finish the current edit before changing symbols.");
    return true;
}
int definitionIndex(const Project &project, const QString &id) {
    for (int i=0;i<project.symbols.size();++i) if (project.symbols[i].id==id) return i;
    return -1;
}
int layerIndex(const Shot &shot, const QString &id) {
    for (int i=0;i<shot.layers.size();++i) if (shot.layers[i].id==id) return i;
    return -1;
}
bool editable(const Shot &shot, int index) {
    if (index<0) return false;
    const auto *layer=&shot.layers[index];
    for (int depth=0;layer&&depth<=8;++depth) {
        if (layer->locked) return false;
        const int parent=layerIndex(shot,layer->parentId);
        if (layer->parentId.isEmpty()) return true;
        if (parent<0) return false;
        layer=&shot.layers[parent];
    }
    return false;
}
bool commit(Document *document, Project after, const QString &label, QString *error, const QString &selection={}) {
    const auto validation=after.validate(); if (!validation.isEmpty()) return fail(document,error,validation);
    QString detail;
    const auto connection=QObject::connect(document,&Document::warning,document,[&](const QString &value){detail=value;});
    const bool okay=document->replaceSymbolProject(after,label);
    QObject::disconnect(connection);
    if (!okay) return fail(document,error,detail.isEmpty()?QString("Could not complete the symbol change."):detail);
    if (!selection.isEmpty() && layerPresentAtFrame(document->activeShot(),selection,document->currentFrame()))
        document->selectLayer(layerIndex(document->activeShot(),selection));
    return true;
}
void stripWorkspace(Shot &shot) {
    shot.references.clear(); shot.guides.clear(); shot.savedSelections.clear(); shot.guideSettings={};
    shot.audioClips.clear(); shot.camera2D={}; shot.camera2DKeys.clear(); shot.sceneCameraAnimations.clear();
    shot.timelineCues.clear();
    for (auto &layer:shot.layers) layer.renderTransform={};
}
Shot clonedContent(const Shot &source) {
    Shot result=source; result.id=freshId(); stripWorkspace(result);
    QHash<QString,QString> ids;
    const auto own=[&](const QString &id){ids.insert(id,freshId());};
    for (const auto &layer:result.layers) { own(layer.id); for (const auto &object:layer.objects) own(object.id); }
    for (const auto &track:result.animationTracks) {
        own(track.id); for (const auto &drawing:track.drawings) own(drawing.id);
        for (const auto &exposure:track.exposures) own(exposure.id);
    }
    const auto mapped=[&](const QString &id){return id.isEmpty()?QString():ids.value(id,id);};
    for (auto &layer:result.layers) {
        layer.id=mapped(layer.id); layer.parentId=mapped(layer.parentId);
        for (auto &object:layer.objects) object.id=mapped(object.id);
    }
    for (auto &track:result.animationTracks) {
        track.id=mapped(track.id); track.rootGroupLayerId=mapped(track.rootGroupLayerId);
        for (auto &drawing:track.drawings) {
            drawing.id=mapped(drawing.id); drawing.groupLayerId=mapped(drawing.groupLayerId);
            for (auto it=drawing.templateLayers.begin();it!=drawing.templateLayers.end();++it) it.value()=mapped(it.value());
        }
        for (auto &exposure:track.exposures) {exposure.id=mapped(exposure.id);exposure.drawingId=mapped(exposure.drawingId);}
    }
    for (auto &animation:result.layerAnimations) {
        animation.layerId=mapped(animation.layerId);
        for (auto &path:animation.paths) path.objectId=mapped(path.objectId);
    }
    return result;
}
bool sameContent(const Shot &first, const Shot &second) {
    Project a,b; a.shots={first}; b.shots={second};
    if (a.toJson()!=b.toJson() || first.layers.size()!=second.layers.size()) return false;
    for (int i=0;i<first.layers.size();++i) {
        const auto &x=first.layers[i], &y=second.layers[i];
        if (x.tiles!=y.tiles || x.objects!=y.objects || x.mask!=y.mask) return false;
    }
    return true;
}
RasterLayer instanceLayer(const SymbolDefinition &definition) {
    auto layer=RasterLayer::create(definition.name); layer.kind=LayerKind::Symbol;
    layer.symbol=SymbolInstance{definition.id}; return layer;
}
QString copiedName(const QString &name) { return name.left(95)+" copy"; }
}
QString createEmptySymbol(Document *document, const QString &name, QString *error) {
    if (!ready(document,error)) return {};
    auto after=document->project();
    auto content=Project::create().shots.first(); content.durationFrames=1; content.name=name.trimmed();
    content.sheet=document->activeShot().sheet;
    SymbolDefinition definition{freshId(),name.trimmed(),content}; const auto id=definition.id;
    after.symbols.append(std::move(definition));
    return commit(document,std::move(after),"Create symbol",error)?id:QString();
}
QString convertLayersToSymbol(Document *document, const QStringList &layerIds, const QString &name, QString *error) {
    if (!ready(document,error)) return {};
    const auto &source=document->activeShot(); QSet<QString> requested(layerIds.begin(),layerIds.end());
    if (requested.isEmpty()) {fail(document,error,"Select the layers to convert into a symbol.");return {};}
    for (const auto &id:requested) if (layerIndex(source,id)<0) {fail(document,error,"A selected layer no longer exists.");return {};}
    QVector<int> roots;
    for (int i=0;i<source.layers.size();++i) if (requested.contains(source.layers[i].id)) {
        bool included=false;
        for (const auto &id:requested) if (id!=source.layers[i].id&&layerIsDescendant(source.layers,i,id)) {included=true;break;}
        if (!included) roots.append(i);
    }
    const auto parent=source.layers[roots.first()].parentId;
    QSet<QString> selected;
    for (const int root:roots) {
        const auto &layer=source.layers[root];
        if (layer.parentId!=parent || !editable(source,root)) {fail(document,error,"Choose unlocked layers in the same folder.");return {};}
        if (layer.clipped || layer.adjustment || (roots.size()>1&&layer.blendMode!=LayerBlendMode::Normal)) {
            fail(document,error,"Put clipped or blending layers in a folder with their base artwork before converting the folder into a symbol.");return {};
        }
        selected.insert(layer.id);
        for (int i=0;i<source.layers.size();++i) if (layerIsDescendant(source.layers,i,layer.id)) selected.insert(source.layers[i].id);
    }
    for (int i=roots.first();i<=roots.last();++i) if (source.layers[i].parentId==parent&&!selected.contains(source.layers[i].id)) {
        fail(document,error,"Select a continuous range of layers in the same folder.");return {};
    }
    // A clipped sibling above the range depends on the original base's alpha.
    for (int i=roots.last()+1;i<source.layers.size();++i) if (source.layers[i].parentId==parent) {
        if (source.layers[i].clipped) {fail(document,error,"Include the clipping group in a folder before converting its base into a symbol.");return {};}
        break;
    }
    for (int i=0;i<source.layers.size();++i) if (selected.contains(source.layers[i].id)) {
        const auto &layer=source.layers[i];
        if (layer.scene3d || layerIsAnnotation(source.layers,i)) {fail(document,error,"Symbols contain 2D artwork. Keep 3D references and production notes in the shot.");return {};}
        const auto owner=animationOwner(source,layer.id);
        if (!owner.trackId.isEmpty()) {
            const auto track=std::find_if(source.animationTracks.cbegin(),source.animationTracks.cend(),[&](const auto &item){return item.id==owner.trackId;});
            if (track==source.animationTracks.cend()||!selected.contains(track->rootGroupLayerId)) {
                fail(document,error,"Select the complete animation track to convert its drawings and timing into a symbol.");return {};
            }
        }
    }
    auto after=document->project(); auto &shot=after.shots[document->activeShotIndex()];
    Shot content; content.id=freshId();content.name=name.trimmed();content.sheet=shot.sheet;content.durationFrames=shot.durationFrames;
    for (const auto &layer:shot.layers) if (selected.contains(layer.id)) {auto copy=layer;if(copy.parentId==parent)copy.parentId.clear();content.layers.append(std::move(copy));}
    for (const auto &track:shot.animationTracks) if (selected.contains(track.rootGroupLayerId)) content.animationTracks.append(track);
    for (const auto &animation:shot.layerAnimations) if (selected.contains(animation.layerId)) content.layerAnimations.append(animation);
    const bool nestedTiming=std::any_of(content.layers.cbegin(),content.layers.cend(),[](const auto &layer){return layer.symbol.has_value();});
    if(content.animationTracks.isEmpty()&&content.layerAnimations.isEmpty()&&!nestedTiming) content.durationFrames=1;
    SymbolDefinition definition{freshId(),name.trimmed(),content};
    auto instance=instanceLayer(definition); instance.parentId=parent;
    if (roots.size()==1) {
        // External blend modes belong to the instance, not the isolated master.
        instance.blendMode=source.layers[roots.first()].blendMode;
        for(auto &layer:definition.content.layers)if(layer.id==source.layers[roots.first()].id)layer.blendMode=LayerBlendMode::Normal;
    }
    const auto id=definition.id, instanceId=instance.id;
    const int insertion=roots.first();
    shot.layers.erase(std::remove_if(shot.layers.begin(),shot.layers.end(),[&](const auto &layer){return selected.contains(layer.id);}),shot.layers.end());
    shot.layers.insert(qMin(insertion,int(shot.layers.size())),std::move(instance));
    shot.animationTracks.erase(std::remove_if(shot.animationTracks.begin(),shot.animationTracks.end(),[&](const auto &track){return selected.contains(track.rootGroupLayerId);}),shot.animationTracks.end());
    shot.layerAnimations.erase(std::remove_if(shot.layerAnimations.begin(),shot.layerAnimations.end(),[&](const auto &animation){return selected.contains(animation.layerId);}),shot.layerAnimations.end());
    after.symbols.append(std::move(definition));
    return commit(document,std::move(after),"Convert layers to symbol",error,instanceId)?id:QString();
}
QString insertSymbolInstance(Document *document, const QString &symbolId, QString *error) {
    if(!ready(document,error))return {};
    const auto *definition=findSymbol(document->project(),symbolId);
    if(!definition){fail(document,error,"Choose a symbol from the library.");return {};}
    auto after=document->project();auto &shot=after.shots[document->activeShotIndex()];
    const int active=document->activeLayer();
    if(!editable(shot,active)){fail(document,error,"Choose an unlocked layer or folder for the instance.");return {};}
    auto layer=instanceLayer(*definition);const auto id=layer.id;
    layer.parentId=shot.layers[active].group?shot.layers[active].id:shot.layers[active].parentId;
    const auto owner=animationOwner(shot,shot.layers[active].id);
    if(!owner.trackId.isEmpty()&&owner.drawingId.isEmpty()) {
        fail(document,error,"Select an exposed drawing or a static layer before inserting a symbol.");return {};
    }
    int insertion=active+1;
    if(shot.layers[active].group)while(insertion<shot.layers.size()&&layerIsDescendant(shot.layers,insertion,shot.layers[active].id))++insertion;
    shot.layers.insert(insertion,std::move(layer));
    return commit(document,std::move(after),"Insert symbol",error,id)?id:QString();
}
bool renameSymbol(Document *document,const QString &id,const QString &name,QString *error) {
    if(!ready(document,error))return false;auto after=document->project();const int at=definitionIndex(after,id);
    if(at<0)return fail(document,error,"This symbol no longer exists.");
    if(after.symbols[at].name==name.trimmed())return true;
    after.symbols[at].name=name.trimmed();return commit(document,std::move(after),"Rename symbol",error);
}
QString duplicateSymbol(Document *document,const QString &id,QString *error) {
    if(!ready(document,error))return {};auto after=document->project();const auto *source=findSymbol(after,id);
    if(!source){fail(document,error,"This symbol no longer exists.");return {};}
    SymbolDefinition copy{freshId(),copiedName(source->name),clonedContent(source->content)};const auto result=copy.id;
    after.symbols.append(std::move(copy));return commit(document,std::move(after),"Duplicate symbol",error)?result:QString();
}
bool removeSymbol(Document *document,const QString &id,QString *error) {
    if(!ready(document,error))return false;auto after=document->project();const int at=definitionIndex(after,id);
    if(at<0)return fail(document,error,"This symbol no longer exists.");
    if(symbolUseCount(after,id)>0)return fail(document,error,"This symbol is in use. Delete or break apart its instances before removing it from the library.");
    after.symbols.removeAt(at);return commit(document,std::move(after),"Delete symbol",error);
}
QString makeSymbolInstanceUnique(Document *document,const QString &layerId,QString *error) {
    if(!ready(document,error))return {};auto after=document->project();auto &shot=after.shots[document->activeShotIndex()];const int at=layerIndex(shot,layerId);
    if(at<0||!shot.layers[at].symbol||!editable(shot,at)){fail(document,error,"Select an unlocked symbol instance.");return {};}
    const auto *source=findSymbol(after,shot.layers[at].symbol->symbolId);
    if(!source){fail(document,error,"This symbol no longer exists.");return {};}
    SymbolDefinition copy{freshId(),copiedName(source->name),clonedContent(source->content)};const auto result=copy.id;
    shot.layers[at].symbol->symbolId=result;after.symbols.append(std::move(copy));
    return commit(document,std::move(after),"Make symbol unique",error,layerId)?result:QString();
}
bool setSymbolInstance(Document *document,const QString &layerId,const SymbolInstance &instance,QString *error) {
    if(!ready(document,error))return false;auto after=document->project();auto &shot=after.shots[document->activeShotIndex()];const int at=layerIndex(shot,layerId);
    const auto timingError=symbolClipEditError(shot,layerId);
    if(!timingError.isEmpty())return fail(document,error,timingError);
    if(*shot.layers[at].symbol==instance)return true;
    if ((instance.startFrame != shot.layers[at].symbol->startFrame && instance.startFrame > shot.durationFrames)
        || (instance.durationFrames >= 0 && qint64(instance.startFrame)+instance.durationFrames > shot.durationFrames))
        return fail(document,error,"Keep the Symbol clip inside the shot.");
    shot.layers[at].symbol=instance;return commit(document,std::move(after),"Change symbol playback",error,layerId);
}
bool breakApartSymbolInstance(Document *document,const QString &layerId,QString *error) {
    if(!ready(document,error))return false;auto after=document->project();auto &shot=after.shots[document->activeShotIndex()];const int at=layerIndex(shot,layerId);
    if(at<0||!shot.layers[at].symbol||!editable(shot,at))return fail(document,error,"Select an unlocked symbol instance.");
    if(shot.layers[at].clipped)return fail(document,error,"Remove this instance's clipping before breaking it apart. Its editable layers will be placed in a folder.");
    const auto instance=*shot.layers[at].symbol;const auto *definition=findSymbol(after,instance.symbolId);
    if(!definition)return fail(document,error,"This symbol no longer exists.");
    const int frame=symbolLocalFrame(instance,document->currentFrame(),definition->content.durationFrames);
    if(frame<0)return fail(document,error,"Move to a frame where the symbol is visible before breaking it apart.");
    Shot posed=definition->content;QString detail;
    posed.layers=evaluateLayers(definition->content,frame,false,{},&detail);
    if(!detail.isEmpty())return fail(document,error,detail);
    posed.animationTracks.clear();posed.layerAnimations.clear();posed.durationFrames=shot.durationFrames;
    for(auto &layer:posed.layers) {
        const auto *animation=layerAnimation(definition->content,layer.id);
        if(animation) {
            const int original=layerIndex(definition->content,layer.id);
            layer.opacity=definition->content.layers[original].opacity;
            LayerAnimation held;held.layerId=layer.id;held.pivot=animation->pivot;
            const auto position=evaluateAnimatedPosition(*animation,frame);
            for(const auto property:{KeyframeProperty::PositionX,KeyframeProperty::PositionY,KeyframeProperty::ScaleX,KeyframeProperty::ScaleY,KeyframeProperty::Rotation,KeyframeProperty::Opacity}) {
                qreal value=evaluateProperty(*animation,property,frame);
                if(property==KeyframeProperty::PositionX)value=position.x();
                if(property==KeyframeProperty::PositionY)value=position.y();
                if(property==KeyframeProperty::Rotation&&animation->orientAlongPath){const auto direction=animatedPositionDirection(*animation,frame);if(!direction.isNull())value+=std::atan2(direction.y(),direction.x())*180/3.14159265358979323846;}
                if(value!=keyframeDefaultValue(property))held.channels.append({property,{{0,value,KeyframeInterpolation::Hold}}});
            }
            if(!held.channels.isEmpty())posed.layerAnimations.append(std::move(held));
        }
        layer.renderTransform={};
        if(layer.symbol){const auto *nested=findSymbol(after,layer.symbol->symbolId);const int nestedFrame=nested?symbolLocalFrame(*layer.symbol,frame,nested->content.durationFrames):-1;layer.symbol->playback=SymbolPlayback::SingleFrame;layer.symbol->startFrame=0;layer.symbol->firstFrame=qMax(0,nestedFrame);layer.symbol->durationFrames=-1;layer.symbol->playbackOffset=0;layer.symbol->poseKeys.clear();if(nestedFrame<0)layer.visible=false;}
    }
    posed=clonedContent(posed);
    if(posed.layers.isEmpty())posed.layers.append(RasterLayer::create("Drawing"));
    auto &wrapper=shot.layers[at];wrapper.kind=LayerKind::Raster;wrapper.symbol.reset();wrapper.group=true;
    for(auto &layer:posed.layers)if(layer.parentId.isEmpty())layer.parentId=wrapper.id;
    for(int i=0;i<posed.layers.size();++i)shot.layers.insert(at+1+i,posed.layers[i]);
    for(const auto &animation:posed.layerAnimations)shot.layerAnimations.append(animation);
    // The wrapper keeps the former instance ID, effects, mask and animation.
    return commit(document,std::move(after),"Break apart symbol at current frame",error,layerId);
}
Project editProjectForSymbol(const Project &project,const QString &id,QString *error) {
    if(error)error->clear();const auto *definition=findSymbol(project,id);
    if(!definition){if(error)*error="This symbol no longer exists.";return {};}
    Project workspace=project;workspace.movie=definition->name;workspace.transparent=true;
    workspace.shots={definition->content};workspace.symbols.clear();workspace.drawingAids={};
    QSet<QString> visited{id};
    std::function<void(const Shot&)> include=[&](const Shot &content){for(const auto &layer:content.layers)if(layer.symbol&&!visited.contains(layer.symbol->symbolId)){visited.insert(layer.symbol->symbolId);if(const auto *nested=findSymbol(project,layer.symbol->symbolId)){workspace.symbols.append(*nested);include(nested->content);}}};
    include(definition->content);
    const auto validation=workspace.validate();if(!validation.isEmpty()){if(error)*error=validation;return {};}
    return workspace;
}
bool updateSymbolContent(Document *document,const QString &id,const Shot &content,const Shot &expectedOriginal,QString *error) {
    if(!ready(document,error))return false;auto after=document->project();const int at=definitionIndex(after,id);
    if(at<0)return fail(document,error,"This symbol was removed while its editor was open.");
    if(!sameContent(after.symbols[at].content,expectedOriginal))return fail(document,error,"This symbol changed while its editor was open. Reopen it before applying these edits.");
    auto replacement=content;replacement.id=after.symbols[at].content.id;stripWorkspace(replacement);
    if(sameContent(after.symbols[at].content,replacement))return true;
    after.symbols[at].content=std::move(replacement);
    return commit(document,std::move(after),"Edit symbol",error);
}
} // namespace toon
