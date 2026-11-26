// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)

#include "engine/save/game_saves.h"

#include "engine/game_state/session.h"
#include "engine/ids/game_numbers.h"
#include "engine/log/sh_log.h"
#include "engine/save/console_records.h"
#include "engine/save/save_c.h"
#include "engine/save/save_service.h"

#include <cstdio>
#include <cstring>

namespace sh::save {

namespace {

constexpr int kNextFearLocation = 24;  // a Next Fear save point starts the next playthrough

// The blood colours of the options screen (Normal, Green, Violet, Black).
constexpr std::uint8_t kBloodColors[] = {0, 2, 5, 11};

game::SaveRecord& progress() { return game::session().game.save; }

const char* map_name_of(int number) {
    GameSavesHost* host = game_saves().host();
    return host != nullptr ? host->map_name(number) : nullptr;
}
int map_number_of(const char* name) {
    GameSavesHost* host = game_saves().host();
    return host != nullptr ? host->map_number(name) : -1;
}

// What a save names: items (and the weapon in hand), event flags, maps, paper maps.
const ShSaveNameTable kNames = {ids::item_name, ids::item_number, ids::flag_name, ids::flag_number,
                                map_name_of,    map_number_of,    ids::paper_map_name, ids::paper_map_number,
                                ids::flag_name_in_old_save};

}  // namespace

int GameSaves::list_newest(std::int32_t* numbers, int capacity) { return ShSave_ListNewest(numbers, capacity); }

void GameSaves::open(const char* exe_dir, const char* setting, const char* gamedata_dir) {
    if (opened_) {
        return;
    }
    opened_ = true;
    ShSave_Open(exe_dir, setting);
    ShSave_SetNames(&kNames);

    // Older builds kept the card files in gamedata/save under the current directory.
    char exe_save[1100];
    char data_save[1100];
    std::snprintf(exe_save, sizeof(exe_save), "%sgamedata/save", exe_dir != nullptr ? exe_dir : "");
    std::snprintf(data_save, sizeof(data_save), "%s/save", gamedata_dir != nullptr ? gamedata_dir : "");
    const char* dirs[3] = {"gamedata/save", exe_save, data_save};
    const int saves = ShSave_MigrateLegacy(dirs, 3);
    if (saves > 0) {
        SH_LOG_INFO(SAVE, "migration: %d save(s) from older builds now in %s", saves, ShSave_Root());
    }
}

int GameSaves::boot_check() {
    switch (boot_.check()) {
        case BootCheck::Outcome::NoSave:
            return 1;
        case BootCheck::Outcome::HasSave:
            return 2;
        default:
            return 0;
    }
}

bool GameSaves::boot_poll_load() {
    if (!boot_.poll_load()) {
        return false;
    }
    SH_LOG_INFO(SAVE, "boot: options of the newest save %s", boot_.options_applied() ? "loaded" : "not accepted");
    return true;
}

// The options of save `number` become the session's settings. False when it has none the game accepts (the
// current ones stay).
bool GameSaves::apply_options(std::int32_t number) {
    game::Settings options{};
    if (ShSave_ReadOptions(number, &options) <= 0) {
        return false;
    }
    game::Settings& settings = game::session().game.settings;
    settings = options;
    // Saves of older PC builds can carry garbage in option bytes the player never set; a stray blood colour
    // re-palettes every blood effect of the session.
    bool valid = false;
    for (const std::uint8_t c : kBloodColors) {
        valid = valid || settings.blood_color == c;
    }
    if (!valid) {
        settings.blood_color = 0;
    }
    if (host_ != nullptr) {
        host_->blood_color_loaded(settings.blood_color);
    }
    return true;
}

std::uint32_t GameSaves::fingerprint() {
    // The journal's tag of a save: the console byte form, so it reads the same as before the records had names.
    const std::vector<std::uint8_t> bytes = console::encode_save_record(progress());
    std::uint32_t hash = 2166136261u;
    for (const std::uint8_t b : bytes) {
        hash = (hash ^ b) * 16777619u;
    }
    return hash;
}

int GameSaves::save(int number) {
    game::SaveRecord& record = progress();
    ShSaveSummary summary = {};

    if (host_ != nullptr) {
        host_->saving(number);
    }
    const int location = record.location;
    if (location == kNextFearLocation) {
        summary.save_count = 0;  // the entry of a Next Fear save counts no saves
    } else {
        record.save_count = static_cast<std::int16_t>(record.save_count + 1);
        summary.save_count = static_cast<std::uint16_t>(record.save_count);
    }
    const std::uint8_t mode = record.mode_bits;
    summary.sequence = ShSave_NewestSequence() + 1;
    summary.play_time = record.play_time;
    summary.location = static_cast<std::uint8_t>(location);
    summary.next_fear = mode & 1u;
    summary.extra_hours = (mode >> 1) & 3u;
    summary.special_items = mode >> 3;

    game::GameBlock& block = game::session().game;
    ShSave_SetRemains(host_ != nullptr ? host_->remains_text().c_str() : "");
    set_outgoing_mods(host_ != nullptr ? host_->active_mods() : std::nullopt);
    const int written = ShSave_Write(number, &summary, &record, &block.settings);
    if (written > 0) {
        block.autosave = block.save;
        SH_LOG_INFO(SAVE, "saved save %d (map %d, state %08x)", written, static_cast<int>(record.map),
                    static_cast<unsigned>(fingerprint()));
    }
    return written;
}

int GameSaves::load(int number) {
    std::int32_t newest = 0;

    // Damaged, or a map this build lacks: the running state stays as it was ("Failed to load!").
    const int result = ShSave_Read(number, nullptr, &progress());
    if (result <= 0) {
        return result;
    }
    // The options come from the newest save, as the original took them from the newest file of the card.
    if (ShSave_ListNewest(&newest, 1) <= 0 || !apply_options(newest)) {
        SH_LOG_WARN(SAVE, "save %d: the newest save has no valid options, the current ones stay", static_cast<int>(newest));
    }
    SH_LOG_INFO(SAVE, "loaded save %d (map %d, state %08x)", number, static_cast<int>(progress().map),
                static_cast<unsigned>(fingerprint()));
    if (host_ != nullptr) {
        host_->remains_loaded(ShSave_LoadedRemains());
        host_->loaded(number);
    }
    return 1;
}

int GameSaves::quick_save(int index) {
    game::SaveRecord& record = progress();
    ShSaveSummary summary = {};

    if (host_ != nullptr) {
        host_->quick_saving(index);
    }
    const int location = record.location;
    const std::int16_t count_before = record.save_count;
    if (location == kNextFearLocation) {
        summary.save_count = 0;
    } else {
        record.save_count = static_cast<std::int16_t>(record.save_count + 1);
        summary.save_count = static_cast<std::uint16_t>(record.save_count);
    }
    const std::uint8_t mode = record.mode_bits;
    summary.sequence = ShSave_NewestSequence() + 1;
    summary.play_time = record.play_time;
    summary.location = static_cast<std::uint8_t>(location);
    summary.next_fear = mode & 1u;
    summary.extra_hours = (mode >> 1) & 3u;
    summary.special_items = mode >> 3;

    game::GameBlock& block = game::session().game;
    ShSave_SetRemains(host_ != nullptr ? host_->remains_text().c_str() : "");
    set_outgoing_mods(host_ != nullptr ? host_->active_mods() : std::nullopt);
    const int written = ShSave_QuickWrite(index, &summary, &record, &block.settings);
    if (written <= 0) {
        // Nothing was written, so the running game must not count a save either.
        record.save_count = count_before;
        return -1;
    }
    block.autosave = block.save;
    SH_LOG_INFO(SAVE, "quick saved %d (map %d, state %08x)", index + 1, static_cast<int>(record.map),
                static_cast<unsigned>(fingerprint()));
    return 1;
}

int GameSaves::quick_load(int index) {
    const int result = ShSave_QuickRead(index, nullptr, &progress());
    if (result <= 0) {
        return result;
    }
    SH_LOG_INFO(SAVE, "quick loaded %d (map %d, state %08x)", index + 1, static_cast<int>(progress().map),
                static_cast<unsigned>(fingerprint()));
    if (host_ != nullptr) {
        host_->remains_loaded(ShSave_LoadedRemains());
        host_->quick_loaded(index);
    }
    return 1;
}

int GameSaves::save_file(const std::string& path) {
    const game::SaveRecord& record = progress();
    ShSaveSummary summary = {};
    const std::uint8_t mode = record.mode_bits;
    summary.save_count = static_cast<std::uint16_t>(record.save_count);
    summary.sequence = ShSave_NewestSequence() + 1;
    summary.play_time = record.play_time;
    summary.location = static_cast<std::uint8_t>(record.location);
    summary.next_fear = mode & 1u;
    summary.extra_hours = (mode >> 1) & 3u;
    summary.special_items = mode >> 3;
    ShSave_SetRemains(host_ != nullptr ? host_->remains_text().c_str() : "");
    set_outgoing_mods(host_ != nullptr ? host_->active_mods() : std::nullopt);
    return ShSave_FileWrite(path.c_str(), &summary, &record, &game::session().game.settings) > 0 ? 1 : -1;
}

int GameSaves::load_file(const std::string& path) {
    const int result = ShSave_FileRead(path.c_str(), nullptr, &progress());
    if (result <= 0) {
        return result;
    }
    SH_LOG_INFO(SAVE, "loaded %s (map %d, state %08x)", path.c_str(), static_cast<int>(progress().map),
                static_cast<unsigned>(fingerprint()));
    if (host_ != nullptr) {
        host_->remains_loaded(ShSave_LoadedRemains());
        host_->file_loaded();
    }
    return 1;
}

int GameSaves::file_state(const std::string& path) { return ShSave_FileRead(path.c_str(), nullptr, nullptr); }

int GameSaves::quick_state(int index) { return ShSave_QuickRead(index, nullptr, nullptr); }

GameSaves& game_saves() {
    static GameSaves saves;
    return saves;
}

}  // namespace sh::save
