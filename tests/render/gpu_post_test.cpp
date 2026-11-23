// sh::gpu post pass decisions (render/gpu/post_pass.h): settings -> post uniforms, scale mode -> present blits.

#include "framework/sh_test.h"
#include "render/gpu/post_pass.h"

namespace {

using sh::gpu::BlitStep;
using sh::gpu::PostInputs;
using sh::gpu::PresentPlan;
using sh::gpu::PresentScale;
using sh::rhi::Filter;
using sh::rhi::Rect;

bool same(const Rect& a, const Rect& b) { return a.x == b.x && a.y == b.y && a.w == b.w && a.h == b.h; }

PostInputs inputs() {
    PostInputs in;
    in.width = 640;
    in.height = 480;
    return in;
}

}  // namespace

SH_TEST(gpu_post_uniforms_settings) {
    PostInputs in = inputs();
    in.settings.postProcess = 5;
    in.settings.tonemap = 2;
    in.settings.fxaa = true;
    in.settings.calibBar = true;
    in.settings.brightness = 1.5f;
    in.settings.contrast = 0.8f;
    in.settings.saturation = 0.25f;
    in.post_intensity = 0.5f;
    in.tonemap_intensity = 0.75f;
    in.frame = 1024 + 7;  // the grain time wraps at 1024
    const auto u = sh::gpu::post_uniforms(in);
    SH_CHECK(u.mode[0] == 5.f && u.mode[1] == 2.f && u.mode[2] == 1.f && u.mode[3] == 1.f);
    SH_CHECK(u.mix[0] == 0.5f && u.mix[1] == 0.75f && u.mix[2] == 7.f && u.mix[3] == 0.f);
    SH_CHECK(u.adjust[0] == 1.5f && u.adjust[1] == 0.8f && u.adjust[2] == 0.25f && u.adjust[3] == 0.f);
    SH_CHECK(u.texel[0] == 1.f / 640.f && u.texel[1] == 1.f / 480.f);
}

SH_TEST(gpu_post_uniforms_adjust_out_of_scope_is_neutral) {
    PostInputs in = inputs();
    in.settings.brightness = 2.f;
    in.settings.contrast = 0.5f;
    in.settings.saturation = 0.f;
    in.settings.adjustActive = false;
    const auto u = sh::gpu::post_uniforms(in);
    SH_CHECK(u.adjust[0] == 1.f && u.adjust[1] == 1.f && u.adjust[2] == 1.f);
}

SH_TEST(gpu_post_uniforms_backend_flips) {
    PostInputs in = inputs();
    in.sample_flip_y = true;
    in.frag_coord_top_left = true;
    const auto u = sh::gpu::post_uniforms(in);
    SH_CHECK(u.adjust[3] == 1.f);
    SH_CHECK(u.mix[3] == 480.f);
    in.width = in.height = 0;
    const auto z = sh::gpu::post_uniforms(in);
    SH_CHECK(z.texel[0] == 0.f && z.texel[1] == 0.f);
}

SH_TEST(gpu_post_pass_needed) {
    sh::post::PostSettings s;
    SH_CHECK(!sh::post::needsPostPass(s));
    s.calibBar = true;
    SH_CHECK(sh::post::needsPostPass(s));
    s.adjustActive = false;  // world-only scope on a UI frame: the bar alone does not run the pass
    SH_CHECK(!sh::post::needsPostPass(s));
    s.fxaa = true;
    SH_CHECK(sh::post::needsPostPass(s));
    SH_CHECK(sh::post::imageAdjustActive(0, false, false));
    SH_CHECK(!sh::post::imageAdjustActive(1, false, false));
    SH_CHECK(sh::post::imageAdjustActive(1, false, true));
}

SH_TEST(gpu_present_same_size_is_one_nearest_copy) {
    const PresentPlan p = sh::gpu::plan_present(PresentScale::Bilinear, false, 640, 480, {80, 0, 640, 480});
    SH_CHECK(p.count == 1 && p.intermediate_w == 0);
    SH_CHECK(same(p.steps[0].src, {0, 0, 640, 480}) && same(p.steps[0].dst, {80, 0, 640, 480}));
    SH_CHECK(p.steps[0].filter == Filter::Nearest);
}

SH_TEST(gpu_present_stretch_modes) {
    const Rect dst{0, 0, 1920, 1080};
    PresentPlan p = sh::gpu::plan_present(PresentScale::Nearest, false, 640, 480, dst);
    SH_CHECK(p.count == 1 && same(p.steps[0].dst, dst) && p.steps[0].filter == Filter::Nearest);
    p = sh::gpu::plan_present(PresentScale::Bilinear, false, 640, 480, dst);
    SH_CHECK(p.count == 1 && same(p.steps[0].dst, dst) && p.steps[0].filter == Filter::Linear);
    // A frame larger than the rect is downsampled linearly whatever the mode.
    p = sh::gpu::plan_present(PresentScale::Integer, false, 2560, 1440, dst);
    SH_CHECK(p.count == 1 && same(p.steps[0].dst, dst) && p.steps[0].filter == Filter::Linear);
    // Nothing to draw into.
    SH_CHECK(sh::gpu::plan_present(PresentScale::Bilinear, false, 640, 480, {0, 0, 0, 0}).count == 0);
    SH_CHECK(sh::gpu::plan_present(PresentScale::Bilinear, false, 0, 480, dst).count == 0);
}

SH_TEST(gpu_present_integer_centres_whole_multiple) {
    // 640x480 into 1440x1080 at x 240: n = 2 (1280x960), centred; the odd spare goes to the top like GL's.
    const PresentPlan p = sh::gpu::plan_present(PresentScale::Integer, false, 640, 480, {240, 0, 1440, 1081});
    SH_CHECK(p.count == 1);
    SH_CHECK(same(p.steps[0].dst, {240 + 80, 61, 1280, 960}));
    SH_CHECK(p.steps[0].filter == Filter::Nearest);
}

SH_TEST(gpu_present_integer_native_scales_lines) {
    // 320x224 native into 1440x1080: k = 4 lines (896), width 4:3 of it (1194).
    const PresentPlan p = sh::gpu::plan_present(PresentScale::Integer, true, 320, 224, {240, 0, 1440, 1080});
    SH_CHECK(p.count == 1);
    const Rect d = p.steps[0].dst;
    SH_CHECK(d.h == 896 && d.w == 896 * 4 / 3);
    SH_CHECK(d.x == 240 + (1440 - d.w) / 2 && d.y == 92);
    // A narrow rect walks k down until the 4:3 width fits.
    const PresentPlan q = sh::gpu::plan_present(PresentScale::Integer, true, 320, 224, {0, 0, 700, 1080});
    SH_CHECK(q.steps[0].dst.h == 448 && q.steps[0].dst.w <= 700);
}

SH_TEST(gpu_present_sharp_bilinear) {
    const Rect dst{0, 0, 1920, 1080};
    const PresentPlan p = sh::gpu::plan_present(PresentScale::SharpBilinear, false, 640, 480, dst);
    SH_CHECK(p.count == 2);
    SH_CHECK(p.intermediate_w == 1280 && p.intermediate_h == 960);
    const BlitStep& a = p.steps[0];
    const BlitStep& b = p.steps[1];
    SH_CHECK(!a.from_intermediate && a.to_intermediate && a.filter == Filter::Nearest);
    SH_CHECK(same(a.dst, {0, 0, 1280, 960}));
    SH_CHECK(b.from_intermediate && !b.to_intermediate && b.filter == Filter::Linear && same(b.dst, dst));
    // At 1x the intermediate is skipped: a plain linear stretch.
    const PresentPlan one = sh::gpu::plan_present(PresentScale::SharpBilinear, false, 640, 480, {0, 0, 1000, 700});
    SH_CHECK(one.count == 1 && one.intermediate_w == 0 && one.steps[0].filter == Filter::Linear);
}
