// SPDX-License-Identifier: GPL-3.0-or-later
// The splash in the drain of map5_s00 (the key ambush cutscene): a burst
// of spray particles thrown from one point stamps brightness into a 41 x 81 height field laid
// over the screen around the projected origin; every frame the field is smoothed, decays, and is
// drawn as 2-pixel Gouraud quads in a shade ramp (a table of colour words). The module owns the
// field and the particles; the game gives the random numbers, the trigonometry, the frame time and
// the projection, and turns the quads into primitives (DrainSplashHost). Numbers (sizes, speeds,
// delays, the shade table, the draw mode) live in gamedata/effects/drain_splash.json.

#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "engine/effects/effect_prims.h"
#include "formats/common/status.h"

namespace sh::effects {

constexpr int kSplashCols = 41;
constexpr int kSplashRows = 81;
constexpr int kSplashParticles = 200;

struct DrainSplashParams {
    std::int32_t start_delay = 9830;          // Q12 time before the first frame (2.4 s)
    std::int32_t spawn_delay_max = 4914;      // Q12, each particle waits 0..this before it flies
    std::int32_t start_brightness = 255;
    std::int32_t brightness_decay = 20;       // lost by a particle per frame
    std::int32_t gravity = 4915;              // added to a particle's vertical speed per frame (Q12)
    std::int32_t speed_side = 7372;           // Q12 scales of the throw: sideways
    std::int32_t speed_up = 18022;            //   vertical
    std::int32_t smooth_loss = 3;             // taken from the neighbour average every frame
    std::int32_t clear_range = 4095;          // a cell is cleared when a roll of 0..this gives 0
    std::uint32_t quiet_color = 0x3A000000u;  // the colour word of a flat, unlit cell run
    std::int32_t prim_code = 0x3A;            // semi-transparent Gouraud quad
    std::int32_t mode_tpage = 0x2A;           // draw-mode word closing the frame's quads
    std::int32_t mode_dfe = 0;
    std::int32_t mode_dtd = 1;
    std::array<std::uint32_t, 256> shade{};   // colour word by brightness
};

// One quad of the field, corners in the order the primitive stores them.
struct SplashQuad {
    std::int32_t x[4] = {};
    std::int32_t y[4] = {};
    std::uint32_t color[4] = {};  // raw colour words
};

class DrainSplashHost {
public:
    virtual ~DrainSplashHost() = default;

    virtual std::int32_t rand16() = 0;  // 0..32767
    virtual std::int32_t sin(std::int32_t angle) = 0;
    virtual std::int32_t cos(std::int32_t angle) = 0;
    virtual std::int32_t delta_time() = 0;  // Q12 seconds of the frame

    // Sets the view for the frame: origin of the splash in Q8 world units, yaw of the splash.
    virtual void begin_view(const Vec3& origin_q8, std::int32_t yaw) = 0;
    // Projects the origin itself.
    virtual ScreenPoint project_origin() = 0;
    // Projects a point relative to the origin (Q8).
    virtual ScreenPoint project(const Vec3s& v) = 0;

    virtual void emit_quad(const SplashQuad& q, std::int32_t slot) = 0;
    // Closes the frame's quads with the draw-mode primitive.
    virtual void emit_mode(std::int32_t slot, std::int32_t tpage, std::int32_t dfe, std::int32_t dtd) = 0;
};

struct SplashParticle {
    Vec3 pos;                       // Q12 relative to the origin
    std::int16_t vel[3] = {};       // Q12 per frame
    std::int32_t brightness = 0;
    std::int32_t delay = 0;         // Q12 wait; the particle flies once it is negative
};

struct DrainSplashState {
    Vec3 origin_q8;
    std::int32_t yaw = 0;
    std::int32_t delay = 0;
    std::int32_t centre_x = 0;  // the projected origin; x as the unsigned word the original kept
    std::int32_t centre_y = 0;
    std::int32_t slot = 0;      // depth word of the origin
    // The field with one all-zero row on each side: the smoothing reads the rows beside the
    // field and the original found zeros there.
    std::array<std::uint8_t, (kSplashRows + 2) * kSplashCols> field{};
    std::array<SplashParticle, kSplashParticles> particles{};

    std::uint8_t* row(int i) { return field.data() + (i + 1) * kSplashCols; }
    const std::uint8_t* row(int i) const { return field.data() + (i + 1) * kSplashCols; }
};

class DrainSplash {
public:
    explicit DrainSplash(DrainSplashParams params) : params_(params) {}
    const DrainSplashParams& params() const { return params_; }
    const DrainSplashState& state() const { return state_; }

    // Starts the splash at a world position (Q12): clears the field and throws the particles.
    void start(DrainSplashHost& host, const Vec3& position);
    // One frame: waits out the start delay, then flies the particles, smooths the field and draws it.
    void update(DrainSplashHost& host);

private:
    void stamp(int x, int y, std::int32_t brightness);
    void fly(DrainSplashHost& host);
    void smooth(DrainSplashHost& host);
    void draw(DrainSplashHost& host);
    std::uint32_t shade_at(int col, int row) const;

    DrainSplashParams params_;
    DrainSplashState state_;
};

Result<DrainSplashParams> parse_drain_splash_params(std::string_view json_text);
// gamedata/effects/drain_splash.json, or the last mod's copy.
Result<DrainSplashParams> load_drain_splash_params(const std::filesystem::path& gamedata_dir,
                                                   const std::filesystem::path& mods_dir, std::string* source);

}  // namespace sh::effects
