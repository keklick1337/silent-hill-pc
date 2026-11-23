// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)

#include "engine/world/bone_order.h"

namespace sh::world {

std::int8_t BoneModelStream::next() {
    if (cur_[0] != kBoneStreamMulti) {
        run_ = cur_[0];
        ++cur_;
    } else if (++run_ >= static_cast<std::int8_t>(cur_[1] - 1)) {
        ++cur_;
    }
    return run_;
}

std::size_t bone_link_order(const std::uint8_t* model_order, std::size_t model_count,
                            const std::int32_t* bone_model_idx, std::size_t bone_count,
                            std::uint32_t* out) {
    std::size_t n = 0;
    for (std::size_t m = 0; m < model_count; ++m) {
        for (std::size_t b = 0; b < bone_count; ++b) {
            if (bone_model_idx[b] == static_cast<std::int32_t>(model_order[m])) {
                out[n++] = static_cast<std::uint32_t>(b);
            }
        }
    }
    return n;
}

}  // namespace sh::world
