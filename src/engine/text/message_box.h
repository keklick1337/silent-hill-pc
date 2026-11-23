// SPDX-License-Identifier: GPL-3.0-or-later
// The map-message box state machine: setup, voice wait, typewriter rollout, page
// and next-message advance, selection handling, finish. All its state lives
// here; everything game-side (input, timers, audio, the
// text itself, drawing) is asked through MessageBoxHost so the state machine's
// read/write order relative to those calls is the original's.

#pragma once

#include <cstdint>

namespace sh::text {

// Result codes of MessageBox::draw — the game's message-state contract.
inline constexpr std::int32_t kMsgResultFinish       = -1; // setup / advance / input done
inline constexpr std::int32_t kMsgResultIdle         = 0;  // keep displaying
inline constexpr std::int32_t kMsgResultSelectEntry0 = 1;  // spurious re-display no-op

// Everything host-specific the box needs as data, with the game's values.
struct MessageBoxSpec {
    std::int32_t display_all_length      = 400;        // "show the whole page" budget
    std::int32_t timer_max               = 0x7FFFFFFF; // the PSX saturating conversion of Q12(524288.0f)-1
    std::int32_t streaming_ready_state   = 4;          // XA "ready" the original wait watches
    std::int32_t streaming_playing_state = 1;          // XA "playing" the synced release watches
    std::uint8_t audio_block_j2          = 3;          // single-file-audio (J2) load block id
    std::int8_t  no_value                = -1;         // the sentinel for "no selection / no timer"
};

class MessageBoxHost {
public:
    virtual ~MessageBoxHost() = default;

    enum class Sfx { Cancel, Confirm, Move };

    // Clock and frame input.
    virtual std::int32_t  delta_time_q12() const = 0;
    virtual std::uint32_t virtual_ms() const     = 0;
    virtual bool input_enter_clicked() const     = 0;
    virtual bool input_cancel_clicked() const    = 0;
    virtual bool input_skip_held() const         = 0;
    virtual bool input_up_clicked() const        = 0;
    virtual bool input_down_clicked() const      = 0;

    // Per-frame side effects, in call order.
    virtual void voice_touch()         = 0; // runs before anything else each call
    virtual void reset_player_weapon() = 0; // gas weapon timer/model reset

    // Message identity and text.
    virtual std::int32_t msg_index() const                  = 0; // the exported msg index global
    virtual void         set_msg_index(std::int32_t idx)    = 0;
    virtual const char*  message_text(std::int32_t idx) const = 0;

    // Game state the box reads and writes.
    virtual bool string_set_active() const           = 0;
    virtual void set_string_set_active(bool active)  = 0;
    virtual std::int32_t map_msg_timer() const       = 0;
    virtual void set_map_msg_timer(std::int32_t t)   = 0;
    virtual bool cutscene_active() const             = 0; // cutscene flag OR border state
    virtual bool voice_dialog() const                = 0;
    virtual std::int32_t audio_streaming_state() const = 0;
    virtual bool voice_draining() const              = 0; // real audio still playing
    virtual bool voice_holding() const               = 0; // loose-file voice still owed screen time
    virtual int  voice_dropped() const               = 0;
    virtual void set_voice_dropped(int v)            = 0;
    virtual void set_re_display_setup(int v)         = 0;
    virtual bool silent_yes_selection() const        = 0;
    virtual void clear_silent_yes_selection()        = 0;
    virtual void set_high_res_glyphs(bool on)        = 0;

    // Sound.
    virtual void play_sfx(Sfx sfx) = 0;
    virtual void stop_voice()      = 0; // stop the current voice line

    // Flow hooks.
    virtual void update_timer()                  = 0;
    virtual void voice_on_page(std::int32_t idx) = 0;
    virtual void voice_on_end()                  = 0;
    virtual void page_reset()                    = 0;
    virtual bool page_pending() const            = 0;
    virtual void page_advance()                  = 0;

    // Drawing. draw_page returns the page's markup result code (select, display
    // all, end...); begin_page_render sets the page's base color and position.
    virtual void begin_page_render()    = 0;
    virtual void default_string_info()  = 0;
    virtual std::int32_t measure(std::int32_t idx) = 0;
    virtual std::int32_t draw_page(std::int32_t idx, std::int32_t budget) = 0;
    // Option lines: fixed = the shared first two messages, listed = the messages
    // that follow the prompt's own index. The host owns their positions.
    virtual void draw_option_fixed(std::int32_t line, std::int32_t color_id) = 0;
    virtual void draw_option_listed(std::int32_t prompt_idx, std::int32_t line,
                                    std::int32_t color_id) = 0;

    // Palette ids for the selection flash: the timer-driven formula runs here,
    // the palette entries themselves are the game's.
    virtual std::int32_t white_color_id() const  = 0;
    virtual std::int32_t flash_color_base() const = 0;
};

class MessageBox {
public:
    // The yes/no selection and the voice load block of the message on screen.
    struct Selection {
        std::int8_t max_idx = 0;           // highest option index; no_value: no selection
        std::uint8_t selected = 0;         // the highlighted option
        std::uint8_t audio_load_block = 0; // voice load block of the message (spec.audio_block_j2: single file)
        std::int8_t cancel_idx = 0;        // the option Cancel picks
    };

    explicit MessageBox(const MessageBoxSpec& spec = {}) : spec_(spec) {}

    std::int32_t draw(std::int32_t request_idx, MessageBoxHost& host);

    // The option/page updater behind the draw loop; also callable on its own
    // (the game exports it). display_length is the rollout budget in/out.
    std::int32_t selection_update(std::int32_t msg_idx, MessageBoxHost& host,
                                  std::int32_t* display_length);

    const MessageBoxSpec& spec() const { return spec_; }
    Selection& selection() { return sel_; }
    const Selection& selection() const { return sel_; }

private:
    bool single_file_audio_(const MessageBoxHost& host) const;
    std::int32_t finish_tail_(MessageBoxHost& host);

    MessageBoxSpec spec_;
    Selection sel_;

    // State of the game's message display.
    std::int32_t  state0_           = 0;  // stateMachineIdx0
    std::int32_t  state1_           = 0;  // stateMachineIdx1
    std::int32_t  display_length_   = 0;  // msgDisplayLength
    std::int32_t  display_inc_      = 2;  // msgDisplayInc (glyphs per step)
    std::int32_t  voice_wait_latch_ = 0;  // the page waits for its voice line
    std::uint32_t voice_wait_ms_    = 0;  // s_voiceWaitStartMs
    std::int32_t  glyph_accum_      = 0;  // s_glyphAccum (Q12)
    std::int16_t  flash_timer_      = 0;  // g_MapMsg_SelectFlashTimer (Q12)
    std::int32_t  current_idx_      = 0;  // g_MapMsg_CurrentIdx
};

// The game's one message box.
MessageBox& message_box();

}  // namespace sh::text
