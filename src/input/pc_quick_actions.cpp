// SPDX-License-Identifier: GPL-3.0-or-later
//
// The PC action binds outside the PS1 pad mapping: keyboard keys and physical controller buttons resolved from the
// control scheme, sampled once per frame (frame-stable edges), and handed to the engine's quick actions.
#include "input/pc_quick_actions.h"

#include <SDL3/SDL.h>

#include "engine/config/pc_config.h"
#include "engine/game_state/game_state.h"
#include "engine/input/frame_edge.h"
#include "engine/items/quick_actions.h"
#include "input/pad.h"
#include "input/camera_style.h"

extern "C" {
extern const unsigned char* g_sdlKeyboardState;
extern int g_DebugThirdPersonCam;
extern int g_PcDebugMenuActive;
int Pc_ConsoleIsOpen(void);
}

int g_PcQuickTurnRequest = 0;
int g_PcRearLookActive = 0;
int g_PcQuickWeaponsEnabled = 0;

namespace {

// Frame identity for the edge caches. Not the vblank count: that is a per-frame delta, constant at a steady frame
// rate, which would freeze a cached edge (stuck reloads or dead binds).
std::int32_t s_inputFrame = 0;

sh::input::FrameEdgeCache& key_edges() {
    static sh::input::FrameEdgeCache cache;
    return cache;
}

sh::input::FrameEdgeCache& pad_edges() {
    static sh::input::FrameEdgeCache cache;
    return cache;
}

// Rising edge of a keyboard scancode, the same for every caller within a frame (a press that opens a screen must
// not count again when that screen first asks).
bool key_clicked(SDL_Scancode key) {
    if (key == SDL_SCANCODE_UNKNOWN || g_sdlKeyboardState == nullptr) {
        return false;
    }
    return key_edges().edge(key, g_sdlKeyboardState[key] != 0, s_inputFrame);
}

bool pad_clicked(int button) {
    if (button < 0) {
        return false;
    }
    return pad_edges().edge(button, sh::input::controller_bind_held(button), s_inputFrame);
}

int pad_button(const char* name) {
    return name[0] != '\0' ? sh::input::lookup_controller_bind(name, SDL_GAMEPAD_BUTTON_INVALID)
                           : SDL_GAMEPAD_BUTTON_INVALID;
}

// The binds of one control scheme (0 classic, 1 alternate cameras), resolved again when the binds change.
struct Binds {
    SDL_Scancode reload = SDL_SCANCODE_UNKNOWN, reload2 = SDL_SCANCODE_UNKNOWN, cycle = SDL_SCANCODE_UNKNOWN,
                 heal = SDL_SCANCODE_UNKNOWN, quick_turn = SDL_SCANCODE_UNKNOWN, flashlight = SDL_SCANCODE_UNKNOWN,
                 rear_look = SDL_SCANCODE_UNKNOWN;
    int pad_reload = -1, pad_cycle = -1, pad_heal = -1, pad_quick_turn = -1, pad_flashlight = -1, pad_rear_look = -1;
};

const Binds& binds(int scheme) {
    static Binds s_binds[2];
    static int s_gen = -1;
    if (s_gen != g_PcBindsGen) {
        s_gen = g_PcBindsGen;
        const ControlScheme* sc[2] = {&g_PcConfig.classic, &g_PcConfig.altcam};
        for (int i = 0; i < 2; ++i) {
            Binds& b = s_binds[i];
            b.reload = SDL_GetScancodeFromName(sc[i]->keyReload);
            b.reload2 = SDL_GetScancodeFromName(sc[i]->keyReload2);
            b.cycle = SDL_GetScancodeFromName(sc[i]->keyCycleWeapons);
            b.heal = SDL_GetScancodeFromName(sc[i]->keyQuickHeal);
            b.quick_turn = SDL_GetScancodeFromName(sc[i]->keyQuickTurn);
            b.flashlight = SDL_GetScancodeFromName(sc[i]->keyFlashlight);
            b.rear_look = SDL_GetScancodeFromName(sc[i]->keyRearLook);
            b.pad_reload = pad_button(sc[i]->padReload);
            b.pad_cycle = pad_button(sc[i]->padCycleWeapons);
            b.pad_heal = pad_button(sc[i]->padQuickHeal);
            b.pad_quick_turn = pad_button(sc[i]->padQuickTurn);
            b.pad_flashlight = pad_button(sc[i]->padFlashlight);
            b.pad_rear_look = pad_button(sc[i]->padRearLook);
        }
    }
    return s_binds[scheme];
}

// The wheel through an event watch: it still reports while the window grabs the mouse.
SDL_AtomicInt s_wheel = {0};

bool SDLCALL wheel_watch(void*, SDL_Event* event) {
    if (event->type != SDL_EVENT_MOUSE_WHEEL) {
        return true;
    }
    int delta = event->wheel.integer_y;
    if (event->wheel.direction == SDL_MOUSEWHEEL_FLIPPED) {
        delta = -delta;
    }
    if (delta != 0) {
        SDL_AddAtomicInt(&s_wheel, delta);
    }
    return true;
}

// Number keys 1..0 and '-' (kQuickWeaponOrder), their own previous state; the last one pressed this frame wins.
int weapon_key_pressed() {
    static const SDL_Scancode kKeys[sh::items::kQuickWeaponCount] = {
        SDL_SCANCODE_1, SDL_SCANCODE_2, SDL_SCANCODE_3, SDL_SCANCODE_4, SDL_SCANCODE_5,    SDL_SCANCODE_6,
        SDL_SCANCODE_7, SDL_SCANCODE_8, SDL_SCANCODE_9, SDL_SCANCODE_0, SDL_SCANCODE_MINUS};
    static bool s_prev[sh::items::kQuickWeaponCount];
    const bool* keyboard = SDL_GetKeyboardState(nullptr);
    int target = -1;
    for (int i = 0; i < sh::items::kQuickWeaponCount; ++i) {
        const bool down = keyboard != nullptr && keyboard[kKeys[i]];
        if (down && !s_prev[i]) {
            target = i;
        }
        s_prev[i] = down;
    }
    return target;
}

bool in_gameplay() {
    namespace gs = sh::game_state;
    return gs::game_state() == gs::GameState::InGame && gs::sys_state() == gs::SysState::Gameplay;
}

}  // namespace

namespace sh::items {

QuickActions& quick_actions() {
    static QuickActions s_actions;
    return s_actions;
}

}  // namespace sh::items

unsigned int PC_RawControllerButtonClicked(int sdlButton) { return pad_clicked(sdlButton) ? 1u : 0u; }

int Pc_ReloadRequestGet(void) { return sh::items::quick_actions().reload_requested() ? 1 : 0; }

void Pc_ReloadRequestSet(int onOff) { sh::items::quick_actions().set_reload_requested(onOff != 0); }

// Off, the reload button is the only way to reload; a touchscreen has none, so it keeps the dry-fire reload.
int Pc_AutoReloadEnabled(void) {
#if defined(__ANDROID__) || defined(SH_IOS) || defined(__EMSCRIPTEN__)
    extern int Pc_Touch_Active(void);
    if (Pc_Touch_Active()) {
        return 1;
    }
#endif
    return g_PcConfig.autoReload != 0;
}

int Pc_EquipOwnedWeapon(int invItemId) {
    return sh::items::quick_actions().equip_owned(invItemId, sh::items::game_quick_actions_host()) ? 1 : 0;
}

void Pc_ExtraActionsUpdate(void) {
    static bool s_watch = false;
    ++s_inputFrame;
    const int scheme = g_DebugThirdPersonCam ? 1 : 0;
    const Binds& b = binds(scheme);

    // A key press skips its pad bind's sample that frame, as it always did. The reload binds are all sampled
    // every frame: a stale previous state there produced phantom reloads on aim entry.
    sh::items::QuickActionPresses p;
    p.cycle_weapons = key_clicked(b.cycle) || pad_clicked(b.pad_cycle);
    p.quick_heal = key_clicked(b.heal) || pad_clicked(b.pad_heal);
    p.quick_turn = key_clicked(b.quick_turn) || pad_clicked(b.pad_quick_turn);
    p.flashlight = key_clicked(b.flashlight) || pad_clicked(b.pad_flashlight);
    const bool reload_key = key_clicked(b.reload), reload_key2 = key_clicked(b.reload2),
               reload_pad = pad_clicked(b.pad_reload);
    p.reload = reload_key || reload_key2 || reload_pad;

    if (!s_watch) {
        SDL_AddEventWatch(wheel_watch, nullptr);
        s_watch = true;
    }
    // Taken even while a menu is open, so a turn of the wheel there does not switch later.
    p.wheel = SDL_SetAtomicInt(&s_wheel, 0);
    p.weapon_key = weapon_key_pressed();

    sh::items::quick_actions().frame(p, sh::items::game_quick_actions_host());
}

void Pc_RearLookUpdate(void) {
    const int scheme = g_DebugThirdPersonCam ? 1 : 0;
    const Binds& b = binds(scheme);
    const bool* keys = SDL_GetKeyboardState(nullptr);
    const bool held = (keys != nullptr && b.rear_look != SDL_SCANCODE_UNKNOWN && keys[b.rear_look]) ||
                      (b.pad_rear_look >= 0 && sh::input::controller_button_held(b.pad_rear_look));
    // TPS/OTS only (not first person, not classic), gameplay only; cleared otherwise so it never latches.
    g_PcRearLookActive = (held && g_DebugThirdPersonCam && !sh::input::camera_styles().first_person() && in_gameplay() && !Pc_ConsoleIsOpen() &&
                          !g_PcDebugMenuActive)
                             ? 1
                             : 0;
}
