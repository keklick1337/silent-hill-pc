// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// The readable names gamedata/config/ files use for the game's top-level states and in-game system states
// ("InGame", "SaveScreen", "StatusMenu", ...), as in sh::game_state.

#pragma once

#include <cstdint>
#include <string_view>

namespace sh::gameflow {

// -1 when the name is unknown.
std::int32_t game_state_by_name(std::string_view name);
std::int32_t sys_state_by_name(std::string_view name);
// The name of a state value; empty when the value has none.
std::string_view game_state_name(std::int32_t value);
std::string_view sys_state_name(std::int32_t value);

}  // namespace sh::gameflow
