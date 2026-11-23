// The options' brightness screen (src/engine/screens/brightness_panel.h) against the
// shipped layout: row selection, value steps and clamps, the leave fade, the text and
// arrow positions (hand-computed from the percent layout).

#include "engine/screens/brightness_panel.h"

#include "framework/sh_test.h"

#include <fstream>
#include <iterator>
#include <map>
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

class FakeHost final : public sc::BrightnessHost {
public:
    std::map<std::string, float> values{{"brightness", 1.0f}, {"contrast", 2.0f}, {"saturation", 1.0f}};
    std::int32_t classic = 3;
    std::int32_t steps[3] = {0, 2, 0};
    sc::Fade fade_{sc::FadeState::None, false};
    bool up = false, down = false, left = false, right = false, enter = false, held_left = false;
    std::vector<std::string> labels;
    std::vector<int> label_y;
    std::vector<int> numbers;
    int sfx = 0;
    bool left_called = false;

    std::int32_t screen_width() override { return 320; }
    std::int32_t screen_height() override { return 240; }
    bool pulsed(sc::MenuDir d) override {
        return (d == sc::MenuDir::Up && up) || (d == sc::MenuDir::Down && down) ||
               (d == sc::MenuDir::Left && left) || (d == sc::MenuDir::Right && right);
    }
    bool held(sc::MenuDir d) override { return d == sc::MenuDir::Left && held_left; }
    bool clicked_enter() override { return enter; }
    bool clicked_cancel() override { return false; }
    bool mouse_ui_position(std::int32_t&, std::int32_t&) override { return false; }
    std::int32_t mouse_wheel_step() override { return 0; }
    bool mouse_left_clicked() override { return false; }
    bool mouse_right_clicked() override { return false; }
    void inject_cancel() override {}
    void show_mouse_cursor() override {}
    float setting(const std::string& n) override { return values[n]; }
    float live_setting(const std::string& n) override { return values[n]; }
    void set_setting(const std::string& n, float v) override { values[n] = v; }
    std::int32_t level(const std::string&) override { return classic; }
    void set_level(const std::string&, std::int32_t v) override { classic = v; }
    void play_sfx(sc::MenuSfx) override { sfx++; }
    std::int32_t step(int l) override { return steps[l]; }
    void set_step(int l, std::int32_t v) override { steps[l] = v; }
    sc::Fade fade() override { return fade_; }
    void set_fade(sc::Fade f) override { fade_ = f; }
    void leave() override { left_called = true; }
    void show_reference_bar(bool) override {}
    void draw_background() override {}
    void draw_level_lines() override {}
    void draw_label(std::int32_t, std::int32_t y, bool, const std::string& t, std::int32_t) override {
        labels.push_back(t);
        label_y.push_back(y);
    }
    void draw_number(std::int32_t, std::int32_t v) override { numbers.push_back(v); }
    std::int32_t frame_counter() override { return 0; }
};

}  // namespace

SH_TEST(brightness_panel_rows_values_and_arrows) {
    auto layout = sc::parse_brightness_layout(read("/gamedata/menu/brightness.json"));
    SH_CHECK(layout && layout.value().rows.size() == 4);
    auto style = sc::parse_menu_style(read("/gamedata/menu/menu_style.json"));
    SH_CHECK(style);
    sc::MenuWidgets widgets(style.value());
    sc::BrightnessPanel panel(layout.value(), widgets);
    FakeHost host;
    Recorder rec;

    host.held_left = true;
    panel.update(host, rec);
    SH_CHECK(host.labels.size() == 4 && host.labels[0] == "BRIGHTNESS_");
    // 40 % and 48 % of 240 lines, truncated.
    SH_CHECK(host.label_y[0] == 96 && host.label_y[1] == 115 && host.label_y[3] == 153);
    SH_CHECK(host.numbers[0] == 100 && host.numbers[1] == 200 && host.numbers[3] == 3);
    // Two flashing arrows and the held side's outline, centred at 96 - 107.
    SH_CHECK(rec.prims.size() == 3);
    SH_CHECK(rec.prims[0].x[0] == 18 && rec.prims[0].y[0] == -11 && rec.prims[0].y[1] == -19);
    SH_CHECK(rec.prims[2].x[0] == 17 && rec.prims[2].y[1] == -21);

    // Contrast is at its maximum: Right clamps; Up from row 0 wraps to the level row.
    host.down = true;
    panel.update(host, rec);
    host.down = false;
    host.right = true;
    panel.update(host, rec);
    SH_CHECK(panel.selected_row() == 1 && host.values["contrast"] == 2.0f);
    host.right = false;
    host.up = true;
    panel.update(host, rec);
    panel.update(host, rec);
    host.up = false;
    SH_CHECK(panel.selected_row() == 3);
    host.left = true;
    panel.update(host, rec);
    SH_CHECK(host.classic == 2);
    host.left = false;

    host.enter = true;
    panel.update(host, rec);
    SH_CHECK(host.steps[1] == 3 && host.fade_.state == sc::FadeState::OutStart);
    host.enter = false;
    host.fade_ = {sc::FadeState::OutComplete, false};
    panel.update(host, rec);
    SH_CHECK(host.left_called && host.fade_.state == sc::FadeState::InStart);
}
