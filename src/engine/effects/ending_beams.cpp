// SPDX-License-Identifier: GPL-3.0-or-later
// The final fight's beams of light and thunder pacing (ending_beams.h).

#include "engine/effects/ending_beams.h"

#include <algorithm>

#include "engine/camera/camera_motion.h"

namespace sh::effects {

namespace {

constexpr int kRows = 6;
constexpr int kColumns = 16;
constexpr std::int32_t kOne = 4096;

std::int32_t mul12(std::int32_t a, std::int32_t b) {
    return static_cast<std::int32_t>(static_cast<std::uint32_t>(a) * static_cast<std::uint32_t>(b)) >> 12;
}
std::int32_t mul12_wide(std::int32_t a, std::int32_t b) {
    return static_cast<std::int32_t>((static_cast<std::int64_t>(a) * b) >> 12);
}

// Moves `current` toward `target` by rate * dt, landing on it when within one step.
std::int32_t damp(std::int32_t rate, std::int32_t current, std::int32_t target, std::int32_t dt) {
    const std::int32_t delta = current - target;
    const std::int32_t step = mul12_wide(rate, dt);
    if (step < delta) return current - step;
    return delta >= -step ? target : current + step;
}

// One end of a fan: its progress from the start point to the target eases toward 1.
void ease_end(Vec3& point, std::int32_t& lift, std::int16_t& progress, std::int16_t& speed, const Vec3& target,
              std::int32_t target_lift, const Vec3& start, std::int32_t start_lift, std::int16_t max_speed,
              std::int32_t dt) {
    speed = static_cast<std::int16_t>(camera::speed_toward(speed, progress, kOne, 2 * kOne, max_speed, 10 * kOne, dt));
    progress = static_cast<std::int16_t>(progress + mul12_wide(speed, dt));
    lift = start_lift + mul12(progress, target_lift - start_lift);
    point.x = start.x + mul12(progress, target.x - start.x);
    point.y = start.y + mul12(progress, target.y - start.y);
    point.z = start.z + mul12(progress, target.z - start.z);
}

}  // namespace

void EndingBeams::mode_change(BeamsState& s, std::int32_t mode) {
    if (mode == s.mode) return;
    s.mode = mode;
    s.step = 0;
    s.timer = 0;
    s.strikes = 0;
    s.mode_timer = 0;
}

void EndingBeams::init(EndingBeamsHost& host) {
    BeamsState s;
    host.load(s);
    s.mode = -1;
    s.timer = 0;
    s.color[0] = 250;
    s.color[1] = 200;
    s.color[2] = 150;
    mode_change(s, 0);
    host.store(s);
}

void EndingBeams::set_mode(EndingBeamsHost& host, std::int32_t mode) {
    BeamsState s;
    host.load(s);
    mode_change(s, mode);
    host.store(s);
}

void EndingBeams::set_pattern(EndingBeamsHost& host, BeamsState& s, int index) {
    s.motion[0] = {};
    s.motion[1] = {};
    s.target = index;
    const BeamPattern p = host.pattern(index);
    if (p.snap) {
        s.current[0] = p.fans[0];
        s.current[1] = p.fans[1];
        s.start[0] = p.fans[0];
        s.start[1] = p.fans[1];
    } else {
        s.start[0] = s.current[0];
        s.start[1] = s.current[1];
    }
}

void EndingBeams::update(EndingBeamsHost& host) {
    BeamsState s;
    host.load(s);
    if (s.mode == -1) return;

    // The step machine of each mode: which pattern, and when the next one.
    const auto once = [&](int pattern) {
        if (s.step == 0) {
            s.step = 1;
            set_pattern(host, s, pattern);
        }
    };
    switch (s.mode) {
        case 0: set_pattern(host, s, 0); break;
        case 1: once(1); break;
        case 2: once(2); break;
        case 3:
            if (s.step == 0) {
                set_pattern(host, s, 3);
                s.step++;
            } else if (s.step == 1 && s.timer > 2457) {  // 0.6 s
                s.step = 2;
                set_pattern(host, s, 4);
                s.timer = 0;
            }
            break;
        case 5: once(5); break;
        case 6: once(6); break;
        case 7:
            host.track_boss_bone();
            once(7);
            break;
        case 8:
            host.track_boss_bone();
            if (s.step == 0 && s.timer > 2048) {  // 0.5 s
                s.step = 1;
                set_pattern(host, s, 8);
            }
            break;
        case 9: once(9); break;
        case 11: once(10); break;
        case 12:
            if (s.step == 0) {
                s.step = 1;
                set_pattern(host, s, 11);
            } else if (s.step == 1 && s.timer > 14336) {  // 3.5 s
                s.step = 2;
                set_pattern(host, s, 12);
                s.timer = 0;
            }
            break;
        case 13:
            // Pulses between patterns 14 and 15; the wait of step 2 runs straight into step 3's check, as the
            // original fell through.
            switch (s.step) {
                case 0:
                    set_pattern(host, s, 13);
                    s.step++;
                    break;
                case 1:
                    if (s.timer > 8192) {
                        set_pattern(host, s, 14);
                        s.timer = 0;
                        s.step++;
                    }
                    break;
                case 2:
                    if (s.timer > 12288) {
                        s.timer = 0;
                        s.step++;
                    }
                    [[fallthrough]];
                case 3:
                    if (s.timer > 409) {
                        set_pattern(host, s, 15);
                        s.timer = 0;
                        s.step++;
                    }
                    break;
                case 4:
                    if (s.timer > 1024) {
                        set_pattern(host, s, 14);
                        s.timer = 0;
                        s.step--;
                    }
                    break;
                default: break;
            }
            break;
        case 14:
            if (s.step == 0 && s.timer > 16384) {
                set_pattern(host, s, 16);
                s.step++;
            }
            break;
        case 15:
            if (s.step == 0) {
                set_pattern(host, s, 17);
                s.step++;
            }
            break;
        default: break;
    }

    // The lightning of the ending modes.
    switch (s.mode) {
        case 4: {
            if (s.strikes == 0) {
                s.strikes = 1;
                host.bolts_attach_ending();
            }
            // After 11 s the strikes grow more frequent over 6 s.
            const std::int32_t ramp = s.mode_timer < 45056 ? 0 : std::min(s.mode_timer - 45056, 24576) / 6;
            strike_around(host, s, 7372, static_cast<std::int16_t>(mul12(ramp, 1228) + 245));
            host.bolts_update();
            break;
        }
        case 5:
            if (s.mode_timer < 8192) {
                Vec3 at = host.npc_position(4);
                at.y -= 4505;
                s.strikes++;
                if (s.strikes % 2) strike_at(host, s, 16384, 1638, true, at);
                strike_at(host, s, 14336, 4096, false, at);
                strike_at(host, s, 14336, 2048, false, at);
                host.bolts_update();
            }
            break;
        case 10:
            if (s.strikes == 0) {
                host.bolts_attach_ending();
                s.strikes++;
            }
            if (s.mode_timer < 8192) {
                Vec3 at = host.strike_target();
                at.y -= 15605;
                s.strikes++;
                if (s.strikes % 2) {
                    strike_at(host, s, 14336, 2048, true, at);
                    strike_at(host, s, 14336, 2048, true, at);
                }
                strike_at(host, s, 12288, 4096, false, at);
                strike_at(host, s, 12288, 2048, false, at);
                host.bolts_update();
            }
            break;
        default: break;
    }

    if (s.target >= 0) {
        const BeamPattern p = host.pattern(s.target);
        ease(host, s.current[0], s.motion[0], p.fans[0], s.start[0]);
        ease(host, s.current[1], s.motion[1], p.fans[1], s.start[1]);
    }
    draw(host, s, s.current[0]);
    draw(host, s, s.current[1]);

    s.timer += host.delta_time();
    s.mode_timer += host.delta_time();
    host.store(s);
}

void EndingBeams::ease(EndingBeamsHost& host, BeamFan& fan, BeamMotion& motion, const BeamFan& target,
                       const BeamFan& start) {
    const std::int32_t dt = host.delta_time();
    fan.perspective = target.perspective;
    fan.intensity = static_cast<std::int16_t>(damp(target.fade_rate, fan.intensity, target.intensity, dt));

    // The colour cap walks to its target in fifteen steps of the pattern change.
    const std::int32_t step = std::abs((target.cap - start.cap) / 15) + 1;
    if (fan.cap > target.cap + step) {
        fan.cap = static_cast<std::int16_t>(fan.cap - step);
    } else if (fan.cap < target.cap - step) {
        fan.cap = static_cast<std::int16_t>(fan.cap + step);
    } else {
        fan.cap = target.cap;
    }

    motion.width_speed = static_cast<std::int16_t>(
        camera::speed_toward(motion.width_speed, fan.width, target.width, 2400, target.width_speed_max, 8000, dt));
    fan.width = static_cast<std::int16_t>(fan.width + mul12_wide(motion.width_speed, dt));

    ease_end(fan.near, fan.near_lift, motion.near_progress, motion.near_speed, target.near, target.near_lift,
             start.near, start.near_lift, target.near_speed_max, dt);
    ease_end(fan.far, fan.far_lift, motion.far_progress, motion.far_speed, target.far, target.far_lift, start.far,
             start.far_lift, target.far_speed_max, dt);
}

void EndingBeams::draw(EndingBeamsHost& host, const BeamsState& s, const BeamFan& fan) {
    if (fan.width == 0 || fan.intensity == 0 || fan.cap == 0) return;

    host.begin_projection();
    const ScreenPoint near =
        host.project(Vec3{fan.near.x >> 4, (fan.near.y + fan.near_lift) >> 4, fan.near.z >> 4});
    const std::int32_t depth = near.depth * 4;
    const ScreenPoint far = host.project(Vec3{fan.far.x >> 4, (fan.far.y + fan.far_lift) >> 4, fan.far.z >> 4});

    // The far end's radius, shrunk with the depth for a perspective fan.
    const std::int32_t scale = fan.perspective != 0 ? 4194304 / std::max(depth, 384) : 4096;
    const std::int32_t radius = mul12_wide(fan.width >> 4, scale);
    const std::uint8_t cap = static_cast<std::uint8_t>(fan.cap);

    struct Point {
        std::int16_t x, y;
        draw3d::Rgb color;
    };
    Point grid[kRows][kColumns];
    for (int i = 0; i < kRows; i++) {
        const std::int32_t t = (i * kOne) / (kRows - 1);
        const std::int32_t level = camera::piecewise_linear(host.curve().data(), 9, t, 0, kOne);
        draw3d::Rgb c;
        const auto channel = [&](std::uint8_t base) {
            const std::int32_t v = mul12(fan.intensity, mul12(base, level));
            return static_cast<std::uint8_t>(v < cap ? v : cap);
        };
        c.r = channel(s.color[0]);
        c.g = channel(s.color[1]);
        c.b = channel(s.color[2]);
        const std::int32_t x = near.x + mul12(t, far.x - near.x);
        const std::int32_t y = near.y + mul12(t, far.y - near.y);
        const std::int32_t r = mul12(t, radius);
        for (int j = 0; j < kColumns; j++) {
            const std::int32_t angle = ((j * kOne) / (kColumns - 1) * kOne) >> 12;
            grid[i][j] = {static_cast<std::int16_t>(x + mul12(r, host.cos(angle))),
                          static_cast<std::int16_t>(y + mul12(r, host.sin(angle))), c};
        }
    }

    for (int row = 0; row < kRows - 1; row++) {
        for (int col = 0; col < kColumns - 1; col++) {
            const Point* corners[4] = {&grid[row][col], &grid[row][col + 1], &grid[row + 1][col],
                                       &grid[row + 1][col + 1]};
            draw3d::Polygon cell;
            cell.count = 4;
            cell.gouraud = true;
            cell.shade = {true, false};
            for (int k = 0; k < 4; k++) {
                cell.v[k].xy = {corners[k]->x, corners[k]->y};
                cell.v[k].color = corners[k]->color;
            }
            host.emit_cell(cell, 1);
        }
    }
    // Additive blending, linked last so it is drawn first.
    host.emit_blend(1, 1);
}

std::int32_t EndingBeams::random(EndingBeamsHost& host, BeamsState& s) {
    if (host.delta_time() != 0) {
        const std::int32_t mixed = static_cast<std::int32_t>(static_cast<std::uint32_t>(s.random_seed) * 0x7169AC35u);
        s.random_out = static_cast<std::int32_t>(static_cast<std::uint32_t>(mixed >> 3) ^ 0xA547B39Eu);
        s.random_seed =
            static_cast<std::int32_t>(((static_cast<std::uint32_t>(s.random_out) * 0x892D719Cu) >> 3) ^ 0xC65A4B97u);
    }
    return s.random_out & 0x7FFF;
}

void EndingBeams::strike_around(EndingBeamsHost& host, BeamsState& s, std::int32_t radius, std::int16_t chance) {
    if (chance <= (random(host, s) & 0xFFF)) return;
    const Vec3 boss = host.npc_position(4);
    const std::int32_t angle = random(host, s) & 0xFFF;
    Vec3 from;
    from.x = boss.x + mul12(host.sin(angle), radius);
    from.z = boss.z + mul12(host.cos(angle), radius);
    from.y = ((random(host, s) % (radius * 2)) - radius) - kOne;
    Vec3 to = boss;
    to.y -= 4505;
    to.x += mul12(host.sin(angle), 1638);
    to.z += mul12(host.cos(angle), 1638);
    host.bolts_strike(from, to, 614, kOne);
}

void EndingBeams::strike_at(EndingBeamsHost& host, BeamsState& s, std::int32_t radius, std::int16_t chance,
                            bool forced, const Vec3& at) {
    if (!forced && chance <= (random(host, s) & 0xFFF)) return;
    const std::int32_t angle = random(host, s) & 0xFFF;
    Vec3 from;
    if (forced) {
        // From the boss's partner.
        const Vec3 partner = host.npc_position(1);
        from = {partner.x - 2048, partner.y - 4915, partner.z};
    } else {
        const Vec3 boss = host.npc_position(4);
        from.x = boss.x + mul12(host.sin(angle), radius);
        from.z = boss.z + mul12(host.cos(angle), radius);
        from.y = (random(host, s) % (radius >> 1)) - (radius >> 2);
    }
    host.bolts_strike(at, from, 2048, 2048);
}

void ThunderPacing::reset() { gaps_ = {}; }

void ThunderPacing::tick(std::int32_t dt) {
    for (std::int32_t& gap : gaps_) {
        if (gap > 0) gap -= dt;
    }
}

}  // namespace sh::effects
