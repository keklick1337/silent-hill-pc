// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// The mouse pointer's arithmetic (src/input/pointer_math.h) and the camera styles' plain half
// (src/input/camera_style.h).

#include "framework/sh_test.h"

#include "input/camera_style.h"
#include "input/pointer_math.h"

#include <array>
#include <vector>

using namespace sh::input;

SH_TEST(pointer_button_edges) {
    ButtonEdge b;
    b.sample(true);
    SH_CHECK(b.down && b.pressed);
    b.sample(true);
    SH_CHECK(b.down && !b.pressed);
    b.sample(false);
    SH_CHECK(!b.down && !b.pressed);
    b.sample(true);
    b.release();
    SH_CHECK(!b.pressed);
    b.sample(true);  // held while the pointer was off: a press when it is back
    SH_CHECK(b.pressed);
}

SH_TEST(pointer_wheel_steps_and_notches) {
    WheelReader w;
    w.sample(0, 0, 0);
    SH_CHECK(w.step == 0 && w.notches == 0);
    w.sample(2, 0, 1);  // a notch up: its latch starts at 2
    SH_CHECK(w.step == 1 && w.notches == 1);
    w.sample(1, 0, 1);  // the latch runs down
    SH_CHECK(w.step == 0 && w.notches == 0);
    w.sample(2, 0, 4);  // three notches in one frame: one step, three notches
    SH_CHECK(w.step == 1 && w.notches == 3);
    w.sample(2, 0, 5);  // a notch every frame keeps the latch at 2: no new step
    SH_CHECK(w.step == 0 && w.notches == 1);
    w.sample(0, 2, 3);
    SH_CHECK(w.step == -1 && w.notches == -2);
    // While the pointer is off the spin is dropped.
    w.skip(40);
    SH_CHECK(w.step == 0 && w.notches == 0);
    w.sample(0, 0, 41);
    SH_CHECK(w.notches == 1);
}

SH_TEST(pointer_menu_point) {
    // The middle of the picture is (160, 112) whatever the frame's height.
    MenuPoint p = menu_point(0.5f, 0.5f, 1.0f, 320, 224);
    SH_CHECK(p.x == 160.0f && p.y == 112.0f);
    p = menu_point(0.5f, 0.5f, 1.0f, 320, 448);
    SH_CHECK(p.x == 160.0f && p.y == 112.0f);
    p = menu_point(0.0f, 0.0f, 1.0f, 320, 224);
    SH_CHECK(p.x == 0.0f && p.y == 0.0f);
    p = menu_point(1.0f, 1.0f, 1.0f, 320, 448);
    SH_CHECK(p.x == 320.0f && p.y == 336.0f);
    // A 2D space widened to the window: the picture's edge is further out than 320 wide.
    p = menu_point(0.0f, 0.5f, 1.5f, 320, 224);
    SH_CHECK(p.x == -80.0f);
    p = menu_point(1.0f, 0.5f, 1.5f, 320, 224);
    SH_CHECK(p.x == 400.0f);
}

SH_TEST(pointer_scale_is_whole) {
    SH_CHECK(pointer_scale(240) == 1);
    SH_CHECK(pointer_scale(720) == 1);
    SH_CHECK(pointer_scale(1080) == 1);
    SH_CHECK(pointer_scale(1440) == 2);
    SH_CHECK(pointer_scale(2160) == 2);
    SH_CHECK(pointer_scale(2400) == 3);
}

SH_TEST(pointer_puzzle_cursor_owner) {
    using O = CursorOwner;
    SH_CHECK(next_cursor_owner(O::Mouse, 64, 0, 64, true, true) == O::Stick);   // the stick wins a frame
    SH_CHECK(next_cursor_owner(O::Mouse, 0, -64, 64, false, false) == O::Stick);
    SH_CHECK(next_cursor_owner(O::Stick, 63, -63, 64, true, false) == O::Mouse);
    SH_CHECK(next_cursor_owner(O::Stick, 0, 0, 64, true, true) == O::Mouse);    // then the mouse
    SH_CHECK(next_cursor_owner(O::Stick, 0, 0, 64, false, true) == O::Touchpad);
    SH_CHECK(next_cursor_owner(O::Mouse, 10, 10, 64, false, false) == O::Mouse);  // nothing moved: stays
    SH_CHECK(next_cursor_owner(O::Touchpad, 0, 0, 64, false, false) == O::Touchpad);
}

SH_TEST(pointer_touch_drag) {
    TouchDrag t;
    t.sample(7, 0.25f, 0.5f, 320);
    SH_CHECK(t.dx == 0.0f && t.dy == 0.0f);  // the first touch only sets the start
    t.sample(7, 0.5f, 0.75f, 320);
    SH_CHECK(t.dx == 80.0f && t.dy == 40.0f);  // the pad is half as tall as wide
    t.sample(9, 0.9f, 0.9f, 320);              // another controller's finger starts over
    SH_CHECK(t.dx == 0.0f && t.dy == 0.0f && t.last_finger() == 9);
    t.sample(0, 0.0f, 0.0f, 320);
    t.sample(9, 0.1f, 0.1f, 320);  // lifted and put down again
    SH_CHECK(t.dx == 0.0f && t.dy == 0.0f);
    t.sample(9, 0.2f, 0.1f, 320);
    SH_CHECK(t.dx > 31.9f && t.dx < 32.1f);
    t.lift();
    t.sample(9, 0.5f, 0.5f, 320);
    SH_CHECK(t.dx == 0.0f && t.last_finger() == 9);
}

SH_TEST(pointer_decodes_the_arrow_picture) {
    // 8x2 pixels: two words a row. Pixel x takes bits 4*(x%4) of word x/4.
    const std::vector<std::uint16_t> words = {0x3210, 0x0000, 0x000F, 0xF000};
    std::array<std::uint16_t, 16> palette{};
    std::array<std::uint8_t, 8 * 2 * 4> rgba{};
    SH_CHECK(!decode_indexed_picture(words, palette, 8, 2, rgba.data()));  // not loaded yet
    palette[1] = 0x001F;   // red
    palette[2] = 0x03E0;   // green
    palette[3] = 0x7C00;   // blue
    palette[15] = 0x7FFF;  // white
    SH_CHECK(decode_indexed_picture(words, palette, 8, 2, rgba.data()));
    const auto px = [&](int x, int y) { return std::array<int, 4>{rgba[(y * 8 + x) * 4], rgba[(y * 8 + x) * 4 + 1],
                                                                 rgba[(y * 8 + x) * 4 + 2], rgba[(y * 8 + x) * 4 + 3]}; };
    SH_CHECK((px(0, 0) == std::array<int, 4>{0, 0, 0, 0}));  // colour 0 is see-through
    SH_CHECK((px(1, 0) == std::array<int, 4>{248, 0, 0, 255}));
    SH_CHECK((px(2, 0) == std::array<int, 4>{0, 248, 0, 255}));
    SH_CHECK((px(3, 0) == std::array<int, 4>{0, 0, 248, 255}));
    SH_CHECK((px(4, 0) == std::array<int, 4>{0, 0, 0, 0}));
    SH_CHECK((px(0, 1) == std::array<int, 4>{248, 248, 248, 255}));
    SH_CHECK((px(7, 1) == std::array<int, 4>{248, 248, 248, 255}));
    SH_CHECK((px(6, 1) == std::array<int, 4>{0, 0, 0, 0}));
}

SH_TEST(camera_style_ids_and_round) {
    SH_CHECK(std::string_view(camera_style_id(CameraStyle::Classic)) == "classic");
    SH_CHECK(std::string_view(camera_style_id(CameraStyle::ThirdPerson)) == "tps");
    SH_CHECK(std::string_view(camera_style_id(CameraStyle::OverShoulder)) == "ots");
    SH_CHECK(std::string_view(camera_style_id(CameraStyle::FirstPerson)) == "fps");
    SH_CHECK(std::string_view(camera_style_id(static_cast<CameraStyle>(9))).empty());
    SH_CHECK(camera_style_from(2) == CameraStyle::OverShoulder);
    SH_CHECK(camera_style_from(4) == CameraStyle::Classic);
    SH_CHECK(camera_style_from(-1) == CameraStyle::Classic);

    using S = CameraStyle;
    SH_CHECK(next_camera_style(S::Classic, 1) == S::ThirdPerson);
    SH_CHECK(next_camera_style(S::ThirdPerson, 1) == S::FirstPerson);
    SH_CHECK(next_camera_style(S::FirstPerson, 1) == S::Classic);
    SH_CHECK(next_camera_style(S::Classic, -1) == S::FirstPerson);
    SH_CHECK(next_camera_style(S::FirstPerson, -1) == S::ThirdPerson);
    // Over the shoulder is off the round and steps as third person does.
    SH_CHECK(next_camera_style(S::OverShoulder, 1) == S::FirstPerson);
    SH_CHECK(next_camera_style(S::OverShoulder, -1) == S::Classic);
    SH_CHECK(next_camera_style(S::Classic, 0) == S::ThirdPerson);

    SH_CHECK(!is_alternate_camera(S::Classic));
    SH_CHECK(is_alternate_camera(S::ThirdPerson) && is_alternate_camera(S::OverShoulder) &&
             is_alternate_camera(S::FirstPerson));
    // The line config.cfg gets as control_styles.
    SH_CHECK(camera_style_list() == "classic:Classic (Default)|tps:Thirdperson Shooter|fps:First Person");
}

SH_TEST(pointer_mouse_button_of_a_bind_number) {
    // The device's button state: bit 0 left, bit 1 middle, bit 2 right, bits 3 and 4 the side buttons. A bind
    // numbers them left, right, middle: Mouse2 (Aim by default) is the right button, Mouse3 the middle one.
    SH_CHECK(mouse_button_bit(1) == 0x01);
    SH_CHECK(mouse_button_bit(2) == 0x04);
    SH_CHECK(mouse_button_bit(3) == 0x02);
    SH_CHECK(mouse_button_bit(4) == 0x08);
    SH_CHECK(mouse_button_bit(5) == 0x10);
    SH_CHECK(mouse_button_bit(0) == 0 && mouse_button_bit(6) == 0 && mouse_button_bit(-1) == 0);
}

SH_TEST(camera_style_mouse_button_of_a_bind) {
    SH_CHECK(mouse_button_of_bind("Mouse1") == 1);
    SH_CHECK(mouse_button_of_bind("mouse3") == 3);
    SH_CHECK(mouse_button_of_bind("MOUSE5") == 5);
    SH_CHECK(mouse_button_of_bind("Mouse6") == 0);
    SH_CHECK(mouse_button_of_bind("Mouse0") == 0);
    SH_CHECK(mouse_button_of_bind("MouseWheelUp") == 0);
    SH_CHECK(mouse_button_of_bind("Mouse") == 0);
    SH_CHECK(mouse_button_of_bind("M") == 0);
    SH_CHECK(mouse_button_of_bind("F9") == 0);
    SH_CHECK(mouse_button_of_bind("") == 0);
}
