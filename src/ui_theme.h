#pragma once

namespace toon {
void applyTheme();

namespace ui {
// Spectrum-inspired neutral surfaces, refined for the compact drawing workspace.
// Selection and focus use silver so the artist's work supplies the color.
inline constexpr char canvas[] = "#1b1b1b";
inline constexpr char panel[] = "#282828";
inline constexpr char base[] = "#222222";
inline constexpr char raised[] = "#303030";
inline constexpr char hover[] = "#393939";
inline constexpr char border[] = "#444444";
inline constexpr char text[] = "#e8e8e8";
inline constexpr char muted[] = "#b5b5b5";
inline constexpr char disabled[] = "#6d6d6d";
inline constexpr char accent[] = "#bdbdbd";
inline constexpr char accentHover[] = "#d5d5d5";
inline constexpr char selected[] = "#414141";
inline constexpr char focus[] = "#dedede";
inline constexpr char onionPrevious[] = "#ff7c98";
inline constexpr char onionNext[] = "#62c4e5";
inline constexpr int tooltipDelayMs = 600;
}
}
