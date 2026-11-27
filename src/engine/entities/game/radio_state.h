// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
#pragma once

// What the radio's static is doing now, for the console's sound dump (engine/entities/game/npc_main_host.cpp holds
// the channels).

#include <array>
#include <cstdint>

namespace sh::entities::game {

struct RadioState {
    bool on = false;  // the radio item is switched on
    struct Channel {
        int sound = 0;       // the sound id of the channel's loop
        int playing = -1;    // the NPC slot it sounded for at the last tick, -1 none
        int target = -1;     // the NPC slot it follows now
    };
    std::array<Channel, 2> channels{};
    struct Heard {
        int slot = -1;                  // an NPC slot inside the radio's reach, nearest first
        std::uint32_t distance_sq = 0;  // square metres
    };
    std::array<Heard, 3> heard{};
};

RadioState radio_state();

}  // namespace sh::entities::game
