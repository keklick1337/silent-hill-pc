// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)

#include "engine/runtime/world_object_runtime.h"

#include "engine/config/pc_config.h"
#include "engine/game_state/game_state.h"
#include "engine/game_state/game_state_world.h"
#include "engine/log/sh_log.h"
#include "engine/script/script_host.h"
#include "engine/vfs/vfs_data_json.h"
#include "engine/world/disc_poses.h"
#include "engine/world/object_queue.h"
#include "formats/convert/gamefiles/game_files.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>

extern "C" const char* PcPort_GetGameDataPath(void);

namespace sh::world {

namespace gs = sh::game_state;

namespace {

constexpr long long kSummaryEvery = 600;  // frames between the periodic world_objects_shadow summaries

bool shadow_enabled() { return g_PcConfig.worldObjectsShadow != 0; }

// The map's table registry and its overlay file, read once per map load when an object needs a disc pose.
struct DiscSource {
    bool read = false;
    Result<OverlayTableRegistry> registry = Result<OverlayTableRegistry>::fail("not read");
    std::vector<std::uint8_t> overlay;
};

void read_disc_source(const std::string& map, DiscSource& src) {
    src.read = true;
    const std::string relative = "maps/" + map + "/tables.json";
    const std::optional<std::string> text = ShVfs_ReadDataJsonText(relative.c_str(), PcPort_GetGameDataPath());
    if (!text) {
        src.registry = Result<OverlayTableRegistry>::fail("no map tables registry");
        return;
    }
    src.registry = parse_overlay_tables(*text);
    if (!src.registry) {
        return;
    }
    if (auto bytes = sh::convert::gamefiles::read_game_file(PcPort_GetGameDataPath(), src.registry.value().bin)) {
        src.overlay = std::move(*bytes);
    }
}

}  // namespace

WorldObjectRuntime::WorldObjectRuntime() = default;
WorldObjectRuntime::~WorldObjectRuntime() = default;

WorldObjectRuntime& world_object_runtime() {
    static WorldObjectRuntime runtime;
    return runtime;
}

namespace {

// world_object_runtime() is built before this handler is registered, so it is still alive here.
void log_summary_at_exit() { world_object_runtime().log_summary("exit"); }

}  // namespace

void WorldObjectRuntime::log_summary(const char* why) const {
    if (!shadow_enabled() || map_.empty() || !table_) {
        return;
    }
    SH_LOG("[WOSHADOW] %s: data path, %lld frames drawn, %lld with objects drawn, %lld drawn-set changes (%s)",
           map_.c_str(), frames_, frames_with_objects_, set_changes_, why);
}

bool WorldObjectRuntime::flag(const std::string& name) {
    const int number = gs::flag_number(name.c_str());
    if (number < 0) {
        for (const std::string& seen : unknown_flags_) {
            if (seen == name) {
                return false;
            }
        }
        unknown_flags_.push_back(name);
        SH_WARN("[WORLDOBJ] %s: unknown event flag \"%s\" in world_objects.json (read as clear)", map_.c_str(),
                name.c_str());
        return false;
    }
    return gs::event_flag(number);
}

void WorldObjectRuntime::apply_model(std::size_t index, std::string_view model) {
    char want[9] = {};
    std::snprintf(want, sizeof(want), "%.*s", static_cast<int>(model.size() < 8 ? model.size() : 8), model.data());
    // Setting the name drops the drawer's cached model lookup: only on a change.
    if (std::strncmp(applied_[index].data(), want, 9) != 0) {
        gs::world_object_record_model(index, want);
        std::memcpy(applied_[index].data(), want, 9);
    }
}

bool WorldObjectRuntime::load(std::string_view map) {
    map_ = std::string(map);
    table_.reset();
    states_.clear();
    resolved_.clear();
    applied_.clear();
    script_.reset();
    script_has_update_ = false;
    unknown_flags_.clear();
    frames_ = frames_with_objects_ = set_changes_ = 0;
    previous_.clear();
    queued_.clear();

    const std::string relative = "maps/" + map_ + "/world_objects.json";
    // Base + every active mod's patch (objects by "name"), in load order.
    const std::optional<std::string> text = ShVfs_ReadDataJsonText(relative.c_str(), PcPort_GetGameDataPath());
    Result<WorldObjectTable> loaded =
        text ? parse_world_objects(*text) : Result<WorldObjectTable>::fail("cannot open " + relative);
    if (!loaded) {
        SH_ERR("[WORLDOBJ] %s: no world objects drawn: %s: %s", map_.c_str(), relative.c_str(),
               loaded.error().c_str());
        return false;
    }
    table_ = std::move(loaded.value());
    states_ = initial_states(*table_);
    resolved_.assign(table_->objects.size(), true);
    DiscSource disc;
    for (std::size_t i = 0; i < table_->objects.size(); ++i) {
        const WorldObjectDef& def = table_->objects[i];
        if (!def.pose) {
            continue;
        }
        if (!disc.read) {
            read_disc_source(map_, disc);
        }
        Result<DiscPose> pose = disc.registry ? resolve_disc_pose(disc.registry.value(), *def.pose, disc.overlay)
                                              : Result<DiscPose>::fail(disc.registry.error());
        if (!pose) {
            SH_ERR("[WORLDOBJ] %s: object %s: pose %s[%d] not resolved: %s", map_.c_str(), def.name.c_str(),
                   def.pose->disc_table.c_str(), def.pose->index, pose.error().c_str());
            resolved_[i] = false;
            continue;
        }
        states_[i].position = pose.value().position;
        states_[i].rotation = pose.value().rotation;
    }
    object_item_models_clear();
    for (const WorldObjectDef& def : table_->objects) {
        if (!def.item_model.empty()) {
            object_item_model_set(def.model, {def.item_model, def.item_model_scale});
        }
    }
    // The draw records start as the states say.
    gs::world_object_records_reset(table_->objects.size());
    applied_.assign(table_->objects.size(), std::array<char, 9>{});
    for (std::size_t i = 0; i < table_->objects.size(); ++i) {
        apply_model(i, states_[i].model);
        gs::world_object_record_set_pose(i, states_[i].position, states_[i].rotation);
    }
    // The map's script: the base file extended by every mod's copy (Host::run_layered).
    script_ = std::make_unique<sh::script::Host>(sh::script::game_host_config());
    Result<bool> ran = script_->run_layered("maps/" + map_ + "/world_objects.lua");
    if (!ran) {
        SH_ERR("[WORLDOBJ] %s: world_objects.lua: %s (objects stay as the data says)", map_.c_str(),
               ran.error().c_str());
        script_.reset();
    } else if (!ran.value()) {
        script_.reset();
    } else {
        script_has_update_ = script_->has_global("update");
        SH_LOG("[WORLDOBJ] %s: script %s", map_.c_str(), script_->script_path().c_str());
    }
    return true;
}

void WorldObjectRuntime::init(std::string_view map) {
    static bool exit_hook = false;
    if (!exit_hook) {
        exit_hook = true;
        (void)world_object_runtime();  // built before the registration: statics built later die before the handler
        std::atexit(log_summary_at_exit);
    }
    log_summary("map change");
    if (!load(map)) {
        return;
    }
    if (shadow_enabled()) {
        SH_LOG("[WOSHADOW] %s: data path, %d objects", map_.c_str(), static_cast<int>(table_->objects.size()));
    }
}

void WorldObjectRuntime::sync_from_record(std::size_t index) {
    if (!table_->objects[index].script) {
        return;
    }
    const gs::ObjectPose record = gs::world_object_record_pose(index);
    WorldObjectState& s = states_[index];
    s.position = record.position;
    s.rotation = record.rotation;
    s.model = record.model;
}

std::vector<std::size_t> WorldObjectRuntime::update_and_select() {
    if (!table_) {
        return {};
    }
    if (script_has_update_ && script_ != nullptr) {
        if (!script_->call_global_ints("update", {static_cast<long long>(gs::delta_time())})) {
            SH_ERR("[WORLDOBJ] %s: world_objects.lua update failed, script stopped", map_.c_str());
            script_.reset();
            script_has_update_ = false;
        }
    }
    for (std::size_t i = 0; i < states_.size(); ++i) {
        sync_from_record(i);
    }
    // near_pickup_draw (not in the original, off by default): pickups also drawn from the next cell.
    WorldObjectNear near;
    if (g_PcConfig.nearPickupDraw != 0) {
        near = [](const WorldObjectDef&, const std::array<std::int32_t, 3>& position) {
            return gs::common_pickup_in_range(position);
        };
    }
    const sh::entities::Vec3 player = gs::chara_position(0);
    const std::vector<std::size_t> selected = select_world_objects(
        *table_, &states_, player.x, player.z, [this](const std::string& name) { return flag(name); }, near);
    std::vector<std::size_t> out;
    out.reserve(selected.size());
    for (const std::size_t i : selected) {
        if (resolved_[i]) {
            out.push_back(i);
        }
    }
    return out;
}

void WorldObjectRuntime::draw() {
    if (!table_) {
        return;
    }
    const std::vector<std::size_t> selected = update_and_select();
    for (const std::size_t i : selected) {
        gs::world_object_record_draw(i);
    }
    queued_ = selected;
    if (!shadow_enabled()) {
        return;
    }
    ++frames_;
    if (!selected.empty()) {
        ++frames_with_objects_;
    }
    if (frames_ > 1 && selected != previous_) {
        ++set_changes_;
    }
    previous_ = selected;
    if (frames_ % kSummaryEvery == 0) {
        log_summary("periodic");
    }
}

std::vector<WorldObjectRuntime::ObjectInfo> WorldObjectRuntime::describe() {
    std::vector<ObjectInfo> out;
    if (!table_ || !on_loaded_map()) {
        return out;
    }
    out.reserve(table_->objects.size());
    for (std::size_t i = 0; i < table_->objects.size(); ++i) {
        sync_from_record(i);
        const WorldObjectDef& def = table_->objects[i];
        const WorldObjectState& s = states_[i];
        ObjectInfo info;
        info.name = def.name;
        info.model = s.model;
        info.item_model = def.item_model;
        info.position = s.position;
        info.rotation = s.rotation;
        info.pose_resolved = resolved_[i];
        info.queued = std::find(queued_.begin(), queued_.end(), i) != queued_.end();
        info.library = gs::world_object_record_library(i);
        out.push_back(std::move(info));
    }
    return out;
}

bool WorldObjectRuntime::on_loaded_map() const {
    const char* loaded = gs::map_overlay_name();
    return loaded != nullptr && map_ == loaded;
}

int WorldObjectRuntime::find(std::string_view name, const WorldObjectDef** def, std::string* why) const {
    // The set outlives its map until the next map's init; another map must not reach it.
    const char* loaded = gs::map_overlay_name();
    if (!table_ || (loaded != nullptr && map_ != loaded)) {
        *why = "no world objects on this map";
        return -1;
    }
    const int index = find_world_object(*table_, name);
    if (index < 0) {
        *why = "no world object \"" + std::string(name) + "\" on " + map_;
        return -1;
    }
    *def = &table_->objects[static_cast<std::size_t>(index)];
    return index;
}

int WorldObjectRuntime::find_script(std::string_view name, std::string* why) const {
    const WorldObjectDef* def = nullptr;
    const int index = find(name, &def, why);
    if (index >= 0 && !def->script) {
        *why = "world object \"" + std::string(name) + "\" is not a script object";
        return -1;
    }
    return index;
}

Result<void> WorldObjectRuntime::set_position(std::string_view name, std::int32_t x, std::int32_t y,
                                              std::int32_t z) {
    std::string why;
    const int index = find_script(name, &why);
    if (index < 0) {
        return Result<void>::fail(why);
    }
    const auto i = static_cast<std::size_t>(index);
    const gs::ObjectPose record = gs::world_object_record_pose(i);
    gs::world_object_record_set_pose(i, {x, y, z}, record.rotation);
    sync_from_record(i);
    return Result<void>::ok();
}

Result<void> WorldObjectRuntime::set_rotation(std::string_view name, std::int32_t x, std::int32_t y,
                                              std::int32_t z) {
    std::string why;
    const int index = find_script(name, &why);
    if (index < 0) {
        return Result<void>::fail(why);
    }
    const auto i = static_cast<std::size_t>(index);
    const gs::ObjectPose record = gs::world_object_record_pose(i);
    // Q12 angles kept in 16 bits: a full turn (and more) wraps.
    gs::world_object_record_set_pose(
        i, record.position,
        {static_cast<std::int16_t>(x), static_cast<std::int16_t>(y), static_cast<std::int16_t>(z)});
    sync_from_record(i);
    return Result<void>::ok();
}

Result<void> WorldObjectRuntime::set_visible(std::string_view name, std::optional<bool> visible) {
    std::string why;
    const int index = find_script(name, &why);
    if (index < 0) {
        return Result<void>::fail(why);
    }
    states_[static_cast<std::size_t>(index)].visible = visible;
    return Result<void>::ok();
}

Result<void> WorldObjectRuntime::set_model(std::string_view name, std::string_view model) {
    std::string why;
    const int index = find_script(name, &why);
    if (index < 0) {
        return Result<void>::fail(why);
    }
    if (model.empty() || model.size() > 8) {
        return Result<void>::fail("model name must be 1..8 characters");
    }
    apply_model(static_cast<std::size_t>(index), model);
    sync_from_record(static_cast<std::size_t>(index));
    return Result<void>::ok();
}

Result<WorldObjectRuntime::Pose> WorldObjectRuntime::pose(std::string_view name) {
    std::string why;
    const WorldObjectDef* def = nullptr;
    const int index = find(name, &def, &why);
    if (index < 0) {
        return Result<Pose>::fail(why);
    }
    sync_from_record(static_cast<std::size_t>(index));
    const WorldObjectState& s = states_[static_cast<std::size_t>(index)];
    return Result<Pose>::ok(Pose{s.position, s.rotation});
}

Result<void*> WorldObjectRuntime::binding(std::string_view first, int count) {
    std::string why;
    const WorldObjectDef* def = nullptr;
    const int index = find(first, &def, &why);
    if (index < 0) {
        return Result<void*>::fail(why);
    }
    if (count < 1 || static_cast<std::size_t>(index) + static_cast<std::size_t>(count) > table_->objects.size()) {
        return Result<void*>::fail("world objects from \"" + std::string(first) + "\": " + std::to_string(count) +
                                   " in a row needed");
    }
    for (int k = 0; k < count; ++k) {
        const WorldObjectDef& d = table_->objects[static_cast<std::size_t>(index + k)];
        if (!d.script) {
            return Result<void*>::fail("world object \"" + d.name +
                                       "\" is bound to a map variable but not a script object");
        }
    }
    return Result<void*>::ok(gs::world_object_record_address(static_cast<std::size_t>(index)));
}

bool WorldObjectRuntime::call(std::string_view name, long long a, long long b, long long c) {
    if (script_ == nullptr || !on_loaded_map() || !script_->has_global(name)) {
        return false;
    }
    if (!script_->call_global_ints(name, {a, b, c})) {
        SH_ERR("[WORLDOBJ] %s: world_objects.lua %.*s failed, script stopped", map_.c_str(),
               static_cast<int>(name.size()), name.data());
        script_.reset();
        script_has_update_ = false;
        return false;
    }
    return true;
}

bool WorldObjectRuntime::call_int(std::string_view name, long long a, long long b, long long c, long long* result) {
    if (script_ == nullptr || !on_loaded_map() || !script_->has_global(name)) {
        return false;
    }
    long long v = 0;
    if (script_->call_global_int(name, {a, b, c}, &v)) {
        *result = v;
    }
    return true;
}

}  // namespace sh::world
