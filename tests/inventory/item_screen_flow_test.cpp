// SPDX-License-Identifier: GPL-3.0-or-later
// Unit tests for the item screen flow (sh::inventory::update_item_screen) and the play time.

#include "engine/inventory/item_screen_flow.h"

#include <string>
#include <vector>

#include "framework/sh_test.h"

namespace {

using namespace sh::inventory;

class FakeScreen final : public ScreenHost {
public:
    std::int32_t current = 0;
    std::int32_t sub = 5;
    bool pending = false;
    bool restart = false;
    EnteredFrom from = EnteredFrom::Other;
    bool ready = true;
    bool faded = false;
    bool side = false;
    bool enter = false;
    int choice = 0;
    std::int32_t step_after_menu = -1;
    std::vector<std::string> log;

    std::int32_t step() override { return current; }
    void set_step(std::int32_t s) override { current = s; sub = 0; log.push_back("step " + std::to_string(s)); }
    std::int32_t sub_step() override { return sub; }
    void set_sub_step(std::int32_t s) override { sub = s; }
    void begin_frame() override { log.push_back("frame"); }
    void advance_play_time() override { log.push_back("time"); }
    bool results_pending() override { return pending; }
    bool restart_requested() override { return restart; }
    void restart_game() override { log.push_back("restart"); }
    void grant_unlocked_items() override { log.push_back("grant"); }
    void open_screen() override { log.push_back("open"); }
    EnteredFrom entered_from() override { return from; }
    void start_menu(bool has, Selection s) override { log.push_back(has ? "menu " + std::to_string(int(s)) : "menu"); }
    void prepare_items() override { log.push_back("items"); }
    bool menu_ready() override { return ready; }
    void run_menu() override { log.push_back("logic"); if (step_after_menu >= 0) { current = step_after_menu; sub = 0; } }
    void draw_commands() override { log.push_back("commands"); }
    void show_notice(Notice n) override { log.push_back("notice " + std::to_string(int(n))); }
    void fade_out() override { log.push_back("fade"); }
    bool fade_finished() override { return faded; }
    void update_weapon() override { log.push_back("weapon " + std::to_string(sub)); }
    void go_to_options() override { log.push_back("options"); }
    void go_to_map() override { log.push_back("map"); }
    void return_to_game() override { log.push_back("game"); }
    void open_results() override { log.push_back("results"); }
    bool results_continue_pressed() override { return enter; }
    bool results_side_pressed() override { return side; }
    bool results_enter_pressed() override { return enter; }
    int results_choice() override { return choice; }
    void set_results_choice(int c) override { choice = c; }
    void sound(MenuSound s, int) override { log.push_back("sound " + std::to_string(int(s))); }
    void load_save_screen() override { log.push_back("saveload"); }
    void go_to_save_screen() override { log.push_back("save"); }
    bool restart_data_loaded() override { return true; }
    void draw_screen() override { log.push_back("draw"); }
    void draw_scroll_arrows() override { log.push_back("arrows"); }
    void draw_results() override { log.push_back("drawresults"); }
};

std::string joined(const std::vector<std::string>& log)
{
    std::string out;
    for (const std::string& s : log)
        out += (out.empty() ? "" : ",") + s;
    return out;
}

}  // namespace

SH_TEST(item_screen_opens_and_returns_early) {
    FakeScreen screen;
    screen.from = EnteredFrom::Options;
    update_item_screen(screen);
    SH_CHECK(joined(screen.log) == "frame,time,grant,open,menu 3,items,step 1");
    FakeScreen results;
    results.pending = true;
    update_item_screen(results);
    SH_CHECK(joined(results.log) == "frame,time,step 21");
}

SH_TEST(item_screen_browses_and_shows_notices) {
    FakeScreen screen;
    screen.current = 1;
    screen.step_after_menu = 12;
    update_item_screen(screen);
    SH_CHECK(joined(screen.log) == "frame,time,logic,commands,step 1,notice 0,draw,arrows");
    SH_CHECK(screen.sub == 0);
    screen.log.clear();
    screen.step_after_menu = -1;
    screen.ready = false;
    screen.current = 9;
    update_item_screen(screen);
    SH_CHECK(joined(screen.log) == "frame,time,commands,draw,arrows");
}

SH_TEST(item_screen_leaves_and_keeps_sub_step) {
    FakeScreen screen;
    screen.current = 17;
    screen.sub = 7;
    update_item_screen(screen);
    SH_CHECK(joined(screen.log) == "frame,time,fade,step 20,weapon 0,draw,arrows");
    SH_CHECK(screen.current == 20 && screen.sub == 7);
    screen.log.clear();
    screen.faded = true;
    update_item_screen(screen);
    SH_CHECK(joined(screen.log) == "frame,time,game");
}

SH_TEST(item_screen_results_choice) {
    FakeScreen screen;
    screen.current = 23;
    screen.side = true;
    screen.enter = true;
    update_item_screen(screen);
    SH_CHECK(screen.choice == 1 && screen.current == 25);
    SH_CHECK(joined(screen.log) == "frame,sound 0,fade,sound 1,step 25,draw,drawresults");
}

SH_TEST(play_time_wraps_and_saturates) {
    std::uint32_t timer = 0x7FFFFF00u;
    std::uint8_t overflows = 0;
    advance_play_time(timer, overflows, 0x200);
    SH_CHECK(overflows == 1 && timer == 0x100u);
    timer = 0x7FFFFFFFu;
    overflows = 3;
    advance_play_time(timer, overflows, 1);
    SH_CHECK(timer == 130u * 3600u * 4096u);
    timer = 0;
    advance_play_time(timer, overflows, 0);
    SH_CHECK(timer == 1);
    timer = 10;
    overflows = 2;
    advance_play_time(timer, overflows, -4);
    SH_CHECK(timer == 6 && overflows == 2);
}
