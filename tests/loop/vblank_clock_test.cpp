// The vertical blank clock (engine/loop/vblank_clock): virtual time, the SDK's VSync modes and fast forward.

#include "framework/sh_test.h"
#include "engine/loop/vblank_clock.h"

namespace {

int g_interrupts = 0;
int g_listened = 0;
int g_rate_heard = 0;

void interrupt() { g_interrupts++; }
void listen(int rate) {
    g_listened++;
    g_rate_heard = rate;
}

}  // namespace

SH_TEST(vblank_clock_virtual_time) {
    sh::loop::VBlankClock clock;
    clock.set_mode(sh::loop::VBlankClock::Mode::Virtual);
    SH_CHECK(clock.start());  // nothing runs by itself
    g_interrupts = g_listened = 0;
    SH_CHECK(clock.set_callback(&interrupt) == nullptr);
    clock.set_virtual_listener(&listen);

    SH_CHECK(clock.vsync(-1, false) == 0);  // counting does not advance
    SH_CHECK(clock.vsync(0, false) == 1);   // a wait raises one blank
    SH_CHECK(clock.vsync(2, false) == 1);   // waiting for several is not used by the game
    SH_CHECK(g_interrupts == 1 && g_listened == 1 && g_rate_heard == 60);

    for (int i = 0; i < 63; i++) clock.busy();
    SH_CHECK(clock.count() == 1);
    clock.busy();  // the 64th poll without a blank raises one
    SH_CHECK(clock.count() == 2 && g_interrupts == 2);

    clock.set_rate(50);
    for (int i = 0; i < 48; i++) clock.virtual_blank();
    SH_CHECK(clock.count() == 50);
    SH_CHECK(clock.virtual_ms() == 1000);
    SH_CHECK(g_rate_heard == 50);
    clock.stop();
}

SH_TEST(vblank_clock_fast_forward_counts) {
    sh::loop::VBlankClock clock;
    clock.set_mode(sh::loop::VBlankClock::Mode::RealTime);  // not started: no thread advances it
    g_interrupts = 0;
    clock.set_callback(&interrupt);
    SH_CHECK(clock.vsync(-1, true) == 1);  // each count adds a blank, without the interrupt
    SH_CHECK(clock.vsync(0, true) == 2);   // and a wait returns at once
    SH_CHECK(g_interrupts == 0);
}

SH_TEST(vblank_clock_real_time_thread) {
    sh::loop::VBlankClock clock;
    clock.set_mode(sh::loop::VBlankClock::Mode::RealTime);
    g_interrupts = 0;
    clock.set_callback(&interrupt);
    SH_CHECK(clock.start());
    clock.wait_blanks(2);
    clock.stop();
    SH_CHECK(clock.count() >= 2 && g_interrupts >= 2);
}
