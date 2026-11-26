#pragma once

// World objects of a map from gamedata/maps/<map>/world_objects.json (version 2): static models placed in the map (save
// pads, items lying on the floor, doors, props), each drawn while the player is in one of its
// cells and its event-flag condition holds. Nothing here knows the game: flags are names read
// through an injected lookup, models are names, the caller draws what visible_objects returns.

#include "formats/common/status.h"

#include <array>
#include <cstdint>
#include <optional>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace sh::world {

// Side of a map cell in Q19.12 (40 m).
inline constexpr std::int32_t kWorldObjectCellSize = 40 * 4096;

// One alternative of where an object is drawn: the player's cell must be one of x_cells along X
// and one of z_cells along Z; an empty list puts no bound on that axis.
struct WorldObjectZone {
    std::vector<int> x_cells;
    std::vector<int> z_cells;
};

// Where a pose object's placement lives instead of the file: a named table of the map's disc
// overlay (the importer emits the disc address) and the row to take.
struct WorldObjectPoseRef {
    std::string disc_table;  // "CommonWorldObjectPoses"
    int index = 0;
    // Disc address of row 0 for a map whose table registry does not name the table; 0 = none.
    std::uint32_t disc_address = 0;
};

// A solid part of an object: an upright cylinder characters cannot walk through. `offset` is the middle of its foot
// from the object's position along the world's axes (it does not turn with the object); it stands `height` up from
// there. Q19.12.
struct WorldObjectCollider {
    std::array<std::int32_t, 3> offset{};
    std::int32_t radius = 0;
    std::int32_t height = 0;
};

struct WorldObjectDef {
    std::string name;                        // readable id within the map ("SavePad")
    std::string model;                       // model name in the map's or the item models (<= 8 chars)
    std::array<std::int32_t, 3> position{};  // Q19.12
    std::array<std::int16_t, 3> rotation{};  // Q3.12 turns
    std::vector<WorldObjectZone> zones;      // any of them; empty = drawn everywhere
    std::vector<std::string> flags_set;      // every one must be set
    std::vector<std::string> flags_clear;    // every one must be clear
    // Set: the position and rotation above are zero in the def; the game resolves them from a
    // table of the map's disc overlay when the map loads.
    std::optional<WorldObjectPoseRef> pose;
    // A script (maps/<map>/world_objects.lua, sh.world_object.*) may move, turn, re-model and
    // show/hide this object at run time.
    bool script = false;
    // Port deviation: in interiors the object is also drawn when the player is in a neighbouring
    // cell on the same floor. The engine only asks the "near" callback.
    bool pc_near_pickup = false;
    // Port deviation: the object is drawn from this item model file ("ITEM/UNQ46.TMD") at this scale instead of
    // the model `model` names in the map's libraries; `model` is then only the object's own model name.
    std::string item_model;
    double item_model_scale = 1.0;
    // Port addition: while the object is drawn, characters are stopped by these cylinders ("collision"). The game's
    // own objects have none: what blocks the way there is the map's collision.
    std::vector<WorldObjectCollider> collision;
};

struct WorldObjectTable {
    std::string map;
    std::vector<WorldObjectDef> objects;  // file order is draw order
};

// Metres and degrees as the original computed them from its float literals: the product is
// formed in single precision and truncated toward zero.
std::int32_t world_metres_to_q12(double metres);
std::int16_t world_degrees_to_angle(double degrees);

// Cell c >= 0 holds positions (40c, 40(c+1)) m, cell c < 0 holds (40c, 40(c+1)] m: the original
// divides the position by 40 m with truncation and tests positive and non-positive positions
// separately, so 0 belongs to cell -1.
bool world_position_in_cell(std::int32_t position, int cell);

bool world_zone_contains(const WorldObjectZone& zone, std::int32_t player_x, std::int32_t player_z);

// Parses a version 2 file. Version 1 is refused.
Result<WorldObjectTable> parse_world_objects(std::string_view json_text);
Result<WorldObjectTable> load_world_objects(const std::filesystem::path& file);

// Event flag by name; an unknown name reads as clear (the caller reports it).
using WorldObjectFlagGet = std::function<bool(const std::string& name)>;

// The game's "near" test of pc_near_pickup objects, given the object's live position. Empty =
// never near.
using WorldObjectNear = std::function<bool(const WorldObjectDef& def, const std::array<std::int32_t, 3>& position)>;

// The running state of one object while its map is loaded: starts from the def (or the resolved
// pose), scripts of "script" objects change it. visible: empty = by the data (cells and flags);
// true/false = forced by a script.
struct WorldObjectState {
    std::array<std::int32_t, 3> position{};
    std::array<std::int16_t, 3> rotation{};
    std::string model;
    std::optional<bool> visible;
};

// Initial states for the table (position, rotation, model from each def; visible empty).
std::vector<WorldObjectState> initial_states(const WorldObjectTable& table);

// Index of the object by name, -1 when none.
int find_world_object(const WorldObjectTable& table, std::string_view name);

// Indices of the objects to draw this frame, in file order. states may be null (then defs are
// used and nothing is forced); when given it must have one entry per object.
// Rule per object i: a forced states[i].visible wins (no place or flag lookups); otherwise the
// place test (zones, then the "near" callback of pc_near_pickup objects whose zone did not
// match) and, when it passed, the flag condition.
std::vector<std::size_t> select_world_objects(const WorldObjectTable& table, const std::vector<WorldObjectState>* states,
                                              std::int32_t player_x, std::int32_t player_z,
                                              const WorldObjectFlagGet& flag_get, const WorldObjectNear& near);

// The objects to draw this frame, in file order.
std::vector<const WorldObjectDef*> visible_objects(const WorldObjectTable& table, std::int32_t player_x,
                                                   std::int32_t player_z, const WorldObjectFlagGet& flag_get);

}  // namespace sh::world
