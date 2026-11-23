// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// sh::save::GameSaves (engine/save/game_saves.h): the session's progress and settings to a save file and back.

#include "engine/game_state/session.h"
#include "engine/save/game_saves.h"
#include "framework/sh_test.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

namespace fs = std::filesystem;

namespace {

class TestHost final : public sh::save::GameSavesHost {
public:
    const char* map_name(int number) override {
        static std::string names[64];
        if (number < 0 || number >= 64) {
            return nullptr;
        }
        names[number] = "map" + std::to_string(number);
        return names[number].c_str();
    }
    int map_number(const char* name) override {
        return std::strncmp(name, "map", 3) == 0 ? std::atoi(name + 3) : -1;
    }
    void saving(int number) override { saves.push_back(number); }
    void loaded(int number) override { loads.push_back(number); }
    void blood_color_loaded(std::uint8_t color) override { blood = color; }

    std::vector<int> saves, loads;
    int blood = -1;
};

}  // namespace

SH_TEST(game_saves_session_round_trip) {
    std::error_code ec;
    const fs::path dir = fs::temp_directory_path() / "sh_game_saves_test";
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);

    sh::game::GameBlock& game = sh::game::session().game;
    sh::game::SaveRecord& save = game.save;
    save = sh::game::SaveRecord{};
    game.autosave = sh::game::SaveRecord{};
    save.map = 3;
    save.save_count = 5;
    save.location = 1;  // Cafe
    save.play_time = 0x1234;
    save.mode_bits = 1 | (2 << 1) | (4 << 3);  // next fear, extra hours, special items
    save.event_flags[1] = 0x40;
    game.settings.brightness = 7;
    game.settings.blood_color = 2;

    TestHost host;
    sh::save::GameSaves& saves = sh::save::game_saves();
    saves.set_host(&host);
    const std::string exe = dir.string() + "/";
    saves.open(exe.c_str(), "", (dir / "gamedata").string().c_str());

    const int written = saves.save(0);
    SH_CHECK(written >= 1);
    SH_CHECK(host.saves.size() == 1 && host.saves[0] == 0);
    SH_CHECK(save.save_count == 6);  // counted before writing
    SH_CHECK(game.autosave == game.save);
    const std::uint32_t saved = sh::save::GameSaves::fingerprint();

    // The session changes; the load gives back exactly what was saved, the settings of the newest save with it.
    save.map = 9;
    save.event_flags[1] = 0;
    game.settings.brightness = 1;
    game.settings.blood_color = 5;
    SH_CHECK(saves.load(written) == 1);
    SH_CHECK(sh::save::GameSaves::fingerprint() == saved);
    SH_CHECK(save.map == 3 && save.event_flags[1] == 0x40);
    SH_CHECK(game.settings.brightness == 7 && game.settings.blood_color == 2);
    SH_CHECK(host.blood == 2);
    SH_CHECK(host.loads.size() == 1 && host.loads[0] == written);

    // A save that is not there leaves the session alone.
    SH_CHECK(saves.load(written + 100) <= 0);
    SH_CHECK(sh::save::GameSaves::fingerprint() == saved);

    // The boot sees the save.
    int outcome = 0;
    for (int i = 0; i < 4 && outcome == 0; ++i) {
        outcome = saves.boot_check();
    }
    SH_CHECK(outcome == 2);

    game.save = sh::game::SaveRecord{};
    game.autosave = sh::game::SaveRecord{};
    game.settings = sh::game::Settings{};
    saves.set_host(nullptr);
    fs::remove_all(dir, ec);
}
