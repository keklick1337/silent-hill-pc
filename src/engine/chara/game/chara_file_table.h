// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// The files and the look of every character type of the running game: its animation, model and
// texture files (numbers of the running file table, -1 none), the blend of its materials, its shadow and where the
// camera looks at it. Built at boot from gamedata/chara/ (a mod's copy first); the disc region decides the Grey
// Child's model. The game retargets rows while it runs (the cast's animation files), so the rows are
// writable.

#pragma once

#include <cstdint>
#include <filesystem>

namespace sh::chara::game {

enum class CharaBlend : std::uint8_t { Average = 0, Additive = 1, Subtractive = 2 };
enum class CameraAnchor : std::uint8_t { Character = 0, Ground = 1, Camera = 2 };

struct CharaFileRow {
    std::int16_t anim = -1;
    std::int16_t model = -1;
    std::int16_t texture = -1;
    std::int16_t shadow_q8 = 0;             // 10 bits in the original row
    std::uint8_t blend = 0;                 // CharaBlend, 6 bits
    std::uint8_t camera_anchor = 0;         // CameraAnchor, 2 bits
    std::int16_t camera_offset_y_q8 = 0;    // 14 bits
};

constexpr int kCharaFileRows = 256;  // every run-time number: None .. Padlock, then the mods' new characters

// The row of a type; types outside the table share one scratch row (reads give -1 files).
CharaFileRow& chara_file_row(int chara_id);

// The disc file the "model" of the type's character.json stood for when the table was built (-1: none). The row's
// own model can move away from it afterwards (a region's model, a retarget at run time).
std::int32_t chara_data_model_file(int chara_id);

// Builds the table from the character library (once; later calls do nothing).
void chara_files_load(const std::filesystem::path& gamedata_dir, const std::filesystem::path& mods_dir);
// `mods reload`: every row back to its start value, then the table built again from the data.
void chara_files_reload(const std::filesystem::path& gamedata_dir, const std::filesystem::path& mods_dir);

// The value an original row's bit field of `bits` bits keeps of `value` (signed fields wrap like the original's).
constexpr std::int32_t wrap_signed_bits(std::int32_t value, int bits) {
    const std::uint32_t mask = (1u << bits) - 1u;
    const std::uint32_t v = static_cast<std::uint32_t>(value) & mask;
    return (v & (1u << (bits - 1))) != 0 ? static_cast<std::int32_t>(v) - static_cast<std::int32_t>(1u << bits)
                                         : static_cast<std::int32_t>(v);
}

}  // namespace sh::chara::game
