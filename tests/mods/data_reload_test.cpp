// SPDX-License-Identifier: GPL-3.0-or-later
//
// `mods reload` of the parsed data (engine/mods/data_reload.h, engine/ids/game_numbers.h refresh()): which changed
// files apply how, the epoch every cache of parsed data compares with, and the numbers a reload must keep.

#include "framework/sh_test.h"

#include "engine/ids/game_numbers.h"
#include "engine/items/item_library.h"
#include "engine/mods/data_merge.h"
#include "engine/mods/data_reload.h"
#include "framework/mod_fixture.h"

#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace fs = std::filesystem;

namespace {

fs::path repo_gamedata() { return fs::path(SH_PROJECT_ROOT) / "gamedata"; }

struct Scratch {
    fs::path root;
    explicit Scratch(const char* name) : root(fs::path(SH_PROJECT_ROOT) / ".tmp" / "data_reload" / name) {
        fs::remove_all(root);
        fs::create_directories(root);
    }
    ~Scratch() {
        std::error_code ec;
        fs::remove_all(root, ec);
    }
    void write(const fs::path& relative, std::string_view text) const {
        fs::create_directories(root / relative.parent_path());
        std::ofstream(root / relative, std::ios::binary) << text;
        sh::test::mod_file_written(root / relative);
    }
};

bool contains(const std::vector<std::string>& list, std::string_view item) {
    for (const std::string& s : list) {
        if (s == item) {
            return true;
        }
    }
    return false;
}

}  // namespace

SH_TEST(data_reload_sorts_changed_files) {
    SH_CHECK(sh::mods::change_kind("text/en/ui.json") == 0);
    SH_CHECK(sh::mods::change_kind("menus/extra.lua") == 0);
    SH_CHECK(sh::mods::change_kind("scripts/main.lua") == 0);
    SH_CHECK(sh::mods::change_kind("items/Crowbar.json") == 1);
    SH_CHECK(sh::mods::change_kind("weapons/Handgun.json") == 1);
    SH_CHECK(sh::mods::change_kind("config/gameplay.json") == 1);
    SH_CHECK(sh::mods::change_kind("maps/map0_s01/events.lua") == 1);
    SH_CHECK(sh::mods::change_kind("scripts/map_hooks.lua") == 1);
    SH_CHECK(sh::mods::change_kind("convert/textures/CHARA/HERO.png") == 1);
    SH_CHECK(sh::mods::change_kind("shaders/scene.frag") == 2);
    SH_CHECK(sh::mods::change_kind("Config/Main_Frame.json") == 2);

    const sh::mods::Registry::FileStamps before = {
        {"a/items/Crowbar.json", 1}, {"a/text/en/x.json", 2}, {"a/shaders/post.frag", 3}, {"a/game.json", 4}};
    const sh::mods::Registry::FileStamps after = {
        {"a/items/Crowbar.json", 1}, {"a/text/en/x.json", 5}, {"a/shaders/post.frag", 6}, {"a/flags.json", 7}};
    const sh::mods::ReloadChanges c = sh::mods::classify_changes(before, after);
    SH_CHECK(c.live.size() == 1 && contains(c.live, "a/text/en/x.json"));
    SH_CHECK(c.data.size() == 2 && contains(c.data, "a/flags.json") && contains(c.data, "a/game.json"));
    SH_CHECK(c.restart.size() == 1 && contains(c.restart, "a/shaders/post.frag"));
    SH_CHECK(sh::mods::classify_changes(after, after).empty());
}

SH_TEST(data_reload_epoch_marks_caches_stale) {
    std::uint32_t seen = sh::mods::kNeverRead;
    SH_CHECK(sh::mods::data_stale(seen));   // never read: read now
    SH_CHECK(!sh::mods::data_stale(seen));  // read in this epoch
    sh::mods::advance_data_epoch();
    SH_CHECK(sh::mods::data_stale(seen));
    SH_CHECK(!sh::mods::data_stale(seen));
}

// A cache of a merged file reads the edited mod copy again after the epoch moves on.
SH_TEST(data_reload_reads_a_changed_config_file_again) {
    Scratch dir("config");
    dir.write("mods/tuning/mod.json", R"({"id": "tuning", "version": "1.0.0", "gameplay": true})");
    dir.write("mods/tuning/config/gameplay.json", R"({"player": {"max_health": 150}})");
    struct Cache {
        std::uint32_t epoch = sh::mods::kNeverRead;
        std::string text;
        const std::string& get(const fs::path& mods) {
            if (sh::mods::data_stale(epoch)) {
                text = sh::mods::read_data_text(repo_gamedata(), mods, "config/gameplay.json").value();
            }
            return text;
        }
    } cache;
    SH_CHECK(cache.get(dir.root / "mods").find("150") != std::string::npos);
    dir.write("mods/tuning/config/gameplay.json", R"({"player": {"max_health": 175}})");
    SH_CHECK(cache.get(dir.root / "mods").find("150") != std::string::npos);  // same epoch: the cache holds
    sh::mods::advance_data_epoch();
    SH_CHECK(cache.get(dir.root / "mods").find("175") != std::string::npos);
}

// refresh() keeps every number handed out: a new item whose name sorts first takes a new number instead of the
// numbers the inventory already holds; a new flag takes a free bit; base numbers never move.
SH_TEST(data_reload_keeps_the_numbers_handed_out) {
    Scratch dir("numbers");
    dir.write("mods/kit/mod.json", R"({"id": "kit", "version": "1.0.0", "gameplay": true})");
    dir.write("mods/kit/items/Mallet.json", R"({"group": "melee"})");
    dir.write("mods/kit/items/Tonic.json", R"({"group": "health"})");
    dir.write("mods/kit/weapons/Pistol.json", R"({"class": "gun", "item": "Mallet", "clip": 6,
        "attacks": [{"input": "tap", "hit_type": 3, "power": 90, "reach_q12": 40960}]})");
    dir.write("mods/kit/flags.json", R"({"format": "sh-flags", "version": 1, "flags": {"zeta": {}}})");
    sh::ids::configure(repo_gamedata(), dir.root / "mods");
    std::vector<std::string> warnings;
    SH_CHECK(sh::items::ItemLibrary::load(repo_gamedata(), dir.root / "mods", warnings));
    const int mallet = sh::ids::item_number("kit:Mallet");
    const int tonic = sh::ids::item_number("kit:Tonic");
    const int pistol = sh::ids::weapon_number("kit:Pistol");
    const int zeta = sh::ids::flag_number("kit:zeta");
    const int drink = sh::ids::item_number("HealthDrink");
    SH_CHECK(mallet > 0 && tonic > 0 && pistol > 0 && zeta > 0 && drink > 0);

    // New things that sort before the old ones: fresh numbering would hand them the old numbers.
    dir.write("mods/kit/items/Anvil.json", R"({"group": "melee"})");
    dir.write("mods/kit/items/Balm.json", R"({"group": "health"})");
    dir.write("mods/kit/weapons/Arbalest.json", R"({"class": "gun", "item": "Anvil", "clip": 1,
        "attacks": [{"input": "tap", "hit_type": 3, "power": 50, "reach_q12": 40960}]})");
    dir.write("mods/kit/flags.json",
              R"({"format": "sh-flags", "version": 1, "flags": {"alpha": {}, "zeta": {}}})");
    sh::ids::refresh();
    warnings.clear();
    SH_CHECK(sh::items::ItemLibrary::load(repo_gamedata(), dir.root / "mods", warnings));
    SH_CHECK(sh::ids::item_number("kit:Mallet") == mallet);
    SH_CHECK(sh::ids::item_number("kit:Tonic") == tonic);
    SH_CHECK(sh::ids::weapon_number("kit:Pistol") == pistol);
    SH_CHECK(sh::ids::flag_number("kit:zeta") == zeta);
    const int anvil = sh::ids::item_number("kit:Anvil");
    const int balm = sh::ids::item_number("kit:Balm");
    const int arbalest = sh::ids::weapon_number("kit:Arbalest");
    const int alpha = sh::ids::flag_number("kit:alpha");
    SH_CHECK(anvil > 0 && anvil != mallet && anvil != tonic);
    SH_CHECK(balm > 0 && balm != mallet && balm != tonic);
    SH_CHECK(arbalest > 0 && arbalest != pistol);
    SH_CHECK(alpha > 0 && alpha != zeta);
    // The base keeps its numbers.
    SH_CHECK(sh::ids::item_number("HealthDrink") == drink);
    SH_CHECK(sh::ids::weapon_number("Handgun") == 32);
    SH_CHECK(sh::ids::flag_number("M0S00_OpeningCutsceneSeen") == 1);
    sh::ids::configure({}, {});
}
