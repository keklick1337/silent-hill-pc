// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)

#include "engine/debug/commands/spawn_roster.h"

#include "engine/chara/chara_library.h"
#include "engine/chara/game/chara_entry.h"
#include "engine/chara/runtime/chara_data.h"
#include "engine/chara/runtime/chara_spawn.h"
#include "engine/debug/command_services.h"
#include "engine/debug/commands/command_kit.h"
#include "engine/game_state/game_state.h"
#include "input/object_pick.h"

#include <algorithm>
#include <cstdint>
#include <map>

namespace sh::debug {

namespace {

namespace gs = sh::game_state;

constexpr int kPreviewSettleTicks = 3;
constexpr int kMenuReachMetres = 20;

bool in_list(const std::vector<SpawnEntry>& entries, int index) {
    return index >= 0 && index < static_cast<int>(entries.size());
}

}  // namespace

SpawnRoster& spawn_roster() {
    static SpawnRoster roster;
    return roster;
}

const std::vector<SpawnEntry>& SpawnRoster::entries() {
    entries_.clear();
    const sh::chara::CharaLibrary* library = Pc_CharaData_Library();
    if (library == nullptr) return entries_;
    for (const sh::chara::CharacterDef& def : library->characters()) {
        if (def.number == 0 || def.is_player) continue;
        SpawnEntry entry;
        entry.name = def.folder;
        entry.id = def.id;
        entry.category = sh::chara::CharaLibrary::spawn_category(def);
        entry.chara = def.number;
        entry.state = Pc_Chara_SpawnState(def.number);
        entry.spawnable = Pc_Chara_Spawnable(def.number) != 0;
        entry.has_ai = !library->script_file(def.number, "behavior.lua").empty();
        entries_.push_back(std::move(entry));
    }
    return entries_;
}

const SpawnEntry* SpawnRoster::find(std::string_view name) {
    for (const SpawnEntry& entry : entries()) {
        if (commands::same_word(name, entry.name) || commands::same_word(name, entry.id)) return &entry;
    }
    return nullptr;
}

int SpawnRoster::count() { return static_cast<int>(entries().size()); }

const char* SpawnRoster::name(int index) {
    const auto& list = entries();
    return in_list(list, index) ? list[static_cast<std::size_t>(index)].name.c_str() : "";
}

bool SpawnRoster::spawnable(int index) {
    const auto& list = entries();
    return in_list(list, index) && list[static_cast<std::size_t>(index)].spawnable;
}

const char* SpawnRoster::category(int index) {
    const auto& list = entries();
    return in_list(list, index) ? list[static_cast<std::size_t>(index)].category.c_str() : "";
}

const char* SpawnRoster::ai_run(int index) {
    const auto& list = entries();
    if (!in_list(list, index) || !list[static_cast<std::size_t>(index)].spawnable) return Pc_Spawn_RunName(0);
    return Pc_Spawn_RunName(Pc_Chara_AiResolve(list[static_cast<std::size_t>(index)].chara, menu_ai_));
}

int SpawnRoster::category_count() const {
    const sh::chara::CharaLibrary* library = Pc_CharaData_Library();
    return library != nullptr ? static_cast<int>(library->spawn_categories().size()) : 0;
}

const char* SpawnRoster::category_name(int index) {
    const sh::chara::CharaLibrary* library = Pc_CharaData_Library();
    if (library == nullptr) return "";
    const std::vector<std::string> names = library->spawn_categories();
    category_name_ = index >= 0 && index < static_cast<int>(names.size()) ? names[static_cast<std::size_t>(index)]
                                                                            : std::string();
    return category_name_.c_str();
}

bool SpawnRoster::resident(const SpawnEntry& entry) const {
    // Drawing needs the model's file streamed in; the bones need the animation slot.
    return gs::chara_model_loaded(entry.chara) && gs::chara_anim_slot(entry.chara) >= 0;
}

bool SpawnRoster::make_resident(const SpawnEntry& entry) {
    if (!entry.spawnable) return false;
    if (resident(entry)) return true;
    return Pc_CharaPool_Ensure(entry.chara) != 0 && resident(entry);
}

int SpawnRoster::spawn(const SpawnEntry& entry, int state, int metres, int turn_q12, int ai) {
    const sh::entities::Vec3 harry = gs::chara_position(0);
    const int heading = (gs::chara_rotation(0).y + turn_q12) & 0xFFF;
    const std::int64_t reach = static_cast<std::int64_t>(metres) * 4096;
    const std::int32_t x = harry.x + static_cast<std::int32_t>((reach * gs::sin_q12(heading)) >> 12);
    const std::int32_t z = harry.z + static_cast<std::int32_t>((reach * gs::cos_q12(heading)) >> 12);
    return Pc_Spawn_CharacterAi(entry.chara, state, x, z, (heading + 0x800) & 0xFFF, ai >= 0 ? ai : menu_ai_);
}

void SpawnRoster::spawn_at(int index, int metres, int state) {
    const auto& list = entries();
    if (!in_list(list, index)) return;
    const SpawnEntry entry = list[static_cast<std::size_t>(index)];
    if (!make_resident(entry)) return;
    spawn(entry, state >= 0 ? state : entry.state, std::clamp(metres, 1, kMenuReachMetres), 0);
}

void SpawnRoster::preview_cancel() {
    if (preview_slot_ > 0 && gs::chara_id(preview_slot_) == preview_chara_) {
        sh::input::object_pick().set_frozen(preview_slot_ - 1, false);
        gs::despawn_npc(preview_slot_);
    }
    preview_slot_ = -1;
}

bool SpawnRoster::preview_show(int index, int metres, int state) {
    const auto& list = entries();
    preview_cancel();
    if (!in_list(list, index) || gs::game_state() != gs::GameState::InGame) return false;
    const SpawnEntry entry = list[static_cast<std::size_t>(index)];
    if (!make_resident(entry)) return false;
    const int slot = spawn(entry, state >= 0 ? state : entry.state, std::clamp(metres, 1, kMenuReachMetres), 0);
    if (slot < 0) return false;
    preview_slot_ = slot;
    preview_chara_ = entry.chara;
    preview_ticks_ = 0;
    return true;
}

bool SpawnRoster::preview_keep() {
    if (preview_slot_ <= 0) return false;
    sh::input::object_pick().set_frozen(preview_slot_ - 1, false);
    preview_slot_ = -1;
    return true;
}

void SpawnRoster::preview_tick() {
    if (preview_slot_ <= 0) return;
    if (gs::chara_id(preview_slot_) != preview_chara_) {  // killed, or the slot went to another character
        preview_slot_ = -1;
        return;
    }
    if (preview_ticks_ < kPreviewSettleTicks && ++preview_ticks_ == kPreviewSettleTicks) {
        sh::input::object_pick().set_frozen(preview_slot_ - 1, true);
    }
}

const char* spawn_name_of(int chara_id) {
    static std::map<int, std::string> names;  // the picker keeps the pointer
    const sh::chara::CharacterDef* def = sh::chara::type_def(chara_id);
    if (def == nullptr) return nullptr;
    std::string& name = names[chara_id];
    if (name.empty()) name = commands::upper(def->folder);
    return name.c_str();
}

}  // namespace sh::debug
