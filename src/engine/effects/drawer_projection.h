#pragma once

// The engine projection of an effect drawer over the game's view state (RENDER.md section 4): the game's matrices as
// the engine view projection (engine/math/view_projection.h) takes them, with the exact camera the precise path
// keeps for them, and the projection history of the game's geometry state (engine/math/geometry_state.h) continued
// and handed back around the drawer (the code after it still projects with that state). Implemented in src/engine/effects/game/drawer_projection.cpp.

#include <cstdint>

#include "engine/math/view_projection.h"
#include "render/draw3d/draw3d.h"

namespace sh::effects::drawer {

// Sets the view of `vp` to the game matrix at `matrix` (a matrix record: nine Q12 rotation values, padding,
// three translation values), with the exact camera the precise path knows for that record.
void set_matrix(sh::math::ViewProjection& vp, const void* matrix);

// The view of `vp` (set from the world-screen record `world_screen`) moved to the world point `point` (geometry
// units): the translation becomes the point's view position, the rotation is kept; the unquantised camera's view
// position goes with it.
void move_to(sh::math::ViewProjection& vp, const void* world_screen, const sh::math::Vec3i& point);

// Sets the view of `vp` to the one loaded into the geometry state before the drawer ran (its rotation and
// translation, with the exact camera latched for them).
void set_current(sh::math::ViewProjection& vp);

// Loads the game matrix at `matrix` as the view of the geometry state, for drawers that run after an engine drawer and
// still project with that state (the map effect frame's view before the overlays' slot drawers).
void load_view(const void* matrix);

// The screen distance word the game's view code set (as the library's read of it returns it).
std::int32_t screen_distance();

// The precise position and the view-space record of a projected corner, as storing its screen word recorded them
// and placing the packet took them back: only with the precise path or the view-space pass on.
void corner_records(const sh::math::PreciseVertex& r, sh::draw3d::Vertex& v);

// Where an effect polygon of the 3D scene is tested against the scene's depth.
enum class SceneDepth : std::uint8_t {
    // A little in front of where it stands: a pool or a mark lying on the floor wins against that floor, a chair leg
    // or a fence in front of it still hides it.
    Margin,
    // Exactly where it is against the map, no margin either way: a surface that floats above its floor (the drain's
    // water) still wins against it, but the walls and ledges it runs along hide it where they stand in front.
    Exact,
    // At the depth of its order slot, whatever depth its corners were projected at: the game put it at a fixed slot
    // far behind where it stands so that only what lies beyond that distance shows it (the water's sheen under the
    // surface of the water near the camera).
    Slot,
};

// Gives a polygon of the 3D scene placed at `key` its own per-corner view depth, as `mode` says, so the renderer
// tests it against the scene where it stands rather than at its order slot (the slot is the farthest corner's: a
// slanted polygon would lose to whatever lies behind its near end). Corners projected precisely give their depth
// themselves; `corner_z` is the view depth of each corner (hardware order) for those that do not. One that already
// carries a depth or lies outside the world layer is left as it is, and so is one drawn without the precise
// projection when the caller has no corner depths for it.
void bind_scene_depth(sh::draw3d::Polygon& polygon, sh::frame::OrderKey key, SceneDepth mode,
                      const std::int32_t* corner_z = nullptr);

// The projection of one engine drawer call over the game's view state: it continues the geometry state's history when
// the drawer starts a view and gives the history back when the drawer is done, and around code that projects with
// the geometry state itself (a ray trace, a collision query), so the depths and screen words read afterwards are
// the ones a single shared history would hold.
class DrawerProjection {
public:
    DrawerProjection() = default;
    DrawerProjection(const DrawerProjection&) = delete;
    DrawerProjection& operator=(const DrawerProjection&) = delete;
    ~DrawerProjection() { sync_out(); }

    // Starts the drawer's projection (once; later calls keep it).
    sh::math::ViewProjection& vp() {
        if (!active_) {
            vp_ = sh::math::game_view_projection();
            active_ = true;
        }
        return vp_;
    }
    bool active() const { return active_; }

    // Places `polygon` at `key` with the depth of the drawer's newest projections, as placing a packet after them
    // took the shared depth history (and the armed alpha and flags, as before); `depth` says how it meets the scene.
    void place(sh::draw3d::Polygon& polygon, sh::frame::OrderKey key, SceneDepth depth = SceneDepth::Margin);

    // Code that projects with the geometry state runs in `f`; the drawer's view is kept, its history continues from
    // the state's.
    template <class F>
    void around(F&& f) {
        const bool was = active_;
        sh::math::ViewProjection saved = vp_;
        sync_out();
        f();
        if (was) {
            vp_ = sh::math::game_view_projection();
            vp_.set_view(saved.rotation(), saved.translation(), &saved.exact());
            active_ = true;
        }
    }

private:
    void sync_out() {
        if (active_) sh::math::end_game_view_projection(vp_);
        active_ = false;
    }

    sh::math::ViewProjection vp_;
    bool active_ = false;
};

}  // namespace sh::effects::drawer
