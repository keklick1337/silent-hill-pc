// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The narrow link of the effect hosts in src/engine/effects/game (weather, insects, beams, acid, glass, water,
// drain splash, effect textures) to the rest of the game: the packet cursor the drawers after them advance, the map
// effect flags, the weather light mode and the image places of the file queue. The records behind the accessors are
// in effect_link.cpp.

#include <cstddef>
#include <cstdint>

#include "engine/camera/view_system.h"
#include "engine/effects/effect_world.h"
#include "engine/maps/map_runtime.h"

namespace sh::effects::game_link {

// The game's packet cursor: the drawers that run after an engine drawer write where the packets it stood for
// ended. Engine drawers place their polygons by order key and only step the cursor.
unsigned char*& packet_cursor();

// The packets the original's drawers reserved (a pointer-wide link word, a
// 16-bit length and a 16-bit precise-vertex index, then the primitive's own words, byte-packed); only their sizes and the colour words are used.
namespace packet {
#pragma pack(push, 1)
struct QuadTextured {
    std::uintptr_t link;
    std::uint16_t length;
    std::uint16_t precise_index;  // the renderer's precise-vertex record of the packet
    std::uint8_t r0, g0, b0, code;
    std::int16_t x0, y0;
    std::uint8_t u0, v0;
    std::uint16_t palette;
    std::int16_t x1, y1;
    std::uint8_t u1, v1;
    std::uint16_t page;
    std::int16_t x2, y2;
    std::uint8_t u2, v2;
    std::uint16_t pad2;
    std::int16_t x3, y3;
    std::uint8_t u3, v3;
    std::uint16_t pad3;
};
struct QuadGouraudTextured {
    std::uintptr_t link;
    std::uint16_t length;
    std::uint16_t precise_index;  // the renderer's precise-vertex record of the packet
    std::uint8_t r0, g0, b0, code;
    std::int16_t x0, y0;
    std::uint8_t u0, v0;
    std::uint16_t palette;
    std::uint8_t r1, g1, b1, pad1;
    std::int16_t x1, y1;
    std::uint8_t u1, v1;
    std::uint16_t page;
    std::uint8_t r2, g2, b2, pad2;
    std::int16_t x2, y2;
    std::uint8_t u2, v2;
    std::uint16_t pad3;
    std::uint8_t r3, g3, b3, pad4;
    std::int16_t x3, y3;
    std::uint8_t u3, v3;
    std::uint16_t pad5;
};
struct QuadGouraud {
    std::uintptr_t link;
    std::uint16_t length;
    std::uint16_t precise_index;  // the renderer's precise-vertex record of the packet
    std::uint8_t r0, g0, b0, code;
    std::int16_t x0, y0;
    std::uint8_t r1, g1, b1, pad1;
    std::int16_t x1, y1;
    std::uint8_t r2, g2, b2, pad2;
    std::int16_t x2, y2;
    std::uint8_t r3, g3, b3, pad3;
    std::int16_t x3, y3;
};
struct TriangleGouraud {
    std::uintptr_t link;
    std::uint16_t length;
    std::uint16_t precise_index;  // the renderer's precise-vertex record of the packet
    std::uint8_t r0, g0, b0, code;
    std::int16_t x0, y0;
    std::uint8_t r1, g1, b1, pad1;
    std::int16_t x1, y1;
    std::uint8_t r2, g2, b2, pad2;
    std::int16_t x2, y2;
};
struct DrawMode {
    std::uintptr_t link;
    std::uint16_t length;
    std::uint16_t precise_index;  // the renderer's precise-vertex record of the packet
    std::uint32_t code[2];
};
struct TexturePage {
    std::uintptr_t link;
    std::uint16_t length;
    std::uint16_t precise_index;  // the renderer's precise-vertex record of the packet
    std::uint32_t code[1];
};
#pragma pack(pop)
// The packet code of a flat textured quad (what setting its length and code wrote).
constexpr std::uint8_t kQuadTexturedCode = 0x2C;
constexpr std::uint16_t kQuadTexturedLength = 9;
}  // namespace packet

// The map effect flags (bit 3: the drain's water runs; bit 5: the lake surface).
std::uint32_t map_effect_flags();
void set_map_effect_flags(std::uint32_t flags);

// The weather's light mode (the word the weather setters write, sh::maps::WeatherControlHost).
int weather_light_mode();
void set_weather_light_mode(int mode);

// The three image places the map effect textures load into (the file queue's image records: fire, blood and the
// shared effect sheet).
enum class EffectImagePlace : std::uint8_t { Fire, Blood, Sheet };
effect_world::ImagePlace& effect_image_place(EffectImagePlace place);

// One element of the map's glass pane table (the overlay keeps it; MapRuntime points at it).
struct GlassPaneRecord {
    std::int32_t x;
    std::int32_t z;
    std::int16_t size;
};
static_assert(sizeof(GlassPaneRecord) == 12);

// The drain's water stream block of the map (four segments; the overlay keeps it, MapRuntime points at it).
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
static_assert(offsetof(WaterStreamBlock, phase) == 0x54);
static_assert(offsetof(WaterStreamBlock, count) == 0x78);

// One water light zone of the world drawing (the map's table).
struct WaterZoneRecord {
    std::uint8_t enabled;
    std::int16_t illumination;
    std::int16_t min_x;
    std::int16_t max_x;
    std::int16_t min_z;
    std::int16_t max_z;
};
static_assert(sizeof(WaterZoneRecord) == 12);

// The halves of the 32-bit words of an effect slot (sh::maps::EffectSlotRecord) the drawers pack two values in.
inline std::int16_t low_half(std::uint32_t w) { return static_cast<std::int16_t>(w & 0xFFFF); }
inline std::int16_t high_half(std::uint32_t w) { return static_cast<std::int16_t>(w >> 16); }
inline std::uint32_t with_low_half(std::uint32_t w, std::int32_t v) {
    return (w & 0xFFFF0000u) | static_cast<std::uint16_t>(v);
}
inline std::uint32_t with_high_half(std::uint32_t w, std::int32_t v) {
    return (w & 0x0000FFFFu) | (static_cast<std::uint32_t>(static_cast<std::uint16_t>(v)) << 16);
}
inline std::uint8_t byte_of(std::uint32_t w, int n) { return static_cast<std::uint8_t>(w >> (8 * n)); }
inline std::uint32_t with_byte(std::uint32_t w, int n, std::uint8_t v) {
    return (w & ~(0xFFu << (8 * n))) | (static_cast<std::uint32_t>(v) << (8 * n));
}
inline std::int32_t low_word(std::int32_t w) { return static_cast<std::int16_t>(w & 0xFFFF); }
inline std::int32_t high_word(std::int32_t w) { return static_cast<std::int16_t>(static_cast<std::uint32_t>(w) >> 16); }
inline std::int32_t with_low_word(std::int32_t w, std::int32_t v) {
    return static_cast<std::int32_t>(with_low_half(static_cast<std::uint32_t>(w), v));
}
inline std::int32_t with_high_word(std::int32_t w, std::int32_t v) {
    return static_cast<std::int32_t>(with_high_half(static_cast<std::uint32_t>(w), v));
}

// The slot table's rotating allocator (the slot is marked with `kind`); -1 when the table is full.
int slot_alloc(std::uint8_t kind);

// The frame of the session's particle anchor `index` (0: the weather's, 1: the beams' and insects'): reset to the
// identity at `translation` (Q8) and its view matrix (the world-screen view times its world frame).
sh::camera::Matrix particle_anchor_view(int index, const std::int32_t translation[3]);
// Resets the anchor frame to the identity (as a root, its world frame up to date).
void reset_particle_anchor(int index);

// The world-screen matrix the renderer and the view code share.
sh::camera::Matrix& world_screen_matrix();

}  // namespace sh::effects::game_link
