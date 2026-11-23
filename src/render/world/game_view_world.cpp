// SPDX-License-Identifier: GPL-3.0-or-later
//
// What the renderer reads of the running game (render/game_view.h) from the engine's own state: the disc file
// table, the view system's world-to-screen matrix, the projection of the transform unit, the atmosphere's light of
// lit models, the map light's flashlight and the character world's solid ray.

#include <cstdio>
#include <cstring>

#include "engine/camera/camera_host.h"
#include "engine/camera/view_space.h"
#include "engine/effects/map_lighting.h"
#include "engine/loader/queue/disc_files.h"
#include "engine/math/geometry_state.h"
#include "engine/world/game/character_world.h"
#include "engine/world/world_atmosphere.h"
#include "render/game_view.h"

namespace sh::render {

namespace {

static_assert(sizeof(sh::camera::Matrix) == sizeof(Transform), "view matrices are read as records::Transform");

}  // namespace

bool disc_file_loose_path(int file_index, char* out, std::size_t size) {
    const sh::loader::DiscFiles& files = sh::loader::disc_files();
    if (file_index < 0 || file_index >= files.count()) {
        return false;
    }
    return std::snprintf(out, size, "%s", files.path(file_index).c_str()) < static_cast<int>(size);
}

std::int32_t disc_file_sector_size(int file_index) {
    return static_cast<std::int32_t>(sh::loader::disc_files().sector_bytes(file_index));
}

void world_screen_at(Transform& out, std::int32_t x, std::int32_t y, std::int32_t z) {
    const sh::camera::Matrix m =
        sh::camera::world_screen_at(sh::camera::camera_host().world_screen(), sh::camera::Vec3{x, y, z});
    std::memcpy(&out, &m, sizeof out);
}

void projection_matrix_set(const Transform& m) {
    sh::math::geometry().load_rotation(sh::math::transform_of(m));
    sh::math::geometry().load_translation(sh::math::transform_of(m));
}

bool flashlight_on() { return sh::effects::map_lighting().flashlight_on(); }

}  // namespace sh::render
