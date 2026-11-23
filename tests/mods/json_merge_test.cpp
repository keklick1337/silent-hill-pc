#include "engine/mods/data_merge.h"
#include "engine/mods/json_merge.h"
#include "engine/mods/registry.h"
#include "framework/sh_test.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>
#include "framework/mod_fixture.h"

namespace fs = std::filesystem;
using namespace sh::mods;
using sh::json::Value;

namespace {

Value doc(std::string_view text) {
    auto parsed = sh::json::parse(text);
    if (!parsed) {
        std::fprintf(stderr, "bad test JSON: %s\n", parsed.error().c_str());
        return Value();
    }
    return parsed.value();
}

// Merges `patch` over `base` and returns the result as compact text (member order kept).
std::string merged(std::string_view base, std::string_view patch, MergeStats* stats = nullptr,
                   std::vector<std::string>* warnings = nullptr, const MergeRules& rules = MergeRules{}) {
    Value v = doc(base);
    MergeStats s;
    std::vector<std::string> w;
    merge_patch(v, doc(patch), rules, s, w);
    if (stats) {
        *stats = s;
    }
    if (warnings) {
        *warnings = w;
    }
    return sh::json::write(v);
}

bool has(const std::vector<std::string>& lines, std::string_view part) {
    return std::any_of(lines.begin(), lines.end(),
                       [&](const std::string& l) { return l.find(part) != std::string::npos; });
}

class Scratch {
public:
    explicit Scratch(const std::string& name) : path_(fs::path(SH_PROJECT_ROOT) / ".tmp" / "json_merge" / name) {
        std::error_code ec;
        fs::remove_all(path_, ec);
        fs::create_directories(path_, ec);
    }
    ~Scratch() {
        std::error_code ec;
        fs::remove_all(path_, ec);
    }
    Scratch(const Scratch&) = delete;
    Scratch& operator=(const Scratch&) = delete;

    const fs::path& path() const { return path_; }

    void write(const std::string& relative, std::string_view text) const {
        const fs::path file = path_ / relative;
        fs::create_directories(file.parent_path());
        std::ofstream out(file, std::ios::binary);
        out << text;
        out.close();
        sh::test::mod_file_written(file);
    }
    // A mod folder with a manifest; `extra` is more mod.json members (", \"replace\": [...]").
    void mod(const std::string& id, int priority, std::string_view extra = "") const {
        write("mods/" + id + "/mod.json", "{\"id\": \"" + id + "\", \"version\": \"1.0.0\", \"priority\": " +
                                              std::to_string(priority) + std::string(extra) + "}");
    }

private:
    fs::path path_;
};

std::string read_all(const fs::path& file) {
    std::ifstream in(file, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

}  // namespace

// ---- json::Value building and writing ----

SH_TEST(json_value_write_round_trips_and_compares) {
    const Value v = doc(R"({"a": 1, "b": [true, null, "x\"y"], "c": {"d": 1.5, "e": -0.0, "f": 2.0}})");
    const std::string text = sh::json::write(v);
    SH_CHECK(text == R"({"a":1,"b":[true,null,"x\"y"],"c":{"d":1.5,"e":-0.0,"f":2.0}})");
    SH_CHECK(doc(text) == v);
    SH_CHECK(doc(R"({"b": 2, "a": 1})") == doc(R"({"a": 1, "b": 2})"));  // member order does not count
    SH_CHECK(!(doc("[1, 2]") == doc("[2, 1]")));
    SH_CHECK(!(doc("1") == doc("1.0")));
    Value built = Value::empty_object();
    built.set("k", Value::from_integer(3));
    built.set("k", Value::from_string("s"));
    built.set("l", Value::empty_array()).array_mut().push_back(Value::from_bool(false));
    SH_CHECK(sh::json::write(built) == R"({"k":"s","l":[false]})");
    SH_CHECK(built.erase("k") && !built.erase("k"));
    SH_CHECK(sh::json::write(sh::json::Value::from_real(3.0)) == "3.0");
}

// ---- json_merge ----

SH_TEST(json_merge_objects_key_by_key_and_null_deletes) {
    MergeStats s;
    const std::string out = merged(R"({"a": 1, "b": {"x": 1, "y": 2}, "c": 3})",
                                   R"({"a": 5, "b": {"y": 20, "z": 30}, "c": null, "d": "new"})", &s);
    SH_CHECK(out == R"({"a":5,"b":{"x":1,"y":20,"z":30},"d":"new"})");
    // Counted per member: a and b.y modified, b.z and d added, c removed.
    SH_CHECK(s.modified == 2 && s.added == 2 && s.removed == 1);
    SH_CHECK(s.describe() == "modified 2, added 2, removed 1");
    MergeStats none;
    SH_CHECK(merged(R"({"a": 1})", R"({"a": 1})", &none) == R"({"a":1})" && !none.changed());
}

SH_TEST(json_merge_arrays_of_objects_by_id_then_name) {
    MergeStats s;
    std::vector<std::string> w;
    const std::string out = merged(
        R"({"list": [{"id": "a", "v": 1}, {"id": "b", "v": 2}, {"id": "c", "v": 3}]})",
        R"({"list": [{"id": "b", "v": 20}, {"id": "c", "$remove": true}, {"id": "d", "v": 4},
                     {"id": "zz", "$remove": true}]})",
        &s, &w);
    SH_CHECK(out == R"({"list":[{"id":"a","v":1},{"id":"b","v":20},{"id":"d","v":4}]})");
    SH_CHECK(s.modified == 1 && s.added == 1 && s.removed == 1);
    SH_CHECK(has(w, "no entry id = \"zz\""));
    // "name" when the entries have no "id"; an entry merges recursively.
    SH_CHECK(merged(R"([{"name": "x", "p": {"a": 1, "b": 2}}])", R"([{"name": "x", "p": {"b": null}}])") ==
             R"([{"name":"x","p":{"a":1}}])");
}

SH_TEST(json_merge_array_positions_after_and_before) {
    const std::string base = R"([{"id": 1}, {"id": 2}, {"id": 3}])";
    SH_CHECK(merged(base, R"([{"id": 9, "$after": 1}])") == R"([{"id":1},{"id":9},{"id":2},{"id":3}])");
    SH_CHECK(merged(base, R"([{"id": 9, "$before": 1}])") == R"([{"id":9},{"id":1},{"id":2},{"id":3}])");
    // An existing entry moves.
    SH_CHECK(merged(base, R"([{"id": 3, "$before": 2}])") == R"([{"id":1},{"id":3},{"id":2}])");
    std::vector<std::string> w;
    SH_CHECK(merged(base, R"([{"id": 9, "$after": 7}])", nullptr, &w) == R"([{"id":1},{"id":2},{"id":3},{"id":9}])");
    SH_CHECK(has(w, "\"$after\": no entry id = 7"));
}

SH_TEST(json_merge_keyless_and_scalar_arrays_replace_unless_append) {
    MergeStats s;
    SH_CHECK(merged(R"({"a": [1, 2, 3]})", R"({"a": [4]})", &s) == R"({"a":[4]})");
    SH_CHECK(s.modified == 1);
    // Entries without a key field: the array is replaced whole (an old full copy keeps working).
    SH_CHECK(merged(R"([{"x": 1}, {"x": 2}])", R"([{"x": 3}])") == R"([{"x":3}])");
    MergeStats a;
    SH_CHECK(merged(R"({"a": [1, 2, 3]})", R"({"a": {"$append": [4, 5], "$prepend": [0], "$remove": [2]}})", &a) ==
             R"({"a":[0,1,3,4,5]})");
    SH_CHECK(a.added == 3 && a.removed == 1);
    std::vector<std::string> w;
    merged(R"({"a": [1]})", R"({"a": {"$remove": [7]}})", nullptr, &w);
    SH_CHECK(has(w, "no 7 to remove"));
}

SH_TEST(json_merge_rules_compound_keys_and_patch_keys) {
    MergeRules rules;
    rules.array_keys = {{"spawns", "group,slot"}, {"handlers", "index"}};
    MergeStats s;
    const std::string out =
        merged(R"({"spawns": [{"group": 0, "slot": 1, "x": 1}, {"group": 1, "slot": 1, "x": 2}],
                   "handlers": [{"index": 0, "name": "door"}, {"index": 1}]})",
               R"({"spawns": [{"group": 1, "slot": 1, "x": 20}, {"group": 1, "slot": 2, "x": 3,
                                "$after": {"group": 0, "slot": 1}}],
                   "handlers": [{"index": 1, "name": "bell"}]})",
               &s, nullptr, rules);
    SH_CHECK(out == R"({"spawns":[{"group":0,"slot":1,"x":1},{"group":1,"slot":2,"x":3},{"group":1,"slot":1,"x":20}],)"
                    R"("handlers":[{"index":0,"name":"door"},{"index":1,"name":"bell"}]})");
    SH_CHECK(s.modified == 2 && s.added == 1);
    // "$keys" in the patch names a key the rules do not know.
    SH_CHECK(merged(R"({"t": [{"sym": "A", "v": 1}, {"sym": "B", "v": 2}]})",
                    R"({"$keys": {"t": "sym"}, "t": [{"sym": "B", "v": 3}]})") ==
             R"({"t":[{"sym":"A","v":1},{"sym":"B","v":3}]})");
}

SH_TEST(json_merge_nested_replace_and_directives_never_leak) {
    SH_CHECK(merged(R"({"snow": {"a": 1, "b": 2}})", R"({"snow": {"$replace": true, "c": 3}})") ==
             R"({"snow":{"c":3}})");
    SH_CHECK(merged(R"({"l": [{"id": 1}]})", R"({"l": [{"id": 2, "$after": 1, "sub": {"$replace": true, "k": 1}}]})") ==
             R"({"l":[{"id":1},{"id":2,"sub":{"k":1}}]})");
    std::vector<std::string> w;
    SH_CHECK(merged(R"({"a": 1})", R"({"$oops": 1, "a": 2})", nullptr, &w) == R"({"a":2})");
    SH_CHECK(has(w, "unknown directive \"$oops\""));
    SH_CHECK(wants_replace(doc(R"({"$replace": true})")) && !wants_replace(doc(R"({"a": 1})")));
    // A top-level "$replace" is the caller's decision: merge_patch only skips it.
    SH_CHECK(merged(R"({"a": 1, "b": 2})", R"({"$replace": true, "a": 3})") == R"({"a":3,"b":2})");
}

SH_TEST(json_merge_fixed_point_twins) {
    const std::string base = R"({"speed": 35, "speed_q12": 143360, "road_max_y": 1.5, "road_max_y_q4": 24, "q": 1})";
    SH_CHECK(merged(base, R"({"speed_q12": 70000})") == R"({"speed_q12":70000,"road_max_y":1.5,"road_max_y_q4":24,"q":1})");
    SH_CHECK(merged(base, R"({"road_max_y": 2})") == R"({"speed":35,"speed_q12":143360,"road_max_y":2,"q":1})");
    SH_CHECK(merged(base, R"({"speed": 40, "speed_q12": 163840})") ==
             R"({"speed":40,"speed_q12":163840,"road_max_y":1.5,"road_max_y_q4":24,"q":1})");
    MergeRules keep;
    keep.drop_stale_twins = false;
    SH_CHECK(merged(R"({"a": 1, "a_q12": 4096})", R"({"a_q12": 1})", nullptr, nullptr, keep) == R"({"a":1,"a_q12":1})");
}

// ---- data_merge: base + mods ----

SH_TEST(data_merge_rules_table) {
    auto key = [](std::string_view file, std::string_view path) {
        const std::string* k = rules_for(file).key_for(path);
        return k != nullptr ? *k : std::string("(none)");
    };
    SH_CHECK(key("maps/map2_s00/world_objects.json", "objects") == "name");
    SH_CHECK(key("MAPS/map2_s00/handlers.json", "handlers") == "index");
    SH_CHECK(key("maps/map0_s01/tables.json", "functions") == "symbol");
    SH_CHECK(key("maps/map1_s02/map.json", "spawns") == "group,slot");
    SH_CHECK(key("tables/anim/HarryAnims.json", "entries") == "index");
    SH_CHECK(key("config/difficulties.json", "difficulties") == "label");
    SH_CHECK(key("menu/options.json", "main.rows") == "(none)");
    SH_CHECK(key("maps/x/world_objects.json/extra", "objects") == "(none)");
}

SH_TEST(data_merge_without_mods_reads_the_file_as_it_is) {
    Scratch dir("no_mods");
    const std::string text = "{\n  \"objects\": [ {\"name\": \"a\"} ]   \n}\n";
    dir.write("gamedata/maps/m/world_objects.json", text);
    std::vector<std::string> warnings;
    auto read = read_data_file(dir.path() / "gamedata", dir.path() / "mods", "maps/m/world_objects.json", warnings);
    SH_CHECK(static_cast<bool>(read));
    SH_CHECK(read.value().text == text);  // byte for byte: no reformatting without mods
    SH_CHECK(read.value().base && !read.value().merged() && read.value().trace.empty());
    SH_CHECK(warnings.empty());
    auto missing = read_data_file(dir.path() / "gamedata", dir.path() / "mods", "maps/m/none.json", warnings);
    SH_CHECK(!missing && missing.error().starts_with("no file"));
    auto text_missing = read_data_text(dir.path() / "gamedata", "", "maps/m/none.json");
    SH_CHECK(!text_missing && text_missing.error().starts_with("cannot read"));
}

SH_TEST(data_merge_two_mods_patch_one_file) {
    Scratch dir("two_mods");
    dir.write("gamedata/maps/m/world_objects.json",
              R"({"format": "x", "objects": [{"name": "bench", "model": "B"}, {"name": "sign", "model": "S"},
                                           {"name": "lamp", "model": "L", "zones": [1, 2]}]})");
    dir.mod("alpha", 0);
    dir.mod("beta", 1);
    dir.write("mods/alpha/maps/m/world_objects.json",
              R"({"objects": [{"name": "bench", "model": "B2"}, {"name": "box", "model": "X"},
                              {"name": "lamp", "zones": [3]}]})");
    dir.write("mods/beta/maps/m/world_objects.json",
              R"({"objects": [{"name": "bench", "model": "B3"}, {"name": "sign", "$remove": true},
                              {"name": "crate", "model": "C", "$before": "bench"}]})");
    std::vector<std::string> warnings;
    auto read = read_data_file(dir.path() / "gamedata", dir.path() / "mods", "maps/m/world_objects.json", warnings);
    SH_CHECK(static_cast<bool>(read));
    SH_CHECK(warnings.empty());
    const Value v = doc(read.value().text);
    const std::string objects = sh::json::write(*v.find("objects"));
    // Both mods' changes are there; on "bench" both changed the model: beta loads later and wins.
    SH_CHECK(objects == R"([{"name":"crate","model":"C"},{"name":"bench","model":"B3"},)"
                        R"({"name":"lamp","model":"L","zones":[3]},{"name":"box","model":"X"}])");
    SH_CHECK(v.find("format")->string() == "x");
    SH_CHECK(read.value().merged() && read.value().base && read.value().last_mod() == "beta");
    SH_CHECK(read.value().mods.size() == 2 && read.value().mods[0].mod_id == "alpha");
    SH_CHECK(has(read.value().trace, "mod alpha: maps/m/world_objects.json: modified 2, added 1, removed 0"));
    SH_CHECK(has(read.value().trace, "mod beta: maps/m/world_objects.json: modified 1, added 1, removed 1"));
    SH_CHECK(read.value().source().find("merged: base + mods alpha, beta") != std::string::npos);
}

SH_TEST(data_merge_whole_file_replace_needs_the_manifest_list) {
    Scratch dir("replace");
    dir.write("gamedata/config/x.json", R"({"a": 1, "b": 2})");
    dir.mod("plain", 0);
    dir.write("mods/plain/config/x.json", R"({"$replace": true, "a": 10})");
    std::vector<std::string> warnings;
    auto read = read_data_file(dir.path() / "gamedata", dir.path() / "mods", "config/x.json", warnings);
    SH_CHECK(static_cast<bool>(read) && doc(read.value().text) == doc(R"({"a": 10, "b": 2})"));
    SH_CHECK(has(warnings, "mod plain: config/x.json: \"$replace\" ignored"));

    dir.mod("plain", 0, R"(, "replace": ["config/X.json"])");
    warnings.clear();
    read = read_data_file(dir.path() / "gamedata", dir.path() / "mods", "config/x.json", warnings);
    SH_CHECK(static_cast<bool>(read) && doc(read.value().text) == doc(R"({"a": 10})"));
    SH_CHECK(!read.value().base && !has(warnings, "$replace"));
    SH_CHECK(has(read.value().trace, "replaced whole"));

    // A second mod replacing the same file conflicts: the load order switches the later one off (load_order.cpp);
    // a later patch still applies on top of the first replacement.
    dir.mod("other", 1, R"(, "replace": ["config/x.json"])");
    dir.write("mods/other/config/x.json", R"({"$replace": true, "c": 3})");
    dir.mod("late", 2);
    dir.write("mods/late/config/x.json", R"({"d": 4})");
    warnings.clear();
    read = read_data_file(dir.path() / "gamedata", dir.path() / "mods", "config/x.json", warnings);
    SH_CHECK(static_cast<bool>(read) && doc(read.value().text) == doc(R"({"a": 10, "d": 4})"));
    SH_CHECK(has(warnings, "which mod 'plain' also replaces"));
}

SH_TEST(data_merge_broken_files) {
    Scratch dir("broken");
    dir.write("gamedata/config/x.json", R"({"a": 1})");
    dir.mod("bad", 0);
    dir.write("mods/bad/config/x.json", R"({"a": )");
    dir.mod("good", 1);
    dir.write("mods/good/config/x.json", R"({"b": 2})");
    std::vector<std::string> warnings;
    auto read = read_data_file(dir.path() / "gamedata", dir.path() / "mods", "config/x.json", warnings);
    SH_CHECK(static_cast<bool>(read) && doc(read.value().text) == doc(R"({"a": 1, "b": 2})"));
    SH_CHECK(has(warnings, "mod bad: config/x.json: line 1") && has(warnings, "skipped"));
    // A broken base: the mods' copies stand in for it.
    dir.write("gamedata/config/x.json", R"({)");
    warnings.clear();
    read = read_data_file(dir.path() / "gamedata", dir.path() / "mods", "config/x.json", warnings);
    SH_CHECK(static_cast<bool>(read) && read.value().text == R"({"b": 2})" && !read.value().base);
    SH_CHECK(has(warnings, "the mods' copies are used without it"));
    // Only a mod has the file: its bytes, then the next mod patches them.
    dir.write("mods/bad/config/new.json", R"({"n": 1})");
    dir.write("mods/good/config/new.json", R"({"m": 2})");
    read = read_data_file(dir.path() / "gamedata", dir.path() / "mods", "config/new.json", warnings);
    SH_CHECK(static_cast<bool>(read) && doc(read.value().text) == doc(R"({"n": 1, "m": 2})"));
    SH_CHECK(has(read.value().trace, "mod bad: config/new.json: added (no base file)"));
}

SH_TEST(data_merge_through_the_registry) {
    Scratch dir("registry");
    dir.write("gamedata/maps/m/handlers.json", R"({"handlers": [{"index": 0, "name": "a"}, {"index": 1}]})");
    dir.mod("one", 0);
    dir.write("mods/one/maps/m/handlers.json", R"({"handlers": [{"index": 1, "name": "b"}]})");
    dir.mod("two", 1);
    dir.write("mods/two/maps/m/handlers.json", R"({"handlers": [{"index": 2, "name": "c"}]})");
    RegistryConfig config;
    config.dirs = {dir.path() / "mods"};
    config.base_dir = dir.path() / "gamedata";
    registry().configure(config);
    std::vector<std::string> warnings;
    auto read = read_registry_data_file("maps/m/handlers.json", dir.path() / "gamedata", warnings);
    SH_CHECK(static_cast<bool>(read));
    SH_CHECK(doc(read.value().text) ==
             doc(R"({"handlers": [{"index": 0, "name": "a"}, {"index": 1, "name": "b"}, {"index": 2, "name": "c"}]})"));
    SH_CHECK(read.value().mods.size() == 2 && read.value().mods[1].mod_id == "two");
    // Without mod copies: the base bytes.
    auto plain = read_registry_data_file("maps/m/none.json", dir.path() / "gamedata", warnings);
    SH_CHECK(!plain);
    registry().configure(RegistryConfig{});
    SH_CHECK(read_all(dir.path() / "gamedata/maps/m/handlers.json") ==
             read_registry_data_file("maps/m/handlers.json", dir.path() / "gamedata", warnings).value().text);
}
