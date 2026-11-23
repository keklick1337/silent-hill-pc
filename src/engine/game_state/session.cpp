// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)

#include "engine/game_state/session.h"

#include <cstring>

namespace sh::game {

void Session::set_sys_state_next(std::int32_t state)
{
    SessionFlow& f = system.flow;
    f.sys_state = state;
    f.frame_counters[2] = 0;
    f.sys_steps[0] = f.sys_steps[1] = f.sys_steps[2] = 0;
    f.sys_step_data[0] = f.sys_step_data[1] = 0;
}

void Session::set_sys_step(int level, std::int32_t value)
{
    SessionFlow& f = system.flow;
    if (level == 0) {
        f.sys_steps[0] = value;
        f.sys_step_data[0] = 0;
        f.sys_steps[1] = 0;
        f.sys_step_data[1] = 0;
        f.sys_steps[2] = 0;
    } else if (level == 1) {
        f.sys_steps[1] = value;
        f.sys_step_data[1] = 0;
        f.sys_steps[2] = 0;
    } else {
        f.sys_steps[2] = value;
    }
}

void Session::advance_sys_step(int level)
{
    set_sys_step(level, system.flow.sys_steps[level == 0 ? 0 : (level == 1 ? 1 : 2)] + 1);
}

void Session::set_game_state_next(std::int32_t state)
{
    const std::int32_t previous = game.game_state;
    game.game_state = state;
    system.flow.frame_counters[0] = 0;
    system.flow.frame_counters[1] = 0;
    set_sys_state_next(sys_state::kGameplay);
    game.game_steps[0] = game.game_steps[1] = game.game_steps[2] = 0;
    game.game_state_previous = previous;
}

void Session::return_to_previous_screen()
{
    const std::int32_t left = game.game_state;
    system.flow.frame_counters[0] = 0;
    system.flow.frame_counters[1] = 0;
    set_sys_state_next(sys_state::kGameplay);
    game.game_steps[0] = game.game_steps[1] = game.game_steps[2] = 0;
    game.game_state = game.game_state_previous;
    game.game_state_previous = left;
}

void Session::set_game_step(int level, std::int32_t value)
{
    if (level == 0) {
        game.game_steps[0] = value;
        system.flow.frame_counters[1] = 0;
        game.game_steps[1] = 0;
        game.game_steps[2] = 0;
    } else if (level == 1) {
        game.game_steps[1] = value;
        game.game_steps[2] = 0;
    } else {
        game.game_steps[2] = value;
    }
}

void Session::set_sys_flag(std::uint32_t flag, bool on)
{
    if (on) {
        system.process.sys_flags |= flag;
    } else {
        system.process.sys_flags &= ~flag;
    }
}

void Session::clear_system()
{
    std::memset(static_cast<void*>(&system), 0, sizeof(system));
}

}  // namespace sh::game
