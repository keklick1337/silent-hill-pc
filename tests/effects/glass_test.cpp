// SPDX-License-Identifier: GPL-3.0-or-later
// Unit tests for the glass shards (sh::effects::GlassEffects); expected values are
// hand-computed from the original's arithmetic with a scripted host.

#include "engine/effects/glass.h"

#include <cmath>
#include <vector>

#include "framework/sh_test.h"

using namespace sh::effects;

namespace {

// Deterministic host: a queue of "random" numbers, identity transforms, a projection that
// echoes the relative point, and a plain integer square root.
class FakeHost final : public GlassHost {
public:
    std::vector<std::int32_t> rng{100, 200, 150, 3, 7, 100, 120, 140, 50, 60, 1, 2, 3, 4, 5, 6};
    std::size_t rng_at = 0;

    std::int32_t next() { return rng_at < rng.size() ? rng[rng_at++] : 0; }
    std::int32_t rand16() override { return next(); }
    std::int32_t rand_uint(std::int32_t low, std::int32_t high) override { return next() % (high - low + 1) + low; }
    std::int32_t rand_add(std::int32_t base, std::int32_t low, std::int32_t high) override
    {
        return base + next() % (high - low + 1) + low;
    }

    std::int32_t sin(std::int32_t) override { return 0; }
    std::int32_t cos(std::int32_t) override { return 4096; }
    std::int32_t atan2(std::int32_t, std::int32_t) override { return 100; }
    std::int32_t sqrt(std::int32_t v) override
    {
        std::int32_t r = 0;
        while ((r + 1) * (r + 1) <= v) {
            r++;
        }
        return r;
    }
    std::int32_t delta_time() override { return dt; }
    std::int32_t gravity() override { return 4096; }
    std::int32_t dt = 136;

    void rotate_setup(std::int16_t pitch, std::int16_t yaw) override
    {
        setup_pitch = pitch;
        setup_yaw = yaw;
    }
    Vec3 rotate(const Vec3s& v) override { return Vec3{v.x, v.y, v.z}; }

    void frame_setup(std::int16_t yaw, std::int16_t pitch, std::int32_t tx, std::int32_t tz) override
    {
        frame_yaw = yaw;
        frame_pitch = pitch;
        frame_tx = tx;
        frame_tz = tz;
    }
    void use_view() override { used_view = true; }
    Vec3s view_origin() override { return Vec3s{0, 0, 0}; }

    void project3(const Vec3s& a, const Vec3s& b, const Vec3s& c, ScreenPoint out[3]) override
    {
        out[0] = ScreenPoint{a.x, a.y, a.z};
        out[1] = ScreenPoint{b.x, b.y, b.z};
        out[2] = ScreenPoint{c.x, c.y, c.z};
    }
    ScreenPoint project(const Vec3s& v) override { return ScreenPoint{v.x, v.y, v.z}; }

    Rgb fog(std::int32_t depth_times_16) override { return Rgb{static_cast<std::uint8_t>(depth_times_16 >> 8), 2, 3}; }

    GlassHost::Ground ground(std::int32_t, std::int32_t) override { return the_ground; }
    GlassHost::Ground the_ground{100000, 1};

    int alloc(std::uint8_t group) override
    {
        alloced_groups.push_back(group);
        return static_cast<int>(alloced_groups.size()) - 1;
    }
    std::vector<std::uint8_t> alloced_groups;

    GlassShard load(int slot) override { return the_shard; }
    void store(int slot, const GlassShard& shard) override
    {
        stored_slot = slot;
        stored = shard;
    }
    int stored_slot = -1;
    GlassShard stored;
    GlassShard the_shard;

    void set_pane(int group, const GlassPane& pane) override { panes[static_cast<std::size_t>(group)] = pane; }
    GlassPane pane(int group) override { return panes[static_cast<std::size_t>(group)]; }
    GlassPane panes[4];

    void emit(const Quad& q) override { quads.push_back(q); }
    std::vector<Quad> quads;

    std::int16_t setup_pitch = 0, setup_yaw = 0;
    std::int16_t frame_yaw = 0, frame_pitch = 0;
    std::int32_t frame_tx = 0, frame_tz = 0;
    bool used_view = false;
};

GlassParams params()
{
    GlassParams p;  // the shipped numbers
    return p;
}

}  // namespace

SH_TEST(glass_spawn_first_shard_hand_computed) {
    FakeHost host;
    GlassEffects fx(params());

    // from == to: the burst direction is zero; half sizes 64 make one Q6 cell per axis,
    // spread 256 steps the walk past one column per side.
    Vec3 from{10, 0, 20};
    Vec3 to{10, 0, 20};
    fx.spawn(host, from, to, 9830, 90, 64, 64, 256, 0);

    // The pane table entry.
    SH_CHECK(host.panes[0].x == 10 && host.panes[0].z == 20 && host.panes[0].size == 90);

    // One spawn group, one shard (the walk steps over the rest).
    SH_CHECK(host.alloced_groups.size() == 1 && host.alloced_groups[0] == 23);
    SH_CHECK(host.stored_slot == 0);

    // rotate_setup takes the burst's angles (both atan2 stubs answer 100).
    SH_CHECK(host.setup_pitch == 100 && host.setup_yaw == 100);

    const GlassShard& s = host.stored;
    // The cell: (u + rand % 128) - 64 with u = -64 and rand 200 -> -56; same row with
    // rand 150 -> -106.
    SH_CHECK(s.x == -56 && s.y == -106 && s.z == 0);
    // |(-56, -106, 0)| in the Q6 way: sqrt(1 + 4) << 6 = 128; scale = 128 / (9830 >> 10)
    // = 128 / 9 = 14.
    SH_CHECK(s.spin == 14);
    // look = 128*6/65 + 3 = 14, capped at 8; variant = (3 % 8)*16 - 128 + 7 = -73.
    SH_CHECK(s.variant == static_cast<std::uint8_t>(-73));
    // The launch velocity: (speed * (rand % 128 + 64)) >> 7 scaled by the cell vector.
    // x: (9830 * 164) >> 7 = 12594; 12594 * -56 / 128 = -5509.
    SH_CHECK(s.vel_x == -5509);
    // y: (9830 * 184) >> 7 = 14130; 14130 * -106 / 128 = -11701.
    SH_CHECK(s.vel_y == -11701);
    SH_CHECK(s.vel_z == 0);
    // The angles: 100 + (50 % 512) - 255 = -105 and 100 + 60 - 255 = -95.
    SH_CHECK(s.yaw == -105 && s.pitch == -95);
    SH_CHECK(s.state == 23);  // the group the alloc marked the slot with
}

SH_TEST(glass_spawn_consumes_rng_in_order) {
    FakeHost host;
    GlassEffects fx(params());
    Vec3 from{10, 0, 20};
    fx.spawn(host, from, from, 9830, 90, 64, 64, 256, 0);
    // Per cell: hole roll, cell x, cell y, look, look jitter, three speed factors, two
    // angle jitters = 10 numbers.
    SH_CHECK(host.rng_at == 10);
}

SH_TEST(glass_update_flight_and_draw) {
    FakeHost host;
    GlassEffects fx(params());

    GlassShard s;
    s.state = 23;
    s.variant = 0x50;  // size class 5, flying
    s.x = 0;
    s.y = 0;
    s.z = 16384;
    s.vel_x = 1000;
    s.vel_y = 0;
    s.vel_z = 2000;
    s.yaw = 100;
    s.pitch = 200;
    s.spin = 0x0082;  // both spin bits off... 0x80 set: yaw += dt * (0x02 << 7) >> 12

    host.panes[0] = GlassPane{40, 60, 90};
    host.the_ground = GlassHost::Ground{100000, 1};  // nothing frees the slot

    const bool drew = fx.update(host, 3, s);
    SH_CHECK(drew);

    // The flight: drag = ((25 + 7) * 6144) / 28 = 7021; sin is 0 and cos is 4096, so only
    // vel_z loses (136 * 7021) >> 12 = 233.
    SH_CHECK(s.vel_x == 1000);
    SH_CHECK(s.vel_z == 2000 - 233);
    // Gravity: (4096 * (5 + 9)) >> 4 = 3584.
    SH_CHECK(s.vel_y == 3584);
    // Positions: z += (136 * 1767) >> 12 = 58.
    SH_CHECK(s.z == 16384 + 58);
    SH_CHECK(s.x == 0 + (136 * 1000 >> 12));
    SH_CHECK(s.y == 0 + (136 * 3584 >> 12));
    // The spin word 0x82: bit 7 set -> yaw += (136 * (0x02 << 7)) >> 12 = 8.
    SH_CHECK(s.yaw == 108);
    // Bit 0x8000 clear -> pitch -= (136 * ((0x82 >> 1) & 0x3F80)) >> 12 = 0.
    SH_CHECK(s.pitch == 200);
    SH_CHECK(s.state == 23);

    // The store happened before the draw.
    SH_CHECK(host.stored_slot == 3 && host.stored.z == s.z);

    // The draw: the frame matrix took the shard's angles on the pane's centre.
    SH_CHECK(host.frame_yaw == 108 && host.frame_pitch == 200);
    SH_CHECK(host.frame_tx == 40 && host.frame_tz == 60);
    SH_CHECK(host.used_view);

    // One quad: half = (90 * (25 + 4)) >> 4 = 163; the moved centre (x 33, y 119, z 16442)
    // shifts the corners before the >> 4: (-9, 17) and (12, -3), depth 1027.
    SH_CHECK(host.quads.size() == 1);
    const GlassHost::Quad& q = host.quads[0];
    SH_CHECK(q.variant == 0x50);
    SH_CHECK(q.corners[0].x == -9 && q.corners[0].y == 17 && q.corners[0].depth == 1027);
    SH_CHECK(q.corners[3].x == 12 && q.corners[3].y == -3 && q.corners[3].depth == 1027);
    // The fog colour: depth 1027 * 16 >> 8 = 64.
    SH_CHECK(q.color.r == 64 && q.color.g == 2 && q.color.b == 3);
    // Depth 1027 - 6144 < 0: the quad goes to slot 0.
    SH_CHECK(q.slot == 0);
}

SH_TEST(glass_update_resting_relaunch) {
    FakeHost host;
    GlassEffects fx(params());

    GlassShard s;
    s.state = 23;
    s.variant = 0x80 | 0x30;  // resting, size class 3
    s.spin = 30;              // below the frame's dt (40): relaunch this frame
    s.x = 100;
    s.y = -500;
    s.z = 200;
    s.vel_x = 10;
    s.vel_y = 0;
    s.vel_z = -20;
    s.yaw = 0;
    s.pitch = 0;
    host.dt = 40;
    host.panes[0] = GlassPane{0, 0, 90};

    // atan2 answers 100: (100 - 0) > 0 -> bit 7 set; (100 - 0) > 0 -> bit 15 set.
    // mag2_q6(0, -20) = sqrt(0 + 1) << 6 = 64; jitter span 16, base 8:
    // (64 + (rng % 16) - 8) / 256 with rng 100 -> (64 + 4 - 8) / 256 = 0.
    fx.update(host, 7, s);

    // Each half draws twice (the original's clamp names its expression twice).
    SH_CHECK(host.rng_at == 4);
    SH_CHECK(s.variant == 0x30);  // the resting bit cleared
    SH_CHECK((s.spin & 0x8080) == 0x8080);
    SH_CHECK((s.spin & 0xFF) == 0x80);  // both jitter terms zero with this rng queue
}

SH_TEST(glass_update_resting_counts_down) {
    FakeHost host;
    GlassEffects fx(params());

    GlassShard s;
    s.state = 23;
    s.variant = 0x80;
    s.spin = 100;
    host.dt = 40;
    host.panes[0] = GlassPane{0, 0, 90};

    fx.update(host, 7, s);
    SH_CHECK(s.variant == 0x80 && s.spin == 60);
    SH_CHECK(host.quads.size() == 0);  // behind the camera: nothing drawn, slot kept
}

SH_TEST(glass_update_ground_frees_the_slot) {
    FakeHost host;
    GlassEffects fx(params());

    GlassShard s;
    s.state = 23;
    s.variant = 0x50;
    s.x = 100;
    s.y = 500;
    s.z = 100;
    host.the_ground = GlassHost::Ground{0, 0};  // default ground, the shard still above 0

    fx.update(host, 7, s);
    SH_CHECK(s.state == 0);
    SH_CHECK(host.stored.state == 0);
}

SH_TEST(glass_update_culls_offscreen) {
    FakeHost host;
    GlassEffects fx(params());

    GlassShard s;
    s.state = 23;
    s.variant = 0x10;
    s.x = 0;
    s.z = 65536;
    host.panes[0] = GlassPane{0, 0, 90};

    // The fourth corner lands at x = 163 >> 4 = 10... push it out through a big centre.
    s.x = 32768;
    const bool drew = fx.update(host, 7, s);
    SH_CHECK(!drew && host.quads.empty());
}

SH_TEST(glass_params_json) {
    const sh::Result<GlassParams> ok = parse_glass_params(
        R"({"_comment": "x", "max_x": 200, "max_y": 160, "ordering_table_size": 2048, "slot_depth_offset": 6144, "fog_mix": 128})");
    SH_CHECK(ok && ok.value().max_x == 200 && ok.value().slot_depth_offset == 6144);

    SH_CHECK(!parse_glass_params(R"({"nope": 1})"));
    SH_CHECK(!parse_glass_params(R"({"max_x": 200, "max_y": 160, "ordering_table_size": 2048, "slot_depth_offset": 6144})"));
    SH_CHECK(!parse_glass_params(R"({"max_x": -1, "max_y": 160, "ordering_table_size": 2048, "slot_depth_offset": 6144, "fog_mix": 128})"));
}
