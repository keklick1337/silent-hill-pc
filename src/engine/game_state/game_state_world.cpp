// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// The world objects', runtime pickups' and mod hooks' view of the game (game_state_world.h) over the engine: the
// draw records the world drawer keeps (world_object_record.h), the id tables, the synthetic event a runtime pickup
// runs under (the map event state), the item prompt and the collision queries.

#include "engine/game_state/game_state_world.h"

#include <cstdio>
#include <cstring>
#include <memory>
#include <vector>

#include "engine/chara/chara_library.h"
#include "engine/chara/runtime/chara_data.h"
#include "engine/chara/runtime/chara_spawn.h"
#include "engine/gameflow/map_event_state.h"
#include "engine/ids/game_numbers.h"
#include "engine/inventory/item_catalog.h"
#include "engine/maps/map_runtime.h"
#include "engine/player/player_records.h"
#include "engine/runtime/world_object_record.h"
#include "render/world/world_objects_draw.h"

extern "C" {
// The world drawer's entry points (render/world/world_objects_draw.cpp).
void WorldGfx_ObjectAdd(void* model, const void* position, const void* rotation);
void WorldObject_ModelNameSet(void* model, char* name);
// The item prompt (engine/gameflow/game/event_helpers_host.cpp).
void Event_ItemTake(std::int32_t item, std::int32_t count, std::int32_t flag, std::int32_t message);
void Event_CommonItemTake(std::uint32_t pickup, std::int32_t flag);
std::int32_t Event_CommonPickupMessage(std::uint32_t pickup);
// Collision queries (engine/world/game/collision_entry_game.cpp, the ray host).
std::uint32_t WorldBridge_CellHasCollision(std::int32_t x, std::int32_t z);
bool Ray_TraceQuery(sh::game::RayHit* trace, const std::int32_t* from, const std::int32_t* to);
}

namespace sh::game_state {

namespace {

using PoseRecord = sh::world::WorldObjectPoseRecord;

/* Reserved up front so the records of a map keep their addresses while the draw list holds them. */
std::vector<PoseRecord>& object_records() {
    static std::vector<PoseRecord> records = [] {
        std::vector<PoseRecord> v;
        v.reserve(128);
        return v;
    }();
    return records;
}

std::vector<std::unique_ptr<PoseRecord>>& pickup_records() {
    static std::vector<std::unique_ptr<PoseRecord>> records;
    return records;
}

void model_set(PoseRecord& record, const char* name) {
    char want[9];
    std::snprintf(want, sizeof(want), "%.8s", name);
    WorldObject_ModelNameSet(&record.object, want);
}

void pose_set(PoseRecord& o, const std::array<std::int32_t, 3>& p, const std::array<std::int16_t, 3>& r) {
    o.position = {p[0], p[1], p[2]};
    o.rotation = {r[0], r[1], r[2]};
}

void draw(PoseRecord& o) { WorldGfx_ObjectAdd(&o.object, &o.position, &o.rotation); }

// The event a runtime pickup runs under: a button event without flags that asks for the event callback state with
// no parameter, as a pickup of the map data has.
constexpr std::int32_t kEventCallbackState = 10;
constexpr std::uint8_t kActivationButton = 2;
constexpr std::uint32_t kNoParam = 0xFF;
constexpr std::int32_t kReadMessageState = 7;
sh::maps::MapEventRecord g_pickup_event;
sh::maps::MapEventRecord g_message_event;
sh::maps::MapEventRecord g_script_event;

}  // namespace

void world_object_records_reset(std::size_t count) {
    std::vector<PoseRecord>& v = object_records();
    if (v.size() < count) {
        v.resize(count);
    }
    for (std::size_t i = 0; i < count; ++i) {
        v[i] = PoseRecord{};
    }
}

void world_object_record_model(std::size_t index, const char* name) { model_set(object_records()[index], name); }

void world_object_record_set_pose(std::size_t index, const std::array<std::int32_t, 3>& position,
                                  const std::array<std::int16_t, 3>& rotation) {
    pose_set(object_records()[index], position, rotation);
}

ObjectPose world_object_record_pose(std::size_t index) {
    const PoseRecord& o = object_records()[index];
    ObjectPose out;
    out.position = {o.position.x, o.position.y, o.position.z};
    out.rotation = {o.rotation.x, o.rotation.y, o.rotation.z};
    std::memcpy(out.model, o.object.model.name, 8);
    out.model[8] = '\0';
    return out;
}

void world_object_record_draw(std::size_t index) { draw(object_records()[index]); }

std::int32_t world_object_record_library(std::size_t index) { return object_records()[index].object.model.library; }

void* world_object_record_address(std::size_t index) { return &object_records()[index]; }

PickupRecord pickup_record_create(const char* model) {
    auto pose = std::make_unique<PoseRecord>();
    model_set(*pose, model);
    pickup_records().push_back(std::move(pose));
    return pickup_records().back().get();
}

void pickup_record_set_pose(PickupRecord record, const std::array<std::int32_t, 3>& position,
                            const std::array<std::int16_t, 3>& rotation) {
    pose_set(*static_cast<PoseRecord*>(record), position, rotation);
}

void pickup_record_draw(PickupRecord record) { draw(*static_cast<PoseRecord*>(record)); }

void pickup_records_clear() { pickup_records().clear(); }

bool common_pickup_in_range(const std::array<std::int32_t, 3>& position) {
    return sh::world_scene::common_pickup_in_range(position[0], position[1], position[2]);
}

int item_number(const char* name) { return sh::ids::item_number(name); }
int flag_number(const char* name) { return sh::ids::flag_number(name); }
int common_pickup_number(const char* name) { return sh::ids::common_pickup_number(name); }
const char* item_name(int item) { return sh::ids::item_name(item); }
const char* flag_name(int flag) { return flag >= 0 ? sh::ids::flag_name(flag) : nullptr; }

const char* chara_name(int chara) {
    const sh::chara::CharaLibrary* lib = Pc_CharaData_Library();
    const sh::chara::CharacterDef* def =
        lib != nullptr && chara > 0 ? lib->find(chara) : nullptr;
    return def != nullptr ? def->id.c_str() : nullptr;
}

const char* item_world_model(const char* name) {
    const sh::items::ItemLibrary* items = sh::inventory::item_catalog().library();
    const sh::items::ItemDef* def = items != nullptr ? items->find_item(name) : nullptr;
    return def != nullptr ? def->world_model.c_str() : nullptr;
}

void start_pickup_event() {
    g_pickup_event = sh::maps::MapEventRecord{};
    g_pickup_event.trigger = static_cast<std::uint8_t>(kActivationButton << 4);
    g_pickup_event.action = static_cast<std::uint32_t>(kEventCallbackState & 0x1F) | kNoParam << 5;
    sh::gameflow::MapEventState& e = sh::gameflow::map_event_state();
    e.event = &g_pickup_event;
    e.sys_state = kEventCallbackState;
    e.param = kNoParam;
}

void start_script_event() {
    g_script_event = sh::maps::MapEventRecord{};
    g_script_event.trigger = static_cast<std::uint8_t>(kActivationButton << 4);
    g_script_event.action = static_cast<std::uint32_t>(kEventCallbackState & 0x1F) | kNoParam << 5;
    sh::gameflow::MapEventState& e = sh::gameflow::map_event_state();
    e.event = &g_script_event;
    e.sys_state = kEventCallbackState;
    e.param = kNoParam;
}

bool script_event_current() { return sh::gameflow::map_event_state().event == &g_script_event; }

void start_message_event(int message) {
    g_message_event = sh::maps::MapEventRecord{};
    g_message_event.trigger = static_cast<std::uint8_t>(kActivationButton << 4);
    g_message_event.action = static_cast<std::uint32_t>(kReadMessageState & 0x1F) |
                             (static_cast<std::uint32_t>(message) & 0xFF) << 5;
    sh::gameflow::MapEventState& e = sh::gameflow::map_event_state();
    e.event = &g_message_event;
    e.sys_state = kReadMessageState;
    e.param = static_cast<std::uint32_t>(message);
}

bool pickup_event_current() { return sh::gameflow::map_event_state().event == &g_pickup_event; }

void item_take(int item, int count, int flag, int message) { Event_ItemTake(item, count, flag, message); }

void common_item_take(int kind, int flag) { Event_CommonItemTake(static_cast<std::uint32_t>(kind), flag); }

int common_pickup_message(int kind) { return Event_CommonPickupMessage(static_cast<std::uint32_t>(kind)); }

bool current_map_event_point(std::int32_t* x, std::int32_t* z) {
    const auto* event = static_cast<const sh::maps::MapEventRecord*>(sh::gameflow::map_event_state().event);
    if (event == nullptr) {
        return false;
    }
    const sh::maps::MapPointRecord& point = sh::maps::map_runtime().points()[event->point];
    *x = point.x;
    *z = point.z;
    return true;
}

bool cell_has_collision(std::int32_t x, std::int32_t z) { return WorldBridge_CellHasCollision(x, z) != 0; }

bool ray_blocked(const std::array<std::int32_t, 3>& from, const std::array<std::int32_t, 3>& to) {
    sh::game::RayHit trace{};
    Ray_TraceQuery(&trace, from.data(), to.data());
    return trace.has_hit != 0;
}

int chara_find(const char* name) { return Pc_CharaIdFind(name); }
int spawn_ai_find(const char* name) { return Pc_Spawn_AiFind(name); }
int spawn_character_ai(int chara, int state, int x, int z, int yaw, int ai) {
    return Pc_Spawn_CharacterAi(chara, state, x, z, yaw, ai);
}
const char* chara_spawn_note(int chara) { return Pc_Chara_SpawnNote(chara); }

}  // namespace sh::game_state
