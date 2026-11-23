// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)

#include "engine/script/script_host.h"
#include "engine/script/api_registry.h"
#include "engine/script/bind_config.h"
#include "engine/script/bind_engine.h"
#include "engine/script/bind_flags.h"
#include "engine/script/bind_player.h"
#include "engine/script/bind_sound.h"
#include "engine/script/bind_world.h"
#include "engine/script/bind_console.h"
#include "engine/script/bind_timers.h"
#include "engine/script/bind_world_pickups.h"
#include "engine/script/caller_mod.h"

#include "engine/ids/game_numbers.h"
#include "engine/mods/mods.h"

// The vendored lua.h carries no extern "C" guard of its own, so wrap it here.
extern "C" {
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
}

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "engine/devtools/perf_monitor.h"

/* The original handlers pass true/false to integer parameters (e_ScreenFadeType,
 * reset/incSubStep flags), and the map scripts write them the same way; luaL_check/optinteger
 * reject booleans. Booleans read as 0/1 everywhere. */
static lua_Integer sh_checkinteger(lua_State* L, int arg) {
    return lua_isboolean(L, arg) ? (lua_toboolean(L, arg) ? 1 : 0) : luaL_checkinteger(L, arg);
}
/* Fade timesteps written as seconds (2.5) are Q12(2.5f); integers stay raw Q12. */
static int sh_q12_arg(lua_State* L, int arg) {
    if (lua_type(L, arg) == LUA_TNUMBER && !lua_isinteger(L, arg)) {
        volatile float seconds = static_cast<float>(lua_tonumber(L, arg));
        return static_cast<int>(seconds * 4096.0f);
    }
    return static_cast<int>(sh_checkinteger(L, arg));
}

namespace sh::script {

namespace {

// The map scripts pass the bools of the original calls as Lua booleans
// (sh.event_fade_cmd(0, false, ...)); luaL_check/optinteger reject them with an error
// that would turn the whole handler tick into a no-op. Every integer argument of sh.* takes
// a boolean as 0/1.
lua_Integer check_int_or_bool(lua_State* L, int arg) {
    if (lua_isboolean(L, arg)) {
        return lua_toboolean(L, arg);
    }
    return luaL_checkinteger(L, arg);
}

lua_Integer opt_int_or_bool(lua_State* L, int arg, lua_Integer def) {
    return lua_isnoneornil(L, arg) ? def : check_int_or_bool(L, arg);
}

}  // namespace

#define luaL_checkinteger check_int_or_bool
#define luaL_optinteger opt_int_or_bool

namespace {

// Registry key for the active call's instruction budget counter; an object address,
// not the hook function pointer (function pointers do not convert to const void*).
char kBudgetKey = 0;


Host* host_of(lua_State* L) {
    return static_cast<Host*>(lua_touserdata(L, lua_upvalueindex(1)));
}

int l_log(lua_State* L) {
    // sh.log(message) or sh.log(level, message): level 0 info, 1 warning.
    int nargs = lua_gettop(L);
    int level = 0;
    const char* message = nullptr;
    if (nargs >= 2 && lua_isinteger(L, 1)) {
        level = static_cast<int>(lua_tointeger(L, 1));
        message = lua_tostring(L, 2);
    } else if (nargs >= 1) {
        message = lua_tostring(L, -1);
    }
    if (message != nullptr) {
        host_of(L)->log(level, message);
    }
    return 0;
}

int l_api_version(lua_State* L) {
    lua_pushinteger(L, kApiVersion);
    return 1;
}

// ---- the sh.* game functions: thin, checked wrappers over GameApi ---------------------------------

// An argument that names a thing of the data, or the number old scripts pass. `find` is the
// game's lookup; an unknown name is an error of the call (a typo fails loudly, not silently). A short name from a
// mod's script means the mod's own thing first (caller_mod.h).
int name_arg(lua_State* L, int arg, int (*find)(const char*), const char* what) {
    if (lua_type(L, arg) != LUA_TSTRING) {
        return static_cast<int>(luaL_checkinteger(L, arg));
    }
    const char* name = lua_tostring(L, arg);
    const int number = find != nullptr ? find_scoped(L, name, find) : -1;
    if (number < 0) {
        return luaL_error(L, "unknown %s '%s'", what, name);
    }
    return number;
}

// "M1S03_ShotgunShells0", "mymod:door_opened" (gamedata/flags/*.json) or the save bit (0 = no flag).
int flag_arg(lua_State* L, int arg) { return name_arg(L, arg, host_of(L)->game_api().flag_number, "flag"); }

// "Cheryl", "mymod:Ghost" (the folder of gamedata/chara/) or the e_CharaId.
int chara_id_arg(lua_State* L, int arg) {
    return name_arg(L, arg, host_of(L)->game_api().chara_id_find, "character");
}

// A game file by its path on the disc ("TIM/LOCKER1.TIM") or the FILE_* number.
int file_arg(lua_State* L, int arg) { return name_arg(L, arg, host_of(L)->game_api().file_number, "game file"); }

// A paper map page by its name ("FogSchool1F", gamedata/paper_maps.json) or the e_PaperMapIdx.
int paper_map_arg(lua_State* L, int arg) {
    return name_arg(L, arg, host_of(L)->game_api().paper_map_number, "paper map");
}

// A cutscene state by its name ("pickup_crouch"), resolved against the character of the
// slot: slot 0 is the player (Harry's "states"), 1+ an NPC (its "controls"). The number is
// the old form. A number is also accepted for slots the game has no character in yet.
int state_arg(lua_State* L, int slot, int arg) {
    if (lua_type(L, arg) != LUA_TSTRING) {
        return static_cast<int>(luaL_checkinteger(L, arg));
    }
    Host* host = host_of(L);
    const auto slot_chara = host->game_api().chara_slot_chara_id_get;
    const auto state_number = host->game_api().chara_state_number;
    const char* name = lua_tostring(L, arg);
    if (slot_chara == nullptr || state_number == nullptr) {
        return luaL_error(L, "cannot resolve the state '%s': the game api is not bound", name);
    }
    // Slot 0 is the player: its state machine is Harry's, and the slot table only covers the NPCs
    // (slot 1 = the first NPC).
    const auto find_chara = host->game_api().chara_id_find;
    // "Character:state" names the state machine explicitly: a scene that drives whatever
    // occupies the slot (the original sets the number blindly) stays exact even when a test or
    // a mod puts another character there.
    if (const char* colon = std::strchr(name, ':'); colon != nullptr) {
        const std::string chara_name(name, static_cast<std::size_t>(colon - name));
        const int chara = find_chara != nullptr ? find_chara(chara_name.c_str()) : -1;
        const int number = chara >= 0 ? state_number(chara, colon + 1) : -1;
        if (number < 0) {
            return luaL_error(L, "no state '%s'", name);
        }
        return number;
    }
    const int chara = slot == 0 ? (find_chara != nullptr ? find_chara("Harry") : -1) : slot_chara(slot);
    const int number = state_number(chara, name);
    if (number < 0) {
        return luaL_error(L, "the character of slot %d has no state '%s'", slot, name);
    }
    return number;
}

int opt_flag_arg(lua_State* L, int arg, int fallback) {
    return lua_isnoneornil(L, arg) ? fallback : flag_arg(L, arg);
}

// A message argument: the slot number, or a text key resolved in the current map.
int msg_arg(lua_State* L, int arg) {
    if (lua_type(L, arg) != LUA_TSTRING) {
        return static_cast<int>(luaL_checkinteger(L, arg));
    }
    Host* host = host_of(L);
    const auto msg_index = host->game_api().msg_index;
    const char* key = lua_tostring(L, arg);
    const int idx = msg_index != nullptr ? msg_index(key) : -1;
    if (idx < 0) {
        return luaL_error(L, "no message '%s' in this map", key);
    }
    return idx;
}

// An item argument: its name ("KitchenKnife", "mymod:Crowbar"), or the number old scripts pass.
int item_arg(lua_State* L, int arg) {
    if (lua_type(L, arg) != LUA_TSTRING) {
        return static_cast<int>(luaL_checkinteger(L, arg));
    }
    const char* name = lua_tostring(L, arg);
    const auto find = host_of(L)->game_api().item_number;
    const int number = find != nullptr ? find_scoped(L, name, find) : -1;
    if (number < 0) {
        return luaL_error(L, "unknown item '%s'", name);
    }
    return number;
}

// A common pickup: the item's name ("HandgunBullets"), or the old pickup number (0..5).
int pickup_arg(lua_State* L, int arg) {
    if (lua_type(L, arg) != LUA_TSTRING) {
        return static_cast<int>(luaL_checkinteger(L, arg));
    }
    const char* name = lua_tostring(L, arg);
    const auto find = host_of(L)->game_api().common_pickup_number;
    const int number = find != nullptr ? find(name) : -1;
    if (number < 0) {
        return luaL_error(L, "'%s' is not a common pickup", name);
    }
    return number;
}

int l_give_item(lua_State* L) {
    Host* host = host_of(L);
    const auto give = host->game_api().give_item;
    if (give == nullptr) {
        luaL_error(L, "sh.give_item is not available");
        return 0;
    }
    const int id = item_arg(L, 1);
    const lua_Integer count = luaL_optinteger(L, 2, 1);
    give(id, static_cast<int>(count));
    return 0;
}

int l_map_name(lua_State* L) {
    Host* host = host_of(L);
    const auto name = host->game_api().map_name;
    if (name == nullptr) {
        luaL_error(L, "sh.map_name is not available");
        return 0;
    }
    const char* map = name();
    if (map == nullptr) {
        lua_pushlstring(L, "", 0);
    } else {
        lua_pushstring(L, map);
    }
    return 1;
}

// ---- v3: the rest of the game surface, the same thin-checked shape ------------------------------

// Calls `api->field` if bound, else raises "<name> is not available". Fails cleanly on
// a missing bind so a script written for the full game still loads in tests.
#define REQUIRE_API(field, name)                                                                   \
    Host* host = host_of(L);                                                                       \
    const auto field = host->game_api().field;                                                     \
    if (field == nullptr) {                                                                        \
        luaL_error(L, name " is not available");                                                   \
        return 0;                                                                                  \
    }

int l_item_count(lua_State* L) {
    REQUIRE_API(item_count, "sh.item_count")
    lua_pushinteger(L, item_count(item_arg(L, 1)));
    return 1;
}

int l_has_item(lua_State* L) {
    REQUIRE_API(item_count, "sh.has_item")
    lua_pushboolean(L, item_count(item_arg(L, 1)) > 0);
    return 1;
}

int l_take_item(lua_State* L) {
    REQUIRE_API(take_item, "sh.take_item")
    take_item(item_arg(L, 1), static_cast<int>(luaL_optinteger(L, 2, 1)));
    return 0;
}

int l_msg_draw(lua_State* L) {
    REQUIRE_API(msg_draw, "sh.msg_draw")
    lua_pushinteger(L, msg_draw(msg_arg(L, 1)));
    return 1;
}

int l_fade_start(lua_State* L) {
    REQUIRE_API(fade_start, "sh.fade")
    luaL_checkany(L, 1);
    fade_start(lua_toboolean(L, 1) ? 1 : 0, lua_toboolean(L, 2) ? 1 : 0);
    return 0;
}

int l_fade_done(lua_State* L) {
    REQUIRE_API(fade_done, "sh.fade_done")
    lua_pushboolean(L, fade_done() != 0);
    return 1;
}

int l_map_event_set(lua_State* L) {
    REQUIRE_API(map_event_set, "sh.map_event_set")
    const lua_Integer sys_state = luaL_checkinteger(L, 1);
    const lua_Integer param = luaL_optinteger(L, 2, 0);
    const lua_Integer map_idx = luaL_optinteger(L, 3, 0);
    const int complete = opt_flag_arg(L, 4, -1);
    map_event_set(static_cast<int>(sys_state), static_cast<int>(param), static_cast<int>(map_idx), complete);
    return 0;
}

int l_event_point(lua_State* L) {
    REQUIRE_API(event_point, "sh.event_point")
    lua_pushinteger(L, event_point());
    return 1;
}

int l_event_item_take(lua_State* L) {
    REQUIRE_API(event_item_take, "sh.event_item_take")
    event_item_take(item_arg(L, 1), static_cast<int>(luaL_optinteger(L, 2, 1)),
                    flag_arg(L, 3), msg_arg(L, 4));
    return 0;
}

int l_event_common_item_take(lua_State* L) {
    REQUIRE_API(event_common_item_take, "sh.event_common_item_take")
    event_common_item_take(pickup_arg(L, 1), flag_arg(L, 2));
    return 0;
}

int l_state_set(lua_State* L) {
    REQUIRE_API(state_set, "sh.state_set")
    state_set(static_cast<int>(luaL_checkinteger(L, 1)));
    return 0;
}

int l_event_msg(lua_State* L) {
    REQUIRE_API(event_msg, "sh.event_msg")
    luaL_checkany(L, 1);
    event_msg(lua_toboolean(L, 1) ? 1 : 0, msg_arg(L, 2),
              static_cast<int>(luaL_optinteger(L, 3, 0)), static_cast<int>(luaL_optinteger(L, 4, 0)),
              static_cast<int>(luaL_optinteger(L, 5, 0)), lua_toboolean(L, 6) ? 1 : 0);
    return 0;
}

int l_event_msg_with_sfx(lua_State* L) {
    REQUIRE_API(event_msg_with_sfx, "sh.event_msg_with_sfx")
    event_msg_with_sfx(msg_arg(L, 1), sound_arg(L, 2));
    return 0;
}

int l_event_msg_with_bg(lua_State* L) {
    REQUIRE_API(event_msg_with_bg, "sh.event_msg_with_bg")
    event_msg_with_bg(file_arg(L, 1), sh_q12_arg(L, 2), sh_q12_arg(L, 3), msg_arg(L, 4));
    return 0;
}

int l_event_paper_map_take(lua_State* L) {
    REQUIRE_API(event_paper_map_take, "sh.event_paper_map_take")
    event_paper_map_take(paper_map_arg(L, 1), flag_arg(L, 2), msg_arg(L, 3));
    return 0;
}

int l_input_action(lua_State* L) {
    REQUIRE_API(input_action, "sh.input_action")
    const char* action = luaL_checkstring(L, 1);
    const lua_Integer mode = luaL_optinteger(L, 2, 1);  // default: clicked
    lua_pushboolean(L, input_action(action, static_cast<int>(mode)) != 0);
    return 1;
}

int l_input_axis(lua_State* L) {
    REQUIRE_API(input_axis, "sh.input_axis")
    lua_pushinteger(L, input_axis(static_cast<int>(luaL_checkinteger(L, 1)), static_cast<int>(luaL_checkinteger(L, 2))));
    return 1;
}

// camera_pos/look(x, y, z, a0..a6, warp): x/y/z in metres (converted to Q12 by the
// adapter), a0..a6 are the original Q12 offset/accel/speed words passed verbatim.
int l_event_msg_with_texture(lua_State* L) {
    REQUIRE_API(event_msg_with_texture, "sh.event_msg_with_texture")
    event_msg_with_texture(file_arg(L, 1), sh_q12_arg(L, 2), sh_q12_arg(L, 3),
                           msg_arg(L, 4), msg_arg(L, 5));
    return 0;
}

int l_background_gamma(lua_State* L) {
    REQUIRE_API(background_gamma, "sh.background_gamma")
    background_gamma(static_cast<int>(luaL_checkinteger(L, 1)));
    return 0;
}

int l_event_wait_timer(lua_State* L) {
    REQUIRE_API(event_wait_timer, "sh.event_wait_timer")
    lua_pushboolean(L, event_wait_timer(static_cast<int>(luaL_checkinteger(L, 1)),
                                        static_cast<int>(luaL_optinteger(L, 2, 0))) != 0);
    return 1;
}

int l_event_step_get(lua_State* L) {
    REQUIRE_API(event_step_get, "sh.event_step_get")
    lua_pushinteger(L, event_step_get(static_cast<int>(luaL_checkinteger(L, 1))));
    return 1;
}

int l_event_step_set(lua_State* L) {
    REQUIRE_API(event_step_set, "sh.event_step_set")
    event_step_set(static_cast<int>(luaL_checkinteger(L, 1)), static_cast<int>(luaL_checkinteger(L, 2)));
    return 0;
}

int l_event_step_increment(lua_State* L) {
    REQUIRE_API(event_step_increment, "sh.event_step_increment")
    event_step_increment(static_cast<int>(luaL_checkinteger(L, 1)));
    return 0;
}

int l_event_fade_cmd(lua_State* L) {
    REQUIRE_API(event_fade_cmd, "sh.event_fade_cmd")
    event_fade_cmd(static_cast<int>(luaL_checkinteger(L, 1)), static_cast<int>(luaL_optinteger(L, 2, 0)),
                   static_cast<int>(luaL_optinteger(L, 3, 0)), static_cast<int>(luaL_optinteger(L, 4, 0)),
                   static_cast<int>(luaL_optinteger(L, 5, 0)));
    return 0;
}

int l_event_selection(lua_State* L) {
    REQUIRE_API(event_selection, "sh.event_selection")
    luaL_checkany(L, 1);
    event_selection(lua_toboolean(L, 1) ? 1 : 0, msg_arg(L, 2),
                    static_cast<int>(luaL_optinteger(L, 3, 0)), static_cast<int>(luaL_optinteger(L, 4, 0)),
                    static_cast<int>(luaL_optinteger(L, 5, 0)), lua_toboolean(L, 6) ? 1 : 0);
    return 0;
}

int l_event_waypoint_set(lua_State* L) {
    REQUIRE_API(event_waypoint_set, "sh.event_waypoint_set")
    luaL_checkany(L, 1);
    event_waypoint_set(lua_toboolean(L, 1) ? 1 : 0, static_cast<int>(luaL_checkinteger(L, 2)),
                       static_cast<int>(luaL_checkinteger(L, 3)), static_cast<int>(luaL_checkinteger(L, 4)),
                       static_cast<int>(luaL_checkinteger(L, 5)), static_cast<int>(luaL_checkinteger(L, 6)));
    return 0;
}

int l_event_waypoint_player(lua_State* L) {
    REQUIRE_API(event_waypoint_player, "sh.event_waypoint_player")
    event_waypoint_player(state_arg(L, 0, 1),
                          static_cast<int>(luaL_checkinteger(L, 2)), lua_toboolean(L, 3) ? 1 : 0);
    return 0;
}

int l_chara_load(lua_State* L) {
    REQUIRE_API(chara_load, "sh.chara_load")
    lua_pushboolean(L, chara_load(static_cast<int>(luaL_checkinteger(L, 1)), chara_id_arg(L, 2),
                                  static_cast<int>(luaL_optinteger(L, 3, 0))) != 0);
    return 1;
}

int l_chara_process_loads(lua_State* L) {
    REQUIRE_API(chara_process_loads, "sh.chara_process_loads")
    lua_pushboolean(L, chara_process_loads() != 0);
    return 1;
}

int l_event_chara_anim(lua_State* L) {
    REQUIRE_API(event_chara_anim, "sh.event_chara_anim")
    const int slot = static_cast<int>(luaL_checkinteger(L, 2));
    event_chara_anim(static_cast<int>(luaL_checkinteger(L, 1)), slot, state_arg(L, slot, 3),
                     lua_toboolean(L, 4) ? 1 : 0);
    return 0;
}

int l_fs_read_dms(lua_State* L) {
    REQUIRE_API(fs_read_dms, "sh.fs_read_dms")
    fs_read_dms(file_arg(L, 1));
    return 0;
}

int l_fs_queue_done(lua_State* L) {
    REQUIRE_API(fs_queue_done, "sh.fs_queue_done")
    lua_pushboolean(L, fs_queue_done() != 0);
    return 1;
}

int l_dms_ready(lua_State* L) {
    REQUIRE_API(dms_ready, "sh.dms_ready")
    dms_ready();
    return 0;
}

int l_cutscene_border(lua_State* L) {
    REQUIRE_API(cutscene_border, "sh.cutscene_border")
    cutscene_border(static_cast<int>(luaL_checkinteger(L, 1)));
    return 0;
}

int l_cutscene_flag(lua_State* L) {
    REQUIRE_API(cutscene_flag, "sh.cutscene_flag")
    luaL_checkany(L, 1);
    cutscene_flag(lua_toboolean(L, 1) ? 1 : 0);
    return 0;
}

int l_overlay_screen_set(lua_State* L) {
    REQUIRE_API(overlay_screen_set, "sh.overlay_screen_set")
    luaL_checkany(L, 3);
    overlay_screen_set(static_cast<int>(luaL_checkinteger(L, 1)), file_arg(L, 2), lua_toboolean(L, 3) ? 1 : 0);
    return 0;
}

int l_overlay_screen_show(lua_State* L) {
    REQUIRE_API(overlay_screen_show, "sh.overlay_screen_show")
    overlay_screen_show(file_arg(L, 1), static_cast<int>(luaL_checkinteger(L, 2)),
                        static_cast<int>(luaL_checkinteger(L, 3)));
    return 0;
}

int l_overlay_screen_hide(lua_State* L) {
    REQUIRE_API(overlay_screen_hide, "sh.overlay_screen_hide")
    overlay_screen_hide(static_cast<int>(luaL_checkinteger(L, 1)), static_cast<int>(luaL_checkinteger(L, 2)));
    return 0;
}

int l_pad_effect(lua_State* L) {
    REQUIRE_API(pad_effect, "sh.pad_effect")
    pad_effect(static_cast<int>(luaL_checkinteger(L, 1)), static_cast<int>(luaL_checkinteger(L, 2)));
    return 0;
}

int l_selection_silent(lua_State* L) {
    REQUIRE_API(selection_silent, "sh.selection_silent")
    selection_silent(lua_toboolean(L, 1) ? 1 : 0);
    return 0;
}

int l_bg_img_transition(lua_State* L) {
    REQUIRE_API(bg_img_transition, "sh.bg_img_transition")
    bg_img_transition(static_cast<int>(luaL_checkinteger(L, 1)), static_cast<int>(luaL_checkinteger(L, 2)));
    return 0;
}

int l_state_timer_get(lua_State* L) {
    REQUIRE_API(state_timer_get, "sh.state_timer_get")
    lua_pushinteger(L, state_timer_get());
    return 1;
}

int l_state_timer_set(lua_State* L) {
    REQUIRE_API(state_timer_set, "sh.state_timer_set")
    state_timer_set(static_cast<int>(luaL_checkinteger(L, 1)));
    return 0;
}

int l_event_step_reset(lua_State* L) {
    REQUIRE_API(event_step_reset, "sh.event_step_reset")
    event_step_reset();
    return 0;
}

int l_camera_auto_return(lua_State* L) {
    REQUIRE_API(camera_auto_return, "sh.camera_auto_return")
    camera_auto_return(lua_toboolean(L, 1) ? 1 : 0);
    return 0;
}

int l_player_fall_tick(lua_State* L) {
    REQUIRE_API(player_fall_tick, "sh.player_fall_tick")
    player_fall_tick(static_cast<int>(luaL_optinteger(L, 1, 1)));
    return 0;
}

int l_delta_time_get(lua_State* L) {
    REQUIRE_API(delta_time_get, "sh.delta_time_get")
    lua_pushinteger(L, delta_time_get());
    return 1;
}

int l_world_object_add(lua_State* L) {
    REQUIRE_API(world_object_add, "sh.world_object_add")
    world_object_add(luaL_checkstring(L, 1));
    return 0;
}

int l_event_bg_texture_cmd(lua_State* L) {
    REQUIRE_API(event_bg_texture_cmd, "sh.event_bg_texture_cmd")
    event_bg_texture_cmd(static_cast<int>(luaL_checkinteger(L, 1)), file_arg(L, 2),
                         lua_toboolean(L, 3) ? 1 : 0);
    return 0;
}

int l_event_inv_item_cmd(lua_State* L) {
    REQUIRE_API(event_inv_item_cmd, "sh.event_inv_item_cmd")
    event_inv_item_cmd(static_cast<int>(luaL_checkinteger(L, 1)),
                       item_arg(L, 2),
                       static_cast<int>(luaL_optinteger(L, 3, 1)),
                       lua_toboolean(L, 4) ? 1 : 0);
    return 0;
}

int l_pickup_animate(lua_State* L) {
    REQUIRE_API(pickup_animate, "sh.pickup_animate")
    lua_pushboolean(L, pickup_animate(item_arg(L, 1)) != 0);
    return 1;
}

int l_chara_anim_play_to_end(lua_State* L) {
    REQUIRE_API(chara_anim_play_to_end, "sh.chara_anim_play_to_end")
    const int slot = static_cast<int>(luaL_checkinteger(L, 1));
    chara_anim_play_to_end(slot, state_arg(L, slot, 2));
    return 0;
}

int l_chara_slot_transform_get(lua_State* L) {
    REQUIRE_API(chara_slot_transform_get, "sh.chara_slot_transform_get")
    int posRotQ12[4] = {0, 0, 0, 0};
    chara_slot_transform_get(static_cast<int>(luaL_checkinteger(L, 1)), posRotQ12);
    lua_pushinteger(L, posRotQ12[0]);
    lua_pushinteger(L, posRotQ12[1]);
    lua_pushinteger(L, posRotQ12[2]);
    lua_pushinteger(L, posRotQ12[3]);
    return 4;
}

int l_chara_slot_transform_set(lua_State* L) {
    REQUIRE_API(chara_slot_transform_set, "sh.chara_slot_transform_set")
    const int xyzQ12[3] = {static_cast<int>(luaL_checkinteger(L, 2)),
                           static_cast<int>(luaL_checkinteger(L, 3)),
                           static_cast<int>(luaL_checkinteger(L, 4))};
    chara_slot_transform_set(static_cast<int>(luaL_checkinteger(L, 1)), xyzQ12,
                             static_cast<int>(luaL_checkinteger(L, 5)));
    return 0;
}

int l_chara_slot_chara_id_get(lua_State* L) {
    REQUIRE_API(chara_slot_chara_id_get, "sh.chara_slot_chara_id_get")
    lua_pushinteger(L, chara_slot_chara_id_get(static_cast<int>(luaL_checkinteger(L, 1))));
    return 1;
}

int l_chara_slot_control_set(lua_State* L) {
    REQUIRE_API(chara_slot_control_set, "sh.chara_slot_control_set")
    chara_slot_control_set(static_cast<int>(luaL_checkinteger(L, 1)),
                           static_cast<int>(luaL_checkinteger(L, 2)),
                           static_cast<int>(luaL_checkinteger(L, 3)));
    return 0;
}

int l_event_msg_with_audio(lua_State* L) {
    REQUIRE_API(event_msg_with_audio, "sh.event_msg_with_audio")
    lua_pushinteger(L, event_msg_with_audio(msg_arg(L, 1),
                                            static_cast<int>(luaL_optinteger(L, 2, 0))));
    return 1;
}

int l_event_activation_type_get(lua_State* L) {
    REQUIRE_API(event_activation_type_get, "sh.event_activation_type_get")
    lua_pushinteger(L, event_activation_type_get());
    return 1;
}

int l_math_sin(lua_State* L) {
    REQUIRE_API(math_sin, "sh.math_sin")
    lua_pushinteger(L, math_sin(static_cast<int>(luaL_checkinteger(L, 1))));
    return 1;
}

int l_math_cos(lua_State* L) {
    REQUIRE_API(math_cos, "sh.math_cos")
    lua_pushinteger(L, math_cos(static_cast<int>(luaL_checkinteger(L, 1))));
    return 1;
}

int l_event_tween_linear(lua_State* L) {
    REQUIRE_API(event_tween_linear, "sh.event_tween_linear")
    lua_pushinteger(L, event_tween_linear(static_cast<int>(luaL_checkinteger(L, 1)),
                                         static_cast<int>(luaL_checkinteger(L, 2)),
                                         static_cast<int>(luaL_checkinteger(L, 3))));
    return 1;
}

int l_event_tween_reset(lua_State* L) {
    REQUIRE_API(event_tween_reset, "sh.event_tween_reset")
    event_tween_reset(static_cast<int>(luaL_checkinteger(L, 1)));
    return 0;
}

int l_delta_time_set(lua_State* L) {
    REQUIRE_API(delta_time_set, "sh.delta_time_set")
    delta_time_set(static_cast<int>(luaL_checkinteger(L, 1)));
    return 0;
}

int l_chara_slot_anim_flags_set(lua_State* L) {
    REQUIRE_API(chara_slot_anim_flags_set, "sh.chara_slot_anim_flags_set")
    chara_slot_anim_flags_set(static_cast<int>(luaL_checkinteger(L, 1)),
                              static_cast<int>(luaL_checkinteger(L, 2)));
    return 0;
}

int l_chara_slot_anim_flags_clear(lua_State* L) {
    REQUIRE_API(chara_slot_anim_flags_clear, "sh.chara_slot_anim_flags_clear")
    chara_slot_anim_flags_clear(static_cast<int>(luaL_checkinteger(L, 1)),
                                static_cast<int>(luaL_checkinteger(L, 2)));
    return 0;
}

int l_water_effect_enable(lua_State* L) {
    REQUIRE_API(water_effect_enable, "sh.water_effect_enable")
    water_effect_enable();
    return 0;
}

int l_water_effect_disable(lua_State* L) {
    REQUIRE_API(water_effect_disable, "sh.water_effect_disable")
    water_effect_disable();
    return 0;
}

int l_event_sys_state_branch_on_flag(lua_State* L) {
    REQUIRE_API(event_sys_state_branch_on_flag, "sh.event_sys_state_branch_on_flag")
    event_sys_state_branch_on_flag(flag_arg(L, 1),
                                   static_cast<int>(luaL_checkinteger(L, 2)),
                                   static_cast<int>(luaL_checkinteger(L, 3)),
                                   static_cast<int>(lua_toboolean(L, 4)));
    return 0;
}

int l_held_item_show(lua_State* L) {
    REQUIRE_API(held_item_show, "sh.held_item_show")
    held_item_show();
    return 0;
}

int l_held_item_hide(lua_State* L) {
    REQUIRE_API(held_item_hide, "sh.held_item_hide")
    held_item_hide();
    return 0;
}

int l_world_object_pos_get(lua_State* L) {
    REQUIRE_API(world_object_pos_get, "sh.world_object_pos_get")
    int pos[3] = {0, 0, 0};
    lua_pushinteger(L, world_object_pos_get(luaL_checkstring(L, 1), pos) ? 1 : 0);
    lua_pushinteger(L, pos[0]);
    lua_pushinteger(L, pos[1]);
    lua_pushinteger(L, pos[2]);
    return 4;
}

int l_world_object_pos_set(lua_State* L) {
    REQUIRE_API(world_object_pos_set, "sh.world_object_pos_set")
    const int xyz[3] = {static_cast<int>(luaL_checkinteger(L, 2)),
                        static_cast<int>(luaL_checkinteger(L, 3)),
                        static_cast<int>(luaL_checkinteger(L, 4))};
    lua_pushinteger(L, world_object_pos_set(luaL_checkstring(L, 1), xyz) ? 1 : 0);
    return 1;
}

int l_world_object_rot_y_set(lua_State* L) {
    REQUIRE_API(world_object_rot_y_set, "sh.world_object_rot_y_set")
    lua_pushinteger(L, world_object_rot_y_set(luaL_checkstring(L, 1),
                                              static_cast<int>(luaL_checkinteger(L, 2))) ? 1 : 0);
    return 1;
}

int l_held_item_attach(lua_State* L) {
    REQUIRE_API(held_item_attach, "sh.held_item_attach")
    held_item_attach(static_cast<int>(luaL_checkinteger(L, 1)),
                     static_cast<int>(luaL_checkinteger(L, 2)));
    return 0;
}

int l_held_item_set(lua_State* L) {
    REQUIRE_API(held_item_set, "sh.held_item_set")
    lua_pushinteger(L, held_item_set(item_arg(L, 1)));
    return 1;
}

int l_held_item_prev(lua_State* L) {
    REQUIRE_API(held_item_prev, "sh.held_item_prev")
    lua_pushinteger(L, held_item_prev());
    return 1;
}

int l_player_weapon_attack_set(lua_State* L) {
    REQUIRE_API(player_weapon_attack_set, "sh.player_weapon_attack_set")
    player_weapon_attack_set(static_cast<int>(luaL_checkinteger(L, 1)));
    return 0;
}

int l_player_weapon_attack_get(lua_State* L) {
    REQUIRE_API(player_weapon_attack_get, "sh.player_weapon_attack_get")
    lua_pushinteger(L, player_weapon_attack_get());
    return 1;
}

int l_math_atan2(lua_State* L) {
    REQUIRE_API(math_atan2, "sh.math_atan2")
    lua_pushinteger(L, math_atan2(static_cast<int>(luaL_checkinteger(L, 1)),
                                  static_cast<int>(luaL_checkinteger(L, 2))));
    return 1;
}

int l_event_bg_texture_fade_in(lua_State* L) {
    REQUIRE_API(event_bg_texture_fade_in, "sh.event_bg_texture_fade_in")
    event_bg_texture_fade_in(file_arg(L, 1),
                             static_cast<int>(luaL_checkinteger(L, 2)),
                             static_cast<int>(luaL_checkinteger(L, 3)));
    return 0;
}

int l_event_bg_texture_fade_out(lua_State* L) {
    REQUIRE_API(event_bg_texture_fade_out, "sh.event_bg_texture_fade_out")
    event_bg_texture_fade_out(static_cast<int>(luaL_checkinteger(L, 1)),
                              static_cast<int>(luaL_checkinteger(L, 2)));
    return 0;
}

int l_screen_fade_finished(lua_State* L) {
    REQUIRE_API(screen_fade_finished, "sh.screen_fade_finished")
    lua_pushboolean(L, screen_fade_finished() != 0);
    return 1;
}

int l_world_object_rot_y_get(lua_State* L) {
    REQUIRE_API(world_object_rot_y_get, "sh.world_object_rot_y_get")
    lua_pushinteger(L, world_object_rot_y_get(luaL_checkstring(L, 1)));
    return 1;
}

int l_player_move_speed_clear(lua_State* L) {
    REQUIRE_API(player_move_speed_clear, "sh.player_move_speed_clear")
    player_move_speed_clear();
    return 0;
}

int l_rng_int(lua_State* L) {
    REQUIRE_API(rng_int, "sh.rng_int")
    lua_pushinteger(L, rng_int(static_cast<int>(luaL_checkinteger(L, 1)),
                               static_cast<int>(luaL_checkinteger(L, 2))));
    return 1;
}

int l_rng_uint(lua_State* L) {
    REQUIRE_API(rng_uint, "sh.rng_uint")
    lua_pushinteger(L, rng_uint(static_cast<int>(luaL_checkinteger(L, 1)),
                                static_cast<int>(luaL_checkinteger(L, 2))));
    return 1;
}

int l_game_warm_boot(lua_State* L) {
    REQUIRE_API(game_warm_boot, "sh.game_warm_boot")
    game_warm_boot();
    return 0;
}

int l_screen_fade_timestep_get(lua_State* L) {
    REQUIRE_API(screen_fade_timestep_get, "sh.screen_fade_timestep_get")
    lua_pushinteger(L, screen_fade_timestep_get());
    return 1;
}

int l_screen_fade_timestep_set(lua_State* L) {
    REQUIRE_API(screen_fade_timestep_set, "sh.screen_fade_timestep_set")
    screen_fade_timestep_set(static_cast<int>(luaL_checkinteger(L, 1)));
    return 0;
}

int l_player_rotate(lua_State* L) {
    REQUIRE_API(player_rotate, "sh.player_rotate")
    player_rotate(static_cast<int>(luaL_checkinteger(L, 1)));
    return 0;
}

int l_player_rot_y_get(lua_State* L) {
    REQUIRE_API(player_rot_y_get, "sh.player_rot_y_get")
    lua_pushinteger(L, player_rot_y_get());
    return 1;
}

int l_event_wait_player_stop(lua_State* L) {
    REQUIRE_API(event_wait_player_stop, "sh.event_wait_player_stop")
    lua_pushboolean(L, event_wait_player_stop() != 0);
    return 1;
}

int l_event_step_increment_delayed(lua_State* L) {
    REQUIRE_API(event_step_increment_delayed, "sh.event_step_increment_delayed")
    event_step_increment_delayed(static_cast<int>(luaL_checkinteger(L, 1)),
                                 lua_toboolean(L, 2) ? 1 : 0);
    return 0;
}

int l_event_step_increment_after_fade(lua_State* L) {
    REQUIRE_API(event_step_increment_after_fade, "sh.event_step_increment_after_fade")
    luaL_checkany(L, 2);
    event_step_increment_after_fade(static_cast<int>(luaL_checkinteger(L, 1)),
                                    lua_toboolean(L, 2) ? 1 : 0,
                                    static_cast<int>(luaL_optinteger(L, 3, 0)),
                                    static_cast<int>(luaL_optinteger(L, 4, 0)),
                                    lua_toboolean(L, 5) ? 1 : 0);
    return 0;
}

int l_screen_fade_status_set(lua_State* L) {
    REQUIRE_API(screen_fade_status_set, "sh.screen_fade_status_set")
    screen_fade_status_set(static_cast<int>(luaL_checkinteger(L, 1)));
    return 0;
}

int l_game_state_update(lua_State* L) {
    REQUIRE_API(game_state_update, "sh.game_state_update")
    game_state_update();
    return 0;
}

int l_delta_time_reset(lua_State* L) {
    REQUIRE_API(delta_time_reset, "sh.delta_time_reset")
    delta_time_reset();
    return 0;
}

int l_screen_fade_reset_timestep(lua_State* L) {
    REQUIRE_API(screen_fade_reset_timestep, "sh.screen_fade_reset_timestep")
    screen_fade_reset_timestep();
    return 0;
}

int l_player_vel_y_get(lua_State* L) {
    REQUIRE_API(player_vel_y_get, "sh.player_vel_y_get")
    lua_pushinteger(L, player_vel_y_get());
    return 1;
}

int l_player_vel_y_set(lua_State* L) {
    REQUIRE_API(player_vel_y_set, "sh.player_vel_y_set")
    player_vel_y_set(static_cast<int>(luaL_checkinteger(L, 1)));
    return 0;
}

// sh.chara_* take the character as an id (28) or a name ("Chara_Cheryl", "Cheryl"); an
// unknown one answers with the fallback, like an unset slot - scripts never error on data.
int chara_arg(lua_State* L, int index, int (*find)(const char*)) {
    if (lua_type(L, index) == LUA_TNUMBER) {
        return static_cast<int>(luaL_checkinteger(L, index));
    }
    const char* name = luaL_checkstring(L, index);
    return find != nullptr ? find_scoped(L, name, find) : -1;
}

int l_chara_id(lua_State* L) {
    REQUIRE_API(chara_id_find, "sh.chara_id")
    const int id = find_scoped(L, luaL_checkstring(L, 1), chara_id_find);
    if (id < 0) {
        lua_pushnil(L);
    } else {
        lua_pushinteger(L, id);
    }
    return 1;
}

int l_chara_param(lua_State* L) {
    REQUIRE_API(chara_param, "sh.chara_param")
    const int id = chara_arg(L, 1, host->game_api().chara_id_find);
    const char* name = luaL_checkstring(L, 2);
    int found = 0;
    const double value = id < 0 ? 0.0 : chara_param(id, name, 0.0, &found);
    if (found) {
        lua_pushnumber(L, value);
    } else if (lua_isnoneornil(L, 3)) {
        lua_pushnil(L);
    } else {
        lua_pushvalue(L, 3);
    }
    return 1;
}

int l_chara_param_q12(lua_State* L) {
    REQUIRE_API(chara_param_q12, "sh.chara_param_q12")
    const int id = chara_arg(L, 1, host->game_api().chara_id_find);
    const char* name = luaL_checkstring(L, 2);
    const int fallback = static_cast<int>(luaL_checkinteger(L, 3));
    lua_pushinteger(L, id < 0 ? fallback : chara_param_q12(id, name, fallback));
    return 1;
}

int l_chara_anim(lua_State* L) {
    REQUIRE_API(chara_anim, "sh.chara_anim")
    const int id = chara_arg(L, 1, host->game_api().chara_id_find);
    const char* name = luaL_checkstring(L, 2);
    const int fallback = static_cast<int>(luaL_optinteger(L, 3, -1));
    lua_pushinteger(L, id < 0 ? fallback : chara_anim(id, name, fallback));
    return 1;
}

// sh.chara_state("Harry", "pickup_crouch"): a cutscene state number by name (Harry's
// "states", an NPC's "controls"); the fallback when the character or the name is unknown.
int l_chara_state(lua_State* L) {
    REQUIRE_API(chara_state_number, "sh.chara_state")
    const int id = chara_arg(L, 1, host->game_api().chara_id_find);
    const char* name = luaL_checkstring(L, 2);
    const int fallback = static_cast<int>(luaL_optinteger(L, 3, -1));
    const int number = id < 0 ? -1 : chara_state_number(id, name);
    lua_pushinteger(L, number >= 0 ? number : fallback);
    return 1;
}

int l_text(lua_State* L) {
    REQUIRE_API(text_get, "sh.text")
    const char* key = luaL_checkstring(L, 1);
    const char* text = text_get(key);
    if (text == nullptr) {
        // An unknown key reads as itself, so a missing translation shows where it is.
        lua_pushstring(L, key);
        lua_pushboolean(L, 0);
        return 2;
    }
    lua_pushstring(L, text);
    lua_pushboolean(L, 1);
    return 2;
}

int l_msg_index(lua_State* L) {
    REQUIRE_API(msg_index, "sh.msg_index")
    lua_pushinteger(L, msg_index(luaL_checkstring(L, 1)));
    return 1;
}

// ---- the flat functions of the maps 0-3 handlers ----------------------------------------------------

int ea_int(lua_State* L, int idx) { return static_cast<int>(luaL_checkinteger(L, idx)); }

int ea_push3(lua_State* L, const int v[3]) {
    for (int i = 0; i < 3; ++i) {
        lua_pushinteger(L, v[i]);
    }
    return 3;
}

int l_fs_read(lua_State* L) {
    REQUIRE_API(fs_read, "sh.fs_read")
    lua_pushinteger(L, fs_read(file_arg(L, 1), ea_int(L, 2)));
    return 1;
}

int l_fs_wait(lua_State* L) {
    REQUIRE_API(fs_wait, "sh.fs_wait")
    fs_wait();
    return 0;
}

int l_fs_seek(lua_State* L) {
    REQUIRE_API(fs_seek, "sh.fs_seek")
    lua_pushinteger(L, fs_seek(file_arg(L, 1)));
    return 1;
}

int l_file_number(lua_State* L) {
    REQUIRE_API(file_number, "sh.file_number")
    const char* name = luaL_checkstring(L, 1);
    const int number = file_number(name);
    if (number < 0) {
        return luaL_error(L, "unknown game file '%s'", name);
    }
    lua_pushinteger(L, number);
    return 1;
}

int l_dms_fix(lua_State* L) {
    REQUIRE_API(dms_fix, "sh.dms_fix")
    dms_fix(ea_int(L, 1));
    return 0;
}

int l_dms_chara_apply(lua_State* L) {
    REQUIRE_API(dms_chara_apply, "sh.dms_chara_apply")
    dms_chara_apply(ea_int(L, 1), luaL_checkstring(L, 2), ea_int(L, 3), ea_int(L, 4));
    return 0;
}

int l_dms_camera_apply(lua_State* L) {
    REQUIRE_API(dms_camera_apply, "sh.dms_camera_apply")
    dms_camera_apply(ea_int(L, 1), ea_int(L, 2));
    return 0;
}

int l_vc_cam_target(lua_State* L) {
    REQUIRE_API(vc_cam_target, "sh.vc_cam_target")
    const int xyz[3] = {ea_int(L, 1), ea_int(L, 2), ea_int(L, 3)};
    vc_cam_target(xyz, lua_toboolean(L, 4) ? 1 : 0);
    return 0;
}

int l_vc_watch_target(lua_State* L) {
    REQUIRE_API(vc_watch_target, "sh.vc_watch_target")
    const int xyz[3] = {ea_int(L, 1), ea_int(L, 2), ea_int(L, 3)};
    vc_watch_target(xyz, lua_toboolean(L, 4) ? 1 : 0);
    return 0;
}

int l_vc_view_position(lua_State* L) {
    REQUIRE_API(vc_view_position, "sh.vc_view_position")
    int out[3] = {};
    vc_view_position(out);
    return ea_push3(L, out);
}

int l_light_intensity_set(lua_State* L) {
    REQUIRE_API(light_intensity_set, "sh.light_intensity_set")
    light_intensity_set(ea_int(L, 1));
    return 0;
}

int l_light_bones_clear(lua_State* L) {
    REQUIRE_API(light_bones_clear, "sh.light_bones_clear")
    light_bones_clear();
    return 0;
}

int l_light_pos_set(lua_State* L) {
    REQUIRE_API(light_pos_set, "sh.light_pos_set")
    const int xyz[3] = {ea_int(L, 1), ea_int(L, 2), ea_int(L, 3)};
    light_pos_set(xyz);
    return 0;
}

int l_light_rot_set(lua_State* L) {
    REQUIRE_API(light_rot_set, "sh.light_rot_set")
    const int xyz[3] = {ea_int(L, 1), ea_int(L, 2), ea_int(L, 3)};
    light_rot_set(xyz);
    return 0;
}

int l_flashlight_attrs_fix(lua_State* L) {
    REQUIRE_API(flashlight_attrs_fix, "sh.flashlight_attrs_fix")
    flashlight_attrs_fix();
    return 0;
}

int l_math_vec2_mag_q6(lua_State* L) {
    REQUIRE_API(math_vec2_mag_q6, "sh.math_vec2_mag_q6")
    lua_pushinteger(L, math_vec2_mag_q6(ea_int(L, 1), ea_int(L, 2)));
    return 1;
}

int l_chara_model_clear(lua_State* L) {
    REQUIRE_API(chara_model_clear, "sh.chara_model_clear")
    chara_model_clear(ea_int(L, 1));
    return 0;
}

int l_chara_props_reset(lua_State* L) {
    REQUIRE_API(chara_props_reset, "sh.chara_props_reset")
    chara_props_reset(ea_int(L, 1));
    return 0;
}

int l_player_weapon_stash(lua_State* L) {
    REQUIRE_API(player_weapon_stash, "sh.player_weapon_stash")
    player_weapon_stash();
    return 0;
}

int l_player_weapon_restore(lua_State* L) {
    REQUIRE_API(player_weapon_restore, "sh.player_weapon_restore")
    player_weapon_restore();
    return 0;
}

int l_map_effects_init(lua_State* L) {
    REQUIRE_API(map_effects_init, "sh.map_effects_init")
    map_effects_init(ea_int(L, 1), ea_int(L, 2));
    return 0;
}

int l_sys_flags_get(lua_State* L) {
    REQUIRE_API(sys_flags_get, "sh.sys_flags_get")
    lua_pushinteger(L, sys_flags_get());
    return 1;
}

int l_sys_flags_set(lua_State* L) {
    REQUIRE_API(sys_flags_set, "sh.sys_flags_set")
    sys_flags_set(ea_int(L, 1));
    return 0;
}

// "s8"/"u8"/"s16"/"u16"/"s32" -> the C side's type code.
// The audio index is either a map symbol (string, shared with C++) or a Lua-held number
// (returned updated); the voice-cmd table is always the map's symbol.
int ea_msg_audio(lua_State* L, int alias) {
    REQUIRE_API(event_msg_audio, "sh.event_msg_audio")
    const bool by_name = lua_type(L, 2) == LUA_TSTRING;
    const int idx = event_msg_audio(alias, msg_arg(L, 1), by_name ? lua_tostring(L, 2) : nullptr,
                                    by_name ? 0 : ea_int(L, 2), luaL_checkstring(L, 3));
    lua_pushinteger(L, idx);
    return 1;
}

int l_event_msg_audio(lua_State* L) { return ea_msg_audio(L, 0); }

int l_map_msg_audio(lua_State* L) { return ea_msg_audio(L, 1); }

int l_event_msg_dimmed_bg(lua_State* L) {
    REQUIRE_API(event_msg_dimmed_bg, "sh.event_msg_dimmed_bg")
    event_msg_dimmed_bg(file_arg(L, 1), ea_int(L, 2), ea_int(L, 3), msg_arg(L, 4), msg_arg(L, 5));
    return 0;
}

int l_chara_anim_unlock_play_to_end(lua_State* L) {
    REQUIRE_API(chara_anim_unlock_play_to_end, "sh.chara_anim_unlock_play_to_end")
    chara_anim_unlock_play_to_end(ea_int(L, 1));
    return 0;
}

int l_paper_map_state(lua_State* L) {
    REQUIRE_API(paper_map_state, "sh.paper_map_state")
    paper_map_state(ea_int(L, 1), paper_map_arg(L, 2));
    return 0;
}

int l_paper_map_cmd(lua_State* L) {
    REQUIRE_API(paper_map_cmd, "sh.paper_map_cmd")
    paper_map_cmd(ea_int(L, 1), paper_map_arg(L, 2));
    return 0;
}

int l_map_screen_vertex_draw(lua_State* L) {
    REQUIRE_API(map_screen_vertex_draw, "sh.map_screen_vertex_draw")
    map_screen_vertex_draw(ea_int(L, 1), ea_int(L, 2), ea_int(L, 3));
    return 0;
}

int l_map_screen_marking_draw(lua_State* L) {
    REQUIRE_API(map_screen_marking_draw, "sh.map_screen_marking_draw")
    int args[7] = {};
    for (int i = 0; i < 7; ++i) {
        // The third argument picks one mark half by its flag ("MapMark_OldTown_BalkanChurchCircle"; 0 = none).
        args[i] = i == 2 ? flag_arg(L, i + 1) : ea_int(L, i + 1);
    }
    lua_pushboolean(L, map_screen_marking_draw(args) != 0);
    return 1;
}

int l_map_screen_player_mark_draw(lua_State* L) {
    REQUIRE_API(map_screen_player_mark_draw, "sh.map_screen_player_mark_draw")
    lua_pushinteger(L, map_screen_player_mark_draw(ea_int(L, 1), ea_int(L, 2), ea_int(L, 3), ea_int(L, 4)));
    return 1;
}

int l_map_box_outline_draw(lua_State* L) {
    REQUIRE_API(map_box_outline_draw, "sh.map_box_outline_draw")
    int args[9] = {};
    for (int i = 0; i < 9; ++i) {
        args[i] = ea_int(L, i + 1);
    }
    map_box_outline_draw(args);
    return 0;
}

int l_vc_stereo_balance(lua_State* L) {
    REQUIRE_API(vc_stereo_balance, "sh.vc_stereo_balance")
    const int xyz[3] = {ea_int(L, 1), ea_int(L, 2), ea_int(L, 3)};
    lua_pushinteger(L, vc_stereo_balance(xyz));
    return 1;
}

int l_light_intensity_get(lua_State* L) {
    REQUIRE_API(light_intensity_get, "sh.light_intensity_get")
    lua_pushinteger(L, light_intensity_get());
    return 1;
}

// chara_get/chara_set field names -> the C side's field code (PcEventsA_CharaField order).
int ea_chara_field(lua_State* L, int idx) {
    static const char* const kFields[] = {"health",     "move_speed",  "fall_speed",    "control_state",
                                          "state_step", "anim_time",   "anim_status",   "anim_keyframe",
                                          "anim_flags", "collision_state", "flags",     "chara_id", nullptr};
    return luaL_checkoption(L, idx, nullptr, kFields);
}

int l_chara_get(lua_State* L) {
    REQUIRE_API(chara_get, "sh.chara_field_get")
    int ok = 0;
    const int value = chara_get(ea_int(L, 1), ea_chara_field(L, 2), &ok);
    if (!ok) {
        return luaL_error(L, "sh.chara_field_get: no character in slot %d", ea_int(L, 1));
    }
    lua_pushinteger(L, value);
    return 1;
}

int l_chara_set(lua_State* L) {
    REQUIRE_API(chara_set, "sh.chara_field_set")
    if (!chara_set(ea_int(L, 1), ea_chara_field(L, 2), ea_int(L, 3))) {
        return luaL_error(L, "sh.chara_field_set: no character in slot %d", ea_int(L, 1));
    }
    return 0;
}

int l_chara_anim_start_keyframe_get(lua_State* L) {
    REQUIRE_API(chara_anim_start_keyframe_get, "sh.chara_anim_start_keyframe_get")
    lua_pushinteger(L, chara_anim_start_keyframe_get(ea_int(L, 1)));
    return 1;
}

int l_math_point_from_angle(lua_State* L) {
    REQUIRE_API(math_point_from_angle, "sh.math_point_from_angle")
    const int xyz[3] = {ea_int(L, 1), ea_int(L, 2), ea_int(L, 3)};
    math_point_from_angle(xyz, ea_int(L, 4), ea_int(L, 5), ea_int(L, 6));
    return 0;
}

int l_chara_bones_init(lua_State* L) {
    REQUIRE_API(chara_bones_init, "sh.chara_bones_init")
    chara_bones_init(ea_int(L, 1));
    return 0;
}

int l_camera_default_speeds(lua_State* L) {
    REQUIRE_API(camera_default_speeds, "sh.camera_default_speeds")
    int out[4] = {};
    camera_default_speeds(out);
    for (int v : out) {
        lua_pushinteger(L, v);
    }
    return 4;
}

int l_fs_entry_loaded(lua_State* L) {
    REQUIRE_API(fs_entry_loaded, "sh.fs_entry_loaded")
    lua_pushboolean(L, fs_entry_loaded(ea_int(L, 1)) != 0);
    return 1;
}

int l_map_effects_update(lua_State* L) {
    REQUIRE_API(map_effects_update, "sh.map_effects_update")
    if (!map_effects_update(ea_int(L, 1), ea_int(L, 2), ea_int(L, 3), luaL_checkstring(L, 4), ea_int(L, 5),
                            ea_int(L, 6))) {
        return luaL_error(L, "sh.map_effects_update: %s is not a symbol of the loaded map", lua_tostring(L, 4));
    }
    return 0;
}

int ea_savegame_field(lua_State* L, int idx) {
    static const char* const kFields[] = {"map_idx", "paper_map_idx", "item_toggle_flags", nullptr};
    return luaL_checkoption(L, idx, nullptr, kFields);
}

int l_savegame_get(lua_State* L) {
    REQUIRE_API(savegame_get, "sh.savegame_get")
    lua_pushinteger(L, savegame_get(ea_savegame_field(L, 1)));
    return 1;
}

int l_savegame_set(lua_State* L) {
    REQUIRE_API(savegame_set, "sh.savegame_set")
    const int field = ea_savegame_field(L, 1);
    // paper_map_idx takes the page name ("FogSchool1F"); the other fields stay integers.
    savegame_set(field, field == 1 ? paper_map_arg(L, 2) : ea_int(L, 2));
    return 0;
}

int l_cursor_draw(lua_State* L) {
    REQUIRE_API(cursor_draw, "sh.cursor_draw")
    int args[12] = {};
    for (int i = 0; i < 12; ++i) {
        args[i] = ea_int(L, i + 1);
    }
    cursor_draw(args);
    return 0;
}

int l_mouse_cursor_puzzle_override(lua_State* L) {
    REQUIRE_API(mouse_cursor_puzzle_override, "sh.mouse_cursor_puzzle_override")
    if (!mouse_cursor_puzzle_override(luaL_checkstring(L, 1), luaL_checkstring(L, 2))) {
        return luaL_error(L, "sh.mouse_cursor_puzzle_override: %s/%s are not symbols of the loaded map",
                          lua_tostring(L, 1), lua_tostring(L, 2));
    }
    return 0;
}

int l_item_remove(lua_State* L) {
    REQUIRE_API(item_remove, "sh.item_remove")
    lua_pushboolean(L, item_remove(item_arg(L, 1), ea_int(L, 2)) != 0);
    return 1;
}

/* render: sh.render.*. Every field has a fixed set of values or a
 * range; an unknown key or value is an error so a typo fails loudly. `fn` names the call in
 * the message. */
int render_option_at(lua_State* L, int idx, const char* fn, const char* key, const char* const* names,
                     int fallback) {
    const int type = lua_getfield(L, idx, key);
    int value = fallback;
    if (type != LUA_TNIL) {
        const char* given = lua_tostring(L, -1);
        value = -1;
        for (int i = 0; given != nullptr && names[i] != nullptr; ++i) {
            if (std::strcmp(given, names[i]) == 0) {
                value = i;
            }
        }
        if (value < 0) {
            return luaL_error(L, "%s: bad %s '%s'", fn, key, given ? given : luaL_typename(L, -1));
        }
    }
    lua_pop(L, 1);
    return value;
}

int render_option(lua_State* L, const char* fn, const char* key, const char* const* names, int fallback) {
    return render_option_at(L, 1, fn, key, names, fallback);
}

int render_int(lua_State* L, int idx, const char* fn, const char* what, int lo, int hi) {
    if (!lua_isinteger(L, idx)) {
        return luaL_error(L, "%s: %s must be an integer %d..%d", fn, what, lo, hi);
    }
    const lua_Integer v = lua_tointeger(L, idx);
    if (v < lo || v > hi) {
        return luaL_error(L, "%s: %s %d is outside %d..%d", fn, what, static_cast<int>(v), lo, hi);
    }
    return static_cast<int>(v);
}

int render_channel(lua_State* L, int idx, const char* fn, const char* what) {
    return render_int(L, idx, fn, what, 0, 255);
}

// <key> = 128 (grey level) or {r, g, b} in the table at `idx`; absent keeps `tint`.
void render_colour_at(lua_State* L, int idx, const char* fn, const char* key, int tint[3]) {
    const int tintType = lua_getfield(L, idx, key);
    if (tintType == LUA_TTABLE) {
        for (int c = 0; c < 3; ++c) {
            lua_geti(L, -1, c + 1);
            tint[c] = render_channel(L, -1, fn, "tint channel");
            lua_pop(L, 1);
        }
    } else if (tintType != LUA_TNIL) {
        const int grey = render_channel(L, -1, fn, key);
        tint[0] = tint[1] = tint[2] = grey;
    }
    lua_pop(L, 1);
}

void render_tint(lua_State* L, const char* fn, int tint[3]) {
    render_colour_at(L, 1, fn, "tint", tint);
}

// offset = {dx, dy} of the table at `idx` in screen pixels; absent keeps `offset`.
void render_offset_at(lua_State* L, int idx, const char* fn, int offset[2]) {
    const int type = lua_getfield(L, idx, "offset");
    if (type == LUA_TTABLE) {
        for (int i = 0; i < 2; ++i) {
            lua_geti(L, -1, i + 1);
            offset[i] = render_int(L, -1, fn, i == 0 ? "offset dx" : "offset dy", -320, 320);
            lua_pop(L, 1);
        }
    } else if (type != LUA_TNIL) {
        luaL_error(L, "%s: offset must be {dx, dy}", fn);
    }
    lua_pop(L, 1);
}

// The blend names of frame_hold, world_redirect and tile; index - 1 is the API value.
const char* const kRenderBlends[] = {"none", "half", "add", "sub", "quarter", nullptr};

// Fails on a key of the table at `idx` that is not in `keys`.
void render_check_keys(lua_State* L, int idx, const char* fn, const char* const* keys) {
    lua_pushnil(L);
    while (lua_next(L, idx) != 0) {
        const char* key = lua_type(L, -2) == LUA_TSTRING ? lua_tostring(L, -2) : nullptr;
        bool known = false;
        for (int i = 0; key != nullptr && keys[i] != nullptr; ++i) {
            known = known || std::strcmp(key, keys[i]) == 0;
        }
        if (!known) {
            luaL_error(L, "%s: unknown field '%s'", fn, key ? key : "?");
            return;
        }
        lua_pop(L, 1);
    }
}

int l_render_frame_hold(lua_State* L) {
    REQUIRE_API(render_frame_hold, "sh.render.frame_hold")
    static const char* const kFn = "sh.render.frame_hold";
    static const char* const kKeys[] = {"source", "tint", "blend", "layer", "offset", nullptr};
    static const char* const kSources[] = {"previous", "held", "current", nullptr};
    static const char* const kLayers[] = {"world", "overlay", nullptr};
    FrameHold spec;
    if (!lua_isnoneornil(L, 1)) {
        luaL_checktype(L, 1, LUA_TTABLE);
        render_check_keys(L, 1, kFn, kKeys);
        spec.source = render_option(L, kFn, "source", kSources, spec.source);
        spec.blend = render_option(L, kFn, "blend", kRenderBlends, spec.blend + 1) - 1;
        spec.layer = render_option(L, kFn, "layer", kLayers, spec.layer);
        render_tint(L, kFn, spec.tint);
        render_offset_at(L, 1, kFn, spec.offset);
    }
    char err[160] = {};
    if (render_frame_hold(&spec, err, static_cast<int>(sizeof(err))) != 0) {
        return luaL_error(L, "%s: %s", kFn, err[0] != '\0' ? err : "refused");
    }
    return 0;
}

/* sh.render.held_clear() / sh.render.held_capture(): the held picture becomes black / gets a
 * copy of the frame as drawn so far (at the end of the world layer). */
int l_render_held_clear(lua_State* L) {
    REQUIRE_API(render_held_clear, "sh.render.held_clear")
    char err[160] = {};
    if (render_held_clear(err, static_cast<int>(sizeof(err))) != 0) {
        return luaL_error(L, "sh.render.held_clear: %s", err[0] != '\0' ? err : "refused");
    }
    return 0;
}

int l_render_held_capture(lua_State* L) {
    REQUIRE_API(render_held_capture, "sh.render.held_capture")
    char err[160] = {};
    if (render_held_capture(err, static_cast<int>(sizeof(err))) != 0) {
        return luaL_error(L, "sh.render.held_capture: %s", err[0] != '\0' ? err : "refused");
    }
    return 0;
}

/* sh.render.world_redirect{layers = {{offset=, tint=, blend=}, ...}}: this frame the world is
 * drawn into the held picture and comes onto the screen as the layers, the first at the
 * bottom (each layer as frame_hold{source = "held", layer = "world"}). */
constexpr int kMaxLayers = 8;

int l_render_world_redirect(lua_State* L) {
    REQUIRE_API(render_world_redirect, "sh.render.world_redirect")
    static const char* const kFn = "sh.render.world_redirect";
    static const char* const kKeys[] = {"layers", nullptr};
    static const char* const kLayerKeys[] = {"offset", "tint", "blend", nullptr};
    luaL_checktype(L, 1, LUA_TTABLE);
    render_check_keys(L, 1, kFn, kKeys);
    if (lua_getfield(L, 1, "layers") != LUA_TTABLE) {
        return luaL_error(L, "%s: layers = {{offset=, tint=, blend=}, ...} is required", kFn);
    }
    const int list = lua_gettop(L);
    const lua_Integer n = luaL_len(L, list);
    if (n < 1 || n > kMaxLayers) {
        return luaL_error(L, "%s: %d layers, 1..%d per call", kFn, static_cast<int>(n), kMaxLayers);
    }
    Layer items[kMaxLayers];
    for (lua_Integer i = 1; i <= n; ++i) {
        if (lua_geti(L, list, i) != LUA_TTABLE) {
            return luaL_error(L, "%s: layers[%d] must be a table {offset=, tint=, blend=}", kFn, static_cast<int>(i));
        }
        const int item = lua_gettop(L);
        render_check_keys(L, item, kFn, kLayerKeys);
        Layer& layer = items[i - 1];
        render_offset_at(L, item, kFn, layer.offset);
        render_colour_at(L, item, kFn, "tint", layer.tint);
        layer.blend = render_option_at(L, item, kFn, "blend", kRenderBlends, layer.blend + 1) - 1;
        lua_pop(L, 1);
    }
    lua_pop(L, 1);
    Layers spec;
    spec.count = static_cast<int>(n);
    spec.items = items;
    char err[160] = {};
    if (render_world_redirect(&spec, err, static_cast<int>(sizeof(err))) != 0) {
        return luaL_error(L, "%s: %s", kFn, err[0] != '\0' ? err : "refused");
    }
    return 0;
}

/* sh.render.tile{rect = {x, y, w, h}, colour =, blend =, layer =, tpage =, dither =}: one
 * untextured rectangle; colour is a grey level or {r, g, b} (required), blend as frame_hold
 * (default "none"), layer "world", "overlay", "picture" or "screen" (default), tpage a raw
 * texture page word for the page packet (0..0xFFFF, as in sh.render.sprite), dither true to
 * turn dithering on in that packet (default false). */
int l_render_tile(lua_State* L) {
    REQUIRE_API(render_tile, "sh.render.tile")
    static const char* const kFn = "sh.render.tile";
    static const char* const kKeys[] = {"rect", "colour", "blend", "layer", "tpage", "dither", nullptr};
    static const char* const kLayers[] = {"world", "overlay", "picture", "screen", nullptr};
    luaL_checktype(L, 1, LUA_TTABLE);
    render_check_keys(L, 1, kFn, kKeys);
    Tile spec;
    if (lua_getfield(L, 1, "rect") != LUA_TTABLE) {
        return luaL_error(L, "%s: rect = {x, y, w, h} is required", kFn);
    }
    static const char* const kRect[] = {"rect x", "rect y", "rect w", "rect h"};
    int r[4] = {};
    for (int i = 0; i < 4; ++i) {
        lua_geti(L, -1, i + 1);
        r[i] = i < 2 ? render_int(L, -1, kFn, kRect[i], -1024, 1023) : render_int(L, -1, kFn, kRect[i], 0, 1023);
        lua_pop(L, 1);
    }
    lua_pop(L, 1);
    spec.x = r[0];
    spec.y = r[1];
    spec.w = r[2];
    spec.h = r[3];
    if (lua_getfield(L, 1, "colour") == LUA_TNIL) {
        return luaL_error(L, "%s: colour is required", kFn);
    }
    lua_pop(L, 1);
    render_colour_at(L, 1, kFn, "colour", spec.colour);
    spec.blend = render_option(L, kFn, "blend", kRenderBlends, 0) - 1;
    spec.layer = render_option(L, kFn, "layer", kLayers, spec.layer);
    if (lua_getfield(L, 1, "tpage") != LUA_TNIL) {
        spec.tpage = render_int(L, -1, kFn, "tpage", 0, 0xFFFF);
    }
    lua_pop(L, 1);
    if (const int t = lua_getfield(L, 1, "dither"); t != LUA_TNIL) {
        if (t != LUA_TBOOLEAN) {
            return luaL_error(L, "%s: dither must be true or false", kFn);
        }
        spec.dither = lua_toboolean(L, -1) ? 1 : 0;
    }
    lua_pop(L, 1);
    char err[160] = {};
    if (render_tile(&spec, err, static_cast<int>(sizeof(err))) != 0) {
        return luaL_error(L, "%s: %s", kFn, err[0] != '\0' ? err : "refused");
    }
    return 0;
}

// sh.render.fade_in_level() -> 4096 when the screen is fully faded in, 0 when faded out.
int l_render_fade_in_level(lua_State* L) {
    REQUIRE_API(render_fade_in_level, "sh.render.fade_in_level")
    lua_pushinteger(L, render_fade_in_level());
    return 1;
}

/* sh.render.sprite{texture=, rect=, uv=, tint=, blend=, layer=} or
 * sh.render.sprite{texture=, uv=, sprites={{rect=[, uv=]}, ...}, tint=, blend=, layer=}:
 * textured rectangles drawn 1:1 from a named texture; the list form shares one texture page
 * and the call's uv is the default of its items (an empty list is valid: the renderer may
 * still set the page up from that uv, as the original code did). */
constexpr int kMaxSprites = 64;

// uv = {u, v} of the table at `idx`; false when absent.
bool render_uv(lua_State* L, int idx, const char* fn, int* u, int* v) {
    const int type = lua_getfield(L, idx, "uv");
    if (type == LUA_TNIL) {
        lua_pop(L, 1);
        return false;
    }
    if (type != LUA_TTABLE) {
        luaL_error(L, "%s: uv must be {u, v}", fn);
        return false;
    }
    lua_geti(L, -1, 1);
    *u = render_int(L, -1, fn, "uv u", 0, 1023);
    lua_pop(L, 1);
    lua_geti(L, -1, 2);
    *v = render_int(L, -1, fn, "uv v", 0, 511);
    lua_pop(L, 2);
    return true;
}

// rect = {x, y, w, h} of the table at `idx`.
void render_rect(lua_State* L, int idx, const char* fn, SpriteRect* out) {
    if (lua_getfield(L, idx, "rect") != LUA_TTABLE) {
        luaL_error(L, "%s: rect = {x, y, w, h} is required", fn);
        return;
    }
    static const char* const kRect[] = {"rect x", "rect y", "rect w", "rect h"};
    int r[4] = {};
    for (int i = 0; i < 4; ++i) {
        lua_geti(L, -1, i + 1);
        r[i] = i < 2 ? render_int(L, -1, fn, kRect[i], -1024, 1023) : render_int(L, -1, fn, kRect[i], 0, 256);
        lua_pop(L, 1);
    }
    lua_pop(L, 1);
    out->x = r[0];
    out->y = r[1];
    out->w = r[2];
    out->h = r[3];
}

int l_render_sprite(lua_State* L) {
    REQUIRE_API(render_sprites, "sh.render.sprite")
    static const char* const kFn = "sh.render.sprite";
    static const char* const kKeys[] = {"texture", "rect", "uv", "sprites", "tint", "blend", "layer", nullptr};
    static const char* const kItemKeys[] = {"rect", "uv", "tint", "blend", "clut", "tpage", nullptr};
    static const char* const kTextures[] = {"background", nullptr};
    static const char* const kBlends[] = {"none", "half", "add", "sub", "quarter", nullptr};
    static const char* const kLayers[] = {"world", "overlay", "picture", nullptr};
    luaL_checktype(L, 1, LUA_TTABLE);
    render_check_keys(L, 1, kFn, kKeys);
    Sprites spec;
    SpriteRect items[kMaxSprites] = {};
    spec.texture = render_option(L, kFn, "texture", kTextures, -1);
    if (spec.texture < 0) {
        return luaL_error(L, "%s: texture is required", kFn);
    }
    spec.layer = render_option(L, kFn, "layer", kLayers, spec.layer);
    spec.blend = render_option(L, kFn, "blend", kBlends, 0) - 1;
    render_tint(L, kFn, spec.tint);
    const bool hasUv = render_uv(L, 1, kFn, &spec.u, &spec.v);
    if (lua_getfield(L, 1, "sprites") != LUA_TNIL) {
        luaL_checktype(L, -1, LUA_TTABLE);
        const int list = lua_gettop(L);
        if (lua_getfield(L, 1, "rect") != LUA_TNIL) {
            return luaL_error(L, "%s: give either rect or sprites, not both", kFn);
        }
        lua_pop(L, 1);
        const lua_Integer n = luaL_len(L, list);
        if (n > kMaxSprites) {
            return luaL_error(L, "%s: %d sprites, at most %d per call", kFn, static_cast<int>(n), kMaxSprites);
        }
        for (lua_Integer i = 1; i <= n; ++i) {
            if (lua_geti(L, list, i) != LUA_TTABLE) {
                return luaL_error(L, "%s: sprites[%d] must be a table {rect=[, uv=]}", kFn, static_cast<int>(i));
            }
            const int item = lua_gettop(L);
            render_check_keys(L, item, kFn, kItemKeys);
            SpriteRect& r = items[i - 1];
            render_rect(L, item, kFn, &r);
            render_colour_at(L, item, kFn, "tint", r.tint);
            if (lua_getfield(L, item, "blend") != LUA_TNIL) {
                const char* name = luaL_checkstring(L, -1);
                int found = -1;
                for (int b = 0; kBlends[b] != nullptr; ++b) {
                    if (std::strcmp(kBlends[b], name) == 0) {
                        found = b;
                    }
                }
                if (found < 0) {
                    return luaL_error(L, "%s: sprites[%d] blend '%s' is unknown", kFn, static_cast<int>(i), name);
                }
                r.blend = found - 1;
            }
            lua_pop(L, 1);
            if (lua_getfield(L, item, "tpage") != LUA_TNIL) {
                r.tpage = render_int(L, -1, kFn, "tpage", 0, 0xFFFF);
            }
            lua_pop(L, 1);
            if (lua_getfield(L, item, "clut") != LUA_TNIL) {
                r.clut = render_int(L, -1, kFn, "clut", 0, 0xFFFF);
            }
            lua_pop(L, 1);
            if ((r.tpage >= 0) != (r.clut >= 0)) {
                return luaL_error(L, "%s: sprites[%d] needs both tpage and clut, or neither", kFn,
                                  static_cast<int>(i));
            }
            if (!render_uv(L, item, kFn, &r.u, &r.v)) {
                if (!hasUv) {
                    return luaL_error(L, "%s: sprites[%d] has no uv and the call gives none", kFn, static_cast<int>(i));
                }
                r.u = spec.u;
                r.v = spec.v;
            }
            lua_pop(L, 1);
        }
        spec.count = static_cast<int>(n);
    } else {
        if (!hasUv) {
            return luaL_error(L, "%s: uv = {u, v} is required", kFn);
        }
        render_rect(L, 1, kFn, &items[0]);
        items[0].u = spec.u;
        items[0].v = spec.v;
        spec.count = 1;
    }
    lua_pop(L, 1);
    spec.items = items;
    char err[160] = {};
    if (render_sprites(&spec, err, static_cast<int>(sizeof(err))) != 0) {
        return luaL_error(L, "%s: %s", kFn, err[0] != '\0' ? err : "refused");
    }
    return 0;
}

/* sh.render.world_quads{points=, quads=, texture=, blend=, depth_bias=, clip=} -> quads drawn:
 * textured quads at world positions (sh_script.h, ShRenderWorldQuads). points is a flat list
 * x1, y1, z1, x2, ... in world units; a quad names its corners by 1-based point numbers. */
constexpr int kMaxWorldPoints = 256;
constexpr int kMaxWorldQuads = 128;

// {a, b} of the field `key` of the table at `idx`, each in lo..hi; false when absent.
bool render_pair_at(lua_State* L, int idx, const char* fn, const char* key, int lo, int hi, int out[2]) {
    const int type = lua_getfield(L, idx, key);
    if (type == LUA_TNIL) {
        lua_pop(L, 1);
        return false;
    }
    if (type != LUA_TTABLE || luaL_len(L, -1) != 2) {
        luaL_error(L, "%s: %s must be a pair {a, b}", fn, key);
        return false;
    }
    for (int i = 0; i < 2; ++i) {
        lua_geti(L, -1, i + 1);
        out[i] = render_int(L, -1, fn, key, lo, hi);
        lua_pop(L, 1);
    }
    lua_pop(L, 1);
    return true;
}

// A list of exactly n integers lo..hi of the field `key`; false when absent.
bool render_list_at(lua_State* L, int idx, const char* fn, const char* key, int n, int lo, int hi, int* out) {
    const int type = lua_getfield(L, idx, key);
    if (type == LUA_TNIL) {
        lua_pop(L, 1);
        return false;
    }
    if (type != LUA_TTABLE || luaL_len(L, -1) != n) {
        luaL_error(L, "%s: %s must be a list of %d integers", fn, key, n);
        return false;
    }
    for (int i = 0; i < n; ++i) {
        lua_geti(L, -1, i + 1);
        out[i] = render_int(L, -1, fn, key, lo, hi);
        lua_pop(L, 1);
    }
    lua_pop(L, 1);
    return true;
}

// "average" / "all" (-1) or a 1-based corner number (0..3) of the field `key`.
int render_corner_choice(lua_State* L, int idx, const char* fn, const char* key, const char* all) {
    const int type = lua_getfield(L, idx, key);
    int value = -1;
    if (type == LUA_TSTRING) {
        if (std::strcmp(lua_tostring(L, -1), all) != 0) {
            return luaL_error(L, "%s: %s must be \"%s\" or a corner 1..4", fn, key, all);
        }
    } else if (type != LUA_TNIL) {
        value = render_int(L, -1, fn, key, 1, 4) - 1;
    }
    lua_pop(L, 1);
    return value;
}

int l_render_world_quads(lua_State* L) {
    REQUIRE_API(render_world_quads, "sh.render.world_quads")
    static const char* const kFn = "sh.render.world_quads";
    static const char* const kKeys[] = {"points", "quads", "texture", "blend", "depth_bias", "clip", nullptr};
    static const char* const kTextureKeys[] = {"page", "palette", "colours", nullptr};
    static const char* const kQuadKeys[] = {"corners", "uv",          "tint", "depth_test", "screen_test",
                                            "precise", "sort_points", nullptr};
    luaL_checktype(L, 1, LUA_TTABLE);
    render_check_keys(L, 1, kFn, kKeys);
    WorldQuads spec;
    int points[kMaxWorldPoints * 3] = {};
    WorldQuad quads[kMaxWorldQuads];

    if (lua_getfield(L, 1, "points") != LUA_TTABLE) {
        return luaL_error(L, "%s: points = {x1, y1, z1, ...} is required", kFn);
    }
    const lua_Integer values = luaL_len(L, -1);
    if (values % 3 != 0 || values / 3 > kMaxWorldPoints) {
        return luaL_error(L, "%s: points must hold x, y, z triples, at most %d points", kFn, kMaxWorldPoints);
    }
    for (lua_Integer i = 0; i < values; ++i) {
        lua_geti(L, -1, i + 1);
        points[i] = render_int(L, -1, kFn, "point coordinate", -0x7FFFFFFF, 0x7FFFFFFF);
        lua_pop(L, 1);
    }
    lua_pop(L, 1);
    spec.point_count = static_cast<int>(values / 3);

    if (lua_getfield(L, 1, "texture") != LUA_TTABLE) {
        return luaL_error(L, "%s: texture = {page = {x, y}, palette = {x, y}, colours = 16} is required", kFn);
    }
    {
        const int tex = lua_gettop(L);
        render_check_keys(L, tex, kFn, kTextureKeys);
        int page[2] = {};
        if (!render_pair_at(L, tex, kFn, "page", 0, 1023, page)) {
            return luaL_error(L, "%s: texture.page = {x, y} is required", kFn);
        }
        spec.page_x = page[0];
        spec.page_y = page[1];
        const int colourType = lua_getfield(L, tex, "colours");
        if (colourType == LUA_TSTRING && std::strcmp(lua_tostring(L, -1), "direct") == 0) {
            spec.colours = 2;
        } else if (colourType == LUA_TNUMBER && lua_isinteger(L, -1) &&
                   (lua_tointeger(L, -1) == 16 || lua_tointeger(L, -1) == 256)) {
            spec.colours = lua_tointeger(L, -1) == 16 ? 0 : 1;
        } else {
            return luaL_error(L, "%s: texture.colours must be 16, 256 or \"direct\"", kFn);
        }
        lua_pop(L, 1);
        int palette[2] = {};
        if (render_pair_at(L, tex, kFn, "palette", 0, 1023, palette)) {
            spec.palette_x = palette[0];
            spec.palette_y = palette[1];
        } else if (spec.colours != 2) {
            return luaL_error(L, "%s: texture.palette = {x, y} is required for a palette texture", kFn);
        }
    }
    lua_pop(L, 1);
    spec.blend = render_option(L, kFn, "blend", kRenderBlends, 0) - 1;
    if (lua_getfield(L, 1, "depth_bias") != LUA_TNIL) {
        spec.depth_bias = render_int(L, -1, kFn, "depth_bias", -0xFFFF, 0xFFFF);
    }
    lua_pop(L, 1);
    int clip[2] = {};
    if (render_pair_at(L, 1, kFn, "clip", 0, 1024, clip)) {
        spec.clip_x = clip[0];
        spec.clip_y = clip[1];
    }

    if (lua_getfield(L, 1, "quads") != LUA_TTABLE) {
        return luaL_error(L, "%s: quads = {{corners=, uv=}, ...} is required", kFn);
    }
    const int list = lua_gettop(L);
    const lua_Integer n = luaL_len(L, list);
    if (n > kMaxWorldQuads) {
        return luaL_error(L, "%s: %d quads, at most %d per call", kFn, static_cast<int>(n), kMaxWorldQuads);
    }
    for (lua_Integer i = 1; i <= n; ++i) {
        if (lua_geti(L, list, i) != LUA_TTABLE) {
            return luaL_error(L, "%s: quads[%d] must be a table {corners=, uv=}", kFn, static_cast<int>(i));
        }
        const int item = lua_gettop(L);
        render_check_keys(L, item, kFn, kQuadKeys);
        WorldQuad& q = quads[i - 1];
        if (!render_list_at(L, item, kFn, "corners", 4, 1, spec.point_count, q.corners)) {
            return luaL_error(L, "%s: quads[%d].corners = {p1, p2, p3, p4} is required", kFn, static_cast<int>(i));
        }
        if (!render_list_at(L, item, kFn, "uv", 8, 0, 255, q.uv)) {
            return luaL_error(L, "%s: quads[%d].uv = {u1, v1, ..., u4, v4} is required", kFn, static_cast<int>(i));
        }
        for (int& c : q.corners) {
            --c;
        }
        for (int k = 0; k < 4; ++k) {
            q.sort_points[k] = q.corners[k];
        }
        if (render_list_at(L, item, kFn, "sort_points", 4, 1, spec.point_count, q.sort_points)) {
            for (int& p : q.sort_points) {
                --p;
            }
        }
        render_colour_at(L, item, kFn, "tint", q.tint);
        q.depth_test = render_corner_choice(L, item, kFn, "depth_test", "average");
        q.screen_test = render_corner_choice(L, item, kFn, "screen_test", "all");
        const int preciseType = lua_getfield(L, item, "precise");
        if (preciseType == LUA_TBOOLEAN) {
            q.precise = lua_toboolean(L, -1) ? 0xF : 0;
        } else if (preciseType == LUA_TTABLE) {
            q.precise = 0;
            const lua_Integer m = luaL_len(L, -1);
            for (lua_Integer k = 1; k <= m; ++k) {
                lua_geti(L, -1, k);
                q.precise |= 1 << (render_int(L, -1, kFn, "precise corner", 1, 4) - 1);
                lua_pop(L, 1);
            }
        } else if (preciseType != LUA_TNIL) {
            return luaL_error(L, "%s: quads[%d].precise must be true, false or a list of corners", kFn,
                              static_cast<int>(i));
        }
        lua_pop(L, 2);
    }
    lua_pop(L, 1);
    spec.points = points;
    spec.quad_count = static_cast<int>(n);
    spec.quads = quads;
    char err[160] = {};
    const int drawn = render_world_quads(&spec, err, static_cast<int>(sizeof(err)));
    if (drawn < 0) {
        return luaL_error(L, "%s: %s", kFn, err[0] != '\0' ? err : "refused");
    }
    lua_pushinteger(L, drawn);
    return 1;
}

/* sh.render.texture_load{file=, texture=, buffer=}: file is a game file name ("TIM/3X3DOR.TIM")
 * or number; texture as sh.render.sprite (required); buffer "file" (default) or "image". */
int l_render_texture_load(lua_State* L) {
    REQUIRE_API(render_texture_load, "sh.render.texture_load")
    static const char* const kFn = "sh.render.texture_load";
    static const char* const kKeys[] = {"file", "texture", "buffer", nullptr};
    static const char* const kTextures[] = {"background", "paper_map", nullptr};
    static const char* const kBuffers[] = {"file", "image", "paper_map", nullptr};
    luaL_checktype(L, 1, LUA_TTABLE);
    render_check_keys(L, 1, kFn, kKeys);
    TextureLoad spec;
    if (lua_getfield(L, 1, "file") == LUA_TNIL) {
        return luaL_error(L, "%s: file is required", kFn);
    }
    spec.file = file_arg(L, lua_gettop(L));
    lua_pop(L, 1);
    spec.texture = render_option(L, kFn, "texture", kTextures, -1);
    if (spec.texture < 0) {
        return luaL_error(L, "%s: texture is required", kFn);
    }
    spec.buffer = render_option(L, kFn, "buffer", kBuffers, spec.buffer);
    char err[160] = {};
    if (render_texture_load(&spec, err, static_cast<int>(sizeof(err))) != 0) {
        return luaL_error(L, "%s: %s", kFn, err[0] != '\0' ? err : "refused");
    }
    return 0;
}

/* render (screen): what a scene that covers the whole screen with a picture asks of the
 * display. sh.render.vram_save{rect = {x, y, w, h}, buffer =} / vram_restore{...}: a VRAM
 * rectangle into a named RAM buffer and back; buffer "screen_backup" (default) or
 * "screen_backup_alt". */
int render_vram_copy(lua_State* L, const char* fn, int (*copy)(const VramCopy*, char*, int)) {
    static const char* const kKeys[] = {"rect", "buffer", nullptr};
    static const char* const kBuffers[] = {"screen_backup", "screen_backup_alt", nullptr};
    luaL_checktype(L, 1, LUA_TTABLE);
    render_check_keys(L, 1, fn, kKeys);
    if (lua_getfield(L, 1, "rect") != LUA_TTABLE) {
        return luaL_error(L, "%s: rect = {x, y, w, h} is required", fn);
    }
    static const char* const kRect[] = {"rect x", "rect y", "rect w", "rect h"};
    static const int kHi[] = {1023, 511, 1024, 512};
    int r[4] = {};
    for (int i = 0; i < 4; ++i) {
        lua_geti(L, -1, i + 1);
        r[i] = render_int(L, -1, fn, kRect[i], i < 2 ? 0 : 1, kHi[i]);
        lua_pop(L, 1);
    }
    lua_pop(L, 1);
    if (r[0] + r[2] > 1024 || r[1] + r[3] > 512) {
        return luaL_error(L, "%s: rect leaves VRAM (1024x512)", fn);
    }
    VramCopy spec;
    spec.x = r[0];
    spec.y = r[1];
    spec.w = r[2];
    spec.h = r[3];
    spec.buffer = render_option(L, fn, "buffer", kBuffers, spec.buffer);
    char err[160] = {};
    if (copy(&spec, err, static_cast<int>(sizeof(err))) != 0) {
        return luaL_error(L, "%s: %s", fn, err[0] != '\0' ? err : "refused");
    }
    return 0;
}

int l_render_vram_save(lua_State* L) {
    REQUIRE_API(render_vram_save, "sh.render.vram_save")
    return render_vram_copy(L, "sh.render.vram_save", render_vram_save);
}

int l_render_vram_restore(lua_State* L) {
    REQUIRE_API(render_vram_restore, "sh.render.vram_restore")
    return render_vram_copy(L, "sh.render.vram_restore", render_vram_restore);
}

// sh.render.display_init{width = 320, interlaced = false}: the display is set up again.
int l_render_display_init(lua_State* L) {
    REQUIRE_API(render_display_init, "sh.render.display_init")
    static const char* const kFn = "sh.render.display_init";
    static const char* const kKeys[] = {"width", "interlaced", nullptr};
    static const int kWidths[] = {256, 320, 368, 512, 640};
    luaL_checktype(L, 1, LUA_TTABLE);
    render_check_keys(L, 1, kFn, kKeys);
    int width = 320;
    if (lua_getfield(L, 1, "width") != LUA_TNIL) {
        width = render_int(L, -1, kFn, "width", 256, 640);
        bool known = false;
        for (int w : kWidths) {
            known = known || w == width;
        }
        if (!known) {
            return luaL_error(L, "%s: width %d is not a display mode (256, 320, 368, 512, 640)", kFn, width);
        }
    }
    lua_pop(L, 1);
    lua_getfield(L, 1, "interlaced");
    const int interlaced = lua_toboolean(L, -1) ? 1 : 0;
    lua_pop(L, 1);
    char err[160] = {};
    if (render_display_init(width, interlaced, err, static_cast<int>(sizeof(err))) != 0) {
        return luaL_error(L, "%s: %s", kFn, err[0] != '\0' ? err : "refused");
    }
    return 0;
}

// sh.render.display_swap(): the shown and the drawn buffer swap now.
int l_render_display_swap(lua_State* L) {
    REQUIRE_API(render_display_swap, "sh.render.display_swap")
    render_display_swap();
    return 0;
}

// sh.render.vblank_interval(n): a frame lasts at least n vertical blanks (1 = 60 fps).
int l_render_vblank_interval(lua_State* L) {
    REQUIRE_API(render_vblank_interval, "sh.render.vblank_interval")
    render_vblank_interval(render_int(L, 1, "sh.render.vblank_interval", "n", 1, 8));
    return 0;
}

// sh.render.picture_screen(on): the PC presenter treats the frames as a flat full-screen
// picture (4:3 framing, black side bars) until switched off.
int l_render_picture_screen(lua_State* L) {
    REQUIRE_API(render_picture_screen, "sh.render.picture_screen")
    luaL_checkany(L, 1);
    render_picture_screen(lua_toboolean(L, 1) ? 1 : 0);
    return 0;
}

// sh.render.background{texture = "paper_map"}: the named picture fills the screen this frame.
int l_render_background(lua_State* L) {
    REQUIRE_API(render_background, "sh.render.background")
    static const char* const kFn = "sh.render.background";
    static const char* const kKeys[] = {"texture", nullptr};
    static const char* const kTextures[] = {"background", "paper_map", nullptr};
    luaL_checktype(L, 1, LUA_TTABLE);
    render_check_keys(L, 1, kFn, kKeys);
    const int texture = render_option(L, kFn, "texture", kTextures, -1);
    if (texture < 0) {
        return luaL_error(L, "%s: texture is required", kFn);
    }
    char err[160] = {};
    if (render_background(texture, err, static_cast<int>(sizeof(err))) != 0) {
        return luaL_error(L, "%s: %s", kFn, err[0] != '\0' ? err : "refused");
    }
    return 0;
}

// sh.paper_map_grant(name): the paper map page counts as found.
int l_paper_map_grant(lua_State* L) {
    REQUIRE_API(paper_map_grant, "sh.paper_map_grant")
    paper_map_grant(paper_map_arg(L, 1));
    return 0;
}

#undef REQUIRE_API

// ---- coroutines: sh.spawn(fn) + wait / wait_frames / wait_until -----------------------------------

int l_spawn(lua_State* L) {
    Host* host = host_of(L);
    luaL_checktype(L, 1, LUA_TFUNCTION);
    if (host->coroutine_count() >= Host::kMaxCoroutines) {
        luaL_error(L, "sh.spawn: too many live coroutines (at most %d)", (int)Host::kMaxCoroutines);
        return 0;
    }
    lua_State* thread = lua_newthread(L);  // stack: fn, thread (the thread refs the fn's state)
    // Hooks do not carry over to new threads; without this a runaway coroutine
    // would loop outside the instruction budget.
    lua_sethook(thread, &Host::count_hook, LUA_MASKCOUNT, 1000);
    lua_pushvalue(L, 1);                   // copy of the fn
    lua_xmove(L, thread, 1);               // the copy becomes the thread's pending call
    const int ref = luaL_ref(L, LUA_REGISTRYINDEX);  // refs the thread, pops it
    host->add_coroutine(ref, 1);
    return 0;
}

// wait(n) / wait_frames(n): n update ticks. Inside a coroutine only: the yield crosses
// the host's resume boundary (outside one, Lua's own "attempt to yield across a
// C-call boundary" error fires and the hook is dropped as usual).
int l_wait_frames(lua_State* L) {
    const lua_Integer n = luaL_optinteger(L, 1, 1);
    lua_pushinteger(L, n <= 0 ? 0 : n);
    return lua_yield(L, 1);
}

int l_wait(lua_State* L) {
    Host* host = host_of(L);
    const double sec = luaL_optnumber(L, 1, 0.0);
    const double frames = sec * host->updates_per_second();
    lua_pushinteger(L, frames <= 0.5 ? 0 : static_cast<lua_Integer>(frames + 0.5));
    return lua_yield(L, 1);
}

// wait_until(fn): fn (a plain function, not the coroutine) is called once per tick
// under the instruction budget until it returns truthy.
int l_wait_until(lua_State* L) {
    luaL_checktype(L, 1, LUA_TFUNCTION);
    lua_settop(L, 1);
    return lua_yield(L, 1);
}

/* The generic bridge. Upvalue 1 = host, upvalue 2 = the bind name, or nil for
 * sh.call(fn, ...) where the first argument names the function. Integers, booleans, nil
 * and strings are passed through; the game side resolves handle strings. */
using BridgeFn = int (*)(const char*, int, const CallArg*, long long*, int, char*, int);
int bridge_call(lua_State* L, BridgeFn evb_call, const char* name, int first);

int l_evb(lua_State* L) {
    const auto evb_call = host_of(L)->game_api().evb_call;
    if (evb_call == nullptr) {
        return luaL_error(L, "sh.call is not available");
    }
    int first = 1;
    const char* name = lua_tostring(L, lua_upvalueindex(2));
    if (name == nullptr) {
        name = luaL_checkstring(L, 1);
        first = 2;
    }
    return bridge_call(L, evb_call, name, first);
}

/* sh.<group>.<name>(...); upvalue 2 = "<group>.<name>". */
int l_named(lua_State* L) {
    const auto named_call = host_of(L)->game_api().named_call;
    const char* name = lua_tostring(L, lua_upvalueindex(2));
    if (named_call == nullptr) {
        return luaL_error(L, "sh.%s is not available", name);
    }
    return bridge_call(L, named_call, name, 1);
}

// Arguments of the named functions that are names in Lua (an event flag, a message key, a
// sound) and numbers to the game: (function, 0-based argument, kind).
enum class NamedArg { Flag, Msg, Sfx, Chara, File, State };
struct NamedArgSpec {
    const char* fn;
    int arg;
    NamedArg kind;
};
constexpr NamedArgSpec kNamedArgs[] = {
    {"event.flag", 0, NamedArg::Flag},
    {"event.flag_set", 0, NamedArg::Flag},
    {"event.flag_clear", 0, NamedArg::Flag},
    {"event.branch_on_flag", 0, NamedArg::Flag},
    {"msg.show", 1, NamedArg::Msg},
    {"msg.show_with_audio", 0, NamedArg::Msg},
    {"msg.show_dimmed", 3, NamedArg::Msg},
    {"msg.show_dimmed", 4, NamedArg::Msg},
    {"msg.show_with_sfx", 0, NamedArg::Msg},
    {"msg.show_with_sfx", 1, NamedArg::Sfx},
    {"msg.play_with_audio", 0, NamedArg::Msg},
    {"msg.selection", 1, NamedArg::Msg},
    {"msg.draw", 0, NamedArg::Msg},
    {"npc.spawn", 0, NamedArg::Chara},
    {"npc.load", 1, NamedArg::Chara},
    {"npc.set_chara_id", 1, NamedArg::Chara},
    // A cutscene state of the character of the slot (arg 0 of these functions).
    // (npc.set_anim_status is NOT here: it writes the packed anim status, not the state.)
    {"npc.anim_cmd", 2, NamedArg::State},
    {"npc.anim_play_to_end", 1, NamedArg::State},
    {"npc.path_walk", 1, NamedArg::State},
    {"npc.control_request", 1, NamedArg::State},
    {"event.bg_texture", 1, NamedArg::File},
    {"event.bg_texture_load_fade_in", 0, NamedArg::File},
    {"load.file", 0, NamedArg::File},
    {"load.tim", 0, NamedArg::File},
    {"load.loading_screen_image", 0, NamedArg::File},  // engine/scripting/binds/binds_chara.cpp
    {"load.tim_at", 0, NamedArg::File},  // binds_chara.cpp
    {"chara.update_variant_set", 0, NamedArg::Chara},
    {"chara.anim_data_alloc", 1, NamedArg::Chara},  // binds_chara.cpp
    {"chara.model_load", 0, NamedArg::Chara},
    {"chara.anim_file_set", 0, NamedArg::Chara},
    {"chara.anim_file_set", 1, NamedArg::File},
    {"chara.update_as", 0, NamedArg::Chara},
    {"chara.update_as", 1, NamedArg::Chara},
    {"npc.record_reset", 1, NamedArg::Chara},
    {"item_view.read", 0, NamedArg::File},
    {"item_view.read", 1, NamedArg::File},
};

const NamedArgSpec* named_arg(const char* fn, int arg) {
    for (const NamedArgSpec& spec : kNamedArgs) {
        if (spec.arg == arg && std::strcmp(spec.fn, fn) == 0) {
            return &spec;
        }
    }
    return nullptr;
}

int bridge_call(lua_State* L, BridgeFn evb_call, const char* name, int first) {
    constexpr int kMaxArgs = 16;
    CallArg args[kMaxArgs] = {};
    const int top = lua_gettop(L);
    int argc = 0;
    for (int i = first; i <= top; ++i) {
        if (argc >= kMaxArgs) {
            return luaL_error(L, "sh.%s: too many arguments", name);
        }
        CallArg& a = args[argc++];
        switch (lua_type(L, i)) {
            case LUA_TNIL:
            case LUA_TNONE:
                a.kind = 0;
                break;
            case LUA_TBOOLEAN:
                a.kind = 3;
                a.i = lua_toboolean(L, i);
                break;
            case LUA_TNUMBER:
                if (!lua_isinteger(L, i)) {
                    return luaL_error(L, "sh.%s: argument %d is not an integer", name, i);
                }
                a.kind = 1;
                a.i = lua_tointeger(L, i);
                break;
            case LUA_TSTRING:
                a.kind = 2;
                a.s = lua_tostring(L, i);
                if (const NamedArgSpec* spec = named_arg(name, i - first)) {
                    a.kind = 1;
                    a.i = spec->kind == NamedArg::Flag  ? flag_arg(L, i)
                        : spec->kind == NamedArg::Msg   ? msg_arg(L, i)
                        : spec->kind == NamedArg::Chara ? chara_id_arg(L, i)
                        : spec->kind == NamedArg::File  ? file_arg(L, i)
                        : spec->kind == NamedArg::State ? state_arg(L, args[0].kind == 1
                                                                                        ? static_cast<int>(args[0].i)
                                                                                        : 0,
                                                                    i)
                                                        : sound_arg(L, i);
                    break;
                }
                // sh.sound.*: the sound argument may be a registry name ("door_locked").
                if (i == first && std::strncmp(name, "sound.", 6) == 0) {
                    const int id = sound_id_find(L, a.s);
                    if (id >= 0) {
                        a.kind = 1;
                        a.i = id;
                    }
                }
                break;
            default:
                return luaL_error(L, "sh.%s: argument %d has an unsupported type", name, i);
        }
    }
    long long out[8] = {};
    char err[192] = {};
    int n = -1;
    {
        // The names the game side resolves itself (items of the named API) are the calling mod's first. The scope
        // closes before any Lua error: a longjmp would skip its destructor.
        const sh::ids::NamespaceScope names(caller_mod(L));
        n = evb_call(name, argc, args, out, 8, err, static_cast<int>(sizeof(err)));
    }
    if (n < 0) {
        return luaL_error(L, "sh.%s: %s", name, err[0] != '\0' ? err : "failed");
    }
    for (int i = 0; i < n; ++i) {
        lua_pushinteger(L, static_cast<lua_Integer>(out[i]));
    }
    return n;
}

const char* const kEvbFuncs[] = {
    "ovl_get", "ovl_set", "chara_get", "chara_set", "sys_get", "sys_set",
    "vec_get", "vec_set", "svec_get", "svec_set", "dms_chara", "dms_transform",
    "dms_camera", "dms_light", "event_step_set_full", "struct_offset", nullptr,
};

/* The symbol bridge (overlay globals and functions by symbol): like the generic
 * bridge, only with script_raw_api = 1; scripts use sh.map.get/set and sh.map.<fn>. */
const luaL_Reg kEvaRawFuncs[] = {
    {nullptr, nullptr},
};

const luaL_Reg kShFuncs[] = {
    {"log",            l_log},
    {"api_version",    l_api_version},
    {"give_item",      l_give_item},
    {"item_count",     l_item_count},
    {"has_item",       l_has_item},
    {"take_item",      l_take_item},
    {"map_name",       l_map_name},
    {"msg_draw",       l_msg_draw},
    {"fade",           l_fade_start},
    {"fade_done",      l_fade_done},
    {"map_event_set",  l_map_event_set},
    {"event_point",    l_event_point},
    {"event_item_take",        l_event_item_take},
    {"event_common_item_take", l_event_common_item_take},
    {"state_set",      l_state_set},
    {"event_msg",      l_event_msg},
    {"event_msg_with_sfx",   l_event_msg_with_sfx},
    {"event_msg_with_bg",    l_event_msg_with_bg},
    {"event_paper_map_take", l_event_paper_map_take},
    {"input_action",   l_input_action},
    {"input_axis",     l_input_axis},
    {"event_msg_with_texture", l_event_msg_with_texture},
    {"background_gamma", l_background_gamma},
    {"event_wait_timer", l_event_wait_timer},
    {"event_step_get",   l_event_step_get},
    {"event_step_set",   l_event_step_set},
    {"event_step_increment", l_event_step_increment},
    {"event_fade_cmd",   l_event_fade_cmd},
    {"event_selection",  l_event_selection},
    {"event_waypoint_set",   l_event_waypoint_set},
    {"event_waypoint_player", l_event_waypoint_player},
    {"chara_load",      l_chara_load},
    {"chara_process_loads", l_chara_process_loads},
    {"event_chara_anim", l_event_chara_anim},
    {"fs_read_dms",     l_fs_read_dms},
    {"fs_queue_done",   l_fs_queue_done},
    {"dms_ready",       l_dms_ready},
    {"cutscene_border", l_cutscene_border},
    {"cutscene_flag",   l_cutscene_flag},
    {"overlay_screen_set",  l_overlay_screen_set},
    {"overlay_screen_show", l_overlay_screen_show},
    {"overlay_screen_hide", l_overlay_screen_hide},
    {"pad_effect",      l_pad_effect},
    {"selection_silent", l_selection_silent},
    {"bg_img_transition", l_bg_img_transition},
    {"state_timer_get",   l_state_timer_get},
    {"state_timer_set",   l_state_timer_set},
    {"event_step_reset",  l_event_step_reset},
    {"camera_auto_return", l_camera_auto_return},
    {"player_fall_tick",   l_player_fall_tick},
    {"delta_time_get",    l_delta_time_get},
    {"world_object_add",  l_world_object_add},
    {"event_bg_texture_cmd", l_event_bg_texture_cmd},
    {"event_inv_item_cmd",  l_event_inv_item_cmd},
    {"pickup_animate",     l_pickup_animate},
    {"chara_anim_play_to_end", l_chara_anim_play_to_end},
    {"chara_slot_transform_get", l_chara_slot_transform_get},
    {"chara_slot_transform_set", l_chara_slot_transform_set},
    {"chara_slot_chara_id_get", l_chara_slot_chara_id_get},
    {"chara_slot_control_set", l_chara_slot_control_set},
    {"event_msg_with_audio", l_event_msg_with_audio},
    {"event_activation_type_get", l_event_activation_type_get},
    {"math_sin",         l_math_sin},
    {"math_cos",         l_math_cos},
    {"event_tween_linear", l_event_tween_linear},
    {"event_tween_reset",  l_event_tween_reset},
    {"delta_time_set",     l_delta_time_set},
    {"chara_slot_anim_flags_set", l_chara_slot_anim_flags_set},
    {"chara_slot_anim_flags_clear", l_chara_slot_anim_flags_clear},
    {"water_effect_enable",  l_water_effect_enable},
    {"water_effect_disable", l_water_effect_disable},
    {"event_sys_state_branch_on_flag", l_event_sys_state_branch_on_flag},
    {"held_item_show", l_held_item_show},
    {"held_item_hide", l_held_item_hide},
    {"world_object_pos_get", l_world_object_pos_get},
    {"world_object_pos_set", l_world_object_pos_set},
    {"world_object_rot_y_set", l_world_object_rot_y_set},
    {"held_item_attach", l_held_item_attach},
    {"held_item_set", l_held_item_set},
    {"held_item_prev", l_held_item_prev},
    {"player_weapon_attack_set", l_player_weapon_attack_set},
    {"player_weapon_attack_get", l_player_weapon_attack_get},
    {"math_atan2", l_math_atan2},
    {"event_bg_texture_fade_in", l_event_bg_texture_fade_in},
    {"event_bg_texture_fade_out", l_event_bg_texture_fade_out},
    {"screen_fade_finished", l_screen_fade_finished},
    {"world_object_rot_y_get", l_world_object_rot_y_get},
    {"player_move_speed_clear", l_player_move_speed_clear},
    {"rng_int", l_rng_int},
    {"rng_uint", l_rng_uint},
    {"game_warm_boot", l_game_warm_boot},
    {"screen_fade_timestep_get", l_screen_fade_timestep_get},
    {"screen_fade_timestep_set", l_screen_fade_timestep_set},
    {"player_rotate",   l_player_rotate},
    {"player_rot_y_get", l_player_rot_y_get},
    {"event_wait_player_stop", l_event_wait_player_stop},
    {"event_step_increment_delayed", l_event_step_increment_delayed},
    {"event_step_increment_after_fade", l_event_step_increment_after_fade},
    {"screen_fade_status_set", l_screen_fade_status_set},
    {"game_state_update", l_game_state_update},
    {"delta_time_reset", l_delta_time_reset},
    {"screen_fade_reset_timestep", l_screen_fade_reset_timestep},
    {"player_vel_y_get", l_player_vel_y_get},
    {"player_vel_y_set", l_player_vel_y_set},
    {"chara_id",        l_chara_id},
    {"chara_param",     l_chara_param},
    {"chara_param_q12", l_chara_param_q12},
    {"chara_anim",      l_chara_anim},
    {"chara_state",     l_chara_state},
    {"text",           l_text},
    {"msg_index",      l_msg_index},
    {"fs_read", l_fs_read},
    {"fs_wait", l_fs_wait},
    {"fs_seek", l_fs_seek},
    {"file_number", l_file_number},
    {"dms_fix", l_dms_fix},
    {"dms_chara_apply", l_dms_chara_apply},
    {"dms_camera_apply", l_dms_camera_apply},
    {"vc_cam_target", l_vc_cam_target},
    {"vc_watch_target", l_vc_watch_target},
    {"vc_view_position", l_vc_view_position},
    {"light_intensity_set", l_light_intensity_set},
    {"light_bones_clear", l_light_bones_clear},
    {"light_pos_set", l_light_pos_set},
    {"light_rot_set", l_light_rot_set},
    {"flashlight_attrs_fix", l_flashlight_attrs_fix},
    {"math_vec2_mag_q6", l_math_vec2_mag_q6},
    {"chara_model_clear", l_chara_model_clear},
    {"chara_props_reset", l_chara_props_reset},
    {"player_weapon_stash", l_player_weapon_stash},
    {"player_weapon_restore", l_player_weapon_restore},
    {"map_effects_init", l_map_effects_init},
    {"sys_flags_get", l_sys_flags_get},
    {"sys_flags_set", l_sys_flags_set},
    {"event_msg_audio", l_event_msg_audio},
    {"map_msg_audio", l_map_msg_audio},
    {"event_msg_dimmed_bg", l_event_msg_dimmed_bg},
    {"chara_anim_unlock_play_to_end", l_chara_anim_unlock_play_to_end},
    {"paper_map_state", l_paper_map_state},
    {"paper_map_cmd", l_paper_map_cmd},
    {"paper_map_grant", l_paper_map_grant},
    {"map_screen_vertex_draw", l_map_screen_vertex_draw},
    {"map_screen_marking_draw", l_map_screen_marking_draw},
    {"map_screen_player_mark_draw", l_map_screen_player_mark_draw},
    {"map_box_outline_draw", l_map_box_outline_draw},
    {"vc_stereo_balance", l_vc_stereo_balance},
    {"light_intensity_get", l_light_intensity_get},
    {"chara_field_get", l_chara_get},
    {"chara_field_set", l_chara_set},
    {"chara_anim_start_keyframe_get", l_chara_anim_start_keyframe_get},
    {"math_point_from_angle", l_math_point_from_angle},
    {"chara_bones_init", l_chara_bones_init},
    {"camera_default_speeds", l_camera_default_speeds},
    {"fs_entry_loaded", l_fs_entry_loaded},
    {"map_effects_update", l_map_effects_update},
    {"savegame_get", l_savegame_get},
    {"savegame_set", l_savegame_set},
    {"cursor_draw", l_cursor_draw},
    {"mouse_cursor_puzzle_override", l_mouse_cursor_puzzle_override},
    {"item_remove", l_item_remove},
    {nullptr, nullptr},
};

}  // namespace

Host::Host(HostConfig config) : config_(std::move(config)), alive_(std::make_shared<bool>(true)) {
    static std::atomic<std::uint64_t> serial{0};
    hook_group_ = "host:" + std::to_string(++serial);
}

Host::~Host() {
    *alive_ = false;
    close_state();
}

void Host::close_state() {
    hooks::bus().off_group(hook_group_);
    lua_hooks_.clear();  // the refs die with the state
    if (L_ != nullptr) {
        remove_console_family(this);
        lua_close(L_);
        L_ = nullptr;
    }
}

void Host::warn_once(const std::string& key, const std::string& message) {
    if (warned_.insert(key).second) {
        log(1, message);
    }
}

void Host::log(int level, const std::string& message) const {
    if (config_.log) {
        config_.log(level, "[SCRIPT] " + message);
    } else {
        std::fprintf(stderr, "%s[SCRIPT] %s\n", level ? "warning: " : "", message.c_str());
    }
}

void Host::log_call_error(const std::string& message) {
    if (message == last_call_error_) {
        // The first repeats, then every power of ten, keep the count visible without a line per frame.
        ++call_error_repeats_;
        std::uint64_t n = call_error_repeats_;
        while (n % 10 == 0) {
            n /= 10;
        }
        if (n == 1 && call_error_repeats_ >= 10) {
            log(1, message + " (repeated " + std::to_string(call_error_repeats_) + " times)");
        }
        return;
    }
    if (call_error_repeats_ > 0) {
        log(1, "the previous error repeated " + std::to_string(call_error_repeats_) + " times");
    }
    last_call_error_ = message;
    call_error_repeats_ = 0;
    log(1, message);
}

void Host::open_sandbox() {
    luaL_requiref(L_, LUA_GNAME, luaopen_base, 1);
    luaL_requiref(L_, LUA_TABLIBNAME, luaopen_table, 1);
    luaL_requiref(L_, LUA_STRLIBNAME, luaopen_string, 1);
    luaL_requiref(L_, LUA_MATHLIBNAME, luaopen_math, 1);
    lua_pop(L_, 4);

    // Everything that can leave the sandbox or touch the machine goes away. A
    // text-only script chunk has no use for `load` that the script file itself cannot
    // express, and binary chunks are an escape hatch.
    static const char* const kRemoved[] = {"load", "loadfile", "dofile", "require",
                                           "collectgarbage", "os", "io", "package", "debug"};
    for (const char* name : kRemoved) {
        lua_pushnil(L_);
        lua_setglobal(L_, name);
    }
    // string.dump only makes binary chunks (the loaders take text only); and with the string metatable hidden,
    // a mod cannot reach the shared string table through getmetatable("") to change the base's string methods.
    lua_getglobal(L_, LUA_STRLIBNAME);
    lua_pushnil(L_);
    lua_setfield(L_, -2, "dump");
    lua_pop(L_, 1);
    lua_pushliteral(L_, "");
    if (lua_getmetatable(L_, -1) != 0) {
        lua_pushboolean(L_, 0);
        lua_setfield(L_, -2, "__metatable");
        lua_pop(L_, 1);
    }
    lua_pop(L_, 1);
    // print goes to the log like sh.log, not to the raw stdout the game redirects.
    lua_pushlightuserdata(L_, this);
    lua_pushcclosure(L_, l_log, 1);
    lua_setglobal(L_, "print");

    // The API table. Every entry carries the host as an upvalue.
    lua_createtable(L_, 0, 8);
    for (const luaL_Reg* f = kShFuncs; f->name != nullptr; ++f) {
        lua_pushlightuserdata(L_, this);
        lua_pushcclosure(L_, f->func, 1);
        lua_setfield(L_, -2, f->name);
    }
    install_flag_family(L_, this);
    install_player_camera_families(L_, this);
    install_timer_family(L_, this);
    install_config_family(L_, this);
    install_console_family(L_, this);
    install_sound_families(L_, this);
    push_hooks_api(L_, this, std::string());
    lua_setfield(L_, -2, "hooks");
    if (lua_getfield(L_, -1, "mods") != LUA_TTABLE) {
        lua_pop(L_, 1);
        lua_newtable(L_);
        lua_pushvalue(L_, -1);
        lua_setfield(L_, -3, "mods");
    }
    push_mods_get(L_, this);
    lua_setfield(L_, -2, "get");
    lua_pop(L_, 1);
    install_world_family(L_, this);
    install_world_pickup_family(L_, this);
    /* The raw bridge: only with script_raw_api = 1. */
    if (config_.game.raw_api_enabled != nullptr && config_.game.raw_api_enabled() != 0) {
        lua_pushlightuserdata(L_, this);
        lua_pushnil(L_);
        lua_pushcclosure(L_, l_evb, 2);
        lua_setfield(L_, -2, "call");
        for (const char* const* name = kEvbFuncs; *name != nullptr; ++name) {
            lua_pushlightuserdata(L_, this);
            lua_pushstring(L_, *name);
            lua_pushcclosure(L_, l_evb, 2);
            lua_setfield(L_, -2, *name);
        }
        for (const luaL_Reg* f = kEvaRawFuncs; f->name != nullptr; ++f) {
            lua_pushlightuserdata(L_, this);
            lua_pushcclosure(L_, f->func, 1);
            lua_setfield(L_, -2, f->name);
        }
    }
    /* sh.<group> tables of the named engine API. */
    if (config_.game.named_list != nullptr) {
        for (const char* const* full = config_.game.named_list(); full != nullptr && *full != nullptr; ++full) {
            const char* dot = std::strchr(*full, '.');
            if (dot == nullptr) {
                continue;
            }
            const std::string group(*full, dot);
            if (lua_getfield(L_, -1, group.c_str()) != LUA_TTABLE) {
                lua_pop(L_, 1);
                lua_newtable(L_);
                lua_pushvalue(L_, -1);
                lua_setfield(L_, -3, group.c_str());
            }
            lua_pushlightuserdata(L_, this);
            lua_pushstring(L_, *full);
            lua_pushcclosure(L_, l_named, 2);
            lua_setfield(L_, -2, dot + 1);
            lua_pop(L_, 1);
        }
    }
    /* render: sh.render.*. */
    if (lua_getfield(L_, -1, "render") != LUA_TTABLE) {
        lua_pop(L_, 1);
        lua_newtable(L_);
        lua_pushvalue(L_, -1);
        lua_setfield(L_, -3, "render");
    }
    lua_pushlightuserdata(L_, this);
    lua_pushcclosure(L_, l_render_frame_hold, 1);
    lua_setfield(L_, -2, "frame_hold");
    lua_pushlightuserdata(L_, this);
    lua_pushcclosure(L_, l_render_sprite, 1);
    lua_setfield(L_, -2, "sprite");
    lua_pushlightuserdata(L_, this);
    lua_pushcclosure(L_, l_render_texture_load, 1);
    lua_setfield(L_, -2, "texture_load");
    for (const auto& f : {std::pair<const char*, lua_CFunction>{"held_clear", l_render_held_clear},
                          {"held_capture", l_render_held_capture},
                          {"world_redirect", l_render_world_redirect},
                          {"tile", l_render_tile},
                          {"fade_in_level", l_render_fade_in_level},
                          {"vram_save", l_render_vram_save},
                          {"vram_restore", l_render_vram_restore},
                          {"display_init", l_render_display_init},
                          {"display_swap", l_render_display_swap},
                          {"vblank_interval", l_render_vblank_interval},
                          {"picture_screen", l_render_picture_screen},
                          {"background", l_render_background},
                          {"world_quads", l_render_world_quads}}) {
        lua_pushlightuserdata(L_, this);
        lua_pushcclosure(L_, f.second, 1);
        lua_setfield(L_, -2, f.first);
    }
    lua_pop(L_, 1);
    /* Canonical sh.<group>.<name> names of the flat functions (api_registry.h). */
    for (const ApiAlias& al : api_aliases()) {
        if (lua_getfield(L_, -1, al.legacy) == LUA_TFUNCTION) {
            if (lua_getfield(L_, -2, al.group) != LUA_TTABLE) {
                lua_pop(L_, 1);
                lua_newtable(L_);
                lua_pushvalue(L_, -1);
                lua_setfield(L_, -4, al.group);
            }
            if (lua_getfield(L_, -1, al.name) == LUA_TNIL) {
                lua_pushvalue(L_, -3);
                lua_setfield(L_, -3, al.name);
            }
            lua_pop(L_, 2);
        }
        lua_pop(L_, 1);
    }
    open_engine_api(L_);
    lua_setglobal(L_, "sh");

    open_coroutine_api();
}

void Host::open_coroutine_api() {
    // The coroutine library itself is safe in the sandbox (threads cannot escape it).
    // sh.spawn schedules through the host.
    luaL_requiref(L_, LUA_COLIBNAME, luaopen_coroutine, 1);
    lua_pop(L_, 1);

    lua_getglobal(L_, "sh");
    lua_pushlightuserdata(L_, this);
    lua_pushcclosure(L_, l_spawn, 1);
    lua_setfield(L_, -2, "spawn");
    lua_pop(L_, 1);

    for (const auto& g : {std::pair<const char*, lua_CFunction>{"wait", l_wait},
                          {"wait_frames", l_wait_frames}, {"wait_until", l_wait_until}}) {
        lua_pushlightuserdata(L_, this);
        lua_pushcclosure(L_, g.second, 1);
        lua_setglobal(L_, g.first);
    }

    // Prelude: the coroutine idioms every ported map handler needs, written once in
    // Lua over the sh.* primitives instead of one C++ bind each.
    static const char* kPrelude = R"lua(
        -- say(msg): show a map message to the end (returns the selection entry 1..3,
        -- or -1 when it finished without a selection). Coroutine only.
        function say(msg)
            local state = 0
            repeat
                state = sh.msg_draw(msg)
                if state == 0 then wait(0) end
            until state ~= 0
            return state
        end

        function wait_fade()
            while not sh.fade_done() do wait(0) end
        end

        -- load_map(map_idx, arrival_point, complete_flag): door transition through the
        -- game's own map-event path (same fade and loading screens as a data door).
        function load_map(map, point, complete)
            sh.map_event_set(5, point or 0, map, complete or -1)
        end

        function fmv(param, complete)
            sh.map_event_set(4, param, 0, complete or -1)
        end
    )lua";
    if (luaL_dostring(L_, kPrelude) != LUA_OK) {
        const std::string error = lua_tostring(L_, -1) ? lua_tostring(L_, -1) : "unknown error";
        lua_pop(L_, 1);
        log(1, std::string("prelude: ") + error);
    }
}

struct Host::CallScope {
    // Every script call of the frame, for the performance history; a call inside a call is timed once.
    static int perf_section() {
        static const int id = sh::devtools::perf_monitor().section("scripts", "game_update");
        return id;
    }
    sh::devtools::PerfScope perf{perf_section()};
    std::uint32_t counter = 0;
    lua_State* L = nullptr;
    void* outer = nullptr;
    /* Scopes nest (an engine callback inside a script call runs the script again); the outer
     * budget comes back when the inner call ends, and no pointer to a finished call stays behind. */
    explicit CallScope(Host& host) : L(host.L_) {
        lua_rawgetp(L, LUA_REGISTRYINDEX, &kBudgetKey);
        outer = lua_touserdata(L, -1);
        lua_pop(L, 1);
        lua_pushlightuserdata(L, &counter);
        lua_rawsetp(L, LUA_REGISTRYINDEX, &kBudgetKey);
    }
    ~CallScope() {
        if (outer != nullptr) {
            lua_pushlightuserdata(L, outer);
        } else {
            lua_pushnil(L);
        }
        lua_rawsetp(L, LUA_REGISTRYINDEX, &kBudgetKey);
    }
};

int Host::pcall_budget(int nargs, int nresults) {
    CallScope scope(*this);
    return lua_pcall(L_, nargs, nresults, 0);
}

Result<bool> Host::load() {
    if (L_ != nullptr) {
        return Result<bool>::ok(true);  // already loaded; a second boot call is a no-op
    }

    namespace fs = std::filesystem;
    const fs::path script = config_.gamedata_dir / "scripts" / "game.lua";
    std::error_code ec;
    // A mod adds to the base game script (scripts/main.lua) instead of replacing it: two mods can then both
    // hook the game, and a mod cannot drop what the base script does.
    std::vector<mods::ModInfo> entries;
    if (!config_.mods_dir.empty() && fs::is_directory(config_.mods_dir, ec)) {
        std::vector<std::string> warnings;
        const std::vector<mods::ModInfo> found = mods::discover(config_.mods_dir, warnings);
        for (const mods::ModInfo& mod : found) {  // load order: the first loads first
            const bool has_main = fs::is_regular_file(mod.root / "scripts" / "main.lua", ec);
            const bool has_old = fs::is_regular_file(mod.root / "scripts" / "game.lua", ec);
            if ((!has_main && !has_old) || !mods::scripts_allowed(mod, warnings)) {
                continue;
            }
            if (has_old) {
                warnings.push_back("mod " + mod.id + ": scripts/game.lua no longer replaces the base game script; "
                                                     "put the mod's code in scripts/main.lua");
            }
            if (has_main) {
                entries.push_back(mod);
            }
        }
        for (const std::string& w : warnings) {
            log(1, "mods: " + w);
        }
    }

    if (fs::is_regular_file(script, ec)) {
        const Result<bool> base = load_file(script);
        if (!base) {
            return base;
        }
    } else if (entries.empty()) {
        return Result<bool>::ok(false);
    } else {
        if (!create_state()) {
            return Result<bool>::fail("cannot create the Lua state");
        }
        lua_sethook(L_, &Host::count_hook, LUA_MASKCOUNT, 1000);
    }
    for (const mods::ModInfo& mod : entries) {
        load_mod_script(mod.id, mod.name, mod.version, mod.root);
    }
    return Result<bool>::ok(true);
}

std::vector<std::string> Host::loaded_mods() const {
    std::vector<std::string> ids;
    for (const ModScript& m : mods_) {
        if (!m.extension) {
            ids.push_back(m.id);
        }
    }
    return ids;
}

std::vector<std::string> Host::extension_mods() const {
    std::vector<std::string> ids;
    for (const ModScript& m : mods_) {
        if (m.extension) {
            ids.push_back(m.id);
        }
    }
    return ids;
}

Result<bool> Host::run_layered(std::string_view relative) {
    std::vector<std::string> warnings;
    const mods::LuaLayers layers = mods::lua_layers(config_.mods_dir, config_.gamedata_dir, relative, warnings);
    for (const std::string& w : warnings) {
        log(1, "mods: " + w);
    }
    return run_layers(layers);
}

Result<bool> Host::run_layers(const mods::LuaLayers& layers) {
    if (layers.empty()) {
        return Result<bool>::ok(false);
    }
    if (!layers.base.empty()) {
        base_owner_ = layers.base_owner;
        Result<bool> base = run_file(layers.base);
        if (!base) {
            return base;
        }
        if (layers.base_owner != "silenthill") {
            log(0, "mod " + layers.base_owner + ": " + layers.base.generic_string() + " replaces the base script");
        }
    } else if (L_ == nullptr) {
        if (!create_state()) {
            return Result<bool>::fail("cannot create the Lua state");
        }
        lua_sethook(L_, &Host::count_hook, LUA_MASKCOUNT, 1000);
    }
    for (const mods::LuaLayer& layer : layers.extensions) {
        const std::string where = "mod " + layer.mod.id + ": " + layer.file.generic_string();
        if (!push_mod_environment(layer.mod.id, layer.mod.name, layer.mod.version, layer.mod.root, true, where)) {
            continue;
        }
        if (luaL_loadfilex(L_, layer.file.string().c_str(), "t") != LUA_OK) {
            log(1, where + ": " + (lua_tostring(L_, -1) ? lua_tostring(L_, -1) : "cannot load") +
                       " (the extension is not run)");
            lua_pop(L_, 2);
            continue;
        }
        lua_pushvalue(L_, -2);
        lua_setupvalue(L_, -2, 1);  // _ENV
        if (pcall_budget(0, 0) != LUA_OK) {
            log(1, where + ": " + (lua_tostring(L_, -1) ? lua_tostring(L_, -1) : "unknown error") +
                       " (the extension is left out)");
            lua_pop(L_, 2);
            continue;
        }
        ModScript m;
        m.id = layer.mod.id;
        m.extension = true;
        m.env_ref = luaL_ref(L_, LUA_REGISTRYINDEX);
        mods_.push_back(std::move(m));
        log(0, where + ": extension loaded");
    }
    return Result<bool>::ok(true);
}

bool Host::push_function(const char* name) {
    for (auto it = mods_.rbegin(); it != mods_.rend(); ++it) {
        if (!it->extension) {
            continue;
        }
        lua_rawgeti(L_, LUA_REGISTRYINDEX, it->env_ref);
        lua_pushstring(L_, name);
        lua_rawget(L_, -2);
        lua_remove(L_, -2);
        if (lua_isfunction(L_, -1)) {
            return true;
        }
        lua_pop(L_, 1);
    }
    lua_getglobal(L_, name);
    if (lua_isfunction(L_, -1)) {
        return true;
    }
    lua_pop(L_, 1);
    return false;
}

bool Host::create_state() {
    L_ = luaL_newstate();
    if (L_ == nullptr) {
        return false;
    }
    // A resume can spawn more coroutines; with the capacity reserved up front the
    // pump's references survive that push_back (the count is capped at kMaxCoroutines).
    coroutines_.reserve(kMaxCoroutines);
    open_sandbox();
    return true;
}

// Builds a mod's environment: its own globals, with reads falling back to the host's. The libraries and `sh`
// are copies, so a mod that reassigns string.format or sh.log changes only its own view; `_G` is the
// environment itself, and the hidden metatable keeps the host's global table out of reach.
//
// An extension layer (`prev` = the earlier extensions' environments) also gets extend(name, wrapper): `base` is
// the definition the scripts saw so far (this layer's own, then the latest earlier layer's, then the global),
// so mods that extend one handler chain in load order.
static const char kModEnvironment[] = R"lua(
    local G, id, name, version, dir, exports, prev = ...
    local function copy(t, depth)
        local c = {}
        for k, v in pairs(t) do
            if type(v) == "table" and depth > 0 then
                v = copy(v, depth - 1)
            end
            c[k] = v
        end
        return c
    end
    local env = {}
    for _, lib in ipairs({"string", "table", "math", "coroutine", "sh"}) do
        if type(G[lib]) == "table" then
            env[lib] = copy(G[lib], lib == "sh" and 2 or 0)
        end
    end
    env._G = env
    env.mod = {id = id, name = name, version = version, dir = dir, storage = {}}
    -- Another mod reads it with sh.mods.get(id).api; one table per mod and state.
    function env.mod.export(api)
        if type(api) ~= "table" then
            error("mod.export(api) expects a table", 2)
        end
        exports[id] = api
        return api
    end
    if prev then
        local function seen(key)
            for i = #prev, 1, -1 do
                local v = rawget(prev[i], key)
                if v ~= nil then
                    return v
                end
            end
            return G[key]
        end
        function env.extend(key, wrapper)
            if type(key) ~= "string" or type(wrapper) ~= "function" then
                error("extend(name, function(base, ...) ... end) expects a name and a function", 2)
            end
            local base = rawget(env, key)
            if base == nil then
                base = seen(key)
            end
            if type(base) ~= "function" then
                env.sh.log(1, "mod " .. id .. ": extend('" .. key .. "'): nothing of that name to extend; " ..
                              "base() does nothing")
                base = function() end
            end
            rawset(env, key, function(...)
                return wrapper(base, ...)
            end)
        end
    end
    return setmetatable(env, {__index = G, __metatable = false})
)lua";

bool Host::push_mod_environment(const std::string& id, const std::string& name, const std::string& version,
                                const std::filesystem::path& root, bool extension, const std::string& where) {
    if (luaL_loadbufferx(L_, kModEnvironment, sizeof(kModEnvironment) - 1, "=mod environment", "t") != LUA_OK) {
        log(1, where + ": " + (lua_tostring(L_, -1) ? lua_tostring(L_, -1) : "environment error"));
        lua_pop(L_, 1);
        return false;
    }
    lua_pushglobaltable(L_);
    lua_pushstring(L_, id.c_str());
    lua_pushstring(L_, name.c_str());
    lua_pushstring(L_, version.c_str());
    const std::u8string dir = root.generic_u8string();
    lua_pushlstring(L_, reinterpret_cast<const char*>(dir.data()), dir.size());
    push_exports(L_);
    if (extension) {
        lua_newtable(L_);
        lua_Integer n = 0;
        for (const ModScript& m : mods_) {
            if (m.extension) {
                lua_rawgeti(L_, LUA_REGISTRYINDEX, m.env_ref);
                lua_rawseti(L_, -2, ++n);
            }
        }
    } else {
        lua_pushnil(L_);
    }
    if (lua_pcall(L_, 7, 1, 0) != LUA_OK) {
        log(1, where + ": " + (lua_tostring(L_, -1) ? lua_tostring(L_, -1) : "environment error"));
        lua_pop(L_, 1);
        return false;
    }
    // The copied sh.hooks would register as the state's own globals; this one registers as the mod.
    lua_getfield(L_, -1, "sh");
    push_hooks_api(L_, this, id);
    lua_setfield(L_, -2, "hooks");
    lua_pop(L_, 1);
    return true;
}

void Host::load_mod_script(const std::string& id, const std::string& name, const std::string& version,
                           const std::filesystem::path& root) {
    const std::filesystem::path file = root / "scripts" / "main.lua";
    const std::string where = "mod " + id + ": " + file.generic_string();
    if (!push_mod_environment(id, name, version, root, false, where)) {
        return;
    }
    // env
    if (luaL_loadfilex(L_, file.string().c_str(), "t") != LUA_OK) {
        log(1, where + ": " + (lua_tostring(L_, -1) ? lua_tostring(L_, -1) : "cannot load") +
                   " (the mod's scripts are not run)");
        lua_pop(L_, 2);
        return;
    }
    lua_pushvalue(L_, -2);
    lua_setupvalue(L_, -2, 1);  // the chunk's only upvalue is _ENV
    int rc;
    {
        CallScope scope(*this);
        rc = lua_pcall(L_, 0, 0, 0);
    }
    if (rc != LUA_OK) {
        log(1, where + ": " + (lua_tostring(L_, -1) ? lua_tostring(L_, -1) : "unknown error") +
                   " (the mod's hooks are not called)");
        lua_pop(L_, 2);
        return;
    }
    ModScript m;
    m.id = id;
    m.env_ref = luaL_ref(L_, LUA_REGISTRYINDEX);  // pops env
    mods_.push_back(std::move(m));
    log(0, "mod " + id + ": scripts/main.lua loaded");
}

template <typename... Push>
void Host::call_mod_hooks(const char* name, Push&&... push) {
    for (ModScript& m : mods_) {
        if (std::find(m.broken.begin(), m.broken.end(), name) != m.broken.end()) {
            continue;
        }
        lua_rawgeti(L_, LUA_REGISTRYINDEX, m.env_ref);
        lua_pushstring(L_, name);
        lua_rawget(L_, -2);  // raw: the base's hook of that name, seen through __index, is not the mod's
        lua_remove(L_, -2);
        if (!lua_isfunction(L_, -1)) {
            lua_pop(L_, 1);
            continue;
        }
        int pushed = 0;
        (..., (push(), ++pushed));
        int rc;
        {
            CallScope scope(*this);
            rc = lua_pcall(L_, pushed, 0, 0);
        }
        if (rc != LUA_OK) {
            const std::string error = lua_tostring(L_, -1) ? lua_tostring(L_, -1) : "unknown error";
            lua_pop(L_, 1);
            log(1, "mod " + m.id + ": " + name + " error: " + error + " (the hook is off for this mod)");
            m.broken.emplace_back(name);
        }
    }
}

Result<bool> Host::load_file(const std::filesystem::path& script) {
    if (L_ != nullptr) {
        return Result<bool>::ok(true);  // already loaded
    }
    std::error_code ec;
    if (!std::filesystem::is_regular_file(script, ec)) {
        return Result<bool>::ok(false);  // no script: the normal case
    }

    if (!create_state()) {
        return Result<bool>::fail("cannot create the Lua state");
    }

    script_path_ = script.generic_string();
    // Text chunks only: a crafted binary chunk is a known way out of a Lua sandbox, and mods can supply files.
    int rc = luaL_loadfilex(L_, script_path_.c_str(), "t");
    if (rc == LUA_OK) {
        rc = lua_pcall(L_, 0, LUA_MULTRET, 0);
    }
    if (rc != LUA_OK) {
        const bool no_file = rc == LUA_ERRFILE;
        std::string error =
            no_file ? std::string("cannot read ") + script.generic_string()
                    : std::string(lua_tostring(L_, -1) ? lua_tostring(L_, -1) : "unknown error");
        if (!no_file) {
            error = script.generic_string() + ": " + error;
        }
        close_state();
        script_path_.clear();
        return Result<bool>::fail(error);
    }

    lua_sethook(L_, &Host::count_hook, LUA_MASKCOUNT, 1000);
    return Result<bool>::ok(true);
}

void Host::count_hook(lua_State* L, lua_Debug*) {
    lua_rawgetp(L, LUA_REGISTRYINDEX, &kBudgetKey);
    void* key = lua_touserdata(L, -1);
    lua_pop(L, 1);
    if (key != nullptr) {
        auto* counter = static_cast<std::uint32_t*>(key);
        *counter += 1000;
        if (*counter > kInstructionBudget) {
            // lua_pushfstring has no %u; the budget fits an int.
            luaL_error(L, "script exceeded its instruction budget (%d)", (int)kInstructionBudget);
        }
    }
}

template <typename... Push>
bool Host::call_hook(const char* name, Push&&... push) {
    if (L_ == nullptr) {
        return false;
    }
    lua_getglobal(L_, name);
    if (!lua_isfunction(L_, -1)) {
        lua_pop(L_, 1);
        return false;
    }
    int pushed = 0;
    (..., (push(), ++pushed));
    CallScope scope(*this);
    if (lua_pcall(L_, pushed, 0, 0) != LUA_OK) {
        const std::string error = lua_tostring(L_, -1) ? lua_tostring(L_, -1) : "unknown error";
        lua_pop(L_, 1);
        log(1, std::string(name) + " error: " + error);
        return false;
    }
    return true;
}

void Host::on_boot() {
    call_hook("on_boot");
    call_mod_hooks("on_boot");
}

// Ported map handlers: the dispatcher checks has_global (a raw getglobal, no pcall)
// and then calls the function once per tick through call_global (protected; an error
// is logged and this tick simply does nothing — the next tick tries again, like a C++
// handler would after a caught error).
bool Host::has_global(std::string_view name) {
    if (L_ == nullptr) {
        return false;
    }
    const std::string key(name);  // lua_getglobal needs a C string
    if (!push_function(key.c_str())) {
        return false;
    }
    lua_pop(L_, 1);
    return true;
}

namespace {

// print() while a tools snippet runs: its words go to the snippet's output instead of the log.
int l_snippet_print(lua_State* L) {
    auto* sink = static_cast<std::string*>(lua_touserdata(L, lua_upvalueindex(1)));
    const int n = lua_gettop(L);
    for (int i = 1; i <= n; ++i) {
        if (i > 1) sink->push_back('\t');
        sink->append(luaL_tolstring(L, i, nullptr));
        lua_pop(L, 1);
    }
    sink->push_back('\n');
    return 0;
}

}  // namespace

bool Host::run_snippet(std::string_view code, std::string* out) {
    out->clear();
    if (L_ == nullptr) {
        *out = "no script is loaded in this state";
        return false;
    }
    const int top = lua_gettop(L_);
    const std::string expr = "return " + std::string(code);
    if (luaL_loadbufferx(L_, expr.data(), expr.size(), "=snippet", "t") != LUA_OK) {
        lua_pop(L_, 1);
        if (luaL_loadbufferx(L_, code.data(), code.size(), "=snippet", "t") != LUA_OK) {
            *out = lua_tostring(L_, -1) ? lua_tostring(L_, -1) : "cannot compile";
            lua_settop(L_, top);
            return false;
        }
    }
    std::string printed;
    lua_getglobal(L_, "print");
    const int old_print = luaL_ref(L_, LUA_REGISTRYINDEX);
    lua_pushlightuserdata(L_, &printed);
    lua_pushcclosure(L_, l_snippet_print, 1);
    lua_setglobal(L_, "print");
    const int status = pcall_budget(0, LUA_MULTRET);
    lua_rawgeti(L_, LUA_REGISTRYINDEX, old_print);
    lua_setglobal(L_, "print");
    luaL_unref(L_, LUA_REGISTRYINDEX, old_print);
    *out = printed;
    if (status != LUA_OK) {
        *out += lua_tostring(L_, -1) ? lua_tostring(L_, -1) : "unknown error";
        lua_settop(L_, top);
        return false;
    }
    std::string results;
    for (int i = top + 1; i <= lua_gettop(L_); ++i) {
        if (!results.empty()) results.push_back('\t');
        results.append(luaL_tolstring(L_, i, nullptr));
        lua_pop(L_, 1);
    }
    *out += results;
    lua_settop(L_, top);
    return true;
}

bool Host::run_test_file(std::string_view harness, std::string_view code, const std::string& chunk_name,
                         std::string* out) {
    out->clear();
    if (L_ == nullptr) {
        *out = "SHTEST ERROR\tno script is loaded in this state";
        return false;
    }
    const int top = lua_gettop(L_);
    const auto fail = [&](const char* fallback) {
        *out += std::string("SHTEST ERROR\t") + (lua_tostring(L_, -1) ? lua_tostring(L_, -1) : fallback);
        lua_settop(L_, top);
        return false;
    };
    const std::string name = "@" + chunk_name;
    if (luaL_loadbufferx(L_, code.data(), code.size(), name.c_str(), "t") != LUA_OK) return fail("cannot compile");
    if (luaL_loadbufferx(L_, harness.data(), harness.size(), "=test harness", "t") != LUA_OK)
        return fail("cannot compile the harness");
    if (pcall_budget(0, 2) != LUA_OK) return fail("the harness failed");
    // Stack: chunk, env, run.
    lua_pushvalue(L_, top + 2);
    if (lua_setupvalue(L_, top + 1, 1) == nullptr) lua_pop(L_, 1);
    lua_pushvalue(L_, top + 3);
    lua_pushvalue(L_, top + 1);
    std::string printed;
    lua_getglobal(L_, "print");
    const int old_print = luaL_ref(L_, LUA_REGISTRYINDEX);
    lua_pushlightuserdata(L_, &printed);
    lua_pushcclosure(L_, l_snippet_print, 1);
    lua_setglobal(L_, "print");
    const int status = pcall_budget(1, 1);
    lua_rawgeti(L_, LUA_REGISTRYINDEX, old_print);
    lua_setglobal(L_, "print");
    luaL_unref(L_, LUA_REGISTRYINDEX, old_print);
    *out = printed;
    if (status != LUA_OK) return fail("unknown error");
    *out += lua_tostring(L_, -1) ? lua_tostring(L_, -1) : "";
    lua_settop(L_, top);
    return true;
}

bool Host::call_global(std::string_view name) {
    if (L_ == nullptr) {
        return false;
    }
    const std::string key(name);
    if (!push_function(key.c_str())) {
        return false;
    }
    if (pcall_budget(0, 0) != LUA_OK) {
        const std::string error = lua_tostring(L_, -1) ? lua_tostring(L_, -1) : "unknown error";
        lua_pop(L_, 1);
        log_call_error(key + " error: " + error);
        return false;
    }
    return true;
}

Result<bool> Host::run_file(const std::filesystem::path& script) {
    if (L_ == nullptr) {
        return load_file(script);
    }
    std::error_code ec;
    if (!std::filesystem::is_regular_file(script, ec)) {
        return Result<bool>::ok(false);
    }
    lua_sethook(L_, nullptr, 0, 0);  // the budget hook needs a CallScope; a chunk runs outside one
    int rc = luaL_loadfilex(L_, script.string().c_str(), "t");  // text only, as in load_file
    if (rc == LUA_OK) {
        rc = lua_pcall(L_, 0, LUA_MULTRET, 0);
    }
    lua_sethook(L_, &Host::count_hook, LUA_MASKCOUNT, 1000);
    if (rc != LUA_OK) {
        std::string error = lua_tostring(L_, -1) ? lua_tostring(L_, -1) : "unknown error";
        lua_pop(L_, 1);
        return Result<bool>::fail(script.generic_string() + ": " + error);
    }
    script_path_ = script.generic_string();
    return Result<bool>::ok(true);
}

bool Host::call_global_ints(std::string_view name, std::initializer_list<long long> args) {
    if (L_ == nullptr) {
        return false;
    }
    const std::string key(name);
    if (!push_function(key.c_str())) {
        return false;
    }
    for (const long long v : args) {
        lua_pushinteger(L_, static_cast<lua_Integer>(v));
    }
    CallScope scope(*this);
    if (lua_pcall(L_, static_cast<int>(args.size()), 0, 0) != LUA_OK) {
        const std::string error = lua_tostring(L_, -1) ? lua_tostring(L_, -1) : "unknown error";
        lua_pop(L_, 1);
        log_call_error(script_path_ + ": " + key + " error: " + error);
        return false;
    }
    return true;
}

bool Host::call_global_int(std::string_view name, std::initializer_list<long long> args, long long* result) {
    if (L_ == nullptr) {
        return false;
    }
    const std::string key(name);
    if (!push_function(key.c_str())) {
        return false;
    }
    for (const long long v : args) {
        lua_pushinteger(L_, static_cast<lua_Integer>(v));
    }
    int rc;
    {
        CallScope scope(*this);
        rc = lua_pcall(L_, static_cast<int>(args.size()), 1, 0);
    }
    if (rc != LUA_OK) {
        const std::string error = lua_tostring(L_, -1) ? lua_tostring(L_, -1) : "unknown error";
        lua_pop(L_, 1);
        log_call_error(script_path_ + ": " + key + " error: " + error);
        return false;
    }
    int ok = 0;
    const lua_Integer v = lua_tointegerx(L_, -1, &ok);
    lua_pop(L_, 1);
    if (ok == 0) {
        return false;
    }
    *result = static_cast<long long>(v);
    return true;
}

bool Host::call_ref(int ref, const std::function<int(lua_State*)>& push,
                    const std::function<void(lua_State*)>& take, std::string* error) {
    if (L_ == nullptr) {
        if (error != nullptr) *error = "script state is closed";
        return false;
    }
    lua_rawgeti(L_, LUA_REGISTRYINDEX, ref);
    if (!lua_isfunction(L_, -1)) {
        lua_pop(L_, 1);
        if (error != nullptr) *error = "script function is gone";
        return false;
    }
    const int nargs = push ? push(L_) : 0;
    int rc;
    {
        CallScope scope(*this);
        rc = lua_pcall(L_, nargs, 1, 0);
    }
    if (rc != LUA_OK) {
        const std::string message = lua_tostring(L_, -1) ? lua_tostring(L_, -1) : "unknown error";
        lua_pop(L_, 1);
        log(1, "callback error: " + message);
        if (error != nullptr) *error = message;
        return false;
    }
    if (take) take(L_);
    lua_pop(L_, 1);
    return true;
}

Host::HookResult Host::call_global_table(std::string_view name, std::span<const HookField> fields) {
    HookResult result;
    if (L_ == nullptr) {
        return result;
    }
    const std::string key(name);
    if (!push_function(key.c_str())) {
        return result;
    }
    lua_createtable(L_, 0, static_cast<int>(fields.size()));
    for (const HookField& f : fields) {
        switch (f.value.kind) {
            case 1: lua_pushinteger(L_, static_cast<lua_Integer>(f.value.i)); break;
            case 2: lua_pushstring(L_, f.value.s != nullptr ? f.value.s : ""); break;
            case 3: lua_pushboolean(L_, f.value.i != 0); break;
            default: lua_pushnil(L_); break;
        }
        lua_setfield(L_, -2, f.key);
    }
    CallScope scope(*this);
    if (lua_pcall(L_, 1, 1, 0) != LUA_OK) {
        const std::string error = lua_tostring(L_, -1) ? lua_tostring(L_, -1) : "unknown error";
        lua_pop(L_, 1);
        log(1, key + " error: " + error);
        return result;
    }
    if (lua_isinteger(L_, -1)) {
        result.kind = 1;
        result.integer = lua_tointeger(L_, -1);
    } else if (lua_type(L_, -1) == LUA_TSTRING) {
        result.kind = 2;
        result.text = lua_tostring(L_, -1);
    }
    lua_pop(L_, 1);
    return result;
}

void Host::on_new_game() {
    call_hook("on_new_game");
    call_mod_hooks("on_new_game");
}

void Host::on_map_load(std::string_view map) {
    const auto push_map = [&] { lua_pushlstring(L_, map.data(), map.size()); };
    call_hook("on_map_load", push_map);
    call_mod_hooks("on_map_load", push_map);
}

void Host::on_update() {
    if (L_ == nullptr) {
        return;
    }
    // A missing hook is the normal case; only a hook that RUNS and errors is dropped.
    // Coroutines pump either way: sh.spawn works in a script with no on_update at all.
    lua_getglobal(L_, "on_update");
    const bool has_hook = lua_isfunction(L_, -1);
    lua_pop(L_, 1);
    if (update_ok_ && has_hook && !call_hook("on_update")) {
        update_ok_ = false;  // stop calling a broken hook every frame
    }
    call_mod_hooks("on_update");
    pump_coroutines();
}

void Host::add_coroutine(int thread_ref, int nargs) {
    Coroutine co;
    co.thread_ref = thread_ref;
    co.nargs = nargs;
    coroutines_.push_back(co);
}

void Host::drop_coroutine(std::size_t i, const char* why) {
    Coroutine& co = coroutines_[i];
    if (co.thread_ref != kNoRef) {
        luaL_unref(L_, LUA_REGISTRYINDEX, co.thread_ref);
    }
    if (co.pred_ref != kNoRef) {
        luaL_unref(L_, LUA_REGISTRYINDEX, co.pred_ref);
    }
    coroutines_.erase(coroutines_.begin() + static_cast<std::ptrdiff_t>(i));
    if (std::string_view(why) != "finished") {
        log(0, std::string("coroutine: ") + why);
    }
}

bool Host::resume_one(Coroutine& co, const char*) {
    lua_rawgeti(L_, LUA_REGISTRYINDEX, co.thread_ref);
    lua_State* thread = lua_tothread(L_, -1);
    lua_pop(L_, 1);
    if (thread == nullptr) {
        return false;  // the ref went stale; the caller drops it
    }
    int nres = 0;
    CallScope scope(*this);
    const int rc = lua_resume(thread, L_, co.started ? 0 : co.nargs, &nres);
    co.nargs = 0;
    if (rc == LUA_OK) {
        return false;  // finished
    }
    if (rc != LUA_YIELD) {
        const std::string error = lua_tostring(thread, -1) ? lua_tostring(thread, -1) : "unknown error";
        lua_pop(thread, 1);
        log(1, "coroutine error: " + error);
        return false;
    }
    // The yield tells the pump how long to wait: an integer = frames, a function =
    // a predicate polled once per tick. Anything else resumes on the next tick.
    // The yielded values sit on top of the thread's stack (above the call's leftovers).
    if (co.pred_ref != kNoRef) {
        luaL_unref(L_, LUA_REGISTRYINDEX, co.pred_ref);
        co.pred_ref = kNoRef;
    }
    co.frames_left = 0;
    const int first = lua_gettop(thread) - nres + 1;
    if (nres >= 1 && first >= 1 && lua_isinteger(thread, first)) {
        const lua_Integer n = lua_tointeger(thread, first);
        co.frames_left = n > 0 ? static_cast<int>(n) : 0;
    } else if (nres >= 1 && first >= 1 && lua_isfunction(thread, first)) {
        lua_pushvalue(thread, first);
        lua_xmove(thread, L_, 1);
        co.pred_ref = luaL_ref(L_, LUA_REGISTRYINDEX);
    }
    lua_pop(thread, nres);  // keep the thread's stack flat across many waits
    return true;
}

void Host::pump_coroutines() {
    if (coroutines_.empty()) {
        return;
    }
    for (std::size_t i = 0; i < coroutines_.size();) {
        Coroutine& co = coroutines_[i];
        bool resume_now = false;

        if (!co.started) {
            co.started = true;
            resume_now = true;  // the first run: its function and arguments are on the thread
        } else if (co.pred_ref != kNoRef) {
            lua_rawgeti(L_, LUA_REGISTRYINDEX, co.pred_ref);
            CallScope scope(*this);
            if (lua_pcall(L_, 0, 1, 0) != LUA_OK) {
                const std::string error = lua_tostring(L_, -1) ? lua_tostring(L_, -1) : "unknown error";
                lua_pop(L_, 1);
                log(1, "wait_until error: " + error);
                drop_coroutine(i, "predicate failed");
                continue;
            }
            const bool ready = lua_toboolean(L_, -1) != 0;
            lua_pop(L_, 1);
            if (ready) {
                luaL_unref(L_, LUA_REGISTRYINDEX, co.pred_ref);
                co.pred_ref = kNoRef;
                resume_now = true;
            }
        } else if (co.frames_left > 0) {
            --co.frames_left;
            resume_now = co.frames_left == 0;
        } else {
            // A zero-frame wait (wait(0)): one full tick passes before the next resume.
            resume_now = true;
        }

        if (resume_now) {
            if (!resume_one(co, "pump")) {
                drop_coroutine(i, "finished");
                continue;
            }
        }
        ++i;
    }
}

}  // namespace sh::script
