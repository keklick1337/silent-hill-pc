// SPDX-License-Identifier: GPL-3.0-or-later
// gamedata/menu/map_screen.json carries every table and constant of the paper map screen: the shipped file against
// the recorded table (tests/data/screens/map_screen.txt), bad files are refused, and a mod file wins.

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

#include "engine/screens/map_screen_data.h"
#include "framework/golden.h"
#include "framework/mod_fixture.h"
#include "framework/sh_test.h"
#include "engine/ids/game_numbers.h"

using namespace sh::screens;

namespace {

const char* const kPageNames[25] = {
    "OtherPlaces",   "OldTown",       "FogCentralTown", "AltCentralTown", "ResortTown",
    "FogSchoolBF",   "FogSchool1F",   "FogSchool2F",    "FogSchoolRF",    "AltSchoolBF",
    "AltSchool1F",   "AltSchool2F",   "AltSchoolRF",    "FogSewer1F",     "FogSewer2F",
    "AltSewer",      "FogHospitalBF", "FogHospital1F",  "FogHospital2F",  "FogHospital3F",
    "AltHospitalBF", "AltHospital1F", "AltHospital2F",  "AltHospital3F",  "PaperMap24"};

constexpr const char* kTable = "screens/map_screen.txt";

int page_index(const std::string& name) {
    for (int i = 0; i < 25; ++i) {
        if (name == kPageNames[i]) {
            return i;
        }
    }
    return -1;
}

int flag_number(const std::string& name) { return sh::ids::flag_number(name.c_str()); }

bool rgb_is(const MapRgb& c, int r, int g, int b) { return c.r == r && c.g == g && c.b == b; }

}  // namespace

SH_TEST(map_screen_json_constants) {
    const auto loaded = load_map_screen(std::filesystem::path(SH_PROJECT_ROOT) / "gamedata", {}, nullptr);
    SH_CHECK(static_cast<bool>(loaded));
    const MapScreenData& d = loaded.value();
    SH_CHECK(d.zoom_step == 196 && d.cursor_divisor == 75);
    SH_CHECK(d.focus_offset[0] == 80 && d.focus_offset[1] == 60);
    SH_CHECK(d.pan_max[0] == 160 && d.pan_max[1] == 120);  // half the screen
    SH_CHECK(d.image_shade == 128 && d.marking_shade == 128);
    SH_CHECK(d.player_arrow.tip == 6 && d.player_arrow.back == 6 && d.player_arrow.half_width == 4);
    SH_CHECK(rgb_is(d.player_arrow.outline, 0x10, 0x10, 0x10));
    SH_CHECK(rgb_is(d.player_arrow.tip_color, 0x00, 0x00, 0xFF));
    SH_CHECK(rgb_is(d.player_arrow.back_color, 0x00, 0xFF, 0x00));
    SH_CHECK(d.page_arrows.tip_y == 216 && d.page_arrows.base_y == 200 && d.page_arrows.half_width == 8);
    SH_CHECK(rgb_is(d.page_arrows.tip_color, 0x10, 0x10, 0xC4));
    SH_CHECK(rgb_is(d.page_arrows.base_color, 0x80, 0x80, 0xC4));
}

// Every page link, anchor cell, atlas sprite and mark of the shipped file equals its recorded row, and the file
// holds nothing the table does not.
SH_TEST(map_screen_json_tables_as_recorded) {
    const auto loaded = load_map_screen(std::filesystem::path(SH_PROJECT_ROOT) / "gamedata", {}, nullptr);
    SH_CHECK(static_cast<bool>(loaded));
    const MapScreenData& d = loaded.value();
    int differences = 0;
    const auto differs = [&](const sh::test::GoldenRow& r, const std::string& got) {
        if (++differences <= 5) sh::test::golden_mismatch(kTable, r, got);
    };

    // Links: every page of the table; a page the file does not name links nowhere.
    const auto links = sh::test::golden_rows(kTable, "link");
    SH_CHECK(links.size() == 24);
    for (const MapPageLink& l : d.links) SH_CHECK(page_index(l.page) >= 0 && page_index(l.page) < 24);
    for (const auto& r : links) {
        std::string up = "-", down = "-";
        for (const MapPageLink& l : d.links) {
            if (l.page == r.str(0)) {
                up = l.up.empty() ? "-" : l.up;
                down = l.down.empty() ? "-" : l.down;
            }
        }
        if (up != r.str(1) || down != r.str(2)) differs(r, up + " " + down);
    }

    // Anchor grids: the bounds, every cell, and nothing outside the bounds.
    const auto grids = sh::test::golden_rows(kTable, "grid");
    SH_CHECK(grids.size() == 3);
    std::size_t cells = 0;
    for (const auto& r : grids) {
        const MapAnchorGrid* g = d.anchor_grid(r.str(0));
        if (g == nullptr || g->first_x != r.num(1) || g->first_z != r.num(2) || g->size_x != r.num(3) ||
            g->size_z != r.num(4)) {
            differs(r, g == nullptr ? "no such grid" : "other bounds");
            continue;
        }
        cells += static_cast<std::size_t>(g->size_x * g->size_z);
        SH_CHECK(!g->at(g->first_x - 1, g->first_z) && !g->at(g->first_x, g->first_z + g->size_z));
    }
    const auto cell_rows = sh::test::golden_rows(kTable, "cell");
    SH_CHECK(cell_rows.size() == cells);
    for (const auto& r : cell_rows) {
        const MapAnchorGrid* g = d.anchor_grid(r.str(0));
        const auto p = g != nullptr ? g->at(static_cast<int>(r.num(1)), static_cast<int>(r.num(2))) : std::nullopt;
        if (!p || (*p)[0] != r.num(3) || (*p)[1] != r.num(4)) {
            differs(r, p ? std::to_string((*p)[0]) + " " + std::to_string((*p)[1]) : "no cell");
        }
    }

    // Marks: each page's atlas and mark count, the sprites of that atlas, and each mark with the game numbers of
    // its two flags (the save bits that show its halves).
    const auto pages = sh::test::golden_rows(kTable, "page");
    SH_CHECK(pages.size() == 23 && d.markings.size() == 23);
    const auto page_marks = [&](const std::string& page) -> const MapPageMarks* {
        for (const MapPageMarks& pm : d.markings) {
            if (pm.page == page) return &pm;
        }
        return nullptr;
    };
    std::size_t sprites = 0, marks = 0;
    for (const auto& r : pages) {
        const MapPageMarks* pm = page_marks(r.str(0));
        const MapMarkingAtlas* atlas = pm != nullptr ? d.atlas(pm->atlas) : nullptr;
        if (atlas == nullptr || pm->atlas != r.str(1) || static_cast<std::int64_t>(pm->marks.size()) != r.num(2)) {
            differs(r, pm == nullptr ? "no such page" : pm->atlas + " " + std::to_string(pm->marks.size()));
            continue;
        }
        sprites += atlas->sprites.size();
        marks += pm->marks.size();
    }
    const auto sprite_rows = sh::test::golden_rows(kTable, "sprite");
    SH_CHECK(sprite_rows.size() == sprites);
    for (const auto& r : sprite_rows) {
        const MapPageMarks* pm = page_marks(r.str(0));
        const MapMarkingAtlas* atlas = pm != nullptr ? d.atlas(pm->atlas) : nullptr;
        const std::size_t n = static_cast<std::size_t>(r.num(1));
        if (atlas == nullptr || n < 1 || n > atlas->sprites.size()) {
            differs(r, "no such sprite");
            continue;
        }
        const MapSprite& sp = atlas->sprites[n - 1];
        // One byte each on the texture page.
        if ((sp.u & 0xFF) != r.num(2) || (sp.v & 0xFF) != r.num(3) || (sp.w & 0xFF) != r.num(4) ||
            (sp.h & 0xFF) != r.num(5)) {
            differs(r, std::to_string(sp.u) + " " + std::to_string(sp.v) + " " + std::to_string(sp.w) + " " +
                           std::to_string(sp.h));
        }
    }
    const auto mark_rows = sh::test::golden_rows(kTable, "mark");
    SH_CHECK(mark_rows.size() == marks);
    for (const auto& r : mark_rows) {
        const MapPageMarks* pm = page_marks(r.str(0));
        const MapMarkingAtlas* atlas = pm != nullptr ? d.atlas(pm->atlas) : nullptr;
        const std::size_t k = static_cast<std::size_t>(r.num(1));
        if (atlas == nullptr || k >= pm->marks.size()) {
            differs(r, "no such mark");
            continue;
        }
        const MapMark& m = pm->marks[k];
        const int count = static_cast<int>(atlas->sprites.size());
        // The position is one signed byte a coordinate; a mark names sprites of its page's atlas only.
        if ((m.x & 0xFF) != (r.num(2) & 0xFF) || (m.y & 0xFF) != (r.num(3) & 0xFF) || m.sprites[0] != r.num(4) ||
            m.sprites[1] != r.num(5) || flag_number(m.flags[0]) != r.num(6) || flag_number(m.flags[1]) != r.num(7) ||
            m.sprites[0] > count || m.sprites[1] > count) {
            differs(r, std::to_string(m.x) + " " + std::to_string(m.y) + " " + std::to_string(m.sprites[0]) + " " +
                           std::to_string(m.sprites[1]) + " " + std::to_string(flag_number(m.flags[0])) + " " +
                           std::to_string(flag_number(m.flags[1])));
        }
    }
    SH_CHECK(differences == 0);
}

// The floors of a building link to each other, and the two flags of a mark follow each other in the save.
SH_TEST(map_screen_json_floors_and_flags) {
    const auto loaded = load_map_screen(std::filesystem::path(SH_PROJECT_ROOT) / "gamedata", {}, nullptr);
    SH_CHECK(static_cast<bool>(loaded));
    const MapScreenData& d = loaded.value();
    const auto link = [&](const char* page) -> const MapPageLink* {
        for (const MapPageLink& l : d.links) {
            if (l.page == page) return &l;
        }
        return nullptr;
    };
    const MapPageLink* first = link("FogSchool1F");
    SH_CHECK(first != nullptr && first->up == "FogSchool2F" && first->down == "FogSchoolBF");
    const MapPageLink* roof = link("FogSchoolRF");
    SH_CHECK(roof != nullptr && roof->up.empty() && roof->down == "FogSchool2F");
    const MapPageLink* sewer = link("FogSewer1F");
    SH_CHECK(sewer != nullptr && sewer->up == "FogSewer2F" && sewer->down.empty());
    SH_CHECK(link("OldTown") == nullptr || (link("OldTown")->up.empty() && link("OldTown")->down.empty()));
    for (const MapPageMarks& pm : d.markings) {
        int previous = 0;
        for (const MapMark& m : pm.marks) {
            const int a = flag_number(m.flags[0]), b = flag_number(m.flags[1]);
            SH_CHECK(b == a + 1 && (previous == 0 || a == previous + 1));
            previous = b;
        }
    }
}

SH_TEST(map_screen_builtin_constants) {
    const MapScreenData d;
    SH_CHECK(d.zoom_step == 196 && d.cursor_divisor == 75 && d.pan_max[0] == 160 && d.focus_offset[1] == 60);
    SH_CHECK(d.player_arrow.tip == 6 && rgb_is(d.player_arrow.back_color, 0, 255, 0));
    SH_CHECK(d.page_arrows.tip_y == 216 && rgb_is(d.page_arrows.base_color, 128, 128, 196));
    SH_CHECK(d.links.empty() && d.anchors.empty() && d.markings.empty());
}

SH_TEST(map_screen_refuses_bad_files) {
    const std::string head = "{\"format\": \"sh-map-screen\", \"version\": 1";
    SH_CHECK(!parse_map_screen("[]"));
    SH_CHECK(!parse_map_screen("{}"));
    SH_CHECK(!parse_map_screen("{\"format\": \"other\"}"));
    SH_CHECK(!parse_map_screen(head + ", \"nope\": 1}"));
    SH_CHECK(!parse_map_screen(head + ", \"zoom\": {\"step\": 0}}"));
    SH_CHECK(!parse_map_screen(head + ", \"zoom\": {\"speed\": 1}}"));
    SH_CHECK(!parse_map_screen(head + ", \"player_arrow\": {\"outline\": [1, 2]}}"));
    SH_CHECK(!parse_map_screen(head + ", \"page_arrows\": {\"tip_color\": [0, 0, 256]}}"));
    SH_CHECK(!parse_map_screen(head + ", \"links\": {\"OldTown\": {\"left\": \"AltSewer\"}}}"));
    SH_CHECK(!parse_map_screen(head + ", \"links\": {\"OldTown\": {\"up\": \"\"}}}"));
    SH_CHECK(!parse_map_screen(head + ", \"player_anchors\": {\"school\": {\"first_cell\": [0, 0], \"cells\": [[[1, 2]], [[1, 2], [3, 4]]]}}}"));
    SH_CHECK(!parse_map_screen(head + ", \"player_anchors\": {\"school\": {\"cells\": [[[1, 2]]]}}}"));
    SH_CHECK(!parse_map_screen(head + ", \"marking_atlases\": {\"a\": [[0, 0, 300, 1]]}}"));
    const std::string atlas = ", \"marking_atlases\": {\"a\": [[0, 0, 8, 8]]}";
    SH_CHECK(!parse_map_screen(head + atlas + ", \"markings\": {\"OldTown\": {\"atlas\": \"b\", \"marks\": [{\"at\": [0, 0], \"sprites\": [1, 0], \"flags\": [\"F\", \"G\"]}]}}}"));
    SH_CHECK(!parse_map_screen(head + atlas + ", \"markings\": {\"OldTown\": {\"atlas\": \"a\", \"marks\": [{\"at\": [0, 0], \"sprites\": [2, 0], \"flags\": [\"F\", \"G\"]}]}}}"));
    SH_CHECK(!parse_map_screen(head + atlas + ", \"markings\": {\"OldTown\": {\"atlas\": \"a\", \"marks\": [{\"at\": [200, 0], \"sprites\": [1, 0], \"flags\": [\"F\", \"G\"]}]}}}"));
    SH_CHECK(!parse_map_screen(head + atlas + ", \"markings\": {\"OldTown\": {\"atlas\": \"a\", \"marks\": [{\"at\": [0, 0], \"sprites\": [1, 0], \"flags\": [\"F\"]}]}}}"));
    SH_CHECK(!parse_map_screen(head + atlas + ", \"markings\": {\"OldTown\": {\"atlas\": \"a\", \"marks\": [{\"at\": [0, 0], \"sprites\": [1, 0]}]}}}"));
    const auto ok = parse_map_screen(head + ", \"_c\": 1, \"zoom\": {\"step\": 100}" + atlas +
                                     ", \"markings\": {\"OldTown\": {\"atlas\": \"a\", "
                                     "\"marks\": [{\"at\": [-3, 4], \"sprites\": [1, 0], \"flags\": [\"F\", \"G\"]}]}}}");
    SH_CHECK(static_cast<bool>(ok));
    SH_CHECK(ok.value().zoom_step == 100 && ok.value().cursor_divisor == 75);
    SH_CHECK(ok.value().markings.size() == 1 && ok.value().markings[0].marks[0].flags[1] == "G" &&
             ok.value().markings[0].marks[0].x == -3);
}

SH_TEST(map_screen_mod_file_patches_the_base) {
    namespace fs = std::filesystem;
    const fs::path root = fs::path(SH_PROJECT_ROOT) / ".tmp" / "screens_test" / "map_screen";
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root / "mods" / "bigarrow" / "menu", ec);
    std::ofstream(root / "mods" / "bigarrow" / "mod.json") << R"({"id": "bigarrow", "version": "1.0.0"})";
    std::ofstream(root / "mods" / "bigarrow" / "menu" / "map_screen.json")
        << "{\"format\": \"sh-map-screen\", \"version\": 1, \"player_arrow\": {\"tip\": 12}}";
    sh::test::enable_mods(root / "mods");
    std::string source;
    const auto loaded = load_map_screen(fs::path(SH_PROJECT_ROOT) / "gamedata", root / "mods", &source);
    SH_CHECK(static_cast<bool>(loaded));
    SH_CHECK(source.find("bigarrow") != std::string::npos);
    // The mod's file is a patch: its arrow tip over the base file, whose markings stay.
    SH_CHECK(loaded.value().player_arrow.tip == 12 && !loaded.value().markings.empty());
    std::ofstream(root / "mods" / "bigarrow" / "menu" / "map_screen.json") << "{\"player_arrow\": 5}";
    SH_CHECK(!load_map_screen(fs::path(SH_PROJECT_ROOT) / "gamedata", root / "mods", nullptr));
    fs::remove_all(root, ec);
}

SH_TEST(map_screen_two_mods_stack) {
    namespace fs = std::filesystem;
    const fs::path root = fs::path(SH_PROJECT_ROOT) / ".tmp" / "screens_test" / "map_screen_two";
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root / "mods" / "a" / "menu", ec);
    fs::create_directories(root / "mods" / "b" / "menu", ec);
    std::ofstream(root / "mods" / "a" / "mod.json") << R"({"id": "a", "version": "1.0.0", "priority": 0})";
    std::ofstream(root / "mods" / "a" / "menu" / "map_screen.json")
        << R"({"player_arrow": {"tip": 12}, "zoom": {"step": 50}})";
    std::ofstream(root / "mods" / "b" / "mod.json") << R"({"id": "b", "version": "1.0.0", "priority": 1})";
    std::ofstream(root / "mods" / "b" / "menu" / "map_screen.json") << R"({"player_arrow": {"tip": 14}})";
    sh::test::enable_mods(root / "mods");
    const auto base = load_map_screen(fs::path(SH_PROJECT_ROOT) / "gamedata", "", nullptr);
    const auto both = load_map_screen(fs::path(SH_PROJECT_ROOT) / "gamedata", root / "mods", nullptr);
    SH_CHECK(base && both);
    if (base && both) {
        SH_CHECK(both.value().player_arrow.tip == 14);  // both set it: b loads later
        SH_CHECK(both.value().zoom_step == 50);         // only a
        SH_CHECK(both.value().cursor_divisor == base.value().cursor_divisor);
        SH_CHECK(both.value().markings.size() == base.value().markings.size());
    }
    fs::remove_all(root, ec);
}
