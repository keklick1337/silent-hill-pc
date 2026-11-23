// Post-process policy, shadow light and frozen-frame state (render/post).

#include "framework/sh_test.h"
#include "render/post/post.h"

#include <cmath>
#include <cstring>

namespace p = sh::post;

SH_TEST(post_image_adjust_scope) {
    SH_CHECK(p::imageAdjustActive(0, false, false));
    SH_CHECK(!p::imageAdjustActive(1, false, false));
    SH_CHECK(p::imageAdjustActive(1, true, false));
    SH_CHECK(p::imageAdjustActive(1, false, true));
}

SH_TEST(post_needs_pass) {
    p::PostSettings s;
    SH_CHECK(!p::needsPostPass(s));
    s.brightness = 1.2f;
    SH_CHECK(p::needsPostPass(s));
    s.adjustActive = false;
    SH_CHECK(!p::needsPostPass(s));
    s.fxaa = true;
    SH_CHECK(p::needsPostPass(s));
    SH_CHECK(p::effectiveAdjust(s).brightness == 1.f);
}

SH_TEST(post_shadow_map_size) {
    SH_CHECK(p::clampShadowMapSize(10, 0) == 256);
    SH_CHECK(p::clampShadowMapSize(99999, 0) == 8192);
    SH_CHECK(p::clampShadowMapSize(8192, 4096) == 4096);
    SH_CHECK(p::clampShadowMapSize(2048, 1024) == 2048);
}

SH_TEST(post_shadow_light) {
    p::ShadowLightInput in;
    in.direction = {0.f, 0.f, 2.f};
    in.range = 1000.f;
    const auto l = p::buildShadowLight(in);
    SH_CHECK(l.zNear == 20.f);
    SH_CHECK(std::fabs(l.zFar - 1300.f) < 1e-3f);
    // A point straight ahead of the light projects to the centre of the light's clip space.
    const float z = 500.f;
    const float cx = l.matrix[0] * 0 + l.matrix[4] * 0 + l.matrix[8] * z + l.matrix[12];
    const float cw = l.matrix[3] * 0 + l.matrix[7] * 0 + l.matrix[11] * z + l.matrix[15];
    SH_CHECK(std::fabs(cx) < 1e-3f);
    SH_CHECK(cw > 0.f);
}

SH_TEST(post_freeze_frame) {
    p::FreezeFrame f;
    SH_CHECK(f.shouldCapture(false));
    SH_CHECK(!f.shouldCapture(true));
    f.resized(640, 480);
    SH_CHECK(!f.valid() && !f.sizeDiffers(640, 480) && f.sizeDiffers(1, 1));
    f.captured();
    SH_CHECK(f.valid());
    f.presented();
    SH_CHECK(!f.shouldCapture(false));
    SH_CHECK(f.shouldCapture(false));
}
