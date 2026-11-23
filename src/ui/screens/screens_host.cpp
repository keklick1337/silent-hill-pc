// SPDX-License-Identifier: GPL-3.0-or-later
//
// The screens' 2D placement and the full-screen pictures (engine/screens/screens_view.h, background.h):
// every 2D command of the menus, the puzzle cursor, the background pictures and the loading screen's trail is placed
// by an order key in the lists being built (render/frame). The game's packet area hands out the identity of
// each command (the depth records are keyed by it), so the cursor moves by the packet the game's drawer built.

#include <cstddef>
#include <cstdint>
#include <variant>
#include <vector>

#include "engine/app/loader/game_files.h"
#include "engine/game_state/session.h"
#include "engine/memory/work_buffers.h"
#include "engine/loop/frame_view.h"
#include "engine/screens/background.h"
#include "engine/screens/screens_view.h"
#include "engine/timing/vblank.h"
#include "input/mouse_pointer.h"
#include "render/draw2d/draw2d.h"
#include "render/frame/frame.h"
#include "render/gpu/render_state.h"
#include "render/gpu/game_display.h"
#include "engine/app/loader/picture_places.h"
#include "engine/math/geometry_state.h"
#include "engine/loader/queue/disc_files.h"


extern "C" {
extern unsigned char* GsOUT_PACKET_P;  // the game's packet cursor
// Frames a full-screen 2D picture keeps the black clear colour (no fog colour behind it); the main loop counts it down.
std::int32_t g_Pc2dBackgroundActive = 0;

// The menus' pictures (src/engine/app/loader/picture_places.cpp).
void Screen_BackgroundImgDraw(sh::loader::ImagePlace* image);
}

namespace sh::screens {
namespace {

constexpr std::uint32_t kWorldBackSlot = 2047;  // the back of the world list: drawn first
constexpr std::int32_t kInterlacedHeight = 448;

// The game's packets are the retail ones behind a native-pointer tag.
constexpr std::size_t pc_packet(std::size_t retail_bytes) { return sizeof(void*) + retail_bytes; }

frame::OrderKey key_of(Table2d table, int entry) {
    switch (table) {
        case Table2d::Menu:
        case Table2d::Hud: return {frame::Layer::Overlay, static_cast<std::uint32_t>(entry)};
        case Table2d::World: return {frame::Layer::World, static_cast<std::uint32_t>(entry)};
        case Table2d::NextBack: break;
    }
    return {frame::Layer::World, kWorldBackSlot + static_cast<std::uint32_t>(entry)};
}

void place(frame::OrderKey key, std::size_t packet_bytes, const draw2d::Command& command) {
    sh::draw2d::submit(command, key, sh::math::geometry().depth_history_max());
    GsOUT_PACKET_P += packet_bytes;
}

// The packet the game's drawer built for a command.
std::size_t packet_bytes(const draw2d::Command& command) {
    if (const auto* p = std::get_if<draw2d::Polygon>(&command)) {
        if (p->textured) return packet_size(p->count == 3 ? Packet2d::PolyFT3 : Packet2d::PolyFT4);
        if (p->gouraud) return packet_size(p->count == 3 ? Packet2d::PolyG3 : Packet2d::PolyG4);
        return packet_size(p->count == 3 ? Packet2d::PolyF3 : Packet2d::PolyF4);
    }
    if (const auto* l = std::get_if<draw2d::Line>(&command))
        return packet_size(l->gouraud ? Packet2d::LineG2 : Packet2d::LineF2);
    if (std::holds_alternative<draw2d::DrawMode>(command)) return packet_size(Packet2d::DrawMode);
    if (std::holds_alternative<draw2d::Sprite>(command)) return packet_size(Packet2d::Sprite);
    return packet_size(Packet2d::Tile);
}

constexpr std::size_t kLineF3Bytes = pc_packet(24);

BackgroundImage picture(const loader::ImagePlace& image) {
    return {image.page[0], image.page[1], image.u, image.v, image.palette_x, image.palette_y};
}

// The picture tiles and the trail go to the back of the world list.
void submit_back(const std::vector<draw2d::Command>& commands) {
    for (const draw2d::Command& c : commands) {
        place(key_of(Table2d::NextBack, 0), packet_size(std::holds_alternative<draw2d::Sprite>(c) ? Packet2d::Sprite
                                                                                                 : Packet2d::DrawMode),
              c);
    }
}

void picture_shown() { game::session().system.process.music_status |= game::music_status::kPause; }

// 127 takes one 5-bit level off the stored frame, which the original does once every `vblanks` vblanks of 60 Hz
// time. The decision is made once per drawn frame from the emulated vblanks elapsed since the last decay, so the fade
// runs at the console's pace whatever the host frame rate and the loading screen's pacing (the vblank parity of a
// frame, which the original tests, drifts with both: steps of 1 and 2 vblanks decayed on every step).
class FrameDecay final : public BlurDecay {
public:
    explicit FrameDecay(std::int32_t vblanks) {
        static int s_last = 0;
        static bool s_started = false;
        const int now = sh::timing::vblank_count();
        const int period = vblanks > 0 ? vblanks : 1;
        if (!s_started || now < s_last || now - s_last >= period) {
            decay_ = true;
            // Keep the 60 Hz grid: a late frame does not push the next decay later.
            s_last = s_started && now >= s_last && now - s_last < 2 * period ? s_last + period : now;
            s_started = true;
        }
    }
    bool decay_now() override { return decay_; }

private:
    bool decay_ = false;
};

// load_screen_trail = smooth (VISUAL DEVIATION, every preset but original): the copy keeps kSmoothKeep of the stored
// frame per 60 Hz vblank elapsed since the last copy, in full precision, so Harry's trail is a short smear that is
// gone in about 0.1 s instead of one copy per 5-bit level that dark pixels hold for a third of a second.
class SmoothDecay final : public BlurDecay {
public:
    SmoothDecay() {
        static int s_last = 0;
        static bool s_started = false;
        const int now = sh::timing::vblank_count();
        int elapsed = s_started && now >= s_last ? now - s_last : 2;
        if (elapsed > 8) elapsed = 8;
        s_last = now;
        s_started = true;
        float keep = 1.0f;
        for (int i = 0; i < elapsed; ++i) keep *= kSmoothKeep;
        level_ = static_cast<std::uint8_t>(keep * 128.0f + 0.5f);
    }
    bool decay_now() override { return level_ < 128; }
    std::uint8_t level() override { return level_; }

private:
    static constexpr float kSmoothKeep = 0.65f;
    std::uint8_t level_ = 128;
};

}  // namespace

void draw_puzzle_cursor(const PuzzleCursor& cursor) {
    sh::input::mouse_pointer().puzzle_cursor_drawn(cursor.x, static_cast<std::int16_t>(cursor.y));
    place(key_of(Table2d::World, 0), packet_size(Packet2d::PolyFT4), puzzle_cursor_quad(cursor));
}

void draw_outline_box(std::int16_t t, OutlineBox from, OutlineBox to) {
    for (const draw2d::Line& line : outline_box_lines(t, from, to)) place(key_of(Table2d::World, 0), kLineF3Bytes, line);
}

FadeControl& fade_control() {
    static FadeControl control;
    return control;
}

std::int32_t fade_status() { return fade_control().status; }
void set_fade_status(std::int32_t status) { fade_control().status = status; }

void submit_2d(Table2d table, int entry, const draw2d::Command& command) {
    place(key_of(table, entry), packet_bytes(command), command);
}

std::size_t packet_size(Packet2d packet) {
    switch (packet) {
        case Packet2d::PolyF3: return pc_packet(20);
        case Packet2d::PolyF4: return pc_packet(24);
        case Packet2d::PolyG3: return pc_packet(28);
        case Packet2d::PolyG4: return pc_packet(36);
        case Packet2d::PolyFT3: return pc_packet(32);
        case Packet2d::PolyFT4: return pc_packet(40);
        case Packet2d::LineF2: return pc_packet(16);
        case Packet2d::LineG2: return pc_packet(20);
        case Packet2d::LineF4: return pc_packet(28);
        case Packet2d::Tile: return pc_packet(16);
        case Packet2d::Sprite: return pc_packet(20);
        case Packet2d::Sprite8: return pc_packet(16);
        case Packet2d::Sprite16: return pc_packet(16);
        case Packet2d::DrawMode: return pc_packet(8);
    }
    return 0;
}

void* packet_cursor() { return GsOUT_PACKET_P; }
void set_packet_cursor(void* cursor) { GsOUT_PACKET_P = static_cast<unsigned char*>(cursor); }

void link_2d(Table2d table, int entry, void* packet, const draw2d::Command& command) {
    sh::draw2d::submit(command, key_of(table, entry), sh::math::geometry().depth_history_max());
}

void submit_2d_packet(Table2d table, int entry, const draw2d::Command& command, Packet2d packet) {
    place(key_of(table, entry), packet_size(packet), command);
}

void link_scene_2d(Table2d table, int entry, std::uint32_t item) {
    frame::submit_keyed(frame::Kind::Scene, item, key_of(table, entry));
}

void draw_2d_now(const draw2d::Command& command) { sh::gpu::display::draw_now(command); }

namespace {

loader::ImagePlace* place_of(ScreenImage image) {
    switch (image) {
        case ScreenImage::BrightnessFromTitle: return &sh::loader::picture_places().brightness_from_title;
        case ScreenImage::BrightnessFromGame: return &sh::loader::picture_places().brightness_from_game;
        case ScreenImage::ControllerButtons: return &sh::loader::picture_places().controller_buttons;
        case ScreenImage::CardMessage: return &sh::loader::picture_places().card_message;
        case ScreenImage::ItemInspection: break;
    }
    return &sh::loader::picture_places().item_inspection;
}

}  // namespace

void queue_screen_image(const char* path, ScreenImage image) {
    loader::queue_read_image(sh::loader::disc_files().find(path), sh::memory::work_buffers().data(sh::memory::WorkBuffer::ScreenImage), place_of(image));
}
void wait_for_file_queue() { loader::queue_wait_empty(); }
void draw_screen_image(ScreenImage image) { Screen_BackgroundImgDraw(place_of(image)); }

void set_frame_interval(int vblanks) { loop::frame_interval_vblanks() = vblanks; }
void set_fade_timestep(std::int32_t step) { fade_control().timestep = step; }
void wait_vblanks(int count) { sh::timing::wait_vblanks(count); }

}  // namespace sh::screens

// The game's picture entry points (the flow code and the map scripts call them by these names).
extern "C" {

void Screen_BackgroundImgDraw(sh::loader::ImagePlace* image) {
    using namespace sh::screens;
    const sh::game::GameBlock& g = sh::game::session().game;
    g_Pc2dBackgroundActive = 2;
    submit_back(background_tiles(picture(*image), g.screen_width, g.screen_height, fade_control().background_gamma));
    picture_shown();
    fade_control().background_gamma = 128;
}

void Screen_BackgroundImgTransition(sh::loader::ImagePlace* image0, sh::loader::ImagePlace* image1,
                                    std::int16_t alpha) {
    using namespace sh::screens;
    // Keeps the black clear through the whole fade (about a second of 1/32 steps).
    g_Pc2dBackgroundActive = 2;
    for (const sh::draw2d::Polygon& poly : background_transition(picture(*image0), picture(*image1), alpha))
        place(key_of(Table2d::World, 0), packet_size(Packet2d::PolyFT4), poly);
    picture_shown();
}

void Screen_BackgroundImgDrawAlt(sh::loader::ImagePlace* image) {
    using namespace sh::screens;
    g_Pc2dBackgroundActive = 2;
    for (const sh::draw2d::Polygon& poly : background_columns(picture(*image), fade_control().background_gamma))
        place(key_of(Table2d::World, 2), packet_size(Packet2d::PolyFT4), poly);
    picture_shown();
    fade_control().background_gamma = 128;
}

bool Screen_BackgroundMotionBlur(std::int32_t vBlanks) {
    using namespace sh::screens;
    const sh::game::GameBlock& g = sh::game::session().game;
    // Arms the frame store before the first sampling sprite is drawn, so the trail has a frame at once.
    const int trail = sh::render::graphics().load_screen_trail;
    if (trail == 0) {
        return false;  // load_screen_trail = off: no copy of the last frame, no trail
    }
    sh::render::feedback_control().store_allowed = 2;
    const bool interlaced = g.screen_height == kInterlacedHeight;
    if (trail == 2) {
        sh::render::feedback_control().smooth_trail = 1;
        SmoothDecay decay;
        submit_back(motion_blur_tiles(g.screen_width, interlaced, sh::frame::build_buffer(), decay));
    } else {
        FrameDecay decay(vBlanks);
        submit_back(motion_blur_tiles(g.screen_width, interlaced, sh::frame::build_buffer(), decay));
    }
    return false;
}

}  // extern "C"
