// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)

#include "engine/config/gameplay_config.h"

#include "engine/inventory/game/inventory_host.h"
#include "engine/items/item_library.h"
#include "engine/gconfig/difficulties.h"
#include "engine/gconfig/gameplay_params.h"
#include "engine/log/sh_log.h"

#include <cmath>
#include <optional>
#include <string>
#include <vector>

namespace sh::gconfig {
namespace {

std::optional<GameplayLibrary> s_gameplay;
std::optional<DifficultyLibrary> s_difficulties;

std::string mod_note(const std::string& mod_id) {
    return mod_id.empty() ? std::string() : " (mod " + mod_id + ")";
}

void load_gameplay(const std::filesystem::path& gamedata, const std::filesystem::path& mods) {
    if (s_gameplay) {
        return;
    }
    std::vector<std::string> warnings;
    LoadSource source;
    Result<GameplayLibrary> loaded = GameplayLibrary::load(gamedata, mods, warnings, &source);
    if (!loaded) {
        SH_WARN("[GAMEPLAY] %s (keeping the compiled-in values)", loaded.error().c_str());
        return;
    }
    for (const std::string& w : warnings) {
        SH_WARN("[GAMEPLAY] %s", w.c_str());
    }
    for (const std::string& p : loaded.value().check_consistency()) {
        SH_WARN("[GAMEPLAY] %s", p.c_str());
    }
    s_gameplay.emplace(std::move(loaded.value()));
    SH_LOG("[GAMEPLAY] gamedata/config: max health %g, saturation %g%s", s_gameplay->params().max_health,
           s_gameplay->params().max_saturation, mod_note(source.mod_id).c_str());
}

void load_difficulties(const std::filesystem::path& gamedata, const std::filesystem::path& mods) {
    if (s_difficulties) {
        return;
    }
    std::vector<std::string> warnings;
    DifficultyLoadSource source;
    Result<DifficultyLibrary> loaded = DifficultyLibrary::load(gamedata, mods, warnings, &source);
    if (!loaded) {
        SH_WARN("[DIFFICULTY] %s (keeping the compiled-in EASY/NORMAL/HARD)", loaded.error().c_str());
        return;
    }
    for (const std::string& w : warnings) {
        SH_WARN("[DIFFICULTY] %s", w.c_str());
    }
    for (const std::string& p : loaded.value().check_consistency()) {
        SH_WARN("[DIFFICULTY] %s", p.c_str());
    }
    s_difficulties.emplace(std::move(loaded.value()));
    const std::size_t n = s_difficulties->count();
    SH_LOG("[DIFFICULTY] gamedata/config: %zu entr%s, default \"%s\"%s", n, n == 1 ? "y" : "ies",
           s_difficulties->entry(s_difficulties->default_index()).label.c_str(), mod_note(source.mod_id).c_str());
}

// Q12 of a value in units, truncated like the original's integer casts.
std::int32_t q12(double units) {
    return static_cast<std::int32_t>(static_cast<int>(units * 4096.0));
}

const DifficultyEntry* difficulty_entry(std::int32_t index) {
    const DifficultyLibrary& lib = active_difficulties();
    if (index < 0 || static_cast<std::size_t>(index) >= lib.count()) {
        return nullptr;
    }
    return &lib.entry(static_cast<std::size_t>(index));
}

}  // namespace

void load_active_config(const std::filesystem::path& gamedata_dir, const std::filesystem::path& mods_dir) {
    load_gameplay(gamedata_dir, mods_dir);
    load_difficulties(gamedata_dir, mods_dir);
}

void reload_active_config(const std::filesystem::path& gamedata_dir, const std::filesystem::path& mods_dir) {
    s_gameplay.reset();
    s_difficulties.reset();
    load_active_config(gamedata_dir, mods_dir);
}

const GameplayLibrary& active_gameplay() {
    static const GameplayLibrary original;
    return s_gameplay ? *s_gameplay : original;
}

const DifficultyLibrary& active_difficulties() {
    static const DifficultyLibrary original;
    return s_difficulties ? *s_difficulties : original;
}

}  // namespace sh::gconfig

extern "C" {

void Gameplay_GamedataInit(const char* gamedataDir, const char* modsDir) {
    if (gamedataDir != nullptr) {
        sh::gconfig::load_gameplay(gamedataDir, modsDir != nullptr ? modsDir : "");
    }
}

void Difficulty_GamedataInit(const char* gamedataDir, const char* modsDir) {
    if (gamedataDir != nullptr) {
        sh::gconfig::load_difficulties(gamedataDir, modsDir != nullptr ? modsDir : "");
    }
}

int32_t Gameplay_MaxHealthQ12(void) {
    return sh::gconfig::q12(sh::gconfig::active_gameplay().params().max_health);
}

int32_t Gameplay_MaxSaturationQ12(void) {
    return sh::gconfig::q12(sh::gconfig::active_gameplay().params().max_saturation);
}

int32_t Gameplay_HealAmountQ12(int32_t itemId) {
    // The item's "heal" names its gameplay.json "health_items" row.
    const sh::items::ItemDef* def = sh::inventory::game::item_def(itemId);
    if (def == nullptr || def->inventory.heal.empty()) {
        return 0;
    }
    const sh::gconfig::GameplayParams& p = sh::gconfig::active_gameplay().params();
    const auto it = p.heal_amounts.find(def->inventory.heal);
    return it != p.heal_amounts.end() ? sh::gconfig::q12(it->second) : 0;
}

int32_t Gameplay_GrabEscapeCount(int32_t grab, int32_t difficulty) {
    if (grab < 0 || grab >= static_cast<int32_t>(sh::gconfig::Grab::Count)) {
        return 0;
    }
    return sh::gconfig::active_gameplay().grab_escape(static_cast<sh::gconfig::Grab>(grab), difficulty);
}

int32_t Difficulty_Count(void) {
    return static_cast<int32_t>(sh::gconfig::active_difficulties().count());
}

const char* Difficulty_Label(int32_t index) {
    const sh::gconfig::DifficultyEntry* e = sh::gconfig::difficulty_entry(index);
    return e != nullptr ? e->label.c_str() : "";
}

int32_t Difficulty_BaseDifficulty(int32_t index) {
    const sh::gconfig::DifficultyEntry* e = sh::gconfig::difficulty_entry(index);
    return e != nullptr ? e->base_difficulty : 0;
}

const char* Difficulty_StartMap(int32_t index) {
    const sh::gconfig::DifficultyEntry* e = sh::gconfig::difficulty_entry(index);
    return e != nullptr && !e->start_map.empty() ? e->start_map.c_str() : nullptr;
}

int32_t Difficulty_DefaultIndex(void) {
    return static_cast<int32_t>(sh::gconfig::active_difficulties().default_index());
}

}  // extern "C"
