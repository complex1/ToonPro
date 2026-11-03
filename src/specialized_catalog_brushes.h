#pragma once
#include "brush_library.h"
namespace toon {
QVector<BrushPreset> specializedCatalogBrushPresets(bool corrected = true);
QString specializedCatalogBrushCategory(const QString &id);
QStringList specializedCatalogBrushTags(const QString &id);
}
