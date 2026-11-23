/* The shared cutscene clock (engine/maps/cutscene_timer.h): window start clamp, Q12 rate step
 * with a floor shift, hold at the end only on request, end reached only past (not at) the end. */
#include "engine/maps/cutscene_timer.h"
#include "framework/sh_test.h"

namespace {

constexpr int kQ12 = 4096;

}  // namespace

SH_TEST(cutscene_clock_before_window_jumps_to_start) {
    const auto s = sh::maps::cutscene_clock_step(0, kQ12 / 30, 10 * kQ12, 17 * kQ12, 31 * kQ12, true);
    SH_CHECK(s.value == 17 * kQ12);
    SH_CHECK(!s.reached_end);
}

SH_TEST(cutscene_clock_runs_at_rate) {
    // 1/30 s (136 in Q12) at 10 frames/s: (136 * 40960) >> 12 = 1360.
    const auto s = sh::maps::cutscene_clock_step(17 * kQ12, 136, 10 * kQ12, 17 * kQ12, 31 * kQ12, true);
    SH_CHECK(s.value == 17 * kQ12 + 1360);
    SH_CHECK(!s.reached_end);
}

SH_TEST(cutscene_clock_end_is_inclusive) {
    const auto at = sh::maps::cutscene_clock_step(31 * kQ12, 0, 10 * kQ12, 0, 31 * kQ12, true);
    SH_CHECK(at.value == 31 * kQ12);
    SH_CHECK(!at.reached_end);
}

SH_TEST(cutscene_clock_past_end_holds_only_on_request) {
    const auto held = sh::maps::cutscene_clock_step(31 * kQ12, 136, 10 * kQ12, 0, 31 * kQ12, true);
    SH_CHECK(held.value == 31 * kQ12);
    SH_CHECK(held.reached_end);
    const auto free_run = sh::maps::cutscene_clock_step(31 * kQ12, 136, 10 * kQ12, 0, 31 * kQ12, false);
    SH_CHECK(free_run.value == 31 * kQ12 + 1360);
    SH_CHECK(free_run.reached_end);
}

SH_TEST(cutscene_clock_negative_product_floors) {
    // -1 * 1 >> 12 is -1 (arithmetic shift), not 0.
    const auto s = sh::maps::cutscene_clock_step(100, -1, 1, 0, 1000, false);
    SH_CHECK(s.value == 99);
}
