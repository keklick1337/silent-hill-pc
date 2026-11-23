// SPDX-License-Identifier: GPL-3.0-or-later
// Unit tests for the ground effects (sh::effects::GroundFx) with a scripted host.

#include "engine/effects/ground_fx.h"

#include <string>
#include <vector>

#include "framework/sh_test.h"

using namespace sh::effects;

namespace {

class Host final : public GroundFxHost {
public:
    std::int32_t dt = 136;
    std::int32_t depth = 400;
    bool too_near = false;
    std::int32_t rng = 0;
    std::int32_t heading = 0;
    bool has_texture = true;
    std::uint16_t row[16] = {};
    std::vector<std::pair<int, int>> writes;  // (row, rows)
    std::vector<std::uint16_t> written;
    int boxes = 0, drawn = 0, pages = 0, marks = 0, quads = 0;
    std::vector<std::int32_t> quad_slots;
    std::vector<std::uint16_t> quad_pages;

    std::int32_t rand16() override { return rng; }
    std::int32_t sin(std::int32_t) override { return 0; }
    std::int32_t cos(std::int32_t) override { return 4096; }
    std::int32_t delta_time() override { return dt; }
    std::int32_t ordering_size() override { return 2048; }
    bool texture(const std::string&, std::uint16_t& page, std::uint16_t& px, std::uint16_t& py) override {
        page = 13;
        px = 448;
        py = 0;
        return has_texture;
    }
    void read_palette_row(int, int, std::uint16_t out[16]) override {
        for (int i = 0; i < 16; i++) {
            out[i] = row[i];
        }
    }
    void write_palette_rows(int, int y, int rows, const std::uint16_t* data) override {
        writes.emplace_back(y, rows);
        written.assign(data, data + 16 * rows);
    }
    std::int32_t ground_height(std::int32_t, std::int32_t) override { return -100; }
    std::int32_t owner_heading(std::uint64_t) override { return heading; }
    std::int32_t view_yaw() override { return 0; }
    void begin_frame(std::int32_t, std::int32_t, const Vec3s&) override {}
    void frame_view() override {}
    void local_frame(const Vec3s&, std::int32_t) override {}
    void view_at(std::int32_t, const Vec3s&) override {}
    void push_view() override {}
    void pop_view() override {}
    std::uint8_t light(const Vec3s&, const Vec3s&) override { return 255; }
    std::uint32_t tint(std::uint8_t a) override { return a; }
    std::int32_t geom_screen() override { return 512; }
    GroundProjected project_origin() override { return GroundProjected{160, 120, depth, too_near}; }
    GroundProjected project4(const std::array<Vec3s, 4>&, std::array<GroundProjected, 4>& out) override {
        out = {};
        return GroundProjected{0, 0, 300, too_near};
    }
    std::int32_t project_grid(const std::array<Vec3s, 9>&) override { return 600; }
    void box(std::int16_t, std::int16_t, std::int16_t, std::int16_t, const std::array<std::uint32_t, 4>&, std::int32_t,
             bool d) override {
        boxes++;
        drawn += d ? 1 : 0;
    }
    void draw_page(std::int32_t, int, int, std::uint16_t) override { pages++; }
    void mark(const std::array<GroundProjected, 4>&, std::uint32_t, const std::array<std::uint8_t, 4>&, std::uint16_t,
              std::uint16_t, std::int32_t) override {
        marks++;
    }
    void grid_quad(const GroundGridQuad& q, std::int32_t slot) override {
        quads++;
        quad_slots.push_back(slot);
        quad_pages.push_back(q.page);
    }
};

GroundFxParams shipped() {
    std::string source;
    auto loaded = load_ground_fx(std::string(SH_PROJECT_ROOT) + "/gamedata", "", &source);
    SH_CHECK(static_cast<bool>(loaded));
    return loaded ? loaded.value().at("TwinfeelerBurrow") : GroundFxParams{};
}

}  // namespace

SH_TEST(ground_fx_shipped_file_loads) {
    const GroundFxParams p = shipped();
    SH_CHECK(p.dust.count == 16);
    SH_CHECK(p.holes.size() == 2);
    SH_CHECK(p.holes[0].name == "BurstOut");
    SH_CHECK(p.holes[1].steps.back().end);
    SH_CHECK(p.rings.ramp_layers);
    SH_CHECK(p.craters.wrap_amount);
    SH_CHECK(!parse_ground_fx(R"({"sets": {"S": {"grid_shift": 18}}})"));
    SH_CHECK(!parse_ground_fx(R"({"sets": {}, "extra": 1})"));
}

SH_TEST(ground_fx_reset_snaps_and_builds_palettes) {
    GroundFx f(shipped());
    Host h;
    for (int i = 0; i < 16; i++) {
        h.row[i] = static_cast<std::uint16_t>(i == 0 ? 0 : (i | (i << 5) | (i << 10)));
    }
    f.reset(h, Vec3{-5, 0, (1 << 18) + 7});
    SH_CHECK(f.origin_x() == 0);  // towards zero
    SH_CHECK(f.origin_z() == (1 << 18));
    SH_CHECK(f.page_a() == (13 | 0x20));
    SH_CHECK(f.page_b() == (13 | 0x40));
    SH_CHECK(f.palette_above() == ((14 << 6) | 28));
    SH_CHECK(h.writes.size() == 2);
    SH_CHECK(h.writes[0] == std::make_pair(14, 2));
    SH_CHECK(h.writes[1] == std::make_pair(13, 1));
    const auto& rows = f.palette_rows();
    // Entry 3 is below the reference (7): 7 - 3 = 4 on every channel; entry 9 is above: 2.
    SH_CHECK(rows[32 + 3] == (4 | (4 << 5) | (4 << 10) | 0x8000));
    SH_CHECK(rows[16 + 9] == (2 | (2 << 5) | (2 << 10) | 0x8000));
    SH_CHECK(rows[16 + 3] == 0 && rows[32 + 9] == 0);
    SH_CHECK(rows[32] == 0);  // a zero entry stays zero
    SH_CHECK(rows[0] == 0 && (rows[15] & 0x8000) != 0);
}

SH_TEST(ground_fx_dust_cloud_draws_boxes_and_a_page) {
    GroundFx f(shipped());
    Host h;
    f.reset(h, Vec3{});
    f.dust_at(Vec3{4096, 0, 4096}, 4);
    SH_CHECK(f.dust()[4].life == 4096 && f.dust()[4].kind == 0);
    SH_CHECK(f.dust()[4].pos.x == 256);
    h.rng = 0x7FFF;  // every seed try lands
    f.frame(h, Vec3s{});
    SH_CHECK(h.boxes == 11 * 9);
    SH_CHECK(h.pages == 1);
    SH_CHECK(f.dust()[4].life == 4096 - h.dt);
    // Overflow: no boxes and no page.
    h.too_near = true;
    h.boxes = h.pages = 0;
    f.frame(h, Vec3s{});
    SH_CHECK(h.boxes == 0 && h.pages == 0);
    // A cloud already alive is not restarted.
    f.dust_at(Vec3{}, 4);
    SH_CHECK(f.dust()[4].pos.x != 0);
    f.dust_at(Vec3{}, 99);  // out of range: nothing
}

SH_TEST(ground_fx_hole_timeline_runs_on_the_first_hole) {
    GroundFx f(shipped());
    Host h;
    f.reset(h, Vec3{});
    h.rng = 1;
    SH_CHECK(f.hole(h, 1, 42, Vec3{8192, 0, 0}));  // DiveIn
    SH_CHECK(f.holes()[0].position.y == -100);
    f.frame(h, Vec3s{});
    // Step 0: burst point 5120 ahead (cos = 1), dust ring (5 clouds), crater, ring.
    SH_CHECK(f.burst_point().z == 5120 && f.burst_point().x == 8192);
    int alive = 0;
    for (const DustCloud& c : f.dust()) {
        alive += c.life > 0 ? 1 : 0;
    }
    SH_CHECK(alive == 5);
    SH_CHECK(f.craters()[0].life > 0 && f.rings()[0].life > 0);
    SH_CHECK(f.holes()[0].step == 1);
    // The ring pool draws face b one bias lower than face a with the ramp page.
    SH_CHECK(h.quads == 16);  // crater 8 + ring 8
    SH_CHECK(h.quad_pages.back() == 0x4D);
    SH_CHECK(h.quad_slots[15] + 25 == h.quad_slots[14]);
    // A second hole runs the first hole's timeline state too.
    SH_CHECK(f.hole(h, 1, 43, Vec3{}));
    const std::int32_t t = f.holes()[0].time;
    f.frame(h, Vec3s{});
    SH_CHECK(f.holes()[0].time == t + 2 * h.dt);
    SH_CHECK(f.holes()[1].time == 0);
    // Frozen time: holes do not run.
    h.dt = 0;
    f.frame(h, Vec3s{});
    SH_CHECK(f.holes()[0].time == t + 2 * 136);
}

SH_TEST(ground_fx_marks_fade_after_hold) {
    GroundFx f(shipped());
    Host h;
    f.reset(h, Vec3{});
    f.mark_at(Vec3{}, 3);
    f.frame(h, Vec3s{});
    SH_CHECK(h.marks == 2);
    SH_CHECK(f.marks()[3].alpha == 4096);
    f.mark_at(Vec3{4096, 0, 0}, 3);  // still showing: unchanged
    SH_CHECK(f.marks()[3].pos.x == 0);
    while (f.marks()[3].timer > 4915) {
        f.frame(h, Vec3s{});
    }
    f.frame(h, Vec3s{});
    SH_CHECK(f.marks()[3].alpha == 4096 - ((136 * 1228) >> 12));
}
