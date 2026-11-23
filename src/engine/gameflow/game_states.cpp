#include "engine/gameflow/game_states.h"

namespace sh::gameflow {

GameplayDecision decide_gameplay(const GameplayInput& in) {
    GameplayDecision out;
    if (!in.alive) {
        return out;
    }
    out.toggle_light = in.light_pressed && in.light_available;

    if (in.map_event != GameState::None) {
        out.next = in.map_event;
    } else if (in.pause_pressed) {
        out.next = GameState::Paused;
    } else if (in.attacking) {
        // The swing finishes first; the menu flag is not touched on these frames.
        return out;
    } else if (in.status_pressed) {
        out.next = GameState::StatusMenu;
    } else if (in.map_pressed) {
        out.next = GameState::MapScreen;
        out.clear_message = true;
    } else if (in.options_pressed) {
        out.next = GameState::OptionsMenu;
    }

    const GameState after = out.next != GameState::None ? out.next : in.state;
    if (after == GameState::OptionsMenu || after == GameState::StatusMenu || after == GameState::MapScreen) {
        out.menu = MenuFlag::Set;
    } else if (in.fade_idle) {
        out.menu = MenuFlag::Clear;
    }
    return out;
}

void StateMachine::frame(StateHost& host) {
    if (host.state() != GameState::Gameplay && !host.player_alive()) {
        host.enter_state(GameState::Gameplay);
    }

    // States that resume the world use the last running delta: while time is stopped (delta 0) the
    // unscaled one stands in.
    const std::int32_t delta = host.delta();
    saved_delta_ = delta != 0 ? delta : host.raw_delta();

    if (host.state() == GameState::Gameplay) {
        host.clear_message_set();
        host.run_gameplay();
        return;
    }

    const GameState s = host.state();
    if (s == GameState::ReadMessage) {
        // A message stops the world. An examined object (button event) keeps it stopped for the
        // whole message, as the original's frozen frame did; other messages let ReadMessage decide.
        host.set_delta(0);
        const RunningEvent ev = host.running_event();
        if (ev.present && ev.button) {
            saved_delta_ = 0;
        }
    } else {
        // Event states run on the unscaled clock so their timers advance even when the world's
        // delta is 0.
        host.set_delta(host.raw_delta());
    }

    if (static_cast<int>(s) >= rules_.state_count) {
        host.store_state(GameState::Gameplay);
    } else {
        run(s, host);
    }

    if (host.state() == GameState::Gameplay) {
        const GameState next = host.scan_events();
        if (next != GameState::None) {
            host.before_event_state(next);
            host.enter_state(next);
        }
    }
}

void StateMachine::run(GameState s, StateHost& host) {
    switch (s) {
    case GameState::Gameplay:
        host.run_gameplay();
        break;
    case GameState::EventCallback:
        event_callback(host);
        break;
    case GameState::EventSetFlag:
        event_set_flag(host);
        break;
    case GameState::EventPlaySound:
        event_play_sound(host);
        break;
    case GameState::ReadMessage:
        read_message(host);
        break;
    case GameState::Paused:
    case GameState::OptionsMenu:
    case GameState::StatusMenu:
    case GameState::MapScreen:
    case GameState::Movie:
    case GameState::SaveMenu:
    case GameState::SaveMenuAlt:
        if (!host.run_menu_state(s)) {
            host.run_state(s);
        }
        break;
    default:
        host.run_state(s);
        break;
    }
}

void StateMachine::event_set_flag(StateHost& host) {
    host.set_delta(saved_delta_);
    host.set_flag(host.running_event().done_flag);
    host.store_state(GameState::Gameplay);
}

void StateMachine::event_play_sound(StateHost& host) {
    host.set_delta(saved_delta_);
    const RunningEvent ev = host.running_event();
    // The parameter is an unsigned 16-bit offset and the sound number wraps at 16 bits.
    host.play_sound((static_cast<std::int32_t>(static_cast<std::uint16_t>(ev.param)) + rules_.sound_base) & 0xFFFF);
    host.set_flag(ev.done_flag);
    host.store_state(GameState::Gameplay);
}

void StateMachine::event_callback(StateHost& host) {
    const RunningEvent ev = host.running_event();
    if (!ev.present || !host.event_script_allowed(ev.param)) {
        host.store_state(GameState::Gameplay);
        return;
    }
    if (ev.options != 0) {
        host.set_flag(ev.done_flag);
    }
    host.set_delta(saved_delta_);
    if (!host.run_event_script(ev.param)) {
        host.store_state(GameState::Gameplay);
    }
}

void StateMachine::read_message(StateHost& host) {
    const RunningEvent ev = host.running_event();
    // The world stays stopped under a message while an enemy lives, unless the event or the scene
    // says otherwise.
    if (!(ev.options & kEventKeepsWorldRunning) && !host.scene_keeps_world_running()) {
        if (!host.enemy_alive()) {
            host.set_delta(saved_delta_);
        }
    } else {
        host.set_delta(saved_delta_);
    }

    if (!host.message_set()) {
        host.freeze_player();
    }

    if (host.show_message(ev.param) == MessageResult::Choice0) {
        host.set_flag(ev.done_flag);
        host.enter_state(GameState::Gameplay);
        host.unfreeze_player();
    }
}

void StateMachine::event_screen_frame(StateHost& host) {
    if (host.event_screen_step() == 0) {
        host.start_event_screen();
    }
    host.update_ready();
    const RunningEvent ev = host.running_event();
    host.set_flag(ev.done_flag);
    host.run_event_script(ev.param);
    host.draw_event_screen();
}

}  // namespace sh::gameflow
