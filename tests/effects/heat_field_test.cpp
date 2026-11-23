// SPDX-License-Identifier: GPL-3.0-or-later
// Unit tests for the heat fields (sh::effects::HeatField) with a scripted host.

#include "engine/effects/heat_field.h"

#include <string>
#include <vector>

#include "framework/sh_test.h"

using namespace sh::effects;

namespace {

class Host final : public HeatFieldHost {
public:
    std::int32_t seed = 12345;
    std::int32_t dt = 68;
    std::vector<SplashQuad> quads;
    std::vector<std::int32_t> slots;
    int modes = 0;
    std::int32_t mode_slot = 0;
    int rolls = 0;

    std::int32_t rand16() override {
        rolls++;
        seed = (seed * 1103515245 + 12345) & 0x7FFFFFFF;
        return seed >> 16;
    }
    std::int32_t sin(std::int32_t) override { return 2048; }
    std::int32_t cos(std::int32_t) override { return 2048; }
    std::int32_t atan2(std::int32_t, std::int32_t) override { return 512; }
    std::int32_t delta_time() override { return dt; }
    void begin_view(const Vec3&, std::int32_t) override {}
    ScreenPoint project_origin() override { return ScreenPoint{10, 20, 400}; }
    ScreenPoint project(const Vec3s& v) override {
        return ScreenPoint{static_cast<std::int16_t>(10 + v.x), static_cast<std::int16_t>(20 + v.y), 400};
    }
    void emit_quad(const SplashQuad& q, std::int32_t slot) override {
        quads.push_back(q);
        slots.push_back(slot);
    }
    void emit_mode(std::int32_t slot, std::int32_t, std::int32_t, std::int32_t) override {
        modes++;
        mode_slot = slot;
    }
};

std::string field_json(const std::string& body) {
    return R"({"fields": {"F": {)" + body +
           R"(, "smooth_loss": 1, "quiet_color": 973078528, "prim_code": 58, "mode_tpage": 42, "mode_dfe": 0,
               "mode_dtd": 1, "shade": [973078528, 973078529]}}})";
}

const char* kFan =
    R"("emitter": "fan", "cols": 41, "rows": 31, "cell": 8, "follow_origin": false, "stamp": [160, 120],
       "draw": [-168, -120], "origin_offset": [0, -332, 0], "lifetime": 6144, "hold_on_still_frame": true,
       "particles": 100, "start_brightness": 255, "brightness_decay": 8, "project_shift": 7)";

const char* kCloud =
    R"("emitter": "cloud", "cols": 41, "rows": 25, "cell": 8, "follow_origin": false, "stamp": [164, 100],
       "draw": [-164, -100], "depth_add": 320, "slot_add": 250, "slot_limit": 1798, "slot_far": 2036,
       "row_slot_step": 10, "particles": 200, "start_brightness": 4095, "brightness_decay": 4096, "stamp_shift": 4,
       "project_shift": 4, "speed": 2457, "spread": 4096, "spread_push": 4, "life": 4096)";

const char* kGather =
    R"("emitter": "gather", "cols": 41, "rows": 25, "cell": 8, "follow_origin": false, "stamp": [160, 96],
       "draw": [-160, -96], "slot_limit": -16777216, "slot_far": 0, "particles": 4, "start_brightness": 1044480,
       "brightness_decay": 0, "stamp_shift": 12, "project_shift": 4, "speed": 16384, "radius": 12288)";

HeatField make(const char* body) {
    auto parsed = parse_heat_fields(field_json(body));
    SH_CHECK(static_cast<bool>(parsed));
    return HeatField(parsed.value().at("F"));
}

}  // namespace

SH_TEST(heat_field_rejects_unknown_and_foreign_keys) {
    SH_CHECK(!parse_heat_fields(field_json(std::string(kFan) + R"(, "speed": 1)")));
    SH_CHECK(!parse_heat_fields(field_json(std::string(kFan) + R"(, "colour": 1)")));
    SH_CHECK(!parse_heat_fields(R"({"fields": {"F": {"emitter": "rain"}}})"));
    SH_CHECK(static_cast<bool>(parse_heat_fields(field_json(kFan))));
}

SH_TEST(heat_field_fan_shows_for_its_lifetime) {
    HeatField f = make(kFan);
    Host h;
    f.update(h);
    SH_CHECK(h.modes == 0);  // not armed
    f.arm(Vec3{4096, 8192, 0}, 100);
    SH_CHECK(f.state().origin_q8.y == 512 - 332);
    f.update(h);
    SH_CHECK(h.modes == 1);
    SH_CHECK(h.mode_slot == 200);  // depth 400 >> 1
    SH_CHECK(f.state().timer == 2 + 68);
    int frames = 1;
    while (f.state().timer != 0 && frames < 1000) {
        f.update(h);
        frames++;
    }
    SH_CHECK(frames == 1 + (6144 - 2) / 68);
    // A still frame draws without stepping or rolling.
    f.arm(Vec3{}, 0);
    h.dt = 0;
    std::size_t before = h.quads.size();
    f.update(h);
    const std::size_t first = h.quads.size() - before;
    const int rolls = h.rolls;
    before = h.quads.size();
    f.update(h);
    SH_CHECK(h.rolls == rolls);
    SH_CHECK(h.quads.size() - before == first);
}

SH_TEST(heat_field_cloud_spawns_lives_and_slides_rows) {
    HeatField f = make(kCloud);
    Host h;
    f.start(h, Vec3{16, 32, 48});
    SH_CHECK(f.state().origin_q8.x == 1);
    f.spawn(h, Vec3{}, 30);
    int active = 0;
    for (const HeatParticle& p : f.state().particles) {
        active += p.active ? 1 : 0;
    }
    SH_CHECK(active == 30);
    SH_CHECK(h.rolls == 30 * 6);
    f.update(h);
    // (400 + 320) >> 1 = 360, + 250; 24 rows move it by -10 each.
    SH_CHECK(h.mode_slot == 610 - 240);
    for (int i = 0; i < 80; i++) {
        f.update(h);
    }
    active = 0;
    for (const HeatParticle& p : f.state().particles) {
        active += p.active ? 1 : 0;
    }
    SH_CHECK(active == 0);  // a second of life at 68 per frame
    f.update(h, false);
    SH_CHECK(h.modes == 82);
}

SH_TEST(heat_field_gather_flies_in_rests_and_goes) {
    HeatField f = make(kGather);
    Host h;
    f.start(h, Vec3{8192, -1228, -124928});
    SH_CHECK(f.state().origin_q8.x == 512);
    // Six places rolled, only four slots: the last two roll but find no room.
    f.spawn(h, Vec3{}, 6);
    SH_CHECK(h.rolls == 12);
    const HeatParticle& p = f.state().particles[0];
    SH_CHECK(p.active && !p.landed);
    // sin = cos = 0.5: the start is (0.25, 0.5, 0.25) * 3 m; the test host's normalize keeps the
    // way as it is, so the speed scales it.
    SH_CHECK(p.pos.x == 3072 && p.pos.y == 6144 && p.pos.z == 3072);
    SH_CHECK(p.vel[0] == -12288 && p.vel[1] == -24576);
    const std::int32_t life = p.life;
    SH_CHECK(life > 0);
    int frames = 0;
    while (f.state().particles[0].active && frames < 1000) {
        f.update(h);
        frames++;
    }
    // Flies until its life is spent, rests one frame (stopped), goes on the next.
    SH_CHECK(frames == (life + h.dt - 1) / h.dt + 2 || frames == life / h.dt + 2);
    SH_CHECK(h.mode_slot == 0);
    f.clear();
    SH_CHECK(!f.state().particles[1].active);
}

SH_TEST(heat_field_shipped_file_loads) {
    std::string source;
    auto loaded = load_heat_fields(std::string(SH_PROJECT_ROOT) + "/gamedata", "", &source);
    SH_CHECK(static_cast<bool>(loaded));
    if (loaded) {
        for (const char* name : {"EndingBurst", "BottleShatter", "BossLightning", "ShockSparks", "DahliaPortal"}) {
            SH_CHECK(loaded.value().count(name) == 1);
        }
    }
}
