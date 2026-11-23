// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// The developer layer over the game's picture: once a frame it runs the console, the key binds, the tools and
// the keys that work in every state (the debug menu key, the exit key), and after the frame is captured it
// draws the panels that sit above the game (the tools, the console and its pointer, the keyframe inspector,
// the message toast). Console output of the game's other modules comes in through console_print.

#pragma once

#include "engine/console/registry.h"

#include <string_view>

namespace sh::debug {

// Once per frame, before the game reads its input.
void overlay_update();
// The renderer's post-capture hook: queues this frame's panels on the engine overlay.
void overlay_draw();

// One line of command output into the console (never a toast).
void console_print(const char* line);
// A feedback line: into the console, and as a toast in the corner while the console is closed.
void console_notice(const char* line);

// One plain line of a console command's output into `out`. The lines the log queued (from any thread) are moved
// into the console first, so the command's line never lands above what was logged before it. A command that runs inside
// a logged script line but writes to the console itself (a Lua exec, a tool's button) has its line logged too.
void print_command_line(sh::console::Output& out, std::string_view line);

}  // namespace sh::debug
