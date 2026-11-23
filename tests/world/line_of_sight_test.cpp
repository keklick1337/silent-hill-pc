// The line-of-sight policies (engine/world/line_of_sight.h) against their specification: a
// scripted rng (the draw order is part of the observable behaviour) and a stub ray probe that
// records every call. Expected numbers are worked out by hand in the comments; the game layer
// is not linked.

#include "engine/world/line_of_sight.h"

#include "framework/sh_test.h"

#include <cstdint>
#include <functional>
#include <vector>

namespace {

namespace w = sh::world;

constexpr std::int32_t kQ12One = 4096;

// Hands out a scripted sequence of 15-bit draws and counts the calls.
class ScriptedRng {
public:
    explicit ScriptedRng(std::vector<std::int32_t> draws) : m_draws(std::move(draws)) {}

    std::int32_t operator()() {
        ++m_calls;
        return m_index < static_cast<std::int32_t>(m_draws.size()) ? m_draws[m_index++] : 0;
    }

    std::int32_t calls() const { return m_calls; }

private:
    std::vector<std::int32_t> m_draws;
    std::int32_t              m_index = 0;
    std::int32_t              m_calls = 0;
};

// A probe that decides per call (by call index) and records what it was asked.
class StubRays {
public:
    // rule(call index) -> SightOutcome
    explicit StubRays(std::function<w::SightOutcome(std::int32_t)> rule) : m_rule(std::move(rule)) {}

    w::SightOutcome operator()(const w::Vec3& from, const w::Vec3& offset, bool with_characters) {
        const w::SightOutcome out = m_rule(static_cast<std::int32_t>(m_calls.size()));
        m_calls.push_back({from, offset, with_characters});
        return out;
    }

    struct Call {
        w::Vec3 from;
        w::Vec3 offset;
        bool with_characters;
    };
    const std::vector<Call>& calls() const { return m_calls; }

private:
    std::function<w::SightOutcome(std::int32_t)> m_rule;
    std::vector<Call>                            m_calls;
};

w::SightOutcome clear() {
    return w::SightOutcome{false, false};
}

w::SightOutcome wall() {
    return w::SightOutcome{true, false};
}

}  // namespace

SH_TEST(world_los_normalize_heading) {
    // The fold keeps +2048 and turns (2048, 4095] negative; negatives wrap first.
    SH_CHECK(w::normalize_heading(0) == 0);
    SH_CHECK(w::normalize_heading(2048) == 2048);
    SH_CHECK(w::normalize_heading(2049) == -2047);
    SH_CHECK(w::normalize_heading(4095) == -1);
    SH_CHECK(w::normalize_heading(4096) == 0);
    SH_CHECK(w::normalize_heading(-1) == -1);
    SH_CHECK(w::normalize_heading(6000) == 1904);
    SH_CHECK(w::normalize_heading(-2049) == 2047);
}

SH_TEST(world_los_heading_offset) {
    // Quarter turn: sin = 4096, cos = 0, so a Q.12 dist of 2.0 lands exactly on (2.0, 0, 0).
    const w::Vec3 quarter = w::heading_offset(2 * kQ12One, 1024);
    SH_CHECK(quarter.x == 2 * kQ12One);
    SH_CHECK(quarter.y == 0);
    SH_CHECK(quarter.z == 0);
    // Eighth turn: sin = cos = 2896 (0.7071 in Q.12, truncated), (8192 * 2896) >> 12 = 5792.
    const w::Vec3 eighth = w::heading_offset(2 * kQ12One, 512);
    SH_CHECK(eighth.x == 5792);
    SH_CHECK(eighth.z == 5792);
    // Negative heading mirrors X; a zero dist is a zero offset.
    const w::Vec3 mirrored = w::heading_offset(2 * kQ12One, -1024);
    SH_CHECK(mirrored.x == -2 * kQ12One);
    SH_CHECK(w::heading_offset(0, 512).x == 0);
}

SH_TEST(world_los_planar_distance) {
    // (3, 4) -> 5 in Q.12: the Q.6 squares are exact, so the root is exact.
    SH_CHECK(w::planar_distance_q12(3 * kQ12One, 4 * kQ12One) == 5 * kQ12One);
    SH_CHECK(w::planar_distance_q12(0, 0) == 0);
    // Under one Q.6 unit each way truncates to a zero distance.
    SH_CHECK(w::planar_distance_q12(1, 1) == 0);
    SH_CHECK(w::planar_distance_q12(-3 * kQ12One, -4 * kQ12One) == 5 * kQ12One);
}

SH_TEST(world_los_clear_heading_all_blocked) {
    // Every probe blocked: fifteen draws, fifteen probes, the "none" answer 4096.
    ScriptedRng rng(std::vector<std::int32_t>(15, 10));
    StubRays    rays([](std::int32_t) { return wall(); });
    const w::LosHead head{w::Vec3{0, 0, 0}, 100, 0};
    SH_CHECK(w::clear_heading(head, kQ12One, 100, std::ref(rays), std::ref(rng)) == 4096);
    SH_CHECK(rng.calls() == 15);
    SH_CHECK(rays.calls().size() == 15);
    // i = 0 draws once and probes at the drawn offset: 10 % 64 - 32 = -22, plus heading 100.
    SH_CHECK(rays.calls().front().offset.x == w::heading_offset(kQ12One, 78).x);
}

SH_TEST(world_los_clear_heading_keeps_first) {
    // The first candidate (draw 50 -> +18) is clear; every later candidate is wider and gets
    // skipped by the |offset| guard without a probe: one probe, fifteen draws.
    ScriptedRng rng({50, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10});
    StubRays    rays([](std::int32_t) { return clear(); });
    const w::LosHead head{w::Vec3{0, 0, 0}, 100, 0};
    SH_CHECK(w::clear_heading(head, kQ12One, 100, std::ref(rays), std::ref(rng)) == 118);
    SH_CHECK(rng.calls() == 15);
    SH_CHECK(rays.calls().size() == 1);
}

SH_TEST(world_los_clear_heading_equal_abs_probes) {
    // |best| == |candidate| is NOT skipped: i = 0 (draw 40 -> +8) is blocked, i = 1 (draw 10 ->
    // 512 + 10 = 522) is clear and becomes best, then i = 2 draws the same 10 and offers
    // -(512 + 10); the equal magnitude forces a probe. Blocked there, so the answer stays
    // 522 + heading 100 = 622. Three probes: at 108, 622 and -422.
    ScriptedRng rng({40, 10, 10});
    StubRays    rays([](std::int32_t call) { return call == 1 ? clear() : wall(); });
    const w::LosHead head{w::Vec3{0, 0, 0}, 100, 0};
    SH_CHECK(w::clear_heading(head, kQ12One, 100, std::ref(rays), std::ref(rng)) == 622);
    SH_CHECK(rays.calls().size() == 3);
    SH_CHECK(rays.calls()[2].offset.x == w::heading_offset(kQ12One, -422).x);
}

SH_TEST(world_los_heading_toward_full_span) {
    // A full span sweeps twelve headings (i*30 + draw%30) degrees; with every draw 10 only
    // i = 6 is clear: (6*30 + 10) * 4096 / 360 = 2161, normalised to 2161 - 4096 = -1935.
    // The target sits away from the head so the landing distance beats the initial 0.
    ScriptedRng rng(std::vector<std::int32_t>(12, 10));
    StubRays    rays([](std::int32_t call) { return call == 6 ? clear() : wall(); });
    const w::LosHead head{w::Vec3{0, 0, 0}, 0, 0};
    SH_CHECK(w::heading_toward(head, 0, 40 * kQ12One, 0, 4096, false, std::ref(rays), std::ref(rng)) == -1935);
    SH_CHECK(rng.calls() == 12);
    SH_CHECK(rays.calls().size() == 12);
}

SH_TEST(world_los_heading_toward_span) {
    // span 1024 (a quarter turn): step 1024/3 = 341, seven probes at
    // heading + (i - 3)*341 + (draw%341 >> 1) - 85. Draw 0, heading 100: only i = 2 is clear,
    // 100 + (2 - 3)*341 - 85 = -326; with dist 0 every landing point is the head itself, so the
    // nearest-to-target contest is equal and the first clear probe wins for !clockwise.
    ScriptedRng rng(std::vector<std::int32_t>(7, 0));
    StubRays    rays([](std::int32_t call) { return call == 2 ? clear() : wall(); });
    const w::LosHead head{w::Vec3{0, 0, 0}, 100, 0};
    SH_CHECK(w::heading_toward(head, 0, 100, 0, 1024, false, std::ref(rays), std::ref(rng)) == -326);
    // Clockwise keeps the first clear probe too (equal distances never replace the incumbent).
    StubRays rays_cw([](std::int32_t call) { return call == 2 ? clear() : wall(); });
    SH_CHECK(w::heading_toward(head, 0, 100, 0, 1024, true, std::ref(rays_cw), std::ref(rng)) == -326);
    // All blocked: 4096.
    StubRays rays_no([](std::int32_t) { return wall(); });
    SH_CHECK(w::heading_toward(head, 0, 100, 0, 1024, false, std::ref(rays_no), std::ref(rng)) == 4096);
}

SH_TEST(world_los_heading_toward_nearest) {
    // Two clear probes with different landing distances: dist 4096, heading 0, span 1024
    // (step 341), all draws 0: i = 3 aims at -85 angle units, i = 1 at -767.
    // sin(-85) = -532, cos(-85) = 4061 -> i = 3 lands (-532, 4061);
    // sin(-767) = -3781, cos(-767) = 1573 -> i = 1 lands (-3781, 1573).
    // For the target (40.0, 0): Q.6 squares (dx, dz) (2568, -64) -> 6598720 and
    // (2619, -25) -> 6859786: i = 3 lands nearer. Counter-clockwise keeps the farthest
    // landing (i = 1, -767), clockwise the nearest (i = 3, -85).
    ScriptedRng rng(std::vector<std::int32_t>(7, 0));
    StubRays    rays([](std::int32_t call) { return call == 1 || call == 3 ? clear() : wall(); });
    const w::LosHead head{w::Vec3{0, 0, 0}, 0, 0};
    SH_CHECK(w::heading_toward(head, kQ12One, 40 * kQ12One, 0, 1024, false, std::ref(rays), std::ref(rng)) == -767);
    ScriptedRng rng_cw(std::vector<std::int32_t>(7, 0));
    StubRays    rays_cw([](std::int32_t call) { return call == 1 || call == 3 ? clear() : wall(); });
    SH_CHECK(w::heading_toward(head, kQ12One, 40 * kQ12One, 0, 1024, true, std::ref(rays_cw), std::ref(rng_cw)) == -85);
}

SH_TEST(world_los_visible_reach) {
    // Straight ahead (rel = 0): dist_mult = 2048*2 = 4096, one count step keeps it (4096^2>>12);
    // dist = 3.0, reach = 2.0 + 2.0 = 4.0: the ray happens, unblocked -> visible.
    const w::LosHead head{w::Vec3{0, 0, 0}, 0, 100};
    const w::LosBody body{w::Vec3{0, -200, 3 * kQ12One}, 300, 50, false};
    StubRays rays([](std::int32_t) { return clear(); });
    SH_CHECK(w::is_visible(head, body, 1, 2 * kQ12One, 2 * kQ12One, std::ref(rays)));
    SH_CHECK(rays.calls().size() == 1);
    // The band: from the watcher's eye (y + 100), the offset spans
    // (body.y + 50) - (watcher.y - 100) = -50, flat ahead by 3.0.
    SH_CHECK(rays.calls()[0].from.y == 100);
    SH_CHECK(rays.calls()[0].offset.y == -50);
    SH_CHECK(rays.calls()[0].offset.z == 3 * kQ12One);
    SH_CHECK(rays.calls()[0].with_characters);
    // A wall alone means unseen; a wall that is a character means seen.
    StubRays walled([](std::int32_t) { return wall(); });
    SH_CHECK(!w::is_visible(head, body, 1, 2 * kQ12One, 2 * kQ12One, std::ref(walled)));
    StubRays charned([](std::int32_t) { return w::SightOutcome{true, true}; });
    SH_CHECK(w::is_visible(head, body, 1, 2 * kQ12One, 2 * kQ12One, std::ref(charned)));
    // Out of reach: 100.0 away vs reach 4.0 -> false without a ray.
    const w::LosBody far{w::Vec3{0, 0, 100 * kQ12One}, 0, 0, false};
    StubRays       idle([](std::int32_t) { return clear(); });
    SH_CHECK(!w::is_visible(head, far, 1, 2 * kQ12One, 2 * kQ12One, std::ref(idle)));
    SH_CHECK(idle.calls().size() == 0);
}

SH_TEST(world_los_visible_behind_and_head_band) {
    // Behind the watcher (rel = 2048): dist_mult = 0, and with a zero base the reach is 0 —
    // no ray for a body any distance away.
    const w::LosHead head{w::Vec3{0, 0, 0}, 0, 0};
    const w::LosBody behind{w::Vec3{0, 0, -4 * kQ12One}, 0, 0, false};
    StubRays rays([](std::int32_t) { return clear(); });
    SH_CHECK(!w::is_visible(head, behind, 1, 0, 2 * kQ12One, std::ref(rays)));
    SH_CHECK(rays.calls().size() == 0);
    // The head band flattens the ray: from the body's head (body.y + box_top), zero climb.
    const w::LosBody low{w::Vec3{0, -200, 3 * kQ12One}, 300, 50, true};
    StubRays         band([](std::int32_t) { return clear(); });
    SH_CHECK(w::is_visible(head, w::LosBody{low}, 1, 2 * kQ12One, 2 * kQ12One, std::ref(band)));
    SH_CHECK(band.calls()[0].from.y == -200 + 300);
    SH_CHECK(band.calls()[0].offset.y == 0);
}
