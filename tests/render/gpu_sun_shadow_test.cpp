// The sun's shadow map fit (render/gpu/sun_shadow_pass.h): the fog's reach, the view's points inside the map, the
// sun's axis as depth, and the texel snapping that keeps the map still while the camera moves.

#include "framework/sh_test.h"
#include "render/gpu/sun_shadow_pass.h"

#include <array>
#include <cmath>

namespace {

namespace sun = sh::gpu::sun_shadow;

constexpr std::array<double, 9> kIdentity{1, 0, 0, 0, 1, 0, 0, 0, 1};

std::array<float, 3> clip(const sun::Fit& f, float x, float y, float z) {
    const auto& m = f.caster;
    return {m[0] * x + m[4] * y + m[8] * z + m[12], m[1] * x + m[5] * y + m[9] * z + m[13],
            m[2] * x + m[6] * y + m[10] * z + m[14]};
}

bool inside(const std::array<float, 3>& c) {
    return std::abs(c[0]) <= 1.0f && std::abs(c[1]) <= 1.0f && std::abs(c[2]) <= 1.0f;
}

}  // namespace

SH_TEST(gpu_sun_shadow_fog_reach) {
    float ramp[128] = {};
    for (int i = 0; i < 128; ++i) ramp[i] = static_cast<float>(i * 2);
    // Fog on: the ramp is complete where 16 * byte + 16 reaches 4092 (byte 255; 254 at most before index 100).
    const float on[4] = {1.0f, 8.0f / 1024.0f, 16.0f, 0.0f};
    SH_CHECK(std::abs(sun::fog_reach(on, ramp, 65536.0f) - 16384.0f) < 1.0f);
    ramp[100] = 255.0f;
    SH_CHECK(std::abs(sun::fog_reach(on, ramp, 65536.0f) - 12800.0f) < 1.0f);
    SH_CHECK(sun::fog_reach(on, ramp, 4096.0f) == 4096.0f);
    const float culled[4] = {1.0f, 8.0f / 1024.0f, 0.0f, 3000.0f};
    SH_CHECK(sun::fog_reach(culled, ramp, 65536.0f) == 3000.0f);
    const float small[4] = {1.0f, 8.0f / 1024.0f, 0.0f, 100.0f};
    SH_CHECK(sun::fog_reach(small, ramp, 65536.0f) == 2048.0f);  // never below 8 m
    const float off[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    SH_CHECK(sun::fog_reach(off, ramp, 9000.0f) == 9000.0f);
}

SH_TEST(gpu_sun_shadow_fit_covers_the_view) {
    const sun::Fit f = sun::fit(kIdentity, {0, 0, 0}, {0.3f, 1.0f, 0.2f}, 256.0f, 4000.0f, 2048, false);
    SH_CHECK(f.texel > 0.0f && f.depth_range > 0.0f);
    SH_CHECK(inside(clip(f, 0, 0, 0)));
    SH_CHECK(inside(clip(f, 0, 0, 4000)));
    SH_CHECK(inside(clip(f, 2600, 1800, 3900)));
    SH_CHECK(inside(clip(f, -2600, -1800, 3900)));
    // Depth grows along the way the light travels.
    const float d = 100.0f;
    SH_CHECK(clip(f, 0.3f * d, d, 0.2f * d + 2000.0f)[2] > clip(f, 0, 0, 2000.0f)[2]);
    // The receivers' rows are flipped only for top-left targets.
    const sun::Fit t = sun::fit(kIdentity, {0, 0, 0}, {0.3f, 1.0f, 0.2f}, 256.0f, 4000.0f, 2048, true);
    SH_CHECK(t.receiver[1] == -f.caster[1] && t.receiver[13] == -f.caster[13] && t.receiver[0] == f.caster[0]);
}

SH_TEST(gpu_sun_shadow_fit_snaps_to_texels) {
    // Moving the camera by a fraction of a texel keeps a world point on the same texel centre offset.
    const std::array<float, 3> dir{0.0f, 1.0f, 0.0f};
    const sun::Fit a = sun::fit(kIdentity, {0, 0, 0}, dir, 256.0f, 4000.0f, 2048, false);
    const sun::Fit b = sun::fit(kIdentity, {-0.37, 0, 0}, dir, 256.0f, 4000.0f, 2048, false);
    // World point (100, 0, 1000) seen from both cameras: view = world + trans.
    const float ua = clip(a, 100.0f, 0.0f, 1000.0f)[0], ub = clip(b, 100.0f - 0.37f, 0.0f, 1000.0f)[0];
    const float texels = 1024.0f;
    const float fa = ua * texels - std::floor(ua * texels), fb = ub * texels - std::floor(ub * texels);
    SH_CHECK(std::abs(fa - fb) < 1e-2f);
}
