// SPDX-License-Identifier: GPL-3.0-or-later
#include "engine/screens/health_cues.h"

#include <cmath>

namespace sh::screens {

namespace {

// The game's runtime sine: double precision, truncated to Q12.
std::int32_t sine_q12(std::int32_t angle) {
    const double rad = (static_cast<double>(angle) / 4096.0) * 2.0 * 3.14159265358979323846;
    return static_cast<std::int32_t>(std::sin(rad) * 4096.0);
}

draw2d::Point point(std::int32_t x, std::int32_t y) {
    return {static_cast<std::int16_t>(x), static_cast<std::int16_t>(y)};
}

}  // namespace

draw2d::DrawMode additive_mode() {
    draw2d::DrawMode mode;
    mode.page = draw2d::TexturePage::from_word(1 << 5);  // page 0 at the origin, blend B + F
    mode.dither = true;
    return mode;
}

CueCommands HealFlash::update(std::int32_t frame_time, int screen_width, int screen_height) {
    CueCommands out;
    if (timer_ <= 0) {
        return out;
    }
    timer_ -= frame_time;
    if (timer_ < 0) {
        timer_ = 0;
    }
    const std::int32_t green = (timer_ * kPeakGreen) / kDuration;

    draw2d::Fill tile;
    tile.translucent = true;
    tile.color = {0, static_cast<std::uint8_t>(green), 0};
    tile.w = static_cast<std::uint16_t>(screen_width * 4);
    tile.h = static_cast<std::uint16_t>(screen_height * 2);
    tile.at = point(-screen_width, -screen_height);
    out.list[out.count++] = tile;
    out.list[out.count++] = additive_mode();
    return out;
}

CueCommands LowHealthGlow::update(const LowHealthGlowInput& in) {
    CueCommands out;
    if (!in.enabled || !in.gameplay || in.cutscene) {
        return out;
    }
    const std::int32_t hp = in.health >> 12;
    if (hp <= 0 || hp >= kHealthMax) {
        return out;
    }

    phase_ += in.frame_time;
    while (phase_ >= kPeriod) {
        phase_ -= kPeriod;
    }
    const std::int32_t pulse =
        (sine_q12(static_cast<std::int32_t>((static_cast<std::int64_t>(phase_) * 4096) / kPeriod)) + 4096) >> 1;
    const std::int32_t strength = ((kHealthMax - hp) * 4096) / kHealthMax;  // by the deficit
    std::int32_t r = kRedMin + (((kRedMax - kRedMin) * pulse) >> 12);
    r = (r * (2048 + (strength >> 1))) >> 12;  // half at 19 points, full at the brink
    if (r <= 0) {
        return out;
    }

    const std::int32_t hw = in.half_width;
    const std::int32_t hh = in.half_height;
    const std::int32_t dy = (hh * 27) / 100;
    const std::int32_t dx = (hw * 20) / 100;
    const draw2d::Rgb red{static_cast<std::uint8_t>(r), 0, 0};
    const draw2d::Rgb black{};

    for (int i = 0; i < 4; ++i) {
        draw2d::Polygon q;
        q.count = 4;
        q.gouraud = true;
        q.shade.translucent = true;
        auto corners = [&q](std::int32_t x0, std::int32_t y0, std::int32_t x1, std::int32_t y1, std::int32_t x2,
                            std::int32_t y2, std::int32_t x3, std::int32_t y3) {
            q.xy[0] = point(x0, y0);
            q.xy[1] = point(x1, y1);
            q.xy[2] = point(x2, y2);
            q.xy[3] = point(x3, y3);
        };
        switch (i) {
            case 0:  // top
                corners(-hw, -hh, hw, -hh, -hw, -hh + dy, hw, -hh + dy);
                q.color[0] = red, q.color[1] = red, q.color[2] = black, q.color[3] = black;
                break;
            case 1:  // bottom
                corners(-hw, hh - dy, hw, hh - dy, -hw, hh, hw, hh);
                q.color[0] = black, q.color[1] = black, q.color[2] = red, q.color[3] = red;
                break;
            case 2:  // left
                corners(-hw, -hh, -hw + dx, -hh, -hw, hh, -hw + dx, hh);
                q.color[0] = red, q.color[1] = black, q.color[2] = red, q.color[3] = black;
                break;
            default:  // right
                corners(hw - dx, -hh, hw, -hh, hw - dx, hh, hw, hh);
                q.color[0] = black, q.color[1] = red, q.color[2] = black, q.color[3] = red;
                break;
        }
        out.list[out.count++] = q;
    }
    out.list[out.count++] = additive_mode();
    return out;
}

int glow_half_width(int screen_width, int screen_height, bool hor_plus, int render_width, int render_height,
                    float pixel_aspect, float world_hscale) {
    const int half_h = screen_height / 2;
    int half_w = screen_width / 2;
    if (hor_plus && render_height > 0) {
        const float hs = world_hscale > 0.01f ? world_hscale : 1.0f;
        const float w = ((static_cast<float>(half_h) * static_cast<float>(render_width)) /
                         static_cast<float>(render_height)) * pixel_aspect / hs;
        half_w = static_cast<int>(w + 0.5f);
        if (half_w > 512) {
            half_w = 512;  // past 32:9, inside the 16-bit coordinate range
        }
        if (half_w < screen_width / 2) {
            half_w = screen_width / 2;
        }
    }
    return half_w;
}

}  // namespace sh::screens
