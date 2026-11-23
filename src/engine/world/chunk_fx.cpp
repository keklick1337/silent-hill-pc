// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)

#include "engine/world/chunk_fx.h"

namespace sh::world {

std::uint16_t chunk_fx_clock_step(std::uint16_t clock, std::int32_t dt_q12, bool extra_blood,
                                  bool kind33) {
    // The original stored the sum into a 16-bit field, so every path wraps.
    if (extra_blood) {
        return static_cast<std::uint16_t>(clock + static_cast<std::uint16_t>(dt_q12) * 4);
    }
    if (kind33) {
        return static_cast<std::uint16_t>(clock + static_cast<std::uint16_t>(dt_q12) * 2);
    }
    return static_cast<std::uint16_t>(clock + dt_q12);
}

bool chunk_fx_a_visible(std::int32_t sx, std::int32_t sy, std::int32_t sz, std::int32_t z_bias,
                        int ot_size) {
    const std::int32_t depth = sz - z_bias;
    if (depth <= 0 || (depth >> 3) >= ot_size) {
        return false;
    }
    const std::int32_t abs_sx = sx < 0 ? -sx : sx;
    const std::int32_t abs_sy = sy < 0 ? -sy : sy;
    return abs_sx <= 200 && abs_sy <= 160;
}

ChunkFxHalfSize chunk_fx_a_half_size(std::int32_t sz, std::int32_t h, std::int32_t size_byte,
                                     std::int32_t clock, bool angle_below3) {
    const std::int32_t numerator = (size_byte * 3) + (clock * 2) / 327 + (angle_below3 ? size_byte : 0);
    const std::int32_t raw = numerator * h / sz;

    ChunkFxHalfSize out;
    const std::int32_t abs_raw = raw < 0 ? -raw : raw;
    if (abs_raw > 1000) {
        out.skip = true;  // garbage, not a legit close-camera splat
        out.half = raw;
        return out;
    }
    out.skip = false;
    if (abs_raw > 80) {
        out.half = raw < 0 ? -80 : 80;
    } else {
        out.half = raw;
    }
    return out;
}

namespace {

// The original's three-argument selection, kept in its own spelling: the value
// is 0 or 0x1F depending on whether a 0/1 mask equals bit 2 of the rotation.
inline std::uint8_t mask_if(bool equals_bit2, std::uint8_t when_equal, std::uint8_t when_other) {
    return equals_bit2 ? when_equal : when_other;
}

}  // namespace

ChunkFxMasks chunk_fx_a_masks(std::int32_t angle) {
    const int a = angle >> 3;
    const bool bit2 = (a & 4) != 0;
    const int low0 = (a ^ 1) & 1;              // negated bit 0
    const int low1 = (static_cast<unsigned>(a) >> 1 ^ 1) & 1;  // negated bit 1
    const int both = low0 ^ low1;

    ChunkFxMasks m;
    // Each component follows the original's `(!(a&4) && t==0) || ((a&4) && t!=0)`
    // form, i.e. "t equals bit 2 picks the first value".
    m.v0u = mask_if(both == (bit2 ? 1 : 0), 0x00, 0x1F);
    m.v0v = a >= 4 ? 0x1F : 0x00;
    m.v1u = mask_if(low1 == (bit2 ? 1 : 0), 0x1F, 0x00);
    m.v1v = mask_if(low0 == (bit2 ? 1 : 0), 0x00, 0x1F);
    m.v2u = mask_if(low1 == (bit2 ? 1 : 0), 0x00, 0x1F);
    m.v2v = mask_if(low0 == (bit2 ? 1 : 0), 0x1F, 0x00);
    m.v3u = a < 4 ? 0x1F : 0x00;
    m.v3v = mask_if(both == (bit2 ? 1 : 0), 0x1F, 0x00);
    return m;
}

std::int32_t chunk_fx_a_uv_column(std::int32_t angle) {
    return (angle & 0x7) + 2;
}

std::int32_t chunk_fx_a_uv_row(std::int32_t clock) {
    const std::int32_t row = clock / 327;
    const std::int32_t floored = row < 0 ? 0 : row;
    return floored < 7 ? floored : 7;
}

std::int32_t chunk_fx_a_fog_gray(std::int32_t clock) {
    const std::int32_t gray = 0xFF - (clock * 0x10) / 327;
    return gray < 0 ? 0 : gray;
}

bool chunk_fx_f_visible(std::int32_t sx, std::int32_t sy) {
    const std::int32_t abs_sx = sx < 0 ? -sx : sx;
    const std::int32_t abs_sy = sy < 0 ? -sy : sy;
    return abs_sx <= 200 && abs_sy <= 160;
}

ChunkFxSparkSize chunk_fx_f_half_size(std::int32_t scale, std::int32_t h, std::int32_t sz) {
    ChunkFxSparkSize out;
    if (sz == 0) {
        out.skip = true;  // SZ saturates to 0 on/behind the camera plane
        out.half = 0;
        return out;
    }
    out.skip = false;
    out.half = ((scale * h) / sz) >> 4;
    return out;
}

std::int32_t chunk_fx_bucket(std::int32_t depth, int ot_size) {
    std::int32_t bucket = depth >> 3;
    if (bucket < 0) bucket = 0;
    if (bucket >= ot_size) bucket = ot_size - 1;
    return bucket;
}

}  // namespace sh::world
