// SPDX-License-Identifier: GPL-3.0-or-later
#include "engine/camera/view_system.h"
#include "engine/effects/named_effects_host.h"

#include <cstring>

#include "engine/app/pc_paths.h"
#include "engine/camera/view_space.h"
#include "engine/effects/effect_world.h"
#include "engine/math/view_projection.h"
#include "render/draw2d/draw2d.h"
#include "render/frame/frame.h"
#include "render/game_view.h"

extern "C" const char* PcPort_GetGameDataPath(void);

namespace sh::effects::named {

namespace {

math::Transform to_transform(const camera::Matrix& m) {
    math::Transform t;
    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 3; ++c) t.rot.m[r][c] = m.m[r][c];
    }
    t.trans = {m.t[0], m.t[1], m.t[2]};
    return t;
}

void* frame_of(const void* skeleton, int bone) {
    return render::game_frame_at(const_cast<void*>(skeleton), bone);
}

}  // namespace

std::string gamedata_dir() { return PcPort_GetGameDataPath(); }

std::string mods_dir() {
    char mods[1024];
    PcPaths_ExeFile(mods, sizeof(mods), "mods");
    return mods;
}

EffectView begin_view() { return EffectView(math::game_view_projection(), effect_world::current_frame()); }

void end_view(const EffectView& view) {
    math::end_game_view_projection(view.projection());
    effect_world::leave_frame(view.frame());
}

math::Transform world_screen() { return to_transform(camera::view_system().world_screen().world_screen); }

math::Transform world_screen_at(const Vec3& at) {
    return to_transform(camera::world_screen_at(camera::view_system().world_screen().world_screen, camera::Vec3{at.x, at.y, at.z}));
}

math::Transform bone_world(const void* skeleton, int bone) {
    return to_transform(camera::frame_world(camera::frame_records(), frame_of(skeleton, bone)));
}

math::Vec3i bone_local_translation(const void* skeleton, int bone) {
    const camera::Matrix m = camera::frame_records().local(frame_of(skeleton, bone));
    return {m.t[0], m.t[1], m.t[2]};
}

Vec3 position_at(const void* ref) {
    std::int32_t w[3];
    std::memcpy(w, ref, sizeof(w));
    return Vec3{w[0], w[1], w[2]};
}

math::Vec3s angles_at(const void* ref) {
    std::int16_t a[3];
    std::memcpy(a, ref, sizeof(a));
    return {a[0], a[1], a[2]};
}

std::int32_t timer_at(const void* ref) {
    std::int32_t t;
    std::memcpy(&t, ref, sizeof(t));
    return t;
}

void submit(const draw3d::Polygon& polygon, const EffectView& view, std::int32_t slot) {
    draw3d::submit_projected(polygon, view.depths(), {frame::Layer::World, static_cast<std::uint32_t>(slot)});
}

void submit_mode(std::int32_t slot, std::int32_t tpage, bool dither) {
    // The mode's display bit only matters for interlace, which the renderer does not draw: it is not kept.
    draw2d::DrawMode mode;
    mode.page = draw2d::TexturePage::from_word(static_cast<std::uint16_t>(tpage & 0x9FF));
    mode.dither = dither;
    frame::submit_keyed(frame::Kind::Draw2D, draw2d::frame_commands().record(mode),
                        {frame::Layer::World, static_cast<std::uint32_t>(slot)});
}

}  // namespace sh::effects::named
