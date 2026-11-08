#pragma once
#include <QImage>
#include <QPainter>
#include <QRandomGenerator>

inline QImage lineArtArchitectureFixture(bool noise) {
    QImage image(640,420,QImage::Format_ARGB32_Premultiplied);image.fill(QColor(240,240,240));
    {
        QPainter p(&image);p.setRenderHint(QPainter::Antialiasing);
        p.setPen(QPen(QColor(40,40,40),1.6));p.setBrush(QColor(226,226,226));
        p.drawPolygon(QPolygonF({{60,160},{240,70},{530,150},{500,350},{60,350}}));
        p.drawLine(QPointF(240,70),QPointF(240,350));p.drawLine(QPointF(60,160),QPointF(530,150));
        p.setBrush(Qt::NoBrush);p.setPen(QPen(QColor(95,95,95),1.3));
        for(int row=0;row<3;++row)for(int column=0;column<3;++column){
            const QRectF rect(82+column*48,185+row*48,29,33);p.drawRect(rect);p.drawLine(rect.center().x(),rect.top(),rect.center().x(),rect.bottom());
        }
        p.drawEllipse(QRectF(365,190,78,116));
        p.setPen(QPen(QColor(70,70,70),1));p.drawLine(QPointF(12,110),QPointF(625,25));
        p.setPen(QPen(QColor(175,175,175),1));p.drawLine(QPointF(80,390),QPointF(580,390));
    }
    if(noise){QRandomGenerator random(0x419c);
        for(int y=0;y<image.height();++y){auto *row=reinterpret_cast<QRgb *>(image.scanLine(y));for(int x=0;x<image.width();++x){
            int amount=int(random.bounded(21))-10;if(random.bounded(100)<2)amount+=random.bounded(2)?35:-35;
            const int value=qBound(0,qRed(row[x])+amount,255);row[x]=qRgb(value,value,value);
        }}
    }
    return image;
}
