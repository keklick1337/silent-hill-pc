// The world objects reader (engine/world/world_objects.h) — the cell test against the
// original chunk macro, the float conversions, the selection by zone and flags, the parse errors,
// and the version 2 files in gamedata/maps against the original's literals.

#include "engine/world/object_queue.h"
#include "engine/world/world_objects.h"
#include "framework/sh_test.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <string>
#include <vector>

namespace {

namespace sw = sh::world;

// PLAYER_IN_MAP_CHUNK(comp, x0, x1, x2, x3) of the original, on one coordinate.
bool chunk_macro(std::int32_t p, int x0, int x1, int x2, int x3) {
    const std::int32_t idx = p / (40 * 4096);
    return (p > 0 && idx + x0 == x1) || (p <= 0 && idx + x2 == x3);
}

// The cells a macro call stands for: the positive branch is chunk x1 - x0 (possible when >= 0),
// the non-positive branch chunk x3 - x2 (possible when <= 0), which is cell x3 - x2 - 1.
std::vector<int> macro_cells(int x0, int x1, int x2, int x3) {
    std::vector<int> cells;
    if (x1 - x0 >= 0) {
        cells.push_back(x1 - x0);
    }
    if (x3 - x2 <= 0) {
        cells.push_back(x3 - x2 - 1);
    }
    return cells;
}

std::int32_t q12(float x) {
    volatile float v = x;
    return static_cast<std::int32_t>(v * static_cast<float>(1 << 12));
}

std::int16_t angle(float deg) {
    volatile float v = deg;
    return static_cast<std::int16_t>(static_cast<std::int32_t>((v / 360.0f) * static_cast<float>(1 << 12)));
}

sw::WorldObjectFlagGet flags_of(std::map<std::string, bool> state) {
    return [state = std::move(state)](const std::string& name) {
        const auto it = state.find(name);
        return it != state.end() && it->second;
    };
}

std::string doc(const std::string& objects, int version = 2) {
    return R"({"format":"sh-map-world-objects","version":)" + std::to_string(version) +
           R"(,"map":"test","objects":[)" + objects + "]}";
}

}  // namespace

SH_TEST(world_objects_cells_match_chunk_macro) {
    // Every argument set the maps use, plus both-branch and impossible ones.
    const int args[][4] = {{1, 1, -1, 1}, {1, 3, -1, 3}, {1, 4, -1, 4}, {0, 0, -1, 1}, {1, -1, 0, 0},
                           {1, 2, -1, 2}, {1, -2, -1, -2}, {1, -5, -1, -5}, {0, 0, 0, 0}, {0, 1, 0, -1}};
    const std::int32_t edges[] = {0, 1, -1, 40 * 4096, -40 * 4096, 40 * 4096 - 1, -40 * 4096 + 1,
                                  80 * 4096, -80 * 4096, 123 * 4096 + 77, -123 * 4096 - 77, 200 * 4096,
                                  -200 * 4096};
    for (const auto& a : args) {
        const std::vector<int> cells = macro_cells(a[0], a[1], a[2], a[3]);
        for (std::int32_t p = -260 * 4096; p <= 260 * 4096; p += 4096 * 3 + 517) {
            bool any = false;
            for (int c : cells) {
                any = any || sw::world_position_in_cell(p, c);
            }
            SH_CHECK(any == chunk_macro(p, a[0], a[1], a[2], a[3]));
        }
        for (std::int32_t p : edges) {
            bool any = false;
            for (int c : cells) {
                any = any || sw::world_position_in_cell(p, c);
            }
            SH_CHECK(any == chunk_macro(p, a[0], a[1], a[2], a[3]));
        }
    }
    // 0 is in cell -1 and 40 m is in cell 1 (truncating division of the original).
    SH_CHECK(sw::world_position_in_cell(0, -1));
    SH_CHECK(!sw::world_position_in_cell(0, 0));
    SH_CHECK(sw::world_position_in_cell(40 * 4096, 1));
    SH_CHECK(sw::world_position_in_cell(-40 * 4096, -2));
}

// The projected cells of the CELL_XZ switches (map2_s00, map3_s03, map3_s04, map7_s01,
// map7_s02, ...): p = position / 40 m (truncated) + 17 for a positive position, + 15 otherwise. In
// the file's cells p >= 17 is cell p - 17 and p <= 15 is cell p - 16; p = 16 never happens.
SH_TEST(world_objects_cells_match_projected_cells) {
    for (std::int32_t pos = -700 * 4096; pos <= 700 * 4096; pos += 4096 + 311) {
        for (const std::int32_t p : {pos, pos - pos % (40 * 4096), pos - pos % (40 * 4096) + 1}) {
            const std::int32_t proj = p / (40 * 4096) + (p > 0 ? 17 : 15);
            SH_CHECK(proj != 16);
            for (int q = -2; q <= 34; ++q) {
                if (q == 16) {
                    continue;
                }
                const int cell = q >= 17 ? q - 17 : q - 16;
                SH_CHECK(sw::world_position_in_cell(p, cell) == (proj == q));
            }
        }
    }
}

// PLAYER_IN_MAP_CHUNK_2(comp, x0, x1, x2, x3) — "chunk + x0 < x1" — is the union of the macro
// cells below the bound on each branch; checked on a bounded range of cells.
SH_TEST(world_objects_cells_match_chunk_macro_2) {
    const int args[][4] = {{1, 4, -1, 4}, {1, 3, -1, 3}, {0, 1, -1, 0}, {1, -2, -1, -2}};
    for (const auto& a : args) {
        for (std::int32_t p = -500 * 4096; p <= 500 * 4096; p += 4096 + 173) {
            const std::int32_t idx = p / (40 * 4096);
            const bool macro = (p > 0 && idx + a[0] < a[1]) || (p <= 0 && idx + a[2] < a[3]);
            bool any = false;
            for (int c = -14; c <= 14; ++c) {
                // positive branch: chunk c >= 0 with c + x0 < x1; non-positive: cell c < 0 is chunk c + 1.
                const bool in = c >= 0 ? (c + a[0] < a[1]) : (c + 1 + a[2] < a[3]);
                any = any || (in && sw::world_position_in_cell(p, c));
            }
            SH_CHECK(any == macro);
        }
    }
}

SH_TEST(world_objects_float_conversions) {
    const float metres[] = {19.7f, -19.1f, 96.9405f, -0.3365f, 21.092f, 0.0f, -0.002f, 95.734f, -0.8062f,
                            99.8885f, -119.8019f, 107.861f, 134.6f, -18.85f, 3.06f, -21.16f};
    for (float m : metres) {
        SH_CHECK(sw::world_metres_to_q12(static_cast<double>(m)) == q12(m));
    }
    SH_CHECK(sw::world_metres_to_q12(134.6) == 551321);  // the float product, not the double one
    const float degrees[] = {163.8f, 180.0f, -35.9f, -90.0f, 111.6f, 66.8f, 92.9f, -37.45f, 162.6f, 120.0f, 0.5f};
    for (float d : degrees) {
        SH_CHECK(sw::world_degrees_to_angle(static_cast<double>(d)) == angle(d));
    }
    SH_CHECK(sw::world_degrees_to_angle(120.0) == 1365);
    SH_CHECK(sw::world_degrees_to_angle(0.5) == 5);
}

SH_TEST(world_objects_selection) {
    const auto parsed = sw::parse_world_objects(doc(
        R"({"name":"Pad","model":"PAD_NEAR","position":[1,0,1],"rotation":[0,90,0],"zones":[{"x":0,"z":3}]},)"
        R"({"name":"Map","model":"MAP_HIDE","position":[2,0,2],"zones":[{"x":0,"z":3}],"flags_clear":["Taken"]},)"
        R"({"name":"MapTaken","model":"MAP2_HID","position":[2,0,2],"zones":[{"x":0,"z":3}],"flags_set":["Taken"]},)"
        R"({"name":"Key","model":"KEY_HIDE","position":[3,0,3],"zones":[{"x":[2,-3]},{"z":-1}],"flags_set":["A"],"flags_clear":["B"]},)"
        R"({"name":"Anywhere","model":"FAN0_HID","position":[0,0,0]})"));
    SH_CHECK(static_cast<bool>(parsed));
    if (!parsed) {
        return;
    }
    const sw::WorldObjectTable& t = parsed.value();
    SH_CHECK(t.objects.size() == 5);
    SH_CHECK(t.objects[0].rotation[1] == 1024);
    SH_CHECK(t.objects[0].position[0] == 4096);

    const std::int32_t x0 = 10 * 4096, z3 = 130 * 4096;
    auto names = [&](std::int32_t x, std::int32_t z, std::map<std::string, bool> f) {
        std::string out;
        for (const sw::WorldObjectDef* d : sw::visible_objects(t, x, z, flags_of(std::move(f)))) {
            out += d->name + ";";
        }
        return out;
    };
    SH_CHECK(names(x0, z3, {}) == "Pad;Map;Anywhere;");
    SH_CHECK(names(x0, z3, {{"Taken", true}}) == "Pad;MapTaken;Anywhere;");
    SH_CHECK(names(90 * 4096, 0, {{"A", true}}) == "Key;Anywhere;");          // x cell 2, any z
    SH_CHECK(names(-100 * 4096, 0, {{"A", true}}) == "Key;Anywhere;");        // x cell -3
    SH_CHECK(names(500 * 4096, -4096, {{"A", true}}) == "Key;Anywhere;");     // second zone: z cell -1
    SH_CHECK(names(90 * 4096, 0, {{"A", true}, {"B", true}}) == "Anywhere;");  // flags_clear fails
    SH_CHECK(names(90 * 4096, 0, {}) == "Anywhere;");                          // flags_set fails
}

SH_TEST(world_objects_parse_errors) {
    SH_CHECK(!sw::parse_world_objects(doc(R"({"name":"A","model":"M","position":[0,0,0]})", 1)));
    SH_CHECK(!sw::parse_world_objects(doc(R"({"name":"A","model":"M"})")));                      // no position
    SH_CHECK(!sw::parse_world_objects(doc(R"({"name":"A","model":"M","position":[0,0]})")));     // two numbers
    SH_CHECK(!sw::parse_world_objects(doc(R"({"name":"A","model":"TOOLONGNAME","position":[0,0,0]})")));
    SH_CHECK(!sw::parse_world_objects(doc(R"({"name":"A","model":"M","position":[0,0,0],"zone":[]})")));
    SH_CHECK(!sw::parse_world_objects(doc(R"({"name":"A","model":"M","position":[0,0,0],"zones":[{"y":1}]})")));
    SH_CHECK(!sw::parse_world_objects(doc(R"({"name":"A","model":"M","position":[0,0,0],"flags_set":[3]})")));
    SH_CHECK(!sw::parse_world_objects(
        doc(R"({"name":"A","model":"M","position":[0,0,0]},{"name":"A","model":"N","position":[0,0,0]})")));
    SH_CHECK(static_cast<bool>(
        sw::parse_world_objects(doc(R"({"name":"A","model":"M","position":[0,0,0],"_note":"free text"})"))));
}

// The version 2 files against the float literals of the original's world object set-up, in file order.
SH_TEST(world_objects_gamedata_files) {
    struct Want {
        const char* model;
        float px, py, pz, rx, ry, rz;
    };
    const std::map<std::string, std::vector<Want>> maps = {
        {"map1_s06",
         {{"MAP_HIDE", 96.9405f, -0.3365f, 21.092f, 0.0f, 66.8f, 0.0f},
          {"PAD_NEAR", 61.8531f, -0.805f, 140.61f, 0, 92.9f, 0.0f},
          {"REDX_HID", 20.4568f, -0.8345f, -17.97f, 0, 0, 0},
          {"KEY_HIDE", 20.9021f, -0.002f, -17.26f, 0.0f, -37.45f, 0.0f},
          {"PICT00_H", 133.294f, -0.8614f, 19.04f, 0.0f, 162.6f, 0.0f}}},
        {"map3_s06",
         {{"PAD_NEAR", 26.0f, -1.02f, 144.55f, 0.0f, 163.8f, 0.0f},
          {"MAP_HIDE", 24.6f, -1.56f, 143.16f, 0.0f, 180.0f, 0.0f},
          {"MAP2_HID", 24.6f, -1.56f, 143.16f, 0.0f, 180.0f, 0.0f},
          {"AIDKIT_N", 27.6f, -0.45f, 143.6f, 0.0f, -90.0f, 0.0f},
          {"KEY_HIDE", 95.734f, -0.8062f, 99.8885f, 0.0f, -35.9f, 0.0f}}},
        {"map4_s04",
         {{"PAD_NEAR", 106.687f, -1.02f, 144.601f, 0.0f, 163.0f, 0.0f},
          {"ISU_HIDE", 57.8f, 0.0f, 0.0f, 0, 0, 0}}},
        {"map4_s05",
         {{"KIDN05_H", -114.89f, 5.0f, 108.36f, 0, 0, 0},
          {"KIDN04_H", -119.8019f, 5.0f, 107.861f, 0, 0, 0}}},
        {"map6_s02", {{"PAD_NEAR", -18.85f, 3.06f, -21.16f, 0.0f, 111.6f, 0.0f}}},
        {"map3_s00",
         {{"TRUNK_HI", 97.2496f, 0.0f, 100.3428f, 0, 0, 0},
          {"BIRD_HID", 98.4512f, 0.0f, 101.087f, 0, 0, 0},
          {"BIRD2_HI", 98.4512f, 0.0f, 101.087f, 0, 0, 0},
          {"PAD_NEAR", 26.0f, -1.0198f, 144.5498f, 0.0f, 163.8f, 0.0f},
          {"MAP2_HID", 24.6f, -1.56f, 143.16f, 0.0f, 180.0f, 0.0f},
          {"MAP_HIDE", 24.6f, -1.56f, 143.16f, 0.0f, 180.0f, 0.0f},
          {"AIDKIT_N", 27.6f, -0.45f, 143.6f, 0.0f, -90.0f, 0.0f}}},
        // pickups: pose from the disc, zero here
        {"map5_s01",
         {{"RSRMAP_H", -46.5f, 0.0f, 2.5f, 0.0f, 0.0f, 0.0f},
          {"FOOK_HID", 52.15f, -1.359f, -57.925f, 0.0f, 0.0f, 5.8f},
          {"SHELL_NE", 0, 0, 0, 0, 0, 0},
          {"BULLET_N", 0, 0, 0, 0, 0, 0},
          {"AIDKIT_N", 0, 0, 0, 0, 0, 0},
          {"DRINK_NE", 0, 0, 0, 0, 0, 0},
          {"SHELL_NE", 0, 0, 0, 0, 0, 0}}},
        // pickups: pose from the disc, zero here
        {"map2_s04",
         {{"PAD_NEAR", -60.9976f, -0.8289f, 20.1067f, 0.0f, -100.7f, 0.0f},
          {"SHOTGUN_", -59.484f, -0.8443f, 18.1517f, 0.0f, -23.3f, 0.0f},
          {"MAP_HIDE", -20.0f, 0.0f, 20.45f, 0.0f, 180.0f, 0.0f},
          {"PAD_NEAR", -102.8699f, -1.25f, 57.15f, 0.0f, -117.55f, 0.0f},
          {"SHOT_NEA", 0, 0, 0, 0, 0, 0},
          {"BULLET_N", 0, 0, 0, 0, 0, 0},
          {"BULLET_N", 0, 0, 0, 0, 0, 0},
          {"SHOT_NEA", 0, 0, 0, 0, 0, 0},
          {"BULLET_N", 0, 0, 0, 0, 0, 0},
          {"BULLET_N", 0, 0, 0, 0, 0, 0},
          {"SHELL_NE", 0, 0, 0, 0, 0, 0}}},
        // pickups: pose from the disc, zero here
        {"map0_s02",
         {{"PAD_NEAR", -100.47f, -1.575f, 22.22f, 0.0f, -75.8f, 0.0f},
          {"WINR_0_N", -100.0f, 0.0f, 20.0f, 0.0f, 0.0f, 0.0f},
          {"WINU_0_N", -100.0f, 0.0f, 20.0f, 0.0f, 0.0f, 0.0f},
          {"WINR_1_H", -100.0f, 0.0f, 20.0f, 0.0f, 0.0f, 0.0f},
          {"WINU_1_H", -100.0f, 0.0f, 20.0f, 0.0f, 0.0f, 0.0f},
          {"WINR_2_H", -100.0f, 0.0f, 20.0f, 0.0f, 0.0f, 0.0f},
          {"WINU_2_H", -100.0f, 0.0f, 20.0f, 0.0f, 0.0f, 0.0f},
          {"WINR_4_N", -100.0f, 0.0f, 20.0f, 0.0f, 0.0f, 0.0f},
          {"WINU_4_N", -100.0f, 0.0f, 20.0f, 0.0f, 0.0f, 0.0f},
          {"PAD_NEAR", -178.08f, -0.705f, 22.0298f, 0.0f, 12.1f, 0.0f},
          {"GAS_HIDE", -177.2998f, -1.2522f, 21.125f, 0.0f, -145.55f, 0.0f},
          {"PAD_NEAR", -146.4248f, -0.911f, 21.325f, 0.0f, 8.0f, 0.0f},
          {"COSMO_HI", -145.75f, -0.92f, 18.3274f, 0.0f, -55.1f, 0.0f},
          {"KATANA1_", -140.8628f, -0.5511f, 62.3404f, 0.0f, 0.0f, 0.0f},
          {"AIDKIT_N", 0, 0, 0, 0, 0, 0},
          {"DRINK_NE", 0, 0, 0, 0, 0, 0},
          {"DRINK_NE", 0, 0, 0, 0, 0, 0},
          {"DRINK_NE", 0, 0, 0, 0, 0, 0},
          {"BULLET_N", 0, 0, 0, 0, 0, 0},
          {"DRINK_NE", 0, 0, 0, 0, 0, 0},
          {"DRINK_NE", 0, 0, 0, 0, 0, 0}}},
        // pickups: pose from the disc, zero here
        {"map2_s02",
         {{"DRINK_NE", 0, 0, 0, 0, 0, 0},
          {"DRINK_NE", 0, 0, 0, 0, 0, 0},
          {"BULLET_N", 0, 0, 0, 0, 0, 0},
          {"BULLET_N", 0, 0, 0, 0, 0, 0},
          {"DRINK_NE", 0, 0, 0, 0, 0, 0}}},
        // pickups: pose from the disc, zero here
        {"map5_s00",
         {{"MAP_NEAR", 41.2f, -0.7f, -48.7f, 0.0f, -100.1f, 0.0f},
          // Port addition: the Sewer Key's item model on the office key board.
          {"SEWERKEY", 42.5f, -1.5f, -48.115f, 90.0f, 0.0f, 0.0f},
          {"PAD_NEAR", 40.503f, -0.709f, -48.7925f, 0.0f, 5.7f, 0.0f},
          {"PAD_NEAR", -86.469f, -1.041f, -103.4905f, 0.0f, 206.3f, 0.0f},
          {"SHOT_NEA", 0, 0, 0, 0, 0, 0},
          {"DRINK_NE", 0, 0, 0, 0, 0, 0},
          {"BULLET_N", 0, 0, 0, 0, 0, 0},
          {"SHELL_NE", 0, 0, 0, 0, 0, 0},
          {"AIDKIT_N", 0, 0, 0, 0, 0, 0},
          {"AIDKIT_N", 0, 0, 0, 0, 0, 0},
          {"DRINK_NE", 0, 0, 0, 0, 0, 0},
          {"BULLET_N", 0, 0, 0, 0, 0, 0}}},
        // pickups: pose from the disc, zero here
        {"map6_s01",
         {{"PAD_NEAR", -19.237060546875f, -0.90576171875f, 60.533f, 0.0f, 78.93f, 0.0f},
          {"DOOR2_HI", -21.38f, -0.95f, 58.7f, 0.0f, 180.0f, 0.0f},
          {"BRI3_HID", -21.29f, -1.434814453125f, 60.31f, 0.0f, 180.0f, 0.0f},
          {"BAR1_HID", -21.31f, -1.71f, 59.87f, 0.0f, 180.0f, 0.0f},
          {"WID1_HID", -21.38f, -0.95f, 57.8f, 0.0f, 180.0f, 0.0f},
          {"WID2_HID", -21.38f, -0.95f, 60.05f, 0.0f, 180.0f, 0.0f},
          {"GLAS2_HI", -21.4f, -1.4208984375f, 58.77f, 0.0f, 180.0f, 0.0f},
          {"RIGHT_HI", -19.6f, -0.825f, 59.4f, 0.0f, 180.0f, 0.0f},
          {"SHELL_NE", 0, 0, 0, 0, 0, 0},
          {"BULLET_N", 0, 0, 0, 0, 0, 0},
          {"DRINK_NE", 0, 0, 0, 0, 0, 0}}},
        // pickups: pose from the disc, zero here
        {"map1_s05",
         {{"SPHERE_H", 19.7f, 1.58f, -19.1f, 0.0f, 0.0f, 0.0f},
          {"WHEEL_HI", 19.7f, -0.06f, -19.1f, 0.0f, 0.0f, 0.0f}}},
        // Port bug fixed: the drink is DRINK_NE at its disc pose (zero here), as in the original.
        {"map3_s01",
         {{"BOT_HIDE", 20.3f, 0.0f, 59.25f, 0, 0, 0},
          {"MAP_HIDE", 60.0f, 0.0f, 60.0f, 0.0f, 9.7f, 0.0f},
          {"KEY_HIDE", 100.3f, 0.0f, 58.9f, 0.0f, 72.8f, 0.0f},
          {"DR1L_HID", 59.1f, 0.0f, 19.1f, 0, 0, 0},
          {"DR1R_HID", 59.1f, 0.0f, 19.1f, 0, 0, 0},
          {"DR3L_HID", 59.1f, 0.0f, 19.1f, 0, 0, 0},
          {"DR3R_HID", 59.1f, 0.0f, 19.1f, 0, 0, 0},
          {"DR2L_HID", 59.1f, 0.0f, 19.1f, 0, 0, 0},
          {"DR2R_HID", 59.1f, 0.0f, 19.1f, 0, 0, 0},
          {"BT4_HIDE", 59.1f, 0.0f, 19.1f, 0, 0, 0},
          {"BT3_HIDE", 59.1f, 0.0f, 19.1f, 0, 0, 0},
          {"BT3_HIDE", 59.1f, 0.0f, 19.1f, 0, 0, 0},
          {"PAD_NEAR", 26.0f, -1.0198f, 144.5498f, 0.0f, 163.8f, 0.0f},
          {"MAP_HIDE", 24.6f, -1.56f, 143.16f, 0.0f, 180.0f, 0.0f},
          {"MAP2_HID", 24.6f, -1.56f, 143.16f, 0.0f, 180.0f, 0.0f},
          {"AIDKIT_N", 27.6f, -0.45f, 143.6f, 0.0f, -90.0f, 0.0f},
          {"DRINK_NE", 0, 0, 0, 0, 0, 0}}},
    };
    const std::filesystem::path root = std::filesystem::path(SH_PROJECT_ROOT) / "gamedata" / "maps";
    for (const auto& [map, want] : maps) {
        const auto loaded = sw::load_world_objects(root / map / "world_objects.json");
        SH_CHECK(static_cast<bool>(loaded));
        if (!loaded) {
            continue;
        }
        const sw::WorldObjectTable& t = loaded.value();
        SH_CHECK(t.map == map);
        SH_CHECK(t.objects.size() == want.size());
        for (std::size_t i = 0; i < want.size() && i < t.objects.size(); ++i) {
            const sw::WorldObjectDef& d = t.objects[i];
            SH_CHECK(d.model == want[i].model);
            SH_CHECK(d.position[0] == q12(want[i].px));
            SH_CHECK(d.position[1] == q12(want[i].py));
            SH_CHECK(d.position[2] == q12(want[i].pz));
            SH_CHECK(d.rotation[0] == angle(want[i].rx));
            SH_CHECK(d.rotation[1] == angle(want[i].ry));
            SH_CHECK(d.rotation[2] == angle(want[i].rz));
        }
    }
}

// The pickups: which disc pose each one takes and whether the common in-range test is asked for it.
SH_TEST(world_objects_gamedata_poses) {
    struct Want {
        const char* map;
        const char* name;
        const char* model;
        int index;
        bool near;
    };
    const Want want[] = {
        {"map5_s01", "RifleShells0", "SHELL_NE", 0, true},
        {"map5_s01", "HandgunBullets", "BULLET_N", 1, true},
        {"map5_s01", "FirstAidKit", "AIDKIT_N", 2, false},
        {"map5_s01", "HealthDrink", "DRINK_NE", 3, true},
        {"map5_s01", "RifleShells1", "SHELL_NE", 4, true},
        {"map2_s04", "ShotgunShells0", "SHOT_NEA", 0, true},
        {"map2_s04", "HandgunBullets0", "BULLET_N", 1, true},
        {"map2_s04", "HandgunBullets1", "BULLET_N", 2, true},
        {"map2_s04", "ShotgunShells1", "SHOT_NEA", 3, true},
        {"map2_s04", "HandgunBullets2", "BULLET_N", 4, true},
        {"map2_s04", "HandgunBullets3", "BULLET_N", 5, true},
        {"map2_s04", "RifleShells", "SHELL_NE", 6, true},
        {"map0_s02", "FirstAidKit", "AIDKIT_N", 0, true},
        {"map0_s02", "HealthDrink0", "DRINK_NE", 1, true},
        {"map0_s02", "HealthDrink1", "DRINK_NE", 2, true},
        {"map0_s02", "HealthDrink2", "DRINK_NE", 3, true},
        {"map0_s02", "HandgunBullets", "BULLET_N", 4, true},
        {"map0_s02", "HealthDrink3", "DRINK_NE", 5, true},
        {"map0_s02", "HealthDrink4", "DRINK_NE", 6, true},
        {"map2_s02", "HealthDrink0", "DRINK_NE", 0, true},
        {"map2_s02", "HealthDrink1", "DRINK_NE", 1, true},
        {"map2_s02", "HandgunBullets0", "BULLET_N", 2, true},
        {"map2_s02", "HandgunBullets1", "BULLET_N", 3, true},
        {"map2_s02", "HealthDrink2", "DRINK_NE", 4, true},
        {"map5_s00", "ShotgunShells", "SHOT_NEA", 0, false},
        {"map5_s00", "HealthDrink0", "DRINK_NE", 1, false},
        {"map5_s00", "HandgunBullets0", "BULLET_N", 2, false},
        {"map5_s00", "RifleShells", "SHELL_NE", 3, false},
        {"map5_s00", "FirstAidKit0", "AIDKIT_N", 4, false},
        {"map5_s00", "FirstAidKit1", "AIDKIT_N", 5, false},
        {"map5_s00", "HealthDrink1", "DRINK_NE", 6, false},
        {"map5_s00", "HandgunBullets1", "BULLET_N", 7, false},
        {"map6_s01", "RifleShells", "SHELL_NE", 0, true},
        {"map6_s01", "HandgunBullets", "BULLET_N", 1, true},
        {"map6_s01", "HealthDrink", "DRINK_NE", 2, true},
        {"map3_s01", "HealthDrink", "DRINK_NE", 0, false},
        {"map1_s01", "HandgunBullets0", "BULLET_N", 0, true},
        {"map1_s01", "FirstAidKit", "AIDKIT_N", 1, true},
        {"map1_s01", "HealthDrink", "DRINK_NE", 2, true},
        {"map1_s01", "HandgunBullets1", "BULLET_N", 3, true},
        {"map1_s01", "HandgunBullets2", "BULLET_N", 4, true},
        {"map1_s02", "Ampoule0", "AMPULE_N", 0, false},
        {"map1_s02", "ShotgunShells0", "SHOT_NEA", 1, true},
        {"map1_s02", "ShotgunShells1", "SHOT_NEA", 2, true},
        {"map1_s02", "FirstAidKit0", "AIDKIT_N", 3, false},
        {"map1_s02", "HandgunBullets0", "BULLET_N", 4, true},
        {"map1_s02", "HandgunBullets1", "BULLET_N", 5, true},
        {"map1_s02", "HandgunBullets2", "BULLET_N", 6, true},
        {"map1_s02", "Ampoule1", "AMPULE_N", 7, true},
        {"map1_s02", "HealthDrink0", "DRINK_NE", 8, true},
        {"map1_s02", "FirstAidKit1", "AIDKIT_N", 9, false},
        {"map1_s02", "HealthDrink1", "DRINK_NE", 10, true},
        {"map1_s03", "ShotgunShells0", "SHOT_NEA", 0, true},
        {"map1_s03", "HandgunBullets0", "BULLET_N", 1, true},
        {"map1_s03", "HealthDrink0", "DRINK_NE", 2, true},
        {"map1_s03", "HandgunBullets1", "BULLET_N", 3, true},
        {"map1_s03", "HandgunBullets2", "BULLET_N", 4, true},
        {"map1_s03", "ShotgunShells1", "SHOT_NEA", 5, true},
        {"map1_s03", "HandgunBullets3", "BULLET_N", 6, true},
        {"map1_s03", "HealthDrink1", "DRINK_NE", 7, true},
        {"map1_s03", "HealthDrink2", "DRINK_NE", 8, true},
        {"map1_s03", "FirstAidKit", "AIDKIT_N", 9, true},
    };
    const std::filesystem::path root = std::filesystem::path(SH_PROJECT_ROOT) / "gamedata" / "maps";
    for (const Want& w : want) {
        const auto loaded = sw::load_world_objects(root / w.map / "world_objects.json");
        SH_CHECK(static_cast<bool>(loaded));
        if (!loaded) {
            continue;
        }
        const int i = sw::find_world_object(loaded.value(), w.name);
        SH_CHECK(i >= 0);
        if (i < 0) {
            continue;
        }
        const sw::WorldObjectDef& d = loaded.value().objects[static_cast<std::size_t>(i)];
        SH_CHECK(d.model == w.model);
        SH_CHECK(d.pose.has_value() && d.pose->disc_table == "CommonWorldObjectPoses" && d.pose->index == w.index);
        SH_CHECK(d.pc_near_pickup == w.near);
        SH_CHECK(!d.script);
    }
}

SH_TEST(world_objects_pose_parsing) {
    const auto ok = sw::parse_world_objects(doc(
        R"({"name":"Chair","model":"CHAIR_HI","pose":{"disc_table":"CommonWorldObjectPoses","index":3,"_note":"row 3"}})"));
    SH_CHECK(static_cast<bool>(ok));
    if (ok) {
        const sw::WorldObjectDef& d = ok.value().objects[0];
        SH_CHECK(d.pose.has_value());
        if (d.pose.has_value()) {
            SH_CHECK(d.pose->disc_table == "CommonWorldObjectPoses");
            SH_CHECK(d.pose->index == 3);
        }
        SH_CHECK(d.position[0] == 0 && d.position[1] == 0 && d.position[2] == 0);
        SH_CHECK(d.script == false);
        SH_CHECK(d.pc_near_pickup == false);
    }
    // Exactly one of position and pose; no rotation next to a pose.
    SH_CHECK(!sw::parse_world_objects(doc(
        R"({"name":"A","model":"M","position":[0,0,0],"pose":{"disc_table":"T","index":0}})")));
    SH_CHECK(!sw::parse_world_objects(doc(R"({"name":"A","model":"M"})")));
    SH_CHECK(!sw::parse_world_objects(doc(
        R"({"name":"A","model":"M","pose":{"disc_table":"T","index":0},"rotation":[0,90,0]})")));
    SH_CHECK(!sw::parse_world_objects(doc(
        R"({"name":"A","model":"M","pose":{"disc_table":"T"}})")));                       // no index
    SH_CHECK(!sw::parse_world_objects(doc(
        R"({"name":"A","model":"M","pose":{"index":0}})")));                                // no disc_table
    SH_CHECK(!sw::parse_world_objects(doc(
        R"({"name":"A","model":"M","pose":{"disc_table":"","index":0}})")));            // empty table
    SH_CHECK(!sw::parse_world_objects(doc(
        R"({"name":"A","model":"M","pose":{"disc_table":"T","index":-1}})")));          // negative
    SH_CHECK(!sw::parse_world_objects(doc(
        R"({"name":"A","model":"M","pose":{"disc_table":"T","index":0,"row":1}})"))); // unknown key
}

SH_TEST(world_objects_script_and_near_flags) {
    const auto ok = sw::parse_world_objects(doc(
        R"({"name":"A","model":"M","position":[0,0,0],"script":true,"pc_near_pickup":true},)"
        R"({"name":"B","model":"N","position":[0,0,0]})"));
    SH_CHECK(static_cast<bool>(ok));
    if (ok) {
        SH_CHECK(ok.value().objects[0].script == true);
        SH_CHECK(ok.value().objects[0].pc_near_pickup == true);
        SH_CHECK(ok.value().objects[1].script == false);
        SH_CHECK(ok.value().objects[1].pc_near_pickup == false);
    }
    SH_CHECK(!sw::parse_world_objects(doc(
        R"({"name":"A","model":"M","position":[0,0,0],"script":1})")));
    SH_CHECK(!sw::parse_world_objects(doc(
        R"({"name":"A","model":"M","position":[0,0,0],"pc_near_pickup":"yes"})")));
}

SH_TEST(world_objects_select_forced_and_near) {
    const auto parsed = sw::parse_world_objects(doc(
        R"({"name":"Far","model":"A","position":[0,0,0],"zones":[{"x":9}],"pc_near_pickup":true,"flags_set":["Never"]},)"
        R"({"name":"Home","model":"B","position":[0,0,0],"zones":[{"x":0}]},)"
        R"({"name":"NearMoved","model":"C","position":[0,0,0],"zones":[{"x":9}],"pc_near_pickup":true},)"
        R"({"name":"Pickup","model":"D","position":[0,0,0],"zones":[{"x":5}],"pc_near_pickup":true,"flags_set":["Got"]},)"
        R"({"name":"PickupNoFlag","model":"E","position":[0,0,0],"zones":[{"x":5}],"pc_near_pickup":true,"flags_clear":["Got"]},)"
        R"({"name":"Plain","model":"F","position":[0,0,0],"zones":[{"x":5}]})"));
    SH_CHECK(static_cast<bool>(parsed));
    if (!parsed) {
        return;
    }
    const sw::WorldObjectTable& t = parsed.value();
    std::vector<sw::WorldObjectState> states = sw::initial_states(t);
    SH_CHECK(states.size() == 6);
    states[2].position[0] = 123 * 4096;  // a script moved NearMoved: "near" must see this, not the def

    int near_calls = 0;
    std::map<std::string, std::int32_t> near_at;  // the position each object was tested at
    const sw::WorldObjectNear near = [&](const sw::WorldObjectDef& def, const std::array<std::int32_t, 3>& position) {
        ++near_calls;
        near_at[def.name] = position[0];
        return def.name == "NearMoved" ? position[0] == 123 * 4096 : position[0] == 0;
    };
    const std::int32_t x0 = 10 * 4096;  // cell 0: Home's zone
    const auto draw = [&](const std::map<std::string, bool>& f, std::vector<sw::WorldObjectState>* st,
                          std::int32_t x, const sw::WorldObjectNear& n) {
        std::string out;
        for (std::size_t i : sw::select_world_objects(t, st, x, 0, flags_of(f), n)) {
            out += t.objects[i].name + ";";
        }
        return out;
    };

    // Defs only: Home in its cell; the pickup objects outside theirs ask "near"; Pickup's flag
    // condition fails, PickupNoFlag's holds; Plain has no pc_near_pickup, so it never draws
    // outside its zone; NearMoved is "near" only at its live position, which defs do not have.
    SH_CHECK(draw({}, nullptr, x0, near) == "Home;PickupNoFlag;");
    SH_CHECK(near_at["NearMoved"] == 0);  // the def position was passed
    // With the states: the moved object is "near" through its live position.
    SH_CHECK(draw({}, &states, x0, near) == "Home;NearMoved;PickupNoFlag;");
    SH_CHECK(near_at["NearMoved"] == 123 * 4096);
    // Forced visibility: Far drawn outside its cell with its flag condition failing; Home hidden
    // in its cell. The forced objects consult neither the place nor the flags.
    states[0].visible = true;
    states[1].visible = false;
    SH_CHECK(draw({{"Got", true}}, &states, x0, near) == "Far;NearMoved;Pickup;");
    // PickupNoFlag is "near" but its flags_clear condition fails.
    // Objects whose zone matched do not ask "near" (cell 5 run below); a failing flag condition
    // is still evaluated after a passed place test.
    near_calls = 0;
    states[0].visible = std::nullopt;
    states[1].visible = std::nullopt;
    SH_CHECK(draw({{"Got", true}}, &states, 200 * 4096, near) == "NearMoved;Pickup;Plain;");
    SH_CHECK(near_calls == 2);  // NearMoved (outside) and Far (outside, place passed); zone-matched Pickup did not ask
    // Without the callback nothing outside its zone is drawn.
    SH_CHECK(draw({{"Got", true}}, nullptr, x0, {}) == "Home;");
}

SH_TEST(world_objects_state_helpers) {
    const auto parsed = sw::parse_world_objects(doc(
        R"({"name":"SavePad","model":"PAD_NEAR","position":[1,2,3],"rotation":[0,90,0]})"));
    SH_CHECK(static_cast<bool>(parsed));
    if (!parsed) {
        return;
    }
    const sw::WorldObjectTable& t = parsed.value();
    SH_CHECK(sw::find_world_object(t, "SavePad") == 0);
    SH_CHECK(sw::find_world_object(t, "Nope") == -1);
    const std::vector<sw::WorldObjectState> states = sw::initial_states(t);
    SH_CHECK(states.size() == 1);
    SH_CHECK(states[0].position[0] == 4096 && states[0].position[1] == 2 * 4096 && states[0].position[2] == 3 * 4096);
    SH_CHECK(states[0].rotation[1] == 1024);
    SH_CHECK(states[0].model == "PAD_NEAR");
    SH_CHECK(!states[0].visible.has_value());
}

// Every version 2 file in gamedata/maps parses (a version 1 inventory is skipped).
SH_TEST(world_objects_gamedata_all_version2_parse) {
    const std::filesystem::path root = std::filesystem::path(SH_PROJECT_ROOT) / "gamedata" / "maps";
    int version2 = 0;
    for (const auto& entry : std::filesystem::directory_iterator(root)) {
        const std::filesystem::path file = entry.path() / "world_objects.json";
        if (!std::filesystem::is_regular_file(file)) {
            continue;
        }
        std::ifstream in(file, std::ios::binary);
        const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        if (text.find("\"version\": 2") == std::string::npos) {
            continue;
        }
        ++version2;
        const auto loaded = sw::parse_world_objects(text);
        SH_CHECK(static_cast<bool>(loaded));
        if (loaded) {
            SH_CHECK(loaded.value().map == entry.path().filename().string());
        }
    }
    SH_CHECK(version2 >= 6);
}

// "item_model": the object is drawn from an item model file at a scale; "description" is a note.
SH_TEST(world_objects_item_model) {
    const auto parsed = sw::parse_world_objects(doc(
        R"({"name":"Key","description":"lies on the grass","model":"CLASSKEY","position":[1,0,1],)"
        R"("item_model":{"file":"ITEM/UNQ46.TMD","scale":0.09}},)"
        R"({"name":"Plain","model":"KEY_HIDE","position":[1,0,1],"item_model":{"file":"ITEM/UNQ45.TMD"}})"));
    SH_CHECK(static_cast<bool>(parsed));
    if (!parsed) {
        return;
    }
    const sw::WorldObjectTable& t = parsed.value();
    SH_CHECK(t.objects[0].item_model == "ITEM/UNQ46.TMD" && t.objects[0].item_model_scale == 0.09);
    SH_CHECK(t.objects[1].item_model == "ITEM/UNQ45.TMD" && t.objects[1].item_model_scale == 1.0);
    SH_CHECK(!sw::parse_world_objects(doc(R"({"name":"A","model":"M","position":[0,0,0],"item_model":"ITEM/A.TMD"})")));
    SH_CHECK(!sw::parse_world_objects(
        doc(R"({"name":"A","model":"M","position":[0,0,0],"item_model":{"file":"ITEM/A.TMD","scale":0}})")));
    SH_CHECK(!sw::parse_world_objects(
        doc(R"({"name":"A","model":"M","position":[0,0,0],"item_model":{"file":"ITEM/A.TMD","size":2}})")));

    sw::object_item_models_clear();
    SH_CHECK(sw::object_item_model("CLASSKEY") == nullptr);
    sw::object_item_model_set("CLASSKEY", {"ITEM/UNQ46.TMD", 0.09});
    const sw::ObjectItemModel* m = sw::object_item_model("CLASSKEY");
    SH_CHECK(m != nullptr && m->file == "ITEM/UNQ46.TMD" && m->scale == 0.09);
    sw::object_item_models_clear();
    SH_CHECK(sw::object_item_model("CLASSKEY") == nullptr);
}
