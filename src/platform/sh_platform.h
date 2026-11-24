/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef SH_PLATFORM_H
#define SH_PLATFORM_H

/* The few SDL3 services the game's code needs behind plain C types, without the SDL3 headers. */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

uint64_t Sh_PerfCounter(void);
uint64_t Sh_PerfFrequency(void);

/* SDL2-shaped mouse queries: SDL3 reports float coordinates. The position is in window
 * coordinates truncated to whole units; the relative motion since the last call is whole units
 * too, with the fraction carried into the next call so slow sub-pixel motion is not lost.
 * Both return the SDL button mask. Either pointer may be NULL. */
uint32_t Sh_MouseState(int* x, int* y);
uint32_t Sh_RelativeMouseState(int* dx, int* dy);

/* A pointer driven by the touch screen (window coordinates, SDL button bits), for the screens that follow the mouse:
 * Sh_MouseState reports its place, and its buttons beside the real mouse's, from the first call until the real mouse
 * moves (Sh_ReleaseTouchPointer). */
void Sh_SetTouchPointer(int x, int y, uint32_t buttons);
void Sh_ReleaseTouchPointer(void);

/* 1 on a phone or tablet build (Android): touch comes first, a keyboard and a mouse are not to be counted on, and the
 * window is always the whole screen. Code that adapts controls or layout asks here instead of testing the platform. */
int Sh_IsMobile(void);

#ifdef __cplusplus
}
#endif

#endif
