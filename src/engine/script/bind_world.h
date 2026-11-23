// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)

#pragma once

#include <functional>
#include <optional>

struct lua_State;

namespace sh::script {

class Host;

// Pushes the queries mods need into the sh table on top of the stack (WorldQueryHost):
//   sh.game.difficulty()                          -> "easy" | "normal" | "hard", nil outside a game
//   sh.player.facing()                            -> Q12 angle; forward is (sin, cos) of it
//   sh.world.walkable(x, z [, from_x, from_z])    -> bool, ground y
//   sh.world.nearest_walkable(x, z [, radius [, from_x, from_z]]) -> x, z, y | nil
//   sh.entity.spawn{chara=, x=, z=, rot=, ai=, state=} -> slot | nil, error
void install_world_family(lua_State* L, Host* host);

// The search of nearest_walkable: rings of `step` around (x, z) out to `radius`, nearest first; the first point
// `walkable` accepts, as {x, z}.
struct WalkPoint {
    int x = 0;
    int z = 0;
};
std::optional<WalkPoint> nearest_walkable(int x, int z, int radius, int step, const std::function<bool(int, int)>& walkable);

}  // namespace sh::script
