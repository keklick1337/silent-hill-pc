// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// The values of PlayerVar that live in the engine's PlayerState (player_state.h) rather than in the character
// record: read and written here like an assignment to the field (its low bits).

#pragma once

#include "engine/player/player_state.h"
#include "engine/player/player_vars.h"

#include <cstdint>

namespace sh::game {

// False when the value is not one of PlayerState's.
bool player_state_get(const PlayerState& state, PlayerVar v, std::int64_t* out);
// True when the value is PlayerState's (the equip block's values are read-only: a write is dropped).
bool player_state_set(PlayerState& state, PlayerVar v, std::int64_t value);
bool player_state_owns(PlayerVar v);

}  // namespace sh::game
