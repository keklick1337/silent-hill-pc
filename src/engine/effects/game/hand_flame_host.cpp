// SPDX-License-Identifier: GPL-3.0-or-later
// The match flame in Harry's hand (engine/effects/hand_flame.h): its picture, and its host over the session's
// skeleton, the camera's view and the frame. The map light machine draws it while its preset shows the flame.

#include <cstdint>

#include "engine/camera/camera_types.h"
#include "engine/camera/view_space.h"
#include "engine/camera/view_system.h"
#include "engine/effects/drawer_projection.h"
#include "engine/effects/effect_world.h"
#include "engine/effects/hand_flame.h"
#include "engine/effects/map_lights.h"
#include "engine/game_state/game_state.h"
#include "engine/game_state/session.h"
#include "engine/memory/work_buffers.h"
#include "engine/loop/game_random.h"
#include "engine/player/harry_states.h"
#include "engine/loader/queue/disc_files.h"

namespace {

namespace fx = sh::effects;

// The packet memory one textured quad took.
constexpr std::size_t kQuadPacketBytes = 40;

fx::HandFlame s_flame;

class FlameHost final : public fx::HandFlameHost {
public:
    std::int32_t rand16() override { return sh::loop::game_random().next15(); }
    std::int32_t delta_time() override { return sh::game_state::delta_time(); }

    fx::ScreenPoint project_hand(const fx::Vec3s& local) override {
        auto& bones = sh::game::session().system.characters.player_bones;
        // A matrix record of its own, as the precise path looks its exact camera up by the record.
        sh::camera::Matrix hand = sh::camera::view_matrix(
            sh::camera::view_system().world_screen(),
            sh::camera::frame_world(&bones[sh::game::harry::bone::kRightHand]));
        sh::effects::drawer::set_matrix(projection_.vp(), &hand);
        return project(local);
    }

    fx::ScreenPoint project_flat(const fx::Vec3s& v) override {
        static const sh::camera::Matrix identity{{{4096, 0, 0}, {0, 4096, 0}, {0, 0, 4096}}, {0, 0, 0}};
        sh::effects::drawer::set_matrix(projection_.vp(), &identity);
        return project(v);
    }


    void emit(const sh::draw3d::Polygon& flame, std::int32_t bucket) override {
        sh::draw3d::Polygon p = flame;
        // The flame's grey runs 48 .. 80 with its flicker: a light that follows it (lights.json) flickers with it.
        fx::effect_shown("HandFlame", (static_cast<float>(flame.v[0].color.r) - 48.0f) / 32.0f);
        projection_.place(p, {sh::frame::Layer::World, static_cast<std::uint32_t>(bucket)});
        fx::effect_world::skip_packet(kQuadPacketBytes);
    }

private:
    fx::ScreenPoint project(const fx::Vec3s& v) {
        sh::math::ViewProjection& vp = projection_.vp();
        const sh::math::PreciseVertex r = vp.project({v.x, v.y, v.z});
        return fx::ScreenPoint{r.v.sx, r.v.sy, vp.ordering_depth()};
    }

    sh::effects::drawer::DrawerProjection projection_;
};

}  // namespace

extern "C" {

void GameFs_FlameGfxLoad(void) {
    static fx::effect_world::ImagePlace flame{{0, 12}, 32, 0, 800, 64};
    sh::loader::queue_read_image(sh::loader::disc_files().find("TIM/FLAME.TIM"), sh::memory::work_buffers().data(sh::memory::WorkBuffer::FileStaging), &flame);
}

void WorldGfx_HandEffectDraw(void) {
    FlameHost host;
    s_flame.draw(host);
}
}
