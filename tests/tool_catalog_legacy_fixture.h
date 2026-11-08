#pragma once
#include "canvas.h"
#include <QJsonArray>
#include <QJsonObject>
#include <QSet>

namespace toon::test_fixture {
// Historical fixtures must omit newer engine/guide identities and editor
// settings, rather than disguising the current catalog with an older number.
inline QJsonObject toolCatalogBefore14(QJsonObject root,int version=13){
    root["version"]=version;QSet<QString> removed;QJsonArray tools;
    for(const auto&value:root["tools"].toArray()){
        auto tool=value.toObject();const auto engine=Canvas::Tool(tool["engine"].toInt());
        if(engine==Canvas::Tool::VectorNudge||(engine==Canvas::Tool::Guides&&tool["variant"].toInt()==int(GuideType::Ellipse))){removed.insert(tool["id"].toString());continue;}
        auto options=tool["options"].toObject();for(const auto*key:{"stabilizationMode","stabilizationDeadZone","stabilizationFinish","nudgeRadius","nudgeStrength"})options.remove(key);tool["options"]=options;tools.append(tool);
    }
    root["tools"]=tools;QJsonArray entries;
    for(const auto&value:root["entries"].toArray()){
        auto entry=value.toObject();QJsonArray members;for(const auto&id:entry["tools"].toArray())if(!removed.contains(id.toString()))members.append(id);
        if(!entry["group"].toBool()&&members.isEmpty())continue;entry["tools"]=members;
        if(removed.contains(entry["lastToolId"].toString()))entry["lastToolId"]=members.isEmpty()?QString():members.first().toString();entries.append(entry);
    }
    root["entries"]=entries;return root;
}
}
