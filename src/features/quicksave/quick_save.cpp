// SPDX-License-Identifier: GPL-3.0-or-later
#include "features/quicksave/quick_save.h"

#include <cstdint>

#include <SDL3/SDL.h>

#include "engine/chara/chara_library.h"
#include "engine/chara/runtime/chara_data.h"
#include "engine/config/pc_config.h"
#include "engine/entities/entity_world.h"
#include "engine/game_state/game_state.h"
#include "engine/game_state/session.h"
#include "engine/gameflow/game/game_link.h"
#include "engine/log/sh_log.h"
#include "engine/player/player.h"
#include "engine/runtime/game_hooks.h"
#include "engine/save/game_saves.h"
#include "i18n/localization.h"
#include "input/pad.h"
#include "input/pc_quick_actions.h"
#include "ui/screens/save_screen_game.h"

extern "C" int Pc_ConsoleIsOpen(void);

namespace sh::quicksave {

namespace gs = sh::game_state;

Request PressDetector::update(const Input& input) noexcept {
    const bool save = (input.save_key_down && !save_was_down_) || input.pad_save_pressed;
    const bool load = (input.load_key_down && !load_was_down_) || input.pad_load_pressed;
    save_was_down_ = input.save_key_down;
    load_was_down_ = input.load_key_down;
    if (input.ctrl_down) {
        return Request::None;
    }
    return save ? Request::Save : load ? Request::Load : Request::None;
}

namespace {

// The one slot the keys use (.savegames/quick/quick_01.sav).
constexpr int kSlot = 0;

void tell_player(const char* text_key) { SH_DBG_ECHO("%s", sh::i18n::ui_text(text_key)); }

// The player walks around: the game runs, no menu, cutscene or map event has the controls.
bool in_gameplay() {
    return gs::game_state() == gs::GameState::InGame && gs::sys_state() == gs::SysState::Gameplay;
}

bool console_in_the_way(bool from_console) { return !from_console && Pc_ConsoleIsOpen() != 0; }

// Gameplay that a save can be taken from: also not the demo, not between two rooms, not during a fade.
bool in_free_play(bool from_console) {
    if (!in_gameplay() || console_in_the_way(from_console) || gs::demo_active()) {
        return false;
    }
    const std::uint32_t between_rooms =
        sh::game::process_flag::kRoomTransition | sh::game::process_flag::kOverlayTransition;
    return (sh::game::session().system.process.process_flags & between_rooms) == 0 &&
           sh::gameflow::game::screen_fade_none();
}

void log_states(const char* refused) {
    SH_LOG_INFO(SAVE, "%s (game state %d, sys state %d)", refused, static_cast<int>(gs::game_state()),
                static_cast<int>(gs::sys_state()));
}

// Where and how the player is, into the progress record. The save location stays the last notebook's: a quick
// save is no save point, and a Next Fear location written here would start the next playthrough.
void record_player() {
    using sh::game::PlayerVar;
    const sh::game::Player& player = sh::game::player();
    sh::gameflow::game::SavedPlayer saved = sh::gameflow::game::saved_player();
    saved.x = static_cast<std::int32_t>(player.get(PlayerVar::PositionX));
    saved.z = static_cast<std::int32_t>(player.get(PlayerVar::PositionZ));
    saved.rotation_y = static_cast<std::int16_t>(player.get(PlayerVar::RotationY));
    saved.health = static_cast<std::int32_t>(player.get(PlayerVar::Health));
    sh::gameflow::game::store_saved_player(saved);
}

bool write_slot(bool from_console) {
    if (!in_free_play(from_console) || sh::game::player().get(sh::game::PlayerVar::Health) <= 0) {
        log_states("quick save refused: not in free gameplay");
        tell_player("ui.save.quick_not_now");
        return false;
    }
    if (boss_alive()) {
        SH_LOG_INFO(SAVE, "quick save refused: a boss is alive");
        tell_player("ui.save.quick_boss");
        return false;
    }
    if (!sh::hooks::game_quick_save(true)) {
        SH_LOG_INFO(SAVE, "quick save cancelled by a game.quick_save handler");
        return false;
    }
    record_player();
    if (sh::save::game_saves().quick_save(kSlot) <= 0) {
        tell_player("ui.save.save_failed");
        return false;
    }
    tell_player("ui.save.quick_saved");
    return true;
}

bool read_slot(bool from_console) {
    if (!in_gameplay() || console_in_the_way(from_console) || gs::demo_active()) {
        log_states("quick load refused: not in gameplay");
        return false;
    }
    const int state = sh::save::GameSaves::quick_state(kSlot);
    if (state == -2) {
        // the store's journal line names the two versions
        tell_player("ui.save.newer_refused");
        return false;
    }
    if (state <= 0) {
        const bool none = state == 0;
        SH_LOG_INFO(SAVE, "quick load: %s", none ? "no quick save" : "the quick save is damaged");
        tell_player(none ? "ui.save.quick_none" : "ui.save.load_failed");
        return false;
    }
    if (!sh::hooks::game_quick_load(true)) {
        SH_LOG_INFO(SAVE, "quick load cancelled by a game.quick_load handler");
        return false;
    }
    SH_LOG_INFO(SAVE, "quick load: loading quick save %d", kSlot + 1);
    PcSaveGame_OpenQuickLoad(kSlot);
    return true;
}

// The save key without quick_save_instant: the save screen, where the settings allow it.
void save_through_screen() {
    if (!g_PcConfig.saveAnywhere) {
        // Names the setting: a key that only refuses leaves the player looking for the switch.
        tell_player("ui.save.quick_off");
    } else if (boss_alive()) {
        tell_player("ui.save.quick_boss");
    } else if (sh::hooks::game_quick_save()) {
        open_save_screen();
    }
}

// The keys and buttons of the config, looked up again whenever the bindings change.
struct Binds {
    SDL_Scancode save_key = SDL_SCANCODE_UNKNOWN;
    SDL_Scancode load_key = SDL_SCANCODE_UNKNOWN;
    int save_button = -1;
    int load_button = -1;
    int generation = -1;

    void refresh() {
        if (generation == g_PcBindsGen) {
            return;
        }
        generation = g_PcBindsGen;
        save_key = SDL_GetScancodeFromName(g_PcConfig.keyQuickSave);
        load_key = SDL_GetScancodeFromName(g_PcConfig.keyQuickLoad);
        save_button = button(g_PcConfig.padQuickSave);
        load_button = button(g_PcConfig.padQuickLoad);
    }

    static int button(const char* name) { return name[0] != '\0' ? sh::input::lookup_controller_bind(name, -1) : -1; }
};

}  // namespace

bool boss_alive() {
    sh::entities::EntityWorld& world = gs::characters();
    for (int slot = 1; slot <= gs::kNpcSlots; ++slot) {
        const sh::entities::Character* npc = world.character(slot);
        if (npc == nullptr || npc->health() <= 0) {
            continue;
        }
        const sh::chara::CharacterDef* type = sh::chara::type_def(npc->chara_id());
        if (type != nullptr && type->is_boss) {
            return true;
        }
    }
    return false;
}

void open_save_screen() {
    SH_LOG_INFO(SAVE, "quick save: opening the save screen");
    gs::open_save_screen();
}

void open_load_screen() {
    SH_LOG_INFO(SAVE, "quick load: opening the load screen");
    gs::open_load_screen();
}

bool save_now(bool from_console) {
    if (!g_PcConfig.quickSaveInstant) {
        SH_LOG_INFO(SAVE, "quick save: quick_save_instant is off");
        return false;
    }
    return write_slot(from_console);
}

bool load_now(bool from_console) {
    if (!g_PcConfig.quickSaveInstant) {
        SH_LOG_INFO(SAVE, "quick load: quick_save_instant is off");
        return false;
    }
    return read_slot(from_console);
}

void poll_keys() {
    static Binds binds;
    static PressDetector presses;

    binds.refresh();
    const bool* const keys = SDL_GetKeyboardState(nullptr);
    if (keys == nullptr) {
        return;
    }
    PressDetector::Input input;
    input.save_key_down = binds.save_key != SDL_SCANCODE_UNKNOWN && keys[binds.save_key];
    input.load_key_down = binds.load_key != SDL_SCANCODE_UNKNOWN && keys[binds.load_key];
    input.pad_save_pressed = binds.save_button >= 0 && PC_RawControllerButtonClicked(binds.save_button);
    input.pad_load_pressed = binds.load_button >= 0 && PC_RawControllerButtonClicked(binds.load_button);
    input.ctrl_down = (SDL_GetModState() & SDL_KMOD_CTRL) != 0;

    const Request request = presses.update(input);
    if (request == Request::None) {
        return;
    }
    if (g_PcConfig.quickSaveInstant) {
        request == Request::Save ? write_slot(false) : read_slot(false);
        return;
    }
    if (!in_gameplay() || Pc_ConsoleIsOpen()) {
        return;
    }
    if (request == Request::Save) {
        save_through_screen();
    } else if (sh::hooks::game_quick_load()) {
        open_load_screen();
    }
}

}  // namespace sh::quicksave
