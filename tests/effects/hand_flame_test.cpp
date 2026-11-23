// The match flame in Harry's hand (src/engine/effects/hand_flame.h): its random words, the depth cut and the quad.

#include "engine/effects/hand_flame.h"

#include "framework/sh_test.h"

#include <vector>

namespace {

namespace fx = sh::effects;

class FakeHost final : public fx::HandFlameHost {
public:
    std::uint32_t seed = 5;
    int draws = 0;
    std::int32_t dt = 136;
    std::int32_t depth = 400;
    std::vector<std::pair<sh::draw3d::Polygon, std::int32_t>> emitted;

    std::int32_t rand16() override {
        draws++;
        seed = seed * 1103515245u + 12345u;
        return static_cast<std::int32_t>((seed >> 16) & 0x7FFF);
    }
    std::int32_t delta_time() override { return dt; }
    fx::ScreenPoint project_hand(const fx::Vec3s&) override { return {10, 20, depth}; }
    fx::ScreenPoint project_flat(const fx::Vec3s& v) override {
        return {static_cast<std::int16_t>(v.x / 2), static_cast<std::int16_t>(v.y / 2), 0};
    }
    void emit(const sh::draw3d::Polygon& p, std::int32_t bucket) override { emitted.emplace_back(p, bucket); }
};

}  // namespace

SH_TEST(hand_flame_draws_a_quad_at_the_hand) {
    fx::HandFlame flame;
    FakeHost host;
    flame.draw(host);
    SH_CHECK(host.draws == 8 && host.emitted.size() == 1);
    if (host.emitted.empty()) return;
    const sh::draw3d::Polygon& q = host.emitted[0].first;
    SH_CHECK(host.emitted[0].second == (400 >> 1) - 2);
    SH_CHECK(q.textured && q.shade.translucent && q.v[0].uv.u == 128 && q.v[3].uv.u == 191);
    // The corners sit around the hand point, about 25 pixels out (the projection halves the offsets).
    SH_CHECK(q.v[0].xy.x < 10 && q.v[1].xy.x > 10 && q.v[0].xy.y < 20 && q.v[2].xy.y > 20);
    // A paused frame draws no new random words.
    host.dt = 0;
    flame.draw(host);
    SH_CHECK(host.draws == 8 && host.emitted.size() == 2 && host.emitted[1].first.v[0].uv.u == 128);
}

SH_TEST(hand_flame_skipped_too_close) {
    fx::HandFlame flame;
    FakeHost host;
    host.depth = 32;  // 128 or nearer
    flame.draw(host);
    SH_CHECK(host.emitted.empty());
}
