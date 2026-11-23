#pragma once

// How a character's movement query is set up: the cylinder it sweeps, what it
// gets when no cell is under it, which other characters slow it down, the low ceilings of the collision triggers and
// the collision group mask. The per-character rules come from the character's data (gamedata/chara/<Name>/
// character.json, params "collision_*"); the game only hands its records over.

#include "engine/world/chara_collision.h"
#include "engine/world/collision_triggers.h"
#include "engine/world/move_query.h"

#include <cstdint>

namespace sh::world {

// The per-character movement rules. The defaults are those of a character whose data names none.
struct MoverRules {
    bool ground_walker = false;    // walks on the ground: the wall / ledge / void classification applies to it
    bool holes_as_walls = false;   // MoveOptions::walls_to_ceiling
    // With no cell under it the mover drops to `off_map_ground`; otherwise it keeps its height (a streamed cell that
    // is not loaded yet must not pull a character through the floor).
    bool off_map_drop = false;
    std::int32_t off_map_ground = 8 * 4096;  // Q.12
    // How much of a mover's speed this character takes away when the mover walks into it (Q.12).
    std::int32_t crowd_slowdown = 1638;
};

// The mover as the game keeps it: world position (Q.12), the cylinder's horizontal offset from it and the shape
// relative to it (Y grows downwards, `top` is the smaller value).
struct MoverShape {
    Vec3 position;
    std::int32_t offset_x = 0;
    std::int32_t offset_z = 0;
    std::int16_t top = 0;
    std::int16_t bottom = 0;
    std::int16_t radius = 0;
    std::uint8_t collision_state = 0;
};

// The cylinder the mover sweeps: its shape, lifted 0.02 above its feet.
MoveCylinder mover_cylinder(const MoverShape& shape);

// The answer for a mover with no cell under it: no movement, flat ground of type 0, no ceiling.
struct OffMapAnswer {
    std::int32_t ground_height = 0;
    std::int32_t ceiling_height = -16 * 4096;
};
OffMapAnswer off_map_answer(const MoverRules& rules, std::int32_t mover_height);

// Whether walking into a character of `other_state` slows a mover of `mover_state` down: only a lower state gives
// way, and never an ignored character or one in the player's state.
bool slows_mover(int other_state, int mover_state, const CollisionStates& states);

// The low ceiling a collision trigger puts over its box: half its height steps (half metres) above, less 1.5 m.
CeilingBox trigger_ceiling(const TriggerBox& box);

// The collision group mask. Bit 0 (map geometry) stays on whatever is cleared.
constexpr std::uint16_t kMapGeometryGroup = 1;
std::uint16_t clear_groups(std::uint16_t mask, std::int32_t bits);

}  // namespace sh::world
