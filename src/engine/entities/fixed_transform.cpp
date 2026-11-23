// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)

#include "engine/entities/fixed_transform.h"

#include <algorithm>

namespace sh::entities {

namespace {

// Sum of three 16 x 16 products (each fits 32 bits) in 64 bits, shifted right by 12, cut to 32 bits.
std::int32_t dot_shift(std::int64_t base, std::int32_t a0, std::int32_t b0, std::int32_t a1, std::int32_t b1,
                       std::int32_t a2, std::int32_t b2) {
    const std::int64_t sum = base + std::int64_t{a0 * b0} + std::int64_t{a1 * b1} + std::int64_t{a2 * b2};
    return static_cast<std::int32_t>(static_cast<std::uint32_t>(static_cast<std::uint64_t>(sum >> 12)));
}

std::int16_t saturate16(std::int32_t v) {
    return static_cast<std::int16_t>(std::clamp<std::int32_t>(v, -32768, 32767));
}

std::int32_t low16(std::int32_t v) { return static_cast<std::int16_t>(static_cast<std::uint16_t>(v & 0xFFFF)); }

}  // namespace

BoneTransform compose(const BoneTransform& parent, const BoneTransform& local) {
    BoneTransform out;
    for (int column = 0; column < 3; ++column) {
        const std::int32_t v0 = local.at(0, column);
        const std::int32_t v1 = local.at(1, column);
        const std::int32_t v2 = local.at(2, column);
        for (int row = 0; row < 3; ++row) {
            out.rotation[static_cast<std::size_t>(row * 3 + column)] = saturate16(
                dot_shift(0, parent.at(row, 0), v0, parent.at(row, 1), v1, parent.at(row, 2), v2));
        }
    }
    out.translation = transform_point(parent, local.translation);
    return out;
}

std::array<std::int32_t, 3> transform_point(const BoneTransform& transform, const std::array<std::int32_t, 3>& point) {
    const std::int32_t v0 = low16(point[0]);
    const std::int32_t v1 = low16(point[1]);
    const std::int32_t v2 = low16(point[2]);
    std::array<std::int32_t, 3> out{};
    for (int row = 0; row < 3; ++row) {
        const std::int64_t base = std::int64_t{transform.translation[static_cast<std::size_t>(row)]} * 4096;
        out[static_cast<std::size_t>(row)] =
            dot_shift(base, transform.at(row, 0), v0, transform.at(row, 1), v1, transform.at(row, 2), v2);
    }
    return out;
}

}  // namespace sh::entities
