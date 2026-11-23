// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)

#include "input/mouse_pointer.h"

#include "engine/config/pc_config.h"
#include "engine/debug/console_host.h"
#include "engine/game_state/game_state.h"
#include "engine/platform/host_state.h"
#include "input/pad.h"
#include "input/pc_actions.h"
#include "platform/sh_platform.h"
#include "render/gpu/render_queries.h"
#include "render/gpu/render_state.h"
#include "render/overlay/overlay.h"
#include "ui/menu/pc_debug_menu.h"

#include <SDL3/SDL.h>

#include <cmath>

namespace sh::input {
namespace {

namespace gs = sh::game_state;

constexpr int kFixedOne = 4096;  // the puzzles keep their cursor with 12 fractional bits

// A puzzle that stops drawing its cursor is over this many frames later.
constexpr int kPuzzleFrames = 3;

// A request to show the pointer lasts this many presents: on a slow frame the picture is presented again before
// the game has drawn, and a request good for one present made the pointer blink.
constexpr int kShowPresents = 3;

// The game's own arrow, the 32x32 picture the puzzles draw their cursor with. It is part of the interface sheet
// the boot sequence puts into video memory (4-bit pixels, so 8 words a row), with its 16 colours at the start of
// the palette row. Its tip is the top-left corner; the puzzles draw it centred 8 pixels right and down of the tip.
constexpr int kArrowSize = 32;
constexpr int kArrowWordsX = 768, kArrowWordsY = 64;
constexpr int kArrowPaletteX = 192, kArrowPaletteY = 0;
constexpr int kArrowTipInset = 8;

// A 2D screen that took the whole window has its 320-wide space centred in it and stretched by this.
float wide_2d_factor() {
    const auto& shape = sh::render::picture();
    return (!shape.hor_plus && (shape.menu_pillarbox || shape.ui2d_wide_frames > 0)) ? GR_Ui2dWideFactor() : 1.0f;
}

}  // namespace

bool MousePointer::enabled() const {
    if (!g_PcConfig.mouseCursor) {
        return false;
    }
    // A captured mouse has no position, and the console has its own pointer and selection: nothing may reach
    // the menu under it.
    if (g_window != nullptr && SDL_GetWindowRelativeMouseMode(g_window)) {
        return false;
    }
    return Pc_ConsoleIsOpen() == 0;
}

void MousePointer::poll_touchpad() {
    // Every controller with a touchpad counts. A finger already followed keeps the cursor, so a thumb resting on
    // another controller cannot take it in the middle of a drag.
    std::uint32_t finger = 0;
    float x = 0.f;
    float y = 0.f;
    bool button = false;
    int count = 0;
    SDL_JoystickID* ids = SDL_GetGamepads(&count);
    for (int i = 0; ids != nullptr && i < count; ++i) {
        SDL_Gamepad* pad = SDL_GetGamepadFromID(ids[i]);
        if (pad == nullptr || SDL_GetNumGamepadTouchpads(pad) <= 0) {
            continue;
        }
        button = button || SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_TOUCHPAD);
        bool touching = false;
        float fx = 0.f, fy = 0.f, pressure = 0.f;
        if (!SDL_GetGamepadTouchpadFinger(pad, 0, 0, &touching, &fx, &fy, &pressure) || !touching) {
            continue;
        }
        if (finger == 0 || ids[i] == touch_.last_finger()) {
            finger = ids[i];
            x = fx;
            y = fy;
        }
    }
    SDL_free(ids);

    touch_.sample(finger, x, y, gs::screen_width());
    touch_clicked_ = button && !touch_button_;
    touch_button_ = button;
}

void MousePointer::frame_update() {
    left_.pressed = false;
    right_.pressed = false;
    overrode_ = false;

    if (!enabled()) {
        left_.release();
        right_.release();
        wheel_.skip(wheel().total);
        if (puzzle_frames_ > 0) {
            --puzzle_frames_;
        }
        return;
    }

    const WheelState& w = wheel();
    wheel_.sample(w.upFrames, w.downFrames, w.total);

    int mx = 0;
    int my = 0;
    const std::uint32_t buttons = Sh_MouseState(&mx, &my);
    moved_ = mx != window_x_ || my != window_y_;
    window_x_ = mx;
    window_y_ = my;
    left_.sample((buttons & SDL_BUTTON_LMASK) != 0);
    right_.sample((buttons & SDL_BUTTON_RMASK) != 0);

    in_picture_ = sh::gpu::window_to_picture(mx, my, &picture_x_, &picture_y_);
    // The frame is 448 lines on the interlaced menus and 224 in the game, so its size is read every frame.
    at_ = menu_point(picture_x_, picture_y_, wide_2d_factor(), gs::screen_width(), gs::screen_height());

    if (puzzle_frames_ <= 0) {
        owner_ = CursorOwner::Stick;
        touch_.lift();
        return;
    }

    // The cursor itself is placed by puzzle_override, called from inside the puzzle; here: who owns it, and clicks.
    poll_touchpad();
    int stick_x = 0;
    int stick_y = 0;
    if (gs::pad_left_stick(&stick_x, &stick_y)) {
        owner_ = next_cursor_owner(owner_, stick_x, stick_y, gs::pad::kStickThreshold, moved_,
                                   touch_.dx != 0.f || touch_.dy != 0.f);
        // The puzzle adds the stick after the override placed the cursor, and a resting stick's drift would push
        // it off the pointer. The player is held still during a puzzle, so the stick moves nothing else.
        if (owner_ != CursorOwner::Stick) {
            gs::set_pad_left_stick(0, 0);
        }
        std::uint16_t binds[gs::kPadBindCount] = {};
        gs::pad_binds(binds);
        if (left_.pressed || touch_clicked_) {
            gs::pad_press(binds[gs::kPadEnter]);
        }
        if (right_.pressed) {
            gs::pad_press(binds[gs::kPadCancel]);
        }
    }
    --puzzle_frames_;
}

void MousePointer::puzzle_override(int* cursor_x, int* cursor_y) {
    if (!enabled()) {
        return;
    }
    if (owner_ == CursorOwner::Touchpad) {
        // From where the cursor is: the puzzle limited it last frame, so dragging back off an edge moves at once.
        *cursor_x += static_cast<int>(touch_.dx * kFixedOne);
        *cursor_y += static_cast<int>(touch_.dy * kFixedOne);
        return;
    }
    if (owner_ != CursorOwner::Mouse) {
        return;
    }
    // A position outside the picture is passed on as it is: the puzzle's limits hold the cursor at its edge.
    target_x_ = at_.x - static_cast<float>(kMenuCentreX);
    target_y_ = at_.y - static_cast<float>(kMenuCentreY);
    overrode_ = true;
    *cursor_x = static_cast<int>(target_x_ * kFixedOne);
    *cursor_y = static_cast<int>(target_y_ * kFixedOne);
}

void MousePointer::puzzle_cursor_drawn(int x, int y) {
    puzzle_frames_ = kPuzzleFrames;
    if (overrode_) {
        overrode_ = false;
        pull_pointer_to(static_cast<float>(x - kArrowTipInset), static_cast<float>(y - kArrowTipInset));
    }
}

// The puzzle's limits are the game's; only the system pointer is kept inside them. Without this it travels on past
// the edge and has to come all the way back before the cursor leaves it.
void MousePointer::pull_pointer_to(float tip_x, float tip_y) {
    const float off_x = tip_x - target_x_;
    const float off_y = tip_y - target_y_;
    if (std::fabs(off_x) < 1.0f && std::fabs(off_y) < 1.0f) {
        return;
    }
    if (g_window == nullptr || SDL_GetKeyboardFocus() != g_window) {
        return;
    }
    // Window pixels per frame pixel, measured over a long span of the window-to-picture mapping, so the bars and
    // the render size it accounts for need no inverse here.
    constexpr int kSpan = 1024;
    float x0 = 0.f, y0 = 0.f, x1 = 0.f, y1 = 0.f;
    sh::gpu::window_to_picture(window_x_, window_y_, &x0, &y0);
    sh::gpu::window_to_picture(window_x_ + kSpan, window_y_ + kSpan, &x1, &y1);
    if (x1 <= x0 || y1 <= y0) {
        return;
    }
    const int dx = static_cast<int>(off_x / static_cast<float>(gs::screen_width()) * kSpan / (x1 - x0));
    const int dy = static_cast<int>(off_y / static_cast<float>(gs::screen_height()) * kSpan / (y1 - y0));
    SDL_WarpMouseInWindow(g_window, static_cast<float>(window_x_ + dx), static_cast<float>(window_y_ + dy));
}

bool MousePointer::menu_position(int* x, int* y) const {
    if (!enabled() || !in_picture_) {
        return false;
    }
    if (x != nullptr) *x = static_cast<int>(at_.x);
    if (y != nullptr) *y = static_cast<int>(at_.y);
    return true;
}

bool MousePointer::picture_position(float* x, float* y) const {
    if (!enabled() || !in_picture_) {
        return false;
    }
    if (x != nullptr) *x = picture_x_;
    if (y != nullptr) *y = picture_y_;
    return true;
}

bool MousePointer::left_clicked() const { return enabled() && in_picture_ && left_.pressed; }
bool MousePointer::left_held() const { return enabled() && in_picture_ && left_.down; }
bool MousePointer::right_clicked() const { return enabled() && in_picture_ && right_.pressed; }
int MousePointer::wheel_step() const { return enabled() ? wheel_.step : 0; }
int MousePointer::wheel_notches() const { return enabled() ? wheel_.notches : 0; }

std::uint32_t MousePointer::bound_pad_bits() const {
    std::uint32_t bits = PcActions_MouseBits();
    if (bits & gs::pad::kDpadUp) bits |= gs::pad::kStickUp;
    if (bits & gs::pad::kDpadDown) bits |= gs::pad::kStickDown;
    if (bits & gs::pad::kDpadLeft) bits |= gs::pad::kStickLeft;
    if (bits & gs::pad::kDpadRight) bits |= gs::pad::kStickRight;
    return bits;
}

void MousePointer::show() { show_presents_ = kShowPresents; }

void MousePointer::draw() {
    // The debug options panel is drawn over the game and shows the pointer without asking.
    const bool wanted = show_presents_ > 0 || Pc_DebugMenu_IsOpen() != 0;
    if (show_presents_ > 0) {
        --show_presents_;
    }
    // After controller input only the picture hides; hover and clicks stay live, and any use of the mouse brings
    // it back before they could matter.
    const bool pad_in_use = last_device() == LastDevice::Pad;
    auto& overlay = sh::overlay::overlay();
    if (!wanted || !enabled() || !in_picture_ || pad_in_use || !overlay.frame().valid()) {
        return;
    }

    int left = 0, bottom = 0, width = 0, height = 0;
    sh::gpu::picture_viewport(&left, &bottom, &width, &height);
    if (width <= 0 || height <= 0) {
        return;
    }

    if (arrow_texture_ == 0) {
        std::uint16_t words[kArrowSize * kArrowSize / 4];
        std::uint16_t palette[16];
        std::uint8_t rgba[kArrowSize * kArrowSize * 4];
        sh::gpu::read_video_memory(words, kArrowWordsX, kArrowWordsY, kArrowSize / 4, kArrowSize);
        sh::gpu::read_video_memory(palette, kArrowPaletteX, kArrowPaletteY, 16, 1);
        // The sheet reaches video memory during the boot logos, a few frames in.
        if (!decode_indexed_picture(words, palette, kArrowSize, kArrowSize, rgba)) {
            return;
        }
        arrow_texture_ = overlay.textures().create(kArrowSize, kArrowSize, sh::overlay::Format::Rgba8,
                                                   sh::overlay::Filter::Nearest, rgba);
    }

    // Whole pixels at a whole scale: the arrow stays square and evenly sampled in every display mode and the bars
    // never cut it. The viewport's origin is the bottom-left corner, the overlay's the top-left.
    const float size = static_cast<float>(kArrowSize * pointer_scale(height));
    const int x = static_cast<int>(std::floor(static_cast<float>(left) + picture_x_ * static_cast<float>(width)));
    const int y_up = static_cast<int>(
        std::floor(static_cast<float>(bottom) + (1.0f - picture_y_) * static_cast<float>(height)));
    overlay.quad(sh::overlay::Space::Target,
                 {static_cast<float>(x), static_cast<float>(overlay.frame().target_h - y_up), size, size},
                 {1.0f, 1.0f, 1.0f, 1.0f}, arrow_texture_);
}

MousePointer& mouse_pointer() {
    static MousePointer pointer;
    return pointer;
}

}  // namespace sh::input
