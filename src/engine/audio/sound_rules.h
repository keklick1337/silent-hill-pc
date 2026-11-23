// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// The rules of the game's sound driver: gamedata/config/sound_driver.json. Command
// numbers, bus volumes, the voice lines' own volumes, the effects that key a reserved voice and the maps whose
// ambient bank depends on progress. Sounds, flags and maps are named; the caller turns names into the running
// game's numbers (resolve).

#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include "engine/audio/sound_driver.h"
#include "formats/common/status.h"

namespace sh::audio {

struct SoundRules {
    struct Commands {
        std::uint8_t first_bank = 0;
        std::uint8_t first_ambient = 0;
        std::uint8_t last_ambient = 0;
        std::uint8_t song_file_offset = 0;
        std::uint8_t song_bank_offset = 0;
        std::uint16_t fade_out_ambient = 0;
        std::uint16_t fade_out_music = 0;
        std::uint16_t fade_out_music_fast = 0;
    };
    struct FixedVoice {
        std::string sound;
        std::uint8_t voice = 0;
        std::uint8_t fine = 0;
    };
    // A map whose ambient bank is `ambient` until any of `later_flags` is set, then `later_ambient`.
    struct AmbientOverride {
        std::string map;
        int ambient = 0;
        std::vector<std::string> later_flags;
        int later_ambient = 0;
    };

    Commands commands;
    std::uint8_t music_slot = 0;
    std::uint8_t music_volume = 0;
    std::uint8_t music_ducked_volume = 0;
    std::uint8_t default_reverb = 0;
    std::uint8_t default_line_volume = 0;
    std::vector<LineVolume> line_volumes;
    std::vector<FixedVoice> fixed_voices;
    std::vector<AmbientOverride> ambient_overrides;

    static Result<SoundRules> parse(std::string_view json_text);
};

// Names of the rules as the running game's numbers (-1: unknown name, which matches nothing).
struct ResolvedSoundRules {
    std::vector<FixedVoiceEffect> fixed_voices;

    bool fixed_voice(int sound) const;
};

ResolvedSoundRules resolve(const SoundRules& rules, const std::function<int(std::string_view)>& sound_number);

// The driver's tables: the rules' fields of DriverTables (the effect, file, line, reverb and layer tables come from
// sound_tables.json).
void apply_rules(const SoundRules& rules, const ResolvedSoundRules& resolved, DriverTables& tables);

// The ambient bank `map` asks for given the flags (`flag_set` by name), or -1 when no rule names the map.
int ambient_override(const SoundRules& rules, std::string_view map, const std::function<bool(std::string_view)>& flag_set);

}  // namespace sh::audio
