// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// The Puppet Nurse / Puppet Doctor part of the behaviour API (gamedata/chara/PuppetNurse/behavior.lua, shared by
// PuppetNurse, DummyNurse, PuppetDoctor and DummyDoctor): the record's pointer to its stats entry served as an index
// (the values live in the Lua file), and the generic calls the behaviour needs that the enemy API does not have.
// The two clip tables of the type are character data (character.json "clip_tables": the nurses', the doctor's).

#include <cstdint>
#include <string_view>

#include "engine/chara/behavior_host.h"
#include "engine/chara/game/chara_entry.h"
#include "engine/chara/game/chara_link.h"
#include "engine/chara/game/chara_records.h"

namespace {

namespace cg = sh::chara::game;
namespace ent = sh::entities;
using sh::chara::behavior::Args;
using sh::chara::behavior::fail;

constexpr int kStatsCount = 4;

const char* const kNames[] = {
    "entity.puppet_nurse_stats",    "entity.puppet_nurse_stats_set",
    // generic
    "entity.movement_update",       "entity.weapon_attack_stats",   "entity.bone_matrix_scale",
};

int call(std::string_view fn, const Args& a, long long* out, char* err, int errCap) {
    if (fn == "weapon_attack_stats") {
        // weapon_attack_stats(attack): the attack's base damage and hit type (weapon attack table).
        const long long i = a.integer(0, -1);
        if (i < 0 || i >= cg::link::attack_count()) {
            return fail(err, errCap, "weapon attack out of range");
        }
        const sh::items::WeaponAttack row = cg::link::attack_row(static_cast<int>(i));
        out[0] = row.power;
        out[1] = row.hit_type;
        return 2;
    }
    const int slot = static_cast<int>(a.integer(0, -1));
    ent::CharacterRecord* r = cg::record(slot);
    if (r == nullptr) {
        return fail(err, errCap, "bad character slot");
    }
    if (fn == "puppet_nurse_stats") {
        // The stats entry the record points at, -1 when the pointer is not one of them (a reused slot).
        out[0] = cg::link::puppet_nurse_stats(slot);
        return 1;
    }
    if (fn == "puppet_nurse_stats_set") {
        const long long i = a.integer(1, -1);
        if (i < 0 || i >= kStatsCount) {
            return fail(err, errCap, "stats entry out of range");
        }
        cg::link::set_puppet_nurse_stats(slot, static_cast<int>(i));
        return 0;
    }
    if (fn == "movement_update") {
        // The step of this tick (move_speed along heading_angle, fall_speed) with collision; the game's result.
        out[0] = PcCombat_Move(reinterpret_cast<_SubCharacter*>(r));
        return 1;
    }
    if (fn == "bone_matrix_scale") {
        // bone_matrix_scale(slot, bone, scale): every element of the bone's rotation matrix times the Q12 scale.
        const cg::link::Skeleton sk = cg::link::skeleton(slot);
        const int bone = static_cast<int>(a.integer(1, -1));
        if (sk.bones == nullptr || sk.animation == nullptr || bone < 0 || bone >= sk.bone_count) {
            return fail(err, errCap, "bone out of range");
        }
        const auto scale = static_cast<std::int64_t>(a.integer(2));
        for (auto& row : sk.bones[bone].local.m) {
            for (std::int16_t& m : row) {
                m = static_cast<std::int16_t>(static_cast<std::int32_t>((scale * m) >> 12));
            }
        }
        return 0;
    }
    return sh::chara::behavior::kNotMine;
}

sh::chara::behavior::Extension s_extension = {
    nullptr, 0, kNames, sizeof(kNames) / sizeof(kNames[0]), call, nullptr, 0, nullptr,
};

const sh::chara::behavior::Registrar s_registrar{&s_extension};

}  // namespace
