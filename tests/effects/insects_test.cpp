// The insects of the first map (src/engine/effects/insects.h): the shipped file, the start, the
// flies' and the crawlers' modes, the sprites and the flashlight's footprint.

#include "engine/effects/insects.h"

#include "framework/sh_test.h"

#include <cmath>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace {

namespace fx = sh::effects;

fx::InsectConfig shipped() {
    std::ifstream in(std::string(SH_PROJECT_ROOT) + "/gamedata/effects/map0_s00_effect.json", std::ios::binary);
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    sh::Result<fx::InsectConfig> r = fx::parse_insect_config(text);
    if (!r) {
        std::fprintf(stderr, "map0_s00_effect.json: %s\n", r.error().c_str());
    }
    SH_CHECK(r);
    return r ? r.value() : fx::InsectConfig{};
}

class FakeHost final : public fx::InsectHost {
public:
    std::vector<std::int32_t> randoms{12345};
    std::size_t drawn = 0;
    std::int32_t dt = 136;
    bool is_paused = false;
    bool flash = false;
    bool hand = false;
    bool real_trig = false;
    std::int32_t mode = 0;
    std::int32_t counter = 0;
    fx::Vec3 harry{};
    fx::Vec3 hand_pos{};
    std::int32_t cone = 0;
    fx::Vec3 cone_at{};
    int weather = 0;
    int views = 0;
    std::vector<fx::EffectPrim> prims;
    std::int32_t last_darkness = 0;
    fx::Vec3 darkness_at{};

    std::int32_t rand16() override { return randoms[drawn++ % randoms.size()]; }
    std::int32_t sin(std::int32_t a) override {
        return real_trig ? static_cast<std::int32_t>(std::lround(4096 * std::sin(a * 6.283185307179586 / 4096)))
                         : (a == 1024 ? 4096 : (a == 0 ? 0 : 1000));
    }
    std::int32_t cos(std::int32_t a) override {
        return real_trig ? static_cast<std::int32_t>(std::lround(4096 * std::cos(a * 6.283185307179586 / 4096)))
                         : (a == 1024 ? 0 : 4096);
    }
    std::int32_t atan2(std::int32_t a, std::int32_t b) override { return a == 0 && b == 0 ? 0 : 1024; }
    std::int32_t sqrt(std::int32_t v) override {
        std::int32_t r = 0;
        while ((r + 1) * (r + 1) <= v) {
            r++;
        }
        return r;
    }
    std::int32_t delta_time() override { return dt; }
    bool paused() override { return is_paused; }
    std::int32_t frame_counter() override { return counter; }
    fx::Vec3 player() override { return harry; }
    bool hand_follows() override { return hand; }
    fx::Vec3 hand_q8() override { return hand_pos; }
    bool flashlight_on() override { return flash; }
    std::int32_t light_mode() override { return mode; }
    fx::Rgb light_color() override { return fx::Rgb{10, 20, 30}; }
    std::int32_t darkness(const fx::Vec3& at) override {
        darkness_at = at;
        return last_darkness;
    }
    void start_weather() override { weather++; }
    void begin_view() override { views++; }
    fx::ScreenPoint project(const fx::Vec3s& r) override { return fx::ScreenPoint{r.x, r.z, 64 + r.y}; }
    std::int32_t cone_size() override { return cone; }
    fx::Vec3 cone_origin() override { return cone_at; }
    void cone_begin() override {}
    fx::Vec3s cone_rotate(const fx::Vec3s& c) override { return c; }
    void emit(const fx::EffectPrim& p) override { prims.push_back(p); }
};

}  // namespace

SH_TEST(insects_shipped_file) {
    const fx::InsectConfig c = shipped();
    SH_CHECK(c.anchors[0].x == -1056768 && c.anchors[0].y == -5734 && c.anchors[0].z == 999833);
    SH_CHECK(c.anchors[3].x == -1026048 && c.anchors[3].z == 891699);
    SH_CHECK(c.fly_count == 20 && c.crawler_count == 60 && c.crawlers_per_group == 20);
    SH_CHECK(c.fly_spread == 10240 && c.fly_height == 16384 && c.crawler_spread == 2048);
    SH_CHECK(c.active_distance == 163840 && c.startle_radius_q8 == 256 && c.dart_chance == 65);
    SH_CHECK(c.look.flies.texture.page == 44 && c.look.flies.texture.palette_x == 192);
    SH_CHECK(c.look.crawlers.texture.page == 76 && c.look.crawlers.texture.palette_y == 1);
    SH_CHECK(c.look.crawlers.u_base == 64 && c.look.crawlers.min_slot == 32 && c.look.crawler_brightness == 128);
    SH_CHECK(c.look.full_darkness_modes.size() == 3 && c.look.plain_modes.size() == 3);
    SH_CHECK(!fx::parse_insect_config("{}"));
}

SH_TEST(insects_start_order_of_draws) {
    fx::InsectSwarms swarms(shipped());
    FakeHost host;
    host.randoms = {100, 200, 300, 13333};
    const int mode = swarms.start(host);
    SH_CHECK(mode == 2 && host.weather == 1);
    // 60 crawlers draw x then z, then 20 flies draw x, y, z.
    SH_CHECK(host.drawn == 60 * 2 + 20 * 3);
    const fx::Crawler& c0 = swarms.crawlers()[0];
    SH_CHECK(c0.pos.x == swarms.config().anchors[1].x + 100 % 4096 - 2048);
    SH_CHECK(c0.pos.z == swarms.config().anchors[1].z + 200 % 4096 - 2048);
    SH_CHECK(c0.pos.y == swarms.config().anchors[1].y && c0.mode == 1);
    SH_CHECK(swarms.crawlers()[59].pos.y == swarms.config().anchors[3].y);
    // The 121st draw is the first fly's x.
    const fx::Fly& f0 = swarms.flies()[0];
    const std::int32_t d120 = host.randoms[120 % host.randoms.size()];
    SH_CHECK(f0.pos.x == swarms.config().anchors[0].x + d120 % 20480 - 10240);
    SH_CHECK(f0.pos.y <= swarms.config().anchors[0].y && f0.mode == 1);
}

SH_TEST(insects_fly_modes) {
    fx::InsectSwarms swarms(shipped());
    swarms.crawlers().clear();
    swarms.flies().assign(4, fx::Fly{});
    FakeHost host;
    host.randoms = {4};  // a jitter of 0, a threshold of 128 + 4
    fx::Fly& f = swarms.flies()[3];
    f.mode = 1;
    f.timer = 7;
    f.pos = {1000, 0, 2000};
    host.harry = swarms.config().anchors[0];
    swarms.update(host, 1);
    // Mode 1, index 3: vx += sin((7+3)<<5 = 320) * 40 >> 12 = 1000 * 40 >> 12 = 9; vz += 4096 * 40 >> 12 = 40.
    SH_CHECK(f.vx == 9 && f.vz == 40 && f.vy == 0);
    SH_CHECK(f.timer == 8 && f.mode == 1);
    SH_CHECK(f.pos.x == 1000);  // it moved before its speed changed
    swarms.update(host, 1);
    SH_CHECK(f.pos.x == 1000 + 9 && f.pos.z == 2000 + 40);
    // The dart: a draw 64 modulo 65 sends every fly to mode 5.
    FakeHost dart;
    dart.randoms = {64};
    dart.harry = swarms.config().anchors[0];
    swarms.update(dart, 1);
    SH_CHECK(f.mode == 5 && f.timer == 0);
}

SH_TEST(insects_fly_leaves_mode_when_timer_passes) {
    fx::InsectSwarms swarms(shipped());
    FakeHost host;
    host.randoms = {1, 2, 3};
    host.harry = swarms.config().anchors[0];
    swarms.flies().assign(1, fx::Fly{});
    swarms.crawlers().clear();
    fx::Fly& f = swarms.flies()[0];
    f.mode = 3;
    f.timer = 200;
    swarms.update(host, 1);
    // Mode 3: the threshold 100 + x is below 200, so the mode is picked again (1..3) and the timer
    // starts over.
    SH_CHECK(f.timer == 0 && f.mode >= 1 && f.mode <= 3);
}

SH_TEST(insects_crawler_flees_from_harry) {
    fx::InsectSwarms swarms(shipped());
    FakeHost host;
    host.randoms = {1};
    swarms.flies().clear();
    swarms.crawlers().assign(1, fx::Crawler{});
    fx::Crawler& c = swarms.crawlers()[0];
    c.mode = 1;
    c.pos = swarms.config().anchors[1];
    host.harry = swarms.config().anchors[1];
    host.harry.x += 2000;  // within a metre
    swarms.update(host, 2);
    // Mode 1 startled: the draw 1 % 2 != 0 -> flee (mode 5, timer 0).
    SH_CHECK(c.mode == 5 && c.timer == 0);
    swarms.update(host, 2);
    // Next frame: the heading from the fake atan2 is 1024, sin 4096, cos 0: vx = 4096 * 3 / 16 = 768 -> 64.
    SH_CHECK(c.vx == 64 && c.vz == 0 && c.timer == 1);
    // Far from Harry (mode 2) the crawler does not see him.
    FakeHost calm;
    calm.randoms = {1};
    calm.harry = swarms.config().anchors[1];
    calm.harry.x += 100000;
    c.mode = 4;
    swarms.update(calm, 2);
    SH_CHECK(c.mode == 4);
}

SH_TEST(insects_mode_gating) {
    fx::InsectSwarms swarms(shipped());
    FakeHost host;
    host.harry = {0, 0, 0};  // far from both anchors
    swarms.update(host, 2);
    SH_CHECK(host.drawn == 0 && host.views == 0 && host.prims.empty());
    swarms.update(host, 0);
    SH_CHECK(host.drawn == 0 && host.views == 0);
    swarms.update(host, 1);  // mode 1 runs everywhere
    SH_CHECK(host.views == 1);
    // Paused: nothing moves but every fly still draws its dart chance.
    FakeHost held;
    held.is_paused = true;
    held.randoms = {3};
    held.harry = swarms.config().anchors[0];
    const fx::Fly before = swarms.flies()[0];
    swarms.update(held, 2);
    SH_CHECK(held.drawn == swarms.flies().size() && swarms.flies()[0].pos.x == before.pos.x);
}

SH_TEST(insects_sprites) {
    fx::InsectSwarms swarms(shipped());
    FakeHost host;
    host.randoms = {5};
    host.harry = swarms.config().anchors[0];
    swarms.flies().clear();
    swarms.crawlers().assign(2, fx::Crawler{});
    swarms.crawlers()[0].mode = 9;  // stands still
    swarms.crawlers()[1].mode = 9;
    swarms.crawlers()[0].pos = {0, 16 * 100, 0};  // fake depth 64 + 100
    swarms.crawlers()[1].pos = {0, 16 * 10, 0};   // fake depth 74
    host.counter = 2;                             // the second step of the cycle
    swarms.update(host, 1);
    SH_CHECK(host.prims.size() == 2);
    const fx::EffectPrim& p = host.prims[0];
    // Depth 164: slot 82, steps 164 >> 4 = 10, a side of 16 - 10 = 6.
    SH_CHECK(p.slot == 82 && p.x[2] - p.x[0] == 6 && p.y[1] - p.y[0] == 6);
    SH_CHECK(p.semi_transparent && p.rgb[0].r == 128 && p.texture.page == 76);
    // Cell (0 + step 1) % 4 = 1: u 80..96 of the sheet that starts at 64, v 240..255.
    SH_CHECK(p.u[0] == 80 && p.u[2] == 96 && p.v[0] == 240 && p.v[1] == 255);
    SH_CHECK(host.prims[1].u[0] == 96);  // the second crawler is one cell on
}

SH_TEST(insects_fly_colour_follows_light_mode) {
    fx::InsectSwarms swarms(shipped());
    swarms.crawlers().clear();
    swarms.flies().assign(1, fx::Fly{});
    swarms.flies()[0].mode = 9;
    swarms.flies()[0].pos = {0, 16 * 100, 0};
    const auto colour = [&](bool flash, std::int32_t mode, std::int32_t dark) {
        FakeHost host;
        host.randoms = {5};
        host.harry = swarms.config().anchors[0];
        host.flash = flash;
        host.mode = mode;
        host.last_darkness = dark;
        swarms.update(host, 1);
        return host.prims.empty() ? fx::Rgb{} : host.prims[0].rgb[0];
    };
    SH_CHECK(colour(true, 1, 100).r == 110 && colour(true, 1, 255).g == 176 + 20);  // capped at 176
    SH_CHECK(colour(true, 1, 0).b == 31);                                          // darkness 0 counts 1
    SH_CHECK(colour(true, 2, 160).r == 10 + (160 >> 3));
    SH_CHECK(colour(true, 7, 160).r == 10);
    SH_CHECK(colour(false, 2, 160).r == 10 && colour(false, 4, 100).r == 110);
    // The darkness is read at Harry's place plus the fly's offset.
    FakeHost host;
    host.randoms = {5};
    host.harry = {1000, 2000, 3000};
    host.flash = true;
    host.last_darkness = 50;
    swarms.update(host, 1);
    SH_CHECK(host.darkness_at.x == 1000 + swarms.flies()[0].pos.x && host.darkness_at.z == 3000);
}

SH_TEST(insects_outline_of_points) {
    const fx::ConeCorner triangle[3] = {{0, 0}, {5, 8}, {10, 0}};
    SH_CHECK(fx::convex_outline(triangle, 3) == 3);
    const fx::ConeCorner square[4] = {{0, 0}, {0, 10}, {10, 0}, {10, 10}};
    SH_CHECK(fx::convex_outline(square, 4) == 4);
    // No points: the ring of three zeros.
    SH_CHECK(fx::convex_outline(nullptr, 0) == 3);
}

SH_TEST(insects_footprint_of_the_cone) {
    fx::InsectSwarms swarms(shipped());
    swarms.flies().clear();
    swarms.crawlers().clear();
    FakeHost host;
    host.harry = swarms.config().anchors[0];
    host.cone = 0;
    swarms.update(host, 1);
    SH_CHECK(swarms.footprint_corners() == 0);  // a cone of no size never meets the ground
    host.cone = 4096 * 20;
    host.real_trig = true;
    host.cone_at = host.harry;  // the cone starts at Harry
    swarms.update(host, 1);
    // Each hexagon's ring crosses the plane in two places: four cuts, an outline of four or less.
    SH_CHECK(swarms.footprint_corners() >= 3 && swarms.footprint_corners() <= 4);
}
