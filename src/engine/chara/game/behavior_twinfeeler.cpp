// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// The Twinfeeler part of the behaviour API (gamedata/chara/Twinfeeler/behavior.lua, the larva boss of the mall,
// map4_s03): the ground effects the map draws for it (holes it bursts out of and dives into, dust where its body
// crosses the floor, the acid it spits), and a few generic calls the boss needs (darkness at a point, a cylinder
// ray, the file queue, model bone visibility, extra collision cylinders).
//
// The effects are the engine's: the ground effect set "TwinfeelerBurrow" (gamedata/effects/ground_fx.json) and the
// acid spit. A map draws them when its overlay.json lists the type in "character_effects" (map4_s03); on another
// map (a debug spawn) they do nothing, as no other overlay had that code.

#include <cstdint>
#include <string_view>

#include "engine/app/loader/game_files.h"
#include "engine/chara/behavior_host.h"
#include "engine/chara/game/chara_link.h"
#include "engine/chara/game/chara_records.h"
#include "engine/effects/named_effects.h"
#include "engine/script/sh_script.h"
#include "engine/maps/map_runtime.h"
#include "engine/world/world_atmosphere.h"
#include "engine/chara/game/game_records.h"
#include "engine/world/game/character_world.h"

extern "C" {
// The acid drops (src/engine/effects/game/particle_host.cpp).
void Acid_Spit(void* from, void* to);
}  // extern "C"

namespace {

namespace cg = sh::chara::game;
namespace wg = sh::world::game;
namespace ent = sh::entities;
using sh::chara::behavior::Args;
using sh::chara::behavior::fail;

const char* const kNames[] = {
    "entity.twinfeeler_textures_load", "entity.twinfeeler_effects_reset", "entity.twinfeeler_hole",
    "entity.twinfeeler_dust", "entity.twinfeeler_dust_trail", "entity.twinfeeler_effects_draw",
    "entity.twinfeeler_acid_throw", "entity.twinfeeler_dust_still",
    // generic
    "entity.update_variant", "entity.darkness_at", "entity.ray_cylinder", "entity.file_queue_ready",
    "entity.model_bones_show", "entity.model_bone_hide", "entity.colliders_attach", "entity.collider_set",
};

// The extra collision cylinders the body carries (the record points at them).
constexpr int kColliderCount = 4;
cg::ExtraCollider s_colliders[kColliderCount];

// The bone nodes of a character model.
constexpr int kModelNodes = 56;

constexpr char kBurrow[] = "TwinfeelerBurrow";

// The hole timelines of the set (ground_fx.json "holes").
constexpr std::int32_t kHoleBurstOut = 0;
constexpr std::int32_t kHoleDiveIn = 1;

// The dust kind that stays where it starts.
constexpr std::int32_t kDustStill = 3;

// The map draws the effects of the type whose behaviour runs (overlay.json "character_effects").
bool effects_on() { return sh::maps::map_runtime().chara_effects(ShScript_CharaBehaviorRunning()); }

std::int32_t i32(const Args& a, int i) { return static_cast<std::int32_t>(a.integer(i)); }

ent::Vec3 vec(const Args& a, int i) { return {i32(a, i), i32(a, i + 1), i32(a, i + 2)}; }

int call(std::string_view fn, const Args& a, long long* out, char* err, int errCap) {
    if (fn == "update_variant") {
        out[0] = sh::chara::behavior::update_variant();
        return 1;
    }
    if (fn == "darkness_at") {
        // darkness_at(x, y, z): the darkness of the area at the point (0..255).
        out[0] = sh::world::atmosphere().darkness_at(i32(a, 0), i32(a, 1), i32(a, 2));
        return 1;
    }
    if (fn == "ray_cylinder") {
        // ray_cylinder(x, y, z, dx, dy, dz): the cylinder-swept ray against the world; hit, the hit point and
        // its distance.
        const ent::Vec3 from = vec(a, 0);
        const ent::Vec3 dir = vec(a, 3);
        cg::RayRecord trace = {};
        out[0] = wg::trace(cg::world_record(trace),
                           cg::world_vec(from), cg::world_vec(dir), wg::TraceKind::Probe, nullptr) ? 1 : 0;
        out[1] = trace.target.x, out[2] = trace.target.y, out[3] = trace.target.z;
        out[4] = trace.distance;
        return 5;
    }
    if (fn == "file_queue_ready") {
        // The file queue moved on; true when everything asked for is in.
        out[0] = sh::loader::queue_load_chunks() ? 1 : 0;
        return 1;
    }
    if (fn == "twinfeeler_textures_load") {
        // The effect texture the dust clouds draw from.
        if (effects_on()) {
            cg::link::twinfeeler_dust_texture_load();
        }
        return 0;
    }
    if (fn == "twinfeeler_dust" || fn == "twinfeeler_dust_trail") {
        // dust(x, z, kind) / dust_trail(x, z, kind): a dust cloud (and the trail it leaves on the open floor) of
        // the kind at the floor point (Q12).
        if (effects_on()) {
            const sh::effects::Vec3 pos{i32(a, 0), 0, i32(a, 1)};
            if (fn == "twinfeeler_dust") {
                sh::effects::ground_fx_sets().dust_at(kBurrow, pos, i32(a, 2));
            } else {
                sh::effects::ground_fx_sets().mark_at(kBurrow, pos, i32(a, 2));
            }
        }
        return 0;
    }
    if (fn == "twinfeeler_dust_still") {
        // dust_still(x, y, z): a dust cloud that stays where it starts (Q12), as the map's cutscenes raise.
        if (effects_on()) {
            sh::effects::ground_fx_sets().dust_free(kBurrow, {i32(a, 0), i32(a, 1), i32(a, 2)}, kDustStill);
        }
        return 0;
    }
    if (fn == "twinfeeler_acid_throw") {
        // acid_throw(x0, y0, z0, x1, y1, z1): an acid ball from the first point at the second (Q12).
        ent::Vec3 from = vec(a, 0);
        ent::Vec3 to = vec(a, 3);
        if (effects_on()) {
            Acid_Spit(&from, &to);
        }
        return 0;
    }

    const int slot = static_cast<int>(a.integer(0, -1));
    ent::CharacterRecord* r = cg::record(slot);
    if (r == nullptr) {
        return fail(err, errCap, "bad character slot");
    }
    if (fn == "twinfeeler_effects_reset") {
        // The ground effects start over around the character (the map's effect grid follows it).
        if (effects_on()) {
            sh::effects::ground_fx_sets().reset(kBurrow, {r->position.x, r->position.y, r->position.z});
        }
        return 0;
    }
    if (fn == "twinfeeler_hole") {
        // hole(slot, kind): the hole the character opens where it stands: 0 bursting out, 1 diving in.
        if (effects_on()) {
            sh::effects::ground_fx_sets().hole(kBurrow, a.integer(1) == 0 ? kHoleBurstOut : kHoleDiveIn, r,
                                               {r->position.x, r->position.y, r->position.z});
        }
        return 0;
    }
    if (fn == "twinfeeler_effects_draw") {
        // One tick of the ground effects (holes, dust) and their drawing.
        if (effects_on()) {
            sh::effects::ground_fx_sets().frame(kBurrow, {r->rotation.x, r->rotation.y, r->rotation.z});
        }
        return 0;
    }
    if (fn == "model_bones_show") {
        // model_bones_show(slot): every bone of the type's model drawn.
        cg::link::model_bones_show(r->model.chara_id);
        return 0;
    }
    if (fn == "model_bone_hide") {
        // model_bone_hide(slot, node): the bone node collapsed (not drawn) this frame.
        const long long node = a.integer(1, -1);
        if (cg::link::model_bones_present(r->model.chara_id) && (node < 0 || node >= kModelNodes)) {
            return fail(err, errCap, "bone node out of range");
        }
        cg::link::model_bone_hide(r->model.chara_id, static_cast<int>(node));
        return 0;
    }
    if (fn == "colliders_attach") {
        // colliders_attach(slot, count): the character collides with Harry through its extra cylinders.
        r->collision.records = s_colliders;
        r->collision.state_and_count =
            static_cast<std::uint8_t>((r->collision.state_and_count & 0x0F) | ((a.integer(1) & 0xF) << 4));
        return 0;
    }
    if (fn == "collider_set") {
        // collider_set(slot, i, x, y, z, bottom, top, radius): one extra cylinder (Q12).
        const long long i = a.integer(1, -1);
        if (i < 0 || i >= kColliderCount) {
            return fail(err, errCap, "collider out of range");
        }
        cg::ExtraCollider& c = s_colliders[i];
        c.position = vec(a, 2);
        c.bottom = static_cast<std::int16_t>(a.integer(5));
        c.top = static_cast<std::int16_t>(a.integer(6));
        c.radius = static_cast<std::int16_t>(a.integer(7));
        return 0;
    }
    return sh::chara::behavior::kNotMine;
}

sh::chara::behavior::Extension s_extension = {
    nullptr, 0, kNames, sizeof(kNames) / sizeof(kNames[0]), call, nullptr, 0, nullptr,
};

const sh::chara::behavior::Registrar s_registrar{&s_extension};

}  // namespace
