#include "framework/sh_test.h"

#include "engine/input/input_primitives.h"

using namespace sh::input;

SH_TEST(input_shift_register_masks) {
    SH_CHECK(shift_mask(2) == 0x3u);
    SH_CHECK(shift_mask(5) == 0x1Fu);
    SH_CHECK(shift_mask(6) == 0x3Fu);
    SH_CHECK(shift_mask(1) == 0x1u);
    SH_CHECK(shift_mask(16) == 0xFFFFu);

    // Free functions: aging drops the oldest bit and clears bit 0; pushing only ORs.
    SH_CHECK(shift_age(0x3u, 2) == 0x2u);
    SH_CHECK(shift_age(0x2u, 2) == 0x0u);
    SH_CHECK(shift_age(0x1Fu, 5) == 0x1Eu);
    SH_CHECK(shift_push(0x0u, true) == 0x1u);
    SH_CHECK(shift_push(0x2u, false) == 0x2u);
    SH_CHECK(shift_push(0x2u, true) == 0x3u);
}

SH_TEST(input_shift_register_class) {
    ShiftRegister r2(2);
    SH_CHECK(r2.width() == 2u);
    SH_CHECK(r2.value() == 0u);

    // Two held ticks in a row: push, age, push (pushing twice only ORs bit 0).
    r2.push(true);   // 0x1
    SH_CHECK(r2.value() == 0x1u);
    r2.age();        // 0x2
    SH_CHECK(r2.value() == 0x2u);
    r2.push(true);   // 0x3
    SH_CHECK(r2.value() == 0x3u);
    r2.age();        // 0x2
    SH_CHECK(r2.value() == 0x2u);
    r2.age();        // 0x0
    SH_CHECK(r2.value() == 0x0u);

    // Push without aging keeps the aged bit and ORs the new one in.
    r2.set(0x1u);
    r2.push(true);
    SH_CHECK(r2.value() == 0x1u);
    r2.push(false);
    SH_CHECK(r2.value() == 0x1u);

    // set() masks to the register's width.
    ShiftRegister r5(5);
    r5.set(0xFFFFFFFFu);
    SH_CHECK(r5.value() == 0x1Fu);
    r5.set(0x15u);
    SH_CHECK(r5.value() == 0x15u);

    ShiftRegister r6(6, 0x3F);
    SH_CHECK(r6.value() == 0x3Fu);
    r6.age();
    SH_CHECK(r6.value() == 0x3Eu);
}

SH_TEST(input_tick30_gate_values) {
    SH_CHECK(kTick30Q12 == 136);

    // Exactly one tick per call at a 30 Hz frame rate.
    std::int32_t acc = 0;
    for (int i = 0; i < 5; ++i)
    {
        SH_CHECK(tick30_ready(acc, 136));
        SH_CHECK(acc == 0);
    }

    // 60 fps: a tick every second call.
    acc = 0;
    for (int i = 0; i < 4; ++i)
    {
        SH_CHECK(!tick30_ready(acc, 68));
        SH_CHECK(tick30_ready(acc, 68));
        SH_CHECK(acc == 0);
    }

    // 240 fps: a tick exactly every 8th call (17 * 8 = 136).
    acc = 0;
    for (int call = 1; call <= 8; ++call)
    {
        const bool ready = tick30_ready(acc, 17);
        SH_CHECK(ready == (call == 8));
    }
    SH_CHECK(acc == 0);

    // The last unit crossing the boundary leaves the accumulator empty.
    acc = 135;
    SH_CHECK(tick30_ready(acc, 1));
    SH_CHECK(acc == 0);

    // A single huge frame is one tick and the surplus is discarded.
    acc = 0;
    SH_CHECK(tick30_ready(acc, 1000));
    SH_CHECK(acc == 0);

    // 408 = 3 * 136: one tick, the remainder 272 > 136 is discarded.
    acc = 0;
    SH_CHECK(tick30_ready(acc, 408));
    SH_CHECK(acc == 0);

    // 272 = 2 * 136: one tick, the remainder of exactly 136 is KEPT...
    acc = 0;
    SH_CHECK(tick30_ready(acc, 272));
    SH_CHECK(acc == 136);
    // ...so an empty next frame fires the tick again.
    SH_CHECK(tick30_ready(acc, 0));
    SH_CHECK(acc == 0);

    // No delta, nothing pending: not ready.
    SH_CHECK(!tick30_ready(acc, 0));
    SH_CHECK(acc == 0);
}

SH_TEST(input_tick30_gate_class) {
    Tick30Gate gate;
    SH_CHECK(gate.accumulator() == 0);
    SH_CHECK(!gate.ready(68));
    SH_CHECK(gate.accumulator() == 68);
    SH_CHECK(gate.ready(68));
    SH_CHECK(gate.accumulator() == 0);
    gate.reset();
    SH_CHECK(gate.accumulator() == 0);
    SH_CHECK(!gate.ready(0));
}

SH_TEST(input_stick_deadzone) {
    SH_CHECK(kStickThreshold == 64);

    // Negative side: v < -64.
    SH_CHECK(!axis_negative(-64));
    SH_CHECK(axis_negative(-65));
    SH_CHECK(axis_negative(-128));
    // Positive side: v >= 64.
    SH_CHECK(!axis_positive(63));
    SH_CHECK(axis_positive(64));
    SH_CHECK(axis_positive(127));

    // Every boundary excess from the spec.
    SH_CHECK(axis_negative_excess(-128) == 64);
    SH_CHECK(axis_negative_excess(-65) == 1);
    SH_CHECK(axis_negative_excess(-64) == 0);
    SH_CHECK(axis_negative_excess(0) == 0);
    SH_CHECK(axis_negative_excess(127) == 0);
    SH_CHECK(axis_positive_excess(127) == 64);
    SH_CHECK(axis_positive_excess(64) == 1);
    SH_CHECK(axis_positive_excess(63) == 0);
    SH_CHECK(axis_positive_excess(0) == 0);
    SH_CHECK(axis_positive_excess(-128) == 0);

    // The whole signed-byte range.
    for (int v = -128; v <= 127; ++v)
    {
        SH_CHECK(axis_negative_excess(v) == (v < -64 ? -v - 64 : 0));
        SH_CHECK(axis_positive_excess(v) == (v >= 64 ? v - 63 : 0));
    }

    // A stick is outside the zone when any of its axes is.
    SH_CHECK(!stick_outside_deadzone(0, 0));
    SH_CHECK(!stick_outside_deadzone(63, -64));
    SH_CHECK(stick_outside_deadzone(64, 0));
    SH_CHECK(stick_outside_deadzone(0, -65));
    SH_CHECK(stick_outside_deadzone(-65, 0));
    SH_CHECK(stick_outside_deadzone(0, 64));
    SH_CHECK(!stick_outside_deadzone(63, 63));
}
