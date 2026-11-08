#pragma once
#include "project.h"
#include <QPainter>

inline toon::Project puppetWarpFixture(){
    using namespace toon;
    auto project=Project::create(ProjectKind::DrawingOnly);project.width=480;project.height=360;project.shots[0].sheet.overscan=0;
    QImage image(480,360,QImage::Format_ARGB32_Premultiplied);image.fill(Qt::transparent);
    {
        QPainter p(&image);p.setRenderHint(QPainter::Antialiasing);
        // An original raster fabric sample with crisp ink and translucent detail.
        QLinearGradient fabric(70,60,390,300);fabric.setColorAt(0,QColor("#6dc3d8"));fabric.setColorAt(1,QColor("#227aab"));
        p.setPen(QPen(QColor("#16364b"),3));p.setBrush(fabric);p.drawRoundedRect(QRectF(60,50,360,260),18,18);
        p.save();QPainterPath clip;clip.addRoundedRect(QRectF(61,51,358,258),17,17);p.setClipPath(clip);
        p.setPen(QPen(QColor(235,250,250,150),2));for(int y=65;y<310;y+=24)p.drawLine(60,y,420,y);for(int x=76;x<420;x+=24)p.drawLine(x,50,x,310);
        p.setPen(QPen(QColor("#16364b"),3));p.setBrush(QColor("#f6cd70"));p.drawEllipse(QRectF(182,120,116,116));
        p.setBrush(QColor("#f17e66"));p.drawRoundedRect(QRectF(215,148,50,62),9,9);p.restore();
        p.setPen(QPen(QColor(80,100,120,100),2));p.setBrush(QColor(255,255,255,80));p.drawEllipse(QRectF(96,87,22,22));
    }
    auto &layer=project.shots[0].layers[0];layer.name="Raster fabric";layer.tiles.clear();
    for(int y=0;y<image.height();y+=tileSize)for(int x=0;x<image.width();x+=tileSize){
        QImage tile(tileSize,tileSize,QImage::Format_ARGB32_Premultiplied);tile.fill(Qt::transparent);QPainter p(&tile);p.drawImage(-x,-y,image);p.end();layer.tiles[{x/tileSize,y/tileSize}]=tile;
    }
    return project;
}
