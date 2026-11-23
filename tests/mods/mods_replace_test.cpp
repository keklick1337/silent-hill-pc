// mod.json "replace": the layered Lua files a mod replaces whole, the conflict of two mods replacing one path,
// and the registry report line.
#include "engine/mods/load_order.h"
#include "engine/mods/manifest.h"
#include "engine/mods/mods.h"
#include "engine/mods/registry.h"
#include "framework/sh_test.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>
#include "framework/mod_fixture.h"

namespace fs = std::filesystem;
using namespace sh::mods;

namespace {

Candidate mod(std::string id, std::vector<std::string> replace = {}) {
    Candidate c;
    c.manifest.id = id;
    c.manifest.name = id;
    c.manifest.version = Version{1, 0, 0, {}};
    c.manifest.scripts = true;
    c.manifest.replace = std::move(replace);
    c.folder = id;
    return c;
}

bool has(const std::vector<std::string>& lines, std::string_view part) {
    return std::any_of(lines.begin(), lines.end(),
                       [&](const std::string& l) { return l.find(part) != std::string::npos; });
}

std::string order_of(const Plan& plan) {
    std::string out;
    for (const ResolvedMod* m : plan.active()) {
        out += (out.empty() ? "" : ",") + m->candidate.manifest.id;
    }
    return out;
}

struct Scratch {
    fs::path root;
    explicit Scratch(const char* name) : root(fs::path(SH_PROJECT_ROOT) / ".tmp" / "mods_replace" / name) {
        fs::remove_all(root);
        fs::create_directories(root);
    }
    ~Scratch() {
        std::error_code ec;
        fs::remove_all(root, ec);
    }
    void write(const std::string& relative, std::string_view text) const {
        fs::create_directories((root / relative).parent_path());
        std::ofstream(root / relative, std::ios::binary) << text;
        sh::test::mod_file_written(root / relative);
    }
};

// The mods switched on in this order (new mods start off).
UserState all_on(std::initializer_list<const char*> ids) {
    UserState user;
    for (const char* id : ids) {
        user.mods.push_back({id, true});
    }
    return user;
}

}  // namespace

SH_TEST(mods_manifest_replace_list) {
    const ManifestResult r = parse_manifest_json(
        R"({"id": "x", "version": "1.0.0", "scripts": true,
            "replace": ["maps/map0_s01/events.lua", "./chara\\Groaner\\behavior.lua", ""]})",
        "x");
    SH_CHECK(r.errors.empty());
    SH_CHECK(r.manifest.has_value());
    SH_CHECK((r.manifest->replace ==
              std::vector<std::string>{"maps/map0_s01/events.lua", "chara/Groaner/behavior.lua"}));
    SH_CHECK(write_manifest_json(*r.manifest).find("\"replace\"") != std::string::npos);
    SH_CHECK(!parse_manifest_json(R"({"id": "x", "version": "1", "replace": [1]})", "x").manifest);
}

SH_TEST(mods_replace_conflict_switches_the_later_off) {
    Candidate dep = mod("d");
    dep.manifest.dependencies = {ModRef{"b", VersionRange::parse("*").value()}};
    const Plan plan = resolve_load_order(
        {mod("a", {"maps/map0_s01/events.lua"}), mod("b", {"MAPS/map0_s01/Events.lua", "maps/x/hooks.lua"}),
         mod("c", {"maps/x/hooks.lua"}), dep},
        all_on({"a", "b", "c", "d"}), {});
    // b loads after a and replaces the same file: b goes; c replaces what only b also replaced, so c stays.
    SH_CHECK(order_of(plan) == "a,c");
    const ResolvedMod* b = plan.find("b");
    SH_CHECK(b != nullptr && b->state == ModState::Error);
    SH_CHECK(has(b->problems, "replaces MAPS/map0_s01/Events.lua which mod 'a' also replaces"));
    SH_CHECK(plan.find("d")->state == ModState::Error);  // its dependency is gone
    SH_CHECK(plan.find("c")->load_index == 1);

    // Without the overlap both load.
    const Plan apart = resolve_load_order({mod("a", {"maps/a/events.lua"}), mod("b", {"maps/b/events.lua"})}, all_on({"a", "b"}), {});
    SH_CHECK(order_of(apart) == "a,b");
}

SH_TEST(mods_replace_layers_and_report) {
    Scratch dir("layers");
    dir.write("gamedata/maps/m/events.lua", "-- base");
    dir.write("mods/ext/mod.json", R"({"id": "ext", "version": "1.0.0", "scripts": true})");
    dir.write("mods/ext/maps/m/events.lua", "-- ext");
    dir.write("mods/rep/mod.json",
              R"({"id": "rep", "version": "2.0.0", "scripts": true, "replace": ["maps/m/events.lua"]})");
    dir.write("mods/rep/maps/m/events.lua", "-- rep");
    dir.write("mods/quiet/mod.json", R"({"id": "quiet", "version": "1.0.0"})");  // no "scripts": its Lua is skipped
    dir.write("mods/quiet/maps/m/events.lua", "-- quiet");

    std::vector<std::string> warnings;
    const LuaLayers layers = lua_layers(dir.root / "mods", dir.root / "gamedata", "maps/m/events.lua", warnings);
    SH_CHECK(layers.base_owner == "rep");
    SH_CHECK(layers.base == dir.root / "mods" / "rep" / "maps/m/events.lua");
    SH_CHECK(layers.extensions.size() == 1 && layers.extensions[0].mod.id == "ext");
    SH_CHECK(has(warnings, "mod quiet has Lua files"));

    const LuaLayers none = lua_layers(dir.root / "mods", dir.root / "gamedata", "maps/other/events.lua", warnings);
    SH_CHECK(none.empty());

    Registry reg;
    RegistryConfig config;
    config.dirs = {dir.root / "mods"};
    config.base_dir = dir.root / "gamedata";
    reg.configure(config);
    SH_CHECK(has(reg.report(false), "replaces whole maps/m/events.lua"));
}
