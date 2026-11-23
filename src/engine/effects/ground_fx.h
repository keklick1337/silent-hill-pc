// SPDX-License-Identifier: GPL-3.0-or-later
// Ground effects: what a creature leaves on the floor when it bursts out
// of it or dives into it. One set has four pools and a few hole timelines:
//  - dust clouds: soft sprites drawn as a grid of Gouraud boxes over a small heat buffer (a seed
//    row at the bottom diffuses upwards every frame; a cell's value times the cloud's light picks
//    a colour word from a ramp, with a little random jitter); each cloud drifts by its kind;
//  - marks: a textured square on the floor in two layers that fades after a while;
//  - craters: a textured 3x3 grid on the floor in two layers that grows, holds, fades;
//  - rings: a 3x3 grid turned to the view, two textured faces, that grows and fades;
//  - holes: timelines (steps at given times) that place the burst point and throw dust rings,
//    blood, craters and rings around the creature.
// Positions are kept relative to an effect grid origin (Q8 game units); the origin snaps to the
// grid when the set is reset. The set also builds its palettes at reset: it reads a palette row
// from video memory and writes two shades of it and a ramp. The module decides everything drawn;
// the game gives trigonometry, random numbers, the view, lighting, projection and the primitives
// (GroundFxHost). Twinfeeler (map4_s03) uses the set "TwinfeelerBurrow". Every number is data:
// gamedata/effects/ground_fx.json.

#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "engine/effects/effect_prims.h"
#include "formats/common/status.h"

namespace sh::effects {

struct GroundPalette {
    std::string texture;  // the texture whose page and palette place the set uses
    int read_row = 1;     // video memory row read back (16 entries at the texture palette's x)
    int below_row = 15;   // rows written: the shade below the reference, above it, the ramp
    int above_row = 14;
    int ramp_row = 13;
    int split = 7;            // entries before it shade below the reference, the rest above
    int reference_entry = 7;  // the entry the shades are measured from
    std::array<std::int32_t, 3> reference_offset{};  // added to its 5-bit r, g, b
    std::int32_t gain_below = 128;  // shade = (difference * gain) >> 7, clamped to 0..31
    std::int32_t gain_above = 128;
    std::int32_t ramp_red = 1638;   // ramp entry i: r = (2i * ramp_red) >> 12, g = b likewise
    std::int32_t ramp_green_blue = 1433;
    std::uint16_t page_layer_a = 0x20;  // or-ed into the texture's page for the two floor layers
    std::uint16_t page_layer_b = 0x40;
};

struct DustParams {
    int count = 16;
    int cols = 11;         // boxes across and down
    int rows = 9;
    int stride = 12;       // heat buffer row length
    int buffer_rows = 11;  // the last row is the seed row
    std::int32_t cell_w = 8;   // box size = geometry screen distance * cell / depth + 1
    std::int32_t cell_h = 12;
    std::int32_t life = 4096;
    std::int32_t hot_life = 3072;  // above it the cloud seeds its buffer and drifts fast
    int seed_tries = 3;
    std::int32_t seed_mask = 0x70;
    std::int32_t jitter_low = -2;  // light index jitter range (inclusive)
    std::int32_t jitter_high = 1;
    std::int32_t page_x = 320;     // the draw page that closes each cloud
    std::int32_t abr = 1;
    Vec3s normal{0, -4096, 0};
    // Drift of kinds 0 and 1: turned by +-turn from the heading; fast/slow by hot_life.
    std::int32_t turn = 1024;
    std::array<std::int32_t, 2> speed{128, 76};
    std::array<std::int32_t, 2> rise{0x59, 0x80};
    std::vector<std::uint32_t> ramp = std::vector<std::uint32_t>(256, 0);  // colour word by light
};

struct DustRingParams {
    int count = 5;
    std::int32_t speed = 0x99;
    std::int32_t spread = 0x80;  // each cloud turns by a random -spread..spread-1 from the last
    std::int32_t step = 819;     // and by step after it
    std::int32_t rise_low = 0x70;
    std::int32_t rise_high = 0x8F;
    std::int32_t life = 7782;
    std::int32_t delay_max = 255;
    std::int32_t alpha = 1228;
    // Fade of a ring cloud after its delay: while life > fade_after the light rises by rise_rate
    // up to peak, then falls by fall_rate down to 0.
    std::int32_t fade_after = 2457;
    std::int32_t peak = 2662;
    std::int32_t rise_rate = 1392;
    std::int32_t fall_rate = 1024;
};

struct BloodParams {
    std::int32_t skip_mask = 0x30;  // a drop is thrown when (rand & skip_mask) == 0
    std::int32_t lift = 819;
    std::int32_t spread = 0x99;     // velocity = rand % spread - centre on x and z
    std::int32_t centre = 0x4C;
    std::int32_t rise = -0x4C;
};

struct MarkParams {
    int count = 16;
    std::int32_t life = 12288;
    std::int32_t hold = 4915;       // full alpha while life > hold
    std::int32_t fade_rate = 1228;
    std::int32_t alpha_reset = 0x800;
    std::int16_t half = 0x80;       // half size (Q8)
    std::array<std::uint8_t, 4> uv{0xE0, 0xC0, 0xFF, 0xE0};  // u0 v0 u1 v1
    Vec3s normal{0, -4096, 0};
    std::int32_t slot_bias = 25;
};

struct GridParams {
    int count = 3;
    std::int32_t life = 32768;
    std::int32_t hold = 0x2B33;      // life above it: alpha rises (or is full); below: it fades
    std::int32_t alpha_start = 0;
    std::int32_t scale_start = 0;
    std::int32_t alpha_rise = -1;    // -1: full alpha while held
    std::int32_t fade_rate = 0x666;
    std::int32_t grow_rate = 0x800;
    std::int32_t size = 0x100;       // half size at scale 4096 (Q8)
    bool wrap_amount = false;        // the light amount wraps at 256 instead of clamping
    std::array<std::uint8_t, 2> uv_a{0x40, 0x80};  // u0 v0 of the first texture face
    std::array<std::uint8_t, 2> uv_b{0x40, 0x80};
    std::uint8_t uv_step = 0x20;
    Vec3s normal{0, -4096, 0};
    std::int32_t slot_bias = 25;
    // Layers: the craters draw both faces at slot + bias with the floor layer pages; the rings
    // draw face a at slot + bias and face b at slot with page `page` and the ramp palette.
    bool ramp_layers = false;
    std::uint16_t page = 0x4D;
};

struct HoleStep {
    enum class Action : std::uint8_t { BurstPoint, DustRing, Blood, Crater, Ring };
    struct Act {
        Action kind = Action::DustRing;
        std::int32_t distance = 0;  // BurstPoint / Crater / Ring: ahead of the hole along the heading
    };
    bool has_at = false;
    std::int32_t at = 0;            // the step runs once the timeline's time reaches it
    std::vector<Act> run;
    bool end = false;               // after running the hole is free again
    bool has_each = false;          // every frame of this step, after the check
    std::int32_t each_mask = 0;     // 0: always, else when rand & mask
    std::vector<Act> each;
};

struct HoleTimeline {
    std::string name;
    std::vector<HoleStep> steps;
};

struct GroundFxParams {
    std::int32_t grid_shift = 18;  // the origin snaps to multiples of 1 << shift (towards zero)
    GroundPalette palette;
    DustParams dust;
    DustRingParams dust_ring;
    BloodParams blood;
    MarkParams marks;
    GridParams craters;
    GridParams rings;
    std::int32_t rings_yaw_offset = 0x800;  // rings turn to the view's yaw plus this
    std::array<std::array<std::uint8_t, 4>, 4> grid_quads{{{3, 0, 4, 1}, {4, 1, 5, 2}, {6, 3, 7, 4}, {7, 4, 8, 5}}};
    int hole_count = 3;
    std::vector<HoleTimeline> holes;
};

struct GroundProjected {
    std::int16_t x = 0;
    std::int16_t y = 0;
    std::int32_t depth = 0;  // ordering depth (screen z / 4)
    bool too_near = false;   // a vertex was nearer than half the screen distance (the projection saturated)
};

// A textured quad of the grid: corners are indices of the last project_grid points.
struct GroundGridQuad {
    std::array<std::uint8_t, 4> corner{};
    std::array<std::uint16_t, 4> uv{};
    std::array<std::uint32_t, 4> color{};
    std::uint16_t page = 0;
    std::uint16_t palette = 0;
};

class GroundFxHost {
public:
    virtual ~GroundFxHost() = default;

    virtual std::int32_t rand16() = 0;  // 0..32767
    virtual std::int32_t sin(std::int32_t angle) = 0;
    virtual std::int32_t cos(std::int32_t angle) = 0;
    virtual std::int32_t delta_time() = 0;
    virtual std::int32_t ordering_size() = 0;

    // The texture's page word and palette place; false when it is not loaded.
    virtual bool texture(const std::string& name, std::uint16_t& page, std::uint16_t& palette_x,
                         std::uint16_t& palette_y) = 0;
    virtual void read_palette_row(int x, int y, std::uint16_t out[16]) = 0;
    virtual void write_palette_rows(int x, int y, int rows, const std::uint16_t* data) = 0;

    virtual std::int32_t ground_height(std::int32_t x, std::int32_t z) = 0;
    virtual std::int32_t owner_heading(std::uint64_t owner) = 0;
    virtual std::int32_t view_yaw() = 0;

    // The frame's view: the world-screen matrix at the grid origin, then the creature's rotation.
    virtual void begin_frame(std::int32_t origin_x, std::int32_t origin_z, const Vec3s& rotation) = 0;
    virtual void frame_view() = 0;  // the view back to the frame matrix
    // The lighting frame of a point (Q8, relative to the origin; y not used), turned by yaw.
    virtual void local_frame(const Vec3s& at, std::int32_t yaw) = 0;
    // The view moved to a point of the frame and turned by yaw.
    virtual void view_at(std::int32_t yaw, const Vec3s& at) = 0;
    virtual void push_view() = 0;
    virtual void pop_view() = 0;
    // Light falloff (0..255) of a point of the lighting frame facing `normal`.
    virtual std::uint8_t light(const Vec3s& point, const Vec3s& normal) = 0;
    // Colour word (code byte 0) of the world tint faded by amount.
    virtual std::uint32_t tint(std::uint8_t amount) = 0;

    virtual std::int32_t geom_screen() = 0;
    virtual GroundProjected project_origin() = 0;
    // Four points: their screen positions; returns the depth and whether the call saturated near.
    virtual GroundProjected project4(const std::array<Vec3s, 4>& p, std::array<GroundProjected, 4>& out) = 0;
    // Nine points as three triples; the host keeps the screen points for grid quads. Returns the
    // depth of the last triple.
    virtual std::int32_t project_grid(const std::array<Vec3s, 9>& p) = 0;

    // A Gouraud box (semi-transparent); drawn false: its packet space is taken, nothing linked.
    virtual void box(std::int16_t x, std::int16_t y, std::int16_t w, std::int16_t h,
                     const std::array<std::uint32_t, 4>& color, std::int32_t slot, bool drawn) = 0;
    virtual void draw_page(std::int32_t slot, int dfe, int dtd, std::uint16_t page) = 0;
    virtual void mark(const std::array<GroundProjected, 4>& p, std::uint32_t color, const std::array<std::uint8_t, 4>& uv,
                      std::uint16_t page, std::uint16_t palette, std::int32_t slot) = 0;
    virtual void grid_quad(const GroundGridQuad& q, std::int32_t slot) = 0;
};

struct DustCloud {
    std::int32_t index = 0;
    std::int32_t kind = 0;   // 0, 1: drift sideways; 2: by velocity; 3: still
    std::int32_t life = 0;
    Vec3s velocity;
    Vec3s pos;               // Q8, relative to the origin
    std::int32_t abr = 1;
    std::array<std::uint8_t, 133> heat{};  // buffer at offset 1 (offset 0 is read as the left of cell 0)
    bool fades = false;      // a ring cloud: delay, then fade in and out
    std::int32_t alpha = 0;
    std::int32_t phase = 0;
    std::int32_t delay = 0;
};

struct GroundMark {
    std::int32_t timer = 0;
    std::int32_t alpha = 0;
    Vec3s pos;
};

struct GroundGrid {
    std::int32_t life = 0;
    Vec3s pos;
    std::int32_t alpha = 0;
    std::int32_t scale = 0;
};

struct GroundHole {
    std::int32_t step = 0;
    std::int32_t time = 0;
    std::uint64_t owner = 0;
    Vec3 position;
    int timeline = -1;  // -1: free
};

class GroundFx {
public:
    explicit GroundFx(GroundFxParams params);
    const GroundFxParams& params() const { return params_; }

    // The origin snaps to the grid around `at` (Q12), the palettes are rebuilt and every pool
    // and hole is cleared.
    void reset(GroundFxHost& host, const Vec3& at);
    // Dust cloud number `index` at a floor point (Q12) if it is not alive (kind = index & 1).
    void dust_at(const Vec3& at, int index);
    // The first free dust cloud of a kind at a point; -1 when none is free.
    int dust_free(const Vec3& at, int kind);
    void mark_at(const Vec3& at, int index);
    void crater_at(const Vec3& at);
    void ring_at(const Vec3& at);
    // A hole of timeline `timeline` under the owner standing at (x, z); false when all are busy.
    bool hole(GroundFxHost& host, int timeline, std::uint64_t owner, const Vec3& owner_pos);
    // One frame: the holes advance (when time runs) and every pool is drawn.
    void frame(GroundFxHost& host, const Vec3s& rotation);

    const std::vector<DustCloud>& dust() const { return dust_; }
    const std::vector<GroundMark>& marks() const { return marks_; }
    const std::vector<GroundGrid>& craters() const { return craters_; }
    const std::vector<GroundGrid>& rings() const { return rings_; }
    const std::vector<GroundHole>& holes() const { return holes_; }
    const Vec3& burst_point() const { return burst_point_; }
    std::int32_t origin_x() const { return origin_x_; }
    std::int32_t origin_z() const { return origin_z_; }
    // Pages and palette words: layer a, layer b (page, palette), the ramp palette.
    std::uint16_t page_a() const { return page_a_; }
    std::uint16_t page_b() const { return page_b_; }
    std::uint16_t palette_below() const { return palette_below_; }
    std::uint16_t palette_above() const { return palette_above_; }
    std::uint16_t palette_ramp() const { return palette_ramp_; }
    // The palette rows written at the last reset: the row read (turned into the ramp), above, below.
    const std::array<std::uint16_t, 48>& palette_rows() const { return rows_; }

private:
    Vec3s relative(const Vec3& at) const;
    void textures(GroundFxHost& host);
    void build_palettes(GroundFxHost& host);
    void run_holes(GroundFxHost& host);
    void run_actions(GroundFxHost& host, GroundHole& h, const std::vector<HoleStep::Act>& acts);
    void dust_ring(GroundFxHost& host);
    void blood_drop(GroundFxHost& host);
    void draw_dust(GroundFxHost& host, std::int32_t heading);
    void draw_cloud(GroundFxHost& host, DustCloud& c, std::int32_t light);
    void draw_marks(GroundFxHost& host);
    void draw_grids(GroundFxHost& host, std::vector<GroundGrid>& pool, const GridParams& p, std::int32_t yaw);

    GroundFxParams params_;
    std::vector<DustCloud> dust_;
    std::vector<GroundMark> marks_;
    std::vector<GroundGrid> craters_;
    std::vector<GroundGrid> rings_;
    std::vector<GroundHole> holes_;
    Vec3 burst_point_;
    std::int32_t origin_x_ = 0;
    std::int32_t origin_z_ = 0;
    std::uint16_t page_a_ = 0, page_b_ = 0;
    std::uint16_t palette_below_ = 0, palette_above_ = 0, palette_ramp_ = 0, palette_x_ = 0;
    std::array<std::uint16_t, 48> rows_{};
};

Result<std::map<std::string, GroundFxParams>> parse_ground_fx(std::string_view json_text);
Result<std::map<std::string, GroundFxParams>> load_ground_fx(const std::filesystem::path& gamedata_dir,
                                                            const std::filesystem::path& mods_dir, std::string* source);

}  // namespace sh::effects
