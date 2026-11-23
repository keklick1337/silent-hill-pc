// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// Action hooks of a character: gamedata/chara/<Name>/actions.lua (a mod's copy first) defines
// Lua functions the engine calls when the character does something - on_footstep, on_damage,
// on_death, on_attack, on_sound (any other sound), and the reserved on_spawn and
// on_animation_event. Each character's script runs in its own sandbox
// (sh::script::Host), so a broken script cannot take another one down.
//
// The sound hooks sit where the game picks a character's sound (Pc_CharaSfxNamed): the hook
// gets the slot and the sound the data chose and returns nil (keep it), a registry name or a
// sound id. No script or no hook = the data's sound, so the shipped game stays the original.

#pragma once

#include "engine/chara/chara_library.h"
#include "engine/script/script_host.h"

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <string_view>

namespace sh::chara {

class CharaActions {
public:
    // `config` is the sandbox every character script gets (the game's sh.* API, log, dirs).
    CharaActions(const CharaLibrary& library, script::HostConfig config);
    ~CharaActions();
    CharaActions(const CharaActions&) = delete;
    CharaActions& operator=(const CharaActions&) = delete;

    // The sound character `id` plays for `slot`: `chosen` (the data's id, or the original)
    // unless its actions.lua hook returns another one.
    int sound(int id, std::string_view slot, int chosen);
    // A step on a named floor surface (footsteps.h): on_footstep with e.surface and e.place (the map's place rule
    // that chose `chosen`, empty for the character's table).
    int footstep(int id, std::string_view surface, std::string_view place, int chosen);

    // The hook a sound slot reports through: "footstep*" -> on_footstep, "*damage*" ->
    // on_damage, "death*" -> on_death, "*attack*" -> on_attack, else on_sound. A character
    // without the specific hook falls back to on_sound.
    static const char* hook_for_slot(std::string_view slot);
    // The surface a footstep slot names: "footstep" -> "default", "footstep_grass" -> "grass".
    static std::string footstep_surface(std::string_view slot);

    // The character's script host, nullptr without actions.lua (tests, console).
    script::Host* host(int id);

private:
    int run_hook(int id, std::string_view slot, const char* hook, std::string_view surface,
                 std::string_view place, int chosen);

    struct Script {
        bool tried = false;
        std::unique_ptr<script::Host> host;
    };

    const CharaLibrary& library_;
    script::HostConfig config_;
    std::map<int, Script> scripts_;
    bool in_hook_ = false;  // a hook asking for a character sound gets the data's answer
};

}  // namespace sh::chara
