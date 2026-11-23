// The map inspector's entry snippets (engine/devtools/map_snippets.h): valid JSON
// in the key order of the gamedata files, exact metres beside the integers, and world_objects.json entries that the
// engine's own reader takes back to the same object.

#include "engine/devtools/map_snippets.h"
#include "engine/world/world_objects.h"
#include "formats/common/json.h"

#include "framework/sh_test.h"

#include <cstdlib>
#include <string>

namespace {

namespace dt = sh::devtools;

bool contains(const std::string& s, const std::string& part) { return s.find(part) != std::string::npos; }

sh::json::Value parsed(const std::string& text) {
    auto v = sh::json::parse(text);
    return v ? v.value() : sh::json::Value::null_value();
}

}  // namespace

SH_TEST(devtools_map_snippets_degrees_read_back) {
    for (int a = -32768; a <= 32767; a += 7) {
        const auto angle = static_cast<std::int16_t>(a);
        const std::string text = dt::degrees_text(angle);
        SH_CHECK(sh::world::world_degrees_to_angle(std::strtod(text.c_str(), nullptr)) == angle);
    }
    SH_CHECK(dt::degrees_text(1024) == "90");
    SH_CHECK(dt::degrees_text(0) == "0");
}

SH_TEST(devtools_map_snippets_event) {
    dt::EventEntry e;
    e.index = 30;
    e.trigger_type = 3;
    e.activation_type = 2;
    e.map_point = 22;
    e.sys_state = 5;
    e.param = 18;
    e.sfx_pair = 3;
    e.map_idx = 4;
    e.map = "map1_s02";
    const std::string door = dt::event_json(e);
    SH_CHECK(door == "{\"index\": 30, \"required_flag\": 0, \"complete_flag\": 0, \"trigger_type\": 3, "
                     "\"activation_type\": 2, \"map_point\": 22, \"required_item\": 0, \"sys_state\": 5, "
                     "\"param\": 18, \"transition_flags\": 0, \"sfx_pair\": 3, \"camera_rail\": 0, \"map\": \"map1_s02\"}");
    e.sys_state = 10;
    e.complete_flag = "M1S00_HandgunBullets0";
    const sh::json::Value v = parsed(dt::event_json(e));
    SH_CHECK(v.is_object());
    SH_CHECK(v.find("complete_flag") && v.find("complete_flag")->string() == "M1S00_HandgunBullets0");
    SH_CHECK(v.find("map_idx") && v.find("map_idx")->integer() == 4);
    SH_CHECK(v.find("map") == nullptr);
}

SH_TEST(devtools_map_snippets_point_spawn_camera_collision) {
    dt::PointEntry p;
    p.index = 1;
    p.x_q12 = 405913;
    p.z_q12 = 67174;
    p.half_width_q4 = 8;
    const sh::json::Value pv = parsed(dt::point_json(p));
    SH_CHECK(pv.find("x") && pv.find("x")->number() == 405913 / 4096.0);
    SH_CHECK(contains(dt::point_json(p), "\"x\": 99.099853515625"));
    SH_CHECK(pv.find("x_q12") && pv.find("x_q12")->integer() == 405913);

    dt::SpawnEntry s;
    s.x_q12 = 582041;
    s.z_q12 = 90931;
    s.rotation_q8 = 128;
    s.chara = "None";
    s.difficulty_min = -1;
    const std::string sj = dt::spawn_json(s);
    SH_CHECK(contains(sj, "\"rotation_deg\": 180"));
    SH_CHECK(contains(sj, "\"chara\": \"None\""));
    SH_CHECK(parsed(sj).find("difficulty_min")->integer() == -1);
    SH_CHECK(contains(dt::spawn_lua(s, "GreyChild"), "sh.chara_spawn(\"GreyChild\", 0, 582041, 90931, 2048, 0)"));

    dt::CameraEntry c;
    c.switch_area_q4 = {2204, 2305, 859, 1049};
    c.road_min_y_q4 = -96;
    const std::string cj = dt::camera_json(c);
    SH_CHECK(contains(cj, "\"switch_area\": [137.75, 144.0625, 53.6875, 65.5625]"));
    SH_CHECK(contains(cj, "\"switch_area_q4\": [2204, 2305, 859, 1049]"));
    SH_CHECK(contains(cj, "\"road_min_y\": -6"));
    SH_CHECK(parsed(cj).is_object());

    dt::CollisionEntry t{3, -12, 40, 2, 5, 3};
    SH_CHECK(dt::collision_json(t) == "{\"index\": 3, \"x\": -12, \"z\": 40, \"size_x\": 2, \"size_z\": 5, "
                                      "\"height_half_m\": 3, \"end\": false}");
}

SH_TEST(devtools_map_snippets_world_object_reads_back) {
    dt::WorldObjectEntry o;
    o.name = "GoldMedallion";
    o.model = "GOLD_HID";
    o.position = {-49971, -4505, -278036};
    o.rotation = {0, 1024, -341};
    o.zones = {{{-1}, {-2}}, {{0, 1}, {}}};
    o.flags_set = {"M1S00_GoldMedallionPlaced"};
    o.script = true;
    const std::string text = "{\"format\": \"sh-map-world-objects\", \"version\": 2, \"map\": \"map1_s00\", \"objects\": [" +
                             dt::world_object_json(o) + "]}";
    auto table = sh::world::parse_world_objects(text);
    SH_CHECK(static_cast<bool>(table));
    if (!table) return;
    SH_CHECK(table.value().objects.size() == 1);
    const sh::world::WorldObjectDef& d = table.value().objects[0];
    SH_CHECK(d.name == "GoldMedallion" && d.model == "GOLD_HID");
    SH_CHECK(d.position == o.position);
    SH_CHECK(d.rotation == o.rotation);
    SH_CHECK(d.zones.size() == 2 && d.zones[0].x_cells == std::vector<int>{-1} && d.zones[1].z_cells.empty());
    SH_CHECK(d.flags_set == o.flags_set && d.script);

    dt::WorldObjectEntry posed;
    posed.name = "SavePad";
    posed.model = "SAVE_PAD";
    posed.pose_table = "CommonWorldObjectPoses";
    posed.pose_index = 2;
    const std::string pj = dt::world_object_json(posed);
    SH_CHECK(contains(pj, "\"pose\": {\"disc_table\": \"CommonWorldObjectPoses\", \"index\": 2}"));
    SH_CHECK(!contains(pj, "position"));
}

SH_TEST(devtools_map_snippets_handler) {
    SH_CHECK(dt::handler_lua("PickupHandgunBullets", 3, "item") ==
             "-- handler slot 3 (item)\nfunction PickupHandgunBullets()\nend\n");
}
