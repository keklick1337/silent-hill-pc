// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// Short names in mod scripts (rule 1): a name without a namespace from a mod's script
// means the mod's own thing first, then the base game's, then the one mod that has it. The script layer knows the
// calling mod from the environment of the calling function (engine/script/caller_mod.h); the numbering of sh::ids
// takes the same rule through sh::ids::NamespaceScope.

#include "engine/ids/game_numbers.h"
#include "engine/script/script_host.h"
#include "framework/mod_fixture.h"
#include "framework/sh_test.h"

#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

namespace {

struct Scratch {
    fs::path root;
    explicit Scratch(const char* name) : root(fs::path(SH_PROJECT_ROOT) / ".tmp" / "short_names" / name) {
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

// A lookup with rules 2 and 3 of its own (base namespace, then the one namespace that has the name), as the game's.
int lookup(const std::map<std::string, int>& known, const char* name) {
    const std::string n = name != nullptr ? name : "";
    if (const auto it = known.find(n); it != known.end()) {
        return it->second;
    }
    if (n.find(':') != std::string::npos) {
        return -1;
    }
    if (const auto it = known.find("silenthill:" + n); it != known.end()) {
        return it->second;
    }
    int found = -1;
    for (const auto& [id, number] : known) {
        if (id.size() > n.size() && id.ends_with(":" + n)) {
            if (found >= 0) {
                return -1;
            }
            found = number;
        }
    }
    return found;
}

const std::map<std::string, int> kItems{
    {"silenthill:Lantern", 1}, {"lamp:Lantern", 2}, {"other:Lantern", 3}, {"lamp:Crowbar", 4}};
const std::map<std::string, int> kFlags{{"silenthill:SeenSaveScreen", 10}, {"lamp:door", 11}, {"other:door", 12}};
const std::map<std::string, int> kCharas{{"silenthill:Cat", 20}, {"lamp:Cat", 21}};
std::vector<std::pair<std::string, int>> g_given;  // (who, item)
std::vector<int> g_set;                           // flags set, in order

}  // namespace

SH_TEST(script_short_names_prefer_the_calling_mod) {
    Scratch dir("script");
    dir.write("gamedata/scripts/game.lua", R"lua(
        function on_boot() sh.give_item("Lantern", 0) end
    )lua");
    dir.write("mods/lamp/mod.json", R"({"id": "lamp", "version": "1.0.0", "scripts": true})");
    dir.write("mods/lamp/scripts/main.lua", R"lua(
        local function give(name) sh.give_item(name, 1) end
        function on_boot()
            give("Lantern")             -- the mod's own
            give("Crowbar")             -- only the mod has it
            give("silenthill:Lantern")  -- the base game's, by its full name
            give("other:Lantern")
            sh.flag_set("door", true)
            sh.flag_set("SeenSaveScreen", true)
            sh.log("cat " .. tostring(sh.chara_id("Cat")))
            sh.spawn(function() give("Lantern") end)  -- a coroutine of the mod is the mod too
        end
    )lua");
    dir.write("mods/other/mod.json",
              R"({"id": "other", "version": "1.0.0", "scripts": true, "load_after": ["lamp"]})");
    dir.write("mods/other/scripts/main.lua", R"lua(
        function on_boot()
            sh.give_item("Lantern", 2)
            sh.flag_set("door", true)
            sh.log("other cat " .. tostring(sh.chara_id("Cat")))
        end
    )lua");

    g_given.clear();
    g_set.clear();
    std::vector<std::string> lines;
    sh::script::HostConfig c;
    c.gamedata_dir = dir.root / "gamedata";
    c.mods_dir = dir.root / "mods";
    c.log = [&lines](int, const std::string& message) { lines.push_back(message); };
    c.game.item_number = [](const char* name) { return lookup(kItems, name); };
    c.game.flag_number = [](const char* name) { return lookup(kFlags, name); };
    c.game.chara_id_find = [](const char* name) { return lookup(kCharas, name); };
    c.game.flag_set = [](int id, int) { g_set.push_back(id); };
    c.game.give_item = [](int item, int count) { g_given.emplace_back(std::to_string(count), item); };
    sh::script::Host host(std::move(c));
    SH_CHECK(host.load().value());
    host.on_boot();
    host.on_update();
    const auto has = [&lines](std::string_view part) {
        for (const std::string& l : lines) {
            if (l.find(part) != std::string::npos) {
                return true;
            }
        }
        return false;
    };
    // count 0: the base script, 1: mod lamp, 2: mod other.
    const std::vector<std::pair<std::string, int>> expected{{"0", 1}, {"1", 2}, {"1", 4}, {"1", 1},
                                                            {"1", 3}, {"2", 3}, {"1", 2}};
    SH_CHECK(g_given == expected);
    SH_CHECK((g_set == std::vector<int>{11, 10, 12}));
    SH_CHECK(has("cat 21"));
    SH_CHECK(has("other cat 20"));
}

// sh::ids: two mods declare a flag of the same name; the short name is ambiguous (rule 3 finds two) unless a scope
// names the mod, and a base name stays the base's when the mod has none of its own.
SH_TEST(ids_namespace_scope_resolves_short_names) {
    Scratch dir("ids");
    dir.write("mods/lamp/mod.json", R"({"id": "lamp", "version": "1.0.0"})");
    dir.write("mods/lamp/flags.json", R"({"format": "sh-flags", "version": 1, "flags": {"door_open": {}}})");
    dir.write("mods/other/mod.json", R"({"id": "other", "version": "1.0.0"})");
    dir.write("mods/other/flags.json", R"({"format": "sh-flags", "version": 1, "flags": {"door_open": {}}})");
    sh::ids::configure(dir.root / "gamedata", dir.root / "mods");

    const int lamp = sh::ids::flag_number("lamp:door_open");
    const int other = sh::ids::flag_number("other:door_open");
    SH_CHECK(lamp > 0 && other > 0 && lamp != other);
    SH_CHECK(sh::ids::flag_number("door_open") == -1);
    {
        const sh::ids::NamespaceScope scope("lamp");
        SH_CHECK(sh::ids::flag_number("door_open") == lamp);
        SH_CHECK(sh::ids::flag_number("other:door_open") == other);
        SH_CHECK(sh::ids::flag_number("M0S00_OpeningCutsceneSeen") == 1);
        SH_CHECK(sh::ids::item_number("HealthDrink") >= 0);
        {
            const sh::ids::NamespaceScope inner("other");
            SH_CHECK(sh::ids::flag_number("door_open") == other);
        }
        SH_CHECK(sh::ids::flag_number("door_open") == lamp);
    }
    SH_CHECK(sh::ids::flag_number("door_open") == -1);
    sh::ids::configure({}, {});
}
