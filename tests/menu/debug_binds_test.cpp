// SPDX-License-Identifier: GPL-3.0-or-later
// DEBUG OPTIONS (gamedata/menus/debug.lua): the pure list builders behind its dropdowns, and the
// consistency of the menu data with the natives and the text tables. The natives themselves need
// the running game and are checked at start-up (Pc_DebugBinds_Register logs a missing one).

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "features/cheats/pc_debug_binds.h"
#include "features/cheats/pc_debug_binds_util.h"
#include "framework/sh_test.h"

#ifndef SH_PROJECT_ROOT
#define SH_PROJECT_ROOT "../.."
#endif

namespace {

namespace fs = std::filesystem;
using namespace sh::debug_binds;

std::string read_file(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

std::set<std::string> matches(const std::string& text, const char* pattern) {
    std::set<std::string> out;
    const std::regex re(pattern);
    for (auto it = std::sregex_iterator(text.begin(), text.end(), re); it != std::sregex_iterator(); ++it)
        out.insert((*it)[1].str());
    return out;
}

sh::items::ItemDef item(const char* id, const char* group, unsigned number) {
    sh::items::ItemDef d;
    d.id = id;
    d.group = group;
    d.number = static_cast<std::uint16_t>(number);
    return d;
}

}  // namespace

SH_TEST(DebugBinds_ShortName) {
    SH_CHECK(short_name("silenthill:HealthDrink") == "HealthDrink");
    SH_CHECK(short_name("mymod:Crowbar") == "mymod:Crowbar");
    SH_CHECK(short_name("Plain") == "Plain");
}

SH_TEST(DebugBinds_GiveItemListSortsByGroupThenName) {
    const std::vector<sh::items::ItemDef> items = {
        item("silenthill:Key", "keys", 5),        item("silenthill:Handgun", "guns", 3),
        item("silenthill:Aaa", "other", 9),       item("silenthill:HealthDrink", "health", 1),
        item("silenthill:FirstAidKit", "health", 2), item("silenthill:Ghost", "puzzle", 0),
    };
    const auto list = give_item_choices(items);
    SH_CHECK(list.size() == 5);  // the item without a number cannot be given
    SH_CHECK(list[0].value == "silenthill:FirstAidKit");
    SH_CHECK(list[1].value == "silenthill:HealthDrink");
    SH_CHECK(list[2].value == "silenthill:Handgun");
    SH_CHECK(list[3].value == "silenthill:Key");
    SH_CHECK(list[4].value == "silenthill:Aaa");  // unknown groups come last
    SH_CHECK(list[1].label == "item.HealthDrink.name");
}

SH_TEST(DebugBinds_FlagChoicesKeepOrderAndDropJunk) {
    const auto list = flag_choices({"silenthill:M0S00_CherylFootstepsHeard", "", "silenthill:M0S00_OpeningCutsceneSeen", "silenthill:M0S00_CherylFootstepsHeard", "mymod:Open"});
    SH_CHECK(list.size() == 3);
    SH_CHECK(list[0].value == "silenthill:M0S00_CherylFootstepsHeard" && list[0].label == "M0S00_CherylFootstepsHeard");
    SH_CHECK(list[1].value == "silenthill:M0S00_OpeningCutsceneSeen");
    SH_CHECK(list[2].value == "mymod:Open" && list[2].label == "mymod:Open");
}

SH_TEST(DebugBinds_LuaNativesAreRegisteredNames) {
    const fs::path lua = fs::path(SH_PROJECT_ROOT) / "gamedata" / "menus" / "debug.lua";
    if (!fs::exists(lua)) SH_SKIP("gamedata/menus/debug.lua is not there");
    const std::string text = read_file(lua);
    const std::set<std::string> used = matches(text, "native\\s*=\\s*\"([^\"]+)\"");
    SH_CHECK(!used.empty());
    const std::set<std::string> known(std::begin(kNativeNames), std::end(kNativeNames));
    for (const std::string& name : used) {
        if (!known.count(name)) {
            SH_CHECK(false && "native used in debug.lua but missing from kNativeNames");
        }
    }
    // Every registered native has a row, so none is dead.
    for (const std::string& name : known) SH_CHECK(used.count(name) == 1);
}

SH_TEST(DebugBinds_EveryTextKeyIsTranslated) {
    const fs::path root = fs::path(SH_PROJECT_ROOT) / "gamedata";
    if (!fs::exists(root / "menus" / "debug.lua")) SH_SKIP("gamedata/menus/debug.lua is not there");
    std::set<std::string> keys = matches(read_file(root / "menus" / "debug.lua"), "\"(ui\\.debug\\.[A-Za-z0-9_.]+)\"");
    // The FPS corners come from the native, not from the Lua file.
    for (const char* k : {"off", "top_left", "top_right", "bottom_left", "bottom_right"})
        keys.insert(std::string("ui.debug.fps_") + k);
    SH_CHECK(keys.size() > 40);
    for (const char* lang : {"en", "ru", "pl", "pt"}) {
        const fs::path file = root / "text" / lang / "debug_menu.json";
        SH_CHECK(fs::exists(file));
        const std::set<std::string> present = matches(read_file(file), "\"(ui\\.debug\\.[A-Za-z0-9_.]+)\"\\s*:");
        for (const std::string& key : keys) {
            if (!present.count(key)) SH_CHECK(false && "text key missing in a language");
        }
    }
}

SH_TEST(DebugBinds_SpawnLabels) {
    SH_CHECK(spawn_category_label("Bosses") == "ui.debug.spawn_cat.Bosses");
    SH_CHECK(spawn_category_label("boats:Ferries") == "boats:Ferries");
    SH_CHECK(spawn_character_label("Groaner", "map") == "Groaner (map AI)");
    SH_CHECK(spawn_character_label("Cybil", "fallback") == "Cybil (fallback AI)");
    SH_CHECK(spawn_character_label("Cat", "none") == "Cat");
    SH_CHECK(spawn_ai_choices().size() == 4 && spawn_ai_choices()[2].value == "fallback");
}

// Every category the base characters use (and "Mods") has a name in each language.
SH_TEST(DebugBinds_SpawnCategoriesAreTranslated) {
    const fs::path root = fs::path(SH_PROJECT_ROOT) / "gamedata";
    if (!fs::exists(root / "chara")) SH_SKIP("gamedata/chara is not there");
    std::set<std::string> cats = {"Mods"};
    for (const auto& dir : fs::directory_iterator(root / "chara")) {
        const fs::path file = dir.path() / "character.json";
        if (!fs::exists(file)) continue;
        for (const std::string& c : matches(read_file(file), "\"category\"\\s*:\\s*\"([^\"]+)\"")) cats.insert(c);
    }
    SH_CHECK(cats.size() >= 6);
    for (const char* lang : {"en", "ru", "pl", "pt"}) {
        const std::string text = read_file(root / "text" / lang / "debug_menu.json");
        for (const std::string& c : cats)
            SH_CHECK(text.find("\"" + spawn_category_label(c) + "\"") != std::string::npos);
    }
}
