// The character ray volume (engine/world/chara_volume.h) against its specification: every
// expected number below is worked out by hand from the integer pipeline — the Q.12 -> Q.8
// truncating shift, the 16-bit wraps of the state fields and the 64-bit products. The tests do not link the game layer.

#include "engine/world/chara_volume.h"

#include "framework/sh_test.h"

namespace {

namespace w = sh::world;

constexpr std::int32_t kQ12One = 4096;

w::CharaVolumeSource sample() {
    // A mid-size enemy: x=10.0, y=-1.0, z=100.0; move cylinder radius 0.5, bullet radius 0.2;
    // box: top 0.8 above the origin, bottom 1.2 below it, height 1.6; small shape offsets.
    w::CharaVolumeSource s;
    s.x = 10 * kQ12One;
    s.y = -1 * kQ12One;
    s.z = 100 * kQ12One;
    s.cylinder_radius = kQ12One / 2;              // 2048
    s.box_radius = 819;                           // 0.2 in Q.12, not a power of two
    s.box_top = 3277;                             // 0.8 in Q.12
    s.box_bottom = -4915;                         // -1.2 in Q.12
    s.box_height = 6553;                          // 1.6 in Q.12
    s.cylinder_offset_x = 100;
    s.cylinder_offset_z = 200;
    s.box_offset_x = -50;
    s.box_offset_z = 60;
    s.path = w::VolumePath::Cylinder;
    return s;
}

}  // namespace

SH_TEST(world_chara_volume_cylinder_path) {
    // radius = 2048 >> 4 = 128; top = (-4096 + -4915) >> 4 = -9011 >> 4 = -564 (-563.1875
    // floors to -564); low = (-4096 + 3277) >> 4 = -819 >> 4 = -52; x = (40960 + 100) >> 4 =
    // 2566; z = (409600 + 200) >> 4 = 25612 (the fractions truncate toward negative infinity).
    const w::CharaVolume v = w::build_chara_volume(sample(), w::VolumeInflate{});
    SH_CHECK(v.x == 2566);
    SH_CHECK(v.z == 25612);
    SH_CHECK(v.low == -52);
    SH_CHECK(v.high == -564);
    SH_CHECK(v.radius == 128);
}

SH_TEST(world_chara_volume_box_path) {
    // The box path swaps in the narrow radius (819 >> 4 = 51), the box offsets and box_height:
    // top = (-4096 + 6553) >> 4 = 2457 >> 4 = 153; x = (40960 - 50) >> 4 = 2556;
    // z = (409600 + 60) >> 4 = 25603; low is the box top bound in both paths.
    w::CharaVolumeSource s = sample();
    s.path = w::VolumePath::Box;
    const w::CharaVolume v = w::build_chara_volume(s, w::VolumeInflate{});
    SH_CHECK(v.x == 2556);
    SH_CHECK(v.z == 25603);
    SH_CHECK(v.low == -52);
    SH_CHECK(v.high == 153);
    SH_CHECK(v.radius == 51);
}

SH_TEST(world_chara_volume_bullet_inflate) {
    // vert 2.0, rad 1.5 on the box path: band = (819 * 8192) >> 12 = 1638 exactly;
    // top = 2457 + 1638 = 4095 -> 255; low = -819 - 1638 = -2457 -> -154;
    // radius = 51 + (((819 * 6144) >> 12) >> 4) = 51 + (1228 >> 4) = 51 + 76 = 127.
    w::CharaVolumeSource s = sample();
    s.path = w::VolumePath::Box;
    w::VolumeInflate p;
    p.active = true;
    p.vert_mul = 2 * kQ12One;
    p.rad_mul = 6144;
    const w::CharaVolume v = w::build_chara_volume(s, p);
    SH_CHECK(v.low == -154);
    SH_CHECK(v.high == 255);
    SH_CHECK(v.radius == 127);
    // The same overlay on the cylinder path is ignored (it is for bullets only).
    const w::CharaVolume c = w::build_chara_volume(sample(), p);
    SH_CHECK(c.high == -564);
    SH_CHECK(c.radius == 128);
}

SH_TEST(world_chara_volume_negative_truncation) {
    // Arithmetic shifts floor: -4095 >> 4 = -256 (not -255), -4097 >> 4 = -257,
    // -8191 >> 4 = -512 with offset -7 -> (-8191 - 7) >> 4 = -8198 >> 4 = -513... the sums
    // first: x = (-4095 - 7) >> 4 = -4102 >> 4 = -257, z = (-4097 - 9) >> 4 = -4106 >> 4 = -257,
    // top = (-8191 + 100) >> 4 = -8091 >> 4 = -506, low = (-8191 - 100) >> 4 = -8291 >> 4 = -519.
    w::CharaVolumeSource s;
    s.x = -4095;
    s.y = -8191;
    s.z = -4097;
    s.cylinder_radius = 2048;
    s.box_radius = 819;
    s.box_top = -100;
    s.box_bottom = 100;
    s.box_height = -33;
    s.cylinder_offset_x = -7;
    s.cylinder_offset_z = -9;
    s.box_offset_x = 5;
    s.box_offset_z = -11;
    const w::CharaVolume v = w::build_chara_volume(s, w::VolumeInflate{});
    SH_CHECK(v.x == -257);
    SH_CHECK(v.z == -257);
    SH_CHECK(v.low == -519);
    SH_CHECK(v.high == -506);
    SH_CHECK(v.radius == 128);
    // The box path: top = (-8191 - 33) >> 4 = -8224 >> 4 = -514, x = (-4095 + 5) >> 4 = -256.
    s.path = w::VolumePath::Box;
    const w::CharaVolume b = w::build_chara_volume(s, w::VolumeInflate{});
    SH_CHECK(b.x == -256);
    SH_CHECK(b.high == -514);
    SH_CHECK(b.radius == 51);
}

SH_TEST(world_chara_volume_large_values) {
    // Values past 16 bits in Q.8. radius: 0x7FFF000 >> 4 = 0x7FFF00, which wraps the 16-bit store to -256.
    w::CharaVolumeSource s;
    s.x = 0x10000000;
    s.y = 0x20000000;
    s.z = 0x10000;
    s.cylinder_radius = 0x10000;
    s.box_radius = 0x7FFF000;
    s.box_top = 0x100000;
    s.box_bottom = -0x100000;
    s.box_height = 0x180000;
    s.cylinder_offset_x = 0x10000;
    s.cylinder_offset_z = -0x10000;
    s.box_offset_x = 0x10000;
    s.box_offset_z = 0x10000;
    s.path = w::VolumePath::Box;
    const w::CharaVolume v = w::build_chara_volume(s, w::VolumeInflate{});
    // x = (0x10000000 + 0x10000) >> 4 = 0x1001000 (positions stay 32-bit); z = (0x10000 + 0x10000) >> 4 = 0x2000;
    // low: (0x20000000 + 0x100000) >> 4 = 0x2010000, which wraps to 0;
    // high: (0x20000000 + 0x180000) >> 4 = 0x2018000, which wraps to -32768.
    SH_CHECK(v.x == 0x1001000);
    SH_CHECK(v.z == 0x2000);
    SH_CHECK(v.low == 0);
    SH_CHECK(v.high == -32768);
    SH_CHECK(v.radius == -256);
}

SH_TEST(world_chara_volume_radius_wrap) {
    // A radius whose Q.8 form passes 32767 wraps like the game's 16-bit state field:
    // 600000 >> 4 = 37500 -> 37500 - 65536 = -28036.
    w::CharaVolumeSource s = sample();
    s.cylinder_radius = 600000;
    const w::CharaVolume v = w::build_chara_volume(s, w::VolumeInflate{});
    SH_CHECK(v.radius == -28036);
}
