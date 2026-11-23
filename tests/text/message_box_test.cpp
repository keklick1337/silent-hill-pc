// SPDX-License-Identifier: GPL-3.0-or-later
// Unit tests for the map-message box state machine (sh::text::MessageBox). The fake host records
// every call; all expected budgets, timers and accumulators are hand-computed.

#include "engine/text/message_box.h"

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "framework/sh_test.h"

namespace {

using sh::text::MessageBox;
using sh::text::MessageBoxHost;

class FakeHost final : public MessageBoxHost {
public:
    // Input knobs (per frame).
    bool enter = false;
    bool cancel = false;
    bool skip = false;
    bool up = false;
    bool down = false;
    std::int32_t dt = 136; // ~Q12(1/30 s)

    // World knobs.
    std::uint32_t ms = 0;
    std::int32_t streaming = 4;
    bool dialog_on = false;
    bool draining_on = false;
    bool holding_on = false;
    bool cutscene = false;
    bool silent_yes = false;
    bool page_pending_flag = false;

    // State the box owns logically but lives game-side.
    bool string_set = false;
    std::int32_t timer = 0;
    std::int32_t msg_index_state = 0; // the message index, zero-initialised
    int dropped = 0;
    int re_display = -1;
    bool high_res = false;

    // Texts and page-draw behaviour.
    std::map<std::int32_t, std::string> texts;
    std::int32_t draw_page_result = 0;
    std::vector<std::int32_t> draw_page_budgets;

    // Records.
    std::vector<std::string> log;
    std::vector<std::string> sfx;
    int stop_voice_calls = 0;
    int measure_calls = 0;

    // --- clock and input ---
    std::int32_t delta_time_q12() const override { return dt; }
    std::uint32_t virtual_ms() const override { return ms; }
    bool input_enter_clicked() const override { return enter; }
    bool input_cancel_clicked() const override { return cancel; }
    bool input_skip_held() const override { return skip; }
    bool input_up_clicked() const override { return up; }
    bool input_down_clicked() const override { return down; }

    // --- per-frame side effects ---
    void voice_touch() override { log.push_back("touch"); }
    void reset_player_weapon() override { log.push_back("weapon"); }

    // --- identity and text ---
    std::int32_t msg_index() const override { return msg_index_state; }
    void set_msg_index(std::int32_t idx) override { msg_index_state = idx; }
    const char* message_text(std::int32_t idx) const override {
        static const std::string empty;
        const auto it = texts.find(idx);
        return it != texts.end() ? it->second.c_str() : empty.c_str();
    }

    // --- game state ---
    bool string_set_active() const override { return string_set; }
    void set_string_set_active(bool v) override { string_set = v; }
    std::int32_t map_msg_timer() const override { return timer; }
    void set_map_msg_timer(std::int32_t v) override { timer = v; }
    bool cutscene_active() const override { return cutscene; }
    bool voice_dialog() const override { return dialog_on; }
    std::int32_t audio_streaming_state() const override { return streaming; }
    bool voice_draining() const override { return draining_on; }
    bool voice_holding() const override { return holding_on; }
    int voice_dropped() const override { return dropped; }
    void set_voice_dropped(int v) override { dropped = v; }
    void set_re_display_setup(int v) override { re_display = v; }
    bool silent_yes_selection() const override { return silent_yes; }
    void clear_silent_yes_selection() override { silent_yes = false; }
    void set_high_res_glyphs(bool on) override { high_res = on; }

    // --- selection / audio-load state ---

    // --- sound ---
    void play_sfx(Sfx s) override {
        sfx.push_back(s == Sfx::Cancel ? "cancel" : s == Sfx::Confirm ? "confirm" : "move");
    }
    void stop_voice() override {
        ++stop_voice_calls;
        log.push_back("stop_voice");
    }

    // --- flow hooks ---
    void update_timer() override { log.push_back("timer_update"); }
    void voice_on_page(std::int32_t idx) override { log.push_back("on_page " + std::to_string(idx)); }
    void voice_on_end() override { log.push_back("on_end"); }
    void page_reset() override { log.push_back("page_reset"); }
    bool page_pending() const override { return page_pending_flag; }
    void page_advance() override { log.push_back("page_advance"); }

    // --- drawing ---
    void begin_page_render() override { log.push_back("begin_render"); }
    void default_string_info() override { log.push_back("default_info"); }
    std::int32_t measure(std::int32_t idx) override {
        ++measure_calls;
        log.push_back("measure " + std::to_string(idx));
        return 0;
    }
    std::int32_t draw_page(std::int32_t idx, std::int32_t budget) override {
        draw_page_budgets.push_back(budget);
        log.push_back("draw_page " + std::to_string(idx));
        return draw_page_result;
    }
    void draw_option_fixed(std::int32_t line, std::int32_t color_id) override {
        log.push_back("opt_fixed " + std::to_string(line) + " c" + std::to_string(color_id));
    }
    void draw_option_listed(std::int32_t prompt_idx, std::int32_t line, std::int32_t color_id) override {
        log.push_back("opt_listed " + std::to_string(prompt_idx + line + 1) + " c" + std::to_string(color_id));
    }

    std::int32_t white_id = 7;
    std::int32_t white_color_id() const override { return white_id; }
    std::int32_t flash_color_base() const override { return 4; }

    bool log_has(const std::string& s) const {
        for (const auto& e : log) {
            if (e == s) return true;
        }
        return false;
    }
    std::size_t log_count(const std::string& s) const {
        std::size_t n = 0;
        for (const auto& e : log) {
            if (e == s) ++n;
        }
        return n;
    }
};

// Index of an entry in the log, or -1.
std::ptrdiff_t log_pos(const FakeHost& h, const std::string& s) {
    for (std::size_t i = 0; i < h.log.size(); ++i) {
        if (h.log[i] == s) return static_cast<std::ptrdiff_t>(i);
    }
    return -1;
}

// A fresh box with a message set up and displayed once (setup returns Finish).
void prime_setup(MessageBox& box, FakeHost& host, std::int32_t idx) {
    host.texts[idx] = "A~E ";
    box.draw(idx, host);
}

}  // namespace

SH_TEST(text_message_box_setup_returns_finish_and_resets_state) {
    FakeHost host;
    MessageBox box;
    box.selection().max_idx = 5;
    box.selection().selected = 3;
    host.timer = 123;
    box.selection().audio_load_block = 2;

    const std::int32_t r = box.draw(7, host);

    SH_CHECK(r == sh::text::kMsgResultFinish);
    SH_CHECK(host.string_set);
    SH_CHECK(host.msg_index_state == 7);
    SH_CHECK(box.selection().max_idx == -1);
    SH_CHECK(box.selection().selected == 0);
    SH_CHECK(box.selection().audio_load_block == 0);
    SH_CHECK(host.timer == -1);
    SH_CHECK(host.dropped == 0);
    // Call order: page reset, voice, defaults, widths.
    const std::ptrdiff_t reset = log_pos(host, "page_reset");
    const std::ptrdiff_t page = log_pos(host, "on_page 7");
    const std::ptrdiff_t info = log_pos(host, "default_info");
    const std::ptrdiff_t measure = log_pos(host, "measure 7");
    SH_CHECK(reset >= 0 && reset < page && page < info && info < measure);
    // No rendering happened on the setup frame.
    SH_CHECK(!host.log_has("draw_page 7"));
}

SH_TEST(text_message_box_rollout_rate_and_fractional_carry) {
    FakeHost host;
    MessageBox box;
    prime_setup(box, host, 5);

    // Q12(2 glyphs * 30 fps) = 245760.
    // dt = 137: 245760*137 >> 12 = 8220 -> 2 glyphs, carry 28.
    host.dt = 137;
    SH_CHECK(box.draw(5, host) == sh::text::kMsgResultIdle);
    SH_CHECK(host.draw_page_budgets.size() == 1 && host.draw_page_budgets[0] == 2);

    // dt = 68: 245760*68 >> 12 = 4080; 4080 + 28 = 4108 -> 1 glyph, carry 12.
    host.dt = 68;
    box.draw(5, host);
    SH_CHECK(host.draw_page_budgets.size() == 2 && host.draw_page_budgets[1] == 3);

    // dt = 1: 245760*1 >> 12 = 60; 60 + 12 = 72 -> 0 glyphs this frame.
    host.dt = 1;
    box.draw(5, host);
    SH_CHECK(host.draw_page_budgets.size() == 3 && host.draw_page_budgets[2] == 3);
}

SH_TEST(text_message_box_rollout_clamps_to_display_all) {
    FakeHost host;
    MessageBox box;
    prime_setup(box, host, 5);

    // dt = 4096 (a full second) steps exactly Q12_MULT(Q12(60), 4096) >> 12 = 60
    // glyphs with no carry; six frames reach 360, the seventh would pass 420
    // and lands on the 400 budget cap. (A single huge dt cannot reach 400: the
    // 32-bit Q12 multiply wraps first, and the original wraps the same way --
    // dt 28672 actually steps negative, clamping the page shut at 0.)
    host.dt = 4096;
    for (int i = 0; i < 6; ++i) {
        box.draw(5, host);
    }
    SH_CHECK(host.draw_page_budgets.size() == 6 && host.draw_page_budgets[5] == 360);
    box.draw(5, host);
    SH_CHECK(host.draw_page_budgets.size() == 7 && host.draw_page_budgets[6] == 400);
}

SH_TEST(text_message_box_input_skips_to_full_page) {
    FakeHost host;
    MessageBox box;
    prime_setup(box, host, 5);

    host.enter = true;
    SH_CHECK(box.draw(5, host) == sh::text::kMsgResultIdle);
    SH_CHECK(host.draw_page_budgets.size() == 1 && host.draw_page_budgets[0] == 400);
}

SH_TEST(text_message_box_display_all_code_sets_full_budget) {
    FakeHost host;
    MessageBox box;
    prime_setup(box, host, 5);

    host.draw_page_result = 20; // ~X display-all code
    box.draw(5, host);
    // The code is stored but does not arm the input branch (20 is not < 4).
    box.draw(5, host);
    SH_CHECK(host.draw_page_budgets.size() == 2);
    SH_CHECK(host.draw_page_budgets[1] == 400);
}

SH_TEST(text_message_box_timer_decrement_only_with_audio_block) {
    FakeHost host;
    MessageBox box;
    prime_setup(box, host, 5);

    box.selection().audio_load_block = 1;
    host.timer = 137;
    host.dt = 137;
    box.draw(5, host);
    SH_CHECK(host.timer == 0); // decremented and clamped at 0

    // Without an audio load block the timer is never touched.
    box.selection().audio_load_block = 0;
    host.timer = 137;
    box.draw(5, host);
    SH_CHECK(host.timer == 137);
    // The timer update hook only runs in the input branch with no audio block.
    SH_CHECK(!host.log_has("timer_update"));
}

SH_TEST(text_message_box_advances_to_next_index_on_timer) {
    FakeHost host;
    MessageBox box;
    host.texts[5] = "AB~N ";
    host.texts[6] = "CD~E ";
    box.draw(5, host); // setup

    box.selection().audio_load_block = 3;      // J2 single-file audio
    host.timer = 137;  // runs out this frame
    host.dt = 137;
    host.draw_page_result = 1; // page over, more follows
    box.draw(5, host);         // render + arm the advance branch
    SH_CHECK(!host.log_has("on_page 6"));

    const std::int32_t r = box.draw(5, host); // timer == 0 -> advance
    SH_CHECK(r == sh::text::kMsgResultIdle);  // J2 pages advance silently
    SH_CHECK(host.log_has("on_page 6"));
    SH_CHECK(host.log_has("measure 6"));
    SH_CHECK(host.timer == -1); // timer re-armed from max_idx (no_value)
    SH_CHECK(!host.log_has("stop_voice"));
    // The next frame renders the new index: the rollout adds its first step
    // (dt 137 -> 2 glyphs) before the draw, exactly as on the first page.
    SH_CHECK(box.draw(5, host) == sh::text::kMsgResultIdle);
    SH_CHECK(host.draw_page_budgets.size() == 2 && host.draw_page_budgets[1] == 2);
}

SH_TEST(text_message_box_pending_page_turns_within_message) {
    FakeHost host;
    MessageBox box;
    host.texts[5] = "AB~P ";
    box.draw(5, host); // setup

    box.selection().audio_load_block = 1;
    host.timer = 1;
    host.dt = 1;
    host.draw_page_result = 1;
    box.draw(5, host); // render, arm

    host.page_pending_flag = true;
    const std::int32_t r = box.draw(5, host);
    SH_CHECK(r == sh::text::kMsgResultFinish); // a real page turn always finishes
    SH_CHECK(host.log_has("page_advance"));
    SH_CHECK(!host.log_has("on_page 6")); // no new voice for a continuation page
    SH_CHECK(host.re_display == 0);        // and it is never a spurious re-display
    SH_CHECK(host.msg_index_state == 5);   // the message index stays at the chain's start
}

SH_TEST(text_message_box_voice_hold_blocks_auto_advance) {
    FakeHost host;
    MessageBox box;
    host.texts[5] = "AB~N ";
    box.draw(5, host);

    box.selection().audio_load_block = 1;
    host.timer = 1;
    host.dt = 1;
    host.draw_page_result = 1;
    box.draw(5, host); // render, arm

    // A loose voice file still playing holds the authored auto-advance.
    host.holding_on = true;
    SH_CHECK(box.draw(5, host) == sh::text::kMsgResultIdle);
    SH_CHECK(!host.log_has("on_page 6"));
    SH_CHECK(!host.log_has("page_advance"));

    // Released: the advance goes through (pending page variant).
    host.holding_on = false;
    host.page_pending_flag = true;
    box.draw(5, host);
    SH_CHECK(host.log_has("page_advance"));
}

SH_TEST(text_message_box_draining_voice_holds_via_dialog_flag) {
    FakeHost host;
    MessageBox box;
    host.texts[5] = "AB~N ";
    box.draw(5, host);

    // Voice dialog path: the wait must release on the playing state first.
    host.dialog_on = true;
    host.streaming = 1;
    box.selection().audio_load_block = 1;
    host.timer = 1;
    host.dt = 1;
    host.draw_page_result = 1;
    box.draw(5, host); // released + rendered, armed

    host.draining_on = true; // still audible: hold
    SH_CHECK(box.draw(5, host) == sh::text::kMsgResultIdle);
    SH_CHECK(!host.log_has("on_page 6"));

    host.draining_on = false;
    box.draw(5, host);
    SH_CHECK(host.log_has("on_page 6"));
}

SH_TEST(text_message_box_j2_chain_exempts_voice_hold) {
    FakeHost host;
    MessageBox box;
    host.texts[4] = "~J2 lead";
    host.texts[5] = "AB~N "; // chaining page: the NEXT page opens the tape's clip
    host.texts[6] = "~J2 x";
    box.draw(5, host);

    box.selection().audio_load_block = 1;
    host.timer = 1;
    host.dt = 1;
    host.draw_page_result = 1;
    box.draw(5, host); // render, arm

    // The chaining page is the clip's lead-in: no hold even while it drains.
    host.dialog_on = true;
    host.streaming = 1;
    host.draining_on = true;
    SH_CHECK(box.draw(5, host) == sh::text::kMsgResultFinish);
    SH_CHECK(host.log_has("on_page 6"));
    SH_CHECK(host.log_has("stop_voice")); // alb is 1, not the J2 block
}

SH_TEST(text_message_box_lone_j2_page_still_holds) {
    FakeHost host;
    MessageBox box;
    host.texts[4] = "~J0 lead";
    host.texts[5] = "AB~E "; // lone ~J2 page that closes its message
    box.draw(5, host);

    box.selection().audio_load_block = 3; // set by the ~J2 page itself
    host.timer = 1;
    host.dt = 1;
    host.draw_page_result = 1;
    box.draw(5, host); // render, arm

    // Previous page is not ~J2: the exemption is revoked, the hold applies.
    host.holding_on = true;
    SH_CHECK(box.draw(5, host) == sh::text::kMsgResultIdle);
    SH_CHECK(!host.log_has("on_page 6"));

    // With a ~J2 previous page the exemption keeps the authored pacing.
    host.texts[4] = "~J2 ride";
    host.holding_on = false;
    box.draw(5, host);
    SH_CHECK(host.log_has("page_advance") || host.log_has("on_page 6"));
}

SH_TEST(text_message_box_select3_options_and_cursor) {
    FakeHost host;
    MessageBox box;
    host.texts[5] = "Pick ~S3";
    host.texts[6] = "Opt1";
    host.texts[7] = "Opt2";
    host.texts[8] = "Opt3";
    box.draw(5, host); // setup

    host.draw_page_result = 3; // Select3
    SH_CHECK(box.draw(5, host) == sh::text::kMsgResultIdle);
    SH_CHECK(box.selection().max_idx == 1);
    SH_CHECK(box.selection().cancel_idx == 2);
    // Three listed options at prompt index + 1: 6, 7, 8; entry 0 flashes
    // (flash timer 136 -> phase 0 -> base color 4), the others stay white.
    SH_CHECK(host.log_has("opt_listed 6 c4"));
    SH_CHECK(host.log_has("opt_listed 7 c7"));
    SH_CHECK(host.log_has("opt_listed 8 c7"));
    SH_CHECK(host.log_count("opt_fixed 0 c4") == 0);

    // Cursor down (entry 0 -> 1) with the move sfx; up is a no-op at the top.
    host.down = true;
    box.draw(5, host);
    SH_CHECK(box.selection().selected == 1);
    SH_CHECK(host.sfx.size() == 1 && host.sfx[0] == "move");
    host.down = false;
    host.up = true;
    box.selection().selected = 0;
    box.draw(5, host); // already at the top: no sfx
    SH_CHECK(host.sfx.size() == 1);
}

SH_TEST(text_message_box_select4_uses_shared_yes_no) {
    FakeHost host;
    MessageBox box;
    host.texts[0] = "Yes";
    host.texts[1] = "No";
    box.draw(5, host);

    host.draw_page_result = 4;
    box.draw(5, host);
    SH_CHECK(box.selection().cancel_idx == 1);
    SH_CHECK(host.log_has("opt_fixed 0 c4")); // entry 0 flashes at phase 0
    SH_CHECK(host.log_has("opt_fixed 1 c7"));
    SH_CHECK(host.log_count("opt_listed 6 c4") == 0);

    // Confirm on the No entry (the cancel index): cancel sfx, returns entry+1.
    box.selection().selected = 1;
    host.enter = true;
    const std::int32_t r = box.draw(5, host);
    SH_CHECK(r == 2);
    SH_CHECK(host.sfx.size() == 1 && host.sfx[0] == "cancel");
    SH_CHECK(host.string_set == false);
    SH_CHECK(host.log_has("on_end"));
    SH_CHECK(host.high_res == false);
}

SH_TEST(text_message_box_cancel_selects_cancel_index) {
    FakeHost host;
    MessageBox box;
    box.draw(5, host);

    host.draw_page_result = 2; // Select2, cancel index 1
    box.draw(5, host);
    SH_CHECK(box.selection().cancel_idx == 1);

    host.cancel = true;
    const std::int32_t r = box.draw(5, host);
    SH_CHECK(r == 2); // cancel index 1 + 1
    SH_CHECK(host.sfx.size() == 1 && host.sfx[0] == "cancel");
    SH_CHECK(box.selection().selected == 1);
    SH_CHECK(host.string_set == false);
}

SH_TEST(text_message_box_confirm_silent_yes) {
    FakeHost host;
    MessageBox box;
    box.draw(5, host);

    host.draw_page_result = 2;
    box.draw(5, host);

    // Silent yes: no confirm sfx, the flag clears, the message finishes.
    host.silent_yes = true;
    host.enter = true;
    const std::int32_t r = box.draw(5, host);
    SH_CHECK(r == 1);
    SH_CHECK(host.sfx.empty());
    SH_CHECK(host.silent_yes == false);
}

SH_TEST(text_message_box_plain_message_finishes_on_input) {
    FakeHost host;
    MessageBox box;
    host.texts[5] = "A~E ";
    box.draw(5, host); // setup

    host.draw_page_result = -1; // the page reached its end code
    box.draw(5, host);          // render; state0 arms on -1
    SH_CHECK(box.draw(5, host) == sh::text::kMsgResultIdle); // waiting for input

    host.enter = true;
    const std::int32_t r = box.draw(5, host);
    SH_CHECK(r == 1); // entry 0 + 1
    SH_CHECK(host.string_set == false);
    SH_CHECK(host.log_has("on_end"));
}

SH_TEST(text_message_box_auto_finish_without_input_when_timer_runs_out) {
    FakeHost host;
    MessageBox box;
    box.draw(5, host);

    host.draw_page_result = -1;
    box.draw(5, host); // arm

    // Timed pages (audio block set, timer at 0) finish with no input at all.
    box.selection().audio_load_block = 1;
    host.timer = 0;
    const std::int32_t r = box.draw(5, host);
    SH_CHECK(r == 1);
    SH_CHECK(host.string_set == false);
}

SH_TEST(text_message_box_redisplay_noop_for_completed_chain) {
    FakeHost host;
    MessageBox box;
    host.texts[5] = "AB~N ";
    host.texts[6] = "CD~E ";
    box.draw(5, host); // setup

    // Advance once so the chain's last page differs from its start index.
    box.selection().audio_load_block = 1;
    host.timer = 1;
    host.dt = 1;
    host.draw_page_result = 1;
    box.draw(5, host);
    box.draw(5, host); // advance to index 6

    // Finish the chain (the audio block must be gone: a timed page with the
    // timer at -1 does not finish on input, in the original too).
    host.draw_page_result = -1;
    box.selection().audio_load_block = 0;
    host.enter = true;
    box.draw(5, host);
    box.draw(5, host);
    SH_CHECK(host.string_set == false);

    // A cutscene re-display of the completed chain is a full no-op.
    host.enter = false;
    host.cutscene = true;
    const std::int32_t r = box.draw(5, host);
    SH_CHECK(r == sh::text::kMsgResultSelectEntry0);
    SH_CHECK(host.log_count("on_page 5") == 1); // nothing re-set-up, no new voice
    SH_CHECK(host.string_set == false);
}

SH_TEST(text_message_box_redisplay_single_page_suppresses_voice_only) {
    FakeHost host;
    MessageBox box;
    host.texts[5] = "A~E ";
    box.draw(5, host); // setup

    host.draw_page_result = -1;
    host.enter = true;
    box.draw(5, host); // arm
    box.draw(5, host); // finish
    SH_CHECK(host.string_set == false);

    // Same index, single page: the lighter path -- a fresh setup flagged as a
    // re-display so the voice wrapper suppresses the duplicate fire.
    host.enter = false;
    const std::int32_t r = box.draw(5, host);
    SH_CHECK(r == sh::text::kMsgResultFinish);
    SH_CHECK(host.re_display == 1);
    SH_CHECK(host.log_count("on_page 5") == 2);
}

SH_TEST(text_message_box_voice_wait_states) {
    FakeHost host;
    MessageBox box;
    box.draw(5, host); // setup

    // Fail-open: while the stream is neither ready (4) nor playing (1), the
    // page holds for 1000 ms of virtual time, then renders anyway.
    host.dialog_on = true;
    host.streaming = 2;
    host.ms = 100;
    SH_CHECK(box.draw(5, host) == sh::text::kMsgResultIdle);
    SH_CHECK(host.draw_page_budgets.empty()); // the wait start latched at 100
    host.ms = 1099;
    SH_CHECK(box.draw(5, host) == sh::text::kMsgResultIdle);
    SH_CHECK(host.draw_page_budgets.empty()); // 999 ms elapsed
    host.ms = 1100;
    box.draw(5, host);
    SH_CHECK(host.draw_page_budgets.size() == 1); // exactly 1000 ms: rendered
}

SH_TEST(text_message_box_voice_wait_dropped_voice_renders_now) {
    FakeHost host;
    MessageBox box;
    box.draw(5, host);

    host.dialog_on = true;
    host.streaming = 2;
    host.dropped = 1; // the page's voice cmd was dropped: render immediately
    box.draw(5, host);
    SH_CHECK(host.draw_page_budgets.size() == 1);
    SH_CHECK(host.dropped == 0);
}

SH_TEST(text_message_box_finish_rearms_wait_for_next_message) {
    FakeHost host;
    MessageBox box;
    box.draw(5, host);

    host.draw_page_result = -1;
    host.dialog_on = true;
    host.streaming = 1; // release the wait
    box.draw(5, host);  // render, arm
    host.enter = true;
    const std::int32_t r = box.draw(5, host);
    SH_CHECK(r == 1);
    SH_CHECK(host.string_set == false);

    // The next message's first frame waits again (latch re-armed by the finish).
    host.enter = false;
    host.draw_page_result = 0;
    host.streaming = 2;
    host.ms = 0;
    box.draw(6, host); // setup
    SH_CHECK(box.draw(6, host) == sh::text::kMsgResultIdle);
    SH_CHECK(host.draw_page_budgets.size() == 1); // only the first message rendered
}

SH_TEST(text_message_box_flash_timer_wraps_each_half_second) {
    FakeHost host;
    host.white_id = 9; // keep white apart from the flash colors (4 and 7)
    MessageBox box;
    box.draw(5, host);

    // dt 1024: the flash timer is 1024 -> phase 1 -> flash base + 3 = 7.
    host.dt = 1024;
    host.draw_page_result = 2;
    box.draw(5, host);
    SH_CHECK(host.log_has("opt_listed 6 c7")); // selected entry 0 flashes
    SH_CHECK(host.log_has("opt_listed 7 c9")); // the other entry stays white

    // Another 1024 wraps the timer (2048 -> 0) -> phase 0 -> base color 4.
    host.dt = 1024;
    box.draw(5, host);
    SH_CHECK(host.log_has("opt_listed 6 c4"));
    SH_CHECK(host.log_has("opt_listed 7 c9"));
}
