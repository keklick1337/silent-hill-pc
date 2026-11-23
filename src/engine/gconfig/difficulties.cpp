// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)

#include "engine/gconfig/difficulties.h"

#include "engine/mods/data_merge.h"
#include "formats/common/json.h"

#include <algorithm>
#include <fstream>
#include <iterator>

namespace sh::gconfig {

namespace {

using json::Value;

[[noreturn]] void fail(std::string message) { throw std::runtime_error(std::move(message)); }

std::string where(const Value& value, std::string_view what) {
    return std::to_string(value.line()) + ", column " + std::to_string(value.column()) + ": " +
           std::string(what);
}

}  // namespace

DifficultyLibrary::DifficultyLibrary() {
    // The original's picker (title.cpp MainMenu_DifficultyTextDraw): EASY/NORMAL/HARD,
    // NORMAL preselected, indices 0..2 mapping to e_GameDifficulty -1/0/+1.
    entries_.push_back({"EASY", -1, false});
    entries_.push_back({"NORMAL", 0, true});
    entries_.push_back({"HARD", 1, false});
}

Result<DifficultyLibrary> DifficultyLibrary::parse(std::string_view text, std::vector<std::string>& warnings) try {
    const Result<Value> doc = json::parse(text);
    if (!doc) {
        return Result<DifficultyLibrary>::fail("difficulties.json: " + doc.error());
    }

    DifficultyLibrary library;
    library.entries_.clear();

    const Value* list = doc.value().find("difficulties");
    if (!list || !list->is_array() || list->array().empty()) {
        fail("difficulties.json: \"difficulties\" must be a non-empty array");
    }
    for (const Value& item : list->array()) {
        if (!item.is_object()) {
            fail(where(item, "difficulty entries must be objects"));
        }
        const Value* label = item.find("label");
        if (!label || !label->is_string() || label->string().empty()) {
            fail(where(item, "a difficulty entry needs a non-empty string \"label\""));
        }
        DifficultyEntry entry;
        entry.label = label->string();
        entry.base_difficulty = 0;
        if (const Value* base = item.find("base_difficulty"); base && !base->is_null()) {
            if (!base->is_number() || base->number() != static_cast<double>(static_cast<int>(base->number())) ||
                base->number() < -1 || base->number() > 1) {
                fail(where(*base, "\"base_difficulty\" must be -1 (easy), 0 (normal) or 1 (hard)"));
            }
            entry.base_difficulty = static_cast<int>(base->number());
        }
        if (const Value* def = item.find("default"); def && !def->is_null()) {
            if (!def->is_bool()) {
                fail(where(*def, "\"default\" must be true or false"));
            }
            entry.is_default = def->boolean();
        }
        if (const Value* map = item.find("start_map"); map && !map->is_null()) {
            if (!map->is_string() || map->string().empty()) {
                fail(where(*map, "\"start_map\" must be a non-empty map name"));
            }
            entry.start_map = map->string();
        }
        library.entries_.push_back(std::move(entry));
    }
    return Result<DifficultyLibrary>::ok(std::move(library));
} catch (const std::exception& e) {
    return Result<DifficultyLibrary>::fail(std::string(e.what()));
}

Result<DifficultyLibrary> DifficultyLibrary::load(const std::filesystem::path& gamedata_dir,
                                                  const std::filesystem::path& mods_dir,
                                                  std::vector<std::string>& warnings, DifficultyLoadSource* source) {
    namespace fs = std::filesystem;
    const fs::path relative = fs::path("config") / "difficulties.json";
    // Base + every active mod's patch, in load order.
    Result<mods::MergedFile> read = mods::read_data_file(gamedata_dir, mods_dir, relative, warnings);
    if (!read) {
        return Result<DifficultyLibrary>::fail(read.error());
    }
    for (const mods::DataLayer& mod : read.value().mods) {
        if (source) {
            source->mod_id = mod.mod_id;
        }
        if (!mod.gameplay) {
            warnings.push_back("mod " + mod.mod_id + " changes the difficulty list but does not declare "
                                                     "\"gameplay\": true in mod.json");
        }
    }
    return parse(read.value().text, warnings);
}

std::size_t DifficultyLibrary::default_index() const {
    for (std::size_t i = 0; i < entries_.size(); ++i) {
        if (entries_[i].is_default) {
            return i;
        }
    }
    return entries_.size() / 2;  // the middle entry, i.e. NORMAL for the original's three
}

std::vector<std::string> DifficultyLibrary::check_consistency() const {
    std::vector<std::string> problems;
    std::size_t defaults = 0;
    for (const DifficultyEntry& entry : entries_) {
        if (entry.is_default) {
            ++defaults;
        }
    }
    if (defaults > 1) {
        problems.push_back("more than one difficulty is flagged \"default\"; the first wins");
    }
    if (defaults == 0) {
        problems.push_back("no difficulty flagged \"default\"; the middle entry is preselected");
    }
    return problems;
}

}  // namespace sh::gconfig
