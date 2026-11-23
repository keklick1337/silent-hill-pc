// Sprite lighting, projection and quad size, the flashlight cone and the debounce (engine/world/billboard.h and
// effects_2d.h).

#include "engine/world/billboard.h"
#include "engine/world/effects_2d.h"

#include "framework/sh_test.h"

namespace {
namespace sw = sh::world;
}

SH_TEST(billboard_flat_light_is_the_tint_scaled_by_the_map_light) {
    sw::BillboardLightIn in;
    in.mode = 0;
    in.tint = {128, 64, 255};
    in.map_light = 2048;  // Q12 0.5
    const sw::BillboardLightOut out = sw::billboard_light(in);
    for (const auto& c : out.corners) {
        SH_CHECK(c.r == 64 && c.g == 32 && c.b == 127);
    }
}

SH_TEST(billboard_lit_light_rebuilds_the_light_rows_from_the_sun_direction) {
    sw::BillboardLightIn in;
    in.mode = 2;
    in.light.m[0][0] = 100;
    in.light.m[0][1] = 200;
    in.light.m[0][2] = -320;
    const sw::BillboardLightOut out = sw::billboard_light(in);
    SH_CHECK(out.light.m[0][0] == 100 && out.light.m[1][0] == (320 >> 4) && out.light.m[1][1] == (200 >> 4) &&
             out.light.m[2][2] == (-100 >> 4));
}

SH_TEST(billboard_quad_projects_and_sizes_by_depth) {
    sw::BillboardView view;
    view.projection.h = 500;
    view.projection.trans = {0, 0, 2000};
    view.depth_limit = 100000;
    const sw::BillboardQuad q = sw::billboard_quad(view, {0, 0, 0, 400, 800});
    SH_CHECK(q.visible && q.depth == 500 && q.full_depth == 2000);
    // scale = 500 * 4096 / 2000 = 1024: a quarter of the Q12 sizes (100 wide, 200 tall).
    SH_CHECK(q.x[1] - q.x[0] == 100 && q.x[0] - q.x[2] == 100 && q.y[3] - q.y[1] == 200 && q.y[3] - q.y[0] == 400);
}

SH_TEST(billboard_is_skipped_when_too_near_or_too_far) {
    sw::BillboardView view;
    view.projection.h = 500;
    view.depth_limit = 1000;
    view.projection.trans = {0, 0, 100};
    SH_CHECK(!sw::billboard_quad(view, {0, 0, 0, 10, 10}).visible);   // depth / 4 = 25, not above 32
    view.projection.trans = {0, 0, 5000};
    SH_CHECK(!sw::billboard_quad(view, {0, 0, 0, 10, 10}).visible);   // past the limit
}

SH_TEST(billboard_depth_limit_and_fog_byte) {
    SH_CHECK(sw::billboard_depth_limit(5000, false, 3000, false) == 5000);
    SH_CHECK(sw::billboard_depth_limit(5000, true, 3000, false) == 3000);
    SH_CHECK(sw::billboard_depth_limit(5000, true, 3000, true) == 0x7fffffff);
    SH_CHECK(sw::billboard_fog_byte(0, 0) == 0);
    SH_CHECK(sw::billboard_fog_byte(255, 4096) == 127);
}

SH_TEST(flashlight_cone_puts_the_light_in_view_space) {
    sw::FlashlightConeIn in;
    in.view.trans = {10, 20, 30};
    in.light_position = {16 * 100, 16 * 200, 16 * 300};  // Q12, 100/200/300 in Q8
    in.direction = {0, 0, 4096};
    const sw::FlashlightCone c = sw::flashlight_cone(in);
    SH_CHECK(c.position[0] == 110.0f && c.position[1] == 220.0f && c.position[2] == 330.0f);
    SH_CHECK(c.direction[2] == 4096.0f && c.color[0] == 1.0f);
}

SH_TEST(stable_flag_changes_only_after_it_has_agreed_for_several_frames) {
    sw::StableFlag f(3);
    SH_CHECK(f.update(true));      // the first answer is taken at once
    SH_CHECK(f.update(false));     // a new candidate
    SH_CHECK(f.update(false));     // agreed once
    SH_CHECK(f.update(false));     // twice
    SH_CHECK(!f.update(false));    // three times: adopted
    SH_CHECK(!f.update(true));     // a flicker back restarts the count
    SH_CHECK(!f.update(true));
    SH_CHECK(!f.update(false));
    SH_CHECK(!f.update(true));
}

SH_TEST(overlay_gray_repeats_the_value_in_three_bytes) {
    SH_CHECK(sw::overlay_gray(0x20) == 0x202020u);
}
