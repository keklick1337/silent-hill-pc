// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// The console's byte form of the progress record and the settings: what a PS1
// memory card save, the GAME and OPTS chunks of a format 1 save and a demo's setup file hold. This is the one place
// that knows where each value sits in those bytes; the running game keeps them in sh::game::SaveRecord and
// sh::game::Settings by name.

#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "engine/game_state/progress_record.h"
#include "engine/game_state/settings_record.h"
#include "engine/save/console_fields.h"

namespace sh::save::console {

inline constexpr std::size_t kSaveRecordBytes = 636;
inline constexpr std::size_t kSettingsBytes = 56;

// Bytes shorter than the record read their missing values as zero.
game::SaveRecord decode_save_record(std::span<const std::uint8_t> bytes);
std::vector<std::uint8_t> encode_save_record(const game::SaveRecord& record);
game::Settings decode_settings(std::span<const std::uint8_t> bytes);
std::vector<std::uint8_t> encode_settings(const game::Settings& settings);

// The record's values at their console offsets from `base`.
template <class Io>
void visit_save_record(const Io& io, std::size_t base, console::Visited<Io, game::SaveRecord>& r) {
    for (int i = 0; i < game::SaveRecord::kItemSlots; ++i) {
        const std::size_t at = base + 4 * static_cast<std::size_t>(i);
        io.template narrow<std::uint8_t>(at + 0, r.items[i].item);
        io.template narrow<std::uint8_t>(at + 1, r.items[i].count);
        io.field(at + 2, r.items[i].command);
        io.field(at + 3, r.items[i].extra);
    }
    io.field(base + 0xA0, r.player_reset);
    io.template narrow<std::int8_t>(base + 0xA4, r.map);
    io.template narrow<std::int8_t>(base + 0xA5, r.room);
    io.field(base + 0xA6, r.save_count);
    io.field(base + 0xA8, r.location);
    io.template narrow<std::uint8_t>(base + 0xA9, r.paper_map);
    io.template narrow<std::uint8_t>(base + 0xAA, r.equipped_weapon);
    io.field(base + 0xAB, r.slot_count);
    io.field(base + 0xAC, r.item_toggles);
    for (int m = 0; m < game::SaveRecord::kEnemyStateMaps; ++m) {
        io.field(base + 0xB0 + 4 * static_cast<std::size_t>(m), r.enemy_states[m]);
    }
    io.field(base + 0x164, r.paper_maps);
    for (int w = 0; w < game::SaveRecord::kEventFlagWords; ++w) {
        io.field(base + 0x168 + 4 * static_cast<std::size_t>(w), r.event_flags[w]);
    }
    io.field(base + 0x238, r.health_saturation);
    io.field(base + 0x23C, r.picked_up_items);
    io.field(base + 0x23F, r.inventory_flags);
    io.field(base + 0x240, r.health);
    io.field(base + 0x244, r.position_x);
    io.field(base + 0x248, r.rotation_y);
    io.field(base + 0x24A, r.clear_count);
    io.field(base + 0x24B, r.endings);
    io.field(base + 0x24C, r.position_z);
    io.field(base + 0x250, r.play_time);
    io.field(base + 0x254, r.run_distance);
    io.field(base + 0x258, r.walk_distance);
    io.field(base + 0x25C, r.mode_bits);
    io.field(base + 0x25D, r.melee_kills);
    io.field(base + 0x25E, r.melee_kills_b);
    io.field(base + 0x25F, r.ranged_kills);
    io.field(base + 0x260, r.hits_and_difficulty);
    for (int c = 0; c < game::SaveRecord::kCounters; ++c) {
        io.field(base + 0x264 + 2 * static_cast<std::size_t>(c), r.counters[c]);
    }
    io.field(base + 0x27A, r.current_ending);
    io.field(base + 0x27B, r.continues);
}

template <class Io>
void visit_settings(const Io& io, std::size_t base, console::Visited<Io, game::Settings>& s) {
    auto& b = s.binds;
    const std::size_t binds = base;
    io.field(binds + 0x00, b.enter);
    io.field(binds + 0x02, b.cancel);
    io.field(binds + 0x04, b.skip);
    io.field(binds + 0x06, b.action);
    io.field(binds + 0x08, b.aim);
    io.field(binds + 0x0A, b.light);
    io.field(binds + 0x0C, b.run);
    io.field(binds + 0x0E, b.view);
    io.field(binds + 0x10, b.step_left);
    io.field(binds + 0x12, b.step_right);
    io.field(binds + 0x14, b.pause);
    io.field(binds + 0x16, b.item);
    io.field(binds + 0x18, b.map);
    io.field(binds + 0x1A, b.option);
    io.field(base + 0x1C, s.screen_x);
    io.field(base + 0x1D, s.screen_y);
    io.field(base + 0x1E, s.mono);
    io.field(base + 0x1F, s.music_volume);
    io.field(base + 0x20, s.sfx_volume);
    io.field(base + 0x21, s.vibration);
    io.field(base + 0x22, s.brightness);
    io.field(base + 0x23, s.weapon_control_press);
    io.field(base + 0x24, s.blood_color);
    io.field(base + 0x25, s.auto_load);
    io.field(base + 0x27, s.extra_options);
    io.field(base + 0x28, s.view_reverse);
    io.field(base + 0x29, s.self_view);
    io.field(base + 0x2A, s.retreat_turn_reverse);
    io.field(base + 0x2B, s.walk_run_reverse);
    io.field(base + 0x2C, s.auto_aim_off);
    io.field(base + 0x2D, s.bullet_multiplier);
    io.field(base + 0x2E, s.seen_game_over_tips);
    io.field(base + 0x34, s.pal_language);
}

}  // namespace sh::save::console
