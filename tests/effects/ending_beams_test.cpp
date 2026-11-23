// The final fight's beams and thunder pacing (src/engine/effects/ending_beams.h): the mode machine's pattern
// changes, the fans' easing and cells, the map's own generator and the thunder slots.

#include "engine/effects/ending_beams.h"

#include "framework/sh_test.h"

#include <vector>

namespace {

namespace fx = sh::effects;

class FakeHost final : public fx::EndingBeamsHost {
public:
    fx::BeamsState state;
    std::vector<fx::BeamPattern> patterns = std::vector<fx::BeamPattern>(18);
    std::array<std::int32_t, 9> curve_{4096, 4096, 4096, 4096, 4096, 4096, 4096, 4096, 4096};
    std::int32_t dt = 136;
    int cells = 0;
    int blends = 0;
    int strikes = 0;
    int attaches = 0;
    int bone_tracks = 0;

    void load(fx::BeamsState& s) override { s = state; }
    void store(const fx::BeamsState& s) override { state = s; }
    fx::BeamPattern pattern(int index) override { return patterns[static_cast<std::size_t>(index)]; }
    const std::array<std::int32_t, 9>& curve() override { return curve_; }
    std::int32_t delta_time() override { return dt; }
    std::int32_t sin(std::int32_t) override { return 0; }
    std::int32_t cos(std::int32_t) override { return 4096; }
    fx::Vec3 npc_position(int) override { return {}; }
    void track_boss_bone() override { bone_tracks++; }
    fx::Vec3 strike_target() override { return {}; }
    void bolts_attach_ending() override { attaches++; }
    void bolts_update() override {}
    void bolts_strike(const fx::Vec3&, const fx::Vec3&, std::int32_t, std::int32_t) override { strikes++; }
    void begin_projection() override {}
    fx::ScreenPoint project(const fx::Vec3& at) override {
        return {static_cast<std::int16_t>(at.x), static_cast<std::int16_t>(at.z), 500};
    }
    void emit_cell(const sh::draw3d::Polygon& cell, std::int32_t bucket) override {
        SH_CHECK(cell.gouraud && bucket == 1);
        cells++;
    }
    void emit_blend(std::int32_t bucket, std::int32_t blend) override {
        SH_CHECK(bucket == 1 && blend == 1);
        blends++;
    }
};

fx::BeamFan lit_fan() {
    fx::BeamFan f;
    f.far = {4096, 0, 8192};
    f.width = 4096;
    f.intensity = 4096;
    f.cap = 200;
    f.width_speed_max = 4096;
    f.near_speed_max = 4096;
    f.far_speed_max = 4096;
    return f;
}

}  // namespace

SH_TEST(ending_beams_init_and_modes) {
    FakeHost host;
    fx::EndingBeams::init(host);
    SH_CHECK(host.state.mode == 0 && host.state.color[0] == 250 && host.state.color[2] == 150);
    // Mode 0 snaps to pattern 0 every frame; nothing lit draws nothing.
    fx::EndingBeams::update(host);
    SH_CHECK(host.state.target == 0 && host.cells == 0 && host.state.timer == 136);
    // Mode 3: pattern 3, then 4 after 0.6 s.
    fx::EndingBeams::set_mode(host, 3);
    SH_CHECK(host.state.step == 0 && host.state.timer == 0);
    fx::EndingBeams::update(host);
    SH_CHECK(host.state.target == 3 && host.state.step == 1);
    for (int i = 0; i < 20; i++) fx::EndingBeams::update(host);
    SH_CHECK(host.state.target == 4 && host.state.step == 2);
    // Mode 8 follows the boss bone every frame.
    fx::EndingBeams::set_mode(host, 8);
    fx::EndingBeams::update(host);
    SH_CHECK(host.bone_tracks == 1);
    // -1: no beams at all.
    fx::EndingBeams::set_mode(host, -1);
    const std::int32_t timer = host.state.timer;
    fx::EndingBeams::update(host);
    SH_CHECK(host.state.timer == timer);
}

SH_TEST(ending_beams_snap_draws_two_fans) {
    FakeHost host;
    host.patterns[1].snap = true;
    host.patterns[1].fans[0] = lit_fan();
    host.patterns[1].fans[1] = lit_fan();
    fx::EndingBeams::init(host);
    fx::EndingBeams::set_mode(host, 1);
    fx::EndingBeams::update(host);
    // 5 x 15 cells and one blend mode per fan.
    SH_CHECK(host.cells == 150 && host.blends == 2);
}

SH_TEST(ending_beams_strikes_use_the_maps_generator) {
    FakeHost host;
    fx::EndingBeams::init(host);
    fx::EndingBeams::set_mode(host, 10);
    fx::EndingBeams::update(host);
    // Attached once; the first strike of a 1.0 chance always lands.
    SH_CHECK(host.attaches == 1 && host.strikes >= 1);
    SH_CHECK(host.state.random_seed != 0);
    // Without time the generator stands still.
    const std::int32_t seed = host.state.random_seed;
    host.dt = 0;
    fx::EndingBeams::update(host);
    SH_CHECK(host.state.random_seed == seed);
}

SH_TEST(ending_thunder_pacing) {
    fx::ThunderPacing t;
    std::uint32_t seed = 3;
    const auto rand16 = [&] {
        seed = seed * 1103515245u + 12345u;
        return static_cast<std::int32_t>((seed >> 16) & 0x7FFF);
    };
    // The first sounds take free slots; with every slot freshly busy nothing more plays.
    int played = 0;
    for (int i = 0; i < 8; i++) played += t.take(rand16);
    SH_CHECK(played >= 1 && played <= 6);
    // Two seconds later every slot is free again.
    t.tick(8192);
    SH_CHECK(t.take(rand16));
    t.reset();
    SH_CHECK(t.take(rand16));
}
