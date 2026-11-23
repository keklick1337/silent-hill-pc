// SPDX-License-Identifier: GPL-3.0-or-later
// The map message box state machine. The order of host calls and state writes is the
// game's, which the replay harness compares frame by frame, so the branches below keep
// that order even where a flatter structure would read better.

#include "engine/text/message_box.h"

#include <algorithm>

namespace sh::text {
namespace {

// The voice-wait fail-open: a page whose voice never starts renders after this
// much virtual time instead of hanging the box.
constexpr std::uint32_t kVoiceFailOpenMs = 1000;

// The typewriter rolls out display_inc glyphs per this many frames; the
// accumulator makes the rate frame-rate independent (dt is Q12 seconds).
constexpr std::int32_t kRolloutFramesPerSecond = 30;

// The selection flash wraps every half second (Q12) and flips between two
// palette entries one `flash_step` apart.
constexpr std::int16_t kFlashHalfPeriodQ12 = 2048; // Q12(0.5)
constexpr std::int32_t kFlashStep          = 3;

// The sentinel the machine stores to mean "finish this frame" (0xFF).
constexpr std::int32_t kFinishSentinel = 0xFF;

// Markup result codes of the page draw (part of the game's markup contract).
constexpr std::int32_t kPageNone       = 0;
constexpr std::int32_t kPageSelect2    = 2;
constexpr std::int32_t kPageSelect3    = 3;
constexpr std::int32_t kPageSelect4    = 4;
constexpr std::int32_t kPageDisplayAll = 20;

std::int32_t q12(std::int32_t v) { return v * 4096; }

// Q12 multiply with the original's 32-bit wrap-and-shift semantics.
std::int32_t q12_mult(std::int32_t a, std::int32_t b) {
    return static_cast<std::int32_t>(static_cast<std::uint32_t>(a) * static_cast<std::uint32_t>(b)) >> 12;
}

bool message_has_end_code(const char* text) {
    for (const char* p = text; *p != '\0'; ++p) {
        if (p[0] == '~' && p[1] == 'E') {
            return true;
        }
    }
    return false;
}

bool first_code_is_j2(const char* text) {
    for (const char* p = text; *p != '\0'; ++p) {
        if (*p == ' ' || *p == '\t') {
            continue;
        }
        return p[0] == '~' && p[1] == 'J' && p[2] == '2';
    }
    return false;
}

}  // namespace

std::int32_t MessageBox::draw(std::int32_t request_idx, MessageBoxHost& host) {
    host.voice_touch();

    const bool has_input =
        host.input_enter_clicked() || host.input_cancel_clicked() || host.input_skip_held();

    host.reset_player_weapon();

    const bool re_setup_same = host.msg_index() == request_idx;

    // A cutscene re-display of a multi-page chain that already completed is a
    // full no-op that just advances the step (the PC's instant loads restart
    // what PSX's CD latency left in progress). Single-page re-displays keep the
    // lighter voice-only suppression path via re_display_setup below.
    if (re_setup_same && !host.string_set_active() && current_idx_ != host.msg_index() &&
        host.cutscene_active()) {
        return kMsgResultSelectEntry0;
    }

    if (host.msg_index() != request_idx) {
        host.set_string_set_active(false);
    }

    if (!host.string_set_active()) {
        // Fresh setup of a message.
        host.set_voice_dropped(0);
        host.set_map_msg_timer(spec_.no_value);
        sel_.max_idx = spec_.no_value;
        sel_.selected = 0;
        sel_.audio_load_block = 0;
        current_idx_  = request_idx;
        host.page_reset();
        host.voice_on_page(request_idx);
        state0_       = 0;
        state1_       = 0;
        host.set_msg_index(request_idx);
        display_length_ = 0;
        display_inc_    = 2; // advance 2 glyphs at a time
        host.default_string_info();
        (void)host.measure(current_idx_); // widths; the JP-only size variants are bridge-side
        voice_wait_latch_ = 1;
        host.set_string_set_active(true);
        host.set_re_display_setup(re_setup_same ? 1 : 0);
        return kMsgResultFinish;
    }

    // Voice wait: hold the page until the XA line is ready (or actually
    // streaming), with a wall-clock fail-open and an instant release when the
    // page's voice was dropped. The original left this block with `break`s that
    // skipped rendering for the frame; here that is finish_tail_ on an
    // unmodified state1_, which returns Idle.
    if (host.voice_dialog()) {
        const std::int32_t xa_state = host.audio_streaming_state();
        bool hold_frame = false;

        if (xa_state == spec_.streaming_ready_state) {
            voice_wait_latch_ = 0;
            voice_wait_ms_    = 0;
            hold_frame        = true;
        } else {
            if (voice_wait_latch_ != 0 && host.voice_dropped() != 0) {
                voice_wait_latch_ = 0;
                voice_wait_ms_    = 0;
                host.set_voice_dropped(0);
            }

            if (voice_wait_latch_ != 0) {
                if (xa_state == spec_.streaming_playing_state) {
                    // Release with the stream: the text appears in sync with the line.
                    voice_wait_latch_ = 0;
                    voice_wait_ms_    = 0;
                } else {
                    if (voice_wait_ms_ == 0) {
                        voice_wait_ms_ = host.virtual_ms();
                    }
                    if (host.virtual_ms() - voice_wait_ms_ < kVoiceFailOpenMs) {
                        hold_frame = true;
                    } else {
                        voice_wait_latch_ = 0;
                        voice_wait_ms_    = 0;
                    }
                }
            }
        }

        if (hold_frame) {
            return finish_tail_(host);
        }
    } else {
        voice_wait_latch_ = 0;
    }

    host.begin_page_render();

    // Typewriter rollout at the original rate: display_inc glyphs per 30fps
    // frame, fractional carry in Q12, clamped to the whole-page budget.
    {
        glyph_accum_    = glyph_accum_ + q12_mult(q12(display_inc_ * kRolloutFramesPerSecond),
                                                  host.delta_time_q12());
        const std::int32_t step_glyphs = glyph_accum_ >> 12;
        glyph_accum_  -= step_glyphs << 12;
        display_length_ += step_glyphs;
        display_length_ = std::clamp(display_length_, 0, spec_.display_all_length);
    }

    if (sel_.audio_load_block != 0 && host.map_msg_timer() > 0) {
        host.set_map_msg_timer(
            std::clamp(host.map_msg_timer() - host.delta_time_q12(), 0, spec_.timer_max));
    }

    const bool single_file = single_file_audio_(host);
    // Hold the page's auto-advance while its line is still audible; manual skip
    // (input) is NOT gated.
    const bool voice_hold = !single_file &&
                            ((host.voice_dialog() && host.voice_draining()) || host.voice_holding());

    if (state0_ == spec_.no_value) {
        if (sel_.audio_load_block == 0) {
            host.update_timer();
        }

        const std::int32_t temp = state1_;
        if (temp == state0_) {
            // Waiting for input on a fully drawn page.
            if (sel_.max_idx == temp) {
                if (!((sel_.audio_load_block & 1) || !has_input) ||
                    (sel_.audio_load_block != 0 && host.map_msg_timer() == 0 && !voice_hold))
                {
                    state1_ = kFinishSentinel;
                    if (host.voice_dialog()) {
                        host.stop_voice();
                    }
                    return finish_tail_(host);
                }
            } else if (host.input_cancel_clicked()) {
                sel_.max_idx = temp;
                sel_.selected = static_cast<std::uint8_t>(sel_.cancel_idx);
                host.play_sfx(MessageBoxHost::Sfx::Cancel);
                if (host.silent_yes_selection()) {
                    host.clear_silent_yes_selection();
                }
                state1_ = kFinishSentinel;
                return finish_tail_(host);
            } else if (host.input_enter_clicked()) {
                sel_.max_idx = temp;
                // The original compares the entry against the sign-extended cancel index.
                const std::int32_t entry = sel_.selected;
                const std::int32_t cancel = static_cast<std::int32_t>(sel_.cancel_idx);
                if (entry == cancel) {
                    host.play_sfx(MessageBoxHost::Sfx::Cancel);
                } else if (!host.silent_yes_selection()) {
                    host.play_sfx(MessageBoxHost::Sfx::Confirm);
                }
                if (host.silent_yes_selection()) {
                    host.clear_silent_yes_selection();
                }
                state1_ = kFinishSentinel;
                return finish_tail_(host);
            }
        } else if (((sel_.audio_load_block & 1) == 0 && has_input && sel_.max_idx != 0) ||
                   (sel_.audio_load_block != 0 && host.map_msg_timer() == 0 && !voice_hold))
        {
            if (sel_.max_idx != spec_.no_value) {
                sel_.max_idx = spec_.no_value;
                state1_ = kFinishSentinel;
                return finish_tail_(host);
            }

            // A page over, more follows: turn the page within the message, or
            // step to the next index.
            if (host.page_pending()) {
                host.page_advance();
            } else {
                current_idx_ += 1;
                host.voice_on_page(current_idx_);
            }
            host.set_map_msg_timer(sel_.max_idx); // still no_value here
            (void)host.measure(current_idx_);
            display_length_ = 0;
            state0_         = 0;

            if (sel_.audio_load_block == spec_.audio_block_j2) {
                // Single-file audio cutscene: pages advance on their own timers
                // and no voice is stopped or fired.
                voice_wait_latch_ = 0;
                return kMsgResultIdle;
            }

            if (host.voice_dialog()) {
                host.stop_voice();
            }
            voice_wait_latch_ = 1;
            // A genuine page turn is never a spurious re-display: its voice fires.
            host.set_re_display_setup(0);
            return kMsgResultFinish;
        }
    } else {
        if (has_input) {
            display_length_ = spec_.display_all_length;
        }
    }

    state0_ = 0;
    state1_ = selection_update(current_idx_, host, &display_length_);

    if (state1_ != 0 && state1_ < kPageSelect4) {
        state0_ = spec_.no_value;
    }

    return finish_tail_(host);
}

std::int32_t MessageBox::selection_update(std::int32_t msg_idx, MessageBoxHost& host,
                                          std::int32_t* display_length) {
    std::int32_t code = host.draw_page(msg_idx, *display_length);

    flash_timer_ = static_cast<std::int16_t>(flash_timer_ + host.delta_time_q12());
    if (flash_timer_ >= kFlashHalfPeriodQ12) {
        flash_timer_ = static_cast<std::int16_t>(flash_timer_ - kFlashHalfPeriodQ12);
    }

    switch (code) {
        case -1: // end of message (~E)
        case kPageNone:
            flash_timer_ = 0;
            break;

        case kPageSelect2:
        case kPageSelect3:
        case kPageSelect4: {
            sel_.max_idx = 1;
            sel_.cancel_idx = code == kPageSelect3 ? 2 : 1;

            std::int32_t option_count = code;
            const std::int32_t flash_color =
                host.flash_color_base() + (static_cast<std::int32_t>(flash_timer_ >> 10) * kFlashStep);
            const std::int32_t white = host.white_color_id();

            if (code == kPageSelect4) {
                // The shared first two messages ("Yes"/"No").
                for (std::int32_t i = 0; i < 2; ++i) {
                    host.draw_option_fixed(i, sel_.selected == i ? flash_color : white);
                }
                option_count = 2;
            } else {
                // The messages that follow this prompt's index.
                for (std::int32_t i = 0; i < option_count; ++i) {
                    host.draw_option_listed(msg_idx, i, sel_.selected == i ? flash_color : white);
                }
            }

            if (host.input_up_clicked() && sel_.selected != 0) {
                flash_timer_ = 0;
                sel_.selected = sel_.selected - 1;
                host.play_sfx(MessageBoxHost::Sfx::Move);
            }

            if (host.input_down_clicked() && sel_.selected != option_count - 1) {
                flash_timer_ = 0;
                sel_.selected = sel_.selected + 1;
                host.play_sfx(MessageBoxHost::Sfx::Move);
            }

            code = -1;
            break;
        }

        case kPageDisplayAll:
            *display_length = spec_.display_all_length;
            break;

        default:
            break; // alignment and other codes pass through untouched
    }

    return code;
}

bool MessageBox::single_file_audio_(const MessageBoxHost& host) const {
    bool single = sel_.audio_load_block == spec_.audio_block_j2;

    if (!single && sel_.audio_load_block != 0) {
        // A chaining page (no end code) whose NEXT page opens a single-file
        // clip is that clip's lead-in: exempt it too.
        if (!message_has_end_code(host.message_text(current_idx_)) &&
            first_code_is_j2(host.message_text(current_idx_ + 1)))
        {
            single = true;
        }
    }

    if (single && sel_.audio_load_block == spec_.audio_block_j2 && current_idx_ > 0) {
        // ...except a LONE ~J2 page that closes its message: holding it would
        // stall the scene, so only keep the exemption when the previous page
        // also rode the same clip.
        const bool has_end = message_has_end_code(host.message_text(current_idx_));
        const bool prev_is_j2 = first_code_is_j2(host.message_text(current_idx_ - 1));
        if (has_end && !prev_is_j2) {
            single = false;
        }
    }

    return single;
}

std::int32_t MessageBox::finish_tail_(MessageBoxHost& host) {
    if (state1_ != kFinishSentinel) {
        return kMsgResultIdle;
    }

    host.voice_on_end();
    host.set_string_set_active(false);
    host.set_high_res_glyphs(false);
    display_length_ = 0;

    if (host.voice_dialog()) {
        voice_wait_latch_ = 1;
    }

    return sel_.selected + 1;
}

MessageBox& message_box() {
    static MessageBox box;
    return box;
}

}  // namespace sh::text
