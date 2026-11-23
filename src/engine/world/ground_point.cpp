// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)

#include "engine/world/ground_point.h"

namespace sh::world {

namespace {

constexpr std::int32_t kNoGroundDepth = 8 * 4096;    // 8 m below the origin
constexpr std::int32_t kDropGroundDepth = 4 * 4096;  // the drop maps' open ground
constexpr int kNoGround = 0;
constexpr int kOpenGround = 12;
constexpr int kDropKind = 7;

}  // namespace

void GroundPointCache::fill(std::int32_t x, std::int32_t z) {
    if (kind_ != -1 && x_ == x && z_ == z) {
        return;
    }
    std::int32_t height = 0;
    int kind = 0;
    if (source_ != nullptr) {
        source_->ground(x, z, &height, &kind);
    }
    x_ = x;
    z_ = z;
    const VoidDrop drop = source_ != nullptr ? source_->void_drop() : VoidDrop{};
    switch (kind) {
        case kNoGround:
            height = kNoGroundDepth;
            if (drop.no_ground && (!drop.no_ground_bounded || z <= drop.no_ground_max_z)) {
                height = kDropGroundDepth;
                kind = kDropKind;
            }
            break;
        case kOpenGround:
            height = kNoGroundDepth;
            if (drop.open_ground) {
                height = kDropGroundDepth;
                kind = kDropKind;
            }
            break;
        default:
            break;
    }
    height_ = height;
    kind_ = kind;
}

std::int32_t GroundPointCache::height(std::int32_t x, std::int32_t z) {
    fill(x, z);
    return height_;
}

int GroundPointCache::kind(std::int32_t x, std::int32_t z) {
    fill(x, z);
    return kind_;
}

GroundPointCache& ground_point_cache() {
    static GroundPointCache cache;
    return cache;
}

}  // namespace sh::world
