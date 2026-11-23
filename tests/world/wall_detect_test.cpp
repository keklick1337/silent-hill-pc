// The wall/ledge/void classification (engine/world/wall_detect.h) against its specification:
// a scripted surface answers the probes in order and records every query; the expected numbers
// are worked out by hand in the comments. The game layer is not linked.

#include "engine/world/wall_detect.h"

#include "framework/sh_test.h"

#include <cstdint>
#include <functional>
#include <vector>

namespace {

namespace w = sh::world;

constexpr std::int32_t kQ12One = 4096;
constexpr std::uint8_t  kVoidType = 12;  // the "no floor" ground type of the base game

// A surface that answers per call (by call index) and records where it was asked.
class StubSurface {
public:
    // rule(call index) -> WallProbeSample
    explicit StubSurface(std::function<w::WallProbeSample(std::int32_t)> rule) : m_rule(std::move(rule)) {}

    w::WallProbeSample operator()(std::int32_t x, std::int32_t z) {
        const w::WallProbeSample out = m_rule(static_cast<std::int32_t>(m_queries.size()));
        m_queries.push_back({x, z});
        return out;
    }

    const std::vector<std::pair<std::int32_t, std::int32_t>>& queries() const { return m_queries; }

private:
    std::function<w::WallProbeSample(std::int32_t)> m_rule;
    std::vector<std::pair<std::int32_t, std::int32_t>> m_queries;
};

w::WallProbeSample floor_at(std::int32_t height) {
    return w::WallProbeSample{height, 1};
}

w::WallProbeSample nothing() {
    return w::WallProbeSample{0, kVoidType};
}

w::WallScan scan_at(std::int32_t height, w::WallSurface standing) {
    w::WallScan scan{};
    scan.x = 100000;
    scan.z = -200000;
    scan.height = height;
    scan.standing = standing;
    scan.void_ground_type = kVoidType;
    return scan;
}

}  // namespace

SH_TEST(world_wall_clear_answers_without_queries) {
    // Standing on the floor itself (the drop bound sits half a unit lower): nothing to classify.
    StubSurface surface([](std::int32_t) { return floor_at(0); });
    const w::WallVerdict verdict = w::classify_wall(scan_at(0, w::WallSurface{0, 1}), std::ref(surface));
    SH_CHECK(verdict.kind == w::WallKind::Clear);
    SH_CHECK(verdict.wall_count == 0);
    SH_CHECK(surface.queries().size() == 0);
}

SH_TEST(world_wall_counts_and_forgives_a_ledge) {
    // The floor under the mover sits 3000/4096 below: a wall candidate. Bound = 4096 - 2048 =
    // 2048; two of the nine probes fall below it (calls 2 and 5) -> wall_count 2 < 3: a ledge,
    // forgiven.
    StubSurface surface([](std::int32_t call) { return call == 2 || call == 5 ? floor_at(0) : floor_at(3000); });
    const w::WallVerdict verdict =
        w::classify_wall(scan_at(kQ12One, w::WallSurface{kQ12One - 3000, 1}), std::ref(surface));
    SH_CHECK(verdict.kind == w::WallKind::Wall);
    SH_CHECK(verdict.wall_count == 2);
    SH_CHECK(verdict.forgive);
    SH_CHECK(surface.queries().size() == 9);
    // The ring runs at radius 0.2 around the mover: probe 0 dead ahead — X gets the sine (0),
    // Z the cosine (819) — probe 1 at the game's own 467-unit step (41.1 degrees).
    const auto& q = surface.queries();
    SH_CHECK(q[0].first == 100000);
    SH_CHECK(q[0].second == -200000 + 819);
    SH_CHECK(q[1].first == 100000 + ((819 * w::fixed_sin(467)) >> 12));
    SH_CHECK(q[1].second == -200000 + ((819 * w::fixed_cos(467)) >> 12));
}

SH_TEST(world_wall_threshold_is_strict) {
    // Exactly three probes below the bound is already a wall (the threshold is "fewer than 3").
    StubSurface three([](std::int32_t call) { return call < 3 ? floor_at(0) : floor_at(3000); });
    const w::WallVerdict v3 =
        w::classify_wall(scan_at(kQ12One, w::WallSurface{kQ12One - 3000, 1}), std::ref(three));
    SH_CHECK(v3.wall_count == 3);
    SH_CHECK(!v3.forgive);
    StubSurface five([](std::int32_t call) { return call < 5 ? floor_at(0) : floor_at(3000); });
    const w::WallVerdict v5 =
        w::classify_wall(scan_at(kQ12One, w::WallSurface{kQ12One - 3000, 1}), std::ref(five));
    SH_CHECK(v5.wall_count == 5);
    SH_CHECK(!v5.forgive);
}

SH_TEST(world_wall_void_keeps_last_floor) {
    // No floor under the mover at all (the void type): the ring looks for a floor around it and
    // keeps the LAST one found. Calls 0-3 void, call 4 a floor of type 3 at -500, calls 5-8 void.
    StubSurface surface([](std::int32_t call) {
        return call == 4 ? w::WallProbeSample{-500, 3} : nothing();
    });
    const w::WallVerdict verdict = w::classify_wall(scan_at(kQ12One, w::WallSurface{-4000, kVoidType}),
                                                    std::ref(surface));
    SH_CHECK(verdict.kind == w::WallKind::Void);
    SH_CHECK(verdict.void_height == -500);
    SH_CHECK(verdict.void_ground_type == 3);
    SH_CHECK(surface.queries().size() == 9);
}

SH_TEST(world_wall_void_finds_nothing) {
    // Every ring sample void too: the verdict keeps the void type, so the caller changes
    // nothing (its "found a floor" test is the type no longer being void).
    StubSurface surface([](std::int32_t) { return nothing(); });
    const w::WallVerdict verdict =
        w::classify_wall(scan_at(kQ12One, w::WallSurface{-4000, kVoidType}), std::ref(surface));
    SH_CHECK(verdict.kind == w::WallKind::Void);
    SH_CHECK(verdict.void_ground_type == kVoidType);
    SH_CHECK(verdict.void_height == 0);
}

SH_TEST(world_radial_flat_ground) {
    // Sixteen samples, 22.5 degrees apart from the heading's low 4 bits: flat ground at 100
    // gives mid 100, no spread (nothing is above the mid), a full-unit forward step of 1/16.
    StubSurface surface([](std::int32_t) { return floor_at(100); });
    const w::RadialBand band = w::radial_ground_band(w::RadialScan{1000, -1000, 100, 0, 409},
                                                     std::ref(surface));
    SH_CHECK(band.mid_height == 100);
    SH_CHECK(band.offset_x == 0);
    SH_CHECK(band.offset_z == 256);
    SH_CHECK(surface.queries().size() == 16);
    // The ring is a unit circle: sample 0 at (x + sin 0, z + cos 0) = (x, z + 4096), sample 1 at
    // 22.5 degrees: sin 256 = 1567, cos 256 = 3784.
    SH_CHECK(surface.queries()[0].first == 1000);
    SH_CHECK(surface.queries()[0].second == -1000 + 4096);
    SH_CHECK(surface.queries()[1].first == 1000 + 1567);
    SH_CHECK(surface.queries()[1].second == -1000 + 3784);
}

SH_TEST(world_radial_slope_and_spread) {
    // A ramp h[i] = i*10: high 150 at index 15, low 0, mid 75. The clockwise spread stops at
    // once (ring slot 0 is back at height 0, below the mid), so it ends at 15 itself.
    // Bisector = (15 + 15) << 8 >> 1 = 3840 (337.5 degrees):
    // sin = -1567, cos = 3784 -> a 1/16 step of (-98, 236).
    StubSurface surface([](std::int32_t call) { return floor_at(call * 10); });
    const w::RadialBand band = w::radial_ground_band(w::RadialScan{0, 0, 75, 0, 409}, std::ref(surface));
    SH_CHECK(band.mid_height == 75);
    SH_CHECK(band.offset_x == -98);
    SH_CHECK(band.offset_z == 236);
}

SH_TEST(world_radial_clamps_under_start) {
    // The mid never falls more than 0.1 under the height the mover fell from: floors at 100,
    // start 1000 -> mid = 1000 - 409 = 591. Nothing is above 591, so the step points dead ahead.
    StubSurface surface([](std::int32_t) { return floor_at(100); });
    const w::RadialBand band = w::radial_ground_band(w::RadialScan{0, 0, 1000, 0, 409}, std::ref(surface));
    SH_CHECK(band.mid_height == 591);
    SH_CHECK(band.offset_x == 0);
    SH_CHECK(band.offset_z == 256);
}

SH_TEST(world_radial_spread_wraps_the_ring) {
    // The spread may run past index 15 into ring slot 0: slot 15 at 250, slot 0 at 200, the
    // rest at 100. High = 250 at index 15 (a strictly later higher sample would steal it),
    // low = 100, mid = 175. The clockwise spread: slot 0 (200 > 175) -> end 16; slot 1 (100)
    // stops it. Bisector = (15 + 16) << 8 >> 1 = 3968 (348.75 degrees): sin = -799,
    // cos = 4017 -> a 1/16 step of (-50, 251).
    StubSurface surface([](std::int32_t call) {
        return call == 15 ? floor_at(250) : call == 0 ? floor_at(200) : floor_at(100);
    });
    const w::RadialBand band = w::radial_ground_band(w::RadialScan{0, 0, 175, 0, 409}, std::ref(surface));
    SH_CHECK(band.mid_height == 175);
    SH_CHECK(band.offset_x == -50);
    SH_CHECK(band.offset_z == 251);
}
