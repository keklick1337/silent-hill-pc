#include "engine/ids/game_numbers.h"
#include "framework/sh_test.h"

#include "engine/flags/flag_registry.h"
#include "engine/save/save_game.h"
#include "engine/script/script_host.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <regex>
#include <set>
#include <string>
#include <system_error>
#include <vector>
#include "framework/mod_fixture.h"

// The game numbers of the engine (src/engine/ids/game_numbers.h), read from the repository gamedata/.

namespace fs = std::filesystem;
using sh::flags::FlagRegistry;

namespace {

fs::path repo_gamedata() { return fs::path(SH_PROJECT_ROOT) / "gamedata"; }

std::string read_text(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

void write_text(const fs::path& p, const std::string& text) {
    fs::create_directories(p.parent_path());
    std::ofstream(p, std::ios::binary) << text;
    sh::test::mod_file_written(p);
}

fs::path scratch(const char* name) {
    const fs::path dir = fs::temp_directory_path() / (std::string("sh_flags_test_") + name);
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);
    return dir;
}

// A save's 52 words of event flags with the given bits set.
std::array<std::uint32_t, 52> words_with(std::initializer_list<int> bits) {
    std::array<std::uint32_t, 52> w{};
    for (int b : bits) {
        w[static_cast<std::size_t>(b >> 5)] |= 1u << (b & 31);
    }
    return w;
}

bool bit_set(const std::array<std::uint32_t, 52>& w, int b) {
    return ((w[static_cast<std::size_t>(b >> 5)] >> (b & 31)) & 1u) != 0;
}

}  // namespace

// The shipped declarations (gamedata/flags/*.json): names only; the original layout gives each its save bit.
SH_TEST(flags_shipped_registry_names_the_save_bits) {
    std::vector<std::string> warnings;
    auto registry = FlagRegistry::load(repo_gamedata(), {}, warnings);
    SH_CHECK(registry);
    SH_CHECK(warnings.empty());
    SH_CHECK(registry.value().flags().size() > 1000);
    SH_CHECK(registry.value().find("silenthill:M1S03_ShotgunShells0") != nullptr);
    std::set<int> bits;
    for (const sh::flags::FlagInfo& f : registry.value().flags()) {
        const int bit = sh::ids::flag_number(f.id.c_str());
        SH_CHECK(bit > 0);
        SH_CHECK(bits.insert(bit).second);  // one bit per flag
        // A placeholder name is never a flag's name, only a former one.
        SH_CHECK(!std::regex_match(f.id, std::regex(R"(silenthill:Flag\d+)")));
        for (const std::string& a : f.aliases) {
            SH_CHECK(sh::ids::flag_number(a.c_str()) == bit);
        }
    }
    // Every bit of the original layout is declared.
    const std::string order = read_text(repo_gamedata() / "original_order.json");
    SH_CHECK(order.find("\"event_flags\"") != std::string::npos);
    for (int bit = 1; bit <= 1643; ++bit) {
        SH_CHECK(sh::ids::flag_name(bit) != nullptr && registry.value().find(sh::ids::flag_name(bit)) != nullptr);
    }
    SH_CHECK(sh::ids::flag_number("M1S03_ShotgunShells0") == 626);  // EventFlag_M1S03_ShotgunShells0
    SH_CHECK(sh::ids::flag_number("silenthill:M1S03_ShotgunShells0") == 626);
    SH_CHECK(sh::ids::flag_number("MapMark_AltSchool2F_LibraryReserveArrows") == 1311);
    SH_CHECK(sh::ids::flag_number("M1S03_KeyInDrainSeen") == 112);
    SH_CHECK(sh::ids::flag_number("Flag112") == 112);  // its former placeholder
    SH_CHECK(sh::ids::flag_number("NoSuchFlag") == -1);
    SH_CHECK(sh::ids::flag_name(626) != nullptr && std::string(sh::ids::flag_name(626)) == "silenthill:M1S03_ShotgunShells0");
    SH_CHECK(sh::ids::flag_name(0) == nullptr);
    // The paper map marks are the soft-blocking done flags of the event data; nothing else is.
    SH_CHECK(sh::ids::flag_is_paper_map_mark(sh::ids::flag_number("MapMark_OldTown_BachmanRdTopCross")));
    SH_CHECK(!sh::ids::flag_is_paper_map_mark(626));
    // The declarations carry no numbers: every value is an object of strings and lists of strings.
    for (const auto& entry : fs::directory_iterator(repo_gamedata() / "flags")) {
        SH_CHECK(!std::regex_search(read_text(entry.path()), std::regex(R"(\n    "\w+": \{[^}\n]*":\s*-?[0-9])")));
    }
}

// A gamedata of the older layout (one flags.json, no flags/) still declares its flags; a stale flags.json next to
// flags/ is ignored.
SH_TEST(flags_declarations_by_directory) {
    const fs::path root = scratch("dirs");
    write_text(root / "old/flags.json", R"json({"flags": {"A": {"aliases": ["Flag1"]}}})json");
    write_text(root / "new/flags.json", R"json({"flags": {"Stale": {}}})json");
    write_text(root / "new/flags/b.json", R"json({"flags": {"B": {}}})json");
    write_text(root / "new/flags/a.json",
               R"json({"kind": "paper_map_mark", "flags": {"A": {"aliases": ["Flag1"], "old_save_names": ["Moved"]}}})json");
    std::vector<std::string> warnings;
    auto old_layout = FlagRegistry::load(root / "old", {}, warnings);
    SH_CHECK(old_layout && old_layout.value().find("silenthill:A") != nullptr);
    auto layout = FlagRegistry::load(root / "new", {}, warnings);
    SH_CHECK(layout);
    const FlagRegistry& r = layout.value();
    SH_CHECK(r.find("silenthill:Stale") == nullptr);
    SH_CHECK(r.flags().size() == 2 && r.flags()[0].id == "silenthill:A");  // by file name
    SH_CHECK(r.find("silenthill:A")->aliases == std::vector<std::string>{"silenthill:Flag1"});
    SH_CHECK(r.find("silenthill:A")->old_save_names == std::vector<std::string>{"silenthill:Moved"});
    SH_CHECK(r.find("silenthill:A")->paper_map_mark && !r.find("silenthill:B")->paper_map_mark);
    SH_CHECK(warnings.empty());
    fs::remove_all(root);
}

// A mod declares its flags by name; they get bits no base flag uses, the same for the same mods.
SH_TEST(flags_mod_flags_get_free_bits) {
    const fs::path root = scratch("mods");
    write_text(root / "gamedata/flags.json", R"json({"flags": {"A": {}, "B": {"description": "base"}}})json");
    write_text(root / "mods/zmod/mod.json", R"({"id": "zmod", "version": "1.0.0", "priority": 1})");
    write_text(root / "mods/zmod/flags.json",
               R"json({"flags": {"door_opened": {"description": "the door"}, "B": {"description": "patched"}, "bad name": {}}})json");
    write_text(root / "mods/amod/mod.json", R"({"id": "amod", "version": "1.0.0", "priority": 0})");
    write_text(root / "mods/amod/flags.json", R"json({"flags": {"lever": {}}})json");
    std::vector<std::string> warnings;
    auto registry = FlagRegistry::load(root / "gamedata", root / "mods", warnings);
    SH_CHECK(registry);
    const FlagRegistry& r = registry.value();
    SH_CHECK(r.find("zmod:door_opened") != nullptr);
    SH_CHECK(r.find("amod:lever") != nullptr);
    SH_CHECK(r.find("zmod:B") == nullptr);  // a base name in a mod is that base flag
    SH_CHECK(r.find("silenthill:B") != nullptr && r.find("silenthill:B")->description == "patched");
    SH_CHECK(warnings.size() == 1);  // "bad name"

    const std::map<std::string, int, std::less<>> base = {{"silenthill:A", 10}, {"silenthill:B", 15}};
    std::vector<std::string> assign_warnings;
    const auto bits = r.assign_bits(base, 16, assign_warnings);
    SH_CHECK(bits.at("silenthill:A") == 10 && bits.at("silenthill:B") == 15);
    // A mod's flags beyond the layout, sorted by id: amod:lever first; the layout's free bits stay the base game's.
    SH_CHECK(bits.at("amod:lever") == 16);
    SH_CHECK(bits.at("zmod:door_opened") == 17);
    SH_CHECK(assign_warnings.empty());
    std::vector<std::string> again;
    SH_CHECK(r.assign_bits(base, 16, again) == bits);
    fs::remove_all(root);
}

// A big mod declares thousands of flags: every one gets a number past the layout, the base flags keep theirs.
SH_TEST(flags_many_mod_flags_all_assigned) {
    const fs::path root = scratch("many");
    write_text(root / "gamedata/flags.json", R"json({"flags": {"A": {}, "Late": {}}})json");
    write_text(root / "mods/big/mod.json", R"({"id": "big", "version": "1.0.0", "priority": 0})");
    std::string text = R"({"flags": {)";
    constexpr int kCount = 2400;
    for (int i = 0; i < kCount; ++i) text += (i ? ",\"f" : "\"f") + std::to_string(i) + "\": {}";
    text += "}}";
    write_text(root / "mods/big/flags.json", text);
    std::vector<std::string> warnings;
    auto registry = FlagRegistry::load(root / "gamedata", root / "mods", warnings);
    SH_CHECK(registry && warnings.empty());
    if (!registry) return;
    const std::map<std::string, int, std::less<>> base = {{"silenthill:A", 3}};
    std::vector<std::string> assign_warnings;
    const auto bits = registry.value().assign_bits(base, 64, assign_warnings);
    SH_CHECK(assign_warnings.empty() && bits.size() == static_cast<std::size_t>(kCount) + 2);
    SH_CHECK(bits.at("silenthill:A") == 3 && bits.at("silenthill:Late") == 63);  // as without the mod
    std::set<int> seen;
    for (const auto& [id, bit] : bits) {
        SH_CHECK(seen.insert(bit).second);
        if (id.starts_with("big:")) SH_CHECK(bit >= 64);
    }
    fs::remove_all(root);
}

// Saves store the set flags by name; the names win over the numbers, bits without a name stay.
SH_TEST(flags_save_names_round_trip) {
    auto w = words_with({626, 1311, 1650});  // past the last flag: no name
    const std::vector<std::string> names = sh::flags::set_flag_names(w, sh::ids::flag_name);
    SH_CHECK(names.size() == 2);
    SH_CHECK(names[0] == "silenthill:M1S03_ShotgunShells0");

    // An old save (numbers only) read back with its own names: nothing changes.
    std::vector<std::string> unknown;
    auto same = w;
    SH_CHECK(!sh::flags::apply_flag_names(same, names, sh::ids::flag_number, sh::ids::flag_name, unknown));
    SH_CHECK(same == w);

    // The names say 627 instead of 1311: the named bits follow, bit 1650 (unnamed) is kept.
    auto other = w;
    SH_CHECK(sh::flags::apply_flag_names(other, {"M1S03_ShotgunShells0", "M1S03_HandgunBullets0", "gone:flag"},
                                         sh::ids::flag_number, sh::ids::flag_name, unknown));
    SH_CHECK(bit_set(other, 626) && bit_set(other, 627) && !bit_set(other, 1311) && bit_set(other, 1650));
    SH_CHECK(unknown == std::vector<std::string>{"gone:flag"});
}

// A save written with the older names loads into the same bits: a former placeholder is an alias, a name that
// has moved to another flag since is read as the flag it meant then. A save of this build reads every name as is.
SH_TEST(flags_old_name_save_loads) {
    sh::save::NameTable names;
    names.flag_name = sh::ids::flag_name;
    names.flag_number = sh::ids::flag_number;
    names.flag_name_in_old_save = sh::ids::flag_name_in_old_save;
    names.map_number = [](const char*) { return 0; };
    sh::save::SaveGame game;
    game.map = sh::save::Ref::of_number(0);
    game.location = sh::save::Ref::of_number(0);
    game.paper_map = sh::save::Ref::of_number(0);
    game.equipped = sh::save::Ref::of_number(0);
    game.flags = {"silenthill:Flag47", "MapMark_AltHospital3F_LinenRoomLeftArrows", "M1S03_ShotgunShells0"};
    std::string text = sh::save::to_json(game);
    const std::string marker = "\"flag_names\": 2";
    SH_CHECK(text.find(marker) != std::string::npos);
    std::string old_text = text;
    old_text.replace(old_text.find(marker), marker.size(), "\"flag_names_absent\": 0");

    const auto read_bits = [&](const std::string& json) {
        std::set<int> bits;
        auto g = sh::save::from_json(json);
        SH_CHECK(static_cast<bool>(g));
        if (!g) {
            return bits;
        }
        sh::save::Warnings warnings;
        auto live = sh::save::to_live_state(g.value(), names, warnings);
        SH_CHECK(live && warnings.empty());
        if (!live) {
            return bits;
        }
        for (int bit = 1; bit < 52 * 32; ++bit) {
            if (live.value().event_flag(bit)) {
                bits.insert(bit);
            }
        }
        return bits;
    };
    // The older save: MapMark_AltHospital3F_LinenRoomLeftArrows was the name of bit 1620 then.
    SH_CHECK(read_bits(old_text) == (std::set<int>{47, 626, 1620}));
    // This build's save: the name is the mark of bit 1641.
    SH_CHECK(read_bits(text) == (std::set<int>{47, 626, 1641}));
}

// Scripts pass flag names; numbers still work. An unknown name is an error of the call.
SH_TEST(flags_lua_names) {
    static std::map<int, int> s_flags;
    s_flags.clear();
    sh::script::HostConfig config;
    config.game.flag_get = [](int id) { return s_flags.count(id) ? s_flags[id] : 0; };
    config.game.flag_set = [](int id, int v) { s_flags[id] = v; };
    config.game.flag_number = sh::ids::flag_number;
    std::vector<std::string> log;
    config.log = [&](int, const std::string& m) { log.push_back(m); };
    sh::script::Host host(config);
    const fs::path dir = scratch("lua");
    write_text(dir / "s.lua", R"lua(
        function on_boot()
            sh.flag_set("M1S03_ShotgunShells0", true)
            sh.flag_set(627, true)
            ok = sh.flag_get("silenthill:M1S03_ShotgunShells0") and sh.flag_get("M1S03_HandgunBullets0")
            bad = pcall(sh.flag_get, "NoSuchFlag")
            -- A script of the older names: the former placeholder is the same flag.
            sh.flag_set("Flag47", true)
            old_ok = sh.flag_get("M0S01_AirScreamerFightStarted")
        end
    )lua");
    SH_CHECK(host.load_file(dir / "s.lua"));
    host.on_boot();
    SH_CHECK(s_flags[626] == 1 && s_flags[627] == 1);
    SH_CHECK(s_flags[47] == 1);
    fs::remove_all(dir);
}

// The shipped map scripts name every flag, sound and message: no number in their tables.
SH_TEST(flags_map_scripts_use_names) {
    const std::regex table(R"(^local (Flag\d*|Sfx|Msg) = \{)", std::regex::multiline);
    for (const auto& entry : fs::directory_iterator(repo_gamedata() / "maps")) {
        const fs::path script = entry.path() / "events.lua";
        if (!fs::is_regular_file(script)) {
            continue;
        }
        const std::string text = read_text(script);
        SH_CHECK(!std::regex_search(text, table));
    }
}
