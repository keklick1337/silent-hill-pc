#include "engine/world/screen_glow_overlay.h"

namespace sh::world {

namespace {

draw2d::Rgb rgb(std::uint32_t word) {
    return {static_cast<std::uint8_t>(word), static_cast<std::uint8_t>(word >> 8), static_cast<std::uint8_t>(word >> 16)};
}

draw2d::Point point(const ScreenPoint& p) { return {p.x, p.y}; }

}  // namespace

void ScreenGlowOverlay::build_ring(const GlowScreen& screen) {
    const std::array<ScreenPoint, 17> points = screen_ring_points(screen.ring_half_w, screen.height);
    // Stretched vertically like the world view, so the fill still reaches the top and bottom edges.
    const std::int32_t h2 = screen.height >> 1;
    const float scale = screen.v_scale > 1.0f ? screen.v_scale : 1.0f;
    const float shift = screen.v_shift < 0.0f ? -screen.v_shift : screen.v_shift;
    const float half_h = static_cast<float>(h2) * scale + shift + 8.0f;
    std::array<draw2d::Point, 17> at{};
    for (std::size_t i = 0; i < at.size(); ++i) {
        at[i].x = points[i].x;
        at[i].y = static_cast<std::int16_t>(static_cast<float>(points[i].y) * half_h / static_cast<float>(h2 > 0 ? h2 : 1));
    }
    for (Shapes& s : shapes_) {
        for (std::size_t i = 0; i < 16; ++i) {
            s.fill[i].xy[2] = at[i];
            s.fill[i].xy[3] = at[i + 1];
        }
    }
    ring_half_w_ = screen.ring_half_w;
}

void ScreenGlowOverlay::init(GlowOverlayHost& host) {
    for (Shapes& s : shapes_) {
        s.subtract.page = draw2d::TexturePage::from_word(0x40);
        s.subtract.dither = true;
        s.subtract.draw_on_display = true;
        s.add.page = draw2d::TexturePage::from_word(0x20);
        s.add.dither = true;
        s.add.draw_on_display = true;
        for (draw2d::Polygon& p : s.fan) {
            p.count = 3;
            p.gouraud = true;
            p.shade.translucent = true;
        }
        for (draw2d::Polygon& p : s.ring) {
            p.count = 4;
            p.gouraud = true;
            p.shade.translucent = true;
        }
        for (draw2d::Polygon& p : s.fill) {
            p.count = 4;
            p.gouraud = false;
            p.shade.translucent = true;
        }
    }
    built_ = true;
    build_ring(host.screen());
}

void ScreenGlowOverlay::set_tint(std::uint8_t fan_r, std::uint8_t fan_g, std::uint8_t fan_b, std::uint8_t edge_r,
                                 std::uint8_t edge_g, std::uint8_t edge_b) {
    const RingColors colors = ring_colors(fan_r, fan_g, fan_b, edge_r, edge_g, edge_b);
    for (Shapes& s : shapes_) {
        for (std::size_t j = 0; j < 16; ++j) {
            s.fan[j].color[0] = rgb(colors.fan);
            s.fan[j].color[1] = rgb(colors.steps[0]);
            s.fan[j].color[2] = rgb(colors.steps[0]);
            s.fill[j].color[0] = rgb(colors.steps[3]);
        }
        for (std::size_t k = 0; k < 3; ++k) {
            for (std::size_t j = 0; j < 16; ++j) {
                draw2d::Polygon& p = s.ring[k * 16 + j];
                p.color[0] = rgb(colors.steps[k]);
                p.color[1] = rgb(colors.steps[k]);
                p.color[2] = rgb(colors.steps[k + 1]);
                p.color[3] = rgb(colors.steps[k + 1]);
            }
        }
    }
}

void ScreenGlowOverlay::draw(GlowOverlayHost& host, const Vec3& pos, std::int32_t size, std::int32_t angle0,
                             std::int32_t angle1) {
    const GlowScreen screen = host.screen();
    // The ring radii are made for the 4:3 frame, where full darkness lay past the edge: a wide view stretches them.
    const std::int32_t half_w = screen.width >> 1;
    const float x_scale = half_w > 0 ? static_cast<float>(screen.ring_half_w) / static_cast<float>(half_w) : 1.0f;
    GlowRings rings;
    if (!glow_rings_build(pos, size, angle0, angle1, x_scale, rings)) {
        return;  // degenerate: the original divided by zero here
    }
    if (screen.ring_half_w != ring_half_w_) {
        build_ring(screen);
    }
    if (!built_) {
        return;  // the shapes are still blank: nothing is drawn
    }
    const int buf = host.buffer();
    Shapes& s = shapes_[static_cast<std::size_t>(buf & 1)];
    for (std::size_t j = 0; j < 16; ++j) {
        draw2d::Polygon& fan = s.fan[j];
        fan.xy[0] = {static_cast<std::int16_t>(pos.x), static_cast<std::int16_t>(pos.y)};
        fan.xy[1] = point(rings.ring[0][j]);
        fan.xy[2] = point(rings.ring[0][j + 1]);
        if (!host.flashlight_lit()) {
            host.link(0, buf, kGlowPartFan + static_cast<int>(j), fan);
        }
        draw2d::Polygon& fill = s.fill[j];
        fill.xy[0] = point(rings.ring[3][j]);
        fill.xy[1] = point(rings.ring[3][j + 1]);
        host.link(1, buf, kGlowPartFill + static_cast<int>(j), fill);
    }
    for (std::size_t i = 0; i < 3; ++i) {
        for (std::size_t j = 0; j < 16; ++j) {
            draw2d::Polygon& quad = s.ring[i * 16 + j];
            quad.xy[0] = point(rings.ring[i][j]);
            quad.xy[1] = point(rings.ring[i][j + 1]);
            quad.xy[2] = point(rings.ring[i + 1][j]);
            quad.xy[3] = point(rings.ring[i + 1][j + 1]);
            host.link(1, buf, kGlowPartRing + static_cast<int>(i * 16 + j), quad);
        }
    }
    // The additive mode goes in even without the fan: it is what returns the blend mode from the subtractive page,
    // and a draw mode draws no pixels.
    host.link(0, buf, kGlowPartAdd, s.add);
    host.link(1, buf, kGlowPartSubtract, s.subtract);
}

ScreenGlowOverlay& screen_glow_overlay() {
    static ScreenGlowOverlay overlay;
    return overlay;
}

}  // namespace sh::world
