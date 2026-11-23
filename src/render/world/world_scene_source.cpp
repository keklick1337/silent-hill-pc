// SPDX-License-Identifier: GPL-3.0-or-later
//
// What the world scene reads of the running game (world_scene_source.h), from the engine: the view system's
// world-to-screen matrix and the geometry state's projection, the cell stream and the cells the world placed, the map
// type of the world drawing and the resident character models. The ordering table of the frame, its depth scale and
// the lamp glow sprites are reached through their entry point.

#include "render/world/world_scene_source.h"

#include <cstddef>
#include <utility>

#include "engine/camera/camera_host.h"
#include "engine/chara/runtime/chara_models.h"
#include "engine/math/geometry_state.h"
#include "engine/math/precise_capture.h"
#include "engine/runtime/cell_stream_runtime.h"
#include "engine/runtime/world_map.h"
#include "engine/world/world.h"
#include "render/scene/scene.h"
#include "render/gpu/game_display.h"

extern "C" {
// A lamp glow sprite (kind 1 or 2) at a Q12 world point, into the world layer (the effect billboards).
void Gfx_BillboardDraw(std::int32_t kind, std::int32_t x, std::int32_t y, std::int32_t z, void* ot, std::int32_t shift);
// The farthest view depth the world draws at a depth shift (draw_distance.cpp).
std::int32_t Pc_MeshFarLimit(std::int32_t shift);
}

namespace sh::world_scene::source {

namespace {

// A cell is 40 m; the glow points are Q8 inside it.
constexpr std::int32_t kCellSizeQ8 = 40 * 256;

}  // namespace

Camera camera()
{
    const sh::camera::Matrix& ws = sh::camera::camera_host().world_screen();
    sh::math::Transform key;
    for (int r = 0; r < 3; r++)
        for (int k = 0; k < 3; k++)
            key.rot.m[r][k] = ws.m[r][k];
    key.trans = {ws.t[0], ws.t[1], ws.t[2]};
    const sh::math::capture::CameraTwin* twin = sh::math::capture::camera_of(key.rot, key.trans);
    Camera c;
    for (int r = 0; r < 3; r++)
    {
        for (int k = 0; k < 3; k++)
            c.m[r][k] = twin != nullptr ? twin->rot[r * 3 + k] : ws.m[r][k] / 4096.0;
        c.t[r] = twin != nullptr ? twin->trans[r] : static_cast<double>(ws.t[r]);
    }
    const sh::math::Projection screen = sh::math::geometry().projection();
    const sh::math::Vec3i far = sh::math::geometry().far_color();
    c.h = (float)screen.h;
    c.ofs_x = (float)screen.ofx / 65536.0f;
    c.ofs_y = (float)screen.ofy / 65536.0f;
    c.far_color[0] = far.x;
    c.far_color[1] = far.y;
    c.far_color[2] = far.z;
    return c;
}

bool global_library_ready() { return sh::world::stream::library_ready(); }

void drawable_cells(std::vector<Cell>& out)
{
    static std::vector<sh::world::stream::DrawnCell> cells;
    cells.clear();
    sh::world::stream::drawn_cells(cells);
    for (sh::world::stream::DrawnCell& c : cells)
        out.push_back({c.slot, std::move(c.stem), c.cell_x, c.cell_z});
}

std::optional<std::string> resident_cell_stem(int slot) { return sh::world::stream::slot_stem(slot); }

std::string global_library_stem() { return sh::world::stream::library_stem(); }

void draw_cell_glows(int slot, int shift)
{
    const sh::world::CellCollision* cell = sh::world::world().placed(static_cast<std::size_t>(slot));
    if (cell == nullptr)
        return;
    std::int32_t cellX = 0;
    std::int32_t cellZ = 0;
    if (!sh::world::stream::slot_cell(slot, cellX, cellZ))
        return;
    const std::int32_t baseX = cellX * kCellSizeQ8;
    const std::int32_t baseZ = cellZ * kCellSizeQ8;
    for (const sh::world::CellCollision::Glow& g : cell->glows)
    {
        // The stored kind is a byte: only its low half selects the effect.
        const std::int32_t kind = static_cast<std::int8_t>(g.kind);
        if (kind == 0 || kind == 1) {
            Gfx_BillboardDraw(kind + 1, (g.position.x + baseX) * 16, g.position.y * 16, (g.position.z + baseZ) * 16,
                              nullptr, shift);
            sh::scene::current_frame().lamp_glows.push_back(
                {static_cast<float>(g.position.x + baseX), static_cast<float>(g.position.y),
                 static_cast<float>(g.position.z + baseZ), static_cast<float>(kind)});
        }
    }
}

std::uint8_t map_flags() { return sh::world::world_map().flags(); }

bool map_is_town() { return sh::world::world_map().is_town(); }

const void* chara_skeleton(int chara)
{
    return chara > 0 ? sh::chara::chara_models().skeleton(chara) : nullptr;
}


void set_bucket_depth_shift(int shift) { sh::gpu::display::set_view_z_shift(shift); }

std::int32_t world_far_limit(int shift) { return Pc_MeshFarLimit(shift); }

}  // namespace sh::world_scene::source
