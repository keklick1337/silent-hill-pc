#pragma once

// The engine's world-space effect draw list (docs/RENDER.md section 2.2): particles, billboards, splashes and
// map effects described as polygons of the 3D scene with engine types, so drawers build no GPU packets and the
// frame looks nothing of theirs up by packet address.
//
// A polygon keeps the screen position the game computed for each corner (the image is drawn from it when nothing
// better is known) and carries, explicitly, everything else the renderer needs:
// - the unrounded projection of a corner and its view depth (perspective-correct drawing),
// - the corner's view-space position with the projection constants it was transformed with (flashlight, near clip),
// - the per-corner depth of the polygon and how it was produced (depth ordering against the scene),
// - a per-polygon alpha and the "draw affine" / "not lit by the flashlight" flags,
// - for world and character mesh polygons, the surface normals of the corners (the per-pixel flashlight lights the
//   surface from them instead of using the corner colour) and the per-corner fog amount.
//
// A drawer records a Command and gets an id back; its place in the frame travels separately with the id (as for the
// 2D list, src/render/draw2d). Shared field semantics (pixels, texels, colour 128, pages, palettes) are those of
// draw2d.h.

#include "render/draw2d/draw2d.h"
#include "render/frame/frame.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <variant>
#include <vector>

namespace sh::draw3d {

using draw2d::Palette;
using draw2d::Point;
using draw2d::Rgb;
using draw2d::Shade;
using draw2d::TexCoord;
using draw2d::TexturePage;

// The unrounded projection of a corner: screen position relative to the draw offset and the view depth (> 0; the
// corner is drawn perspective-correct only when every corner of the polygon has one).
struct Precise {
    float x = 0.f, y = 0.f, w = 0.f;
    friend bool operator==(const Precise&, const Precise&) = default;
};

// A corner's position in view space and the projection it went through (screen = offset + position * h / z), so a
// polygon crossing the near plane can be clipped and the clip corners projected the same way.
struct ViewPosition {
    float x = 0.f, y = 0.f, z = 0.f;
    bool casts_shadow = true;  // false: the flashlight's shadow pass skips it (the player himself)
    float fade = 0.f;          // the character fade the corner belonged to
    float ofx = 0.f, ofy = 0.f, h = 1.f;
    friend bool operator==(const ViewPosition&, const ViewPosition&) = default;
};

// A corner's surface normal in view space (unit length).
struct Normal {
    float x = 0.f, y = 0.f, z = 0.f;
    friend bool operator==(const Normal&, const Normal&) = default;
};

// What the normals of a polygon describe; the shader lights the two kinds differently.
enum class LightKind : std::uint8_t {
    None,       // the polygon carries no normals: it keeps its corner colours
    World,      // static world geometry
    Character,  // a character's part
};

struct Vertex {
    Point xy;                      // screen position as the game computed it
    TexCoord uv;                   // textured polygons
    Rgb color;                     // gouraud polygons; flat ones use vertex 0's
    std::uint8_t fog = 0;          // fog amount of the corner (gouraud polygons; 0 = none)
    std::optional<Precise> precise;
    std::optional<ViewPosition> view;
    Normal normal;                 // meaningful when the polygon's `light` is not None
    friend bool operator==(const Vertex&, const Vertex&) = default;
};

// How the per-corner depth of a polygon was produced; the renderer orders each kind differently against the scene.
enum class DepthSource : std::uint8_t {
    Projection,  // the projection unit's depth registers as they stood when the polygon was placed
    WorldMesh,   // static world geometry (drawn in painter order among itself)
    Exact,       // a depth the drawer computed itself
    Actor,       // a character's own per-corner depth
};

struct Depth {
    std::array<std::uint32_t, 4> sz{};  // view depth per corner, projection units (a triangle uses 0..2)
    DepthSource source = DepthSource::Projection;
    bool far = false;  // far-band world geometry of the whole-town view
    friend bool operator==(const Depth&, const Depth&) = default;
};

// A triangle or a quad of the 3D scene (vertex order 0 1 2 3 is the hardware's: triangles 0-1-2 and 1-2-3). A
// textured polygon makes its page the current draw-mode page.
struct Polygon {
    std::uint8_t count = 4;  // 3 or 4
    bool textured = false;
    bool gouraud = false;  // per-vertex colour, else v[0].color for the whole polygon
    Shade shade;
    std::array<Vertex, 4> v{};
    Palette palette;
    TexturePage page;
    std::optional<Depth> depth;  // absent: the polygon takes its layer's depth
    std::uint8_t alpha = 255;    // 255 = the blend as the page and shade say; less fades the polygon out
    bool affine = false;         // drawn from the screen positions alone, never perspective-correct
    bool no_flashlight = false;  // kept out of the per-pixel flashlight
    // A mark lying on a surface (render/decals): its override texture holds a normal, a darkening and a coverage
    // instead of a colour, and the shader lights it as part of that surface.
    bool surface_decal = false;
    LightKind light = LightKind::None;  // corners 0..count-1 carry normals (a triangle needs three)
    friend bool operator==(const Polygon&, const Polygon&) = default;
};

using Command = std::variant<Polygon>;

// Ids are 24-bit and wrap; 0 is never handed out, so a zeroed marker names nothing.
using CommandId = std::uint32_t;
constexpr CommandId kIdMask = 0xFFFFFFu;

// The commands of the last frames (same contract as draw2d::CommandRing): recording never fails, the oldest
// commands are overwritten once the ring is full, and a lookup of an overwritten or never issued id returns nothing.
class CommandRing {
public:
    explicit CommandRing(std::size_t capacity_log2 = 17);

    CommandId record(const Command& command);
    const Command* find(CommandId id) const;

    std::size_t capacity() const { return slots_.size(); }
    std::uint64_t recorded() const { return recorded_; }

private:
    struct Slot {
        CommandId id = 0;
        Command command;
    };
    std::vector<Slot> slots_;
    CommandId next_ = 1;
    std::uint64_t recorded_ = 0;
};

// The process-wide ring the game's drawers record into.
CommandRing& frame_commands();

// The depth of a polygon placed right after a run of projections: the projection's four newest depths (oldest
// first, engine/math/view_projection.h), corners 0..3 of a quad or the three newest for a triangle's 0..2.
inline Depth projection_depth(const std::array<std::uint16_t, 4>& newest, int count) {
    Depth d;
    if (count == 3)
        d.sz = {newest[1], newest[2], newest[3], 0};
    else
        d.sz = {newest[0], newest[1], newest[2], newest[3]};
    d.source = DepthSource::Projection;
    return d;
}

// Records `polygon` and places it in the frame at `key`. The polygon carries everything itself (depth, alpha, flags):
// nothing armed elsewhere is taken. `depth_max` feeds the frame's depth maximum (what the renderer normalises
// per-corner depth with); submit_projected passes the largest of the four newest depths.
CommandId submit(const Polygon& polygon, frame::OrderKey key, std::uint32_t depth_max);
// submit() with the polygon's depth taken from the projection's four newest depths.
CommandId submit_projected(Polygon polygon, const std::array<std::uint16_t, 4>& newest, frame::OrderKey key);
// submit() of a polygon placed after a run of projections: one that carries no depth takes the projection's four
// newest depths (submit_projected); one that does feeds the frame maximum with its own corners'.
CommandId submit_placed(Polygon polygon, const std::array<std::uint16_t, 4>& newest, frame::OrderKey key);

}  // namespace sh::draw3d
