// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// The Unknown23 part of the behaviour API (gamedata/chara/Unknown23/behavior.lua). Unknown23 is the final boss of the
// bad endings: map7_s03 hands its update to the Incubator for that fight (Pc_CharaBehavior_ScriptedAs), so the record
// keeps the Incubator's type while it plays Unknown23's clips and sounds (sh.chara_sfx names the type). The clip
// calls below name the type explicitly for that reason; the shock that hits Harry draws a burst of sparks, the heat
// field "ShockSparks" (gamedata/effects/heat_fields.json), on a map whose overlay.json lists Unknown23 in
// "character_effects".

#include <cstdint>
#include <string_view>

#include "engine/chara/behavior_host.h"
#include "engine/chara/game/chara_clips.h"
#include "engine/chara/game/chara_link.h"
#include "engine/chara/game/chara_records.h"
#include "engine/effects/named_effects.h"
#include "engine/script/sh_script.h"
#include "engine/maps/map_runtime.h"

namespace {

namespace cg = sh::chara::game;
namespace ent = sh::entities;
using sh::chara::behavior::Args;
using sh::chara::behavior::fail;

constexpr char kSparks[] = "ShockSparks";

const char* const kNames[] = {
    "entity.unknown23_fx_clear", "entity.unknown23_fx_update", "entity.unknown23_shock_fx",
    // generic: a record that plays another type's clips and sounds
    "entity.type_clip_table_attach", "entity.type_anim_step",
};

// The map draws the effects of the type whose behaviour runs (overlay.json "character_effects").
bool effects() { return sh::maps::map_runtime().chara_effects(ShScript_CharaBehaviorRunning()); }

int call(std::string_view fn, const Args& a, long long* out, char* err, int errCap) {
    if (fn == "unknown23_fx_clear") {
        // The shock sparks are reset (the fight starts).
        if (effects()) {
            sh::effects::heat_fields().stop(kSparks);
        }
        return 0;
    }
    if (fn == "unknown23_fx_update") {
        // Steps and draws the shock sparks (once a tick).
        if (effects()) {
            sh::effects::heat_fields().update(kSparks, true);
        }
        return 0;
    }
    if (fn == "unknown23_shock_fx") {
        // unknown23_shock_fx(x, y, z, angle): a burst of sparks at the point (Q12), flying along angle.
        if (effects()) {
            const sh::effects::Vec3 p{static_cast<std::int32_t>(a.integer(0)), static_cast<std::int32_t>(a.integer(1)),
                                      static_cast<std::int32_t>(a.integer(2))};
            sh::effects::heat_fields().arm(kSparks, p, static_cast<std::int32_t>(a.integer(3)));
        }
        return 0;
    }
    const int slot = static_cast<int>(a.integer(0, -1));
    ent::CharacterRecord* r = cg::record(slot);
    if (r == nullptr) {
        return fail(err, errCap, "bad character slot");
    }
    cg::ClipTableRef table;
    if (!cg::type_clip_table(static_cast<int>(a.integer(1)), 0, &table)) {
        return fail(err, errCap, "the type has no clip table");
    }
    if (fn == "type_clip_table_attach") {
        // type_clip_table_attach(slot, chara_id): the record plays from that type's first clip table.
        r->model.anim.clip_table = table.clips;
        r->model.anim.map_clip_table = nullptr;
        return 0;
    }
    if (fn == "type_anim_step") {
        // type_anim_step(slot, chara_id): one tick of the entry of the record's status in that type's first table;
        // 1 when it played.
        const int status = r->model.anim.status;
        const cg::link::Skeleton sk = cg::link::skeleton(slot);
        if (status >= table.count || table.clips[status].playback == nullptr || sk.bones == nullptr ||
            sk.animation == nullptr) {
            out[0] = 0;
            return 1;
        }
        table.clips[status].playback(&r->model, sk.animation, sk.bones, &table.clips[status]);
        out[0] = 1;
        return 1;
    }
    return fail(err, errCap, "unknown function");
}

sh::chara::behavior::Extension s_extension = {
    nullptr, 0, kNames, sizeof(kNames) / sizeof(kNames[0]), call, nullptr, 0, nullptr,
};

const sh::chara::behavior::Registrar s_registrar{&s_extension};

}  // namespace
