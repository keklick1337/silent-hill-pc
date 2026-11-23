// SPDX-License-Identifier: GPL-3.0-or-later
// The match flame in Harry's hand (hand_flame.h).

#include "engine/effects/hand_flame.h"

namespace sh::effects {

namespace {

// A random word as a signed 12-bit angle.
std::int32_t signed12(std::int32_t v) { return static_cast<std::int32_t>(static_cast<std::uint32_t>(v) << 20) >> 20; }

}  // namespace

void HandFlame::draw(HandFlameHost& host) {
    if (host.delta_time() != 0) {
        flicker_ += 8;
        for (std::int16_t& n : noise_) n = static_cast<std::int16_t>(host.rand16());
    }

    // The flame sits at a point of the hand that wanders by a few units.
    const Vec3s at{static_cast<std::int16_t>(((signed12(noise_[0]) * 5) >> 12) + 1), -7,
                   static_cast<std::int16_t>(((signed12(noise_[1]) * 5) >> 12) + 33)};
    const ScreenPoint hand = host.project_hand(at);
    const std::int32_t depth = hand.depth * 4;
    std::int32_t bucket = (hand.depth >> 1) - 2;
    if (bucket < 0) bucket = 0;
    if (depth <= 128 || bucket >= 2047) return;

    // Now and then the flame dips; the brightness climbs back by 8 a frame and starts over past 32.
    const std::int32_t dip = noise_[2];
    if ((dip & 0xFFF) >= 3482) flicker_ -= 16 + (dip & 0xF);
    if (flicker_ >= 33) flicker_ = 0;
    const std::uint8_t gray = static_cast<std::uint8_t>(flicker_ + 48);

    std::int32_t jitter[4];
    for (int i = 0; i < 4; i++) jitter[i] = (noise_[3 + i] & 0xF) - 8;

    // The corners are offsets at the flame's depth: the centre's projection is taken off each.
    const std::int16_t z = static_cast<std::int16_t>(depth);
    const ScreenPoint centre = host.project_flat(Vec3s{0, 0, z});
    const std::int32_t offsets[4][2] = {{jitter[0] - 51, jitter[2] - 51},
                                        {jitter[1] + 51, jitter[3] - 51},
                                        {-51 - jitter[1], 51 - jitter[3]},
                                        {51 - jitter[0], 51 - jitter[2]}};

    draw3d::Polygon flame;
    flame.count = 4;
    flame.textured = true;
    flame.shade = {true, false};
    flame.v[0].color = {gray, gray, gray};
    flame.page = draw3d::TexturePage::from_word(44);
    flame.palette = draw3d::Palette::from_word(4146);
    for (int k = 0; k < 4; k++) {
        const ScreenPoint c = host.project_flat(Vec3s{static_cast<std::int16_t>(offsets[k][0]),
                                                      static_cast<std::int16_t>(offsets[k][1]), z});
        flame.v[k].xy = {static_cast<std::int16_t>(hand.x + (c.x - centre.x)),
                         static_cast<std::int16_t>(hand.y + (c.y - centre.y))};
    }
    const std::uint8_t u = 128;
    flame.v[0].uv = {u, 0};
    flame.v[1].uv = {static_cast<std::uint8_t>(u + 63), 0};
    flame.v[2].uv = {u, 63};
    flame.v[3].uv = {static_cast<std::uint8_t>(u + 63), 63};
    host.emit(flame, bucket);
}

}  // namespace sh::effects
