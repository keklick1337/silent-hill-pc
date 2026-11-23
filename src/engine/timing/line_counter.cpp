// SPDX-License-Identifier: GPL-3.0-or-later
#include "engine/timing/line_counter.h"

#include <cstdint>

#include "engine/timing/vblank.h"
#include "platform/sh_platform.h"

extern "C" int g_PcFastForward;  // sticky quick-options toggle

namespace sh::timing {

namespace {

constexpr int kHBlanksPerVBlank = 263;
constexpr int kFastForwardScale = 4;

struct State {
    int base = 0;       // blank count when the counter was last cleared
    bool stale = true;  // base must be read again before use
    bool active = false;
    std::uint64_t epoch = 0;
    std::uint64_t last = 0;
    long long ticks = 0;
};

State g_state;

}  // namespace

void start_line_counter() {
    const std::uint64_t t0 = Sh_PerfCounter();
    g_state.epoch = t0;
    g_state.last = t0;
    g_state.ticks = 0;
    g_state.base = vblank_count();
    g_state.stale = false;
    g_state.active = true;
}

long long wall_clock_q12() {
    if (!g_state.active)
        return 0;

    const std::uint64_t now = Sh_PerfCounter();
    const std::uint64_t freq = Sh_PerfFrequency();

    // Accumulate scaled tick deltas so the scale can change mid-session without re-scaling the whole history;
    // one divide per read keeps sub-tick precision.
    if (g_state.last == 0)
        g_state.last = g_state.epoch;
    g_state.ticks += static_cast<long long>(now - g_state.last) *
                     ((g_skipSwapInterval || g_PcFastForward) ? kFastForwardScale : 1);
    g_state.last = now;

    return (g_state.ticks * 4096ll) / static_cast<long long>(freq);
}

int line_count() {
    // While fast-forward skips the vsync wait a count read would advance the count behind the loop's back, so no
    // read is made then.
    if (!g_state.active || g_skipSwapInterval)
        return 0;

    const int now = vblank_count();
    if (g_state.stale) {
        g_state.base = now;
        g_state.stale = false;
        return 0;
    }
    return (now - g_state.base) * kHBlanksPerVBlank;
}

void clear_line_count() {
    if (g_skipSwapInterval) {
        g_state.stale = true;
        return;
    }
    // Fresh read: the demo branch of the main loop clears without reading first.
    g_state.base = vblank_count();
    g_state.stale = false;
}

}  // namespace sh::timing
