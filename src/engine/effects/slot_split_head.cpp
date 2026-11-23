// SPDX-License-Identifier: GPL-3.0-or-later
// The Split Head's slot effects (slot_effects.h): drool drops that fall and splat, and the drag mark Harry leaves
// pushing the boss.

#include <algorithm>
#include <cstdlib>

#include "engine/effects/slot_effects.h"

namespace sh::effects {

namespace {

constexpr std::int32_t kOrderingTableSize = 2048;
constexpr std::uint32_t kTranslucentQuad = 0x2Eu << 24;
// Drops further than this from Harry (sum of the axis distances, Q12) are not drawn.
constexpr std::int32_t kDrawRange = 20 * 4096;
// A splat lives this long (Q12 seconds) and grows a texture frame every 0.6 s.
constexpr std::int32_t kSplatLife = 0x4CC0;
constexpr std::int32_t kSplatFrame = 2456;

std::uint32_t grey_word(std::int32_t c) {
    return static_cast<std::uint32_t>(c + (c << 8) + (c << 16)) + kTranslucentQuad;
}

bool far_from_player(SlotEffectHost& host, const EffectSlot& s) {
    const Vec3 player = host.player_position();
    return std::abs(player.x - s.x) + std::abs(player.z - s.z) > kDrawRange;
}

}  // namespace

void SplitHeadEffects::spawn_drool(SlotEffectHost& host, std::int32_t x, std::int32_t y, std::int32_t z,
                                   std::int32_t speed, std::int32_t heading) {
    const int slot = host.alloc(slot_kind::kSplitHeadDrool);
    if (slot < 0) return;
    EffectSlot s = host.load(slot);
    const auto jitter = [&](std::int32_t base, std::int32_t low, std::uint32_t range) {
        return (base + static_cast<std::int32_t>(static_cast<std::uint32_t>(host.rand16()) % range) + low) * 16;
    };
    s.x = jitter(x, -32, 64);
    s.y = static_cast<std::int16_t>(jitter(y, 0, 64));
    s.z = jitter(z, -32, 64);
    s.flags = 0;  // falling
    set_lo16(s.a, 0);      // vertical speed
    set_hi16(s.a, speed);  // horizontal speed
    set_lo16(s.b, heading);
    set_byte(s.b, 2, static_cast<std::uint32_t>(host.rand16()) % 32u + 16u);  // splat size
    set_byte(s.b, 3, static_cast<std::uint32_t>(host.rand16()) % 16u);        // splat cell and mirror
    host.store(slot, s);
}

bool SplitHeadEffects::draw_drool(SlotEffectHost& host, int slot) {
    EffectSlot s = host.load(slot);
    const bool drawn = s.flags == 0 ? draw_falling(host, slot, s) : draw_splat(host, slot, s);
    return drawn;
}

bool SplitHeadEffects::draw_falling(SlotEffectHost& host, int slot, EffectSlot& s) {
    const std::int32_t dt = host.delta_time();
    const std::int16_t heading = lo16(s.b);
    const std::int32_t speed = hi16(s.a);
    const std::int32_t vx = mul12(speed, host.sin(heading));
    s.y = static_cast<std::int16_t>(s.y + mul12_wide(dt, lo16(s.a)));
    s.x += mul12_wide(dt, vx);
    const std::int32_t vz = mul12(speed, host.cos(heading));
    s.z += mul12_wide(dt, vz);
    set_lo16(s.a, lo16(s.a) + (host.gravity_step() >> 1));
    set_hi16(s.a, std::max(hi16(s.a) - mul12_wide(dt, 2048), 0));

    const std::int32_t ground = host.ground_height(s.x, s.z);
    if (ground < s.y || s.y > 0) {
        s.y = static_cast<std::int16_t>(std::min(ground, 0));
        s.flags = 1;
        set_lo16(s.a, 0);
        host.play_landing_sound(Vec3{s.x, s.y, s.z});
    }
    host.restore_view();
    host.store(slot, s);

    if (far_from_player(host, s)) return false;

    const SlotView& view = host.view();
    const ScreenPoint p = host.project(Vec3s{static_cast<std::int16_t>((s.x >> 4) - view.origin.x),
                                             static_cast<std::int16_t>((s.y >> 4) - view.origin.y),
                                             static_cast<std::int16_t>((s.z >> 4) - view.origin.z)});
    if (p.depth - 16 <= 0 || ((p.depth - 16) >> 3) >= kOrderingTableSize || std::abs(p.x) > 200 ||
        std::abs(p.y) > 160) {
        return false;
    }
    // A thin streak, taller the steeper the camera looks down.
    const std::int32_t half_w = view.screen_distance * 4 / p.depth;
    const std::int32_t half_h = ((((view.pitch + 0x400) >> 9) + 7) * view.screen_distance) / p.depth;
    const std::uint32_t xy[4] = {xy_word(p.x - half_w, p.y - half_h), xy_word(p.x + half_w, p.y - half_h),
                                 xy_word(p.x - half_w, p.y + half_h), xy_word(p.x + half_w, p.y + half_h)};
    const std::uint32_t uv[4] = {0x530000u, 0x6B001Fu, 0x1F00u, 0x1F1Fu};
    const std::int32_t grey =
        std::min(static_cast<std::int32_t>((static_cast<std::uint32_t>(host.darkness(s.x, s.y, s.z)) * 0x31) >> 5),
                 0xC4);
    host.emit(textured_quad(grey_word(grey), xy, uv), SlotLayer::World, (p.depth - 8) >> 3);
    return true;
}

bool SplitHeadEffects::draw_splat(SlotEffectHost& host, int slot, EffectSlot& s) {
    // The splat's age is read unsigned.
    const auto age_of = [](const EffectSlot& e) { return static_cast<std::int32_t>(static_cast<std::uint16_t>(lo16(e.a))); };
    std::int32_t bright = std::max(0xFF - ((age_of(s) * 16) / 1228), 0);
    set_lo16(s.a, lo16(s.a) + host.delta_time());
    if (age_of(s) > kSplatLife) s.kind = 0;  // drawn this frame still
    host.store(slot, s);

    if (far_from_player(host, s)) return false;

    const std::int32_t age = age_of(s);
    const std::int32_t full = byte_of(s.b, 2);
    const std::int32_t half = std::min(full, (age * full) / kSplatFrame + 8);

    const SlotView& view = host.view();
    const std::int32_t cx = (s.x >> 4) - view.origin.x;
    const std::int32_t cy = (s.y >> 4) - view.origin.y;
    const std::int32_t cz = (s.z >> 4) - view.origin.z;
    const auto at = [&](std::int32_t dx, std::int32_t dz) {
        return Vec3s{static_cast<std::int16_t>(cx + dx), static_cast<std::int16_t>(cy), static_cast<std::int16_t>(cz + dz)};
    };
    const Vec3s corners[4] = {at(-half, half), at(half, half), at(-half, -half), at(half, -half)};
    ScreenPoint p[4];
    host.project_corners(corners, p);

    const std::int32_t depth = (p[0].depth + p[1].depth + p[2].depth + p[3].depth) >> 2;
    if (depth - 16 <= 0 || ((depth - 16) >> 3) >= kOrderingTableSize || std::abs(p[3].x) > 200 ||
        std::abs(p[3].y) > 160) {
        return false;
    }

    bright = std::min(bright, (bright * host.darkness(s.x, s.y, s.z)) >> 7);

    // One of eight cells of a 32 x 32 sheet, mirrored or turned by the cell's bits; the row grows with the age.
    const std::int32_t cell = byte_of(s.b, 3) & 7;
    const std::int32_t u0 = (byte_of(s.b, 3) & 8) ? 0 : 32;
    const std::int32_t v0 = std::min(age / kSplatFrame, 7) << 5;
    const bool turned = (cell & 4) != 0;
    const bool x_flip = (cell & 1) == 0;
    const bool y_flip = (cell & 2) == 0;
    // Edge selection of the original: with the turn bit it takes a flag as is, otherwise inverted.
    const auto edge = [&](bool f) { return turned ? f : !f; };
    const std::int32_t u_a = edge(x_flip != y_flip) ? 0 : 0x1F;
    const std::int32_t v_a = cell >= 4 ? 0x1F : 0;
    const std::int32_t u_b = edge(x_flip) ? 0 : 0x1F;
    const std::int32_t v_b = edge(y_flip) ? 0x1F : 0;
    const std::int32_t u_c = edge(x_flip) ? 0x1F : 0;
    const std::int32_t v_c = edge(y_flip) ? 0 : 0x1F;
    const std::int32_t u_d = edge(x_flip != y_flip) ? 0x1F : 0;
    const std::int32_t v_d = cell < 4 ? 0x1F : 0;
    const std::uint32_t uv[4] = {static_cast<std::uint32_t>(u0 + u_a + ((v0 + v_a) << 8)) + 0x530000u,
                                 static_cast<std::uint32_t>(u0 + u_b + ((v0 + v_b) << 8)) + 0x6B0000u,
                                 static_cast<std::uint32_t>(u0 + u_c + ((v0 + v_c) << 8)) & 0xFFFFu,
                                 static_cast<std::uint32_t>(u0 + u_d + ((v0 + v_d) << 8)) & 0xFFFFu};
    const std::uint32_t xy[4] = {xy_word(p[0].x, p[0].y), xy_word(p[1].x, p[1].y), xy_word(p[2].x, p[2].y),
                                 xy_word(p[3].x, p[3].y)};
    host.emit(textured_quad(grey_word(bright & 0xFF), xy, uv), SlotLayer::World, (depth - 16) >> 3);
    return true;
}

void SplitHeadEffects::spawn_drag(SlotEffectHost& host, std::int32_t row, std::int32_t brightness, const Vec3& front,
                                  const Vec3& back) {
    const int first = host.alloc(slot_kind::kSplitHeadDragFront);
    if (first < 0) return;
    const int second = host.alloc(slot_kind::kSplitHeadDragBack);
    EffectSlot a = host.load(first);
    if (second < 0) {
        a.kind = 0;
        host.store(first, a);
        return;
    }
    a.flags = static_cast<std::uint8_t>(row);
    set_lo16(a.a, second);
    set_byte(a.a, 2, static_cast<std::uint32_t>(brightness));
    a.x = front.x;
    a.y = static_cast<std::int16_t>(front.y);
    a.z = front.z;
    host.store(first, a);
    EffectSlot b = host.load(second);
    b.x = back.x;
    b.y = static_cast<std::int16_t>(back.y);
    b.z = back.z;
    host.store(second, b);
}

bool SplitHeadEffects::draw_drag(SlotEffectHost& host, int slot) {
    EffectSlot s = host.load(slot);
    const int other = lo16(s.a);
    s.kind = 0;  // both ends last one frame
    host.store(slot, s);
    EffectSlot e = host.load(other);
    e.kind = 0;
    host.store(other, e);

    // The ends are taken as geometry units here (the original read the low halves of the positions).
    const SlotView& view = host.view();
    const ScreenPoint a = host.project(Vec3s{static_cast<std::int16_t>(s.x - view.origin.x),
                                             static_cast<std::int16_t>(s.y - view.origin.y),
                                             static_cast<std::int16_t>(s.z - view.origin.z)});
    if (std::abs(a.x) > 320 || std::abs(a.y) > 240) return false;
    const ScreenPoint b = host.project(Vec3s{static_cast<std::int16_t>(e.x - view.origin.x),
                                             static_cast<std::int16_t>(e.y - view.origin.y),
                                             static_cast<std::int16_t>(e.z - view.origin.z)});

    const std::int32_t deeper = std::max(a.depth, b.depth);
    const std::int32_t nearer = b.depth >= a.depth ? a.depth : b.depth;
    const std::int32_t depth = (nearer * 3 + deeper) >> 2;
    if (depth <= 0 || (depth >> 3) >= kOrderingTableSize) return false;

    const std::int16_t angle = static_cast<std::int16_t>(host.atan2(b.y - a.y, b.x - a.x));
    const std::int32_t narrowing =
        static_cast<std::int32_t>((static_cast<std::int64_t>(4096 - host.length2(s.x - e.x, s.z - e.z)) * 24) >> 12);
    const std::int32_t width = std::max(narrowing * view.screen_distance / depth, 1);
    const std::int16_t ox = static_cast<std::int16_t>(mul12(width, host.sin(angle)));
    const std::int16_t oy = static_cast<std::int16_t>(mul12(width, host.cos(angle)));

    const std::uint32_t xy[4] = {xy_word(a.x - ox, a.y - oy), xy_word(b.x - ox, b.y - oy), xy_word(a.x + ox, a.y + oy),
                                 xy_word(b.x + ox, b.y + oy)};
    const std::uint32_t row = static_cast<std::uint32_t>(s.flags) * 0xC00u;
    const std::uint32_t uv[4] = {row + 0x010E4000u, row + 0x6D407Fu, (row + 0x4B00u) & 0xFFFFu,
                                 (row + 0x4B7Fu) & 0xFFFFu};

    const std::int32_t dark =
        host.darkness(((s.x + e.x) * 16) >> 1, (s.y + e.y) * 8, ((s.z + e.z) * 16) >> 1);
    const std::int32_t level = byte_of(s.a, 2);
    const std::int32_t shaded = (level * dark) >> 6;
    host.emit(textured_quad(grey_word(std::min(level, shaded)), xy, uv), SlotLayer::World, depth >> 3);
    return true;
}

}  // namespace sh::effects
