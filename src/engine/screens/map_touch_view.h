// SPDX-License-Identifier: GPL-3.0-or-later
//
// The paper map under the fingers, as a browser shows a page on a touch screen: one finger pans 1:1 and glides on
// after the release, two fingers pinch about their midpoint, the page stretches past its edges on a rubber band and
// springs back, a double tap zooms in on the tapped point and out again. The state is the page picture's scale and
// offset in one plane: screen = offset + page * scale. The units are the caller's; they only have to be the same on
// both axes. No SDL, no renderer, no game.
#pragma once

namespace sh::screens {

struct MapTouchVec {
    float x = 0.f;
    float y = 0.f;
};

struct MapTouchTuning {
    float max_zoom = 3.0f;  // largest scale, times the fit scale
    float tap_zoom = 2.0f;  // the scale a double tap zooms in to, times the fit scale
    // Velocity falls by exp(-friction * t): 2/s is a browser's normal deceleration (0.998 a millisecond).
    float friction = 2.0f;
    float band_slope = 0.55f;  // how much of the finger's movement the page follows past an edge, at the edge
    float band_reach = 0.5f;   // the farthest the page stretches past an edge, a share of the viewport
    float scale_band = 0.4f;   // exponent of the stretch past the zoom limits
    float spring = 18.0f;      // rad/s of the critically damped spring back into the bounds
    float ease_time = 0.25f;   // seconds of a double tap's zoom and of the zoom's return into its limits
    float stop_speed = 4.0f;   // units/s below which a glide stops
    float max_speed = 4.0f;    // a release's speed limit, viewport heights per second
};

class MapTouchView {
public:
    explicit MapTouchView(MapTouchTuning tuning = {}) : tuning_(tuning) {}

    // The page's size and the viewport's. The first call puts the page at fit; a later change keeps the page
    // point in the viewport's centre and the zoom relative to fit.
    void set_frame(MapTouchVec page, MapTouchVec viewport);
    // At fit, centred, nothing moving.
    void reset();
    // Takes over a view shown by other means (the pad's zoom), clamped as at rest.
    void set_view(float scale, MapTouchVec offset);

    float fit_scale() const;
    float max_scale() const;
    float scale() const { return scale_; }
    MapTouchVec offset() const { return offset_; }
    MapTouchVec viewport() const { return viewport_; }
    MapTouchVec to_screen(MapTouchVec page_point) const;
    MapTouchVec to_page(MapTouchVec screen_point) const;

    // The whole page fits and nothing zooms: a vertical swipe turns the page here.
    bool at_fit() const;
    // No finger, no glide, no spring, no zoom animation.
    bool settled() const;

    // Zooms by `factor` at once, keeping the page point under `point` there (limits and bounds clamp it).
    void zoom_about(MapTouchVec point, float factor);
    // One finger: the page follows the movement, with resistance past the bounds. The first call after another
    // gesture starts the drag.
    void begin_pan();
    void pan(MapTouchVec delta);
    // Two fingers: zoom by the ratio of the distances and move with the midpoint, from the state at the start.
    void begin_pinch(MapTouchVec mid, float distance);
    void pinch(MapTouchVec mid, float distance);
    // The fingers are up: a drag glides on at `velocity` (units/s); a pinch past the zoom limits eases back.
    void release(MapTouchVec velocity);
    // Fit: zoom in to tap_zoom centred on `point`; zoomed: back to fit.
    void double_tap(MapTouchVec point);
    // Glide, spring and zoom animation over `dt` seconds.
    void update(float dt);

    // Where the offset may rest at `scale`: the page covers the viewport along an axis where it is larger and is
    // centred where it is smaller.
    MapTouchVec min_offset(float scale) const;
    MapTouchVec max_offset(float scale) const;
    MapTouchVec clamp_offset(MapTouchVec offset, float scale) const;

private:
    enum class Gesture { None, Pan, Pinch };

    float band_scale(float raw) const;
    float unband_scale(float shown) const;
    MapTouchVec band_offset(MapTouchVec raw, float scale) const;
    MapTouchVec unband_offset(MapTouchVec shown, float scale) const;
    void animate_to(float scale, MapTouchVec offset);
    void step_glide(float dt);

    MapTouchTuning tuning_;
    MapTouchVec page_{};
    MapTouchVec viewport_{};
    float scale_ = 0.f;
    MapTouchVec offset_{};
    MapTouchVec velocity_{};

    Gesture gesture_ = Gesture::None;
    MapTouchVec raw_{};  // the drag's offset without the band
    float pinch_scale_ = 1.f;
    float pinch_distance_ = 1.f;
    MapTouchVec pinch_point_{};  // the page point under the midpoint at the start
    MapTouchVec anchor_{};       // the last midpoint: a zoom past its limits eases back about it

    // A zoom animation: the page point at the viewport's centre and the scale, eased from -> to.
    bool animating_ = false;
    float anim_time_ = 0.f;
    float anim_scale_[2] = {1.f, 1.f};
    MapTouchVec anim_centre_[2] = {};
    MapTouchVec anim_offset_{};
};

}  // namespace sh::screens
