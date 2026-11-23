// Mesh set-up of the model drawing (engine/world/mesh_prep.h).

#include "engine/world/mesh_prep.h"

#include "framework/sh_test.h"

namespace {
namespace sw = sh::world;
}

SH_TEST(mesh_prep_copy_unpadded_copies_exactly_the_counts) {
    const sw::ScreenXy in_xy[4] = {{1, 2}, {3, 4}, {5, 6}, {7, 8}};
    const std::int16_t in_z[4] = {10, 20, 30, 40};
    const std::uint8_t in_slots[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    sw::ScreenXy out_xy[4] = {};
    std::int16_t out_z[4] = {};
    std::uint8_t out_slots[8] = {};
    sw::copy_vertices(in_xy, in_z, in_slots, 3, 5, false, out_xy, out_z, out_slots);
    SH_CHECK(out_xy[2].x == 5 && out_xy[3].x == 0);
    SH_CHECK(out_z[2] == 30 && out_z[3] == 0);
    SH_CHECK(out_slots[4] == 5 && out_slots[5] == 0);
}

SH_TEST(mesh_prep_copy_padded_rounds_depths_to_pairs_and_slots_to_fours) {
    const sw::ScreenXy in_xy[4] = {{1, 2}, {3, 4}, {5, 6}, {7, 8}};
    const std::int16_t in_z[4] = {10, 20, 30, 40};
    const std::uint8_t in_slots[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    sw::ScreenXy out_xy[4] = {};
    std::int16_t out_z[4] = {};
    std::uint8_t out_slots[8] = {};
    sw::copy_vertices(in_xy, in_z, in_slots, 3, 5, true, out_xy, out_z, out_slots);
    SH_CHECK(out_xy[2].x == 5 && out_xy[3].x == 0);
    SH_CHECK(out_z[3] == 40);
    SH_CHECK(out_slots[7] == 8);
}

SH_TEST(mesh_prep_static_light_is_the_dot_product_plus_map_light_in_shade_bytes) {
    const std::int16_t light[3] = {0, 4096, 0};
    const sw::MeshNormal normals[2] = {{0, 127, 0, 2}, {0, -5, 0, 1}};
    std::uint8_t shade[4] = {9, 9, 9, 9};
    sw::light_normals_static(light, normals, 2, 0, shade);
    // 127 << 5 = 4064 -> IR1 4064, >> 5 = 127; a normal facing away clamps to 0.
    SH_CHECK(shade[0] == 127 && shade[1] == 127 && shade[2] == 0 && shade[3] == 9);
}

SH_TEST(mesh_prep_dynamic_light_slot_for_an_empty_normal_is_still_consumed) {
    sw::DynamicLightIn in;
    in.light_cap = 100;
    in.depth_cue_slope = 0;
    const sw::MeshNormal normals[2] = {{1, 2, 3, 0}, {1, 2, 3, 1}};
    const sw::ScreenXy xy[2] = {{100, 100}, {-100, 50}};
    const std::int16_t z[2] = {500, 700};
    std::uint8_t slots[3] = {0, 1, 1};
    const sw::DynamicLightOut out = sw::light_normals_dynamic(in, normals, 2, xy, z, slots);
    SH_CHECK(out.slots_used == 2 && out.any);
    SH_CHECK(slots[0] <= 100 && slots[1] <= 100 && slots[2] == 1);
}

SH_TEST(mesh_prep_scaled_light_halves_the_scaled_direction) {
    const std::int16_t dir[3] = {4096, -4096, 0};
    const std::uint8_t env[3] = {100, 50, 200};
    const sw::ScaledLight out = sw::scaled_light(dir, env, 2048);
    SH_CHECK(out.source[0] == 1024 && out.source[1] == -1024 && out.source[2] == 0);
    SH_CHECK(out.back[0] == 50 && out.back[1] == 25 && out.back[2] == 100);
}

SH_TEST(mesh_prep_chara_light_with_no_light_position_has_a_unit_length_fallback) {
    sw::CharaLightIn in;
    in.light_cap = 255;
    in.alpha = 4096;
    const sw::CharaLight out = sw::chara_light(in);
    SH_CHECK(out.rot[1][0] == 0 && out.rot[2][0] == 0);
    SH_CHECK(out.screen.x >= 0 && out.screen.x < 2048);
}
