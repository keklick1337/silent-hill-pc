// SPDX-License-Identifier: GPL-3.0-or-later
// Glow fields: a coarse byte grid laid over the whole screen into which
// round soft spots (splats of a radial falloff kernel) add brightness; a frame clears the grid,
// adds the spots of the glowing things, jitters every lit cell down a little and draws the grid
// as Gouraud quads in a colour ramp (a table of colour words). The Flauros scene of map6_s04
// draws its glows and light rays this way, and colours its meshes from the same cells: a mesh
// is a flat grid of the field's size laid in the world (two axes of a plane, a step and an
// offset each) whose quads take the colours of the cells and are projected by the game. The
// glowing things themselves are the field's emitters (a point, the bones of a skeleton, a ray, a
// flash), which each frame put their spots into the cleared grid. The module owns the grid, the
// kernel and the emitters; the game gives the random numbers, the views and projections, the
// things the emitters follow, and turns the quads into primitives (GlowFieldHost). Every field
// (grid, kernel, decay, colours, draw mode, meshes, emitters) is data:
// gamedata/effects/glow_fields.json.

#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "formats/common/status.h"

namespace sh::effects {

// A world mesh coloured from the field's cells: vertex (row i, column j) has
// v[col_axis] = j * col_step - col_offset and v[row_axis] = i * row_step - row_offset (the third
// axis 0), in the model space of the transform. Its quads of rows from far_from_row on go
// far_slot_add slots further than the field's slot; one page packet goes to each of the two
// slots after the quads.
struct GlowMesh {
    int col_axis = 0;  // 0 x, 1 y, 2 z
    int row_axis = 1;
    std::int32_t col_step = 0;
    std::int32_t col_offset = 0;
    std::int32_t row_step = 0;
    std::int32_t row_offset = 0;
    bool fold = false;  // the cells' colours mirrored above fold_peak
    int far_from_row = 0;
    std::int32_t far_slot_add = 0;
    std::int32_t tpage = 0;
    std::int32_t dtd = 1;
    // Placed at a world point (Q8, through the world-screen matrix) turned by rotation (Q12
    // angles, Z X Y negated); without a transform the mesh is drawn with whatever the game's
    // view is at the time (the ray mesh of the original never set one).
    bool has_transform = false;
    std::int16_t position[3] = {};
    std::int16_t rotation[3] = {};
};

// The game's handle of a thing an emitter follows (a position, a rotation, a skeleton, a scene
// timer); the module keeps it and hands it back to the host, which reads the thing.
using GlowAnchor = std::uintptr_t;

enum class GlowKind : std::uint8_t {
    Point,  // a spot on a point that follows a position and a rotation, growing to its size
    Bones,  // spots on bones of a skeleton that swell and brighten, then shrink and fade
    Ray,    // a sweep of random spots across the screen (a light ray); keeps its view set
    Flash,  // waits for a scene time, then a spot at a fixed screen point that grows ever faster
};

// One glowing thing.
struct GlowEmitter {
    bool active = false;
    GlowKind kind = GlowKind::Point;
    std::int32_t size = 0;
    std::int32_t size_limit = 0;  // Point: the size it grows to; Bones: the swell factor
    std::int32_t strength = 0;
    std::int32_t strength_max = 0;
    std::int32_t phase = 0;
    std::int32_t scale = 0;       // Bones: the skeleton's swell (Q12)
    std::int32_t base = 0;        // Bones: the size before the swell
    std::int16_t offset[3] = {};  // the spot in the emitter's view (model space)
    GlowAnchor at = 0;
    GlowAnchor turn = 0;
    GlowAnchor skeleton = 0;
    GlowAnchor timer = 0;
    GlowAnchor target = 0;
};

// The emitters of a field (glow_fields.json "emitters"); every number is the original's.
struct GlowEmitterParams {
    int capacity = 0;
    // A projected spot is size_q8 * (1 - depth * depth_scale / 4096) / 16 pixels.
    std::int32_t depth_scale = 4;
    struct Point {
        std::int16_t offset[3] = {};
        std::int32_t size = 0;      // at the start
        std::int32_t grow = 0;      // per frame
        std::int32_t size_max = 0;
        std::int32_t strength = 0;
    } point;
    struct BoneSpot {
        int bone = 0;               // 0: the skeleton's root, at its origin
        std::int32_t factor = 0;    // size = base + swell * factor
        std::int32_t strength = 0;
        std::int32_t strength_max = 0;
    };
    struct Bones {
        std::vector<BoneSpot> spots;
        std::int32_t offset_scale = 0;  // a bone's spot is its translation times this (Q12)
        std::int32_t base_root = 0;
        std::int32_t base_bone = 0;
        std::int32_t grow = 0;           // swell per frame, then...
        std::int32_t shrink = 0;         // ...per frame once the timer passes fade_after
        std::int32_t strength_step = 0;  // brightening, then fading, per frame
        std::int32_t fade_after = 0;     // scene timer (Q12 s)
    } bones;
    struct Ray {
        std::int32_t sweep = 0;   // the sweep's advance per frame (pixels)
        int spots = 0;            // spots per frame
        std::int32_t width = 0;   // x = rand % width / x_divisor + sweep - width
        std::int32_t x_divisor = 1;
        std::int32_t height = 0;  // y = rand % height / y_divisor + y_offset
        std::int32_t y_divisor = 1;
        std::int32_t y_offset = 0;
        std::int32_t size_range = 1;  // size = rand % size_range + size_min
        std::int32_t size_min = 0;
        std::int32_t strength_range = 1;  // strength = rand % strength_range
    } ray;
    struct Flash {
        std::int32_t size = 0;      // Q12 (the spot's own size; the drawn one is below)
        std::int32_t strength = 0;
        std::int32_t start_after = 0;  // scene timer (Q12 s)
        std::int32_t sound = 0;
        std::int32_t rumble = 0;
        std::int32_t x = 0;
        std::int32_t y = 0;
        std::int32_t spot_size = 0;      // drawn size at the start (pixels)...
        std::int32_t spot_speed = 0;     // ...growing by speed (Q12) per frame...
        std::int32_t spot_speed_add = 0; // ...which grows by this
        std::int32_t spot_strength = 0;
    } flash;
};

struct GlowFieldParams {
    int cols = 28;
    int rows = 19;
    int cell = 12;               // pixels per cell
    std::int32_t origin_x = 0;   // screen point (from the centre) of cell (0, 0)
    std::int32_t origin_y = 0;
    std::int32_t extent_x = 0;   // spots are clipped to 0..extent from the origin
    std::int32_t extent_y = 0;
    int kernel_radius = 64;      // the spot kernel is (2r + 1) x (2r + 1) bytes
    // Decay of a lit cell per frame: v - decay_step + ((rand & decay_jitter_mask) >> decay_jitter_shift),
    // clamped to 0..255.
    std::int32_t decay_step = 2;
    std::int32_t decay_jitter_mask = 0x30;
    int decay_jitter_shift = 4;
    // Folded colour (colour_at with fold): a cell above fold_peak reads as 2 * fold_peak - v.
    std::int32_t fold_peak = 100;
    std::int32_t slot = 0;       // ordering-table slot of the grid's primitives
    std::int32_t prim_code = 0x3A;
    std::int32_t mode_tpage = 0;
    std::int32_t mode_dtd = 1;
    std::vector<std::uint32_t> shade = std::vector<std::uint32_t>(256, 0);  // colour word by brightness
    std::map<std::string, GlowMesh> meshes;
    GlowEmitterParams emitters;
};

struct GlowQuad {
    std::int16_t x[4] = {};
    std::int16_t y[4] = {};
    std::uint32_t color[4] = {};
    bool lit = false;        // false: the quad takes its packet space but is not linked
    bool projected = false;  // a mesh quad: x, y are the projection's screen points
};

class GlowFieldHost {
public:
    virtual ~GlowFieldHost() = default;

    virtual std::int32_t rand16() = 0;  // 0..32767
    virtual void emit_quad(const GlowQuad& q, std::int32_t slot) = 0;
    virtual void emit_page(std::int32_t slot, std::int32_t tpage, std::int32_t dtd) = 0;
    // Meshes: sets the view of the mesh (its transform, if it has one), then projects the four
    // corners of each quad (model space) to screen points.
    virtual void begin_mesh(const GlowMesh& mesh) { (void)mesh; }
    virtual void project4(const std::int16_t (&v)[4][3], std::int16_t (&x)[4], std::int16_t (&y)[4]) {
        for (int k = 0; k < 4; k++) {
            x[k] = v[k][0];
            y[k] = v[k][1];
        }
    }

    // Emitters. The views: a point's (the world-screen view moved to `at` and turned by `turn`),
    // a skeleton's (its root bone's world view scaled by `scale`, Q12) and a ray's (from `at`
    // aimed at `target`; the ray's view stays set for the frame, a mesh without a transform draws
    // with it). project_view projects a model point with the last view set: screen x, y from the
    // centre and the depth.
    struct Projected {
        std::int32_t x = 0;
        std::int32_t y = 0;
        std::int32_t depth = 0;
    };
    virtual void view_point(GlowAnchor at, GlowAnchor turn) { (void)at, (void)turn; }
    virtual void view_skeleton(GlowAnchor skeleton, std::int32_t scale) { (void)skeleton, (void)scale; }
    virtual void view_ray(GlowAnchor at, GlowAnchor target) { (void)at, (void)target; }
    virtual Projected project_view(const std::int16_t (&v)[3]) { return Projected{v[0], v[1], 0}; }
    // A bone's translation in its parent (as the skeleton holds it).
    virtual void bone_translation(GlowAnchor skeleton, int bone, std::int32_t (&t)[3]) {
        (void)skeleton, (void)bone;
        t[0] = t[1] = t[2] = 0;
    }
    virtual std::int32_t timer(GlowAnchor timer) { (void)timer; return 0; }
    // A flash starts: its sound and the pad's rumble.
    virtual void flash_cue(std::int32_t sound, std::int32_t rumble) { (void)sound, (void)rumble; }
};

class GlowField {
public:
    explicit GlowField(GlowFieldParams params);
    const GlowFieldParams& params() const { return params_; }

    // The kernel is built (once) and the slot reset; the cells are kept.
    void reset();
    void clear();
    // Adds a spot centred on a screen point (from the screen centre) of radius `size` pixels
    // scaled by `strength` (Q12), which also scales its brightness.
    void splat(std::int32_t x, std::int32_t y, std::int32_t size, std::int32_t strength);
    void decay(GlowFieldHost& host);
    void draw(GlowFieldHost& host) const;
    // Draws a mesh of the field by name; false when the field has no such mesh.
    bool draw_mesh(GlowFieldHost& host, const std::string& mesh) const;
    // The colour word of a cell (by index, row-major); fold mirrors the ramp above fold_peak.
    std::uint32_t colour_at(int index, bool fold) const;
    std::int32_t slot() const { return slot_; }

    // Emitters (params().emitters): each add takes the first free one (nothing when all are in
    // use). remove ends every emitter of a kind; clear_emitters ends all of them.
    void add_point(GlowAnchor at, GlowAnchor turn);
    void add_bones(GlowFieldHost& host, GlowAnchor at, GlowAnchor skeleton, GlowAnchor timer);
    void add_ray(GlowFieldHost& host, GlowAnchor at, GlowAnchor target);
    void add_flash(GlowAnchor timer);
    void remove(GlowKind kind);
    void clear_emitters();
    // A frame: the grid is cleared and every emitter (in slot order) updates and adds its spots.
    void emit(GlowFieldHost& host);
    const std::vector<GlowEmitter>& emitters() const { return emitters_; }
    std::int32_t flash_size() const { return flash_size_; }
    std::int32_t flash_speed() const { return flash_speed_; }

    const std::vector<std::uint8_t>& cells() const { return cells_; }
    const std::vector<std::uint8_t>& kernel() const { return kernel_; }

private:
    GlowFieldParams params_;
    std::vector<std::uint8_t> kernel_;
    std::vector<std::uint8_t> cells_;
    std::int32_t slot_ = 0;
    std::vector<GlowEmitter> emitters_;
    // The flash's drawn size and its growth are one pair for the whole field, as in the original.
    std::int32_t flash_size_ = 0;
    std::int32_t flash_speed_ = 0;

    GlowEmitter* acquire();
    bool update(GlowFieldHost& host, GlowEmitter& e);
};

// Reads every field of gamedata/effects/glow_fields.json (or the last mod's copy), by name.
Result<std::map<std::string, GlowFieldParams>> parse_glow_fields(std::string_view json_text);
Result<std::map<std::string, GlowFieldParams>> load_glow_fields(const std::filesystem::path& gamedata_dir,
                                                               const std::filesystem::path& mods_dir,
                                                               std::string* source);

}  // namespace sh::effects
