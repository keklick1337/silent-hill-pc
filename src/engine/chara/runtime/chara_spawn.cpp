// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)

#include "engine/chara/runtime/chara_spawn.h"

#include "engine/chara/behavior_host.h"
#include "engine/chara/chara_library.h"
#include "engine/chara/runtime/chara_data.h"
#include "engine/entities/entity_world.h"
#include "engine/game_state/game_state.h"
#include "engine/script/sh_script.h"

#include <array>
#include <cctype>
#include <string_view>

namespace gs = sh::game_state;

namespace sh::chara {
namespace {

SpawnPorts s_ports;

constexpr std::array<const char*, 4> kAiNames = {"auto", "full", "fallback", "none"};
constexpr std::array<const char*, 5> kRunNames = {"none", "map", "own", "fallback", "none"};

const CharacterDef* def_of(int chara_id) {
    const CharaLibrary* lib = game_library();
    if (lib == nullptr || chara_id <= 0) {
        return nullptr;
    }
    return lib->find(chara_id);
}

bool model_ready(int chara_id) {
    return gs::chara_model_loaded(chara_id) && gs::chara_anim_slot(chara_id) >= 0;
}

bool map_runs(int chara_id) {
    return chara_id > 0 && gs::chara_has_behaviour(chara_id);
}

bool has_own(int chara_id) {
    return behavior::behaviour_exists(chara_id);
}

bool has_fallback(int chara_id) {
    return ShScript_CharaFallbackExists() != 0 && behavior::has_clips(chara_id);
}

bool same_text(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i]))) {
            return false;
        }
    }
    return true;
}

}  // namespace

void install_spawn_ports(const SpawnPorts& ports) {
    s_ports = ports;
}

}  // namespace sh::chara

using namespace sh::chara;

extern "C" {

int Pc_Spawn_AiFind(const char* name) {
    if (name == nullptr) {
        return -1;
    }
    for (std::size_t i = 0; i < kAiNames.size(); ++i) {
        if (same_text(name, kAiNames[i])) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

const char* Pc_Spawn_AiName(int ai) {
    return ai >= 0 && ai < static_cast<int>(kAiNames.size()) ? kAiNames[static_cast<std::size_t>(ai)] : kAiNames[0];
}

const char* Pc_Spawn_RunName(int run) {
    return run >= 0 && run < static_cast<int>(kRunNames.size()) ? kRunNames[static_cast<std::size_t>(run)]
                                                                 : kRunNames[0];
}

int Pc_Chara_AiResolve(int charaId, int ai) {
    const bool own = has_own(charaId);
    const int fallback = has_fallback(charaId) ? PcSpawnRun_Fallback : PcSpawnRun_None;
    switch (ai) {
        case PcSpawnAi_None: return PcSpawnRun_None;
        case PcSpawnAi_Fallback: return fallback;
        case PcSpawnAi_Full:
            if (map_runs(charaId)) {
                return PcSpawnRun_Map;
            }
            return own ? PcSpawnRun_Own : fallback;
        default: break;
    }
    if (map_runs(charaId)) {
        return PcSpawnRun_Map;
    }
    const CharacterDef* d = def_of(charaId);
    const std::string_view pick = d != nullptr ? std::string_view(d->spawn_auto) : std::string_view();
    if (pick == "none") {
        return PcSpawnRun_None;
    }
    if (pick == "fallback" || !own) {
        return fallback;
    }
    return PcSpawnRun_Own;
}

int Pc_Chara_Spawnable(int charaId) {
    // The asset pool loads the types whose character.json says "pool" (by default those with a "spawn" block: the
    // monsters and the cutscene cast, and a mod's new character that declares one).
    const CharacterDef* d = def_of(charaId);
    return charaId < gs::chara::kMaxTypes && d != nullptr && d->pooled() ? 1 : 0;
}

const char* Pc_Chara_AiKind(int charaId) {
    const CharacterDef* d = def_of(charaId);
    if (d != nullptr && !d->spawn_kind.empty()) {
        return d->spawn_kind.c_str();
    }
    return has_own(charaId) || map_runs(charaId) ? "enemy" : "prop";
}

int Pc_Chara_HasAi(int charaId) {
    return Pc_Chara_AiResolve(charaId, PcSpawnAi_Auto) != PcSpawnRun_None ? 1 : 0;
}

int Pc_Chara_SpawnState(int charaId) {
    const CharacterDef* d = def_of(charaId);
    if (d == nullptr) {
        return 3;
    }
    if (const char* map = ShScript_MapName(); map != nullptr && !d->spawn_map_states.empty()) {
        if (const auto it = d->spawn_map_states.find(map); it != d->spawn_map_states.end()) {
            return it->second;
        }
    }
    // "params.spawn_state" is the older key of the same thing (mods written before "spawn").
    return d->spawn_state >= 0 ? d->spawn_state : Character(d).param_int("spawn_state", 3);
}

const char* Pc_Chara_SpawnNote(int charaId) {
    const CharacterDef* d = def_of(charaId);
    return d != nullptr ? d->spawn_note.c_str() : "";
}

int Pc_Spawn_CharacterAi(int charaId, int state, int x, int z, int yaw, int ai) {
    if (Pc_Chara_Spawnable(charaId) == 0) {
        return -1;
    }
    if (!model_ready(charaId) &&
        (s_ports.pool_ensure == nullptr || !s_ports.pool_ensure(charaId) || !model_ready(charaId))) {
        return -1;
    }
    if (state < 0) {
        state = Pc_Chara_SpawnState(charaId);
    }
    const int slot = gs::characters().create(charaId, sh::entities::Vec3{x, 0, z},
                                             sh::entities::Angles{0, static_cast<std::int16_t>(yaw & 0xFFF), 0}, state);
    if (slot >= 1) {
        const int run = Pc_Chara_AiResolve(charaId, ai);
        if (s_ports.set_spawn_run != nullptr) {
            s_ports.set_spawn_run(slot, run);
        }
        if (run != PcSpawnRun_None && s_ports.harry_reactions_ensure != nullptr) {
            const std::string_view kind = Pc_Chara_AiKind(charaId);
            s_ports.harry_reactions_ensure(charaId, kind == "enemy" || kind == "boss");
        }
        if (run != PcSpawnRun_None && s_ports.sound_banks_ensure != nullptr) {
            s_ports.sound_banks_ensure(charaId);
        }
    }
    return slot;
}

int Pc_Spawn_Character(int charaId, int state, int x, int z, int yaw) {
    return Pc_Spawn_CharacterAi(charaId, state, x, z, yaw, PcSpawnAi_Auto);
}

}  // extern "C"
