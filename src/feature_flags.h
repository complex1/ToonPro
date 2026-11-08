#pragma once

// The artist-approved reference workspace is enabled by default. Builds can
// explicitly opt out; the compatibility switch keeps its original name.
// Legacy rig/weight authoring stays retired even when this flag is enabled.
// Project readers and compatibility rendering remain available in every build.
#ifndef TOON_ENABLE_EXPERIMENTAL_3D
#define TOON_ENABLE_EXPERIMENTAL_3D 1
#endif

namespace toon::features {
inline constexpr bool experimental3D = TOON_ENABLE_EXPERIMENTAL_3D != 0;
inline constexpr bool legacy3DEditor = false;
}
