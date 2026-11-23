// Ending lake and sprites (src/engine/effects/lake_surface.h): the page blur, the ripple seeds and rings, the
// sprite and title geometry.

#include "engine/effects/lake_surface.h"

#include "framework/sh_test.h"

#include <vector>

namespace {

namespace fx = sh::effects;

class FakeHost final : public fx::LakeHost {
public:
    std::uint32_t seed = 3;
    std::int32_t dt = 136;
    int glows = 0;
    std::vector<std::int32_t> buckets;
    std::vector<std::int32_t> uploaded;
    std::int32_t rand16() override {
        seed = seed * 1103515245u + 12345u;
        return static_cast<std::int32_t>((seed >> 16) & 0x7FFF);
    }
    std::int32_t delta_time() override { return dt; }
    std::int32_t begin_view() override { return 300; }
    void project3(const fx::Vec3s v[3], fx::ScreenPoint out[3]) override {
        for (int k = 0; k < 3; k++) out[k] = project(v[k]);
    }
    fx::ScreenPoint project(const fx::Vec3s& v) override {
        return {static_cast<std::int16_t>(v.x / 16), static_cast<std::int16_t>(v.z / 16), 1000};
    }
    void screen_glow() override { glows++; }
    void emit(const sh::draw3d::Polygon&, std::int32_t bucket) override { buckets.push_back(bucket); }
    void upload_page(std::int32_t page, const std::uint32_t*) override { uploaded.push_back(page); }
};

}  // namespace

SH_TEST(lake_blur_keeps_flat_inside_and_darkens_edges) {
    std::vector<std::uint32_t> in(fx::kLakePageWords, 0x88888888u);  // every texel 8
    std::vector<std::uint32_t> out(fx::kLakePageWords, 0);
    fx::LakeSurface::blur(in.data(), out.data());
    // Inside: eight neighbours of 8 -> (64 + 7) / 8 = 8.
    SH_CHECK(out[100 * 32 + 5] == 0x88888888u);
    // The top-left texel has three neighbours: (24 + 7) / 8 = 3; its right neighbour five: (40 + 7) / 8 = 5.
    SH_CHECK((out[0] & 0xF) == 3 && ((out[0] >> 4) & 0xF) == 5);
}

SH_TEST(lake_pages_ping_pong_and_upload) {
    FakeHost h;
    std::vector<std::uint32_t> a(fx::kLakePageWords, 0x11111111u), b(fx::kLakePageWords, 0);
    std::uint32_t* buffers[2] = {a.data(), b.data()};
    const std::array<std::uint8_t, 8> pages = {10, 11, 12, 13, 14, 15, 16, 17};
    fx::LakeSurface::build_pages(h, pages, 0, buffers);
    SH_CHECK(h.uploaded.size() == 7 && h.uploaded.front() == 11 && h.uploaded.back() == 17);
    FakeHost r;
    fx::LakeSurface::build_pages(r, pages, 2, buffers);
    SH_CHECK(r.uploaded.size() == 5 && r.uploaded.front() == 13);
}

SH_TEST(lake_ripples_seed_and_draw) {
    FakeHost h;
    std::vector<std::uint32_t> ripples(fx::kLakeRippleWords, 0);
    fx::LakeSurface::seed(h, ripples.data());
    for (int c = 0; c < 256; c++) SH_CHECK(ripples[8 * 256 + c] == 0x100000u);
    int started = 0;
    for (int c = 0; c < 8 * 256; c++) started += ripples[c] != 0 ? 1 : 0;
    SH_CHECK(started > 0);

    const std::array<std::uint8_t, 8> pages = {1, 2, 3, 4, 5, 6, 7, 8};
    fx::LakeSurface::draw(h, ripples.data(), pages);
    SH_CHECK(h.glows == 1);
    // Every drawn quad is at the bucket of its depth.
    for (std::int32_t b : h.buckets) SH_CHECK(b == (1000 - 0x40) >> 3);
}

SH_TEST(ufo_ending_sprite_and_title) {
    fx::EndingLook look;
    look.jitter_x = 3;
    look.jitter_y = -2;
    look.palette = 0x1234;
    look.sprite_page = 9;
    look.title_page = 20;
    const fx::EndingSprite s{-50, 40, 30, 20, 7, 9};
    const sh::draw3d::Polygon plain = fx::UfoEnding::sprite(s, 0, look);
    SH_CHECK(plain.v[0].xy.x == -50 && plain.v[3].xy.x == -20 && plain.v[3].xy.y == 60);
    SH_CHECK(plain.v[0].uv.u == 7 && plain.v[3].uv.v == 29 && plain.palette.word() == 0x1234);
    SH_CHECK(!plain.shade.translucent && plain.v[0].color.r == 0x80);
    const sh::draw3d::Polygon shaken = fx::UfoEnding::sprite(s, 4, look);
    SH_CHECK(shaken.v[0].xy.x == -47 && shaken.v[0].xy.y == 38);
    SH_CHECK(shaken.page.word() != plain.page.word());

    const auto title = fx::UfoEnding::slide_in(4096, look);
    // At the end of the slide the halves meet at x + 18.
    SH_CHECK(title[0].v[1].xy.x == 21 && title[1].v[0].xy.x == 21);
    SH_CHECK(title[0].v[0].xy.y == -2 + 76 - 88 && title[0].v[2].xy.y == -10);
}
