// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// What the map's effect frame (map_effect_frame.h) leaves for the slot drawers each frame, and the layouts of the
// parameter blocks the map's disc tables give its effects (sh::maps::MapRuntime::effect_block). The frame's view
// is engine state (src/engine/effects/game/map_effect_frame_host.cpp), read by the drawers through
// map_effect_view().

#include <cstdint>

#include "engine/camera/camera_types.h"
#include "engine/effects/slot_effects.h"
#include "engine/effects/water_stream.h"

namespace sh::effects {

struct MapEffectView {
    SlotView view;
    // The world-to-screen matrix at the view origin; a record of its own, as the precise path looks the exact
    // camera of a matrix up by its record.
    sh::camera::Matrix world_screen;
    // The water stream's per-segment directions and offsets for this frame.
    WaterSegmentTables water;
};

MapEffectView& map_effect_view();

// The map's effect slot table (sh::maps::MapRuntime::effect_slots) in the slot effects' record.
int effect_slot_count();
EffectSlot load_effect_slot(int slot);
void store_effect_slot(int slot, const EffectSlot& s);

// The smoke column's block (EffectBlock::Smoke, 32 bytes).
struct SmokeColumnBlock {
    std::uint8_t shape;
    std::uint8_t size;
    std::uint8_t keep;
    std::uint8_t reserved;
    std::int16_t puffs;
    std::int16_t top;
    std::int16_t base;
    std::int16_t radius;
    std::int16_t rise;
    std::int16_t fade_rate;
    std::int16_t fade;
    std::int16_t depth_bias;
    std::int32_t offset_x;
    std::int32_t offset_z;
    std::int32_t fade_delay;
};
static_assert(sizeof(SmokeColumnBlock) == 32);

// The particle emitter's block (EffectBlock::Fire, 40 bytes).
struct EmitterBlock {
    std::uint8_t flags;
    std::uint8_t mode;
    std::uint8_t level_speed;
    std::uint8_t spark_spread;
    std::uint8_t fade_phase;
    std::uint8_t fade_amp;
    std::uint8_t spark_size;
    std::uint8_t reserved;
    std::uint16_t wobble;
    std::int16_t swirl;
    std::uint16_t count;
    std::int16_t speed;
    std::int16_t level;
    std::int16_t top;
    std::int16_t bottom;
    std::int16_t height;
    std::int16_t half_x;
    std::int16_t half_z;
    std::uint16_t radius;
    std::int16_t reserved2;
    std::int32_t ember_x;
    std::int32_t ember_z;
};
static_assert(sizeof(EmitterBlock) == 40);

// An entry of the drip table (EffectBlock::Drips, 24 bytes).
struct DripBlock {
    std::int32_t x;
    std::int32_t z;
    std::int16_t y;
    std::int16_t angle;
    std::int16_t spread;
    std::int16_t size;
    std::int16_t period;
    std::int16_t count;
    std::uint8_t pattern;
    std::uint8_t reserved[3];
};
static_assert(sizeof(DripBlock) == 24);

// The water stream's block (EffectBlock::Water, 124 bytes): per-segment columns, the drop count, the segments in
// use.
struct WaterStreamBlock {
    std::int32_t x[4];
    std::int16_t y[4];
    std::int32_t z[4];
    std::int16_t length[4];
    std::uint8_t mode[4];
    std::int16_t angle[4];
    std::int16_t width[4];
    std::int16_t step[4];
    std::int16_t flow[4];
    std::uint16_t phase[4];
    std::int16_t spread[4];
    std::int16_t ramp[4];
    std::int16_t jitter[4];
    std::int16_t drops;
    std::uint8_t reserved[2];
    std::uint8_t count;
};
static_assert(sizeof(WaterStreamBlock) == 124);

}  // namespace sh::effects

// The frame's effect flags (bit 3 the water stream, bit 5 the lake surface; the map scripts read them as
// map_vars "map_effect_flags") and the slot table's allocation cursor.
extern "C" {
extern std::int8_t g_MapEffectFlags;
extern std::int16_t g_MapEffectSlotCursor;
}
