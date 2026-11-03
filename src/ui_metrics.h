#pragma once

namespace toon::ui::metrics {
// Logical pixels. Font-aware rows may grow; icon glyphs and pen targets have
// separate roles so compact chrome does not reduce the clickable area.
inline constexpr int actionIcon = 16;
inline constexpr int primaryIcon = 18;
inline constexpr int detailIcon = 20;
inline constexpr int actionTarget = 28;
inline constexpr int railWidth = 40;
inline constexpr int railTarget = 32;
inline constexpr int toolRow = 30;
inline constexpr int layerRow = 32;
inline constexpr int layerThumbnail = 24;
inline constexpr int previewWidth = 46;
inline constexpr int previewHeight = 24;
inline constexpr int propertyHeight = 28;
inline constexpr int numberWidth = 48;
inline constexpr int editorMinimumWidth = 128;
inline constexpr int labelWidth = 64;
inline constexpr int controlGap = 4;
inline constexpr int panelMargin = 4;
inline constexpr int sectionGap = 8;
inline constexpr int tabIcon = 16;
inline constexpr int tabHeight = 28;
inline constexpr int presetTileWidth = 40;
inline constexpr int presetTileHeight = 38;
inline constexpr int presetCueSize = 16;
} // namespace toon::ui::metrics
