// The options screen (src/engine/screens/options_menu.h) against the shipped lists:
// entry, the underline slide, rows (toggle, list, count, volume), the shoulder switch
// to the extra list and its leave, the volume bar's notches.

#include "engine/screens/options_menu.h"

#include "framework/sh_test.h"

#include <fstream>
#include <iterator>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace {

namespace sc = sh::screens;

std::string read(const char* rel) {
    std::ifstream in(std::string(SH_PROJECT_ROOT) + rel, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

class Recorder final : public sc::MenuSink {
public:
    std::vector<sc::MenuPrim> prims;
    void prim(const sc::MenuPrim& r) override { prims.push_back(r); }
};

struct Text {
    std::int32_t x, y;
    std::string text;
};

class FakeHost final : public sc::OptionsHost {
public:
    std::map<std::string, std::int32_t> values{{"blood_color", 5}, {"bgm_volume", 16}, {"se_volume", 128}};
    std::int32_t state_ = 0;
    std::int32_t extras = 0;
    bool fade_done = false;
    int tree_frames = 0;
    bool tree_active = false;
    void update_tree(bool active) override {
        tree_frames++;
        tree_active = active;
    }
    sc::Fade fade_{sc::FadeState::None, false};
    std::set<sc::MenuButton> pulsed_, clicked_;
    std::vector<Text> texts;
    std::int32_t tx = 0, ty = 0;
    std::vector<std::string> calls;
    int moves = 0, errors = 0;

    bool from_gameplay() override { return false; }
    bool from_title() override { return true; }
    bool opened_from_pause() override { return false; }
    bool pulsed(sc::MenuButton b) override { return pulsed_.count(b) != 0; }
    bool clicked(sc::MenuButton b) override { return clicked_.count(b) != 0; }
    bool held(sc::MenuButton) override { return false; }
    bool mouse_ui_position(std::int32_t&, std::int32_t&) override { return false; }
    bool mouse_moved() override { return false; }
    bool mouse_left_clicked() override { return false; }
    bool mouse_left_held() override { return false; }
    bool mouse_right_clicked() override { return false; }
    std::int32_t mouse_wheel_step() override { return 0; }
    void inject_enter() override {}
    void inject_cancel() override {}
    void inject_direction(std::int32_t) override {}
    void show_mouse_cursor() override {}
    std::int32_t value(const std::string& n) override { return values[n]; }
    void set_value(const std::string& n, std::int32_t v) override { values[n] = v; }
    std::int32_t choice_count(const std::string&) override { return 3; }
    std::int32_t volume(const std::string& n) override { return values[n]; }
    void set_volume(const std::string& n, std::int32_t v) override { values[n] = v; }
    bool extras_unlocked() override { return extras != 0; }
    bool language_row_active() override { return false; }
    std::int32_t language_count() override { return 1; }
    std::int32_t language_slot() override { return 0; }
    void set_language_slot(std::int32_t) override {}
    std::string language_name(std::int32_t) override { return ""; }
    std::int32_t language_name_x(std::int32_t) override { return 0; }
    std::int32_t language_name_length(std::int32_t) override { return 0; }
    std::int32_t language_arrow_spread(std::int32_t) override { return 0; }
    void play_sfx(sc::MenuSfx s) override { moves += s == sc::MenuSfx::Move; }
    void play_sound(sc::MenuSfx s) override { errors += s == sc::MenuSfx::Error; }
    std::int32_t state() override { return state_; }
    void set_state(std::int32_t s) override { state_ = s; }
    bool fade_finished() override { return fade_done; }
    void set_fade(sc::Fade f) override { fade_ = f; }
    void update_bgm_track() override {}
    void update_play_timer() override {}
    void prepare_screen() override { calls.push_back("prepare"); }
    void leave_screen() override { calls.push_back("leave"); }
    void load_submenu(sc::Submenu) override { calls.push_back("load"); }
    void wait_for_loads() override {}
    void open_submenu(sc::Submenu) override { calls.push_back("open"); }
    void update_submenu(sc::Submenu) override { calls.push_back("submenu"); }
    void draw_background() override {}
    // Linear stand-in: full at 1024 (the slide's end, 8 << 7).
    std::int32_t sine(std::int32_t a) override { return a >= 1024 ? 4096 : a * 4; }
    std::int32_t frame_counter() override { return 0; }
    void text_color(sc::TextColor) override {}
    void text_position(std::int32_t x, std::int32_t y) override { tx = x; ty = y; }
    void text_layer(std::int32_t) override {}
    void text_reset_layer() override {}
    void text_draw(const std::string& t, std::int32_t) override { texts.push_back({tx, ty, t}); }

    void frame(sc::OptionsScreen& s, Recorder& r) {
        texts.clear();
        r.prims.clear();
        s.update(*this, r);
        pulsed_.clear();
        clicked_.clear();
    }
    // Idle frames until the next frame takes input (the underline's slide has ended).
    void ready(sc::OptionsScreen& s, Recorder& r) {
        while (s.highlight_timer() < 7) {
            frame(s, r);
        }
    }
};

sc::MenuStyle style() {
    sh::Result<sc::MenuStyle> s = sc::parse_menu_style(read("/gamedata/menu/menu_style.json"));
    SH_CHECK(bool(s));
    return s ? s.value() : sc::MenuStyle{};
}

// The shipped file shows the engine menu tree; the tests of the original lists turn it off.
sc::OptionsLayout shipped_layout() {
    sh::Result<sc::OptionsLayout> l = sc::parse_options_layout(read("/gamedata/menu/options.json"));
    SH_CHECK(bool(l));
    return l ? l.value() : sc::OptionsLayout{};
}

sc::OptionsLayout layout() {
    sc::OptionsLayout l = shipped_layout();
    l.tree.clear();
    return l;
}

const Text* find_text(const std::vector<Text>& t, const std::string& s) {
    for (const Text& x : t) {
        if (x.text == s) {
            return &x;
        }
    }
    return nullptr;
}

}  // namespace

SH_TEST(options_menu_layout_parses) {
    const sc::OptionsLayout l = layout();
    SH_CHECK(l.main.rows.size() == 9 && l.extra.rows.size() == 8);
    SH_CHECK(l.main.rows[0].kind == sc::OptionsRow::Kind::Leave);
    SH_CHECK(l.main.rows[8].kind == sc::OptionsRow::Kind::Volume && l.main.rows[8].bar_y == 72);
    SH_CHECK(l.extra.rows[1].codes.size() == 4 && l.extra.rows[1].codes[3] == 11);
    SH_CHECK(l.extra.rows[6].locked && !l.extra.rows[5].locked);
    SH_CHECK(l.volume_bar.max_volume() == 128);
    SH_CHECK(!sc::parse_options_layout("{\"highlight_frames\": 8}"));
}

SH_TEST(options_menu_tree_replaces_main_list) {
    const sc::MenuStyle st = style();
    const sc::MenuWidgets widgets(st);
    const sc::OptionsLayout l = shipped_layout();
    SH_CHECK(l.tree == "options");
    sc::OptionsScreen screen(l, widgets);
    FakeHost host;
    Recorder rec;

    host.frame(screen, rec);  // enter, then the tree draws (not yet active: the state was EnterMain)
    host.frame(screen, rec);
    SH_CHECK(host.tree_frames >= 1 && host.tree_active);
    SH_CHECK(host.texts.empty());  // the original main list draws nothing
    host.set_state(static_cast<std::int32_t>(sc::OptionsState::Leave));
    host.frame(screen, rec);
    SH_CHECK(!host.tree_active);  // fading out: drawn, not driven
}

SH_TEST(options_menu_entry_and_main_list) {
    const sc::MenuStyle st = style();
    const sc::MenuWidgets widgets(st);
    const sc::OptionsLayout l = layout();
    sc::OptionsScreen screen(l, widgets);
    FakeHost host;
    Recorder rec;

    host.frame(screen, rec);  // enter: the main list, the leave row selected
    SH_CHECK(host.state_ == 1 && host.calls[0] == "prepare" && host.fade_.state == sc::FadeState::InSteps);
    SH_CHECK(screen.main_selected() == 0 && screen.extra_rows() == 6);

    host.frame(screen, rec);
    const Text* heading = find_text(host.texts, "OPTION_\x01\x01\x01\x01\x01S");
    SH_CHECK(heading != nullptr && heading->x == 121 && heading->y == 20);
    const Text* bgm = find_text(host.texts, "BGM_Volume");
    SH_CHECK(bgm != nullptr && bgm->x == 64 && bgm->y == 56 + 6 * 16);
    const Text* sound = find_text(host.texts, "Auto");  // speaker layout 0, right-aligned at 242
    SH_CHECK(sound != nullptr && sound->x == 242 - 4 * 6 && sound->y == 136);
    const Text* vibration = find_text(host.texts, "Off");
    SH_CHECK(vibration != nullptr && vibration->x == 214 && vibration->y == 104);

    // Volume bars: 16 notches of two quads each per bar, BGM 16 = two notches lit.
    int quads = 0;
    for (const sc::MenuPrim& p : rec.prims) {
        if (p.kind == sc::MenuPrim::Kind::FlatQuad) {
            if (quads == 0) {
                SH_CHECK(p.x[0] == 53 && p.y[0] == 41 && p.x[2] == 56 && p.y[1] == 52 && p.rgb[0] == 0xE0E0E0);
            }
            if (quads == 5) {  // notch 2 (holds the volume), outline: partial 64 + 0
                SH_CHECK(p.x[0] == 64 && p.y[0] == 40 && p.rgb[0] == 0x404040);
            }
            quads++;
        }
    }
    SH_CHECK(quads == 3 * 16 * 2);

    // Input waits for the underline's slide (8 frames), then Up wraps to Voice.
    host.ready(screen, rec);
    host.pulsed_.insert(sc::MenuButton::Up);
    host.frame(screen, rec);
    SH_CHECK(screen.main_selected() == 8 && host.moves == 1);
    host.pulsed_.insert(sc::MenuButton::Up);
    host.frame(screen, rec);  // still sliding: ignored
    SH_CHECK(screen.main_selected() == 8);

    // On BGM: Right steps one notch; at the top Right clicks an error.
    for (int i = 0; i < 2; i++) {
        host.ready(screen, rec);
        host.pulsed_.insert(sc::MenuButton::Up);
        host.frame(screen, rec);
    }
    SH_CHECK(screen.main_selected() == 6);
    host.ready(screen, rec);
    host.pulsed_.insert(sc::MenuButton::Right);
    host.frame(screen, rec);
    SH_CHECK(host.values["bgm_volume"] == 24);
    host.values["bgm_volume"] = 128;
    host.clicked_.insert(sc::MenuButton::Right);
    host.frame(screen, rec);
    SH_CHECK(host.errors == 1 && host.values["bgm_volume"] == 128);

    // Cancel jumps to the leave row; a shoulder button switches to the extra list.
    host.clicked_.insert(sc::MenuButton::Cancel);
    host.frame(screen, rec);
    SH_CHECK(screen.main_selected() == 0);
    host.ready(screen, rec);
    host.clicked_.insert(sc::MenuButton::Shoulder);
    host.frame(screen, rec);
    SH_CHECK(host.state_ == 13 && host.fade_.state == sc::FadeState::OutStart);
}

SH_TEST(options_menu_extra_list) {
    const sc::MenuStyle st = style();
    const sc::MenuWidgets widgets(st);
    const sc::OptionsLayout l = layout();
    sc::OptionsScreen screen(l, widgets);
    FakeHost host;
    Recorder rec;
    host.extras = 3;
    host.frame(screen, rec);
    SH_CHECK(screen.extra_rows() == 8);

    host.state_ = 13;  // entering the extra list
    host.fade_done = true;
    host.frame(screen, rec);
    SH_CHECK(host.state_ == 14);
    // Blood color (stored 5 = violet, index 2): Right -> black (11), drawn at 217.
    host.ready(screen, rec);
    host.pulsed_.insert(sc::MenuButton::Down);
    host.frame(screen, rec);
    host.ready(screen, rec);
    host.clicked_.insert(sc::MenuButton::Right);
    host.frame(screen, rec);
    SH_CHECK(host.values["blood_color"] == 11);
    host.frame(screen, rec);
    const Text* black = find_text(host.texts, "Black");
    SH_CHECK(black != nullptr && black->x == 217 && black->y == 80);

    // Bullet adjust: three choices (the host's count), Left from x1 wraps to x3.
    host.pulsed_.insert(sc::MenuButton::Up);
    host.frame(screen, rec);
    SH_CHECK(screen.extra_selected() == 0);
    host.ready(screen, rec);
    host.pulsed_.insert(sc::MenuButton::Up);
    host.frame(screen, rec);
    SH_CHECK(screen.extra_selected() == 7);
    host.ready(screen, rec);
    host.clicked_.insert(sc::MenuButton::Left);
    host.frame(screen, rec);
    SH_CHECK(host.values["bullet_adjust"] == 2);

    // Cancel leaves the extra list with a fade.
    host.clicked_.insert(sc::MenuButton::Cancel);
    host.frame(screen, rec);
    SH_CHECK(host.state_ == 15 && host.fade_.state == sc::FadeState::OutStart);
    host.frame(screen, rec);
    SH_CHECK(host.state_ == 0 && host.fade_.state == sc::FadeState::InSteps);
}
