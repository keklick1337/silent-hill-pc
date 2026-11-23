// The engine's 2D draw list (render/draw2d): lossless page and palette words, and the command ring's ids.

#include "framework/sh_test.h"
#include "render/draw2d/draw2d.h"

#include <cstdint>
#include <variant>

namespace {

namespace d2 = sh::draw2d;

}  // namespace

SH_TEST(draw2d_page_word_round_trip) {
    for (std::uint32_t w = 0; w <= 0xFFFF; ++w) {
        const auto word = static_cast<std::uint16_t>(w);
        SH_CHECK(d2::TexturePage::from_word(word).word() == word);
        SH_CHECK(d2::Palette::from_word(word).word() == word);
    }
}

SH_TEST(draw2d_page_fields) {
    // 8-bit page at x 640 (column 10), y 256, additive blend.
    const std::uint16_t word = (1 << 7) | (1 << 5) | (1 << 4) | 10;
    const d2::TexturePage page = d2::TexturePage::from_word(word);
    SH_CHECK(page.column == 10);
    SH_CHECK(page.row == 1);
    SH_CHECK(page.blend == d2::Blend::Add);
    SH_CHECK(page.depth == d2::Depth::Indexed8);
    SH_CHECK(page.other_bits == 0);
    // Row bit 1 lives in bit 11.
    SH_CHECK(d2::TexturePage::from_word(0x0800).row == 2);

    const d2::Palette pal = d2::Palette::from_word((480 << 6) | (256 >> 4));
    SH_CHECK(pal.column == 16);
    SH_CHECK(pal.row == 480);
}

SH_TEST(draw2d_ring_ids) {
    d2::CommandRing ring(2);  // 4 slots
    d2::Sprite s;
    s.at = {10, 20};
    const d2::CommandId first = ring.record(s);
    SH_CHECK(first != 0);
    const d2::Command* got = ring.find(first);
    SH_CHECK(got != nullptr);
    SH_CHECK(std::get<d2::Sprite>(*got) == s);

    for (int i = 0; i < 3; ++i) ring.record(d2::Fill{});
    SH_CHECK(ring.find(first) != nullptr);  // still within the ring
    ring.record(d2::DrawMode{});
    SH_CHECK(ring.find(first) == nullptr);  // overwritten
    SH_CHECK(ring.find(0) == nullptr);
    SH_CHECK(ring.find(0x1000000) == nullptr);
    SH_CHECK(ring.recorded() == 5);
}
