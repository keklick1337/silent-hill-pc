// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// The game's host of sh::entities::EntityWorld and of the sh.entity.* core calls: the character
// library and the behaviour variable layouts, the bones the game poses for a slot, an engine spawn into a free NPC
// slot and its removal; and the animation bank's loader for the animation entry points.

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace sh::entities {
class EntityWorld;
}

namespace sh::entities::game {

// The engine's world with the game's host installed.
EntityWorld& world();

// A behaviour variable of the slot by name (EntityWorld::var / set_var).
std::optional<long long> var_get(int slot, std::string_view name);
bool var_set(int slot, std::string_view name, long long value);

// The file queue finished reading `file` into `data`: the animation bank learns where the file lives.
void anim_file_loaded(std::int32_t file, void* data);
// The engine samples [data, data + size) from the import's copy of `game_path` ("ANIM/GHOST.ANM", a mod's file
// first) instead of the disc file read there (a mod's new character over its carrier's file).
void anim_file_rebind(void* data, std::size_t size, const std::string& game_path);

}  // namespace sh::entities::game
