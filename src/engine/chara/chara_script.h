// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// The sh.chara_* script API binds: a map's events.lua names a character's
// sound by slot and its numbers by key instead of hard-coded ids. Pure engine
// data: the character library (gamedata/chara) and, lazily, the character's
// action hooks (chara/<Name>/actions.lua). The game layer supplies both
// providers when it binds the API; nothing here knows the game's records.
//
// Template over the API struct: the C-facing ShScriptGameApi (sh_script.h) and
// the script host's sh::script::GameApi (script_host.h) carry the same
// chara_* function-pointer fields.

#pragma once

#include "engine/chara/chara_actions.h"
#include "engine/chara/chara_library.h"

namespace sh::chara {

// What the binds run on; every entry may be null and the binds then answer
// the caller's fallback, exactly as with no data loaded.
// `actions_provider` is called lazily on the first named sound (the hooks run
// in the game's script API, which exists only after the game bound it).
struct ScriptCharaDeps {
    const CharaLibrary* library = nullptr;
    CharaActions* (*actions_provider)() = nullptr;
};

namespace detail {

// One script API per process (the game binds one host); the deps the binds
// close over live here. Defined in chara_script.cpp.
void set_script_deps(const ScriptCharaDeps& deps);
const CharaLibrary* script_library();
// The hooks for an id, or null: the provider runs only for a valid id.
CharaActions* script_actions(int charaId);

}  // namespace detail

// Fills the chara_id_find / chara_sfx / chara_param / chara_param_q12 /
// chara_anim / chara_state_number entries of the API.
template <typename Api>
void bind_script_api(Api* api, const ScriptCharaDeps& deps) {
    detail::set_script_deps(deps);

    api->chara_id_find = [](const char* name) -> int {
        const CharaLibrary* lib = detail::script_library();
        return lib != nullptr && name != nullptr ? lib->find_id(name) : -1;
    };
    api->chara_sfx = [](int charaId, const char* slot, int fallback) -> int {
        if (slot == nullptr) {
            return fallback;
        }
        const CharaLibrary* lib = detail::script_library();
        const Character c = lib != nullptr && charaId >= 0
                                ? lib->character(charaId)
                                : Character();
        const int chosen = c.sfx(slot, fallback);
        CharaActions* hooks = detail::script_actions(charaId);
        return hooks != nullptr ? hooks->sound(charaId, slot, chosen) : chosen;
    };
    api->chara_param = [](int charaId, const char* name, double fallback, int* found) -> double {
        const CharaLibrary* lib = detail::script_library();
        const Character c = lib != nullptr && charaId >= 0
                                ? lib->character(charaId)
                                : Character();
        const bool set = c.valid() && name != nullptr && c.def()->params.contains(std::string_view(name));
        if (found != nullptr) {
            *found = set ? 1 : 0;
        }
        return set ? c.param(name, fallback) : fallback;
    };
    api->chara_param_q12 = [](int charaId, const char* name, int fallbackQ12) -> int {
        if (name == nullptr) {
            return fallbackQ12;
        }
        const CharaLibrary* lib = detail::script_library();
        return lib != nullptr && charaId >= 0
                   ? lib->character(charaId).param_q12(name, fallbackQ12)
                   : fallbackQ12;
    };
    api->chara_anim = [](int charaId, const char* name, int fallback) -> int {
        if (name == nullptr) {
            return fallback;
        }
        const CharaLibrary* lib = detail::script_library();
        return lib != nullptr && charaId >= 0
                   ? lib->character(charaId).anim(name, fallback)
                   : fallback;
    };
    api->chara_state_number = [](int charaId, const char* name) -> int {
        if (name == nullptr) {
            return -1;
        }
        const CharaLibrary* lib = detail::script_library();
        const Character c = lib != nullptr && charaId >= 0
                                ? lib->character(charaId)
                                : Character();
        // Harry answers from "states" (e_PlayerState), an NPC from "controls" (e_<Name>Control).
        return c.state(name, c.control(name, -1));
    };
}

}  // namespace sh::chara
