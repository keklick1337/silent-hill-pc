#pragma once

// What a map says about itself in gamedata/maps/<map>/ besides its data kinds, read into the
// running map (map_runtime.h):
//
//   overlay.json   "map_type" (one of MapRuntime::kMapTypeNames), "characters" (the character types the map
//                  updates through their gamedata/chara/<Name>/behavior.lua), "character_effects" (the types whose
//                  own map effects the map draws), "effect_draws" (MapRuntime::kMapEffectNames)
//   handlers.json  "handlers": [{"index", "shape"}]: a slot with a shape runs the map's events.lua function of that
//                  index; an empty one (no shape) has no handler
//
// Both texts are the base file with every active mod's patch already merged (vfs_data_json.h). False (logged under
// the relative path) when a text is broken; the runtime then keeps what was read before the error.

#include "engine/maps/map_runtime.h"

#include <string_view>

namespace sh::maps {

bool apply_overlay_description(MapRuntime& runtime, std::string_view relative, std::string_view text);
bool apply_event_handlers(MapRuntime& runtime, std::string_view relative, std::string_view text);

}  // namespace sh::maps
