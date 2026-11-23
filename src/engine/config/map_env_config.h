#pragma once

// The numbers of the map environment blend (src/engine/world/map_env.h), from gamedata/config/map_effects.json (a
// mod replaces the file whole), loaded on first use. Defaults when the file is missing or broken (logged).

#include "engine/world/map_env.h"

namespace sh::world {

const MapEnvParams& map_env_params();

}  // namespace sh::world
