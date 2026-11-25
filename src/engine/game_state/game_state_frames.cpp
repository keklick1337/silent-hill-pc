// SPDX-License-Identifier: GPL-3.0-or-later
//
// The game-state API's frames and images (game_state.h, game_state_app.h): the menus' and HUD's 2D shapes on the
// frame layers, TIM uploads to video memory, the boot screens' own frames, the character files and the work buffers
// of the map data and the demo. Over the engine's frame, draw2d, renderer, file queue and character tables; the
// display calls by the renderer's engine names.

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "engine/app/gameflow/demo_host.h"
#include "engine/app/loader/game_files.h"
#include "engine/chara/game/chara_file_table.h"
#include "engine/chara/runtime/chara_models.h"
#include "engine/entities/bind_pose.h"
#include "engine/entities/character_record.h"
#include "engine/entities/clip_record.h"
#include "engine/game_state/game_state.h"
#include "engine/game_state/game_state_app.h"
#include "engine/game_state/session.h"
#include "engine/platform/window.h"
#include "engine/gameflow/anim_slots.h"
#include "engine/gameflow/demo_player.h"
#include "engine/gameflow/map_event_state.h"
#include "engine/gconfig/sound_tables.h"
#include "engine/timing/vblank.h"
#include "engine/inventory/game/item_stage.h"
#include "engine/inventory/item_view.h"
#include "engine/memory/work_buffers.h"
#include "engine/math/fixed_geometry.h"
#include "engine/math/geometry_state.h"
#include "engine/world/collision_triggers.h"
#include "engine/world/held_item.h"
#include "engine/world/lm_materials.h"
#include "engine/world/skeleton.h"
#include "render/draw2d/draw2d.h"
#include "render/frame/frame.h"
#include "render/gpu/game_display.h"
#include "render/gpu/renderer.h"
#include "engine/loop/frame_view.h"
#include "engine/app/loader/picture_places.h"
#include "engine/game_state/game_state_ui.h"
#include "engine/app/map_info_tables.h"
#include "engine/loader/queue/disc_files.h"


extern "C" {
extern unsigned char* GsOUT_PACKET_P;   // the frame's packet cursor
// Harry's base clips (src/engine/inventory/game/harry_clip_table.cpp).
extern sh::entities::ClipRecord HARRY_BASE_ANIM_INFOS[];
void Screen_Init(std::int32_t screenWidth, bool isInterlaced);
void Anim_BoneUpdate(void* anm_header, sh::entities::BoneCoord* bones, std::int32_t keyframe0, std::int32_t keyframe1,
                     std::int32_t alpha);
void WorldGfx_CharaModelDraw(std::int32_t chara, void* bones, std::int32_t visible, std::int16_t timer,
                             std::int32_t palette);
}

namespace sh::game_state {

namespace {

namespace d2 = sh::draw2d;

// The packets of the order layers (the renderer keys a placed command by its packet's address), as large as the
// shapes' own packets on this build: a flat textured quad, a gouraud quad, a draw-mode word pair, a fill tile.
struct alignas(void*) QuadTexturedPacket { std::uint8_t bytes[sizeof(void*) + 4 + 8 * 4 + 4]; };
struct alignas(void*) TilePacket { std::uint8_t bytes[sizeof(void*) + 4 + 3 * 4]; };
struct alignas(void*) TexturePagePacket { std::uint8_t bytes[sizeof(void*) + 4 + 4]; };

// The hardware's texture page and palette words (the GPU's attribute layout).
d2::TexturePage page_at(int depth, int blend, int x, int y)
{
    const int word = ((depth & 3) << 7) | ((blend & 3) << 5) | ((y & 0x100) >> 4) | ((x & 0x3FF) >> 6) | ((y & 0x200) << 2);
    return d2::TexturePage::from_word(static_cast<std::uint16_t>(word));
}
d2::TexturePage page_numbered(int depth, int blend, int column, int row)
{
    const int word = ((depth & 3) << 7) | ((blend & 3) << 5) | ((row & 1) << 4) | (column & 0xF);
    return d2::TexturePage::from_word(static_cast<std::uint16_t>(word));
}
d2::Palette palette_at(int x, int y) { return d2::Palette::from_word(static_cast<std::uint16_t>((y << 6) | ((x >> 4) & 0x3F))); }

constexpr int kScreenWidth = 320;
constexpr int kScreenHeight = 240;
constexpr std::size_t kPacketBufferBytes = 0x8000;  // the frame packets, one block per display buffer

}  // namespace

void overlay_translucent_quads(const OverlayQuad* quads, int count, std::uint8_t r, std::uint8_t g, std::uint8_t b)
{
    const sh::frame::OrderKey key{sh::frame::Layer::Overlay, 4};

    if (count > kOverlayQuadsMax)
        count = kOverlayQuadsMax;

    for (int i = 0; i < count; i++) {
        const OverlayQuad& q = quads[i];
        d2::Polygon poly;
        poly.count = 4;
        poly.gouraud = true;
        poly.shade.translucent = true;
        for (int k = 0; k < 4; k++) {
            poly.color[k] = {r, g, b};
            poly.xy[k] = {q.x[k], q.y[k]};
        }
        d2::submit(poly, key, sh::math::geometry().depth_history_max());
    }
    // Page 0 (average blend), dithered: placed after the shapes so it draws before them.
    d2::DrawMode mode;
    mode.dither = true;
    d2::submit(mode, key, sh::math::geometry().depth_history_max());
}

void menu_textured_quad(int layer, const TexturedQuad& q)
{
    d2::Polygon poly;
    poly.count = 4;
    poly.textured = true;
    poly.color[0] = {q.r, q.g, q.b};
    for (int i = 0; i < 4; i++) {
        poly.xy[i] = {q.x[i], q.y[i]};
        poly.uv[i] = {q.u[i], q.v[i]};
    }
    poly.page = page_at(0, 0, q.texture_x, 0);
    poly.palette = palette_at(q.clut_x, q.clut_y);
    d2::submit(poly, {sh::frame::Layer::Overlay, static_cast<std::uint32_t>(layer)}, sh::math::geometry().depth_history_max());
    GsOUT_PACKET_P += sizeof(QuadTexturedPacket);
}

bool chara_model_loaded(int chara_id)
{
    const sh::chara::CharaModelRecord* m = sh::chara::chara_models().registered(chara_id);
    return m != nullptr && m->loaded;
}

int chara_anim_slot(int chara_id)
{
    // The slot index alone can be stale (the original never invalidates it; a failed pool load resets it): the slot
    // must hold a live animation header or a spawn renders unposed or invisible.
    const int idx = sh::gameflow::anim_slot_table().chara_slot_of(chara_id);
    const sh::gameflow::AnimSlotTable& table = sh::gameflow::anim_slot_table();
    if (!table.has_slot(idx) || table.slots[static_cast<std::size_t>(idx)].active_header == nullptr)
        return -1;
    return idx;
}

bool harry_anim_range(int idx, int* start, int* end)
{
    const int sk = HARRY_BASE_ANIM_INFOS[idx].start_keyframe;
    const int ek = HARRY_BASE_ANIM_INFOS[idx].end_keyframe;
    if (sk < 0 || ek < sk)
        return false;
    *start = sk;
    *end = ek;
    return true;
}

bool upload_tim(void* data, const ImageHome& home, void (*patch)(void* pixels, int width_words, int height))
{
    // A TIM file: id word (0x10), flags (bit 3: a palette block follows), then each block as its byte length, its
    // rectangle (x, y, w, h halfwords) and its words.
    auto* w = static_cast<std::uint32_t*>(data);
    if (w == nullptr || (w[0] & 0xFF) != 0x10)
        return false;
    const std::uint32_t flags = w[1];
    w += 2;
    const std::int16_t* palette_rect = nullptr;
    const std::uint16_t* palette = nullptr;
    if (flags & 0x8) {
        palette_rect = reinterpret_cast<const std::int16_t*>(&w[1]);
        palette = reinterpret_cast<const std::uint16_t*>(&w[3]);
        w += w[0] >> 2;
    }
    const std::int16_t* pixel_rect = reinterpret_cast<const std::int16_t*>(&w[1]);
    void* pixels = &w[3];

    if (patch != nullptr)
        patch(pixels, pixel_rect[2], pixel_rect[3]);

    if (sh::gpu::Renderer* gpu = sh::gpu::renderer()) {
        const auto x = static_cast<std::int16_t>(home.u + ((home.tpage[1] & 0xF) << 6));
        const auto y = static_cast<std::int16_t>(home.v + ((home.tpage[1] << 4) & 0x100));
        gpu->load_image(sh::gpu::Rect{x, y, pixel_rect[2], pixel_rect[3]}, static_cast<const std::uint16_t*>(pixels));
        if (palette != nullptr) {
            gpu->load_image(sh::gpu::Rect{home.clut_x, home.clut_y, palette_rect[2], palette_rect[3]}, palette);
        }
    }
    sh::gpu::display::sync();
    return true;
}

namespace {

void boot_frame_clear()
{
    sh::game_state::set_frame_buffer_index(sh::gpu::display::active_buffer());
    GsOUT_PACKET_P = sh::memory::work_buffers().as<unsigned char>(
        sh::memory::WorkBuffer::PacketArea,
        static_cast<std::uint32_t>(sh::game_state::frame_buffer_index()) * kPacketBufferBytes);
    sh::gpu::display::clear_layer(sh::frame::Layer::World);
    sh::gpu::display::clear_layer(sh::frame::Layer::Overlay);
}

}  // namespace

void boot_frame_begin()
{
    // The 640x480 interlaced mode with the draw offset at the frame centre, as the Konami logo screen sets it.
    Screen_Init(kScreenWidth * 2, true);
    boot_frame_clear();
}

void boot_frame_load_tim(DiscFile file, const ImageHome& home)
{
    static sh::loader::ImagePlace place;
    place.page[0] = home.tpage[0];
    place.page[1] = home.tpage[1];
    place.u = home.u;
    place.v = home.v;
    place.palette_x = home.clut_x;
    place.palette_y = home.clut_y;
    void* staging = sh::memory::work_buffers().data(sh::memory::WorkBuffer::PlayerBaseAnim);
    sh::loader::queue_read_image(disc_file_index(file), staging, &place);
    while (sh::loader::queue_length() > 0) {
        sh::loader::queue_update();
        sh::timing::wait_vblank();
    }
}

void boot_frame_show()
{
    sh::gpu::display::set_display_enabled(true);
    // Whatever boot stage ran before may have left the interval at 2 (30 fps).
    sh::loop::frame_interval_vblanks() = 1;
}

void boot_frame_quad(const BootQuad& q)
{
    // The last bucket of the overlay layer, like the Konami fade tile.
    const sh::frame::OrderKey key{sh::frame::Layer::Overlay, 0xF};

    d2::Polygon poly;
    poly.count = 4;
    poly.textured = true;
    poly.color[0] = {0x80, 0x80, 0x80};
    poly.xy[0] = {q.x0, q.y0};
    poly.xy[1] = {q.x1, q.y0};
    poly.xy[2] = {q.x0, q.y1};
    poly.xy[3] = {q.x1, q.y1};
    poly.uv[0] = {q.u0, q.v0};
    poly.uv[1] = {q.u1, q.v0};
    poly.uv[2] = {q.u0, q.v1};
    poly.uv[3] = {q.u1, q.v1};
    poly.palette = palette_at(q.clut_x, q.clut_y);
    // The polygon's own page (8-bit): a separate draw-mode page would not reach it.
    poly.page = page_numbered(1, 0, q.page, 0);
    d2::submit(poly, key, sh::math::geometry().depth_history_max());
    GsOUT_PACKET_P += sizeof(QuadTexturedPacket);
}

void boot_frame_fade(int fade)
{
    const sh::frame::OrderKey key{sh::frame::Layer::Overlay, 0xF};
    unsigned char* packet = GsOUT_PACKET_P;

    // Head insertion: the tile goes on first, the subtractive page second, so the mode is set before the tile draws.
    d2::Fill fill;
    fill.translucent = true;
    fill.color = {static_cast<std::uint8_t>(fade), static_cast<std::uint8_t>(fade), static_cast<std::uint8_t>(fade)};
    fill.w = kScreenWidth * 2;
    fill.h = kScreenHeight * 2;
    fill.at = {-kScreenWidth, -kScreenHeight};
    d2::submit(fill, key, sh::math::geometry().depth_history_max());

    d2::DrawMode mode;
    mode.page = page_numbered(0, 2, 0, 0);
    mode.dither = true;
    d2::submit(mode, key, sh::math::geometry().depth_history_max());

    GsOUT_PACKET_P = packet + sizeof(TilePacket) + sizeof(TexturePagePacket);
}

void boot_frame_present(bool next)
{
    sh::timing::wait_vblank();
    sh::gpu::display::swap_buffers();
    sh::gpu::display::draw_layer(sh::frame::Layer::Overlay);
    sh::platform::end_frame();
    if (next)
        boot_frame_clear();
}

// ---- character files and the item in hand ----

int chara_anim_file(int chara_id) { return sh::chara::game::chara_file_row(chara_id).anim; }

int held_item_id() { return sh::world::held_item().item(); }
bool held_item_hidden() { return sh::world::held_item().hidden(); }

// ---- the work buffers of the map data and the demo ----

void attach_work_buffers()
{
    sh::game_state::set_map_overlay_buffer(
        sh::memory::work_buffers().as<std::uint8_t>(sh::memory::WorkBuffer::MapOverlayData));
    sh::gameflow::demo_player().set_play_buffer(
        sh::memory::work_buffers().as<std::uint8_t>(sh::memory::WorkBuffer::DemoPlayBuffer));
}

}  // namespace sh::game_state
