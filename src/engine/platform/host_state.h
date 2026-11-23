/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef SH_ENGINE_PLATFORM_HOST_STATE_H
#define SH_ENGINE_PLATFORM_HOST_STATE_H

/* State and entry points the host modules (window, frame, events, input) share among themselves and with the
 * render and pad modules they drive. */

#include <SDL3/SDL.h>

#include "engine/platform/host_api.h"
#include "engine/platform/window.h"

/* The game window; null before start and after shutdown. */
extern SDL_Window* g_window;

/* Input module: the stock keyboard and pad bindings. */
void sh_host_default_input();

namespace sh::platform {

/* A close on purpose still ends in a benign exception escaping from static teardown; once marked, the terminate
 * handler exits quietly instead of reporting a crash. */
void mark_normal_exit();
void install_terminate_handler();

/* The diagnostic keys and the debug mouse hook (host_events.cpp). */
void debug_key(int key, bool down);
void debug_mouse_motion(int x, int y);

}  // namespace sh::platform

#endif
