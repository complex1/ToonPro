#pragma once
#include "puppet_warp.h"
#include <QImage>
#include <QJsonObject>
#include <QMap>
#include <QPainterPath>

namespace toon {
struct PuppetWarpState {
    QRectF bounds;
    int density=16;
    QVector<PuppetPin> pins;
    QMap<QPair<int,int>,QImage> sourceTiles;
    bool selectionActive=false;
    QPainterPath selectionPath;
    qreal featherRadius=0;
    bool operator==(const PuppetWarpState &) const = default;
};
QString validatePuppetWarpState(const PuppetWarpState &,bool pixels=true);
QJsonObject puppetWarpStateToJson(const PuppetWarpState &);
bool puppetWarpStateFromJson(const QJsonObject &,PuppetWarpState *,QString *error);
}
