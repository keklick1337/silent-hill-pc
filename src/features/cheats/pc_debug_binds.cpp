// SPDX-License-Identifier: GPL-3.0-or-later
// The natives behind the DEBUG OPTIONS menu (gamedata/menus/debug.lua). Every one drives the same
// globals and helpers as the console commands and the debug keys, so a cheat reads the same from
// the menu, the console and the keys.

#include "engine/ids/game_numbers.h"
#include "engine/ids/ids.h"
#include "engine/inventory/item_catalog.h"
#include "features/cheats/pc_debug_binds.h"

#include "engine/config/pc_config.h"
#include "engine/chara/chara_library.h"
#include "engine/chara/runtime/chara_data.h"
#include "engine/game_state/game_state.h"
#include "engine/log/sh_log.h"
#include "engine/menu/menu_model.h"
#include "engine/runtime/game_hooks.h"
#include "features/cheats/pc_debug_binds_util.h"
#include "features/dump/perf_recorder.h"
#include "ui/menu/pc_spawn_turntable.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include "render/gpu/render_state.h"
#include "engine/debug/collision_view.h"
#include "engine/debug/commands/spawn_roster.h"

namespace gs = sh::game_state;
namespace menu = sh::menu;

/* engine/app/loop/main_loop.cpp: the flags the console and the debug keys drive. */
extern int g_PcGodMode;
extern int g_DebugNoWallCollision;
extern int g_DebugNoTarget;
extern int g_PcInfiniteAmmo;
extern int g_DebugCamEnabled;
extern "C" void Pc_FreeCam_Set(int on);
extern "C" void Pc_CamSnapDump(void);

/* engine/debug/commands/spawn_roster.h: the characters the console SPAWN and this menu list. */
extern "C" int Pc_Spawn_AiFind(const char* name);
extern "C" const char* Pc_Spawn_AiName(int ai);
/* A New Game on a map, from play or the title (0 = refused here). */
extern "C" int Pc_MapWarp_Request(int mapId);

namespace {

using menu::Value;

bool in_play() { return gs::game_state() >= gs::GameState::InGame; }

void echo_toggle(const char* name, bool on) { SH_DBG_ECHO("[DEBUG] %s: %s", name, on ? "ON" : "OFF"); }

/* A toggle bound to an int flag shared with the console and the keys. */
void bind_flag(const char* native, int* flag) {
    menu::registry().native_value(native, {[flag] { return Value{*flag != 0}; },
                                           [flag, native](const Value& v) {
                                               *flag = menu::as_bool(v) ? 1 : 0;
                                               echo_toggle(native, *flag != 0);
                                           }});
}

void bind_action(const char* native, std::function<void()> fn, bool play_only) {
    menu::registry().native_action(native, std::move(fn));
    if (play_only) menu::registry().native_enabled(native, in_play);
}

void close_menu() {
    if (menu::host().close) menu::host().close();
}

/* ---- cheats ----------------------------------------------------------- */

void heal_full() {
    gs::set_chara_health(0, 100 * 4096);
    SH_DBG_ECHO("[DEBUG] Harry healed");
}

void heal_items() {
    // Three of every healing item (items/<Name>.json "inventory.heal"), in item number order.
    const sh::items::ItemLibrary* lib = sh::inventory::item_catalog().library();
    std::vector<int> healing;
    if (lib != nullptr) {
        for (const sh::items::ItemDef& item : lib->items()) {
            if (!item.inventory.heal.empty() && item.number != 0) healing.push_back(item.number);
        }
    }
    std::sort(healing.begin(), healing.end());
    for (const int item : healing) {
        // Like every grant without a pickup: mods see it (item.given) and may change or cancel it.
        const int given = sh::hooks::game_item_given(item, 3, "debug");
        if (given > 0) gs::inventory_add(item, given);
    }
    SH_DBG_ECHO("[DEBUG] Added 3 of each healing item (%d kinds)", static_cast<int>(healing.size()));
}

/* Same routing as the KILLALL console command: a huge hit sends every nearby enemy through its
 * own death path. */
void kill_nearby() {
    const sh::entities::Vec3 harry = gs::chara_position(0);
    int killed = 0;
    for (int i = 0; i < gs::kNpcSlots; i++) {
        const int slot = i + 1;
        const int id = gs::chara_id(slot);
        if (id == gs::chara::kNone || sh::chara::is_player_type(id) || gs::chara_health(slot) <= 0) continue;
        const sh::entities::Vec3 p = gs::chara_position(slot);
        if (std::abs(p.x - harry.x) > 50 * 4096 || std::abs(p.z - harry.z) > 50 * 4096) continue;
        gs::chara_take_damage(slot, 99999 * 4096);
        killed++;
    }
    SH_DBG_ECHO("[DEBUG] Killed %d nearby enemies", killed);
}

/* ---- items ------------------------------------------------------------ */

std::string s_giveItem;  // full id of the picked item; empty: the first of the list
std::int64_t s_giveQuantity = 1;

std::vector<menu::Choice> give_list() {
    const sh::items::ItemLibrary* library = sh::inventory::item_catalog().library();
    return library ? sh::debug_binds::give_item_choices(library->items()) : std::vector<menu::Choice>{};
}

void give() {
    const sh::items::ItemLibrary* library = sh::inventory::item_catalog().library();
    if (!library) return;
    if (s_giveItem.empty()) {
        const auto list = give_list();
        if (list.empty()) return;
        s_giveItem = list.front().value;
    }
    const sh::items::ItemDef* item = library->find_item(s_giveItem);
    if (!item || item->number == 0) {
        SH_DBG_ECHO("[DEBUG] Give: %s cannot be given", s_giveItem.c_str());
        return;
    }
    const int given = sh::hooks::game_item_given(item->number, static_cast<int>(s_giveQuantity), "debug");
    if (given <= 0) {
        SH_DBG_ECHO("[DEBUG] Give: %s cancelled by item.given", s_giveItem.c_str());
        return;
    }
    gs::inventory_add(item->number, given);
    // As the console's give: without the unlock the inventory's functional test keeps a given blaster from aiming.
    if (sh::ids::local_name(item->id) == "HyperBlaster") gs::unlock_hyperblaster();
    SH_DBG_ECHO("[DEBUG] Gave %s x%d", s_giveItem.c_str(), given);
}

/* ---- maps ------------------------------------------------------------- */

std::vector<menu::Choice> map_list() {
    std::vector<menu::Choice> out;
    for (int i = 0; i < gs::map_count(); ++i) {
        const char* name = gs::map_name(i);
        const char* description = gs::map_description(i);
        out.push_back({name, std::string(name) + "  " + (description ? description : "")});
    }
    return out;
}

/* Picking a map from the list warps there (a New Game on that map, from play or the title). */
void warp_to(const std::string& name) {
    const int id = gs::map_find(name.c_str());
    if (id < 0) return;
    if (!Pc_MapWarp_Request(id)) {
        SH_DBG_ECHO("[DEBUG] Go to map: not now (only in play or on the title)");
        return;
    }
    SH_DBG_ECHO("[DEBUG] Going to %s - %s", gs::map_name(id), gs::map_description(id));
    close_menu();
}

void restart_map() {
    if (!Pc_MapWarp_Request(gs::current_map())) SH_DBG_ECHO("[DEBUG] Restart map: not now");
    else close_menu();
}

/* The map a New Game starts on: it writes config.cfg straight away, the value only matters on the
 * next New Game. */
void set_start_map(const std::string& name) {
    const int id = gs::map_find(name.c_str());
    if (id < 0) return;
    std::strncpy(g_PcConfig.mapName, name.c_str(), sizeof(g_PcConfig.mapName) - 1);
    g_PcConfig.mapName[sizeof(g_PcConfig.mapName) - 1] = '\0';
    PcConfig_SaveMapName(g_PcConfig.mapName);
    SH_DBG_ECHO("[DEBUG] Starting map: %s - %s", name.c_str(), gs::map_description(id));
}

/* ---- spawn ------------------------------------------------------------ */

int s_spawnIdx = 0;
std::string s_spawnCat;          // the category the character dropdown lists; empty: that of the picked one
std::int64_t s_spawnDist = 4;    // metres in front of Harry
std::int64_t s_spawnState = -1;  // initial behaviour state; -1: the character's default
bool s_spawnPreview = true;      // the picked character turns in a panel beside the menu

/* The category in force: the picked one, else the one of the current character. */
std::string spawn_category() {
    if (!s_spawnCat.empty()) return s_spawnCat;
    return sh::debug::spawn_roster().category(s_spawnIdx);
}

std::vector<menu::Choice> spawn_category_list() {
    std::vector<menu::Choice> out;
    for (int k = 0; k < sh::debug::spawn_roster().category_count(); ++k) {
        const std::string c = sh::debug::spawn_roster().category_name(k);
        out.push_back({c, sh::debug_binds::spawn_category_label(c)});
    }
    return out;
}

std::vector<menu::Choice> spawn_character_list() {
    std::vector<menu::Choice> out;
    const std::string cat = spawn_category();
    for (int i = 0; i < sh::debug::spawn_roster().count(); ++i) {
        if (cat != sh::debug::spawn_roster().category(i)) continue;
        // Not spawnable (Padlock, the placeholder Dummy* types): the console's `spawn list` says why.
        if (!sh::debug::spawn_roster().spawnable(i)) continue;
        out.push_back({std::to_string(i), sh::debug_binds::spawn_character_label(sh::debug::spawn_roster().name(i), sh::debug::spawn_roster().ai_run(i))});
    }
    return out;
}

/* The turntable's pick: the chosen character while the Preview row is on. */
const char* spawn_turntable_pick() {
    if (!s_spawnPreview || s_spawnIdx < 0 || s_spawnIdx >= sh::debug::spawn_roster().count()) return nullptr;
    return sh::debug::spawn_roster().name(s_spawnIdx);
}

/* Only this action puts a character in the world; the preview is the turntable beside the menu. */
void spawn() {
    if (s_spawnIdx < 0 || s_spawnIdx >= sh::debug::spawn_roster().count()) return;
    if (!sh::debug::spawn_roster().spawnable(s_spawnIdx)) {
        SH_DBG_ECHO("[DEBUG] %s cannot be spawned (see 'spawn list')", sh::debug::spawn_roster().name(s_spawnIdx));
        return;
    }
    sh::debug::spawn_roster().spawn_at(s_spawnIdx, static_cast<int>(s_spawnDist), static_cast<int>(s_spawnState));
    SH_DBG_ECHO("[DEBUG] Spawned %s", sh::debug::spawn_roster().name(s_spawnIdx));
}

/* ---- tools ------------------------------------------------------------ */

std::string s_flagName;  // full id of the picked event flag; empty: the first of the list

std::vector<menu::Choice> flag_list() {
    std::vector<std::string> names;
    for (int bit = 1; bit < sh::debug_binds::kFlagBitCount; ++bit) {
        const char* name = sh::ids::flag_name(bit);
        if (name) names.emplace_back(name);
    }
    return sh::debug_binds::flag_choices(names);
}

int flag_bit() {
    if (s_flagName.empty()) {
        const auto list = flag_list();
        if (list.empty()) return -1;
        s_flagName = list.front().value;
    }
    return sh::ids::flag_number(s_flagName.c_str());
}

void log_position() {
    const sh::entities::Vec3 p = gs::chara_position(0);
    SH_DBG_ECHO("HARRY POSITION LOGGED mapId=%d roomIdx=%d pos=(%ld,%ld,%ld) yaw=%d", gs::save_map_index(),
                gs::save_room(), static_cast<long>(p.x), static_cast<long>(p.y), static_cast<long>(p.z),
                static_cast<int>(gs::chara_rotation(0).y));
}

}  // namespace

extern "C" void Pc_DebugBinds_Register(void) {
    menu::Registry& reg = menu::registry();

    /* Cheats */
    bind_flag("debug.god_mode", &g_PcGodMode);
    bind_flag("debug.infinite_ammo", &g_PcInfiniteAmmo);
    bind_flag("debug.noclip", &g_DebugNoWallCollision);
    bind_flag("debug.enemies_ignore", &g_DebugNoTarget);
    bind_action("debug.heal_full", heal_full, true);
    bind_action("debug.heal_items", heal_items, true);
    bind_action("debug.kill_nearby", kill_nearby, true);

    /* Items: the dropdown only picks, Give adds. */
    reg.native_list("debug.give_item", give_list);
    reg.native_value("debug.give_item", {[] {
                                             if (s_giveItem.empty()) {
                                                 const auto list = give_list();
                                                 if (!list.empty()) s_giveItem = list.front().value;
                                             }
                                             return Value{s_giveItem};
                                         },
                                         [](const Value& v) { s_giveItem = menu::as_string(v); }});
    reg.native_value("debug.give_quantity", {[] { return Value{s_giveQuantity}; }, [](const Value& v) {
                                                 s_giveQuantity = std::clamp<std::int64_t>(menu::as_int(v), 1, 99);
                                             }});
    bind_action("debug.give", give, true);
    reg.native_enabled("debug.give_item", in_play);
    reg.native_enabled("debug.give_quantity", in_play);

    /* Maps: the pick is the action. */
    reg.native_list("debug.goto_map", map_list);
    reg.native_value("debug.goto_map", {[] {
                                            const int id = in_play() ? gs::current_map() : gs::map_find(g_PcConfig.mapName);
                                            const char* name = id >= 0 ? gs::map_name(id) : nullptr;
                                            return Value{std::string(name ? name : "")};
                                        },
                                        [](const Value& v) { warp_to(menu::as_string(v)); }});
    reg.native_list("debug.start_map", map_list);
    reg.native_value("debug.start_map", {[] { return Value{std::string(g_PcConfig.mapName)}; },
                                         [](const Value& v) { set_start_map(menu::as_string(v)); }});
    bind_action("debug.restart_map", restart_map, true);

    /* Spawn */
    reg.native_list("debug.spawn_category", spawn_category_list);
    reg.native_value("debug.spawn_category", {[] { return Value{spawn_category()}; }, [](const Value& v) {
                                                  s_spawnCat = menu::as_string(v);
                                                  /* The first character of the category becomes the pick. */
                                                  const std::vector<menu::Choice> in = spawn_character_list();
                                                  if (!in.empty()) s_spawnIdx = std::atoi(in.front().value.c_str());
                                              }});
    reg.native_list("debug.spawn_character", spawn_character_list);
    /* With the Preview row on, browsing the list turns the character under the cursor; "" is the list
     * closing, back to the pick. */
    reg.native_highlight("debug.spawn_character", [](const std::string& v) {
        if (v.empty() || !s_spawnPreview) Pc_SpawnTurntable_Hide();
        else Pc_SpawnTurntable_Show(sh::debug::spawn_roster().name(std::atoi(v.c_str())));
    });
    Pc_SpawnTurntable_SetPicker(spawn_turntable_pick);
    reg.native_value("debug.spawn_character", {[] { return Value{std::to_string(s_spawnIdx)}; }, [](const Value& v) {
                                                   s_spawnIdx = std::atoi(menu::as_string(v).c_str());
                                               }});
    reg.native_value("debug.spawn_distance", {[] { return Value{s_spawnDist}; }, [](const Value& v) {
                                                  s_spawnDist = std::clamp<std::int64_t>(menu::as_int(v), 1, 20);
                                              }});
    reg.native_value("debug.spawn_state", {[] { return Value{s_spawnState}; }, [](const Value& v) {
                                               s_spawnState = std::clamp<std::int64_t>(menu::as_int(v), -1, 40);
                                           }});
    reg.native_list("debug.spawn_ai", sh::debug_binds::spawn_ai_choices);
    reg.native_value("debug.spawn_ai", {[] { return Value{std::string(Pc_Spawn_AiName(sh::debug::spawn_roster().menu_ai()))}; },
                                        [](const Value& v) {
                                            sh::debug::spawn_roster().set_menu_ai(Pc_Spawn_AiFind(menu::as_string(v).c_str()));
                                        }});
    reg.native_value("debug.spawn_preview_on", {[] { return Value{s_spawnPreview}; }, [](const Value& v) {
                                                    s_spawnPreview = menu::as_bool(v);
                                                    if (!s_spawnPreview) Pc_SpawnTurntable_Hide();
                                                }});
    bind_action("debug.spawn", spawn, true);
    for (const char* n : {"debug.spawn_category", "debug.spawn_character", "debug.spawn_distance", "debug.spawn_state", "debug.spawn_ai", "debug.spawn_preview_on"})
        reg.native_enabled(n, in_play);

    /* Camera */
    reg.native_value("debug.free_camera", {[] { return Value{g_DebugCamEnabled != 0}; }, [](const Value& v) {
                                               Pc_FreeCam_Set(menu::as_bool(v) ? 1 : 0);
                                               echo_toggle("Free camera", g_DebugCamEnabled != 0);
                                           }});
    reg.native_enabled("debug.free_camera", in_play);
    bind_action("debug.log_camera", Pc_CamSnapDump, true);

    /* Tools */
    reg.native_list("debug.flag", flag_list);
    reg.native_value("debug.flag", {[] {
                                        flag_bit();  // fills the default pick
                                        return Value{s_flagName};
                                    },
                                    [](const Value& v) { s_flagName = menu::as_string(v); }});
    reg.native_value("debug.flag_value", {[] {
                                              const int bit = flag_bit();
                                              return Value{bit > 0 && gs::event_flag(bit)};
                                          },
                                          [](const Value& v) {
                                              const int bit = flag_bit();
                                              if (bit <= 0) return;
                                              gs::set_event_flag(bit, menu::as_bool(v));
                                              SH_DBG_ECHO("[DEBUG] Flag %s: %s", s_flagName.c_str(), menu::as_bool(v) ? "SET" : "CLEARED");
                                          }});
    reg.native_enabled("debug.flag", in_play);
    reg.native_enabled("debug.flag_value", in_play);

    reg.native_list("debug.fps_display", [] {
        return std::vector<menu::Choice>{{"0", "ui.debug.fps_off"},
                                         {"1", "ui.debug.fps_top_left"},
                                         {"2", "ui.debug.fps_top_right"},
                                         {"3", "ui.debug.fps_bottom_left"},
                                         {"4", "ui.debug.fps_bottom_right"}};
    });
    reg.native_value("debug.fps_display", {[] { return Value{std::to_string(g_PcConfig.fpsDisplay)}; }, [](const Value& v) {
                                               const int pos = std::clamp(std::atoi(menu::as_string(v).c_str()), 0, 4);
                                               g_PcConfig.fpsDisplay = pos;
                                               PcConfig_SaveKeyValue("fps_display", std::to_string(pos).c_str());
                                           }});
    reg.native_value("debug.profiler", {[] { return Value{g_PcConfig.fpsProfiler != 0}; }, [](const Value& v) {
                                            g_PcConfig.fpsProfiler = menu::as_bool(v) ? 1 : 0;
                                            PcConfig_SaveKeyValue("fps_profiler", g_PcConfig.fpsProfiler ? "1" : "0");
                                        }});
    reg.native_value("debug.perf_record",
                     {[] { return Value{sh::dump::perf_record_active()}; },
                      [](const Value& v) {
                          const bool on = menu::as_bool(v);
                          if (on == sh::dump::perf_record_active()) return;
                          if (on) {
                              auto r = sh::dump::perf_record_start({});
                              SH_DBG_ECHO("%s", r ? ("perfrec: recording to " + r.value()).c_str() : r.error().c_str());
                          } else if (auto r = sh::dump::perf_record_stop(); r) {
                              SH_DBG_ECHO("%s", r.value().c_str());
                          }
                      }});
    reg.native_value("debug.collision_view", {[] { return Value{sh::debug::collision_view().enabled()}; },
                                              [](const Value& v) {
                                                  sh::debug::collision_view().set_enabled(menu::as_bool(v));
                                                  echo_toggle("debug.collision_view",
                                                              sh::debug::collision_view().enabled());
                                              }});
    bind_action("debug.log_position", log_position, true);

    /* A name in kNativeNames that was not registered above is a bug in this file. */
    for (const char* name : sh::debug_binds::kNativeNames)
        if (!reg.has_native(name)) SH_DBG_ECHO("[DEBUG] native %s is not registered", name);
}
