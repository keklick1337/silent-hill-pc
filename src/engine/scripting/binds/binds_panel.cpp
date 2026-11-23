// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// Named binds of flat textured panels a map script draws in the world (screens, signs, mirrors):
//   render.panel_view(x, y, z): the view of the world point (x, y, z) (Q12) the panels drawn after it use; their
//       corners are relative to that point in geometry units (Q8 metres, 16 bits).
//   render.panel(ax, ay, az, bx, by, bz, u0, v0, u1, v1, shade, page, palette_x, palette_y, blend, split) -> panels
//       drawn (0..4): an upright textured rectangle from corner a (top) to corner b (bottom); the other two corners
//       are (b.x, a.y, b.z) and (a.x, b.y, a.z). Texels u0..u1 x v0..v1 (low 8 bits) of the 4-bit texture page
//       `page` (0..31), palette at (palette_x, palette_y); `blend` the page's blend mode (the panel itself is
//       opaque), `shade` the grey level (128 = the texture as it is). The panel is ordered by its farthest corner
//       (1/8 of the depth) and dropped when that is at the camera or past the far end of the order. split 1 draws
//       it as 2 x 2 tiles: with the precise projection the tile corners are projected on the panel's plane,
//       otherwise they are halfway between the projected corners, as the PSX did. Against the scene's depth the
//       panel stands where its corners are (a little in front, as an effect lying on a surface): the screen or the
//       wall it is laid on does not cut it at any view angle, and what stands in front of it hides it.

#include "engine/scripting/binds/binds.h"

#include <array>
#include <cstdint>
#include <optional>

#include "engine/effects/drawer_projection.h"
#include "engine/math/view_projection.h"
#include "engine/scripting/binds/record_links.h"
#include "render/draw3d/draw3d.h"
#include "render/frame/frame.h"

namespace sh::script::binds {

namespace {

using named::Call;

// Order slots of the world layer; the last one is the far end.
constexpr int kOrderSlots = 2048;

// The view render.panel_view set; the panels continue the projection history of whatever drew before them.
std::optional<sh::math::ViewProjection> g_view;

// One projected point: screen position, the ordering depth (the projection's depth / 4, times 4, in 16 bits as the
// original kept it), the texel, and the precise record when the projection made one.
struct PanelPoint {
    std::int16_t x = 0, y = 0, depth = 0;
    std::uint16_t u = 0, v = 0;
    std::optional<sh::math::PreciseVertex> precise;
};

struct Vec3s16 {
    std::int16_t x, y, z;
};

PanelPoint project(sh::math::ViewProjection& vp, const Vec3s16& p) {
    const sh::math::PreciseVertex r = vp.project({p.x, p.y, p.z});
    PanelPoint out;
    out.x = r.v.sx;
    out.y = r.v.sy;
    out.depth = static_cast<std::int16_t>((r.v.sz >> 2) * 4);
    out.precise = r;
    return out;
}

PanelPoint mid(const PanelPoint& a, const PanelPoint& b) {
    PanelPoint m;
    m.x = static_cast<std::int16_t>((a.x + b.x) >> 1);
    m.y = static_cast<std::int16_t>((a.y + b.y) >> 1);
    m.depth = static_cast<std::int16_t>((a.depth + b.depth) >> 1);
    m.u = static_cast<std::uint16_t>((a.u + b.u) >> 1);
    m.v = static_cast<std::uint16_t>((a.v + b.v) >> 1);
    return m;
}

struct PanelLook {
    std::uint8_t grey;
    std::uint16_t page_word;
    std::uint16_t palette_word;
    bool view_space;
};

// One textured quad from four points (top-left, top-right, bottom-left, bottom-right). `precise`: the corners carry
// their precise records (on the precise path). The order slot is the farthest corner's, so the quad takes its
// corners' own depths against the scene: at the slot's depth a panel seen at an angle lost its near part to the
// map surface right behind it.
bool emit(const sh::math::ViewProjection& vp, const PanelPoint* p[4], const PanelLook& look, bool precise) {
    int z = p[0]->depth;
    for (int k = 1; k < 4; ++k) {
        z = p[k]->depth > z ? p[k]->depth : z;
    }
    const int slot = z >> 3;
    if (slot <= 0 || slot >= kOrderSlots - 1) {
        return false;
    }
    sh::draw3d::Polygon q;
    q.count = 4;
    q.textured = true;
    q.page = sh::draw3d::TexturePage::from_word(look.page_word);
    q.palette = sh::draw3d::Palette::from_word(look.palette_word);
    q.v[0].color = {look.grey, look.grey, look.grey};
    for (int k = 0; k < 4; ++k) {
        sh::draw3d::Vertex& v = q.v[static_cast<std::size_t>(k)];
        v.xy = {p[k]->x, p[k]->y};
        v.uv = {static_cast<std::uint8_t>(p[k]->u), static_cast<std::uint8_t>(p[k]->v)};
        if (precise && p[k]->precise) {
            const sh::math::PreciseVertex& r = *p[k]->precise;
            v.precise = sh::draw3d::Precise{r.sx, r.sy, r.w};
            if (look.view_space) {
                const sh::frame::State& fs = sh::frame::state();
                v.view = sh::draw3d::ViewPosition{r.view_x, r.view_y, r.view_z, !fs.no_shadow_cast, fs.chara_fade,
                                                  r.ofx, r.ofy, r.h};
            }
        }
    }
    const sh::frame::OrderKey key{sh::frame::Layer::World, static_cast<std::uint32_t>(slot)};
    const std::int32_t corner_z[4] = {p[0]->depth, p[1]->depth, p[2]->depth, p[3]->depth};
    sh::effects::drawer::bind_scene_depth(q, key, sh::effects::drawer::SceneDepth::Margin, corner_z);
    sh::draw3d::submit_placed(q, vp.depths(), key);
    return true;
}

std::uint16_t texture_page_word(int page, int blend) {
    const int x = (page & 0xF) << 6;
    const int y = (page & 0x10) << 4;
    return static_cast<std::uint16_t>(((blend & 3) << 5) | ((y & 0x100) >> 4) | ((x & 0x3FF) >> 6));
}

std::uint16_t palette_word(int x, int y) { return static_cast<std::uint16_t>((y << 6) | ((x >> 4) & 0x3F)); }

int draw_panel(sh::math::ViewProjection& vp, const long long* v) {
    const Vec3s16 a{static_cast<std::int16_t>(v[0]), static_cast<std::int16_t>(v[1]), static_cast<std::int16_t>(v[2])};
    const Vec3s16 b{static_cast<std::int16_t>(v[3]), static_cast<std::int16_t>(v[4]), static_cast<std::int16_t>(v[5])};
    const Vec3s16 corner[4] = {a, {b.x, a.y, b.z}, {a.x, b.y, a.z}, b};
    const auto u0 = static_cast<std::uint16_t>(v[6]);
    const auto v0 = static_cast<std::uint16_t>(v[7]);
    const auto u1 = static_cast<std::uint16_t>(v[8]);
    const auto v1 = static_cast<std::uint16_t>(v[9]);
    const bool precise = vp.precise().precise;
    const PanelLook look{static_cast<std::uint8_t>(v[10] & 0xFF), texture_page_word(static_cast<int>(v[11]), static_cast<int>(v[14])),
                         palette_word(static_cast<int>(v[12]), static_cast<int>(v[13])),
                         precise && sh::script::links::precise_view_space()};
    PanelPoint c[4];
    for (int i = 0; i < 4; ++i) {
        c[i] = project(vp, corner[i]);
    }
    c[0].u = u0, c[0].v = v0;
    c[1].u = u1, c[1].v = v0;
    c[2].u = u0, c[2].v = v1;
    c[3].u = u1, c[3].v = v1;
    if (v[15] == 0) {
        const PanelPoint* p[4] = {&c[0], &c[1], &c[2], &c[3]};
        return emit(vp, p, look, precise) ? 1 : 0;
    }
    // 3 x 3 grid of tile corners: rows top, middle, bottom.
    PanelPoint g[3][3];
    g[0][0] = c[0];
    g[0][2] = c[1];
    g[2][0] = c[2];
    g[2][2] = c[3];
    g[0][1] = mid(g[0][0], g[0][2]);
    g[1][0] = mid(g[0][0], g[2][0]);
    g[1][2] = mid(g[0][2], g[2][2]);
    g[2][1] = mid(g[2][0], g[2][2]);
    g[1][1] = mid(g[1][0], g[1][2]);
    if (precise) {
        // Screen-space halves bend the panel under a perspective-correct renderer: the tile corners are projected
        // on the panel's plane instead (the texels stay the halves above).
        for (int j = 0; j < 3; ++j) {
            for (int k = 0; k < 3; ++k) {
                auto blend = [&](std::int16_t Vec3s16::*axis) {
                    return static_cast<std::int16_t>(
                        ((2 - j) * ((2 - k) * (corner[0].*axis) + k * (corner[1].*axis)) +
                         j * ((2 - k) * (corner[2].*axis) + k * (corner[3].*axis))) >> 2);
                };
                const Vec3s16 p{blend(&Vec3s16::x), blend(&Vec3s16::y), blend(&Vec3s16::z)};
                const std::uint16_t u = g[j][k].u;
                const std::uint16_t tv = g[j][k].v;
                g[j][k] = project(vp, p);
                g[j][k].u = u;
                g[j][k].v = tv;
            }
        }
    }
    int drawn = 0;
    for (int j = 0; j < 2; ++j) {
        for (int k = 0; k < 2; ++k) {
            const PanelPoint* p[4] = {&g[j][k], &g[j][k + 1], &g[j + 1][k], &g[j + 1][k + 1]};
            drawn += emit(vp, p, look, precise) ? 1 : 0;
        }
    }
    return drawn;
}

int panel_view(Call& c) {
    long long v[3];
    if (!c.ints(3, v)) {
        return -1;
    }
    g_view = sh::script::links::view_at(static_cast<std::int32_t>(v[0]), static_cast<std::int32_t>(v[1]),
                                        static_cast<std::int32_t>(v[2]));
    return 0;
}

int panel(Call& c) {
    long long v[16];
    if (!c.ints(16, v)) {
        return -1;
    }
    if (v[11] < 0 || v[11] > 31 || v[12] < 0 || v[12] > 1023 || (v[12] & 15) != 0 || v[13] < 0 || v[13] > 511 ||
        v[14] < 0 || v[14] > 3) {
        return c.fail("%s: page 0..31, palette x 0..1008 (multiple of 16), y 0..511, blend 0..3", c.name());
    }
    if (!g_view) {
        return c.fail("%s: no render.panel_view before it", c.name());
    }
    // The view of panel_view, continuing the projections made since (screen set-up and history as they are now).
    sh::math::ViewProjection vp = *g_view;
    const sh::math::ViewProjection now = sh::math::game_view_projection();
    vp.set_screen(now.screen());
    vp.set_history(now.history());
    const int drawn = draw_panel(vp, v);
    sh::math::end_game_view_projection(vp);
    return c.reply({drawn});
}

const named::Bind kBinds[] = {
    {"render.panel_view", panel_view},
    {"render.panel", panel},
};

}  // namespace

std::span<const named::Bind> panel_binds() { return kBinds; }

}  // namespace sh::script::binds
