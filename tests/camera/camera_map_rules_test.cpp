// The per-map camera rules (camera.json "map_rules") and their evaluation: these cases pin the edges and the
// parsing.

#include <fstream>
#include <sstream>
#include <string>

#include "engine/camera/camera_config.h"
#include "engine/camera/camera_map_rules.h"

#include "framework/sh_test.h"

namespace cam = sh::camera;

namespace {

std::string read_text(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

cam::MapPlace place(const char* type, int occurrence, const char* map = "", int room = -1) {
    cam::MapPlace p;
    p.map_type = type;
    p.map_type_occurrence = occurrence;
    p.map = map;
    p.room = room;
    return p;
}

cam::CameraRoad point_road(std::int16_t x, std::int16_t z) {
    cam::CameraRoad r;
    r.road_area.min_x = x;
    r.road_area.max_x = x;
    r.road_area.min_z = z;
    r.road_area.max_z = z;
    return r;
}

}  // namespace

SH_TEST(camera_map_rules_shipped_file_is_the_builtin) {
    const std::string text = read_text(std::string(SH_PROJECT_ROOT) + "/gamedata/config/camera.json");
    const auto parsed = cam::parse_camera_config(text);
    SH_CHECK(parsed);
    const cam::MapCameraRules& m = parsed.value().map_rules;
    const cam::MapCameraRules d;
    SH_CHECK(m.self_view_blocks.size() == 2 && d.self_view_blocks.size() == 2);
    for (std::size_t i = 0; i < 2; ++i) {
        const cam::SelfViewBlock& a = m.self_view_blocks[i];
        const cam::SelfViewBlock& b = d.self_view_blocks[i];
        SH_CHECK(a.map_type.tag == b.map_type.tag && a.map_type.occurrence == b.map_type.occurrence);
        SH_CHECK(a.min_x == b.min_x && a.max_x == b.max_x && a.min_z == b.min_z && a.max_z == b.max_z);
    }
    // Q12(201.8f) truncates in single precision: 826572, not the double product's rounding.
    SH_CHECK(m.self_view_blocks[0].min_x == 826572 && m.self_view_blocks[0].max_x == 942080);
    SH_CHECK(m.self_view_blocks[1].min_x == -942080 && m.self_view_blocks[1].max_x == -823296);
    SH_CHECK(m.tight_doorway_types.size() == 3);
    SH_CHECK(m.tight_doorway_types[0].tag == "ER" && m.tight_doorway_types[0].occurrence == 2);
    SH_CHECK(m.tight_doorway_types[1].tag == "HP" && m.tight_doorway_types[1].occurrence == 0);
    SH_CHECK(m.road_anchor_shifts.size() == 1);
    SH_CHECK(m.road_anchor_shifts[0].map == "map3_s01" && m.road_anchor_shifts[0].room == 10);
    SH_CHECK(m.road_anchor_shifts[0].road_x == 281 && m.road_anchor_shifts[0].road_z == 955);
    SH_CHECK(m.road_anchor_shifts[0].shift_x == 1024);
    SH_CHECK(m.wide_shots.size() == 1);
    SH_CHECK(m.wide_shots[0].map == "map2_s00" && m.wide_shots[0].room == 23);
    SH_CHECK(m.wide_shots[0].anchor_x == -769024 && m.wide_shots[0].anchor_z == 1243136);
    SH_CHECK(m.wide_shots[0].tolerance == 4096);
}

SH_TEST(camera_map_rules_parse_errors) {
    SH_CHECK(!cam::parse_camera_config(R"({"map_rules": []})"));
    SH_CHECK(!cam::parse_camera_config(R"({"map_rules": {"tight_doorways": "HP"}})"));
    SH_CHECK(!cam::parse_camera_config(R"({"map_rules": {"tight_doorways": ["ER#0"]}})"));
    SH_CHECK(!cam::parse_camera_config(R"({"map_rules": {"tight_doorways": ["#2"]}})"));
    SH_CHECK(!cam::parse_camera_config(R"({"map_rules": {"tight_doorways": ["ER#x"]}})"));
    SH_CHECK(!cam::parse_camera_config(R"({"map_rules": {"self_view_blocked": [{"map_type": "THR", "min_x": 1}]}})"));
    SH_CHECK(!cam::parse_camera_config(R"({"map_rules": {"wide_shots": [{"map": "", "room": 1}]}})"));
    SH_CHECK(!cam::parse_camera_config(
        R"({"map_rules": {"road_anchor_shifts": [{"map": "m", "room": 200, "road_x": 0, "road_z": 0, "shift_x": 0}]}})"));
    // A list given replaces the shipped one; the others stay.
    const auto p = cam::parse_camera_config(R"({"map_rules": {"tight_doorways": [], "wide_shots": []}})");
    SH_CHECK(p);
    SH_CHECK(p.value().map_rules.tight_doorway_types.empty() && p.value().map_rules.wide_shots.empty());
    SH_CHECK(p.value().map_rules.self_view_blocks.size() == 2 && p.value().map_rules.road_anchor_shifts.size() == 1);
}

SH_TEST(camera_map_rules_self_view_block_edges) {
    const cam::MapCameraRules r;
    const cam::MapPlace thr = place("THR", 1);
    SH_CHECK(cam::self_view_blocked(r, thr, 826572, -376832));
    SH_CHECK(cam::self_view_blocked(r, thr, 942080, -278528));
    SH_CHECK(!cam::self_view_blocked(r, thr, 826571, -300000));
    SH_CHECK(!cam::self_view_blocked(r, thr, 942081, -300000));
    SH_CHECK(!cam::self_view_blocked(r, thr, 900000, -376833));
    SH_CHECK(!cam::self_view_blocked(r, thr, 900000, -278527));
    SH_CHECK(!cam::self_view_blocked(r, thr, -900000, -300000));  // below the stretch
    SH_CHECK(!cam::self_view_blocked(r, place("SPR", 1), 900000, -300000));
    const cam::MapPlace spr = place("SPR", 1);
    SH_CHECK(cam::self_view_blocked(r, spr, -942080, 49152));
    SH_CHECK(cam::self_view_blocked(r, spr, -823296, -49152));
    SH_CHECK(!cam::self_view_blocked(r, spr, -823295, 0));
    SH_CHECK(!cam::self_view_blocked(r, spr, -942081, 0));
    SH_CHECK(!cam::self_view_blocked(r, place("", 0), -900000, 0));
}

SH_TEST(camera_map_rules_map_type_selectors) {
    const cam::MapCameraRules r;
    SH_CHECK(!cam::tight_doorways(r, place("ER", 1)));
    SH_CHECK(cam::tight_doorways(r, place("ER", 2)));
    SH_CHECK(cam::tight_doorways(r, place("HP", 1)));
    SH_CHECK(cam::tight_doorways(r, place("HU", 1)));
    SH_CHECK(!cam::tight_doorways(r, place("THR", 1)));
    SH_CHECK(!cam::tight_doorways(r, place("", 0)));
    cam::MapTypeSelector any{"ER", 0};
    SH_CHECK(cam::map_type_matches(any, place("ER", 1)) && cam::map_type_matches(any, place("ER", 2)));
}

SH_TEST(camera_map_rules_road_anchor_and_wide_shot) {
    const cam::MapCameraRules r;
    const std::vector<cam::CameraRoad> roads{point_road(0, 0), point_road(281, 955), point_road(281, 956),
                                             point_road(281, 955)};
    const auto shifts = cam::road_anchor_shifts(r, place("SC", 1, "map3_s01", 10), roads);
    SH_CHECK(shifts.size() == 2);
    SH_CHECK(shifts[0].road == 1 && shifts[0].x_shift == 1024 && shifts[1].road == 3);
    SH_CHECK(cam::road_anchor_shifts(r, place("SC", 1, "map3_s01", 11), roads).empty());
    SH_CHECK(cam::road_anchor_shifts(r, place("SC", 1, "map3_s02", 10), roads).empty());

    cam::HorPlusWidths wide{true, 160.0f, 213.0f};
    const cam::MapPlace alley = place("THR", 1, "map2_s00", 23);
    const cam::WideShot w = cam::wide_shot(r, alley, wide, 256);
    SH_CHECK(w.enabled && w.anchor_x == -769024 && w.anchor_z == 1243136 && w.tolerance == 4096);
    SH_CHECK(w.yaw_delta > 0);
    SH_CHECK(!cam::wide_shot(r, alley, wide, 0).enabled);
    SH_CHECK(!cam::wide_shot(r, alley, cam::HorPlusWidths{true, 160.0f, 160.0f}, 256).enabled);
    SH_CHECK(!cam::wide_shot(r, alley, cam::HorPlusWidths{false, 160.0f, 213.0f}, 256).enabled);
    SH_CHECK(!cam::wide_shot(r, place("THR", 1, "map2_s00", 22), wide, 256).enabled);
}

SH_TEST(camera_enemies_by_name_from_the_shipped_file) {
    const std::string text = read_text(std::string(SH_PROJECT_ROOT) + "/gamedata/config/camera.json");
    const auto config = cam::parse_camera_config(text);
    SH_CHECK(config);
    SH_CHECK(config.value().warnings.empty());
    // The original's character numbers 2..24 (Air Screamer..Monster Cybil), 7..9 always active.
    const auto& e = config.value().enemy_characters;
    SH_CHECK(e.size() == 23 && e.front() == "AirScreamer" && e.back() == "MonsterCybil");
    const auto& a = config.value().always_active_characters;
    SH_CHECK(a.size() == 3 && a[0] == "Stalker" && a[1] == "GreyChild" && a[2] == "Mumbler");
}

SH_TEST(camera_enemies_keys_are_checked) {
    auto c = cam::parse_camera_config(R"({"view": {}})");
    SH_CHECK(c && c.value().enemy_characters.empty() && c.value().warnings.size() == 1);
    c = cam::parse_camera_config(R"({"enemies": {"characters": ["Stalker"]}})");
    SH_CHECK(!c && c.error().find("enemies.always_active is missing") != std::string::npos);
    c = cam::parse_camera_config(R"({"enemies": {"characters": [], "always_active": [], "ranged": []}})");
    SH_CHECK(!c && c.error().find("unknown key \"ranged\"") != std::string::npos);
    c = cam::parse_camera_config(R"({"enemies": {"characters": ["Romper"], "always_active": ["Stalker"]}})");
    SH_CHECK(!c && c.error().find("not in enemies.characters") != std::string::npos);
    c = cam::parse_camera_config(R"({"enemies": {"characters": [], "always_active": []}, "zoom": 1})");
    SH_CHECK(c && c.value().warnings.size() == 1 && c.value().warnings[0].find("\"zoom\"") != std::string::npos);
}
