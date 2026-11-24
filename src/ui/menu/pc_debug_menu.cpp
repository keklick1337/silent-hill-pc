// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/menu/pc_debug_menu.h"
#include "ui/menu/pc_spawn_turntable.h"
#include "ui/screens/pc_controls_ref.h"

#include <SDL3/SDL.h>

#include <string>

#include "engine/audio/sound_api.h"
#include "engine/game_state/game_state.h"
#include "engine/log/sh_log.h"
#include "engine/menu/menu_model.h"
#include "input/mouse_pointer.h"
#include "ui/menu/menu_view.h"
#include "ui/menu/nm_draw.h"
#include "ui/pause/pause_menu.h"

namespace gs = sh::game_state;
namespace nm = sh::ui::nm;

extern const unsigned char* g_sdlKeyboardState;

// Registered by other modules (cheats / options); absent in a build that does not have them.
extern "C" void Pc_DebugBinds_Register(void) __attribute__((weak));
extern "C" void Pc_OptionsBinds_Register(void) __attribute__((weak));

int g_PcDebugMenuActive = 0;

namespace {

sh::menu::Controller& controller() {
    static sh::menu::Controller c(sh::menu::registry());
    return c;
}

// The Options screen's look over the frozen game (or the title), under a darkening veil.
sh::menu::MenuView& view() {
    static sh::menu::MenuView v = [] {
        sh::menu::MenuView m(sh::menu::nm_canvas());
        m.set_style(sh::menu::Style::Options);
        m.set_veil(true);
        return m;
    }();
    return v;
}

bool s_initialised = false;
bool s_opening = false;       // the first update after open ignores pointer motion and clicks
bool s_close_request = false;  // F10 pressed while open
bool s_resume_on_close = false;
std::string s_pending_id;
bool s_prev_pgup = false;
bool s_prev_pgdn = false;

// The pause menu's cues key on outside the pause's master mute so they still sound.
void play(int sfx) {
    ShSound_SetMainExempt(1);
    gs::sound_command(sfx);
    ShSound_SetMainExempt(0);
}

void sound_for(sh::menu::Event e) {
    using sh::menu::Event;
    switch (e) {
    case Event::Moved:
    case Event::Changed: play(gs::sfx::kMenuMove); break;
    case Event::Activated:
    case Event::Entered: play(gs::sfx::kMenuConfirm); break;
    case Event::Back:
    case Event::Closed:
    case Event::Refused: play(gs::sfx::kMenuCancel); break;
    case Event::None: break;
    }
}

bool open_now(const std::string& id) {
    sh::menu::Controller& c = controller();
    if (!c.open(id)) {
        SH_WARN("[MENU] cannot open menu \"%s\"", id.c_str());
        return false;
    }
    view().fit(c);
    s_opening = true;
    s_close_request = false;
    g_PcDebugMenuActive = 1;
    return true;
}

sh::menu::MenuInput build_input() {
    sh::menu::MenuInput mi;
    const nm::Input in = nm::read_input();
    mi.up = in.up != 0;
    mi.down = in.down != 0;
    mi.left = in.left != 0;
    mi.right = in.right != 0;
    mi.mirrored = in.mirrored;
    mi.confirm = in.confirm != 0;
    mi.cancel = in.cancel != 0;
    mi.wheel = in.wheel;
    mi.pointer_moved = in.pointer_moved != 0;
    mi.click = in.click != 0;
    mi.right_click = in.right_click != 0;
    view().pointer_input(controller(), in.pointer, in.pointer_x, in.pointer_y, mi);

    // A Pause bind that is not also a confirm button closes the menu like Cancel (as in the pause menu).
    std::uint16_t binds[gs::kPadBindCount] = {};
    std::uint32_t clicked = 0;
    gs::pad_binds(binds);
    gs::pad_clicked(&clicked);
    if (in.pointer) clicked &= ~sh::input::mouse_pointer().bound_pad_bits();
    if (clicked & binds[gs::kPadPause] & ~binds[gs::kPadEnter]) mi.cancel = true;

    // Pages: PgUp / PgDn, and the shoulder buttons (the sidestep binds: Q / E on the keyboard, L1 / R1).
    const bool pgup = g_sdlKeyboardState != nullptr && g_sdlKeyboardState[SDL_SCANCODE_PAGEUP] != 0;
    const bool pgdn = g_sdlKeyboardState != nullptr && g_sdlKeyboardState[SDL_SCANCODE_PAGEDOWN] != 0;
    mi.page_up = (pgup && !s_prev_pgup) || (clicked & binds[gs::kPadStepLeft]) != 0;
    mi.page_down = (pgdn && !s_prev_pgdn) || (clicked & binds[gs::kPadStepRight]) != 0;
    s_prev_pgup = pgup;
    s_prev_pgdn = pgdn;
    return mi;
}

}  // namespace

extern "C" void Pc_DebugMenu_Init(void) {
    if (s_initialised) return;
    s_initialised = true;
    sh::menu::HostHooks& h = sh::menu::host();
    h.open = [](std::string_view id) { return Pc_DebugMenu_OpenById(std::string(id).c_str()) != 0; };
    h.close = [] { Pc_DebugMenu_Close(); };
    h.is_open = [] { return Pc_DebugMenu_IsOpen() != 0; };
    if (Pc_DebugBinds_Register) Pc_DebugBinds_Register();
    if (Pc_OptionsBinds_Register) Pc_OptionsBinds_Register();
}

extern "C" int Pc_DebugMenu_IsOpen(void) { return controller().is_open() ? 1 : 0; }

extern "C" void Pc_DebugMenu_Close(void) {
    controller().close();
    Pc_SpawnTurntable_Hide();
    Pc_SpawnTurntable_SetPage(0);
    g_PcDebugMenuActive = 0;
    s_pending_id.clear();
    if (gs::game_state() == gs::GameState::MainMenu) nm::keep_text_unfiltered(false);
}

extern "C" int Pc_DebugMenu_OpenById(const char* id) {
    if (id == nullptr || *id == '\0') return 0;
    Pc_DebugMenu_Init();
    if (sh::menu::registry().find(id) == nullptr) {
        SH_WARN("[MENU] no menu \"%s\" (gamedata/menus)", id);
        return 0;
    }
    if (Pc_DebugMenu_IsOpen()) {
        // A menu open over the pause or the title: the new one replaces it.
        return open_now(id) ? 1 : 0;
    }
    if (gs::demo_active()) return 0;
    const gs::GameState state = gs::game_state();
    if (state == gs::GameState::MainMenu) {
        s_resume_on_close = false;
        return open_now(id) ? 1 : 0;
    }
    if (state != gs::GameState::InGame) return 0;
    if (gs::sys_state() == gs::SysState::GamePaused) {
        if (!sh::ui::pause_menu().is_shown()) return 0;
        s_resume_on_close = false;  // opened from the pause menu: closing goes back to it
        return open_now(id) ? 1 : 0;
    }
    if (gs::sys_state() != gs::SysState::Gameplay) return 0;
    // Over the running game: through the pause state, which freezes and captures the world.
    s_pending_id = id;
    sh::ui::pause_menu().enter_for_debug();
    return 1;
}

extern "C" void Pc_DebugMenu_Request(void) {
    Pc_DebugMenu_Init();
    if (Pc_DebugMenu_IsOpen()) {
        s_close_request = true;
        return;
    }
    if (!s_pending_id.empty()) return;
    Pc_DebugMenu_OpenById("debug");
}

extern "C" int Pc_DebugMenu_Pending(void) { return s_pending_id.empty() ? 0 : 1; }

extern "C" int Pc_DebugMenu_TakePending(void) {
    if (s_pending_id.empty()) return 0;
    const std::string id = s_pending_id;
    s_pending_id.clear();
    s_resume_on_close = true;
    if (!open_now(id)) {
        s_resume_on_close = false;
        return 0;
    }
    return 1;
}

extern "C" void Pc_DebugMenu_ClearPending(void) { s_pending_id.clear(); }

extern "C" int Pc_DebugMenu_ResumeOnClose(void) { return s_resume_on_close ? 1 : 0; }

extern "C" void Pc_DebugMenu_Draw(void) {
    if (!controller().is_open()) return;
    view().draw(controller());
    const sh::menu::MenuDef* page = controller().current();
    Pc_SpawnTurntable_SetPage(page != nullptr && page->id == "debug.spawn" ? 1 : 0);
    Pc_SpawnTurntable_Draw();
}

extern "C" void Pc_DebugMenu_Update(void) {
    sh::menu::Controller& c = controller();
    if (!c.is_open()) {
        g_PcDebugMenuActive = 0;
        return;
    }
    // The controls reference opened from a row owns the frame until it closes.
    if (Pc_ControlsRef_IsOpen()) {
        Pc_ControlsRef_Update();
        return;
    }
    view().fit(c);
    sh::menu::MenuInput mi = build_input();
    if (s_opening) {
        // That tick's motion is the mouse-look release, not a hover.
        s_opening = false;
        mi.pointer_moved = false;
        mi.click = false;
    }
    sh::menu::Event ev;
    if (s_close_request) {
        s_close_request = false;
        c.close();
        ev = sh::menu::Event::Closed;
    } else {
        ev = c.update(mi);
    }
    sound_for(ev);
    if (!c.is_open()) {
        g_PcDebugMenuActive = 0;
        if (gs::game_state() == gs::GameState::MainMenu) nm::keep_text_unfiltered(false);
        return;
    }
    Pc_DebugMenu_Draw();
    sh::input::mouse_pointer().show();
}
