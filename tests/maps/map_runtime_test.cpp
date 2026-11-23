/* The running map (engine/maps/map_runtime.h): map type names, the data kinds it takes from MapStore, the widths
 * values keep, the camera zone list up to its end marker, the rooms by position and the weather state. */
#include "engine/maps/map_runtime.h"
#include "engine/maps/weather_control.h"
#include "framework/sh_test.h"

#include <string>
#include <vector>

namespace md = sh::convert::mapdata;
using sh::maps::MapRuntime;

SH_TEST(map_runtime_map_type_names) {
    SH_CHECK(MapRuntime::map_type_of("THR") == 0);
    SH_CHECK(MapRuntime::map_type_of("ER") == 9);
    SH_CHECK(MapRuntime::map_type_of("ER2") == 10);
    SH_CHECK(MapRuntime::map_type_of("XXX") == 15);
    SH_CHECK(MapRuntime::map_type_of("thr") == -1);
    SH_CHECK(MapRuntime::map_type_of("") == -1);
}

SH_TEST(map_runtime_loads_only_the_kinds_found) {
    md::MapData data;
    data.header.bgmIdx = -3;
    data.header.ambientAudioIdx = 200;
    data.header.field16 = 3;
    data.header.field17 = 2;
    data.spawns.charaGroupIds[1] = 7;
    data.spawns.slots[1][2].positionX = 4096;
    data.spawns.slots[1][2].positionZ = -8192;
    data.spawns.slots[1][2].charaId = 5;
    data.spawns.slots[1][2].rotationY = 200;
    data.spawns.slots[1][2].flags = -1;
    data.spawns.slots[1][2].gameDifficultyMin = 0x0F;  // 4-bit signed: -1
    data.cameras[0].flags = 0;
    data.cameras[1].flags = 1;  // end marker

    MapRuntime rt;
    rt.begin("map1_s02");
    rt.set_map_type(4);
    MapRuntime::Kinds spawns_only;
    spawns_only.spawns = true;
    rt.load_data(data, spawns_only);
    SH_CHECK(rt.name() == "map1_s02");
    SH_CHECK(rt.map_type() == 4);
    SH_CHECK(rt.music_track() == 0 && rt.light_mode() == 0);
    SH_CHECK(rt.group_chara(1) == 7);
    const sh::maps::SpawnPoint& p = rt.spawn(1, 2);
    SH_CHECK(p.x == 4096 && p.z == -8192 && p.chara == 5 && p.rotation_y == 200 && p.flags == -1);
    SH_CHECK(p.difficulty_min == -1);
    SH_CHECK(&rt.spawn(18) == &p);
    SH_CHECK(rt.camera_zones().empty());

    MapRuntime::Kinds header_cameras;
    header_cameras.header = header_cameras.cameras = true;
    rt.load_data(data, header_cameras);
    SH_CHECK(rt.music_track() == -3);
    SH_CHECK(rt.ambient_bank() == 200);
    SH_CHECK(rt.light_mode() == 3 && rt.weather() == 2);
    SH_CHECK(rt.camera_zones().size() == 1);

    rt.begin("map0_s00");
    SH_CHECK(rt.name() == "map0_s00" && rt.map_type() == 0 && rt.group_chara(1) == 0 && rt.spawn(18).x == 0);
    SH_CHECK(rt.camera_zones().empty());
}

SH_TEST(map_runtime_setters_keep_wide_values) {
    // Music tracks, ambient banks and characters are numbers past a byte (a mod's); the setters keep them whole.
    MapRuntime rt;
    rt.set_music_track(0x1FF);
    SH_CHECK(rt.music_track() == 0x1FF);
    rt.set_ambient_bank(-1);
    SH_CHECK(rt.ambient_bank() == -1);
    rt.set_group_chara(3, 0x181);
    SH_CHECK(rt.group_chara(3) == 0x181);
    rt.set_chara_update(300, sh::maps::CharaUpdate::Behaviour);
    SH_CHECK(rt.chara_update(300) == sh::maps::CharaUpdate::Behaviour);
    SH_CHECK(rt.chara_update(44) == sh::maps::CharaUpdate::None);
    rt.set_chara_update_as(300, 299);
    SH_CHECK(rt.chara_update_as(300) == 299 && rt.chara_update_as(301) == -1);
    rt.set_chara_effects(1000, true);
    SH_CHECK(rt.chara_effects(1000) && !rt.chara_effects(999));
}

SH_TEST(map_runtime_camera_zone_widths) {
    ShMapCamera r{};
    r.switchArea[0] = -480;
    r.switchArea[3] = 960;
    r.roadArea[1] = 16;
    r.flags = 0xF6;
    r.areaSizeType = 0xFE;  // 2 bits: 2
    r.roadType = 0x0D;      // 3 bits: 5
    r.moveYType = 0x0B;     // 3 bits: 3
    r.roadMaxY = -96;
    r.roadMinY = 96;
    r.watchOffsetY = 8;
    r.lensFlare = 0x13;      // 4 bits: 3
    r.cameraMoveType = 0x0E;  // 4 bits signed: -2
    r.fixAngleX = -64;
    r.fixAngleY = 127;
    const sh::camera::CameraRoad z = sh::maps::camera_zone_of(r);
    SH_CHECK(z.switch_area.min_x == -480 && z.switch_area.max_z == 960 && z.road_area.max_x == 16);
    SH_CHECK(z.flags == 0xF6);
    SH_CHECK(z.area_size == sh::camera::AreaSize::Wide);
    SH_CHECK(static_cast<int>(z.road_type) == 5);
    SH_CHECK(z.move_y_type == 3);
    SH_CHECK(z.road_max_y == -96 && z.road_min_y == 96 && z.watch_offset_y == 8);
    SH_CHECK(z.lens_flare == 3);
    SH_CHECK(static_cast<int>(z.camera_move) == -2);
    SH_CHECK(z.fix_angle_x == -64 && z.fix_angle_y == 127);

    r.cameraMoveType = 2;
    SH_CHECK(sh::maps::camera_zone_of(r).camera_move == sh::camera::CameraMove::FixedAngle);
}

SH_TEST(map_runtime_camera_list_ends_at_the_marker) {
    ShMapCamera list[4]{};
    list[0].flags = 0x80;
    list[1].flags = 0x02;
    list[2].flags = 0x81;  // end marker with other bits
    SH_CHECK(sh::maps::camera_zones_of(list, 4).size() == 2);
    SH_CHECK(sh::maps::camera_zones_of(list, 1).size() == 1);  // no marker inside the count: stops at the count
    list[0].flags = 1;
    SH_CHECK(sh::maps::camera_zones_of(list, 4).empty());
}

namespace {

// Disc tables by name and a hook rule that answers from x: x < 0 does not run, else x / 4096 - 1.
class TestRoomsHost final : public sh::maps::MapRoomsHost {
public:
    std::vector<std::uint8_t> rooms;
    std::vector<std::uint8_t> streets;
    std::string last_map;

    std::span<const std::uint8_t> table(const std::string& map, const std::string& name) override {
        last_map = map;
        if (name == "rooms") {
            return rooms;
        }
        if (name == "street_rooms") {
            return streets;
        }
        return {};
    }
    bool call_rule(const std::string& rule, std::int32_t x, std::int32_t, long long* out) override {
        if (rule != "zone_of" || x < 0) {
            return false;
        }
        *out = x / 4096 - 1;
        return true;
    }
};

}  // namespace

SH_TEST(map_runtime_rooms_grid_and_zones) {
    TestRoomsHost host;
    host.rooms = {1, 0, 3, 4};  // 2 x 2 grid, column by column
    host.streets = {9, 0, 7};
    MapRuntime rt;
    rt.begin("map0_s00");
    SH_CHECK(!rt.has_rooms() && rt.room_at(0, 0) == 0);

    const char* json = R"({"zones": {"rule": "zone_of", "table": "street_rooms"},
                           "grid": {"min_x": -80, "max_x": 0, "min_z": 0, "max_z": 80, "empty_room": 6}})";
    SH_CHECK(rt.load_rooms("maps/map0_s00/rooms.json", json, host));
    SH_CHECK(rt.has_rooms() && host.last_map == "map0_s00");
    // Zone cells: x = 4096 -> cell 0 -> 9; 3 * 4096 -> cell 2 -> 7; cell 1 holds 0 -> the grid (outside: 0).
    SH_CHECK(rt.room_at(4096, 0) == 9);
    SH_CHECK(rt.room_at(3 * 4096, 0) == 7);
    SH_CHECK(rt.room_at(2 * 4096, 0) == 0);
    SH_CHECK(rt.room_at(9 * 4096, 0) == 0);  // cell 8: beyond the table
    // The rule does not run: the grid. Column 0 holds 1, 0 (0 reads as the empty room 6), column 1 holds 3, 4.
    SH_CHECK(rt.room_at(-80 * 4096, 0) == 1);
    SH_CHECK(rt.room_at(-80 * 4096, 40 * 4096) == 6);
    SH_CHECK(rt.room_at(-1, 79 * 4096) == 4);
    SH_CHECK(rt.room_at(-1, 80 * 4096) == 0);

    // A rule without a table gives the room itself, cut to a byte.
    SH_CHECK(rt.load_rooms("r", R"({"zones": {"rule": "zone_of"}})", host));
    SH_CHECK(rt.room_at(258 * 4096, 0) == 1);

    // Broken text or shapes: no rooms.
    SH_CHECK(!rt.load_rooms("r", "{", host) && !rt.has_rooms());
    SH_CHECK(!rt.load_rooms("r", R"({"grid": {"min_x": 0}})", host) && !rt.has_rooms());
    SH_CHECK(!rt.load_rooms("r", R"({"grid": {"min_x": 0, "max_x": 1, "min_z": 0, "max_z": 1, "empty_room": 300}})",
                            host));
    SH_CHECK(!rt.load_rooms("r", R"({"zones": {"rule": ""}})", host));
    SH_CHECK(rt.room_at(4096, 0) == 0);

    SH_CHECK(rt.load_rooms("r", "{}", host) && rt.has_rooms());
    rt.begin("map0_s01");  // a new map: no rooms until it loads its own
    SH_CHECK(!rt.has_rooms());
}

SH_TEST(map_runtime_weather_starts_clear_on_each_map) {
    MapRuntime rt;
    rt.begin("map0_s00");
    rt.weather_control().words().rain_flag = 1;
    rt.weather_control().words().snow_flag = 1;
    rt.set_wind(-5, 7);
    rt.snow_light() = sh::maps::WeatherLight{1, 2, 3, 4};
    SH_CHECK(rt.raining() && rt.snowing());
    SH_CHECK(rt.wind(0) == -5 && rt.wind(1) == 7 && rt.wind(5) == 7);
    rt.begin("map0_s01");
    SH_CHECK(!rt.raining() && !rt.snowing());
    SH_CHECK(rt.wind(0) == 0 && rt.wind(1) == 0);
    SH_CHECK(rt.snow_light().r == 0 && rt.rain_light().code == 0);
}

SH_TEST(map_runtime_variables_live_through_maps) {
    sh::maps::MapVariables vars;
    auto* a = static_cast<std::uint8_t*>(vars.block("fog", 4));
    SH_CHECK(a != nullptr && a[0] == 0 && a[sh::maps::MapVariables::kMinBlock - 1] == 0);
    a[3] = 7;
    SH_CHECK(vars.block("fog", 16) == a && a[3] == 7);
    SH_CHECK(vars.block("fog", sh::maps::MapVariables::kMinBlock + 1) == nullptr);
    auto* big = static_cast<std::uint8_t*>(vars.block("bones", 5000));
    SH_CHECK(big != nullptr && big != a && big[4999] == 0);
    SH_CHECK(vars.block("bones", 5000) == big && vars.count() == 2);
}
