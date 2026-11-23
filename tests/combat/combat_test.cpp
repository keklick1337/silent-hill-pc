// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)

#include "framework/sh_test.h"

#include "engine/combat/attack.h"
#include "engine/combat/combat_math.h"
#include "engine/combat/movement.h"

#include <array>
#include <string>
#include <vector>

using namespace sh::combat;

namespace {

// A world of seven characters with no walls, a fixed random number and a log of the effects.
class FakeView final : public CombatView, public MoveView {
public:
    std::array<Combatant, 7> chars{};
    std::array<AttackState, 7> attacks{};
    std::array<SwingClock, 7> clocks{};
    std::array<bool, 7> ticked{};
    std::array<std::int32_t, 7> damage{};
    std::array<Vec3, 7> push{};
    std::array<int, 7> received{};
    std::array<MoverState, 7> movers{};
    std::array<ShapeOffsets, 7> shapes{};
    sh::items::WeaponAttack row;
    std::int32_t levels[4] = {};
    std::vector<std::string> log;

    FakeView() { received.fill(-1); }

    std::int32_t delta_time() const override { return 137; }
    bool in_gameplay() const override { return true; }
    bool game_over() const override { return false; }
    bool room_fading_in() const override { return false; }
    bool no_target_cheat() const override { return false; }
    bool alternate_camera() const override { return false; }
    bool bullet_fat_hitbox() const override { return false; }
    bool melee_once_per_swing() const override { return false; }
    int target_npc() const override { return -1; }
    bool foggy() const override { return false; }
    std::int32_t aim_time() const override { return 0; }
    int hyper_blaster_color() const override { return 0; }
    std::int32_t random_q12() override { return 0; }
    sh::items::WeaponAttack attack_row(int) const override { return row; }
    int attack_count() const override { return 70; }
    Combatant combatant(int slot) const override {
        return slot >= 0 && slot < static_cast<int>(chars.size()) ? chars[static_cast<std::size_t>(slot)] : Combatant{};
    }
    AttackState attack_state(int slot) const override { return attacks[static_cast<std::size_t>(slot)]; }
    void set_attack_state(int slot, const AttackState& s) override { attacks[static_cast<std::size_t>(slot)] = s; }
    SwingClock swing_clock(int slot) const override { return clocks[static_cast<std::size_t>(slot)]; }
    std::int32_t swing_start_keyframe(int) const override { return 0; }
    void bind_player_clips() override {}
    bool attack_ticked(int slot) const override { return ticked[static_cast<std::size_t>(slot)]; }
    void set_attack_ticked(int slot, bool t) override { ticked[static_cast<std::size_t>(slot)] = t; }
    void add_damage(int slot, std::int32_t amount, const Vec3& p) override {
        damage[static_cast<std::size_t>(slot)] += amount;
        push[static_cast<std::size_t>(slot)] = p;
    }
    int attack_received(int slot) const override { return received[static_cast<std::size_t>(slot)]; }
    void set_attack_received(int slot, int a) override { received[static_cast<std::size_t>(slot)] = a; }
    void set_attacker(int, int) override {}
    ShotTrace trace_shot(const Vec3&, const Vec3&, int, bool) override { return {}; }
    bool line_blocked(const Vec3&, const Vec3&) override { return false; }
    void rumble(int pattern) override { log.push_back("rumble " + std::to_string(pattern)); }
    void motor_rumble(bool) override { log.push_back("motor"); }
    void play_sound(std::string_view name, const Vec3&, std::int32_t) override { log.push_back(std::string(name)); }
    void blood(int slot, const Vec3&, int, int kind) override {
        log.push_back("blood " + std::to_string(slot) + " " + std::to_string(kind));
    }
    void bullet_impact(const Vec3&, const Vec3&, const Vec3&) override {}
    void count_attack(AttackStat stat, bool hit, std::int32_t) override {
        log.push_back("stat " + std::to_string(static_cast<int>(stat)) + (hit ? " hit" : " miss"));
    }
    std::int32_t gas_sound_level(GasSound c) const override { return levels[level_index(c)]; }
    void set_gas_sound_level(GasSound c, std::int32_t l) override { levels[level_index(c)] = l; }
    void gas_sound_start(GasSound, std::int32_t v, std::int32_t) override { log.push_back("start " + std::to_string(v)); }
    void gas_sound_keep(GasSound, std::int32_t v, std::int32_t) override { log.push_back("keep " + std::to_string(v)); }
    void gas_sound_stop(GasSound) override { log.push_back("stop"); }

    MoverState mover(int slot) const override { return movers[static_cast<std::size_t>(slot)]; }
    void set_mover(int slot, const Vec3& p, std::int32_t fall) override {
        movers[static_cast<std::size_t>(slot)].position = p;
        movers[static_cast<std::size_t>(slot)].fall_speed = fall;
    }
    std::int32_t collide(int, const Vec3& offset, MoveSurface& s) override {
        s.offset = offset;
        s.ground_height = 0;
        return 0;
    }
    ShapeOffsets shape_offsets(int slot) const override { return shapes[static_cast<std::size_t>(slot)]; }
    void set_shape_offsets(int slot, const ShapeOffsets& o) override { shapes[static_cast<std::size_t>(slot)] = o; }
    std::int16_t yaw(int) const override { return 1024; }

private:
    static int level_index(GasSound c) {
        return c == GasSound::Contact ? 0 : c == GasSound::RockDrillRun ? 2 : 1;
    }
};

Combatant body(std::int32_t x, std::int32_t z, int chara_id) {
    Combatant c;
    c.present = true;
    c.chara_id = chara_id;
    c.position = Vec3{x, 0, z};
    c.health = 100 * 4096;
    c.collidable = true;
    c.box_top = -6000;
    c.radius = 1200;
    if (chara_id == 1) {  // Harry's traits (gamedata/chara/Harry/character.json)
        c.traits.is_player = true;
        c.traits.blood_kind = 1;
        c.traits.blood_at_zero_health = true;
    } else if (chara_id >= 2 && chara_id <= 24) {
        c.traits.enemy = true;
        if (chara_id == 4) {  // the Groaner's blood
            c.traits.blood_kind = 6;
        }
    }
    return c;
}

}  // namespace

SH_TEST(combat_math_known_values) {
    SH_CHECK(signed_angle(0) == 0);
    SH_CHECK(signed_angle(2048) == 2048);
    SH_CHECK(signed_angle(2049) == -2047);
    SH_CHECK(signed_angle(-1) == -1);
    SH_CHECK(signed_angle(4096 + 5) == 5);
    SH_CHECK(sector_angle(0) == 4096 && sector_angle(3) == 0x555 && sector_angle(15) == 0x111);
    SH_CHECK(sqrt_q12_table(4096) == 4096);
    SH_CHECK(sqrt_q12_table(16384) == 8192);
    SH_CHECK(sqrt_q12_table(1) == 64);
    SH_CHECK(sqrt_q12(4096) == 4096);
    SH_CHECK(square_sum_q12(4096, 4096, 0) == 8192);
    SH_CHECK(coarse_arc_cosine(0) == 0x3F6 && coarse_arc_cosine(1024) == 0);
}

SH_TEST(combat_point_segment_distance) {
    std::int16_t along = -1;
    // The point (0, 2 m) above the middle of a 4 m segment on the X axis.
    const std::uint32_t d = point_segment_distance(0, 8192, -8192, 0, 8192, 0, &along);
    SH_CHECK(d >= 8100 && d <= 8192);
    SH_CHECK(along >= 2000 && along <= 2100);
    // Beyond the end a: the distance to a, along 0.
    const std::uint32_t e = point_segment_distance(-16384, 0, -8192, 0, 8192, 0, &along);
    SH_CHECK(e >= 8100 && e <= 8192);
    SH_CHECK(along == 0);
}

SH_TEST(combat_step_offset_and_move) {
    // One second at 1 m/s straight along +Z (heading 0), no fall.
    const Vec3 step = step_offset(4096, 4096, 0, 0, 0, 0);
    SH_CHECK(step.x == 0 && step.z == 4096 && step.y == 0);
    // A large step is computed at reduced precision but stays close.
    const Vec3 fast = step_offset(4096, 40 * 4096, 1024, 0, 0, 0);
    SH_CHECK(fast.x >= 40 * 4096 - 64 && fast.x <= 40 * 4096 && fast.z >= -64 && fast.z <= 64);

    FakeView v;
    v.movers[2].position = Vec3{0, -100, 0};
    v.movers[2].move_speed = 4096;
    v.movers[2].fall_speed = 4096;
    MoveSurface s;
    SH_CHECK(move_character(v, 2, 10, 0, &s) == 0);
    // The fall goes below the ground at 0: the character lands and stops falling.
    SH_CHECK(v.movers[2].position.y == 0 && v.movers[2].fall_speed == 0);
    SH_CHECK(v.movers[2].position.x == 10 && s.offset.x == 10);

    v.shapes[1].box = PlanarOffset{100, 0};
    turn_shape_offsets(v, 1);  // a quarter turn
    SH_CHECK(v.shapes[1].box.x == 0 && v.shapes[1].box.z == -100);
}

SH_TEST(combat_player_swing_hits_npc_in_front) {
    FakeView v;
    v.chars[0] = body(0, 0, 1);  // Harry
    v.chars[1] = body(0, 2048, 4);
    v.row.hit_type = sh::items::WeaponAttack::kHitHeavySweep;
    v.row.reach_q12 = 4096;
    v.row.power = 10;
    v.row.keyframe_length = 5;
    v.row.blood_splatter = 3;
    v.clocks[0] = SwingClock{3, 2 * 4096};

    const int result = request_attack(v, 1, 1, kPlayerSlot, Vec3{0, -4096, 0}, 1, 0, 1024);
    SH_CHECK(result == kNpcSlots);  // 6 - index of NPC 0
    // power 10 at the fixed random factor 1.125, four times against a target not already recoiling.
    SH_CHECK(v.damage[1] == 10 * 4096 * 4608 / 4096 * 4);
    SH_CHECK(v.received[1] == 1);
    SH_CHECK(v.attacks[0].phase == attack_phase::kFinished);
    SH_CHECK(v.attacks[0].hit_mask == 1);
    SH_CHECK(v.ticked[0]);
    bool blood = false, stat = false;
    for (const std::string& l : v.log) {
        blood = blood || l == "blood 1 6";
        stat = stat || l == "stat 6 hit";
    }
    SH_CHECK(blood && stat);

    // An idle attack nobody updated is cancelled at the end of the tick.
    v.ticked[0] = false;
    settle_attack(v, kPlayerSlot);
    SH_CHECK(v.attacks[0].phase == attack_phase::kIdle && v.attacks[0].hit_mask == 0);
}

SH_TEST(combat_gas_sound_levels) {
    FakeView v;
    SH_CHECK(gas_sound_update(v, GasSound::ChainsawRun, 128, 0));
    SH_CHECK(v.levels[1] == (128 + 256) * 4096);
    SH_CHECK(v.log.back() == "start 128");
    gas_sound_motor(v, true, 300, 0);
    SH_CHECK(v.levels[1] == 511 * 4096);
    SH_CHECK(v.log.back() == "motor");
    gas_sound_update(v, GasSound::ChainsawRun, -1, 0);  // fades by delta_time << 9
    SH_CHECK(v.levels[1] == 511 * 4096 - (137 << 9));
    gas_sound_stop_all(v);
    SH_CHECK(v.levels[0] == 0 && v.levels[1] == 0 && v.levels[2] == 0);
}
