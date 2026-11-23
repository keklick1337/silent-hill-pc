// SPDX-License-Identifier: GPL-3.0-or-later
// Shared helpers of the slot effects and the two small ones: the drips and the light glow (slot_effects.h).

#include "engine/effects/slot_effects.h"
#include "engine/effects/effect_view.h"

#include <algorithm>
#include <cstdlib>

namespace sh::effects {

namespace {

// The ordering-table entries the effects may use (the scene's table has 2048).
constexpr std::int32_t kOrderingTableSize = 2048;

// Drips: texture sheet of four rows of 32 texels, 64 wide.
constexpr std::uint32_t kDripPalette = 0x0E;
constexpr std::uint32_t kDripPage = 0x2D;
// Light glow: a 128 x 64 sparkle, two variants one above the other.
constexpr std::int32_t kGlowPaletteX = 768;
constexpr std::int32_t kGlowPaletteY = 128;
constexpr std::uint32_t kGlowPage = 44;
constexpr std::uint8_t kGlowGrey = 0x40;

std::uint16_t palette_word(std::int32_t x, std::int32_t y) {
    return static_cast<std::uint16_t>((y << 6) | ((x >> 4) & 0x3F));
}

}  // namespace

draw3d::Polygon textured_quad(std::uint32_t rgbc, const std::uint32_t xy[4], const std::uint32_t uv[4]) {
    draw3d::Polygon q;
    q.count = 4;
    q.textured = true;
    const std::uint32_t code = rgbc >> 24;
    q.shade = {(code & 2) != 0, (code & 1) != 0};
    for (int i = 0; i < 4; i++) {
        q.v[i].xy = {static_cast<std::int16_t>(xy[i]), static_cast<std::int16_t>(xy[i] >> 16)};
        q.v[i].uv = {static_cast<std::uint8_t>(uv[i]), static_cast<std::uint8_t>(uv[i] >> 8)};
    }
    q.palette = draw3d::Palette::from_word(static_cast<std::uint16_t>(uv[0] >> 16));
    q.page = draw3d::TexturePage::from_word(static_cast<std::uint16_t>(uv[1] >> 16));
    q.v[0].color = {static_cast<std::uint8_t>(rgbc), static_cast<std::uint8_t>(rgbc >> 8),
                    static_cast<std::uint8_t>(rgbc >> 16)};
    return q;
}

// ---- drips -----------------------------------------------------------------------------------------------------

void Drips::spawn(SlotEffectHost& host, int /*slot*/, EffectSlot& s, int source, bool respawn) {
    const DripSource src = host.drip_source(source);
    switch (src.pattern) {
        case 0:  // the point itself
            s.x = src.x;
            s.z = src.z;
            break;
        case 1: {  // along a line through the point
            // An unset entry (spread 0) gets no offset; the console's divide simply gave garbage there.
            const std::int16_t along =
                src.spread != 0 ? static_cast<std::int16_t>(host.rand16() % src.spread - (src.spread >> 1)) : 0;
            s.x = src.x + ((along * host.sin(src.angle)) >> 12);
            s.z = src.z + ((along * host.cos(src.angle)) >> 12);
            break;
        }
        case 2: {  // inside a disc: up to eight tries in the square around it
            std::int16_t dx = 0;
            std::int16_t dz = 0;
            if (src.spread != 0) {
                const std::int32_t range = src.spread * 2;
                int tries = 0;
                for (; tries < 8; tries++) {
                    dx = static_cast<std::int16_t>(host.rand16() % range - src.spread);
                    dz = static_cast<std::int16_t>(host.rand16() % range - src.spread);
                    if (dx * dx + dz * dz < src.spread * src.spread) break;
                }
                if (tries == 8) dx = dz = 0;
            }
            s.x = src.x + dx;
            s.z = src.z + dz;
            break;
        }
        case 3: {  // on a ring
            const std::int16_t angle = static_cast<std::int16_t>(static_cast<std::uint32_t>(host.rand16()) % 4096u);
            s.x = src.x + ((src.spread * host.sin(angle)) >> 12);
            s.z = src.z + ((src.spread * host.cos(angle)) >> 12);
            break;
        }
        case 4: {  // on the half ring facing `angle`
            const std::int16_t angle = static_cast<std::int16_t>(
                src.angle + static_cast<std::int32_t>(static_cast<std::uint32_t>(host.rand16()) % 2048u) - 1024);
            s.x = src.x + ((src.spread * host.sin(angle)) >> 12);
            s.z = src.z + ((src.spread * host.cos(angle)) >> 12);
            break;
        }
        default:
            break;
    }
    s.y = src.y;

    if (!respawn) {
        set_lo16(s.a, src.period != 0 ? host.rand16() % src.period : 0);
    } else {
        set_lo16(s.a, static_cast<std::int32_t>(s.a) - src.period);
    }
    set_byte(s.a, 2, static_cast<std::uint32_t>(host.rand16()) % 4u);
    set_byte(s.a, 3, static_cast<std::uint32_t>(host.rand16()) % 64u + 96u);
    set_byte(s.b, 0, static_cast<std::uint32_t>(source));
}

void Drips::start(SlotEffectHost& host, int source) {
    const DripSource src = host.drip_source(source);
    for (int i = 0; i < src.count; i++) {
        const int slot = host.alloc(slot_kind::kDrip);
        if (slot < 0) break;
        EffectSlot s = host.load(slot);
        spawn(host, slot, s, source, false);
        host.store(slot, s);
    }
}

bool Drips::draw(SlotEffectHost& host, int slot) {
    EffectSlot s = host.load(slot);
    set_lo16(s.a, lo16(s.a) + host.delta_time());
    const std::uint16_t age = static_cast<std::uint16_t>(lo16(s.a));
    const int source = byte_of(s.b, 0);
    const DripSource src = host.drip_source(source);
    if (age > src.period) {
        spawn(host, slot, s, source, true);
        host.store(slot, s);
        return false;
    }
    host.store(slot, s);

    const SlotView& view = host.view();
    const Vec3s rel{static_cast<std::int16_t>((s.x >> 4) - view.origin.x),
                    static_cast<std::int16_t>((s.y >> 4) - view.origin.y),
                    static_cast<std::int16_t>((s.z >> 4) - view.origin.z)};
    const ScreenPoint p = host.project(rel);
    // A drop on the camera plane (the drain valve cutscene moves the camera as they spawn) is skipped.
    if (p.depth == 0) return true;

    const std::int32_t phase = src.period != 0 ? (static_cast<std::int32_t>(age) << 12) / src.period : 0;
    const std::int32_t half_w =
        ((mul12_wide(src.size * byte_of(s.a, 3), host.sin(phase >> 2)) >> 12) * view.screen_distance) / p.depth;
    const std::int32_t half_h = (half_w * (0x800 - std::abs(static_cast<std::int32_t>(view.pitch)))) >> 10;

    const std::uint32_t xy[4] = {xy_word(p.x - half_w, p.y - half_h), xy_word(p.x + half_w, p.y - half_h),
                                 xy_word(p.x - half_w, p.y), xy_word(p.x + half_w, p.y)};
    const std::uint32_t row = static_cast<std::uint32_t>(byte_of(s.a, 2)) << 13;
    const std::uint32_t uv[4] = {row + (kDripPalette << 16), row + (kDripPage << 16) + 0x3F,
                                 (row | 0x1F00u) & 0xFFFFu, (row | 0x1F3Fu) & 0xFFFFu};

    std::int32_t grey = phase < 0x800 ? mul12_wide(phase * 2, 0x80) : 0x80 - mul12_wide((phase - 0x800) * 2, 0x80);
    grey = (grey * host.darkness(s.x, s.y, s.z)) >> 7;
    // Red and green were one halfword: a red past 255 carries into green.
    const std::uint32_t rg = static_cast<std::uint16_t>(grey + (grey << 8));
    draw3d::Polygon q = textured_quad(0, xy, uv);
    q.shade = {true, false};
    q.v[0].color = {static_cast<std::uint8_t>(rg), static_cast<std::uint8_t>(rg >> 8), static_cast<std::uint8_t>(grey)};
    host.emit(q, SlotLayer::World, (p.depth + 256) >> 3);
    return true;
}

// ---- light glow ------------------------------------------------------------------------------------------------

void LightGlow::spawn(SlotEffectHost& host, const Vec3& at, std::uint16_t size, std::uint16_t spread,
                      std::uint32_t flags) {
    const int slot = host.alloc(slot_kind::kLightGlow);
    if (slot < 0) return;
    EffectSlot s = host.load(slot);
    s.x = at.x >> 4;  // the glow keeps its position in geometry units
    s.y = static_cast<std::int16_t>(at.y >> 4);
    s.z = at.z >> 4;
    set_lo16(s.a, size / 2);
    set_hi16(s.a, spread >> 4);
    s.flags = static_cast<std::uint8_t>(flags);
    host.store(slot, s);
}

bool LightGlow::draw(SlotEffectHost& host, int slot) {
    EffectSlot s = host.load(slot);
    s.kind = 0;  // a glow lasts one frame
    host.store(slot, s);
    if (s.flags & 1) host.screen_copy();

    const SlotView& view = host.view();
    const std::int32_t size = static_cast<std::uint16_t>(lo16(s.a));
    const std::int32_t spread = static_cast<std::uint16_t>(hi16(s.a));
    const std::uint16_t palette = palette_word(kGlowPaletteX, kGlowPaletteY);
    for (int i = 0; i < 4; i++) {
        Vec3s rel{static_cast<std::int16_t>(s.x - view.origin.x), static_cast<std::int16_t>(s.y - view.origin.y),
                  static_cast<std::int16_t>(s.z - view.origin.z)};
        std::uint32_t v = 0;
        if (i != 0) {
            // A zero spread would have trapped on the divide; the draws are still taken.
            const auto jitter = [&](std::int16_t& c) {
                const std::int32_t r = host.rand16();
                if (spread != 0) c = static_cast<std::int16_t>(c + (r % spread - (spread >> 1)));
            };
            jitter(rel.x);
            jitter(rel.y);
            jitter(rel.z);
            v = (static_cast<std::uint32_t>(host.rand16()) % 2u == 0) ? 64u : 0u;
        }
        const ScreenPoint p = host.project(rel);
        const bool in_depth = p.depth > 0 && (p.depth >> 3) < kOrderingTableSize && std::abs(p.y) <= 160;
        // The original stops at a quad outside its box; a wider frame still shows that one (effect_view.h).
        const bool shown = in_depth && std::abs(p.x) <= 200;
        if (!shown && !(in_depth && std::abs(p.x) <= screen_box_half_width())) {
            return false;
        }
        const std::uint16_t half = static_cast<std::uint16_t>(((size * view.screen_distance) / p.depth) >> 4);
        const std::int32_t hy = static_cast<std::int16_t>(half);
        const std::uint32_t xy[4] = {xy_word(p.x - half, p.y - hy), xy_word(p.x + half, p.y - hy),
                                     xy_word(p.x - half, p.y + hy), xy_word(p.x + half, p.y + hy)};
        const std::uint32_t uv[4] = {128u + (v << 8) + (static_cast<std::uint32_t>(palette) << 16),
                                     255u + (v << 8) + (kGlowPage << 16), (128u + ((v + 63u) << 8)) & 0xFFFFu,
                                     (255u + ((v + 63u) << 8)) & 0xFFFFu};
        draw3d::Polygon q = textured_quad(0, xy, uv);
        q.shade = {true, false};
        q.v[0].color = {kGlowGrey, kGlowGrey, kGlowGrey};
        host.emit(q, SlotLayer::World, p.depth >> 3);
        if (!shown) {
            return false;
        }
    }
    return true;
}

}  // namespace sh::effects
