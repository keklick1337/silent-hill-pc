// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)

#include "engine/entities/bind_pose.h"

namespace sh::entities {

namespace {

constexpr int kBoneCountOffset = 0x06;
constexpr int kScaleOffset = 0x12;
constexpr int kBoneTableOffset = 0x14;
constexpr int kBoneEntrySize = 6;  // s8 parent, rotation slot, translation slot, bind x, y, z

}  // namespace

void bind_pose_init(const void* anm_header, BoneCoord* bones) {
    BoneCoord& root = bones[0];
    root = BoneCoord{};
    for (int i = 0; i < 3; i++) {
        root.local.m[i][i] = 4096;
    }
    root.world_current = 1;

    if (anm_header == nullptr) {
        return;
    }
    const auto* file = static_cast<const std::uint8_t*>(anm_header);
    const int bone_count = file[kBoneCountOffset];
    const int scale = file[kScaleOffset];
    for (int b = 1; b < bone_count; b++) {
        const auto* entry = reinterpret_cast<const std::int8_t*>(file + kBoneTableOffset + b * kBoneEntrySize);
        BoneCoord& bone = bones[b];
        bone.parent = &bones[entry[0]];
        if (entry[2] < 0) {
            for (int i = 0; i < 3; i++) {
                // A left shift of the signed byte, as the file's scale is applied.
                bone.local.t[i] = static_cast<std::int32_t>(static_cast<std::uint32_t>(entry[3 + i]) << scale);
            }
        }
        if (entry[1] < 0) {
            for (int r = 0; r < 3; r++) {
                for (int c = 0; c < 3; c++) {
                    bone.local.m[r][c] = static_cast<std::int16_t>(r == c ? 4096 : 0);
                }
            }
        }
    }
}

}  // namespace sh::entities
