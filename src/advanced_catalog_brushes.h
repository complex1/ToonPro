#pragma once
#include "brush_library.h"
namespace toon {
QVector<BrushPreset> advancedCatalogBrushPresets();
QString advancedCatalogBrushCategory(const QString &id);
QStringList advancedCatalogBrushTags(const QString &id);
void refineCatalogBrushQuality(BrushPreset &preset);
}
