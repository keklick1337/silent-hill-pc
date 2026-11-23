// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// The built-in modding tools: the editor mode (F2, console `editor`) with its Dear ImGui UI,
// and the extension API other tool modules build on. A module adds
//   - panels: ImGui windows listed in the editor's Panels menu (register_panel);
//   - overlays: world-space marks drawn over the frozen world while the editor is open (register_overlay), through
//     Draw3D (lines, boxes, circles, labels at world positions);
//   - selection handlers: what a click on the world means (register_selection_handler), in priority order.
// Everything is called on the main thread from the overlay draw of the frame, with an ImGui frame open. Work that
// must reach the game (warps, teleports, file reads) goes through defer(): it runs in the next frame's update,
// before the game's frame, not from the draw hook.
//
// Units: world positions are metres with the game's axes (x right, y down, z forward). q12() / metres() convert to
// the Q12 integers the Lua API takes.

#pragma once

#include <cstdint>
#include <functional>
#include <string>

struct ImVec2;

namespace sh::tools {

struct Vec3 {
    double x = 0, y = 0, z = 0;
};

// Straight RGBA, 0..1.
struct Color {
    float r = 1, g = 1, b = 1, a = 1;
};

// Metres -> Q12 as the engine's readers do it ((s32)(m * 4096.0f)); Q12 -> metres.
std::int32_t q12(double metres);
double metres(std::int32_t q12);

// Draws over the world picture of this frame, in the frozen camera's projection. Points behind the camera are
// clipped; a label is skipped when its point is not in view.
class Draw3D {
public:
    void line(const Vec3& a, const Vec3& b, const Color& color, float width = 1.5f);
    // Axis-aligned box between two corners.
    void box(const Vec3& lo, const Vec3& hi, const Color& color, float width = 1.5f);
    // A circle on the horizontal plane through `centre` (spawn and trigger radii).
    void circle_xz(const Vec3& centre, double radius, const Color& color, int segments = 32, float width = 1.5f);
    // Three short axis lines through `p` (`size` metres each way).
    void cross(const Vec3& p, double size, const Color& color, float width = 1.5f);
    void label(const Vec3& p, const std::string& text, const Color& color);
    // The point on the overlay (ImGui screen pixels); false behind the camera.
    bool project(const Vec3& p, float* x, float* y) const;
};

struct Panel {
    std::string id;        // unique, "<module>.<name>" ("tools.viewer")
    std::string title;     // window title
    std::string menu;      // group in the Panels menu ("View", "World", "Data")
    std::function<void()> draw;  // draws the window body; the host has opened the window with `title`
    bool open = true;      // shown when the editor first opens
    bool auto_size = true; // the window follows its contents (false: the user sizes it; for lists and editors)
};
// False when the id is taken.
bool register_panel(Panel panel);

struct Overlay {
    std::string id;     // unique
    std::string label;  // its checkbox in the Overlays menu
    std::function<void(Draw3D&)> draw;
    bool enabled = false;
};
bool register_overlay(Overlay overlay);

// A click on the world (outside the UI).
struct Pick {
    bool hit = false;            // the ray met drawn geometry
    Vec3 point;                  // metres; meaningful when hit
    Vec3 normal;                 // unit, toward the camera
    Vec3 ray_origin, ray_dir;    // the camera ray in metres (dir is unit)
    int button = 0;              // 0 left, 1 right, 2 middle
};
// true: the handler took the click and the ones after it are not asked. Lower priority runs first; the built-in
// coordinates handler is priority 1000.
using SelectFn = std::function<bool(const Pick&)>;
bool register_selection_handler(std::string id, int priority, SelectFn fn);

// The editor is open (the game is held, the free camera flies).
bool active();
// Runs `fn` at the next frame's update, before the game's frame (warps, teleports, console commands, file reads).
void defer(std::function<void()> fn);
// Runs a console line (as typed in the console) at the next update.
void run_command(const std::string& line);
void copy_to_clipboard(const std::string& text);
// The last world pick of the left button (hit false when none yet).
const Pick& last_pick();
// Where the free camera stands and looks (metres; yaw and pitch in degrees).
Vec3 camera_position();
void camera_angles(double* yaw_deg, double* pitch_deg);
// Moves the free camera (yaw in degrees, level).
void place_camera(const Vec3& position, double yaw_deg, double pitch_deg = 0.0);  // pitch + looks down

}  // namespace sh::tools
