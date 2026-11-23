// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// Sound driver tables as data: gamedata/config/sound_tables.json.
// Reverb depths, music layer channel maps, VAB/KDT file locations, sound effect programs and XA lines.
// Rows are positional (the row number is the id game code and saves use). Mods patch the file in load order;
// rows have no key, so a mod's array replaces the base array whole.

#pragma once

#include "formats/common/status.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace sh::gconfig {

constexpr std::size_t kReverbDepthCount = 36;
constexpr std::size_t kLayerChannelRows = 41;
constexpr std::size_t kLayerChannelCols = 16;
constexpr std::size_t kSoundFileCount = 127;
constexpr std::size_t kSoundEffectCount = 420;
constexpr std::size_t kXaLineCount = 727;

// Bank numbers: 0 BaseAudio/MusicKey, 1 Weapon, 2 Ambient/SpecialScreen, 3 MusicBank.
struct SoundFile {
    std::uint8_t bank = 0;
    std::uint16_t vag_data_offset = 0;
    std::uint32_t size = 0;
    std::int32_t sector = 0;
};

struct SoundEffect {
    std::uint8_t audio_index = 0;
    std::uint8_t bank = 0;
    std::uint8_t program = 0;
    std::uint8_t note = 0;
    std::int8_t volume = 0;
};

struct XaLine {
    std::uint8_t xa_file = 0;
    std::uint32_t sector = 0;      // 24 bits
    std::uint8_t group_index = 0;
    std::uint32_t length = 0;      // 24 bits
    std::uint8_t group = 0;
};

struct SoundTables {
    std::array<std::uint8_t, kReverbDepthCount> reverb_depths{};
    std::array<std::array<std::uint8_t, kLayerChannelCols>, kLayerChannelRows> layer_channels{};
    std::vector<SoundFile> files;
    std::vector<SoundEffect> effects;
    std::vector<XaLine> xa_lines;

    static Result<SoundTables> load(const std::filesystem::path& gamedata_dir, const std::filesystem::path& mods_dir,
                                    std::vector<std::string>& warnings, std::string* mod_id = nullptr);
    static Result<SoundTables> parse(std::string_view json);

    // All rows zero (what the game ran on before a load, and after a failed one).
    static SoundTables empty();
};

// The tables the running game uses: empty() until the boot loads them; the disc's file table may move a file's
// sector afterwards.
struct ActiveSoundTables {
    SoundTables tables = SoundTables::empty();
};
ActiveSoundTables& active_sound_tables();
// Loads gamedata/config/sound_tables.json (with the mods' patches) into active_sound_tables(); on an error the
// tables stay as they were. Warnings and the error go to `messages`.
bool load_active_sound_tables(const std::filesystem::path& gamedata_dir, const std::filesystem::path& mods_dir,
                              std::vector<std::string>& messages, std::string* mod_id = nullptr);

}  // namespace sh::gconfig
