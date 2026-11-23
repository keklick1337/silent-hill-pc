// The in-game state machine against its specification (the original's state code): the gameplay
// decision's priorities and menu flag, the frame dispatch and the clock it gives each state, and the
// engine-run event states, checked as call sequences on a recording host.

#include <string>
#include <vector>

#include "engine/gameflow/game_states.h"

#include "framework/sh_test.h"

namespace gf = sh::gameflow;

namespace {

class FakeHost final : public gf::StateHost {
public:
    gf::GameState cur = gf::GameState::Gameplay;
    bool alive = true;
    std::int32_t d = 0, raw = 0;
    bool msg = false;
    gf::RunningEvent ev{};
    gf::GameState scan_result = gf::GameState::None;
    gf::GameState state_after_handler = gf::GameState::None;  // None = handler keeps the state
    bool has_script = true;
    bool keeps_running = false, enemy = false;
    gf::MessageResult message = gf::MessageResult::Showing;
    int screen_step = 0;
    std::vector<std::string> log;

    gf::GameState state() const override { return cur; }
    void enter_state(gf::GameState s) override { cur = s; log.push_back("enter " + std::to_string(int(s))); }
    void store_state(gf::GameState s) override { cur = s; log.push_back("store " + std::to_string(int(s))); }
    bool player_alive() const override { return alive; }
    std::int32_t delta() const override { return d; }
    std::int32_t raw_delta() const override { return raw; }
    void set_delta(std::int32_t v) override { d = v; log.push_back("delta " + std::to_string(v)); }
    bool message_set() const override { return msg; }
    void clear_message_set() override { msg = false; log.push_back("clear_msg"); }
    void run_gameplay() override { log.push_back("gameplay"); }
    void run_state(gf::GameState s) override {
        log.push_back("run " + std::to_string(int(s)));
        if (state_after_handler != gf::GameState::None) {
            cur = state_after_handler;
        }
    }
    gf::RunningEvent running_event() const override { return ev; }
    gf::GameState scan_events() override { log.push_back("scan"); return scan_result; }
    void before_event_state(gf::GameState s) override { log.push_back("before " + std::to_string(int(s))); }
    void set_flag(std::int16_t f) override { log.push_back("flag " + std::to_string(f)); }
    void play_sound(std::int32_t s) override { log.push_back("sound " + std::to_string(s)); }
    bool run_event_script(int p) override { log.push_back("script " + std::to_string(p)); return has_script; }
    bool refuse_script = false;
    bool event_script_allowed(int p) override {
        log.push_back("allowed? " + std::to_string(p));
        return !refuse_script;
    }
    bool scene_keeps_world_running() const override { return keeps_running; }
    bool enemy_alive() const override { return enemy; }
    void freeze_player() override { log.push_back("freeze"); }
    void unfreeze_player() override { log.push_back("unfreeze"); }
    gf::MessageResult show_message(int m) override { log.push_back("message " + std::to_string(m)); return message; }
    int event_screen_step() const override { return screen_step; }
    void start_event_screen() override { log.push_back("start_screen"); }
    void update_ready() override { log.push_back("ready"); }
    void draw_event_screen() override { log.push_back("draw_screen"); }
};

gf::StateMachine make() {
    gf::StateMachine m;
    gf::StateRules r;
    r.sound_base = 1280;
    m.set_rules(r);
    return m;
}

bool same(const std::vector<std::string>& a, const std::vector<std::string>& b) { return a == b; }

}  // namespace

SH_TEST(gameplay_decision_priorities) {
    gf::GameplayInput in;
    in.map_event = gf::GameState::LoadRoom;
    in.pause_pressed = in.status_pressed = in.map_pressed = in.options_pressed = true;
    in.fade_idle = true;
    gf::GameplayDecision d = gf::decide_gameplay(in);
    SH_CHECK(d.next == gf::GameState::LoadRoom);
    SH_CHECK(d.menu == gf::MenuFlag::Clear);

    in.map_event = gf::GameState::None;
    SH_CHECK(gf::decide_gameplay(in).next == gf::GameState::Paused);

    // An attack in progress blocks the menus and leaves the menu flag alone.
    in.pause_pressed = false;
    in.attacking = true;
    d = gf::decide_gameplay(in);
    SH_CHECK(d.next == gf::GameState::None);
    SH_CHECK(d.menu == gf::MenuFlag::Keep);

    in.attacking = false;
    d = gf::decide_gameplay(in);
    SH_CHECK(d.next == gf::GameState::StatusMenu);
    SH_CHECK(d.menu == gf::MenuFlag::Set);

    in.status_pressed = false;
    d = gf::decide_gameplay(in);
    SH_CHECK(d.next == gf::GameState::MapScreen);
    SH_CHECK(d.clear_message);

    in.map_pressed = false;
    SH_CHECK(gf::decide_gameplay(in).next == gf::GameState::OptionsMenu);

    // Nothing pressed: the flag clears only once no fade runs.
    in.options_pressed = false;
    in.fade_idle = false;
    SH_CHECK(gf::decide_gameplay(in).menu == gf::MenuFlag::Keep);
    // A state another path already chose counts as the state after the decision.
    in.state = gf::GameState::MapScreen;
    SH_CHECK(gf::decide_gameplay(in).menu == gf::MenuFlag::Set);
}

SH_TEST(gameplay_decision_light_and_death) {
    gf::GameplayInput in;
    in.light_pressed = true;
    SH_CHECK(!gf::decide_gameplay(in).toggle_light);
    in.light_available = true;
    SH_CHECK(gf::decide_gameplay(in).toggle_light);
    in.alive = false;
    in.pause_pressed = true;
    gf::GameplayDecision d = gf::decide_gameplay(in);
    SH_CHECK(!d.toggle_light && d.next == gf::GameState::None && d.menu == gf::MenuFlag::Keep);
}

SH_TEST(state_frame_gameplay_and_saved_delta) {
    gf::StateMachine m = make();
    FakeHost h;
    h.d = 0;
    h.raw = 136;
    h.msg = true;
    m.frame(h);
    SH_CHECK(same(h.log, {"clear_msg", "gameplay"}));
    SH_CHECK(m.saved_delta() == 136);  // stopped world: the unscaled delta stands in
    h.d = 68;
    m.frame(h);
    SH_CHECK(m.saved_delta() == 68);
}

SH_TEST(state_frame_dead_player_returns_to_gameplay) {
    gf::StateMachine m = make();
    FakeHost h;
    h.cur = gf::GameState::Paused;
    h.alive = false;
    m.frame(h);
    SH_CHECK(same(h.log, {"enter 0", "clear_msg", "gameplay"}));
}

SH_TEST(state_frame_game_state_then_scan) {
    gf::StateMachine m = make();
    FakeHost h;
    h.cur = gf::GameState::Movie;
    h.raw = 136;
    h.d = 0;
    h.state_after_handler = gf::GameState::Gameplay;
    h.scan_result = gf::GameState::LoadRoom;
    m.frame(h);
    SH_CHECK(same(h.log, {"delta 136", "run 4", "scan", "before 6", "enter 6"}));

    // A state that stays: no scan.
    h.log.clear();
    h.cur = gf::GameState::GameOver;
    h.state_after_handler = gf::GameState::None;
    m.frame(h);
    SH_CHECK(same(h.log, {"delta 136", "run 13"}));

    // Unknown states fall back to gameplay (plain store) and the scan runs.
    h.log.clear();
    h.cur = gf::GameState::None;
    h.scan_result = gf::GameState::None;
    m.frame(h);
    SH_CHECK(same(h.log, {"delta 136", "store 0", "scan"}));
}

SH_TEST(state_frame_message_stops_clock) {
    gf::StateMachine m = make();
    FakeHost h;
    h.cur = gf::GameState::ReadMessage;
    h.d = 136;
    h.raw = 136;
    h.ev.present = true;
    h.ev.button = true;
    h.ev.param = 9;
    h.ev.done_flag = 300;
    // Button event: the saved delta is 0, so even the "no enemy" restore keeps the world stopped.
    m.frame(h);
    SH_CHECK(same(h.log, {"delta 0", "delta 0", "freeze", "message 9"}));

    // Touch event, an enemy lives: stays stopped (no restore).
    h.log.clear();
    h.ev.button = false;
    h.enemy = true;
    h.msg = true;
    h.d = 136;
    h.message = gf::MessageResult::Choice0;
    m.frame(h);
    SH_CHECK(same(h.log, {"delta 0", "message 9", "flag 300", "enter 0", "unfreeze", "scan"}));

    // The event lets the world run.
    h.log.clear();
    h.cur = gf::GameState::ReadMessage;
    h.ev.options = gf::kEventKeepsWorldRunning;
    h.message = gf::MessageResult::Showing;
    h.d = 136;
    m.frame(h);
    SH_CHECK(same(h.log, {"delta 0", "delta 136", "message 9"}));
}

SH_TEST(event_states) {
    gf::StateMachine m = make();
    FakeHost h;
    h.raw = 100;
    h.d = 50;
    h.ev.present = true;
    h.ev.done_flag = 12;
    h.ev.param = 3;

    h.cur = gf::GameState::EventSetFlag;
    m.frame(h);
    SH_CHECK(same(h.log, {"delta 100", "delta 50", "flag 12", "store 0", "scan"}));

    h.log.clear();
    h.d = 50;
    h.cur = gf::GameState::EventPlaySound;
    m.frame(h);
    SH_CHECK(same(h.log, {"delta 100", "delta 50", "sound 1283", "flag 12", "store 0", "scan"}));

    // Sound number wraps at 16 bits.
    h.log.clear();
    h.ev.param = 65535;
    m.event_play_sound(h);
    SH_CHECK(h.log[1] == "sound 1279");

    h.log.clear();
    h.d = 50;
    h.ev.param = 3;
    h.cur = gf::GameState::EventCallback;
    m.frame(h);
    SH_CHECK(same(h.log, {"delta 100", "allowed? 3", "delta 50", "script 3"}));

    h.log.clear();
    h.d = 50;
    h.ev.options = 2;
    h.has_script = false;
    m.frame(h);
    SH_CHECK(same(h.log, {"delta 100", "allowed? 3", "flag 12", "delta 50", "script 3", "store 0", "scan"}));

    // A hook skipped the handler: no done flag, no script, back to gameplay.
    h.log.clear();
    h.d = 50;
    h.cur = gf::GameState::EventCallback;
    h.refuse_script = true;
    m.frame(h);
    SH_CHECK(same(h.log, {"delta 100", "allowed? 3", "store 0", "scan"}));
    h.refuse_script = false;

    h.log.clear();
    h.d = 50;
    h.cur = gf::GameState::EventCallback;
    h.ev.present = false;
    m.frame(h);
    SH_CHECK(same(h.log, {"delta 100", "store 0", "scan"}));
}

SH_TEST(event_screen_frame) {
    gf::StateMachine m = make();
    FakeHost h;
    h.ev.present = true;
    h.ev.done_flag = 7;
    h.ev.param = 2;
    m.event_screen_frame(h);
    SH_CHECK(same(h.log, {"start_screen", "ready", "flag 7", "script 2", "draw_screen"}));
    h.log.clear();
    h.screen_step = 1;
    m.event_screen_frame(h);
    SH_CHECK(same(h.log, {"ready", "flag 7", "script 2", "draw_screen"}));
}
