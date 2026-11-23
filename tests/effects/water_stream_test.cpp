// SPDX-License-Identifier: GPL-3.0-or-later
// Unit tests for the drain's water (sh::effects::WaterStream) with a scripted host.

#include "engine/effects/water_stream.h"

#include <vector>

#include "framework/sh_test.h"

using namespace sh::effects;

namespace {

class Host final : public WaterStreamHost {
public:
    std::vector<std::int32_t> rng{5, 7, 1, 2, 3, 4};
    std::size_t at = 0;
    std::vector<WaterDrop> slots = std::vector<WaterDrop>(8);
    int allocated = 0;
    bool running = false;
    WaterSegmentTables seg;
    std::vector<DropQuad> drops;
    std::vector<RibbonQuad> ribbons;

    std::int32_t next() { return at < rng.size() ? rng[at++] : 0; }
    std::int32_t rand16() override { return next(); }
    std::int32_t rand_uint(std::int32_t low, std::int32_t high) override { return low + next() % (high - low + 1); }
    std::int32_t sin(std::int32_t) override { return 0; }
    std::int32_t cos(std::int32_t) override { return 4096; }
    std::int32_t delta_time() override { return 136; }
    std::int32_t gravity() override { return 4096; }
    Vec3 view_origin() override { return Vec3{0, 0, 0}; }
    std::int32_t view_yaw() override { return 0; }
    const WaterSegmentTables& segments() override { return seg; }
    std::int32_t darkness(std::int32_t, std::int32_t, std::int32_t) override { return 128; }
    void project3(const Vec3s& a, const Vec3s& b, const Vec3s& c, ScreenPoint out[3]) override {
        out[0] = ScreenPoint{a.x, a.y, 400};
        out[1] = ScreenPoint{b.x, b.y, 400};
        out[2] = ScreenPoint{c.x, c.y, 400};
    }
    ScreenPoint project(const Vec3s& v) override { return ScreenPoint{v.x, v.y, 400}; }
    int alloc(std::uint8_t) override { return allocated < 8 ? allocated++ : -1; }
    WaterDrop load(int s) override { return slots[s]; }
    void store(int s, const WaterDrop& d) override { slots[s] = d; }
    void set_running(bool on) override { running = on; }
    void clear_groups(std::uint8_t, std::uint8_t) override { allocated = 0; }
    void emit_drop(const DropQuad& q) override { drops.push_back(q); }
    void emit_ribbon(const RibbonQuad& q) override { ribbons.push_back(q); }
};

StreamTable one_segment() {
    StreamTable t;
    t.count = 1;
    t.drops = 3;
    t.length[0] = 8192;
    t.length[1] = 2048;  // the closing length
    t.ramp[0] = 1024;
    t.step[0] = 2048;
    t.width[0] = 1024;
    t.jitter[0] = 128;
    t.flow[0] = 4096;
    t.x[0] = 4096;
    t.z[0] = 8192;
    return t;
}

}  // namespace

SH_TEST(water_stream_params_json) {
    const sh::Result<WaterStreamParams> ok = parse_water_stream_params(
        R"({"_comment": "x", "group": 27, "gravity_cap": 24576, "spawn_speed_low": 96, "spawn_speed_high": 159,
            "spawn_speed_shift": 7, "fall_speed_low": 112, "fall_speed_high": 143, "max_x": 200, "max_y": 160,
            "ordering_table_size": 2048, "slot_depth_offset": 256, "drop_clut": 14, "drop_page": 45,
            "ribbon_clut": 78, "ribbon_page": 45, "ribbon_max_rows": 16})");
    SH_CHECK(ok && ok.value().group == 27 && ok.value().gravity_cap == 24576);
    SH_CHECK(!parse_water_stream_params(R"({"nope": 1})"));
}

SH_TEST(water_stream_start_spawns_drops_and_stop_clears) {
    WaterStream w{WaterStreamParams{}};
    Host host;
    StreamTable t = one_segment();
    t.phase[0] = 99;
    w.start(host, t);
    SH_CHECK(host.running && host.allocated == 3 && t.phase[0] == 0);
    SH_CHECK(host.slots[0].x == 4096 && host.slots[0].z >= 8192 && host.slots[0].elapsed < 8192);
    w.stop(host);
    SH_CHECK(!host.running && host.allocated == 0);
}

SH_TEST(water_stream_spawn_without_segments_frees_the_slot) {
    WaterStream w{WaterStreamParams{}};
    Host host;
    StreamTable t = one_segment();
    t.count = 0;
    host.slots[2].state = 27;
    w.spawn(host, t, 2, 2, 0);
    SH_CHECK(host.slots[2].state == 0);
}

SH_TEST(water_stream_drop_moves_and_draws) {
    WaterStream w{WaterStreamParams{}};
    Host host;
    StreamTable t = one_segment();
    host.seg.sin[0] = 4096;
    WaterDrop d;
    d.state = 27;
    d.scale = 4096;  // 1 unit of progress per unit of time
    host.slots[0] = d;
    SH_CHECK(w.update_drop(host, t, 0));
    // 136 time units at scale 4096 move the drop 136 along the run, 136 in x at sin 1.0
    SH_CHECK(host.slots[0].elapsed == 136 && host.slots[0].x == 136);
    SH_CHECK(host.drops.size() == 1 && host.drops[0].slot == (400 + 256) >> 3);
}

SH_TEST(water_stream_zero_length_skips_the_drop) {
    WaterStream w{WaterStreamParams{}};
    Host host;
    StreamTable t = one_segment();
    t.length[0] = 0;
    SH_CHECK(!w.update_drop(host, t, 0));
    SH_CHECK(host.drops.empty());
}

SH_TEST(water_stream_ribbon_rows_and_scroll) {
    WaterStream w{WaterStreamParams{}};
    Host host;
    StreamTable t = one_segment();
    w.draw_ribbon(host, t);
    SH_CHECK(!host.ribbons.empty());
    SH_CHECK(host.ribbons.size() <= 15);
    SH_CHECK(t.phase[0] == 136 * 4096 / 4096);  // advanced by delta * flow
}
