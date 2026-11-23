// SPDX-License-Identifier: GPL-3.0-or-later
// Lens flare and reflecting water, written from the original's behaviour.

#include "engine/effects/water.h"

#include <algorithm>

#include "engine/camera/camera_motion.h"

namespace sh::effects {
namespace {

std::int32_t mul12(std::int32_t a, std::int32_t b) { return static_cast<std::int32_t>(static_cast<std::int64_t>(a) * b) >> 12; }

std::uint8_t byte(std::int32_t v) { return static_cast<std::uint8_t>(v); }
std::uint8_t byte(std::uint32_t v) { return static_cast<std::uint8_t>(v); }

Rgb grey(std::int32_t v) { return Rgb{byte(v), byte(v), byte(v)}; }

TextureRef texture(std::uint16_t page, std::uint16_t clut) {
    return TextureRef{page, static_cast<std::uint16_t>((clut & 0x3F) * 16), static_cast<std::uint16_t>(clut >> 6)};
}

// A screen-aligned square of the flare, centred (cx, cy), half-side h.
EffectPrim square(std::int32_t cx, std::int32_t cy, std::int32_t h, Rgb rgb, TextureRef tex, std::int32_t slot,
                  std::uint8_t u0, std::uint8_t v0, std::uint8_t u1, std::uint8_t v1) {
    EffectPrim p;
    p.kind = EffectPrim::Kind::Quad;
    p.semi_transparent = true;
    p.rgb[0] = rgb;
    p.texture = tex;
    p.slot = slot;
    const std::int32_t xs[4] = {cx - h, cx + h, cx - h, cx + h};
    const std::int32_t ys[4] = {cy - h, cy - h, cy + h, cy + h};
    const std::uint8_t us[4] = {u0, u1, u0, u1};
    const std::uint8_t vs[4] = {v0, v0, v1, v1};
    for (int i = 0; i < 4; i++) {
        p.x[i] = static_cast<std::int16_t>(xs[i]);
        p.y[i] = static_cast<std::int16_t>(ys[i]);
        p.u[i] = us[i];
        p.v[i] = vs[i];
    }
    return p;
}

}  // namespace

void WaterEffects::reset() {
    flare_suppressed_ = false;
    reflection_suppressed_ = false;
    flare_seen_ = false;
    level_ = 0;
}

std::int32_t WaterEffects::flare_target(std::int32_t light_strength, std::int32_t x1, std::int32_t x2) const {
    namespace cam = sh::camera;
    const auto at = [](const WaterCurve& c, std::int32_t x) {
        return cam::piecewise_linear(c.values.data(), static_cast<std::int32_t>(c.values.size()), x, c.min_x, c.max_x);
    };
    const std::int32_t strength = at(params_.flare_intensity, light_strength);
    const std::int32_t distance = at(params_.flare_distance, x1);
    const std::int32_t result = (at(params_.flare_angle, x2) * mul12(strength, distance)) >> 12;
    return std::min(result, params_.flare_max);
}

std::int32_t WaterEffects::facing_factor(const FacingProbe& probe) {
    if (probe.delta_z <= 0 || probe.step_len == 0) {
        return 0;
    }
    if (probe.delta_z >= probe.step_len) {
        return 4096;
    }
    return (probe.delta_z << 12) / probe.step_len;
}

const WaterZone* WaterEffects::find_zone(const WaterZone* zones, std::size_t count, std::int32_t x, std::int32_t z) {
    if (zones == nullptr) {
        return nullptr;
    }
    for (std::size_t i = 0; i < count && zones[i].enabled; i++) {
        const WaterZone& zone = zones[i];
        if (x >= zone.min_x && x < zone.max_x && z >= zone.min_z && z < zone.max_z) {
            return &zone;
        }
    }
    return nullptr;
}

void WaterEffects::frame(WaterHost& host, std::int32_t light_strength, const Vec3s& rot, const Vec3& pos,
                         const WaterZone* zones, std::size_t zone_count) {
    if (!flare_suppressed_) {
        view_ = host.flare_view(pos, rot);
        // Past the 16-bit view depth nothing of the lamp's holder is drawn (a free camera flown far away), and the
        // glare, which keeps its size on the screen at any distance, would hang there alone.
        constexpr std::int32_t kFarthestDepth = 0xFFFF;
        if ((host.projection_distance() >> 1) < view_.screen.z && view_.screen.z <= kFarthestDepth) {
            flare_seen_ = true;
            target_ = static_cast<std::int16_t>(flare_target(light_strength, view_.screen.z, view_.altitude));
        } else {
            return;
        }
    }

    if (reflection_suppressed_) {
        return;
    }
    const WaterZone* zone = find_zone(zones, zone_count, pos.x >> 8, pos.z >> 8);
    if (zone == nullptr) {
        return;
    }
    scroll(host);
    if (host.surface_shaded()) {
        host.surface_glow(pos, glint_strength(host, view_.altitude));
        return;
    }
    const std::int32_t surface_y = static_cast<std::int32_t>(zone->illumination) << 8;
    glint(host, pos, view_.altitude, surface_y);
    sheen(host, rot, pos, surface_y);
}

void WaterEffects::scroll(WaterHost& host) {
    // The counter counts 30 Hz ticks; fractions carry over at higher frame rates, and the
    // waves repeat after scroll_period ticks.
    const std::int32_t dt = host.delta_time();
    if (dt > 0) {
        scroll_time_ = (scroll_time_ + dt) % (params_.scroll_period * params_.scroll_tick);
        scroll_count_ = scroll_time_ / params_.scroll_tick;
    }
    host.surface_clock(scroll_time_, params_.scroll_period * params_.scroll_tick);
}

// Nothing while the lamp points away from the eye within 40 degrees, full from 130 degrees on.
std::int32_t WaterEffects::glint_strength(WaterHost& host, std::int32_t angle) {
    std::int32_t a = angle - 455;  // 40 degrees
    a = std::min(a, 1024);
    return std::max(host.sin(a), 0);
}

void WaterEffects::glint(WaterHost& host, const Vec3& pos, std::int32_t angle, std::int32_t surface_y) {
    // The lamp mirrored in the surface.
    const Vec3 mirrored{pos.x >> 4, ((surface_y * 2) >> 4) - (pos.y >> 4), pos.z >> 4};
    const ScreenPoint at = host.project_mirror(mirrored);
    if (at.depth * 4 < params_.hidden_depth) {
        return;
    }

    const std::int32_t s = glint_strength(host, angle);

    EffectPrim p;
    p.kind = EffectPrim::Kind::Quad;
    p.semi_transparent = true;
    p.rgb[0] = Rgb{byte(s >> 6), byte((s * 7) >> 9), byte((s * 5) >> 9)};
    p.texture = texture(45, 78);
    p.slot = params_.glint_slot;
    const std::int32_t cx = at.x;
    const std::int32_t cy = at.y;
    const std::int32_t xs[4] = {cx - 24, cx + 24, cx - 24, cx + 24};
    const std::int32_t ys[4] = {cy - 48, cy - 48, cy + 48, cy + 48};
    const std::uint8_t us[4] = {64, 64, 127, 127};
    const std::uint8_t vs[4] = {31, 0, 31, 0};
    for (int i = 0; i < 4; i++) {
        p.x[i] = static_cast<std::int16_t>(xs[i]);
        p.y[i] = static_cast<std::int16_t>(ys[i]);
        p.u[i] = us[i];
        p.v[i] = vs[i];
    }
    host.emit(p);
}

void WaterEffects::sheen(WaterHost& host, const Vec3s& rot, const Vec3& pos, std::int32_t surface_y) {
    SheenDraw d;
    const std::int32_t yaw = host.atan2(rot.x, rot.z);
    d.cos_yaw = host.cos(yaw);
    d.sin_yaw = host.sin(yaw);
    d.place = Vec3{pos.x >> 4, surface_y >> 4, pos.z >> 4};
    d.hub = Vec3s{0, 0, static_cast<std::int16_t>(params_.octagon_centre_z)};
    d.ring_slot = params_.ring_slot;
    d.band_slot = params_.band_slot;
    d.colors = &params_.octagon_colors;

    std::int16_t angle = 0;
    for (int i = 0; i < 8; i++, angle = static_cast<std::int16_t>(angle + 512)) {
        const std::int16_t s0 = static_cast<std::int16_t>(host.sin(angle));
        const std::int16_t c0 = static_cast<std::int16_t>(host.cos(angle));
        const std::int32_t next = angle + 512;
        const std::int16_t s1 = static_cast<std::int16_t>(host.sin(next));
        const std::int16_t c1 = static_cast<std::int16_t>(host.cos(next));

        const auto inner = [&](std::int16_t s, std::int16_t c, std::int16_t y) {
            return Vec3s{static_cast<std::int16_t>((s * params_.inner_radius) >> 10), y,
                         static_cast<std::int16_t>(((c * params_.inner_radius) >> 10) + params_.inner_z)};
        };
        const auto outer = [&](std::int16_t s, std::int16_t c, std::int16_t y) {
            return Vec3s{static_cast<std::int16_t>((s * params_.outer_radius) >> 12), y,
                         static_cast<std::int16_t>(((c * params_.outer_radius) >> 12) + params_.outer_z)};
        };
        const std::int16_t top = static_cast<std::int16_t>(params_.outer_top);
        d.corners[i] = {inner(s0, c0, 0), inner(s1, c1, 0), outer(s0, c0, top), outer(s1, c1, top)};
    }
    host.sheen(d);
}

void WaterEffects::end_frame(WaterHost& host) {
    if (!flare_seen_) {
        return;
    }
    flare_seen_ = false;
    if (flare_suppressed_) {
        return;
    }

    const std::int32_t facing = facing_factor(host.facing_probe());
    const std::int32_t delta = ((target_ * facing) >> 12) - level_;
    bool front = facing != 0;
    level_ = static_cast<std::int16_t>(level_ + (delta >> 1));
    if (host.camera_chasing()) {
        level_ = 0;
        front = false;
    }
    draw_flare(host, front, level_);
}

void WaterEffects::draw_flare(WaterHost& host, bool front, std::int32_t level) const {
    const Vec3& at = view_.screen;
    std::int32_t width = 0;
    std::int32_t height = 0;
    host.screen_size(width, height);
    // The flare is dropped when its centre is far outside the picture.
    if (at.x < ((-width >> 1) - 512) || ((width >> 1) + 512) < at.x || at.y < ((-height >> 1) - 512) ||
        ((height >> 1) + 512) < at.y) {
        return;
    }

    const std::int32_t alt = view_.altitude;
    const std::int32_t az = view_.azimuth;
    // The glare's offset from the centre, along the lamp's azimuth.
    const std::int32_t off_x = mul12(host.sin(alt), host.cos(az));
    const std::int32_t off_y = mul12(host.sin(alt), host.sin(az));

    const std::int32_t depth_slot = at.z >> 3;
    // The halo and the core sit at the lamp's depth when it faces away, in front otherwise.
    const std::int32_t body_slot = front ? 1 : (alt > 0x400 ? depth_slot : depth_slot + 7);

    const std::int32_t size = (level + 12288) >> 2;

    host.emit(square(at.x + mul12(off_x, 5), at.y + mul12(off_y, 5), mul12(size, 30), grey(mul12(size, 40)),
                     texture(0x2C, 0x18C), body_slot, 0, 0, 63, 63));
    host.emit(square(at.x, at.y, 11, grey(0x10), texture(44, 0x18C), body_slot, 0, 0, 63, 63));
    host.emit(square(at.x + ((off_x * 9) >> 11), at.y + ((off_y * 9) >> 11), mul12(size, 48),
                     grey(mul12(std::min(level * 2, 4096), 48)), texture(44, 0x18C), 1, 0, 0, 63, 63));

    if (level == 0) {
        return;
    }

    // The star: sixteen rays fanned around the lamp.
    const std::int32_t base = ((-alt >> 1) + ((at.x + at.y) * 4)) + 0x2AA;
    const std::int32_t seed = (at.z * 4) + ((at.x + at.y) * 16);
    for (int i = 0; i < 16; i++) {
        const std::int16_t a = static_cast<std::int16_t>(params_.ray_angles[i] + base);
        std::int16_t reach = static_cast<std::int16_t>(
            mul12(host.cos(a - az) + 6963, (host.cos((a * 12) + seed) + 4096) >> 1));
        reach = std::min<std::int16_t>(reach, 6144);
        const std::int32_t s = static_cast<std::int16_t>(host.sin(a));
        const std::int32_t c = static_cast<std::int16_t>(host.cos(a));

        EffectPrim p;
        p.kind = EffectPrim::Kind::Quad;
        p.semi_transparent = true;
        p.rgb[0] = grey((level * reach) >> 18);
        p.texture = texture(0x2C, 0x14C);
        p.slot = 1;
        const std::uint8_t us[4] = {0x20, 0x3F, 0x20, 0x3F};
        const std::uint8_t vs[4] = {0x40, 0x40, 0x5F, 0x5F};
        for (int k = 0; k < 4; k++) {
            const std::int32_t px = params_.ray_shape[k][0];
            const std::int32_t py = params_.ray_shape[k][1];
            p.x[k] = static_cast<std::int16_t>(at.x + ((px * c - py * s) >> 12));
            p.y[k] = static_cast<std::int16_t>(at.y + ((px * s + py * c) >> 12));
            p.u[k] = us[k];
            p.v[k] = vs[k];
        }
        host.emit(p);
    }

    // Two coloured ghosts mirrored through the picture centre and the lamp's hot spot.
    const std::int32_t ghost = (level + 0x3000) >> 2;
    const std::uint32_t lv = static_cast<std::uint32_t>(level);
    const std::int32_t third = mul12(level, 0x18);
    host.emit(square(mul12(at.x, 0x333), mul12(at.y, 0x333), mul12(ghost, 0x10),
                     Rgb{byte(mul12(level, 32)), byte(mul12(level, 0x30)), byte(mul12(level, 32))},
                     texture(44, 0x1CC), 1, 0x40, 0, 0x7F, 0x3F));
    host.emit(square(mul12(at.x, -0x4CC), mul12(at.y, -0x4CC), mul12(ghost, 0x1C),
                     Rgb{byte((lv * 7) >> 9), byte((lv * 0x13) >> 0xB), byte(third)}, texture(44, 0x1CC), 1, 0x40, 0,
                     0x7F, 0x3F));
    host.emit(square(at.x, at.y, 10, Rgb{byte(lv >> 6), byte(third), byte((lv * 0x2F) >> 11)}, texture(76, 0x18C), 1,
                     0, 0, 63, 63));
}

}  // namespace sh::effects
