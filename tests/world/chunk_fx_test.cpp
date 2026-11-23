// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)

#include "framework/sh_test.h"

#include "engine/world/chunk_fx.h"

using namespace sh::world;

SH_TEST(chunk_fx_clock_rates_and_wrap) {
    // Plain rate: the Q12 delta adds, wrapping at 16 bits.
    SH_CHECK(chunk_fx_clock_step(1000, 136, false, false) == 1136);
    SH_CHECK(chunk_fx_clock_step(65500, 100, false, false) == 64);   // wraps at 16 bits (65600 - 65536)
    // Kind 33 runs at 2x (the dt is cast to 16 bits first).
    SH_CHECK(chunk_fx_clock_step(1000, 136, false, true) == 1272);
    SH_CHECK(chunk_fx_clock_step(1000, 70000, false, true) ==
             static_cast<std::uint16_t>(1000 + static_cast<std::uint16_t>(70000) * 2));
    // Extra blood colour (config 14) outranks kind 33 and runs at 4x.
    SH_CHECK(chunk_fx_clock_step(1000, 136, true, true) == 1544);
    SH_CHECK(chunk_fx_clock_step(1000, -136, true, false) == 456);
    // The kill point.
    SH_CHECK(kChunkFxClockKill == 5233);
}

SH_TEST(chunk_fx_a_visible) {
    // Depth past the bias, OT reach, screen box.
    SH_CHECK(chunk_fx_a_visible(0, 0, 1024, 0, 2048));
    SH_CHECK(!chunk_fx_a_visible(0, 0, 0, 0, 2048));          // at the bias
    SH_CHECK(!chunk_fx_a_visible(0, 0, 8, 40, 2048));          // depth - bias <= 0
    SH_CHECK(chunk_fx_a_visible(0, 0, 40 + 8, 40, 2048));      // one OT step in
    SH_CHECK(!chunk_fx_a_visible(0, 0, 2048 * 8 + 40, 40, 2048));  // past OT reach
    SH_CHECK(chunk_fx_a_visible(200, 160, 1024, 0, 2048));     // on the box edge
    SH_CHECK(!chunk_fx_a_visible(201, 0, 1024, 0, 2048));
    SH_CHECK(!chunk_fx_a_visible(0, -161, 1024, 0, 2048));
    SH_CHECK(chunk_fx_a_visible(-200, -160, 1024, 0, 2048));
}

SH_TEST(chunk_fx_a_half_size_and_guards) {
    // (size*3 + clock*2/327 + size) * h / sz with the angle bonus.
    const ChunkFxHalfSize plain = chunk_fx_a_half_size(4096, 4096, 10, 0, false);
    SH_CHECK(!plain.skip);
    SH_CHECK(plain.half == 10 * 3 * 4096 / 4096);
    const ChunkFxHalfSize bonus = chunk_fx_a_half_size(4096, 4096, 10, 0, true);
    SH_CHECK(bonus.half == 10 * 4 * 4096 / 4096);
    const ChunkFxHalfSize clock = chunk_fx_a_half_size(4096, 4096, 0, 327 * 2, false);
    SH_CHECK(clock.half == 4);  // (0*3 + 654*2/327 + 0) * H / sz = 4

    // |size| in (80, 1000] clamps to +-80.
    SH_CHECK(chunk_fx_a_half_size(4096, 4096, 200, 0, false).half == 80);
    // |size| > 1000 skips the prim; the raw value is still reported.
    const ChunkFxHalfSize garbage = chunk_fx_a_half_size(4096, 4096, 2000, 0, false);
    SH_CHECK(garbage.skip);
    SH_CHECK(garbage.half == 2000 * 3 * 4096 / 4096);
}

SH_TEST(chunk_fx_a_masks_and_uv_cell) {
    // The eight masks are a function of the rotation alone; check the four
    // corner rotations and one odd one.
    for (int angle = 0; angle < 256; angle += 1) {
        const ChunkFxMasks m = chunk_fx_a_masks(angle);
        for (std::uint8_t v : {m.v0u, m.v0v, m.v1u, m.v1v, m.v2u, m.v2v, m.v3u, m.v3v}) {
            SH_CHECK(v == 0x00 || v == 0x1F);
        }
    }
    // angle 0..7 -> rotation 0: the first cell row/column pattern.
    const ChunkFxMasks r0 = chunk_fx_a_masks(0);
    SH_CHECK(r0.v0u == 0x00 && r0.v0v == 0x00);
    SH_CHECK(r0.v3u == 0x1F && r0.v3v == 0x1F);
    // Rotation 4+ flips the v0v/v3u pair.
    const ChunkFxMasks r4 = chunk_fx_a_masks(4 << 3);
    SH_CHECK(r4.v0v == 0x1F && r4.v3u == 0x00);

    // UV cell: column (angle & 7) + 2, row clock/327 clamped to 0..7.
    SH_CHECK(chunk_fx_a_uv_column(0) == 2);
    SH_CHECK(chunk_fx_a_uv_column(255) == 9);
    SH_CHECK(chunk_fx_a_uv_row(0) == 0);
    SH_CHECK(chunk_fx_a_uv_row(327) == 1);
    SH_CHECK(chunk_fx_a_uv_row(100000) == 7);
}

SH_TEST(chunk_fx_fog_gray_bucket_spark) {
    SH_CHECK(chunk_fx_a_fog_gray(0) == 255);
    SH_CHECK(chunk_fx_a_fog_gray(327 * 8) == 127);   // 255 - 16*8
    SH_CHECK(chunk_fx_a_fog_gray(327 * 32) == 0);    // clamped at 0

    SH_CHECK(chunk_fx_bucket(8, 2048) == 1);
    SH_CHECK(chunk_fx_bucket(-1000, 2048) == 0);
    SH_CHECK(chunk_fx_bucket(2048 * 8 * 4, 2048) == 2047);

    SH_CHECK(chunk_fx_f_visible(200, -160));
    SH_CHECK(!chunk_fx_f_visible(-201, 0));
    const ChunkFxSparkSize dead = chunk_fx_f_half_size(10, 4096, 0);
    SH_CHECK(dead.skip);
    const ChunkFxSparkSize live = chunk_fx_f_half_size(16, 4096, 4096);
    SH_CHECK(!live.skip);
    SH_CHECK(live.half == 16 >> 4);
}
