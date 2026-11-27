// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// sh::save::GameSaves (engine/save/game_saves.h): the session's progress and settings to a save file and back.

#include "engine/game_state/session.h"
#include "engine/save/game_saves.h"
#include "engine/save/save_c.h"
#include "engine/save/save_file.h"
#include "engine/save/save_service.h"
#include "engine/save/save_store.h"
#include "engine/version/engine_version.h"
#include "framework/sh_test.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <optional>
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

// The file's bytes, or nothing (the failure is reported).
std::vector<std::uint8_t> bytes_of(const fs::path& path) {
    auto bytes = sh::save::read_whole_file(path);
    SH_CHECK(static_cast<bool>(bytes));
    return bytes ? std::move(bytes.value()) : std::vector<std::uint8_t>();
}

std::optional<sh::save::SaveFile> file_of(const fs::path& path) {
    auto file = sh::save::decode(bytes_of(path));
    SH_CHECK(static_cast<bool>(file));
    return file ? std::optional<sh::save::SaveFile>(std::move(file.value())) : std::nullopt;
}

std::string meta_of(const fs::path& path) {
    const auto file = file_of(path);
    const sh::save::Chunk* meta = file ? file->find(sh::save::kTagMeta) : nullptr;
    SH_CHECK(!file || meta != nullptr);
    return meta != nullptr ? std::string(meta->data.begin(), meta->data.end()) : std::string();
}

// The file's META "game_version" replaced, as a newer game would have written it.
void set_version(const fs::path& path, const std::string& version) {
    auto file = file_of(path);
    if (!file) return;
    std::string meta = meta_of(path);
    const std::string key = "\"game_version\": \"";
    const std::size_t at = meta.find(key);
    SH_CHECK(at != std::string::npos);
    if (at == std::string::npos) return;
    const std::size_t end = meta.find('"', at + key.size());
    meta.replace(at + key.size(), end - at - key.size(), version);
    file->put(sh::save::kTagMeta, std::vector<std::uint8_t>(meta.begin(), meta.end()));
    SH_CHECK(static_cast<bool>(sh::save::write_file_atomic(path, sh::save::encode(*file))));
}

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
    // The store is opened once a process: another test may have opened it in its own folder first. It starts empty.
    const fs::path root = sh::save::active_store().root();
    const bool in_temp = root.lexically_normal().generic_string().starts_with(
        fs::temp_directory_path().lexically_normal().generic_string());
    SH_CHECK(in_temp);
    if (in_temp) fs::remove_all(root, ec);

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

    // The save says which version of the game made it.
    const fs::path first = sh::save::active_store().save_path(1);
    const std::string version(sh::version::kText);
    SH_CHECK(written == 1 && meta_of(first).find("\"game_version\": \"" + version + "\"") != std::string::npos);

    // A second, newer save made by a later game: it is not loaded, the session stays, the file stays, and what
    // CONTINUE and the options take is the newest save that loads.
    save.map = 7;
    const int second = saves.save(0);
    SH_CHECK(second == 2);
    const fs::path later = sh::save::active_store().save_path(2);
    set_version(later, "9.9.9");
    const auto later_bytes = bytes_of(later);
    save.map = 9;
    const std::uint32_t running = sh::save::GameSaves::fingerprint();
    SH_CHECK(saves.load(second) == -2);
    SH_CHECK(sh::save::GameSaves::fingerprint() == running && save.map == 9);
    SH_CHECK(host.loads.size() == 1);
    std::int32_t numbers[4] = {};
    int listed = 0;
    SH_CHECK(ShSave_ListLoadable(numbers, 4, &listed) == 1 && numbers[0] == 1 && listed == 2);
    SH_CHECK(ShSave_ListNewest(numbers, 4) == 1 && numbers[0] == 1);
    SH_CHECK(saves.load(numbers[0]) == 1 && save.map == 3);
    SH_CHECK(bytes_of(later) == later_bytes);
    // A version that does not parse is refused the same way; the same numbers with a suffix load.
    set_version(later, "next");
    SH_CHECK(saves.load(second) == -2);
    set_version(later, version + "-test");
    SH_CHECK(saves.load(second) == 1 && save.map == 7);
    // The newer save can be saved over (the screen asks) and deleted.
    set_version(later, "9.9.9");
    SH_CHECK(saves.save(second) == second && saves.load(second) == 1);
    set_version(later, "9.9.9");
    SH_CHECK(ShSave_Delete(second) == 1 && !fs::exists(later));

    // A quick save and a save file of a newer game are refused too.
    SH_CHECK(saves.quick_save(0) == 1);
    const fs::path quick = sh::save::active_store().quick_path(0);
    SH_CHECK(meta_of(quick).find("\"game_version\": \"" + version + "\"") != std::string::npos);
    set_version(quick, "9.9.9");
    SH_CHECK(sh::save::GameSaves::quick_state(0) == -2 && saves.quick_load(0) == -2);
    const fs::path file = dir / "state.sav";
    SH_CHECK(saves.save_file(file.string()) == 1);
    set_version(file, "10.0.0");
    SH_CHECK(sh::save::GameSaves::file_state(file.string()) == -2 && saves.load_file(file.string()) == -2);

    game.save = sh::game::SaveRecord{};
    game.autosave = sh::game::SaveRecord{};
    game.settings = sh::game::Settings{};
    saves.set_host(nullptr);
    if (in_temp) fs::remove_all(root, ec);
    fs::remove_all(dir, ec);
}
