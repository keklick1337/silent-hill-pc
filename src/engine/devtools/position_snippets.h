// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// A world point written the ways the game data spells positions: Q12 integers for
// the Lua calls that take them (sh.player_set_pos, sh.vec.set, sh.chara_spawn), metres for world_objects.json and
// sh.camera_pos. Metres are written with the fewest decimals whose Q12 reading, (s32)(m * 4096.0f) as the data
// readers and Q12() truncate it in float, gives back the exact point.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace sh::devtools {

struct Q12Point {
    std::int32_t x = 0, y = 0, z = 0;  // Q12 world (4096 = 1 m, +Y down)
};

// The Q12 reading of a metre value as the engine's readers do it: multiply in float, truncate toward zero.
std::int32_t q12_of_metres(float metres);

// The shortest decimal metres (at most 6 decimals) that read back as `q12`; with more, the nearest one.
std::string metres_text(std::int32_t q12);

// The Q12 point nearest to a point in geometry units (256 = 1 m), rounded to the nearest Q12 step.
Q12Point q12_from_geometry(double x, double y, double z);

struct Snippet {
    std::string label;  // what it is for, shown beside the copy button
    std::string text;   // what goes to the clipboard
};

// Every form of `p`: the Lua calls, the JSON position, a Lua table. `yaw` is Q12 turns (sh.chara_spawn's rot).
std::vector<Snippet> position_snippets(const Q12Point& p, std::int32_t yaw = 0);

}  // namespace sh::devtools
