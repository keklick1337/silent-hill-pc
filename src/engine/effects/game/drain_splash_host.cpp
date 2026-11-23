// SPDX-License-Identifier: GPL-3.0-or-later
// The drain splash of map5_s00's key ambush (engine/effects/drain_splash.h): its host over the world-screen view,
// the frame's order keys and the packet cursor. The map's Lua binds (map.drain_splash_start / _update) call it.

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string>

#include "engine/app/pc_paths.h"
#include "engine/effects/drain_splash.h"
#include "engine/effects/drawer_projection.h"
#include "engine/effects/game/effect_link.h"
#include "engine/game_state/game_state.h"
#include "engine/log/sh_log.h"
#include "engine/loop/game_random.h"
#include "engine/math/fixed_geometry.h"
#include "engine/world/fixed_trig.h"
#include "render/draw2d/draw2d.h"
#include "render/frame/frame.h"
#include "engine/mods/data_reload.h"

extern "C" const char* PcPort_GetGameDataPath(void);

namespace {

namespace fx = sh::effects;
namespace fg = sh::effects::game_link;

std::optional<fx::DrainSplash> s_splash;
std::uint32_t s_splash_epoch = sh::mods::kNeverRead;

fx::DrainSplash* splash() {
    if (sh::mods::data_stale(s_splash_epoch)) {
        s_splash.reset();
        char mods[1024];
        PcPaths_ExeFile(mods, sizeof(mods), "mods");
        std::string source;
        sh::Result<fx::DrainSplashParams> loaded =
            fx::load_drain_splash_params(PcPort_GetGameDataPath(), mods, &source);
        if (loaded) {
            s_splash.emplace(loaded.value());
            SH_LOG("[FX] drain splash from %s", source.c_str());
        } else {
            SH_ERR("[FX] %s - the drain splash is not drawn", loaded.error().c_str());
        }
    }
    return s_splash ? &*s_splash : nullptr;
}

class SplashHost final : public fx::DrainSplashHost {
public:
    ~SplashHost() override = default;

    std::int32_t rand16() override { return sh::loop::game_random().next15(); }
    std::int32_t sin(std::int32_t a) override { return sh::world::fixed_sin(a); }
    std::int32_t cos(std::int32_t a) override { return sh::world::fixed_cos(a); }
    std::int32_t delta_time() override { return sh::game_state::delta_time(); }

    void begin_view(const fx::Vec3& origin_q8, std::int32_t yaw) override {
        // The world-screen view moved to the splash's origin and turned by its yaw.
        const sh::camera::Matrix& ws = fg::world_screen_matrix();
        sh::math::ViewProjection& vp = projection_.vp();
        sh::effects::drawer::set_matrix(vp, &ws);
        sh::effects::drawer::move_to(vp, &ws, {origin_q8.x, origin_q8.y, origin_q8.z});
        const sh::math::Matrix3 turn =
            sh::math::rot_matrix_zxy_neg(sh::math::Vec3s{0, static_cast<std::int16_t>(yaw), 0});
        vp.multiply_rotation(turn);
    }

    fx::ScreenPoint project_origin() override {
        sh::math::ViewProjection& vp = projection_.vp();
        const sh::math::PreciseVertex r = vp.project({0, 0, 0});
        return fx::ScreenPoint{r.v.sx, r.v.sy, vp.ordering_depth()};
    }

    fx::ScreenPoint project(const fx::Vec3s& v) override {
        const sh::math::PreciseVertex r = projection_.vp().project({v.x, v.y, v.z});
        return fx::ScreenPoint{r.v.sx, r.v.sy, 0};
    }

    void emit_quad(const fx::SplashQuad& q, std::int32_t slot) override {
        step(sizeof(fg::packet::QuadGouraud));
        // The quads' code is a gouraud quad's; its low bits pick the shading.
        const std::uint32_t code = static_cast<std::uint8_t>(splash()->params().prim_code);
        sh::draw3d::Polygon p;
        p.count = 4;
        p.gouraud = true;
        p.shade = {(code & 2) != 0, (code & 1) != 0};
        for (int i = 0; i < 4; i++) {
            const std::uint32_t c = q.color[i];
            p.v[i].xy = {q.x[i], q.y[i]};
            p.v[i].color = {static_cast<std::uint8_t>(c), static_cast<std::uint8_t>(c >> 8),
                            static_cast<std::uint8_t>(c >> 16)};
        }
        projection_.place(p, {sh::frame::Layer::World, static_cast<std::uint32_t>(slot)});
    }

    void emit_mode(std::int32_t slot, std::int32_t tpage, std::int32_t dfe, std::int32_t dtd) override {
        step(sizeof(fg::packet::DrawMode));
        (void)dfe;  // the renderer always draws to the display
        sh::draw2d::DrawMode mode;
        mode.page = sh::draw2d::TexturePage::from_word(static_cast<std::uint16_t>(tpage & 0x9FF));
        mode.dither = dtd != 0;
        sh::draw2d::submit(mode, {sh::frame::Layer::World, static_cast<std::uint32_t>(slot)});
    }

private:
    // The packet memory the original's primitive took: the drawers after the splash write past it.
    void* step(std::size_t size) {
        unsigned char*& cursor = fg::packet_cursor();
        void* at = cursor;
        std::memset(at, 0, size);  // the packet starts blank, as the original cleared it
        cursor += size;
        return at;
    }

    sh::effects::drawer::DrawerProjection projection_;
};

}  // namespace

extern "C" void DrainSplash_Start(const std::int32_t* at) {
    fx::DrainSplash* s = splash();
    if (s == nullptr) {
        return;
    }
    SplashHost host;
    s->start(host, fx::Vec3{at[0], at[1], at[2]});
}

extern "C" void DrainSplash_Update(void) {
    fx::DrainSplash* s = splash();
    if (s == nullptr) {
        return;
    }
    SplashHost host;
    s->update(host);
}
