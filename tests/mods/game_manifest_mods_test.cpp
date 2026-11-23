// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// A mod's game.json: merged over gamedata/game.json like any JSON data file (maps[] by "name"), then read by the
// manifest (engine/manifest/map_manifest.h, MapManifest_LoadText).

#include "engine/manifest/map_manifest.h"
#include "engine/mods/data_merge.h"
#include "framework/mod_fixture.h"
#include "framework/sh_test.h"

#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

namespace fs = std::filesystem;

namespace {

struct Dir {
    fs::path root;
    explicit Dir(const char* name) : root(fs::path(SH_PROJECT_ROOT) / ".tmp" / "game_manifest_mods" / name) {
        std::error_code ec;
        fs::remove_all(root, ec);
        fs::create_directories(root);
    }
    ~Dir() {
        MapManifest_LoadText(nullptr);
        std::error_code ec;
        fs::remove_all(root, ec);
    }
    void write(const std::string& rel, std::string_view text) const {
        fs::create_directories((root / rel).parent_path());
        std::ofstream(root / rel, std::ios::binary) << text;
        sh::test::mod_file_written(root / rel);
    }
};

std::string str(const char* s) { return s != nullptr ? s : "(null)"; }

}  // namespace

SH_TEST(game_manifest_mod_patches_maps_by_name) {
    Dir d("patch");
    d.write("gamedata/game.json", R"({"format": "sh-game-manifest", "version": 1, "start_map": "m1",
        "new_game_map": "m0", "intro_file": "XA/C1_20670",
        "maps": [{"name": "m0", "description": "zero"}, {"name": "m1", "description": "one"},
                 {"name": "m2", "description": "two"}]})");
    d.write("mods/a/mod.json", R"({"id": "a", "version": "1.0.0"})");
    d.write("mods/a/game.json", R"({"new_game_map": "a_start",
        "maps": [{"name": "m1", "description": "one, changed"}, {"name": "a_start", "description": "new"},
                 {"name": "m0", "$remove": true}]})");
    d.write("mods/b/mod.json", R"({"id": "b", "version": "1.0.0", "load_after": ["a"]})");
    d.write("mods/b/game.json", R"({"intro_file": "XA/C2_20670",
        "maps": [{"name": "b_map", "$after": "m1"}]})");

    std::vector<std::string> warnings;
    const auto merged = sh::mods::read_data_file(d.root / "gamedata", d.root / "mods", "game.json", warnings);
    SH_CHECK(static_cast<bool>(merged));
    SH_CHECK(warnings.empty());
    if (!merged) {
        return;
    }
    SH_CHECK(merged.value().from_mods() && merged.value().mods.size() == 2);
    SH_CHECK(MapManifest_LoadText(merged.value().text.c_str()) == 4);
    SH_CHECK(str(MapManifest_Name(0)) == "m1");
    SH_CHECK(str(MapManifest_Description(0)) == "one, changed");
    SH_CHECK(str(MapManifest_Name(1)) == "b_map");
    SH_CHECK(str(MapManifest_Name(2)) == "m2");
    SH_CHECK(str(MapManifest_Name(3)) == "a_start");
    SH_CHECK(str(MapManifest_String("new_game_map")) == "a_start");
    SH_CHECK(str(MapManifest_String("start_map")) == "m1");
    SH_CHECK(str(MapManifest_String("intro_file")) == "XA/C2_20670");
    SH_CHECK(MapManifest_Count() == 4);
}

SH_TEST(game_manifest_without_mods_reads_the_base_unchanged) {
    Dir d("base_only");
    const std::string base = R"({"maps": [{"name": "m0"}], "new_game_map": "m0"})";
    d.write("gamedata/game.json", base);
    std::vector<std::string> warnings;
    const auto merged = sh::mods::read_data_file(d.root / "gamedata", d.root / "mods", "game.json", warnings);
    SH_CHECK(static_cast<bool>(merged) && !merged.value().from_mods() && merged.value().text == base);
}
