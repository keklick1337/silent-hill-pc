// SPDX-License-Identifier: GPL-3.0-or-later
// The final fight's beams of light and thunder pacing (map7_s03).
//
// EndingBeams: two fans of gouraud quads (6 rows of 16 points, a colour curve along the fan) between a near and a
// far point, easing between the patterns of the map's beam table as the fight's script switches modes; some modes
// also throw lightning strikes around the bosses (the "BossBolts" bolt pool). ThunderPacing: at most a few thunder
// sounds at once (six two-second slots).
//
// Every number, the order of the random
// draws (the map's own generator, which only runs while time runs) and the 16-bit truncations are the original's.
// The state lives where the map keeps it (the host loads and stores it); the pattern table and the colour curve are
// the map's data from the disc; the pattern ends may follow live points (the boss bone).

#pragma once

#include <array>
#include <cstdint>

#include "engine/effects/effect_prims.h"
#include "render/draw3d/draw3d.h"

namespace sh::effects {

// One fan of light: from a near point to a far point (each with a height lift), its width at the far end, its
// intensity and colour cap, and the speeds it eases with.
struct BeamFan {
    Vec3 near;
    std::int32_t near_lift = 0;
    Vec3 far;
    std::int32_t far_lift = 0;
    std::int16_t width = 0;
    std::int16_t intensity = 0;
    std::int16_t cap = 0;
    std::int16_t fade_rate = 0;       // intensity change per second toward the target
    std::int16_t width_speed_max = 0;
    std::int16_t far_speed_max = 0;
    std::int16_t near_speed_max = 0;
    std::int16_t perspective = 0;     // nonzero: the width shrinks with the depth
};

// How far each fan has eased toward its target (Q12 progress 0..1 of the two ends) and the easing speeds.
struct BeamMotion {
    std::int16_t width_speed = 0;
    std::int16_t far_speed = 0;
    std::int16_t far_progress = 0;
    std::int16_t near_speed = 0;
    std::int16_t near_progress = 0;
};

// A pattern of the map's table: both fans' targets; `snap` jumps there at once.
struct BeamPattern {
    bool snap = false;
    BeamFan fans[2];
};

struct BeamsState {
    std::int32_t mode = 0;  // -1: no beams
    std::int32_t step = 0;
    std::int32_t timer = 0;       // Q12 s since the last pattern change of the step machine
    std::int32_t strikes = 0;
    std::int32_t mode_timer = 0;  // Q12 s in the mode
    std::uint8_t color[3] = {0, 0, 0};
    BeamFan current[2];
    std::int32_t target = -1;     // pattern the fans ease to (-1 none yet)
    BeamFan start[2];
    BeamMotion motion[2];
    std::int32_t random_out = 0;  // the map's generator: last output word and seed word
    std::int32_t random_seed = 0;
};

class EndingBeamsHost {
public:
    virtual ~EndingBeamsHost() = default;

    virtual void load(BeamsState& state) = 0;
    virtual void store(const BeamsState& state) = 0;
    // Pattern `index` with its end points as they are now.
    virtual BeamPattern pattern(int index) = 0;
    virtual const std::array<std::int32_t, 9>& curve() = 0;

    virtual std::int32_t delta_time() = 0;
    virtual std::int32_t sin(std::int32_t angle) = 0;
    virtual std::int32_t cos(std::int32_t angle) = 0;

    // NPC slot `slot`'s position (Q12): 4 the boss, 1 its partner.
    virtual Vec3 npc_position(int slot) = 0;
    // The boss bone the patterns of modes 7 and 8 follow: brought up to date from the bone.
    virtual void track_boss_bone() = 0;
    // The two points of the ending strikes (Q12).
    virtual Vec3 strike_target() = 0;

    virtual void bolts_attach_ending() = 0;
    virtual void bolts_update() = 0;
    virtual void bolts_strike(const Vec3& from, const Vec3& to, std::int32_t roll, std::int32_t life) = 0;

    // Sets the world-to-screen projection and projects a point in geometry units: screen point, depth / 4.
    virtual void begin_projection() = 0;
    virtual ScreenPoint project(const Vec3& at) = 0;
    // A cell of a fan (gouraud quad, screen corners as the game computed them) in ordering-table entry `bucket`, and
    // the blend mode linked after the cells (so drawn before them).
    virtual void emit_cell(const draw3d::Polygon& cell, std::int32_t bucket) = 0;
    virtual void emit_blend(std::int32_t bucket, std::int32_t blend) = 0;
};

class EndingBeams {
public:
    static void init(EndingBeamsHost& host);
    static void set_mode(EndingBeamsHost& host, std::int32_t mode);
    static void update(EndingBeamsHost& host);

private:
    static void mode_change(BeamsState& s, std::int32_t mode);
    static void set_pattern(EndingBeamsHost& host, BeamsState& s, int index);
    static void ease(EndingBeamsHost& host, BeamFan& fan, BeamMotion& motion, const BeamFan& target,
                     const BeamFan& start);
    static void draw(EndingBeamsHost& host, const BeamsState& s, const BeamFan& fan);
    static std::int32_t random(EndingBeamsHost& host, BeamsState& s);
    static void strike_around(EndingBeamsHost& host, BeamsState& s, std::int32_t radius, std::int16_t chance);
    static void strike_at(EndingBeamsHost& host, BeamsState& s, std::int32_t radius, std::int16_t chance, bool forced,
                          const Vec3& at);
};

// Thunder sounds: six slots that each block for two seconds after a sound; a sound may play when fewer than two
// "recent" slots are busy (the recency thresholds are drawn anew whenever a busy slot exceeds them).
class ThunderPacing {
public:
    void reset();
    void tick(std::int32_t dt);
    // True when a thunder sound may play now (a free slot is taken). `rand16` is the game's generator.
    template <class Rand>
    bool take(Rand&& rand16) {
        int busy = 0;
        std::int32_t* free = nullptr;
        for (std::int32_t& gap : gaps_) {
            if (gap <= 0) {
                free = &gap;
            } else if (double_after_ < gap) {
                busy += 2;
                double_after_ = rand16() / 21 + 3276;
            } else if (single_after_ < gap) {
                busy++;
                single_after_ = rand16() / 41 + 1638;
            }
        }
        if (busy < 2 && free != nullptr) {
            *free = 8192;
            return true;
        }
        return false;
    }

private:
    std::array<std::int32_t, 6> gaps_{};
    std::int32_t double_after_ = 0;
    std::int32_t single_after_ = 0;
};

}  // namespace sh::effects
