#include "engine/gameflow/menu_states.h"

namespace sh::gameflow {

namespace {

void next_step(MenuStateHost& host) { host.store_step(0, host.step(0) + 1); }

}  // namespace

void MenuStates::run(GameState s, MenuStateHost& host) {
    switch (s) {
    case GameState::Paused:
        paused(host);
        break;
    case GameState::OptionsMenu:
        options(host);
        break;
    case GameState::StatusMenu:
        status(host);
        break;
    case GameState::MapScreen:
        map_screen(host);
        break;
    case GameState::Movie:
        movie(host);
        break;
    case GameState::SaveMenu:
    case GameState::SaveMenuAlt:
        save(host);
        break;
    default:
        break;
    }
}

void MenuStates::paused(MenuStateHost& host) {
    host.hold_world();
    if (!host.native_pause_menu()) {
        // "PAUSED" is shown for half of every 1 s (2048 in Q12) and the clear-time clock keeps running.
        blink_ += host.raw_delta();
        if (((blink_ >> 11) & 1) == 0) {
            host.draw_paused();
        }
        host.paused_extras();
    }
    if (host.step(0) == 0) {
        host.sound_command(rules_.pause_on_sound);
        next_step(host);
    }
    // The original's debug way from the pause to the save screen.
    if (host.debug_save_combo()) {
        blink_ = 0;
        host.sound_command(rules_.pause_off_sound);
        host.clear_event_param();
        host.release_world();
        host.reset_pause_menu();
        host.enter_state(GameState::SaveMenu);
        return;
    }
    if (host.native_pause_menu()) {
        host.update_pause_menu();
        return;
    }
    if (host.pause_pressed()) {
        blink_ = 0;
        host.sound_command(rules_.pause_off_sound);
        host.release_world();
        host.enter_state(GameState::Gameplay);
    }
}

void MenuStates::options(MenuStateHost& host) {
    // Opened from the pause menu: the world stays frozen behind the fade, as in the pause.
    if (host.pause_menu_return_pending()) {
        host.hold_world();
    }
    if (host.step(0) == 0) {
        host.fade_out(true);
        host.fade_instant();
        host.store_step(0, 1);
    }
    if (host.step(0) == 1 && host.chunks_idle()) {
        host.sound_command(rules_.stop_stream_sound);
        host.load_options_screen();
        next_step(host);
    }
    if (host.screen_ready()) {
        host.open_screen(MenuScreen::Options);
    }
}

void MenuStates::status(MenuStateHost& host) {
    // One frame black before the hand-over, so the world is not drawn once more with nothing over it.
    if (host.step(0) == 0) {
        host.fade_out(true);
        host.fade_instant();
        next_step(host);
        return;
    }
    host.open_screen(MenuScreen::Inventory);
}

void MenuStates::map_screen(MenuStateHost& host) {
    const MapBlock block = host.map_block();
    if (block != MapBlock::None) {
        // The world stays frozen under the message, so no trigger fires behind it.
        host.hold_world();
        const int message = block == MapBlock::NoMap ? rules_.no_map_message : rules_.too_dark_message;
        if (host.map_pressed() || host.show_message(message) > MessageResult::Showing) {
            host.release_world();
            host.enter_state(GameState::Gameplay);
        }
        return;
    }
    if (host.step(0) == 0) {
        host.load_paper_map();
        host.fade_out(true);
        host.fade_instant();
        next_step(host);
    }
    if (host.screen_ready()) {
        host.open_screen(MenuScreen::PaperMap);
    }
}

void MenuStates::movie(MenuStateHost& host) {
    if (host.step(0) == 0) {
        host.fade_out(false);
        host.clear_screen_ready();
        host.store_step(0, 1);
    }
    if (host.step(0) == 1 && host.chunks_idle()) {
        host.load_movie_stream();
        next_step(host);
    }
    if (!host.screen_ready()) {
        return;
    }
    const RunningEvent ev = host.running_event();
    host.play_movie(ev.param);
    host.set_flag(ev.done_flag);
    host.open_screen(MenuScreen::InGame);
    if (ev.options & rules_.movie_resumes_game) {
        host.resume_game_at_step(1);
    }
}

void MenuStates::save(MenuStateHost& host) {
    host.poll_saves();
    switch (host.step(0)) {
    case 0: {
        host.store_player();
        const RunningEvent ev = host.running_event();
        bool go = host.flag(rules_.save_seen_flag) || host.save_location() == rules_.save_without_prompt ||
                  ev.param == 0;
        if (!go && host.show_message(rules_.save_prompt_message) == MessageResult::Choice0) {
            host.set_flag(rules_.save_seen_flag);
            go = true;
        }
        if (go) {
            host.load_save_screen();
            host.fade_out(true);
            steps::advance(host, 0);
        }
        break;
    }
    case 1:
        if (host.screen_ready()) {
            host.fade_in();
            host.stop_rumble();
            host.open_screen(MenuScreen::Save);
        }
        break;
    default:
        break;
    }
}

}  // namespace sh::gameflow
