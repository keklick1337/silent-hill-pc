// SPDX-License-Identifier: GPL-3.0-or-later
// The map-message box on the engine (src/engine/text/message_box.h): input, timers, voice, sound and the text
// drawer for sh::text::message_box(). Gfx_MapMsg_Draw and Gfx_MapMsg_SelectionUpdate are the game's entry
// points (events, the script API); init_message_box runs at the game's start.

#include <cstdint>

#include "audio/voice/game_message_voice.h"
#include "audio/voice/voice_line_player.h"
#include "engine/game_state/game_state.h"
#include "engine/game_state/game_state_ui.h"
#include "engine/game_state/session.h"
#include "engine/gameflow/map_event_state.h"
#include "engine/screens/screens_view.h"
#include "engine/text/game_text.h"
#include "engine/text/message_box.h"
#include "engine/text/message_box_view.h"
#include "engine/timing/vblank.h"
#include "input/pc_actions.h"

// The index of the message on screen.
static std::int32_t s_message_index;

// Set by the event code when it drops this page's voice command, and below when a spurious re-display of a
// message that already fired its voice is set up.

namespace {

namespace t = sh::text;
namespace gs = sh::game_state;

sh::game::Session& game() { return sh::game::session(); }
const sh::game::PadState& pad() { return sh::game::session().game.pads[0]; }

constexpr int kMenuSfxVolume = 64;
constexpr int kSoundStopVoice = 19;
// The letterbox state with no bars (the game's border state encoding starts at 1).
constexpr std::int32_t kBordersNone = 1;
constexpr std::uint32_t kMusicVoiceDialog = 1u << 5;

class MessageBoxHostImpl final : public t::MessageBoxHost {
public:
    // Clock and frame input.
    std::int32_t delta_time_q12() const override { return gs::delta_time_raw(); }
    std::uint32_t virtual_ms() const override { return sh::timing::virtual_time::ticks_ms(); }
    bool input_enter_clicked() const override {
        const auto& binds = game().game.settings.binds;
        return (pad().clicked & (binds.enter | (binds.skip & PcActions_TapBits()))) != 0;
    }
    bool input_cancel_clicked() const override { return (pad().clicked & game().game.settings.binds.cancel) != 0; }
    // A held pad button fast-forwards the text; the Confirm key is one press, one page.
    bool input_skip_held() const override {
        return (pad().held & game().game.settings.binds.skip & ~PcActions_TapBits()) != 0;
    }
    bool input_up_clicked() const override { return (pad().clicked & gs::pad::kStickUp) != 0; }
    bool input_down_clicked() const override { return (pad().clicked & gs::pad::kStickDown) != 0; }

    // Per-frame side effects.
    void voice_touch() override { sh::audio::message_voice().box_drawn(); }
    void reset_player_weapon() override { t::stop_player_gas_weapon(); }

    // Message identity and text.
    std::int32_t msg_index() const override { return s_message_index; }
    void set_msg_index(std::int32_t idx) override { s_message_index = idx; }
    const char* message_text(std::int32_t idx) const override { return gs::map_messages()[idx]; }

    // Game state.
    bool string_set_active() const override { return game().system.flow.message_shown != 0; }
    void set_string_set_active(bool active) override { game().system.flow.message_shown = active ? 1 : 0; }
    std::int32_t map_msg_timer() const override { return game().system.map_message_timer; }
    void set_map_msg_timer(std::int32_t v) override { game().system.map_message_timer = v; }
    bool cutscene_active() const override
    {
        return game().sys_flag(sh::game::sys_flag::kCutscene) || game().system.flow.cutscene_border != kBordersNone;
    }
    bool voice_dialog() const override { return (game().system.process.music_status & kMusicVoiceDialog) != 0; }
    std::int32_t audio_streaming_state() const override { return t::voice_stream_state(); }
    bool voice_draining() const override { return sh::audio::voice_lines().sounding(); }
    bool voice_holding() const override { return sh::audio::message_voice().page_held(); }
    int voice_dropped() const override { return sh::gameflow::map_event_state().voice_dropped; }
    void set_voice_dropped(int v) override { sh::gameflow::map_event_state().voice_dropped = v; }
    void set_re_display_setup(int v) override { sh::gameflow::map_event_state().redisplay_setup = v; }
    // text_flags: half-height glyphs in the low nibble, a silent "yes" in the high one.
    bool silent_yes_selection() const override { return (game().system.text_flags >> 4) != 0; }
    void clear_silent_yes_selection() override { game().system.text_flags &= 0x0F; }
    void set_high_res_glyphs(bool on) override
    {
        game().system.text_flags = static_cast<std::uint8_t>((game().system.text_flags & 0xF0) | (on ? 1 : 0));
    }

    // Sound.
    void play_sfx(Sfx sfx) override
    {
        switch (sfx) {
            case Sfx::Cancel: gs::sfx_play(gs::sfx::kMenuCancel, kMenuSfxVolume); break;
            case Sfx::Confirm: gs::sfx_play(gs::sfx::kMenuConfirm, kMenuSfxVolume); break;
            case Sfx::Move: gs::sfx_play(gs::sfx::kMenuMove, kMenuSfxVolume); break;
        }
    }
    void stop_voice() override { gs::sound_command(kSoundStopVoice); }

    // Flow hooks.
    void update_timer() override { sh::screens::update_play_timer(); }
    void voice_on_page(std::int32_t idx) override { sh::audio::message_voice().page_started(idx); }
    void voice_on_end() override { sh::audio::message_voice().box_closed(); }
    void page_reset() override { t::game_text().reset_pages(); }
    bool page_pending() const override { return t::game_text().page_pending(); }
    void page_advance() override { t::game_text().advance_page(); }

    // Drawing.
    void begin_page_render() override
    {
        t::game_text().set_color(gs::text_color::kWhite);
        t::game_text().set_position(40, 160);
    }
    void default_string_info() override { t::game_text().reset_message_layout(); }
    std::int32_t measure(std::int32_t idx) override
    {
        t::game_text().measure_message(idx);
        return 0;
    }
    std::int32_t draw_page(std::int32_t idx, std::int32_t budget) override
    {
        return t::game_text().draw_message(const_cast<char*>(gs::map_messages()[idx]), budget);
    }
    void draw_option_fixed(std::int32_t line, std::int32_t color_id) override
    {
        t::game_text().set_color(static_cast<std::int16_t>(color_id));
        t::game_text().set_position(32, (16 * line) + 98);
        t::game_text().draw(const_cast<char*>(gs::map_messages()[line]), t::message_box().spec().display_all_length);
    }
    void draw_option_listed(std::int32_t prompt_idx, std::int32_t line, std::int32_t color_id) override
    {
        t::game_text().set_color(static_cast<std::int16_t>(color_id));
        t::game_text().set_position(32, (16 * line) + 96);
        t::game_text().draw(const_cast<char*>(gs::map_messages()[(prompt_idx + line) + 1]), t::message_box().spec().display_all_length);
    }

    // Palette ids for the selection flash.
    std::int32_t white_color_id() const override { return gs::text_color::kWhite; }
    std::int32_t flash_color_base() const override { return 4; }
};

MessageBoxHostImpl s_host;

}  // namespace

namespace sh::text {

void init_message_box()
{
    message_box().selection().max_idx = message_box().spec().no_value;
    upload_message_font_palette();
}

}  // namespace sh::text

extern "C" {

std::int32_t Gfx_MapMsg_Draw(std::int32_t mapMsgIdx) { return t::message_box().draw(mapMsgIdx, s_host); }

std::int32_t Gfx_MapMsg_SelectionUpdate(unsigned char mapMsgIdx, std::int32_t* displayLength)
{
    return t::message_box().selection_update(mapMsgIdx, s_host, displayLength);
}

}  // extern "C"
