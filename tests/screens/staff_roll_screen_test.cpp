// SPDX-License-Identifier: GPL-3.0-or-later
// Unit tests for the staff roll screen's font placement and draw2d commands (hand-computed values).

#include "engine/screens/staff_roll_screen.h"

#include "framework/sh_test.h"

namespace {
namespace sc = sh::screens;
namespace d2 = sh::draw2d;
}  // namespace

SH_TEST(screens_staff_roll_screen_font_page_and_blend) {
    sc::StaffRollScreen s;
    s.set_font_image(0x1B, 0x150, 0x1F0);  // page column 11, row 1; palette at (336, 496)
    sc::RollPrim page;
    page.kind = sc::RollPrim::Kind::TexturePage;
    auto c = s.commands(page);
    SH_CHECK(c.size() == 1);
    SH_CHECK(std::get<d2::DrawMode>(c[0]).page.word() == 0x1B);
    s.set_flat_blend_mode(1);
    c = s.commands(page);
    SH_CHECK(std::get<d2::DrawMode>(c[0]).page.word() == (0x1B | (1 << 5)));

    sc::RollPrim glyph;
    glyph.kind = sc::RollPrim::Kind::Glyph;
    glyph.uv[0] = 0x1234;
    glyph.rgb = 0x102030;
    c = s.commands(glyph);
    const auto& sp = std::get<d2::Sprite>(c[0]);
    SH_CHECK(sp.uv.u == 0x34 && sp.uv.v == 0x12);
    SH_CHECK(sp.palette.word() == ((0x1F0 << 6) | (0x150 >> 4)));
    SH_CHECK(sp.size == d2::RectSize::Free && sp.w == 24 && sp.h == 24);
    SH_CHECK(sp.color == (d2::Rgb{0x30, 0x20, 0x10}));

    glyph.kind = sc::RollPrim::Kind::LogoGlyph;
    c = s.commands(glyph);
    SH_CHECK(std::get<d2::Sprite>(c[0]).size == d2::RectSize::Px16);
}

SH_TEST(screens_staff_roll_screen_plane_takes_page_on_change) {
    sc::StaffRollScreen s;
    s.set_font_image(0x05, 0, 0);
    sc::RollPrim quad;
    quad.kind = sc::RollPrim::Kind::Quad;
    auto c = s.commands(quad);
    SH_CHECK(std::get<d2::Polygon>(c[0]).page.word() == 0);
    s.plane_changed();
    c = s.commands(quad);
    SH_CHECK(std::get<d2::Polygon>(c[0]).page.word() == 0x05);

    sc::RollPrim bar;
    bar.kind = sc::RollPrim::Kind::Bar;
    bar.x[0] = 3;
    bar.y[0] = 4;
    bar.x[1] = 512;
    bar.y[1] = 32;
    c = s.commands(bar);
    const auto& f = std::get<d2::Fill>(c[0]);
    SH_CHECK(f.at.x == 3 && f.at.y == 4 && f.w == 512 && f.h == 32);
    // The ending table comes with staff_roll.json: nothing before the data is loaded.
    SH_CHECK(s.ending_music(5) == -1);
    SH_CHECK(s.ending_timing(1).start == 0 && s.ending_timing(1).length == 0);
}
