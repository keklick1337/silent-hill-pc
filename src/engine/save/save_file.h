// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
#pragma once

// One save file of .savegames/: a fixed header with the format version, the kind
// of file and a CRC-32 of the payload, then tagged chunks. Readers skip chunks they do not know, so later versions
// and mods can add chunks without breaking older builds.

#include "formats/common/status.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace sh::save {

inline constexpr std::array<char, 8> kMagic = {'S', 'H', 'P', 'C', 'S', 'A', 'V', 'E'};
// 2: the game state is the STAT chunk (named, save_game.h). Files of version 1 (GAME, NAME, OPTS) are still read.
inline constexpr std::uint16_t kFormatVersion = 2;
inline constexpr std::size_t kHeaderSize = 32;

enum class FileKind : std::uint16_t {
    Slot = 1,   // one save: INFO, STAT, META (format 1: INFO, GAME, NAME, OPTS, META)
    Group = 2,  // format 1 only: a file group of the old card layout, OPTS when the game wrote its options
    Quick = 3,  // a quick save, as Slot
};

using Tag = std::array<char, 4>;
constexpr Tag make_tag(const char (&text)[5]) { return {text[0], text[1], text[2], text[3]}; }

inline constexpr Tag kTagInfo = make_tag("INFO");  // SlotSummary, 16 bytes
// UTF-8 JSON "sh-savegame/1": the whole game state by name, options included (save_game.h).
inline constexpr Tag kTagState = make_tag("STAT");
inline constexpr Tag kTagMeta = make_tag("META");
// Optional: UTF-8 JSON "sh-remains/1", the bodies and blood pools kept by `persistent_remains`
// (engine/entities/remains.h). Written only when something is kept; a save without it keeps nothing.
inline constexpr Tag kTagRemains = make_tag("RMNS");  // UTF-8 JSON for people and tools; never read back by the game
// Format 1 only, read for the migration: the game's raw save record, its options record and the names of its
// things (save_game.h, from_legacy).
inline constexpr Tag kTagGame = make_tag("GAME");
inline constexpr Tag kTagOptions = make_tag("OPTS");
inline constexpr Tag kTagNames = make_tag("NAME");

struct Chunk {
    Tag tag{};
    std::vector<std::uint8_t> data;
};

struct SaveFile {
    std::uint16_t version = kFormatVersion;  // written as is (tests and tools make format 1 files)
    FileKind kind = FileKind::Slot;
    std::vector<Chunk> chunks;

    // First chunk with the tag, or null.
    const Chunk* find(Tag tag) const;
    // Replaces the first chunk with the tag, or appends one.
    void put(Tag tag, std::vector<std::uint8_t> data);
};

std::vector<std::uint8_t> encode(const SaveFile& file);
// Fails on a wrong magic, a newer format version, a size that does not match or a bad CRC.
Result<SaveFile> decode(std::span<const std::uint8_t> bytes);

// The game's entry of a save in the list of saves, field by field: what the save screen shows without reading the
// save itself.
struct SlotSummary {
    std::int32_t sequence = 0;     // grows with every save on any card; 0 = the slot is empty for the game
    std::uint32_t play_time = 0;   // play time, 1/4096 s
    std::uint16_t save_count = 0;  // how many times this playthrough was saved
    std::uint8_t location = 0;     // where it was saved (save point index)
    bool next_fear = false;        // a "Next Fear" playthrough
    std::uint8_t extra_hours = 0;  // play time overflow counter (0..3)
    std::uint8_t special_items = 0;  // special items picked up (0..31)

    bool operator==(const SlotSummary&) const = default;
};

std::vector<std::uint8_t> encode_summary(const SlotSummary& summary);
Result<SlotSummary> decode_summary(std::span<const std::uint8_t> bytes);

// Writes `bytes` to `path` so that a reader sees either the old file or the whole new one: a temporary file next
// to it is written and flushed, then renamed over the target. Creates the parent directories.
Result<void> write_file_atomic(const std::filesystem::path& path, std::span<const std::uint8_t> bytes);
Result<std::vector<std::uint8_t>> read_whole_file(const std::filesystem::path& path);

}  // namespace sh::save
