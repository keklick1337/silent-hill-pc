// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// Former names keep working after a rename: every "aliases" entry of gamedata/sounds/sounds.json and
// gamedata/flags/*.json resolves to the same number as the name it now stands for, and the sound
// registry finds an alias as the same sound while the number keeps naming the new name.

#include "engine/audio/sound_registry.h"
#include "engine/ids/game_numbers.h"
#include "formats/common/json.h"
#include "framework/sh_test.h"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

fs::path repo_gamedata() { return fs::path(SH_PROJECT_ROOT) / "gamedata"; }

sh::json::Value read(const fs::path& file) {
    std::ifstream in(file, std::ios::binary);
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    auto doc = sh::json::parse(text);
    return doc ? std::move(doc.value()) : sh::json::Value{};
}

std::vector<std::string> aliases_of(const sh::json::Value& entry) {
    std::vector<std::string> out;
    if (const sh::json::Value* list = entry.find("aliases"); list != nullptr && list->is_array()) {
        for (const sh::json::Value& a : list->array()) {
            if (a.is_string()) {
                out.push_back(a.string());
            }
        }
    }
    return out;
}

}  // namespace

SH_TEST(sound_aliases_resolve_to_the_renamed_sound) {
    const sh::json::Value doc = read(repo_gamedata() / "sounds" / "sounds.json");
    const sh::json::Value* sounds = doc.find("sounds");
    SH_CHECK(sounds != nullptr && sounds->is_object());
    if (sounds == nullptr || !sounds->is_object()) {
        return;
    }
    std::vector<std::string> warnings;
    const auto registry = sh::audio::SoundRegistry::load(repo_gamedata(), {}, warnings);
    SH_CHECK(static_cast<bool>(registry));
    SH_CHECK(warnings.empty());
    int aliased = 0;
    for (const auto& [name, entry] : sounds->members()) {
        const int number = sh::ids::sound_number(name.c_str());
        SH_CHECK(number > 0);
        for (const std::string& alias : aliases_of(entry)) {
            ++aliased;
            SH_CHECK(sh::ids::sound_number(alias.c_str()) == number);
            if (static_cast<bool>(registry)) {
                SH_CHECK(registry.value().id(alias) == number);
                const std::string* first = registry.value().name(number);
                SH_CHECK(first != nullptr && *first == name);
            }
        }
    }
    SH_CHECK(aliased > 0);
    // Spot checks of renamed sounds: place in the table and XA row.
    SH_CHECK(sh::ids::sound_number("bullet_impact") == 1286);
    SH_CHECK(sh::ids::sound_number("melee_impact_a") == 1286);
    SH_CHECK(sh::ids::sound_number("gas_weapon_contact") == 1300);
    SH_CHECK(sh::ids::sound_number("melee_impact_d") == 1300);
    SH_CHECK(sh::ids::sound_number("finale_lightning_strike") == 1694);
    SH_CHECK(sh::ids::sound_number("finale_boss_scene_f") == 1694);
    SH_CHECK(sh::ids::sound_number("finale_boss_voice_f") == 4753);
}

SH_TEST(flag_aliases_resolve_to_the_renamed_flag) {
    int aliased = 0;
    for (const fs::directory_entry& file : fs::directory_iterator(repo_gamedata() / "flags")) {
        if (file.path().extension() != ".json") {
            continue;
        }
        const sh::json::Value doc = read(file.path());
        const sh::json::Value* flags = doc.find("flags");
        SH_CHECK(flags != nullptr && flags->is_object());
        if (flags == nullptr || !flags->is_object()) {
            continue;
        }
        for (const auto& [name, entry] : flags->members()) {
            const int bit = sh::ids::flag_number(name.c_str());
            SH_CHECK(bit > 0);
            for (const std::string& alias : aliases_of(entry)) {
                ++aliased;
                SH_CHECK(sh::ids::flag_number(alias.c_str()) == bit);
            }
        }
    }
    SH_CHECK(aliased > 0);
}
