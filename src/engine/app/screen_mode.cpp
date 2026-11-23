// SPDX-License-Identifier: GPL-3.0-or-later
// The game's display mode (docs/RENDER.md section 3): the 320- or 640-wide progressive or interlaced screen
// the title, the game and the staff roll set, on the game display of the renderer (render/gpu/game_display.h).
// The C entry points keep the game's names; `interlaced` is a boolean the callers pass as one byte or as a 4-byte
// word, so only its low byte is read.

#include <algorithm>
#include <cstdint>

#include "engine/game_state/session.h"
#include "engine/math/geometry_state.h"
#include "engine/platform/host_api.h"
#include "engine/timing/vblank.h"
#include "render/gpu/game_display.h"

namespace {

namespace display = sh::gpu::display;

constexpr std::int32_t kProgressiveHeight = 224;
constexpr std::int32_t kInterlacedHeight = kProgressiveHeight * 2;
constexpr std::int32_t kScreenWidth = 320;
// The screen position option's range (the PC options replaced it; imported saves may still carry a value).
constexpr std::int32_t kRangeX = 11;
constexpr std::int32_t kRangeY = 8;

bool is_set(std::int32_t flag) { return static_cast<std::uint8_t>(flag) != 0; }

// The projection the game's 3D code starts from: the screen centre (160, 120) of the 240-line display (the draw
// offset already carries 160, 112), screen distance 240, and the depth cue of the item close-ups (the world loads
// its own per frame).
void reset_projection() {
    sh::math::geometry().reset();
    sh::math::geometry().set_screen_offset(0, 8);
    sh::math::geometry().set_screen_distance(240);
    sh::math::geometry().set_depth_cue_slope(1024);
    sh::math::geometry().set_depth_cue_base(1988608);
}

}  // namespace

extern "C" {

void Screen_XyPositionSet(std::int32_t x, std::int32_t y) {
    // Only the setting is kept (clamped as the option did): the display itself always stays centred.
    sh::game::Settings& s = sh::game::session().game.settings;
    s.screen_x = static_cast<std::int8_t>(std::clamp(x, -kRangeX, kRangeX));
    s.screen_y = static_cast<std::int8_t>(std::clamp(y, -kRangeY, kRangeY));
}

void Screen_Init(std::int32_t screenWidth, std::int32_t interlaced) {
    const bool on = is_set(interlaced);
    sh::game::GameBlock& game = sh::game::session().game;
    game.screen_width = static_cast<std::uint16_t>(screenWidth);
    game.screen_height = static_cast<std::uint16_t>(on ? kInterlacedHeight : kProgressiveHeight);

    display::sync();
    display::init(game.screen_width, game.screen_height);
    display::set_display_pages(0, 32, 0, on ? 32 : 256);
    reset_projection();
    Screen_XyPositionSet(game.settings.screen_x, game.settings.screen_y);
    display::swap_buffers();
    display::swap_buffers();
}

void Screen_RectInterlacedClear(std::int16_t x, std::int16_t y, std::int16_t w, std::int16_t h, std::uint8_t r,
                                std::uint8_t g, std::uint8_t b) {
    sh::timing::wait_vblank();
    display::clear_image(sh::gpu::Rect{x, y, w, h}, r, g, b);
    display::sync();
}

void Screen_Refresh(std::int32_t screenWidth, std::int32_t interlaced) {
    display::sync();
    Screen_RectInterlacedClear(0, 32, kScreenWidth, kInterlacedHeight, 0, 0, 0);
    Screen_Init(screenWidth, interlaced);
}

}  // extern "C"
