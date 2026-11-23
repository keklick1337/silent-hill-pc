// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)

#include "features/dump/game_dump.h"

#include "engine/app/pc_paths.h"
#include "engine/camera/game_camera.h"
#include "engine/config/pc_config.h"
#include "engine/debug/commands/frame_capture.h"
#include "engine/debug/overlay/debug_overlay.h"
#include "engine/debug/dbg_trace.h"
#include "engine/devtools/state_dump.h"
#include "engine/effects/map_effect_view.h"
#include "engine/entities/entity_world.h"
#include "engine/entities/game/remains_host.h"
#include "engine/game_state/game_state.h"
#include "engine/game_state/game_state_inventory.h"
#include "engine/game_state/game_state_world.h"
#include "engine/game_state/session.h"
#include "engine/gameflow/game/game_link.h"
#include "engine/gameflow/state_names.h"
#include "engine/log/log.h"
#include "engine/log/sh_log.h"
#include "engine/player/player.h"
#include "engine/runtime/world_object_runtime.h"
#include "engine/save/game_saves.h"
#include "engine/world/cell_stream.h"
#include "engine/world/object_queue.h"
#include "features/dump/perf_sampler.h"
#include "render/gpu/device_host.h"
#include "render/gpu/renderer.h"
#include "render/settings/render_preset.h"
#include "ui/screens/save_screen_game.h"

#include <chrono>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <sstream>

extern int g_DebugCamEnabled;

#ifndef SH_GIT_REVISION
#define SH_GIT_REVISION ""
#endif

namespace sh::dump {

namespace {

namespace fs = std::filesystem;
namespace gs = sh::game_state;
namespace dt = sh::devtools;
using sh::game::PlayerVar;

constexpr std::size_t kLogLines = 200;

fs::path dumps_dir() { return fs::path(PcPaths_ExeDir()) / "dumps"; }

dt::DumpPoint point(const sh::entities::Vec3& v) { return {v.x, v.y, v.z}; }

std::string local_time(std::chrono::system_clock::time_point now) {
    const std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tm = {};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char text[40];
    std::strftime(text, sizeof(text), "%Y-%m-%dT%H:%M:%S", &tm);
    return text;
}

// The player is in control: no menu, event, demo, transition or fade.
bool free_gameplay() {
    if (gs::game_state() != gs::GameState::InGame || gs::sys_state() != gs::SysState::Gameplay || gs::demo_active()) {
        return false;
    }
    const std::uint32_t moving = sh::game::process_flag::kRoomTransition | sh::game::process_flag::kOverlayTransition;
    return (sh::game::session().system.process.process_flags & moving) == 0 && sh::gameflow::game::screen_fade_none();
}

bool map_running() {
    const gs::GameState state = gs::game_state();
    return gs::map_overlay_name() != nullptr && (state == gs::GameState::InGame || state == gs::GameState::MapEvent);
}

std::vector<std::string> character_flags(std::uint32_t flags) {
    static constexpr struct {
        std::uint32_t bit;
        const char* name;
    } kNames[] = {
        {sh::entities::kFlagPadlockBroken, "PadlockBroken"}, {sh::entities::kFlagDowned, "Downed"},
        {sh::entities::kFlagHit, "Hit"},                     {sh::entities::kFlagAttackVeto, "AttackVeto"},
        {sh::entities::kFlagCameraExcluded, "CameraExcluded"}, {sh::entities::kFlagDamaged, "Damaged"},
        {sh::entities::kFlagDead, "Dead"},                   {sh::entities::kFlagAttackTicked, "AttackTicked"},
        {sh::entities::kFlagNoRadioStatic, "NoRadioStatic"},
    };
    std::vector<std::string> out;
    for (const auto& n : kNames) {
        if ((flags & n.bit) != 0) {
            out.emplace_back(n.name);
        }
    }
    return out;
}

// Inside the picture's pyramid: in front of the camera and within the frame, by the frame's own projection.
bool in_frustum(const sh::entities::Vec3& p, int frame_w, int frame_h) {
    std::int32_t view[3];
    gs::view_point(p.x, p.y, p.z, view);
    const std::int64_t h = gs::view_projection().h;
    if (view[2] <= 0 || h <= 0) {
        return false;
    }
    // Screen units: the picture is 224 tall, and as wide as the frame's shape makes it.
    const std::int64_t half_h = 112;
    const std::int64_t half_w = frame_h > 0 ? half_h * frame_w / frame_h : 160;
    const std::int64_t ax = view[0] < 0 ? -static_cast<std::int64_t>(view[0]) : view[0];
    const std::int64_t ay = view[1] < 0 ? -static_cast<std::int64_t>(view[1]) : view[1];
    return ax * h <= half_w * view[2] && ay * h <= half_h * view[2];
}

void fill_player(dt::StateDump& d) {
    const sh::game::Player& player = sh::game::player();
    d.position = point(gs::chara_position(0));
    d.yaw = gs::chara_rotation(0).y & 0xFFF;
    d.health = gs::chara_health(0);
    d.aiming = gs::player_aiming();
    d.weapon_attack = gs::player_weapon_attack();
    const char* weapon = gs::item_name(gs::held_item_id());
    d.weapon = weapon != nullptr && !gs::held_item_hidden() ? weapon : "";
    d.ammo = static_cast<int>(player.get(PlayerVar::Ammo));
    const gs::PlayerAnimDebug anim = gs::player_anim_debug();
    d.anim_status = anim.status;
    d.anim_flags = anim.flags;
    d.anim_keyframe = static_cast<int>(player.get(PlayerVar::BodyAnimKeyframe));
    d.control_state = anim.control;
    d.control_step = anim.step;
    d.upper_body_state = anim.upper_body;
}

void fill_camera(dt::StateDump& d) {
    const gs::CameraDebug cam = gs::camera_debug();
    d.camera_position = {cam.position[0], cam.position[1], cam.position[2]};
    d.camera_look_at = {cam.look_at[0], cam.look_at[1], cam.look_at[2]};
    d.camera_pitch = cam.pitch;
    d.camera_yaw = cam.yaw;
    d.camera_roll = cam.roll;
    const gs::ViewProjection projection = gs::view_projection();
    d.projection_distance = projection.h;
    d.projection_offset_x = projection.offset_x;
    d.projection_offset_y = projection.offset_y;
    const sh::camera::CameraState& state = sh::camera::game_camera().state();
    d.half_angle_x = state.screen_half_angle_x;
    d.half_angle_y = state.screen_half_angle_y;
    d.camera_flags = state.flags;
    d.script_camera = (state.flags & sh::camera::camera_flags::user_camera) != 0;
    d.script_look_at = (state.flags & sh::camera::camera_flags::user_watch) != 0;
    d.free_camera = g_DebugCamEnabled != 0;
    d.camera_zone = state.current_road.road;
    std::int16_t rotation[9];
    gs::camera_rotation(rotation);
    for (std::size_t i = 0; i < 9; ++i) {
        d.view_rotation[i] = rotation[i];
    }
    std::int32_t origin[3];
    gs::camera_position(origin);
    d.view_origin = {origin[0], origin[1], origin[2]};
}

void fill_flags_and_items(dt::StateDump& d) {
    if (const std::uint32_t* words = gs::save_event_flags(); words != nullptr) {
        for (int flag = 0; flag < gs::kEventFlagWords * 32; ++flag) {
            if (((words[flag >> 5] >> (flag & 31)) & 1u) == 0) {
                continue;
            }
            const char* name = DbgTrace_EventFlagName(flag);
            d.flags.push_back(name != nullptr ? name : "unnamed:" + std::to_string(flag));
        }
    }
    const int shown = gs::inventory_shown_slots();
    for (int i = 0; i < shown; ++i) {
        const sh::inventory::Slot slot = gs::inventory_slot(i);
        std::string name = gs::inventory_item_name(i);
        if (name.empty()) {
            if (slot.item == sh::inventory::kNoItem) {
                continue;
            }
            const char* known = gs::item_name(slot.item);
            name = known != nullptr ? known : "unnamed:" + std::to_string(slot.item);
        }
        d.inventory.push_back({std::move(name), static_cast<int>(slot.count)});
    }
}

void fill_characters(dt::StateDump& d) {
    for (int slot = 0; slot <= gs::kNpcSlots; ++slot) {
        const int id = gs::chara_id(slot);
        if (id == gs::chara::kNone) {
            continue;
        }
        dt::DumpCharacter c;
        c.slot = slot;
        const char* name = gs::chara_name(id);
        c.name = name != nullptr ? name : DbgTrace_CharaName(id);
        const sh::entities::Vec3 position = gs::chara_position(slot);
        c.position = point(position);
        c.yaw = gs::chara_rotation(slot).y & 0xFFF;
        c.health = gs::chara_health(slot);
        gs::chara_control(slot, &c.control_state, &c.control_step);
        if (const sh::entities::Character* chara = gs::characters().character(slot); chara != nullptr) {
            c.flags = character_flags(chara->flags());
        }
        c.model_loaded = gs::chara_model_loaded(id);
        c.in_camera_frustum = in_frustum(position, d.frame_width, d.frame_height);
        d.characters.push_back(std::move(c));
    }
}

void fill_world(dt::StateDump& d) {
    const sh::world::CellStream& cells = sh::world::cell_stream();
    char file[64];
    d.exterior = cells.exterior();
    if (cells.library().file != sh::world::kNoFile) {
        d.library_file = DbgTrace_FileName(cells.library().file, file, sizeof(file));
    }
    for (int i = 0; i < cells.slot_count(); ++i) {
        const sh::world::CellStream::Slot& slot = cells.slot(static_cast<std::size_t>(i));
        if (slot.file == sh::world::kNoFile) {
            continue;
        }
        d.cells.push_back({i, slot.cell.x, slot.cell.z, DbgTrace_FileName(slot.file, file, sizeof(file)), slot.ready});
    }

    for (sh::world::WorldObjectRuntime::ObjectInfo& info : sh::world::world_object_runtime().describe()) {
        dt::DumpObject o;
        o.name = std::move(info.name);
        o.model = std::move(info.model);
        o.item_model = std::move(info.item_model);
        o.position = {info.position[0], info.position[1], info.position[2]};
        o.rotation = {info.rotation[0], info.rotation[1], info.rotation[2]};
        o.pose_resolved = info.pose_resolved;
        o.queued = info.queued;
        // ObjectModel::library: 1 the item library, 2 the map's global one, 3 + n the cell in resident slot n.
        if (info.library == sh::world::kItemModelLibrary) {
            o.library = "item model file";
        } else if (info.library == 1) {
            o.library = "item";
        } else if (info.library == 2) {
            o.library = "global";
        } else if (info.library >= 3 && info.library - 3 < cells.slot_count()) {
            const sh::world::CellStream::Slot& slot = cells.slot(static_cast<std::size_t>(info.library - 3));
            o.library = "cell " + std::to_string(slot.cell.x) + "," + std::to_string(slot.cell.z);
            o.library_slot = info.library - 3;
        } else {
            o.library = "none";
        }
        d.objects.push_back(std::move(o));
    }

    const int effect_slots = sh::effects::effect_slot_count();
    for (int i = 0; i < effect_slots; ++i) {
        const sh::effects::EffectSlot slot = sh::effects::load_effect_slot(i);
        if (slot.kind != 0) {
            d.effects.push_back({i, slot.kind, {slot.x, slot.y, slot.z}});
        }
    }

    const sh::game::EnvironmentMix& env = sh::game::session().system.environment;
    d.flashlight_on = env.flashlight_on != 0;
    d.flashlight_unavailable = env.flashlight_unavailable != 0;
    const sh::game::EnvironmentState& zone = env.transition[env.flashlight_on != 0 ? 1 : 0];
    d.fog_mode = zone.preset.fog_mode;
    d.fog_distance = zone.fog_distance;
    d.fog_color = {zone.preset.fog_color.r, zone.preset.fog_color.g, zone.preset.fog_color.b};
    d.light_mode = zone.preset.light_mode;
    d.light_level_max = zone.preset.light_level_max;
    d.spot_light_intensity = zone.preset.spot_light_intensity;
    d.world_light_intensity = zone.preset.world_light_intensity;
}

void fill_config(dt::StateDump& d) {
    const int preset = g_PcConfig.graphicsPreset;
    d.graphics_preset =
        preset >= 0 && preset < RENDER_PRESET_COUNT ? RenderPreset_Name(preset) : std::to_string(preset);
    d.renderer = g_PcConfig.renderer;
    if (sh::gpu::Renderer* renderer = sh::gpu::renderer(); renderer != nullptr) {
        d.renderer = std::string(sh::gpu::backend_name(renderer->host().backend()));
        renderer->frame_size(&d.frame_width, &d.frame_height);
    }
    d.window_width = g_PcConfig.windowWidth;
    d.window_height = g_PcConfig.windowHeight;
    d.widescreen_mode = g_PcConfig.widescreenMode;
    d.render_scale = g_PcConfig.renderScale;
    d.native_resolution = g_PcConfig.nativeResolution != 0;
}

// Where the time went in the seconds before the dump, and how much the map holds.
void fill_performance(dt::StateDump& d) {
    d.performance = perf_snapshot();
    dt::DumpPerformance& p = d.performance;
    p.characters = static_cast<int>(d.characters.size());
    p.effect_slots = static_cast<int>(d.effects.size());
    const sh::entities::RemainsStore& remains = sh::entities::game::remains::store();
    for (const auto& [map, kept] : remains.maps()) {
        p.remains_bodies_all_maps += static_cast<int>(kept.corpses.size());
        if (map == d.map) {
            p.remains_bodies = static_cast<int>(kept.corpses.size());
            p.remains_stains = static_cast<int>(kept.stains.size());
        }
    }
}

// The progress into `path` with the live player's place in it, as an instant quick save stores it; the session's
// own record of the last save point is put back, so taking a dump changes nothing in the run.
bool write_save(const fs::path& path) {
    namespace link = sh::gameflow::game;
    const link::SavedPlayer kept = link::saved_player();
    link::SavedPlayer now = kept;
    const sh::entities::Vec3 position = gs::chara_position(0);
    now.x = position.x;
    now.z = position.z;
    now.rotation_y = gs::chara_rotation(0).y;
    now.health = gs::chara_health(0);
    link::store_saved_player(now);
    const int written = sh::save::game_saves().save_file(path.string());
    link::store_saved_player(kept);
    return written > 0;
}

Result<void> write_text(const fs::path& path, const std::string& text) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
    out.close();
    if (!out) {
        return Result<void>::fail("cannot write " + path.string());
    }
    return Result<void>::ok();
}

// ---- dump load ----

enum class LoadStep { None, WaitStart, WaitScreen, WaitMap };

struct PendingLoad {
    LoadStep step = LoadStep::None;
    std::string id;
    fs::path folder;
    dt::DumpRestore restore;
};
PendingLoad s_load;

void load_failed(const char* why) {
    SH_LOG_ERROR(SAVE, "dump load %s: %s", s_load.id.c_str(), why);
    SH_DBG_ECHO("dump load %s: %s", s_load.id.c_str(), why);
    s_load = PendingLoad{};
}

void apply_pose() {
    const dt::DumpPoint& p = s_load.restore.position;
    gs::place_player(p.x, p.z, &p.y);
    gs::set_chara_rotation_y(0, static_cast<std::int16_t>(s_load.restore.yaw & 0xFFF));
    SH_LOG_INFO(SAVE, "dump %s restored: map %s, Harry at %.2f %.2f %.2f, yaw %d deg", s_load.id.c_str(),
                s_load.restore.map.c_str(), p.x / 4096.0, p.y / 4096.0, p.z / 4096.0,
                ((s_load.restore.yaw & 0xFFF) * 360) >> 12);
    SH_DBG_ECHO("dump %s restored", s_load.id.c_str());
    s_load = PendingLoad{};
}

bool on_dump_map() {
    const char* map = gs::map_overlay_name();
    return map != nullptr && s_load.restore.map == map;
}

}  // namespace

Result<Written> write(std::string_view note) {
    const auto now = std::chrono::system_clock::now();
    dt::StateDump d;
    d.time = local_time(now);
    d.revision = SH_GIT_REVISION;
    d.note = std::string(note);
    const int map = gs::current_map();
    const char* overlay = gs::map_overlay_name();
    d.map = overlay != nullptr ? overlay : "";
    if (const char* description = gs::map_description(map); description != nullptr && overlay != nullptr) {
        d.map_description = description;
    }
    d.room = gs::save_room();
    d.game_state = std::string(sh::gameflow::game_state_name(gs::game_state_raw()));
    d.system_state = std::string(sh::gameflow::sys_state_name(gs::sys_state_raw()));
    d.free_gameplay = free_gameplay();
    fill_config(d);
    fill_flags_and_items(d);
    if (map_running()) {
        fill_player(d);
        fill_camera(d);
        fill_characters(d);
        fill_world(d);
        d.state_saved = true;  // settled below, once the folder exists
    }
    fill_performance(d);
    sh::log::Logger::instance().flush();
    d.log = sh::log::Logger::instance().recent_lines(kLogLines);

    const std::uint64_t stamp = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(now.time_since_epoch()).count());
    const std::string id = dt::state_dump_id(dt::state_dump_json(d, ""), stamp);
    const fs::path folder = dumps_dir() / id;
    std::error_code ec;
    fs::create_directories(folder, ec);
    if (ec) {
        return Result<Written>::fail("cannot create " + folder.string() + ": " + ec.message());
    }

    if (d.state_saved) {
        d.state_saved = write_save(folder / "state.sav");
        if (!d.state_saved) {
            SH_LOG_WARN(SAVE, "dump %s: state.sav was not written", id.c_str());
        }
    }
    if (auto r = write_text(folder / "dump.json", dt::state_dump_json(d, id)); !r) {
        return Result<Written>::fail(r.error());
    }
    const fs::path shot = folder / "shot.png";
    sh::debug::console_print(sh::debug::save_screenshot(shot.string(), false).c_str());
    if (!fs::exists(shot, ec)) {
        SH_LOG_WARN(CONSOLE, "dump %s: no frame to take shot.png from", id.c_str());
    }
    return Result<Written>::ok(Written{id, folder.string()});
}

Result<std::string> load(std::string_view id) {
    if (!dt::valid_dump_id(id)) {
        return Result<std::string>::fail("dump load <id>: the id is the 8 hex digits 'dump' printed");
    }
    const fs::path folder = dumps_dir() / std::string(id);
    std::ifstream in(folder / "dump.json", std::ios::binary);
    if (!in) {
        return Result<std::string>::fail("no dump " + std::string(id) + " (" + (folder / "dump.json").string() + ")");
    }
    std::ostringstream text;
    text << in.rdbuf();
    auto restore = dt::parse_state_dump(text.str());
    if (!restore) {
        return Result<std::string>::fail("dump.json: " + restore.error());
    }
    if (restore.value().state_saved &&
        sh::save::GameSaves::file_state((folder / "state.sav").string()) <= 0) {
        return Result<std::string>::fail("state.sav of dump " + std::string(id) + " is missing or damaged");
    }
    if (restore.value().map.empty()) {
        return Result<std::string>::fail("dump " + std::string(id) + " was taken outside a map: nothing to restore");
    }
    s_load = PendingLoad{LoadStep::WaitStart, std::string(id), folder, std::move(restore.value())};
    SH_LOG_INFO(SAVE, "dump load %s: queued (%s)", s_load.id.c_str(), folder.string().c_str());
    return Result<std::string>::ok(folder.string());
}

void tick() {
    switch (s_load.step) {
        case LoadStep::None:
            return;
        case LoadStep::WaitStart:
            if (!free_gameplay()) {
                return;
            }
            if (!s_load.restore.state_saved) {
                // A dump without a save: only the place can be restored, and only on its map.
                if (!on_dump_map()) {
                    load_failed("the dump has no state.sav; load its map first ('map <name>'), then repeat");
                    return;
                }
                apply_pose();
                return;
            }
            PcSaveGame_OpenFileLoad((s_load.folder / "state.sav").string().c_str());
            s_load.step = LoadStep::WaitScreen;
            return;
        case LoadStep::WaitScreen:
            // The load screen takes over on the next state step, then the map load.
            if (gs::game_state() != gs::GameState::InGame) {
                s_load.step = LoadStep::WaitMap;
            }
            return;
        case LoadStep::WaitMap:
            if (!free_gameplay()) {
                return;
            }
            if (!on_dump_map()) {
                load_failed("the save did not bring the dump's map up");
                return;
            }
            apply_pose();
            return;
    }
}

}  // namespace sh::dump
