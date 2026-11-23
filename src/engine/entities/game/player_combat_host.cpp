// The named calls Harry's combat script (gamedata/chara/Harry/combat.lua) makes:
// clip and attack rows, the inventory counts, the weapon's motor, rays, the alternate camera's aim and the weapon
// effects. The rays, effects and attack table belong to other modules (plain-type declarations below).

#include <cstddef>
#include <cstdint>
#include <iterator>
#include <string>
#include <string_view>

#include "engine/chara/behavior_host.h"
#include "engine/chara/runtime/chara_data.h"
#include "engine/entities/character_record.h"
#include "engine/game_state/save_record.h"
#include "engine/game_state/session.h"
#include "engine/ids/game_numbers.h"
#include "engine/ids/ids.h"
#include "engine/inventory/item_catalog.h"
#include "engine/items/item_library.h"
#include "engine/player/player_records.h"

extern "C" {
extern std::int32_t g_TpsCamPos[3];
extern std::int32_t g_TpsCamFwd[3];
int HarryAttack_Info(int attack, int* reach, int* hit_start, int* hit_length);
void Player_WeaponModelStateSet(std::int32_t state, std::int8_t weapon_attack);
bool Ray_CharaTraceQuery(sh::game::RayHit* trace, const std::int32_t* from, std::int32_t* offset, void* excluded);
bool Ray_TraceQuery(sh::game::RayHit* trace, const std::int32_t* from, const std::int32_t* to);
std::int32_t Pc_AimAssistFind(const std::int32_t* position, const std::int32_t* forward, std::int32_t range,
                              std::int32_t* aim_point);
void Combat_WeaponEffectSpawn(std::int32_t weapon_attack, std::int16_t rot_y, std::int16_t rot_x,
                              sh::entities::BoneCoord* coord);
void WeaponBeam_SparkLineDraw(const std::int32_t* from, const std::int32_t* to);
void WeaponBeam_HyperBlasterDraw(const std::int32_t* from, const std::int16_t* yaw, const std::int16_t* pitch);
}

namespace {

namespace ent = sh::entities;
using sh::chara::behavior::Args;
using sh::chara::behavior::Field;
using sh::chara::behavior::Kind;
using sh::chara::behavior::fail;
using sh::game::HarryVars;

sh::game::Session& s() { return sh::game::session(); }

#define F(name, member, kind) \
    {name, offsetof(ent::CharacterRecord, vars) + offsetof(HarryVars, member), SH_FIELD_KIND(HarryVars, member, kind)}

const Field kFields[] = {
    // Ticks the target lock has held still (the lock's turn waits for the aim clip).
    F("harry_aim_hold", aim_hold, S32),
    // Time in the attack (Q12 s).
    F("harry_attack_time", attack_time, S32),
    // Up-down angle to the aimed target (Q12 angle; 1024 level).
    F("harry_aim_pitch", aim_pitch, S16),
};

#undef F

const char* const kNames[] = {
    "entity.clip_info",        "entity.weapon_attack_info", "entity.item_number",
    "entity.inventory_count_set", "entity.inventory_slot_count", "entity.inventory_slot_count_set",
    "entity.weapon_model_set", "entity.ray_trace_slot",     "entity.ray_world",
    "entity.tps_camera",       "entity.aim_assist_find",    "entity.harry_bone_world",
    "entity.harry_weapon_effect", "entity.weapon_beam",     "entity.hyper_blaster_beam",
    "entity.mod_attack",       "entity.weapon_data",       "entity.weapon_keyframes",
};

struct Vec {
    std::int32_t v[3];
};

Vec vec(const Args& a, int i) {
    return {{static_cast<std::int32_t>(a.integer(i)), static_cast<std::int32_t>(a.integer(i + 1)),
             static_cast<std::int32_t>(a.integer(i + 2))}};
}

int call(std::string_view fn, const Args& a, long long* out, char* err, int errCap) {
    ent::CharacterSlots& slots = s().system.characters;
    sh::game::SaveRecord& save = sh::game::save_record();
    if (fn == "clip_info") {
        // clip_info(slot, status) -> first keyframe, last keyframe, variable speed (1) or fixed, the fixed
        // speed (keyframes per second, Q12; negative plays backward), the status it hands over to.
        const ent::CharacterRecord* r = slots.record(static_cast<int>(a.integer(0, -1)));
        if (r == nullptr) {
            return fail(err, errCap, "bad character slot");
        }
        sh::chara::behavior::ClipTable t{};
        if (!sh::chara::behavior::clip_table(r->model.chara_id, 0, &t)) {
            return fail(err, errCap, "the character has no clip table");
        }
        const long long status = a.integer(1, -1);
        if (status < 0 || status >= t.count) {
            return fail(err, errCap, "clip status out of range");
        }
        const sh::game::ClipTableEntry& info = static_cast<const sh::game::ClipTableEntry*>(t.table)[status];
        out[0] = info.start_keyframe;
        out[1] = info.end_keyframe;
        out[2] = info.has_variable_duration ? 1 : 0;
        out[3] = info.duration.constant;
        out[4] = info.link_status;
        return 5;
    }
    if (fn == "weapon_attack_info") {
        // weapon_attack_info(attack) -> reach (Q12), first keyframe of the hit window after the clip start,
        // its length in keyframes (gamedata/weapons attack row), rounds per magazine of the weapon.
        const long long i = a.integer(0, -1);
        int row[3] = {};
        if (i < 0 || i > 0xFFFF || HarryAttack_Info(static_cast<int>(i), &row[0], &row[1], &row[2]) == 0) {
            return fail(err, errCap, "weapon attack out of range");
        }
        out[0] = row[0], out[1] = row[1], out[2] = row[2];
        out[3] = sh::inventory::item_catalog().gun_clip(static_cast<int>(i));
        return 4;
    }
    if (fn == "mod_attack") {
        // mod_attack(attack) -> the weapon number a mod's weapon row belongs to (-1: an attack of the original,
        // decoded by its arithmetic), its input (0 tap, 1 hold, 2 multitap), 1 for a gun's row.
        const int attack = static_cast<int>(a.integer(0, -1));
        const int weapon = sh::ids::mod_attack_weapon(attack);
        out[0] = weapon;
        out[1] = weapon >= 0 ? sh::ids::mod_attack_input(attack) : 0;
        out[2] = weapon >= 0 && sh::ids::attack_is_gun(attack) ? 1 : 0;
        return 3;
    }
    if (fn == "weapon_data" || fn == "weapon_keyframes") {
        // weapon_data(number) -> what gamedata/weapons/<Name>.json says of the weapon: the item it is carried as and
        // its ammo item (-1 none), its attack, reload and empty sounds (rig "sounds"; 0 none).
        // weapon_keyframes(number) -> the melee follow-up keyframes and the alternate cameras' aim hold keyframes
        // (first person, other) of its rig "anims" (-1 none), the base weapon it is "like" (-1: not a base one).
        // An unknown weapon answers -1 everywhere.
        const sh::items::ItemLibrary* library = sh::inventory::item_catalog().library();
        const sh::items::WeaponDef* w =
            library != nullptr ? library->find_weapon(static_cast<int>(a.integer(0, -1))) : nullptr;
        constexpr int count = 5;
        for (int i = 0; i < count; ++i) {
            out[i] = -1;
        }
        if (w == nullptr) {
            return count;
        }
        if (fn == "weapon_keyframes") {
            out[0] = w->rig.follow_up_keyframes[0], out[1] = w->rig.follow_up_keyframes[1];
            out[2] = w->rig.aim_hold_keyframes[0], out[3] = w->rig.aim_hold_keyframes[1];
            // "like" (a base weapon), by default the steel pipe or the handgun; a base weapon is its own.
            std::string like = w->like;
            if (like.empty() && !sh::ids::is_base(w->id)) {
                like = w->is_gun() ? "Handgun" : "SteelPipe";
            }
            const sh::items::WeaponDef* base = like.empty() ? w : library->find_weapon(like, w->id);
            out[4] = base != nullptr && sh::ids::is_base(base->id) ? base->number : -1;
            return count;
        }
        const auto item = [](const std::string& id) { return id.empty() ? -1 : sh::ids::item_number(id.c_str()); };
        const auto sound = [](const std::string& name) {
            const int id = name.empty() ? 0 : Pc_SoundIdFind(name.c_str());
            return id < 0 ? 0 : id;
        };
        out[0] = item(w->item);
        out[1] = item(w->ammo);
        out[2] = sound(w->rig.attack_sound);
        out[3] = sound(w->rig.reload_sound);
        out[4] = sound(w->rig.out_of_ammo_sound);
        return count;
    }
    if (fn == "item_number") {
        // item_number(name) -> the run-time number of an item (gamedata/items/<Name>.json), -1 unknown.
        out[0] = a.text(0) != nullptr ? sh::ids::item_number(a.text(0)) : -1;
        return 1;
    }
    if (fn == "inventory_count_set") {
        // inventory_count_set(item, count): every inventory slot holding the item gets the count.
        const long long item = a.integer(0, -1);
        for (sh::game::SaveItem& slot : save.items) {
            if (item >= 0 && slot.item == item) {
                slot.count = static_cast<std::int32_t>(a.integer(1));
            }
        }
        return 0;
    }
    if (fn == "inventory_slot_count" || fn == "inventory_slot_count_set") {
        // inventory_slot_count(slot) / inventory_slot_count_set(slot, count): the count of an inventory slot.
        const long long slot = a.integer(0, -1);
        if (slot < 0 || slot >= sh::game::SaveRecord::kItemSlots) {
            return fail(err, errCap, "inventory slot out of range");
        }
        if (fn == "inventory_slot_count") {
            out[0] = save.items[slot].count;
            return 1;
        }
        save.items[slot].count = static_cast<std::int32_t>(a.integer(1));
        return 0;
    }
    if (fn == "weapon_model_set") {
        // weapon_model_set(attack, state): the held weapon's motor and sound (chainsaw, rock drill): 0 starts
        // it, 1 runs, -1 stops.
        Player_WeaponModelStateSet(static_cast<std::int32_t>(a.integer(0)), static_cast<std::int8_t>(a.integer(1)));
        return 0;
    }
    if (fn == "ray_trace_slot") {
        // ray_trace_slot(slot, x, y, z, dx, dy, dz) -> hit, the slot of the character hit (-1 none), the point.
        // The ray runs from (x, y, z) along (dx, dy, dz) against the world and the characters but `slot`'s.
        ent::CharacterRecord* r = slots.record(static_cast<int>(a.integer(0, -1)));
        if (r == nullptr) {
            return fail(err, errCap, "bad character slot");
        }
        const Vec from = vec(a, 1);
        Vec offset = vec(a, 4);
        sh::game::RayHit trace{};
        out[0] = Ray_CharaTraceQuery(&trace, from.v, offset.v, r) ? 1 : 0;
        out[1] = trace.character != nullptr ? slots.slot_of(trace.character) : -1;
        out[2] = trace.target[0], out[3] = trace.target[1], out[4] = trace.target[2];
        return 5;
    }
    if (fn == "ray_world") {
        // ray_world(x0, y0, z0, x1, y1, z1) -> hit, the point where the segment meets the world (its end
        // when it meets nothing).
        const Vec from = vec(a, 0);
        const Vec to = vec(a, 3);
        sh::game::RayHit trace{};
        out[0] = Ray_TraceQuery(&trace, from.v, to.v) ? 1 : 0;
        out[1] = trace.target[0], out[2] = trace.target[1], out[3] = trace.target[2];
        return 4;
    }
    if (fn == "tps_camera") {
        // tps_camera() -> position, forward unit vector (Q12) of the PC's alternate camera.
        out[0] = g_TpsCamPos[0], out[1] = g_TpsCamPos[1], out[2] = g_TpsCamPos[2];
        out[3] = g_TpsCamFwd[0], out[4] = g_TpsCamFwd[1], out[5] = g_TpsCamFwd[2];
        return 6;
    }
    if (fn == "aim_assist_find") {
        // aim_assist_find(x, y, z, fx, fy, fz, range) -> the NPC the alternate camera's aim assist picks
        // (-1 none), its aim point.
        const Vec pos = vec(a, 0);
        const Vec fwd = vec(a, 3);
        Vec aim{};
        out[0] = Pc_AimAssistFind(pos.v, fwd.v, static_cast<std::int32_t>(a.integer(6)), aim.v);
        out[1] = aim.v[0], out[2] = aim.v[1], out[3] = aim.v[2];
        return 4;
    }
    if (fn == "harry_bone_world") {
        // harry_bone_world(bone) -> the bone's world position as last computed (geometry units, 256 = 1 m).
        const long long bone = a.integer(0, -1);
        if (bone < 0 || bone >= ent::kPlayerBoneCount) {
            return fail(err, errCap, "bone out of range");
        }
        const ent::BoneCoord& b = slots.player_bones[bone];
        out[0] = b.world.t[0], out[1] = b.world.t[1], out[2] = b.world.t[2];
        return 3;
    }
    if (fn == "harry_weapon_effect") {
        // harry_weapon_effect(attack, rot_y, rot_x): the weapon's effect (muzzle flash, the motor's smoke) at
        // Harry's bones.
        Combat_WeaponEffectSpawn(static_cast<std::int32_t>(a.integer(0)), static_cast<std::int16_t>(a.integer(1)),
                                 static_cast<std::int16_t>(a.integer(2)), slots.player_bones);
        return 0;
    }
    if (fn == "weapon_beam") {
        // weapon_beam(x0, y0, z0, x1, y1, z1): the powered weapon's spark line between the two points.
        const Vec from = vec(a, 0);
        const Vec to = vec(a, 3);
        WeaponBeam_SparkLineDraw(from.v, to.v);
        return 0;
    }
    if (fn == "hyper_blaster_beam") {
        // hyper_blaster_beam(x, y, z, yaw, pitch): the Hyper Blaster's beam from the point.
        const Vec from = vec(a, 0);
        const std::int16_t yaw = static_cast<std::int16_t>(a.integer(3));
        const std::int16_t pitch = static_cast<std::int16_t>(a.integer(4));
        WeaponBeam_HyperBlasterDraw(from.v, &yaw, &pitch);
        return 0;
    }
    return sh::chara::behavior::kNotMine;
}

sh::chara::behavior::Extension s_extension = {
    kFields, std::size(kFields), kNames, std::size(kNames), call, nullptr, 0, nullptr,
};

const sh::chara::behavior::Registrar s_registrar{&s_extension};

}  // namespace
