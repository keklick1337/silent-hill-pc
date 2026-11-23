// SPDX-License-Identifier: GPL-3.0-or-later
// The staff roll's timing, layout and screen steps.

#include "engine/screens/staff_roll.h"

#include "engine/screens/roll_math.h"

namespace sh::screens {

namespace {

using namespace roll_math;

constexpr std::int32_t kUnity = 4096;

// Flat roll geometry: the roll display is 448 lines tall and scrolls at two pixels per
// clock tick; rows enter at the bottom (240 below the centre) and leave 264 above it.
constexpr std::int32_t kFlatEnterOffset = 240;
constexpr std::int32_t kFlatSpan = 504;          // rows visible between enter and leave
constexpr std::int32_t kFlatTail = 480;          // the logo starts a screen below the last row
constexpr std::int32_t kFlatLogoStop = -24;      // the logo stops here, centred
constexpr std::int32_t kFlatShadowOffset = -1;   // shadow passes at -1 and +1
constexpr std::int32_t kFlatOutroTicks = 180;
constexpr std::int32_t kEnterWaitTicks = 60;
constexpr std::int32_t kLeaveSettleTicks = 10;
// The roll display's two top lines are covered every frame.
constexpr std::int32_t kBarX = -256;
constexpr std::int32_t kBarY = -224;
constexpr std::int32_t kBarW = 512;
constexpr std::int32_t kBarH = 2;

// Perspective roll: one pixel per clock tick; rows enter 120 below the plane origin and
// leave 288 above it; the logo stops at -48.
constexpr std::int32_t kPerspEnterOffset = 120;
constexpr std::int32_t kPerspSpan = 408;
constexpr std::int32_t kPerspLengthTrim = 264;
constexpr std::int32_t kPerspTail = 240;
constexpr std::int32_t kPerspLogoStop = -48;
constexpr std::int32_t kPerspSkipLead = 168;

constexpr int kMainStep = 0;
constexpr int kSubStep = 1;
constexpr int kSubStep2 = 2;

bool is(const Fade& f, FadeState state, bool white) { return f.state == state && f.white == white; }

}  // namespace

void StaffRoll::set_flat_blend(StaffRollHost& host, bool semi_transparent, std::uint32_t mode) {
    flat_.set_semi_transparent(semi_transparent);
    if (mode < 4) {
        host.set_flat_blend_mode(mode);
    }
}

void StaffRoll::start_flat(std::int32_t index, StaffRollHost& host) {
    ending_ = index;
    host.preload_music(index);
    const std::int32_t rows = host.row_count();
    const std::int32_t distance = sub(mul(host.ending(index).length, 2), kFlatSpan);
    row_step_ = distance / rows;
    music_step_ = 1;
    roll_end_ = add(mul(row_step_, rows), kFlatTail);
    inv_row_step_ = 0x10000 / row_step_;
}

bool StaffRoll::music_started(StaffRollHost& host) {
    switch (music_step_) {
        case 1:
            host.play_music(ending_);
            music_step_++;
            return false;
        case 2:
            if (host.music_state() == 1) {
                host.set_clock(host.ending(ending_).start);
                return true;
            }
            return false;
        default:
            return false;
    }
}

bool StaffRoll::finish(StaffRollHost& host) {
    const Fade f = host.fade();
    switch (f.state) {
        case FadeState::ResetTimestep:
        case FadeState::OutComplete:
            if (host.file_queue_length() != 0) {
                return false;
            }
            host.enter_roll_state();
            return true;
        case FadeState::OutStart:
        case FadeState::OutSteps:
            return false;
        default:
            host.set_fade({FadeState::OutStart, true});
            host.set_fade_step(kUnity);
            return false;
    }
}

void StaffRoll::update(StaffRollHost& host, RollSink& sink, RollSink& bar_sink) {
    host.set_clock(add(host.clock(), host.vblanks()));
    const std::int32_t main_step = host.step(kMainStep);
    bool done = false;
    switch (main_step) {
        case 0:
            done = enter(host);
            break;
        case 1:
            done = roll(host, sink, bar_sink);
            break;
        default:
            done = leave(host);
            break;
    }
    if (done) {
        host.reset_step_timer();
        host.set_step(kSubStep, 0);
        host.set_step(kSubStep2, 0);
        host.set_step(kMainStep, main_step + 1);
    }
}

bool StaffRoll::enter(StaffRollHost& host) {
    const std::int32_t sub_step = host.step(kSubStep);
    switch (sub_step) {
        case 0: {
            const Fade f = host.fade();
            if (is(f, FadeState::OutComplete, true)) {
                host.set_background(0xFFFFFF);
            } else if (is(f, FadeState::OutComplete, false)) {
                host.set_background(0);
            }
            host.stop_bgm();
            host.stop_sfx();
            host.set_step(kSubStep, sub_step + 1);
            break;
        }
        case 1: {
            host.save_screen();
            const Fade f = host.fade();
            if (is(f, FadeState::OutComplete, true)) {
                host.clear_for_roll(true);
            } else if (is(f, FadeState::OutComplete, false)) {
                host.clear_for_roll(false);
            }
            host.set_step(kSubStep, sub_step + 1);
            break;
        }
        case 2:
            host.set_roll_display(true);
            host.set_frame_interval(1);
            wait_ = kEnterWaitTicks;
            host.set_step(kSubStep, sub_step + 1);
            break;
        case 3:
            wait_--;
            if (wait_ <= 0 && host.music_state() < 2) {
                host.set_background(0);
                return true;
            }
            break;
        default:
            break;
    }
    return false;
}

bool StaffRoll::roll(StaffRollHost& host, RollSink& sink, RollSink& bar_sink) {
    if (host.skip_pressed()) {
        host.set_clock(add(host.ending(ending_).length, roll_end_ / 2));
        host.stop_voice();
    }

    RollPrim bar;
    bar.kind = RollPrim::Kind::Bar;
    bar.x[0] = kBarX;
    bar.y[0] = kBarY;
    bar.x[1] = kBarW;
    bar.y[1] = kBarH;
    bar_sink.prim(bar);

    const bool logo_done = draw_flat(host, sink);

    const std::int32_t sub_step = host.step(kSubStep);
    switch (sub_step) {
        case 0: {
            const Fade f = host.fade();
            if (is(f, FadeState::OutComplete, true)) {
                host.set_fade({FadeState::InStart, true});
            } else if (is(f, FadeState::OutComplete, false)) {
                host.set_fade({FadeState::InStart, false});
            }
            host.set_fade_step(kUnity);
            host.set_step(kSubStep, sub_step + 1);
            outro_ = kFlatOutroTicks;
            break;
        }
        case 1:
            if (!logo_done) {
                break;
            }
            outro_--;
            if (host.music_state() == 0) {
                host.set_step(kSubStep, sub_step + 1);
            }
            break;
        case 2:
            outro_--;
            if (outro_ <= 0) {
                // The original stores the step number (2) as the fade status: a black
                // fade-out from the start.
                host.set_fade({FadeState::OutStart, false});
                host.set_step(kSubStep, 3);
            }
            break;
        case 3:
            if (is(host.fade(), FadeState::OutComplete, false)) {
                host.set_step(kSubStep, 4);
                return true;
            }
            break;
        default:
            break;
    }
    return false;
}

bool StaffRoll::leave(StaffRollHost& host) {
    if (!host.came_from_gameplay()) {
        return false;
    }
    const std::int32_t sub_step = host.step(kSubStep);
    if (sub_step == 0) {
        host.set_roll_display(false);
        host.set_fade({FadeState::Reset, false});
        host.set_step(kSubStep, sub_step + 1);
    } else if (sub_step != kLeaveSettleTicks) {
        host.set_step(kSubStep, sub_step + 1);
    } else {
        host.restore_screen();
        host.return_to_previous_state();
    }
    return false;
}

bool StaffRoll::draw_flat(StaffRollHost& host, RollSink& sink) {
    const std::int32_t t = mul(host.clock(), 2);
    std::int32_t first = mul(sub(t, kFlatSpan), inv_row_step_) >> 16;
    if (first < 0) {
        first = 0;
    }
    std::int32_t last = mul(t, inv_row_step_) >> 16;
    const std::int32_t rows = host.row_count();
    if (last >= rows) {
        last = rows - 1;
    }
    const std::int32_t first_y = add(sub(mul(first, row_step_), t), kFlatEnterOffset);
    const std::int32_t count = last - first + 1;

    std::int32_t logo_y = add(sub(roll_end_, t), kFlatEnterOffset);
    const bool logo_visible = logo_y < kFlatEnterOffset;
    bool logo_stopped = false;
    if (logo_visible) {
        logo_stopped = logo_y < kFlatLogoStop + 1;
        if (logo_stopped) {
            logo_y = kFlatLogoStop;
        }
    }

    set_flat_blend(host, true, style_.shadow_blend);
    flat_.set_color(style_.shadow_rgb);

    std::int32_t offset = kFlatShadowOffset;
    for (int pass = 2; pass >= 0; pass--, offset += 2) {
        if (pass == 0) {
            offset = 0;
            set_flat_blend(host, false, 0);
            flat_.set_color(style_.text_rgb);
        }
        std::int32_t y = add(first_y, offset);
        for (std::int32_t i = 0; i < count; i++, y = add(y, row_step_)) {
            flat_.set_position(0, y);
            flat_.draw(host.row(first + i), sink);
        }
        if (logo_visible) {
            flat_.set_position(0, add(logo_y, offset));
            flat_.draw(host.logo(), sink);
        }
    }
    return logo_stopped;
}

void StaffRoll::start_perspective(std::int32_t index, StaffRollHost& host) {
    perspective_.set_plane(style_.plane);
    host.plane_changed();

    const EndingTiming timing = host.ending(index);
    // The length goes through a 16-bit field.
    const std::int32_t length = static_cast<std::int16_t>(static_cast<std::uint16_t>(timing.length));
    ending_ = index;
    const std::int32_t rows = host.row_count();
    host.set_clock(timing.start);
    row_step_ = sub(length, kPerspLengthTrim) / rows;
    roll_end_ = add(mul(row_step_, rows), kPerspTail);
    inv_row_step_ = 0x10000 / row_step_;
}

void StaffRoll::set_logo_plane(StaffRollHost& host, std::int32_t z_q12, std::int32_t row_y, std::int32_t row_z) {
    RollPlane plane = style_.plane;
    plane.origin_z = z_q12;
    plane.row_y = row_y;
    plane.row_z = row_z;
    perspective_.set_plane(plane);
    host.plane_changed();
}

bool StaffRoll::update_perspective(StaffRollHost& host, RollSink& sink) {
    const std::int32_t v = host.clock();
    std::int32_t first = mul(sub(v, kPerspSpan), inv_row_step_) >> 16;
    if (first < 0) {
        first = 0;
    }
    std::int32_t last = mul(v, inv_row_step_) >> 16;
    const std::int32_t rows = host.row_count();
    if (last >= rows) {
        last = rows - 1;
    }

    std::int32_t logo_y = add(sub(roll_end_, v), kPerspEnterOffset);
    const std::int32_t logo_anchor = logo_y;
    std::int32_t anim = 0;
    bool logo_visible = logo_y < kPerspEnterOffset;
    const std::int32_t first_y = add(sub(mul(first, row_step_), v), kPerspEnterOffset);
    const std::int32_t count = last - first + 1;

    bool animate = false;
    if (logo_visible) {
        animate = logo_y < kPerspLogoStop + 1;
        if (animate) {
            logo_y = kPerspLogoStop;
            anim = sub(kPerspLogoStop, logo_anchor);
        }
    }

    flat_.set_color(style_.perspective_flat_rgb);
    set_flat_blend(host, false, 0);

    std::int32_t y = first_y;
    for (std::int32_t i = 0; i < count; i++, y = add(y, row_step_)) {
        perspective_.set_position(0, y);
        perspective_.draw(host.row(first + i), sink);
    }

    bool finished = false;
    const LogoAnimation& a = style_.logo;
    if (animate) {
        const std::int32_t approach_end = a.spin_ticks + a.approach_ticks;
        if (anim < a.spin_ticks) {
            std::int32_t angle = mul(anim, kUnity) / a.spin_ticks;
            angle = q12_mul(sub(kUnity, angle), a.tilt_angle);
            const std::int32_t s = -host.sin(angle);
            const std::int32_t c = host.cos(angle);
            set_logo_plane(host, q12_from_float(a.approach_from), c, s);
        } else if (anim < approach_end) {
            const std::int32_t k = mul(anim - a.spin_ticks, kUnity) / a.approach_ticks;
            const std::int32_t z = add(q12_mul(sub(kUnity, k), q12_from_float(a.approach_from - a.approach_to)),
                                       q12_from_float(a.approach_to));
            set_logo_plane(host, z, kUnity, 0);
        } else if (anim >= a.hold_until) {
            if (anim < a.hold_until + a.retreat_ticks) {
                const std::int32_t k = mul(anim - a.hold_until, kUnity) / a.retreat_ticks;
                const std::int32_t z = add(q12_mul(sub(kUnity, k), q12_from_float(a.approach_to - a.retreat_to)),
                                           q12_from_float(a.retreat_to));
                set_logo_plane(host, z, kUnity, 0);
            } else {
                finished = true;
                logo_visible = false;
            }
        }
    } else if (host.skip_pressed()) {
        const std::int32_t to = shl(add(roll_end_, kPerspSkipLead), 12) / kUnity;
        host.set_clock(to);
        host.stop_voice();
    }

    if (logo_visible) {
        perspective_.set_position(0, logo_y);
        perspective_.draw(host.logo(), sink);
    }
    return finished;
}

}  // namespace sh::screens
