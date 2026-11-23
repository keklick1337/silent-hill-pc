// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// The engine's side of the built-in modding tools: what the frame loop, the input gates and
// the free camera need to know while a tool (the editor of src/features/tools) owns the screen, and the hooks the
// tools module installs at start-up. Header only, so any module reads it without a build dependency; the engine never
// calls into the tools except through these pointers.

#pragma once

namespace sh::devtools {

struct ToolMode {
    // The editor is open: the game is held (no step reaches the simulation), the mouse belongs to the tool UI.
    bool active = false;
    // The free camera may turn with the mouse this frame (the look button is held outside the UI).
    bool mouse_look = false;
    // The UI is taking text or keys: the free camera's keys and the dev keys stay still.
    bool keyboard_captured = false;
    // The UI uses the wheel (a list is scrolled): the free camera's speed stays.
    bool wheel_captured = false;
    // Frames the game still runs while the editor is open: after the editor moved the player, the place's cells
    // load, its room rules apply and the characters are posed there. The tools count it down.
    int run_frames = 0;
    // No map event starts (the event triggers are not checked): while the editor is open, and from an editor warp
    // until the editor is open on the new map, so the map opens without its entry scene and a room stop does not
    // walk into one. The scene starts as usual once the editor is closed.
    bool hold_events = false;

    // Installed by the tools module. update: once per frame from the debug overlay's update, before the game's
    // frame. draw: from the overlay hook, after the panels of the engine and before the pointer and the console.
    void (*update)() = nullptr;
    void (*draw)() = nullptr;
};

inline ToolMode& tool_mode() {
    static ToolMode mode;
    return mode;
}

}  // namespace sh::devtools
