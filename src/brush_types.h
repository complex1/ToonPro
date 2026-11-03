#pragma once

#include "brush_settings.h"

namespace toon {

// Organization metadata and starting settings, not separate rendering engines.
enum class BrushType { Pen, Ink, Soft, Spray, Stamp, ColoredStamp, Custom, DynamicStamp };

QString brushTypeName(BrushType type);
QString brushTypeIcon(BrushType type);
BrushSettings brushSettingsForType(BrushType type);
qreal brushTypeSmoothing(BrushType type);
BrushType inferredBrushType(const QString &name, const BrushSettings &brush);

} // namespace toon
