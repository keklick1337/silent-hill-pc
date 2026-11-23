// The menu states and the event command helpers against their specification (the original's pause, options, item, map,
// movie and save states and its fade, background and item commands), as call sequences on a fake host.

#include <string>
#include <vector>

#include "engine/gameflow/event_commands.h"
#include "engine/gameflow/menu_states.h"

#include "framework/sh_test.h"

namespace gf = sh::gameflow;

namespace {

class StepsBase {
public:
    std::int32_t s[3] = {0, 0, 0};
    std::int32_t hold_v = 0, timer_v = 0;
    std::vector<std::string> log;
    std::vector<int> messages;  // answers of show_message, in order
};

class MenuHost final : public gf::MenuStateHost, public StepsBase {
public:
    gf::RunningEvent ev;
    bool ready = false, chunks = true, native = false, pause = false, combo = false, pending = false, map = false;
    bool seen = false;
    int location = 0;
    gf::MapBlock block = gf::MapBlock::None;
    std::int32_t dt = 136;

    std::int32_t step(int l) const override { return s[l]; }
    void store_step(int l, std::int32_t v) override { s[l] = v; }
    std::int32_t hold() const override { return hold_v; }
    void store_hold(std::int32_t v) override { hold_v = v; }
    std::int32_t timer() const override { return timer_v; }
    void store_timer(std::int32_t v) override { timer_v = v; }
    std::int32_t raw_delta() const override { return dt; }
    bool player_stopped() const override { return false; }
    bool flag(std::int16_t) const override { return seen; }
    gf::MessageResult show_message(int m) override {
        log.push_back("message " + std::to_string(m));
        const int a = messages.empty() ? 0 : messages.front();
        if (!messages.empty()) {
            messages.erase(messages.begin());
        }
        return static_cast<gf::MessageResult>(a);
    }
    void enter_state(gf::GameState st) override { log.push_back("state " + std::to_string(static_cast<int>(st))); }
    gf::RunningEvent running_event() const override { return ev; }
    void set_flag(std::int16_t f) override { log.push_back("flag " + std::to_string(f)); }
    void hold_world() override { log.push_back("hold"); }
    void release_world() override { log.push_back("release"); }
    bool screen_ready() const override { return ready; }
    void clear_screen_ready() override { ready = false; }
    bool chunks_idle() override { return chunks; }
    void sound_command(int c) override { log.push_back("sound " + std::to_string(c)); }
    void fade_out(bool restart) override { log.push_back(restart ? "fade out" : "fade on"); }
    void fade_in() override { log.push_back("fade in"); }
    void fade_instant() override { log.push_back("instant"); }
    void open_screen(gf::MenuScreen sc) override { log.push_back("screen " + std::to_string(static_cast<int>(sc))); }
    bool native_pause_menu() const override { return native; }
    void draw_paused() override { log.push_back("paused"); }
    void paused_extras() override { log.push_back("extras"); }
    bool pause_pressed() const override { return pause; }
    bool debug_save_combo() const override { return combo; }
    void clear_event_param() override { log.push_back("clear param"); }
    void reset_pause_menu() override { log.push_back("pm reset"); }
    void update_pause_menu() override { log.push_back("pm update"); }
    bool pause_menu_return_pending() const override { return pending; }
    void load_options_screen() override { log.push_back("options"); }
    gf::MapBlock map_block() const override { return block; }
    bool map_pressed() const override { return map; }
    void load_paper_map() override { log.push_back("paper map"); }
    void load_movie_stream() override { log.push_back("stream"); }
    void play_movie(int p) override { log.push_back("movie " + std::to_string(p)); }
    void resume_game_at_step(int st) override { log.push_back("resume " + std::to_string(st)); }
    void poll_saves() override { log.push_back("poll"); }
    void store_player() override { log.push_back("store"); }
    int save_location() const override { return location; }
    void load_save_screen() override { log.push_back("save screen"); }
    void stop_rumble() override { log.push_back("rumble"); }
};

gf::MenuStates make() {
    gf::MenuStates m;
    gf::MenuStateRules r;
    r.pause_on_sound = 3;
    r.pause_off_sound = 4;
    r.stop_stream_sound = 19;
    r.no_map_message = 30;
    r.too_dark_message = 31;
    r.save_prompt_message = 32;
    r.save_seen_flag = 100;
    r.save_without_prompt = 24;
    r.movie_resumes_game = 2;
    m.set_rules(r);
    return m;
}

using Log = std::vector<std::string>;

class CommandHost final : public gf::EventCommandHost, public StepsBase {
public:
    bool idle = false, done = false, hidden = false, shown = false, loaded = true;

    std::int32_t step(int l) const override { return s[l]; }
    void store_step(int l, std::int32_t v) override { s[l] = v; }
    std::int32_t hold() const override { return hold_v; }
    void store_hold(std::int32_t v) override { hold_v = v; }
    std::int32_t timer() const override { return timer_v; }
    void store_timer(std::int32_t v) override { timer_v = v; }
    std::int32_t raw_delta() const override { return 136; }
    bool player_stopped() const override { return false; }
    bool flag(std::int16_t) const override { return false; }
    gf::MessageResult show_message(int) override { return gf::MessageResult::Showing; }
    void set_fade_rate(std::int32_t r) override { log.push_back("rate " + std::to_string(r)); }
    void start_fade(bool in, bool white) override {
        log.push_back(std::string("fade ") + (in ? "in" : "out") + (white ? " white" : ""));
    }
    void show_borders() override { log.push_back("borders on"); }
    void hide_borders() override { log.push_back("borders off"); }
    void mark_cutscene() override { log.push_back("cutscene"); }
    bool fade_idle() const override { return idle; }
    bool fade_done() const override { return done; }
    bool borders_hidden() const override { return hidden; }
    bool borders_shown() const override { return shown; }
    void load_background(int f, bool sec) override { log.push_back("load " + std::to_string(f) + (sec ? " 2" : "")); }
    bool files_loaded() override { return loaded; }
    void draw_background(bool sec) override { log.push_back(sec ? "draw 2" : "draw"); }
    void store_vram() override { log.push_back("store"); }
    void restore_vram() override { log.push_back("restore"); }
    void load_item_model(int i) override { log.push_back("model " + std::to_string(i)); }
    void show_item_pickup(int i) override { log.push_back("pickup " + std::to_string(i)); }
    void add_item(int i, int c) override { log.push_back("add " + std::to_string(i) + " " + std::to_string(c)); }
};

}  // namespace

SH_TEST(menu_pause_blinks_and_resumes) {
    gf::MenuStates m = make();
    MenuHost h;
    m.paused(h);
    SH_CHECK((h.log == Log{"hold", "paused", "extras", "sound 3"}));
    SH_CHECK(h.s[0] == 1);
    h.log.clear();
    h.dt = 2048;  // half a second: the text is off
    m.paused(h);
    SH_CHECK((h.log == Log{"hold", "extras"}));
    h.log.clear();
    h.pause = true;
    m.paused(h);
    SH_CHECK((h.log == Log{"hold", "paused", "extras", "sound 4", "release", "state 0"}));
    SH_CHECK(m.blink() == 0);
}

SH_TEST(menu_pause_native_menu_and_debug_save) {
    gf::MenuStates m = make();
    MenuHost h;
    h.native = true;
    m.paused(h);
    SH_CHECK((h.log == Log{"hold", "sound 3", "pm update"}));
    h.log.clear();
    h.combo = true;
    m.paused(h);
    SH_CHECK((h.log == Log{"hold", "sound 4", "clear param", "release", "pm reset", "state 8"}));
}

SH_TEST(menu_options_and_items_hand_over) {
    gf::MenuStates m = make();
    MenuHost h;
    h.chunks = false;
    m.options(h);
    SH_CHECK((h.log == Log{"fade out", "instant"}) && h.s[0] == 1);
    h.chunks = true;
    h.ready = true;
    h.log.clear();
    m.options(h);
    SH_CHECK((h.log == Log{"sound 19", "options", "screen 0"}) && h.s[0] == 2);

    MenuHost i;
    m.status(i);
    SH_CHECK((i.log == Log{"fade out", "instant"}));
    m.status(i);
    SH_CHECK(i.log.back() == "screen 1");
}

SH_TEST(menu_map_without_map_shows_message) {
    gf::MenuStates m = make();
    MenuHost h;
    h.block = gf::MapBlock::TooDark;
    h.messages = {0, 1};
    m.map_screen(h);
    SH_CHECK((h.log == Log{"hold", "message 31"}));
    h.log.clear();
    m.map_screen(h);
    SH_CHECK((h.log == Log{"hold", "message 31", "release", "state 0"}));
    MenuHost k;
    k.block = gf::MapBlock::NoMap;
    k.map = true;
    m.map_screen(k);
    SH_CHECK((k.log == Log{"hold", "release", "state 0"}));
    MenuHost o;
    o.ready = true;
    m.map_screen(o);
    SH_CHECK((o.log == Log{"paper map", "fade out", "instant", "screen 2"}));
}

SH_TEST(menu_movie_plays_then_returns) {
    gf::MenuStates m = make();
    MenuHost h;
    h.ready = true;
    h.ev.present = true;
    h.ev.param = 5;
    h.ev.done_flag = 77;
    h.ev.options = 2;
    m.movie(h);
    // The first frame clears the ready signal, so the movie waits a frame.
    SH_CHECK((h.log == Log{"fade on", "stream"}));
    h.ready = true;
    h.log.clear();
    m.movie(h);
    SH_CHECK((h.log == Log{"movie 5", "flag 77", "screen 4", "resume 1"}));
}

SH_TEST(menu_save_asks_once) {
    gf::MenuStates m = make();
    MenuHost h;
    h.ev.param = 3;
    h.location = 3;
    h.messages = {0, 1};
    m.save(h);
    SH_CHECK((h.log == Log{"poll", "store", "message 32"}) && h.s[0] == 0);
    h.log.clear();
    m.save(h);
    SH_CHECK((h.log == Log{"poll", "store", "message 32", "flag 100", "save screen", "fade out"}) && h.s[0] == 1);
    h.log.clear();
    h.ready = true;
    m.save(h);
    SH_CHECK((h.log == Log{"poll", "fade in", "rumble", "screen 3"}));
    MenuHost n;
    n.ev.param = 24;
    n.location = 24;
    m.save(n);
    SH_CHECK((n.log == Log{"poll", "store", "save screen", "fade out"}));
}

SH_TEST(event_fade_auto_walks_start_then_wait) {
    CommandHost h;
    gf::commands::fade(h, gf::FadeCommand::Auto, 1, gf::FadeKind::White, 4096, false);
    SH_CHECK((h.log == Log{"rate 4096", "fade out white"}) && h.s[2] == 1);
    h.done = true;
    gf::commands::fade(h, gf::FadeCommand::Auto, 1, gf::FadeKind::White, 4096, false);
    SH_CHECK(h.s[0] == 1 && h.s[2] == 0);
    CommandHost b;
    gf::commands::fade(b, gf::FadeCommand::Start, 1, gf::FadeKind::Borders, 4096, true);
    SH_CHECK((b.log == Log{"borders on"}));
    gf::commands::fade(b, gf::FadeCommand::Start, 1, gf::FadeKind::CutsceneBorders, 100, true);
    SH_CHECK((b.log == Log{"borders on", "rate 100", "borders on", "cutscene"}));
    // A wait tests the raw value for exactly 0 or 1.
    b.shown = true;
    gf::commands::fade(b, gf::FadeCommand::Wait, 2, gf::FadeKind::Borders, 0, true);
    SH_CHECK(b.s[1] == 0);
    gf::commands::fade(b, gf::FadeCommand::Wait, 1, gf::FadeKind::Borders, 0, true);
    SH_CHECK(b.s[1] == 1);
}

SH_TEST(event_background_commands) {
    CommandHost h;
    h.loaded = false;
    gf::commands::background(h, gf::BackgroundCommand::Auto, 9, false);
    SH_CHECK((h.log == Log{"load 9"}) && h.s[2] == 1 && h.s[0] == 0);
    h.loaded = true;
    gf::commands::background(h, gf::BackgroundCommand::Auto, 9, false);
    SH_CHECK(h.s[0] == 1);
    CommandHost e;
    gf::commands::background(e, gf::BackgroundCommand::LoadEither, 4, true);
    SH_CHECK((e.log == Log{"load 4 2"}) && e.s[2] == 1);
    gf::commands::background(e, gf::BackgroundCommand::LoadEither, 4, true);
    SH_CHECK(e.s[1] == 1);
}

SH_TEST(event_item_commands) {
    CommandHost h;
    h.loaded = false;
    h.s[1] = 3;
    gf::commands::item(h, gf::ItemCommand::Load, 40, 1, false);
    SH_CHECK((h.log == Log{"model 40"}) && h.s[1] == 3 && h.s[2] == 1);
    h.loaded = true;
    gf::commands::item(h, gf::ItemCommand::AwaitLoad, 40, 1, false);
    SH_CHECK((h.log == Log{"model 40", "pickup 40"}) && h.s[0] == 1);
    CommandHost a;
    gf::commands::item(a, gf::ItemCommand::AutoAdd, 41, 2, false);
    SH_CHECK((a.log == Log{"add 41 2"}) && a.s[2] == 0);
    CommandHost n;
    gf::commands::item(n, gf::ItemCommand::Nothing, 41, 2, false);
    SH_CHECK(n.log.empty());
}

SH_TEST(event_path_waypoint_modes) {
    gf::Waypoint w;
    SH_CHECK(gf::path_waypoint(0, gf::Waypoint{100, -5, 200}, 10, 20, w) && w.x == 110 && w.y == -5 && w.z == 220);
    SH_CHECK(gf::path_waypoint(1, gf::Waypoint{100, -5, 200}, 10, 20, w) && w.x == 10 && w.z == 20);
    SH_CHECK(!gf::path_waypoint(2, gf::Waypoint{}, 1, 1, w));
}
