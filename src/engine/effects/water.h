// SPDX-License-Identifier: GPL-3.0-or-later
// Water of the flashlight scene: the chest lens flare, the
// reflecting sewer-water surface (the wave clock, the glint quad and the
// octagonal sheen). The module decides what is drawn; the game's bridge (water_bridge.cpp)
// owns the projection, the packets and the texture memory. Numbers: gamedata/effects/water.json.

#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <utility>
#include <string>
#include <string_view>
#include <vector>

#include "engine/effects/effect_prims.h"
#include "formats/common/status.h"

namespace sh::effects {

// Piecewise-linear table spread evenly over [min_x, max_x].
struct WaterCurve {
    std::vector<std::int32_t> values;
    std::int32_t min_x = 0;
    std::int32_t max_x = 0;
};

// How one quality level draws the water's surface (water.json "surface" / "quality").
struct WaterQuality {
    std::int32_t wave_layers = 3;        // 1..3 wave sets summed
    double wave_grid = 0.0;              // squares per metre the waves are evaluated on; 0: every pixel
    std::int32_t color_steps = 0;        // levels the wave's height and the glow are stepped to; 0: smooth
    std::int32_t phase_steps = 0;        // steps of one wave cycle; 0: every frame
    double distortion = 1.0;             // 0..1 of wave_distortion
    std::int32_t reflection_lines = 240; // height of the mirrored picture; 0: the characters are not mirrored
    bool reflection_smooth = true;       // the mirrored picture is filtered and blurred
    std::int32_t blur_taps = 5;          // 1, 3 or 5 samples of it
};

struct WaterParams {
    WaterCurve flare_intensity;  // the flashlight's light strength
    WaterCurve flare_distance;   // the lamp's distance from the camera
    WaterCurve flare_angle;      // the angle to the camera axis
    std::int32_t flare_max = 6144;
    std::array<std::int32_t, 16> ray_angles{};
    std::array<std::array<std::int32_t, 2>, 4> ray_shape{};  // one star ray, (x, y) pixels
    std::int32_t hidden_depth = 128;  // the glint is skipped nearer than this (depth words * 4)
    std::int32_t glint_slot = 641;    // ordering-table slot of the glint and the sheen

    // The octagon sheen: eight segments around a point in front of the lamp, Q8 metres.
    std::int32_t octagon_centre_z = 179;  // the projected hub, in front of the plate
    std::int32_t inner_radius = 51;
    std::int32_t inner_z = 307;
    std::int32_t outer_top = 51;  // the outer corners' height
    std::int32_t outer_radius = 563;
    std::int32_t outer_z = 384;
    std::int32_t ring_slot = 641;  // the bright ring and its fan
    std::int32_t band_slot = 639;  // the dark band and its fan
    std::array<std::array<Rgb, 4>, 4> octagon_colors{};  // ring, ring fan, band, band fan

    // The surface's waves move while the lamp is over a water volume: a clock of 30 Hz ticks that wraps.
    std::int32_t scroll_tick = 136;  // Q12 length of a 30 Hz tick
    std::int32_t scroll_period = 128;  // ticks after which the waves repeat

    // The water's surface: the map material the scene draws with its water shader (the area files give it no
    // picture). Colours 0..255; lengths in metres.
    std::string surface_material = "DWAVE";
    std::array<std::uint8_t, 3> surface_base{};        // what unlit water adds
    std::array<std::uint8_t, 3> surface_lit{};         // what the light shows of it
    double wave_length = 1.0;                          // of the widest wave
    double wave_contrast = 0.5;                        // 0..1: how much of the lit colour the troughs lose
    double wave_distortion = 0.01;                     // how far a wave's slope shifts the reflection (picture heights)
    double reflection_strength = 0.5;
    std::array<std::uint8_t, 3> reflection_tint{};
    double reflection_blur = 1.0;                      // in its texels
    double reflection_plane_offset = 0.0;              // added to the surface's own height
    double glow_strength = 0.3;                        // the lamp's mirrored glow
    std::array<std::uint8_t, 3> glow_color{};
    double glow_size = 1.0;                            // 1: 48 x 96 picture units, the glint's size
    std::array<WaterQuality, 3> quality{};             // low, medium, high (config key water_quality)
};

// One water volume of a map: the illumination height and the box, Q4 metres.
struct WaterZone {
    bool enabled = false;
    std::int16_t illumination = 0;
    std::int16_t min_x = 0, max_x = 0, min_z = 0, max_z = 0;
};

// The flare's point in the picture, and the two angles that decide its look.
struct FlareView {
    Vec3 screen;  // x, y pixels (Q0), z the depth
    std::int32_t azimuth = 0;
    std::int32_t altitude = 0;
};

// How far the lamp's beam points at the camera: the probe point's depth change and the
// probe length, both Q8.
struct FacingProbe {
    std::int32_t delta_z = 0;
    std::int32_t step_len = 0;
};

// The octagon sheen of one frame: the hub's view frame (yaw and place) and the corners
// of its eight segments (four per segment, Q8 relative to the plate's centre).
struct SheenDraw {
    std::int32_t cos_yaw = 0;
    std::int32_t sin_yaw = 0;
    Vec3 place;  // Q8
    Vec3s hub;
    std::array<std::array<Vec3s, 4>, 8> corners{};
    int ring_slot = 0;
    int band_slot = 0;
    const std::array<std::array<Rgb, 4>, 4>* colors = nullptr;
};

class WaterHost {
public:
    virtual ~WaterHost() = default;

    virtual std::int32_t sin(std::int32_t angle) = 0;
    virtual std::int32_t cos(std::int32_t angle) = 0;
    virtual std::int32_t atan2(std::int32_t a, std::int32_t b) = 0;
    virtual std::int32_t delta_time() = 0;  // Q12 seconds of the frame
    virtual void screen_size(std::int32_t& width, std::int32_t& height) = 0;

    // The lamp's picture point and angles for a light at pos looking along rot.
    virtual FlareView flare_view(const Vec3& pos, const Vec3s& rot) = 0;
    virtual std::int32_t projection_distance() = 0;  // the geometry screen plane distance
    virtual FacingProbe facing_probe() = 0;
    virtual bool camera_chasing() = 0;

    // Projects a place given as the mirrored lamp (Q8 world offset); the depth word is
    // screen z / 4.
    virtual ScreenPoint project_mirror(const Vec3& place_q8) = 0;

    virtual void emit(const EffectPrim& prim) = 0;
    // The wave clock: Q12 seconds into its period.
    virtual void surface_clock(std::int32_t time, std::int32_t period) = 0;
    // The scene draws the surface under the lamp with its water shader, which shows the lamp's mirrored glow and
    // the light on the water itself: the glint and the octagon are not drawn over it.
    virtual bool surface_shaded() { return false; }
    // For a shaded surface: the lamp (Q12 world) and how bright its mirrored glow is (Q12, the glint's strength).
    virtual void surface_glow(const Vec3& lamp, std::int32_t strength) {
        (void)lamp;
        (void)strength;
    }
    virtual void sheen(const SheenDraw& sheen) = 0;
};

class WaterEffects {
public:
    explicit WaterEffects(WaterParams params) : params_(std::move(params)) {}

    const WaterParams& params() const { return params_; }

    // A new map: flare tracking and its level restart.
    void reset();
    // The flare and the surface reflection can each be switched off (a map without water
    // switches the flare off for its dark scenes).
    void set_flare_suppressed(bool suppressed) { flare_suppressed_ = suppressed; }
    void set_reflection_suppressed(bool suppressed) { reflection_suppressed_ = suppressed; }

    // Per frame, from the world environment: the flare strength the light state asks for,
    // the light's direction and place, and the map's water volumes (null-terminated by
    // `enabled == false`).
    void frame(WaterHost& host, std::int32_t light_strength, const Vec3s& rot, const Vec3& pos,
               const WaterZone* zones, std::size_t zone_count);

    // After the scene: draws the flare when the frame saw one.
    void end_frame(WaterHost& host);

    // The flare strength the curves give for a lamp at depth x1 and angle x2.
    std::int32_t flare_target(std::int32_t light_strength, std::int32_t x1, std::int32_t x2) const;
    // 0..4096 from the probe; 0 when facing away.
    static std::int32_t facing_factor(const FacingProbe& probe);
    // The volume holding (x, z) in Q4 metres, or null.
    static const WaterZone* find_zone(const WaterZone* zones, std::size_t count, std::int32_t x, std::int32_t z);

    // State, for the tests.
    struct State {
        bool flare_suppressed = false;
        bool reflection_suppressed = false;
        bool flare_seen = false;
        std::int16_t target = 0;
        std::int16_t level = 0;
        FlareView view;
        std::int32_t scroll_time = 0;
        std::int32_t scroll_count = 0;
    };
    State state() const {
        return State{flare_suppressed_, reflection_suppressed_, flare_seen_, target_, level_, view_,
                     scroll_time_, scroll_count_};
    }

private:
    void draw_flare(WaterHost& host, bool front, std::int32_t level) const;
    void scroll(WaterHost& host);
    static std::int32_t glint_strength(WaterHost& host, std::int32_t angle);
    void glint(WaterHost& host, const Vec3& pos, std::int32_t angle, std::int32_t surface_y);
    void sheen(WaterHost& host, const Vec3s& rot, const Vec3& pos, std::int32_t surface_y);

    WaterParams params_;
    bool flare_suppressed_ = false;
    bool reflection_suppressed_ = false;
    bool flare_seen_ = false;
    std::int16_t target_ = 0;
    std::int16_t level_ = 0;
    FlareView view_;
    std::int32_t scroll_time_ = 0;
    std::int32_t scroll_count_ = 0;
};

Result<WaterParams> parse_water_params(std::string_view json_text);
// gamedata/effects/water.json, or the last mod's copy.
Result<WaterParams> load_water_params(const std::filesystem::path& gamedata_dir,
                                      const std::filesystem::path& mods_dir, std::string* source);

}  // namespace sh::effects
