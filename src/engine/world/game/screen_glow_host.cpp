// SPDX-License-Identifier: GPL-3.0-or-later
//
// The screen light glow of the running game (engine/world/screen_glow_overlay.h) and the billboard angles of a point
// light (engine/world/screen_glow.h): the glow's 2D commands are linked into the world ordering table the caller
// draws into, each at a packet address of its own (the frame keys an entry's depth records by it).

#include <cstddef>
#include <cstdint>

#include "engine/camera/view_system.h"
#include "engine/config/pc_config.h"
#include "engine/game_state/session.h"
#include "engine/math/geometry_state.h"
#include "engine/math/fixed_geometry.h"
#include "engine/world/screen_glow.h"
#include "engine/world/screen_glow_overlay.h"
#include "engine/world/world_atmosphere.h"
#include "engine/math/geometry_state.h"
#include "render/draw2d/draw2d.h"
#include "render/frame/frame.h"
#include "render/gpu/render_state.h"

extern "C" {
// The display's pixel aspect as the present scales it.
float GR_LivePixelAspect(void);
}
// The world view's stretch (the present's projection).

namespace {

namespace sw = sh::world;

class GlowHost final : public sw::GlowOverlayHost {
public:
    void* ot = nullptr;

    void link(int entry, int buffer, int part, const sh::draw2d::Command& command) override {
        (void)buffer;
        (void)part;
        sh::draw2d::submit(command, {sh::frame::Layer::World, static_cast<std::uint32_t>(entry)},
                           sh::math::geometry().depth_history_max());
    }
    int buffer() override { return sh::frame::build_buffer(); }
    sw::GlowScreen screen() override {
        sw::GlowScreen s;
        s.width = sh::game::session().game.screen_width;
        s.height = sh::game::session().game.screen_height;
        s.v_scale = sh::render::picture().world_v_scale;
        s.v_shift = sh::render::picture().world_v_shift;
        // The fill quads run from the outer ring to a border at the edge of the visible frame: in a wide view that
        // is past +-width / 2 (the same bound the mesh cull uses).
        const std::int32_t half = s.width >> 1;
        s.ring_half_w = half;
        if (PcConfig_RenderHeight() > 0 && PcConfig_RenderWidth() > 0) {
            const float w =
                ((float)s.height * (float)PcConfig_RenderWidth() / (2.0f * (float)PcConfig_RenderHeight())) *
                GR_LivePixelAspect() /
                (sh::render::picture().world_h_scale > 0.01f ? sh::render::picture().world_h_scale : 1.0f);
            s.ring_half_w = ((std::int32_t)w < half) ? half : (std::int32_t)w;
        }
        return s;
    }
    bool flashlight_lit() override { return sh::render::flashlight().active != 0; }
};

GlowHost& glow_host() {
    static GlowHost h;
    return h;
}

}  // namespace

namespace sh::world {

void screen_glow_init() { screen_glow_overlay().init(glow_host()); }

}  // namespace sh::world

extern "C" {

// The view-space point of a light at `pos` (Q12 world) and its direction `rot` turned into the view: the light's
// screen point (depth in z) and the azimuth and altitude it is seen under. The view matrix of the point stays loaded
// in the transform unit for the code that follows.
void Screen_BillboardAnglesCalc(sw::Vec3* screen_pos, std::int32_t* azimuth, std::int32_t* altitude,
                                const sw::Vec3s16* rot, const sw::Vec3* pos) {
    namespace cam = sh::camera;
    cam::FrameRecord frame{};
    frame.world = cam::Matrix{{{4096, 0, 0}, {0, 4096, 0}, {0, 0, 4096}}, {pos->x >> 4, pos->y >> 4, pos->z >> 4}};
    frame.world_current = 1;
    const cam::Matrix view = cam::view_matrix(cam::view_system().world_screen(), cam::frame_world(&frame));

    const sh::math::Transform& t = sh::math::transform_of(view);
    sh::math::geometry().load_rotation(t);
    sh::math::geometry().load_translation(t);

    const sh::math::MatVec origin = sh::math::matrix_vector(t.rot, {}, sh::math::Offset::Translation,
                                                          {view.t[0], view.t[1], view.t[2]});
    const sh::math::MatVec dir =
        sh::math::matrix_vector(t.rot, {rot->x, rot->y, rot->z});

    const sh::math::RelativeRotation turn = sh::math::relative_rotation({origin.full.x, origin.full.y, origin.full.z},
                                                                      {dir.full.x, dir.full.y, dir.full.z});
    *azimuth = turn.azimuth;
    *altitude = turn.altitude;
    *screen_pos = sw::screen_point_project({origin.full.x, origin.full.y, origin.full.z}, {dir.full.x, dir.full.y, dir.full.z},
                                       sh::math::geometry().screen_distance(), sh::math::geometry().screen_offset_x(),
                                       sh::math::geometry().screen_offset_y());
}

// The screen glow of the point light at `pos` (Q12 world) pointing along `rot`, of `size`, into ordering table `ot`.
void Screen_BillboardQuadDraw(void* ot, std::int32_t size, const sw::Vec3s16* rot, const sw::Vec3* pos) {
    sw::Vec3 screen_pos;
    std::int32_t azimuth = 0;
    std::int32_t altitude = 0;
    Screen_BillboardAnglesCalc(&screen_pos, &azimuth, &altitude, rot, pos);
    glow_host().ot = ot;
    sw::screen_glow_overlay().draw(glow_host(), screen_pos, size, azimuth, altitude);
}

}  // extern "C"
