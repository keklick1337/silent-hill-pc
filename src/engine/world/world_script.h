// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// The script group sh.world.* of the world queries: ground under a point, rays, the map
// cell under a point, the collision group mask and the speed zone. Arguments and answers are the script API's
// integers: positions, heights and distances Q19.12, angles Q3.12, a character is its slot (0 = player,
// 1.. = NPC slot + 1, -1 = none).
//
// The queries themselves go to a host: the game answers them through the same entry points its own code uses, so a
// script sees exactly what the game sees. This file only parses arguments, picks the query and lays out the answer.

#pragma once

#include "engine/script/sh_script.h"
#include "engine/world/ray_query.h"

#include <cstdint>
#include <string_view>

namespace sh::world {

// Which ray the script asked for.
enum class ScriptRay : std::uint8_t {
    Solid,  // walls and solid pillars
    Chara,  // solid plus the characters (NPCs) but the excluded one
    Sight,  // line of sight: one-sided walls, every pillar, NPCs and the player
    Probe,  // sight without characters; thin pillars ignored
};

struct ScriptGround {
    std::int32_t height = 0;
    std::int32_t slope_x = 0;
    std::int32_t slope_z = 0;
    std::int32_t ground_type = 0;
};

struct ScriptRayHit {
    bool hit = false;
    Vec3 point{};
    std::int32_t distance = 0;
    std::int32_t slot = -1;  // the character met, or -1
    std::int32_t ground_type = 0;
    std::int32_t angle = 0;
    std::int32_t ground_height = 0;
};

class WorldScriptHost {
public:
    virtual ~WorldScriptHost() = default;
    virtual ScriptGround ground(std::int32_t x, std::int32_t z) = 0;
    // `exclude_slot` is the script's integer as given: a slot that names no character excludes nobody.
    virtual ScriptRayHit ray(ScriptRay mode, const Vec3& from, const Vec3& to, long long exclude_slot) = 0;
    // A loaded cell answers collision at (x, z).
    virtual bool cell_has_collision(std::int32_t x, std::int32_t z) = 0;
    virtual std::uint32_t collision_groups() = 0;
    // How fast the map lets Harry run at (x, z): 0 slow, 1 normal, 2 fast.
    virtual std::int32_t speed_zone(std::int32_t x, std::int32_t z) = 0;
};

// Result of world_script_call for a name outside the group: the caller passes the call on.
constexpr int kNotWorldCall = -2;

// One call of the group: `name` is "world.<fn>". Returns the count of answers written to `out`, -1 with `err` set,
// or kNotWorldCall.
int world_script_call(WorldScriptHost& host, const char* name, int argc, const ShScriptArg* argv, long long* out,
                      int out_cap, char* err, int err_cap);

// The names of the group, in the order they are listed to scripts (null-terminated).
const char* const* world_script_names();

// Puts the group in front of the API's named calls: its names are answered by `host`, every other name goes to the
// calls bound before. One binding per process (the C call chain carries no context); `host` must outlive it.
void bind_world_script(ShScriptGameApi& api, WorldScriptHost& host);

}  // namespace sh::world
