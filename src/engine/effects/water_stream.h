// SPDX-License-Identifier: GPL-3.0-or-later
// The water of the school drain (map1_s02, map1_s03): small drops
// that leave the emitter segments and fall, and the ribbon waterfall drawn along every
// segment. The map keeps a table of up to four segments (StreamTable) and the shared
// particle table; the numbers live in gamedata/effects/water_stream.json; the game gives
// the particle table, the projection, the fog darkness and the scratch pad's per-segment
// direction tables (WaterStreamHost).
//
// Each array of the table has one word more than the segments: the original read one
// element past the last segment (the next array's first word), and the bridge fills the
// extra word from the same memory.

#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

#include "engine/effects/effect_prims.h"
#include "formats/common/status.h"

namespace sh::effects {

constexpr int kWaterSegments = 4;
constexpr int kWaterRibbonRows = 16;

struct StreamTable {
    std::array<std::int32_t, kWaterSegments + 1> x{};        // emitter position, Q12
    std::array<std::int32_t, kWaterSegments + 1> y{};        // Q12
    std::array<std::int32_t, kWaterSegments + 1> z{};        // Q12
    std::array<std::int32_t, kWaterSegments + 1> length{};   // segment length (the extra word: the closing length)
    std::array<std::int32_t, kWaterSegments + 1> mode{};     // 0 = slanted run, else a straight fall
    std::array<std::int32_t, kWaterSegments + 1> angle{};    // yaw of the run
    std::array<std::int32_t, kWaterSegments + 1> width{};    // ribbon width
    std::array<std::int32_t, kWaterSegments + 1> step{};     // ribbon row length
    std::array<std::int32_t, kWaterSegments + 1> flow{};     // texture scroll speed
    std::array<std::int32_t, kWaterSegments + 1> phase{};    // texture scroll position (u16)
    std::array<std::int32_t, kWaterSegments + 1> spread{};   // drop spread across the run
    std::array<std::int32_t, kWaterSegments + 1> ramp{};     // length of the fade in/out
    std::array<std::int32_t, kWaterSegments + 1> jitter{};   // spawn speed scale
    std::int32_t drops = 0;                                  // drops started with the stream
    std::int32_t count = 0;                                  // segments in use
};

// One slot of the map's particle table (shared with the other effects), the words the
// drops use. The raw halves keep the original's partial writes.
struct WaterDrop {
    std::int32_t x = 0;
    std::int32_t z = 0;
    std::int32_t y = 0;       // s16 in the table
    std::uint8_t state = 0;   // 0 = free
    std::uint8_t flags = 0;
    std::uint8_t cell = 0;    // texture cell (low byte of the word at 0xC)
    std::uint8_t cell_hi = 0; // its second byte, untouched by the drops
    std::uint16_t fall = 0;   // fall speed (word 0xC high half)
    std::uint16_t elapsed = 0;  // time along the run (word 0x10 low half)
    std::uint16_t scale = 0;    // speed scale (word 0x10 high half)
};

// The per-segment direction tables the frame setup leaves in the scratch pad, and the view.
struct WaterSegmentTables {
    std::array<std::int16_t, kWaterSegments> sin{};  // x of the run's direction (Q12)
    std::array<std::int16_t, kWaterSegments> cos{};  // z of the run's direction (Q12)
    // Two words per segment holding a pair of s16 each (low half first): the run's
    // half-width offsets in x (ec) and z (fc); the straight-fall mode reads the halves as
    // the left and right corner offsets.
    std::array<std::int32_t, kWaterSegments> ec{};
    std::array<std::int32_t, kWaterSegments> fc{};
    std::array<std::int32_t, kWaterSegments> far_x{};  // far edge offsets of the run
    std::array<std::int32_t, kWaterSegments> far_z{};
};

struct WaterStreamParams {
    std::int32_t group = 27;
    std::int32_t gravity_cap = 24576;
    std::int32_t spawn_speed_low = 96;
    std::int32_t spawn_speed_high = 159;
    std::int32_t spawn_speed_shift = 7;
    std::int32_t fall_speed_low = 112;
    std::int32_t fall_speed_high = 143;
    std::int32_t max_x = 200;
    std::int32_t max_y = 160;
    std::int32_t ordering_table_size = 2048;
    std::int32_t slot_depth_offset = 256;
    std::int32_t drop_clut = 14;
    std::int32_t drop_page = 45;
    std::int32_t ribbon_clut = 78;
    std::int32_t ribbon_page = 45;
    std::int32_t ribbon_max_rows = 16;
};

class WaterStreamHost {
public:
    virtual ~WaterStreamHost() = default;

    virtual std::int32_t rand16() = 0;
    virtual std::int32_t rand_uint(std::int32_t low, std::int32_t high) = 0;
    virtual std::int32_t sin(std::int32_t angle) = 0;
    virtual std::int32_t cos(std::int32_t angle) = 0;
    virtual std::int32_t delta_time() = 0;  // Q12 seconds of the frame
    virtual std::int32_t gravity() = 0;     // Q12

    virtual Vec3 view_origin() = 0;          // Q8
    virtual std::int32_t view_yaw() = 0;     // Q12 angle
    virtual const WaterSegmentTables& segments() = 0;

    // Fog darkness at a world point, 0..255.
    virtual std::int32_t darkness(std::int32_t x, std::int32_t y, std::int32_t z) = 0;

    // Corners relative to the view origin (Q8). The depth is the raw projection depth word.
    virtual void project3(const Vec3s& a, const Vec3s& b, const Vec3s& c, ScreenPoint out[3]) = 0;
    virtual ScreenPoint project(const Vec3s& v) = 0;

    // Takes a free slot of the group; -1 when the table is full.
    virtual int alloc(std::uint8_t group) = 0;
    virtual WaterDrop load(int slot) = 0;
    virtual void store(int slot, const WaterDrop& drop) = 0;

    // The stream is running (the map's state word).
    virtual void set_running(bool on) = 0;
    // Clears every slot of the two stream groups.
    virtual void clear_groups(std::uint8_t first, std::uint8_t second) = 0;

    struct DropQuad {
        ScreenPoint corners[4];
        std::int32_t intensity = 0;  // grey level, before the packing the primitive does
        std::int32_t cell = 0;
        std::int32_t v_top = 0;      // texel rows of the quad's top and bottom
        std::int32_t v_bottom = 0;
        std::int32_t slot = 0;       // ordering-table entry
    };
    virtual void emit_drop(const DropQuad& q) = 0;

    struct RibbonQuad {
        ScreenPoint corners[4];  // 0 1 on this row, 2 3 on the next
        std::int32_t grey[4] = {};
        std::int32_t v_top = 0;
        std::int32_t v_span = 0;  // added to v_top for the lower corners
        std::int32_t slot = 0;
    };
    virtual void emit_ribbon(const RibbonQuad& q) = 0;
};

class WaterStream {
public:
    explicit WaterStream(WaterStreamParams params) : params_(params) {}
    const WaterStreamParams& params() const { return params_; }

    // Starts a drop of the group in `slot` (mode 2: first spawn, 3: respawn after a run).
    // With no segments the slot is freed.
    void spawn(WaterStreamHost& host, StreamTable& table, int slot, int mode, int segment) const;
    // The stream turns on: its drops are started, the scroll positions reset.
    void start(WaterStreamHost& host, StreamTable& table) const;
    void stop(WaterStreamHost& host) const;
    // Moves one drop and draws it; false when nothing was drawn this frame.
    bool update_drop(WaterStreamHost& host, StreamTable& table, int slot) const;
    // Draws the ribbon of every segment and advances their scroll.
    void draw_ribbon(WaterStreamHost& host, StreamTable& table) const;

private:
    WaterStreamParams params_;
};

Result<WaterStreamParams> parse_water_stream_params(std::string_view json_text);
// gamedata/effects/water_stream.json, or the last mod's copy.
Result<WaterStreamParams> load_water_stream_params(const std::filesystem::path& gamedata_dir,
                                                   const std::filesystem::path& mods_dir, std::string* source);

}  // namespace sh::effects
