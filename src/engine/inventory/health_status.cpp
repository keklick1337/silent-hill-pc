// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// The health status box of the inventory screen as drawing records (
// "Drawing"): the portrait, a scan line running down it, random glitch rows when hurt and a
// bar that pulses in the health colour from where the scan line was.

#include "engine/inventory/inventory_draw.h"

namespace sh::inventory {

namespace {

constexpr std::int32_t kFullHealth = 100 << 12;
constexpr std::int32_t kScanRows = 164;
constexpr std::int32_t kScanVisibleFrom = 30;
constexpr std::int32_t kPulseFrames = 64;
constexpr std::int16_t kNoMark = -300;
constexpr std::int32_t kBarCeiling = -182;
constexpr std::int32_t kHiddenTop = -52;

// Hurt players see the scan line run faster and the pulse beat faster.
std::int32_t pace(std::int32_t health)
{
    if (health < (10 << 12))
        return 3;
    if (health < (50 << 12))
        return 2;
    return 1;
}

Rgb bar_color(std::int32_t health)
{
    const std::int32_t points = health >> 12;
    if (points < 39)
        return {255, 0, 0};
    if (points < 65)
        return {255, 160, 0};
    if (points < 85)
        return {160, 255, 0};
    return {0, 255, 0};
}

std::int32_t s16(std::int32_t v) { return static_cast<std::int16_t>(v); }
std::int32_t u8(std::int32_t v) { return static_cast<std::uint8_t>(v); }

PortraitStrip row_strip(int part, std::int32_t v_top, std::int32_t v_bottom, std::int32_t y, std::int32_t right)
{
    PortraitStrip strip;
    strip.part = part;
    strip.uv = {Point{40, u8(v_top)}, Point{40, u8(v_bottom)}, Point{124, u8(v_bottom - 2)}, Point{124, u8(v_bottom)}};
    strip.corners = {Point{-136, s16(y)}, Point{-136, s16(y + 2)}, Point{right, s16(y)}, Point{right, s16(y + 2)}};
    return strip;
}

}  // namespace

HealthStatus layout_health_status(HealthMonitor& m, std::int32_t health, HealthRandom& random)
{
    HealthStatus out;
    m.scan = static_cast<std::int16_t>(static_cast<std::int16_t>(pace(health) + m.scan) % kScanRows);

    for (int part = 0; part < 3; part++) {
        if (part == 1 && m.scan < kScanVisibleFrom)
            continue;
        if (part == 2) {
            // A glitch starts at random (more often the more hurt) and lasts three frames.
            if (health != kFullHealth && ((random.next() % ((health >> 13) + 2)) == 0 || m.glitch != 0)) {
                if (++m.glitch == 4)
                    m.glitch = 0;
            } else {
                continue;
            }
        }

        PortraitStrip& strip = out.strips[out.strip_count++];
        if (part == 0) {
            strip.part = 0;
            strip.uv = {Point{40, 103}, Point{40, 237}, Point{124, 103}, Point{124, 237}};
            strip.corners = {Point{-138, -184}, Point{-138, -50}, Point{-54, -184}, Point{-54, -50}};
        } else if (part == 1) {
            strip = row_strip(1, m.scan + 73, m.scan + 75, m.scan - 214, -52);
            if (m.pulse == 0)
                m.scan_mark = static_cast<std::int16_t>(m.scan - 214);
        } else {
            // The first glitch frame picks a new row.
            std::int32_t first_v;
            if (m.glitch == 1) {
                m.glitch_row = static_cast<std::int16_t>(random.glitch_row());
                first_v = m.glitch_row;
            } else {
                first_v = m.glitch_row & 0xFF;
            }
            strip = row_strip(2, first_v + 103, m.glitch_row + 105, m.glitch_row - 184, -52);
        }
    }

    out.glow.corners = {Point{-136, s16(m.scan - 212)}, Point{-136, s16(m.scan - 182)}, Point{-54, s16(m.scan - 212)},
                        Point{-54, s16(m.scan - 182)}};
    out.glow.colors = {Rgb{255, 255, 255}, Rgb{0, 0, 0}, Rgb{255, 255, 255}, Rgb{0, 0, 0}};

    // The bar reaches down to just above the glow, or is hidden while the scan line is above it.
    const std::int32_t below_glow = out.glow.corners[0].y - 2;
    const std::int32_t mark_top = m.scan_mark - (m.pulse >> 1);
    const std::int32_t bar_bottom = (below_glow >= -214 && below_glow >= mark_top) ? s16(below_glow) : kHiddenTop;

    if (m.scan_mark != kNoMark) {
        const Rgb base = bar_color(health);
        const Rgb faded{static_cast<std::uint8_t>(base.r - ((base.r * m.pulse) >> 6)),
                        static_cast<std::uint8_t>(base.g - ((base.g * m.pulse) >> 6)), 0};
        const std::int32_t top = s16(mark_top < kBarCeiling ? kBarCeiling : mark_top);
        for (int i = 0; i < 2; i++) {
            GradientQuad& bar = out.bars[out.bar_count++];
            const std::int32_t bottom = i == 0 ? bar_bottom : s16(top - 16);
            bar.corners = {Point{-136, top}, Point{-136, bottom}, Point{-56, top}, Point{-56, bottom}};
            bar.colors = {faded, Rgb{0, 0, 0}, faded, Rgb{0, 0, 0}};
        }
    }

    m.pulse = static_cast<std::int16_t>(pace(health) + m.pulse);
    if (m.pulse >= kPulseFrames) {
        m.pulse = 0;
        m.scan_mark = kNoMark;
    }
    return out;
}

}  // namespace sh::inventory
