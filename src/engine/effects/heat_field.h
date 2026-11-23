// SPDX-License-Identifier: GPL-3.0-or-later
// Heat fields: a byte grid laid over the screen into which a set of
// particles stamps brightness; every frame the grid is smoothed towards its neighbours, loses a
// little, and is drawn as Gouraud quads in a shade ramp (a table of colour words). The bad and
// good ending fights of map7_s03 draw their lightning, the boss's shock and two ending flashes
// this way, and Dahlia's portal in map6_s04 its gathering light. The module owns the grid and the particles; the game gives the random numbers, the
// trigonometry, the frame time and the projection, and turns the quads into primitives
// (HeatFieldHost). Every field (grid size, emitter, speeds, depth rules, shade) is data:
// gamedata/effects/heat_fields.json.

#pragma once

#include <climits>
#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "engine/effects/drain_splash.h"
#include "engine/effects/effect_prims.h"
#include "formats/common/status.h"

namespace sh::effects {

// How the particles of a field are thrown.
enum class HeatEmitter : std::uint8_t {
    Burst,  // a disc of particles thrown outwards, falling, each after its own start delay
    Shell,  // a sphere of particles thrown outwards at one speed
    Cloud,  // a pool of particles spawned on demand around a point; they live a set time
    Fan,    // particles thrown in the screen plane; the field shows for a set time after arm()
    Gather, // particles spawned on demand on a sphere around a point fly into it, rest a frame, go
};

struct HeatFieldParams {
    HeatEmitter emitter = HeatEmitter::Burst;
    int cols = 41;
    int rows = 41;
    int cell = 4;                 // pixels per cell
    bool follow_origin = true;    // the grid sits on the projected origin, else at a fixed screen place
    std::int32_t stamp_x = 0;     // added to a projected point before the anchor is taken away
    std::int32_t stamp_y = 0;
    std::int32_t draw_x = 0;      // top left corner of the grid relative to the anchor
    std::int32_t draw_y = 0;
    Vec3 origin_offset_q8;        // added to the origin (Q8) when the field is placed
    std::int32_t start_delay = 0; // Q12 time before the first frame after start()
    std::int32_t lifetime = 0;    // Fan: Q12 time the field shows after arm()
    bool hold_on_still_frame = false;  // no particle or smoothing step while the frame time is 0

    // Depth slot: (projected depth + depth_add) >> 1, plus slot_add while below slot_limit,
    // else slot_far; every drawn row moves the slot by -row_slot_step.
    std::int32_t depth_add = 0;
    std::int32_t slot_add = 0;
    std::int32_t slot_limit = INT_MAX;
    std::int32_t slot_far = 0;
    std::int32_t row_slot_step = 0;

    // Particles.
    int particles = 0;
    std::int32_t start_brightness = 255;
    std::int32_t brightness_decay = 0;  // per frame; Cloud: per second (Q12), times the frame time
    int stamp_shift = 0;                // brightness >> stamp_shift is stamped, saturating at 255
    int project_shift = 8;              // particle position (Q12) >> project_shift is projected
    std::int32_t speed = 0;             // Shell, Cloud: Q12 throw speed
    // Burst: the throw speed is speed_scale * (speed_min + rand % speed_range); the third axis
    // starts at fall_start and moves by -gravity per frame down to fall_max.
    std::int32_t speed_min = 0;
    std::int32_t speed_range = 1;
    std::int32_t speed_scale = 0;
    std::int32_t fall_start = 0;
    std::int32_t gravity = 0;
    std::int32_t fall_max = 0;
    // Cloud: spawn points spread up to this (Q12) around the point; the throw adds the spawn
    // offset times spread_push; a particle lives `life` (Q12 seconds).
    std::int32_t spread = 0;
    std::int32_t spread_push = 0;
    std::int32_t life = 0;
    // Gather: the sphere's radius (Q12); `speed` is the flight speed (Q12 per second).
    std::int32_t radius = 0;
    // start_between(): the origin is 1/2^between_shift of the way from `from` to `to`, at
    // between_height (Q8).
    int between_shift = 0;
    std::int32_t between_height = 0;

    // The grid.
    std::int32_t smooth_loss = 1;
    std::int32_t clear_range = -1;  // >= 0: a cell is cleared when a roll of 0..this gives 0
    std::uint32_t quiet_color = 0x3A000000u;
    std::int32_t prim_code = 0x3A;
    std::int32_t mode_tpage = 0x2A;
    std::int32_t mode_dfe = 0;
    std::int32_t mode_dtd = 1;
    std::vector<std::uint32_t> shade = std::vector<std::uint32_t>(256, 0);  // colour word by brightness
};

class HeatFieldHost {
public:
    virtual ~HeatFieldHost() = default;

    virtual std::int32_t rand16() = 0;  // 0..32767
    virtual std::int32_t sin(std::int32_t angle) = 0;
    virtual std::int32_t cos(std::int32_t angle) = 0;
    virtual std::int32_t atan2(std::int32_t y, std::int32_t x) = 0;
    virtual std::int32_t delta_time() = 0;  // Q12 seconds of the frame
    // Gather: the game's square root (Q12 in, Q12 out) and vector normalisation (Q12 unit vector).
    virtual std::int32_t sqrt12(std::int32_t v) {
        std::int64_t r = 0;
        while ((r + 1) * (r + 1) <= static_cast<std::int64_t>(v) * 4096) {
            r++;
        }
        return static_cast<std::int32_t>(r);
    }
    virtual Vec3 normalize(const Vec3& v) { return v; }

    // Sets the view for the frame: origin in Q8 world units, yaw of the field.
    virtual void begin_view(const Vec3& origin_q8, std::int32_t yaw) = 0;
    virtual ScreenPoint project_origin() = 0;
    // Projects a point relative to the origin (Q8 when project_shift is 4).
    virtual ScreenPoint project(const Vec3s& v) = 0;

    virtual void emit_quad(const SplashQuad& q, std::int32_t slot) = 0;
    virtual void emit_mode(std::int32_t slot, std::int32_t tpage, std::int32_t dfe, std::int32_t dtd) = 0;
};

struct HeatParticle {
    Vec3 pos;                   // Q12 relative to the origin
    std::int32_t vel[3] = {};   // per frame (Cloud: per second)
    std::int32_t brightness = 0;
    std::int32_t delay = 0;     // Q12 wait; the particle flies once it is negative
    std::int32_t life = 0;      // Cloud, Gather: Q12 time left
    bool active = false;        // Cloud, Gather: a spawned particle
    bool landed = false;        // Gather: arrived, resting
};

struct HeatFieldState {
    Vec3 origin_q8;
    std::int32_t yaw = 0;
    std::int32_t delay = 0;
    std::int32_t timer = 0;     // Fan: 0 idle, 1 armed, then time since the burst (+2)
    std::int32_t centre_x = 0;  // projected origin; x as the unsigned word the original kept
    std::int32_t centre_y = 0;
    std::int32_t depth = 0;     // projected depth word of the origin plus depth_add
    int cols = 0;
    // The grid with one all-zero row on each side: the smoothing reads the rows beside it.
    std::vector<std::uint8_t> grid;
    std::vector<HeatParticle> particles;

    std::uint8_t* row(int i) { return grid.data() + static_cast<std::size_t>(i + 1) * cols; }
    const std::uint8_t* row(int i) const { return grid.data() + static_cast<std::size_t>(i + 1) * cols; }
};

class HeatField {
public:
    explicit HeatField(HeatFieldParams params);
    const HeatFieldParams& params() const { return params_; }
    const HeatFieldState& state() const { return state_; }

    // Places the field at a world point (Q12) with a yaw, clears it and throws the particles
    // (Burst, Shell; a Cloud only clears).
    void start(HeatFieldHost& host, const Vec3& position, std::int32_t yaw = 0);
    // start() at a point between two world points, turned along from -> to.
    void start_between(HeatFieldHost& host, const Vec3& from, const Vec3& to);
    // Fan: places the field; the next update() clears it, throws the particles and shows it for
    // `lifetime`.
    void arm(const Vec3& position, std::int32_t yaw);
    // Fan: hides the field.
    void stop() { state_.timer = 0; }
    // Clears the grid; a Cloud forgets its particles.
    void clear();
    // Cloud: spawns up to `count` free particles around a point relative to the origin (Q12).
    // Gather: `count` particles on the sphere around the point (each rolls its place even when
    // the pool is full).
    void spawn(HeatFieldHost& host, const Vec3& around, int count);
    // One frame. advance = false keeps the particles still (the grid still smooths and draws).
    void update(HeatFieldHost& host, bool advance = true);

private:
    void gather_one(HeatFieldHost& host, const Vec3& around);
    void throw_all(HeatFieldHost& host);
    void stamp(int x, int y, std::int32_t brightness);
    void fly(HeatFieldHost& host);
    void smooth(HeatFieldHost& host);
    void draw(HeatFieldHost& host);
    std::uint32_t shade_at(int col, int row) const;
    Vec3 place(const Vec3& position) const;

    HeatFieldParams params_;
    HeatFieldState state_;
};

// Reads every field of gamedata/effects/heat_fields.json (or the last mod's copy), by name.
Result<std::map<std::string, HeatFieldParams>> parse_heat_fields(std::string_view json_text);
Result<std::map<std::string, HeatFieldParams>> load_heat_fields(const std::filesystem::path& gamedata_dir,
                                                               const std::filesystem::path& mods_dir,
                                                               std::string* source);

}  // namespace sh::effects
