// SPDX-License-Identifier: GPL-3.0-or-later
// The world draw distance (src/engine/world/mesh_draw.h, docs/RENDER.md section 5.3): the far drop distance
// the scene hands the fog ramp as its cull depth, from the config, the fog and the whole-map mode.

#include <cstdint>

#include "engine/world/world_atmosphere.h"

#include "engine/config/pc_config.h"
#include "engine/world/mesh_draw.h"

extern "C" std::int32_t Pc_FogFarDist(void);
extern "C" int Pc_WholeMapDrawActive(void);

extern "C" std::int32_t Pc_MeshFarLimit(std::int32_t shift) {
    return sh::world::mesh_far_limit(g_PcConfig.drawDistancePct, shift,
                                     sh::world::atmosphere().state().fog_enabled != 0, Pc_FogFarDist(),
                                     Pc_WholeMapDrawActive() != 0);
}
