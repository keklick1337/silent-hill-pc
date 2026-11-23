// Full-screen background pictures (src/engine/screens/background.h), worked out by hand from the specification.

#include "engine/screens/background.h"

#include "framework/sh_test.h"

#include <variant>

namespace sc = sh::screens;
namespace d2 = sh::draw2d;

SH_TEST(background_tiles_cover_320_by_224) {
    sc::BackgroundImage img;
    img.depth = 1;  // 8-bit: 128 texels per page column
    img.column = 5;
    img.v = 16;
    img.clut_x = 0;
    img.clut_y = 480;
    const auto cmds = sc::background_tiles(img, 320, 224, 128);
    // 8-bit: columns 0..(319 >> 7); tiles of 256 and 64, the third starts past the width.
    SH_CHECK(cmds.size() == 4);
    const auto& first = std::get<d2::Sprite>(cmds[0]);
    SH_CHECK(first.w == 256 && first.h == 240 && first.at.x == -161 && first.at.y == -120);
    SH_CHECK(first.uv.v == 16 && first.palette.word() == (480 << 6));
    SH_CHECK(first.color.r == 128);
    const auto& mode = std::get<d2::DrawMode>(cmds[1]);
    SH_CHECK(mode.dither && mode.page.word() == ((1 << 7) | 5));
    SH_CHECK(std::get<d2::Sprite>(cmds[2]).w == 64);
    SH_CHECK(std::get<d2::DrawMode>(cmds[3]).page.word() == ((1 << 7) | 7));  // column 5 + (1 << depth)
}

SH_TEST(background_transition_blends) {
    sc::BackgroundImage a, b;
    b.column = 8;
    const auto polys = sc::background_transition(a, b, 2048);
    // `to` added at half, `from` subtracted at half, `from` opaque.
    SH_CHECK(polys[0].shade.translucent && polys[0].color[0].r == 64 && polys[0].page.column == 8);
    SH_CHECK(polys[0].page.blend == d2::Blend::Add);
    SH_CHECK(polys[3].page.blend == d2::Blend::Subtract && polys[3].page.column == 0);
    SH_CHECK(!polys[6].shade.translucent && polys[6].color[0].r == 128);
    // Column widths 128, 128, 64.
    SH_CHECK(polys[2].xy[0].x == 96 && polys[2].xy[1].x == 160 && polys[2].uv[1].u == 64);
}

SH_TEST(background_motion_blur_decay_per_tile) {
    struct Alternate final : sc::BlurDecay {
        int n = 0;
        bool decay_now() override { return (n++ & 1) == 0; }
    } decay;
    const auto cmds = sc::motion_blur_tiles(320, false, 1, decay);
    SH_CHECK(cmds.size() == 4);  // two tiles (256 + 64)
    SH_CHECK(std::get<d2::Sprite>(cmds[0]).color.r == 127 && std::get<d2::Sprite>(cmds[2]).color.r == 128);
    SH_CHECK(std::get<d2::Sprite>(cmds[0]).at.y == -112 && std::get<d2::Sprite>(cmds[0]).uv.v == 0);
    SH_CHECK(std::get<d2::DrawMode>(cmds[1]).page.row == 1);
}
