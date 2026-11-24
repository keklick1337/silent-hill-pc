// SPDX-License-Identifier: GPL-3.0-or-later
//
// The on-screen controls of a touch screen as plain data: which controls there are, where each one sits (config.cfg
// `touch_layout`), which of them a finger hits. No SDL, no renderer, no game: touch_controls.cpp runs them.
//
// A place is the centre of a control as fractions of the safe area (the screen without the cutout and the rounded
// corners), and its size as a fraction of the safe area's height, so a control stays round and keeps its size
// relative to the screen on any aspect.
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace sh::input::touch {

// Struggle shows only while the game asks for it (engine/input/touch_prompts.h); Camera is the View action. With
// `touch_movement = dpad` the d-pad and its run toggle stand in for the stick.
enum class Control : int { Stick, Action, Aim, Attack, Map, Inventory, Pause, Struggle, Camera, Dpad, Run, Count };
inline constexpr int kControlCount = static_cast<int>(Control::Count);

constexpr std::uint32_t bit(Control c) { return 1u << static_cast<int>(c); }

// The control's name in `touch_layout` ("stick", "action", ...).
std::string_view control_name(Control control);
std::optional<Control> find_control(std::string_view name);

struct Place {
    float x = 0.0f;     // centre, 0 = the safe area's left edge, 1 = its right edge
    float y = 0.0f;     // centre, 0 = top, 1 = bottom
    float size = 0.0f;  // diameter, a fraction of the safe area's height

    bool operator==(const Place&) const = default;
};

struct Layout {
    std::array<Place, kControlCount> places{};

    const Place& operator[](Control c) const { return places[static_cast<std::size_t>(c)]; }
    Place& operator[](Control c) { return places[static_cast<std::size_t>(c)]; }
    bool operator==(const Layout&) const = default;
};

// The size of a control may change between these multiples of its default size.
inline constexpr float kMinSizeFactor = 0.5f;
inline constexpr float kMaxSizeFactor = 2.0f;

Layout default_layout();

// "stick:.14,.7,.38;action:..." with every control, up to three decimals without the leading zero, so every control
// fits one config line (touchLayout, 240 bytes) even when all of them were moved.
std::string format_layout(const Layout& layout);
// The places a text names; a missing, unknown or broken entry keeps the default (and is reported in `warnings` when
// given). An empty text is the default layout.
Layout parse_layout(std::string_view text, std::vector<std::string>* warnings = nullptr);

// Keeps a control's size within its limits and its centre on the safe area.
Place clamp_place(Control control, Place place);

// The safe area in pixels of the drawing target.
struct Area {
    float x = 0.0f, y = 0.0f, w = 0.0f, h = 0.0f;
};

struct Circle {
    float x = 0.0f, y = 0.0f, r = 0.0f;
};

// Where a control is drawn: `scale` is `touch_scale`, multiplying every size.
Circle circle_of(const Place& place, const Area& area, float scale);

// The control under a point (pixels), among those in `visible` (bits of Control): the nearest centre whose circle,
// grown by `slop` pixels, holds the point. The stick is never hit here: it takes the left part of the screen. The
// d-pad is (a finger lands on it).
std::optional<Control> hit_test(const Layout& layout, const Area& area, float scale, std::uint32_t visible, float px,
                                float py, float slop);

// True when a point belongs to the stick: the left part of the safe area below the top row of buttons.
bool in_stick_zone(const Area& area, float px, float py);

}  // namespace sh::input::touch
