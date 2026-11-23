// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// The item screens' game state over the session: the order of the steps is
// sh::inventory::update_item_screen; each host method below is one block of work of a step (the screen setup, the
// fades, the file loads and the drawing calls). GameState_ItemScreens_Update and Game_TimerUpdate keep the names the
// state table, the player and the scripts call.

#include <cstdint>

#include "audio/game/game_sound_driver.h"
#include "engine/game_state/game_state.h"
#include "engine/game_state/save_record.h"
#include "engine/game_state/session.h"
#include "engine/gameflow/game/game_link.h"
#include "engine/inventory/game/inventory_host.h"
#include "engine/inventory/game/item_stage.h"
#include "engine/inventory/item_screen_flow.h"
#include "engine/inventory/item_view.h"
#include "engine/player/player.h"
#include "engine/screens/screens_view.h"
#include "engine/text/game_text.h"
#include "input/mouse_pointer.h"
#include "engine/loop/frame_view.h"
#include "engine/gameflow/title_menu.h"


extern "C" {
// The drawing parts of the screen (inventory_draw_bridge.cpp, item_screens_draw.cpp), the held item (the player's
// scene) and the music and radio of other modules.
void Gfx_Inventory_CmdOptionsDraw(void);
void Gfx_Inventory_ScrollArrowsDraw(std::int32_t* selection);
void Gfx_Results_Save(void);
void Gfx_Items_DrawInit(void);
void Gfx_ItemScreens_DrawInit(std::uint32_t* selection);
void Inventory_Logic(void);
void Inventory_ExitAnimFixes(void);
std::int32_t WorldGfx_PlayerPrevHeldItem(sh::game::PlayerCombat* combat);
void WorldGfx_HeldItemShow(void);
void Bgm_TrackIdxUpdate(void);
void Game_RadioSoundStop(void);
void Inventory_UnlockedItemsGrant(void);
void Game_TimerUpdate(void);
}

namespace {

namespace inv = sh::inventory;
namespace link = sh::gameflow::game;
using sh::game::save_record;
using sh::game::session;

// Screens of the top-level state machine.
constexpr std::int32_t kMovieIntro = 6;
constexpr std::int32_t kInGame = 11;
constexpr std::int32_t kInventoryScreen = 14;
constexpr std::int32_t kPaperMapScreen = 15;
constexpr std::int32_t kSaveScreen = 16;
constexpr std::int32_t kOptionScreen = 18;
constexpr std::int32_t kLoadStatusScreen = 19;

constexpr std::int32_t kFadeNone = 1;  // the fade machine is idle (no colour bit)
constexpr std::int32_t kFadeTimestep = 3 * 4096;
constexpr std::int16_t kWhiteText = 7;
constexpr std::uint16_t kSoundMove = 1305, kSoundConfirm = 1307;
constexpr std::uint32_t kPadRight = 1u << 25, kPadLeft = 1u << 27;
constexpr std::int32_t kNoticeCantUse = 3, kNoticeTooDark = 4;
constexpr std::uint32_t kSelectionItem = 0;
constexpr std::int32_t kNormalDifficulty = 0;  // Normal and below go one step up
constexpr std::uint8_t kResultsBits = 0x1F;   // the ending being shown (the save's current ending)
constexpr std::uint8_t kRestartBit = 1u << 6;

void screen_open()
{
    link::clear_interlaced_screen();
    link::screen_init(true);
    link::set_interval_vblanks(1);
    sh::game_state::screen_fade_start(true, true, false);
    sh::game_state::set_screen_fade_timestep(kFadeTimestep);
}

void set_clear_black()
{
    session().game.clear_color.r = 0;
    session().game.clear_color.g = 0;
    session().game.clear_color.b = 0;
}

class ScreenHost final : public inv::ScreenHost {
public:
    std::int32_t step() override { return session().game.game_steps[1]; }

    void set_step(std::int32_t step) override
    {
        session().game.game_steps[1] = step;
        session().game.game_steps[2] = 0;
    }

    std::int32_t sub_step() override { return session().game.game_steps[2]; }
    void set_sub_step(std::int32_t sub_step) override { session().game.game_steps[2] = sub_step; }

    void begin_frame() override
    {
        sh::text::game_text().set_color(kWhiteText);
        Bgm_TrackIdxUpdate();
    }

    void advance_play_time() override { Game_TimerUpdate(); }

    bool results_pending() override { return (static_cast<std::uint8_t>(save_record().current_ending) & kResultsBits) != 0; }

    bool restart_requested() override
    {
        return (save_record().current_ending & kRestartBit) && session().game.game_state_previous == kSaveScreen;
    }

    void restart_game() override
    {
        sh::gameflow::title_demo_count() = 0;
        link::warm_boot();
        link::load_movie_stream();
        link::wait_file_queue();
        session().set_game_state_next(kMovieIntro);
    }

    void grant_unlocked_items() override { Inventory_UnlockedItemsGrant(); }

    void open_screen() override
    {
        screen_open();
        Game_RadioSoundStop();
    }

    inv::EnteredFrom entered_from() override
    {
        switch (session().game.game_state_previous)
        {
            case kLoadStatusScreen: return inv::EnteredFrom::StatusScreen;
            case kOptionScreen:     return inv::EnteredFrom::Options;
            case kPaperMapScreen:   return inv::EnteredFrom::Map;
            default:                return inv::EnteredFrom::Other;
        }
    }

    void start_menu(bool has_selection, inv::Selection selection) override
    {
        inv::InventoryMenuState& menu = inv::inventory_menu();
        menu.scrolling = 1;
        menu.outline_age = 0;
        menu.scrolled = 0;
        menu.command = 0;
        inv::item_screen().carousel.shown_slot = session().system.inventory_selected;
        if (has_selection)
        {
            menu.selection = static_cast<std::uint32_t>(selection);
            menu.outline_target = static_cast<std::int32_t>(selection);
        }
    }

    void prepare_items() override
    {
        session().system.characters.player_upper.last_used_item = 0;
        set_clear_black();
        Gfx_Items_DrawInit();
        inv::game::assign_commands();
    }

    bool menu_ready() override
    {
        return session().system.inventory_selected == inv::item_screen().carousel.shown_slot &&
               session().game.game_state == kInventoryScreen && sh::screens::fade_status() == kFadeNone;
    }

    void run_menu() override { Inventory_Logic(); }
    void draw_commands() override { Gfx_Inventory_CmdOptionsDraw(); }

    void show_notice(inv::Notice notice) override
    {
        session().set_sys_step(1, notice == inv::Notice::CantUseHere ? kNoticeCantUse : kNoticeTooDark);
    }

    void fade_out() override { sh::game_state::screen_fade_start(true, false, false); }
    bool fade_finished() override { return sh::game_state::screen_fade_finished(); }
    void update_weapon() override { sh::game::player().update_weapon(); }

    void go_to_options() override
    {
        link::screen_refresh();
        link::wait_file_queue();
        inv::item_stage().close_projection();
        inv::item_screen().description.last_group = 0xFF;
        inv::item_screen().description.last_index = 0xFF;
        session().set_game_state_next(kOptionScreen);
    }

    void go_to_map() override
    {
        inv::item_stage().close_projection();
        session().set_game_state_next(kPaperMapScreen);
    }

    void return_to_game() override
    {
        link::screen_refresh();
        Inventory_ExitAnimFixes();
        inv::game::tidy_live();
        WorldGfx_PlayerPrevHeldItem(&session().system.combat);
        WorldGfx_HeldItemShow();
        link::wait_file_queue();
        session().set_game_state_next(kInGame);
    }

    void open_results() override
    {
        screen_open();
        set_clear_black();
        inv::inventory_menu().selection = kSelectionItem;

        Gfx_Items_DrawInit();
        sh::screens::evaluate_ranking();

        // The next game is one difficulty higher, up to Hard.
        sh::game::SaveRecord& save = save_record();
        inv::inventory_menu().difficulty = save.difficulty();
        if (save.difficulty() <= kNormalDifficulty)
            save.set_difficulty(save.difficulty() + 1);

        link::boot_savegame_initialize(0, save.difficulty());
        sh::screens::apply_ranking_carry_over();
    }

    bool results_continue_pressed() override
    {
        const sh::game::PadBinds& binds = session().game.settings.binds;
        return (session().game.pads[0].clicked & (binds.enter | binds.skip)) != 0;
    }

    bool results_side_pressed() override { return (session().game.pads[0].clicked & (kPadRight | kPadLeft)) != 0; }

    bool results_enter_pressed() override
    {
        return (session().game.pads[0].clicked & session().game.settings.binds.enter) != 0;
    }

    int results_choice() override { return static_cast<int>(inv::inventory_menu().selection); }
    void set_results_choice(int choice) override { inv::inventory_menu().selection = static_cast<std::uint32_t>(choice); }

    void sound(inv::MenuSound sound, int pan) override
    {
        sh::audio::game_sound_driver().play_effect(sound == inv::MenuSound::Move ? kSoundMove : kSoundConfirm,
                                                   static_cast<std::int8_t>(pan), 64);
    }

    void load_save_screen() override { sh::game_state::load_save_screen_files(); }

    void go_to_save_screen() override
    {
        link::screen_refresh();
        sh::game_state::load_save_screen_files();
        link::wait_file_queue();
        session().set_game_state_next(kSaveScreen);
    }

    bool restart_data_loaded() override { return link::file_chunks_loaded(); }

    void draw_screen() override { Gfx_ItemScreens_DrawInit(&inv::inventory_menu().selection); }

    void draw_scroll_arrows() override
    {
        Gfx_Inventory_ScrollArrowsDraw(reinterpret_cast<std::int32_t*>(&inv::inventory_menu().selection));
        sh::input::mouse_pointer().show();
    }

    void draw_results() override { Gfx_Results_Save(); }
};

}  // namespace

extern "C" {

void Game_TimerUpdate(void)
{
    sh::game::SaveRecord& save = save_record();
    std::uint32_t timer = save.play_time;
    // Bits 1..2 of the mode byte: 290 hours per overflow of the timer.
    std::uint8_t overflows = static_cast<std::uint8_t>((save.mode_bits >> 1) & 0x03);
    inv::advance_play_time(timer, overflows, sh::loop::frame_delta_raw());
    save.play_time = timer;
    save.mode_bits = static_cast<std::uint8_t>((save.mode_bits & ~0x06) | ((overflows & 0x03) << 1));
}

void GameState_ItemScreens_Update(void)
{
    ScreenHost host;
    inv::update_item_screen(host);
}

}  // extern "C"
