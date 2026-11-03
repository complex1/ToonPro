#pragma once
#include <QtGlobal>

namespace toon {
enum class BrushConnectedMode { Off, Ribbon, Repeat };
struct BrushConnectedSettings {
    BrushConnectedMode mode = BrushConnectedMode::Off;
    // Motif length in brush widths. Ribbon also retains the image aspect ratio.
    qreal repeatLength = 1;
    // Captured fragment offset preserves a repeat when vector erasing splits it.
    qreal phaseOffset = 0;
    bool flipAlternate = false;
    bool operator==(const BrushConnectedSettings &) const = default;
};
} // namespace toon
