#include "motion_study.h"
#include <QDir>
#include <QJsonArray>
#include <QImageReader>
#include <QUuid>
#include <cmath>
#include <algorithm>
namespace toon {
QString MotionStudy::validate() const {
    if(name.size()>200 || times.isEmpty() || times.size()>1800 || kept.size()!=times.size() || !kept[0])return "Invalid study frame selection (maximum 1,800 source frames).";
    if(!std::isfinite(fps)||fps<=0||fps>1000||!std::isfinite(duration)||duration<=0||duration>3600)return "Invalid study timing.";
    double previous=-1;
    for(double t:times){if(!std::isfinite(t)||t<0||t<=previous||t>=duration)return "Frame timestamps must increase within the study duration.";previous=t;}
    if(times[0]!=0)return "The first frame must start at zero.";
    return {};
}
void MotionStudy::sample(int step){step=qBound(1,step,12);kept.resize(times.size());for(int i=0;i<kept.size();++i)kept[i]=i%step==0;}
int MotionStudy::heldFrame(int frame)const{frame=qBound(0,frame,int(times.size())-1);while(frame>0&&!kept[frame])--frame;return frame;}
QJsonObject MotionStudy::toJson()const{QJsonArray t,k;for(double v:times)t.append(v);for(bool v:kept)k.append(v);return {{"version",1},{"name",name},{"fps",fps},{"duration",duration},{"times",t},{"kept",k}};}
std::optional<MotionStudy> MotionStudy::fromJson(const QJsonObject &j,QString *error){
    MotionStudy s;auto fail=[&](QString message)->std::optional<MotionStudy>{if(error)*error=message;return {};};
    if(j["version"].toInt()!=1||!j["times"].isArray()||!j["kept"].isArray()||!j["fps"].isDouble()||!j["duration"].isDouble())return fail("Unsupported motion study.");
    s.name=j["name"].toString();s.fps=j["fps"].toDouble();s.duration=j["duration"].toDouble();
    for(auto v:j["times"].toArray()){if(!v.isDouble())return fail("Invalid timestamp.");s.times.append(v.toDouble());}
    for(auto v:j["kept"].toArray()){if(!v.isBool())return fail("Invalid frame selection.");s.kept.append(v.toBool());}
    const auto reason=s.validate();if(!reason.isEmpty())return fail(reason);return s;
}
bool appendMotionStudy(Project *project,int shotIndex,int start,const MotionStudy &study,const QString &directory,bool reference,QString *error){
    auto fail=[&](QString message){if(error)*error=message;return false;};
    if(!project||shotIndex<0||shotIndex>=project->shots.size()||start<0)return fail("Open a shot first.");
    const auto reason=study.validate();if(!reason.isEmpty())return fail(reason);
    auto result=*project;auto &shot=result.shots[shotIndex];
    const int originalLayerCount=shot.layers.size();
    const int end=start+qMax(1,int(std::ceil(study.duration*result.fps-1e-7)));
    QMap<int,int> schedule;
    for(int i=0;i<study.times.size();++i)if(study.kept[i]){
        const int destination=int(std::ceil(study.times[i]*result.fps-1e-7));
        if(start+destination<end)schedule.insert(start+destination,i);
    }
    if(schedule.size()>maxAnimationDrawings)return fail("This selection exceeds 256 drawings. Use Twos/Threes or a shorter study.");
    auto id=[] {return QUuid::createUuid().toString(QUuid::WithoutBraces);};
    auto root=RasterLayer::create((reference?"Study reference · ":"Study cels · ")+study.name);root.group=true;root.locked=reference;root.annotation=reference;
    shot.layers.append(root);AnimationTrack track;track.id=id();track.rootGroupLayerId=root.id;
    const QString slot=id();track.drawingTemplate.append({slot,"Frame",LayerKind::Raster});
    qint64 tiles=0;for(const auto &s:result.shots)for(const auto &l:s.layers)tiles+=l.tiles.size();
    for(auto it=schedule.cbegin();it!=schedule.cend();++it){
        const auto path=QDir(directory).filePath(QString("%1.png").arg(it.value()+1,6,10,QChar('0')));
        QImageReader reader(path);const QSize size=reader.size();
        if(!size.isValid()||size.width()>1024||size.height()>1024)return fail("Study frame is missing or exceeds its preview resolution limit.");
        auto image=reader.read().convertToFormat(QImage::Format_ARGB32_Premultiplied);if(image.isNull())return fail("Could not decode study frame.");
        image=image.scaled(QSize(qMin(result.width,640),qMin(result.height,640)),Qt::KeepAspectRatio,Qt::SmoothTransformation);
        auto group=RasterLayer::create(QString("Source %1").arg(it.value()+1));group.group=true;group.parentId=root.id;
        auto layer=RasterLayer::create("Frame");layer.parentId=group.id;
        // Keep the study centered without allocating a full canvas per frame.
        const int left=(result.width-image.width())/2,top=(result.height-image.height())/2;
        const QRect area(left,top,image.width(),image.height());
        for(int y=top/tileSize;y<=(area.bottom()/tileSize);++y)for(int x=left/tileSize;x<=(area.right()/tileSize);++x){
            if(++tiles>maxProjectTiles)return fail("The study exceeds the project's artwork tile budget. Use fewer frames or a smaller canvas.");
            QImage tile(tileSize,tileSize,QImage::Format_ARGB32_Premultiplied);tile.fill(Qt::transparent);QPainter painter(&tile);painter.drawImage(left-x*tileSize,top-y*tileSize,image);painter.end();layer.tiles.insert({x,y},tile);
        }
        AnimationDrawing drawing;drawing.id=id();drawing.groupLayerId=group.id;drawing.templateLayers.insert(slot,layer.id);
        shot.layers.append(group);shot.layers.append(layer);track.drawings.append(drawing);
        auto next=std::next(it);track.exposures.append({id(),drawing.id,it.key(),(next==schedule.cend()?end:next.key())-it.key()});
    }
    shot.animationTracks.append(track);shot.durationFrames=qMax(shot.durationFrames,end);
    // References sit beneath existing drawings so the artist can trace on the
    // already selected artwork without it being covered by the video image.
    if(reference)std::rotate(shot.layers.begin(),shot.layers.begin()+originalLayerCount,shot.layers.end());
    const auto validation=result.validate();if(!validation.isEmpty())return fail(validation);
    *project=std::move(result);return true;
}
}
