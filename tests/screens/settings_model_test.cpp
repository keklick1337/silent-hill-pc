// SPDX-License-Identifier: GPL-3.0-or-later
// The settings model (src/engine/screens/settings_model.h) against the shipped data: settings by key, value
// labels, cycling, sliders, window sizes and shapes, the reset to defaults, the quick settings' labels and steps,
// and what the data file may hold.

#include "engine/screens/settings_model.h"

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

class FakeHost final : public sc::SettingsHost {
public:
    std::map<std::string, std::int32_t> ints{{"fullscreen", 1}, {"crosshair", 0}, {"dream_blur", 1}};
    std::map<std::string, float> floats{{"fog_strength", 1.1f}};
    std::map<std::string, std::string> saved;
    std::vector<std::string> echoes;
    std::int32_t w = 1280, h = 720;
    int moves = 0, errors = 0;

    void play_sfx(sc::MenuSfx s) override { moves += s == sc::MenuSfx::Move; }
    void play_sound(sc::MenuSfx s) override { errors += s == sc::MenuSfx::Error; }
    std::int32_t int_setting(const std::string& k) override { return ints[k]; }
    void set_int_setting(const std::string& k, std::int32_t v) override {
        ints[k] = v;
        saved[k] = std::to_string(v);
    }
    float float_setting(const std::string& k) override { return floats[k]; }
    void set_float_setting(const std::string& k, float v, const std::string* text) override {
        floats[k] = v;
        if (text != nullptr) saved[k] = *text;
    }
    std::int32_t default_int(const std::string&) override { return 0; }
    float default_float(const std::string&) override { return 0.5f; }
    void window_size(std::int32_t& a, std::int32_t& b) override { a = w; b = h; }
    void set_window_size(std::int32_t a, std::int32_t b) override { w = a; h = b; }
    std::int32_t display_w = 0, display_h = 0;
    void display_size(std::int32_t& a, std::int32_t& b) override { a = display_w; b = display_h; }
    std::string start_map() override { return ""; }
    std::int32_t map_count() override { return 3; }
    std::int32_t map_index(const std::string&) override { return -1; }
    std::string map_name(std::int32_t i) override { return "m" + std::to_string(i); }
    std::string map_description(std::int32_t) override { return ""; }
    void set_start_map(const std::string& n) override { saved["map"] = n; }
    std::string translate(sc::LabelTable, const std::string& t) override { return t; }
    std::string translate_rows(const std::string& n) override { return n + " rows"; }
    void echo(const std::string& t) override { echoes.push_back(t); }
    void log_view_change(const std::string&, const std::string&) override {}
};

sc::SettingsData shipped() {
    sh::Result<sc::SettingsData> l = sc::parse_settings(read("/gamedata/menu/settings.json"));
    SH_CHECK(bool(l));
    return l ? l.value() : sc::SettingsData{};
}

}  // namespace

SH_TEST(settings_data_parses) {
    const sc::SettingsData l = shipped();
    SH_CHECK(l.rows.size() == 29 && l.rows[0].kind == sc::SettingRow::Kind::Resolution);
    SH_CHECK(l.rows[0].setting == "resolution" && !l.rows[0].realtime && l.rows[1].keep_on_reset);
    SH_CHECK(l.resolutions.size() == 36 && l.aspects.size() == 7 && l.quick.size() == 11);
    SH_CHECK(l.default_map == "map0_s00" && !l.reset_done.empty() && !l.restart_notice.empty());
}

SH_TEST(settings_data_rejects_what_it_does_not_know) {
    const std::string head = "{\"reset_done\": \"a\", \"restart_notice\": \"b\", \"default_map\": \"m\", "
                             "\"resolutions\": [[640, 480]], \"quick\": {}, ";
    const auto with_rows = [&head](const std::string& rows) { return sc::parse_settings(head + "\"rows\": " + rows + "}"); };
    SH_CHECK(bool(with_rows("[{\"label\": \"Size\", \"kind\": \"resolution\"}]")));
    SH_CHECK(bool(with_rows("[{\"label\": \"Map\", \"kind\": \"map\", \"setting\": \"map\"}]")));
    SH_CHECK(bool(with_rows(
        "[{\"label\": \"S\", \"kind\": \"slider\", \"setting\": \"s\", \"min\": 0, \"max\": 1, \"step\": 0.5}]")));
    // Row kinds the page does not have are refused: actions, geometry, pages.
    SH_CHECK(!with_rows("[{\"label\": \"Back\", \"kind\": \"back\"}]"));
    SH_CHECK(!with_rows("[{\"label\": \"Next_Page\", \"kind\": \"next\"}]"));
    SH_CHECK(!with_rows("[]"));
    SH_CHECK(!with_rows("[{\"label\": \"C\", \"kind\": \"choice\", \"values\": [0, 1]}]"));  // no setting
    SH_CHECK(!with_rows("[{\"label\": \"C\", \"kind\": \"choice\", \"setting\": \"c\", \"values\": [0, 1], "
                        "\"labels\": [\"Off\"]}]"));
    SH_CHECK(!with_rows("[{\"label\": \"S\", \"kind\": \"slider\", \"setting\": \"s\", \"min\": 1, \"max\": 0, "
                        "\"step\": 0.5}]"));
    SH_CHECK(!sc::parse_settings(head + "\"pages\": []}"));
    SH_CHECK(!sc::parse_settings(head + "\"rows\": [{\"label\": \"Size\", \"kind\": \"resolution\"}], "
                                        "\"heading\": \"x\"}"));
    SH_CHECK(!sc::parse_settings("{\"heading\": \"x\"}"));
}

SH_TEST(settings_reset_leaves_the_kept_rows_and_the_ranges) {
    const sc::SettingsData l = shipped();
    const sc::SettingsModel model(l);
    FakeHost host;
    host.ints["fullscreen"] = 2;
    host.ints["vsync"] = 1;
    host.ints["music_volume"] = 64;
    host.ints["hero_light"] = 3;
    host.ints["disable_dpad_movement"] = 1;
    host.w = 800;
    host.h = 600;
    model.reset_all(host);
    SH_CHECK(host.ints["fullscreen"] == 2 && host.w == 800 && host.h == 600);  // window mode and size stay
    SH_CHECK(host.ints["vsync"] == 0 && host.saved["mouse_sensitivity"] == "0.500");
    SH_CHECK(host.ints["music_volume"] == 64);  // a volume is the game's own audio setting
    SH_CHECK(host.ints["hero_light"] == 0 && host.ints["disable_dpad_movement"] == 0);  // cycles and toggles
    SH_CHECK(host.saved.count("map") == 0 && host.echoes.back() == l.reset_done);
}

SH_TEST(settings_rows) {
    const sc::SettingsData l = shipped();
    const sc::SettingsModel pc(l);
    FakeHost host;

    const sc::SettingRow* win = pc.find("fullscreen");
    SH_CHECK(win != nullptr && pc.value_label(*win, host) == "Fullscreen");
    pc.adjust(*win, 1, host);
    SH_CHECK(host.ints["fullscreen"] == 2 && host.echoes.back() == "Window_Mode: Borderless");

    const sc::SettingRow* msaa = pc.find("msaa");
    pc.adjust(*msaa, -1, host);  // Off wraps to 8x; the scene target is rebuilt at once, so no restart notice
    SH_CHECK(host.ints["msaa"] == 8 && host.echoes.size() == 2 && host.echoes.back() == "Antialiasing: 8x");

    const sc::SettingRow* fog = pc.find("fog_strength");
    pc.adjust(*fog, 1, host);
    SH_CHECK(host.saved["fog_strength"] == "1.150" && pc.value_label(*fog, host) == "1.15");

    // The Resolution row steps through the sizes of the current shape only.
    const sc::SettingRow& res = l.rows[0];
    SH_CHECK(pc.value_label(res, host) == "1280x720" && pc.aspect_label(host) == "16:9");
    pc.adjust(res, -1, host);
    SH_CHECK(host.w == 5120 && host.h == 2880 && host.echoes.back() == "Resolution: 5120x2880");
    pc.adjust(res, 1, host);
    pc.adjust(res, 1, host);
    SH_CHECK(host.w == 1366 && host.h == 768 && pc.aspect_label(host) == "16:9");

    // The Aspect Ratio row moves to the next shape and takes its size nearest in height.
    pc.adjust_aspect(1, host);
    SH_CHECK(host.w == 2560 && host.h == 1080 && pc.aspect_label(host) == "21:9");
    pc.adjust_aspect(-1, host);
    SH_CHECK(host.w == 1920 && host.h == 1080 && pc.aspect_label(host) == "16:9");
    host.w = 1024;
    host.h = 768;
    SH_CHECK(pc.aspect_label(host) == "4:3");
    pc.adjust_aspect(-1, host);  // wraps to the widest
    SH_CHECK(host.w == 3840 && host.h == 1080 && pc.aspect_label(host) == "32:9");

    // A window resized by hand has no listed shape: the row offers every size, from its neighbours on.
    host.w = 1000;
    host.h = 700;
    SH_CHECK(pc.value_label(res, host) == "1000x700" && pc.aspect_label(host).empty());
    pc.adjust(res, 1, host);
    SH_CHECK(host.w == 1024 && host.h == 768);
    host.w = 1000;
    host.h = 700;
    pc.adjust(res, -1, host);
    SH_CHECK(host.w == 800 && host.h == 600);

    // The display's own mode is offered with the sizes of its shape.
    host.display_w = 1707;
    host.display_h = 960;
    const auto wide = pc.resolutions_of(pc.aspect_of(16, 9), host);
    SH_CHECK(wide.size() == 9 && wide[3].first == 1707 && wide[3].second == 960);
    SH_CHECK(pc.resolutions_of(pc.aspect_of(4, 3), host).size() == 9 && pc.resolutions_of(-1, host).size() == 37);
    SH_CHECK(pc.aspect_labels().size() == 7 && pc.aspect_labels()[0] == "4:3");

    SH_CHECK(pc.find("resolution") == &res);  // the engine menu's setting.resolution
    SH_CHECK(pc.find("fps_cap") != nullptr && pc.find("map") == nullptr && pc.find("minimap") == nullptr);

    host.ints["fullscreen"] = 1;
    pc.reset_all(host);
    SH_CHECK(host.ints["fullscreen"] == 1 && host.ints["msaa"] == 0 && host.saved["fog_strength"] == "0.500");
}

SH_TEST(settings_quick) {
    const sc::SettingsData l = shipped();
    const sc::SettingsModel pc(l);
    FakeHost host;

    const sc::QuickSetting* shadow = pc.quick("shadow_resolution");
    host.ints["shadow_resolution"] = 333;  // unknown: steps from the default index
    pc.quick_adjust(*shadow, 1, host);
    SH_CHECK(host.ints["shadow_resolution"] == 2048 && pc.quick_label(*shadow, host) == "2048 x 2048");

    const sc::QuickSetting* style_row = pc.quick("crosshair_style");
    host.ints["crosshair_style"] = 7;
    SH_CHECK(pc.quick_label(*style_row, host) == "Dashes");  // the Crosshair row above says whether it shows

    const sc::QuickSetting* hero = pc.quick("hero_light");
    host.ints["hero_light"] = 3;
    pc.quick_adjust(*hero, 1, host);
    SH_CHECK(host.errors == 1 && host.ints["hero_light"] == 3 && pc.quick_label(*hero, host) == "High");

    const sc::QuickSetting* gap = pc.quick("cutscene_line_gap_ms");
    host.ints["cutscene_line_gap_ms"] = 300;
    SH_CHECK(pc.quick_label(*gap, host) == "Normal");
    pc.quick_adjust(*gap, 1, host);
    SH_CHECK(host.ints["cutscene_line_gap_ms"] == 600);

    SH_CHECK(pc.quick("aspect_trim") == nullptr && pc.quick("dialog_pause") == nullptr);

    const sc::QuickSetting* music = pc.quick("music_volume");
    host.ints["music_volume"] = 16;
    SH_CHECK(pc.quick_label(*music, host) == "2 / 16");

    const sc::QuickSetting* reload = pc.quick("auto_reload");
    host.ints["auto_reload"] = 2;
    SH_CHECK(pc.quick_label(*reload, host) == "Original");
}
