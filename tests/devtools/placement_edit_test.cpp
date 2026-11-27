// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// The placement editor's text and number work (engine/devtools/placement_edit.h) and the origin a script's call
// leaves on what it makes (engine/script/caller_mod.h): the line of a Lua table or the record of a JSON file with a
// moved thing's numbers, raw and metre forms of a point, path lengths and times, the step-back list.

#include "engine/devtools/placement_edit.h"

#include "engine/devtools/position_snippets.h"
#include "engine/script/script_host.h"
#include "engine/world/runtime_pickups.h"
#include "framework/mod_fixture.h"
#include "framework/sh_test.h"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

namespace {

namespace dt = sh::devtools;
namespace fs = std::filesystem;

bool contains(const std::string& s, std::string_view part) { return s.find(part) != std::string::npos; }
bool near(double a, double b) { return std::fabs(a - b) < 1e-9; }

constexpr double kStep = 1.01 / 4096.0;  // a Q12 step in metres

dt::Wanted metres(std::int32_t q12) { return {q12 / 4096.0, kStep, true}; }

const char* kScript = R"lua(local MONSTERS = {
    map2_s00 = { { "Groaner", 5.0, 10.0 }, { "Groaner", 70.0, 0.0 }, { "AirScreamer", 40.0, 0.0 },
                 { "Groaner", 119.412, 113.3018 }, { "Groaner", 122.4, 116.2, from = "normal" } },
}
local PICKUPS = {
    { "map2_s00", "HandgunBullets", 22.27, 51.45, -0.61 },
    { "map2_s00", "HealthDrink", 22.35, 50.95, -0.61 },  -- 22.27, 51.45 in a comment is not a number
}
local PATROL_CAR = { x = 185.29, z = 116.25 }  -- the empty patrol car
local GIRL = { x = -40, y = -2, z = -74729 } -- -0.01, -0.0005, -18.2444 m
spot("map2_s00", { x = PATROL_CAR.x, z = PATROL_CAR.z, r = 3.0, look = true, label = "patrol car",
spot("map2_s00", { x = 3.0, z = 4.5, r = 3, label = "three" })
)lua";

}  // namespace

SH_TEST(placement_edit_numbers_and_raw_rows) {
    SH_CHECK(dt::tidy_number(1.5) == "1.5");
    SH_CHECK(dt::tidy_number(-0.31700001) == "-0.317");
    SH_CHECK(dt::tidy_number(3.0) == "3");
    SH_CHECK(dt::tidy_number(3.0, 4, true) == "3.0");
    SH_CHECK(dt::tidy_number(-0.00001) == "0");
    // A point in metres as a script writes it in the game's integers.
    SH_CHECK(dt::raw_point_row(-0.01, -0.0005, -18.2444) ==
             "{ x = -40, y = -2, z = -74729 } -- -0.01, -0.0005, -18.2444 m");
    // Metres to the game's integers truncate in float toward zero, both ways from zero.
    SH_CHECK(dt::q12_of_metres(134.6f) == 551321);
    SH_CHECK(dt::q12_of_metres(-18.2444f) == -74729);
    SH_CHECK(dt::q12_of_metres(0.9999f) == 4095);
    SH_CHECK(dt::q12_of_metres(-0.9999f) == -4095);
    // Every integer has a metre text that reads back as itself.
    for (std::int32_t q = -80000; q <= 80000; q += 613) {
        SH_CHECK(dt::q12_of_metres(std::strtof(dt::metres_text(q).c_str(), nullptr)) == q);
    }
}

SH_TEST(placement_edit_number_tokens) {
    const auto t = dt::number_tokens(R"(  { "map2_s00", "Drink 5", 22.35, -0.61, a - 2, p[5], 0x10 }, -- 7.5)");
    SH_CHECK(t.size() == 5);
    SH_CHECK(near(t[0].value, 22.35) && !t[0].integer);
    SH_CHECK(near(t[1].value, -0.61));
    SH_CHECK(near(t[2].value, 2.0) && t[2].integer);  // a binary minus is not a sign
    SH_CHECK(near(t[3].value, 5.0));
    SH_CHECK(near(t[4].value, 16.0) && t[4].integer);
    SH_CHECK(dt::number_tokens("x = -40, y=-2").size() == 2);
    SH_CHECK(near(dt::number_tokens("x = -40, y=-2")[1].value, -2.0));
    // JSON has no comments; a name with digits holds no number.
    SH_CHECK(dt::number_tokens(R"("position": [-14.42, -0.317, 201.55], "m": "map2")", false).size() == 3);
}

SH_TEST(placement_edit_finds_the_lua_line) {
    // A monster's place as the engine holds it (Q12) finds its row; the comment's numbers do not count.
    auto hits =
        dt::lines_with_values(kScript, {metres(dt::q12_of_metres(119.412f)), metres(dt::q12_of_metres(113.3018f))});
    SH_CHECK(hits.size() == 1 && hits[0].line == 3);
    hits = dt::lines_with_values(kScript, {metres(dt::q12_of_metres(22.27f)), metres(dt::q12_of_metres(51.45f))});
    SH_CHECK(hits.size() == 1 && hits[0].line == 6);
    // A point written in the game's integers.
    hits = dt::lines_with_values(kScript, {metres(-40), metres(-74729)});
    SH_CHECK(hits.size() == 1 && hits[0].line == 10);
    // A zone's label finds the line of its radius.
    const auto labelled = dt::lines_with_string(kScript, "patrol car");
    SH_CHECK(labelled.size() == 1 && labelled[0].line == 11);
    SH_CHECK(dt::lines_with_values(kScript, {metres(4096 * 999)}).empty());
}

SH_TEST(placement_edit_patches_a_lua_row) {
    int n = 0;
    // The fourth monster moves: only its two numbers change, the rest of the line stays.
    const std::string row =
        R"(                 { "Groaner", 119.412, 113.3018 }, { "Groaner", 122.4, 116.2, from = "normal" } },)";
    std::string out = dt::patch_line(
        row, {{metres(dt::q12_of_metres(119.412f)), 120.5, ""}, {metres(dt::q12_of_metres(113.3018f)), 113.0, ""}}, &n);
    SH_CHECK(n == 2);
    SH_CHECK(out == R"(                 { "Groaner", 120.5, 113.0 }, { "Groaner", 122.4, 116.2, from = "normal" } },)");
    // The radius by its key: 3.0 m to 1.5 m, although x could also be 3.0.
    out = dt::patch_line(R"(spot("map2_s00", { x = 3.0, z = 4.5, r = 3, label = "three" }))",
                         {{{3.0, kStep, true}, 1.5, "r"}}, &n);
    SH_CHECK(n == 1);
    SH_CHECK(out == R"(spot("map2_s00", { x = 3.0, z = 4.5, r = 1.5, label = "three" }))");
    out = dt::patch_line(
        R"(spot("map2_s00", { x = PATROL_CAR.x, z = PATROL_CAR.z, r = 3.0, look = true, label = "patrol car",)",
        {{{3.0, kStep, true}, 1.5, "r"}}, &n);
    SH_CHECK(n == 1 && contains(out, "r = 1.5, look = true"));
    // Integers of the game stay integers; the comment is the author's to redo.
    out = dt::patch_line("local GIRL = { x = -40, y = -2, z = -74729 } -- -0.01, -0.0005, -18.2444 m",
                         {{metres(-40), 1.0, "x"}, {metres(-74729), -20.0, "z"}}, &n);
    SH_CHECK(n == 2);
    SH_CHECK(contains(out, "{ x = 4096, y = -2, z = -81920 }"));
    // Nothing to replace: the line as it was.
    out = dt::patch_line("local A = { 1.0, 2.0 }", {{metres(4096 * 7), 9.0, ""}}, &n);
    SH_CHECK(n == 0 && out == "local A = { 1.0, 2.0 }");
}

SH_TEST(placement_edit_patches_a_json_record) {
    const std::string file = R"({
  "objects": [
    {"name": "SteelPipe", "flags_clear": ["M2S00_PickupSteelPipe"]},
    {
      "name": "CybilBike",
      "model": "CYBBIKE",
      "item_model": {"file": "cybil_story/cybil_bike.glb", "scale": 1.0, "position": [9, 9, 9]},
      "position": [
        -14.42,
        -0.317,
        201.55
      ],
      "rotation": [3, 8, 75],
      "collision": [{"offset": [0.32, 0.169, -0.9], "radius": 0.4}]
    }
  ]
})";
    const auto bike = dt::json_record(file, "name", "CybilBike");
    SH_CHECK(bike.has_value());
    SH_CHECK(dt::line_of(file, bike->begin) == 4);
    const auto position = dt::json_member(file, *bike, "position");
    SH_CHECK(position.has_value() && dt::line_of(file, position->begin) == 8);  // its own, not the item model's
    const dt::PatchedRecord p =
        dt::patch_json_record(file, *bike, {{"position", {-13.42, -0.317, 201.55}, 4}, {"rotation", {3, 98, 75}, 3}});
    SH_CHECK(p.line == 4);
    SH_CHECK(p.before.size() == 2 && p.before[0].line == 8 &&
             p.before[0].text == "\"position\": [-14.42, -0.317, 201.55]");
    SH_CHECK(p.before[1].line == 13 && p.before[1].text == "\"rotation\": [3, 8, 75]");
    SH_CHECK(p.after.size() == 2 && p.after[0] == "\"position\": [-13.42, -0.317, 201.55]");
    SH_CHECK(p.after[1] == "\"rotation\": [3, 98, 75]");
    // The record keeps its layout and its other keys.
    SH_CHECK(contains(p.record, "        -13.42,\n        -0.317,\n        201.55\n"));
    SH_CHECK(contains(p.record, "\"position\": [9, 9, 9]"));
    SH_CHECK(contains(p.record, "\"collision\": [{\"offset\": [0.32, 0.169, -0.9], \"radius\": 0.4}]"));
    SH_CHECK(p.added.empty());

    // A number that did not change keeps the file's literal.
    const dt::PatchedRecord k =
        dt::patch_json_record(file, *bike, {{"rotation", {3.0001, 98, 75}, 3, {true, false, true}}});
    SH_CHECK(k.after.size() == 1 && k.after[0] == "\"rotation\": [3, 98, 75]");

    // A record without the member (a mod's patch of a base object, an object placed by a disc pose): it is added.
    const auto pipe = dt::json_record(file, "name", "SteelPipe");
    SH_CHECK(pipe.has_value());
    const dt::PatchedRecord q = dt::patch_json_record(file, *pipe, {{"position", {1.5, 0, -2.25}, 4}});
    SH_CHECK(q.before.empty() && q.added.size() == 1 && q.added[0] == "\"position\": [1.5, 0, -2.25]");
    SH_CHECK(q.record ==
             R"({"name": "SteelPipe", "flags_clear": ["M2S00_PickupSteelPipe"], "position": [1.5, 0, -2.25]})");
    SH_CHECK(!dt::json_record(file, "name", "Nobody").has_value());
    // A record named by a number.
    const std::string points = R"({"points": [{"index": 4, "x": 1.0}, {"index": 5, "x": 2.0}]})";
    const auto five = dt::json_record(points, "index", "5");
    SH_CHECK(five.has_value() && points.substr(five->begin, five->end - five->begin) == R"({"index": 5, "x": 2.0})");
}

SH_TEST(placement_edit_file_owner) {
    const std::vector<std::pair<std::string, std::string>> mods{{"cybil_story", "/game/mods/cybil_story"},
                                                                {"lamp", "/game/mods/lamp/"}};
    dt::FileOwner o = dt::file_owner("/game/mods/cybil_story/scripts/main.lua", "/game/gamedata", mods);
    SH_CHECK(o.known && o.mod == "cybil_story" && o.relative == "scripts/main.lua");
    SH_CHECK(dt::file_owner_text(o) == "mod cybil_story: scripts/main.lua");
    o = dt::file_owner("/game/gamedata/maps/map2_s00/world_objects.json", "/game/gamedata/", mods);
    SH_CHECK(o.known && o.mod.empty() && o.relative == "maps/map2_s00/world_objects.json");
    SH_CHECK(dt::file_owner_text(o) == "base gamedata/maps/map2_s00/world_objects.json");
    SH_CHECK(!dt::file_owner("/elsewhere/x.lua", "/game/gamedata", mods).known);
    // A folder whose name only begins like a mod's is not that mod.
    SH_CHECK(!dt::file_owner("/game/mods/lamp2/a.lua", "/game/gamedata", mods).known);
}

SH_TEST(placement_edit_path_lengths_and_times) {
    const std::vector<dt::PathPoint> path{{0, 0, 0}, {3, 0, 4}, {3, -12, 4}};
    const auto seg = dt::path_segments(path);
    SH_CHECK(seg.size() == 2 && near(seg[0], 5.0) && near(seg[1], 12.0));
    SH_CHECK(near(dt::path_length(path), 17.0));
    SH_CHECK(near(dt::path_time(17.0, 0.8), 21.25));
    SH_CHECK(near(dt::path_speed(17.0, 21.25), 0.8));
    SH_CHECK(dt::path_time(17.0, 0.0) == 0.0 && dt::path_speed(17.0, 0.0) == 0.0);
    SH_CHECK(dt::path_segments({}).empty() && dt::path_length({{1, 2, 3}}) == 0.0);
    SH_CHECK(dt::path_json(path) == "[[0, 0, 0], [3, 0, 4], [3, -12, 4]]");
    const std::string raw = dt::path_lua_raw({{-0.01, -0.0005, -18.2444}});
    SH_CHECK(raw == "{\n    { x = -40, y = -2, z = -74729 }, -- -0.01, -0.0005, -18.2444 m\n}");
    SH_CHECK(dt::path_lua_metres({{1.5, 0, -2}}) == "{\n    { x = 1.5, y = 0, z = -2 },\n}");
}

SH_TEST(placement_edit_history) {
    dt::EditHistory h;
    dt::EditValue a, b, c, d;
    b.position = {1, 0, 0};
    c.position = {2, 0, 0};
    d.radius = 1.5;
    SH_CHECK(!h.can_undo() && !h.undo().has_value() && !h.original("bike").has_value());
    h.record("bike", a, a);  // no change, no step
    SH_CHECK(!h.can_undo());
    // One drag is one step, however many frames it took.
    h.record("bike", a, b, 7);
    h.record("bike", b, c, 7);
    SH_CHECK(h.size() == 1);
    h.record("zone", a, d);
    SH_CHECK(h.size() == 2);
    auto step = h.undo();
    SH_CHECK(step && step->key == "zone" && step->before == a);
    step = h.undo();
    SH_CHECK(step && step->key == "bike" && step->before == a && step->after == c);
    SH_CHECK(!h.can_undo() && h.can_redo());
    step = h.redo();
    SH_CHECK(step && step->key == "bike" && step->after == c);
    // A new edit drops what was taken back; a second drag is a step of its own.
    h.record("bike", c, b, 8);
    SH_CHECK(h.size() == 2 && !h.can_redo());
    // The file's values are what the thing had before its first edit.
    SH_CHECK(h.original("bike") == a && h.original("zone") == a);
    h.clear();
    SH_CHECK(!h.can_undo() && !h.original("bike").has_value());
}

// A script's call leaves its file and line on what it makes, with who called the function that made it.
SH_TEST(placement_edit_script_origin) {
    const fs::path root = fs::path(SH_PROJECT_ROOT) / ".tmp" / "placement_edit_origin";
    fs::remove_all(root);
    const auto write = [&root](const fs::path& relative, std::string_view text) {
        fs::create_directories((root / relative).parent_path());
        std::ofstream(root / relative, std::ios::binary) << text;
        sh::test::mod_file_written(root / relative);
    };
    write("gamedata/scripts/game.lua", "function on_boot() end\n");
    write("mods/lamp/mod.json", R"({"id": "lamp", "version": "1.0.0", "scripts": true})");
    write(
        "mods/lamp/scripts/main.lua",
        "local SPOTS = { { x = 1.5, z = 2.0, r = 3.0 } }\n"                                    // 1
        "local function place()\n"                                                             // 2
        "    for i, s in ipairs(SPOTS) do\n"                                                   // 3
        "        sh.world.mark('spot' .. i, { x = s.x, z = s.z, r = s.r, label = 'lamp' })\n"  // 4
        "    end\n"                                                                            // 5
        "    sh.world.pickup_create { item = 'Oil', x = 4096, z = 8192, id = 'oil', flag = 'oil', model = 'OIL' }\n"  // 6
        "end\n"                 // 7
        "function on_boot()\n"  // 8
        "    place()\n"         // 9
        "end\n");
    sh::world::clear_script_marks("lamp", "");
    sh::world::pickups().set_resolver(nullptr);
    sh::world::pickups().begin_map("map_origin_test");
    sh::script::HostConfig c;
    c.gamedata_dir = root / "gamedata";
    c.mods_dir = root / "mods";
    c.log = [](int, const std::string&) {};
    {
        sh::script::Host host(std::move(c));
        SH_CHECK(host.load().value());
        host.on_boot();
    }
    const sh::world::ScriptMark* mark = nullptr;
    for (const sh::world::ScriptMark& m : sh::world::script_marks())
        if (m.owner == "lamp") mark = &m;
    SH_CHECK(mark != nullptr);
    if (mark != nullptr) {
        SH_CHECK(mark->origin.known() && mark->origin.line == 4);
        SH_CHECK(mark->origin.file.ends_with("mods/lamp/scripts/main.lua"));
        SH_CHECK(mark->origin.via == "main.lua:9");
        SH_CHECK(mark->x == 6144 && mark->radius == 12288);
        // The numbers of the mark are on line 1 of the file its call names.
        std::ifstream in(mark->origin.file, std::ios::binary);
        const std::string source((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        const auto hits = dt::lines_with_values(source, {metres(mark->x), metres(mark->z)});
        SH_CHECK(hits.size() == 1 && hits[0].line == 1);
        const dt::FileOwner owner = dt::file_owner(mark->origin.file, (root / "gamedata").generic_string(),
                                                   {{"lamp", (root / "mods" / "lamp").generic_string()}});
        SH_CHECK(owner.known && owner.mod == "lamp" && owner.relative == "scripts/main.lua");
    }
    const sh::world::Pickup* oil = sh::world::pickups().find("lamp:oil");
    SH_CHECK(oil != nullptr);
    if (oil != nullptr) {
        SH_CHECK(oil->spec.origin.line == 6 && oil->spec.origin.via == "main.lua:9");
        // The editor moves it where it lies: the height is then its own.
        SH_CHECK(oil->spec.ground);
        SH_CHECK(sh::world::pickups().set_pose("lamp:oil", {1, 2, 3}, {0, 1024, 0}));
        SH_CHECK(oil->spec.position[1] == 2 && oil->spec.rotation[1] == 1024 && !oil->spec.ground);
    }
    SH_CHECK(!sh::world::pickups().set_pose("lamp:none", {}, {}));
    sh::world::clear_script_marks("lamp", "");
    sh::world::pickups().begin_map("");
    std::error_code ec;
    fs::remove_all(root, ec);
}
