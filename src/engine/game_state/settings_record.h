// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// The game's settings of the session (Session::game.settings): the pad binds and the options screen's values. The
// saves keep them by name (sh::save::GameOptions); their console byte form (memory cards, demo setups) is
// engine/save/console_records.cpp's.

#pragma once

#include <cstdint>

namespace sh::game {

// The player's pad binds (bits of the pad word) in the settings order.
struct PadBinds {
    std::uint16_t enter, cancel, skip, action, aim, light, run, view, step_left, step_right, pause, item, map,
        option;
    bool operator==(const PadBinds&) const = default;
};

struct Settings {
    PadBinds binds;
    std::int8_t screen_x;
    std::int8_t screen_y;
    std::uint8_t mono;
    std::uint8_t music_volume;
    std::uint8_t sfx_volume;
    std::uint8_t vibration;
    std::uint8_t brightness;
    std::uint8_t weapon_control_press;
    std::uint8_t blood_color;
    std::int8_t auto_load;
    std::uint8_t extra_options;
    std::int8_t view_reverse;
    std::int8_t self_view;
    std::int8_t retreat_turn_reverse;
    std::int8_t walk_run_reverse;
    std::int8_t auto_aim_off;
    std::int8_t bullet_multiplier;
    std::uint16_t seen_game_over_tips;
    std::uint32_t pal_language;
    bool operator==(const Settings&) const = default;
};

}  // namespace sh::game
