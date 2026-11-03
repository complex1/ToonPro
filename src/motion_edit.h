#pragma once
#include "property_animation.h"
namespace toon {
// Normalize explicit curve editing to paired position keys. Existing key values
// are retained; missing axes are sampled before either channel is changed.
void pairMotionChannels(LayerAnimation &animation);
void insertMotionKey(LayerAnimation &animation, int frame);
} // namespace toon
