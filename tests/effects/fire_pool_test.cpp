// SPDX-License-Identifier: GPL-3.0-or-later
// Unit tests for the fire pools (sh::effects::FirePool) with a scripted host.

#include "engine/effects/fire_pool.h"

#include <string>
#include <vector>

#include "framework/sh_test.h"

using namespace sh::effects;

namespace {

class Host final : public FirePoolHost {
public:
    std::vector<std::int32_t> rolls;  // rand16() results, then 0
    std::size_t next = 0;
    std::int32_t dt = 4096 / 30;
    std::int32_t ground = 1 << 20;
    std::int32_t depth = 512;
    std::vector<FireQuad> quads;
    std::vector<std::int32_t> sounds;
    int views = 0;

    std::int32_t rand16() override { return next < rolls.size() ? rolls[next++] : 0; }
    void reseed(std::uint32_t) override {}
    std::int32_t sin(std::int32_t) override { return 0; }
    std::int32_t cos(std::int32_t) override { return 4096; }
    std::int32_t delta_time() override { return dt; }
    std::int32_t geom_screen() override { return 512; }
    void begin_view(const Vec3&) override { views++; }
    FireProjection project(const Vec3s&) override { return FireProjection{0, 0, depth, false, false}; }
    FireRolledProjection project_rolled(const Vec3s&, std::int32_t, const std::array<Vec3s, 4>&) override {
        FireRolledProjection r;
        r.depth = depth;
        return r;
    }
    std::int32_t ground_height(std::int32_t, std::int32_t) override { return ground; }
    bool sound_allowed() override { return true; }
    void play_sound(std::int32_t id, const Vec3&, std::int32_t) override { sounds.push_back(id); }
    Vec3 bone_position(int bone) override { return Vec3{bone * 4096, 0, 0}; }
    void emit_sprite(const FireQuad& q) override { quads.push_back(q); }
};

const char* kPool = R"({"pools": {"P": {
    "capacity": 4, "attached_capacity": 3, "origin": [0, 0, 0], "hold_rate": 30, "cull": [-160, 160, -160, 112],
    "sprites": [[0, 0, 32, 64, 56, 6, 16, 64], [32, 0, 32, 64, 56, 6, 16, 64]],
    "animations": {
        "Loop": [[1, 0], [1, 1], {"loop": 0}],
        "Once": [[0, 0], [0, 1], {"stop": true}]
    },
    "ember": {"animation": "Once", "speed": 4096, "scale": 8192, "damping": 3276},
    "tongue": {"animation": "Once", "lift": -1024, "rise": -4096},
    "spark": {"animation": "Once", "rise": 4096},
    "trail": {"animation": "Once", "speed_divisor": 41, "speed_base": 1228},
    "burn": {"animation": "Loop", "start_divisor": 16384, "sound": "finale_fire_burn", "volume": 255,
             "tongue_sprite": 1, "fade_time": 2867, "fade": 3686},
    "fireball": {"animation": "Loop", "ceiling": 20480, "embers": 2, "land_time": 20480, "burn_time": 20480,
                 "burn_scale": 8192, "trail_sprite": 1},
    "flash": {"wait": 8, "fade": 10},
    "glow": {"animation": "Once", "alpha": 30, "delay": 0, "scale": 10240},
    "burst": {"animations": ["Once"], "alpha": 128, "pick_divisor": 10923, "count_mask": 80, "count_if_set": 2,
              "count_if_clear": 3, "delays": [0, 819, 1638], "scales": [8192, 12288, 10240]},
    "cue": {"tilt": 170, "tilt_speed": 20480, "area": 25734, "brightness": 128, "brightness_top": 40960,
            "brightness_divisor": 320, "reseed_above": 50},
    "attached": {"animation": "Loop", "start_divisor": 16384, "bone_first": 1, "bone_last": 2,
                 "scale_divisor": 16, "scale_base": 2048, "alpha": 96}
}}})";

FirePool make() {
    auto parsed = parse_fire_pools(kPool);
    SH_CHECK(static_cast<bool>(parsed));
    return FirePool(parsed.value().at("P"));
}

}  // namespace

SH_TEST(fire_pool_rejects_bad_data) {
    SH_CHECK(static_cast<bool>(parse_fire_pools(kPool)));
    std::string s = kPool;
    // An animation must end with a loop or a stop.
    std::string bad = s;
    bad.replace(bad.find(R"({"stop": true})"), 14, "[0, 1]");
    SH_CHECK(!parse_fire_pools(bad));
    // Unknown keys are refused.
    bad = s;
    bad.replace(bad.find(R"("hold_rate")"), 11, R"("hold_rates")");
    SH_CHECK(!parse_fire_pools(bad));
    // A missing animation name is refused.
    bad = s;
    bad.replace(bad.find(R"("animation": "Once", "rise")"), 19, R"("animation": "None")");
    SH_CHECK(!parse_fire_pools(bad));
}

SH_TEST(fire_pool_is_empty_until_reset) {
    FirePool f = make();
    Host h;
    f.glow(h, Vec3{0, 0, 0}, 1, 0, 4096);
    SH_CHECK(f.particles().empty());
    f.reset();
    SH_CHECK(f.particles().size() == 4);
    f.glow(h, Vec3{0, 0, 0}, 1, 0, 4096);
    SH_CHECK(f.particles()[0].pending && f.particles()[0].palette == 64);
}

SH_TEST(fire_pool_flash_waits_then_plays_once) {
    FirePool f = make();
    Host h;
    f.reset();
    f.glow(h, Vec3{0, 0, 0}, 0, 0, 4096);
    f.update(h);  // activated: the wait starts (state 1, 8 frames)
    SH_CHECK(f.particles()[0].active && f.particles()[0].state == 1);
    SH_CHECK(h.quads.empty());
    f.update(h);  // step 0 shows sprite 0, moves to step 1
    SH_CHECK(h.quads.size() == 1);
    SH_CHECK(h.quads[0].color == 30u * 0x10101u);
    // w = 32 * 512 / 4 / 512 = 8 texels scaled by 1.0; the anchor (16, 64) moves the corner.
    SH_CHECK(h.quads[0].x[0] == -4 && h.quads[0].x[1] == 4 && h.quads[0].y[0] == -16);
    SH_CHECK(h.quads[0].slot == 256);
    f.update(h);  // step 1 shows sprite 1 and reaches the stop: the last sprite is still drawn
    SH_CHECK(h.quads.size() == 2 && h.quads[1].u[0] == 32);
    SH_CHECK(!f.particles()[0].active);
}

SH_TEST(fire_pool_burn_sounds_and_attached_flames) {
    FirePool f = make();
    Host h;
    f.reset();
    FireCue c;
    c.kind = 1;
    c.shape = 1;
    c.near = 0;
    c.far = 0;
    c.rate = 4096;
    c.flags = 2;
    c.lead_min = 4096;
    c.lead_max = 4096;
    // area 0: the count is 0 unless the roll is below the fraction; nothing spawns.
    f.cue(h, c);
    SH_CHECK(!f.particles()[0].pending);
    c.near = 4096;
    c.far = 4096 * 2;  // area 2, times rate 1, lead span 0 -> times the frame time
    h.dt = 4096;
    f.cue(h, c);
    SH_CHECK(f.particles()[0].pending && f.particles()[1].pending && !f.particles()[2].pending);
    SH_CHECK(f.particles()[0].update == FireUpdate::Burn);
    f.update(h);
    SH_CHECK(h.sounds.size() == 2 && h.sounds[0] == 1692);

    f.attached_reset();
    h.rolls = {0, 0, 0, 0};
    f.attach(h);
    SH_CHECK(f.attached()[0].active && f.attached()[1].active && !f.attached()[2].active);
    SH_CHECK(f.attached()[1].bone == 2 && f.attached()[0].scale == 2048 && f.attached()[0].alpha == 96);
    h.quads.clear();
    f.draw_attached(h);
    SH_CHECK(h.quads.size() == 2);
}

SH_TEST(fire_pool_paused_stop_shows_its_sprite) {
    // A paused pool keeps a particle on its stop; the stop's own sprite (the original's link read
    // as a sprite) is drawn every frame, and nothing without one.
    std::string s = kPool;
    s.replace(s.find(R"({"stop": true})"), 14, R"({"stop": true, "sprite": 2})");
    s.replace(s.find(R"([32, 0, 32, 64, 56, 6, 16, 64]])"), 31,
              R"([32, 0, 32, 64, 56, 6, 16, 64], [0, 0, -16072, -32754, 0, 0, 0, 0]])");
    for (int with_sprite = 0; with_sprite < 2; with_sprite++) {
        auto parsed = parse_fire_pools(with_sprite != 0 ? s : std::string(kPool));
        SH_CHECK(static_cast<bool>(parsed));
        FirePool f(parsed.value().at("P"));
        Host h;
        h.depth = 1 << 15;  // far enough for the oversized link sprite to pass the cull
        f.reset();
        f.set_paused(true);
        f.glow(h, Vec3{0, 0, 0}, 0, 0, 4096);
        f.update(h);  // step 0
        f.update(h);  // step 1, reaches the stop
        SH_CHECK(h.quads.size() == 2);
        f.update(h);
        f.update(h);
        SH_CHECK(f.particles()[0].active && f.particles()[0].step == 2);
        SH_CHECK(h.quads.size() == (with_sprite != 0 ? 4u : 2u));
        if (with_sprite != 0) {
            SH_CHECK(h.quads[2].tpage == 0 && h.quads[2].u[1] == 0x38 && h.quads[2].v[2] == 0x0E);
        }
        // Resumed, the flash first waits (state 0 to 1), then still shows the stop once and ends.
        f.set_paused(false);
        f.update(h);
        SH_CHECK(f.particles()[0].active && h.quads.size() == (with_sprite != 0 ? 4u : 2u));
        f.update(h);
        SH_CHECK(!f.particles()[0].active);
        SH_CHECK(h.quads.size() == (with_sprite != 0 ? 5u : 2u));
    }
    std::string bad = kPool;
    bad.replace(bad.find(R"({"loop": 0})"), 11, R"({"loop": 0, "sprite": 1})");
    SH_CHECK(!parse_fire_pools(bad));
}
