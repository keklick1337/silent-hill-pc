// SPDX-License-Identifier: GPL-3.0-or-later
// gamedata/menu/item_screens.json carries the constants of the item screen's frames and of its
// unavailable-map text, bad files are refused, and a mod file wins.

#include <filesystem>
#include <fstream>
#include <string>

#include "engine/screens/item_screen_style.h"
#include "framework/mod_fixture.h"
#include "framework/sh_test.h"

using namespace sh::screens;

namespace {

bool is_original(const ItemScreenStyle& s) {
    // setXY4(-160, -224, -160, -222, 160, -224, 160, -222), setRGB0(0, 0, 0).
    return s.rule_x0 == -160 && s.rule_x1 == 160 && s.rule_y0 == -224 && s.rule_y1 == -222 &&
           s.rule_color[0] == 0 && s.rule_color[1] == 0 && s.rule_color[2] == 0 &&
           // Gfx_StringSetPosition(30, 232), StringColorId_White, the two USA strings.
           s.notice_x == 30 && s.notice_y == 232 && s.notice_color == 7 &&
           s.notice_too_dark == "Too_dark_to_look_at\n\t\tthe_map_here." &&
           s.notice_no_map == "I_don't_have_the_map\n\t\tfor_this_place.";
}

}  // namespace

SH_TEST(item_screen_style_json_matches_original) {
    const auto loaded = load_item_screen_style(std::filesystem::path(SH_PROJECT_ROOT) / "gamedata", {}, nullptr);
    SH_CHECK(static_cast<bool>(loaded));
    SH_CHECK(is_original(loaded.value()));
    SH_CHECK(is_original(ItemScreenStyle{}));
}

SH_TEST(item_screen_style_refuses_bad_files) {
    SH_CHECK(!parse_item_screen_style("[]"));
    SH_CHECK(!parse_item_screen_style("{\"nope\": 1}"));
    SH_CHECK(!parse_item_screen_style("{\"format\": \"x\"}"));
    SH_CHECK(!parse_item_screen_style("{\"rule_x0\": 1.5}"));
    SH_CHECK(!parse_item_screen_style("{\"rule_x0\": 5000}"));
    SH_CHECK(!parse_item_screen_style("{\"rule_color\": [0, 0]}"));
    SH_CHECK(!parse_item_screen_style("{\"rule_color\": [0, 0, 256]}"));
    SH_CHECK(!parse_item_screen_style("{\"notice_color\": \"pink\"}"));
    SH_CHECK(!parse_item_screen_style("{\"notice_no_map\": \"\"}"));
    const auto ok = parse_item_screen_style("{\"_c\": 1, \"notice_x\": 10, \"notice_color\": \"red\"}");
    SH_CHECK(static_cast<bool>(ok));
    SH_CHECK(ok.value().notice_x == 10 && ok.value().notice_color == 4 && ok.value().notice_y == 232);
}

SH_TEST(item_screen_style_mod_file_wins) {
    namespace fs = std::filesystem;
    const fs::path root = fs::path(SH_PROJECT_ROOT) / ".tmp" / "screens_test" / "item_screens";
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root / "mods" / "lownote" / "menu", ec);
    std::ofstream(root / "mods" / "lownote" / "mod.json") << R"({"id": "lownote", "version": "1.0.0"})";
    std::ofstream(root / "mods" / "lownote" / "menu" / "item_screens.json") << "{\"notice_y\": 200}";
    sh::test::enable_mods(root / "mods");
    std::string source;
    const auto loaded = load_item_screen_style(fs::path(SH_PROJECT_ROOT) / "gamedata", root / "mods", &source);
    SH_CHECK(static_cast<bool>(loaded));
    SH_CHECK(source.find("lownote") != std::string::npos);
    SH_CHECK(loaded.value().notice_y == 200 && loaded.value().notice_x == 30);
    std::ofstream(root / "mods" / "lownote" / "menu" / "item_screens.json") << "{\"notice_y\": \"low\"}";
    SH_CHECK(!load_item_screen_style(fs::path(SH_PROJECT_ROOT) / "gamedata", root / "mods", nullptr));
    fs::remove_all(root, ec);
}
