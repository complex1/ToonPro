#pragma once

#include "brush_library.h"

namespace toon {
// Original native presets inspired by common drawing-tool families. This is the
// additional starter pack; BrushLibrary keeps its three legacy presets first.
QVector<BrushPreset> standardBrushPresets();

// Metadata for starter presets (including the three legacy IDs). Unknown or
// imported IDs return empty metadata, leaving artist-owned organization alone.
QString standardBrushCategory(const QString &presetId);
QStringList standardBrushTags(const QString &presetId);
} // namespace toon
