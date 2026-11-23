// World object placements read from the map's overlay file (engine/world/disc_poses.h): the table registry, the
// row lookup by table name or disc address, the "flat table" rule and the record decoding; and every pose of
// gamedata/maps against its map's registry.

#include "engine/world/disc_poses.h"
#include "framework/sh_test.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <set>
#include <string>
#include <vector>

namespace {

namespace sw = sh::world;

constexpr std::uint32_t kBase = 0x800C9578;

const char* kRegistry = R"({
  "format": "sh-map-tables/1",
  "bin": "VIN/TEST.BIN",
  "base": "0x800C9578",
  "symbols": [
    {"symbol": "ROOMS", "va": "0x800C9600", "type": "u8", "count": 16, "bytes": 16},
    {"symbol": "g_CommonWorldObjectPoses", "va": "0x800C9610", "type": "s_WorldObjectPose", "count": 2, "bytes": 40},
    {"symbol": "ANIMS", "va": "0x800C9640", "type": "s_AnimInfo", "count": 4},
    {"symbol": "PORT", "port_word": true, "type": "s32", "count": 1},
    {"symbol": "LOOSE", "va": "0x800C9680", "type": "s_Pose", "count": 1, "bytes": 20}
  ]
})";

void put32(std::vector<std::uint8_t>& b, std::size_t at, std::int32_t v) {
    for (int i = 0; i < 4; ++i) {
        b[at + static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(static_cast<std::uint32_t>(v) >> (8 * i));
    }
}

void put16(std::vector<std::uint8_t>& b, std::size_t at, std::int16_t v) {
    b[at] = static_cast<std::uint8_t>(static_cast<std::uint16_t>(v));
    b[at + 1] = static_cast<std::uint8_t>(static_cast<std::uint16_t>(v) >> 8);
}

void put_pose(std::vector<std::uint8_t>& b, std::uint32_t address, std::int32_t x, std::int32_t y, std::int32_t z,
              std::int16_t rx, std::int16_t ry, std::int16_t rz) {
    const std::size_t at = address - kBase;
    put32(b, at, x);
    put32(b, at + 4, y);
    put32(b, at + 8, z);
    put16(b, at + 12, rx);
    put16(b, at + 14, ry);
    put16(b, at + 16, rz);
}

sw::WorldObjectPoseRef ref(const char* table, int index, std::uint32_t address = 0) {
    sw::WorldObjectPoseRef r;
    r.disc_table = table;
    r.index = index;
    r.disc_address = address;
    return r;
}

}  // namespace

SH_TEST(disc_poses_registry_parse) {
    const auto reg = sw::parse_overlay_tables(kRegistry);
    SH_CHECK(static_cast<bool>(reg));
    if (!reg) {
        return;
    }
    SH_CHECK(reg.value().bin == "VIN/TEST.BIN");
    SH_CHECK(reg.value().base == kBase);
    SH_CHECK(reg.value().tables.size() == 4);  // the engine's word has no place on the disc
    SH_CHECK(reg.value().tables[1].address == 0x800C9610 && reg.value().tables[1].bytes == 40);
    SH_CHECK(reg.value().tables[2].bytes == 0);  // relocated per element: not a flat copy
    SH_CHECK(!static_cast<bool>(sw::parse_overlay_tables("{}")));
    SH_CHECK(!static_cast<bool>(sw::parse_overlay_tables(R"({"bin": "A", "base": "zz", "symbols": []})")));
    SH_CHECK(!static_cast<bool>(sw::parse_overlay_tables(R"({"bin": "A", "base": "0x1", "symbols": [{"symbol": "X"}]})")));
}

SH_TEST(disc_poses_rows) {
    const sw::OverlayTableRegistry reg = sw::parse_overlay_tables(kRegistry).value();
    std::vector<std::uint8_t> bin(0x200, 0xCD);
    put_pose(bin, 0x800C9610, -123456, 4096, 0x7FFFFFFF, -1, 1024, -32768);
    put_pose(bin, 0x800C9624, 1, 2, 3, 4, 5, 6);
    put_pose(bin, 0x800C9680, -7, -8, -9, 100, 200, 300);

    auto a = sw::resolve_disc_pose(reg, ref("CommonWorldObjectPoses", 0), bin);
    SH_CHECK(static_cast<bool>(a));
    if (a) {
        SH_CHECK(a.value().position[0] == -123456 && a.value().position[1] == 4096 &&
                 a.value().position[2] == 0x7FFFFFFF);
        SH_CHECK(a.value().rotation[0] == -1 && a.value().rotation[1] == 1024 && a.value().rotation[2] == -32768);
    }
    auto b = sw::resolve_disc_pose(reg, ref("CommonWorldObjectPoses", 1), bin);
    SH_CHECK(static_cast<bool>(b) && b.value().position[2] == 3 && b.value().rotation[2] == 6);
    // Past the table: the next bytes belong to a relocated table, which is no holder.
    SH_CHECK(!static_cast<bool>(sw::resolve_disc_pose(reg, ref("CommonWorldObjectPoses", 2), bin)));
    // The table name wins over the address.
    auto c = sw::resolve_disc_pose(reg, ref("CommonWorldObjectPoses", 1, 0x800C9680), bin);
    SH_CHECK(static_cast<bool>(c) && c.value().position[0] == 1);
    // A registry without the table: the disc address, inside a flat table of any name.
    auto d = sw::resolve_disc_pose(reg, ref("OtherPoses", 0, 0x800C9680), bin);
    SH_CHECK(static_cast<bool>(d) && d.value().position[0] == -7 && d.value().rotation[2] == 300);
    SH_CHECK(!static_cast<bool>(sw::resolve_disc_pose(reg, ref("OtherPoses", 1, 0x800C9680), bin)));
    SH_CHECK(!static_cast<bool>(sw::resolve_disc_pose(reg, ref("OtherPoses", 0), bin)));
    // A short file: the row is not on the disc.
    const std::vector<std::uint8_t> shorter(bin.begin(), bin.begin() + 0x100);
    SH_CHECK(!static_cast<bool>(sw::resolve_disc_pose(reg, ref("OtherPoses", 0, 0x800C9680), shorter)));
}

SH_TEST(disc_poses_world_objects_key) {
    const char* json = R"({"format": "sh-map-world-objects", "version": 2, "map": "m", "objects": [
        {"name": "A", "model": "DRINK_NE", "pose": {"disc_table": "CommonWorldObjectPoses", "index": 3,
                                                   "disc_address": "0x800DAAD0"}}]})";
    const auto t = sw::parse_world_objects(json);
    SH_CHECK(static_cast<bool>(t));
    if (t) {
        SH_CHECK(t.value().objects[0].pose.has_value());
        SH_CHECK(t.value().objects[0].pose->index == 3 && t.value().objects[0].pose->disc_address == 0x800DAAD0);
    }
    const char* bad = R"({"format": "sh-map-world-objects", "version": 2, "map": "m", "objects": [
        {"name": "A", "model": "DRINK_NE", "pose": {"disc_table": "T", "index": 0, "disc_address": "nope"}}]})";
    SH_CHECK(!static_cast<bool>(sw::parse_world_objects(bad)));
}

// Every pose of the base data has a registry that holds its row: without it a pickup is never drawn.
SH_TEST(disc_poses_gamedata) {
    const std::filesystem::path root = std::filesystem::path(SH_PROJECT_ROOT) / "gamedata" / "maps";
    const std::set<std::string> unheld;
    int resolved = 0;
    int missing = 0;
    for (const auto& entry : std::filesystem::directory_iterator(root)) {
        const std::filesystem::path objects = entry.path() / "world_objects.json";
        if (!std::filesystem::exists(objects)) {
            continue;
        }
        const auto table = sw::load_world_objects(objects);
        SH_CHECK(static_cast<bool>(table));
        if (!table) {
            continue;
        }
        bool any = false;
        for (const auto& def : table.value().objects) {
            any = any || def.pose.has_value();
        }
        if (!any) {
            continue;
        }
        std::ifstream in(entry.path() / "tables.json");
        const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        const auto reg = sw::parse_overlay_tables(text);
        SH_CHECK(static_cast<bool>(reg));
        if (!reg) {
            continue;
        }
        // The registry decides; the bytes do not matter here.
        const std::vector<std::uint8_t> zeros(0x40000, 0);
        const std::string map = entry.path().filename().string();
        for (const auto& def : table.value().objects) {
            if (!def.pose) {
                continue;
            }
            const bool ok = static_cast<bool>(sw::resolve_disc_pose(reg.value(), *def.pose, zeros));
            SH_CHECK(ok == (unheld.count(map) == 0));
            ok ? ++resolved : ++missing;
        }
    }
    SH_CHECK(resolved > 100);
    SH_CHECK(missing == 0);
}
