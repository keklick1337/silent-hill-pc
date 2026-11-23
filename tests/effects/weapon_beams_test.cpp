// Weapon beams (src/engine/effects/weapon_beams.h): the shipped file, the Hyper Blaster's
// segments, fading colours, depth culling and end dot, and the spark line's colour by the
// map's light mode.

#include "engine/effects/weapon_beams.h"

#include "framework/sh_test.h"

#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace {

namespace fx = sh::effects;

fx::WeaponBeams shipped() {
    std::ifstream in(std::string(SH_PROJECT_ROOT) + "/gamedata/effects/weapon_beams.json", std::ios::binary);
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    sh::Result<fx::WeaponBeams> r = fx::parse_weapon_beams(text);
    SH_CHECK(r);
    return r ? r.value() : fx::WeaponBeams{};
}

// Projection: x = relative x, y = relative z, depth from a table by call or 2 * (z / 16 + 10).
class FakeHost final : public fx::BeamHost {
public:
    fx::BeamHit hit;
    std::vector<std::int32_t> depths;  // by projection call; empty: from z
    std::vector<fx::Vec3> traced;
    std::vector<fx::Vec3s> projected;
    std::vector<fx::EffectPrim> prims;

    std::int32_t sin(std::int32_t a) override { return a == 1024 ? 4096 : 0; }  // 90 degrees
    std::int32_t cos(std::int32_t a) override { return a == 0 ? 4096 : 0; }
    fx::BeamHit trace(const fx::Vec3& from, const fx::Vec3& offset) override {
        traced.push_back(from);
        traced.push_back(offset);
        return hit;
    }
    fx::ScreenPoint project(const fx::Vec3s& r) override {
        const std::size_t k = projected.size();
        projected.push_back(r);
        const std::int32_t d = k < depths.size() ? depths[k] : 2 * (r.z / 16 + 10);
        return fx::ScreenPoint{r.x, r.z, d};
    }
    void emit(const fx::EffectPrim& p) override { prims.push_back(p); }
};

}  // namespace

SH_TEST(weapon_beams_shipped_file) {
    const fx::WeaponBeams b = shipped();
    SH_CHECK(b.hyper_blaster.line.texture.page == 44 && b.hyper_blaster.line.texture.palette_x == 768 &&
             b.hyper_blaster.line.texture.palette_y == 104);
    SH_CHECK(b.hyper_blaster.channels.size() == 3 && b.hyper_blaster.tip_colors.size() == 3);
    SH_CHECK(b.spark_line.by_light_mode.size() == 4 && b.spark_line.line.slot_max == 158);
    SH_CHECK(!b.drawn_on("map0_s00") && b.drawn_on("map0_s01"));
    SH_CHECK(!fx::parse_weapon_beams("{\"hyper_blaster\": {}, \"spark_line\": {}, \"maps_without_beams\": []}"));
}

SH_TEST(weapon_beams_hyper_blaster_segments_and_tip) {
    const fx::WeaponBeams b = shipped();
    FakeHost host;
    host.hit.hit = true;
    host.hit.distance = 2 * 4096 + 100;  // two metres: three segments
    host.hit.target = fx::Vec3{0, 0, 2 * 4096 + 100};
    fx::HyperBlasterShot shot;
    shot.range = 16 * 4096;
    shot.yaw = 0;      // along +z
    shot.pitch = 1024; // horizontal
    shot.color_option = 1;  // yellow
    fx::draw_hyper_blaster_beam(b.hyper_blaster, shot, host);
    // Direction (0, 0, range); a segment is range / 16 = 1 m = 256 geometry units.
    SH_CHECK(host.traced.size() == 2 && host.traced[1].z == 16 * 4096 && host.traced[1].x == 0);
    SH_CHECK(host.projected.size() == 6);
    SH_CHECK(host.projected[1].z == 256 && host.projected[3].z == 512);
    SH_CHECK(host.projected[5].z == (2 * 4096 + 100) >> 4);  // the last segment ends at the hit
    SH_CHECK(host.prims.size() == 4);                        // three lines and the dot
    const fx::EffectPrim& second = host.prims[1];
    SH_CHECK(second.gouraud && second.semi_transparent && second.has_color);
    SH_CHECK(second.rgb[0].r == 238 && second.rgb[0].g == 238 && second.rgb[0].b == 0);
    SH_CHECK(second.rgb[1].r == 221 && second.rgb[3].g == 221 && second.rgb[2].r == 238);
    // Slot of the near end: 256 / 16 + 10; palette row 104 + (26 >> 5).
    SH_CHECK(second.slot == 26 && second.texture.palette_y == 104);
    SH_CHECK(second.x[1] == 0 && second.y[1] == 512 && second.x[2] == 1 && second.y[2] == 257);
    SH_CHECK(second.u[0] == 10 && second.u[2] == 13 && second.v[3] == 116);
    const fx::EffectPrim& tip = host.prims[3];
    SH_CHECK(tip.kind == fx::EffectPrim::Kind::Sprite && tip.w == 4 && tip.u[0] == 3 && tip.v[0] == 112);
    SH_CHECK(tip.rgb[0].r == 192 && tip.rgb[0].g == 192 && tip.rgb[0].b == 0);
    SH_CHECK(tip.texture.palette_y == 96);
}

SH_TEST(weapon_beams_hyper_blaster_culling_and_no_colour) {
    const fx::WeaponBeams b = shipped();
    FakeHost host;
    host.hit.hit = false;
    host.hit.target = fx::Vec3{0, 0, 16 * 4096};
    // Segment 0: near end behind the camera (skipped with its far end unprojected);
    // segment 1: far end out of range (no line); the rest from z.
    host.depths = {0, 40, 600};
    fx::HyperBlasterShot shot;
    shot.range = 16 * 4096;
    shot.pitch = 1024;
    shot.color_option = 3;  // no such option: colours left as they are
    fx::draw_hyper_blaster_beam(b.hyper_blaster, shot, host);
    SH_CHECK(host.projected.size() == 1 + 2 * 15);
    SH_CHECK(host.prims.size() == 13 + 1);  // segments 2..14, then the last one's dot (its far end is culled)
    SH_CHECK(!host.prims[0].has_color && host.prims[0].slot == 2 * 16 + 10);
    SH_CHECK(host.prims.back().kind == fx::EffectPrim::Kind::Sprite && !host.prims.back().has_color);
}

SH_TEST(weapon_beams_spark_line_colours) {
    const fx::WeaponBeams b = shipped();
    const fx::Vec3 from{0, 0, 4096};
    const fx::Vec3 to{4096, 0, 4096};
    auto draw = [&](fx::BeamLights l, std::vector<std::int32_t> depths = {}) {
        FakeHost host;
        host.depths = std::move(depths);
        fx::draw_spark_line(b.spark_line, from, to, fx::Vec3{}, l, host);
        return host.prims;
    };
    fx::BeamLights l;
    l.map_light_mode = 0;
    SH_CHECK(draw(l).size() == 1 && !draw(l)[0].has_color && !draw(l)[0].gouraud && !draw(l)[0].semi_transparent);
    l.map_light_mode = 3;
    SH_CHECK(draw(l)[0].rgb[0].r == 64 && draw(l)[0].rgb[0].g == 32);
    l.map_light_mode = 2;
    SH_CHECK(draw(l)[0].rgb[0].r == 255);  // flashlight not allowed: lit
    l.flashlight_allowed = true;
    l.dark_flashlight_on = true;
    SH_CHECK(draw(l)[0].rgb[0].r == 48);
    l.flashlight_on = true;
    SH_CHECK(draw(l)[0].rgb[0].r == 255);
    l.flashlight_on = false;
    l.dark_flashlight_on = false;
    SH_CHECK(draw(l)[0].rgb[0].r == 255);
    l.dark_flashlight_off = true;
    SH_CHECK(draw(l)[0].rgb[0].r == 48);
    // Slot from the first point only: 318 / 2 = 159 is past 158.
    SH_CHECK(draw(l, {318, 2}).empty() && draw(l, {316, 900}).size() == 1);
    // Rising line (x grows, y does not): the second edge goes one pixel right.
    const fx::EffectPrim p = draw(l)[0];
    SH_CHECK(p.x[2] == p.x[0] + 1 && p.y[2] == p.y[0] + 1 && p.texture.palette_y == 104 + (p.slot >> 5));
}
