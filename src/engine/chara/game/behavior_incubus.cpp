// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// The Incubus part of the behaviour API (gamedata/chara/Incubus/behavior.lua, the final boss of the good endings in
// map7_s03): the boss effects of that map, the engine's bolt and fire pools (gamedata/effects/bolt_pools.json
// "BossBolts", fire_pools.json "BossFire"). They work on a map whose overlay.json lists the Incubus in
// "character_effects" and do nothing elsewhere.

#include <cstdint>
#include <string_view>

#include "engine/chara/behavior_host.h"
#include "engine/chara/game/chara_link.h"
#include "engine/chara/game/chara_records.h"
#include "engine/chara/chara_library.h"
#include "engine/chara/runtime/chara_data.h"
#include "engine/effects/named_effects.h"
#include "engine/script/sh_script.h"
#include "engine/maps/map_runtime.h"

extern "C" {
// The ammo test (src/engine/inventory/game/inventory_host.cpp).
bool Inventory_HasUsableLongGunAmmo(bool unused);
// The ending boss effects (src/engine/effects/game/ending_effects_host.cpp).
void EndingFx_BoltsTrack(void);
// `owner`: the bolt pool owner (bolt_pools.json "owners") the effects follow.
void EndingFx_BoltsAttach(const char* owner, const void* at, sh::entities::BoneCoord* bones);
void EndingFx_BossDraw(std::int32_t armed);
}  // extern "C"

extern "C" void Vc_LookAtPositionYSet(std::int32_t look_at_y);

namespace {

namespace cg = sh::chara::game;
namespace ent = sh::entities;
using sh::chara::behavior::Args;
using sh::chara::behavior::fail;

constexpr char kBolts[] = "BossBolts";
constexpr char kFire[] = "BossFire";

const char* const kNames[] = {
    "entity.boss_fx_attach",  "entity.boss_fx_track",       "entity.boss_aim_lead",    "entity.boss_fx_spark",
    "entity.boss_fx_arc",     "entity.boss_fx_arc_wide",    "entity.boss_fx_bolt",     "entity.boss_fx_bolt_player",
    "entity.boss_fx_flash",   "entity.boss_fx_fire",        "entity.boss_fx_draw",
    // generic
    "entity.player_long_gun_ammo", "entity.camera_look_at_y",
};

// The map draws the effects of the type whose behaviour runs (overlay.json "character_effects").
bool effects() { return sh::maps::map_runtime().chara_effects(ShScript_CharaBehaviorRunning()); }

std::int32_t i32(const Args& a, int i) { return static_cast<std::int32_t>(a.integer(i)); }

sh::effects::Vec3 point(const Args& a, int first) { return {i32(a, first), i32(a, first + 1), i32(a, first + 2)}; }

int call(std::string_view fn, const Args& a, long long* out, char* err, int errCap) {
    if (fn == "player_long_gun_ammo") {
        // 1 when Harry carries a rifle or shotgun with ammo for it.
        out[0] = Inventory_HasUsableLongGunAmmo(0) != 0 ? 1 : 0;
        return 1;
    }
    if (fn == "camera_look_at_y") {
        // camera_look_at_y(y): the height (Q12) the camera looks at.
        Vc_LookAtPositionYSet(i32(a, 0));
        return 0;
    }
    if (fn == "boss_fx_track") {
        // The effects lead their aim by Harry's movement since the boss arrived (once a tick).
        if (effects()) {
            EndingFx_BoltsTrack();
        }
        return 0;
    }
    if (fn == "boss_aim_lead") {
        // The lead (Q12) boss_fx_track computed: x, z.
        std::int32_t x = 0, z = 0;
        cg::link::boss_aim_lead(x, z);
        out[0] = x, out[1] = z;
        return 2;
    }
    if (fn == "boss_fx_spark") {
        // boss_fx_spark(x, y, z, bone): lightning crackling around the point, tied to the boss's bone.
        if (effects()) {
            const sh::effects::Vec3 p = point(a, 0);
            sh::effects::bolt_pools().spawn(kBolts, "spark", p, p, i32(a, 3), 0, 0);
        }
        return 0;
    }
    if (fn == "boss_fx_arc" || fn == "boss_fx_arc_wide" || fn == "boss_fx_bolt") {
        // boss_fx_arc / arc_wide / bolt(x, y, z, to_x, to_y, to_z): a short arc, a wide arc, a bolt that
        // strikes the target point.
        const char* spawner = fn == "boss_fx_arc" ? "arc" : fn == "boss_fx_arc_wide" ? "arc_wide" : "bolt";
        if (effects()) {
            sh::effects::bolt_pools().spawn(kBolts, spawner, point(a, 0), point(a, 3), 0, 0, 0);
        }
        return 0;
    }
    if (fn == "boss_fx_bolt_player") {
        // boss_fx_bolt_player(x, y, z, dx, dy, dz): a bolt at Harry's chest moved by (dx, dy, dz).
        if (effects()) {
            sh::effects::bolt_pools().spawn(kBolts, "bolt_player", point(a, 0), point(a, 3), 0, 0, 0);
        }
        return 0;
    }
    if (fn == "boss_fx_flash") {
        // boss_fx_flash(x, y, z): a flash at the point.
        if (effects()) {
            const sh::effects::Vec3 p = point(a, 0);
            sh::effects::bolt_pools().spawn(kBolts, "flash", p, p, 0, 0, 0);
        }
        return 0;
    }
    if (fn == "boss_fx_fire") {
        // boss_fx_fire(x, y, z, spin, speed, life): a fire ball at the point.
        if (effects()) {
            sh::effects::fire_pools().glow(kFire, point(a, 0), i32(a, 3), i32(a, 4), i32(a, 5));
        }
        return 0;
    }
    if (fn == "boss_fx_draw") {
        // boss_fx_draw(armed): steps and draws the effects; an armed boss's are drawn outside the cutscenes too.
        if (effects()) {
            EndingFx_BossDraw(a.integer(0) != 0 ? 1 : 0);
        }
        return 0;
    }
    if (fn == "boss_fx_attach") {
        // boss_fx_attach(slot): the effects follow this character's bones from now on, as the bolt pool owner named
        // like the type whose behaviour runs (the Incubus, Unknown23); the effect pools start empty.
        const int slot = static_cast<int>(a.integer(0, -1));
        ent::CharacterRecord* r = cg::record(slot);
        if (r == nullptr) {
            return fail(err, errCap, "bad character slot");
        }
        const sh::chara::CharacterDef* owner = sh::chara::type_def(ShScript_CharaBehaviorRunning());
        if (owner == nullptr) {
            return fail(err, errCap, "no running behaviour type");
        }
        if (effects()) {
            EndingFx_BoltsAttach(owner->folder.c_str(), &r->position, cg::link::skeleton(slot).bones);
        }
        return 0;
    }
    return fail(err, errCap, "unknown function");
}

sh::chara::behavior::Extension s_extension = {
    nullptr, 0, kNames, sizeof(kNames) / sizeof(kNames[0]), call, nullptr, 0, nullptr,
};

const sh::chara::behavior::Registrar s_registrar{&s_extension};

}  // namespace
