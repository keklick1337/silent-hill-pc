// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
#include "engine/save/save_c.h"

#include "engine/log/sh_log.h"
#include "engine/save/memory_card.h"
#include "engine/save/save_game.h"
#include "engine/save/save_service.h"
#include "engine/save/save_store.h"
#include "engine/version/engine_version.h"

#include <algorithm>
#include <cstring>
#include <memory>
#include <string>

namespace {

namespace fs = std::filesystem;
using namespace sh::save;
using sh::Result;

ShSaveNameTable& c_names() {
    static ShSaveNameTable names{};
    return names;
}

NameTable name_table() {
    const ShSaveNameTable& c = c_names();
    NameTable t;
    if (c.item_name != nullptr) t.item_name = c.item_name;
    if (c.item_number != nullptr) t.item_number = c.item_number;
    if (c.flag_name != nullptr) t.flag_name = c.flag_name;
    if (c.flag_number != nullptr) t.flag_number = c.flag_number;
    if (c.flag_name_in_old_save != nullptr) t.flag_name_in_old_save = c.flag_name_in_old_save;
    if (c.map_name != nullptr) t.map_name = c.map_name;
    if (c.map_number != nullptr) t.map_number = c.map_number;
    if (c.paper_map_name != nullptr) t.paper_map_name = c.paper_map_name;
    if (c.paper_map_number != nullptr) t.paper_map_number = c.paper_map_number;
    return t;
}

std::unique_ptr<SaveStore>& store_ptr() {
    static std::unique_ptr<SaveStore> store;
    return store;
}

SaveStore& store() {
    auto& s = store_ptr();
    if (!s) {
        s = std::make_unique<SaveStore>(fs::path(".savegames"));
        s->set_names(name_table());
        s->set_game_version(std::string(sh::version::kText));
    }
    return *s;
}

std::string& root_text() {
    static std::string text;
    return text;
}

SlotSummary from_c(const ShSaveSummary& s) {
    SlotSummary out;
    out.sequence = s.sequence;
    out.play_time = s.play_time;
    out.save_count = s.save_count;
    out.location = s.location;
    out.next_fear = s.next_fear != 0;
    out.extra_hours = s.extra_hours;
    out.special_items = s.special_items;
    return out;
}

ShSaveSummary to_c(const SlotSummary& s) {
    ShSaveSummary out{};
    out.sequence = s.sequence;
    out.play_time = s.play_time;
    out.save_count = s.save_count;
    out.location = s.location;
    out.next_fear = s.next_fear ? 1 : 0;
    out.extra_hours = s.extra_hours;
    out.special_items = s.special_items;
    return out;
}

void log_warnings(const Warnings& warnings, const char* what) {
    for (const std::string& w : warnings) {
        SH_LOG_WARN(SAVE, "save %s: %s", what, w.c_str());
    }
}

// The mods of the next write.
std::optional<std::vector<sh::save::SavedMod>>& outgoing_mods() {
    static std::optional<std::vector<sh::save::SavedMod>> mods;
    return mods;
}

// The record the store writes for the game's running state.
bool record_of(const ShSaveSummary* summary, const sh::game::SaveRecord* state, const sh::game::Settings* options,
               SlotRecord& record) {
    if (summary == nullptr || state == nullptr) {
        return false;
    }
    SaveGame game = from_live_state(*state, store().names());
    if (options != nullptr) {
        game.options = from_live_options(*options);
    }
    record.summary = from_c(*summary);
    record.state = to_json(game);
    record.mods = outgoing_mods();
    record.game_version = store().game_version();
    return true;
}

Result<SaveGame> game_of(const SlotRead& read, const char* what) {
    if (read.state == SlotState::Empty) {
        return Result<SaveGame>::fail("");
    }
    if (read.state == SlotState::Damaged) {
        SH_LOG_WARN(SAVE, "save %s is damaged: %s", what, read.error.c_str());
        return Result<SaveGame>::fail(read.error);
    }
    log_warnings(read.warnings, what);
    auto game = from_json(read.record.state);
    if (!game) {
        SH_LOG_WARN(SAVE, "save %s is damaged: %s", what, game.error().c_str());
    }
    return game;
}

// The RMNS chunk of the next write and of the last save delivered into a state.
std::string& outgoing_remains() {
    static std::string text;
    return text;
}
std::string& incoming_remains() {
    static std::string text;
    return text;
}

// A save of a later game is refused before anything of it is taken (and whatever its state would convert to).
bool refused_as_newer(const SlotRead& read, const char* what) {
    if (read.state != SlotState::Ok || !store().from_newer_game(read.record.game_version)) {
        return false;
    }
    SH_LOG_WARN(SAVE, "save %s was made with version %s, newer than this game (%s): not loaded", what,
                read.record.game_version.c_str(), store().game_version().c_str());
    return true;
}

int deliver(const SlotRead& read, ShSaveSummary* summary, sh::game::SaveRecord* state, const char* what) {
    if (read.state == SlotState::Empty) {
        return 0;
    }
    if (refused_as_newer(read, what)) {
        return -2;
    }
    auto game = game_of(read, what);
    if (!game) {
        return -1;
    }
    if (state != nullptr) {
        Warnings warnings;
        auto live = to_live_state(game.value(), store().names(), warnings);
        log_warnings(warnings, what);
        if (!live) {
            SH_LOG_WARN(SAVE, "save %s cannot be loaded: %s", what, live.error().c_str());
            return -1;
        }
        *state = live.value();
        incoming_remains() = read.record.remains;
    }
    if (summary != nullptr) {
        *summary = to_c(read.record.summary);
    }
    return 1;
}

std::string save_name(int number) { return store().save_path(number).filename().string(); }

}  // namespace

extern "C" void ShSave_Open(const char* exe_dir, const char* setting) {
    const fs::path base = (exe_dir != nullptr && exe_dir[0] != '\0') ? fs::path(exe_dir) : fs::path(".");
    fs::path root;
    if (setting != nullptr && setting[0] != '\0') {
        root = fs::path(setting);
        if (root.is_relative()) {
            root = base / root;
        }
    } else {
        root = base / ".savegames";
    }
    store_ptr() = std::make_unique<SaveStore>(root.lexically_normal());
    store_ptr()->set_names(name_table());
    store_ptr()->set_game_version(std::string(sh::version::kText));
    root_text() = store().root().string();
    SH_LOG_INFO(SAVE, "saves: %s", root_text().c_str());
}

extern "C" const char* ShSave_Root(void) {
    if (root_text().empty()) {
        root_text() = store().root().string();
    }
    return root_text().c_str();
}

extern "C" void ShSave_SetNames(const ShSaveNameTable* names) {
    c_names() = names != nullptr ? *names : ShSaveNameTable{};
    store().set_names(name_table());
}

extern "C" int ShSave_MigrateLegacy(const char* const* dirs, int count) {
    std::vector<fs::path> paths;
    for (int i = 0; i < count; ++i) {
        if (dirs[i] != nullptr && dirs[i][0] != '\0') {
            paths.emplace_back(dirs[i]);
        }
    }
    MigrationReport report = migrate_card_layout(store());
    for (const MigrationReport& more : {migrate_legacy_cards(store(), paths), import_drop_folder(store())}) {
        report.saves += more.saves;
        report.lines.insert(report.lines.end(), more.lines.begin(), more.lines.end());
    }
    for (const std::string& line : report.lines) {
        SH_LOG_INFO(SAVE, "migration: %s", line.c_str());
    }
    // DEPRECATED, to be removed in a future version: saves of the older formats rewritten in the current one.
    std::vector<std::string> converted;
    if (const int n = store().convert_old_saves(converted); n > 0) {
        SH_LOG_INFO(SAVE, "%d save(s) converted to save format %d (the old-format reader is deprecated)", n,
                    static_cast<int>(kFormatVersion));
    }
    for (const std::string& line : converted) {
        SH_LOG_INFO(SAVE, "conversion: %s", line.c_str());
    }
    return report.saves;
}

extern "C" int ShSave_ListLoadable(int32_t* numbers, int capacity, int* listed) {
    int n = 0;
    int all = 0;
    for (const SaveEntry& e : store().list()) {
        // A file of a later save format is a save too, though this build reads nothing of it.
        const bool newer = e.newer_format || (e.state == SlotState::Ok && store().from_newer_game(e.game_version));
        if (e.state != SlotState::Ok && !newer) {
            continue;
        }
        ++all;
        if (newer) {
            continue;
        }
        if (numbers != nullptr && n < capacity) {
            numbers[n] = e.number;
        }
        ++n;
    }
    if (listed != nullptr) {
        *listed = all;
    }
    return n;
}

extern "C" int ShSave_ListNewest(int32_t* numbers, int capacity) {
    return ShSave_ListLoadable(numbers, capacity, nullptr);
}

extern "C" int32_t ShSave_NewestSequence(void) { return store().newest_sequence(); }

extern "C" int ShSave_Read(int number, ShSaveSummary* summary, sh::game::SaveRecord* state) {
    const std::string what = save_name(number);
    return deliver(store().read(number), summary, state, what.c_str());
}

extern "C" int ShSave_ReadOptions(int number, sh::game::Settings* options) {
    const std::string what = save_name(number);
    const SlotRead read = store().read(number);
    if (read.state == SlotState::Empty) {
        return 0;
    }
    if (refused_as_newer(read, what.c_str())) {
        return -2;
    }
    auto game = game_of(read, what.c_str());
    if (!game) {
        return -1;
    }
    if (!game.value().options || options == nullptr) {
        return 0;
    }
    *options = to_live_options(*game.value().options);
    return 1;
}

extern "C" int ShSave_Write(int number, const ShSaveSummary* summary, const sh::game::SaveRecord* state,
                            const sh::game::Settings* options) {
    SlotRecord record;
    if (!record_of(summary, state, options, record)) {
        return -1;
    }
    record.remains = outgoing_remains();
    auto written = store().write(number, record);
    if (!written) {
        SH_LOG_ERROR(SAVE, "write save %d: %s", number, written.error().c_str());
        return -1;
    }
    SH_LOG_INFO(SAVE, "wrote %s", store().save_path(written.value()).string().c_str());
    return written.value();
}

extern "C" int ShSave_Delete(int number) {
    if (store().read(number).state == SlotState::Empty) {
        return 0;
    }
    if (auto r = store().remove(number); !r) {
        SH_LOG_ERROR(SAVE, "delete save %d: %s", number, r.error().c_str());
        return -1;
    }
    SH_LOG_INFO(SAVE, "deleted %s", store().save_path(number).string().c_str());
    return 1;
}

extern "C" int ShSave_QuickRead(int index, ShSaveSummary* summary, sh::game::SaveRecord* state) {
    const std::string what = "quick " + std::to_string(index + 1);
    return deliver(store().read_quick(index), summary, state, what.c_str());
}

extern "C" int ShSave_QuickWrite(int index, const ShSaveSummary* summary, const sh::game::SaveRecord* state,
                                 const sh::game::Settings* options) {
    SlotRecord record;
    if (!record_of(summary, state, options, record)) {
        return 0;
    }
    record.remains = outgoing_remains();
    if (auto r = store().write_quick(index, record); !r) {
        SH_LOG_ERROR(SAVE, "quick save %d: %s", index + 1, r.error().c_str());
        return -1;
    }
    SH_LOG_INFO(SAVE, "wrote %s", store().quick_path(index).string().c_str());
    return 1;
}

extern "C" int ShSave_FileRead(const char* path, ShSaveSummary* summary, sh::game::SaveRecord* state) {
    return deliver(store().read_at(path), summary, state, path);
}

extern "C" int ShSave_FileWrite(const char* path, const ShSaveSummary* summary, const sh::game::SaveRecord* state,
                                const sh::game::Settings* options) {
    SlotRecord record;
    if (!record_of(summary, state, options, record)) {
        return 0;
    }
    record.remains = outgoing_remains();
    if (auto r = store().write_at(path, record); !r) {
        SH_LOG_ERROR(SAVE, "save file %s: %s", path, r.error().c_str());
        return -1;
    }
    SH_LOG_INFO(SAVE, "wrote %s", path);
    return 1;
}

extern "C" void ShSave_SetRemains(const char* text) { outgoing_remains() = text != nullptr ? text : ""; }

extern "C" const char* ShSave_LoadedRemains(void) { return incoming_remains().c_str(); }

sh::save::SaveStore& sh::save::active_store() { return store(); }

void sh::save::set_outgoing_mods(std::optional<std::vector<SavedMod>> mods) { outgoing_mods() = std::move(mods); }
