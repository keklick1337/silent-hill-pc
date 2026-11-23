// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// The running game's gameplay numbers and difficulty picker (gamedata/config/gameplay.json and difficulties.json):
// one GameplayLibrary and one DifficultyLibrary, loaded once at boot (base data, then the highest-priority
// mod's copy). Until then, or when a file is missing or broken, both answer the original's values.
//
// The C entry points below are what the game code calls where the original had its constants; C++ callers
// use sh::gconfig::active_gameplay() / active_difficulties().

#pragma once

#include <stdint.h>

#ifdef __cplusplus
#include <filesystem>

namespace sh::gconfig {

class GameplayLibrary;
class DifficultyLibrary;

// Loads both files once; later calls do nothing. Problems are logged and keep the original's values.
void load_active_config(const std::filesystem::path& gamedata_dir, const std::filesystem::path& mods_dir);
// `mods reload`: both files read again (a file that fails keeps the original's values, as at boot).
void reload_active_config(const std::filesystem::path& gamedata_dir, const std::filesystem::path& mods_dir);

const GameplayLibrary& active_gameplay();
const DifficultyLibrary& active_difficulties();

}  // namespace sh::gconfig

extern "C" {
#endif

void Gameplay_GamedataInit(const char* gamedataDir, const char* modsDir);
void Difficulty_GamedataInit(const char* gamedataDir, const char* modsDir);

// Caps and restores in Q12.
int32_t Gameplay_MaxHealthQ12(void);
int32_t Gameplay_MaxSaturationQ12(void);
// The health an item id restores (health drink, first aid kit, ampoule); 0 for anything else.
int32_t Gameplay_HealAmountQ12(int32_t itemId);
// Escape inputs for a grab situation (sh::gconfig::Grab) at a difficulty (-1 easy, 0 normal, 1 hard).
int32_t Gameplay_GrabEscapeCount(int32_t grab, int32_t difficulty);

// The New Game picker: entries in file order.
int32_t Difficulty_Count(void);
const char* Difficulty_Label(int32_t index);
int32_t Difficulty_BaseDifficulty(int32_t index);
// The entry's own start map name, NULL for the manifest's default.
const char* Difficulty_StartMap(int32_t index);
int32_t Difficulty_DefaultIndex(void);

#ifdef __cplusplus
}
#endif
