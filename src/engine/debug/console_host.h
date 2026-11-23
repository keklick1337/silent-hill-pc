// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// The exe-side host of the engine console (src/engine/console): owns the Console over the process-wide
// command registry, pumps keyboard/mouse input into it, keeps the game-freeze state (g_PcConsoleInputActive,
// the swallow flag, the post-Enter apply window) and draws it through console_render (the engine overlay). The debug
// overlay (overlay/debug_overlay.cpp) calls ConsoleHost_Update / ConsoleHost_Render once per frame.

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/* Per frame, before the game's input parse: toggle key, input pump, slide animation, log drain. */
void ConsoleHost_Update(void);
/* Post-capture hook: queues the panel on the engine overlay (render/overlay/overlay.h). */
void ConsoleHost_Render(void);
/* Drains journal lines queued by other threads into the scrollback. onToast is called for echo lines. */
typedef void (*ConsoleHostToastFn)(const char* text);
void ConsoleHost_DrainLog(ConsoleHostToastFn onToast);
/* Adds one line (level: ShLogLevel value; 0 = plain command output); while a command runs it is also logged. */
void ConsoleHost_PushLine(const char* line, int shLogLevel);
/* 1 while the panel is fully open: the pointer is drawn then (the OS cursor is hidden). */
int  ConsoleHost_PointerVisible(void);

/* Runs a line as if typed, without touching the prompt (binds, -consolecmd files, the replay harness). Like a
 * typed line, the line ("> line") and what it prints are also logged as console-category info lines. */
void Pc_ConsoleExec(const char* line);
int  Pc_ConsoleIsOpen(void);

extern int g_PcConsoleInputActive;
extern int g_PcConsoleSwallowInput;

#ifdef __cplusplus
}
#endif
