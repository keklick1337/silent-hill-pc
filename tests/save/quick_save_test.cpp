// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// Instant quick saves (quick_save_instant): GameSaves::quick_save / quick_load over
// the session, the quick slot file and what they leave alone.

#include "engine/game_state/session.h"
#include "engine/save/game_saves.h"
#include "engine/save/save_service.h"
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

class QuickHost final : public sh::save::GameSavesHost {
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
    void quick_saving(int index) override { quick_saves.push_back(index); }
    void quick_loaded(int index) override { quick_loads.push_back(index); }

    std::vector<int> saves, loads, quick_saves, quick_loads;
};

}  // namespace

SH_TEST(quick_save_session_round_trip) {
    std::error_code ec;
    const fs::path dir = fs::temp_directory_path() / "sh_quick_save_test";
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);

    QuickHost host;
    sh::save::GameSaves& saves = sh::save::game_saves();
    saves.set_host(&host);
    const std::string exe = dir.string() + "/";
    saves.open(exe.c_str(), "", (dir / "gamedata").string().c_str());  // a no-op when another test opened it
    const fs::path quick_file = sh::save::active_store().quick_path(0);
    fs::remove(quick_file, ec);
    const int listed_before = static_cast<int>(sh::save::active_store().list().size());

    sh::game::GameBlock& game = sh::game::session().game;
    sh::game::SaveRecord& save = game.save;
    save = sh::game::SaveRecord{};
    game.autosave = sh::game::SaveRecord{};
    save.map = 4;
    save.save_count = 2;
    save.location = 3;  // Store
    save.play_time = 0x78;
    save.event_flags[0] = 0x110000;  // an event flag
    game.settings.brightness = 5;

    // Nothing there yet: the load leaves the session alone.
    SH_CHECK(sh::save::GameSaves::quick_state(0) == 0);
    const std::uint32_t before = sh::save::GameSaves::fingerprint();
    SH_CHECK(saves.quick_load(0) == 0);
    SH_CHECK(sh::save::GameSaves::fingerprint() == before);
    SH_CHECK(host.quick_loads.empty());

    SH_CHECK(saves.quick_save(0) == 1);
    SH_CHECK(host.quick_saves.size() == 1 && host.quick_saves[0] == 0);
    SH_CHECK(host.saves.empty());
    SH_CHECK(save.save_count == 3);  // a quick save counts as a save
    SH_CHECK(game.autosave == game.save);
    SH_CHECK(fs::exists(quick_file));
    SH_CHECK(sh::save::GameSaves::quick_state(0) == 1);
    // The quick slot is not a save of the list (the load screen, CONTINUE and the newest options ignore it).
    SH_CHECK(static_cast<int>(sh::save::active_store().list().size()) == listed_before);
    const std::uint32_t saved = sh::save::GameSaves::fingerprint();

    // The game goes on; the quick load gives back exactly the quick save, and the settings stay as they are.
    save.map = 7;
    save.event_flags[0] = 0;
    save.play_time = 0x10;
    game.settings.brightness = 1;
    SH_CHECK(saves.quick_load(0) == 1);
    SH_CHECK(sh::save::GameSaves::fingerprint() == saved);
    SH_CHECK(save.map == 4 && save.event_flags[0] == 0x110000 && save.play_time == 0x78 && save.save_count == 3);
    SH_CHECK(game.settings.brightness == 1);
    SH_CHECK(host.quick_loads.size() == 1 && host.quick_loads[0] == 0);
    SH_CHECK(host.loads.empty());

    // A second quick save replaces the first.
    save.map = 5;
    SH_CHECK(saves.quick_save(0) == 1);
    save.map = 6;
    SH_CHECK(saves.quick_load(0) == 1);
    SH_CHECK(save.map == 5 && save.save_count == 4);

    // A damaged quick save is refused and the session stays as it was.
    {
        std::FILE* f = std::fopen(quick_file.string().c_str(), "r+b");
        SH_CHECK(f != nullptr);
        if (f != nullptr) {
            std::fseek(f, 40, SEEK_SET);
            std::fputc(0x5A, f);
            std::fclose(f);
        }
    }
    SH_CHECK(sh::save::GameSaves::quick_state(0) < 0);
    save.map = 9;
    const std::uint32_t live = sh::save::GameSaves::fingerprint();
    SH_CHECK(saves.quick_load(0) < 0);
    SH_CHECK(sh::save::GameSaves::fingerprint() == live);

    // A slot out of range writes nothing and does not count a save.
    const std::int16_t count = save.save_count;
    SH_CHECK(saves.quick_save(99) == -1);
    SH_CHECK(save.save_count == count);

    fs::remove(quick_file, ec);
    game.save = sh::game::SaveRecord{};
    game.autosave = sh::game::SaveRecord{};
    game.settings = sh::game::Settings{};
    saves.set_host(nullptr);
    fs::remove_all(dir, ec);
}
