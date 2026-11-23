// Present-path math (render/present): scaled size, present rectangle, pixel aspect, point-sampled set.

#include "framework/sh_test.h"
#include "render/present/present_math.h"

namespace pr = sh::present;

SH_TEST(present_scaled_size) {
    auto s = pr::scaled_render_size(1280, 720, 0.5f);
    SH_CHECK(s.w == 640 && s.h == 360);
    s = pr::scaled_render_size(1, 1, 0.1f);
    SH_CHECK(s.w == 1 && s.h == 1);
    s = pr::scaled_render_size(640, 480, 0.0f);
    SH_CHECK(s.w == 640 && s.h == 480);
}

SH_TEST(present_msaa_sample_count) {
    SH_CHECK(pr::msaa_sample_count(0, 8) == 1);
    SH_CHECK(pr::msaa_sample_count(1, 8) == 1);
    SH_CHECK(pr::msaa_sample_count(2, 8) == 2);
    SH_CHECK(pr::msaa_sample_count(3, 8) == 2);
    SH_CHECK(pr::msaa_sample_count(4, 8) == 4);
    SH_CHECK(pr::msaa_sample_count(8, 8) == 8);
    SH_CHECK(pr::msaa_sample_count(16, 32) == 8);
    SH_CHECK(pr::msaa_sample_count(8, 4) == 4);   // the device's limit wins
    SH_CHECK(pr::msaa_sample_count(8, 1) == 1);   // a backend without MSAA
    SH_CHECK(pr::msaa_sample_count(4, 0) == 1);
}

SH_TEST(present_supersample_cap) {
    constexpr long long k5k = 5120LL * 2880LL;
    // 1080p at 2x is under the cap and passes; so does a target not larger than the window.
    auto s = pr::cap_supersampled_size(pr::scaled_render_size(1920, 1080, 2.0f), 1920, 1080, k5k, 16384);
    SH_CHECK(s.w == 3840 && s.h == 2160);
    s = pr::cap_supersampled_size({960, 540}, 1920, 1080, 1, 1);
    SH_CHECK(s.w == 960 && s.h == 540);
    // 4K at 2x is shrunk to the pixel cap with its aspect kept.
    s = pr::cap_supersampled_size(pr::scaled_render_size(3840, 2160, 2.0f), 3840, 2160, k5k, 16384);
    SH_CHECK(s.w <= 5120 && s.h <= 2880 && s.w >= 5110 && s.h >= 2870);
    SH_CHECK(static_cast<long long>(s.w) * s.h <= k5k);
    // The device's side limit wins, but never below the window.
    s = pr::cap_supersampled_size({7680, 4320}, 3840, 2160, 0, 4096);
    SH_CHECK(s.w == 4096 && s.h >= 2160 && s.h <= 2304);
    s = pr::cap_supersampled_size({7680, 4320}, 3840, 2160, 0, 2048);
    SH_CHECK(s.w == 3840 && s.h == 2160);
}

SH_TEST(present_native_rect_is_4_3) {
    pr::PresentRectInput in;
    in.present_w = 1920; in.present_h = 1080; in.native_render = true;
    const auto r = pr::present_rect(in);
    SH_CHECK(r.h == 1080 && r.w == 1440 && r.x == 240 && r.y == 0);
}

SH_TEST(present_pillarbox_keeps_aspect) {
    pr::PresentRectInput in;
    in.present_w = 1920; in.present_h = 1080; in.internal_w = 640; in.internal_h = 480;
    in.hor_plus = false; in.menu_pillarbox = true;
    auto r = pr::present_rect(in);
    SH_CHECK(r.w == 1440 && r.x == 240);
    in.menu_pillarbox = false;
    r = pr::present_rect(in);
    SH_CHECK(r.w == 1920 && r.x == 0);
}

SH_TEST(present_wide_factor_and_nearest) {
    SH_CHECK(pr::ui2d_wide_factor(true, 1920, 1080) == 1.0f);
    SH_CHECK(pr::ui2d_wide_factor(false, 640, 480) == 1.0f);
    SH_CHECK(pr::ui2d_wide_factor(false, 1920, 1080) > 1.3f);
    pr::NearestTextureSet set;
    set.mark(0);
    set.mark(7);
    SH_CHECK(set.contains(7) && !set.contains(0) && !set.contains(8));
}

SH_TEST(present_pixel_aspect) {
    pr::PixelAspectInput in;
    in.disp_w = 320; in.disp_h = 240;
    SH_CHECK(in.raw == false);
    const float par = pr::display_pixel_aspect(in);
    SH_CHECK(par > 0.999f && par < 1.001f);
    in.raw = true; in.configured_par = 1.25f;
    SH_CHECK(pr::display_pixel_aspect(in) == 1.25f);
}

SH_TEST(present_pointer_to_overlay) {
    pr::PointerInput in;
    in.window_w = in.drawable_w = 1920;
    in.window_h = in.drawable_h = 1080;
    in.present = {0, 0, 1920, 1080};
    in.target_w = 1920;
    in.target_h = 1080;
    auto p = pr::pointer_to_overlay(in, 300, 200);
    SH_CHECK(p.x == 300 && p.y == 200);
    // render_scale 2: the overlay is laid out in a target twice the window
    in.target_w = 3840;
    in.target_h = 2160;
    p = pr::pointer_to_overlay(in, 300, 200);
    SH_CHECK(p.x == 600 && p.y == 400);
    // the same frame with the overlay drawn onto the window: back-buffer pixels, the target does not matter
    in.overlay_to_window = true;
    p = pr::pointer_to_overlay(in, 300, 200);
    SH_CHECK(p.x == 300 && p.y == 200);
    // high-density display: pointer units are half the back buffer
    in.window_w = 960;
    in.window_h = 540;
    p = pr::pointer_to_overlay(in, 300, 200);
    SH_CHECK(p.x == 600 && p.y == 400);
    // a target placed inside bars
    in = {};
    in.window_w = in.drawable_w = 1920;
    in.window_h = in.drawable_h = 1080;
    in.present = {240, 0, 1440, 1080};
    in.target_w = 2880;
    in.target_h = 2160;
    p = pr::pointer_to_overlay(in, 240, 540);
    SH_CHECK(p.x == 0 && p.y == 1080);
    p = pr::pointer_to_overlay(in, 960, 0);
    SH_CHECK(p.x == 1440 && p.y == 0);
    // nothing known yet: the position passes through
    p = pr::pointer_to_overlay(pr::PointerInput{}, 7, 9);
    SH_CHECK(p.x == 7 && p.y == 9);
}
