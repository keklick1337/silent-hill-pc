// SPDX-License-Identifier: GPL-3.0-or-later
//
// The world queries of the running game's characters: the ground under a
// point, which characters a move or a ray meets, a character's move against the placed cells, rays and the
// line-of-sight checks. The rules are the world module's (ground_query, move_query, ray_query, mover_setup,
// wall_detect, chara_volume, line_of_sight, chara_collision); this layer feeds them the session's character records
// (sh::entities::CharacterRecord) and the cells World placed by window slot, and keeps the per-pass side effects
// (visit ledgers, pass stamps, the last ray length, rng draws) in the game's order.
//
// Compiled into the executable (it asks the streamed cell window which slot answers a point). The C entry-point
// names are wrappers in collision_entry_game.cpp.

#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include "engine/entities/character_record.h"
#include "engine/world/move_query.h"
#include "engine/world/ray_query.h"

namespace sh::world::game {

using entities::CharacterRecord;

// An extra cylinder a character adds to its body against other movers (a Twinfeeler's segments).
// CollisionRecord::records points at an array of these; the count is the high nibble of state_and_count.
// Q12; `bottom` and `top` are relative to position.y (Y grows down, `top` is the smaller value).
struct ColliderPart {
    entities::Vec3 position;
    std::int16_t bottom;
    std::int16_t top;
    std::int16_t radius;
    std::int8_t collision_state;
    std::uint8_t pass_stamp;  // the movement pass that last met it
};
static_assert(sizeof(ColliderPart) == 20);

// The parts attached to a character.
std::span<ColliderPart> collider_parts(const CharacterRecord& chara);

// The ground under a point, in the record the callers pass (12 bytes).
struct SurfaceRecord {
    std::int32_t height;  // Q12; 8.0 when there is no ground
    std::int16_t slope_x;
    std::int16_t slope_z;
    std::int8_t ground_type;
};
static_assert(sizeof(SurfaceRecord) == 12);

// A character's move: the movement allowed, the ground where it ends and the ceiling over it.
struct MoveRecord {
    Vec3 offset;
    SurfaceRecord surface;
    std::int32_t ceiling_height;
};
static_assert(sizeof(MoveRecord) == 28);

// What a ray met.
struct RayTraceRecord {
    std::int8_t hit;
    std::uint8_t ground_type;
    Vec3 point;
    CharacterRecord* character;  // null for geometry
    std::int32_t distance;
    std::int32_t ground_height;
    std::int16_t angle;
};

// The answer of a pass with nothing under the mover (MoveResult::code of a wedged mover).
constexpr int kMoveWedged = -1;

// Starts the world module once (switches from the environment or the config, gamedata files through the VFS).
void boot_world();
// The streamed window loaded cell file `file` (disc file number, -1: none) into `slot` at grid cell (cell_x, cell_z).
void place_cell_file(int slot, std::int32_t cell_x, std::int32_t cell_z, int file);

SurfaceRecord surface_at(std::int32_t x, std::int32_t z);
// The ground under a point is known: a loaded cell, or the map's flat default where its grid has no cell.
bool ground_known(std::int32_t x, std::int32_t z);
// A loaded cell (not the map's flat default) answers collision at the point.
bool cell_has_collision(std::int32_t x, std::int32_t z);

// The characters `excluded` collides with (the player among them when `include_player`); each one's pass stamp is
// cleared. The span stays valid until the next call.
std::span<CharacterRecord* const> collidable_characters(const CharacterRecord* excluded, bool include_player);
// How loud the player is to `listener` at `distance` (Q12; 0: measured from their positions), scaled by `scale`.
std::int32_t player_loudness(const CharacterRecord& listener, std::int32_t distance, std::int32_t scale);

// Moves `chara` by `offset` against the world, then (characters that walk on the ground) classifies the ground as
// wall, ledge or hole. Returns the movement pass code (0 free, 1 stopped).
int move_character(MoveRecord& out, const Vec3& offset, CharacterRecord& chara);
// The push away from the highest ground of 16 points around `position` (only out.offset x and z are written).
void ground_push(MoveRecord& out, const Vec3& position, std::int32_t start_height, std::int32_t heading);

enum class TraceKind : std::uint8_t {
    Solid,  // geometry only
    Chara,  // geometry and the characters (not the player)
    Sight,  // the sight ray: one-sided walls, every character with the player
    Probe,  // the sight ray against geometry, ignoring pillars thinner than 0.3
};
// Traces from `from` by `offset`, leaving out `excluded`. Returns whether something was hit.
bool trace(RayTraceRecord& out, const Vec3& from, const Vec3& offset, TraceKind kind,
           const CharacterRecord* excluded);

// Line of sight (line_of_sight.h) of a character, its rays leaving it out.
std::int32_t clear_heading(CharacterRecord& chara, std::int32_t dist, std::int32_t heading);
std::int32_t heading_toward(CharacterRecord& chara, std::int32_t dist, std::int32_t to_x, std::int32_t to_z,
                            std::int32_t span, bool clockwise);
bool sees_player(CharacterRecord& chara, std::int32_t count, std::int32_t base_dist_max, std::int32_t dist_step);
bool wall_toward_point(CharacterRecord& from, const Vec3& to);
bool wall_toward_character(CharacterRecord& from, const CharacterRecord& to);
bool wall_ahead(CharacterRecord& from, std::int32_t dist, std::int32_t heading);
bool blocked_ahead(CharacterRecord& from, std::int32_t dist, std::int32_t heading);

// --- The collision view's read-only tap ---

// What one movement pass of the player is given. The spans are the pass's own inputs and die with it.
struct MovePassView {
    MoveCylinder cylinder;
    Vec3 asked;  // the offset the character asked for, before crowds and ceilings change it
    std::span<const ActiveCell> cells;
    std::span<const MoveBody> bodies;
    std::span<const CeilingBox> ceilings;
    std::uint16_t groups = 0;
    bool walls_to_ceiling = false;
};

struct MoveTap {
    MoveObserver observer;  // what the pass meets; observer.user is the `user` of the calls below too
    void (*begin)(void* user, const MovePassView& pass) = nullptr;
    void (*end)(void* user, const MoveResult& result) = nullptr;
    // The ground classification after the pass: a wall (`ledge`: held by too few probes, the height is kept) or no
    // ground under the mover at all.
    void (*classified)(void* user, bool wall, bool ledge, bool no_ground) = nullptr;
};

// Watches every movement pass of the player; null stops watching. The tap must outlive its registration.
void set_player_move_tap(const MoveTap* tap);
// The cells the next movement pass or ray would walk, in their order; returns the collision group mask.
std::uint16_t collision_scene(std::vector<ActiveCell>& cells);
// The ground a query at (x, z) would answer right now, with the visit ledgers left as they are.
GroundSample peek_ground(std::int32_t x, std::int32_t z);
// Writes the query counters to the journal.
void log_world_stats();

}  // namespace sh::world::game
