// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// The calls of the character link (chara_link.h) the engine answers itself: the map's character updates and
// drawers (MapRuntime) and Harry's clip tables (the engine's base clips and the map's clips).

#include "engine/chara/game/chara_entry.h"
#include "engine/chara/game/chara_link.h"
#include "engine/maps/map_runtime.h"

extern "C" sh::chara::game::ClipInfo HARRY_BASE_ANIM_INFOS[256];

namespace sh::chara::game::link {

void player_clip_tables(const void** base, const void** map) {
    *base = HARRY_BASE_ANIM_INFOS, *map = sh::maps::map_runtime().player_anim_infos();
}

CharaUpdate map_chara_update(int chara_id) {
    switch (sh::maps::map_runtime().chara_update(chara_id)) {
        case sh::maps::CharaUpdate::Behaviour: return Pc_CharaBehavior_Scripted;
        case sh::maps::CharaUpdate::BehaviourAlt: return Pc_CharaBehavior_ScriptedAlt;
        case sh::maps::CharaUpdate::BehaviourAs: return Pc_CharaBehavior_ScriptedAs;
        case sh::maps::CharaUpdate::None: break;
    }
    return nullptr;
}

void set_map_chara_update(int chara_id, CharaUpdate update) {
    sh::maps::CharaUpdate u = sh::maps::CharaUpdate::None;
    if (update == Pc_CharaBehavior_Scripted) {
        u = sh::maps::CharaUpdate::Behaviour;
    } else if (update == Pc_CharaBehavior_ScriptedAlt) {
        u = sh::maps::CharaUpdate::BehaviourAlt;
    } else if (update == Pc_CharaBehavior_ScriptedAs) {
        u = sh::maps::CharaUpdate::BehaviourAs;
    }
    sh::maps::map_runtime().set_chara_update(chara_id, u);
}

bool map_has_acid_drawer() { return sh::maps::map_runtime().draws(sh::maps::MapEffect::Acid); }

}  // namespace sh::chara::game::link
