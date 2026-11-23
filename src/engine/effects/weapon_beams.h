// SPDX-License-Identifier: GPL-3.0-or-later
// Weapon beams: the Hyper Blaster's beam, a chain of fading
// one-pixel lines from the muzzle to what it hits with a dot at the end, and the spark
// line the powered weapons draw between Harry's hand and body while they cut. The
// looks come from gamedata/effects/weapon_beams.json; the game gives the projection,
// the ray trace, its sine table and the lights (BeamHost, BeamLights).

#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "engine/effects/effect_prims.h"
#include "formats/common/status.h"

namespace sh::effects {

// A one-pixel textured line: the texture row is the same for both ends; the palette row
// is palette_y + (depth slot >> palette_slot_shift), so far lines take darker rows.
struct BeamLineStyle {
    TextureRef texture;
    std::uint8_t u_near = 0;  // corners 0 and 1
    std::uint8_t u_far = 0;   // corners 2 and 3
    std::uint8_t v = 0;
    std::int32_t palette_slot_shift = 5;
    bool semi_transparent = false;
    std::int32_t slot_min = 1;  // depth slots outside [min, max] are not drawn
    std::int32_t slot_max = 254;
};

struct HyperBlasterBeamStyle {
    BeamLineStyle line;
    std::int32_t segments_without_hit = 16;
    // Segment i runs from from + range_vector * i >> segment_shift to the next one.
    std::int32_t segment_shift = 4;
    // Brightness of segment i: start - step * i at its near end, start - step * (i + 1)
    // at its far end (8 bits, wrapping like the original's bytes).
    std::int32_t intensity_start = 255;
    std::int32_t intensity_step = 17;
    // By the beam colour option: which of r, g, b carry the brightness.
    std::vector<std::array<bool, 3>> channels;
    TextureRef tip_texture;
    std::uint8_t tip_u = 0;
    std::uint8_t tip_v = 0;
    std::int16_t tip_size = 4;
    std::vector<Rgb> tip_colors;  // by the beam colour option
};

struct SparkLineStyle {
    BeamLineStyle line;
    // By the map's light mode: a colour, "by_flashlight" (lit or dim, BeamLights) or none
    // (the colour is left as it is).
    enum class ColorKind : std::uint8_t { None, Fixed, ByFlashlight };
    struct ModeColor {
        ColorKind kind = ColorKind::None;
        Rgb rgb;
    };
    std::vector<ModeColor> by_light_mode;
    Rgb lit;
    Rgb dim;
};

struct WeaponBeams {
    HyperBlasterBeamStyle hyper_blaster;
    SparkLineStyle spark_line;
    std::vector<std::string> maps_without_beams;

    bool drawn_on(std::string_view map) const;
};

struct BeamHit {
    bool hit = false;
    std::int32_t distance = 0;  // Q12, valid with hit
    Vec3 target;                // the hit point, or the end of the ray
};

class BeamHost {
public:
    virtual ~BeamHost() = default;
    virtual std::int32_t sin(std::int32_t angle) = 0;  // the game's sine table, Q12 angle -> Q12
    virtual std::int32_t cos(std::int32_t angle) = 0;
    // The ray from `from` along `offset` that skips Harry.
    virtual BeamHit trace(const Vec3& from, const Vec3& offset) = 0;
    virtual ScreenPoint project(const Vec3s& relative) = 0;
    virtual void emit(const EffectPrim& prim) = 0;
};

struct HyperBlasterShot {
    Vec3 from;    // the muzzle
    Vec3 origin;  // the view origin of the projection (Harry's position)
    std::int32_t range = 0;  // Q12, the weapon attack's range
    std::int32_t yaw = 0;    // Q12 angles
    std::int32_t pitch = 0;  // from straight down
    std::int32_t color_option = 0;
};

// The flags the "by_flashlight" spark colour reads.
struct BeamLights {
    std::int32_t map_light_mode = 0;
    bool flashlight_allowed = false;   // the current environment allows the flashlight
    bool flashlight_on = false;
    bool dark_flashlight_off = false;  // the environment without the flashlight is dark
    bool dark_flashlight_on = false;   // the environment with the flashlight is dark
};

void draw_hyper_blaster_beam(const HyperBlasterBeamStyle& style, const HyperBlasterShot& shot, BeamHost& host);
void draw_spark_line(const SparkLineStyle& style, const Vec3& from, const Vec3& to, const Vec3& origin,
                     const BeamLights& lights, BeamHost& host);

Result<WeaponBeams> parse_weapon_beams(std::string_view json_text);
// gamedata/effects/weapon_beams.json, or the last mod's copy.
Result<WeaponBeams> load_weapon_beams(const std::filesystem::path& gamedata_dir, const std::filesystem::path& mods_dir,
                                      std::string* source);

}  // namespace sh::effects
