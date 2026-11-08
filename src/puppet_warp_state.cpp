#include "puppet_warp_state.h"
#include <QJsonArray>
#include <QSet>
#include <cmath>

namespace toon {
namespace {
bool number(QJsonValue value,qreal *out,qreal limit=32768){
    if(!value.isDouble())return false;*out=value.toDouble();return std::isfinite(*out)&&std::abs(*out)<=limit;
}
}
QString validatePuppetWarpState(const PuppetWarpState &state,bool pixels){
    PuppetMesh mesh;QString error;if(!buildPuppetMesh(state.bounds,state.density,state.pins,&mesh,&error))return error;
    if(state.sourceTiles.isEmpty()||state.sourceTiles.size()>1024)return "Invalid retained Puppet Warp source size.";
    if(!std::isfinite(state.featherRadius)||state.featherRadius<0||state.featherRadius>256||state.selectionPath.elementCount()>512
        ||(state.selectionActive&&state.selectionPath.isEmpty())||(!state.selectionActive&&(!state.selectionPath.isEmpty()||state.featherRadius!=0)))return "Invalid retained Puppet Warp selection.";
    for(int i=0;i<state.selectionPath.elementCount();++i){const auto e=state.selectionPath.elementAt(i);if(!std::isfinite(e.x)||!std::isfinite(e.y)||std::abs(e.x)>32768||std::abs(e.y)>32768)return "Invalid retained selection coordinates.";}
    for(auto tile=state.sourceTiles.cbegin();tile!=state.sourceTiles.cend();++tile)
        if(std::abs(tile.key().first)>128||std::abs(tile.key().second)>128||(pixels&&(tile->size()!=QSize(256,256)||tile->format()!=QImage::Format_ARGB32_Premultiplied)))return "Invalid retained Puppet Warp source tile.";
    return {};
}
QJsonObject puppetWarpStateToJson(const PuppetWarpState &state){
    QJsonArray pins,tiles,path;
    for(const auto &pin:state.pins)pins.append(QJsonArray{pin.source.x(),pin.source.y(),pin.target.x(),pin.target.y()});
    for(auto tile=state.sourceTiles.cbegin();tile!=state.sourceTiles.cend();++tile)tiles.append(QJsonArray{tile.key().first,tile.key().second});
    for(int i=0;i<state.selectionPath.elementCount();++i){const auto e=state.selectionPath.elementAt(i);path.append(QJsonArray{int(e.type),e.x,e.y});}
    return {{"version",1},{"bounds",QJsonArray{state.bounds.x(),state.bounds.y(),state.bounds.width(),state.bounds.height()}},{"density",state.density},{"pins",pins},{"sourceTiles",tiles},
        {"selectionActive",state.selectionActive},{"selectionPath",path},{"fillRule",int(state.selectionPath.fillRule())},{"feather",state.featherRadius}};
}
bool puppetWarpStateFromJson(const QJsonObject &object,PuppetWarpState *out,QString *error){
    const auto fail=[&](const QString &message){if(error)*error=message;return false;};
    if(!out)return fail("Missing Puppet Warp destination.");
    const QSet<QString> fields{"version","bounds","density","pins","sourceTiles","selectionActive","selectionPath","fillRule","feather"};
    if(object.size()!=fields.size())return fail("Invalid retained Puppet Warp fields.");for(const auto &key:object.keys())if(!fields.contains(key))return fail("Invalid retained Puppet Warp fields.");
    if(object["version"]!=1||!object["bounds"].isArray()||!object["pins"].isArray()||!object["sourceTiles"].isArray()||!object["selectionPath"].isArray()||!object["selectionActive"].isBool())return fail("Invalid retained Puppet Warp metadata.");
    PuppetWarpState state;const auto bounds=object["bounds"].toArray();qreal b[4];if(bounds.size()!=4)return fail("Invalid retained mesh bounds.");for(int i=0;i<4;++i)if(!number(bounds[i],&b[i],65536))return fail("Invalid retained mesh bounds.");state.bounds={b[0],b[1],b[2],b[3]};
    qreal density,fillRule;if(!number(object["density"],&density,32)||density!=std::floor(density)||!number(object["fillRule"],&fillRule,1)||fillRule!=std::floor(fillRule)||fillRule<0||!number(object["feather"],&state.featherRadius,256))return fail("Invalid retained mesh settings.");state.density=int(density);
    const auto pins=object["pins"].toArray();if(pins.size()>maxPuppetPins)return fail("Too many retained pins.");
    for(const auto &value:pins){const auto p=value.toArray();qreal v[4];if(!value.isArray()||p.size()!=4)return fail("Invalid retained pin.");for(int i=0;i<4;++i)if(!number(p[i],&v[i]))return fail("Invalid retained pin coordinates.");state.pins.append({{v[0],v[1]},{v[2],v[3]}});}
    const auto tiles=object["sourceTiles"].toArray();if(tiles.isEmpty()||tiles.size()>1024)return fail("Invalid retained source tile count.");
    for(const auto &value:tiles){const auto p=value.toArray();qreal x,y;if(!value.isArray()||p.size()!=2||!number(p[0],&x,128)||!number(p[1],&y,128)||x!=std::floor(x)||y!=std::floor(y))return fail("Invalid retained source tile position.");const QPair<int,int> key{int(x),int(y)};if(state.sourceTiles.contains(key))return fail("Duplicate retained source tile.");state.sourceTiles.insert(key,{});}
    const auto path=object["selectionPath"].toArray();if(path.size()>512)return fail("Retained selection is too detailed.");
    QVector<QPointF> points;QVector<int> types;
    for(const auto &value:path){const auto p=value.toArray();qreal type,x,y;if(!value.isArray()||p.size()!=3||!number(p[0],&type,3)||type!=std::floor(type)||type<0||!number(p[1],&x)||!number(p[2],&y))return fail("Invalid retained selection path.");types.append(int(type));points.append({x,y});}
    for(int i=0;i<types.size();++i){if(types[i]==0)state.selectionPath.moveTo(points[i]);else if(types[i]==1&&i>0)state.selectionPath.lineTo(points[i]);else if(types[i]==2&&i>0&&i+2<types.size()&&types[i+1]==3&&types[i+2]==3){state.selectionPath.cubicTo(points[i],points[i+1],points[i+2]);i+=2;}else return fail("Invalid retained selection curve.");}
    state.selectionPath.setFillRule(Qt::FillRule(int(fillRule)));state.selectionActive=object["selectionActive"].toBool();
    const auto validation=validatePuppetWarpState(state,false);if(!validation.isEmpty())return fail(validation);*out=std::move(state);return true;
}
}
