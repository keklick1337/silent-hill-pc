// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)

#include "engine/script/sh_script.h"

#include "engine/log/sh_log.h"
#include "engine/hooks/hook_bus.h"
#include "engine/mods/data_merge.h"
#include "engine/mods/mods.h"
#include "engine/mods/registry.h"
#include "engine/menu/menu_model.h"
#include "engine/script/bind_menu.h"
#include "engine/script/mod_settings.h"
#include "engine/script/script_host.h"
#include "formats/common/json.h"

#include <array>
#include <cctype>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <utility>
#include <map>
#include <vector>

namespace {

sh::script::GameApi g_game_api;
std::string g_gamedata_dir;  // where ShScript_Boot found the data; map scripts come from here
std::string g_mods_dir;
std::string g_map_name;      // the last on_map_load; sh.map_name()
int g_update_ticks = 0;      // ShScript_OnUpdate calls; MainLoop makes one per tick, so this is the dump's t

const char* current_map_name() { return g_map_name.c_str(); }

std::unique_ptr<sh::script::Host>& game_host() {
    static std::unique_ptr<sh::script::Host> host;
    return host;
}

// The per-map script (maps/<map>/events.lua, or a mod's copy), replaced on every map load.
// Separate state: a broken map script cannot take the game script down with it.
std::unique_ptr<sh::script::Host>& map_host() {
    static std::unique_ptr<sh::script::Host> host;
    return host;
}

/* Behaviour scripts by chara id; nullptr = looked up, the type has none. */
std::map<int, std::unique_ptr<sh::script::Host>>& chara_behaviors() {
    static std::map<int, std::unique_ptr<sh::script::Host>> hosts;
    return hosts;
}

// The active map's handler table (handlers.json): g_MapEventFuncs index -> the readable
// handler name an events.lua port defines as a Lua function. The slot's "symbol", a readable
// name too, is the fallback lookup for scripts written before the names existed.
void script_log(int level, const std::string& message);  // defined below
using MapHandler = sh::script::MapHandlerSlot;
std::vector<MapHandler>& map_handlers() {
    static std::vector<MapHandler> table;
    return table;
}

void load_map_handlers(const std::string& map) {
    std::string error;
    map_handlers() = sh::script::read_map_handlers(g_gamedata_dir, g_mods_dir, map, &error);
    if (!error.empty()) {
        script_log(1, error + "; the C++ handlers stay");
    }
}

void script_log(int level, const std::string& message) {
    ShLog_Write(SH_LOGCAT_GAME, level != 0 ? SH_LOGLV_WARN : SH_LOGLV_INFO, "%s", message.c_str());
}

sh::script::HostConfig make_config() {
    sh::script::HostConfig config;
    config.gamedata_dir = g_gamedata_dir;
    config.mods_dir = g_mods_dir;
    config.game = g_game_api;
    config.log = script_log;
    return config;
}

// A layered Lua file (Host::run_layered): the base, or the copy of a mod that replaces it, plus the extensions.
sh::mods::LuaLayers lua_layers_of(const std::string& relative) {
    std::vector<std::string> warnings;
    sh::mods::LuaLayers layers = sh::mods::lua_layers(g_mods_dir, g_gamedata_dir, relative, warnings);
    for (const std::string& w : warnings) {
        script_log(1, "mods: " + w);
    }
    return layers;
}

bool lua_layer_exists(const std::string& relative) {
    return !lua_layers_of(relative).empty();
}

}  // namespace

sh::script::HostConfig sh::script::game_host_config() {
    return make_config();
}

sh::Result<bool> sh::script::reload_host(std::unique_ptr<Host>& host, HostConfig config) {
    host.reset();  // its hook handlers, console commands and coroutines go with its state
    host = std::make_unique<Host>(std::move(config));
    auto loaded = host->load();
    if (!loaded) {
        host.reset();
        return loaded;
    }
    host->on_boot();
    return loaded;
}

extern "C" void ShScript_SetGameApi(const ShScriptGameApi* api) {
    if (api == nullptr) {
        g_game_api = sh::script::GameApi{};
    } else {
        g_game_api.flag_get = api->flag_get;
        g_game_api.flag_set = api->flag_set;
        g_game_api.give_item = api->give_item;
        g_game_api.player_pos = api->player_pos;
        g_game_api.map_name = api->map_name;
        g_game_api.flag_set_alt = api->flag_set_alt;
        g_game_api.item_count = api->item_count;
        g_game_api.take_item = api->take_item;
        g_game_api.player_set_pos = api->player_set_pos;
        g_game_api.player_freeze = api->player_freeze;
        g_game_api.player_unfreeze_set_idle = api->player_unfreeze_set_idle;
        g_game_api.msg_draw = api->msg_draw;
        g_game_api.sfx_play = api->sfx_play;
        g_game_api.bgm_change = api->bgm_change;
        g_game_api.fade_start = api->fade_start;
        g_game_api.fade_done = api->fade_done;
        g_game_api.map_event_set = api->map_event_set;
        g_game_api.event_point = api->event_point;
        g_game_api.event_item_take = api->event_item_take;
        g_game_api.event_common_item_take = api->event_common_item_take;
        g_game_api.state_set = api->state_set;
        g_game_api.event_msg = api->event_msg;
        g_game_api.event_msg_with_sfx = api->event_msg_with_sfx;
        g_game_api.event_msg_with_bg = api->event_msg_with_bg;
        g_game_api.event_paper_map_take = api->event_paper_map_take;
        g_game_api.input_action = api->input_action;
        g_game_api.input_axis = api->input_axis;
        g_game_api.camera_pos = api->camera_pos;
        g_game_api.camera_look = api->camera_look;
        g_game_api.event_msg_with_texture = api->event_msg_with_texture;
        g_game_api.flashlight = api->flashlight;
        g_game_api.background_gamma = api->background_gamma;
        g_game_api.event_wait_timer = api->event_wait_timer;
        g_game_api.event_step_get = api->event_step_get;
        g_game_api.event_step_set = api->event_step_set;
        g_game_api.event_step_increment = api->event_step_increment;
        g_game_api.event_fade_cmd = api->event_fade_cmd;
        g_game_api.event_selection = api->event_selection;
        g_game_api.sfx_stop = api->sfx_stop;
        g_game_api.event_waypoint_set = api->event_waypoint_set;
        g_game_api.event_waypoint_player = api->event_waypoint_player;
        g_game_api.chara_load = api->chara_load;
        g_game_api.chara_process_loads = api->chara_process_loads;
        g_game_api.event_chara_anim = api->event_chara_anim;
        g_game_api.fs_read_dms = api->fs_read_dms;
        g_game_api.fs_queue_done = api->fs_queue_done;
        g_game_api.dms_ready = api->dms_ready;
        g_game_api.cutscene_border = api->cutscene_border;
        g_game_api.cutscene_flag = api->cutscene_flag;
        g_game_api.cutscene_timer_get = api->cutscene_timer_get;
        g_game_api.cutscene_timer_set = api->cutscene_timer_set;
        g_game_api.overlay_screen_set = api->overlay_screen_set;
        g_game_api.overlay_screen_show = api->overlay_screen_show;
        g_game_api.overlay_screen_hide = api->overlay_screen_hide;
        g_game_api.sfx_3d = api->sfx_3d;
        g_game_api.sfx_3d_at = api->sfx_3d_at;
        g_game_api.pad_effect = api->pad_effect;
        g_game_api.selection_silent = api->selection_silent;
        g_game_api.bg_img_transition = api->bg_img_transition;
        g_game_api.state_timer_get = api->state_timer_get;
        g_game_api.state_timer_set = api->state_timer_set;
        g_game_api.event_step_reset = api->event_step_reset;
        g_game_api.camera_auto_return = api->camera_auto_return;
        g_game_api.player_fall_tick = api->player_fall_tick;
        g_game_api.delta_time_get = api->delta_time_get;
        g_game_api.world_object_add = api->world_object_add;
        g_game_api.event_bg_texture_cmd = api->event_bg_texture_cmd;
        g_game_api.event_inv_item_cmd = api->event_inv_item_cmd;
        g_game_api.pickup_animate = api->pickup_animate;
        g_game_api.chara_anim_play_to_end = api->chara_anim_play_to_end;
        g_game_api.chara_spawn = api->chara_spawn;
        g_game_api.chara_spawn_flags_set = api->chara_spawn_flags_set;
        g_game_api.chara_slot_transform_get = api->chara_slot_transform_get;
        g_game_api.chara_slot_transform_set = api->chara_slot_transform_set;
        g_game_api.chara_slot_chara_id_get = api->chara_slot_chara_id_get;
        g_game_api.chara_slot_control_set = api->chara_slot_control_set;
        g_game_api.event_msg_with_audio = api->event_msg_with_audio;
        g_game_api.event_activation_type_get = api->event_activation_type_get;
        g_game_api.math_sin = api->math_sin;
        g_game_api.math_cos = api->math_cos;
        g_game_api.event_tween_linear = api->event_tween_linear;
        g_game_api.event_tween_reset = api->event_tween_reset;
        g_game_api.delta_time_set = api->delta_time_set;
        g_game_api.chara_slot_anim_flags_set = api->chara_slot_anim_flags_set;
        g_game_api.chara_slot_anim_flags_clear = api->chara_slot_anim_flags_clear;
        g_game_api.water_effect_enable = api->water_effect_enable;
        g_game_api.water_effect_disable = api->water_effect_disable;
        g_game_api.event_sys_state_branch_on_flag = api->event_sys_state_branch_on_flag;
        g_game_api.player_rot_y_get = api->player_rot_y_get;
        g_game_api.sfx_flags_play = api->sfx_flags_play;
        g_game_api.sfx_flags_pitch_play = api->sfx_flags_pitch_play;
        g_game_api.sfx_pitch_play = api->sfx_pitch_play;
        g_game_api.sfx_play_at_pos = api->sfx_play_at_pos;
        g_game_api.sfx_falloff_pitch_play = api->sfx_falloff_pitch_play;
        g_game_api.held_item_show = api->held_item_show;
        g_game_api.held_item_hide = api->held_item_hide;
        g_game_api.world_object_pos_get = api->world_object_pos_get;
        g_game_api.world_object_pos_set = api->world_object_pos_set;
        g_game_api.world_object_rot_y_set = api->world_object_rot_y_set;
        g_game_api.held_item_attach = api->held_item_attach;
        g_game_api.held_item_set = api->held_item_set;
        g_game_api.held_item_prev = api->held_item_prev;
        g_game_api.player_weapon_attack_set = api->player_weapon_attack_set;
        g_game_api.player_weapon_attack_get = api->player_weapon_attack_get;
        g_game_api.math_atan2 = api->math_atan2;
        g_game_api.event_bg_texture_fade_in = api->event_bg_texture_fade_in;
        g_game_api.event_bg_texture_fade_out = api->event_bg_texture_fade_out;
        g_game_api.sd_call = api->sd_call;
        g_game_api.sfx_attributes_update = api->sfx_attributes_update;
        g_game_api.screen_fade_finished = api->screen_fade_finished;
        g_game_api.world_object_rot_y_get = api->world_object_rot_y_get;
        g_game_api.player_move_speed_clear = api->player_move_speed_clear;
        g_game_api.rng_int = api->rng_int;
        g_game_api.rng_uint = api->rng_uint;
        g_game_api.game_warm_boot = api->game_warm_boot;
        g_game_api.screen_fade_timestep_get = api->screen_fade_timestep_get;
        g_game_api.screen_fade_timestep_set = api->screen_fade_timestep_set;
        g_game_api.player_rotate = api->player_rotate;
        g_game_api.event_wait_player_stop = api->event_wait_player_stop;
        g_game_api.event_step_increment_delayed = api->event_step_increment_delayed;
        g_game_api.event_step_increment_after_fade = api->event_step_increment_after_fade;
        g_game_api.screen_fade_status_set = api->screen_fade_status_set;
        g_game_api.game_state_update = api->game_state_update;
        g_game_api.delta_time_reset = api->delta_time_reset;
        g_game_api.screen_fade_reset_timestep = api->screen_fade_reset_timestep;
        g_game_api.ambient_sfx = api->ambient_sfx;
        g_game_api.player_vel_y_get = api->player_vel_y_get;
        g_game_api.player_vel_y_set = api->player_vel_y_set;
        g_game_api.chara_id_find = api->chara_id_find;
        g_game_api.chara_sfx = api->chara_sfx;
        g_game_api.chara_param = api->chara_param;
        g_game_api.chara_param_q12 = api->chara_param_q12;
        g_game_api.chara_anim = api->chara_anim;
        g_game_api.chara_state_number = api->chara_state_number;
        g_game_api.item_number = api->item_number;
        g_game_api.common_pickup_number = api->common_pickup_number;
        g_game_api.flag_number = api->flag_number;
        g_game_api.file_number = api->file_number;
        g_game_api.paper_map_number = api->paper_map_number;
        g_game_api.sd_command_number = api->sd_command_number;
        g_game_api.text_get = api->text_get;
        g_game_api.msg_index = api->msg_index;
        static_assert(sizeof(ShScriptArg) == sizeof(sh::script::CallArg), "CallArg layout");
        g_game_api.evb_call = reinterpret_cast<decltype(g_game_api.evb_call)>(api->evb_call);
        g_game_api.fs_read = api->fs_read;
        g_game_api.fs_wait = api->fs_wait;
        g_game_api.fs_seek = api->fs_seek;
        g_game_api.dms_fix = api->dms_fix;
        g_game_api.dms_chara_apply = api->dms_chara_apply;
        g_game_api.dms_camera_apply = api->dms_camera_apply;
        g_game_api.vc_cam_target = api->vc_cam_target;
        g_game_api.vc_watch_target = api->vc_watch_target;
        g_game_api.vc_view_position = api->vc_view_position;
        g_game_api.light_intensity_set = api->light_intensity_set;
        g_game_api.light_bones_clear = api->light_bones_clear;
        g_game_api.light_pos_set = api->light_pos_set;
        g_game_api.light_rot_set = api->light_rot_set;
        g_game_api.flashlight_attrs_fix = api->flashlight_attrs_fix;
        g_game_api.math_vec2_mag_q6 = api->math_vec2_mag_q6;
        g_game_api.chara_model_clear = api->chara_model_clear;
        g_game_api.chara_props_reset = api->chara_props_reset;
        g_game_api.player_weapon_stash = api->player_weapon_stash;
        g_game_api.player_weapon_restore = api->player_weapon_restore;
        g_game_api.map_effects_init = api->map_effects_init;
        g_game_api.sys_flags_get = api->sys_flags_get;
        g_game_api.sys_flags_set = api->sys_flags_set;
        g_game_api.bgm_status_flags_get = api->bgm_status_flags_get;
        g_game_api.bgm_status_flags_set = api->bgm_status_flags_set;
        g_game_api.sys_timer_get = api->sys_timer_get;
        g_game_api.sys_timer_set = api->sys_timer_set;
        g_game_api.event_msg_audio = api->event_msg_audio;
        g_game_api.event_msg_dimmed_bg = api->event_msg_dimmed_bg;
        g_game_api.chara_anim_unlock_play_to_end = api->chara_anim_unlock_play_to_end;
        g_game_api.paper_map_state = api->paper_map_state;
        g_game_api.paper_map_cmd = api->paper_map_cmd;
        g_game_api.map_screen_vertex_draw = api->map_screen_vertex_draw;
        g_game_api.map_screen_marking_draw = api->map_screen_marking_draw;
        g_game_api.map_screen_player_mark_draw = api->map_screen_player_mark_draw;
        g_game_api.map_box_outline_draw = api->map_box_outline_draw;
        g_game_api.sd_play_sfx = api->sd_play_sfx;
        g_game_api.vc_stereo_balance = api->vc_stereo_balance;
        g_game_api.light_intensity_get = api->light_intensity_get;
        g_game_api.chara_get = api->chara_get;
        g_game_api.chara_set = api->chara_set;
        g_game_api.chara_anim_start_keyframe_get = api->chara_anim_start_keyframe_get;
        g_game_api.math_point_from_angle = api->math_point_from_angle;
        g_game_api.chara_bones_init = api->chara_bones_init;
        g_game_api.camera_default_speeds = api->camera_default_speeds;
        g_game_api.fs_entry_loaded = api->fs_entry_loaded;
        g_game_api.xa_preload = api->xa_preload;
        g_game_api.map_effects_update = api->map_effects_update;
        g_game_api.savegame_get = api->savegame_get;
        g_game_api.savegame_set = api->savegame_set;
        g_game_api.cursor_draw = api->cursor_draw;
        g_game_api.mouse_cursor_puzzle_override = api->mouse_cursor_puzzle_override;
        g_game_api.game_timer_update = api->game_timer_update;
        g_game_api.item_remove = api->item_remove;
        g_game_api.named_call = reinterpret_cast<decltype(g_game_api.named_call)>(api->named_call);
        g_game_api.named_list = api->named_list;
        g_game_api.raw_api_enabled = api->raw_api_enabled;
        /* render */
        static_assert(sizeof(ShRenderFrameHold) == sizeof(sh::script::FrameHold), "FrameHold layout");
        g_game_api.render_frame_hold =
            reinterpret_cast<decltype(g_game_api.render_frame_hold)>(api->render_frame_hold);
        static_assert(sizeof(ShRenderSpriteRect) == sizeof(sh::script::SpriteRect), "SpriteRect layout");
        static_assert(sizeof(ShRenderSprites) == sizeof(sh::script::Sprites) &&
                          offsetof(ShRenderSprites, items) == offsetof(sh::script::Sprites, items),
                      "Sprites layout");
        g_game_api.render_sprites = reinterpret_cast<decltype(g_game_api.render_sprites)>(api->render_sprites);
        static_assert(sizeof(ShRenderTextureLoad) == sizeof(sh::script::TextureLoad), "TextureLoad layout");
        g_game_api.render_texture_load =
            reinterpret_cast<decltype(g_game_api.render_texture_load)>(api->render_texture_load);
        g_game_api.render_held_clear = api->render_held_clear;
        g_game_api.render_held_capture = api->render_held_capture;
        static_assert(sizeof(ShRenderLayer) == sizeof(sh::script::Layer), "Layer layout");
        static_assert(sizeof(ShRenderLayers) == sizeof(sh::script::Layers) &&
                          offsetof(ShRenderLayers, items) == offsetof(sh::script::Layers, items),
                      "Layers layout");
        g_game_api.render_world_redirect =
            reinterpret_cast<decltype(g_game_api.render_world_redirect)>(api->render_world_redirect);
        static_assert(sizeof(ShRenderTile) == sizeof(sh::script::Tile), "Tile layout");
        g_game_api.render_tile = reinterpret_cast<decltype(g_game_api.render_tile)>(api->render_tile);
        g_game_api.render_fade_in_level = api->render_fade_in_level;
        static_assert(sizeof(ShRenderVramCopy) == sizeof(sh::script::VramCopy), "VramCopy layout");
        g_game_api.render_vram_save = reinterpret_cast<decltype(g_game_api.render_vram_save)>(api->render_vram_save);
        g_game_api.render_vram_restore =
            reinterpret_cast<decltype(g_game_api.render_vram_restore)>(api->render_vram_restore);
        g_game_api.render_display_init = api->render_display_init;
        g_game_api.render_display_swap = api->render_display_swap;
        g_game_api.render_vblank_interval = api->render_vblank_interval;
        g_game_api.render_picture_screen = api->render_picture_screen;
        g_game_api.render_background = api->render_background;
        g_game_api.paper_map_grant = api->paper_map_grant;
        g_game_api.chara_spawn_entry_set = api->chara_spawn_entry_set;
        static_assert(sizeof(ShRenderWorldQuad) == sizeof(sh::script::WorldQuad), "WorldQuad layout");
        static_assert(sizeof(ShRenderWorldQuads) == sizeof(sh::script::WorldQuads) &&
                          offsetof(ShRenderWorldQuads, quads) == offsetof(sh::script::WorldQuads, quads) &&
                          offsetof(ShRenderWorldQuads, clip_y) == offsetof(sh::script::WorldQuads, clip_y),
                      "WorldQuads layout");
        g_game_api.render_world_quads =
            reinterpret_cast<decltype(g_game_api.render_world_quads)>(api->render_world_quads);
    }
}

extern "C" const char* ShScript_MapName(void) {
    return g_map_name.c_str();
}

extern "C" void ShScript_SetModConfigPath(const char* path) {
    sh::script::set_mod_config_path(path ? std::filesystem::path(path) : std::filesystem::path());
}

extern "C" void ShScript_Boot(const char* gamedataDir, const char* modsDir) {
    auto& host = game_host();
    if (host) {
        return;  // already booted
    }
    g_gamedata_dir = gamedataDir ? gamedataDir : "";
    g_mods_dir = modsDir ? modsDir : "";
    // gamedata/menus/*.lua and the mods' menus/*.lua fill the engine's menu registry.
    sh::script::load_menu_scripts(g_gamedata_dir, g_mods_dir, sh::menu::registry(), script_log);
    // sh.map_name() reports the active map; before the first load that is the empty name.
    g_game_api.map_name = &current_map_name;
    // Hook handlers run base first, then mods in load order (fixed for the run, so it is read once).
    {
        auto order = std::make_shared<std::map<std::string, int, std::less<>>>();
        for (const sh::mods::ResolvedMod* m : sh::mods::registry().active()) {
            order->emplace(m->candidate.manifest.id, m->load_index);
        }
        sh::hooks::bus().set_owner_order([order](std::string_view owner) {
            const auto it = order->find(owner);
            return it == order->end() ? -1 : it->second;
        });
        sh::hooks::bus().set_logger([](const std::string& message) { script_log(1, "[HOOKS] " + message); });
    }
    host = std::make_unique<sh::script::Host>(make_config());
    const auto loaded = host->load();
    if (!loaded) {
        script_log(1, loaded.error() + " (no script will run)");
        host.reset();
        return;
    }
    host->on_boot();
}

extern "C" void ShScript_OnNewGame(void) {
    if (game_host()) {
        game_host()->on_new_game();
    }
}

namespace {
void load_map_script(const std::string& map);
}

extern "C" void ShScript_OnMapLoad(const char* mapName) {
    const std::string map = mapName ? mapName : "";
    g_map_name = map;
    if (game_host()) {
        game_host()->on_map_load(map);
    }
    load_map_script(map);
}

extern "C" void ShScript_ReloadMapScript(void) {
    if (!g_map_name.empty()) {
        script_log(0, "map script reload: maps/" + g_map_name + "/events.lua");
        load_map_script(g_map_name);
    }
}

extern "C" int ShScript_RunSnippet(int target, const char* code, char* out, int cap) {
    sh::script::Host* host = target == 1 ? map_host().get() : game_host().get();
    std::string text;
    const bool ok = host != nullptr ? host->run_snippet(code ? code : "", &text)
                                    : (text = target == 1 ? "no map script is running" : "no game script is running",
                                       false);
    if (out != nullptr && cap > 0) {
        std::snprintf(out, static_cast<std::size_t>(cap), "%s", text.c_str());
    }
    return ok ? 1 : 0;
}

extern "C" int ShScript_RunTestFile(int target, const char* harness, const char* code, const char* chunk_name,
                                    char* out, int cap) {
    sh::script::Host* host = target == 1 ? map_host().get() : game_host().get();
    std::string text;
    const bool ok = host != nullptr ? host->run_test_file(harness ? harness : "", code ? code : "",
                                                          chunk_name ? chunk_name : "test", &text)
                                    : (text = std::string("SHTEST ERROR\t") +
                                              (target == 1 ? "no map script is running" : "no game script is running"),
                                       false);
    if (out != nullptr && cap > 0) {
        std::snprintf(out, static_cast<std::size_t>(cap), "%s", text.c_str());
    }
    return ok ? 1 : 0;
}

namespace {

void load_map_script(const std::string& map) {
    // The per-map script: maps/<map>/events.lua of the base data, extended by every mod's copy. A missing
    // file is the normal case; the previous map's script (and its hook handlers) is dropped either way.
    map_host().reset();
    chara_behaviors().clear();
    if (map.empty()) {
        return;
    }
    auto host = std::make_unique<sh::script::Host>(make_config());
    const auto loaded = host->run_layered("maps/" + map + "/events.lua");
    if (!loaded) {
        script_log(1, loaded.error() + " (no map script will run)");
        return;
    }
    if (loaded.value()) {
        host->on_boot();  // the map script's own entry hook, after the chunk ran
        map_host() = std::move(host);
    }
    load_map_handlers(map);
    // A handler is a Lua function named in handlers.json (a mod names its own the same way); a name no script
    // defines is reported once here, the slot then does nothing when its event runs.
    for (const MapHandler& handler : map_handlers()) {
        const bool defined = map_host() && ((!handler.name.empty() && map_host()->has_global(handler.name)) ||
                                            map_host()->has_global(handler.symbol));
        if (!defined) {
            script_log(1, "maps/" + map + "/handlers.json: slot " + std::to_string(handler.index) +
                              " names the handler '" + (handler.name.empty() ? handler.symbol : handler.name) +
                              "', which no events.lua of the map defines; the slot does nothing");
        }
    }
}

}  // namespace

namespace {
// SH_SCRIPT_HANDLER_TRACE=<file>: one line "<tick> <map> <slot> <how> <symbol>" whenever
// the dispatched slot or the way it ran changes, so the harness knows which slots ran
// (lua, lua-error; none = events.lua has no function for the slot).
void trace_map_handler(int param, const char* how, const std::string& symbol) {
    static FILE* file = [] {
        const char* path = std::getenv("SH_SCRIPT_HANDLER_TRACE");
        return path != nullptr && path[0] != '\0' ? std::fopen(path, "a") : nullptr;
    }();
    static std::string last;
    if (file == nullptr) {
        return;
    }
    std::string line = g_map_name + " " + std::to_string(param) + " " + how + " " +
                       (symbol.empty() ? "-" : symbol);
    if (line != last) {
        std::fprintf(file, "%d %s\n", g_update_ticks, line.c_str());
        std::fflush(file);
        last = std::move(line);
    }
}

}  // namespace

/* hooks: the readable name of a map handler slot (handlers.json "name"), "" when it has none. */
extern "C" const char* ShScript_MapHandlerName(int param) {
    for (const MapHandler& handler : map_handlers()) {
        if (handler.index == param) {
            return handler.name.c_str();
        }
    }
    return "";
}

extern "C" int ShScript_CallMapHandler(int param) {
    if (param < 0) {
        return 0;
    }
    if (map_host()) {
        for (const MapHandler& handler : map_handlers()) {
            if (handler.index != param) {
                continue;
            }
            const std::string& symbol =
                (!handler.name.empty() && map_host()->has_global(handler.name)) ? handler.name
                                                                                 : handler.symbol;
            if (map_host()->has_global(symbol)) {
                // Called once per tick, like the C++ handler; the Event_* steppers inside
                // advance their own state. A Lua error is logged and this tick is a no-op.
                const bool ok = map_host()->call_global(symbol);
                trace_map_handler(param, ok ? "lua" : "lua-error", symbol);
                return 1;
            }
            break;  // a named slot without a Lua function: nothing runs (MapRuntime logs it)
        }
    }
    trace_map_handler(param, "none", std::string());
    return 0;
}

namespace {

/* The names after "-- uses:" in the comment lines that open a behaviour file. */
std::vector<std::string> behaviour_modules(const std::filesystem::path& script) {
    std::vector<std::string> out;
    std::ifstream in(script);
    std::string line;
    while (std::getline(in, line)) {
        if (line.rfind("--", 0) != 0) {
            break;
        }
        const std::size_t at = line.find("uses:");
        if (at == std::string::npos) {
            continue;
        }
        std::string name;
        for (std::size_t i = at + 5; i <= line.size(); ++i) {
            const char ch = i < line.size() ? line[i] : ' ';
            /* A module of the character's own folder, `-- uses: Harry/combat`. */
            if (std::isalnum(static_cast<unsigned char>(ch)) || ch == '_' || ch == '/') {
                name += ch;
            } else if (!name.empty()) {
                out.push_back(name);
                name.clear();
            }
        }
    }
    return out;
}

// A behaviour state: chara/common.lua, chara/combat.lua, chara/fallbacks.lua (what a character uses when the
// map lacks its data), the modules `script` names in "-- uses:", then `script` and the `extra` files that exist
// (a character's own tuning of a shared script). nullptr when it fails or defines no update(slot).
// Every file is layered (Host::run_layered): the base, then each mod's copy as an extension of it.
std::unique_ptr<sh::script::Host> behaviour_host(const std::string& script, const std::vector<std::string>& extra) {
    std::unique_ptr<sh::script::Host> host = std::make_unique<sh::script::Host>(make_config());
    sh::Result<bool> common = host->run_layered("chara/common.lua");
    /* The shared enemy helpers (sight, steering, attacks, collision keyframes). */
    for (const char* shared : {"combat.lua", "fallbacks.lua"}) {
        if (common) {
            if (auto ran = host->run_layered(std::string("chara/") + shared); !ran) {
                common = ran;
            }
        }
    }
    /* Shared modules a behaviour names in its first comment lines, `-- uses: flight`
     * (chara/flight.lua), run before it in the same state. The base layer's header names them. */
    const sh::mods::LuaLayers own_layers = lua_layers_of(script);
    const std::filesystem::path header =
        !own_layers.base.empty() ? own_layers.base
                                 : (own_layers.extensions.empty() ? std::filesystem::path()
                                                                  : own_layers.extensions.front().file);
    for (const std::string& module : behaviour_modules(header)) {
        const std::string file = "chara/" + module + ".lua";
        if (common && lua_layer_exists(file)) {
            if (auto ran = host->run_layered(file); !ran) {
                common = ran;
            }
        } else if (common) {
            script_log(1, script + ": no module " + file);
        }
    }
    sh::Result<bool> own = common ? host->run_layers(own_layers) : common;
    for (const std::string& file : extra) {
        if (own && own.value()) {
            if (auto ran = host->run_layered(file); !ran) {
                own = ran;
            }
        }
    }
    if (!own || !own.value() || !host->has_global("update")) {
        script_log(1, script + ": " + (!own ? own.error() : std::string("no update(slot) function")) +
                          " (the type keeps its C++ update)");
        return nullptr;
    }
    std::string extended;
    for (const std::string& id : host->extension_mods()) {
        extended += (extended.empty() ? " (extended by " : ", ") + id;
    }
    script_log(0, "character behaviour " + script + (extended.empty() ? "" : extended + ")"));
    return host;
}

// The behaviour type running now (ShScript_CharaBehaviorRunning); nested runs restore the outer one.
int g_running_behavior = -1;

class RunningBehavior {
public:
    explicit RunningBehavior(int chara_id) : outer_(g_running_behavior) { g_running_behavior = chara_id; }
    ~RunningBehavior() { g_running_behavior = outer_; }
    RunningBehavior(const RunningBehavior&) = delete;
    RunningBehavior& operator=(const RunningBehavior&) = delete;

private:
    int outer_;
};

sh::script::Host* chara_behavior(int chara_id, const char* name) {
    auto& hosts = chara_behaviors();
    if (auto it = hosts.find(chara_id); it != hosts.end()) {
        return it->second.get();
    }
    std::unique_ptr<sh::script::Host> host;
    if (name != nullptr && name[0] != 0) {
        const std::string script = std::string("chara/") + name + "/behavior.lua";
        if (lua_layer_exists(script)) {
            host = behaviour_host(script, {});
        }
    }
    sh::script::Host* raw = host.get();
    hosts.emplace(chara_id, std::move(host));
    return raw;
}

// The generic fallback behaviour (chara/fallback_ai.lua) of a type, in a state of its own: the key is past
// every character number so it never meets the type's own behaviour state.
constexpr int kFallbackKey = 0x10000;

sh::script::Host* chara_fallback(int chara_id, const char* name) {
    auto& hosts = chara_behaviors();
    if (auto it = hosts.find(kFallbackKey | chara_id); it != hosts.end()) {
        return it->second.get();
    }
    std::unique_ptr<sh::script::Host> host;
    if (lua_layer_exists("chara/fallback_ai.lua")) {
        std::vector<std::string> extra;
        if (name != nullptr && name[0] != 0) {
            extra.push_back(std::string("chara/") + name + "/fallback_ai.lua");
        }
        host = behaviour_host("chara/fallback_ai.lua", extra);
    }
    sh::script::Host* raw = host.get();
    hosts.emplace(kFallbackKey | chara_id, std::move(host));
    return raw;
}

// A character whose behaviour update fails this many ticks in a row stops being updated (it stands still)
// until its slot holds another character: an update that throws every frame would otherwise only spam the log.
constexpr int kUpdateFailLimit = 3;
constexpr int kGuardedSlots = 128;

struct UpdateGuard {
    int key = -1;           // behaviour state of the failing character (type, or fallback | type)
    int fails = 0;
    int last_tick = -1;
};
std::array<UpdateGuard, kGuardedSlots> g_update_guards{};

// Calls update(slot) of `host` unless the character in `slot` is suspended; 1 ran, -1 failed or suspended.
int guarded_update(sh::script::Host* host, int key, const char* name, int slot) {
    if (slot <= 0 || slot >= kGuardedSlots) {
        // The player's update is never suspended.
        return host->call_global_ints("update", {slot}) ? 1 : -1;
    }
    UpdateGuard& g = g_update_guards[static_cast<std::size_t>(slot)];
    // Not updated on the previous tick, or another type: the slot holds a new character.
    if (g.key != key || g.last_tick < g_update_ticks - 1) {
        g = UpdateGuard{key, 0, g_update_ticks};
    }
    g.last_tick = g_update_ticks;
    if (g.fails >= kUpdateFailLimit) {
        return -1;
    }
    if (host->call_global_ints("update", {slot})) {
        g.fails = 0;
        return 1;
    }
    if (++g.fails == kUpdateFailLimit) {
        script_log(1, std::string("chara/") + (name != nullptr ? name : "?") + ": the update of slot " +
                          std::to_string(slot) + " failed " + std::to_string(kUpdateFailLimit) +
                          " ticks in a row; the character is suspended");
    }
    return -1;
}

}  // namespace

extern "C" int ShScript_CharaBehaviorHas(int charaId, const char* name) {
    const RunningBehavior running(charaId);  // a first call runs the script's top level
    return chara_behavior(charaId, name) != nullptr ? 1 : 0;
}

extern "C" int ShScript_CharaBehaviorRunning(void) { return g_running_behavior; }

extern "C" int ShScript_CharaFallbackUpdate(int charaId, const char* name, int slot) {
    sh::script::Host* host = chara_fallback(charaId, name);
    if (host == nullptr) {
        return 0;
    }
    return guarded_update(host, kFallbackKey | charaId, name, slot);
}

extern "C" int ShScript_CharaFallbackExists(void) {
    return lua_layer_exists("chara/fallback_ai.lua") ? 1 : 0;
}

extern "C" int ShScript_CharaBehaviorExists(const char* name) {
    if (name == nullptr || name[0] == 0) {
        return 0;
    }
    return lua_layer_exists(std::string("chara/") + name + "/behavior.lua") ? 1 : 0;
}

extern "C" int ShScript_CharaBehaviorUpdate(int charaId, const char* name, int slot) {
    const RunningBehavior running(charaId);
    sh::script::Host* host = chara_behavior(charaId, name);
    if (host == nullptr) {
        return 0;
    }
    return guarded_update(host, charaId, name, slot);
}

/* A function of an already running behaviour (an engine callback: a clip speed the script
 * computes); false when the type has no behaviour state or the call failed. */
extern "C" int ShScript_CharaBehaviorCall(int charaId, const char* fn, int a, int b, long long* out) {
    auto& hosts = chara_behaviors();
    const auto it = hosts.find(charaId);
    if (it == hosts.end() || it->second == nullptr) {
        return 0;
    }
    const RunningBehavior running(charaId);
    return it->second->call_global_int(fn, {a, b}, out) ? 1 : 0;
}

namespace {

// `mods reload` runs from the console or from a menu callback (sh.mods.reload), that is inside a Lua state the
// reload replaces: it is queued and done at the start of the next frame, outside every script.
bool g_reload_menus = false;
bool g_reload_scripts = false;

void reload_game_scripts() {
    auto& host = game_host();
    if (!host) {
        return;  // not booted (or no script could run): nothing to run again
    }
    const auto loaded = sh::script::reload_host(host, make_config());
    if (!loaded) {
        script_log(1, "mods reload: " + loaded.error() + " (no script will run)");
        return;
    }
    std::string mods;
    for (const std::string& id : host->loaded_mods()) {
        mods += (mods.empty() ? "" : ", ") + id;
    }
    script_log(0, "mods reload: game script run again" + (mods.empty() ? std::string() : "; mods: " + mods));
}

void run_pending_reloads() {
    if (g_reload_menus) {
        g_reload_menus = false;
        sh::script::reload_menu_scripts(g_gamedata_dir, g_mods_dir, sh::menu::registry(), script_log);
        script_log(0, "mods reload: menus defined again");
    }
    if (g_reload_scripts) {
        g_reload_scripts = false;
        reload_game_scripts();
    }
}

}  // namespace

extern "C" void ShScript_RequestReload(void) {
    g_reload_menus = true;
    g_reload_scripts = true;
}

extern "C" void ShScript_OnUpdate(void) {
    if (g_reload_menus || g_reload_scripts) {
        run_pending_reloads();
    }
    ++g_update_ticks;
    if (game_host()) {
        game_host()->on_update();
    }
    if (map_host()) {
        map_host()->on_update();
    }
}

std::vector<sh::script::MapHandlerSlot> sh::script::read_map_handlers(const std::filesystem::path& gamedata_dir,
                                                                      const std::filesystem::path& mods_dir,
                                                                      const std::string& map, std::string* error) {
    std::vector<MapHandlerSlot> out;
    const std::string relative = "maps/" + map + "/handlers.json";
    const auto text = sh::mods::read_data_text(gamedata_dir, mods_dir, relative);
    if (!text) {
        return out;  // no table: every param stays on the C++ handler
    }
    const auto parsed = sh::json::parse(text.value());
    const sh::json::Value* list = parsed && parsed.value().is_object() ? parsed.value().find("handlers") : nullptr;
    if (list == nullptr || !list->is_array()) {
        if (error != nullptr) {
            *error = relative + (parsed ? ": no handlers array" : ": cannot parse");
        }
        return out;
    }
    for (const sh::json::Value& entry : list->array()) {
        if (!entry.is_object()) {
            continue;
        }
        const sh::json::Value* index = entry.find("index");
        const sh::json::Value* symbol = entry.find("symbol");
        if (index == nullptr || !index->is_integer() || symbol == nullptr || !symbol->is_string()) {
            continue;  // NULL slots and unknown entries are simply not Lua handlers
        }
        const sh::json::Value* name = entry.find("name");
        out.push_back({static_cast<int>(index->integer()),
                       (name != nullptr && name->is_string()) ? name->string() : std::string(), symbol->string()});
    }
    return out;
}
