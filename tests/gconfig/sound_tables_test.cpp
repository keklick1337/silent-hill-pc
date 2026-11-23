#include "framework/sh_test.h"

#include "engine/gconfig/sound_tables.h"

#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using sh::gconfig::SoundTables;

SH_TEST(sound_tables_shipped_file_loads) {
    std::vector<std::string> warnings;
    const auto t = SoundTables::load(fs::path(SH_PROJECT_ROOT) / "gamedata", {}, warnings);
    SH_CHECK(static_cast<bool>(t));
    SH_CHECK(t.value().reverb_depths[0] == 20 && t.value().reverb_depths[21] == 120);
    SH_CHECK(t.value().files[0].size == 136208 && t.value().files[0].sector == 140);
    SH_CHECK(t.value().files[3].bank == 1);
    SH_CHECK(t.value().effects[1].bank == 2 && t.value().effects[1].volume == -30);
    SH_CHECK(t.value().xa_lines[1].xa_file == 1 && t.value().xa_lines[1].length == 362);
}

SH_TEST(sound_tables_rejects_bad_data) {
    SH_CHECK(!static_cast<bool>(SoundTables::parse("{}")));
    SH_CHECK(!static_cast<bool>(SoundTables::parse("{\"reverb_depths\": []}")));
}

SH_TEST(sound_tables_active) {
    const SoundTables zero = SoundTables::empty();
    SH_CHECK(zero.files.size() == sh::gconfig::kSoundFileCount && zero.effects.size() == sh::gconfig::kSoundEffectCount);
    SH_CHECK(zero.xa_lines.size() == sh::gconfig::kXaLineCount);

    auto& active = sh::gconfig::active_sound_tables();
    std::vector<std::string> messages;
    SH_CHECK(sh::gconfig::load_active_sound_tables(fs::path(SH_PROJECT_ROOT) / "gamedata", {}, messages));
    SH_CHECK(active.tables.xa_lines[1].length == 362);
}
