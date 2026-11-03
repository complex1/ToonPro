#include "project_templates.h"
#include <QUuid>

namespace toon {
namespace {
QString newId() { return QUuid::createUuid().toString(QUuid::WithoutBraces); }
void plainSheet(Shot &shot) {
    shot.sheet.left=shot.sheet.right=shot.sheet.top=shot.sheet.bottom=0;
    shot.sheet.header=shot.sheet.overscan=0;
    shot.sheet.safeArea=false;shot.sheet.showHeader=false;shot.sheet.showGuides=false;
}
void addDrawingTrack(Shot &shot,const QString &name,DrawingTemplate type,int hold) {
    auto root=RasterLayer::create(name);root.group=true;shot.layers.append(root);
    AnimationTrack track;track.id=newId();track.rootGroupLayerId=root.id;track.drawingTemplate=drawingTemplateSlots(type);
    AnimationDrawing drawing;drawing.id=newId();
    auto group=RasterLayer::create("Drawing 1");group.group=true;group.parentId=root.id;shot.layers.append(group);drawing.groupLayerId=group.id;
    for(const auto &slot:track.drawingTemplate){
        auto layer=RasterLayer::create(slot.name);layer.kind=slot.kind;layer.parentId=group.id;
        drawing.templateLayers.insert(slot.id,layer.id);shot.layers.append(layer);
    }
    track.drawings.append(drawing);track.exposures.append({newId(),drawing.id,0,hold});shot.animationTracks.append(track);
}
}
const QVector<ProjectStarterTemplate> &projectStarterTemplates() {
    static const QVector<ProjectStarterTemplate> templates{
        {"blank","Blank","One drawing layer · no margins, header or frame guides","file-plus","illustration"},
        {"illustration","Illustration","Square canvas · Colors, Sketch and Ink layers","brush","illustration"},
        {"anime","Anime","Production sheet · Background and an Anime drawing track","clapperboard","anime"},
        {"storyboard","Storyboard","A shot with a held board drawing and production header","panels-top-left","storyboard"},
        {"animatic","Animatic","A clean frame and a timed board track, ready for audio","play","animatic"}
    };return templates;
}
std::optional<Project> projectFromStarterTemplate(const QString &id) {
    bool known=false;for(const auto &item:projectStarterTemplates())if(item.id==id){known=true;break;}
    if(!known)return {};
    auto project=Project::create();auto &shot=project.shots[0];shot.layers.clear();
    if(id=="blank"){
        plainSheet(shot);shot.durationFrames=1;shot.layers.append(RasterLayer::create("Drawing"));
    }else if(id=="illustration"){
        project.width=project.height=2048;plainSheet(shot);shot.durationFrames=1;
        for(const auto &name:{"Colors","Sketch","Ink"})shot.layers.append(RasterLayer::create(name));
    }else if(id=="anime"){
        shot.durationFrames=120;shot.layers.append(RasterLayer::create("Background"));addDrawingTrack(shot,"Character",DrawingTemplate::Anime,2);
    }else if(id=="storyboard"){
        shot.durationFrames=72;shot.sheet.left=shot.sheet.right=80;shot.sheet.top=40;shot.sheet.bottom=80;shot.sheet.overscan=0;shot.sheet.header=140;
        shot.layers.append(RasterLayer::create("Background"));addDrawingTrack(shot,"Boards",DrawingTemplate::Rough,shot.durationFrames);
    }else{
        plainSheet(shot);shot.durationFrames=240;shot.layers.append(RasterLayer::create("Background"));addDrawingTrack(shot,"Boards",DrawingTemplate::Rough,48);
    }
    return project;
}
QString starterDrawingLayerId(const Project &project,const QString &templateId) {
    if(project.shots.isEmpty())return {};
    const auto &shot=project.shots[0];
    if(!shot.animationTracks.isEmpty()&&!shot.animationTracks[0].drawings.isEmpty())
        return shot.animationTracks[0].drawings[0].templateLayers.value("rough");
    if(templateId=="illustration")for(const auto &layer:shot.layers)if(layer.name=="Sketch")return layer.id;
    return shot.layers.isEmpty()?QString():shot.layers[0].id;
}
} // namespace toon
