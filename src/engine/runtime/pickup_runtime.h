// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// Runtime pickups in the running game (the registry is engine/world/runtime_pickups.h).
// A runtime pickup takes the path of a pickup of the map data: its model is a world object drawn next to the map's
// own, the action button facing it starts a map event (a synthetic event record) whose step runs the game's item
// prompt (the model turns, "Take it?", "yes" adds the item and keeps the flag set), and a set flag hides it.

#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

namespace sh::world {

// A new map is loading: the registry starts empty and resolves names against the game from now on.
void runtime_pickups_begin_map(std::string_view map);
// Puts pickups waiting for the ground on it (one per frame) and draws the visible ones (after the map's objects).
void runtime_pickups_draw();
// The action button went down and no map event took it: starts the prompt of the first pickup in reach that
// `facing` (x, z) accepts. True when one started.
bool runtime_pickups_try_start(const std::array<std::int32_t, 3>& player,
                               const std::function<bool(std::int32_t x, std::int32_t z)>& facing);
// Starts the prompt of pickup `id` wherever the player stands (a script's sh.world.pickup_take). True when it
// started: the pickup lies on the map and is not taken.
bool runtime_pickups_start(std::string_view id);
// A runtime pickup's prompt is the current map event: its id and where it lies.
bool runtime_pickups_active(std::string* id, std::array<std::int32_t, 3>* position);
// The map event step of a running prompt; false when the current event is not one.
bool runtime_pickups_run_event();

}  // namespace sh::world
