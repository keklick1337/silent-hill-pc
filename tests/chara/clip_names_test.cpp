// SPDX-License-Identifier: GPL-3.0-or-later
// The clip name lists of the character scripts (clip_numbers in gamedata/chara/common.lua): every list names the
// character data's "anims" in the game's clip order, so with the shipped data each name resolves to the number the
// script used before and a mod that patches "anims" moves the clip the script plays.

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <string>
#include <vector>

#include "formats/common/json.h"

#include "framework/sh_test.h"

namespace fs = std::filesystem;

namespace {

std::string read_text(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

// The quoted names of `clip_numbers("<chara>", { ... })` in a script, the character named in `chara`.
std::vector<std::string> listed_names(const std::string& lua, std::string* chara) {
    std::vector<std::string> out;
    const std::size_t call = lua.find("clip_numbers(\"");
    if (call == std::string::npos) {
        return out;
    }
    const std::size_t name_start = call + 14;
    *chara = lua.substr(name_start, lua.find('"', name_start) - name_start);
    const std::size_t open = lua.find('{', name_start);
    const std::size_t close = lua.find('}', open);
    for (std::size_t i = lua.find('"', open); i < close; i = lua.find('"', i)) {
        const std::size_t end = lua.find('"', i + 1);
        out.push_back(lua.substr(i + 1, end - i - 1));
        i = end + 1;
    }
    return out;
}

}  // namespace

SH_TEST(chara_clip_name_lists_follow_the_data) {
    const fs::path root = fs::path(SH_PROJECT_ROOT) / "gamedata/chara";
    int lists = 0;
    for (const fs::directory_entry& dir : fs::directory_iterator(root)) {
        const fs::path script = dir.path() / "behavior.lua";
        if (!fs::exists(script)) {
            continue;
        }
        std::string chara;
        const std::vector<std::string> names = listed_names(read_text(script), &chara);
        if (names.empty()) {
            continue;
        }
        SH_CHECK(chara == dir.path().filename().string());
        const auto doc = sh::json::parse(read_text(dir.path() / "character.json"));
        SH_CHECK(doc);
        const sh::json::Value* anims = doc.value().find("anims");
        SH_CHECK(anims != nullptr && anims->is_object());
        std::map<std::string, std::int64_t> by_name;
        for (const auto& [name, number] : anims->members()) {
            by_name[name] = number.integer();
        }
        for (std::size_t i = 0; i < names.size(); ++i) {
            const auto it = by_name.find(names[i]);
            SH_CHECK(it != by_name.end());
            if (it != by_name.end()) {
                SH_CHECK(it->second == static_cast<std::int64_t>(i));
            }
        }
        ++lists;
    }
    SH_CHECK(lists >= 2);  // Stalker, Romper
}
