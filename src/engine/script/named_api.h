// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// The named engine API of the scripts, sh.<group>.<name>(...): events and their steps, messages,
// camera, cutscene tracks, characters by slot, the player, file loads, light, screen, world, map effects, the game's
// math and random numbers, and the map-state variables of the loaded map (sh.map.get/set). It is the bottom of the
// ShScriptGameApi::named_call chain: the other families (sh.entity.*, sh.world.*, ...) wrap it and pass on the names
// they do not own. Arguments: a character is its slot number (0 the player), a reference is the name of a map-state
// variable ("cutscene_timer", "ufo_light_pos[3]") or a script scratch ("vec:0", "svec:1", "img:2", "pos:1", "rot:1",
// "player_bone:3", "npc_bone:30", "lm:2", "light_pos", "light_rot"), an item is its name or number.

#pragma once

#include "engine/script/sh_script.h"
#include "engine/world/fixed_math.h"

#include <cstdint>
#include <string_view>

namespace sh::script::named {

// "name" or "name[i]": the name and the element index (0 without brackets). False when the text is not of that form.
constexpr bool parse_ref(std::string_view ref, std::string_view* name, long long* index) {
    const std::size_t open = ref.find('[');
    const std::string_view n = open == std::string_view::npos ? ref : ref.substr(0, open);
    if (n.empty() || n.size() >= 96) {
        return false;
    }
    long long i = 0;
    if (open != std::string_view::npos) {
        std::size_t p = open + 1;
        if (p >= ref.size() || ref[p] < '0' || ref[p] > '9') {
            return false;
        }
        for (; p < ref.size() && ref[p] >= '0' && ref[p] <= '9'; ++p) {
            i = i * 10 + (ref[p] - '0');
        }
        if (p + 1 != ref.size() || ref[p] != ']') {
            return false;
        }
    }
    *name = n;
    *index = i;
    return true;
}

// "prefix:N" -> N (digits only, at least one); -1 for anything else.
constexpr int handle_index(std::string_view s, std::string_view prefix) {
    if (s.size() <= prefix.size() || s.substr(0, prefix.size()) != prefix) {
        return -1;
    }
    int n = 0;
    for (const char ch : s.substr(prefix.size())) {
        if (ch < '0' || ch > '9') {
            return -1;
        }
        n = n * 10 + (ch - '0');
    }
    return n;
}

// An integer in [low, high] from a random draw: the signed remainder (rng.int) or the unsigned one (rng.uint).
constexpr std::int32_t random_in_range(std::int32_t draw, std::int32_t low, std::int32_t high) {
    return draw % ((high - low) + 1) + low;
}
constexpr std::int32_t random_in_range_unsigned(std::int32_t draw, std::int32_t low, std::int32_t high) {
    return static_cast<std::int32_t>(static_cast<std::uint32_t>(draw) % static_cast<std::uint32_t>((high - low) + 1)) +
           low;
}

// Length of a 2D Q12 vector computed in Q6 so large components do not overflow (the game's square root).
constexpr std::int32_t planar_length_q6(std::int32_t x, std::int32_t z) {
    const std::int32_t qx = x >> 6;
    const std::int32_t qz = z >> 6;
    return world::game_sqrt(world::wrap_add(world::wrap_mul(qx, qx), world::wrap_mul(qz, qz))) << 6;
}

// The screen fade state a fade start writes: a fade out from the start (reset) or from where it is, +4 for a fade
// in, bit 3 for white.
constexpr std::int32_t screen_fade_start_state(bool reset, bool fade_in, bool white) {
    return ((reset ? 2 : 3) + (fade_in ? 4 : 0)) | (white ? 8 : 0);
}

// sh.<group>.<name>: the count of results in out (up to 8), -1 with the reason in err.
int call(const char* name, int argc, const ShScriptArg* argv, long long* out, int out_cap, char* err, int err_cap);
// Every "<group>.<name>" of the family, NULL-terminated.
const char* const* api_names();

}  // namespace sh::script::named

// The C entry points of the family (the script API binds call them).
extern "C" {
int PcScriptNamed_Call(const char* name, int argc, const ShScriptArg* argv, long long* out, int out_cap, char* err,
                       int err_cap);
const char* const* PcScriptNamed_List(void);
// script_raw_api of config.cfg (the raw debug bridges).
int PcScriptNamed_RawApiEnabled(void);
// A position reference as a Q12 point: 1 = out filled, 0 = the reference is nil, -1 = error (err).
int PcScriptNamed_PositionRef(const char* ref, int out[3], char* err, int err_cap);
// The address a reference names; NULL with the reason in err.
void* PcScriptNamed_RefAddr(const char* ref, char* err, int err_cap);
// The address of a map-state variable ("msg_audio_idx", "ufo_light_pos[2]"); NULL when it names none here.
void* PcScriptNamed_VarAddr(const char* ref);
}
