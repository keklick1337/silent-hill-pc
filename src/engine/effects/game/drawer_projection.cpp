// SPDX-License-Identifier: GPL-3.0-or-later
// The game side of the engine view projection (engine/math/view_projection.h): the screen setup, the precise switches
// and the projection history of the geometry state (engine/math/geometry_state.h), read when an engine drawer
// starts and written back when it is done, for the code after it that still projects with that state.

#include "engine/effects/drawer_projection.h"

#include <cstdint>

#include "engine/math/geometry_state.h"
#include "engine/math/precise_capture.h"
#include "render/draw3d/draw3d.h"
#include "render/frame/frame.h"
#include "render/gpu/render_state.h"
#include "render/scene/scene.h"


namespace g = sh::math;
namespace drawer = sh::effects::drawer;

g::ViewProjection g::game_view_projection() {
    const GeometryState& geometry = sh::math::geometry();
    const ScreenSetup s = geometry.screen();
    PreciseSetup p;
    p.precise = sh::render::precise().enabled != 0;
    p.options.unquantized_depth = sh::render::precise().unquantized_depth != 0;
    p.options.whole_map_far = sh::render::whole_town().reproject_far != 0;
    p.skip_exact_camera = (sh::render::precise().debug & 16) != 0;
    p.unclamped_view = sh::render::precise().enabled != 0 && (sh::render::precise().debug & 32) != 0;
    p.exact_products = sh::render::smooth_view();
    ViewProjection vp(s, p);
    vp.set_history(geometry.history());
    return vp;
}

void g::end_game_view_projection(const ViewProjection& vp) {
    sh::math::geometry().set_history(vp.history());
    const int n = vp.recent_count();
    if (n > 0 && sh::render::precise().enabled) {
        // The projection of the newest vertex, as GeometryState::project leaves it.
        const PreciseVertex& r = vp.recent()[2];
        sh::render::precise().screen_offset_x = r.ofx;
        sh::render::precise().screen_offset_y = r.ofy;
        sh::render::precise().projection_distance = r.h;
    }
    if (sh::render::whole_town().reproject_far && n > 0) sh::render::whole_town().last_depth = vp.recent()[2].unclamped_depth;
}

void drawer::set_matrix(g::ViewProjection& vp, const void* matrix) {
    const auto* rot = static_cast<const std::int16_t*>(matrix);
    const auto* trans = reinterpret_cast<const std::int32_t*>(static_cast<const std::uint8_t*>(matrix) + 20);
    g::Matrix3 m;
    for (int i = 0; i < 9; ++i) m.m[i / 3][i % 3] = rot[i];
    g::ExactTransform exact;
    exact.has_rotation = sh::math::capture::rotation_of(matrix, exact.rot);
    exact.has_translation = sh::math::capture::translation_of(matrix, exact.trans);
    vp.set_view(m, {trans[0], trans[1], trans[2]}, &exact);
}

void drawer::move_to(g::ViewProjection& vp, const void* world_screen, const g::Vec3i& point) {
    const auto& ws = *static_cast<const g::Transform*>(world_screen);
    const g::Vec3i moved = vp.rotate_long(point);
    const auto wrap_add = [](std::int32_t a, std::int32_t b) {
        return static_cast<std::int32_t>(static_cast<std::uint32_t>(a) + static_cast<std::uint32_t>(b));
    };
    g::Transform out;
    out.rot = vp.rotation();
    out.trans = {wrap_add(moved.x, ws.trans.x), wrap_add(moved.y, ws.trans.y), wrap_add(moved.z, ws.trans.z)};
    const double p[3] = {static_cast<double>(point.x), static_cast<double>(point.y), static_cast<double>(point.z)};
    g::capture::register_camera_point(ws, p, out);
    vp.set_translation(out.trans);
}

void drawer::set_current(g::ViewProjection& vp) {
    const g::Transform& view = g::geometry().view();
    g::ExactTransform exact;
    exact.has_rotation = sh::math::capture::current_rotation(exact.rot);
    exact.has_translation = sh::math::capture::current_translation(exact.trans);
    vp.set_view(view.rot, view.trans, &exact);
}

void drawer::load_view(const void* matrix) {
    const auto& record = *static_cast<const g::Transform*>(matrix);
    g::geometry().load_rotation(record);
    g::geometry().load_translation(record);
}

void drawer::corner_records(const g::PreciseVertex& r, sh::draw3d::Vertex& v) {
    if (!sh::render::precise().enabled && !sh::render::flashlight().per_pixel) return;
    if (sh::render::precise().enabled) v.precise = sh::draw3d::Precise{r.sx, r.sy, r.w};
    if (sh::render::view_space_wanted()) {
        const sh::frame::State& f = sh::frame::state();
        v.view = sh::draw3d::ViewPosition{r.view_x, r.view_y, r.view_z, !f.no_shadow_cast, f.chara_fade,
                                          r.ofx,    r.ofy,    r.h};
    }
}

namespace {

// The polygon into the frame at `key`; one without depths of its own takes the geometry state's newest.
void place_keyed(sh::draw3d::Polygon& polygon, sh::frame::OrderKey key) {
    sh::draw3d::submit_placed(polygon, sh::math::geometry().depth_history(), key);
}

}  // namespace

namespace {

// View units an effect is drawn in front of where it stands: a pool or a mark lying on the floor wins against the
// floor it lies on, while a chair leg or a fence in front of it still hides it.
constexpr std::int32_t kEffectDepthBias = 32;

// On the PS1 the ordering table alone put an effect between the walls; the scene renderer draws the world with a
// depth buffer first, so an effect polygon of the 3D scene needs a depth of its own or it is drawn over everything
// nearer (blood pools over the cafe chairs, flies through the alley fence). Corners projected precisely give their
// own view depth; the rest take their ordering slot's (slot = screen z / 8, the world table's scale).
}  // namespace

void drawer::bind_scene_depth(sh::draw3d::Polygon& polygon, sh::frame::OrderKey key, drawer::SceneDepth mode,
                              const std::int32_t* corner_z) {
    const bool enabled = sh::render::precise().enabled;
    if ((!enabled && !corner_z) || key.layer != sh::frame::Layer::World || polygon.depth || key.slot == 0) return;
    const int n = polygon.count == 3 ? 3 : 4;
    const bool at_slot = mode == drawer::SceneDepth::Slot;
    bool precise = true;
    for (int k = 0; k < n; ++k) precise = precise && polygon.v[k].precise && polygon.v[k].precise->w > 0.f;
    sh::draw3d::Depth d;
    d.source = sh::draw3d::DepthSource::Actor;
    const std::int32_t slot_z = static_cast<std::int32_t>(key.slot << 3);
    // Exact: pushed back as far as the map is written behind itself, so the two compare at their true places.
    const std::int32_t bias = mode == drawer::SceneDepth::Exact
                                  ? -static_cast<std::int32_t>(sh::scene::kWorldDepthBias)
                                  : kEffectDepthBias;
    for (int k = 0; k < n; ++k) {
        const std::int32_t z = at_slot   ? slot_z
                               : precise ? static_cast<std::int32_t>(polygon.v[k].precise->w)
                               : corner_z ? corner_z[k]
                                          : slot_z;
        const std::int32_t biased = z - bias > 1 ? z - bias : 1;
        d.sz[static_cast<std::size_t>(k)] = static_cast<std::uint32_t>(biased);
        sh::draw3d::Vertex& v = polygon.v[k];
        // The renderer tests a translucent polygon against the scene only when its corners carry a view depth.
        if (precise)
            v.precise->w = static_cast<float>(biased);
        else if (enabled)
            v.precise = sh::draw3d::Precise{static_cast<float>(v.xy.x), static_cast<float>(v.xy.y),
                                            static_cast<float>(biased)};
    }
    polygon.depth = d;
}

void drawer::DrawerProjection::place(sh::draw3d::Polygon& polygon, sh::frame::OrderKey key, SceneDepth depth) {
    bind_scene_depth(polygon, key, depth);
    if (active_ && polygon.count == 4 && !polygon.depth) {
        polygon.depth = sh::draw3d::projection_depth(vp_.depths(), 4);
        place_keyed(polygon, key);
        return;
    }
    // A triangle noted all four depth registers in the frame maximum: the registers are written back first.
    around([&] { place_keyed(polygon, key); });
}

std::int32_t drawer::screen_distance() {
    return sh::math::geometry().screen_distance();
}
