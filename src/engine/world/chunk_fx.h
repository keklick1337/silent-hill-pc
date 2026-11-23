// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// The decisions of the blood spray and the hit spark (src/engine/effects/combat_effects.cpp):
// animation clocks, screen tests, quad half-sizes, the spray's UV cell masks and
// fog levels, and ordering-table buckets. Plain integers.
//
// The engine's guards (garbage-size skip, bucket bounds clamp, zero depth) are
// part of the behaviour and live here too; where the original relied on 16-bit
// truncation the types spell it.

#pragma once

#include <cstdint>

namespace sh::world {

// ---- Gfx_MapChunkCheckA: the blood/particle spray cell -------------------------

// A slot's animation clock, advanced by the frame's Q12 delta. The "extra
// blood colour" option (config 14) runs the clock at 4x, a slot whose kind
// (byte B >> 2) is 33 at 2x. The clock wraps at 16 bits; 5233 kills the slot.
std::uint16_t chunk_fx_clock_step(std::uint16_t clock, std::int32_t dt_q12, bool extra_blood, bool kind33);
inline constexpr std::uint16_t kChunkFxClockKill = 5233;

// Screen test of the spray: depth past its z-bias, within ordering-table reach
// and inside the engine's screen box (|sx| <= 200, |sy| <= 160).
bool chunk_fx_a_visible(std::int32_t sx, std::int32_t sy, std::int32_t sz, std::int32_t z_bias,
                        int ot_size);

// Half size of the spray quad:
//   ((size_byte * 3) + (clock * 2) / 327 + (angle < 3 ? size_byte : 0)) * h / sz.
// The engine's garbage guard: |size| > 1000 skips the prim outright (the PC H
// register made such values), |size| > 80 clamps — both sides, the original
// could go huge negative.
struct ChunkFxHalfSize {
    bool skip;            // do not emit the prim at all
    std::int32_t half;    // the (possibly clamped) half size
};
ChunkFxHalfSize chunk_fx_a_half_size(std::int32_t sz, std::int32_t h, std::int32_t size_byte,
                                     std::int32_t clock, bool angle_below3);

// The eight 0/0x1F texture corner masks of the spray's 2x2 UV cell, by the
// rotation (angle >> 3): one mask per UV component, vertices 0..3.
struct ChunkFxMasks {
    std::uint8_t v0u, v0v, v1u, v1v, v2u, v2v, v3u, v3v;
};
ChunkFxMasks chunk_fx_a_masks(std::int32_t angle);

// The spray's UV cell: column (angle & 7) + 2, row clock/327 clamped into
// 0..7 (the low-bounds-first clamp of the original).
std::int32_t chunk_fx_a_uv_column(std::int32_t angle);
std::int32_t chunk_fx_a_uv_row(std::int32_t clock);

// Gray level of the spray's fog path: 255 - clock*16/327, floored at 0.
std::int32_t chunk_fx_a_fog_gray(std::int32_t clock);

// ---- Gfx_MapChunkCheckF: the spark/glow quad ------------------------------------

// Screen box only (the original has no z test here; the zero-depth guard below
// is what keeps the divide honest).
bool chunk_fx_f_visible(std::int32_t sx, std::int32_t sy);

// Half size: ((scale * h) / sz) >> 4. sz == 0 (on/behind the camera plane)
// skips the quad — the PSX divided garbage there and x86 would fault.
struct ChunkFxSparkSize {
    bool skip;
    std::int32_t half;
};
ChunkFxSparkSize chunk_fx_f_half_size(std::int32_t scale, std::int32_t h, std::int32_t sz);

// ---- shared ---------------------------------------------------------------------

// Ordering-table bucket by depth, clamped into the table. The original sprays
// had no clamp on one path and wrote prim data into the table itself on
// out-of-range depths; the clamp is the engine's fix and stays.
std::int32_t chunk_fx_bucket(std::int32_t depth, int ot_size);

}  // namespace sh::world
