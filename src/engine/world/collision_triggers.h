#pragma once

// Collision triggers of a map: axis-aligned boxes on a one-metre
// grid that mark uneven ground (walls, stairs, slopes). Two queries:
//   - which triggers lie near a point (the ones the movement checks this frame);
//   - how far a planar move gets before it enters one, and the height step of that trigger (the push an
//     airborne character gets from a slope).
//
// Bit-exactness contract: the results equal the original's to the last bit.

#include <cstdint>
#include <span>
#include <vector>

namespace sh::world {

// One trigger box: corner and size in whole metres, height in half-metre steps.
struct TriggerBox {
    std::int32_t x = 0;
    std::int32_t z = 0;
    std::uint32_t size_x = 0;
    std::uint32_t size_z = 0;
    std::uint32_t height_steps = 0;
};

// A trigger as the map data packs it into one 32-bit word: bit 0 marks the entry that ends a list
// (not a trigger itself), then x (10 bits, signed), z (10 bits, signed), size_x (4), size_z (4) and
// the height steps (3), from the low bit up.
struct PackedTrigger {
    std::uint32_t bits = 0;
};
bool is_list_end(PackedTrigger t);
TriggerBox unpack_trigger(PackedTrigger t);

// The collision groups in force and the triggers near the player that the movement sweeps (the
// game's active-trigger record: groups, count, then the entries of the map's list). The original's record
// had room for 20 entries and wrote on past them; the engine keeps room for every count.
struct ActiveTriggers {
    static constexpr int kCapacity = 255;
    std::uint16_t groups = 0;
    std::uint8_t count = 0;
    const PackedTrigger* near[kCapacity] = {};
};
ActiveTriggers& active_triggers();

// Fills `active` with the entries of the list (up to its end marker) near the point (Q.12).
void find_nearby_triggers(ActiveTriggers& active, std::int32_t x, std::int32_t z, const PackedTrigger* list);

// Indices of the boxes whose area, grown by 16 m on every side, holds the point (Q.12).
std::vector<std::size_t> nearby_triggers(std::int32_t x, std::int32_t z, std::span<const TriggerBox> boxes);

// The sweep of a planar move (Q.12) through trigger boxes, in the layout the game keeps it.
struct TriggerSweep {
    std::int32_t from_x = 0;
    std::int32_t from_z = 0;
    std::int32_t to_x = 0;
    std::int32_t to_z = 0;
    std::int32_t move_x = 0;
    std::int32_t move_z = 0;
    std::int32_t min_x = 0;
    std::int32_t max_x = 0;
    std::int32_t min_z = 0;
    std::int32_t max_z = 0;
    std::int32_t fraction = 4096;     // part of the move before the first trigger (Q.12, 1.0 = none)
    std::int32_t height = 0;          // height of that trigger (Q.12)
};

struct SlopeForce {
    std::int32_t distance = 0;  // planar distance covered before the trigger (Q.12)
    std::int32_t height = 0;
};

// Sweeps the move through the boxes in order, stopping at the first box the move starts inside.
TriggerSweep sweep_triggers(std::int32_t from_x, std::int32_t from_z, std::int32_t move_x, std::int32_t move_z,
                            std::span<const TriggerBox> boxes);

// The force of a finished sweep: no trigger met gives 32 m and a 16 m drop.
SlopeForce slope_force(const TriggerSweep& sweep);

// The sweep of a move through the active triggers.
TriggerSweep sweep_active(const ActiveTriggers& active, std::int32_t from_x, std::int32_t from_z,
                          std::int32_t move_x, std::int32_t move_z);

// Where a move along one axis crosses a slab, as a fraction of the move (Q.12); 1.0 when it does not
// cross it inside [low, high] on the other axis. Lengths are Q.4 (16-bit), the bounds Q.12.
std::int32_t slab_crossing(std::int16_t other_move, std::int16_t move, std::int16_t to_slab, std::int32_t low,
                           std::int32_t high);

}  // namespace sh::world
