// SPDX-License-Identifier: GPL-3.0-or-later
// Effects that live in the map's effect slot table: every
// frame the effect frame walks the table and hands each busy slot to the drawer of its kind. This module holds
// the drawers a map names in gamedata/maps/<map>/overlay.json "effect_draws":
//
//   drips            kind 28        water drops (ripple frames) at the points of the map's drip table (map1_s03)
//   light_glow       kind 35        a one-frame glow burst at a point, optionally over a copy of the screen
//   smoke_column     kinds 13, 14   a rising column of smoke puffs (map1_s01, map6_s04)
//   particle_emitter kinds 8..12    a fire: flames that swirl and rise, burn out into smoke, now and then a spark
//                                   or an ember (map1_s05, map3_s05, map4_s01)
//   split_head       kinds 5..7     the boss's drool drops and splats, and the drag mark of Harry pushing it
//
// Every number, the order of the random
// draws and the 16-bit truncations of the table words are the original's. The game gives the slot table, the
// projection, the random numbers and the parameter blocks the map scripts write (SlotEffectHost); the drawers
// record engine draw-list polygons (render/draw3d) that the host places in the frame.

#pragma once

#include <array>
#include <cstdint>

#include "engine/effects/effect_prims.h"
#include "render/draw3d/draw3d.h"

namespace sh::effects {

// One slot of the map's effect table, the record every slot effect shares (20 bytes in the table). Each effect
// reads the two per-effect words its own way; the helpers below give their halves and bytes.
struct EffectSlot {
    std::int32_t x = 0;
    std::int32_t z = 0;
    std::int16_t y = 0;
    std::uint8_t kind = 0;   // the drawer that owns the slot; 0 = free
    std::uint8_t flags = 0;  // per-effect byte
    std::uint32_t a = 0;     // per-effect word (table offset 0x0C)
    std::uint32_t b = 0;     // per-effect word (table offset 0x10)
};

inline std::int16_t lo16(std::uint32_t w) { return static_cast<std::int16_t>(w & 0xFFFFu); }
inline std::int16_t hi16(std::uint32_t w) { return static_cast<std::int16_t>(w >> 16); }
inline std::uint8_t byte_of(std::uint32_t w, int i) { return static_cast<std::uint8_t>(w >> (8 * i)); }
inline void set_lo16(std::uint32_t& w, std::int32_t v) {
    w = (w & 0xFFFF0000u) | (static_cast<std::uint32_t>(v) & 0xFFFFu);
}
inline void set_hi16(std::uint32_t& w, std::int32_t v) {
    w = (w & 0xFFFFu) | ((static_cast<std::uint32_t>(v) & 0xFFFFu) << 16);
}
inline void set_byte(std::uint32_t& w, int i, std::uint32_t v) {
    const int s = 8 * i;
    w = (w & ~(0xFFu << s)) | ((v & 0xFFu) << s);
}

// The view and the per-frame numbers the effect frame computes before the drawers run.
struct SlotView {
    Vec3 origin;                        // view origin, geometry units (the player's position in whole metres)
    std::int32_t screen_distance = 0;   // distance to the projection plane
    std::int16_t pitch = 0;             // view angles, Q12
    std::int16_t yaw = 0;
    // The particle emitter's frame numbers (valid while its flag bit is set).
    std::int16_t rise = 0;        // height added to every particle this frame
    std::int16_t swirl_rate = 0;  // ember swirl speed
    std::int16_t fade_begin = 0;  // phase below which a spark's orbit is scaled
    std::int16_t fade_amp = 0;
    std::int16_t fade_base = 0;
    std::int16_t age_step = 0;    // phase added to every particle this frame
    std::int32_t offset_x = 0;    // emitter centre, Q12
    std::int32_t offset_z = 0;
    std::int32_t spark_size = 0;  // spark size times the screen distance
};

// An entry of the map's drip table (the map scripts fill it).
struct DripSource {
    std::int32_t x = 0;         // Q12
    std::int32_t z = 0;
    std::int16_t y = 0;
    std::int16_t angle = 0;     // line direction (pattern 1) or arc centre (pattern 4)
    std::int16_t spread = 0;    // spawn spread, Q12
    std::int16_t size = 0;      // drop size
    std::int16_t period = 0;    // Q12 seconds a drop lives
    std::int16_t count = 0;     // drops started with the source
    std::uint8_t pattern = 0;   // 0 point, 1 line, 2 disc, 3 ring, 4 half ring
};

// The smoke column's parameter block (the map scripts write it; the effect frame fades it).
struct SmokeColumnParams {
    std::uint8_t shape = 0;       // 0 disc, else square spawn area
    std::uint8_t size = 0;        // puff size
    std::uint8_t keep = 0;        // 0: a puff starts drawing at once when it respawns
    std::int16_t fade_rate = 0;   // fade level gained per second once the delay is over
    std::int16_t puffs = 0;       // puffs started with the column
    std::int16_t top = 0;         // column height
    std::int16_t base = 0;        // spawn height
    std::int16_t radius = 0;      // spawn radius
    std::int16_t rise = 0;        // rise speed
    std::int16_t fade = 0;        // fade level (Q12 angle: 0 full, 1024 gone)
    std::int16_t depth_bias = 0;  // depth subtracted for the ordering slot
    std::int32_t offset_x = 0;    // column centre, Q12
    std::int32_t offset_z = 0;
    std::int32_t fade_delay = 0;  // 0: puffs above the column may go out
};

// The particle emitter's parameter block (the map scripts write it).
struct EmitterParams {
    std::uint8_t flags = 0;         // 1 disc, 2 flames turn into smoke, 4 rare sparks, 8 rare embers,
                                    // 16 sparse
    std::uint8_t mode = 0;          // 0 burning, 1 flaring up, 2 out at once, 3 dying out
    std::uint8_t level_speed = 0;   // level change per frame (modes 1 and 3)
    std::uint8_t spark_spread = 0;  // phase spread by distance from the centre
    std::uint8_t fade_phase = 0;
    std::uint8_t fade_amp = 0;
    std::uint8_t spark_size = 0;
    std::uint16_t wobble = 0;       // orbit radius scale
    std::int16_t swirl = 0;         // spark swirl speed
    std::uint16_t count = 0;        // particles started with the emitter
    std::int16_t speed = 0;
    std::int16_t level = 0;         // dying out: 4096 full, 0 gone
    std::int16_t top = 0;
    std::int16_t bottom = 0;
    std::int16_t height = 0;        // top - bottom, set by start
    std::int16_t half_x = 0;        // square area
    std::int16_t half_z = 0;
    std::uint16_t radius = 0;       // disc area
    std::int32_t ember_x = 0;       // ember centre offset, Q12
    std::int32_t ember_z = 0;
};

// The ordering table a polygon goes to.
enum class SlotLayer : std::uint8_t {
    World,    // the scene's table
    Overlay,  // the table drawn over the scene
};

class SlotEffectHost {
public:
    virtual ~SlotEffectHost() = default;
    // A flame particle of the particle emitter was drawn at this point (world units, PS1 axes) with this share of
    // light (0..1): the fire's light for the modern lighting follows it (map_lights.h FireLight). Render only.
    virtual void flame_shown(const std::array<float, 3>& position, float amount) {
        (void)position;
        (void)amount;
    }

    virtual std::int32_t rand16() = 0;  // 0..32767
    virtual std::int32_t sin(std::int32_t angle) = 0;
    virtual std::int32_t cos(std::int32_t angle) = 0;
    virtual std::int32_t atan2(std::int32_t y, std::int32_t x) = 0;
    virtual std::int32_t sqrt(std::int32_t v) = 0;
    // The game's angle remap the emitter's orbit scale runs through.
    virtual std::int32_t angle_remap(std::int32_t angle) = 0;
    // The game's overflow-safe 2D length (Q12 in, Q12 out).
    virtual std::int32_t length2(std::int32_t x, std::int32_t z) = 0;
    virtual std::int32_t delta_time() = 0;  // Q12 seconds of the frame
    virtual std::int32_t gravity_step() = 0;
    virtual Vec3 player_position() = 0;     // Q12
    // The ground query leaves the projection's matrices changed; restore_view puts the effect view back.
    virtual std::int32_t ground_height(std::int32_t x, std::int32_t z) = 0;
    virtual void restore_view() = 0;
    virtual void play_landing_sound(const Vec3& at) = 0;
    // Fog darkness at a world point (Q12), 0..255.
    virtual std::int32_t darkness(std::int32_t x, std::int32_t y, std::int32_t z) = 0;

    virtual const SlotView& view() = 0;
    // Projects a point relative to the view origin; depth is the raw screen z.
    virtual ScreenPoint project(const Vec3s& relative) = 0;
    // Projects the four corners of a quad the way the original wrote three of them straight into its packet: the
    // next emitted polygon takes the precise records of those corners.
    virtual void project_corners(const Vec3s relative[4], ScreenPoint out[4]) = 0;

    virtual int slot_count() = 0;
    virtual EffectSlot load(int slot) = 0;
    virtual void store(int slot, const EffectSlot& s) = 0;
    // Takes a free slot for `kind` (rotating start, like the original); -1 when the table is full.
    virtual int alloc(std::uint8_t kind) = 0;
    virtual void set_effect_flag(std::uint32_t bit) = 0;

    virtual DripSource drip_source(int index) = 0;
    virtual SmokeColumnParams smoke_params() = 0;
    virtual void store_smoke_params(const SmokeColumnParams& p) = 0;
    virtual EmitterParams emitter_params() = 0;
    virtual void store_emitter_params(const EmitterParams& p) = 0;

    virtual void emit(const draw3d::Polygon& polygon, SlotLayer layer, std::int32_t bucket) = 0;
    // Draws the displayed frame back over the scene and brackets the frame with the mask bit (the light glow's
    // flash; a feedback copy the renderer owns).
    virtual void screen_copy() = 0;
};

// ---- the drawers ----------------------------------------------------------------------------------------------

namespace slot_kind {
constexpr std::uint8_t kSplitHeadDragFront = 5;
constexpr std::uint8_t kSplitHeadDragBack = 6;
constexpr std::uint8_t kSplitHeadDrool = 7;
constexpr std::uint8_t kFlame = 8;
constexpr std::uint8_t kFireSmoke = 9;
constexpr std::uint8_t kSpark = 10;
constexpr std::uint8_t kEmber = 11;
constexpr std::uint8_t kEmitterIdle = 12;
constexpr std::uint8_t kSmokeWaiting = 13;
constexpr std::uint8_t kSmokePuff = 14;
constexpr std::uint8_t kDrip = 28;
constexpr std::uint8_t kLightGlow = 35;
}  // namespace slot_kind

// Effect frame flag bits the drawers raise.
constexpr std::uint32_t kEmitterRunning = 1u << 0;
constexpr std::uint32_t kSmokeRunning = 1u << 1;

class Drips {
public:
    // Starts the drops of source `source`.
    static void start(SlotEffectHost& host, int source);
    static bool draw(SlotEffectHost& host, int slot);

private:
    static void spawn(SlotEffectHost& host, int slot, EffectSlot& s, int source, bool respawn);
};

class LightGlow {
public:
    // A glow at `at` (Q12) of `size`, its three extra sparkles within `spread` (Q12); flag 1 also flashes the
    // screen.
    static void spawn(SlotEffectHost& host, const Vec3& at, std::uint16_t size, std::uint16_t spread,
                      std::uint32_t flags);
    static bool draw(SlotEffectHost& host, int slot);
};

class SmokeColumn {
public:
    static void start(SlotEffectHost& host);
    // A waiting puff (kind 13): counts down to its start; true when it started.
    static bool wait(SlotEffectHost& host, int slot);
    static bool draw(SlotEffectHost& host, int slot);

private:
    static void respawn(SlotEffectHost& host, const SmokeColumnParams& p, EffectSlot& s);
};

class ParticleEmitter {
public:
    static void start(SlotEffectHost& host);
    // Puts a fresh particle in `slot`; `again`: a particle that went out, not one of the first fill.
    static void respawn(SlotEffectHost& host, int slot, bool again);
    static bool draw_flame(SlotEffectHost& host, int slot);  // kinds 8, 10
    static bool draw_smoke(SlotEffectHost& host, int slot);  // kinds 9, 11

private:
    static void respawn(SlotEffectHost& host, const EmitterParams& p, EffectSlot& s, bool again);
};

class SplitHeadEffects {
public:
    // A drool drop at (x, y, z) (Q8 units scaled by 16 after a random jitter), thrown at `speed` along `heading`.
    static void spawn_drool(SlotEffectHost& host, std::int32_t x, std::int32_t y, std::int32_t z,
                            std::int32_t speed, std::int32_t heading);
    static bool draw_drool(SlotEffectHost& host, int slot);  // kind 7
    // The drag mark between two points (Q12) of texture row `row` and brightness `brightness`.
    static void spawn_drag(SlotEffectHost& host, std::int32_t row, std::int32_t brightness, const Vec3& front,
                           const Vec3& back);
    static bool draw_drag(SlotEffectHost& host, int slot);   // kind 5 (kind 6 is its other end)

private:
    static bool draw_falling(SlotEffectHost& host, int slot, EffectSlot& s);
    static bool draw_splat(SlotEffectHost& host, int slot, EffectSlot& s);
};

// ---- helpers shared by the drawers (exposed for the tests) ------------------------------------------------------

// The game's 32-bit fixed-point products: `mul12` wraps like the 32-bit multiply, `mul12_wide` takes the 64-bit
// product.
inline std::int32_t mul12(std::int32_t a, std::int32_t b) {
    return static_cast<std::int32_t>(static_cast<std::uint32_t>(a) * static_cast<std::uint32_t>(b)) >> 12;
}
inline std::int32_t mul12_wide(std::int32_t a, std::int32_t b) {
    return static_cast<std::int32_t>((static_cast<std::int64_t>(a) * b) >> 12);
}

// A flat textured quad from the words the original stored: the corners as x | y << 16, the texels as
// u | v << 8 | (palette or page) << 16 (the third and fourth only the low half), the colour as
// r | g << 8 | b << 16 | code << 24 (the code's low bits give the shading).
draw3d::Polygon textured_quad(std::uint32_t rgbc, const std::uint32_t xy[4], const std::uint32_t uv[4]);

// x | y << 16 of a screen point, as the 32-bit stores of the original packed it.
inline std::uint32_t xy_word(std::int32_t x, std::int32_t y) {
    return (static_cast<std::uint32_t>(x) & 0xFFFFu) + (static_cast<std::uint32_t>(y) << 16);
}

}  // namespace sh::effects
