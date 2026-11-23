// Ground morph of map6_s00 (src/engine/effects/ground_morph.h): which looks a cell is drawn with as the level
// passes the bands, the cross-fade weights and the cells cut by depth.

#include "engine/effects/ground_morph.h"

#include "framework/sh_test.h"

#include <vector>

namespace {

namespace fx = sh::effects;

class FakeHost final : public fx::GroundMorphHost {
public:
    std::int32_t depth = 1000;
    std::int32_t screen = 300;
    int projections = 0;
    struct Quad {
        sh::draw3d::Polygon p;
        std::int32_t bucket;
    };
    std::vector<Quad> quads;

    fx::Vec3 player_position() override { return {5 * 4096 + 100, 0, -3 * 4096}; }
    std::int32_t begin_view(const fx::Vec3& origin) override {
        // Whole metres.
        SH_CHECK(origin.x == 5 * 4096 && origin.z == -3 * 4096);
        return screen;
    }
    void project3(const fx::Vec3s v[3], fx::ScreenPoint out[3]) override {
        for (int k = 0; k < 3; k++) out[k] = project(v[k]);
    }
    fx::ScreenPoint project(const fx::Vec3s& v) override {
        projections++;
        return fx::ScreenPoint{static_cast<std::int16_t>(v.x / 32), static_cast<std::int16_t>(v.z / 32), depth};
    }
    fx::Rgb light(const fx::Vec3s&, const fx::Vec3s&, const fx::Vec3&) override { return {200, 100, 50}; }
    std::uint16_t palette(int look) override { return static_cast<std::uint16_t>(0x100 + look); }
    void emit(const sh::draw3d::Polygon& p, std::int32_t bucket) override { quads.push_back({p, bucket}); }
};

fx::GroundMorphTable bands(std::int16_t start, std::int16_t end) {
    fx::GroundMorphTable t;
    for (auto& col : t)
        for (auto& b : col) b = {start, end};
    return t;
}

}  // namespace

SH_TEST(ground_morph_street_before_the_band) {
    FakeHost h;
    fx::GroundMorph::draw(h, bands(100, 200), 0);
    SH_CHECK(h.projections == 17 * 17);
    // Every cell drawn once, with look 0's palette, full light, at the depth bucket.
    SH_CHECK(h.quads.size() == 16 * 16);
    SH_CHECK(h.quads[0].p.palette.word() == 0x100 && h.quads[0].bucket == (1000 + 0x80) >> 3);
    SH_CHECK(h.quads[0].p.v[0].color.r == 200 && h.quads[0].p.v[3].color.b == 50);
    SH_CHECK(h.quads[0].p.gouraud && h.quads[0].p.shade.translucent);
}

SH_TEST(ground_morph_cross_fade_and_otherworld) {
    FakeHost h;
    // A third into the band: look 1 fading into look 2 (each cell drawn twice).
    fx::GroundMorph::draw(h, bands(0, 1000), 4000 / 3);
    SH_CHECK(h.quads.size() == 2 * 16 * 16);
    SH_CHECK(h.quads[0].p.palette.word() == 0x101 && h.quads[1].p.palette.word() == 0x102);

    // Past the band: look 3 has no weight left, only the seam layer over it is drawn, at full light.
    FakeHost o;
    fx::GroundMorph::draw(o, bands(0, 10), 100);
    SH_CHECK(o.quads.size() == 16 * 16);
    SH_CHECK(o.quads[0].p.palette.word() == 0x104 && o.quads[0].p.v[0].color.r == 200);
}

SH_TEST(ground_morph_cells_behind_the_screen_are_skipped) {
    FakeHost h;
    h.depth = 100;  // depth + 0x80 below the screen distance
    fx::GroundMorph::draw(h, bands(100, 200), 0);
    SH_CHECK(h.quads.empty());
    FakeHost f;
    f.depth = 0x4000;  // beyond the ordering table
    fx::GroundMorph::draw(f, bands(100, 200), 0);
    SH_CHECK(f.quads.empty());
}
