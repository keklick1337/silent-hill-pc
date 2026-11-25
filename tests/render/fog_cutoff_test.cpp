// SPDX-License-Identifier: GPL-3.0-or-later
// The fog as the draw distance (render/scene/fog_cutoff.h): where the fog the shaders apply is complete, and no
// cutoff where it never is.

#include "framework/sh_test.h"
#include "render/scene/fog_cutoff.h"

#include <cmath>

namespace {

namespace sc = sh::scene;

// A ramp rising by 2 per entry (255 past the last), 8 entries per 1024 view units, the given intensity and cull.
sh::rhi::FogRampUniforms ramp(float intensity, float cull = 0.0f) {
    sh::rhi::FogRampUniforms f;
    f.params = {1.0f, 8.0f / 1024.0f, intensity, cull};
    for (std::size_t i = 0; i < f.ramp.size(); ++i) f.ramp[i] = static_cast<float>(i * 2);
    return f;
}

}  // namespace

SH_TEST(scene_fog_cutoff_follows_the_ramp_strength_and_cull) {
    // 16 * byte + 64 reaches 4096 at byte 252: entry 126 on, so the fog is complete from 126 / (8 / 1024) view units.
    sh::rhi::FogRampUniforms f = ramp(64.0f);
    SH_CHECK(std::abs(sc::fog_full_depth(f, 1.0f, true) - 16128.0f) < 0.5f);
    SH_CHECK(std::abs(sc::fog_draw_cutoff(f, 1.0f, true) - 16128.0f * (1.0f + sc::kFogCutoffMargin)) < 1.0f);
    // A thicker fog completes sooner: 16 * byte + 64 >= 4096 / 2 from byte 124 (entry 62).
    SH_CHECK(std::abs(sc::fog_full_depth(f, 2.0f, true) - 7936.0f) < 0.5f);
    // A thinner one never does: no cutoff, the game's draw distance stands.
    SH_CHECK(sc::fog_full_depth(f, 0.9f, true) == sc::kNoFogCutoff);
    SH_CHECK(sc::fog_draw_cutoff(f, 0.0f, false) == sc::kNoFogCutoff);
    // ...unless the soft fog fades to full at the cull distance (P4), whatever the strength.
    f = ramp(64.0f, 9000.0f);
    SH_CHECK(sc::fog_full_depth(f, 0.9f, true) == 9000.0f);
    SH_CHECK(sc::fog_full_depth(f, 0.9f, false) == sc::kNoFogCutoff);
    // A negative intensity (a lighter fog) never reaches full: 255 * 16 - 64 < 4096.
    SH_CHECK(sc::fog_full_depth(ramp(-64.0f), 1.0f, true) == sc::kNoFogCutoff);
    // The classic fog takes the vertex byte, which rounds to 127 a little before the ramp is complete.
    SH_CHECK(sc::fog_full_depth(ramp(0.0f), 1.0f, false) <= sc::fog_full_depth(ramp(0.0f), 1.0f, true));
    // Fog off (interiors, the viewer's fog-off): no cutoff.
    f.params[0] = 0.0f;
    SH_CHECK(sc::fog_draw_cutoff(f, 1.0f, true) == sc::kNoFogCutoff);
}
