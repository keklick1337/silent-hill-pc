/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The natives behind the DEBUG OPTIONS menu (gamedata/menus/debug.lua). */
#ifndef PC_DEBUG_BINDS_H
#define PC_DEBUG_BINDS_H

#ifdef __cplusplus
extern "C" {
#endif

/* Registers every `debug.*` native into sh::menu::registry(). Call once at startup, before
 * the menu Lua files are loaded. Calling it again re-registers the same names. */
void Pc_DebugBinds_Register(void);

#ifdef __cplusplus
}

/* The names Pc_DebugBinds_Register registers, for the tests: every `native = "..."` of
 * debug.lua must be one of them. */
namespace sh::debug_binds {
inline constexpr const char* const kNativeNames[] = {
    /* Cheats */
    "debug.god_mode", "debug.infinite_ammo", "debug.noclip", "debug.enemies_ignore", "debug.heal_full",
    "debug.heal_items", "debug.kill_nearby",
    /* Items */
    "debug.give_item", "debug.give_quantity", "debug.give",
    /* Maps */
    "debug.goto_map", "debug.start_map", "debug.restart_map",
    /* Spawn */
    "debug.spawn_category", "debug.spawn_character", "debug.spawn_distance", "debug.spawn_state", "debug.spawn_ai", "debug.spawn_preview_on",
    "debug.spawn",
    /* Camera */
    "debug.free_camera", "debug.log_camera",
    /* Tools */
    "debug.flag", "debug.flag_value", "debug.fps_display", "debug.profiler", "debug.perf_record", "debug.collision_view",
    "debug.log_position",
};
}  // namespace sh::debug_binds
#endif

#endif /* PC_DEBUG_BINDS_H */
