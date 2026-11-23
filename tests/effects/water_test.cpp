// SPDX-License-Identifier: GPL-3.0-or-later
// Unit tests for the flare and water module (sh::effects::WaterEffects) against a scripted host.

#include "engine/effects/water.h"

#include <fstream>
#include <iterator>
#include <vector>

#include "framework/sh_test.h"

using namespace sh::effects;

namespace {

WaterParams base_params() {
    std::ifstream in(std::string(SH_PROJECT_ROOT) + "/gamedata/effects/water.json", std::ios::binary);
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    sh::Result<WaterParams> p = parse_water_params(text);
    return p ? p.value() : WaterParams{};
}

class FakeHost final : public WaterHost {
public:
    FlareView view;
    FacingProbe probe;
    bool chase = false;
    std::vector<EffectPrim> prims;
    std::vector<std::int32_t> clocks;
    bool shaded = false;
    int sheens = 0;
    std::int32_t dt = 136;

    std::int32_t sin(std::int32_t) override { return 0; }
    std::int32_t cos(std::int32_t) override { return 4096; }
    std::int32_t atan2(std::int32_t, std::int32_t) override { return 0; }
    std::int32_t delta_time() override { return dt; }
    void screen_size(std::int32_t& w, std::int32_t& h) override { w = 320; h = 240; }
    FlareView flare_view(const Vec3&, const Vec3s&) override { return view; }
    std::int32_t projection_distance() override { return 1000; }
    FacingProbe facing_probe() override { return probe; }
    bool camera_chasing() override { return chase; }
    ScreenPoint project_mirror(const Vec3&) override { return ScreenPoint{100, 50, 40}; }
    void emit(const EffectPrim& p) override { prims.push_back(p); }
    void surface_clock(std::int32_t time, std::int32_t period) override {
        clocks.push_back(time);
        SH_CHECK(period == 128 * 136);
    }
    bool surface_shaded() override { return shaded; }
    int glows = 0;
    void surface_glow(const Vec3&, std::int32_t) override { glows++; }
    void sheen(const SheenDraw&) override { sheens++; }
};

}  // namespace

SH_TEST(water_params_file_parses) {
    const WaterParams p = base_params();
    SH_CHECK(p.flare_intensity.values.size() == 2 && p.flare_distance.values.size() == 7);
    SH_CHECK(p.flare_max == 6144 && p.ray_angles[0] == -1991 && p.scroll_tick == 136);
    SH_CHECK(p.surface_material == "DWAVE" && p.surface_lit[1] > 0 && p.wave_length > 0);
    SH_CHECK(p.quality[0].wave_layers <= p.quality[2].wave_layers && p.quality[0].blur_taps <= p.quality[2].blur_taps);
    SH_CHECK(p.quality[0].reflection_lines <= p.quality[1].reflection_lines && p.quality[2].reflection_lines >= 16);
    SH_CHECK(!parse_water_params(R"({"nope": 1})"));
}

SH_TEST(water_flare_target_follows_the_curves) {
    const WaterEffects fx(base_params());
    // Full strength, a lamp 0.8 m away and a 3.3 m angle value: strength 4096, distance curve
    // 16384, angle curve 614 -> (614 * 16384) >> 12.
    SH_CHECK(fx.flare_target(4096, 204, 853) == 2456);
    SH_CHECK(fx.flare_target(0, 204, 853) == 0);
    // Past the far ends of the distance and angle curves: (5324 * 819) >> 12.
    SH_CHECK(fx.flare_target(4096, 1 << 20, 1 << 20) == 1064);
}

SH_TEST(water_facing_and_zones) {
    SH_CHECK(WaterEffects::facing_factor({0, 100}) == 0);
    SH_CHECK(WaterEffects::facing_factor({50, 0}) == 0);
    SH_CHECK(WaterEffects::facing_factor({200, 100}) == 4096);
    SH_CHECK(WaterEffects::facing_factor({25, 100}) == 1024);

    const WaterZone zones[3] = {{true, 10, 0, 100, 0, 100}, {true, 20, 100, 200, 0, 100}, {false, 0, 0, 999, 0, 999}};
    SH_CHECK(WaterEffects::find_zone(zones, 3, 150, 50) == &zones[1]);
    SH_CHECK(WaterEffects::find_zone(zones, 3, 200, 50) == nullptr);  // the max edge is outside
    SH_CHECK(WaterEffects::find_zone(zones, 3, 500, 500) == nullptr);  // the disabled entry ends the list
    SH_CHECK(WaterEffects::find_zone(nullptr, 0, 0, 0) == nullptr);
}

SH_TEST(water_flare_ramps_and_draws) {
    WaterEffects fx(base_params());
    FakeHost host;
    host.view.screen = Vec3{10, -20, 3000};
    host.view.altitude = 1200;
    host.probe = {200, 100};

    fx.frame(host, 4096, Vec3s{}, Vec3{}, nullptr, 0);
    SH_CHECK(fx.state().flare_seen);
    host.prims.clear();
    fx.end_frame(host);
    // The level moves half way to the target (facing 1.0) and the halo, core, glare and
    // the whole star (16 rays + 3 ghosts) are drawn.
    const auto st = fx.state();
    SH_CHECK(!st.flare_seen && st.level == st.target / 2);
    SH_CHECK(host.prims.size() == 3 + 16 + 3);
    SH_CHECK(host.prims[0].slot == 1);  // facing the camera: in front of everything

    // Facing away: no star after the level is zero, and the draw still happens.
    host.probe = {0, 100};
    fx.frame(host, 4096, Vec3s{}, Vec3{}, nullptr, 0);
    host.prims.clear();
    host.chase = true;
    fx.end_frame(host);
    SH_CHECK(fx.state().level == 0 && host.prims.size() == 3);
}

SH_TEST(water_surface_scrolls_in_a_zone) {
    WaterEffects fx(base_params());
    FakeHost host;
    host.view.screen = Vec3{0, 0, 10};  // behind the half-plane distance: the flare is skipped, no surface
    const WaterZone zone{true, 5, -100, 100, -100, 100};
    fx.frame(host, 0, Vec3s{}, Vec3{}, &zone, 1);
    SH_CHECK(host.clocks.empty() && host.sheens == 0);

    host.view.screen = Vec3{0, 0, 2000};
    for (int i = 0; i < 4; i++) {
        fx.frame(host, 0, Vec3s{}, Vec3{}, &zone, 1);
    }
    // Four 30 Hz ticks.
    SH_CHECK(host.clocks.size() == 4 && host.sheens == 4);
    SH_CHECK(host.clocks[0] == 136 && host.clocks[3] == 4 * 136);
    // A surface the scene shades keeps its clock and loses the glint and the octagon.
    host.shaded = true;
    const std::size_t prims = host.prims.size();
    fx.frame(host, 0, Vec3s{}, Vec3{}, &zone, 1);
    SH_CHECK(host.clocks.size() == 5 && host.sheens == 4 && host.prims.size() == prims && host.glows == 1);
    host.shaded = false;
    fx.set_reflection_suppressed(true);
    host.clocks.clear();
    fx.frame(host, 0, Vec3s{}, Vec3{}, &zone, 1);
    SH_CHECK(host.clocks.empty());
}
