// SPDX-License-Identifier: GPL-3.0-or-later
// The pause menu on the running game (pause_menu.h).
#include "ui/pause/pause_menu.h"

#include <cstdint>

#include "engine/audio/sound_api.h"
#include "engine/config/pc_config.h"
#include "engine/game_state/game_state.h"
#include "engine/log/sh_log.h"
#include "features/quicksave/quick_save.h"
#include "input/pc_actions.h"
#include "input/mouse_pointer.h"
#include "render/gpu/render_state.h"
#include "ui/menu/menu_view.h"
#include "ui/menu/nm_draw.h"
#include "ui/menu/pc_debug_menu.h"
#include "ui/screens/pc_controls_ref.h"

// Set for the main loop: the frozen frame is let go on the first tick that drew the world again.
extern int g_PcFreezeReleasePending;

namespace sh::ui {

namespace {

namespace gs = sh::game_state;

// The game's sound commands the pause uses: 3 fades everything to silence when the pause begins (the game's own
// pause does it), 4 fades it back.
constexpr int kSoundUnmute = 4;

// The heading is shown and hidden for 2048 units of the game's raw time each.
constexpr int kBlinkShift = 11;

sh::menu::Controller& controller() {
    static sh::menu::Controller c(sh::menu::registry());
    return c;
}

sh::menu::MenuView& view() {
    static sh::menu::MenuView v = [] {
        sh::menu::MenuView made(sh::menu::nm_canvas());
        made.set_style(sh::menu::Style::Pause);
        return made;
    }();
    return v;
}

// gamedata/menus/pause.lua is the menu; without it the player must still be able to leave the pause.
void define_fallback_menu() {
    sh::menu::RowDef go_on;
    go_on.id = "continue";
    go_on.label = "ui.pause.continue";
    go_on.native = "pause.continue";

    sh::menu::RowDef exit;
    exit.id = "exit";
    exit.label = "ui.pause.exit";
    exit.native = "pause.exit";
    exit.confirm = "ui.pause.return_to_title";

    sh::menu::MenuDef menu;
    menu.id = "pause";
    menu.title = "ui.pause.paused";
    menu.rows.push_back(std::move(go_on));
    menu.rows.push_back(std::move(exit));
    sh::menu::registry().define(std::move(menu));
}

// The pause holds the game's master fade at silence; the menu's own cues are keyed outside the master so they
// still sound. A voice stays outside until it is keyed again, so a cue that outlives the menu (Continue's, under
// the fade back up) is neither cut nor faded.
void cue(int sound) {
    ShSound_SetMainExempt(1);
    gs::sound_command(sound);
    ShSound_SetMainExempt(0);
}

bool debug_menu_wanted() { return Pc_DebugMenu_Pending() != 0 || Pc_DebugMenu_IsOpen() != 0; }

}  // namespace

PauseMenu& pause_menu() {
    static PauseMenu menu;
    return menu;
}

void PauseMenu::note_entry() {
    const bool by_back = PcActions_TakePauseFromBack() != 0;
    entered_by_back_ = g_PcConfig.pauseMenuOnBack != 0 && by_back;
}

bool PauseMenu::replaces_pause() const {
    return g_PcConfig.pauseMenu != 0 || entered_by_back_ || debug_menu_wanted();
}

bool PauseMenu::stops_clock() const { return g_PcConfig.pauseMenu != 0 || debug_menu_wanted(); }

bool PauseMenu::is_shown() const { return open_ && leaving_ == Leaving::Nowhere && !return_pending_; }

bool PauseMenu::take_return() {
    const bool pending = return_pending_;
    return_pending_ = false;
    return pending;
}

void PauseMenu::reset() {
    // Away on a screen the pause opened its mute is still held; every way off that path that does not come back
    // to the pause has to lift it, as the pause's own exits do.
    if (return_pending_) {
        gs::sound_command(kSoundUnmute);
    }
    ShSound_SetMainExempt(0);
    nm::keep_text_unfiltered(false);
    return_pending_ = false;
    sh::render::feedback_control().freeze_capture_hold = 0;
    open_ = false;
    blink_ = 0;
    controller().close();
    leaving_ = Leaving::Nowhere;
}

void PauseMenu::away_update() {
    const gs::GameState state = gs::game_state();
    const bool on_opened_screen = state == gs::GameState::SaveScreen || state == gs::GameState::LoadSavegameScreen ||
                                  state == gs::GameState::OptionScreen;
    if (return_pending_ && !on_opened_screen && state != gs::GameState::InGame) {
        reset();
    }

    // Those screens key their cues from this thread, as the menu does, and nothing else sounds while they are up
    // (the world is not updated, the music's notes come from the sequencer's thread): being outside the master
    // for the whole screen leaves the game's own sound under the mute.
    ShSound_SetMainExempt(return_pending_ && on_opened_screen ? 1 : 0);

    // In play the debug menu lives in the pause; whatever leaves the pause takes the menu along.
    const gs::SysState sys = gs::sys_state();
    if (Pc_DebugMenu_IsOpen() != 0 && state == gs::GameState::InGame && sys != gs::SysState::GamePaused) {
        Pc_DebugMenu_Close();
    }
    // A debug menu asked for over the running game: the pause has to follow within a tick or two, or not at all.
    if (Pc_DebugMenu_Pending() != 0 && state != gs::GameState::InGame) {
        Pc_DebugMenu_ClearPending();
    }
    if (Pc_DebugMenu_Pending() != 0 && sys != gs::SysState::Gameplay && sys != gs::SysState::GamePaused) {
        Pc_DebugMenu_ClearPending();
    }
}

void PauseMenu::enter_for_debug() {
    // What the pause press does in the main loop: nothing waiting to be let go, then the pause.
    g_PcFreezeReleasePending = 0;
    entered_by_back_ = false;
    gs::set_sys_state_next(gs::SysState::GamePaused);
}

void PauseMenu::register_rows() {
    if (rows_registered_) {
        return;
    }
    rows_registered_ = true;

    sh::menu::Registry& registry = sh::menu::registry();
    const auto ask = [&registry](const char* native, Request request) {
        registry.native_action(native, [request] { pause_menu().request_ = request; });
    };
    ask("pause.continue", Request::Resume);
    ask("pause.debug", Request::Debug);
    ask("pause.save", Request::Save);
    ask("pause.load", Request::Load);
    ask("pause.option", Request::Option);
    ask("pause.exit", Request::Exit);
    registry.native_action("pause.controls", [] { Pc_ControlsRef_Open(); });
    // A row's `visible` and `enabled` are the predicates named as its native.
    // Debug Options shows once the player turns it on (Options > Interface > Debug Options).
    registry.native_visible("pause.debug", [] {
        return g_PcConfig.debugOptions != 0 && sh::menu::registry().find("debug") != nullptr;
    });
    registry.native_enabled("pause.save", [] { return save_allowed(pause_menu().block_); });
}

void PauseMenu::open() {
    block_ = save_block(sh::quicksave::boss_alive(), g_PcConfig.pauseMenu != 0, g_PcConfig.saveAnywhere != 0);
    if (sh::menu::registry().find("pause") == nullptr) {
        SH_WARN("[PAUSE] no menu \"pause\" (gamedata/menus/pause.lua): built-in Continue / Exit");
        define_fallback_menu();
    }
    request_ = Request::None;
    controller().open("pause");
    view().fit(controller());
    first_frame_ = true;
    open_ = true;
    SH_DBG("[PAUSE] open (block %d)", static_cast<int>(block_));
}

void PauseMenu::draw() {
    view().set_header_visible(((blink_ >> kBlinkShift) & 1) == 0);
    view().draw(controller());
}

sh::menu::MenuInput PauseMenu::read_input(bool first_frame) {
    const nm::Input in = nm::read_input();
    view().fit(controller());

    sh::menu::MenuInput mi;
    mi.up = in.up;
    mi.down = in.down;
    mi.left = in.left;
    mi.right = in.right;
    mi.mirrored = in.mirrored;
    mi.confirm = in.confirm;
    mi.cancel = in.cancel;
    mi.wheel = in.wheel;
    mi.pointer_moved = in.pointer_moved;
    mi.click = in.click;
    mi.right_click = in.right_click;
    view().pointer_input(controller(), in.pointer, in.pointer_x, in.pointer_y, mi);

    // A Pause bind that is not also a confirm button closes the menu as Cancel does; where it shares Start with
    // Enter it confirms the row instead.
    std::uint16_t binds[gs::kPadBindCount] = {};
    std::uint32_t clicked = 0;
    gs::pad_binds(binds);
    gs::pad_clicked(&clicked);
    if (in.pointer) {
        clicked &= ~sh::input::mouse_pointer().bound_pad_bits();
    }
    if ((clicked & binds[gs::kPadPause] & ~binds[gs::kPadEnter]) != 0) {
        mi.cancel = true;
    }

    if (first_frame) {
        mi.pointer_moved = false;
        mi.click = false;
    }
    return mi;
}

// Continue, a Cancel, and the end of a debug menu opened over the running game. `cue_sound` 0: no cue.
void PauseMenu::resume(int cue_sound) {
    if (cue_sound != 0) {
        cue(cue_sound);
    }
    gs::sound_command(kSoundUnmute);
    // Letting the frozen frame go on this tick shows one bare frame of fog colour: the world is not drawn on a
    // tick the pause ran. The main loop lets it go on a tick that drew.
    g_PcFreezeReleasePending = 1;
    reset();
    SH_DBG("[PAUSE] resume");
    gs::set_sys_state_next(gs::SysState::Gameplay);
}

// Save, Load and Option keep the pause's mute up on their screens and back in the pause; Continue's sound command
// lifts it, and reset() does on any other way out.
void PauseMenu::leave(Leaving to) {
    if (to == Leaving::Title) {
        gs::sound_command(kSoundUnmute);
        reset();
        gs::request_warm_reset();
        SH_DBG("[PAUSE] exit to title");
        return;
    }

    return_pending_ = true;
    sh::render::feedback_control().freeze_capture_hold = 1;
    open_ = false;
    nm::keep_text_unfiltered(false);
    SH_DBG("[PAUSE] -> %s", controller().last_row_id().c_str());
    if (to == Leaving::OptionScreen) {
        gs::set_sys_state_next(gs::SysState::OptionsMenu);
        return;
    }
    sh::render::feedback_control().present_last_frame = 0;
    if (to == Leaving::SaveScreen) {
        sh::quicksave::open_save_screen();
    } else {
        sh::quicksave::open_load_screen();
    }
}

// Save, Load and Exit: the options screen fades out by itself before it shows, the save and load screens and the
// title would cut in, so the menu fades first. As on the game's own ways to those screens, the menu goes off the
// screen at the confirm and the screen's files load under the fade (which paces itself by the load queue); left
// up, the white text would dim evenly long after the dark world had gone black.
void PauseMenu::start_fade(Request request) {
    cue(gs::sfx::kMenuConfirm);
    if (request != Request::Exit) {
        gs::load_save_screen_files();
    }
    gs::screen_fade_start(true, false, false);
    gs::set_screen_fade_timestep(0);
    leaving_ = request == Request::Save   ? Leaving::SaveScreen
               : request == Request::Load ? Leaving::LoadScreen
                                          : Leaving::Title;
    request_ = Request::None;
}

void PauseMenu::update() {
    register_rows();
    // The controls reference opened from the Controls row owns the frame until it closes.
    if (Pc_ControlsRef_IsOpen() != 0) {
        Pc_ControlsRef_Update();
        return;
    }
    if (Pc_DebugMenu_Pending() != 0) {
        Pc_DebugMenu_TakePending();
    }
    if (Pc_DebugMenu_IsOpen() != 0) {
        Pc_DebugMenu_Update();
        if (Pc_DebugMenu_IsOpen() != 0) {
            return;
        }
        if (Pc_DebugMenu_ResumeOnClose() != 0) {
            resume(0);
            return;
        }
        // Closed back to the pause menu, which draws on this tick.
    }

    if (!open_) {
        open();
    }
    blink_ += gs::delta_time_raw();

    if (leaving_ != Leaving::Nowhere) {
        if (gs::screen_fade_finished()) {
            const Leaving to = leaving_;
            leaving_ = Leaving::Nowhere;
            leave(to);
        }
        return;
    }

    const sh::menu::MenuInput input = read_input(first_frame_);
    first_frame_ = false;

    request_ = Request::None;
    const sh::menu::Event event = controller().update(input);
    if (request_ == Request::None && event == sh::menu::Event::Closed) {
        request_ = Request::Resume;  // Cancel, a right click or a Pause press
    }

    switch (request_) {
        case Request::Resume:
            resume(event == sh::menu::Event::Closed ? gs::sfx::kMenuCancel : gs::sfx::kMenuConfirm);
            return;

        case Request::Debug:
            if (Pc_DebugMenu_OpenById("debug") != 0) {
                cue(gs::sfx::kMenuConfirm);
                Pc_DebugMenu_Draw();
                sh::input::mouse_pointer().show();
                return;
            }
            cue(gs::sfx::kMenuCancel);
            break;

        case Request::Save:
        case Request::Load:
        case Request::Exit:
            start_fade(request_);
            return;

        case Request::Option:
            cue(gs::sfx::kMenuConfirm);
            // The same Options screen as the title's (gamedata/menus/options.lua).
            leave(Leaving::OptionScreen);
            return;

        case Request::None:
            switch (event) {
                case sh::menu::Event::Moved:
                case sh::menu::Event::Changed:
                    cue(gs::sfx::kMenuMove);
                    break;
                case sh::menu::Event::Activated:
                case sh::menu::Event::Entered:
                    cue(gs::sfx::kMenuConfirm);
                    break;
                case sh::menu::Event::Back:
                    cue(gs::sfx::kMenuCancel);
                    break;
                case sh::menu::Event::Refused:
                    cue(gs::sfx::kMenuCancel);
                    // Why a greyed row refused, on the console.
                    if (const char* notice = save_block_notice(block_)) {
                        SH_DBG_ECHO("%s", notice);
                    }
                    break;
                default:
                    break;
            }
            break;
    }

    draw();
    sh::input::mouse_pointer().show();
}

}  // namespace sh::ui
