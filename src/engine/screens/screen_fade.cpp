// SPDX-License-Identifier: GPL-3.0-or-later
#include "engine/screens/screen_fade.h"

#include "engine/screens/screens_view.h"

namespace sh::screens {
namespace {

constexpr std::int32_t kOne = 4096;
constexpr std::int32_t kAlmostOne = kOne - 1;  // a level of 1.0 clamped into the 12-bit range

std::int32_t mul_q12(std::int32_t a, std::int32_t b) {
    return static_cast<std::int32_t>((static_cast<std::int64_t>(a) * b) >> 12);
}

}  // namespace

std::int32_t fade_frame_time(std::int32_t raw) {
    if (raw <= 0) raw = kOne / 60;
    if (raw > kOne / 30) raw = kOne / 30;
    return raw;
}

void ScreenFade::set_level(int buf, std::int32_t progress) {
    const auto level = static_cast<std::uint8_t>(progress >> 4);
    color_[buf] = {level, level, level};
}

void ScreenFade::set_page(int buf, std::int32_t status) {
    // White fades add, black fades subtract.
    page_[buf] = (status & kFadeWhiteBit) ? 32 : 64;
}

void ScreenFade::update(ScreenFadeHost& host) {
    const int buf = host.buffer();
    prev_progress_ = progress_;

    const std::int32_t status = host.status();
    const std::int32_t white = kFadeWhiteBit;
    // The white variants of Reset and None are not idle: they fall to the draw with the levels already set.
    switch (status) {
        case static_cast<std::int32_t>(ScreenFadeState::FadeOutStart):
        case static_cast<std::int32_t>(ScreenFadeState::FadeOutStart) | white:
            progress_ = 0;
            host.set_status(host.status() + 1);
            [[fallthrough]];
        case static_cast<std::int32_t>(ScreenFadeState::FadeOutSteps):
        case static_cast<std::int32_t>(ScreenFadeState::FadeOutSteps) | white: {
            set_page(buf, host.status());
            const int queued = host.file_queue_length();
            const std::int32_t custom = host.timestep();
            const std::int32_t step = custom > 0 ? custom : (3 * kOne) / (queued + 1);
            progress_ += mul_q12(step, fade_frame_time(host.frame_time()));
            if (progress_ >= kAlmostOne) {
                progress_ = kAlmostOne;
                host.set_status(host.status() + 1);
            }
            set_level(buf, progress_);
            break;
        }
        case static_cast<std::int32_t>(ScreenFadeState::ResetTimestep):
        case static_cast<std::int32_t>(ScreenFadeState::ResetTimestep) | white:
            host.set_timestep(0);
            [[fallthrough]];
        case static_cast<std::int32_t>(ScreenFadeState::FadeInStart):
        case static_cast<std::int32_t>(ScreenFadeState::FadeInStart) | white:
            progress_ = kAlmostOne;
            host.set_status(host.status() + 1);
            [[fallthrough]];
        case static_cast<std::int32_t>(ScreenFadeState::FadeOutComplete):
        case static_cast<std::int32_t>(ScreenFadeState::FadeOutComplete) | white:
            set_page(buf, host.status());
            set_level(buf, progress_);
            break;
        case static_cast<std::int32_t>(ScreenFadeState::FadeInSteps):
        case static_cast<std::int32_t>(ScreenFadeState::FadeInSteps) | white: {
            set_page(buf, host.status());
            const std::int32_t custom = host.timestep();
            const std::int32_t step = custom > 0 ? custom : 3 * kOne;
            progress_ -= mul_q12(step, fade_frame_time(host.frame_time()));
            if (progress_ <= 0) {
                progress_ = 0;
                host.set_status(static_cast<std::int32_t>(ScreenFadeState::Reset));
                return;
            }
            set_level(buf, progress_);
            break;
        }
        case static_cast<std::int32_t>(ScreenFadeState::Reset):
            host.set_timestep(0);
            progress_ = 0;
            host.set_status(static_cast<std::int32_t>(ScreenFadeState::None));
            return;
        case static_cast<std::int32_t>(ScreenFadeState::None):
            return;
        default:
            break;
    }

    // The tile reaches well past the 4:3 edges so a widened frame is covered too.
    draw2d::Fill tile;
    tile.at = {-320, -240};
    tile.w = 320 * 4;
    tile.h = 240 * 2;
    tile.color = color_[buf];
    tile.translucent = true;
    host.submit(tile);

    draw2d::DrawMode mode;
    mode.page = draw2d::TexturePage::from_word(page_[buf]);
    mode.dither = true;
    host.submit(mode);
}

ScreenFade& screen_fade() {
    static ScreenFade fade;
    return fade;
}

CutsceneBorders& cutscene_borders() {
    static CutsceneBorders borders;
    return borders;
}

// --- Cutscene borders ---

void CutsceneBorders::set_shade(int buf, std::int32_t shade) {
    const auto outer = static_cast<std::uint8_t>(shade >> 4);
    auto inner = static_cast<std::uint8_t>(shade >> 5);
    if (shade == kAlmostOne) inner = 255;
    for (int i = 0; i < 2; ++i) {
        bar_shade_[buf + i * 2][0] = outer;
        bar_shade_[buf + i * 2][1] = inner;
    }
}

void CutsceneBorders::update(CutsceneBordersHost& host) {
    const int buf = host.buffer();
    // Read every call so a window resize or a console tweak shows at once.
    const auto half_w = static_cast<std::int16_t>(host.half_width());
    const auto outer = static_cast<std::int16_t>(host.bar_outer());
    const auto inner = static_cast<std::int16_t>(host.bar_inner());

    host.set_event_camera_rate(shade_);
    if (host.music_paused()) return;

    switch (host.state()) {
        case static_cast<std::int32_t>(BorderState::FadeInStart):
            host.set_state(host.state() + 1);
            [[fallthrough]];
        case static_cast<std::int32_t>(BorderState::FadingIn):
            shade_ += host.frame_time();
            if (shade_ >= kAlmostOne) {
                shade_ = kAlmostOne;
                host.set_state(host.state() + 1);
            }
            set_shade(buf, shade_);
            break;
        case static_cast<std::int32_t>(BorderState::ForceShow):
        case static_cast<std::int32_t>(BorderState::FadeOutStart):
            shade_ = kAlmostOne;
            host.set_state(host.state() + 1);
            [[fallthrough]];
        case static_cast<std::int32_t>(BorderState::Shown):
            set_shade(buf, shade_);
            break;
        case static_cast<std::int32_t>(BorderState::FadingOut):
            shade_ -= host.frame_time();
            if (shade_ <= 0) {
                shade_ = 0;
                host.set_state(static_cast<std::int32_t>(BorderState::Reset));
                return;
            }
            set_shade(buf, shade_);
            break;
        case static_cast<std::int32_t>(BorderState::Reset):
            shade_ = 0;
            host.set_state(static_cast<std::int32_t>(BorderState::None));
            host.clear_cutscene_active();
            return;
        case static_cast<std::int32_t>(BorderState::None):
            return;
        default:
            break;
    }

    for (int q = buf; q < 4; q += 2) {
        // Subtractive bars: the outer edge shade on the first row of corners, the inner edge on the second.
        const std::int16_t sign = q < 2 ? -1 : 1;
        const std::int16_t y_outer = static_cast<std::int16_t>(sign * outer);
        const std::int16_t y_inner = static_cast<std::int16_t>(sign * inner);
        draw2d::Polygon bar;
        bar.count = 4;
        bar.gouraud = true;
        bar.shade.translucent = true;
        bar.xy[0] = {static_cast<std::int16_t>(-half_w), y_outer};
        bar.xy[1] = {half_w, y_outer};
        bar.xy[2] = {static_cast<std::int16_t>(-half_w), y_inner};
        bar.xy[3] = {half_w, y_inner};
        for (int k = 0; k < 4; ++k) {
            const std::uint8_t s = bar_shade_[q][k / 2];
            bar.color[k] = {s, s, s};
        }
        host.submit(bar);
    }
    draw2d::DrawMode mode;
    mode.page = draw2d::TexturePage::from_word(64);
    mode.dither = true;
    host.submit(mode);

    if (!host.cutscene_active()) {
        const int h = host.screen_height();
        // 377 is the projection distance of the letterboxed view.
        host.set_projection(h + (((377 - h) * shade_) >> 12));
    }
}

}  // namespace sh::screens
