// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)

#include "engine/combat/attack.h"

#include "engine/entities/character_record.h"
#include "engine/world/fixed_math.h"
#include "engine/world/fixed_trig.h"

#include <array>

namespace sh::combat {

namespace {

namespace phase = attack_phase;
using sh::items::WeaponAttack;
using sh::world::fixed_atan2;
using sh::world::fixed_cos;
using sh::world::fixed_sin;

constexpr std::int32_t kOne = 4096;                   // 1.0 in Q12
constexpr std::int32_t kMotorAudible = 256 * kOne;    // a gas sound level above this is heard
constexpr std::int32_t kSoundLevelMax = 511 * kOne;
constexpr std::int32_t kMaxShotDeviation = 170;       // Q12 turns
constexpr std::int32_t kFreeAimShotCap = 5;           // 0.5 degrees
constexpr int kSweepSteps = 4;                        // a melee swing is tested at four points per tick

// A shot of several pellets (the shotgun's): its rays share one hit per target.
bool scatter_shot(const WeaponAttack& row) { return row.gun_shot && row.pellets() > 1; }

bool motor_running(const CombatView& view) {
    return view.gas_sound_level(GasSound::ChainsawRun) > kMotorAudible;
}

std::int32_t wrap_add(std::int32_t a, std::int32_t b) {
    return static_cast<std::int32_t>(static_cast<std::uint32_t>(a) + static_cast<std::uint32_t>(b));
}

std::int32_t wrap_mul(std::int32_t a, std::int32_t b) {
    return static_cast<std::int32_t>(static_cast<std::uint32_t>(a) * static_cast<std::uint32_t>(b));
}

// (a * b) >> 12 with `a` an unsigned 32-bit quantity widened without sign, as the original's unsigned damage and
// push values are.
std::int32_t mul_q12_unsigned(std::uint32_t a, std::int32_t b) {
    return static_cast<std::int32_t>((static_cast<std::int64_t>(a) * b) >> 12);
}

// The NPC bit of a slot in a hit mask. The mask has the original's bit per NPC; the engine's slots past 32 share
// the bits of the first ones (a swing striking both counts as one), which the original's six never meet.
std::int32_t npc_bit(int slot) {
    return static_cast<std::int32_t>(1u << ((slot - 1) & 31));
}

// Unit direction (Q12) of a yaw and a pitch from the vertical.
struct Direction {
    std::int32_t x = 0, y = 0, z = 0;
};

Direction direction_of(std::int32_t yaw, std::int32_t pitch) {
    const std::int32_t across = fixed_sin(pitch);
    return Direction{mul_q12(across, fixed_sin(yaw)), fixed_cos(pitch), mul_q12(across, fixed_cos(yaw))};
}

// The impact sound of a weapon at the point it struck (the row's "impact_sound"; a motor row with the motor running
// grinds instead).
void impact_sound(CombatView& view, const Vec3& point, const WeaponAttack& row) {
    if (row.motor_contact && motor_running(view)) {
        gas_sound_contact(view, 128);
        return;
    }
    if (!row.impact_sound.empty()) {
        view.play_sound(row.impact_sound, point, 192);
    }
}

// The hit of `attacker`'s attack on `target` at `point`. `scale` (Q12) shares a light sweep's damage over the steps
// it lands on. Returns the target's bit in the hit mask, 0 when the attack passes through it.
std::int32_t strike(CombatView& view, int attacker, AttackState& state, int target, const Vec3& point,
                    std::int32_t scale) {
    const Combatant victim = view.combatant(target);
    if (victim.no_collide) {
        return 0;
    }
    const int attack = state.attack;
    const WeaponAttack row = view.attack_row(attack);
    const std::int32_t already = state.hit_mask;

    std::int32_t bit;
    if (target == kPlayerSlot) {
        bit = -1;
        view.set_attacker(target, attacker - 1);
    } else {
        bit = npc_bit(target);
        if ((already & bit) != 0) {
            if (row.motor_contact && motor_running(view)) {
                gas_sound_contact(view, 128);
            }
        } else {
            if (row.hit_type == WeaponAttack::kHitHeavySweep) {
                view.rumble(6);
            } else if (row.hit_type == WeaponAttack::kHitLightSweep) {
                view.rumble(7);
            }
            if (!victim.traits.impact_sound.empty()) {
                view.play_sound(victim.traits.impact_sound, point, 128);
            } else {
                impact_sound(view, point, row);
            }
        }
    }

    std::uint32_t base = static_cast<std::uint32_t>(row.power) * kOne;
    if (row.gas) {
        // The row's window is compared with the checked time as stored (Q12 against whole keyframes), so a gas
        // weapon's swing itself deals no damage; the running motor's bonus does.
        if (!in_hit_window(row, state.checked_time)) {
            base = 0;
        }
        if (motor_running(view)) {
            base += 200u * kOne;
        }
    } else if (row.colour_modes) {
        switch (view.hyper_blaster_color()) {
            case 1:
                base += base * 2;
                break;
            case 2:
                base += static_cast<std::uint32_t>(view.random_q12()) * 50u;
                base *= 5;
                break;
            default:
                break;
        }
    } else if (row.finisher) {
        if (victim.downed) {
            base = 1000u * kOne;
        }
    }

    // 0.875 .. 1.125 at random.
    std::uint32_t push = static_cast<std::uint32_t>(row.knockback_q12);
    const std::int32_t spread = (18432 - view.random_q12()) >> 2;
    std::int32_t damage = mul_q12_unsigned(base, spread);

    Vec3 direction;
    if (row.gun_shot) {
        direction = state.segment[1];
        if (scatter_shot(row)) {
            if ((already & bit) != 0) {
                push = 0;
            }
            direction = Vec3{};
        }
    } else {
        if (target != kPlayerSlot && !victim.recoiling) {
            damage = wrap_mul(damage, 4);
        }
        if (row.hit_type == WeaponAttack::kHitLightSweep) {
            damage = mul_q12_wide(damage, scale);
            push = static_cast<std::uint32_t>(mul_q12_unsigned(push, scale));
        }
        if (row.finisher) {
            direction = Vec3{state.segment[0].x, 0, state.segment[0].z};
        } else {
            // From the far end of the previous segment to the far end of this one.
            const Vec3 before{state.prev_origin.x + state.segment[2].x, state.prev_origin.y + state.segment[2].y,
                              state.prev_origin.z + state.segment[2].z};
            const Vec3 now{state.segment_start[0].x + state.segment[0].x,
                           state.segment_start[0].y + state.segment[0].y,
                           state.segment_start[0].z + state.segment[0].z};
            direction = Vec3{now.x - before.x, now.y - before.y, now.z - before.z};
        }
        auto length = static_cast<std::int32_t>(
            sqrt_q12_table(square_sum_q12(direction.x, direction.y, direction.z)));
        if (length >= 64) {
            if (length >= 4096 * kOne) {
                length /= kOne;
                direction = Vec3{direction.x / kOne, direction.y / kOne, direction.z / kOne};
            }
            const std::int32_t inverse = (4096 * kOne) / length;
            direction = Vec3{mul_q12_wide(direction.x, inverse), mul_q12_wide(direction.y, inverse),
                             mul_q12_wide(direction.z, inverse)};
        }
    }

    // GAMEPLAY CHANGE (melee_once_per_swing): a target is struck once per swing. The gas weapons (continuous) and the
    // guns (every pellet of a shot is its own hit in the same tick) keep the original rule.
    if (view.melee_once_per_swing() && (already & bit) != 0 && !row.gas && !row.gun_shot) {
        damage = 0;
        push = 0;
    }

    Vec3 shove;
    if (push != 0) {
        shove = Vec3{mul_q12_unsigned(push, direction.x), mul_q12_unsigned(push, direction.y),
                     mul_q12_unsigned(push, direction.z)};
    }
    if (damage != 0 || push != 0) {
        view.add_damage(target, damage, shove);
    }
    view.set_attack_received(target, attack);
    state.hit_mask = already | bit;

    if ((static_cast<std::uint32_t>(damage) | push) != 0) {
        int strength = row.blood_splatter;
        if (strength > 0 && strength < 8) {
            const CombatTraits& traits = victim.traits;
            const int kind = traits.blood_kind;
            if (traits.blood_strength >= 0) {
                strength = traits.blood_strength;
            }
            if (traits.fresh_hit_blood_strength >= 0 && !victim.recoiling) {
                const bool scatter = scatter_shot(row);
                strength = scatter && traits.fresh_hit_blood_strength_scatter >= 0
                               ? traits.fresh_hit_blood_strength_scatter
                               : traits.fresh_hit_blood_strength;
            }
            // A corpse spills no more blood (a body collapsed near the camera made the effect's size overflow); the
            // player still bleeds at exactly 0, as the original's check on him was health >= 0.
            const bool bleeds = traits.blood_at_zero_health ? victim.health >= 0 : victim.health > 0;
            if (kind >= 0 && bleeds) {
                view.blood(target, point, strength, kind);
            }
        }
    }
    return bit;
}

// Moves the hit segment to `origin` in the direction (yaw, pitch); the previous segment is kept in [1].
void advance_segment(const CombatView& view, AttackState& state, std::int32_t yaw, std::int32_t pitch) {
    state.segment[1] = state.segment[0];
    state.segment_start[1] = state.segment_start[0];
    const WeaponAttack row = view.attack_row(state.attack);
    const Direction d = direction_of(yaw, pitch);
    const std::int32_t near_end = row.reach_start_q12;
    const std::int32_t length = row.reach_q12 - near_end;
    state.segment_start[0] = Vec3{state.origin.x + mul_q12(near_end, d.x), state.origin.y + mul_q12(near_end, d.y),
                                  state.origin.z + mul_q12(near_end, d.z)};
    state.segment[0] = Vec3{mul_q12(length, d.x), mul_q12(length, d.y), mul_q12(length, d.z)};
}

void begin_sweep(const CombatView& view, AttackState& state) {
    state.prev_aim_yaw = state.aim_yaw;
    state.prev_aim_pitch = state.aim_pitch;
    advance_segment(view, state, state.aim_yaw, state.aim_pitch);
    state.prev_origin = state.origin;
}

// The weapon's hit box at one step of the sweep against every character it can strike. Returns the mask of those it
// struck.
std::int32_t sweep_step(CombatView& view, int attacker, AttackState& state, std::int32_t yaw, const WeaponAttack& row,
                        std::int32_t scale) {
    const Combatant self = view.combatant(attacker);
    const Vec3 start = state.segment_start[0];
    const std::int32_t s = fixed_sin(yaw);
    const std::int32_t c = fixed_cos(yaw);

    // The segment in the attacker's frame turned by the swing's yaw, and the box around it.
    const Vec3 reach = state.segment[0];
    const std::int32_t local_x = mul_q12_wide(reach.x, c) - mul_q12_wide(reach.z, s);
    const std::int32_t local_z = mul_q12_wide(reach.x, s) + mul_q12_wide(reach.z, c);
    std::int32_t max_x = 0, min_x = 0, max_y = 0, min_y = 0, max_z = 0, min_z = 0;
    if (local_x > 0) {
        max_x = local_x;
    } else if (local_x < 0) {
        min_x = local_x;
    }
    if (reach.y > 0) {
        max_y = reach.y;
    } else if (reach.y < 0) {
        min_y = reach.y;
    }
    if (local_z > 0) {
        max_z = local_z;
    } else if (local_z < 0) {
        min_z = local_z;
    }
    const std::uint16_t grow_x = row.min_hit_box_q12(0);
    const std::int32_t grow_y = row.min_hit_box_q12(1);
    const std::int32_t grow_z = row.min_hit_box_q12(2);
    if (max_x - min_x < grow_x) {
        max_x += grow_x >> 1;
        min_x -= grow_x >> 1;
    }
    if (max_y - min_y < grow_y) {
        max_y += grow_y >> 1;
        min_y -= grow_y >> 1;
    }
    if (max_z - min_z < grow_z) {
        max_z += grow_z >> 1;
        min_z -= grow_z >> 1;
    }

    int first;
    int count;
    std::int32_t bit;
    if (attacker == kPlayerSlot) {
        // Every NPC slot: the original's six first, in its order, then the engine's (spawned characters).
        first = 1, count = sh::entities::kNpcSlotCount, bit = 1;
    } else {
        first = kPlayerSlot, count = 1, bit = -1;
    }

    // The sight line starts at the attacker's box top. Its horizontal start then follows the near side of each
    // target that gets this far (the original reuses the variables), which only matters for the player's swing.
    const Vec3 eye{self.position.x, self.position.y + self.box_top, self.position.z};
    std::int32_t sight_x = self.position.x;
    std::int32_t sight_z = self.position.z;
    // Likewise the point a hit is placed at carries over from one struck target to the next.
    std::int32_t tip_x = reach.x, tip_y = reach.y, tip_z = reach.z;

    const bool player_alive = view.combatant(kPlayerSlot).health > 0;
    std::int32_t struck = 0;
    for (int slot = first; slot < first + count; ++slot, bit = npc_bit(slot)) {
        const Combatant t = view.combatant(slot);
        if (view.game_over() || !player_alive || slot == attacker || !t.present || t.health < 0 ||
            !t.collidable) {
            continue;
        }
        const std::int32_t center_x = t.position.x + t.box_offset_x;
        const std::int32_t center_z = t.position.z + t.box_offset_z;
        if (planar_distance(self.position, Vec3{center_x, t.position.y, center_z}) > 3 * kOne) {
            continue;
        }

        const std::int32_t top = t.box_top;
        const std::int32_t radius = t.radius;
        const std::int32_t toward = fixed_atan2(center_x - sight_x, center_z - sight_z);
        sight_x = center_x - mul_q12_wide(radius, fixed_sin(toward));
        sight_z = center_z - mul_q12_wide(radius, fixed_cos(toward));
        if (view.line_blocked(eye, Vec3{sight_x, t.position.y + top, sight_z})) {
            continue;
        }

        // The target in the swing's frame.
        const std::int32_t rel_x = center_x - start.x;
        const std::int32_t rel_z = center_z - start.z;
        const std::int32_t feet = t.position.y;
        const std::int32_t height = t.box_height;
        const std::int32_t rel_y = feet - start.y;
        const std::int32_t u = mul_q12_wide(rel_x, c) - mul_q12_wide(rel_z, s);
        const std::int32_t w = mul_q12_wide(rel_x, s) + mul_q12_wide(rel_z, c);
        if (rel_y < min_y - height || max_y - top < rel_y) {
            continue;
        }
        std::int32_t out_x = 0;
        if (u < min_x) {
            out_x = min_x - u;
        } else if (max_x < u) {
            out_x = u - max_x;
        }
        std::int32_t out_z = 0;
        if (w < min_z) {
            out_z = min_z - w;
        } else if (max_z < w) {
            out_z = w - max_z;
        }
        if ((out_x | out_z) != 0) {
            if (out_x == 0) {
                if (radius < out_z) {
                    continue;
                }
            } else if (out_z == 0) {
                if (radius < out_x) {
                    continue;
                }
            } else if (mul_q12_wide(radius, radius) < mul_q12_wide(out_x, out_x) + mul_q12_wide(out_z, out_z)) {
                continue;
            }
        }

        // Where the blow lands: three quarters along the segment, pulled into the target's cylinder.
        tip_x = wrap_add(tip_x - quarter(tip_x), start.x);
        tip_y = wrap_add(tip_y - quarter(tip_y), start.y);
        tip_z = wrap_add(tip_z - quarter(tip_z), start.z);
        const std::int32_t middle = (top + height) / 2;
        std::int32_t dx = tip_x - center_x;
        std::int32_t dz = tip_z - center_z;
        std::int32_t dy = tip_y - (feet + middle);
        auto distance = static_cast<std::int32_t>(sqrt_q12_table(square_sum_q12(dx, 0, dz)));
        std::int32_t half = (height - top) / 2;
        if (half < 0) {
            half = 0;
        }
        for (int pass = 2; (half < dy || dy < -half || radius < distance) && pass > 0; --pass) {
            std::int32_t f;
            if (radius < distance) {
                f = wrap_mul(radius, kOne) / distance;
                distance = radius;
                dx = mul_q12_wide(f, dx);
                dy = mul_q12_wide(f, dy);
                dz = mul_q12_wide(f, dz);
            } else if (half < dy) {
                f = wrap_mul(half, kOne) / dy;
                dy = half;
                distance = mul_q12_wide(f, distance);
                dx = mul_q12_wide(f, dx);
                dz = mul_q12_wide(f, dz);
            } else {
                f = wrap_mul(-half, kOne) / dy;
                dy = -half;
                distance = mul_q12_wide(f, distance);
                dx = mul_q12_wide(f, dx);
                dz = mul_q12_wide(f, dz);
            }
        }
        const Vec3 point{center_x + dx, (feet + middle) + dy, center_z + dz};
        if (strike(view, attacker, state, slot, point, scale) != 0) {
            struck |= bit;
        }
    }
    state.hit_mask |= struck;
    return struck;
}

struct ShotPlan {
    std::int32_t deviation = 0;   // largest random deviation of a ray from the aim
    int distance_band = -1;       // AttackStat of a single-ray gun's shot, -1 when it had no target
};

// A single-ray gun's accuracy: worse the shorter the player aimed and in fog, better against a target lying close.
ShotPlan plan_single_shot(CombatView& view, int slot, const Combatant& self, const WeaponAttack& row) {
    std::int32_t aim = self.traits.is_player ? view.aim_time() : 2 * kOne;
    if (aim < kOne / 2) {
        aim = kOne / 2;
    } else if (aim > 2 * kOne) {
        aim = 2 * kOne;
    }
    const std::int32_t steadiness = wrap_mul(aim - kOne / 2, kOne) / (3 * kOne / 2);
    std::int32_t deviation = mul_q12_wide(steadiness, -3 * kOne / 2) + 7 * kOne / 4;
    deviation = mul_q12_wide(deviation, row.spread_q12());
    if (deviation < kMaxShotDeviation) {
        if (view.foggy()) {
            deviation = (deviation + kMaxShotDeviation) / 2;
        }
    } else {
        deviation = kMaxShotDeviation;
    }

    ShotPlan plan;
    const std::int32_t reach = row.reach_q12;
    const std::int32_t third = reach / 3;
    const int npc = view.target_npc();
    if (npc >= 0) {
        const Combatant t = view.combatant(npc + 1);
        if (t.traits.enemy) {
            const std::int32_t dx = self.position.x - t.position.x;
            const std::int32_t dy = self.position.y - t.position.y;
            const std::int32_t dz = self.position.z - t.position.z;
            const std::int32_t planar = wrap_add(mul_q12_wide(dx, dx), mul_q12_wide(dy, dy));
            const std::int32_t squared = wrap_add(mul_q12_wide(dz, dz), planar);
            const std::int32_t distance = sqrt_q12(squared);
            plan.distance_band = distance;
            if (t.downed) {
                // A target on the ground close by: the closer, the steadier.
                const std::int32_t past = squared - kOne / 2;
                if (past < 0) {
                    deviation = 0;
                } else if (past <= 0xFFFF) {
                    deviation = mul_q12_wide(deviation, (past * kOne) >> 16);
                }
            }
            if (view.foggy()) {
                plan.distance_band = static_cast<int>(AttackStat::FogShot);
            } else if (third >= distance) {
                if (distance <= 2 * kOne) {
                    plan.distance_band = static_cast<int>(AttackStat::CloseShot);
                    deviation = 0;
                } else {
                    plan.distance_band = static_cast<int>(AttackStat::NearShot);
                    deviation -= deviation / 2;
                }
            } else {
                plan.distance_band = static_cast<int>(AttackStat::FarShot);
            }
        }
    }

    plan.deviation = mul_q12_wide(deviation, view.random_q12());
    if (view.alternate_camera() && slot == kPlayerSlot && plan.deviation > kFreeAimShotCap) {
        plan.deviation = kFreeAimShotCap;
    }
    return plan;
}

// The ranking statistic of a finished attack: the row's "ranking".
AttackStat finished_stat(const WeaponAttack& row, int distance_band, bool foggy) {
    const std::string& r = row.ranking;
    if (r == "distance_shot") {
        if (distance_band != -1) {
            return foggy ? AttackStat::FogShot : static_cast<AttackStat>(distance_band);
        }
        return AttackStat::OtherShot;
    }
    if (r == "other_shot") {
        return AttackStat::OtherShot;
    }
    if (r == "hyper_blaster") {
        return AttackStat::HyperBlaster;
    }
    if (r == "heavy_melee") {
        return AttackStat::HeavyMelee;
    }
    if (r == "none") {
        return AttackStat::None;
    }
    return AttackStat::Melee;
}

// The attack's step this tick. `state` is the character's attack state, stored by the caller.
std::int32_t run_attack(CombatView& view, int slot, AttackState& state) {
    view.set_attack_ticked(slot, true);
    std::int32_t phase_now = state.phase;
    const int attack = state.attack;

    if (phase_now == phase::kIdle) {
        state.shots_fired = 0;
        state.hit_mask = 0;
        state.checked_time = 0;
        return 0;
    }
    if (phase_now == phase::kFinished) {
        state.hit_mask = 0;
        state.checked_time = 0;
        return 0;
    }

    bool done = false;
    int distance_band = -1;
    const WeaponAttack row = view.attack_row(attack);
    const SwingClock clock = view.swing_clock(slot);
    std::int32_t time = clock.time - view.swing_start_keyframe(slot) * kOne;

    const std::int32_t first_keyframe = hit_window_start(view, attack);
    std::int32_t window_keyframes = hit_window_length(view, attack);
    std::int32_t checked = state.checked_time;
    const std::int32_t window_end = (first_keyframe + window_keyframes) * kOne;
    const std::int32_t window_start = first_keyframe * kOne;

    // The animation went back (a new swing of the same clip): start over.
    if (time < checked) {
        phase_now = phase::kRequested;
        state.phase = phase_now;
    }
    if (phase_now == phase::kRequested) {
        state.shots_fired = 0;
        phase_now = phase::kFirstTick;
    }
    if (checked < window_start) {
        checked = window_start;
        state.checked_time = checked;
    } else if (window_end < checked) {
        checked = window_end;
        state.checked_time = checked;
    }
    if (time < window_start) {
        return 0;
    }

    if (view.delta_time() != 0) {
        if (time >= window_end) {
            time = window_end;
            done = true;
        }
        const std::int32_t yaw = state.aim_yaw;
        const std::int32_t pitch = state.aim_pitch;

        if (row.hit_type == WeaponAttack::kHitRay) {
            std::int32_t shots_left = state.shots_left;
            std::uint32_t shots_fired = state.shots_fired;
            const std::int32_t interval = row.shot_interval_q12();
            const std::int32_t reach = row.reach_q12;
            std::uint32_t max_shots = 1;
            if (interval != 0 && row.max_shots() != 0) {
                max_shots = row.max_shots();
            }
            const std::int32_t sector = sector_angle(row.spread_sectors());
            const std::int32_t stride = ((row.spread_stride() - 1) & 0xF) + 1;
            const int pellets = row.pellets();
            const std::int32_t step = stride * sector;
            const Combatant self = view.combatant(slot);

            std::int32_t deviation = row.spread_q12();
            if (interval == 0 && pellets == 1) {
                const ShotPlan plan = plan_single_shot(view, slot, self, row);
                deviation = plan.deviation;
                distance_band = plan.distance_band;
            }

            std::int32_t around = mul_q12(step, phase_now - phase::kFirstTick);
            for (checked += interval; shots_left != 0 && shots_fired < max_shots && time >= checked;
                 checked += interval, ++shots_fired, --shots_left) {
                for (int pellet = pellets; pellet > 0; --pellet) {
                    const std::int32_t off = mul_q12_wide(deviation, view.random_q12());
                    const std::int32_t angle = around + mul_q12_wide(sector, view.random_q12() - 2048);
                    const std::int32_t ray_yaw = yaw + mul_q12(off, fixed_sin(angle));
                    const std::int32_t ray_pitch = pitch + mul_q12(off, fixed_cos(angle));
                    const Direction d = direction_of(ray_yaw, ray_pitch);
                    state.segment[0] = Vec3{mul_q12(reach, d.x), mul_q12(reach, d.y), mul_q12(reach, d.z)};
                    around += step;
                    if (self.traits.is_player) {
                        view.rumble(5);
                    }
                    const bool fat = view.bullet_fat_hitbox() && slot == kPlayerSlot;
                    const ShotTrace trace = view.trace_shot(state.origin, state.segment[0], slot, fat);
                    if (trace.hit && !trace.character && slot == kPlayerSlot) {
                        view.bullet_impact(trace.point, state.segment[0], state.origin);
                    }
                    if (trace.hit && trace.character) {
                        state.segment[1] = Vec3{d.x, d.y, d.z};
                        if (trace.target_slot >= 0) {
                            strike(view, slot, state, trace.target_slot, trace.point, 0);
                        }
                    }
                }
            }
            if (checked >= window_end) {
                done = true;
            } else {
                checked -= interval;
            }
            if (shots_left == 0 || shots_fired >= max_shots) {
                checked = time;
                done = true;
            }
            state.checked_time = checked;
            state.shots_fired = static_cast<std::uint8_t>(shots_fired);
            state.shots_left = static_cast<std::uint8_t>(shots_left);
        } else {
            if (window_keyframes <= 0) {
                window_keyframes = 1;
            }
            bool stop_on_hit;
            std::int32_t scale;
            if (row.hit_type == WeaponAttack::kHitLightSweep) {
                stop_on_hit = false;
                // The part of the window this tick sweeps; with melee_once_per_swing (GAMEPLAY CHANGE) a target is
                // struck once and takes the whole window's share.
                if (view.melee_once_per_swing()) {
                    scale = window_start < window_end ? (window_end - window_start) / (window_keyframes * 4) : 0;
                } else {
                    scale = checked < time ? (time - checked) / (window_keyframes * 4) : 0;
                }
            } else {
                stop_on_hit = true;
                scale = kOne;
            }
            if (phase_now == phase::kFirstTick) {
                begin_sweep(view, state);
                phase_now = phase::kSweeping;
            }

            // Four points between last tick's swing and this one, from the old towards the new.
            const Direction was = direction_of(state.prev_aim_yaw, state.prev_aim_pitch);
            const Direction now = direction_of(yaw, pitch);
            const Vec3 old_origin = state.prev_origin;
            const Vec3 origin = state.origin;
            std::array<std::int16_t, kSweepSteps> step_yaw{};
            std::array<std::int16_t, kSweepSteps> step_pitch{};
            std::array<Vec3, kSweepSteps> step_origin{};
            for (int k = kSweepSteps; k > 0; --k) {
                const int rest = kSweepSteps - k;
                const std::int32_t dx = quarter(wrap_add(wrap_mul(now.x, k), wrap_mul(was.x, rest)));
                const std::int32_t dy = quarter(wrap_add(wrap_mul(now.y, k), wrap_mul(was.y, rest)));
                const std::int32_t dz = quarter(wrap_add(wrap_mul(now.z, k), wrap_mul(was.z, rest)));
                step_yaw[k - 1] = static_cast<std::int16_t>(fixed_atan2(dx, dz));
                step_pitch[k - 1] = static_cast<std::int16_t>(
                    fixed_atan2(sh::world::game_sqrt(wrap_add(wrap_mul(dx, dx), wrap_mul(dz, dz))), dy));
                step_origin[k - 1] =
                    Vec3{quarter(wrap_add(wrap_mul(origin.x, k), wrap_mul(old_origin.x, rest))),
                         quarter(wrap_add(wrap_mul(origin.y, k), wrap_mul(old_origin.y, rest))),
                         quarter(wrap_add(wrap_mul(origin.z, k), wrap_mul(old_origin.z, rest)))};
            }
            state.prev_aim_yaw = state.aim_yaw;
            state.prev_aim_pitch = state.aim_pitch;

            bool stopped = false;
            for (int i = 0; i < kSweepSteps && !stopped; ++i) {
                state.origin = step_origin[i];
                advance_segment(view, state, step_yaw[i], step_pitch[i]);
                if (sweep_step(view, slot, state, step_yaw[i], row, scale) != 0) {
                    stopped = stop_on_hit;
                    done = done || stopped;
                }
            }
            checked = time;
            state.origin = origin;
            state.prev_origin = origin;
        }

        if (done) {
            state.phase = phase::kFinished;
            if (view.combatant(slot).traits.is_player) {
                const AttackStat stat = finished_stat(view.attack_row(attack), distance_band, view.foggy());
                if (stat != AttackStat::None) {
                    view.count_attack(stat, state.hit_mask != 0, view.aim_time());
                }
            }
        } else {
            state.phase = static_cast<std::int16_t>(phase_now);
            state.checked_time = checked;
        }
    }
    return state.hit_mask;
}

}  // namespace

int hit_window_start(const CombatView& view, int attack) {
    const WeaponAttack row = view.attack_row(attack);
    if (row.gas && motor_running(view)) {
        return 0;
    }
    return row.keyframe_start;
}

int hit_window_length(const CombatView& view, int attack) {
    const WeaponAttack row = view.attack_row(attack);
    if (row.motor_hit_keyframes != 0 && motor_running(view)) {
        return row.motor_hit_keyframes;
    }
    return row.keyframe_length;
}

bool in_hit_window(const sh::items::WeaponAttack& row, std::int32_t keyframe) {
    return keyframe >= row.keyframe_start && keyframe < row.keyframe_start + row.keyframe_length;
}

int request_attack(CombatView& view, int request, int attack, int slot, const Vec3& origin, int victim,
                   std::int16_t yaw, std::int16_t pitch) {
    AttackState state = view.attack_state(slot);
    std::int32_t was = state.phase;

    if (view.delta_time() == 0 || !view.in_gameplay()) {
        return -1;
    }
    // No hit lands while the room fades in after a load: the world already runs with the player at his arrival
    // point and no control, and an enemy by the entrance would strike him (and could shove him out of bounds).
    if (view.room_fading_in()) {
        return -1;
    }
    // The no-target cheat: every enemy attack passes here with the player as its victim.
    if (view.no_target_cheat() && victim == kPlayerSlot) {
        return -1;
    }
    if (slot == kPlayerSlot) {
        view.bind_player_clips();
    }
    if (request <= 0) {
        was = 0;
        state.phase = phase::kIdle;
    }
    state.attack = attack;

    // A new swing (another status, or the clock went back) re-arms the hit mask: an enemy that registers its attack
    // only inside its attack state would otherwise keep the mask of its first hit for good.
    const SwingClock clock = view.swing_clock(slot);
    const auto status = static_cast<std::uint8_t>(clock.status);
    const auto keyframe = static_cast<std::uint8_t>(clock.time >> 12);
    if (state.swing_status != status || state.swing_keyframe > keyframe) {
        state.hit_mask = 0;
    }
    state.swing_status = status;
    state.swing_keyframe = keyframe;

    if ((clock.status & 1) == 0) {
        state.phase = phase::kIdle;
        state.hit_mask = 0;
    } else if (was == 0 && request > 0) {
        state.phase = phase::kRequested;
        state.hit_mask = 0;
    }
    state.shots_left = 100;
    state.aim_yaw = yaw;
    state.aim_pitch = pitch;
    state.origin = origin;
    const std::int32_t hits = run_attack(view, slot, state);
    view.set_attack_state(slot, state);

    int result = -1;
    int count;
    std::int32_t bit;
    if (slot == kPlayerSlot) {
        count = kNpcSlots, bit = 1;
    } else {
        count = 1, bit = -1;
    }
    if (hits != 0) {
        for (; count > 0; --count, bit = wrap_mul(bit, 2)) {
            if ((hits & bit) != 0) {
                result = count;
                break;
            }
        }
    }
    if (!view.combatant(slot).traits.is_player) {
        result = view.attack_received(kPlayerSlot) != -1 ? 0 : -1;
    }
    return result;
}

std::int32_t update_attack(CombatView& view, int slot) {
    AttackState state = view.attack_state(slot);
    const std::int32_t hits = run_attack(view, slot, state);
    view.set_attack_state(slot, state);
    return hits;
}

void settle_attack(CombatView& view, int slot) {
    if (!view.attack_ticked(slot)) {
        AttackState state = view.attack_state(slot);
        state.phase = phase::kIdle;
        run_attack(view, slot, state);
        view.set_attack_state(slot, state);
    }
}

bool gas_sound_update(CombatView& view, GasSound channel, std::int32_t volume, std::int32_t pitch) {
    if (channel == GasSound::Contact) {
        pitch = 0;
    }
    std::int32_t level = view.gas_sound_level(channel);
    const bool was_silent = level == 0;
    if (view.delta_time() == 0) {
        volume = 0;
    }
    if (volume >= 0) {
        if (volume == 0) {
            level = 0;
        } else if (volume > 255) {
            level = kSoundLevelMax;
        } else {
            level = volume * kOne + kMotorAudible;
        }
    } else {
        level -= static_cast<std::int32_t>(static_cast<std::uint32_t>(view.delta_time()) << 9);
        if (level < 0) {
            level = 0;
        }
    }

    const std::int32_t heard = level > kMotorAudible ? (level - kMotorAudible) >> 12 : 0;
    if (level == 0) {
        view.gas_sound_stop(channel);
    } else if (was_silent) {
        view.gas_sound_start(channel, heard, pitch);
    } else {
        view.gas_sound_keep(channel, heard, pitch);
    }
    view.set_gas_sound_level(channel, level);
    return level > kMotorAudible;
}

void gas_sound_contact(CombatView& view, std::int32_t volume) {
    gas_sound_update(view, GasSound::Contact, volume, 0);
}

void gas_sound_drill_run(CombatView& view, std::int32_t volume, std::int32_t pitch) {
    gas_sound_update(view, GasSound::RockDrillRun, volume, pitch);
}

void gas_sound_motor(CombatView& view, bool chainsaw, std::int32_t volume, std::int32_t pitch) {
    if (gas_sound_update(view, chainsaw ? GasSound::ChainsawRun : GasSound::RockDrillStart, volume, pitch)) {
        view.motor_rumble(chainsaw);
    }
}

void gas_sound_stop_all(CombatView& view) {
    gas_sound_contact(view, 0);
    gas_sound_motor(view, false, 0, 0);
    gas_sound_motor(view, true, 0, 0);
    gas_sound_drill_run(view, 0, 0);
}

}  // namespace sh::combat
