// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// The world objects of the loaded map in the running game: the table of
// gamedata/maps/<map>/world_objects.json (mods patch it by object name), each object's live state, the poses read
// from the disc, and the map's script maps/<map>/world_objects.lua (sh.world_object.*). Every frame the script's
// update(dt) runs, the objects to draw are selected on the live states (cells, flags, the "near" test) and handed
// to the world drawer in file order.
//
// The drawer reads the draw records (engine/game_state/game_state_world.h): a "script" object's record
// is its live pose and model, because map variables bound to objects by name ("wo:<name>") write the record
// directly; the live state follows the record before every selection.

#pragma once

#include "engine/world/world_objects.h"
#include "formats/common/status.h"

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace sh::script {
class Host;
}

namespace sh::world {

class WorldObjectRuntime {
public:
    WorldObjectRuntime();
    ~WorldObjectRuntime();

    // The map's world objects init: loads the table, resolves the disc poses, sets up the draw records and runs the
    // script's chunk. A map without a readable table draws no objects (logged).
    void init(std::string_view map);
    // The script's update(dt), the selection and the draws of this frame.
    void draw();
    // A tool's draws after the map's objects of each frame (the modding tools' placement try-out); nullptr: none.
    static void set_tool_draw(void (*fn)());

    // sh.world_object.*: getters work for every object, setters only for "script" objects.
    Result<void> set_position(std::string_view name, std::int32_t x, std::int32_t y, std::int32_t z);
    Result<void> set_rotation(std::string_view name, std::int32_t x, std::int32_t y, std::int32_t z);  // 16-bit wrap
    Result<void> set_visible(std::string_view name, std::optional<bool> visible);  // nullopt: by the data
    Result<void> set_model(std::string_view name, std::string_view model);         // 1..8 characters
    struct Pose {
        std::array<std::int32_t, 3> position{};
        std::array<std::int16_t, 3> rotation{};
    };
    Result<Pose> pose(std::string_view name);
    // The modding tools' placement editor moves any object where it stands now, a "script" one or not; the data and
    // the script are not asked. A script that moves the object itself puts it back on its next update.
    Result<void> tool_set_pose(std::string_view name, const Pose& pose);

    // Every object of the loaded map as it stands now, in file order (the state dump).
    struct ObjectInfo {
        std::string name;
        std::string model;
        std::string item_model;  // the item model file it is drawn from; empty: a model of the map's libraries
        std::array<std::int32_t, 3> position{};
        std::array<std::int16_t, 3> rotation{};
        bool pose_resolved = false;  // false: its disc pose was not found, it is never drawn
        bool queued = false;         // the last draw selected it and handed it to the object queue
        // Where the world drawer found the model (ObjectModel::library); 0: not looked up yet, or not found.
        std::int32_t library = 0;
    };
    std::vector<ObjectInfo> describe();
    const std::string& map() const { return map_; }

    // A map variable bound to `count` objects from `first` on (all "script" objects): the first draw record.
    Result<void*> binding(std::string_view first, int count);

    // A global of the loaded map's world_objects.lua with integer arguments; false when the map has none (or the
    // loaded script is another map's: hooks of a new map run during its load, before its init). A failing call
    // stops the script.
    bool call(std::string_view name, long long a, long long b, long long c);
    // The same; *result gets the function's integer result when it returns one.
    bool call_int(std::string_view name, long long a, long long b, long long c, long long* result);

    // world_objects_shadow: the counters of the drawn sets (logged at map change, every 600 frames and at exit).
    void log_summary(const char* why) const;

private:
    bool load(std::string_view map);
    int find(std::string_view name, const WorldObjectDef** def, std::string* why) const;
    int find_script(std::string_view name, std::string* why) const;
    void apply_model(std::size_t index, std::string_view model);
    void sync_from_record(std::size_t index);
    std::vector<std::size_t> update_and_select();
    bool flag(const std::string& name);
    bool on_loaded_map() const;

    std::string map_;
    std::optional<WorldObjectTable> table_;
    std::vector<WorldObjectState> states_;
    std::vector<bool> resolved_;               // false: the pose could not be resolved, never drawn
    std::vector<std::array<char, 9>> applied_;  // the model name last given to each draw record
    std::unique_ptr<sh::script::Host> script_;
    bool script_has_update_ = false;
    std::vector<std::string> unknown_flags_;
    // world_objects_shadow counters of the drawn sets
    long long frames_ = 0;
    long long frames_with_objects_ = 0;
    long long set_changes_ = 0;
    std::vector<std::size_t> previous_;
    std::vector<std::size_t> queued_;  // the objects the last draw queued
};

WorldObjectRuntime& world_object_runtime();

}  // namespace sh::world
