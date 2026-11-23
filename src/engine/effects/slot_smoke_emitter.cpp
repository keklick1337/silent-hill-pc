// SPDX-License-Identifier: GPL-3.0-or-later
// The smoke column and the particle emitter of the slot effects (slot_effects.h).

#include <algorithm>
#include <cstdlib>

#include "engine/effects/slot_effects.h"
#include "engine/effects/effect_view.h"

namespace sh::effects {

namespace {

constexpr std::int32_t kOrderingTableSize = 2048;
// Code byte of the effects' quads: a semi-transparent textured quad.
constexpr std::uint32_t kTranslucentQuad = 0x2Eu << 24;

std::uint32_t grey_word(std::int32_t c) {
    return static_cast<std::uint32_t>(c + (c << 8) + (c << 16)) + kTranslucentQuad;
}

// The square around a projected point: half-size `s` each way, or one pixel when it is 0.
void square_corners(const ScreenPoint& p, std::int32_t s, std::uint32_t xy[4]) {
    if (s != 0) {
        xy[0] = xy_word(p.x - s, p.y - s);
        xy[1] = xy_word(p.x + s, p.y - s);
        xy[2] = xy_word(p.x - s, p.y + s);
        xy[3] = xy_word(p.x + s, p.y + s);
    } else {
        xy[0] = xy_word(p.x, p.y);
        xy[1] = xy_word(p.x + 1, p.y);
        xy[2] = xy_word(p.x, p.y + 1);
        xy[3] = xy_word(p.x + 1, p.y + 1);
    }
}

bool off_screen(const ScreenPoint& p) {
    return p.depth <= 0 || (p.depth >> 3) >= kOrderingTableSize || std::abs(p.x) > 200 || std::abs(p.y) > 160;
}

// Outside the wider frame's box too: nothing of the sprite could show (effect_view.h).
bool off_wide_screen(const ScreenPoint& p) {
    return p.depth <= 0 || (p.depth >> 3) >= kOrderingTableSize || std::abs(p.x) > screen_box_half_width() ||
           std::abs(p.y) > 160;
}

Vec3s relative(const SlotView& view, std::int32_t x, std::int32_t y, std::int32_t z) {
    return Vec3s{static_cast<std::int16_t>((x >> 4) - view.origin.x), static_cast<std::int16_t>((y >> 4) - view.origin.y),
                 static_cast<std::int16_t>((z >> 4) - view.origin.z)};
}

std::int32_t rand_below(SlotEffectHost& host, std::uint32_t n) {
    return static_cast<std::int32_t>(static_cast<std::uint32_t>(host.rand16()) % n);
}

}  // namespace

// ---- smoke column ----------------------------------------------------------------------------------------------

void SmokeColumn::start(SlotEffectHost& host) {
    SmokeColumnParams p = host.smoke_params();
    // An unset block (no puffs or no rise) has nothing to spawn.
    if (p.puffs == 0 || p.rise == 0) return;

    std::int32_t left = p.puffs;
    const int count = host.slot_count();
    for (int i = 0; i < count; i++) {
        EffectSlot s = host.load(i);
        if (s.kind != 0) continue;
        s.kind = slot_kind::kSmokeWaiting;
        // Staggered start: the n-th puff waits n / puffs of the time a puff takes to rise.
        s.a = static_cast<std::uint32_t>(((((p.top - p.base) * 4096) / p.rise) * left) / p.puffs);
        host.store(i, s);
        if (--left == 0) break;
    }
    p.fade = 0;
    host.store_smoke_params(p);
    host.set_effect_flag(kSmokeRunning);
}

void SmokeColumn::respawn(SlotEffectHost& host, const SmokeColumnParams& p, EffectSlot& s) {
    if (p.keep == 0) s.kind = slot_kind::kSmokePuff;
    if (p.radius == 0) {
        s.x = 0;
        s.z = 0;
    } else if (p.shape == 0) {
        const std::int16_t r = static_cast<std::int16_t>(host.rand16() % p.radius);
        const std::int32_t angle = rand_below(host, 4096);
        s.x = (r * host.cos(angle)) >> 12;
        s.z = (r * host.sin(angle)) >> 12;
    } else {
        s.x = host.rand16() % (p.radius * 2) - p.radius;
        s.z = host.rand16() % (p.radius * 2) - p.radius;
    }
    s.y = p.base;
    s.flags = static_cast<std::uint8_t>(host.rand16() % 3);
    s.a = 0;
}

bool SmokeColumn::wait(SlotEffectHost& host, int slot) {
    const SmokeColumnParams p = host.smoke_params();
    EffectSlot s = host.load(slot);
    s.a = static_cast<std::uint32_t>(static_cast<std::int32_t>(s.a) +
                                     mul12_wide(host.delta_time(), host.rand16() % 1638 + 3276));
    if (p.rise == 0) {
        host.store(slot, s);
        return false;
    }
    const bool due = (((p.top - p.base) << 12) / p.rise) < static_cast<std::int32_t>(s.a);
    if (due) respawn(host, p, s);
    host.store(slot, s);
    return due;
}

bool SmokeColumn::draw(SlotEffectHost& host, int slot) {
    const SmokeColumnParams p = host.smoke_params();
    EffectSlot s = host.load(slot);
    std::int32_t phase = static_cast<std::int32_t>(s.a);

    const std::uint32_t row = (static_cast<std::uint32_t>(s.flags) << 5) + 160u;
    const std::uint32_t uv[4] = {(row << 8) + 0x011300E0u, (row << 8) + 0x2B00FFu,
                                 (((row + 31u) << 8) + 0xE0u) & 0xFFFFu, (((row + 31u) << 8) + 0xFFu) & 0xFFFFu};

    const std::int32_t thin = 0x40 - (phase >> 6);
    const std::int32_t grey = thin >= 0 ? mul12_wide(thin, (0x400 - std::min(p.fade * 2, 0x400)) * 4) : 0;
    const std::uint32_t rgbc = grey_word(grey);

    const std::int32_t cx = s.x >> 6;
    const std::int32_t cz = s.z >> 6;
    s.y = static_cast<std::int16_t>(s.y + mul12_wide(host.delta_time(), p.rise));

    // The puff drifts outwards, faster the further out it already is.
    std::int32_t speed = mul12_wide(host.sqrt(cx * cx + cz * cz) << 10, p.radius);
    // A puff at the centre has no speed and no kick (the console's remainder gave garbage there).
    const std::int32_t kick = speed != 0 ? host.rand16() % speed - (speed >> 2) : 0;
    speed = mul12_wide(host.delta_time(), kick) >> 1;
    const std::int32_t heading = host.atan2(s.x, s.z);
    s.x += mul12(speed, host.cos(heading));
    s.z += mul12(speed, host.sin(heading));
    phase += std::max(speed, 0);

    const SlotView& view = host.view();
    const Vec3s rel = relative(view, s.x + p.offset_x, s.y, s.z + p.offset_z);

    // The fading column narrows; the term is skipped where the divisor passes zero.
    const std::int32_t denom = mul12(host.cos(p.fade), p.top) - p.base;
    if (denom != 0) phase += std::max((mul12_wide(host.delta_time(), p.rise) * 4096) / denom, 0);
    s.a = static_cast<std::uint32_t>(phase);

    if (phase > 4096 || p.fade == 1024) {
        if (p.fade_delay == 0 && host.cos(p.fade) < rand_below(host, 4096)) {
            s.kind = 0;
        } else {
            respawn(host, p, s);
        }
        host.store(slot, s);
        return false;
    }
    host.store(slot, s);

    ScreenPoint pt = host.project(rel);
    const bool shown = !off_screen(pt);
    if (!shown && off_wide_screen(pt)) return false;

    std::int32_t size = p.size * view.screen_distance / pt.depth;
    if (phase < 0x200) size = mul12_wide(size, phase * 4 + 0x800);
    std::uint32_t xy[4];
    square_corners(pt, size, xy);

    const std::int32_t depth = std::max(pt.depth - p.depth_bias, 0);
    host.emit(textured_quad(rgbc, xy, uv), SlotLayer::World, depth >> 3);
    return shown;
}

// ---- particle emitter ------------------------------------------------------------------------------------------

void ParticleEmitter::start(SlotEffectHost& host) {
    EmitterParams p = host.emitter_params();
    if (p.flags & 1) {
        p.half_z = static_cast<std::int16_t>(p.radius * 2);
        p.half_x = p.half_z;
    } else {
        p.radius = static_cast<std::uint16_t>((p.half_x + p.half_z) >> 1);
    }
    p.height = static_cast<std::int16_t>(p.top - p.bottom);
    p.level = p.mode != 0 ? 4096 : 0;
    host.store_emitter_params(p);

    std::int32_t left = p.count;
    for (int i = host.slot_count() - 1; i >= 0; i--) {
        EffectSlot s = host.load(i);
        if (s.kind != 0) continue;
        if (p.mode == 0) {
            respawn(host, p, s, false);
        } else {
            s.kind = slot_kind::kEmitterIdle;
        }
        host.store(i, s);
        if (--left == 0) break;
    }
    host.set_effect_flag(kEmitterRunning);
}

void ParticleEmitter::respawn(SlotEffectHost& host, int slot, bool again) {
    const EmitterParams p = host.emitter_params();
    EffectSlot s = host.load(slot);
    respawn(host, p, s, again);
    host.store(slot, s);
}

void ParticleEmitter::respawn(SlotEffectHost& host, const EmitterParams& p, EffectSlot& s, bool again) {
    // Dying out: fewer and fewer particles come back.
    if (p.mode == 1 && rand_below(host, 4096) < p.level && rand_below(host, 8) == 0) {
        s.kind = slot_kind::kEmitterIdle;
        return;
    }
    if ((p.flags & 16) && rand_below(host, 4) == 0) {
        s.kind = slot_kind::kEmitterIdle;
        return;
    }

    const std::int16_t radius = static_cast<std::int16_t>(p.radius);
    const std::int32_t dist = host.rand16() % radius;
    const std::int32_t angle = rand_below(host, 4096);
    s.kind = ((p.flags & 4) && rand_below(host, 64) == 0) ? slot_kind::kSpark : slot_kind::kFlame;

    set_hi16(s.a, again ? rand_below(host, 16) : rand_below(host, 4096));
    const std::int32_t start = static_cast<std::uint16_t>(hi16(s.a));
    const std::int32_t phase = ((p.spark_spread << 4) * dist) / radius + start + p.level;
    if (p.flags & 16) {
        // The original's clamp names the random term twice: one draw for the test, another for the value.
        const bool below = phase + rand_below(host, 1024) < 4095;
        set_lo16(s.a, below ? phase + rand_below(host, 1024) : 4095);
    } else {
        set_lo16(s.a, std::min(phase, 4095));
    }

    set_byte(s.b, 0, static_cast<std::uint32_t>(host.rand16()) | 0x80u);
    set_byte(s.b, 1, static_cast<std::uint32_t>(host.rand16()) | 0x80u);
    set_hi16(s.b, rand_below(host, 4096));

    if (p.flags & 1) {
        s.x = (dist * host.cos(angle)) >> 12;
        s.z = (dist * host.sin(angle)) >> 12;
    } else {
        s.x = host.rand16() % (p.half_x * 2) - p.half_x;
        s.z = host.rand16() % (p.half_z * 2) - p.half_z;
    }
    s.y = static_cast<std::int16_t>(p.bottom + ((start * p.height) >> 12));
    // The original added the two draws in this order (low bit first).
    const std::int32_t low = rand_below(host, 2);
    s.flags = static_cast<std::uint8_t>(low + (host.rand16() % 3) * 2);
}

bool ParticleEmitter::draw_flame(SlotEffectHost& host, int slot) {
    const EmitterParams p = host.emitter_params();
    const SlotView& view = host.view();
    EffectSlot s = host.load(slot);

    std::uint32_t uv[4];
    std::uint32_t rgbc;
    if (s.kind == slot_kind::kFlame) {
        // Four flame cells of 64 x 64 by the particle's variant.
        const std::uint32_t right = s.flags >= 2 ? 1u : 0u;
        const bool lower = s.flags >= 3;
        uv[0] = (right << 6) + (lower ? 0x020EC000u : 0x020E8000u);
        uv[1] = (right ? 0x7Fu : 0x3Fu) + (lower ? 0x6DC000u : 0x6D8000u);
        uv[2] = ((right << 6) | (lower ? 0xFF00u : 0xBF00u)) & 0xFFFFu;
        uv[3] = ((right ? 0x7Fu : 0x3Fu) | (lower ? 0xFF00u : 0xBF00u)) & 0xFFFFu;
        // Hot yellow cooling to dim red with the phase.
        const std::int32_t age = static_cast<std::uint16_t>(lo16(s.a));
        if (age < 0xE00) {
            const std::int32_t rg = 0xB4 + ((0xC4 - (age >> 5)) << 8) - (age >> 7);
            rgbc = static_cast<std::uint32_t>(rg + (std::max(0, 0xC4 - (age >> 4)) << 16)) + kTranslucentQuad;
        } else {
            const std::int32_t r = std::max(0, 0x418 - (age >> 2));
            rgbc = static_cast<std::uint32_t>(r + (std::max(0, 0x3D4 - (age >> 2)) << 8)) + kTranslucentQuad;
        }
    } else {
        uv[0] = 0x024E7800u;
        uv[1] = 0x4D7807u;
        uv[2] = 0x7F00u;
        uv[3] = 0x7F07u;
        rgbc = 0x78u + (0x80u << 8) + (0x80u << 16) + kTranslucentQuad;
    }

    s.y = static_cast<std::int16_t>(s.y + view.rise);
    std::int32_t orbit = (p.wobble * byte_of(s.b, 1)) >> 8;
    std::int32_t px = s.x + mul12(host.sin(hi16(s.b)), orbit);
    std::int32_t pz = s.z + mul12(host.cos(hi16(s.b)), orbit);

    const std::int32_t start = static_cast<std::uint16_t>(hi16(s.a));
    // Near the start the orbit opens up (an unset block's zero divisor skips it).
    if (start < static_cast<std::uint16_t>(view.fade_begin) && p.fade_phase != 0) {
        const std::int32_t angle = host.angle_remap(1024 - ((start << 6) / p.fade_phase));
        orbit = mul12(static_cast<std::uint16_t>(view.fade_amp), host.sin(angle)) +
                static_cast<std::uint16_t>(view.fade_base);
        px = mul12(px, orbit);
        pz = mul12(pz, orbit);
    }
    px += view.offset_x;
    pz += view.offset_z;
    const Vec3s rel = relative(view, px, s.y, pz);

    const std::int32_t swirl = mul12_wide(host.delta_time(), (static_cast<std::uint16_t>(p.swirl) * byte_of(s.b, 0)) >> 8);
    set_hi16(s.b, (s.flags & 1) ? hi16(s.b) + swirl : hi16(s.b) - swirl);
    set_lo16(s.a, lo16(s.a) + view.age_step);
    set_hi16(s.a, hi16(s.a) + view.age_step);

    if (static_cast<std::uint16_t>(lo16(s.a)) >= 4096) {
        if ((p.flags & 2) && s.kind == slot_kind::kFlame) {
            // The flame turns into smoke and is drawn this frame still.
            set_lo16(s.a, static_cast<std::uint16_t>(p.level) + rand_below(host, 4096));
            s.kind = ((p.flags & 8) && rand_below(host, 16) == 0) ? slot_kind::kEmber : slot_kind::kFireSmoke;
        } else {
            if ((p.flags & 8) && rand_below(host, 16) == 0) {
                set_lo16(s.a, rand_below(host, 2048) + 2048 + (p.level >> 1));
                s.kind = slot_kind::kEmber;
            } else {
                respawn(host, p, s, true);
            }
            host.store(slot, s);
            return false;
        }
    }
    host.store(slot, s);
    if (s.kind == slot_kind::kFlame) {
        const float lit = static_cast<float>((rgbc & 0xFFu) + ((rgbc >> 8) & 0xFFu) + ((rgbc >> 16) & 0xFFu)) / 765.0f;
        host.flame_shown({static_cast<float>(px >> 4), static_cast<float>(s.y >> 4), static_cast<float>(pz >> 4)}, lit);
    }

    const ScreenPoint pt = host.project(rel);
    const bool shown = !off_screen(pt);
    if (!shown && off_wide_screen(pt)) return false;

    std::int32_t size = s.kind != slot_kind::kFlame ? (view.screen_distance * 4) / pt.depth : view.spark_size / pt.depth;
    size = std::max(size, 2);
    const std::int32_t up = (size + 1) >> 1;
    const std::int32_t down = -(size >> 1);
    const std::uint32_t at = xy_word(pt.x, pt.y);
    // Added to the packed point: an x past the 16 bits carries into y, as on the console.
    const std::uint32_t xy[4] = {at + static_cast<std::uint32_t>(up) + (static_cast<std::uint32_t>(up) << 16),
                                 at + static_cast<std::uint32_t>(down) + (static_cast<std::uint32_t>(up) << 16),
                                 at + static_cast<std::uint32_t>(up) + (static_cast<std::uint32_t>(down) << 16),
                                 at + static_cast<std::uint32_t>(down) + (static_cast<std::uint32_t>(down) << 16)};
    host.emit(textured_quad(rgbc, xy, uv), SlotLayer::World, pt.depth >> 3);
    return shown;
}

bool ParticleEmitter::draw_smoke(SlotEffectHost& host, int slot) {
    const EmitterParams p = host.emitter_params();
    const SlotView& view = host.view();
    EffectSlot s = host.load(slot);
    const std::int32_t age = static_cast<std::uint16_t>(lo16(s.a));

    std::uint32_t uv[4];
    std::uint32_t rgbc;
    std::int32_t size_unit;
    if (s.kind == slot_kind::kFireSmoke) {
        uv[0] = 0x024EC000u;
        uv[1] = 0x4DC03Fu;
        uv[2] = 0xFF00u;
        uv[3] = 0xFF3Fu;
        size_unit = static_cast<std::int16_t>((age >> 6) + 0x24);
        rgbc = grey_word(std::max(0x40 - (age >> 6), 0));
    } else {
        uv[0] = 0x020E7800u;
        uv[1] = 0xD7807u;
        uv[2] = 0x7F00u;
        uv[3] = 0x7F07u;
        size_unit = 2;
        rgbc = static_cast<std::uint32_t>(0xFF + (std::max(0xC0 - (age >> 6), 0) << 8) + (0x80 << 16)) +
               kTranslucentQuad;
    }

    s.y = static_cast<std::int16_t>(s.y + view.rise);
    const std::int32_t orbit = (p.wobble * byte_of(s.b, 1)) >> 8;
    const std::int32_t px = p.ember_x + s.x + mul12(host.sin(hi16(s.b)), orbit);
    const std::int32_t pz = p.ember_z + s.z + mul12(host.cos(hi16(s.b)), orbit);
    const Vec3s rel = relative(view, px, s.y, pz);

    const std::int32_t swirl = mul12_wide(host.delta_time(), (view.swirl_rate * byte_of(s.b, 0)) >> 8);
    set_hi16(s.b, (s.flags & 1) ? hi16(s.b) + swirl : hi16(s.b) - swirl);
    set_lo16(s.a, lo16(s.a) + view.age_step);

    if (static_cast<std::uint16_t>(lo16(s.a)) > 4096) {
        respawn(host, p, s, true);
        host.store(slot, s);
        return false;
    }
    host.store(slot, s);

    const ScreenPoint pt = host.project(rel);
    const bool shown = !off_screen(pt);
    if (!shown && off_wide_screen(pt)) return false;

    std::uint32_t xy[4];
    square_corners(pt, (size_unit * view.screen_distance) / pt.depth, xy);
    host.emit(textured_quad(rgbc, xy, uv), SlotLayer::World, pt.depth >> 3);
    return shown;
}

}  // namespace sh::effects
