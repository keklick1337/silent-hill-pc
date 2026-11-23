// SPDX-License-Identifier: GPL-3.0-or-later
// The acid spit of the Floatstinger and the Twinfeeler:
// a glob that flies along a line, leaves a fading trail of glow sprites behind it and, where it
// lands, bursts into short-lived droplets. The numbers come from gamedata/effects/acid.json;
// the game gives the particle table, the projection, the ray trace and the lights (AcidHost).

#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "engine/effects/effect_prims.h"
#include "formats/common/status.h"

namespace sh::effects {

// One slot of the map's particle table, in the layout the table keeps (the game's other
// effects share the table). The acid particles read the words like this:
//   spit  (state 33): c0 = yaw, c1 = pitch (Q12 angles), t0 = life left, t1 = fade (Q12)
//   drop  (state 34): c0 = age (Q12 seconds), c1 = size scale (Q12), t0 low byte = look variant
struct AcidParticle {
    std::int32_t x = 0;
    std::int32_t z = 0;
    std::int16_t y = 0;
    std::uint8_t state = 0;  // 0 = free
    std::uint8_t flag = 0;   // spit: 1 after it landed; drop: 1 = from a hit on Harry (draws nearer)
    std::int16_t c0 = 0;
    std::int16_t c1 = 0;
    std::int16_t t0 = 0;
    std::int16_t t1 = 0;
};

constexpr std::uint8_t kAcidSpitState = 33;
constexpr std::uint8_t kAcidDropState = 34;

struct AcidParams {
    std::int32_t speed = 0;       // Q12 metres per second
    std::int32_t size_start = 0;  // Q12 metres, glow size at the head
    std::int32_t size_end = 0;    // and at the tail
    std::int32_t pitch_scale = 0; // Q12, a drop's fall angle is this times a random 0.5..1
    std::int32_t life = 0;        // Q12 seconds a spit flies
    std::int32_t length = 0;      // Q12 metres, the trail's length
    std::int32_t segments = 0;    // glow sprites in the trail
};

struct AcidLook {
    TextureRef glow;  // the soft halo, drawn at one fourth of the brightness
    TextureRef core;  // the bright centre
    std::int32_t drop_life = 0;       // Q12 seconds
    std::int32_t drop_fade_in = 0;    // Q12 seconds
    std::int32_t drop_size_base = 0;  // Q12, a drop's size is scale * (base + (age / 2) / life)
    std::int32_t splash_count = 12;
    std::int32_t drop_spread = 0;      // Q12 metres, a drop starts within +-spread of the burst point
    std::int32_t drop_age_jitter = 1;  // Q12 seconds, the age a drop starts at is below this
    std::int32_t drop_scale_min = 0;   // Q12, random factor of a drop's size scale
    std::int32_t drop_scale_max = 0;
    std::int32_t drop_variant_min = 0;  // brightness and cell of a drop: a random byte in this range
    std::int32_t drop_variant_max = 0;
    std::int32_t slot_near_spit = 0x20;  // depth words subtracted so the glow sits over what it lights
    std::int32_t slot_near_drop = 0x10;
    std::int32_t slot_near_drop_hit = 0x80;
    std::int32_t max_x = 200;  // sprites further from the centre of the screen are skipped
    std::int32_t max_y = 160;
    std::int32_t ordering_table_size = 2048;
};

struct AcidMapParams {
    AcidParams normal;
    AcidParams hard;
};

struct AcidConfig {
    AcidLook look;
    struct Entry {
        std::string map;
        AcidMapParams params;
    };
    std::vector<Entry> maps;

    // Null for a map without acid.
    const AcidMapParams* find(std::string_view map) const;
};

struct AcidProbe {
    bool hit = false;
    bool hit_player = false;
    Vec3 target;
    std::int32_t ground_height = 0;
};

class AcidHost {
public:
    virtual ~AcidHost() = default;
    virtual std::int32_t rand16() = 0;
    virtual std::int32_t sin(std::int32_t angle) = 0;
    virtual std::int32_t cos(std::int32_t angle) = 0;
    virtual std::int32_t delta_time() = 0;  // Q12 seconds of the frame
    virtual std::int32_t atan2(std::int32_t a, std::int32_t b) = 0;
    virtual std::int32_t length2_q6(std::int32_t a, std::int32_t b) = 0;  // the game's safe 2d length
    virtual std::int32_t darkness(const Vec3& at) = 0;                    // 0..255
    // Projection of a point relative to the view origin; depth is the raw screen z.
    virtual ScreenPoint project(const Vec3s& relative) = 0;
    virtual std::int32_t geometry_screen() = 0;  // distance to the projection plane
    virtual Vec3s view_origin() = 0;             // Q8, from the matrix the frame set up
    // Takes a free slot for `state` (the slot's current words in *p); -1 when the table is full.
    virtual int alloc(std::uint8_t state, AcidParticle* p) = 0;
    virtual void store(int slot, const AcidParticle& p) = 0;
    // The ray from `from` along `offset`, ignoring the spitting creature, and the ground at its end.
    virtual AcidProbe probe(const Vec3& from, const Vec3& offset) = 0;
    virtual void strike_player() = 0;
    virtual void emit(const EffectPrim& prim) = 0;
};

class AcidEffects {
public:
    explicit AcidEffects(AcidConfig config) : config_(std::move(config)) {}

    const AcidConfig& config() const { return config_; }

    // A glob leaves `from` toward `to`.
    void spawn_spit(AcidHost& host, const AcidParams& params, const Vec3& from, const Vec3& to) const;
    // Draws one spit, moves it and bursts it where it lands; false when it is gone. `p` is the
    // slot's content; the result is written back with host.store (before the burst takes slots).
    bool update_spit(AcidHost& host, const AcidParams& params, int slot, AcidParticle& p) const;
    // The configured number of droplets around `at`; `on_player` marks them to draw nearer.
    void spawn_drops(AcidHost& host, const AcidParams& params, const Vec3& at, bool on_player) const;
    bool update_drop(AcidHost& host, int slot, AcidParticle& p) const;

private:
    AcidConfig config_;
};

Result<AcidConfig> parse_acid_config(std::string_view json_text);
// gamedata/effects/acid.json, or the last mod's copy.
Result<AcidConfig> load_acid_config(const std::filesystem::path& gamedata_dir, const std::filesystem::path& mods_dir,
                                    std::string* source);

}  // namespace sh::effects
