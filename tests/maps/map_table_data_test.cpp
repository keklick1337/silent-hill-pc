// SPDX-License-Identifier: GPL-3.0-or-later
// The readable data of tables.json arrays (engine/maps/map_table_data.h): the records encode to the disc layout
// the old form copies from the overlay, so both forms fill the same bytes. The disc-backed case checks every base
// map against its imported overlay when SH_TEST_GAMEDATA points at an imported gamedata.

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "engine/chara/harry_reactions.h"
#include "engine/ids/game_numbers.h"
#include "engine/maps/map_table_data.h"
#include "engine/maps/map_tables.h"
#include "formats/common/json.h"

#include "framework/sh_test.h"

namespace m = sh::maps;
namespace fs = std::filesystem;

namespace {

std::string read_text(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

class Names final : public m::TableNames {
public:
    Names() {
        auto parsed = sh::chara::HarryReactions::parse(
            read_text(fs::path(SH_PROJECT_ROOT) / "gamedata/chara/Harry/reactions.json"));
        if (parsed) {
            reactions_ = parsed.value();
        }
    }
    int item(std::string_view name) const override { return sh::ids::item_number(std::string(name).c_str()); }
    int reaction(std::string_view name) const override { return reactions_.id_of(name); }

private:
    sh::chara::HarryReactions reactions_;
};

// The old form of a tables.json: every data key dropped, so the arrays come from the disc.
std::string without_data(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    std::size_t i = 0;
    while (i < text.size()) {
        bool cut = false;
        for (std::string_view key : m::kTableDataKeys) {
            const std::string quoted = ",\n      \"" + std::string(key) + "\": ";
            if (text.compare(i, quoted.size(), quoted) == 0) {
                // The data value ends at the symbol's closing brace line.
                i = text.find("\n    }", i);
                cut = true;
                break;
            }
        }
        if (!cut) {
            out.push_back(text[i++]);
        }
    }
    return out;
}

constexpr std::uint8_t kOnce = 0xF0;  // byte pattern of the fake function address below

}  // namespace

SH_TEST(map_table_data_encodes_the_disc_layout) {
    const std::string json = R"({
      "base": "0x800C9578",
      "symbols": [
        {"symbol": "LOADABLE_INVENTORY_ITEMS", "va": "0x800C9580", "type": "u8", "count": 4, "bytes": 4,
         "items": ["HealthDrink", "Handgun"]},
        {"symbol": "HARRY_TEST_ANIM_INFOS", "va": "0x800C9590", "type": "s_AnimInfo", "count": 3,
         "clips": [{"playback": "once", "status": 77, "link": 255, "speed": 2.5, "start_keyframe": 10, "end_keyframe": 20},
                   {"playback": "once", "status": 78, "link": 78, "speed_function": "variable_duration",
                    "start_keyframe": -1, "end_keyframe": 30}]},
        {"symbol": "g_MapHeaderTable_38", "va": "0x800C95C0", "type": "s_UnkStruct3_Mo", "count": 2, "bytes": 16,
         "reaction_rows": [{"reaction": "Death", "status": 76, "next_status": 77, "start_keyframe": 676, "last_keyframe": 677}]},
        {"symbol": "glass_shards", "va": "0x800C95D0", "type": "u8", "count": 256, "bytes": 24,
         "glass_panes": [{"x": -4096, "z": 8192, "size": 300}]}
      ],
      "functions": [{"va": "0x800449F0", "symbol": "Anim_PlaybackOnce"},
                    {"va": "0x8004F0F0", "symbol": "Player_VariableAnimDurationGet"}]
    })";
    // Disc bytes the data must replace (0xEE everywhere).
    const std::vector<std::uint8_t> disc(0x80, 0xEE);
    const auto r = m::apply_table_data(json, disc, Names());
    SH_CHECK(r);
    const std::vector<std::uint8_t>& b = r.value().overlay;
    SH_CHECK(r.value().arrays == 4 && b.size() == disc.size());
    SH_CHECK(b[0x07] == 0xEE);  // outside every array: the disc's byte
    SH_CHECK(b[0x08] == sh::ids::item_number("HealthDrink") && b[0x09] == sh::ids::item_number("Handgun"));
    SH_CHECK(b[0x0A] == 0 && b[0x0B] == 0);  // the rest of the room is the terminator
    // Clip 0: once, status 77, constant speed 2.5 (Q12 10240), keyframes 10..20.
    SH_CHECK(b[0x18] == 0xF0 && b[0x19] == 0x49 && b[0x1A] == 0x04 && b[0x1B] == 0x80);
    SH_CHECK(b[0x1C] == 77 && b[0x1D] == 0 && b[0x1E] == 255 && b[0x1F] == 0);
    SH_CHECK(b[0x20] == 0x00 && b[0x21] == 0x28 && b[0x22] == 0 && b[0x23] == 0);
    SH_CHECK(b[0x24] == 10 && b[0x26] == 20);
    // Clip 1: the variable duration function's address, the flag set.
    SH_CHECK(b[0x2D] == 1 && b[0x30] == kOnce && b[0x31] == kOnce && b[0x32] == 0x04);
    SH_CHECK(b[0x34] == 0xFF && b[0x35] == 0xFF);
    // Clip 2 (the terminator) is zero.
    SH_CHECK(b[0x38] == 0 && b[0x3F] == 0 && b[0x47] == 0);
    // Reaction row: statuses 76/77, Death = anim 101 * 2.
    SH_CHECK(b[0x48] == 76 && b[0x49] == 77 && b[0x4A] == 202 && b[0x4B] == 0);
    SH_CHECK(b[0x4C] == (676 & 0xFF) && b[0x4D] == (676 >> 8));
    // Glass pane: x -4096, z 8192, size 300; the other 12 bytes of its room zero.
    SH_CHECK(b[0x58] == 0x00 && b[0x59] == 0xF0 && b[0x5A] == 0xFF && b[0x5B] == 0xFF);
    SH_CHECK(b[0x5D] == 0x20 && b[0x60] == (300 & 0xFF) && b[0x61] == 1 && b[0x64] == 0 && b[0x6F] == 0);
}

SH_TEST(map_table_data_rejects_bad_records) {
    const auto with = [](const std::string& record) {
        return R"({"symbols": [{"symbol": "s", "va": "0x800C9580", "type": "u8", "count": 256, "bytes": 48, )" +
               record + R"(}], "functions": []})";
    };
    const std::vector<std::uint8_t> disc(0x100, 0);
    auto r = m::apply_table_data(with(R"("glass_panes": [{"x": 1, "z": 2, "size": 3, "tint": 4}])"), disc, Names());
    SH_CHECK(!r && r.error().find("unknown key \"tint\"") != std::string::npos);
    r = m::apply_table_data(with(R"("glass_panes": [{"x": 1, "z": 2}])"), disc, Names());
    SH_CHECK(!r && r.error().find("missing \"size\"") != std::string::npos);
    r = m::apply_table_data(with(R"("smoke_column": {"shape": 300})"), disc, Names());
    SH_CHECK(!r && r.error().find("shape must be an integer in 0..255") != std::string::npos);
    r = m::apply_table_data(with(R"("items": ["NoSuchItem"])"), disc, Names());
    SH_CHECK(!r && r.error().find("must name an item") != std::string::npos);
    r = m::apply_table_data(with(R"("clips": [{"playback": "once", "status": 1, "link": 1, "speed": 1, "start_keyframe": 0, "end_keyframe": 0}])"),
                            disc, Names());
    SH_CHECK(!r && r.error().find("do not list Anim_PlaybackOnce") != std::string::npos);
    r = m::apply_table_data(with(R"("drips": [], "items": [])"), disc, Names());
    SH_CHECK(!r && r.error().find("one data key") != std::string::npos);
}

SH_TEST(map_table_data_both_forms_fill_identical_bytes) {
    const char* root = std::getenv("SH_TEST_GAMEDATA");
    if (root == nullptr || *root == '\0') SH_SKIP("SH_TEST_GAMEDATA is not set");
    const Names names;
    int maps = 0;
    int arrays = 0;
    for (const fs::directory_entry& dir : fs::directory_iterator(fs::path(SH_PROJECT_ROOT) / "gamedata/maps")) {
        const fs::path tables = dir.path() / "tables.json";
        if (!fs::exists(tables)) {
            continue;
        }
        const std::string text = read_text(tables);
        const auto doc = sh::json::parse(text);
        SH_CHECK(doc);
        const std::string disc_text = read_text(fs::path(root) / "raw" / doc.value().find("bin")->string());
        const std::vector<std::uint8_t> disc(disc_text.begin(), disc_text.end());
        SH_CHECK(!disc.empty());
        // New form: the data written over the disc gives the disc's own bytes back.
        const auto with_data = m::apply_table_data(text, disc, names);
        if (!with_data) {
            std::fprintf(stderr, "    %s: %s\n", dir.path().filename().string().c_str(), with_data.error().c_str());
        }
        SH_CHECK(with_data);
        SH_CHECK(with_data.value().overlay == disc);
        arrays += with_data.value().arrays;
        // Old form: no data at all, the disc stays as it is.
        const std::string old_form = without_data(text);
        SH_CHECK(sh::json::parse(old_form));
        const auto without = m::apply_table_data(old_form, disc, names);
        SH_CHECK(without && without.value().arrays == 0 && without.value().overlay == disc);
        ++maps;
    }
    SH_CHECK(maps == 43 && arrays == 139);
}
