#pragma once
#include "project.h"
#include <QUuid>
namespace animation_fixture {
inline QString id() { return QUuid::createUuid().toString(QUuid::WithoutBraces); }
inline toon::Project project() {
    using namespace toon;
    auto project=Project::create(); project.width=128; project.height=128;
    auto &shot=project.shots.first(); shot.durationFrames=20;
    auto root=RasterLayer::create("Character"); root.group=true;
    AnimationTrack track; track.id=id(); track.rootGroupLayerId=root.id; track.drawingTemplate={{"ink","Ink"}};
    shot.layers.append(root);
    for(int i=0;i<2;++i) {
        auto group=RasterLayer::create(QString("Drawing %1").arg(i+1)); group.group=true; group.parentId=root.id;
        auto layer=RasterLayer::create("Ink"); layer.parentId=group.id;
        QImage tile(tileSize,tileSize,QImage::Format_ARGB32_Premultiplied); tile.fill(i?Qt::blue:Qt::red); layer.tiles.insert({0,0},tile);
        AnimationDrawing drawing{id(),group.id,{{"ink",layer.id}}};
        track.drawings.append(drawing); track.exposures.append({id(),drawing.id,i*4,3});
        shot.layers.append(group); shot.layers.append(layer);
    }
    shot.animationTracks.append(track); return project;
}
inline int layerIndex(const toon::Shot &shot,const QString &id) { for(int i=0;i<shot.layers.size();++i) if(shot.layers[i].id==id) return i; return -1; }
}
