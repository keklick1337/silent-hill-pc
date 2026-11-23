// SPDX-License-Identifier: GPL-3.0-or-later
#include "engine/world/screen_glow.h"

#include <algorithm>

#include "engine/world/fixed_trig.h"

namespace sh::world {

namespace {

constexpr std::int32_t kQ12One = 4096;

std::int16_t to16(std::int64_t v) { return static_cast<std::int16_t>(v); }

// Integer product with truncating division, as the game rounds these (not the arithmetic shift).
std::int32_t mul_div(std::int32_t a, std::int32_t b);

// 32-bit wrap-around, as the original's int arithmetic does on overflow.
std::int32_t w32(std::int64_t v) { return static_cast<std::int32_t>(static_cast<std::uint32_t>(v)); }

std::int32_t mul_div(std::int32_t a, std::int32_t b) { return w32(static_cast<std::int64_t>(a) * b) / kQ12One; }

}  // namespace

std::int32_t flashlight_intensity_step(std::int32_t intensity, bool on, std::int32_t delta_time) {
    // 4.0 in Q12 is 16384; the product is taken in 64 bits and shifted back.
    const std::int32_t step = static_cast<std::int32_t>((static_cast<std::int64_t>(delta_time) * 16384) >> 12);
    intensity = on ? intensity + step : intensity - step;
    return std::clamp(intensity, 0, kQ12One);
}

std::int32_t world_brightness(std::int32_t brightness_intensity, std::int32_t user_brightness) {
    const std::int32_t scaled = (brightness_intensity * (user_brightness * 8 + 4)) >> 12;
    return std::clamp(scaled, 0, 256);
}

std::int32_t brightness_level_grey(std::int32_t user_brightness, std::int32_t bias) {
    if (user_brightness < bias) return 0;
    return (user_brightness - bias) * 8 + 4;
}

std::int32_t world_brightness(std::int32_t brightness_intensity, std::int32_t user_brightness, std::int32_t bias) {
    const std::int32_t scaled = (brightness_intensity * brightness_level_grey(user_brightness, bias)) >> 12;
    return std::clamp(scaled, 0, 256);
}

LensFlare lens_flare_get(std::int32_t alpha, std::int32_t zone_flag, std::uint32_t mode,
                         const std::array<std::int16_t, 3>& light_rot) {
    LensFlare out;
    if (zone_flag != 2) {
        mode = 1;
    }
    out.alpha = (mode == 0) ? 0 : alpha;
    out.follows_bone = mode == 1 || mode > 5;

    // Fixed sky directions as (pitch, yaw) in Q12 angles (-20 deg, 195/-75/15/105 deg); mode 0 looks along -90 deg.
    std::int32_t pitch = 0;
    std::int32_t yaw = 0;
    switch (mode) {
        case 0: yaw = -1024; break;
        case 2: pitch = -227; yaw = 2218; break;
        case 3: pitch = -227; yaw = -853; break;
        case 4: pitch = -227; yaw = 170; break;
        case 5: pitch = -227; yaw = 1194; break;
        default: pitch = light_rot[0]; yaw = light_rot[1]; break;
    }
    const std::int32_t cos_pitch = fixed_cos(pitch);
    out.dir[1] = static_cast<std::int16_t>(-fixed_sin(pitch));
    out.dir[2] = static_cast<std::int16_t>((cos_pitch * fixed_cos(yaw)) >> 12);
    out.dir[0] = static_cast<std::int16_t>((cos_pitch * fixed_sin(yaw)) >> 12);
    return out;
}

std::array<ScreenPoint, 17> screen_ring_points(std::int32_t half_w, std::int32_t h) {
    std::array<ScreenPoint, 17> p{};
    const std::int32_t step = half_w >> 1;
    const std::int32_t h2 = h >> 1;
    for (int i = 0; i < 17; ++i) {
        if (i < 2) {
            p[i] = {to16(half_w), to16((h / 4) * i)};
        } else if (i < 6) {
            p[i] = {to16(half_w - step * (i - 2)), to16(h2)};
        } else if (i < 10) {
            p[i] = {to16(-half_w), to16(h2 - (h2 >> 1) * (i - 6))};
        } else if (i < 14) {
            p[i] = {to16(-half_w + step * (i - 10)), to16(-h2)};
        } else {
            p[i] = {to16(half_w), to16(-h2 + (h >> 2) * (i - 14))};
        }
    }
    return p;
}

RingColors ring_colors(std::uint8_t fan_r, std::uint8_t fan_g, std::uint8_t fan_b, std::uint8_t edge_r,
                       std::uint8_t edge_g, std::uint8_t edge_b) {
    const auto pack = [](std::int32_t r, std::int32_t g, std::int32_t b) {
        return static_cast<std::uint32_t>((r & 0xFF) | (g & 0xFF) << 8 | (b & 0xFF) << 16);
    };
    RingColors c;
    c.fan = pack(fan_r, fan_g, fan_b);
    c.steps[0] = 0;
    c.steps[1] = pack(edge_r / 3, edge_g / 3, edge_b / 3);
    c.steps[2] = pack(edge_r * 2 / 3, edge_g * 2 / 3, edge_b * 2 / 3);
    c.steps[3] = pack(edge_r, edge_g, edge_b);
    return c;
}

Vec3 screen_point_project(const Vec3& from, const Vec3& dir, std::int32_t screen_dist, std::int32_t offset_x,
                          std::int32_t offset_y) {
    const std::int32_t half = screen_dist / 2;
    Vec3 v;
    if (half < from.z) {
        v = from;
    } else {
        const std::int32_t z = dir.z != 0 ? dir.z : 1;
        v.z = half;
        v.x = ((half - from.z) * dir.x) / z + from.x;
        v.y = ((half - from.z) * dir.y) / z + from.y;
    }
    if (v.z == 0) {
        v.z = 1;  // a divide by zero does not trap on the original hardware
    }
    return {(v.x * screen_dist) / v.z + offset_x, (v.y * screen_dist) / v.z + offset_y, v.z};
}

bool glow_rings_build(const Vec3& pos, std::int32_t size, std::int32_t angle0, std::int32_t angle1, float x_scale,
                      GlowRings& out) {
    if (pos.z == 0) {
        return false;
    }
    const std::int32_t depth_scale = pos.z < kQ12One / 4 ? kQ12One : (kQ12One * 1024) / pos.z;

    std::int32_t s = w32(static_cast<std::int64_t>(mul_div(size, 500)) * 1024) / pos.z;
    s = w32(static_cast<std::int64_t>(s) * (3 * kQ12One - ((depth_scale * fixed_cos(angle1)) >> 12))) / 16384;
    if (s == 0) {
        return false;
    }

    const std::int32_t radius[4] = {s / 5, w32(static_cast<std::int64_t>(s) * 4) / 10, w32(static_cast<std::int64_t>(s) * 7) / 10, s};
    const std::int32_t sin1 = fixed_sin(angle1);
    const std::int32_t cos0 = fixed_cos(angle0);
    const std::int32_t sin0 = fixed_sin(angle0);

    ScreenPoint centre[4];
    for (int i = 0; i < 4; ++i) {
        const std::int32_t shrink = w32(static_cast<std::int64_t>(s - (radius[i] >> 1)) * kQ12One) / s;
        centre[i].x = to16(pos.x + mul_div(mul_div(mul_div(radius[i], shrink), cos0), sin1));
        centre[i].y = to16(pos.y + mul_div(mul_div(mul_div(radius[i], shrink), sin0), sin1));
    }

    constexpr std::int32_t kLimit = kQ12One / 4;
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 17; ++j) {
            const std::int32_t c = fixed_cos(j << 8);
            const std::int32_t sn = fixed_sin(j << 8);
            ScreenPoint& p = out.ring[i][j];
            std::int16_t x = to16(mul_div(radius[i], c) + centre[i].x);
            x = to16(pos.x + static_cast<std::int32_t>(static_cast<float>(x - pos.x) * x_scale));
            p.x = std::clamp<std::int16_t>(x, -kLimit, kLimit - 1);
            std::int16_t y = to16(mul_div(radius[i], sn) + centre[i].y);
            p.y = std::clamp<std::int16_t>(y, -kLimit, kLimit - 1);
        }
    }
    return true;
}

}  // namespace sh::world
