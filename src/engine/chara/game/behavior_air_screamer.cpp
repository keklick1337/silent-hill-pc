// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// Air Screamer and Night Flutter (one behaviour, gamedata/chara/AirScreamer/behavior.lua, flight in
// gamedata/chara/flight.lua): the two clip speeds of their clip file that come from the behaviour script, and the
// world, bone and system calls the flight code needs that the other parts of the API do not serve.

#include <cstdint>
#include <span>
#include <string_view>

#include "engine/chara/behavior_host.h"
#include "engine/chara/game/bone_ops.h"
#include "engine/chara/game/chara_clips.h"
#include "engine/chara/game/chara_entry.h"
#include "engine/chara/game/chara_link.h"
#include "engine/chara/game/chara_records.h"
#include "engine/chara/runtime/chara_data.h"
#include "engine/game_state/session.h"
#include "engine/loop/frame_view.h"
#include "engine/maps/map_runtime.h"
#include "engine/player/player.h"
#include "engine/script/named_api_calls.h"
#include "engine/script/sh_script.h"
#include "engine/world/collision_triggers.h"
#include "engine/world/ground_point.h"
#include "engine/world/point_math.h"
#include "audio/game/game_sound_driver.h"
#include "engine/app/gameflow/demo_host.h"
#include "engine/chara/game/game_records.h"
#include "engine/world/game/character_world.h"

extern "C" {
// The clip playback (src/engine/entities/game/model_runtime.cpp).
void Chara_ModelAnimPlay(void* chara, void* animation, sh::entities::BoneCoord* bones);
// Combat effects and the clip colour over records (src/engine/effects/game/combat_effects_host.cpp).
void Chara_AnimStatusSet(std::int32_t handover, void* chara, std::int32_t status, std::int32_t keyframe);
}  // extern "C"

namespace {

namespace cg = sh::chara::game;
namespace wg = sh::world::game;
namespace ent = sh::entities;
using sh::chara::behavior::Args;
using sh::chara::behavior::fail;

constexpr int kSpawnRows = 32;
constexpr std::int32_t kDefaultClipSpeed = 10 * 4096;

// The clip speeds that depend on the bird's state come from the behaviour script: clip_duration(slot, which).
std::int32_t clip_speed(int which, ent::ModelRecord* model) {
    auto* chara = reinterpret_cast<ent::CharacterRecord*>(model);
    const std::ptrdiff_t idx = chara - cg::slots().npcs;
    long long out = kDefaultClipSpeed;
    if (idx >= 0 && idx < ent::kNpcSlotCount) {
        ShScript_CharaBehaviorCall(chara->model.chara_id, "clip_duration", static_cast<int>(idx) + 1, which, &out);
    }
    return static_cast<std::int32_t>(out);
}

// Hover/stand clips: slower or faster with the flight state; transitions: by control state.
std::int32_t clip_speed_flight(ent::ModelRecord* model, cg::ClipInfo*) { return clip_speed(1, model); }
std::int32_t clip_speed_transition(ent::ModelRecord* model, cg::ClipInfo*) { return clip_speed(0, model); }

const bool s_speeds_registered = [] {
    cg::register_clip_speed("air_screamer_flight", clip_speed_flight);
    cg::register_clip_speed("air_screamer_transition", clip_speed_transition);
    return true;
}();

const char* const kNames[] = {
    "entity.ground_height", "entity.ground_kind",     "entity.slope_force",    "entity.ray_trace",
    "entity.ray_los",       "entity.collide",         "entity.in_active_cell", "entity.spawn_point",
    "entity.spawn_flags",   "entity.chara_spawn",     "entity.npc_mask",       "entity.vblanks",
    "entity.event_npc",     "entity.player_aim",      "entity.sfx_stop",       "entity.anim_play",
    "entity.anim_status_handover", "entity.bone_rotate_xyx", "entity.bone_multiply", "entity.bone_scale_xyz",
    "entity.bone_points",   "entity.ranged_attack",   "entity.flyby_active",   "entity.angles_between",
    "entity.chara_param_q12", "entity.chara_param_int",
};

// The ray and wall results stay between calls, as the original's statics did.
cg::RayRecord s_ray;
cg::WallResult s_hit;

int ray_out(bool result, long long* out) {
    out[0] = result ? 1 : 0;
    out[1] = s_ray.hit, out[2] = s_ray.ground_kind;
    out[3] = s_ray.target.x, out[4] = s_ray.target.y, out[5] = s_ray.target.z;
    out[6] = s_ray.distance, out[7] = s_ray.ground_height;
    return 8;
}

std::int32_t i32(const Args& a, int i) { return static_cast<std::int32_t>(a.integer(i)); }

ent::Vec3 vec(const Args& a, int i) { return {i32(a, i), i32(a, i + 1), i32(a, i + 2)}; }

std::int16_t i16(const Args& a, int i) { return static_cast<std::int16_t>(a.integer(i)); }

int call(std::string_view fn, const Args& a, long long* out, char* err, int errCap) {
    if (fn == "ground_height") {
        out[0] = sh::world::ground_point_cache().height(i32(a, 0), i32(a, 1));
        return 1;
    }
    if (fn == "ground_kind") {
        // The kind of collision cell under the point (0 none, 7 / 12 open ground the bird may fall through).
        out[0] = sh::world::ground_point_cache().kind(i32(a, 0), i32(a, 1));
        return 1;
    }
    if (fn == "slope_force") {
        // slope_force(x, z, dx, dz): the distance to the nearest height trigger along the step and its height.
        const sh::world::SlopeForce force = sh::world::slope_force(
            sh::world::sweep_active(sh::world::active_triggers(), i32(a, 0), i32(a, 1), i32(a, 2), i32(a, 3)));
        out[0] = force.distance, out[1] = force.height;
        return 2;
    }
    if (fn == "ray_trace") {
        // ray_trace(x, y, z, tx, ty, tz) -> blocked, has_hit, ground_type, x, y, z, distance, ground_height.
        const ent::Vec3 from = vec(a, 0), to = vec(a, 3);
        // The offset wraps like the game's 32-bit coordinates.
        const auto wrap_sub = [](std::int32_t p, std::int32_t q) {
            return static_cast<std::int32_t>(static_cast<std::uint32_t>(p) - static_cast<std::uint32_t>(q));
        };
        const sh::world::Vec3 offset{wrap_sub(to.x, from.x), wrap_sub(to.y, from.y), wrap_sub(to.z, from.z)};
        return ray_out(wg::trace(cg::world_record(s_ray),
                                 cg::world_vec(from), offset, wg::TraceKind::Solid, nullptr), out);
    }
    if (fn == "in_active_cell") {
        out[0] = sh::gameflow::demo_player().spawn_allowed(sh::gameflow::demo_host(), i32(a, 0), i32(a, 1)) ? 1 : 0;
        return 1;
    }
    if (fn == "spawn_point") {
        // spawn_point(index): the map's spawn point of that row, on the ground.
        const long long i = a.integer(0, -1);
        if (i < 0 || i >= kSpawnRows) {
            return fail(err, errCap, "spawn index out of range");
        }
        const sh::maps::SpawnPoint& p = sh::maps::map_runtime().spawn(static_cast<int>(i));
        out[0] = p.x, out[1] = sh::world::ground_point_cache().height(p.x, p.z), out[2] = p.z;
        return 3;
    }
    if (fn == "spawn_flags") {
        const long long i = a.integer(0, -1);
        if (i < 0 || i >= kSpawnRows) {
            return fail(err, errCap, "spawn index out of range");
        }
        out[0] = sh::maps::map_runtime().spawn(static_cast<int>(i)).flags;
        return 1;
    }
    if (fn == "chara_spawn") {
        // chara_spawn(chara_id, slot_index, x, z, rot, step): the slot's pending bit cleared, the NPC spawned.
        const std::int32_t slot = i32(a, 1) & 0x1F;
        std::int32_t& pending = sh::game::session().system.scene.spawned_groups;
        pending = static_cast<std::int32_t>(static_cast<std::uint32_t>(pending) & ~(1u << slot));
        out[0] = Chara_Spawn(i32(a, 0), slot, i32(a, 2), i32(a, 3), i16(a, 4), static_cast<std::uint32_t>(a.integer(5)));
        return 1;
    }
    if (fn == "npc_mask") {
        out[0] = static_cast<std::uint32_t>(sh::game::session().system.scene.npc_flags);
        return 1;
    }
    if (fn == "vblanks") {
        out[0] = sh::loop::frame_vblanks();
        return 1;
    }
    if (fn == "event_npc") {
        const auto& group = sh::game::session().system.group_npcs;
        const long long i = a.integer(0, -1);
        if (i < 0 || i >= static_cast<long long>(sizeof(group) / sizeof(group[0]))) {
            return fail(err, errCap, "event npc index out of range");
        }
        out[0] = group[i];
        return 1;
    }
    if (fn == "player_aim") {
        // player_aim() -> aiming, the distance the aim reaches.
        const bool aiming = sh::game::session().player.controls.aim != 0;
        out[0] = aiming ? 1 : 0;
        out[1] = aiming ? sh::game::player().attack_time() : 0;
        return 2;
    }
    if (fn == "sfx_stop") {
        sh::audio::game_sound_driver().stop_effect(static_cast<std::uint16_t>(a.integer(0)));
        return 0;
    }
    if (fn == "flyby_active") {
        // map0_s01's fly-by event moves the bird along its cutscene path; the behaviour only flaps.
        const auto* flag =
            static_cast<const std::int32_t*>(cg::link::overlay_variable(cg::link::OverlayVariable::AirScreamerFlyby));
        out[0] = flag != nullptr ? *flag : 0;
        return 1;
    }
    if (fn == "angles_between") {
        const ent::Vec3 p0 = vec(a, 0), p1 = vec(a, 3);
        out[0] = sh::world::heading_and_elevation({p0.x, p0.y, p0.z}, {p1.x, p1.y, p1.z});
        return 1;
    }
    if (fn == "chara_param_q12" || fn == "chara_param_int") {
        const char* name = a.text(1);
        if (name == nullptr) {
            return fail(err, errCap, "no parameter name");
        }
        const int id = static_cast<int>(a.integer(0));
        const int fb = static_cast<int>(a.integer(2));
        out[0] = fn == "chara_param_q12" ? Pc_CharaParamQ12(id, name, fb) : Pc_CharaParamInt(id, name, fb);
        return 1;
    }

    const int slot = static_cast<int>(a.integer(0, -1));
    ent::CharacterRecord* r = cg::record(slot);
    if (r == nullptr) {
        return fail(err, errCap, "bad character slot");
    }
    if (fn == "ray_los") {
        // ray_los(slot, x, y, z, dx, dy, dz): line of sight from a point along a step, the slot's own body
        // ignored; the same values as ray_trace.
        const ent::Vec3 from = vec(a, 1), delta = vec(a, 4);
        return ray_out(wg::trace(cg::world_record(s_ray),
                                 cg::world_vec(from), cg::world_vec(delta), wg::TraceKind::Sight, r), out);
    }
    if (fn == "collide") {
        // collide(slot, dx, dy, dz) -> result, the step after walls and characters (x, y, z).
        const ent::Vec3 offset = vec(a, 1);
        out[0] = wg::move_character(cg::world_record(s_hit), cg::world_vec(offset), *r);
        out[1] = s_hit.offset.x, out[2] = s_hit.offset.y, out[3] = s_hit.offset.z;
        return 4;
    }
    if (fn == "ranged_attack") {
        out[0] = PcCombat_AttackUpdate(reinterpret_cast<_SubCharacter*>(r));
        return 1;
    }
    if (fn == "anim_status_handover") {
        Chara_AnimStatusSet(i32(a, 1), r, i32(a, 2), i32(a, 3));
        return 0;
    }
    const cg::link::Skeleton sk = cg::link::skeleton(slot);
    if (sk.bones == nullptr || sk.animation == nullptr) {
        return fail(err, errCap, "the character has no bones");
    }
    if (fn == "anim_play") {
        // anim_play(slot[, frozen]): one tick of the current clip; frozen: with no time passing.
        std::int32_t& delta = sh::loop::frame_delta();
        const std::int32_t dt = delta;
        if (a.integer(1, 0) != 0) {
            delta = 0;
        }
        Chara_ModelAnimPlay(r, sk.animation, sk.bones);
        delta = dt;
        return 0;
    }
    const long long bone = a.integer(1, -1);
    if (bone < 0 || bone >= sk.bone_count) {
        return fail(err, errCap, "bone out of range");
    }
    ent::BoneCoord& b = sk.bones[bone];
    if (fn == "bone_rotate_xyx") {
        // bone_rotate_xyx(slot, bone, rx, ry, rz): the bone's matrix turned further by an X-Y-X rotation.
        cg::bones::turn_own(b, sh::math::rot_matrix_xyz({i16(a, 2), i16(a, 3), i16(a, 4)}));
        return 0;
    }
    if (fn == "bone_multiply") {
        // bone_multiply(slot, bone, m00, m01, m02, m10, m11, m12, m20, m21, m22): the bone's rotation times m.
        sh::math::Matrix3 m;
        for (int i = 0; i < 9; ++i) {
            m.m[i / 3][i % 3] = i16(a, 2 + i);
        }
        cg::bones::turn_own(b, m);
        return 0;
    }
    if (fn == "bone_scale_xyz") {
        cg::bones::scale_rows(b, i32(a, 2), i32(a, 3), i32(a, 4));
        return 0;
    }
    if (fn == "bone_points") {
        // bone_points(slot, bone, n, x0, y0, z0[, x1, y1, z1]): up to two points of the bone's space
        // (geometry units) in the world, Q12.
        const long long n = a.integer(2, 0);
        if (n < 1 || n > 2) {
            return fail(err, errCap, "one or two points");
        }
        sh::math::Vec3s pts[2] = {};
        for (int i = 0; i < n; ++i) {
            pts[i] = {i16(a, 3 + i * 3), i16(a, 4 + i * 3), i16(a, 5 + i * 3)};
        }
        const cg::bones::World world = cg::bones::world(b);
        sh::world::BoneFrame frame;
        frame.rotation = world.rotation;
        frame.translation = {world.t[0], world.t[1], world.t[2]};
        sh::world::WorldPoint res[2] = {};
        sh::world::bone_points_to_world(frame, std::span(pts, static_cast<std::size_t>(n)),
                                        std::span(res, static_cast<std::size_t>(n)));
        for (int i = 0; i < n; ++i) {
            out[i * 3] = res[i].x, out[i * 3 + 1] = res[i].y, out[i * 3 + 2] = res[i].z;
        }
        return static_cast<int>(n * 3);
    }
    return fail(err, errCap, "unknown function");
}

sh::chara::behavior::Extension s_extension = {
    nullptr, 0, kNames, sizeof(kNames) / sizeof(kNames[0]), call, nullptr, 0, nullptr,
};

const sh::chara::behavior::Registrar s_registrar{&s_extension};

}  // namespace
