// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)

#include "engine/player/player_math.h"

#include "engine/world/fixed_math.h"
#include "engine/world/fixed_trig.h"

#include <algorithm>
#include <cstdlib>

namespace sh::game::player_math {

namespace {

constexpr std::int32_t kNoFloor = 32768;       // 8 m: the collision found no floor
constexpr std::int32_t kPhantomRise = 8192;    // 2 m above the feet
constexpr std::int32_t kMaxDropPer60 = 8192;   // 2 m per 60 fps tick
constexpr std::int32_t kMaxClimbPer60 = 6144;  // 1.5 m per 60 fps tick
constexpr std::uint32_t kDistanceCap = 0x80000000u;

std::int32_t abs32(std::int32_t v) { return v < 0 ? -v : v; }

}  // namespace

std::int32_t slope_speed(std::int32_t speed, std::int32_t heading, std::int32_t tilt_x, std::int32_t tilt_z) {
    const std::int32_t along_x = mul(speed, sh::world::fixed_sin(heading));
    const std::int32_t along_z = mul(speed, sh::world::fixed_cos(heading));
    const std::int32_t cos_x = sh::world::fixed_cos(abs32(tilt_x) >> 3);
    const auto cos_z = static_cast<std::int16_t>(sh::world::fixed_cos(abs32(tilt_z) >> 3));
    // Both slope-scaled axes are 16-bit, as the game keeps them.
    const auto x = static_cast<std::int16_t>(mul(mul(along_x, cos_x), cos_x));
    const auto z = static_cast<std::int16_t>(mul(mul(along_z, cos_z), cos_z));
    const std::int32_t length = sh::world::game_sqrt(x * x + z * z);
    return speed >= 0 ? length : -length;
}

Offset step_offset(std::int32_t step, std::int32_t heading) {
    const bool long_step = static_cast<std::uint32_t>(step + 32767) > 65534u;
    const int down = long_step ? 2 : 0;
    const int up = long_step ? 4 : 0;
    Offset o;
    o.x = static_cast<std::int32_t>(static_cast<std::uint32_t>(mul_wide(step >> down, sh::world::fixed_sin(heading) >> down))
                                    << up);
    o.z = static_cast<std::int32_t>(static_cast<std::uint32_t>(mul_wide(step >> down, sh::world::fixed_cos(heading) >> down))
                                    << up);
    return o;
}

GroundClamp clamp_ground(std::int32_t found, std::int32_t previous, std::int32_t y, std::int32_t dt) {
    GroundClamp g{found, false};
    if (found == kNoFloor) {
        g.height = previous;
    } else if (found < y - kPhantomRise) {
        g.height = previous;
        g.phantom = true;
    } else {
        const std::int32_t max_drop = per_tick_60(dt, kMaxDropPer60);
        const std::int32_t max_climb = per_tick_60(dt, kMaxClimbPer60);
        const std::int32_t delta = found - previous;
        if (delta > max_drop) {
            g.height = previous + max_drop;
        } else if (delta < -max_climb) {
            g.height = previous - max_climb;
        }
    }
    return g;
}

std::uint32_t add_distance(std::uint32_t total, std::int32_t step) {
    const std::uint32_t sum = total + static_cast<std::uint32_t>(step);
    return sum < 1u ? 1u : (sum > kDistanceCap ? kDistanceCap : sum);
}

std::int32_t stick_deflection(std::int32_t x, std::int32_t y, bool radius) {
    if (!radius) {
        return abs32(y);
    }
    return std::min(sh::world::game_sqrt(x * x + y * y), 127);
}

std::int32_t legs_clip_duration(VariableClip clip, const StickState& stick, std::int32_t keyframe,
                                std::int32_t health, std::int32_t exhaustion) {
    constexpr std::int32_t kCentre = 64;  // half deflection: the walk/run threshold of the stick
    switch (clip) {
        case VariableClip::WalkForward:
            if (stick.y < -63) {
                return (abs32(kCentre - stick_deflection(stick.x, stick.y, stick.radius)) * 2662 / 64) * 16 + 49152;
            }
            if (stick.held != 0) {
                return (abs32(stick.held - kCentre) * 2662 / 64) * 16 + 49152;
            }
            return 90112;
        case VariableClip::RunForward:
            if (stick.y < -63) {
                // The stride frames of either foot slow down with the stick; the rest of the cycle runs flat.
                if ((keyframe >= 40 && keyframe < 46) || (keyframe >= 30 && keyframe < 36)) {
                    return abs32(kCentre - stick_deflection(stick.x, stick.y, stick.radius)) * 1024 + 65536;
                }
                return 131072;
            }
            if (stick.held != 0) {
                return abs32(stick.held - kCentre) * 1024 + 65536;
            }
            return 122880;
        case VariableClip::WalkBackward:
            if (stick.y >= 64) {
                return ((abs32(stick_deflection(stick.x, stick.y, stick.radius) - kCentre) * 1638 / 64) * 4096 / 200) + 62914;
            }
            if (stick.held != 0) {
                return ((abs32(stick.held - kCentre) * 1638 / 64) * 4096 / 200) + 62914;
            }
            return 94208;
        case VariableClip::IdleExhausted:
            if (health < 122880) {
                return 163840 - health;
            }
            return (exhaustion >> 12) * 4096;
        case VariableClip::Other: break;
    }
    return 0;
}

Lean aim_lean(const AimLeanInput& in, Lean lean) {
    constexpr std::int32_t kPitchRange = 640;  // 56.25 degrees
    constexpr std::int32_t kYawRange = 384;    // 33.75 degrees
    std::int32_t rate;
    std::int32_t settle;
    switch (in.weapon_attack) {
        case 32: rate = 20; settle = 2; break;  // handgun
        case 33: rate = 18; settle = 6; break;  // hunting rifle
        case 34: rate = 26; settle = 3; break;  // shotgun
        case 35: rate = 30; settle = 0; break;  // hyper blaster
        default: rate = 12; settle = 6; break;
    }

    lean.x = static_cast<std::int16_t>(in.aim_pitch - 1024);
    const std::int32_t rot = in.rotation_y;
    const std::int32_t to = in.angle_to_target;
    if (abs32(to - rot) > 2048) {
        lean.y = to > rot ? static_cast<std::int16_t>(-wrap_angle(rot + 4096 - to))
                          : static_cast<std::int16_t>(wrap_angle(to + 4096 - rot));
    } else {
        lean.y = static_cast<std::int16_t>(to - rot);
    }

    const std::int32_t hold = in.aim_hold;
    if (hold != 0 || in.lock_starting) {
        const std::int32_t min_hold = in.settled ? 0 : settle;
        const std::int32_t target = in.flex_target_x;
        if (target != 0) {
            const std::int32_t gap = target - lean.x;
            if (lean.x < target) {
                const std::int32_t max = hold * per_tick_30(in.dt, rate + hold);
                if (max < gap) {
                    lean.x = static_cast<std::int16_t>(target - max);
                }
            } else {
                const std::int32_t max = -(hold * per_tick_30(in.dt, rate + hold));
                if (gap < max) {
                    lean.x = static_cast<std::int16_t>(target - max);
                }
            }
        } else if (hold < min_hold) {
            lean.y = 0;
            lean.x = 0;
        } else {
            const std::int32_t steps = hold + 1 - min_hold;
            const std::int32_t max = steps * per_tick_30(in.dt, rate + (steps * 2 + 4));
            if (lean.x > 0) {
                if (max < lean.x) {
                    lean.x = static_cast<std::int16_t>(max);
                }
            } else if (lean.x < 0 && lean.x < -max) {
                lean.x = static_cast<std::int16_t>(-max);
            }
            if (lean.y > 0) {
                if (max < lean.y) {
                    lean.y = static_cast<std::int16_t>(max);
                }
            } else if (lean.y < 0 && lean.y < -max) {
                lean.y = static_cast<std::int16_t>(-max);
            }
        }
    }

    lean.x = static_cast<std::int16_t>(std::clamp<std::int32_t>(lean.x, -kPitchRange, kPitchRange));
    lean.y = static_cast<std::int16_t>(std::clamp<std::int32_t>(lean.y, -kYawRange, kYawRange));
    return lean;
}

Lean relax_lean(Lean lean, std::int32_t dt) {
    constexpr std::int32_t kFirstStep = 32;  // 2.9 degrees
    if (lean.x > 0) {
        lean.x = static_cast<std::int16_t>(lean.x - kFirstStep);
    } else if (lean.x < 0) {
        lean.x = static_cast<std::int16_t>(lean.x + kFirstStep);
    }
    const std::int32_t step = per_tick_30(dt, 24);  // 2.15 degrees per 30 fps tick
    auto toward_zero = [step](std::int16_t v) -> std::int16_t {
        if (v > 0) {
            v = static_cast<std::int16_t>(v - step);
            return v < 0 ? std::int16_t{0} : v;
        }
        if (v < 0) {
            v = static_cast<std::int16_t>(v + step);
            return v > 0 ? std::int16_t{0} : v;
        }
        return v;
    };
    lean.x = toward_zero(lean.x);
    lean.y = toward_zero(lean.y);
    return lean;
}

}  // namespace sh::game::player_math
