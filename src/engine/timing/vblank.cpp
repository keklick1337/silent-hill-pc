// SPDX-License-Identifier: GPL-3.0-or-later
#include "engine/timing/vblank.h"

#include <SDL3/SDL_timer.h>

#include "engine/log/sh_log.h"
#include "engine/audio/output/device/platform_sinks.h"
#include "engine/loop/vblank_clock.h"

int g_skipSwapInterval = 0;

namespace sh::timing {

namespace {

bool fast_forward() { return g_skipSwapInterval != 0; }

}  // namespace

int blank_rate() { return kBlankRate; }

bool start_vblank() {
    loop::VBlankClock& clock = loop::vblank_clock();
    clock.set_rate(blank_rate());
    clock.set_virtual_listener(sh::audio::render_virtual_vblank);
    if (!clock.start()) {
        SH_LOG_ERROR(GAME, "the vertical blank thread did not start");
        return false;
    }
    return true;
}

void stop_vblank() { loop::vblank_clock().stop(); }

int vblank_count() { return loop::vblank_clock().counted(fast_forward()); }

int wait_vblank() { return loop::vblank_clock().vsync(0, fast_forward()); }

int wait_vblanks(int count) { return count == 0 ? wait_vblank() : vblank_count(); }

void set_vblank_callback(void (*callback)()) { loop::vblank_clock().set_callback(callback); }

namespace virtual_time {

bool active() { return loop::vblank_clock().virtual_time(); }

bool paced() { return loop::vblank_clock().mode() == loop::VBlankClock::Mode::VirtualPaced; }

std::uint32_t ticks_ms() {
    return active() ? loop::vblank_clock().virtual_ms() : static_cast<std::uint32_t>(SDL_GetTicks());
}

void step_blank() { loop::vblank_clock().virtual_blank(); }

void device_busy() { loop::vblank_clock().busy(); }

}  // namespace virtual_time

}  // namespace sh::timing
