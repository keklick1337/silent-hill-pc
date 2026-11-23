// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// Harry's base clip table (harry_clip_table.cpp): gamedata/chara/Harry/base_clips.json into HARRY_BASE_ANIM_INFOS.

#pragma once

#include <filesystem>

namespace sh::inventory::game {

// Clip statuses are one byte: every status has an entry.
inline constexpr int kHarryClipSlots = 256;

// Reads the base clips (the last mod that ships the file wins); the table stays as it is on an error.
void load_harry_clip_table(const std::filesystem::path& gamedata_dir, const std::filesystem::path& mods_dir);

}  // namespace sh::inventory::game
