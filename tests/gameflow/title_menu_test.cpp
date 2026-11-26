// The title menu against its specification (the original's title menu state): the entries a save or an autosave shows,
// the wrap of the selection, the exit confirmation, the difficulty picker, the hand-off, the idle timer and the
// row layout, as call sequences on a fake host.

#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "engine/gameflow/title_menu.h"

#include "framework/sh_test.h"

namespace gf = sh::gameflow;

namespace {

class Host final : public gf::TitleHost {
public:
    int step = 1, idle = 0, demo = 0, saves = 0, difficulty_default_index = 1;
    bool title = true, loaded = false, skip = false, attract = false, autosave = false;
    bool fade_done = false;
    int queue = 1;
    gf::TitlePad p;
    gf::TitleMouse m;
    gf::ConfirmInput ci;
    std::vector<std::string> log;

    int state_step() const override { return step; }
    void set_state_step(int s) override { step = s; }
    bool in_title() const override { return title; }
    void stay_in_title() override { title = true; }
    bool demo_loaded() const override { return loaded; }
    int idle_frames() const override { return idle; }
    void reset_idle() override { idle = 0; }
    int demo_count() const override { return demo; }
    void set_demo_count(int c) override { demo = c; }
    bool skip_to_game() const override { return skip; }
    bool attract_demos() const override { return attract; }
    bool autosave_alive() const override { return autosave; }
    void poll_saves() override {}
    int save_count() const override { return saves; }
    int loadable = -1;  // -1: every save loads
    int loadable_save_count() const override { return loadable < 0 ? saves : loadable; }
    gf::TitlePad pad() const override { return p; }
    gf::TitleMouse mouse() const override { return m; }
    gf::ConfirmInput confirm_input() const override { return ci; }
    void press_enter() override { p.enter = true; }
    void press_cancel() override { p.cancel = true; }
    void release_enter() override { p.enter = false; }
    void consume_pad() override { p.up = p.down = p.enter = p.cancel = false; }
    void request_demo() override { log.push_back("demo"); }
    void open_title_screen() override { log.push_back("open"); }
    void demo_startup() override { log.push_back("startup"); }
    void reset_file_queue() override { log.push_back("fsreset"); }
    void play_sound(std::string_view n) override { log.push_back("sound " + std::string(n)); }
    void fade_in() override { log.push_back("fade in"); }
    void fade_out() override { log.push_back("fade out"); }
    void fade_reset() override { log.push_back("fade reset"); }
    bool fade_finished() override { return fade_done; }
    void continue_game() override { log.push_back("continue"); }
    void load_save_screen() override { log.push_back("load"); }
    void load_options_screen() override { log.push_back("options"); }
    int difficulty_count() const override { return 3; }
    const char* difficulty_label(int i) const override {
        static const char* const kLabels[] = {"EASY", "NORMAL", "HARD"};
        return kLabels[i];
    }
    int difficulty_default() const override { return difficulty_default_index; }
    void start_new_game(int d) override { log.push_back("new game " + std::to_string(d)); }
    void load_movie_stream() override { log.push_back("stream"); }
    int file_queue_length() override { return queue; }
    void quit() override { log.push_back("quit"); }
    void finish_loading() override { log.push_back("finish"); }
    void place_new_game_player() override { log.push_back("place"); }
    void change_game_state(std::string_view n) override {
        log.push_back("state " + std::string(n));
        title = false;
    }
    void clear_background() override { log.push_back("clear"); }
    void draw_background() override {}
    void show_cursor() override {}
    void leave_title_screen() override {}
    bool text_translated(const char*) const override { return false; }
    int text_width(const char* t) const override { return static_cast<int>(std::string(t).size()) * 10; }
    void text_position(int x, int y) override { log.push_back("pos " + std::to_string(x) + " " + std::to_string(y)); }
    void text_white() override {}
    void text(const char* t) override { log.push_back(std::string("text ") + t); }

    void tap(bool gf::TitlePad::*button) {
        p = gf::TitlePad{};
        p.*button = true;
    }
};

gf::TitleMenuData data() {
    std::ifstream in(std::string(SH_PROJECT_ROOT) + "/gamedata/menu/title_menu.json", std::ios::binary);
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    sh::Result<gf::TitleMenuData> d = gf::TitleMenuData::parse(text);
    SH_CHECK(static_cast<bool>(d));
    return d ? d.value() : gf::TitleMenuData{};
}

gf::TitleMenu menu() {
    gf::TitleMenu m;
    m.set_data(data());
    return m;
}

bool has(const std::vector<std::string>& log, const std::string& s) {
    for (const std::string& l : log) {
        if (l == s) {
            return true;
        }
    }
    return false;
}

constexpr int kLoad = 0, kContinue = 1, kStart = 2, kOption = 3, kExit = 4;

}  // namespace

SH_TEST(title_menu_data_parses) {
    const gf::TitleMenuData d = data();
    SH_CHECK(d.entries.size() == 5);
    SH_CHECK(d.find(gf::TitleEntryKind::Exit) == kExit);
    SH_CHECK(d.entries[kContinue].next == "AutoLoadSavegame" && d.entries[kContinue].next_alt == "MainLoadScreen");
    SH_CHECK(d.idle_frames == 1740 && d.intro_every == 3 && d.idle_state == "MovieIntro");
    SH_CHECK(!gf::TitleMenuData::parse("{\"entries\": []}"));
    SH_CHECK(!gf::TitleMenuData::parse("{\"entries\": [{\"kind\": \"bogus\", \"label\": \"X\"}]}"));
}

SH_TEST(title_menu_first_frame_opens_and_shows_fixed_entries) {
    gf::TitleMenu t = menu();
    Host h;
    h.step = 0;
    t.frame(h);
    SH_CHECK(h.log.front() == "open");
    SH_CHECK(h.step == 1);
    SH_CHECK(t.state() == gf::TitleMenu::State::Main);
    SH_CHECK(t.visible() == ((1u << kStart) | (1u << kOption) | (1u << kExit)));
    SH_CHECK(t.selected() == kStart);
    // Three rows, bottom-aligned: START at 224, OPTION at 244, EXIT at 264, retail centres for START and OPTION.
    SH_CHECK(has(h.log, "pos 126 224") && has(h.log, "pos 119 244") && has(h.log, "pos " + std::to_string(158 - 25) + " 264"));
    SH_CHECK(has(h.log, "text [") && has(h.log, "text START"));
}

SH_TEST(title_menu_save_shows_load_and_moves_to_continue) {
    gf::TitleMenu t = menu();
    Host h;
    t.frame(h);
    h.saves = 2;
    t.frame(h);
    SH_CHECK(t.visible() == 0x1Fu);
    SH_CHECK(t.selected() == kContinue);
    // The card removed: continue stays (the autosave is gone too) only if shown; the cursor walks down.
    h.saves = 0;
    t.frame(h);
    SH_CHECK(t.selected() == kStart);
}

// Saves of a newer version of the game are listed (LOAD) but none of them continues: CONTINUE shows only while a
// save loads.
SH_TEST(title_menu_newer_saves_show_load_without_continue) {
    gf::TitleMenu t = menu();
    Host h;
    t.frame(h);
    h.saves = 2;
    h.loadable = 0;
    t.frame(h);
    SH_CHECK((t.visible() & (1u << kLoad)) != 0 && (t.visible() & (1u << kContinue)) == 0);
    SH_CHECK(t.selected() != kContinue);
    h.loadable = 1;
    t.frame(h);
    SH_CHECK(t.visible() == 0x1Fu);
}

SH_TEST(title_menu_selection_wraps_over_hidden_entries) {
    gf::TitleMenu t = menu();
    Host h;
    t.frame(h);
    h.tap(&gf::TitlePad::up);
    t.frame(h);
    SH_CHECK(t.selected() == kExit);
    SH_CHECK(has(h.log, "sound menu_move"));
    h.tap(&gf::TitlePad::down);
    t.frame(h);
    SH_CHECK(t.selected() == kStart);
    h.tap(&gf::TitlePad::down);
    t.frame(h);
    SH_CHECK(t.selected() == kOption);
}

SH_TEST(title_menu_exit_asks_and_no_returns) {
    gf::TitleMenu t = menu();
    Host h;
    t.frame(h);
    h.tap(&gf::TitlePad::up);
    t.frame(h);
    h.tap(&gf::TitlePad::enter);
    h.log.clear();
    t.frame(h);
    SH_CHECK(t.confirm_open() && !t.confirm_yes());
    SH_CHECK(has(h.log, "sound menu_confirm") && has(h.log, "text QUIT_THE_GAME?"));
    SH_CHECK(!has(h.log, "fade out"));
    // Confirming "NO" closes the box with the cancel sound.
    h.p = gf::TitlePad{};
    h.ci = gf::ConfirmInput{};
    h.ci.confirm = true;
    h.log.clear();
    t.frame(h);
    SH_CHECK(!t.confirm_open() && has(h.log, "sound menu_cancel") && !has(h.log, "quit"));
    // Right toggles to "YES"; confirming it quits.
    h.p.enter = true;
    t.frame(h);
    h.p = gf::TitlePad{};
    h.ci = gf::ConfirmInput{};
    h.ci.right = true;
    t.frame(h);
    SH_CHECK(t.confirm_yes());
    h.ci = gf::ConfirmInput{};
    h.ci.confirm = true;
    t.frame(h);
    SH_CHECK(has(h.log, "quit"));
}

SH_TEST(title_menu_new_game_through_difficulty) {
    gf::TitleMenu t = menu();
    Host h;
    t.frame(h);
    h.tap(&gf::TitlePad::enter);
    t.frame(h);
    SH_CHECK(t.state() == gf::TitleMenu::State::Difficulty);
    SH_CHECK(has(h.log, "fade reset") && has(h.log, "sound menu_confirm"));
    h.tap(&gf::TitlePad::up);
    t.frame(h);
    SH_CHECK(t.difficulty() == 0);
    h.tap(&gf::TitlePad::up);
    t.frame(h);
    SH_CHECK(t.difficulty() == 2);
    h.tap(&gf::TitlePad::enter);
    h.log.clear();
    t.frame(h);
    SH_CHECK((std::vector<std::string>(h.log.begin(), h.log.begin() + 4) ==
              std::vector<std::string>{"new game 2", "stream", "sound menu_start_game", "fade out"}));
    SH_CHECK(t.state() == gf::TitleMenu::State::NewGameStart);
    h.p = gf::TitlePad{};
    h.fade_done = true;
    h.log.clear();
    t.frame(h);
    SH_CHECK((h.log == std::vector<std::string>{"finish", "place", "state MovieOpening"}));
}

SH_TEST(title_menu_continue_with_autosave_goes_to_load_screen) {
    gf::TitleMenu t = menu();
    Host h;
    h.autosave = true;
    t.frame(h);
    t.select_continue();
    h.tap(&gf::TitlePad::enter);
    t.frame(h);
    SH_CHECK(has(h.log, "continue") && has(h.log, "sound menu_start_game"));
    h.p = gf::TitlePad{};
    h.fade_done = true;
    t.frame(h);
    SH_CHECK(has(h.log, "state MainLoadScreen"));
}

SH_TEST(title_menu_skip_to_game_drops_the_movie) {
    gf::TitleMenu t = menu();
    Host h;
    h.skip = true;
    h.difficulty_default_index = 1;
    t.frame(h);  // START picked
    h.p = gf::TitlePad{};
    t.frame(h);  // NORMAL picked
    SH_CHECK(has(h.log, "new game 1"));
    h.p = gf::TitlePad{};
    h.fade_done = true;
    t.frame(h);
    SH_CHECK(has(h.log, "state MainLoadScreen"));
    SH_CHECK(t.skip_step() == 4);
}

SH_TEST(title_menu_idle_plays_intro_movie) {
    gf::TitleMenu t = menu();
    Host h;
    h.demo = 1;  // (1 + 1) % 3 != 0 but attract demos are off
    t.frame(h);
    h.idle = 1741;
    t.frame(h);
    SH_CHECK(h.step == 2 && has(h.log, "stream"));
    h.queue = 0;
    t.frame(h);
    SH_CHECK(h.demo == 2 && has(h.log, "clear") && has(h.log, "state MovieIntro"));
}

SH_TEST(title_menu_mouse_hover_and_click) {
    gf::TitleMenu t = menu();
    Host h;
    t.frame(h);
    h.m.in_view = true;
    h.m.moved = true;
    h.m.y = 264 - 3;  // EXIT row
    t.frame(h);
    SH_CHECK(t.selected() == kExit);
    h.m.moved = false;
    h.m.left = true;
    h.m.y = 244;  // OPTION row
    t.frame(h);
    SH_CHECK(has(h.log, "options"));
}
