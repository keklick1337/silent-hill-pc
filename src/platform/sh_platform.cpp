/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "platform/sh_platform.h"

#include <SDL3/SDL.h>

extern SDL_Window* g_window; /* src/engine/platform/host_window.cpp */

extern "C" uint64_t Sh_PerfCounter(void)
{
    return SDL_GetPerformanceCounter();
}

extern "C" uint64_t Sh_PerfFrequency(void)
{
    return SDL_GetPerformanceFrequency();
}

namespace {
bool s_touchPointer = false;
int s_touchX = 0, s_touchY = 0;
uint32_t s_touchButtons = 0;
}  // namespace

extern "C" void Sh_SetTouchPointer(int x, int y, uint32_t buttons)
{
    s_touchPointer = true;
    s_touchX = x;
    s_touchY = y;
    s_touchButtons = buttons;
}

extern "C" void Sh_ReleaseTouchPointer(void)
{
    s_touchPointer = false;
    s_touchButtons = 0;
}

extern "C" uint32_t Sh_MouseState(int* x, int* y)
{
    float fx = 0.0f, fy = 0.0f;
    SDL_MouseButtonFlags buttons = SDL_GetMouseState(&fx, &fy);
    if (s_touchPointer)
    {
        fx = static_cast<float>(s_touchX);
        fy = static_cast<float>(s_touchY);
        buttons |= s_touchButtons;
    }
    if (x)
        *x = static_cast<int>(fx);
    if (y)
        *y = static_cast<int>(fy);
    return buttons;
}

extern "C" uint32_t Sh_RelativeMouseState(int* dx, int* dy)
{
    static float s_carryX = 0.0f, s_carryY = 0.0f;
    float fx = 0.0f, fy = 0.0f;
    const SDL_MouseButtonFlags buttons = SDL_GetRelativeMouseState(&fx, &fy);
    if (!dx && !dy)
    {
        /* A flush (both NULL) drops the carry with the accumulated motion. */
        s_carryX = s_carryY = 0.0f;
        return buttons;
    }
    fx += s_carryX;
    fy += s_carryY;
    const int ix = static_cast<int>(fx);
    const int iy = static_cast<int>(fy);
    s_carryX = fx - static_cast<float>(ix);
    s_carryY = fy - static_cast<float>(iy);
    if (dx)
        *dx = ix;
    if (dy)
        *dy = iy;
    return buttons;
}

extern "C" int Sh_IsMobile(void)
{
#ifdef __ANDROID__
    return 1;
#else
    return 0;
#endif
}

