// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)

#include "framework/sh_test.h"

#include "engine/entities/bind_pose.h"

#include <cstring>

using namespace sh::entities;

SH_TEST(bind_pose_links_bones_and_fills_static_parts) {
    std::uint8_t file[0x14 + 3 * 6] = {};
    file[6] = 3;     // bones
    file[0x12] = 2;  // scale
    // Bone 1: parent 0, animated rotation, static translation (1, -2, 3).
    const std::int8_t bone1[6] = {0, 0, -1, 1, -2, 3};
    // Bone 2: parent 1, static rotation, animated translation.
    const std::int8_t bone2[6] = {1, -1, 0, 9, 9, 9};
    std::memcpy(file + 0x14 + 6, bone1, 6);
    std::memcpy(file + 0x14 + 12, bone2, 6);

    BoneCoord bones[3];
    std::memset(bones, 0x55, sizeof bones);
    bind_pose_init(file, bones);

    SH_CHECK(bones[0].world_current == 1 && bones[0].parent == nullptr && bones[0].local.m[1][1] == 4096 &&
             bones[0].local.t[0] == 0);
    SH_CHECK(bones[1].parent == &bones[0] && bones[2].parent == &bones[1]);
    SH_CHECK(bones[1].local.t[0] == 4 && bones[1].local.t[1] == -8 && bones[1].local.t[2] == 12);
    SH_CHECK(bones[1].local.m[0][0] == 0x5555);  // animated: left alone
    SH_CHECK(bones[2].local.m[0][0] == 4096 && bones[2].local.m[0][1] == 0);
    SH_CHECK(bones[2].local.t[0] == 0x55555555);
}

SH_TEST(bind_pose_without_file_sets_only_the_root) {
    BoneCoord bones[2];
    std::memset(bones, 0x55, sizeof bones);
    bind_pose_init(nullptr, bones);
    SH_CHECK(bones[0].world_current == 1 && bones[0].local.m[2][2] == 4096);
    SH_CHECK(bones[1].world_current == 0x55555555u);
}
