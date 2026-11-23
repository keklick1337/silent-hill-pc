#pragma once

// The world objects and the held item as the frame draws them (docs/RENDER.md section 6): the object queue
// (engine/world/object_queue.h) is filled by the map's records and drained with the world; an object's model is
// looked up by name in the scene's libraries of the area file; the held item (engine/world/held_item.h) reads its
// files through the file queue and is drawn at the player's hand bone.

#include <cstdint>

#include "engine/world/object_queue.h"

namespace sh::world_scene {

// Queues a world object for this frame's world draw.
void object_add(world::ObjectModel& model, const world::ObjectPosition& pos, const world::ObjectRotation& rot);
// Draws the queued objects and empties the queue.
void draw_objects();

// Takes the item (run-time number, -1 none) in the player's hand and queues the reads its table entry names; the
// ticket of the last read, 0 when the hand already holds it or nothing is read.
std::int32_t held_item_set(std::int32_t item);
// The previous weapon of the player back in hand (`weapon_attack` -1: none).
std::int32_t held_item_weapon_set(std::int32_t weapon_attack);
// Draws the held item at its hand bone once its model has loaded, unless hidden.
void draw_held_item_in_hand();

// A pickup at (x, y, z) (Q12) is drawn although its cell is not the player's: indoors, on the player's storey, in
// the 3x3 cells around the player.
bool common_pickup_in_range(std::int32_t x, std::int32_t y, std::int32_t z);

}  // namespace sh::world_scene
