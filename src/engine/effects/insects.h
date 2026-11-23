// SPDX-License-Identifier: GPL-3.0-or-later
// The insects of the first map: a cloud of flies over the
// carcass in the alley and three groups of crawlers on the ground that scatter when Harry comes
// near. The numbers come from gamedata/effects/map0_s00_effect.json; the game gives the clock, the
// player, the lights and the projection (InsectHost).

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

// One fly. Moves in the air around anchor 0; now and then darts at Harry's hand (or back to
// the anchor). `mode` 1..3 loiter, 5 darts; `timer` counts the frames in the mode.
struct Fly {
    Vec3 pos;
    std::int16_t vx = 0;
    std::int16_t vy = 0;
    std::int16_t vz = 0;
    std::uint8_t mode = 0;
    std::uint8_t timer = 0;
};

// One crawler. Walks on the ground around its group's anchor; flees from Harry (mode 5).
// Modes: 1 and 2 idle, 3 rest, 4 walk back to the anchor, 5 flee, 6 hesitate.
struct Crawler {
    Vec3 pos;
    std::int16_t vx = 0;
    std::int16_t vz = 0;
    std::uint8_t mode = 0;
    std::uint8_t timer = 0;
};

// The four pictures a pool cycles through: `cells` squares of `cell_size` texels side by side
// from u_base, rows v_top..v_bottom.
struct InsectSheet {
    TextureRef texture;
    std::int32_t u_base = 0;
    std::int32_t cell_size = 16;
    std::int32_t cells = 4;
    std::int32_t v_top = 240;
    std::int32_t v_bottom = 255;
    // Frames a cell is shown before the next one (every insect starts on its own cell).
    std::int32_t cell_frames = 2;
    // Sprites in the ordering table slots above this are drawn (the slot is the depth / 2).
    std::int32_t min_slot = 0;
};

struct InsectLook {
    InsectSheet flies;
    InsectSheet crawlers;
    std::uint8_t crawler_brightness = 0x80;  // the crawlers' grey, semi-transparent
    // A sprite's side in pixels: `max` close by, one less every `step` units of depth, `min`
    // from `far_steps` steps on.
    std::int32_t size_min = 3;
    std::int32_t size_max = 16;
    std::int32_t size_far_steps = 13;
    std::int32_t ordering_table_size = 2048;
    // A fly is lit by the darkness at its place, never above this.
    std::int32_t darkness_cap = 176;
    // Light modes (the map's `light_mode`): the flashlight adds the darkness at full strength,
    // lights it by an eighth, or leaves the sprite in the plain light colour.
    std::vector<std::int32_t> full_darkness_modes;
    std::vector<std::int32_t> plain_modes;
};

struct InsectConfig {
    InsectLook look;
    // [0]: the carcass the flies hover over; [1..]: the centre of each crawler group.
    std::array<Vec3, 4> anchors;
    std::int32_t fly_count = 20;
    std::int32_t crawler_count = 60;
    std::int32_t crawlers_per_group = 20;
    std::int32_t fly_spread = 10240;        // Q12 metres, a fly starts within +-spread of the anchor
    std::int32_t fly_height = 16384;        // Q12 metres, and up to this high above it
    std::int32_t crawler_spread = 2048;     // Q12 metres, the same for a crawler
    std::int32_t active_distance = 163840;  // Q12 metres (manhattan) from anchors 0 and 1 the insects run
    std::int32_t startle_radius_q8 = 256;   // Q8 metres, a crawler flees from Harry closer than this
    std::int32_t dart_chance = 65;          // a fly darts when a random number below this is 64
};

// A convex outline's corner (the light cone's footprint); public for the tests.
struct ConeCorner {
    std::int16_t x = 0;
    std::int16_t y = 0;
};

class InsectHost {
public:
    virtual ~InsectHost() = default;
    virtual std::int32_t rand16() = 0;
    virtual std::int32_t sin(std::int32_t angle) = 0;
    virtual std::int32_t cos(std::int32_t angle) = 0;
    virtual std::int32_t atan2(std::int32_t a, std::int32_t b) = 0;
    virtual std::int32_t sqrt(std::int32_t v) = 0;  // the game's integer square root
    virtual std::int32_t delta_time() = 0;          // Q12 seconds of the frame
    virtual bool paused() = 0;                      // the map holds its particles still
    virtual std::int32_t frame_counter() = 0;
    virtual Vec3 player() = 0;
    virtual bool hand_follows() = 0;  // Harry lights the match: flies may go for his hand
    virtual Vec3 hand_q8() = 0;       // the hand, Q8 metres
    virtual bool flashlight_on() = 0;
    virtual std::int32_t light_mode() = 0;
    virtual Rgb light_color() = 0;
    virtual std::int32_t darkness(const Vec3& at) = 0;  // 0..255
    // The game's rain and snow for the map's light mode (once, when the insects start).
    virtual void start_weather() = 0;
    // Puts the world at the origin of the view for the projections below.
    virtual void begin_view() = 0;
    virtual ScreenPoint project(const Vec3s& relative) = 0;
    // The light cone of the flashlight: its size (Q12), its place, and the rotation of a corner.
    virtual std::int32_t cone_size() = 0;
    virtual Vec3 cone_origin() = 0;
    virtual void cone_begin() = 0;
    virtual Vec3s cone_rotate(const Vec3s& corner) = 0;
    virtual void emit(const EffectPrim& prim) = 0;
};

class InsectSwarms {
public:
    explicit InsectSwarms(InsectConfig config);

    const InsectConfig& config() const { return config_; }
    const std::vector<Fly>& flies() const { return flies_; }
    const std::vector<Crawler>& crawlers() const { return crawlers_; }
    std::vector<Fly>& flies() { return flies_; }
    std::vector<Crawler>& crawlers() { return crawlers_; }

    // Starts the insects on a map: scatters them around the anchors. Returns the run mode:
    // 2, they run only while Harry is near the first two anchors (1 would run always, 0 never).
    int start(InsectHost& host);

    // One frame: moves and draws every insect. `mode` is the run mode `start` returned.
    void update(InsectHost& host, int mode);

    // Corners of the flashlight's footprint on the ground at the last frame (0 for none).
    std::int32_t footprint_corners() const { return footprint_corners_; }

    // Whether Harry is close enough to anchor 0 or 1 for the insects to run.
    bool player_near(InsectHost& host) const;

private:
    bool startled(InsectHost& host, int mode, const Vec3& at) const;
    void step_fly(InsectHost& host, int index, bool dart, Fly& f);
    void draw_fly(InsectHost& host, int index, const Fly& f) const;
    void step_crawler(InsectHost& host, int index, bool scared, Crawler& c);
    void draw_crawler(InsectHost& host, int index, const Crawler& c) const;
    void measure_footprint(InsectHost& host);
    EffectPrim sprite(InsectHost& host, const InsectSheet& sheet, int index) const;

    InsectConfig config_;
    std::vector<Fly> flies_;
    std::vector<Crawler> crawlers_;
    std::int32_t footprint_corners_ = 0;
};

// The corners of a convex outline of `n` points sorted by x, one point added at a time the
// way the footprint is built; returns how many corners are left. Public for the tests.
std::int32_t convex_outline(const ConeCorner* points, int n);

Result<InsectConfig> parse_insect_config(std::string_view json_text);
// gamedata/effects/map0_s00_effect.json, or the last mod's copy.
Result<InsectConfig> load_insect_config(const std::filesystem::path& gamedata_dir,
                                        const std::filesystem::path& mods_dir, std::string* source);

}  // namespace sh::effects
