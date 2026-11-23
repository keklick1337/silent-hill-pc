// The game over sequence against its specification (the original's game over state): the order of the
// steps, the 30 fps hold counting and the hard-difficulty exit, as call sequences on a fake host.

#include <string>
#include <vector>

#include "engine/gameflow/game_over.h"

#include "framework/sh_test.h"

namespace gf = sh::gameflow;

namespace {

class Host final : public gf::GameOverHost {
public:
    std::int32_t s[3] = {0, 0, 0};
    std::int32_t h = 0, t = 0;
    bool confirm = false, hard = false, ingame = true;
    std::int32_t dt = 136;
    std::vector<std::string> log;

    std::int32_t step(int l) const override { return s[l]; }
    void store_step(int l, std::int32_t v) override { s[l] = v; }
    std::int32_t hold() const override { return h; }
    void store_hold(std::int32_t v) override { h = v; }
    std::int32_t timer() const override { return t; }
    void store_timer(std::int32_t v) override { t = v; }
    std::int32_t raw_delta() const override { return dt; }
    bool player_stopped() const override { return false; }
    bool flag(std::int16_t) const override { return false; }
    gf::MessageResult show_message(int) override { return gf::MessageResult::Showing; }

    void freeze_player() override { log.push_back("freeze"); }
    void unfreeze_player() override { log.push_back("unfreeze"); }
    void count_continue() override { log.push_back("continue"); }
    void reset_menu_selection() override { log.push_back("menu"); }
    int pick_tip() override { log.push_back("pick"); return 4; }
    void load_tip(int tip) override { log.push_back("load " + std::to_string(tip)); }
    void tip_seen(int tip) override { log.push_back("seen " + std::to_string(tip)); }
    void fade(gf::FadeStep how, bool out, std::int32_t rate) override {
        log.push_back(std::string("fade ") + (how == gf::FadeStep::Auto ? "auto " : "start ") + (out ? "out " : "in ") +
                      std::to_string(rate));
    }
    void draw_title() override { log.push_back("title"); }
    void draw_tip() override { log.push_back("tip"); }
    bool confirm_pressed() const override { return confirm; }
    std::int32_t delta() const override { return dt; }
    bool tips_skipped() const override { return hard; }
    void wait_files() override { log.push_back("wait"); }
    void stop_radio() override { log.push_back("radio"); }
    void leave() override { log.push_back("leave"); }
    bool in_game() const override { return ingame; }
    void pause_world() override { log.push_back("pause"); }
};

gf::GameOver make() {
    gf::GameOver g;
    gf::GameOverRules r;
    r.title_hold = 240;
    r.tip_hold = 480;
    r.fast_fade = 2048;
    r.slow_fade = 8192;
    r.frame_30fps = 136;
    g.set_rules(r);
    return g;
}

}  // namespace

SH_TEST(game_over_first_frame) {
    gf::GameOver g = make();
    Host h;
    g.frame(h);
    SH_CHECK((h.log == std::vector<std::string>{"freeze", "continue", "menu", "pick", "load 4", "fade auto out 2048"}));
    SH_CHECK(h.s[0] == 1);
}

SH_TEST(game_over_title_hold_counts_30fps_frames) {
    gf::GameOver g = make();
    Host h;
    h.s[0] = 3;
    h.dt = 68;  // half a 30 fps frame
    g.frame(h);
    SH_CHECK(h.h == 0);
    g.frame(h);
    SH_CHECK(h.h == 1);
    SH_CHECK(h.log.back() == "pause");
    h.h = 241;
    g.frame(h);
    SH_CHECK(h.s[0] == 4);
}

SH_TEST(game_over_hard_skips_tips_and_leaves) {
    gf::GameOver g = make();
    Host h;
    h.s[0] = 5;
    h.hard = true;
    g.frame(h);
    SH_CHECK(h.s[0] == -1);
    h.log.clear();
    h.ingame = false;
    g.frame(h);
    SH_CHECK((h.log == std::vector<std::string>{"unfreeze", "leave", "pause"}));
}

SH_TEST(game_over_tip_seen_on_confirm) {
    gf::GameOver g = make();
    Host h;
    g.frame(h);  // picks tip 4
    h.log.clear();
    h.s[0] = 7;
    h.confirm = true;
    g.frame(h);
    SH_CHECK((h.log == std::vector<std::string>{"tip", "seen 4", "pause"}));
    SH_CHECK(h.s[0] == 8);
}
