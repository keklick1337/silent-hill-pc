// SPDX-License-Identifier: GPL-3.0-or-later
//
// The game's boot steps over the session: a new save record, the player's reset before a
// map, and the map load itself (the map-entry gates of sh::gameflow, the overlay swap, the language and mod text
// patches, the player's map animations and held item, the script hooks). The numbers are
// gamedata/config/load_boot.json; the record writes go to the session's save record by name.

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

#include "engine/loader/map_list.h"
#include "engine/app/game_view.h"
#include "engine/app/gameflow/game_flow.h"
#include "engine/entities/game/remains_host.h"
#include "engine/app/loader/game_files.h"
#include "engine/app/pc_paths.h"
#include "engine/chara/runtime/chara_models.h"
#include "engine/config/pc_config.h"
#include "engine/game_state/save_record.h"
#include "engine/game_state/session.h"
#include "engine/gameflow/flow_data.h"
#include "engine/gameflow/load_boot.h"
#include "engine/ids/game_numbers.h"
#include "engine/inventory/item_catalog.h"
#include "engine/memory/work_buffers.h"
#include "engine/log/sh_log.h"
#include "engine/player/player.h"
#include "engine/runtime/game_hooks.h"
#include "engine/runtime/pickup_runtime.h"
#include "engine/script/sh_script.h"
#include "i18n/localization.h"
#include "engine/gameflow/anim_slots.h"
#include "engine/game_state/game_state_ui.h"
#include "engine/loader/queue/disc_files.h"
#include "engine/mods/data_reload.h"

extern "C" {
const char* PcPort_GetGameDataPath(void);

// The game's model, animation and held item systems.
void WorldGfx_MapInit(void);
void Item_HeldItemModelFree(void);
void Anim_BoneInit(void* anmHdr, void* boneCoords);
void Game_TurnFlashlightOff(void);
void Game_TurnFlashlightOn(void);
std::int32_t Inventory_ItemsSort(void* save);
void Map_EffectTexturesLoad(std::int32_t mapIdx);
void GameFs_PlayerMapAnimLoad(std::int32_t mapIdx);
std::int32_t WorldGfx_PlayerPrevHeldItem(void* combat);
void Gfx_PlayerHeldItemAttach(std::int32_t weaponAttack);
}

namespace {

namespace gf = sh::gameflow;
using sh::game::save_record;
using sh::game::session;

// The bytes the player's animation slot holds.
constexpr std::int32_t kPlayerAnimBytes = 0x2E630;
constexpr std::uint32_t kToggleRadioOn = 1u << 0;
constexpr std::uint32_t kToggleFlashlightOff = 1u << 1;
constexpr int kEmptyItem = 0xFF;
constexpr std::int8_t kNoMusicTrack = 0;
constexpr std::uint32_t kRestoredGame = sh::game::process_flag::kNewGame | sh::game::process_flag::kLoadSave |
                                        sh::game::process_flag::kContinue | sh::game::process_flag::kBootDemo;

void* player_anim_buffer() { return sh::memory::work_buffers().data(sh::memory::WorkBuffer::PlayerBaseAnim); }

// The boot step's numbers (gamedata/config/load_boot.json, built-ins when missing).
struct LoadBootParams {
    gf::SaveInitParams save_init;
    gf::MapEntryParams map_entry;

    static const LoadBootParams& get() {
        static LoadBootParams params;
        static std::uint32_t params_epoch = sh::mods::kNeverRead;
        if (sh::mods::data_stale(params_epoch)) {
            params = [] {
                LoadBootParams p;
                p.save_init = gf::SaveInitParams::builtin();
                p.map_entry = gf::MapEntryParams::builtin();
                char mods[1024];
                PcPaths_ExeFile(mods, sizeof(mods), "mods");
                std::vector<std::string> warnings;
                std::string source;
                const sh::Result<std::string> text =
                    gf::read_config_file(PcPort_GetGameDataPath(), mods, "load_boot.json", &source, warnings);
                if (text) {
                    if (const sh::Result<gf::SaveInitParams> s = gf::SaveInitParams::parse(text.value())) {
                        p.save_init = s.value();
                    } else {
                        SH_ERR("[BOOT] config/load_boot.json: %s (built-in numbers)", s.error().c_str());
                    }
                    if (const sh::Result<gf::MapEntryParams> m = gf::MapEntryParams::parse(text.value())) {
                        p.map_entry = m.value();
                    } else {
                        SH_ERR("[BOOT] config/load_boot.json: %s (built-in numbers)", m.error().c_str());
                    }
                }
                return p;
            }();
        }
        return params;
    }
};

// True only when every slot holds the empty marker: the slot count carries no occupancy (a reset leaves it at 8
// with all slots empty, the player info clamps it to 8..40 whatever is held).
bool inventory_empty() {
    for (const sh::game::SaveItem& item : save_record().items) {
        if (item.item != kEmptyItem) {
            return false;
        }
    }
    return true;
}

class SaveInitHost final : public gf::SaveInitHost {
public:
    void savegame_clear() override { save_record() = sh::game::SaveRecord{}; }
    void map_index_set(std::int32_t id) override { save_record().map = id; }
    void difficulty_set(std::int32_t d) override { save_record().set_difficulty(d); }
    void paper_map_set(std::int32_t page) override { save_record().paper_map = page; }
    void enemy_state_set(std::int32_t index, std::int32_t value) override { save_record().enemy_states[index] = value; }
    void player_reset() override { sh::game::player().reset_save(); }
};

class MapEntryHost final : public gf::MapEntryHost {
public:
    int force_enemy_spawn() const override { return g_PcConfig.forceEnemySpawn != 0; }
    int inventory_empty() const override { return ::inventory_empty() ? 1 : 0; }
    const char* map_name(std::int32_t idx) const override { return sh::loader::map_name(idx); }

    std::int32_t item_number(std::string_view name) const override {
        return name.empty() ? -1 : sh::ids::item_number(std::string(name).c_str());
    }
    std::int32_t toggle_number(std::string_view name) const override {
        if (name == "radio_on") return kToggleRadioOn;
        if (name == "flashlight_off") return kToggleFlashlightOff;
        return -1;
    }
    void item_slot_set(std::int32_t slot, std::int32_t item, std::int32_t count) override {
        save_record().items[slot].item = item;
        save_record().items[slot].count = count;
    }
    void equipped_set(std::int32_t item) override { save_record().equipped_weapon = item; }
    void toggle_flag_set(std::int32_t flag) override { save_record().item_toggles |= static_cast<std::uint32_t>(flag); }
    void toggle_flag_clear(std::int32_t flag) override {
        save_record().item_toggles &= ~static_cast<std::uint32_t>(flag);
    }
    // Sorts, merges stacks, derives the weapon slot and the total ammo, and gives the count with the game's floor
    // of 8: nothing on a transition path repairs a lower count, so the next pickup would land on a held item.
    std::int32_t inventory_sort() override {
        save_record().slot_count = static_cast<std::uint8_t>(Inventory_ItemsSort(&save_record()));
        return save_record().slot_count;
    }
    // A weapon's tap attack (packed: weapon id + 10 x input kind, the tap is kind 0): the weapon the data carries as
    // the item.
    std::int32_t weapon_attack(std::int32_t item) const override {
        return sh::inventory::item_catalog().held_number(item);
    }
    void weapon_attack_set(std::int32_t attack) override {
        session().system.combat.weapon_attack = static_cast<decltype(session().system.combat.weapon_attack)>(attack);
    }
    std::int32_t weapon_inventory_index() const override { return session().system.combat.weapon_slot; }
    std::int32_t item_count_at_slot(std::int32_t slot) const override { return save_record().items[slot].count; }
    void current_weapon_ammo_set(std::int32_t count) override {
        session().system.combat.ammo_loaded = static_cast<decltype(session().system.combat.ammo_loaded)>(count);
    }
};

}  // namespace

void sh::app::savegame_initialize(int map, int difficulty) {
    sh::entities::game::remains::session_reset();
    const std::int8_t overlayId = static_cast<std::int8_t>(map);
    SaveInitHost host;
    gf::savegame_initialize(host, overlayId, difficulty, LoadBootParams::get().save_init);
}

void sh::app::view::player_init() {
    WorldGfx_MapInit();
    sh::chara::chara_models().free_all();
    Item_HeldItemModelFree();
    Anim_BoneInit(player_anim_buffer(), session().system.characters.player_bones);
    sh::chara::chara_models().process_player_load();

    session().system.process.player_ready_mark = -1;

    if ((save_record().item_toggles & kToggleFlashlightOff) != 0) {
        Game_TurnFlashlightOff();
    } else {
        Game_TurnFlashlightOn();
    }

    sh::gameflow::anim_slot_table().slots[0].active_size = kPlayerAnimBytes;
    sh::gameflow::anim_slot_table().slots[0].alloc_size = kPlayerAnimBytes;
    sh::game::player().init_from_save();
}

void sh::app::view::map_load(int mapIdx) {
    // Runtime pickups of mods belong to one loaded map: the new one starts with none, before any script runs.
    {
        const char* name = sh::loader::map_name(mapIdx);
        sh::world::runtime_pickups_begin_map(name != nullptr ? name : "");
    }

    // The journal is flushed line by line on this path: a crash inside the map swap must not lose its lead-up.
    std::fflush(g_ShDebugLog);
    SH_DBG("[AUTO-EQUIP-CHECK] mapIdx=%d processFlags=0x%x inventorySlotCount=%d equippedWeapon=%d invEmpty=%d",
           mapIdx, (unsigned)session().system.process.process_flags, (int)save_record().slot_count,
           (int)save_record().equipped_weapon, inventory_empty() ? 1 : 0);
    std::fflush(g_ShDebugLog);

    // force_enemy_spawn (gameplay change, off by default) and the starting loadout of a New Game booted straight
    // onto a map.
    MapEntryHost entry;
    const gf::MapEntryParams& params = LoadBootParams::get().map_entry;
    gf::map_entry_spawn_gate(entry, mapIdx, params);
    gf::map_entry_start_loadout(entry, mapIdx, params);

    sh::loader::load_map(mapIdx);
    // The new header is live at once (the console swapped it only when the overlay read landed): the loaded track
    // is cleared so the new map's music logic does not replay the previous area's track for a frame before the
    // music init loads the right one.
    session().game.music_track = kNoMusicTrack;
    std::fflush(g_ShDebugLog);
    // The overlay read keeps the file queue's state as the game expects it.
    sh::loader::queue_read(sh::loader::disc_files().find_in_run("VIN/MAP0_S00.BIN", mapIdx), sh::game_state::map_overlay_buffer());
    std::fflush(g_ShDebugLog);

    // The map's messages in the language in force (gamedata/text and the mods' text/), ready for the message box.
    sh::i18n::install_map_messages(mapIdx);

    Map_EffectTexturesLoad(mapIdx);
    std::fflush(g_ShDebugLog);
    GameFs_PlayerMapAnimLoad(mapIdx);
    std::fflush(g_ShDebugLog);

    // A demo or a save that starts with a weapon in hand: the weapon appears and its data is allocated.
    if ((session().system.process.process_flags & kRestoredGame) != 0) {
        std::fflush(g_ShDebugLog);
        WorldGfx_PlayerPrevHeldItem(&session().system.combat);
        std::fflush(g_ShDebugLog);
    }
    std::fflush(g_ShDebugLog);
    Gfx_PlayerHeldItemAttach(session().system.combat.weapon_attack);
    std::fflush(g_ShDebugLog);

    // The game script and the mod hooks see the map as final, in that order.
    ShScript_OnMapLoad(sh::loader::map_name(mapIdx));
    sh::hooks::game_map_load(sh::loader::map_name(mapIdx),
                             (session().system.process.process_flags & kRestoredGame) != 0);
}

