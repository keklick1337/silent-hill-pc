// SPDX-License-Identifier: GPL-3.0-or-later
// Unit tests for the lens flares (sh::effects::LensFlare) with a scripted host.

#include "engine/effects/lens_flare.h"

#include <string>
#include <vector>

#include "framework/sh_test.h"

using namespace sh::effects;

namespace {

class Host final : public LensFlareHost {
public:
    std::int32_t depth = 999;  // size = 512 * 200 / 1000 = 102
    std::vector<SplashQuad> quads;
    std::vector<std::int32_t> slots;
    std::vector<std::int32_t> angles;
    int modes = 0;
    Vec3 view;

    std::int32_t sin(std::int32_t a) override {
        angles.push_back(a);
        return 4096;
    }
    std::int32_t cos(std::int32_t) override { return 0; }
    std::int32_t geom_screen() override { return 512; }
    void begin_view(const Vec3& o, std::int32_t) override { view = o; }
    ScreenPoint project_origin() override { return ScreenPoint{10, 20, depth}; }
    void emit_quad(const SplashQuad& q, std::int32_t slot) override {
        quads.push_back(q);
        slots.push_back(slot);
    }
    void emit_mode(std::int32_t, std::int32_t, std::int32_t, std::int32_t) override { modes++; }
};

const char* kFlare = R"({"flares": {"F": {"origins": [[1, 2, 3], [4, 5, 6]], "slot": 300, "size_numerator": 200,
    "max_size": 102, "prim_code": 58,
    "fans": [{"rays": 4, "angle": 8, "spin": 4, "outer": 8192, "inner": 4096, "color": 1315860}],
    "corona": {"spokes": 2, "rings": 2, "scale": 4096, "brightness_start": 2, "brightness_step": 1,
               "shade": [10, 11, 12]},
    "mode_tpage": 42, "mode_dfe": 0, "mode_dtd": 1}}})";

LensFlare make() {
    auto parsed = parse_lens_flares(kFlare);
    SH_CHECK(static_cast<bool>(parsed));
    return LensFlare(parsed.value().at("F"));
}

}  // namespace

SH_TEST(lens_flare_rejects_unknown_keys) {
    SH_CHECK(static_cast<bool>(parse_lens_flares(kFlare)));
    SH_CHECK(!parse_lens_flares(R"({"flares": {"F": {"origins": [[0, 0, 0]], "slot": 1, "size_numerator": 1,
        "max_size": 1, "prim_code": 58, "fans": [], "colour": 1}}})"));
    SH_CHECK(!parse_lens_flares(R"({"flares": {"F": {"origins": [], "slot": 1, "size_numerator": 1,
        "max_size": 1, "prim_code": 58, "fans": []}}})"));
}

SH_TEST(lens_flare_draws_fans_corona_and_mode) {
    LensFlare f = make();
    Host h;
    SH_CHECK(!f.draw(h, 2));  // no such origin
    SH_CHECK(f.draw(h, 1));
    SH_CHECK(h.view.x == 4 && h.view.z == 6);
    SH_CHECK(h.quads.size() == 4 + 2 * 2);
    SH_CHECK(h.modes == 1);
    SH_CHECK(h.slots[0] == 300);
    // Ray 0: base at size * 1, tip at size * 2 (sin = 1), centre lit.
    SH_CHECK(h.quads[0].x[0] == 102 + 10);
    SH_CHECK(h.quads[0].x[2] == 204 + 10);
    SH_CHECK(h.quads[0].x[1] == 10 && h.quads[0].y[1] == 20);
    SH_CHECK(h.quads[0].color[1] == 1315860 && h.quads[0].color[0] == 0);
    // Half a ray is (4096 / 4) / 2 = 512; the last ray closes on the start angle.
    SH_CHECK(h.angles[0] == 8 && h.angles[1] == 8 + 512);
    SH_CHECK(h.angles[11] == 8);
    // Corona ring 0 from shade[2] to shade[1], ring 1 to shade[0]; each ring is size / 2 wide.
    SH_CHECK(h.quads[4].color[0] == 12 && h.quads[4].color[1] == 11);
    SH_CHECK(h.quads[5].color[0] == 11 && h.quads[5].color[1] == 10);
    SH_CHECK(h.quads[4].x[1] == 10 + 51);
    // The fan turns by its spin per draw.
    SH_CHECK(f.phases()[0] == 4);
    h.angles.clear();
    f.draw(h, 0);
    SH_CHECK(h.angles[0] == 12);
}

SH_TEST(lens_flare_too_near_is_not_drawn) {
    LensFlare f = make();
    Host h;
    h.depth = 998;  // size 102.6 -> 102, still drawn
    SH_CHECK(f.draw(h, 0));
    h.depth = 900;  // 113 > max_size
    const std::size_t before = h.quads.size();
    SH_CHECK(!f.draw(h, 0));
    SH_CHECK(h.quads.size() == before);
    SH_CHECK(f.phases()[0] == 4);
}

SH_TEST(lens_flare_shipped_file_loads) {
    std::string source;
    auto loaded = load_lens_flares(std::string(SH_PROJECT_ROOT) + "/gamedata", "", &source);
    SH_CHECK(static_cast<bool>(loaded));
    if (loaded) {
        SH_CHECK(loaded.value().count("EndingFlare") == 1);
        SH_CHECK(loaded.value().at("EndingFlare").origins_q8.size() == 3);
    }
}
