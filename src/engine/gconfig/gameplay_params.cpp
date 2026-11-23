// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)

#include "engine/gconfig/gameplay_params.h"

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

double number_of(const Value& parent, std::string_view key, double fallback) {
    const Value* field = parent.find(key);
    if (!field || field->is_null()) {
        return fallback;
    }
    if (!field->is_number()) {
        fail(where(*field, "\"" + std::string(key) + "\" must be a number"));
    }
    return field->number();
}

}  // namespace

GameplayLibrary::GameplayLibrary() {
    // The original's counts (player_control.cpp), so the accessors answer the retail
    // values before (and without) the data.
    using Row = std::array<std::int32_t, 3>;
    const std::array<Row, kGrabNames.size()> original = {
        Row{1600, 3200, 4800},   // on_floor
        Row{800, 1600, 2400},    // grab_pinned_front
        Row{1200, 2400, 3600},   // grab_pinned_back
        Row{800, 1600, 2400},    // grab_torso_front
        Row{1000, 2000, 3000},   // grab_torso_back
        Row{700, 1400, 2100},    // grab_legs_front
        Row{800, 1600, 2400},    // grab_legs_back
        Row{3600, 7200, 10800},  // grab_neck_front
        Row{3600, 7200, 10800},  // grab_neck_back
    };
    params_.grab_escape = original;
}

Result<GameplayLibrary> GameplayLibrary::parse(std::string_view text, std::vector<std::string>& warnings) try {
    const Result<Value> doc = json::parse(text);
    if (!doc) {
        return Result<GameplayLibrary>::fail("gameplay.json: " + doc.error());
    }

    GameplayLibrary library;
    GameplayParams& p = library.params_;
    if (const Value* player = doc.value().find("player"); player && player->is_object()) {
        p.max_health = number_of(*player, "max_health", p.max_health);
        p.max_saturation = number_of(*player, "max_saturation", p.max_saturation);
    }
    if (const Value* items = doc.value().find("health_items"); items && items->is_object()) {
        p.heal_health_drink = number_of(*items, "health_drink", p.heal_health_drink);
        p.heal_first_aid_kit = number_of(*items, "first_aid_kit", p.heal_first_aid_kit);
        p.heal_ampoule = number_of(*items, "ampoule", p.heal_ampoule);
        for (const auto& [name, value] : items->members()) {
            if (!name.empty() && name.front() != '_' && value.is_number()) {
                p.heal_amounts[name] = value.number();
            }
        }
    }
    if (const Value* grab = doc.value().find("grab_escape"); grab && grab->is_object()) {
        for (std::size_t g = 0; g < kGrabNames.size(); ++g) {
            const Value* row = grab->find(kGrabNames[g]);
            if (!row || row->is_null()) {
                warnings.push_back(std::string("grab_escape: no \"") + std::string(kGrabNames[g]) +
                                   "\" row; the compiled-in counts stay");
                continue;
            }
            if (!row->is_array() || row->array().size() != 3) {
                fail(where(*row, "grab_escape." + std::string(kGrabNames[g]) + " must be [easy, normal, hard]"));
            }
            for (int d = 0; d < 3; ++d) {
                const Value& v = row->array()[static_cast<std::size_t>(d)];
                if (!v.is_number() || v.number() < 0) {
                    fail(where(v, "grab_escape." + std::string(kGrabNames[g]) + " entries must be counts >= 0"));
                }
                p.grab_escape[g][d] = static_cast<std::int32_t>(v.number());
            }
        }
    }
    return Result<GameplayLibrary>::ok(std::move(library));
} catch (const std::exception& e) {
    return Result<GameplayLibrary>::fail(std::string(e.what()));
}

Result<GameplayLibrary> GameplayLibrary::load(const std::filesystem::path& gamedata_dir,
                                              const std::filesystem::path& mods_dir,
                                              std::vector<std::string>& warnings, LoadSource* source) {
    namespace fs = std::filesystem;
    const fs::path relative = fs::path("config") / "gameplay.json";
    // Base + every active mod's patch, in load order.
    Result<mods::MergedFile> read = mods::read_data_file(gamedata_dir, mods_dir, relative, warnings);
    if (!read) {
        return Result<GameplayLibrary>::fail(read.error());
    }
    for (const mods::DataLayer& mod : read.value().mods) {
        if (source) {
            source->mod_id = mod.mod_id;
            source->mod_gameplay = mod.gameplay;
        }
        if (!mod.gameplay) {
            warnings.push_back("mod " + mod.mod_id + " changes gameplay parameters but does not declare "
                                                     "\"gameplay\": true in mod.json");
        }
    }
    return parse(read.value().text, warnings);
}

std::int32_t GameplayLibrary::grab_escape(Grab grab, int difficulty) const {
    // e_GameDifficulty: Easy -1, Normal 0, Hard 1 -> row index 0..2.
    const int d = std::clamp(difficulty + 1, 0, 2);
    return params_.grab_escape[static_cast<std::size_t>(grab)][static_cast<std::size_t>(d)];
}

std::vector<std::string> GameplayLibrary::check_consistency() const {
    std::vector<std::string> problems;
    const GameplayParams& p = params_;
    if (p.max_health <= 0) {
        problems.push_back("player.max_health must be positive");
    }
    if (p.max_saturation < p.max_health) {
        problems.push_back("player.max_saturation below max_health: ampoule overflow clamps away");
    }
    for (const auto& row : p.grab_escape) {
        for (std::int32_t count : row) {
            if (count <= 0) {
                problems.push_back("grab_escape: counts must be positive (0 means never breaking free)");
            }
        }
    }
    return problems;
}

}  // namespace sh::gconfig
