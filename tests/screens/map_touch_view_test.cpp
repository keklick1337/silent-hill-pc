// SPDX-License-Identifier: GPL-3.0-or-later
//
// The paper map under the fingers (engine/screens/map_touch_view.h): the zoom limits, a zoom about a point, the
// bounds at rest, the rubber band and its spring back, the glide after a release, a double tap in and out, and the
// pinch.

#include "framework/sh_test.h"

#include "engine/screens/map_touch_view.h"

#include <algorithm>
#include <cmath>

using namespace sh::screens;

namespace {

bool near(float a, float b, float eps = 0.01f) { return std::fabs(a - b) <= eps; }

constexpr MapTouchVec kPage{320.f, 240.f};
constexpr MapTouchVec k43{320.f, 240.f};
constexpr MapTouchVec kWide{568.f, 240.f};

MapTouchView view_of(MapTouchVec viewport) {
    MapTouchView v;
    v.set_frame(kPage, viewport);
    return v;
}

void run(MapTouchView& v, float seconds) {
    for (float t = 0.f; t < seconds; t += 1.f / 60.f) v.update(1.f / 60.f);
}

}  // namespace

SH_TEST(map_touch_view_limits) {
    MapTouchView v = view_of(kWide);
    SH_CHECK(near(v.fit_scale(), 1.f));
    SH_CHECK(near(v.max_scale(), 3.f));
    SH_CHECK(near(v.scale(), 1.f));
    // Narrower than the viewport: centred across, covering down.
    SH_CHECK(near(v.offset().x, 124.f) && near(v.offset().y, 0.f));
    SH_CHECK(v.at_fit() && v.settled());

    // A viewport taller than the page's shape fits the width.
    MapTouchView tall = view_of({320.f, 480.f});
    SH_CHECK(near(tall.fit_scale(), 1.f));
    SH_CHECK(near(tall.offset().y, 120.f));
    MapTouchView small = view_of({160.f, 240.f});
    SH_CHECK(near(small.fit_scale(), 0.5f));

    v.zoom_about({284.f, 120.f}, 10.f);
    SH_CHECK(near(v.scale(), v.max_scale()));
    v.zoom_about({284.f, 120.f}, 0.01f);
    SH_CHECK(near(v.scale(), v.fit_scale()));
}

SH_TEST(map_touch_view_zoom_about_keeps_the_point) {
    MapTouchView v = view_of(k43);
    const MapTouchVec at{100.f, 50.f};
    const MapTouchVec page = v.to_page(at);
    v.zoom_about(at, 2.f);
    SH_CHECK(near(v.scale(), 2.f));
    const MapTouchVec back = v.to_screen(page);
    SH_CHECK(near(back.x, at.x) && near(back.y, at.y));
    // Again from the zoomed view, nowhere near an edge.
    const MapTouchVec mid{160.f, 120.f};
    const MapTouchVec page2 = v.to_page(mid);
    v.zoom_about(mid, 1.25f);
    const MapTouchVec back2 = v.to_screen(page2);
    SH_CHECK(near(back2.x, mid.x) && near(back2.y, mid.y));
}

SH_TEST(map_touch_view_bounds_at_rest) {
    MapTouchView v = view_of(k43);
    v.set_view(2.f, {50.f, 70.f});
    SH_CHECK(near(v.offset().x, 0.f) && near(v.offset().y, 0.f));
    v.set_view(2.f, {-1000.f, -1000.f});
    SH_CHECK(near(v.offset().x, -320.f) && near(v.offset().y, -240.f));
    // Out of the limits, the view comes back into them.
    v.set_view(0.25f, {0.f, 0.f});
    SH_CHECK(near(v.scale(), 1.f) && near(v.offset().x, 0.f) && near(v.offset().y, 0.f));

    MapTouchView w = view_of(kWide);
    w.set_view(1.5f, {400.f, 0.f});
    // 480 wide in 568: still centred across.
    SH_CHECK(near(w.offset().x, 44.f));
    SH_CHECK(near(w.min_offset(1.5f).y, -120.f) && near(w.max_offset(1.5f).y, 0.f));
}

SH_TEST(map_touch_view_pan_is_one_to_one_inside) {
    MapTouchView v = view_of(k43);
    v.set_view(2.f, {-160.f, -120.f});
    v.begin_pan();
    v.pan({30.f, -20.f});
    SH_CHECK(near(v.offset().x, -130.f) && near(v.offset().y, -140.f));
    v.pan({-5.f, 5.f});
    SH_CHECK(near(v.offset().x, -135.f) && near(v.offset().y, -135.f));
}

SH_TEST(map_touch_view_rubber_band_and_spring_back) {
    MapTouchView v = view_of(k43);
    v.begin_pan();
    v.pan({40.f, 0.f});
    const float first = v.offset().x;
    SH_CHECK(first > 0.f && first < 40.f);  // less than 1:1 past the edge
    v.pan({40.f, 0.f});
    SH_CHECK(v.offset().x - first < first);  // and less the farther it goes
    v.pan({5000.f, 5000.f});
    // Never farther than the band's reach (half the viewport): the page cannot be lost.
    SH_CHECK(v.offset().x < 160.f && v.offset().y < 120.f);
    // Back the other way from the stretch follows the finger without a jump.
    const float stretched = v.offset().x;
    v.pan({-1.f, 0.f});
    SH_CHECK(v.offset().x < stretched && v.offset().x > stretched - 1.f);

    v.release({0.f, 0.f});
    SH_CHECK(!v.settled());
    run(v, 2.f);
    SH_CHECK(v.settled());
    SH_CHECK(near(v.offset().x, 0.f) && near(v.offset().y, 0.f));
}

SH_TEST(map_touch_view_glide_slows_and_stops) {
    MapTouchView v = view_of(k43);
    v.set_view(3.f, {-320.f, -240.f});
    v.begin_pan();
    v.release({200.f, 0.f});
    float last = v.offset().x;
    float step = 1e9f;
    for (int i = 0; i < 30; ++i) {
        v.update(1.f / 60.f);
        const float moved = v.offset().x - last;
        SH_CHECK(moved > 0.f && moved < step);
        step = moved;
        last = v.offset().x;
    }
    run(v, 5.f);
    SH_CHECK(v.settled());
    // A glide covers about speed / friction (100 units); the stop speed takes the last bit.
    SH_CHECK(v.offset().x > -320.f + 95.f && v.offset().x < -320.f + 100.5f);
    SH_CHECK(near(v.offset().y, -240.f));
}

SH_TEST(map_touch_view_glide_past_the_edge_springs_back) {
    MapTouchView v = view_of(k43);
    v.set_view(2.f, {-20.f, -120.f});
    v.begin_pan();
    v.release({800.f, 0.f});
    float most = 0.f;
    for (int i = 0; i < 240; ++i) {
        v.update(1.f / 60.f);
        most = std::max(most, v.offset().x);
    }
    SH_CHECK(most > 0.f && most < 160.f);
    SH_CHECK(v.settled());
    SH_CHECK(near(v.offset().x, 0.f));
}

SH_TEST(map_touch_view_double_tap_in_and_out) {
    MapTouchView v = view_of(k43);
    v.double_tap({80.f, 60.f});
    SH_CHECK(!v.at_fit());
    run(v, 1.f);
    SH_CHECK(near(v.scale(), 2.f));
    // The tapped page point comes to the centre.
    const MapTouchVec c = v.to_screen({80.f, 60.f});
    SH_CHECK(near(c.x, 160.f) && near(c.y, 120.f));
    SH_CHECK(v.settled());

    // Near a corner the centring stops at the bounds.
    MapTouchView w = view_of(k43);
    w.double_tap({0.f, 0.f});
    run(w, 1.f);
    SH_CHECK(near(w.offset().x, 0.f) && near(w.offset().y, 0.f));

    v.double_tap({200.f, 200.f});
    run(v, 1.f);
    SH_CHECK(v.at_fit() && v.settled());
    SH_CHECK(near(v.scale(), 1.f) && near(v.offset().x, 0.f) && near(v.offset().y, 0.f));
}

SH_TEST(map_touch_view_pinch) {
    MapTouchView v = view_of(k43);
    const MapTouchVec mid{120.f, 100.f};
    const MapTouchVec page = v.to_page(mid);
    v.begin_pinch(mid, 100.f);
    SH_CHECK(!v.at_fit());
    v.pinch(mid, 200.f);
    SH_CHECK(near(v.scale(), 2.f));
    MapTouchVec under = v.to_screen(page);
    SH_CHECK(near(under.x, mid.x) && near(under.y, mid.y));
    // The midpoint moving pans with it.
    v.pinch({140.f, 90.f}, 200.f);
    under = v.to_screen(page);
    SH_CHECK(near(under.x, 140.f) && near(under.y, 90.f));

    // Past the largest zoom it stretches less and less, and eases back after the release.
    v.pinch({140.f, 90.f}, 1000.f);
    SH_CHECK(v.scale() > v.max_scale() && v.scale() < 10.f);
    v.release({500.f, 500.f});
    run(v, 1.f);
    SH_CHECK(near(v.scale(), v.max_scale()));
    SH_CHECK(v.settled());

    // Below fit it comes back to fit, centred.
    v.begin_pinch({160.f, 120.f}, 300.f);
    v.pinch({160.f, 120.f}, 30.f);
    SH_CHECK(v.scale() < v.fit_scale() && v.scale() > 0.f);
    v.release({0.f, 0.f});
    run(v, 1.f);
    SH_CHECK(v.at_fit() && v.settled());
    SH_CHECK(near(v.offset().x, 0.f) && near(v.offset().y, 0.f));
}

SH_TEST(map_touch_view_new_viewport_keeps_the_zoom) {
    MapTouchView v = view_of(k43);
    v.set_view(2.f, {-160.f, -120.f});
    v.set_frame(kPage, kWide);
    SH_CHECK(near(v.scale(), 2.f));
    const MapTouchVec c = v.to_page({kWide.x * 0.5f, kWide.y * 0.5f});
    SH_CHECK(near(c.x, 160.f) && near(c.y, 120.f));
}
