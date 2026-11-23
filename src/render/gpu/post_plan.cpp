// The device-free half of PostPass (post_pass.h): post uniforms from the settings and the present scaling plan.
// Kept apart from post_pass.cpp, which reads the live config globals, so the unit tests link without them.
#include "render/gpu/post_pass.h"

#include <algorithm>

namespace sh::gpu {

PostUniforms post_uniforms(const PostInputs& in) {
    const post::PostSettings& s = in.settings;
    // Out of scope = neutral values rather than no pass: a look filter or the tonemap may still need it.
    const post::ImageAdjust adj = post::effectiveAdjust(s);
    PostUniforms u;
    u.mode = {float(s.postProcess), float(s.tonemap), s.fxaa ? 1.f : 0.f, s.calibBar ? 1.f : 0.f};
    u.mix = {in.post_intensity, in.tonemap_intensity, float(in.frame & 1023u),
             in.frag_coord_top_left ? float(in.height) : 0.f};
    u.adjust = {adj.brightness, adj.contrast, adj.saturation, in.sample_flip_y ? 1.f : 0.f};
    u.texel = {in.width > 0 ? 1.f / float(in.width) : 0.f, in.height > 0 ? 1.f / float(in.height) : 0.f, 0.f, 0.f};
    return u;
}

namespace {

// Centre a w x h image in `dst`. An odd spare row goes to the top (the centre is taken in bottom-left space):
// the picture must not move by a line between renderers.
rhi::Rect centred(const rhi::Rect& dst, int w, int h) {
    const int spare_y = dst.h - h;
    return {dst.x + (dst.w - w) / 2, dst.y + spare_y - spare_y / 2, w, h};
}

PresentPlan single(int src_w, int src_h, const rhi::Rect& dst, rhi::Filter filter) {
    PresentPlan p;
    p.count = 1;
    p.steps[0] = {false, {0, 0, src_w, src_h}, false, dst, filter};
    return p;
}

}  // namespace

PresentPlan plan_present(PresentScale mode, bool native_render, int src_w, int src_h, const rhi::Rect& dst) {
    if (src_w <= 0 || src_h <= 0 || dst.w <= 0 || dst.h <= 0) return {};
    if (dst.w == src_w && dst.h == src_h) return single(src_w, src_h, dst, rhi::Filter::Nearest);

    // A frame larger than the rect (render_scale above 1) has no whole multiple to snap to: downsample linearly.
    if (mode == PresentScale::Nearest || mode == PresentScale::Bilinear || src_w > dst.w || src_h > dst.h)
        return single(src_w, src_h, dst, mode == PresentScale::Nearest ? rhi::Filter::Nearest : rhi::Filter::Linear);

    const int n = std::max(1, std::min(dst.w / src_w, dst.h / src_h));

    if (mode == PresentScale::Integer && native_render) {
        // PSX modes have non-square pixels (320x224 fills 4:3): whole multiples go on the lines, as a TV
        // scans them, and the width follows 4:3.
        int k = std::max(1, dst.h / src_h);
        const int w43 = src_h * 4 / 3;
        while (k > 1 && w43 * k > dst.w) --k;
        const int ih = src_h * k;
        return single(src_w, src_h, centred(dst, ih * 4 / 3, ih), rhi::Filter::Nearest);
    }
    if (mode == PresentScale::Integer)
        return single(src_w, src_h, centred(dst, src_w * n, src_h * n), rhi::Filter::Nearest);

    // Sharp bilinear: a crisp whole-multiple intermediate, then only the leftover fraction is eased. At 1x the
    // intermediate buys nothing.
    if (n < 2) return single(src_w, src_h, dst, rhi::Filter::Linear);
    PresentPlan p;
    p.count = 2;
    p.intermediate_w = src_w * n;
    p.intermediate_h = src_h * n;
    p.steps[0] = {false, {0, 0, src_w, src_h}, true, {0, 0, p.intermediate_w, p.intermediate_h}, rhi::Filter::Nearest};
    p.steps[1] = {true, {0, 0, p.intermediate_w, p.intermediate_h}, false, dst, rhi::Filter::Linear};
    return p;
}

}  // namespace sh::gpu
