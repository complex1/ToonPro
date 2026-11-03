#pragma once

#include "brush_library.h"

namespace toon {
inline constexpr int catalogBrushRecipeRevision = 5;
// Original, ready-to-draw recipes from the researched anime brush catalog.
// The starter pack remains separate so its existing IDs and behavior never change.
// Revision 1 reproduces the shipped 0.42 recipes for non-destructive preference
// upgrades. Callers creating brushes should use the current default revision.
QVector<BrushPreset> catalogBrushPresets(int revision = catalogBrushRecipeRevision);
QString catalogBrushCategory(const QString &presetId);
QStringList catalogBrushTags(const QString &presetId);
} // namespace toon
