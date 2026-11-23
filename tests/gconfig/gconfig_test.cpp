#include "framework/sh_test.h"

#include "engine/gconfig/gameplay_params.h"
#include "engine/gconfig/difficulties.h"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>
#include "framework/mod_fixture.h"

namespace fs = std::filesystem;
using sh::gconfig::GameplayLibrary;
using sh::gconfig::DifficultyLibrary;
using sh::gconfig::Grab;

namespace {

fs::path repo_gamedata() { return fs::path(SH_PROJECT_ROOT) / "gamedata"; }

struct Scratch {
    fs::path root;
    explicit Scratch(const char* name) : root(fs::path(SH_PROJECT_ROOT) / ".tmp" / "gconfig" / name) {
        fs::remove_all(root);
        fs::create_directories(root);
    }
    ~Scratch() { std::error_code ec; fs::remove_all(root, ec); }
    void write(const fs::path& relative, std::string_view text) const {
        fs::create_directories(root / relative.parent_path());
        std::ofstream out(root / relative, std::ios::binary);
        out << text;
        out.close();
        sh::test::mod_file_written(root / relative);
    }
};

bool has_warning(const std::vector<std::string>& warnings, std::string_view part) {
    for (const std::string& w : warnings) {
        if (w.find(part) != std::string::npos) {
            return true;
        }
    }
    return false;
}

}  // namespace

// The shipped data equals the original's constants and loads clean.
SH_TEST(gamedata_gameplay_loads_with_original_values) {
    std::vector<std::string> warnings;
    const auto library = GameplayLibrary::load(repo_gamedata(), {}, warnings);
    SH_CHECK(library);
    for (const std::string& warning : warnings) {
        SH_CHECK(warning.empty());
    }
    SH_CHECK(library.value().check_consistency().empty());

    SH_CHECK(library.value().params().max_health == 100.0);
    SH_CHECK(library.value().params().max_saturation == 300.0);
    SH_CHECK(library.value().params().heal_health_drink == 40.0);
    SH_CHECK(library.value().params().heal_first_aid_kit == 80.0);
    SH_CHECK(library.value().params().heal_ampoule == 100.0);

    // Every grab row of the original, by difficulty -1/0/+1.
    using G = Grab;
    struct Row { G grab; int easy, normal, hard; };
    const Row rows[] = {
        {G::OnFloor, 1600, 3200, 4800},   {G::PinnedFront, 800, 1600, 2400},
        {G::PinnedBack, 1200, 2400, 3600}, {G::TorsoFront, 800, 1600, 2400},
        {G::TorsoBack, 1000, 2000, 3000}, {G::LegsFront, 700, 1400, 2100},
        {G::LegsBack, 800, 1600, 2400},   {G::NeckFront, 3600, 7200, 10800},
        {G::NeckBack, 3600, 7200, 10800},
    };
    for (const Row& row : rows) {
        SH_CHECK(library.value().grab_escape(row.grab, -1) == row.easy);
        SH_CHECK(library.value().grab_escape(row.grab, 0) == row.normal);
        SH_CHECK(library.value().grab_escape(row.grab, 1) == row.hard);
    }
}

// A default library (no data) answers the original's counts too - the fallback path.
SH_TEST(defaults_answer_original_counts) {
    const GameplayLibrary library;
    SH_CHECK(library.grab_escape(Grab::OnFloor, -1) == 1600);
    SH_CHECK(library.grab_escape(Grab::OnFloor, 1) == 4800);
    SH_CHECK(library.grab_escape(Grab::NeckFront, 0) == 7200);
    SH_CHECK(library.params().max_health == 100.0);
    // A difficulty past the range clamps to the nearest row.
    SH_CHECK(library.grab_escape(Grab::OnFloor, 99) == 4800);
    SH_CHECK(library.grab_escape(Grab::OnFloor, -99) == 1600);
}

SH_TEST(parse_overrides_and_errors) {
    std::vector<std::string> warnings;
    const auto edited = GameplayLibrary::parse(R"json({
        "player": {"max_health": 250.0},
        "grab_escape": {"on_floor": [100, 200, 400]}
    })json",
                                                warnings);
    SH_CHECK(edited);
    SH_CHECK(edited.value().params().max_health == 250.0);
    SH_CHECK(edited.value().grab_escape(Grab::OnFloor, 0) == 200);
    // An unlisted row warns and keeps the default.
    SH_CHECK(has_warning(warnings, "grab_pinned_front"));
    SH_CHECK(edited.value().grab_escape(Grab::PinnedFront, 0) == 1600);

    SH_CHECK(!GameplayLibrary::parse("{", warnings));
    const auto bad_row = GameplayLibrary::parse(R"json({
        "grab_escape": {"on_floor": [1, 2]}})json",
                                                warnings);
    SH_CHECK(!bad_row);

    const auto bad_health = GameplayLibrary::parse(R"json({
        "player": {"max_health": -5}})json",
                                                   warnings);
    SH_CHECK(bad_health);
    SH_CHECK(!bad_health.value().check_consistency().empty());
}

SH_TEST(mod_replaces_gameplay_file) {
    Scratch dir("mod");
    dir.write("base/config/gameplay.json", "{}");  // all defaults
    dir.write("mods/tuned/mod.json", R"({"id": "tuned", "version": "1.0.0", "gameplay": true})");
    dir.write("mods/tuned/config/gameplay.json", R"json({
        "player": {"max_health": 999.0}})json");

    std::vector<std::string> warnings;
    sh::gconfig::LoadSource source;
    const auto library = GameplayLibrary::load(dir.root / "base", dir.root / "mods", warnings, &source);
    SH_CHECK(library);
    SH_CHECK(source.mod_id == "tuned");
    SH_CHECK(source.mod_gameplay);
    SH_CHECK(!has_warning(warnings, "\"gameplay\": true"));
    SH_CHECK(library.value().params().max_health == 999.0);
}

// Two mods patch config/gameplay.json: both changes apply, and on a field both set the later mod wins.
SH_TEST(two_mods_patch_gameplay_file) {
    Scratch dir("two_mods_gameplay");
    dir.write("mods/a/mod.json", R"({"id": "a", "version": "1.0.0", "gameplay": true, "priority": 0})");
    dir.write("mods/a/config/gameplay.json", R"json({"player": {"max_health": 150.0}})json");
    dir.write("mods/b/mod.json", R"({"id": "b", "version": "1.0.0", "gameplay": true, "priority": 1})");
    dir.write("mods/b/config/gameplay.json",
              R"json({"player": {"max_health": 300.0}, "grab_escape": {"on_floor": [10, 20, 30]}})json");
    std::vector<std::string> warnings;
    sh::gconfig::LoadSource source;
    const auto library = GameplayLibrary::load(repo_gamedata(), dir.root / "mods", warnings, &source);
    SH_CHECK(library);
    if (library) {
        SH_CHECK(library.value().params().max_health == 300.0);
        SH_CHECK(library.value().grab_escape(Grab::OnFloor, 0) == 20);
        SH_CHECK(source.mod_id == "b");
    }
    // Only mod a: its value over the shipped file, every other value from the base.
    std::filesystem::remove_all(dir.root / "mods" / "b");
    const auto only_a = GameplayLibrary::load(repo_gamedata(), dir.root / "mods", warnings, &source);
    const auto base = GameplayLibrary::load(repo_gamedata(), {}, warnings);
    SH_CHECK(only_a && base);
    if (only_a && base) {
        SH_CHECK(only_a.value().params().max_health == 150.0);
        SH_CHECK(only_a.value().grab_escape(Grab::OnFloor, 0) == base.value().grab_escape(Grab::OnFloor, 0));
    }
}

// Two mods each add a difficulty (entries by "label") and one renames nothing: all of them are in the list.
SH_TEST(two_mods_add_difficulties) {
    Scratch dir("two_mods_difficulties");
    dir.write("mods/a/mod.json", R"({"id": "a", "version": "1.0.0", "gameplay": true, "priority": 0})");
    dir.write("mods/a/config/difficulties.json",
              R"json({"difficulties": [{"label": "STORY", "base_difficulty": -1, "$before": "EASY"}]})json");
    dir.write("mods/b/mod.json", R"({"id": "b", "version": "1.0.0", "gameplay": true, "priority": 1})");
    dir.write("mods/b/config/difficulties.json",
              R"json({"difficulties": [{"label": "NIGHTMARE", "base_difficulty": 1},
                                       {"label": "NORMAL", "default": null}, {"label": "HARD", "default": true}]})json");
    std::vector<std::string> warnings;
    const auto library = DifficultyLibrary::load(repo_gamedata(), dir.root / "mods", warnings);
    SH_CHECK(library);
    if (library) {
        SH_CHECK(library.value().count() == 5);
        SH_CHECK(library.value().entry(0).label == "STORY");
        SH_CHECK(library.value().entry(1).label == "EASY");
        SH_CHECK(library.value().entry(4).label == "NIGHTMARE");
        SH_CHECK(library.value().default_index() == 3);
    }
}

// The shipped difficulties equal the original's EASY/NORMAL/HARD and load clean.
SH_TEST(gamedata_difficulties_load_with_original_values) {
    std::vector<std::string> warnings;
    const auto library = DifficultyLibrary::load(SH_PROJECT_ROOT "/gamedata", "", warnings);
    SH_CHECK(library);
    if (library) {
        SH_CHECK(warnings.empty());
        SH_CHECK(library.value().check_consistency().empty());
        SH_CHECK(library.value().count() == 3);
        SH_CHECK(library.value().entry(0).label == "EASY");
        SH_CHECK(library.value().entry(0).base_difficulty == -1);
        SH_CHECK(library.value().entry(1).label == "NORMAL");
        SH_CHECK(library.value().entry(1).base_difficulty == 0);
        SH_CHECK(library.value().default_index() == 1);
        SH_CHECK(library.value().entry(2).label == "HARD");
        SH_CHECK(library.value().entry(2).base_difficulty == 1);
    }
}

// Without data the library answers the original's three.
SH_TEST(difficulties_default_library_is_easy_normal_hard) {
    const DifficultyLibrary library;
    SH_CHECK(library.count() == 3);
    SH_CHECK(library.entry(0).label == "EASY");
    SH_CHECK(library.entry(1).label == "NORMAL");
    SH_CHECK(library.entry(1).is_default);
    SH_CHECK(library.default_index() == 1);
    SH_CHECK(library.entry(2).base_difficulty == 1);
}

// A mod's longer list parses; entries map to base difficulties, the first default wins.
SH_TEST(difficulties_parse_modded_list) {
    std::vector<std::string> warnings;
    const auto library = DifficultyLibrary::parse(
        "{\"difficulties\":[{\"label\":\"NIGHTMARE\",\"base_difficulty\":1},"
        "{\"label\":\"BEGINNER\",\"base_difficulty\":-1,\"default\":true}]}",
        warnings);
    SH_CHECK(library);
    if (library) {
        SH_CHECK(warnings.empty());
        SH_CHECK(library.value().count() == 2);
        SH_CHECK(library.value().entry(0).label == "NIGHTMARE");
        SH_CHECK(library.value().entry(0).base_difficulty == 1);
        SH_CHECK(library.value().default_index() == 1);
        SH_CHECK(library.value().check_consistency().empty());
    }
}

// Out-of-range bases and empty lists are rejected.
SH_TEST(difficulties_reject_bad_data) {
    std::vector<std::string> warnings;
    SH_CHECK(!DifficultyLibrary::parse("{\"difficulties\":[]}", warnings));
    SH_CHECK(!DifficultyLibrary::parse("{\"difficulties\":[{\"label\":\"X\",\"base_difficulty\":2}]}", warnings));
    SH_CHECK(!DifficultyLibrary::parse("{\"difficulties\":[{\"base_difficulty\":0}]}", warnings));
}

// Per-difficulty start maps parse and default to empty (the manifest's new_game_map).
SH_TEST(difficulties_parse_start_map) {
    std::vector<std::string> warnings;
    const auto library = DifficultyLibrary::parse(
        "{\"difficulties\":[{\"label\":\"NEW GAME+\",\"base_difficulty\":1,\"start_map\":\"map7_s00\"},"
        "{\"label\":\"NORMAL\",\"base_difficulty\":0}]}",
        warnings);
    SH_CHECK(library);
    if (library) {
        SH_CHECK(library.value().entry(0).start_map == "map7_s00");
        SH_CHECK(library.value().entry(1).start_map.empty());
    }
    SH_CHECK(!DifficultyLibrary::parse("{\"difficulties\":[{\"label\":\"X\",\"start_map\":\"\"}]}", warnings));
}
