// SPDX-License-Identifier: GPL-3.0-or-later
// The window-glass shards of the burst panes: a burst
// fills a ring of cells on the pane, every cell becomes a shard that flies, spins and
// settles, and each live shard draws one small textured quad. The numbers live in
// gamedata/effects/glass.json; the game gives the particle table, the GTE transforms,
// the ground and the fog (GlassHost).

#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

#include "engine/effects/effect_prims.h"
#include "formats/common/status.h"

namespace sh::effects {

// One slot of the map's particle table, in the layout the table keeps (the game's other
// effects share the table). The shard reads the words like this:
//   x/z (s16) and y (Q12)    - the shard's position
//   yaw/pitch (s16, Q12)     - the shard's rotation
//   vel_x/vel_y/vel_z (s16)  - the launch velocity, Q12 units per second
//   spin (u16)               - at spawn the size scale; later the spin flags and, once the
//                              shard has come to rest, its relaunch countdown
//   variant (u8)             - texture cell (bits 0-3), size class (bits 4-6) and the
//                              resting bit (0x80)
struct GlassShard {
    std::int32_t x = 0;
    std::int32_t z = 0;
    std::int32_t y = 0;
    std::uint8_t state = 0;  // 0 = free; the burst groups are 23..26
    std::uint8_t variant = 0;
    std::int32_t vel_x = 0;
    std::int32_t vel_y = 0;
    std::int32_t vel_z = 0;
    std::int32_t yaw = 0;
    std::int32_t pitch = 0;
    std::int32_t spin = 0;
};

// One entry of the burst table each map keeps for its panes: where the pane's centre sits
// (x, z), and the word the original called vz - the pane's shard size factor.
struct GlassPane {
    std::int32_t x = 0;
    std::int32_t z = 0;
    std::int32_t size = 0;
};

struct GlassParams {
    std::int32_t max_x = 200;             // a shard outside this screen box is skipped
    std::int32_t max_y = 160;
    std::int32_t ordering_table_size = 2048;
    std::int32_t slot_depth_offset = 6144;  // Q12(1.5): the quad sits a little nearer
    std::int32_t fog_mix = 128;             // how much of the fog colour the shard takes
};

class GlassHost {
public:
    virtual ~GlassHost() = default;

    // Randomness, in the original's call order.
    virtual std::int32_t rand16() = 0;
    virtual std::int32_t rand_uint(std::int32_t low, std::int32_t high) = 0;
    virtual std::int32_t rand_add(std::int32_t base, std::int32_t low, std::int32_t high) = 0;

    virtual std::int32_t sin(std::int32_t angle) = 0;
    virtual std::int32_t cos(std::int32_t angle) = 0;
    virtual std::int32_t atan2(std::int32_t a, std::int32_t b) = 0;
    virtual std::int32_t sqrt(std::int32_t v) = 0;  // the integer square root
    virtual std::int32_t delta_time() = 0;          // Q12 seconds of the frame
    virtual std::int32_t gravity() = 0;             // Q12

    // The GTE rotation of the burst/shard plane; each call sets the matrix the following
    // rotate() calls use, exactly as the original set its matrices.
    virtual void rotate_setup(std::int16_t pitch, std::int16_t yaw) = 0;
    virtual Vec3 rotate(const Vec3s& v) = 0;
    // The per-shard draw frame: the shard's own rotation, moved to the pane's centre.
    virtual void frame_setup(std::int16_t yaw, std::int16_t pitch, std::int32_t tx, std::int32_t tz) = 0;
    // The frame's view matrix (the caller leaves it in the scratch pad).
    virtual void use_view() = 0;
    virtual Vec3s view_origin() = 0;  // Q8

    // Three corners at once, then the fourth: screen points and the depth words.
    virtual void project3(const Vec3s& a, const Vec3s& b, const Vec3s& c, ScreenPoint out[3]) = 0;
    virtual ScreenPoint project(const Vec3s& v) = 0;

    // The fog colour a shard at this depth (already times 16) takes.
    virtual Rgb fog(std::int32_t depth_times_16) = 0;

    struct Ground {
        std::int32_t height = 0;
        std::int32_t type = 0;
    };
    virtual Ground ground(std::int32_t x, std::int32_t z) = 0;

    // Takes a free slot for the group (23..26); -1 when the table is full.
    virtual int alloc(std::uint8_t group) = 0;
    virtual GlassShard load(int slot) = 0;
    virtual void store(int slot, const GlassShard& shard) = 0;

    // The pane table of the loaded map.
    virtual void set_pane(int group, const GlassPane& pane) = 0;
    virtual GlassPane pane(int group) = 0;

    // One shard quad: the projected corners, the fogged colour, the variant byte (which
    // picks the texture cell) and the ordering-table slot.
    struct Quad {
        ScreenPoint corners[4];
        Rgb color;
        std::uint8_t variant = 0;
        std::int32_t slot = 0;
    };
    virtual void emit(const Quad& q) = 0;
};

class GlassEffects {
public:
    explicit GlassEffects(GlassParams params) : params_(params) {}

    const GlassParams& params() const { return params_; }

    // A pane bursts: from and to are its corners, the rest are the original call's numbers
    // (speed, size factor, pane half sizes, cell spread, group).
    void spawn(GlassHost& host, const Vec3& from, const Vec3& to, std::int16_t speed, std::int32_t size,
               std::int16_t half_a, std::int16_t half_b, std::int32_t spread, int group) const;

    // Moves one shard and draws it; false when nothing was drawn this frame. The shard's
    // slot is freed (state 0) where it settles, as the original did.
    bool update(GlassHost& host, int slot, GlassShard& shard) const;

private:
    GlassParams params_;
};

Result<GlassParams> parse_glass_params(std::string_view json_text);
// gamedata/effects/glass.json, or the last mod's copy.
Result<GlassParams> load_glass_params(const std::filesystem::path& gamedata_dir,
                                      const std::filesystem::path& mods_dir, std::string* source);

}  // namespace sh::effects
