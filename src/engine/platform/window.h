// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The game window and the process around it: SDL start-up and shutdown, the window mode, vsync, pointer
// confinement, the event pump and screenshots. The renderer creates the window with its device (sh::gpu::start);
// this module owns the order everything starts and stops in and what a window-mode change implies.

#include <cstdint>

namespace sh::platform {

// Numbered as the config's `fullscreen` key.
enum class WindowMode : std::uint8_t {
    Windowed = 0,
    Exclusive = 1,   // exclusive fullscreen at the chosen resolution
    Borderless = 2,  // desktop-sized window, the scene rendered at the chosen size
};

constexpr WindowMode window_mode_from_config(int value) {
    return value == 1 ? WindowMode::Exclusive : value == 2 ? WindowMode::Borderless : WindowMode::Windowed;
}

// Brings up SDL, the window with the renderer, and the vertical blank clock. On failure everything started is torn
// down again (sh::gpu::renderer() stays null). Shutdown is registered with atexit.
void start(const char* title, int width, int height, WindowMode mode);
// Tears everything down; safe to call more than once.
void shutdown();
// The normal way out (window close, Alt+F4, quit during a movie). Does not return.
[[noreturn]] void exit_game();

// Applies a window mode and resolution at runtime.
void apply_window_mode(int width, int height, WindowMode mode);
// Keep the pointer inside a fullscreen window while it has focus (config confine_cursor).
void set_mouse_confinement(bool on);
// Re-evaluates the confinement; call after anything that changes the window mode, focus or relative mouse mode.
void update_mouse_confinement();
// The window size in screen units.
void window_size(int* width, int* height);
// Resizes the window the way a window manager does (a drag, maximise, tiling): the mode stays, and the size
// reaches the resize hook as the player's own resize does.
void resize_window(int width, int height);
// The desktop mode of the display the window is on, in screen units; 0, 0 when unknown.
void display_size(int* width, int* height);
// Called when a windowed (not fullscreen) window changed size other than through apply_window_mode: on every
// size while it is dragged (`settled` false) and once when the size stayed (`settled` true: the moment to save).
using WindowResizedHook = void (*)(int width, int height, bool settled);
void set_window_resized_hook(WindowResizedHook hook);

// Vsync through the per-frame swap interval, so the choice persists across frames.
void set_vsync(bool on);

// Drawn after the frame is captured (the dev console), so it is never baked into the frozen image of a pause
// screen; nullptr removes it.
void set_post_capture_hook(void (*hook)());

// Drains the event queue: pad hot-plug, window changes, mouse wheel, text input and the game's debug key hook.
void pump_events();

// Sees every event before the game does (the modding tools' UI). `event` is an SDL_Event;
// returning true keeps a keyboard, text or mouse event from the game. Window and quit events always reach it too.
using EventFilter = bool (*)(const void* event);
void set_event_filter(EventFilter filter);

// Opens the game's frame: pumps the window events, picks the swap interval and starts the renderer's frame (the
// clear, the frozen frame while paused). False when a frame is already open.
bool begin_frame();
// Closes it: decays the per-frame input latches, ends the renderer's frame (feedback store, freeze capture,
// overlays, present), then runs the console command file's per-present step.
void end_frame();

// Writes the frame last presented to SCREENSHOT.BMP in the working directory.
void save_screenshot();

// Writes the renderer's 1024x512 video memory to `path` as a 24-bit TGA (console `vramdump`). False when there is
// no renderer or the file cannot be written.
bool save_vram_tga(const char* path);

}  // namespace sh::platform
