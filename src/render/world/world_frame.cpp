// The world part of a game frame (docs/RENDER.md section 6): the window of map cells kept resident around
// the player and the view, and the frame's world draw (objects, the cell window, cells, decals, 2D effects).

#include <cstdint>

#include "engine/app/game_view.h"
#include "engine/app/loader/game_files.h"
#include "engine/camera/view_system.h"
#include "engine/config/pc_config.h"
#include "engine/game_state/session.h"
#include "engine/memory/work_buffers.h"
#include "engine/loader/queue/disc_files.h"
#include "engine/log/sh_log.h"
#include "engine/runtime/cell_stream_runtime.h"
#include "engine/world/fixed_trig.h"
#include "engine/world/object_queue.h"
#include "engine/world/world_atmosphere.h"
#include "render/scene/scene.h"
#include "render/world/world_objects_draw.h"
#include "render/world/world_scene.h"
#include "render/world/world_scene_source.h"
#include "engine/world/game/atmosphere_host.h"

extern "C" {
// The map set-up steps: the collision queries, the flashlight's attributes and the lens-flare
// sprite table.
void Collision_Init(void);
void Game_FlashlightAttributesFix(void);
void LensFlare_SpriteTableInit(void* view_frame);
// The geometry offset the world is projected with this frame (main_loop.cpp).
std::int32_t Pc_WorldAnchorOfy(void);
std::int32_t Vc_VectorMagnitudeCalc(std::int32_t x, std::int32_t y, std::int32_t z);
void Pc_DecalsDraw(void* ot);
void Gfx_2dEffectsDraw(void);
// The debug collision view (src/engine/debug/collision_view.cpp); nothing when it is off.
void Pc_CollisionViewDraw(void);
}

namespace {

namespace cam = sh::camera;
using sh::world::fixed_cos;
using sh::world::fixed_sin;

constexpr std::int32_t q12(std::int32_t whole) { return whole * 4096; }
constexpr std::uint8_t kOneActiveChunk = 1u << 0;
constexpr std::uint8_t kTwoActiveChunks = 1u << 1;
constexpr std::uint8_t kInterior = 1u << 2;

// Products of the game's word arithmetic: 32-bit, wrapping.
std::int32_t mul(std::int32_t a, std::int32_t b) {
    return static_cast<std::int32_t>(static_cast<std::uint32_t>(a) * static_cast<std::uint32_t>(b));
}
std::int32_t mul_q12(std::int32_t a, std::int32_t b) { return mul(a, b) >> 12; }
std::int32_t mul_q12_wide(std::int32_t a, std::int32_t b) {
    return static_cast<std::int32_t>((static_cast<std::int64_t>(a) * b) >> 12);
}
std::int32_t clamp(std::int32_t v, std::int32_t lo, std::int32_t hi) { return v < lo ? lo : (v > hi ? hi : v); }

struct Point {
    std::int32_t x, z;
};

}  // namespace

// The cell window: one sample point ahead of the player by its speed, one where the view looks (with fog and the
// original culling; else just ahead of the player). A small interior room keeps both inside the player's 2.5 m cell.
extern "C" void Ipd_CloseRangeChunksInit(void) {
    const sh::entities::CharacterRecord& p = sh::game::session().system.characters.player;
    const std::uint8_t flags = sh::world_scene::source::map_flags();
    const bool small_room = (flags & kInterior) && (flags & (kOneActiveChunk | kTwoActiveChunks));

    Point near{p.position.x, p.position.z};
    const std::int32_t lead = clamp(mul(p.move_speed, 22528) / 16015, 0, 22528);  // up to 5.5 m
    near.x += mul_q12_wide(lead, fixed_sin(p.heading));
    near.z += mul_q12_wide(lead, fixed_cos(p.heading));
    // The town's north-west gate: its cells are kept by the near point at the gate's edge.
    if (sh::world_scene::source::map_is_town() && p.position.x >= q12(-40) && p.position.x <= q12(40) &&
        p.position.z >= q12(200) && p.position.z <= q12(240))
        near.z = q12(200);

    Point view;
    if (sh::world::atmosphere().state().fog_enabled && g_PcConfig.culling == 0) {
        const cam::Vec3 eye = cam::view_system().position();
        const cam::PackedAngles& rot = cam::view_system().angles();
        view = {eye.x, eye.z};
        const std::int32_t reach = small_room ? 0 : mul_q12(fixed_cos(rot.x), q12(9));
        view.x += clamp(mul_q12(reach, fixed_sin(rot.y)), q12(-6), q12(6));
        view.z += clamp(mul_q12(reach, fixed_cos(rot.y)), q12(-6), q12(6));
        if (Vc_VectorMagnitudeCalc(view.x - p.position.x, 0, view.z - p.position.z) > q12(16)) {
            view.x = p.position.x + mul_q12(fixed_sin(rot.y), q12(14));
            view.z = p.position.z + mul_q12(fixed_cos(rot.y), q12(14));
        }
    } else {
        view = {p.position.x + fixed_sin(p.rotation.y), p.position.z + fixed_cos(p.rotation.y)};
    }

    if (small_room) {
        constexpr std::int32_t kRoomCell = 10240;  // 2.5 m
        std::int32_t cx = p.position.x / kRoomCell;
        if (p.position.x < 0) cx--;
        std::int32_t cz = p.position.z / kRoomCell;
        if (p.position.z < 0) cz--;
        const std::int32_t x0 = cx * kRoomCell;
        const std::int32_t z0 = cz * kRoomCell;
        near.x = clamp(near.x, x0 + 1, x0 + (kRoomCell - 1));
        near.z = clamp(near.z, z0 + 1, z0 + (kRoomCell - 1));
        view.x = clamp(view.x, x0 + 1, x0 + (kRoomCell - 1));
        view.z = clamp(view.z, z0 + 1, z0 + (kRoomCell - 1));
    }

    sh::world::stream::update(near.x, near.z, view.x, view.z);
}

// Leaving a map's world: a small interior room only resets its window; anything else drops every resident cell
// and the textures they hold.
extern "C" void Map_WorldClear(void) {
    const std::uint8_t flags = sh::world_scene::source::map_flags();
    if ((flags & kInterior) && (flags & (kOneActiveChunk | kTwoActiveChunks))) {
        SH_DBG("[TRANSITION] Map_WorldClear: small interior room, window reset (flags=0x%02X)", flags);
        sh::world::stream::reset();
        return;
    }
    SH_DBG("[TRANSITION] Map_WorldClear: full clear (flags=0x%02X)", flags);
    sh::world::stream::clear_slots();
    sh::world::stream::clear_unreferenced_textures();
}

// A map's world before its first frame: atmosphere, cell streaming, collision,
// full light, the flashlight, lens-flare sprites on the view, an empty object queue.
extern "C" void WorldGfx_MapInit(void) {
    sh::scene::begin_map();
    sh::world::world_env_init();
    sh::world::stream::map_init();
    Collision_Init();
    sh::game::session().system.light_intensity = 4096;
    Game_FlashlightAttributesFix();
    LensFlare_SpriteTableInit(cam::view_system().frame());
    sh::world::object_queue().clear();
}

// The world's effect sheet (snow, rain, particles): BG_ETC.TIM to its fixed page, palette at (192, 0).
extern "C" void GameFs_BgEtcGfxLoad(void) {
    static const sh::loader::ImagePlace kPlace{{0, 12}, 0, 0, 192, 0};
    sh::loader::queue_read_image(sh::loader::disc_files().find("TIM/BG_ETC.TIM"), sh::memory::work_buffers().data(sh::memory::WorkBuffer::FileStaging), &kPlace);
}

extern "C" bool Ipd_ChunkInitCheck(void) {
    Ipd_CloseRangeChunksInit();
    return sh::world::stream::window_loaded();
}

// The world of the frame: the geometry offset is set where the world is projected (a later set would land one frame
// late), the queued objects drawn, the cell window loaded completely this frame, then the cells, the decals on them
// and the 2D effects.
extern "C" void Gfx_InGameDraw(bool shift) {
    sh::app::view::set_projection_offset(0, Pc_WorldAnchorOfy());
    sh::world_scene::draw_objects();

    // Reads are queued by the window step and reformatted by the next: two queue-and-drain rounds and a last step
    // leave the window loaded and drawable now (a cell crossing would otherwise draw frames without its room).
    for (int pass = 0; pass < 2; pass++) {
        Ipd_CloseRangeChunksInit();
        int limit = 500;
        while (sh::loader::queue_length() > 0 && --limit > 0) sh::loader::queue_update();
    }
    Ipd_CloseRangeChunksInit();

    sh::world_scene::draw_world(shift);
    Pc_DecalsDraw(nullptr);
    Pc_CollisionViewDraw();
    Gfx_2dEffectsDraw();
}
