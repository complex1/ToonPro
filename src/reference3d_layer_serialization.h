#pragma once
#include "reference3d.h"
#include <QJsonArray>

namespace toon {
// Small scene metadata references a project-wide, content-addressed asset table.
QJsonObject referenceLayerSceneToJson(const ReferenceScene &scene);
std::optional<ReferenceScene> referenceLayerSceneFromJson(const QJsonObject &json,
    const QHash<QString, ReferenceAssetPtr> &assets, QString *error = nullptr);
QJsonArray referenceLayerAssetsToJson(const QHash<QString, ReferenceAssetPtr> &assets);
bool referenceLayerAssetsFromJson(const QJsonArray &json, QHash<QString, ReferenceAssetPtr> *assets,
    QString *error = nullptr);
bool referenceLayerScenesEqual(const ReferenceScene &a, const ReferenceScene &b);
} // namespace toon
