// SPDX-License-Identifier: GPL-3.0-or-later
// Unit tests for the drain splash (sh::effects::DrainSplash) with a scripted host.

#include "engine/effects/drain_splash.h"

#include <vector>

#include "framework/sh_test.h"

using namespace sh::effects;

namespace {

class Host final : public DrainSplashHost {
public:
    std::int32_t seed = 12345;
    std::vector<SplashQuad> quads;
    int modes = 0;
    ScreenPoint probe{100, 100, 400};

    std::int32_t rand16() override {
        seed = (seed * 1103515245 + 12345) & 0x7FFFFFFF;
        return seed >> 16;
    }
    std::int32_t sin(std::int32_t) override { return 2048; }
    std::int32_t cos(std::int32_t) override { return 2048; }
    std::int32_t delta_time() override { return 68; }
    void begin_view(const Vec3&, std::int32_t) override {}
    ScreenPoint project_origin() override { return probe; }
    ScreenPoint project(const Vec3s& v) override { return ScreenPoint{static_cast<std::int16_t>(100 + v.x), static_cast<std::int16_t>(100 + v.y), 400}; }
    void emit_quad(const SplashQuad& q, std::int32_t) override { quads.push_back(q); }
    void emit_mode(std::int32_t, std::int32_t, std::int32_t, std::int32_t) override { modes++; }
};

DrainSplashParams params() {
    DrainSplashParams p;
    p.start_delay = 100;
    for (std::size_t i = 0; i < p.shade.size(); i++) {
        p.shade[i] = 0x3A000000u + static_cast<std::uint32_t>(i);
    }
    return p;
}

}  // namespace

SH_TEST(drain_splash_start_delay_draws_nothing) {
    DrainSplash s(params());
    Host h;
    s.start(h, Vec3{4096, 0, 4096});
    s.update(h);
    SH_CHECK(h.modes == 0);
    SH_CHECK(s.state().delay == 32);
}

SH_TEST(drain_splash_frames_draw_and_close) {
    DrainSplash s(params());
    Host h;
    s.start(h, Vec3{0, 0, 0});
    for (int i = 0; i < 80; i++) {
        s.update(h);
    }
    SH_CHECK(h.modes == 78);  // the first two frames wait out the delay
    SH_CHECK(!h.quads.empty());
}

SH_TEST(drain_splash_params_reject_short_shade) {
    SH_CHECK(!parse_drain_splash_params("{\"start_delay\":1}"));
}
