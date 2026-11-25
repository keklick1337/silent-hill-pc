// SPDX-License-Identifier: GPL-3.0-or-later
#include "engine/screens/map_touch_view.h"

#include <algorithm>
#include <cmath>

namespace sh::screens {

namespace {

constexpr float kAtFitSlack = 1.001f;
// A double tap counts the page as zoomed in from this much above fit (a pinch that nearly closed).
constexpr float kZoomedSlack = 1.01f;
constexpr float kStepSeconds = 1.0f / 240.0f;
constexpr float kLongestUpdate = 0.25f;
constexpr float kSnapDistance = 0.01f;
constexpr float kSnapSpeed = 1.0f;
// The page never stretches below half the fit scale or above twice the largest one.
constexpr float kScaleFloor = 0.5f;
constexpr float kScaleCeiling = 2.0f;

// The rubber band past an edge: slope `c` at the edge, approaching `reach` far out (as iOS scroll views do).
float band(float d, float c, float reach) {
    if (reach <= 0.f || c <= 0.f) return 0.f;
    return reach * c * d / (c * d + reach);
}
float unband(float y, float c, float reach) {
    if (reach <= 0.f || c <= 0.f) return 0.f;
    y = std::min(y, reach * 0.999f);
    return reach * y / (c * (reach - y));
}

MapTouchVec operator+(MapTouchVec a, MapTouchVec b) { return {a.x + b.x, a.y + b.y}; }
MapTouchVec operator-(MapTouchVec a, MapTouchVec b) { return {a.x - b.x, a.y - b.y}; }
MapTouchVec operator*(MapTouchVec a, float s) { return {a.x * s, a.y * s}; }

}  // namespace

float MapTouchView::fit_scale() const {
    if (page_.x <= 0.f || page_.y <= 0.f || viewport_.x <= 0.f || viewport_.y <= 0.f) return 1.f;
    return std::min(viewport_.x / page_.x, viewport_.y / page_.y);
}

float MapTouchView::max_scale() const { return fit_scale() * std::max(1.f, tuning_.max_zoom); }

void MapTouchView::set_frame(MapTouchVec page, MapTouchVec viewport) {
    if (page.x == page_.x && page.y == page_.y && viewport.x == viewport_.x && viewport.y == viewport_.y) return;
    if (scale_ <= 0.f) {
        page_ = page;
        viewport_ = viewport;
        reset();
        return;
    }
    const MapTouchVec centre = to_page(viewport_ * 0.5f);
    const float zoom = scale_ / fit_scale();
    page_ = page;
    viewport_ = viewport;
    gesture_ = Gesture::None;
    animating_ = false;
    velocity_ = {};
    scale_ = std::clamp(zoom * fit_scale(), fit_scale(), max_scale());
    offset_ = clamp_offset(viewport_ * 0.5f - centre * scale_, scale_);
}

void MapTouchView::reset() {
    gesture_ = Gesture::None;
    animating_ = false;
    velocity_ = {};
    scale_ = fit_scale();
    offset_ = clamp_offset({}, scale_);
    anchor_ = viewport_ * 0.5f;
}

void MapTouchView::set_view(float scale, MapTouchVec offset) {
    gesture_ = Gesture::None;
    animating_ = false;
    velocity_ = {};
    scale_ = std::clamp(scale, fit_scale(), max_scale());
    offset_ = clamp_offset(offset, scale_);
}

MapTouchVec MapTouchView::to_screen(MapTouchVec page_point) const { return offset_ + page_point * scale_; }

MapTouchVec MapTouchView::to_page(MapTouchVec screen_point) const {
    const float s = scale_ > 0.f ? scale_ : 1.f;
    return (screen_point - offset_) * (1.f / s);
}

bool MapTouchView::at_fit() const {
    return gesture_ != Gesture::Pinch && !animating_ && scale_ <= fit_scale() * kAtFitSlack;
}

bool MapTouchView::settled() const {
    if (gesture_ != Gesture::None || animating_ || velocity_.x != 0.f || velocity_.y != 0.f) return false;
    const MapTouchVec rest = clamp_offset(offset_, scale_);
    return rest.x == offset_.x && rest.y == offset_.y && scale_ >= fit_scale() && scale_ <= max_scale();
}

MapTouchVec MapTouchView::min_offset(float scale) const {
    MapTouchVec out;
    const float w = page_.x * scale, h = page_.y * scale;
    out.x = w >= viewport_.x ? viewport_.x - w : (viewport_.x - w) * 0.5f;
    out.y = h >= viewport_.y ? viewport_.y - h : (viewport_.y - h) * 0.5f;
    return out;
}

MapTouchVec MapTouchView::max_offset(float scale) const {
    MapTouchVec out;
    const float w = page_.x * scale, h = page_.y * scale;
    out.x = w >= viewport_.x ? 0.f : (viewport_.x - w) * 0.5f;
    out.y = h >= viewport_.y ? 0.f : (viewport_.y - h) * 0.5f;
    return out;
}

MapTouchVec MapTouchView::clamp_offset(MapTouchVec offset, float scale) const {
    const MapTouchVec lo = min_offset(scale), hi = max_offset(scale);
    return {std::clamp(offset.x, lo.x, hi.x), std::clamp(offset.y, lo.y, hi.y)};
}

float MapTouchView::band_scale(float raw) const {
    const float lo = fit_scale(), hi = max_scale(), k = tuning_.scale_band;
    float shown = raw;
    if (raw > hi) shown = hi * std::pow(raw / hi, k);
    if (raw < lo) shown = lo * std::pow(std::max(raw, 1e-6f) / lo, k);
    return std::clamp(shown, lo * kScaleFloor, hi * kScaleCeiling);
}

float MapTouchView::unband_scale(float shown) const {
    const float lo = fit_scale(), hi = max_scale(), k = tuning_.scale_band;
    if (k <= 0.f) return std::clamp(shown, lo, hi);
    if (shown > hi) return hi * std::pow(shown / hi, 1.f / k);
    if (shown < lo) return lo * std::pow(shown / lo, 1.f / k);
    return shown;
}

MapTouchVec MapTouchView::band_offset(MapTouchVec raw, float scale) const {
    const MapTouchVec lo = min_offset(scale), hi = max_offset(scale);
    const float c = tuning_.band_slope;
    auto axis = [&](float v, float a, float b, float reach) {
        if (v > b) return b + band(v - b, c, reach);
        if (v < a) return a - band(a - v, c, reach);
        return v;
    };
    return {axis(raw.x, lo.x, hi.x, viewport_.x * tuning_.band_reach),
            axis(raw.y, lo.y, hi.y, viewport_.y * tuning_.band_reach)};
}

MapTouchVec MapTouchView::unband_offset(MapTouchVec shown, float scale) const {
    const MapTouchVec lo = min_offset(scale), hi = max_offset(scale);
    const float c = tuning_.band_slope;
    auto axis = [&](float v, float a, float b, float reach) {
        if (v > b) return b + unband(v - b, c, reach);
        if (v < a) return a - unband(a - v, c, reach);
        return v;
    };
    return {axis(shown.x, lo.x, hi.x, viewport_.x * tuning_.band_reach),
            axis(shown.y, lo.y, hi.y, viewport_.y * tuning_.band_reach)};
}

void MapTouchView::zoom_about(MapTouchVec point, float factor) {
    gesture_ = Gesture::None;
    animating_ = false;
    velocity_ = {};
    const MapTouchVec p = to_page(point);
    scale_ = std::clamp(scale_ * factor, fit_scale(), max_scale());
    offset_ = clamp_offset(point - p * scale_, scale_);
}

void MapTouchView::begin_pan() {
    animating_ = false;
    velocity_ = {};
    raw_ = unband_offset(offset_, scale_);
    gesture_ = Gesture::Pan;
}

void MapTouchView::pan(MapTouchVec delta) {
    if (gesture_ != Gesture::Pan) begin_pan();
    raw_ = raw_ + delta;
    offset_ = band_offset(raw_, scale_);
}

void MapTouchView::begin_pinch(MapTouchVec mid, float distance) {
    animating_ = false;
    velocity_ = {};
    pinch_scale_ = unband_scale(scale_);
    pinch_distance_ = std::max(distance, 1e-3f);
    // From the offset without its band, so the first pinch frame shows the page exactly where it was.
    pinch_point_ = (mid - unband_offset(offset_, scale_)) * (1.f / scale_);
    anchor_ = mid;
    gesture_ = Gesture::Pinch;
}

void MapTouchView::pinch(MapTouchVec mid, float distance) {
    if (gesture_ != Gesture::Pinch) {
        begin_pinch(mid, distance);
        return;
    }
    scale_ = band_scale(pinch_scale_ * std::max(distance, 1e-3f) / pinch_distance_);
    offset_ = band_offset(mid - pinch_point_ * scale_, scale_);
    anchor_ = mid;
}

void MapTouchView::release(MapTouchVec velocity) {
    const Gesture was = gesture_;
    gesture_ = Gesture::None;
    velocity_ = {};
    const float s = std::clamp(scale_, fit_scale(), max_scale());
    if (s != scale_) {
        const MapTouchVec p = to_page(anchor_);
        animate_to(s, clamp_offset(anchor_ - p * s, s));
        return;
    }
    if (was == Gesture::Pan) {
        const float limit = tuning_.max_speed * viewport_.y;
        velocity_ = {std::clamp(velocity.x, -limit, limit), std::clamp(velocity.y, -limit, limit)};
    }
}

void MapTouchView::double_tap(MapTouchVec point) {
    gesture_ = Gesture::None;
    velocity_ = {};
    const float fit = fit_scale();
    if (scale_ > fit * kZoomedSlack) {
        animate_to(fit, clamp_offset({}, fit));
        return;
    }
    const float s = std::clamp(fit * tuning_.tap_zoom, fit, max_scale());
    const MapTouchVec p = to_page(point);
    animate_to(s, clamp_offset(viewport_ * 0.5f - p * s, s));
}

void MapTouchView::animate_to(float scale, MapTouchVec offset) {
    animating_ = true;
    anim_time_ = 0.f;
    anim_scale_[0] = scale_;
    anim_scale_[1] = scale;
    anim_centre_[0] = to_page(viewport_ * 0.5f);
    anim_centre_[1] = (viewport_ * 0.5f - offset) * (1.f / scale);
    anim_offset_ = offset;
}

void MapTouchView::update(float dt) {
    if (!(dt > 0.f) || gesture_ != Gesture::None) return;
    dt = std::min(dt, kLongestUpdate);
    if (animating_) {
        anim_time_ += tuning_.ease_time > 0.f ? dt / tuning_.ease_time : 1.f;
        if (anim_time_ >= 1.f) {
            animating_ = false;
            scale_ = anim_scale_[1];
            offset_ = anim_offset_;
            return;
        }
        // Ease out; the scale moves evenly in log space so the zoom looks steady, and the centre travels with it.
        const float r = 1.f - anim_time_;
        const float e = 1.f - r * r * r;
        scale_ = std::exp(std::log(anim_scale_[0]) + (std::log(anim_scale_[1]) - std::log(anim_scale_[0])) * e);
        const MapTouchVec c = anim_centre_[0] + (anim_centre_[1] - anim_centre_[0]) * e;
        offset_ = viewport_ * 0.5f - c * scale_;
        return;
    }
    const float s = std::clamp(scale_, fit_scale(), max_scale());
    if (s != scale_) {
        const MapTouchVec p = to_page(anchor_);
        animate_to(s, clamp_offset(anchor_ - p * s, s));
        return;
    }
    while (dt > 0.f) {
        const float h = std::min(dt, kStepSeconds);
        step_glide(h);
        dt -= h;
    }
}

void MapTouchView::step_glide(float h) {
    const MapTouchVec lo = min_offset(scale_), hi = max_offset(scale_);
    const float w = tuning_.spring;
    auto axis = [&](float& x, float& v, float a, float b, float reach) {
        const float d = x > b ? x - b : (x < a ? x - a : 0.f);
        if (d == 0.f) {
            v *= std::exp(-tuning_.friction * h);
            if (std::fabs(v) < tuning_.stop_speed) v = 0.f;
            x += v * h;
            return;
        }
        const float bound = d > 0.f ? b : a;
        v += (-w * w * d - 2.f * w * v) * h;
        x += v * h;
        x = std::clamp(x, a - reach, b + reach);
        // Back inside, or close enough: rest on the edge (a glide must not start again from the spring's speed).
        const float nd = x - bound;
        if ((nd > 0.f) != (d > 0.f) || nd == 0.f || (std::fabs(nd) < kSnapDistance && std::fabs(v) < kSnapSpeed)) {
            x = bound;
            v = 0.f;
        }
    };
    axis(offset_.x, velocity_.x, lo.x, hi.x, viewport_.x * tuning_.band_reach);
    axis(offset_.y, velocity_.y, lo.y, hi.y, viewport_.y * tuning_.band_reach);
}

}  // namespace sh::screens
